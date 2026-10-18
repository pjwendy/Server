/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#include "zone/headless/headless_client_manager.h"

#include "common/eqemu_logsys.h"
#include "common/servertalk.h"
#include "common/strings.h"
#include "zone/client.h"
#include "zone/corpse.h"
#include "zone/entity.h"
#include "zone/headless/null_stream.h"
#include "zone/worldserver.h"
#include "zone/zone.h"

#include "headless.pb.h"

#include <cmath>
#include <unordered_set>

extern EntityList  entity_list;
extern WorldServer worldserver;
extern Zone        *zone;

namespace {
	constexpr uint64 PUBLISH_INTERVAL_MS = 250;
	// smaller position changes aren't worth a delta
	constexpr float  MOVE_EPSILON        = 0.25f;

	eqheadless::SpawnType SpawnTypeOf(Mob *mob)
	{
		if (mob->IsCorpse()) {
			return mob->CastToCorpse()->IsPlayerCorpse() ? eqheadless::SPAWN_TYPE_PLAYER_CORPSE : eqheadless::SPAWN_TYPE_NPC_CORPSE;
		}
		if (mob->IsBot()) {
			return eqheadless::SPAWN_TYPE_BOT;
		}
		if (mob->IsClient()) {
			return eqheadless::SPAWN_TYPE_PLAYER;
		}
		if (mob->IsNPC()) {
			return eqheadless::SPAWN_TYPE_NPC;
		}
		return eqheadless::SPAWN_TYPE_UNKNOWN;
	}

	std::string SessionID(const char *buf, size_t size)
	{
		return std::string(buf, strnlen(buf, size));
	}
}

bool HeadlessClientManager::IsHeadless(Mob *mob)
{
	return mob && mob->IsClient() && dynamic_cast<NullStream *>(mob->CastToClient()->Connection()) != nullptr;
}

void HeadlessClientManager::HandleSpawn(ServerPacket *pack)
{
	if (pack->size != sizeof(ServerHeadlessSpawn_Struct)) {
		LogError("Wrong size on ServerOP_HeadlessSpawn: got [{}] expected [{}]", pack->size, sizeof(ServerHeadlessSpawn_Struct));
		return;
	}

	auto *s = reinterpret_cast<ServerHeadlessSpawn_Struct *>(pack->pBuffer);
	const std::string session_id = SessionID(s->session_id, sizeof(s->session_id));
	const std::string character(s->character, strnlen(s->character, sizeof(s->character)));

	if (m_sessions.count(session_id) || entity_list.GetClientByName(character.c_str())) {
		SendSpawned(session_id, 0, fmt::format("[{}] is already in this zone", character));
		return;
	}

	// world sent ServerOP_ZoneIncClient first, so Handle_Connect_OP_ZoneEntry's auth check passes
	auto *stream = new NullStream(session_id, character);
	auto *client = new Client(stream);
	stream->SetClient(client);
	m_sessions[session_id].stream = stream;
	entity_list.AddClient(client);

	LogHeadlessClient("Spawning headless character [{}] for session [{}]", character, session_id);
}

void HeadlessClientManager::HandleDespawn(ServerPacket *pack)
{
	if (pack->size != sizeof(ServerHeadlessSession_Struct)) {
		LogError("Wrong size on ServerOP_HeadlessDespawn: got [{}] expected [{}]", pack->size, sizeof(ServerHeadlessSession_Struct));
		return;
	}

	auto *s = reinterpret_cast<ServerHeadlessSession_Struct *>(pack->pBuffer);
	auto it = m_sessions.find(SessionID(s->session_id, sizeof(s->session_id)));
	if (it == m_sessions.end()) {
		return;
	}

	// the stream now reports not-established, so Client::Process() runs the normal
	// linkdead/logout path, which saves the character and removes it from the zone
	LogHeadlessClient("Session [{}] ended; logging its character out", it->first);
	it->second.publish_timer.reset();
	it->second.stream->Close();
}

void HeadlessClientManager::ReportSpawned(const std::string &session_id, uint32 spawn_id)
{
	LogInfo("Headless character for session [{}] is in the zone as spawn [{}]", session_id, spawn_id);
	SendSpawned(session_id, spawn_id, "");

	// world sends Welcome when it sees ServerOP_HeadlessSpawned; frames arrive after it in order
	Publish(session_id, true);

	auto it = m_sessions.find(session_id);
	if (it != m_sessions.end()) {
		it->second.publish_timer = std::make_unique<EQ::Timer>(
			PUBLISH_INTERVAL_MS, true, [this, session_id](EQ::Timer *) { Publish(session_id, false); }
		);
	}
}

void HeadlessClientManager::OnStreamDestroyed(const std::string &session_id, bool was_spawned)
{
	m_sessions.erase(session_id); // stops the publish timer

	if (!was_spawned) {
		SendSpawned(session_id, 0, "character failed to enter the zone");
		return;
	}

	auto pack = new ServerPacket(ServerOP_HeadlessGone, sizeof(ServerHeadlessSession_Struct));
	auto *s   = reinterpret_cast<ServerHeadlessSession_Struct *>(pack->pBuffer);
	strn0cpy(s->session_id, session_id.c_str(), sizeof(s->session_id));
	worldserver.SendPacket(pack);
	safe_delete(pack);
}

