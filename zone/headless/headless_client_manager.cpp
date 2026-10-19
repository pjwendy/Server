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

#include "common/eq_constants.h"
#include "common/eq_packet_structs.h"
#include "common/eqemu_logsys.h"
#include "common/rulesys.h"
#include "common/servertalk.h"
#include "common/spdat.h"
#include "common/classes.h"
#include "common/races.h"
#include "common/item_data.h"
#include "common/strings.h"
#include "common/timer.h"
#include "common/zone_store.h"
#include "zone/bot.h"
#include "zone/client.h"
#include "zone/corpse.h"
#include "zone/entity.h"
#include "zone/groups.h"
#include "zone/headless/null_stream.h"
#include "zone/worldserver.h"
#include "zone/zone.h"
#include "zone/zonedb.h"
#include "zone/water_map.h"

#include "headless.pb.h"

#include <cmath>
#include <unordered_set>

extern EntityList  entity_list;
extern WorldServer worldserver;
extern Zone        *zone;

namespace {
	constexpr uint64 PUBLISH_INTERVAL_MS = 250;
	constexpr uint64 MOVEMENT_HISTORY_MS = 1000;
	// smaller position changes aren't worth a delta
	constexpr float  MOVE_EPSILON        = 0.25f;
	// the client takes a few seconds to memorise a spell (an approximation; it varies a little)
	constexpr uint32 MEMORIZE_MS         = 3000;

	uint64 NowMs()
	{
		return Timer::GetCurrentTime();
	}

	std::string SpellName(uint32 spell_id)
	{
		return IsValidSpell(spell_id) ? spells[spell_id].name : "";
	}

	EQApplicationPacket *StandUp(Client *me)
	{
		auto app = new EQApplicationPacket(OP_SpawnAppearance, sizeof(SpawnAppearance_Struct));
		auto *sa = reinterpret_cast<SpawnAppearance_Struct *>(app->pBuffer);
		sa->spawn_id  = me->GetID();
		sa->type      = AppearanceType::Animation;
		sa->parameter = Animation::Standing;
		return app;
	}

	EQApplicationPacket *SitDown(Client *me)
	{
		auto app = StandUp(me);
		reinterpret_cast<SpawnAppearance_Struct *>(app->pBuffer)->parameter = Animation::Sitting;
		return app;
	}

	EQApplicationPacket *Uint32Packet(EmuOpcode op, uint32 value)
	{
		auto app = new EQApplicationPacket(op, sizeof(uint32));
		memcpy(app->pBuffer, &value, sizeof(uint32));
		return app;
	}

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

	bool ZoneInList(const std::string &list, const std::string &short_name)
	{
		for (auto &z : Strings::Split(list, ',')) {
			if (Strings::EqualFold(Strings::Trim(z), short_name)) {
				return true;
			}
		}
		return false;
	}

	// HeadlessClients:BlockedZones / AllowedZones, as world checks at login
	std::string ZoneRefusal(const std::string &short_name)
	{
		if (ZoneInList(RuleS(HeadlessClients, BlockedZones), short_name)) {
			return fmt::format("zone [{}] is blocked for headless characters", short_name);
		}
		std::string allowed = RuleS(HeadlessClients, AllowedZones);
		if (!Strings::Trim(allowed).empty() && !ZoneInList(allowed, short_name)) {
			return fmt::format("zone [{}] is not in HeadlessClients:AllowedZones", short_name);
		}
		return "";
	}

	std::string ShortName(uint32 zone_id)
	{
		const char *name = ZoneName(zone_id, true);
		return name ? name : "";
	}

	bool Wild(float v)
	{
		return v >= 999998.0f || v <= -999998.0f;
	}

	// zone points this client version can use
	std::vector<ZonePoint *> UsableZonePoints(Client *me)
	{
		std::vector<ZonePoint *> out;
		LinkedListIterator<ZonePoint *> it(zone->zone_point_list);
		for (it.Reset(); it.MoreElements(); it.Advance()) {
			ZonePoint *zp = it.GetData();
			if (zp && !zp->is_virtual && (zp->client_version_mask & me->ClientVersionBit()) && zp->target_zone_id != zone->GetZoneID()) {
				out.push_back(zp);
			}
		}
		return out;
	}

	// where to walk for a zone point: its position, with "anywhere" axes taken from ours
	glm::vec3 ZonePointSpot(const ZonePoint *zp, const glm::vec3 &from)
	{
		return glm::vec3(Wild(zp->x) ? from.x : zp->x, Wild(zp->y) ? from.y : zp->y, Wild(zp->z) ? from.z : zp->z);
	}

