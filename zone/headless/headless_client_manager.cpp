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
#include "common/repositories/character_parcels_repository.h"
#include "zone/bot.h"
#include "zone/client.h"
#include "zone/corpse.h"
#include "zone/entity.h"
#include "zone/groups.h"
#include "zone/raids.h"
#include "common/raid.h"
#include "zone/object.h"
#include "zone/headless/null_stream.h"
#include "zone/worldserver.h"
#include "zone/zone.h"
#include "zone/zonedb.h"
#include "zone/water_map.h"
#include "common/json/json.hpp"
#include "common/path_manager.h"

#include "headless.pb.h"

#include <algorithm>
#include <fstream>
#include <cmath>
#include <unordered_set>

extern EntityList  entity_list;
extern WorldServer worldserver;
extern Zone        *zone;

namespace {
	constexpr uint64 PUBLISH_INTERVAL_MS = 250;
	constexpr uint64 HOLD_TICK_MS        = 1000;  // a held session looks for new fights this often
	constexpr int    HOLD_STUN_MS        = 30000; // a hold's stun, renewed every tick: it wears off by itself if the hold is lost
	constexpr uint64 MOVEMENT_HISTORY_MS = 1000;
	// smaller position changes aren't worth a delta
	constexpr float  MOVE_EPSILON        = 0.25f;
	// the client takes a few seconds to memorise a spell (an approximation; it varies a little)
	constexpr uint32 MEMORIZE_MS         = 3000;
	constexpr uint32 LOOT_ITEM_MS        = 50;   // between a corpse's items (a corpse allows one per 10ms)
	constexpr float  REZ_NEAR            = 100.0f; // a rez is taken when its corpse is this near us (it puts us there)

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

	// what is on a mob that a program planning a fight wants to know: mezzed 1, rooted 2, stunned 4, feared or fleeing 8, charmed 16
	uint32 SpawnStatusOf(Mob *mob)
	{
		if (mob->IsCorpse()) {
			return 0;
		}
		return (mob->IsMezzed() ? 1u : 0u) | (mob->IsRooted() ? 2u : 0u) | (mob->IsStunned() ? 4u : 0u) | (mob->IsFeared() ? 8u : 0u) |
			   (mob->IsCharmed() ? 16u : 0u);
	}

	// the spells on an NPC that is fighting (or has the character on its hate list): what a fight is doing to it
	bool ShowsBuffs(Mob *mob, Client *me)
	{
		return mob && mob->IsNPC() && !mob->IsCorpse() && !mob->GetOwner() && (mob->IsEngaged() || (me && mob->CastToNPC()->IsOnHatelist(me)));
	}