void HeadlessClientManager::FillSpawn(eqheadless::Spawn *out, Mob *mob)
{
	out->set_id(mob->GetID());
	out->set_name(mob->GetCleanName());
	out->set_type(SpawnTypeOf(mob));
	out->set_level(mob->GetLevel());
	out->set_class_id(mob->GetClass());
	out->set_race_id(mob->GetRace());
	out->set_x(mob->GetX());
	out->set_y(mob->GetY());
	out->set_z(mob->GetZ());
	out->set_heading(mob->GetHeading());
	out->set_hp_pct(static_cast<uint32>(std::lround(std::max(0.0f, mob->GetHPRatio()))));
	out->set_owner_id(mob->GetOwnerID());
	out->set_headless(IsHeadless(mob));
}

void HeadlessClientManager::Publish(const std::string &session_id, bool snapshot)
{
	auto it = m_sessions.find(session_id);
	if (it == m_sessions.end() || !it->second.stream) {
		return;
	}

	Session &session = it->second;
	Client  *me      = session.stream->GetClient();
	if (!me || !me->Connected()) {
		return;
	}

	eqheadless::ServerFrame frame;
	eqheadless::Self       *self = snapshot ? frame.mutable_snapshot()->mutable_me() : frame.mutable_delta()->mutable_me();

	self->set_spawn_id(me->GetID());
	self->set_name(me->GetCleanName());
	self->set_level(me->GetLevel());
	self->set_class_id(me->GetClass());
	self->set_race_id(me->GetRace());
	self->set_hp(me->GetHP());
	self->set_max_hp(me->GetMaxHP());
	self->set_mana(me->GetMana());
	self->set_max_mana(me->GetMaxMana());
	self->set_endurance(me->GetEndurance());
	self->set_max_endurance(me->GetMaxEndurance());
	self->set_x(me->GetX());
	self->set_y(me->GetY());
	self->set_z(me->GetZ());
	self->set_heading(me->GetHeading());
	self->set_target_id(me->GetTarget() ? me->GetTarget()->GetID() : 0);
	self->set_sitting(me->IsSitting());
	self->set_auto_attack(me->AutoAttackEnabled());
	self->set_casting_spell_id(me->IsCasting() ? me->CastingSpellID() : 0);

	if (snapshot) {
		auto *z = frame.mutable_snapshot()->mutable_zone();
		z->set_id(zone->GetZoneID());
		z->set_instance_id(zone->GetInstanceID());
		z->set_short_name(zone->GetShortName());
		z->set_long_name(zone->GetLongName());
		session.seen.clear();
	}

	// spawns: everything in the zone except ourselves
	std::unordered_set<uint32> present;
	bool changed = snapshot;

	for (auto &[id, mob] : entity_list.GetMobList()) {
		if (!mob || mob == me) {
			continue;
		}

		present.insert(id);

		const uint32 hp_pct = static_cast<uint32>(std::lround(std::max(0.0f, mob->GetHPRatio())));
		auto seen = session.seen.find(id);
		const bool is_new = seen == session.seen.end();
		if (!is_new) {
			const SeenSpawn &s = seen->second;
			if (std::fabs(s.x - mob->GetX()) < MOVE_EPSILON && std::fabs(s.y - mob->GetY()) < MOVE_EPSILON &&
				std::fabs(s.z - mob->GetZ()) < MOVE_EPSILON && s.heading == mob->GetHeading() &&
				s.hp_pct == hp_pct && s.level == mob->GetLevel() && s.name == mob->GetCleanName()) {
				continue;
			}
		}

		FillSpawn(snapshot ? frame.mutable_snapshot()->add_spawns() : frame.mutable_delta()->add_spawns_changed(), mob);
		session.seen[id] = SeenSpawn{mob->GetX(), mob->GetY(), mob->GetZ(), mob->GetHeading(), hp_pct, mob->GetLevel(), mob->GetCleanName()};
		changed = true;
	}

	if (!snapshot) {
		for (auto s = session.seen.begin(); s != session.seen.end();) {
			if (!present.count(s->first)) {
				frame.mutable_delta()->add_spawns_removed(s->first);
				s = session.seen.erase(s);
				changed = true;
			}
			else {
				++s;
			}
		}
	}

	// Self changes on its own (regen, target), but an unchanged frame isn't worth sending
	std::string self_bytes;
	self->SerializeToString(&self_bytes);
	if (!changed && session.last_self == self_bytes) {
		return;
	}
	session.last_self = std::move(self_bytes);

	SendFrame(session_id, frame);
}

void HeadlessClientManager::SendFrame(const std::string &session_id, const eqheadless::ServerFrame &frame)
{
	std::string body;
	if (!frame.SerializeToString(&body)) {
		LogError("Failed to serialise headless frame for session [{}]", session_id);
		return;
	}

	auto pack = new ServerPacket(ServerOP_HeadlessFrame, sizeof(ServerHeadlessFrame_Struct) + body.size());
	auto *f   = reinterpret_cast<ServerHeadlessFrame_Struct *>(pack->pBuffer);
	strn0cpy(f->session_id, session_id.c_str(), sizeof(f->session_id));
	f->length = static_cast<uint32>(body.size());
	memcpy(f->data, body.data(), body.size());
	worldserver.SendPacket(pack);
	safe_delete(pack);
}

void HeadlessClientManager::SendSpawned(const std::string &session_id, uint32 spawn_id, const std::string &error)
{
	auto pack = new ServerPacket(ServerOP_HeadlessSpawned, sizeof(ServerHeadlessSpawned_Struct));
	auto *s   = reinterpret_cast<ServerHeadlessSpawned_Struct *>(pack->pBuffer);
	strn0cpy(s->session_id, session_id.c_str(), sizeof(s->session_id));
	s->spawn_id = spawn_id;
	strn0cpy(s->error, error.c_str(), sizeof(s->error));
	worldserver.SendPacket(pack);
	safe_delete(pack);
}