	// by entity name ("a_rat00") or, failing that, clean name ("a rat"), ignoring case
	Mob *FindMobByName(const std::string &name)
	{
		if (Mob *mob = entity_list.GetMob(name.c_str())) {
			return mob;
		}
		for (auto &[id, mob] : entity_list.GetMobList()) {
			if (mob && Strings::EqualFold(mob->GetCleanName(), name)) {
				return mob;
			}
		}
		return nullptr;
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

	// log out now rather than standing linkdead for Zone:ClientLinkdeadMS: a kicked client is
	// saved and removed on its next Process() (client_process.cpp, CLIENT_KICKED)
	LogHeadlessClient("Session [{}] ended; logging its character out", it->first);
	it->second.publish_timer.reset();
	if (auto *me = it->second.stream->GetClient()) {
		me->Kick("headless session ended");
	}
	else {
		it->second.stream->Close();
	}
}

void HeadlessClientManager::HandleCommand(ServerPacket *pack)
{
	if (pack->size < sizeof(ServerHeadlessFrame_Struct)) {
		return;
	}

	auto *f = reinterpret_cast<ServerHeadlessFrame_Struct *>(pack->pBuffer);
	if (sizeof(ServerHeadlessFrame_Struct) + f->length > pack->size) {
		return;
	}

	const std::string session_id = SessionID(f->session_id, sizeof(f->session_id));
	auto it = m_sessions.find(session_id);
	if (it == m_sessions.end() || !it->second.stream) {
		return;
	}

	eqheadless::Command cmd;
	if (!cmd.ParseFromArray(f->data, static_cast<int>(f->length))) {
		SendResult(session_id, 0, "malformed command");
		return;
	}

	Client *me = it->second.stream->GetClient();
	if (!me || !me->Connected()) {
		SendResult(session_id, cmd.id(), "character is not in the zone");
		return;
	}

	SendResult(session_id, cmd.id(), Execute(session_id, it->second, me, cmd));
}

std::string HeadlessClientManager::Execute(const std::string &session_id, Session &session, Client *me, const eqheadless::Command &cmd)
{
	NullStream *stream = session.stream;

	switch (cmd.action_case()) {
		case eqheadless::Command::kMoveTo: {
			if (!session.mover) {
				return "character is not ready to move";
			}
			session.zone_line_to = 0;
			const auto &m = cmd.move_to();
			if (!std::isfinite(m.x()) || !std::isfinite(m.y()) || !std::isfinite(m.z())) {
				return "destination must be finite";
			}
			return session.mover->Start(glm::vec3(m.x(), m.y(), m.z()));
		}
		case eqheadless::Command::kStop: {
			if (session.mover) {
				session.mover->Stop(HeadlessMover::Result::Stopped);
			}
			return "";
		}
		case eqheadless::Command::kTarget: {
			const uint32 id = cmd.target().spawn_id();
			if (id != 0 && !entity_list.GetMob(id)) {
				return fmt::format("no spawn with id {}", id);
			}

			// a real client sends OP_TargetCommand and, when the server echoes it back,
			// OP_TargetMouse (OnOutgoing does that half). Clearing goes straight to TargetMouse.
			auto app = new EQApplicationPacket(id ? OP_TargetCommand : OP_TargetMouse, sizeof(ClientTarget_Struct));
			reinterpret_cast<ClientTarget_Struct *>(app->pBuffer)->new_target = id;
			stream->Inject(app);
			return "";
		}
		case eqheadless::Command::kAttack: {
			auto app = new EQApplicationPacket(OP_AutoAttack, 4);
			app->pBuffer[0] = cmd.attack().on() ? 1 : 0;
			stream->Inject(app);
			return "";
		}
		case eqheadless::Command::kChat: {
			const auto &chat = cmd.chat();
			if (chat.text().empty()) {
				return "empty message";
			}
			if (chat.text().size() > 512) {
				return "message longer than 512 characters";
			}
			if (chat.channel() == eqheadless::CHAT_TELL && chat.to().empty()) {
				return "a tell needs a recipient";
			}
			if (chat.channel() == eqheadless::CHAT_SAY && chat.text()[0] == '^' && !RuleB(HeadlessClients, AllowBots)) {
				return "bot commands are turned off for headless characters (HeadlessClients:AllowBots)";
			}

			auto app = new EQApplicationPacket(OP_ChannelMessage, sizeof(ChannelMessage_Struct) + chat.text().size() + 1);
			auto *cm = reinterpret_cast<ChannelMessage_Struct *>(app->pBuffer);
			strn0cpy(cm->targetname, chat.to().c_str(), sizeof(cm->targetname));
			strn0cpy(cm->sender, me->GetName(), sizeof(cm->sender));
			cm->language          = Language::CommonTongue;
			cm->chan_num          = static_cast<uint32>(chat.channel());
			cm->skill_in_language = Language::MaxValue;
			memcpy(cm->message, chat.text().c_str(), chat.text().size() + 1);
			stream->Inject(app);
			return "";
		}
		case eqheadless::Command::kSit: {
			if (cmd.sit().sit() && session.mover) {
				session.mover->Stop(HeadlessMover::Result::Stopped); // sitting ends a walk
			}
			auto app = new EQApplicationPacket(OP_SpawnAppearance, sizeof(SpawnAppearance_Struct));
			auto *sa = reinterpret_cast<SpawnAppearance_Struct *>(app->pBuffer);
			sa->spawn_id  = me->GetID();
			sa->type      = AppearanceType::Animation;
			sa->parameter = cmd.sit().sit() ? Animation::Sitting : Animation::Standing;
			stream->Inject(app);
			return "";
		}
		case eqheadless::Command::kInvite: {
			const std::string &name = cmd.invite().name();
			Mob *invitee = name.empty() ? me->GetTarget() : FindMobByName(name);
			if (!invitee || invitee == me) {
				return name.empty() ? "no target to invite" : fmt::format("no one named {} here", name);
			}
			if (invitee->IsBot()) {
				if (!RuleB(HeadlessClients, AllowBots)) {
					return "bots are turned off for headless characters (HeadlessClients:AllowBots)";
				}
				if (invitee->CastToBot()->GetBotOwner() != me) {
					return fmt::format("{} is not your bot", invitee->GetCleanName());
				}
			}
			else if (invitee->IsClient()) {
				if (!IsHeadless(invitee) && !RuleB(HeadlessClients, AllowGroupWithPlayers)) {
					return "grouping with players is turned off for headless characters (HeadlessClients:AllowGroupWithPlayers)";
				}
			}
			else {
				return fmt::format("{} can't join a group", invitee->GetCleanName());
			}
			if (RuleB(Character, GroupInvitesRequireTarget) && me->GetTarget() != invitee) {
				return "target them first (Character:GroupInvitesRequireTarget)";
			}

			auto app = new EQApplicationPacket(OP_GroupInvite, sizeof(GroupInvite_Struct));
			auto *gi = reinterpret_cast<GroupInvite_Struct *>(app->pBuffer);
			strn0cpy(gi->invitee_name, invitee->GetName(), sizeof(gi->invitee_name));
			strn0cpy(gi->inviter_name, me->GetName(), sizeof(gi->inviter_name));
			stream->Inject(app);
			return "";
		}
		case eqheadless::Command::kFace: {
			if (!session.mover) {
				return "character is not ready to move";
			}
			const uint32 id = cmd.face().spawn_id();
			Mob *mob = id ? entity_list.GetMob(id) : me->GetTarget();
			if (!mob) {
				return id ? fmt::format("no spawn with id {}", id) : "no target to face";
			}
			return session.mover->Face(glm::vec3(mob->GetX(), mob->GetY(), mob->GetZ()));
		}
		case eqheadless::Command::kCast: {
			const uint32 gem = cmd.cast().gem();
			if (gem >= EQ::spells::SPELL_GEM_COUNT) {
				return fmt::format("gem must be 0-{}", EQ::spells::SPELL_GEM_COUNT - 1);
			}
			const uint32 spell_id = me->GetPP().mem_spells[gem];
			if (!IsValidSpell(spell_id)) {
				return fmt::format("gem {} is empty", gem + 1);
			}
			if (cmd.cast().spell_id() && cmd.cast().spell_id() != spell_id) {
				return fmt::format("gem {} holds {}, not spell {}", gem + 1, SpellName(spell_id), cmd.cast().spell_id());
			}
			if (me->IsCasting()) {
				return "already casting";
			}
			const uint64 now = NowMs();
			if (now < session.mem_until_ms) {
				return "still memorising";
			}
			if (now < session.gem_ready_ms[gem]) {
				return fmt::format("{} is not ready yet", SpellName(spell_id));
			}

			// a player stops to cast, and standing up is part of casting
			if (session.mover && session.mover->Moving()) {
				session.mover->Stop(HeadlessMover::Result::Stopped);
			}
			if (me->IsSitting()) {
				stream->Inject(StandUp(me));
			}

			Mob  *target = me->GetTarget();
			auto app    = new EQApplicationPacket(OP_CastSpell, sizeof(CastSpell_Struct));
			auto *cs    = reinterpret_cast<CastSpell_Struct *>(app->pBuffer);
			cs->slot          = gem;
			cs->spell_id      = spell_id;
			cs->inventoryslot = 0xFFFF;
			cs->target_id     = target ? target->GetID() : me->GetID();
			stream->Inject(app);

			// the client's gem refresh: every gem waits out the recovery time, this one its recast too
			const auto &sp = spells[spell_id];
			for (auto &ready : session.gem_ready_ms) {
				ready = std::max<uint64>(ready, now + sp.cast_time + sp.recovery_time);
			}
			session.gem_ready_ms[gem] = std::max<uint64>(session.gem_ready_ms[gem], now + sp.cast_time + std::max(sp.recovery_time, sp.recast_time));
			return "";
		}
		case eqheadless::Command::kStopCast: {
			if (!me->IsCasting()) {
				return "not casting";
			}
			stream->Inject(new EQApplicationPacket(OP_ManaChange, 0)); // the client's "stop casting"
			return "";
		}
		case eqheadless::Command::kMemorize: {
			const uint32 gem      = cmd.memorize().gem();
			const uint32 spell_id = cmd.memorize().spell_id();
			if (gem >= EQ::spells::SPELL_GEM_COUNT) {
				return fmt::format("gem must be 0-{}", EQ::spells::SPELL_GEM_COUNT - 1);
			}
			const uint64 now = NowMs();
			if (now < session.mem_until_ms) {
				return "already memorising";
			}
			if (me->IsCasting()) {
				return "can't memorise while casting";
			}

			auto make = [me, gem](uint32 id, uint32 how) {
				auto app = new EQApplicationPacket(OP_MemorizeSpell, sizeof(MemorizeSpell_Struct));
				auto *ms = reinterpret_cast<MemorizeSpell_Struct *>(app->pBuffer);
				ms->slot     = gem;
				ms->spell_id = id;
				ms->scribing = how;
				return app;
			};

			if (spell_id == 0) {
				const uint32 current = me->GetPP().mem_spells[gem];
				if (!IsValidSpell(current)) {
					return fmt::format("gem {} is already empty", gem + 1);
				}
				stream->Inject(make(current, memSpellForget));
				return "";
			}
			if (!IsValidSpell(spell_id) || !me->HasSpellScribed(spell_id)) {
				return fmt::format("spell {} is not in your spellbook", spell_id);
			}

			// the client sits to memorise and takes a few seconds over it
			if (session.mover && session.mover->Moving()) {
				session.mover->Stop(HeadlessMover::Result::Stopped);
			}
			if (!me->IsSitting()) {
				stream->Inject(SitDown(me));
			}
			session.mem_until_ms = now + MEMORIZE_MS;
			session.mem_timer    = std::make_unique<EQ::Timer>(
				MEMORIZE_MS, false, [this, session_id, make, spell_id](EQ::Timer *) {
					auto s = m_sessions.find(session_id);
					if (s != m_sessions.end() && s->second.stream && s->second.stream->GetClient()) {
						s->second.stream->Inject(make(spell_id, memSpellMemorize));
					}
				}
			);
			return "";
		}
		case eqheadless::Command::kConsider: {
			Mob *mob = entity_list.GetMob(cmd.consider().spawn_id());
			if (!mob) {
				return fmt::format("no spawn with id {}", cmd.consider().spawn_id());
			}
			auto app = new EQApplicationPacket(OP_Consider, sizeof(Consider_Struct));
			auto *c  = reinterpret_cast<Consider_Struct *>(app->pBuffer);
			c->playerid = me->GetID();
			c->targetid = mob->GetID();
			stream->Inject(app);
			return "";
		}
		case eqheadless::Command::kLoot: {
			const uint32 id = cmd.loot().corpse_id();
			Entity *e = entity_list.GetID(id);
			if (!e || !e->IsCorpse()) {
				return fmt::format("no corpse with id {}", id);
			}
			if (session.loot_corpse) {
				return "already looting";
			}
			// as the client: open the corpse, and once the server has listed its items (it echoes
			// the request back), loot them all and close it (OnOutgoing does that half)
			session.loot_corpse = id;
			std::fill(std::begin(session.loot_coin), std::end(session.loot_coin), 0);
			stream->Inject(Uint32Packet(OP_LootRequest, id));
			return "";
		}
		case eqheadless::Command::kZoneTo: {
			if (!session.mover) {
				return "character is not ready to move";
			}
			const std::string &name = cmd.zone_to().zone();
			const uint32 zone_id    = ZoneID(name);
			if (!zone_id) {
				return fmt::format("no zone called {}", name);
			}
			const std::string refusal = ZoneRefusal(ShortName(zone_id));
			if (!refusal.empty()) {
				return refusal;
			}

			const glm::vec3 here(me->GetX(), me->GetY(), me->GetZ());
			const ZonePoint *best = nullptr;
			float best_dist       = 0.0f;
			for (auto *zp : UsableZonePoints(me)) {
				if (zp->target_zone_id != zone_id) {
					continue;
				}
				const glm::vec3 spot = ZonePointSpot(zp, here);
				const float dist     = glm::length(glm::vec2(spot.x - here.x, spot.y - here.y));
				if (!best || dist < best_dist) {
					best      = zp;
					best_dist = dist;
				}
			}
			if (!best) {
				return fmt::format("{} has no zone line to {}", zone->GetShortName(), ShortName(zone_id));
			}

			session.zone_line_to = zone_id;
			const std::string error = session.mover->Start(ZonePointSpot(best, here), true);
			if (!error.empty()) {
				session.zone_line_to = 0;
			}
			return error;
		}
		case eqheadless::Command::kMerchant: {
			Mob *npc = entity_list.GetMob(cmd.merchant().npc_id());
			if (cmd.merchant().open()) {
				if (!npc || !npc->IsNPC() || npc->GetClass() != Class::Merchant) {
					return "that's not a merchant";
				}
				if (DistanceSquared(me->GetPosition(), npc->GetPosition()) > USE_NPC_RANGE2) {
					return "too far from the merchant";
				}
				auto app = new EQApplicationPacket(OP_ShopRequest, sizeof(MerchantClick_Struct));
				auto *mc = reinterpret_cast<MerchantClick_Struct *>(app->pBuffer);
				mc->npc_id    = npc->GetID();
				mc->player_id = me->GetID();
				mc->command   = MerchantActions::Open;
				mc->unknown020 = -1;
				stream->Inject(app);
			}
			else {
				auto app = new EQApplicationPacket(OP_ShopEnd, sizeof(MerchantClick_Struct));
				reinterpret_cast<MerchantClick_Struct *>(app->pBuffer)->npc_id = cmd.merchant().npc_id();
				stream->Inject(app);
			}
			return "";
		}
		case eqheadless::Command::kBuy: {
			const auto &b = cmd.buy();
			// the struct names are the server's point of view: a player buying sends Merchant_Sell_Struct
			auto app = new EQApplicationPacket(OP_ShopPlayerBuy, sizeof(Merchant_Sell_Struct));
			auto *ms = reinterpret_cast<Merchant_Sell_Struct *>(app->pBuffer);
			ms->npcid    = b.npc_id();
			ms->playerid = me->GetID();
			ms->itemslot = b.merchant_slot();
			ms->quantity = std::max<uint32>(1, b.quantity());
			stream->Inject(app);
			return "";
		}
		case eqheadless::Command::kSell: {
			const auto &sl = cmd.sell();
			const EQ::ItemInstance *inst = me->GetInv().GetItem(sl.slot());
			if (!inst || !inst->GetItem()) {
				return fmt::format("nothing in slot {}", sl.slot());
			}
			auto app = new EQApplicationPacket(OP_ShopPlayerSell, sizeof(Merchant_Purchase_Struct));
			auto *mp = reinterpret_cast<Merchant_Purchase_Struct *>(app->pBuffer);
			mp->npcid    = sl.npc_id();
			mp->itemslot = sl.slot();
			mp->quantity = sl.quantity() ? sl.quantity() : (inst->IsStackable() ? std::max<int16>(1, inst->GetCharges()) : 1);
			stream->Inject(app);
			return "";
		}
		case eqheadless::Command::kMoveItem: {
			const auto &mv = cmd.move_item();
			if (!me->GetInv().GetItem(mv.from_slot())) {
				return fmt::format("nothing in slot {}", mv.from_slot());
			}
			auto app = new EQApplicationPacket(OP_MoveItem, sizeof(MoveItem_Struct));
			auto *mi = reinterpret_cast<MoveItem_Struct *>(app->pBuffer);
			mi->from_slot       = mv.from_slot();
			mi->to_slot         = mv.to_slot();
			mi->number_in_stack = mv.quantity();
			stream->Inject(app);
			return "";
		}
		case eqheadless::Command::kDisband: {
			if (!me->GetGroup() && !me->GetRaid()) {
				return "not in a group";
			}
			// as the client's /disband: name1 is who to remove (the target, else ourselves)
			auto app = new EQApplicationPacket(OP_GroupDisband, sizeof(GroupGeneric_Struct));
			auto *gd = reinterpret_cast<GroupGeneric_Struct *>(app->pBuffer);
			strn0cpy(gd->name1, me->GetTarget() ? me->GetTarget()->GetName() : me->GetName(), sizeof(gd->name1));
			strn0cpy(gd->name2, me->GetName(), sizeof(gd->name2));
			stream->Inject(app);
			return "";
		}
		default:
			return "unknown command";
	}
}

void HeadlessClientManager::OnOutgoing(const std::string &session_id, const EQApplicationPacket *app)
{
	auto it = m_sessions.find(session_id);
	if (it == m_sessions.end() || !it->second.stream) {
		return;
	}

	eqheadless::ServerFrame frame;

	switch (app->GetOpcode()) {
		case OP_TargetCommand: {
			// the server approved our target: the client's half is to select it (OP_TargetMouse)
			if (app->size == sizeof(ClientTarget_Struct)) {
				auto mouse = new EQApplicationPacket(OP_TargetMouse, sizeof(ClientTarget_Struct));
				memcpy(mouse->pBuffer, app->pBuffer, sizeof(ClientTarget_Struct));
				it->second.stream->Inject(mouse);
			}
			return;
		}
		case OP_RequestClientZoneChange: {
			// the server wants us elsewhere (#zone, #goto, summon, port): the client's half is to
			// ask to zone with OP_ZoneChange or, within the zone, simply to be there
			if (app->size < sizeof(RequestClientZoneChange_Struct)) {
				return;
			}
			auto   *rq = reinterpret_cast<const RequestClientZoneChange_Struct *>(app->pBuffer);
			Client *me = it->second.stream->GetClient();
			if (!me) {
				return;
			}
			if (rq->zone_id == zone->GetZoneID() && rq->instance_id == zone->GetInstanceID()) {
				if (it->second.mover) {
					it->second.mover->Place(glm::vec3(rq->x, rq->y, rq->z), rq->heading);
				}
				return;
			}

			const std::string refusal = ZoneRefusal(ShortName(rq->zone_id));
			if (!refusal.empty()) {
				SendZoneEvent(session_id, eqheadless::ZONE_REFUSED, rq->zone_id, refusal);
				return;
			}
			if (it->second.mover) {
				it->second.mover->Stop(HeadlessMover::Result::Stopped);
			}

			auto zc_app = new EQApplicationPacket(OP_ZoneChange, sizeof(ZoneChange_Struct));
			auto *zc    = reinterpret_cast<ZoneChange_Struct *>(zc_app->pBuffer);
			strn0cpy(zc->char_name, me->GetName(), sizeof(zc->char_name));
			zc->zoneID      = rq->zone_id;
			zc->instanceID  = rq->instance_id;
			zc->x           = rq->x;
			zc->y           = rq->y;
			zc->z           = rq->z;
			zc->zone_reason = rq->type; // the client echoes `type` back
			zc->success     = 0;
			it->second.stream->Inject(zc_app);
			return;
		}
		case OP_ZonePlayerToBind: {
			// death (or a respawn choice): the client answers like any zone request, asking for the
			// bind zone (0 means this zone; the server works it out from its zone mode)
			if (app->size < sizeof(ZonePlayerToBind_Struct)) {
				return;
			}
			auto   *bind = reinterpret_cast<const ZonePlayerToBind_Struct *>(app->pBuffer);
			Client *me   = it->second.stream->GetClient();
			if (!me) {
				return;
			}
			if (bind->bind_zone_id && bind->bind_zone_id != zone->GetZoneID()) {
				const std::string refusal = ZoneRefusal(ShortName(bind->bind_zone_id));
				if (!refusal.empty()) {
					SendZoneEvent(session_id, eqheadless::ZONE_REFUSED, bind->bind_zone_id, refusal);
					return;
				}
			}
			if (it->second.mover) {
				it->second.mover->Stop(HeadlessMover::Result::Stopped);
			}

			auto zc_app = new EQApplicationPacket(OP_ZoneChange, sizeof(ZoneChange_Struct));
			auto *zc    = reinterpret_cast<ZoneChange_Struct *>(zc_app->pBuffer);
			strn0cpy(zc->char_name, me->GetName(), sizeof(zc->char_name));
			zc->zoneID     = bind->bind_zone_id;
			zc->instanceID = bind->bind_instance_id;
			zc->x          = bind->x;
			zc->y          = bind->y;
			zc->z          = bind->z;
			zc->success    = 0;
			it->second.stream->Inject(zc_app);
			return;
		}
		case OP_ZoneChange: {
			// the server's answer to our OP_ZoneChange. On success a real client disconnects and
			// re-enters through world; we close, and world spawns us in the new zone.
			if (app->size < sizeof(ZoneChange_Struct)) {
				return;
			}
			auto *zc = reinterpret_cast<const ZoneChange_Struct *>(app->pBuffer);
			if (zc->success != 1) {
				SendZoneEvent(session_id, eqheadless::ZONE_REFUSED, zc->zoneID, fmt::format("the server refused the zone change ({})", zc->success));
				return;
			}
			it->second.zoning      = true;
			it->second.zone_to     = zc->zoneID;
			it->second.instance_to = zc->instanceID;
			it->second.publish_timer.reset();
			SendZoneEvent(session_id, eqheadless::ZONE_LEAVING, zc->zoneID, "");
			it->second.stream->Close(); // Client::Process sees it with bZoning set: "left the zone", not linkdead
			return;
		}
		case OP_ShopRequest: {
			// the server opened (or refused) the merchant window: list what it sells
			if (app->size < sizeof(MerchantClick_Struct)) {
				return;
			}
			auto   *mc = reinterpret_cast<const MerchantClick_Struct *>(app->pBuffer);
			Client *me = it->second.stream->GetClient();
			Mob    *npc = entity_list.GetMob(mc->npc_id);
			auto   *ev = frame.mutable_event()->mutable_merchant();
			ev->set_npc_id(mc->npc_id);
			ev->set_open(mc->command == MerchantActions::Open);
			if (me && npc && npc->IsNPC() && mc->command == MerchantActions::Open) {
				const float mod = me->CalcPriceMod(npc, false);
				for (const auto &ml : zone->merchanttable[npc->CastToNPC()->MerchantType]) {
					const EQ::ItemData *item = database.GetItem(ml.item);
					if (!item || (ml.level_required && me->GetLevel() < ml.level_required)) {
						continue;
					}
					auto *out = ev->add_items();
					FillItem(out, item, me);
					out->set_slot(ml.slot);
					float price = item->Price * item->SellRate;
					if (!RuleB(Merchant, UseClassicPriceMod)) {
						price *= RuleR(Merchant, SellCostMod);
					}
					if (RuleB(Merchant, UsePriceMod)) {
						price *= mod;
					}
					out->set_price(static_cast<uint32>(std::max(0.0f, price)));
				}
			}
			break;
		}
		case OP_Consider: {
			if (app->size < sizeof(Consider_Struct)) {
				return;
			}
			auto *c   = reinterpret_cast<const Consider_Struct *>(app->pBuffer);
			auto *con = frame.mutable_event()->mutable_consider();
			con->set_spawn_id(c->targetid);
			con->set_color(c->level); // GetLevelCon(): a ConsiderColor
			con->set_faction(c->faction);
			con->set_hp(c->cur_hp);
			con->set_max_hp(c->max_hp);
			break;
		}
		case OP_MoneyOnCorpse: {
			Session &session = it->second;
			if (!session.loot_corpse || app->size < sizeof(moneyOnCorpseStruct)) {
				return;
			}
			auto *m = reinterpret_cast<const moneyOnCorpseStruct *>(app->pBuffer);
			switch (static_cast<LootResponse>(m->response)) {
				case LootResponse::Normal:
				case LootResponse::Normal2:
					session.loot_coin[0] = m->platinum;
					session.loot_coin[1] = m->gold;
					session.loot_coin[2] = m->silver;
					session.loot_coin[3] = m->copper;
					return; // the item list and the echoed request follow
				case LootResponse::LootAll:
					return;
				default: {
					static const char *reasons[] = {"someone else is looting it", "", "you can't loot it right now", "", "you can't loot with hostiles nearby", "you're too far away"};
					auto *loot = frame.mutable_event()->mutable_loot();
					loot->set_corpse_id(session.loot_corpse);
					loot->set_ok(false);
					loot->set_error(m->response < 6 ? reasons[m->response] : "loot refused");
					session.loot_corpse = 0;
					break;
				}
			}
			break;
		}
		case OP_LootRequest: {
			// the server's echo: the corpse's items now have loot slots
			Session &session = it->second;
			Client  *me      = session.stream->GetClient();
			if (!me || !session.loot_corpse || app->size != sizeof(uint32)) {
				return;
			}
			uint32 id;
			memcpy(&id, app->pBuffer, sizeof(id));
			if (id != session.loot_corpse) {
				return;
			}
			auto *loot = frame.mutable_event()->mutable_loot();
			loot->set_corpse_id(id);
			loot->set_ok(true);
			loot->set_platinum(session.loot_coin[0]);
			loot->set_gold(session.loot_coin[1]);
			loot->set_silver(session.loot_coin[2]);
			loot->set_copper(session.loot_coin[3]);

			Entity *e = entity_list.GetID(id);
			if (e && e->IsCorpse()) {
				for (const auto *item : e->CastToCorpse()->GetLootItems()) {
					if (!item || item->lootslot < EQ::invslot::CORPSE_BEGIN || item->lootslot > EQ::invslot::CORPSE_END) {
						continue;
					}
					auto li = new EQApplicationPacket(OP_LootItem, sizeof(LootingItem_Struct));
					auto *l = reinterpret_cast<LootingItem_Struct *>(li->pBuffer);
					l->lootee    = id;
					l->looter    = me->GetID();
					l->slot_id   = item->lootslot;
					l->auto_loot = 1;
					session.stream->Inject(li);
					const auto *data = database.GetItem(item->item_id);
					loot->add_items(data ? data->Name : fmt::format("item {}", item->item_id));
				}
			}
			session.stream->Inject(Uint32Packet(OP_EndLootRequest, id));
			session.loot_corpse = 0;
			break;
		}
		case OP_Death: {
			Client *me = it->second.stream->GetClient();
			if (!me || app->size < sizeof(Death_Struct)) {
				return;
			}
			auto *d = reinterpret_cast<const Death_Struct *>(app->pBuffer);
			if (d->spawn_id != me->GetID()) {
				return; // someone else died
			}
			auto *death = frame.mutable_event()->mutable_death();
			Mob  *killer = entity_list.GetMob(d->killer_id);
			death->set_killer(killer ? killer->GetCleanName() : "");
			death->set_corpse_id(d->corpseid);
			death->set_spell_id(IsValidSpell(d->spell_id) ? d->spell_id : 0);
			death->set_damage(d->damage);
			it->second.loot_corpse = 0;
			break;
		}
		case OP_RespawnWindow: {
			// Character:RespawnFromHover: the client picks where to respawn; we take our bind point
			if (app->size > sizeof(uint32)) {
				it->second.stream->Inject(Uint32Packet(OP_RespawnWindow, 0));
			}
			return;
		}
		case OP_ChannelMessage: {
			if (app->size < sizeof(ChannelMessage_Struct)) {
				return;
			}
			auto *cm   = reinterpret_cast<const ChannelMessage_Struct *>(app->pBuffer);
			auto *chat = frame.mutable_event()->mutable_chat();
			chat->set_channel(static_cast<eqheadless::ChatChannel>(cm->chan_num));
			chat->set_from(std::string(cm->sender, strnlen(cm->sender, sizeof(cm->sender))));
			chat->set_to(std::string(cm->targetname, strnlen(cm->targetname, sizeof(cm->targetname))));
			chat->set_text(std::string(cm->message, strnlen(cm->message, app->size - sizeof(ChannelMessage_Struct))));
			chat->set_language(cm->language);
			break;
		}
		case OP_SpecialMesg: {
			// speak mode, journal mode, language, type, target, sayer\0, 3 x int32, message\0
			const size_t header = 3 + 4 + 4;
			if (app->size <= header) {
				return;
			}
			const char *p   = reinterpret_cast<const char *>(app->pBuffer);
			const char *end = p + app->size;
			auto *msg = frame.mutable_event()->mutable_message();
			msg->set_speak_mode(static_cast<uint8>(p[0]));
			uint32 type;
			memcpy(&type, p + 3, sizeof(type));
			msg->set_type(type);
			const char *sayer = p + header;
			const size_t sayer_len = strnlen(sayer, end - sayer);
			msg->set_sayer(std::string(sayer, sayer_len));
			const char *text = sayer + sayer_len + 1 + 12;
			if (text < end) {
				msg->set_text(std::string(text, strnlen(text, end - text)));
			}
			break;
		}
		case OP_FormattedMessage: {
			if (app->size < sizeof(FormattedMessage_Struct)) {
				return;
			}
			auto *fm  = reinterpret_cast<const FormattedMessage_Struct *>(app->pBuffer);
			auto *msg = frame.mutable_event()->mutable_message();
			msg->set_string_id(fm->string_id);
			msg->set_type(fm->type);
			// arguments are \0-separated, ending with an extra \0
			const char *a   = fm->message;
			const char *end = reinterpret_cast<const char *>(app->pBuffer) + app->size;
			while (a < end && *a) {
				const size_t n = strnlen(a, end - a);
				msg->add_args(std::string(a, n));
				a += n + 1;
			}
			break;
		}
		case OP_SimpleMessage: {
			if (app->size < sizeof(SimpleMessage_Struct)) {
				return;
			}
			auto *sm  = reinterpret_cast<const SimpleMessage_Struct *>(app->pBuffer);
			auto *msg = frame.mutable_event()->mutable_message();
			msg->set_string_id(sm->string_id);
			msg->set_type(sm->color);
			break;
		}
		default:
			return;
	}

	SendFrame(session_id, frame);
}

void HeadlessClientManager::SendResult(const std::string &session_id, uint32 command_id, const std::string &error)
{
	eqheadless::ServerFrame frame;
	auto *r = frame.mutable_result();
	r->set_id(command_id);
	r->set_ok(error.empty());
	r->set_error(error);
	SendFrame(session_id, frame);
}

void HeadlessClientManager::ReportSpawned(const std::string &session_id, uint32 spawn_id)
{
	LogInfo("Headless character for session [{}] is in the zone as spawn [{}]", session_id, spawn_id);
	SendSpawned(session_id, spawn_id, "");

	// world sends Welcome when it sees ServerOP_HeadlessSpawned; frames arrive after it in order
	Publish(session_id, true);

	auto it = m_sessions.find(session_id);
	if (it == m_sessions.end()) {
		return;
	}

	Session &session = it->second;
	session.publish_timer = std::make_unique<EQ::Timer>(
		PUBLISH_INTERVAL_MS, true, [this, session_id](EQ::Timer *) { Publish(session_id, false); }
	);

	session.mover = std::make_unique<HeadlessMover>(
		session.stream, [this, session_id](HeadlessMover::Result result, const glm::vec3 &where) {
			static const eqheadless::MoveStatus statuses[] = {
				eqheadless::MOVE_ARRIVED, eqheadless::MOVE_STOPPED, eqheadless::MOVE_STUCK,
				eqheadless::MOVE_NO_PATH, eqheadless::MOVE_ZONE_LINE,
			};
			eqheadless::ServerFrame frame;
			auto *move = frame.mutable_event()->mutable_move();
			move->set_status(statuses[static_cast<int>(result)]);
			move->set_x(where.x);
			move->set_y(where.y);
			move->set_z(where.z);
			SendFrame(session_id, frame);

			// ZoneTo: we're at (or in) the zone line; ask to go through it, as the client does
			auto s = m_sessions.find(session_id);
			if (s != m_sessions.end() && s->second.zone_line_to) {
				if (result == HeadlessMover::Result::ZoneLine || result == HeadlessMover::Result::Arrived) {
					RequestZoneLine(session_id, s->second, where);
				}
				else {
					SendZoneEvent(session_id, eqheadless::ZONE_REFUSED, s->second.zone_line_to, "couldn't reach the zone line");
				}
				s->second.zone_line_to = 0;
			}
		}
	);

	// the real client reports its recent movement every second (OP_FloatListThing); without
	// it cheat detection flags a moving character as MQGhost after 70s
	session.history_timer = std::make_unique<EQ::Timer>(
		MOVEMENT_HISTORY_MS, true, [this, session_id](EQ::Timer *) {
			auto s = m_sessions.find(session_id);
			if (s != m_sessions.end()) {
				SendMovementHistory(s->second);
			}
		}
	);
}

void HeadlessClientManager::SendMovementHistory(Session &session)
{
	Client *me = session.stream ? session.stream->GetClient() : nullptr;
	if (!me || !me->Connected()) {
		return;
	}

	auto app = new EQApplicationPacket(OP_FloatListThing, sizeof(UpdateMovementEntry));
	auto *e  = reinterpret_cast<UpdateMovementEntry *>(app->pBuffer);
	e->Y         = me->GetY();
	e->X         = me->GetX();
	e->Z         = me->GetZ();
	e->type      = 0; // ordinary movement
	e->timestamp = Timer::GetCurrentTime();
	session.stream->Inject(app);
}

void HeadlessClientManager::OnStreamDestroyed(const std::string &session_id, bool was_spawned)
{
	auto it = m_sessions.find(session_id);
	const bool   zoning      = it != m_sessions.end() && it->second.zoning;
	const uint32 zone_to     = zoning ? it->second.zone_to : 0;
	const uint32 instance_to = zoning ? it->second.instance_to : 0;
	m_sessions.erase(session_id); // stops the publish timer

	if (was_spawned && zoning) {
		auto pack = new ServerPacket(ServerOP_HeadlessZoning, sizeof(ServerHeadlessZoning_Struct));
		auto *z   = reinterpret_cast<ServerHeadlessZoning_Struct *>(pack->pBuffer);
		strn0cpy(z->session_id, session_id.c_str(), sizeof(z->session_id));
		z->zone_id     = zone_to;
		z->instance_id = instance_to;
		worldserver.SendPacket(pack);
		safe_delete(pack);
		return;
	}

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
	if (mob->IsBot() && mob->CastToBot()->GetBotOwner()) {
		out->set_owner_id(mob->CastToBot()->GetBotOwner()->GetID());
	}
	else {
		out->set_owner_id(mob->GetOwnerID());
	}
	out->set_headless(IsHeadless(mob));
	out->set_target_id(mob->GetTarget() ? mob->GetTarget()->GetID() : 0);
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
	const uint64 now = NowMs();
	for (int i = 0; i < EQ::spells::SPELL_GEM_COUNT; ++i) {
		const uint32 id = me->GetPP().mem_spells[i];
		if (!IsValidSpell(id)) {
			continue;
		}
		auto *gem = self->add_gems();
		gem->set_slot(i);
		gem->set_spell_id(id);
		gem->set_name(SpellName(id));
		gem->set_ready(now >= session.gem_ready_ms[i] && now >= session.mem_until_ms && !me->IsCasting());
	}
	if (Buffs_Struct *buffs = me->GetBuffs()) {
		for (int i = 0; i < me->GetMaxTotalSlots(); ++i) {
			if (!IsValidSpell(buffs[i].spellid)) {
				continue;
			}
			auto *buff = self->add_buffs();
			buff->set_spell_id(buffs[i].spellid);
			buff->set_name(SpellName(buffs[i].spellid));
			buff->set_ticks(buffs[i].ticsremaining);
		}
	}
	for (auto &[id, mob] : entity_list.GetMobList()) {
		if (mob && mob->IsNPC() && !mob->IsCorpse() && mob->CastToNPC()->IsOnHatelist(me)) {
			self->add_haters(id);
		}
	}
	self->set_race_bit(GetPlayerRaceBit(me->GetBaseRace()));
	self->set_weight(me->CalcCurrentWeight() / 10);
	self->set_capacity(me->GetSTR());
	self->set_platinum(me->GetPP().platinum);
	self->set_gold(me->GetPP().gold);
	self->set_silver(me->GetPP().silver);
	self->set_copper(me->GetPP().copper);
	if (Group *group = me->GetGroup()) {
		for (int i = 0; i < MAX_GROUP_MEMBERS; ++i) {
			if (!group->membername[i][0]) {
				continue;
			}
			auto *gm   = self->add_group();
			Mob  *mob  = group->members[i];
			gm->set_name(group->membername[i]);
			gm->set_spawn_id(mob ? mob->GetID() : 0);
			gm->set_leader(group->IsLeader(group->membername[i]));
			gm->set_bot(mob && mob->IsBot());
		}
	}

	if (snapshot) {
		auto *z = frame.mutable_snapshot()->mutable_zone();
		z->set_id(zone->GetZoneID());
		z->set_instance_id(zone->GetInstanceID());
		z->set_short_name(zone->GetShortName());
		z->set_long_name(zone->GetLongName());
		std::unordered_set<uint32> listed;
		for (auto *zp : UsableZonePoints(me)) {
			if (!listed.insert(zp->target_zone_id).second) {
				continue; // one exit per zone; ZoneTo picks the nearest line
			}
			auto *exit = z->add_exits();
			exit->set_zone(ShortName(zp->target_zone_id));
			exit->set_zone_id(zp->target_zone_id);
			exit->set_x(zp->x);
			exit->set_y(zp->y);
			exit->set_z(zp->z);
		}
		session.seen.clear();
	}

	// spawns: everything in the zone except ourselves
	std::unordered_set<uint32> present;
	bool changed = snapshot;

	// spellbook and inventory change rarely: send them only when they do
	eqheadless::SpellBook book;
	for (int i = 0; i < EQ::spells::SPELLBOOK_SIZE; ++i) {
		const uint32 id = me->GetSpellIDByBookSlot(i);
		if (!IsValidSpell(id)) {
			continue;
		}
		auto *spell = book.add_spells();
		spell->set_spell_id(id);
		spell->set_name(SpellName(id));
		spell->set_level(IsPlayerClass(me->GetClass()) ? spells[id].classes[me->GetClass() - 1] : 0);
	}
	std::string book_bytes;
	book.SerializeToString(&book_bytes);
	if (snapshot || book_bytes != session.last_book) {
		(snapshot ? frame.mutable_snapshot()->mutable_book() : frame.mutable_delta()->mutable_book())->Swap(&book);
		session.last_book = std::move(book_bytes);
		changed = true;
	}

	eqheadless::Inventory inventory;
	auto add_item = [&inventory, me](int16 slot, const EQ::ItemInstance *inst) {
		auto *item = inventory.add_items();
		FillItem(item, inst->GetItem(), me);
		item->set_slot(slot);
		item->set_charges(inst->GetCharges());
	};
	for (int16 slot = EQ::invslot::POSSESSIONS_BEGIN; slot <= EQ::invslot::POSSESSIONS_END; ++slot) {
		const EQ::ItemInstance *inst = me->GetInv().GetItem(slot);
		if (!inst || !inst->GetItem()) {
			continue;
		}
		add_item(slot, inst);
		if (inst->IsClassBag()) {
			for (uint8 sub = 0; sub < EQ::invbag::SLOT_COUNT; ++sub) {
				const EQ::ItemInstance *in_bag = inst->GetItem(sub);
				if (in_bag && in_bag->GetItem()) {
					add_item(EQ::InventoryProfile::CalcSlotId(slot, sub), in_bag);
				}
			}
		}
	}
	// our bots' equipment, for the leader to gear them
	eqheadless::OwnedBots owned;
	for (auto &[bid, mob] : entity_list.GetMobList()) {
		if (!mob || !mob->IsBot() || mob->CastToBot()->GetBotOwner() != me) {
			continue;
		}
		Bot  *b  = mob->CastToBot();
		auto *bg = owned.add_bots();
		bg->set_spawn_id(b->GetID());
		bg->set_name(b->GetCleanName());
		bg->set_class_id(b->GetClass());
		bg->set_race_id(b->GetBaseRace());
		bg->set_level(b->GetLevel());
		bg->set_race_bit(GetPlayerRaceBit(b->GetBaseRace()));
		for (int16 slot = EQ::invslot::EQUIPMENT_BEGIN; slot <= EQ::invslot::EQUIPMENT_END; ++slot) {
			const EQ::ItemInstance *inst = b->GetBotItem(slot);
			if (inst && inst->GetItem()) {
				auto *it = bg->add_equipment();
				FillItem(it, inst->GetItem(), b);
				it->set_slot(slot);
				it->set_charges(inst->GetCharges());
			}
		}
	}
	std::string bots_bytes;
	owned.SerializeToString(&bots_bytes);
	if (snapshot || bots_bytes != session.last_bots) {
		(snapshot ? frame.mutable_snapshot()->mutable_bots() : frame.mutable_delta()->mutable_bots())->Swap(&owned);
		session.last_bots = std::move(bots_bytes);
		changed = true;
	}

	std::string inventory_bytes;
	inventory.SerializeToString(&inventory_bytes);
	if (snapshot || inventory_bytes != session.last_inventory) {
		(snapshot ? frame.mutable_snapshot()->mutable_inventory() : frame.mutable_delta()->mutable_inventory())->Swap(&inventory);
		session.last_inventory = std::move(inventory_bytes);
		changed = true;
	}

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
				s.hp_pct == hp_pct && s.level == mob->GetLevel() && s.name == mob->GetCleanName() &&
				s.target_id == (mob->GetTarget() ? mob->GetTarget()->GetID() : 0)) {
				continue;
			}
		}

		FillSpawn(snapshot ? frame.mutable_snapshot()->add_spawns() : frame.mutable_delta()->add_spawns_changed(), mob);
		session.seen[id] = SeenSpawn{
			mob->GetX(), mob->GetY(), mob->GetZ(), mob->GetHeading(), hp_pct, mob->GetLevel(), mob->GetCleanName(),
			mob->GetTarget() ? mob->GetTarget()->GetID() : 0u
		};
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