	// a signature of those spells: which, and about when they end (to the minute), so a change is sent and a tick is not
	uint64 BuffSigOf(Mob *mob, Client *me)
	{
		uint64 sig = 0;
		if (!ShowsBuffs(mob, me)) {
			return sig;
		}
		if (Buffs_Struct *b = mob->GetBuffs()) {
			for (int i = 0; i < mob->GetMaxTotalSlots(); ++i) {
				if (IsValidSpell(b[i].spellid)) {
					sig = sig * 1000003ull + b[i].spellid * 31ull + static_cast<uint64>(std::max(0, b[i].ticsremaining) / 10);
				}
			}
		}
		return sig;
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
	// where a headless character walks to use a zone line, by its number, where that isn't the zone
	// point's own place: a zone point a little off the navmesh (Nektulos's to the East Commonlands, 8
	// units past the tunnel's), where the water map's zone-line region reaches the navmesh. From the
	// zone's patch (puppeteer nav/<zone>.json "zone_line_spots"), installed as maps/nav/<zone>.zonelines.json
	// beside its swims; zoning itself is the server's as ever (the region, the nearest zone point)
	const std::map<uint32, glm::vec3> &ZoneLineSpots()
	{
		static std::map<uint32, glm::vec3> cached;
		static std::string                 cached_zone;
		const std::string name = zone->GetShortName();
		if (name == cached_zone) {
			return cached;
		}
		cached.clear();
		cached_zone = name;
		std::ifstream in(fmt::format("{}/maps/nav/{}.zonelines.json", PathManager::Instance()->GetServerPath(), name));
		if (!in) {
			return cached;
		}
		try {
			const auto j = nlohmann::json::parse(in);
			for (const auto &z : j.value("spots", nlohmann::json::array())) {
				const auto &a = z.at("at");
				cached[z.at("number").get<uint32>()] = glm::vec3(a.at(0).get<float>(), a.at(1).get<float>(), a.at(2).get<float>());
			}
			LogHeadlessClient("[{}] [{}] zone lines walked to elsewhere", name, cached.size());
		}
		catch (const std::exception &e) {
			LogError("[{}] can't read its zone line spots: {}", name, e.what());
		}
		return cached;
	}

	// where to walk for a boat: the navmesh inside its box nearest the box's middle (the proximity has no height: any floor in
	// the box moves you), a little in from the edges. The middle at the NPC's height was walked to before, and it's rarely on the
	// navmesh (Overthere's NPC is 140 above the floor there, Timorous's docks are open water): no walk, so "overthere has no
	// zone line to timorous", and Grakk, Selka, Sylthra and Velthrin stranded in Timorous Deep all evening on 3 November
	constexpr float BOAT_INSIDE = 5.0f;   // in from the box's edges
	constexpr float BOAT_DEPTH  = 400.0f; // up or down from the NPC's height
	glm::vec3 BoatSpot(const HeadlessMover::Boat &b)
	{
		const glm::vec3 middle((b.x1 + b.x2) / 2.0f, (b.y1 + b.y2) / 2.0f, b.z);
		const glm::vec3 half(std::max(1.0f, (b.x2 - b.x1) / 2.0f - BOAT_INSIDE), std::max(1.0f, (b.y2 - b.y1) / 2.0f - BOAT_INSIDE), BOAT_DEPTH);
		glm::vec3 on;
		if (zone->HeadlessPathing() && zone->HeadlessPathing()->NavNearest(middle, half, on, PathingNotDisabled | PathingHeadless)) {
			return on;
		}
		return middle;
	}

	glm::vec3 ZonePointSpot(const ZonePoint *zp, const glm::vec3 &from)
	{
		const auto &spots = ZoneLineSpots();
		auto it = spots.find(zp->number);
		if (it != spots.end()) {
			return it->second;
		}
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
	const bool was_held = it->second.held;
	Unfreeze(it->second);
	if (auto *me = it->second.stream->GetClient()) {
		if (was_held) {
			// a hold ran out (its program didn't come back): out at the zone's safe point, not in the fight it was frozen in
			const auto safe = zone->GetSafePoint();
			LogInfo("Held character [{}] not taken back; logging out at the safe point ({:.0f}, {:.0f}, {:.0f})", me->GetCleanName(), safe.x, safe.y, safe.z);
			me->GMMove(safe.x, safe.y, safe.z, safe.w);
		}
		me->Kick("headless session ended");
	}
	else {
		it->second.stream->Close();
	}
}

void HeadlessClientManager::HandleHold(ServerPacket *pack)
{
	if (pack->size != sizeof(ServerHeadlessSession_Struct)) {
		LogError("Wrong size on ServerOP_HeadlessHold: got [{}] expected [{}]", pack->size, sizeof(ServerHeadlessSession_Struct));
		return;
	}

	auto *h = reinterpret_cast<ServerHeadlessSession_Struct *>(pack->pBuffer);
	const std::string session_id = SessionID(h->session_id, sizeof(h->session_id));
	auto it = m_sessions.find(session_id);
	if (it == m_sessions.end() || !it->second.stream || it->second.held) {
		return;
	}
	Client *me = it->second.stream->GetClient();
	if (!me) {
		return;
	}

	Session &session = it->second;
	session.held = true;
	if (session.mover) {
		session.mover->Stop(HeadlessMover::Result::Stopped);
	}
	if (me->IsCasting()) {
		me->InterruptSpell();
	}
	FreezeAround(session, me);
	session.hold_timer = std::make_unique<EQ::Timer>(
		HOLD_TICK_MS, true, [this, session_id](EQ::Timer *) {
			auto s = m_sessions.find(session_id);
			if (s != m_sessions.end() && s->second.held && s->second.stream && s->second.stream->GetClient()) {
				FreezeAround(s->second, s->second.stream->GetClient());
			}
		}
	);
	LogInfo("Holding [{}] for its SDK: [{}] frozen", me->GetCleanName(), session.frozen.size());
}

void HeadlessClientManager::HandleResume(ServerPacket *pack)
{
	if (pack->size != sizeof(ServerHeadlessSession_Struct)) {
		LogError("Wrong size on ServerOP_HeadlessResume: got [{}] expected [{}]", pack->size, sizeof(ServerHeadlessSession_Struct));
		return;
	}

	auto *r = reinterpret_cast<ServerHeadlessSession_Struct *>(pack->pBuffer);
	const std::string session_id = SessionID(r->session_id, sizeof(r->session_id));
	auto it = m_sessions.find(session_id);
	if (it == m_sessions.end()) {
		return;
	}

	const size_t n = it->second.frozen.size();
	Unfreeze(it->second);
	auto *me = it->second.stream ? it->second.stream->GetClient() : nullptr;
	LogInfo("Session [{}] resumed: [{}] back, [{}] unfrozen", session_id, me ? me->GetCleanName() : "?", n);
	it->second.seen.clear();
	it->second.last_self.clear();
	it->second.last_book.clear();
	it->second.last_inventory.clear();
	it->second.last_bots.clear();
	Publish(session_id, true); // world sent the Welcome first: this Snapshot follows it
}

void HeadlessClientManager::FreezeAround(Session &session, Client *me)
{
	// ours: the character, its pet, its bots and their pets
	std::vector<Mob *> ours{me};
	if (me->GetPet()) {
		ours.push_back(me->GetPet());
	}
	for (auto *b : entity_list.GetBotsByBotOwnerCharacterID(me->CharacterID())) {
		ours.push_back(b);
		if (b->GetPet()) {
			ours.push_back(b->GetPet());
		}
	}
	for (auto *m : ours) {
		Freeze(session, m);
	}

	// and every NPC in a fight with us: we're on its hate list, or it's on one of ours
	std::unordered_set<uint16> mine;
	for (auto *m : ours) {
		mine.insert(m->GetID());
	}
	for (auto &[id, npc] : entity_list.GetNPCList()) {
		if (!npc || mine.count(id) || !npc->IsEngaged()) {
			continue;
		}
		for (auto *m : ours) {
			if (npc->CheckAggro(m)) {
				Freeze(session, npc);
				break;
			}
		}
	}
	for (auto *m : ours) {
		for (auto *e : m->GetHateList()) {
			if (e && e->entity_on_hatelist && e->entity_on_hatelist->IsNPC() && !mine.count(e->entity_on_hatelist->GetID())) {
				Freeze(session, e->entity_on_hatelist);
			}
		}
	}
}

void HeadlessClientManager::Freeze(Session &session, Mob *mob)
{
	if (!mob) {
		return;
	}
	const uint16 id = mob->GetID();
	if (session.frozen.insert(id).second) {
		auto &f = m_frozen[id];
		if (f.holds++ == 0) {
			f.was_invul  = mob->GetInvul();
			f.was_paused = mob->IsBot() && mob->CastToBot()->GetPauseAI();
			if (mob->IsCasting()) {
				mob->InterruptSpell();
			}
		}
	}
	// (every tick: the stun renewed, so that it wears off by itself if the hold is ever lost)
	mob->SetInvul(true);
	mob->Stun(HOLD_STUN_MS);
	if (mob->IsBot()) {
		mob->CastToBot()->SetPauseAI(true);
	}
}

void HeadlessClientManager::Unfreeze(Session &session)
{
	session.held = false;
	session.hold_timer.reset();
	for (uint16 id : session.frozen) {
		auto f = m_frozen.find(id);
		if (f == m_frozen.end() || --f->second.holds > 0) {
			continue;
		}
		if (auto *mob = entity_list.GetMob(id)) {
			mob->UnStun();
			mob->SetInvul(f->second.was_invul);
			if (mob->IsBot()) {
				mob->CastToBot()->SetPauseAI(f->second.was_paused);
			}
		}
		m_frozen.erase(f);
	}
	session.frozen.clear();
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

// The trade window between two of our characters, as two players use it: A asks, B accepts the trade, A puts the item in and both
// click Trade. Both sides are headless sessions here, so the manager plays B's part too (only for a character of A's guild: our own
// leaders passing on gear they earned, the operator 16 Nov: "swap gear around"). The server's own trade code does the rest, its
// no-drop and lore checks included. Each step a moment after the last: each client's packets are handled in its own Process.
std::string HeadlessClientManager::GiveToPlayer(const std::string &session_id, Session &session, Client *me, Client *to, int16 slot,
                                                uint32 quantity)
{
	const EQ::ItemInstance *inst = me->GetInv().GetItem(slot);
	if (!inst || !inst->GetItem()) {
		return fmt::format("nothing in slot {}", slot);
	}
	if (me->GuildID() == GUILD_NONE || to->GuildID() != me->GuildID()) {
		return "they aren't in our guild";
	}
	std::string to_session;
	for (auto &[sid, s] : m_sessions) {
		if (s.stream && s.stream->GetClient() == to) {
			to_session = sid;
			break;
		}
	}
	if (to_session.empty()) {
		return "they aren't one of ours (a headless session)";
	}
	if (DistanceSquared(me->GetPosition(), to->GetPosition()) > USE_NPC_RANGE2) {
		return "too far from them";
	}
	if (me->IsEngaged() || to->IsEngaged()) {
		return "not while fighting";
	}
	if (me->trade->state != TradeNone || to->trade->state != TradeNone) {
		return "a trade is already open";
	}
	if (!inst->IsDroppable(false)) {
		return fmt::format("{} can't be traded (no drop)", inst->GetItem()->Name);
	}
	const uint16 a = me->GetID(), b = to->GetID();
	auto rq = new EQApplicationPacket(OP_TradeRequest, sizeof(TradeRequest_Struct));
	auto *tr = reinterpret_cast<TradeRequest_Struct *>(rq->pBuffer);
	tr->to_mob_id = b;
	tr->from_mob_id = a;
	session.stream->Inject(rq);
	auto ack = new EQApplicationPacket(OP_TradeRequestAck, sizeof(TradeRequest_Struct));
	auto *ak = reinterpret_cast<TradeRequest_Struct *>(ack->pBuffer);
	ak->to_mob_id = a;
	ak->from_mob_id = b;
	m_sessions[to_session].stream->Inject(ack);   // (B's trade starts both: Trade::Start)
	const int16 count = quantity ? static_cast<int16>(quantity) : (inst->IsStackable() ? std::max<int16>(1, inst->GetCharges()) : 0);
	const std::string name = inst->GetItem()->Name;
	session.trade_timer = std::make_unique<EQ::Timer>(GIVE_STEP_MS, false, [this, session_id, to_session, slot, count, a, b, name](EQ::Timer *) {
		auto s = m_sessions.find(session_id);
		auto t = m_sessions.find(to_session);
		if (s == m_sessions.end() || !s->second.stream || !s->second.stream->GetClient() || t == m_sessions.end() || !t->second.stream) {
			return;
		}
		Client *me = s->second.stream->GetClient();
		if (me->trade->state != Trading || me->trade->With() == nullptr || me->trade->With()->GetID() != b) {
			LogHeadlessClient("[{}] the trade with [{}] didn't open", me->GetCleanName(), b);
			return;
		}
		if (slot != EQ::invslot::slotCursor) {
			auto cu = new EQApplicationPacket(OP_MoveItem, sizeof(MoveItem_Struct));
			auto *c = reinterpret_cast<MoveItem_Struct *>(cu->pBuffer);
			c->from_slot = slot;
			c->to_slot = EQ::invslot::slotCursor;
			c->number_in_stack = count;
			s->second.stream->Inject(cu);
		}
		auto mv = new EQApplicationPacket(OP_MoveItem, sizeof(MoveItem_Struct));
		auto *mi = reinterpret_cast<MoveItem_Struct *>(mv->pBuffer);
		mi->from_slot = EQ::invslot::slotCursor;
		mi->to_slot = EQ::invslot::TRADE_BEGIN;
		mi->number_in_stack = 0;
		s->second.stream->Inject(mv);
		auto ok = new EQApplicationPacket(OP_TradeAcceptClick, sizeof(TradeAccept_Struct));
		reinterpret_cast<TradeAccept_Struct *>(ok->pBuffer)->from_mob_id = a;
		s->second.stream->Inject(ok);
		t->second.trade_timer = std::make_unique<EQ::Timer>(GIVE_STEP_MS, false, [this, to_session, b, name](EQ::Timer *) {
			auto t2 = m_sessions.find(to_session);
			if (t2 == m_sessions.end() || !t2->second.stream || !t2->second.stream->GetClient()) {
				return;
			}
			auto ok2 = new EQApplicationPacket(OP_TradeAcceptClick, sizeof(TradeAccept_Struct));
			reinterpret_cast<TradeAccept_Struct *>(ok2->pBuffer)->from_mob_id = b;
			t2->second.stream->Inject(ok2);
			LogHeadlessClient("[{}] accepts [{}] in the trade", t2->second.stream->GetClient()->GetCleanName(), name);
		});
	});
	LogHeadlessClient("[{}] trading [{}] to [{}]", me->GetCleanName(), name, to->GetCleanName());
	return "";
}

std::string HeadlessClientManager::Execute(const std::string &session_id, Session &session, Client *me, const eqheadless::Command &cmd)
{
	NullStream *stream = session.stream;

	switch (cmd.action_case()) {
		case eqheadless::Command::kMoveTo: {
			if (!session.mover) {
				return "character is not ready to move";
			}
			const auto &m = cmd.move_to();
			if (!std::isfinite(m.x()) || !std::isfinite(m.y()) || !std::isfinite(m.z())) {
				return "destination must be finite";
			}
			if (m.plan_only()) {
				const float length = session.mover->Check(glm::vec3(m.x(), m.y(), m.z()));
				eqheadless::ServerFrame frame;
				auto *move = frame.mutable_event()->mutable_move();
				move->set_status(length >= 0.0f ? eqheadless::MOVE_ARRIVED : eqheadless::MOVE_NO_PATH);
				move->set_x(m.x());
				move->set_y(m.y());
				move->set_z(m.z());
				move->set_plan_only(true);
				move->set_route_length(std::max(length, 0.0f));
				move->set_swim_length(length >= 0.0f ? session.mover->CheckedSwim() : 0.0f);
				if (length >= 0.0f) {
					const auto &pts = session.mover->CheckedPath();
					const size_t step = std::max<size_t>(1, (pts.size() + 119) / 120);
					for (size_t i = 0; i < pts.size(); i += step) {
						auto *pt = move->add_path();
						pt->set_x(pts[i].x);
						pt->set_y(pts[i].y);
						pt->set_z(pts[i].z);
					}
				}
				SendFrame(session_id, frame);
				return "";
			}
			session.zone_line_to = 0; // a walk of our own ends a ZoneTo
			session.mover->AllowBoatTo(0);
			session.zone_line_boat = false;
			return session.mover->Start(glm::vec3(m.x(), m.y(), m.z()), false, m.avoid());
		}
		case eqheadless::Command::kStop: {
			if (session.mover) {
				session.mover->Stop(HeadlessMover::Result::Stopped);
			}
			return "";
		}
		case eqheadless::Command::kSurface: {
			return session.mover ? session.mover->Surface() : "character is not ready to move";
		}
		case eqheadless::Command::kCorpseDrag: {
			// as the client's /corpse drag and /corpse drop: the dragged corpse follows our position
			// updates (Client::DragCorpses), within Character:DragCorpseDistance
			const auto &d = cmd.corpse_drag();
			Mob *corpse = d.corpse_id() ? entity_list.GetMob(d.corpse_id()) : nullptr;
			if (d.corpse_id() && (!corpse || !corpse->IsPlayerCorpse())) {
				return fmt::format("no player's corpse with id {}", d.corpse_id());
			}
			if (d.drop()) {
				if (!corpse) {
					auto app = new EQApplicationPacket(OP_CorpseDrop, 1);
					stream->Inject(app);
					return "";
				}
				const std::string name = corpse->GetName();
				auto app = new EQApplicationPacket(OP_CorpseDrop, name.size() + 1);
				memcpy(app->pBuffer, name.c_str(), name.size() + 1);
				stream->Inject(app);
				return "";
			}
			if (!corpse) {
				return "no corpse to drag";
			}
			auto app = new EQApplicationPacket(OP_CorpseDrag, sizeof(CorpseDrag_Struct));
			auto *cd = reinterpret_cast<CorpseDrag_Struct *>(app->pBuffer);
			strn0cpy(cd->CorpseName, corpse->GetName(), sizeof(cd->CorpseName));
			strn0cpy(cd->DraggerName, me->GetName(), sizeof(cd->DraggerName));
			stream->Inject(app);
			return "";
		}
		case eqheadless::Command::kClickDoor: {
			// as the client clicks a door: OP_ClickDoor, its door id. A door with a destination (a
			// teleporter, a Plane of Knowledge book) zones us there; the server checks how far we are
			// (within the door's reach), keys and locks, and says why not, as it would to a player
			const uint32 id = cmd.click_door().door_id();
			Doors *door = id <= 255 ? entity_list.FindDoor(static_cast<uint8>(id)) : nullptr;
			if (!door) {
				return fmt::format("no door {} in this zone", id);
			}
			auto app = new EQApplicationPacket(OP_ClickDoor, sizeof(ClickDoor_Struct));
			auto *cd = reinterpret_cast<ClickDoor_Struct *>(app->pBuffer);
			cd->doorid    = door->GetDoorID();
			cd->player_id = me->GetID();
			stream->Inject(app);
			LogHeadlessClient("[{}] clicking door [{}] ({})", me->GetCleanName(), door->GetDoorID(), door->GetDoorName());
			return "";
		}
		case eqheadless::Command::kClickObject: {
			// as the client clicks a world container (OP_ClickObject): the server opens it for us if we're within reach
			const uint32 id = cmd.click_object().object_id();
			Object *object = entity_list.GetObjectByDBID(id);
			if (!object) {
				return fmt::format("no object {} in this zone", id);
			}
			auto app = new EQApplicationPacket(OP_ClickObject, sizeof(ClickObject_Struct));
			auto *co = reinterpret_cast<ClickObject_Struct *>(app->pBuffer);
			co->drop_id   = object->GetID();
			co->player_id = me->GetID();
			stream->Inject(app);
			LogHeadlessClient("[{}] clicking object [{}] ({})", me->GetCleanName(), id, object->GetModelName());
			return "";
		}
		case eqheadless::Command::kCloseObject: {
			stream->Inject(new EQApplicationPacket(OP_ClickObjectAction, 0)); // (what the client sends to close one)
			return "";
		}
		case eqheadless::Command::kCombine: {
			// the client's Combine button: OP_TradeSkillCombine for the container (the server does the recipe, the skill roll and the items)
			auto app = new EQApplicationPacket(OP_TradeSkillCombine, sizeof(NewCombine_Struct));
			auto *nc = reinterpret_cast<NewCombine_Struct *>(app->pBuffer);
			nc->container_slot      = static_cast<int16>(cmd.combine().container_slot());
			nc->guildtribute_slot   = 0;
			stream->Inject(app);
			LogHeadlessClient("[{}] combining in slot [{}]", me->GetCleanName(), cmd.combine().container_slot());
			return "";
		}
		case eqheadless::Command::kAugment: {
			// the client's augment window: OP_AugmentItem (Client::Handle_OP_AugmentItem does the rules, the swap and the distiller). The
			// augment to insert is on the cursor already (MoveItem); the answer is chat and the item's new augments in the inventory frame
			const auto &ag = cmd.augment();
			if (ag.action() > AugmentActions::Destroy || ag.aug_index() >= EQ::invaug::SOCKET_COUNT) {
				return "no such augment action or socket";
			}
			auto app = new EQApplicationPacket(OP_AugmentItem, sizeof(AugmentItem_Struct));
			auto *au = reinterpret_cast<AugmentItem_Struct *>(app->pBuffer);
			memset(au, 0, sizeof(AugmentItem_Struct));
			au->container_slot = ag.item_slot();
			au->augment_slot   = ag.solvent_slot();
			au->augment_index  = ag.aug_index();
			au->augment_action = ag.action();
			stream->Inject(app);
			LogHeadlessClient("[{}] augment: item slot [{}] socket [{}] action [{}] distiller slot [{}]", me->GetCleanName(), ag.item_slot(),
							  ag.aug_index(), ag.action(), ag.solvent_slot());
			return "";
		}
		case eqheadless::Command::kBuyAa: {
			// as the client's AA window: a point spent on the next rank of an ability (the server checks the level, the price, the prerequisites)
			const int rank_id = static_cast<int>(cmd.buy_aa().rank_id());
			auto *rank = zone->GetAlternateAdvancementRank(rank_id);
			if (!rank || !rank->base_ability) {
				return fmt::format("no AA rank {}", rank_id);
			}
			if (me->GetAAPoints() < static_cast<uint32>(rank->cost)) {
				return fmt::format("{} costs {} points, we have {}", rank->base_ability->name, rank->cost, me->GetAAPoints());
			}
			if (!me->CanPurchaseAlternateAdvancementRank(rank, true, true)) {
				return fmt::format("can't buy rank {} of {} now (level, prerequisites, or already bought)", rank_id, rank->base_ability->name);
			}
			me->PurchaseAlternateAdvancementRank(rank_id);
			LogHeadlessClient("[{}] bought AA rank [{}] ({})", me->GetCleanName(), rank_id, rank->base_ability->name);
			return "";
		}
		case eqheadless::Command::kAaExp: {
			// as the client's AA experience slider: OP_AAAction, set (or 0: off)
			const uint32 pct = std::min<uint32>(cmd.aa_exp().percent(), 100);
			auto app = new EQApplicationPacket(OP_AAAction, sizeof(AA_Action));
			auto *a = reinterpret_cast<AA_Action *>(app->pBuffer);
			a->action    = pct ? 1 : 2; // aaActionSetEXP, aaActionDisableEXP
			a->exp_value = pct;
			stream->Inject(app);
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
			if (session.mover && session.mover->Moving() && me->GetClass() != Class::Bard) { // bards sing on the move
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
			session.loot_peek   = cmd.loot().peek();
			session.loot_only.assign(cmd.loot().only().begin(), cmd.loot().only().end());
			session.loot_filtered = !session.loot_only.empty();
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

			// the zone line with the shortest walk from here (a zone can have several to one neighbour, and
			// the nearest as the crow flies can be in another part of it), or the one asked for by number;
			// with no walk to any, the nearest (the walk then says why not)
			const glm::vec3 here(me->GetX(), me->GetY(), me->GetZ());
			const uint32 number   = cmd.zone_to().number();
			const ZonePoint *best = nullptr, *nearest = nullptr;
			float best_walk = 0.0f, nearest_dist = 0.0f;
			for (auto *zp : UsableZonePoints(me)) {
				if (zp->target_zone_id != zone_id || (number && zp->number != number)) {
					continue;
				}
				const glm::vec3 spot = ZonePointSpot(zp, here);
				const float dist     = glm::length(glm::vec2(spot.x - here.x, spot.y - here.y));
				if (!nearest || dist < nearest_dist) {
					nearest      = zp;
					nearest_dist = dist;
				}
				const float walk = session.mover->Check(spot);
				if (walk >= 0.0f && (!best || walk < best_walk)) {
					best      = zp;
					best_walk = walk;
				}
			}
			// a boat to the zone (a proximity box that moves you there: HeadlessMover::Boats), when no zone line has a walk
			const HeadlessMover::Boat *boat = nullptr;
			float boat_walk = 0.0f;
			if (!best && !number) {
				for (const auto &b : HeadlessMover::Boats()) {
					if (b.to != zone_id) {
						continue;
					}
					const glm::vec3 spot = BoatSpot(b);
					const float walk = session.mover->Check(spot);
					if (walk >= 0.0f && (!boat || walk < boat_walk)) {
						boat      = &b;
						boat_walk = walk;
					}
				}
			}
			if (!best && !boat) {
				best = nearest;
			}
			if (!best && !boat) {
				return number ? fmt::format("{} has no zone line {} to {}", zone->GetShortName(), number, ShortName(zone_id))
					: fmt::format("{} has no zone line to {}", zone->GetShortName(), ShortName(zone_id));
			}

			const glm::vec3 boat_spot = boat ? BoatSpot(*boat) : glm::vec3(0.0f);
			if (cmd.zone_to().plan_only()) {
				// the walk it would take, without taking it (a walk under way carries on)
				const glm::vec3 spot = boat ? boat_spot : ZonePointSpot(best, here);
				const float length   = boat ? boat_walk : session.mover->Check(spot);
				eqheadless::ServerFrame frame;
				auto *move = frame.mutable_event()->mutable_move();
				move->set_status(length >= 0.0f ? eqheadless::MOVE_ARRIVED : eqheadless::MOVE_NO_PATH);
				move->set_x(spot.x);
				move->set_y(spot.y);
				move->set_z(spot.z);
				move->set_plan_only(true);
				move->set_route_length(std::max(length, 0.0f));
				move->set_swim_length(length >= 0.0f ? session.mover->CheckedSwim() : 0.0f);
				if (length >= 0.0f) {
					const auto &pts = session.mover->CheckedPath();
					const size_t step = std::max<size_t>(1, (pts.size() + 119) / 120);
					for (size_t i = 0; i < pts.size(); i += step) {
						auto *pt = move->add_path();
						pt->set_x(pts[i].x);
						pt->set_y(pts[i].y);
						pt->set_z(pts[i].z);
					}
				}
				SendFrame(session_id, frame);
				return "";
			}

			// end a walk that's under way first: its "stopped" must not be taken as this ZoneTo failing
			if (session.mover->Moving()) {
				session.zone_line_to = 0;
				session.mover->Stop(HeadlessMover::Result::Stopped);
			}
			session.zone_line_to   = zone_id;
			session.zone_line_boat = boat != nullptr;
			session.mover->AllowBoatTo(boat ? zone_id : 0);
			if (boat) {
				LogHeadlessClient("[{}] walking into the boat to [{}] at [{:.0f}, {:.0f}, {:.0f}]", me->GetCleanName(), ShortName(zone_id), boat_spot.x, boat_spot.y, boat_spot.z);
			}
			const std::string error = session.mover->Start(boat ? boat_spot : ZonePointSpot(best, here), true);
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
		case eqheadless::Command::kParcelSend: {
			const auto &ps  = cmd.parcel_send();
			NPC        *npc = entity_list.GetNPCByID(ps.npc_id());
			if (!npc || !npc->GetParcelMerchant()) {
				return "that's not a parcel merchant";
			}
			if (DistanceSquared(me->GetPosition(), npc->GetPosition()) > USE_NPC_RANGE2) {
				return "too far from the parcel merchant";
			}
			const EQ::ItemInstance *inst = me->GetInv().GetItem(ps.slot());
			if (!inst || !inst->GetItem()) {
				return fmt::format("nothing in slot {}", ps.slot());
			}
			// the client's parcel window refuses these (no-drop unless the server allows trading it,
			// World:FVNoDropFlag), and the server leaves that to it
			// (Client::CanTradeFVNoDropItem's rule: 1 everyone, 2 status MinStatusForNoDropExemptions and up)
			const int  fv         = RuleI(World, FVNoDropFlag);
			const bool no_drop_ok = fv == 1 || (fv == 2 && me->Admin() >= RuleI(Character, MinStatusForNoDropExemptions));
			if (inst->IsAttuned() || (!inst->GetItem()->NoDrop && !no_drop_ok)) {
				return fmt::format("{} can't be sent: it's no-drop", inst->GetItem()->Name);
			}
			if (ps.to().empty() || ps.to().size() >= sizeof(Parcel_Struct::send_to)) {
				return "send it to whom?";
			}
			auto app = new EQApplicationPacket(OP_ShopSendParcel, sizeof(Parcel_Struct));
			auto *p  = reinterpret_cast<Parcel_Struct *>(app->pBuffer);
			p->npc_id     = npc->GetID();
			p->item_slot  = ps.slot();
			p->quantity   = ps.quantity() ? ps.quantity() : (inst->IsStackable() ? std::max<int16>(1, inst->GetCharges()) : 1);
			p->money_flag = PARCEL_SEND_ITEMS;
			strn0cpy(p->send_to, ps.to().c_str(), sizeof(p->send_to));
			strn0cpy(p->note, ps.note().c_str(), sizeof(p->note));
			stream->Inject(app);
			return "";
		}
		case eqheadless::Command::kParcelRetrieve: {
			const auto &pr  = cmd.parcel_retrieve();
			NPC        *npc = entity_list.GetNPCByID(pr.npc_id());
			if (!npc || !npc->GetParcelMerchant()) {
				return "that's not a parcel merchant";
			}
			if (DistanceSquared(me->GetPosition(), npc->GetPosition()) > USE_NPC_RANGE2) {
				return "too far from the parcel merchant";
			}
			if (!me->GetEngagedWithParcelMerchant()) {
				return "open the parcel merchant's window first";
			}
			auto app = new EQApplicationPacket(OP_ShopRetrieveParcel, sizeof(ParcelRetrieve_Struct));
			auto *r  = reinterpret_cast<ParcelRetrieve_Struct *>(app->pBuffer);
			r->merchant_entity_id = npc->GetID();
			r->player_entity_id   = me->GetID();
			r->parcel_slot_id     = pr.parcel_slot();
			r->parcel_item_id     = pr.item_id();
			stream->Inject(app);
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
		case eqheadless::Command::kCoin: {
			// as the bank window: a coin move a type at a time (OP_MoveCoin, inventory 1 <-> bank 2); the server's handler wants a banker beside us
			const auto &cc = cmd.coin();
			uint32 distance = 0;
			NPC *banker = entity_list.GetClosestBanker(me, distance);
			if (!banker || distance > USE_NPC_RANGE2) {
				return "no banker beside us";
			}
			const auto &pp = me->GetPP();
			const uint32 have[4] = {
				cc.to_bank() ? pp.platinum : pp.platinum_bank, cc.to_bank() ? pp.gold : pp.gold_bank,
				cc.to_bank() ? pp.silver : pp.silver_bank, cc.to_bank() ? pp.copper : pp.copper_bank};
			const uint32 types[4] = {COINTYPE_PP, COINTYPE_GP, COINTYPE_SP, COINTYPE_CP};
			const uint64 worth[4] = {1000, 100, 10, 1};
			uint64 left = cc.copper();
			uint64 take[4] = {0, 0, 0, 0};
			for (int k = 0; k < 4 && left; ++k) {  // whole coins, the biggest first, no more than asked
				take[k] = std::min<uint64>(have[k], left / worth[k]);
				left -= take[k] * worth[k];
			}
			if (left && !cc.to_bank()) {
				// a withdrawal short in small coin: one more of the smallest coin there is still, to pay with (the change stays carried)
				for (int k = 3; k >= 0 && left; --k) {
					if (take[k] < have[k] && worth[k] >= left) {
						take[k] += 1;
						left = 0;
					}
				}
				for (int k = 3; k >= 0 && left; --k) {
					const uint64 more = std::min<uint64>(have[k] - take[k], (left + worth[k] - 1) / worth[k]);
					take[k] += more;
					left = more * worth[k] >= left ? 0 : left - more * worth[k];
				}
			}
			for (int k = 0; k < 4; ++k) {
				if (!take[k]) {
					continue;
				}
				auto app = new EQApplicationPacket(OP_MoveCoin, sizeof(MoveCoin_Struct));
				auto *mc = reinterpret_cast<MoveCoin_Struct *>(app->pBuffer);
				mc->from_slot = cc.to_bank() ? 1 : 2;
				mc->to_slot   = cc.to_bank() ? 2 : 1;
				mc->cointype1 = types[k];
				mc->cointype2 = types[k];
				mc->amount    = static_cast<int32>(take[k]);
				stream->Inject(app);
			}
			LogHeadlessClient("[{}] coin {} the bank: [{}] copper's worth asked", me->GetCleanName(), cc.to_bank() ? "to" : "from", cc.copper());
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
		case eqheadless::Command::kGive: {
			// the client's trade window with an NPC: open it, put the item in, give (the NPC's script takes it)
			const auto &gv = cmd.give();
			Mob *npc = entity_list.GetMob(gv.npc_id());
			const EQ::ItemInstance *inst = me->GetInv().GetItem(gv.slot());
			if (npc && npc->IsClient() && npc != me) {
				return GiveToPlayer(session_id, session, me, npc->CastToClient(), gv.slot(), gv.quantity());
			}
			if (!npc || !npc->IsNPC()) {
				return "that's not an NPC";
			}
			if (DistanceSquared(me->GetPosition(), npc->GetPosition()) > USE_NPC_RANGE2) {
				return "too far from them";
			}
			if (npc->IsEngaged()) {
				return "they're fighting";
			}
			if (!inst || !inst->GetItem()) {
				return fmt::format("nothing in slot {}", gv.slot());
			}
			auto rq = new EQApplicationPacket(OP_TradeRequest, sizeof(TradeRequest_Struct));
			auto *tr = reinterpret_cast<TradeRequest_Struct *>(rq->pBuffer);
			tr->to_mob_id   = npc->GetID();
			tr->from_mob_id = me->GetID();
			stream->Inject(rq);
			// into a trade only from the cursor, and as the whole of what's on it (number_in_stack 0): a count into an empty trade slot is
			// read as stacking onto what's there, and the server kicks ("Error stacking item in trade": twelve leaders handing Relm M`Loch
			// a stackable Fuligan Soulstone, eq3, 15 Nov). A count is taken by the move onto the cursor (it splits the stack)
			if (gv.slot() != EQ::invslot::slotCursor) {
				auto cu = new EQApplicationPacket(OP_MoveItem, sizeof(MoveItem_Struct));
				auto *c = reinterpret_cast<MoveItem_Struct *>(cu->pBuffer);
				c->from_slot       = gv.slot();
				c->to_slot         = EQ::invslot::slotCursor;
				c->number_in_stack = gv.quantity() ? gv.quantity() : (inst->IsStackable() ? std::max<int16>(1, inst->GetCharges()) : 0);
				stream->Inject(cu);
			}
			auto mv = new EQApplicationPacket(OP_MoveItem, sizeof(MoveItem_Struct));
			auto *mi = reinterpret_cast<MoveItem_Struct *>(mv->pBuffer);
			mi->from_slot       = EQ::invslot::slotCursor;
			mi->to_slot         = EQ::invslot::TRADE_BEGIN;
			mi->number_in_stack = 0;
			stream->Inject(mv);
			// the rest of a hand-in of several, each into the next trade slot by way of the cursor (as the client does: the server
			// takes nothing into a trade but from the cursor), so the NPC's script sees them all in the one trade
			int16 next = EQ::invslot::TRADE_BEGIN + 1;
			for (const auto &more : gv.more()) {
				if (next > EQ::invslot::TRADE_END) {
					break;
				}
				const EQ::ItemInstance *m = me->GetInv().GetItem(more.slot());
				if (!m || !m->GetItem() || more.slot() == EQ::invslot::slotCursor) {
					continue;
				}
				const int16 count = more.quantity() ? more.quantity() : (m->IsStackable() ? std::max<int16>(1, m->GetCharges()) : 0);
				auto up = new EQApplicationPacket(OP_MoveItem, sizeof(MoveItem_Struct));
				auto *u = reinterpret_cast<MoveItem_Struct *>(up->pBuffer);
				u->from_slot = more.slot();
				u->to_slot = EQ::invslot::slotCursor;
				u->number_in_stack = count;
				stream->Inject(up);
				auto in = new EQApplicationPacket(OP_MoveItem, sizeof(MoveItem_Struct));
				auto *t = reinterpret_cast<MoveItem_Struct *>(in->pBuffer);
				t->from_slot = EQ::invslot::slotCursor;
				t->to_slot = next++;
				t->number_in_stack = 0;
				stream->Inject(in);
			}
			auto ok = new EQApplicationPacket(OP_TradeAcceptClick, sizeof(TradeAccept_Struct));
			reinterpret_cast<TradeAccept_Struct *>(ok->pBuffer)->from_mob_id = me->GetID();
			stream->Inject(ok);
			LogHeadlessClient("[{}] giving [{}] to [{}]", me->GetCleanName(), inst->GetItem()->Name, npc->GetCleanName());
			return "";
		}
		case eqheadless::Command::kScribe: {
			const auto &sc = cmd.scribe();
			const EQ::ItemInstance *inst = me->GetInv().GetItem(EQ::invslot::slotCursor);
			if (!inst || !inst->GetItem() || inst->GetItem()->Scroll.Effect != static_cast<int32>(sc.spell_id())) {
				return "pick up the scroll for that spell first (it must be on the cursor)";
			}
			if (sc.book_slot() >= EQ::spells::SPELLBOOK_SIZE || IsValidSpell(me->GetSpellIDByBookSlot(sc.book_slot()))) {
				return fmt::format("spellbook page {} isn't free", sc.book_slot());
			}
			auto app = new EQApplicationPacket(OP_MemorizeSpell, sizeof(MemorizeSpell_Struct));
			auto *ms = reinterpret_cast<MemorizeSpell_Struct *>(app->pBuffer);
			ms->slot     = sc.book_slot();
			ms->spell_id = sc.spell_id();
			ms->scribing = memSpellScribing;
			stream->Inject(app);
			return "";
		}
		case eqheadless::Command::kAbility: {
			// the packets the client's ability buttons send; the handlers check skills and timers
			const uint32 skill = cmd.ability().skill_id();
			switch (skill) {
				case EQ::skills::SkillSneak:
					stream->Inject(new EQApplicationPacket(OP_Sneak, 0));
					return "";
				case EQ::skills::SkillHide:
					stream->Inject(new EQApplicationPacket(OP_Hide, 0));
					return "";
				case EQ::skills::SkillFeignDeath:
					stream->Inject(new EQApplicationPacket(OP_FeignDeath, 0));
					return "";
				case EQ::skills::SkillForage:
					// the Forage button: Handle_OP_Forage checks the reuse timer and rolls the zone's forage table (the ranger's and druid's epic
					// pieces are foraged: Ripened Heartfruit and the rest)
					stream->Inject(new EQApplicationPacket(OP_Forage, 0));
					return "";
				case EQ::skills::SkillTaunt: {
					if (!me->GetTarget()) {
						return "no target to taunt";
					}
					auto app = new EQApplicationPacket(OP_Taunt, sizeof(ClientTarget_Struct));
					reinterpret_cast<ClientTarget_Struct *>(app->pBuffer)->new_target = me->GetTarget()->GetID();
					stream->Inject(app);
					return "";
				}
				case EQ::skills::SkillThrowing:
				case EQ::skills::SkillArchery: {
					// the range button: what's in the range slot thrown (or shot) at the target. The client sends it as a combat ability
					// whose m_atk is the range slot (Client::Handle_OP_CombatAbility: ThrowingAttack or RangedAttack checks the item,
					// the range and the timers). A raid's puller tags with it (the operator, 17 Nov)
					if (!me->GetTarget()) {
						return "no target";
					}
					auto app = new EQApplicationPacket(OP_CombatAbility, sizeof(CombatAbility_Struct));
					auto *ca = reinterpret_cast<CombatAbility_Struct *>(app->pBuffer);
					ca->m_target = me->GetTarget()->GetID();
					ca->m_atk    = EQ::invslot::slotRange;
					ca->m_skill  = skill;
					stream->Inject(app);
					return "";
				}
				case EQ::skills::SkillBackstab:
				case EQ::skills::SkillBash:
				case EQ::skills::SkillKick:
				case EQ::skills::SkillFlyingKick:
				case EQ::skills::SkillRoundKick:
				case EQ::skills::SkillTigerClaw:
				case EQ::skills::SkillEagleStrike:
				case EQ::skills::SkillDragonPunch:
				case EQ::skills::SkillFrenzy:
				case EQ::skills::SkillIntimidation: {
					if (!me->GetTarget()) {
						return "no target";
					}
					auto app = new EQApplicationPacket(OP_CombatAbility, sizeof(CombatAbility_Struct));
					auto *ca = reinterpret_cast<CombatAbility_Struct *>(app->pBuffer);
					ca->m_target = me->GetTarget()->GetID();
					ca->m_atk    = 100; // what the client sends for a melee ability (the handler checks it)
					ca->m_skill  = skill;
					stream->Inject(app);
					return "";
				}
				default:
					return fmt::format("skill {} isn't an ability that can be used this way", skill);
			}
		}
		case eqheadless::Command::kPet: {
			// as /pet and the pet window (attack goes for our target)
			if (!me->GetPet()) {
				return "you have no pet";
			}
			const uint32 command = cmd.pet().command();
			if (command >= PetCommand::Max) {
				return fmt::format("no pet command {}", command);
			}
			if ((command == PetCommand::Attack || command == PetCommand::QAttack) && !me->GetTarget()) {
				return "no target for your pet";
			}
			auto app = new EQApplicationPacket(OP_PetCommands, sizeof(PetCommand_Struct));
			auto *pc = reinterpret_cast<PetCommand_Struct *>(app->pBuffer);
			pc->command = command;
			pc->target  = me->GetTarget() ? me->GetTarget()->GetID() : 0;
			stream->Inject(app);
			return "";
		}
		case eqheadless::Command::kDiscipline: {
			// the combat abilities window casts a known discipline in its own slot
			const uint32 spell_id = cmd.discipline().spell_id();
			if (!me->HasDisciplineLearned(spell_id)) {
				return fmt::format("you don't know {}", IsValidSpell(spell_id) ? SpellName(spell_id) : std::to_string(spell_id));
			}
			if (me->IsCasting()) {
				return "already casting";
			}
			auto app = new EQApplicationPacket(OP_CastSpell, sizeof(CastSpell_Struct));
			auto *cs = reinterpret_cast<CastSpell_Struct *>(app->pBuffer);
			cs->slot          = static_cast<uint32>(EQ::spells::CastingSlot::Discipline);
			cs->spell_id      = spell_id;
			cs->inventoryslot = 0xFFFF;
			cs->target_id     = me->GetTarget() ? me->GetTarget()->GetID() : me->GetID();
			stream->Inject(app);
			return "";
		}
		case eqheadless::Command::kUseItem: {
			// a right-click on an item, as the client sends it
			const int32 slot = cmd.use_item().slot();
			if (!me->GetInv().GetItem(slot)) {
				return fmt::format("nothing in slot {}", slot);
			}
			auto app = new EQApplicationPacket(OP_ItemVerifyRequest, sizeof(ItemVerifyRequest_Struct));
			auto *iv = reinterpret_cast<ItemVerifyRequest_Struct *>(app->pBuffer);
			iv->slot   = slot;
			iv->target = me->GetTarget() ? me->GetTarget()->GetID() : me->GetID();
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
		case eqheadless::Command::kBuffRemove: {
			// as the client's buff window (OP_BuffRemoveRequest): the server lets a client click off beneficial buffs on itself and its own bots
			const uint32 id = cmd.buff_remove().spawn_id();
			Mob *who = id ? entity_list.GetMob(id) : me;
			if (!who) {
				return fmt::format("no spawn with id {}", id);
			}
			if (who != me && !(who->IsBot() && who->GetOwner() == me)) {
				return fmt::format("{} is neither you nor one of your bots", who->GetCleanName());
			}
			const uint32 slot = cmd.buff_remove().slot();
			if (slot >= static_cast<uint32>(who->GetMaxTotalSlots()) || !IsValidSpell(who->GetSpellIDFromSlot(slot))) {
				return fmt::format("{} has no buff in slot {}", who->GetCleanName(), slot);
			}
			const uint16 spell = who->GetSpellIDFromSlot(slot);
			if (!IsBeneficialSpell(spell) || spells[spell].no_remove) {
				return fmt::format("{} can't be clicked off", SpellName(spell));
			}
			auto app = new EQApplicationPacket(OP_BuffRemoveRequest, sizeof(BuffRemoveRequest_Struct));
			auto *br = reinterpret_cast<BuffRemoveRequest_Struct *>(app->pBuffer);
			br->SlotID   = slot;
			br->EntityID = who->GetID();
			stream->Inject(app);
			return "";
		}
		case eqheadless::Command::kRaidInvite: {
			// as the client's raid window invite (OP_RaidInvite, which the server routes to Handle_OP_RaidCommand; RaidCommandInvite): a bot goes straight in (Bot::ProcessRaidInvite),
			// a player is asked (OP_RaidUpdate action 20; a headless one answers it in OnOutgoing)
			const std::string &name = cmd.raid_invite().name();
			Mob *invitee = name.empty() ? me->GetTarget() : FindMobByName(name);
			if (!invitee || invitee == me) {
				return name.empty() ? "no target to invite" : fmt::format("no one named {} here", name);
			}
			Raid *raid = me->GetRaid();
			if (raid && raid->IsRaidMember(invitee->GetName())) {
				return fmt::format("{} is already in our raid", invitee->GetCleanName());
			}
			if (invitee->IsBot()) {
				if (!RuleB(HeadlessClients, AllowBots)) {
					return "bots are turned off for headless characters (HeadlessClients:AllowBots)";
				}
				Mob *owner = invitee->CastToBot()->GetBotOwner();
				if (owner != me && !(raid && owner && raid->IsRaidMember(owner->GetName()))) {
					return fmt::format("{} is not your bot, and its owner is not in our raid", invitee->GetCleanName());
				}
				if (invitee->IsRaidGrouped()) {
					return fmt::format("{} is already in a raid", invitee->GetCleanName());
				}
			}
			else if (invitee->IsClient()) {
				if (!IsHeadless(invitee) && !RuleB(HeadlessClients, AllowGroupWithPlayers)) {
					return "raiding with players is turned off for headless characters (HeadlessClients:AllowGroupWithPlayers)";
				}
				if (invitee->CastToClient()->HasRaid()) {
					return fmt::format("{} is already in a raid", invitee->GetCleanName());
				}
			}
			else {
				return fmt::format("{} can't join a raid", invitee->GetCleanName());
			}
			if (Group *g = invitee->GetGroup()) {
				if (g->IsGroupMember(me)) {
					return fmt::format("{} is in our own group: a raid made by inviting another takes our group in with us", invitee->GetCleanName());
				}
				if (!g->IsLeader(invitee)) {
					return fmt::format("{} is in a group but doesn't lead it: invite its leader", invitee->GetCleanName());
				}
			}
			auto app = new EQApplicationPacket(OP_RaidInvite, sizeof(RaidGeneral_Struct));
			auto *rg = reinterpret_cast<RaidGeneral_Struct *>(app->pBuffer);
			rg->action = RaidCommandInvite;
			strn0cpy(rg->player_name, invitee->GetName(), sizeof(rg->player_name));
			strn0cpy(rg->leader_name, me->GetName(), sizeof(rg->leader_name));
			stream->Inject(app);
			return "";
		}
		case eqheadless::Command::kRaidRemove: {
			Raid *raid = me->GetRaid();
			if (!raid) {
				return "not in a raid";
			}
			const std::string name = cmd.raid_remove().name().empty() ? std::string(me->GetName()) : cmd.raid_remove().name();
			const uint32 idx = raid->GetPlayerIndex(name.c_str());
			if (idx >= MAX_RAID_MEMBERS) {
				return fmt::format("{} is not in our raid", name);
			}
			if (name != me->GetName() && !raid->IsLeader(me->GetName())) {
				return "only the raid's leader takes others out";
			}
			// RaidCommandDisband: leader_name is who goes, player_name who does it
			auto app = new EQApplicationPacket(OP_RaidInvite, sizeof(RaidGeneral_Struct));
			auto *rg = reinterpret_cast<RaidGeneral_Struct *>(app->pBuffer);
			rg->action = RaidCommandDisband;
			strn0cpy(rg->leader_name, raid->members[idx].member_name, sizeof(rg->leader_name));
			strn0cpy(rg->player_name, me->GetName(), sizeof(rg->player_name));
			stream->Inject(app);
			return "";
		}
		case eqheadless::Command::kRaidMove: {
			Raid *raid = me->GetRaid();
			if (!raid) {
				return "not in a raid";
			}
			if (!raid->IsLeader(me->GetName())) {
				return "only the raid's leader moves members between groups";
			}
			const std::string &name = cmd.raid_move().name();
			const uint32 idx = raid->GetPlayerIndex(name.c_str());
			if (idx >= MAX_RAID_MEMBERS) {
				return fmt::format("{} is not in our raid", name);
			}
			const uint32 group = cmd.raid_move().group() < MAX_RAID_GROUPS ? cmd.raid_move().group() : RAID_GROUPLESS;
			if (raid->members[idx].group_number == group) {
				return "";
			}
			if (group != RAID_GROUPLESS && raid->GroupCount(group) >= MAX_GROUP_MEMBERS) {
				return fmt::format("raid group {} is full", group + 1);
			}
			// RaidCommandMoveGroup: leader_name is who moves, parameter the group (12 and up: out of every group)
			auto app = new EQApplicationPacket(OP_RaidInvite, sizeof(RaidGeneral_Struct));
			auto *rg = reinterpret_cast<RaidGeneral_Struct *>(app->pBuffer);
			rg->action = RaidCommandMoveGroup;
			strn0cpy(rg->leader_name, raid->members[idx].member_name, sizeof(rg->leader_name));
			strn0cpy(rg->player_name, me->GetName(), sizeof(rg->player_name));
			rg->parameter = group;
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
		case OP_RezzRequest: {
			// a rez offered: the client's half is the yes or no box. Yes when it's one of our own bots, or
			// one of our group, and the corpse is here by us (summoned or dragged to us, or we're at it):
			// accepting puts us at the corpse, and one where we died can be beside what killed us
			Client *me = it->second.stream->GetClient();
			if (!me || app->size < sizeof(Resurrect_Struct)) {
				return;
			}
			auto rez = *reinterpret_cast<const Resurrect_Struct *>(app->pBuffer);
			Mob *rezzer = entity_list.GetMob(rez.rezzer_name);
			if (!rezzer) {
				rezzer = entity_list.GetBotByBotName(rez.rezzer_name);
			}
			const bool ours = rezzer && ((rezzer->IsBot() && rezzer->CastToBot()->GetBotOwner() == me) ||
										 (me->GetGroup() && me->GetGroup()->IsGroupMember(rezzer)));
			// ... or a bot of a guildmate's (the leader's partner, whose group went to the corpse and stands guard by it
			// while we come back to it: the puppeteer's pairs, 4 Nov). Taken from wherever we are, the corpse's zone or not:
			// the rezzer isn't in this zone then, so its owner's guild comes from the database
			bool partner = false;
			if (!ours && me->GuildID() != GUILD_NONE) {
				if (rezzer && rezzer->IsBot()) {
					Mob *owner = rezzer->CastToBot()->GetBotOwner();
					partner = owner && owner->IsClient() && owner != me && owner->CastToClient()->GuildID() == me->GuildID();
				} else if (!rezzer) {
					auto r = database.QueryDatabase(fmt::format(
						"SELECT m.guild_id FROM bot_data b JOIN guild_members m ON m.char_id = b.owner_id "
						"WHERE b.name = '{}' AND b.owner_id != {} LIMIT 1", Strings::Escape(rez.rezzer_name), me->CharacterID()));
					partner = r.Success() && r.RowCount() && Strings::ToUnsignedInt(r.begin()[0]) == me->GuildID();
				}
			}
			// ... or a GM's: a resurrection by a GM (#castspell of a rez on the corpse: the operator's own character, status 255) is accepted from anywhere
			bool gm_rezzer = rezzer && rezzer->IsClient() && rezzer->CastToClient()->GetGM();
			if (!partner && !ours && !rezzer) {  // (not in this zone: the GM's account status from the database)
				auto r = database.QueryDatabase(fmt::format(
					"SELECT a.status FROM character_data c JOIN account a ON a.id = c.account_id WHERE c.name = '{}' LIMIT 1", Strings::Escape(rez.rezzer_name)));
				gm_rezzer = r.Success() && r.RowCount() && Strings::ToInt(r.begin()[0]) >= AccountStatus::GMAdmin;
			}
			if (!partner && !ours && gm_rezzer) {
				rez.action = ResurrectionActions::Accept;
				LogHeadlessClient("[{}] rez from [{}] ([{}]) for [{}]: accepted (a GM's)", me->GetCleanName(), rez.rezzer_name, spells[rez.spellid].name, rez.corpse_name);
				auto answer = new EQApplicationPacket(OP_RezzAnswer, sizeof(Resurrect_Struct));
				memcpy(answer->pBuffer, &rez, sizeof(Resurrect_Struct));
				it->second.stream->Inject(answer);
				return;
			}
			const bool near = rez.zone_id == zone->GetZoneID() && rez.instance_id == zone->GetInstanceID() &&
				glm::length(glm::vec2(rez.x - me->GetX(), rez.y - me->GetY())) <= REZ_NEAR;
			rez.action = ((ours && near) || partner) ? ResurrectionActions::Accept : ResurrectionActions::Decline;
			LogHeadlessClient("[{}] rez from [{}] ([{}]) for [{}]: {}", me->GetCleanName(), rez.rezzer_name,
				spells[rez.spellid].name, rez.corpse_name, rez.action == ResurrectionActions::Accept ? (partner ? "accepted (a guildmate's bot)" : "accepted")
				: !ours ? "declined (not ours)" : "declined (the corpse isn't here by us)");
			auto answer = new EQApplicationPacket(OP_RezzAnswer, sizeof(Resurrect_Struct));
			memcpy(answer->pBuffer, &rez, sizeof(Resurrect_Struct));
			it->second.stream->Inject(answer);
			return;
		}
		case OP_RaidUpdate: {
			// a raid invite (RaidGeneral_Struct, action 20; raidSetLeader, also 20, is a larger packet): the client's half is the
			// accept box. Accepted from a guildmate (the guild's raids: one group per leader, BL-008/009) or a GM; anyone else's
			// is left unanswered, as a player ignoring the box
			Client *me = it->second.stream->GetClient();
			if (!me || app->size != sizeof(RaidGeneral_Struct)) {
				return;
			}
			const auto *rg = reinterpret_cast<const RaidGeneral_Struct *>(app->pBuffer);
			if (rg->action != 20) {
				return;
			}
			const std::string inviter_name(rg->leader_name, strnlen(rg->leader_name, sizeof(rg->leader_name)));
			Client *inviter = entity_list.GetClientByName(inviter_name.c_str());
			const bool guildmate = inviter && me->GuildID() != GUILD_NONE && inviter->GuildID() == me->GuildID();
			const bool gm = inviter && inviter->GetGM();
			if (!guildmate && !gm) {
				LogHeadlessClient("[{}] raid invite from [{}]: left unanswered (not a guildmate)", me->GetCleanName(), inviter_name);
				return;
			}
			LogHeadlessClient("[{}] raid invite from [{}]: accepted ({})", me->GetCleanName(), inviter_name, guildmate ? "a guildmate" : "a GM");
			// RaidCommandAcceptInvite: player_name is who invited, leader_name who accepts
			auto answer = new EQApplicationPacket(OP_RaidInvite, sizeof(RaidGeneral_Struct));
			auto *ra = reinterpret_cast<RaidGeneral_Struct *>(answer->pBuffer);
			ra->action = RaidCommandAcceptInvite;
			strn0cpy(ra->player_name, inviter->GetName(), sizeof(ra->player_name));
			strn0cpy(ra->leader_name, me->GetName(), sizeof(ra->leader_name));
			it->second.stream->Inject(answer);
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

			// the client's answer. Asked while we're still entering the zone (a zone's
			// event_enter_zone moving us on, e.g. the Commonlands to East Commonlands), the server
			// would drop it, leaving its summon pending: wait until we're in, as a client finishes
			// loading the zone before it can zone again
			it->second.zone_request         = *rq;
			it->second.zone_request_pending = true;
			it->second.zone_request_tries   = 0;
			if (!AnswerZoneRequest(session_id)) {
				AnswerZoneRequestSoon(session_id, 250, 120); // once we're in (30 seconds at most)
			}
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
				Session &s = it->second;
				if (s.zone_request_pending && zc->success == ZoningMessage::ZoneNotReady && s.zone_request_tries < 6) {
					// the zone the server sent us to isn't up yet (it's booting): a client is moved
					// again later; try again in 10 seconds, six times
					s.zone_request_tries++;
					AnswerZoneRequestSoon(session_id, 10000, 1);
					return;
				}
				s.zone_request_pending = false;
				// ZoneNotReady: world had no zone process free to start it in (not the zone line's fault)
				SendZoneEvent(session_id, eqheadless::ZONE_REFUSED, zc->zoneID,
					zc->success == ZoningMessage::ZoneNotReady ? std::string("the zone couldn't be started (no free zone process)")
					                                           : fmt::format("the server refused the zone change ({})", zc->success));
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
				if (npc->CastToNPC()->GetParcelMerchant() && RuleB(Parcel, EnableParcelMerchants)) {
					// what's waiting for us, from the database (the server loads its own copy after
					// this packet, when it sends the client the parcel tab)
					ev->set_parcel_merchant(true);
					for (const auto &p : CharacterParcelsRepository::GetWhere(
						database, fmt::format("`char_id` = {} ORDER BY `slot_id`", me->CharacterID()))) {
						auto       *out  = ev->add_parcels();
						const auto *item = database.GetItem(p.item_id);
						out->set_slot(p.slot_id);
						out->set_item_id(p.item_id);
						out->set_name(p.item_id == PARCEL_MONEY_ITEM_ID ? "Money" : item ? item->Name : fmt::format("item {}", p.item_id));
						out->set_quantity(p.quantity);
						out->set_from(p.from_name);
						out->set_note(p.note);
						out->set_sent(static_cast<int64>(p.sent_date));
					}
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
			session.loot_queue.clear();
			session.loot_names.clear();
			if (e && e->IsCorpse()) {
				// in the order a player loots their own corpse (the operator, 5 Nov): what was worn first (it goes straight back on, into the
				// empty worn slots, and takes no room), then the bags (each needs an empty top-level slot, and brings what's in it), then the
				// rest (into the bags, now they're there). Last, anything lore we already have: the server refuses it and ends the loot there
				// (ResetLooter), so put first it cost everything after it (Grakk, eq3: a Rhino Beetle Horn kept three worn items on its
				// corpse for 210 tries)
				const auto &all = e->CastToCorpse()->GetLootItems();
				auto lore_held = [&](const LootItem *item, const EQ::ItemData *data) {
					if (data && me->CheckLoreConflict(data)) {
						return true;
					}
					if (!data || !data->IsClassBag() || item->equip_slot < EQ::invslot::GENERAL_BEGIN || item->equip_slot > EQ::invslot::GENERAL_END) {
						return false;
					}
					for (const auto *in : all) {  // (a bag's things come with it: one lore one we have and the bag is refused)
						if (in && EQ::InventoryProfile::CalcSlotId(in->equip_slot) == item->equip_slot && in != item) {
							const auto *d = database.GetItem(in->item_id);
							if (d && me->CheckLoreConflict(d)) {
								return true;
							}
						}
					}
					return false;
				};
				std::vector<std::tuple<int, int16, std::string>> order;
				for (const auto *item : all) {
					if (!item || item->lootslot < EQ::invslot::CORPSE_BEGIN || item->lootslot > EQ::invslot::CORPSE_END) {
						continue;
					}
					const auto *data = database.GetItem(item->item_id);
					const bool worn = item->equip_slot >= EQ::invslot::EQUIPMENT_BEGIN && item->equip_slot <= EQ::invslot::EQUIPMENT_END;
					const int rank = lore_held(item, data) ? 3 : worn ? 0 : (data && data->IsClassBag()) ? 1 : 2;
					order.emplace_back(rank, item->lootslot, data ? data->Name : fmt::format("item {}", item->item_id));
				}
				std::stable_sort(order.begin(), order.end(), [](const auto &a, const auto &b) { return std::get<0>(a) < std::get<0>(b); });
				for (const auto &[rank, slot, name] : order) {
					if (session.loot_peek) {
						break;
					}
					// (LootCommand.only: each name taken once. Filtered by the flag, not by the list's being non-empty: the list is used up as
					// it's matched, and the server's echo of the corpse came again for the same loot, found it empty and took everything: Ragnvald
					// took Durgrim's Gold Plated Koshigatana with his own Orb of Tishan, Lord Nagafen's loot, eq1, 15 Nov)
					if (session.loot_filtered) {
						auto want = std::find(session.loot_only.begin(), session.loot_only.end(), name);
						if (want == session.loot_only.end()) {
							continue;
						}
						session.loot_only.erase(want);
					}
					session.loot_queue.emplace_back(slot, name);
				}
				session.loot_only.clear();
			}
			// one item every LOOT_ITEM_MS, then close the corpse and say what we got (the event
			// waits for that, so the SDK sees the corpse as it is afterwards)
			auto event = std::make_shared<eqheadless::ServerFrame>(frame);
			frame.Clear();
			session.loot_timer = std::make_unique<EQ::Timer>(
				LOOT_ITEM_MS, true, [this, session_id, id, event](EQ::Timer *t) {
					auto s = m_sessions.find(session_id);
					if (s == m_sessions.end() || !s->second.stream || !s->second.stream->GetClient()) {
						return;
					}
					Session &ss = s->second;
					Client  *c  = ss.stream->GetClient();
					if (!ss.loot_queue.empty() && ss.loot_corpse == id) {
						auto [slot, name] = ss.loot_queue.front();
						ss.loot_queue.pop_front();
						auto li = new EQApplicationPacket(OP_LootItem, sizeof(LootingItem_Struct));
						auto *l = reinterpret_cast<LootingItem_Struct *>(li->pBuffer);
						l->lootee    = id;
						l->looter    = c->GetID();
						l->slot_id   = slot;
						l->auto_loot = 1;
						ss.stream->Inject(li);
						ss.loot_names.push_back(name);
						return;
					}
					// what's still on it: a loot with no room for it leaves it there (and says so only
					// in a message), so what we asked for isn't what we got
					std::vector<std::string> left;
					Entity *ce = entity_list.GetID(id);
					if (ce && ce->IsCorpse()) {
						for (const auto *item : ce->CastToCorpse()->GetLootItems()) {
							if (item && item->lootslot >= EQ::invslot::CORPSE_BEGIN && item->lootslot <= EQ::invslot::CORPSE_END) {
								const auto *data = database.GetItem(item->item_id);
								left.push_back(data ? data->Name : fmt::format("item {}", item->item_id));
							}
						}
					}
					ss.stream->Inject(Uint32Packet(OP_EndLootRequest, id));
					auto *ev = event->mutable_event()->mutable_loot();
					for (const auto &n : ss.loot_names) {
						auto still = std::find(left.begin(), left.end(), n);
						if (still != left.end()) {
							ev->add_left(n);
							left.erase(still);
						}
						else {
							ev->add_items(n);
						}
					}
					for (const auto &n : left) {  // (and what wasn't asked for: a peek's, an only's)
						ev->add_left(n);
					}
					ss.loot_corpse = 0;
					ss.loot_peek   = false;
					ss.loot_queue.clear();
					ss.loot_names.clear();
					t->Stop(); // (the next loot replaces it: deleting it here would delete what's running us)
					SendFrame(session_id, *event);
				}
			);
			return;
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
			// no killer: what hurt us that wasn't a mob ("Drowning"), if something did just now
			death->set_killer(killer ? std::string(killer->GetCleanName())
			                         : (it->second.mover ? it->second.mover->RecentEnvironmentDamage() : std::string()));
			death->set_corpse_id(d->corpseid);
			death->set_spell_id(IsValidSpell(d->spell_id) ? d->spell_id : 0);
			death->set_damage(d->damage);
			it->second.loot_corpse = 0;
			break;
		}
		case OP_SkillUpdate: {
			// the client turns this into "You have become better at <skill>! (<value>)"
			if (app->size < sizeof(SkillUpdate_Struct)) {
				return;
			}
			auto *su    = reinterpret_cast<const SkillUpdate_Struct *>(app->pBuffer);
			auto *skill = frame.mutable_event()->mutable_skill();
			skill->set_skill_id(su->skillId);
			skill->set_value(su->value);
			skill->set_name(su->skillId >= 100 ? EQ::constants::GetLanguageName(static_cast<uint8>(su->skillId - 100))
				: EQ::skills::GetSkillName(static_cast<EQ::skills::SkillType>(su->skillId)));
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
		case OP_Damage: {
			// the client makes its own combat text from this packet ("You slash a rat for 12 points of damage"); a headless one makes it here, for what its
			// group does and takes (not the spells', which arrive as messages)
			if (app->size < sizeof(CombatDamage_Struct)) {
				return;
			}
			auto *cd = reinterpret_cast<const CombatDamage_Struct *>(app->pBuffer);
			Client *me = it->second.stream->GetClient();
			Mob *src = entity_list.GetMob(cd->source), *tgt = entity_list.GetMob(cd->target);
			if (!me || !src || !tgt || cd->type == 231) {
				return;
			}
			auto ours = [&](Mob *m) {
				Mob *o = m->GetOwnerOrSelf();
				return o == me || (me->GetGroup() && me->GetGroup()->IsGroupMember(o)) || (o->IsBot() && o->CastToBot()->GetBotOwner() == me);
			};
			if (!ours(src) && !ours(tgt)) {
				return;
			}
			static const char *verbs[] = {"slashes", "crushes", "pierces", "bashes", "hits", "hits", "hits", "pierces", "kicks", "bashes", "hits", "hits", "hits", "hits", "hits"};
			const int32 dmg = static_cast<int32>(cd->damage);
			const char *a = src->GetCleanName(), *b = tgt->GetCleanName();
			auto *msg = frame.mutable_event()->mutable_message();
			msg->set_type(src->GetOwnerOrSelf() == me || ours(src) ? 279 : 280);
			if (dmg > 0) {
				msg->set_text(fmt::format("{} {} {} for {} points of damage.", a, cd->type < 15 ? verbs[cd->type] : "hits", b, dmg));
			} else {
				msg->set_text(fmt::format("{} tries to hit {}, but misses!", a, b));
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
				eqheadless::MOVE_NO_PATH, eqheadless::MOVE_ZONE_LINE, eqheadless::MOVE_LOCKED,
			};
			eqheadless::ServerFrame frame;
			auto *move = frame.mutable_event()->mutable_move();
			move->set_status(statuses[static_cast<int>(result)]);
			move->set_x(where.x);
			move->set_y(where.y);
			move->set_z(where.z);
			auto s = m_sessions.find(session_id);
			std::string why;
			if (s != m_sessions.end() && s->second.mover) {
				for (const auto &n : s->second.mover->Notes()) {
					auto *nav = move->add_nav();
					nav->set_kind(n.kind);
					nav->set_x(n.at.x);
					nav->set_y(n.at.y);
					nav->set_z(n.at.z);
					nav->set_to_x(n.to.x);
					nav->set_to_y(n.to.y);
					nav->set_to_z(n.to.z);
					nav->set_closest_x(n.closest.x);
					nav->set_closest_y(n.closest.y);
					nav->set_closest_z(n.closest.z);
					if (n.kind == "no_mesh_here" || n.kind == "no_mesh_there" || n.kind == "not_connected" || n.kind == "stuck" ||
						n.kind == "locked" || n.kind == "door_wont_open" || n.kind == "too_deep" || n.kind == "cant_jump") {
						why = n.kind; // (why it failed, not a gap it went round)
					}
				}
			}
			SendFrame(session_id, frame);

			// ZoneTo: we're at (or in) the zone line; ask to go through it, as the client does
			if (s != m_sessions.end() && s->second.zone_line_to) {
				if (s->second.zone_line_boat) {
					// a boat's box: the quest script moves us, as we step in; there's no zone line to ask for
					if (result != HeadlessMover::Result::Arrived && result != HeadlessMover::Result::ZoneLine) {
						SendZoneEvent(session_id, eqheadless::ZONE_REFUSED, s->second.zone_line_to, "couldn't reach the boat");
					}
				}
				else if (result == HeadlessMover::Result::ZoneLine || result == HeadlessMover::Result::Arrived) {
					RequestZoneLine(session_id, s->second, where);
				}
				else {
					SendZoneEvent(session_id, eqheadless::ZONE_REFUSED, s->second.zone_line_to,
						why.empty() ? "couldn't reach the zone line" : "couldn't reach the zone line (" + why + ")");
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

// the client's answer to OP_RequestClientZoneChange (the session's zone_request): OP_ZoneChange. The
// server takes it only once we're in the zone, so false (and nothing sent) until then
bool HeadlessClientManager::AnswerZoneRequest(const std::string &session_id)
{
	auto s = m_sessions.find(session_id);
	if (s == m_sessions.end() || !s->second.stream || !s->second.stream->GetClient()) {
		return true; // gone: nothing to do
	}
	Client *c = s->second.stream->GetClient();
	if (!c->Connected()) {
		return false;
	}
	const RequestClientZoneChange_Struct &req = s->second.zone_request;
	auto zc_app = new EQApplicationPacket(OP_ZoneChange, sizeof(ZoneChange_Struct));
	auto *zc    = reinterpret_cast<ZoneChange_Struct *>(zc_app->pBuffer);
	strn0cpy(zc->char_name, c->GetName(), sizeof(zc->char_name));
	zc->zoneID      = req.zone_id;
	zc->instanceID  = req.instance_id;
	zc->x           = req.x;
	zc->y           = req.y;
	zc->z           = req.z;
	zc->zone_reason = req.type; // the client echoes `type` back
	zc->success     = 0;
	s->second.stream->Inject(zc_app);
	return true;
}

void HeadlessClientManager::AnswerZoneRequestSoon(const std::string &session_id, uint32 every_ms, int max_tries)
{
	auto it = m_sessions.find(session_id);
	if (it == m_sessions.end()) {
		return;
	}
	auto tries = std::make_shared<int>(0);
	it->second.zone_request_timer = std::make_unique<EQ::Timer>(
		every_ms, true, [this, session_id, tries, max_tries](EQ::Timer *t) {
			if (AnswerZoneRequest(session_id) || ++*tries >= max_tries) {
				t->Stop(); // (the next request replaces the timer: deleting it here would delete what's running us)
			}
		}
	);
}

void HeadlessClientManager::OnStreamDestroyed(const std::string &session_id, bool was_spawned)
{
	auto it = m_sessions.find(session_id);
	if (it != m_sessions.end()) {
		Unfreeze(it->second); // (kicked, killed or gone some other way while held: what it froze is given back)
	}
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

void HeadlessClientManager::FillSpawn(eqheadless::Spawn *out, Mob *mob, Client *me)
{
	out->set_id(mob->GetID());
	out->set_name(mob->GetCleanName());
	out->set_type(SpawnTypeOf(mob));
	out->set_level(mob->GetLevel());
	out->set_class_id(mob->GetClass());
	out->set_race_id(mob->GetRace());
	out->set_gender(mob->GetGender());
	out->set_primary(mob->GetEquipmentMaterial(EQ::textures::weaponPrimary));
	out->set_secondary(mob->GetEquipmentMaterial(EQ::textures::weaponSecondary));
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
	out->set_mana_pct(mob->GetMaxMana() > 0 ? static_cast<uint32>(std::lround(mob->GetManaRatio())) : 0);
	out->set_casting_spell_id(mob->IsCasting() ? mob->CastingSpellID() : 0);
	out->set_status(SpawnStatusOf(mob));

	// what it does about us, by the rules of Mob::CheckWillAggro (its faction toward the character, the level rules), without the parts that change at once
	// (our invisibility, line of sight, the quarter chance of a threatening look): the character works those out
	if (me && mob->IsNPC() && !mob->GetOwner() && mob->GetAggroRange() > 0.0f) {
		out->set_aggro_range(mob->GetAggroRange());
		out->set_assist_range(mob->GetAssistRange());
		uint32 aggro = 0;
		const FACTION_VALUE faction = me->GetReverseFactionCon(mob);
		if (faction == FACTION_SCOWLS) {
			aggro |= 1;
		}
		else if (faction == FACTION_THREATENINGLY) {
			aggro |= 2;
		}
		const bool grey_aggro = mob->AlwaysAggro() ||
			(RuleB(Aggro, UseLevelAggro) ? (mob->GetLevel() >= RuleI(Aggro, MinAggroLevel) || mob->GetBodyType() == BodyType::Undead)
			                             : ((RuleB(Aggro, UndeadAlwaysAggro) && mob->GetBodyType() == BodyType::Undead) || mob->GetINT() <= RuleI(Aggro, IntAggroThreshold)));
		if (grey_aggro) {
			aggro |= 4;
		}
		out->set_aggro(aggro);
	}
	else if (mob->IsNPC() && !mob->GetOwner()) {
		out->set_assist_range(mob->GetAssistRange());
	}
	if (ShowsBuffs(mob, me)) {
		if (Buffs_Struct *mb = mob->GetBuffs()) {
			for (int i = 0; i < mob->GetMaxTotalSlots(); ++i) {
				if (!IsValidSpell(mb[i].spellid)) {
					continue;
				}
				auto *buff = out->add_buffs();
				buff->set_spell_id(mb[i].spellid);
				buff->set_name(SpellName(mb[i].spellid));
				buff->set_ticks(mb[i].ticsremaining);
				buff->set_slot(i);
			}
		}
	}
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

	// a player turns to what it fights. A headless one faced only where it last walked, and a tank stood with its back to the boss: hit from
	// behind, it can't parry, block, riposte or dodge (attack.cpp, InFront), and it was drawn swinging at nothing (Brunhild against Lord
	// Nagafen, eq1, 14 Nov: 178 degrees off for minutes at a time)
	if (session.mover && me->AutoAttackEnabled() && !me->IsFeared() && !me->IsStunned() && !me->IsMezzed() && !session.mover->Moving()) {
		Mob *t = me->GetTarget();
		if (t && t != me && !t->IsCorpse() && t->GetHP() > 0) {
			const float want = me->CalculateHeadingToTarget(t->GetX(), t->GetY());
			float off = std::fabs(want - me->GetHeading());
			if (off > 256.0f) {
				off = 512.0f - off;
			}
			if (off > 16.0f) { // (about 11 degrees)
				session.mover->Face(want);
			}
		}
	}

	eqheadless::ServerFrame frame;
	eqheadless::Self       *self = snapshot ? frame.mutable_snapshot()->mutable_me() : frame.mutable_delta()->mutable_me();

	self->set_spawn_id(me->GetID());
	self->set_name(me->GetCleanName());
	self->set_level(me->GetLevel());
	self->set_class_id(me->GetClass());
	self->set_race_id(me->GetRace());
	self->set_gender(me->GetGender());
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
			buff->set_slot(i);
		}
	}
	for (auto &[id, mob] : entity_list.GetMobList()) {
		if (mob && mob->IsNPC() && !mob->IsCorpse() && mob->GetID() != 0 && mob->GetHP() > 0 &&
			mob->CastToNPC()->IsOnHatelist(me)) { // not one that's dying
			self->add_haters(id);
		}
	}
	self->set_race_bit(GetPlayerRaceBit(me->GetBaseRace()));
	{
		auto *rs = self->mutable_resists();
		rs->set_magic(me->GetMR());
		rs->set_cold(me->GetCR());
		rs->set_fire(me->GetFR());
		rs->set_poison(me->GetPR());
		rs->set_disease(me->GetDR());
		// without the buffs (a raid's readiness: a resist buff that happened to be on a tank counted, with the raid's own buffs on top,
		// and the check flipped as it wore off, eq1's Lady Vox, 15 Nov)
		auto *gr = self->mutable_gear_resists();
		const StatBonuses *sb = me->GetSpellBonusesPtr();
		gr->set_magic(me->GetMR() - sb->MR);
		gr->set_cold(me->GetCR() - sb->CR);
		gr->set_fire(me->GetFR() - sb->FR);
		gr->set_poison(me->GetPR() - sb->PR);
		gr->set_disease(me->GetDR() - sb->DR);
	}
	{
		const double here = me->GetEXPForLevel(me->GetLevel()), next = me->GetEXPForLevel(me->GetLevel() + 1);
		const double pct  = next > here ? (me->GetEXP() - here) * 100.0 / (next - here) : 0.0;
		self->set_exp_pct(static_cast<float>(std::clamp(pct, 0.0, 100.0)));
		const uint32 aa_need = me->GetRequiredAAExperience();
		self->set_aa_exp_pct(aa_need ? std::min(100.0f, me->GetAAXP() * 100.0f / aa_need) : 0.0f);
		self->set_aa_points(me->GetAAPoints());
		self->set_aa_split(me->GetAAPercent());
	}
	self->set_run_speed(HeadlessMover::RunSpeed(me));
	self->set_max_air(HeadlessMover::MaxAir(me));
	self->set_air(session.mover ? session.mover->Air() : HeadlessMover::MaxAir(me));
	if (zone) {
		TimeOfDay_Struct tod{};
		zone->zone_time.GetCurrentEQTimeOfDay(time(nullptr), &tod);
		self->set_game_hour(tod.hour);
	}
	for (const uint32 disc : me->GetPP().disciplines.values) {
		if (disc && IsValidSpell(disc)) {
			self->add_disciplines(disc);
		}
	}
	self->set_weight(me->CalcCurrentWeight() / 10);
	self->set_capacity(me->GetSTR());
	self->set_platinum(me->GetPP().platinum);
	self->set_gold(me->GetPP().gold);
	self->set_silver(me->GetPP().silver);
	self->set_copper(me->GetPP().copper);
	self->set_bank_copper(static_cast<uint64>(me->GetPP().platinum_bank) * 1000 + static_cast<uint64>(me->GetPP().gold_bank) * 100 +
		static_cast<uint64>(me->GetPP().silver_bank) * 10 + me->GetPP().copper_bank);
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
			if (mob && mob != me) {
				if (Buffs_Struct *mb = mob->GetBuffs()) {
					for (int b = 0; b < mob->GetMaxTotalSlots(); ++b) {
						if (!IsValidSpell(mb[b].spellid)) {
							continue;
						}
						auto *buff = gm->add_buffs();
						buff->set_spell_id(mb[b].spellid);
						buff->set_name(SpellName(mb[b].spellid));
						buff->set_ticks(mb[b].ticsremaining);
						buff->set_slot(b);
					}
				}
			}
		}
	}
	if (Raid *raid = me->GetRaid()) {
		for (const auto &rm : raid->members) {
			if (!rm.member_name[0]) {
				continue;
			}
			auto *r   = self->add_raid();
			Mob  *mob = rm.member ? static_cast<Mob *>(rm.member) : (rm.is_bot ? static_cast<Mob *>(entity_list.GetBotByBotName(rm.member_name)) : nullptr);
			r->set_name(rm.member_name);
			r->set_spawn_id(mob ? mob->GetID() : 0);
			r->set_group(rm.group_number < MAX_RAID_GROUPS ? rm.group_number : RAID_GROUPLESS);
			r->set_group_leader(rm.is_group_leader);
			r->set_raid_leader(rm.is_raid_leader);
			r->set_bot(rm.is_bot);
			r->set_class_id(rm._class);
			r->set_level(rm.level);
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
		spell->set_slot(i);
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
		FillItem(item, inst, me);
		item->set_slot(slot);
		item->set_charges(inst->GetCharges());
	};
	auto add_with_bag = [&](int16 slot) {
		const EQ::ItemInstance *inst = me->GetInv().GetItem(slot);
		if (!inst || !inst->GetItem()) {
			return;
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
	};
	for (int16 slot = EQ::invslot::POSSESSIONS_BEGIN; slot <= EQ::invslot::POSSESSIONS_END; ++slot) {
		add_with_bag(slot);
	}
	for (int16 slot = EQ::invslot::BANK_BEGIN; slot <= EQ::invslot::BANK_END; ++slot) {
		add_with_bag(slot);
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
		auto *brs = bg->mutable_resists();
		brs->set_magic(b->GetMR());
		brs->set_cold(b->GetCR());
		brs->set_fire(b->GetFR());
		brs->set_poison(b->GetPR());
		brs->set_disease(b->GetDR());
		auto *bgr = bg->mutable_gear_resists();
		const StatBonuses *bsb = b->GetSpellBonusesPtr();
		bgr->set_magic(b->GetMR() - bsb->MR);
		bgr->set_cold(b->GetCR() - bsb->CR);
		bgr->set_fire(b->GetFR() - bsb->FR);
		bgr->set_poison(b->GetPR() - bsb->PR);
		bgr->set_disease(b->GetDR() - bsb->DR);
		if (Buffs_Struct *bb = b->GetBuffs()) {
			for (int i = 0; i < b->GetMaxTotalSlots(); ++i) {
				if (!IsValidSpell(bb[i].spellid)) {
					continue;
				}
				auto *buff = bg->add_buffs();
				buff->set_spell_id(bb[i].spellid);
				buff->set_name(SpellName(bb[i].spellid));
				buff->set_ticks(bb[i].ticsremaining);
				buff->set_slot(i);
			}
		}
		for (int16 slot = EQ::invslot::EQUIPMENT_BEGIN; slot <= EQ::invslot::EQUIPMENT_END; ++slot) {
			const EQ::ItemInstance *inst = b->GetBotItem(slot);
			if (inst && inst->GetItem()) {
				auto *it = bg->add_equipment();
				FillItem(it, inst, b);
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

	// spawns: every mob in the zone except ourselves, and corpses (EQEmu keeps those in their own list)
	auto publish_spawn = [&](uint32 id, Mob *mob) {
		// a dying NPC stays in the mob list for a moment with its id already 0, before its corpse
		// takes over: the client never sees it, so neither do we (it would be a spawn with id 0)
		if (!mob || mob == me || mob->GetID() == 0) {
			return;
		}

		present.insert(id);

		const uint32 hp_pct   = static_cast<uint32>(std::lround(std::max(0.0f, mob->GetHPRatio())));
		const uint32 mana_pct = mob->GetMaxMana() > 0 ? static_cast<uint32>(std::lround(mob->GetManaRatio())) : 0;
		const uint32 casting  = mob->IsCasting() ? mob->CastingSpellID() : 0;
		const uint32 status   = SpawnStatusOf(mob);
		const uint64 buff_sig = BuffSigOf(mob, me);
		auto seen = session.seen.find(id);
		const bool is_new = seen == session.seen.end();
		if (!is_new) {
			const SeenSpawn &s = seen->second;
			if (std::fabs(s.x - mob->GetX()) < MOVE_EPSILON && std::fabs(s.y - mob->GetY()) < MOVE_EPSILON &&
				std::fabs(s.z - mob->GetZ()) < MOVE_EPSILON && s.heading == mob->GetHeading() &&
				s.hp_pct == hp_pct && s.level == mob->GetLevel() && s.name == mob->GetCleanName() &&
				s.target_id == (mob->GetTarget() ? mob->GetTarget()->GetID() : 0) && s.mana_pct == mana_pct &&
				s.casting_spell_id == casting && s.status == status && s.buff_sig == buff_sig) {
				return;
			}
		}

		FillSpawn(snapshot ? frame.mutable_snapshot()->add_spawns() : frame.mutable_delta()->add_spawns_changed(), mob, me);
		session.seen[id] = SeenSpawn{
			mob->GetX(), mob->GetY(), mob->GetZ(), mob->GetHeading(), hp_pct, mob->GetLevel(), mob->GetCleanName(),
			mob->GetTarget() ? mob->GetTarget()->GetID() : 0u, mana_pct, casting, status, buff_sig
		};
		changed = true;
	};
	for (auto &[id, mob] : entity_list.GetMobList()) {
		publish_spawn(id, mob);
	}
	for (auto &[id, corpse] : entity_list.GetCorpseList()) {
		publish_spawn(id, corpse);
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

void HeadlessClientManager::FillItem(eqheadless::Item *out, const EQ::ItemInstance *inst, Mob *me)
{
	// (an item as it is: its own data, and what is in its sockets)
	const EQ::ItemData *item = inst->GetItem();
	FillItem(out, item, me);
	bool sockets = false;
	for (int i = 0; i < EQ::invaug::SOCKET_COUNT; ++i) {
		sockets = sockets || item->AugSlotType[i] != 0;
	}
	if (!sockets) {
		return;
	}
	bool any = false;
	for (int i = 0; i < EQ::invaug::SOCKET_COUNT; ++i) {
		out->add_aug_sockets(item->AugSlotType[i]);
		const uint32 id = inst->GetAugmentItemID(i);
		any = any || id != 0;
		out->add_augs(id);
	}
	if (!any) {
		out->clear_augs();
	}
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
	out->set_bag_wr(item->BagWR);
	out->set_bag_type(item->BagType);
	out->set_item_type(item->ItemType);
	out->set_usable(me && item->IsEquipable(me->GetBaseRace(), me->GetClass()) && item->ReqLevel <= me->GetLevel());
	out->set_classes(item->Classes);
	out->set_races(item->Races);
	if (item->ItemType == EQ::item::ItemTypeAugmentation) {
		out->set_aug_type(item->AugType);
		out->set_aug_restrict(item->AugRestrict);
		out->set_aug_distiller(item->AugDistiller);
	}
	if (item->ItemType == EQ::item::ItemTypeSpell && IsValidSpell(item->Scroll.Effect)) {
		out->set_scroll_spell(item->Scroll.Effect);
		if (me && IsPlayerClass(me->GetClass())) {
			const uint8 level = spells[item->Scroll.Effect].classes[me->GetClass() - 1];
			out->set_scroll_level(level < 255 ? level : 0);
		}
	}
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