void HeadlessClientManager::FillItem(eqheadless::Item *out, const EQ::ItemData *item, Mob *me)
{
	out->set_item_id(item->ID);
	out->set_name(item->Name);
	out->set_container(item->ItemClass == EQ::item::ItemClassBag);
	out->set_ac(item->AC);
	out->set_hp(item->HP);
	out->set_mana(item->Mana);
	out->set_endurance(item->Endur);
	out->set_str(item->AStr);
	out->set_sta(item->ASta);
	out->set_agi(item->AAgi);
	out->set_dex(item->ADex);
	out->set_wis(item->AWis);
	out->set_int_(item->AInt);
	out->set_cha(item->ACha);
	out->set_damage(item->Damage);
	out->set_delay(item->Delay);
	out->set_slots(item->Slots);
	out->set_req_level(item->ReqLevel);
	out->set_weight(item->Weight / 10.0f);
	out->set_price(item->Price);
	out->set_no_drop(item->NoDrop == 0);
	out->set_lore(item->LoreFlag);
	out->set_magic(item->Magic);
	out->set_bag_slots(item->BagSlots);
	out->set_bag_size(item->BagSize);
	out->set_item_type(item->ItemType);
	out->set_usable(me && item->IsEquipable(me->GetBaseRace(), me->GetClass()) && item->ReqLevel <= me->GetLevel());
	out->set_classes(item->Classes);
	out->set_races(item->Races);
}

void HeadlessClientManager::RequestZoneLine(const std::string &session_id, Session &session, const glm::vec3 &where)
{
	Client *me = session.stream ? session.stream->GetClient() : nullptr;
	if (!me) {
		return;
	}

	const uint32 target = session.zone_line_to;
	if (zone->HasWaterMap() && !zone->watermap->InZoneLine(where)) {
		// the zone's regions don't put a zone line here; the server will still let us through but
		// flags it (MQZoneUnknownDest), so say so in the log
		LogHeadlessClient("[{}] asking to zone to [{}] from [{:.1f}, {:.1f}, {:.1f}], outside the water map's zone lines", me->GetCleanName(), ShortName(target), where.x, where.y, where.z);
	}

	// an unsolicited zone request names the zone; the server checks it against its zone points
	auto app = new EQApplicationPacket(OP_ZoneChange, sizeof(ZoneChange_Struct));
	auto *zc = reinterpret_cast<ZoneChange_Struct *>(app->pBuffer);
	strn0cpy(zc->char_name, me->GetName(), sizeof(zc->char_name));
	zc->zoneID     = target;
	zc->instanceID = 0;
	zc->x          = where.x;
	zc->y          = where.y;
	zc->z          = where.z;
	zc->success    = 0;
	session.stream->Inject(app);
}

void HeadlessClientManager::SendZoneEvent(const std::string &session_id, int status, uint32 zone_id, const std::string &detail)
{
	eqheadless::ServerFrame frame;
	auto *z = frame.mutable_event()->mutable_zone();
	z->set_status(static_cast<eqheadless::ZoneStatus>(status));
	z->set_zone_id(zone_id);
	z->set_zone(ShortName(zone_id));
	z->set_detail(detail);
	SendFrame(session_id, frame);
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
