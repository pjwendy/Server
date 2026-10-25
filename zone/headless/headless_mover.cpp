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
#include "zone/headless/headless_mover.h"

#include "common/eq_constants.h"
#include "common/eq_packet_structs.h"
#include "common/eqemu_logsys.h"
#include "common/misc_functions.h"
#include "common/rulesys.h"
#include "zone/map.h"
#include "zone/client.h"
#include "zone/doors.h"
#include "zone/entity.h"
#include "zone/headless/null_stream.h"
#include "zone/pathfinder_interface.h"
#include "zone/water_map.h"
#include "zone/zone.h"

#include "glm/geometric.hpp"

#include "common/json/json.hpp"
#include "common/path_manager.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>

extern Zone       *zone;
extern EntityList entity_list;

static float FloorBelow(const glm::vec3 &p); // the floor under a point (below)

namespace {
	constexpr uint64 TICK_MS        = 100;
	// mob movement uses frame_time * speed * 0.4 * 1.45 (mob_movement_manager.cpp); match it
	constexpr float  SPEED_TO_UNITS = 0.4f * 1.45f;
	constexpr float  ARRIVE_EPSILON = 0.5f;
	constexpr uint64 STUCK_MS       = 3000;
	constexpr float  STUCK_DISTANCE = 1.0f;
	// doors: how far ahead to look, how far up/down they can be, how long to wait for one
	constexpr float  DOOR_LOOKAHEAD    = 15.0f;
	constexpr float  DOOR_HEIGHT_RANGE = 20.0f;
	constexpr uint64 DOOR_WAIT_MS      = 3000;
	// a partial route counts if it ends this close to where we were going (mobs stand a little
	// off the navmesh)
	constexpr float  PARTIAL_OK        = 20.0f;
	constexpr float  PARTIAL_OK_Z      = 25.0f;
	// a partial route that isn't there yet is still walked if it gets this much closer (the search
	// runs out of nodes on long routes; FindRoute keeps 1024 polygons), then planned again from its
	// end, up to PARTIAL_LEGS times. Not when it ends beside the destination but far above or below
	// it: that's a cliff top over the target, not the way there.
	constexpr float  PARTIAL_PROGRESS  = 50.0f;
	constexpr float  CLIFF_NEAR        = 120.0f;
	constexpr float  CLIFF_DZ          = 40.0f;
	constexpr int    PARTIAL_LEGS      = 12;
	// when zoning, walk straight the rest of the way if the zone's map shows a way: floor all
	// along, no steps steeper than a stair, nothing in the way at body height (a tunnel or gateway
	// the navmesh left out)
	constexpr float  ZONE_GAP          = 800.0f;
	constexpr float  ZONE_GAP_DZ       = 120.0f;
	constexpr float  STRAIGHT_STEP     = 8.0f;
	constexpr float  STRAIGHT_RISE     = 8.0f;   // up: a stair
	constexpr float  STRAIGHT_DROP     = 40.0f;  // down: a ledge a player just steps off
	constexpr float  BODY_HEIGHT       = 4.0f;
	// standing where the navmesh has a hole (FindRoute looks 5 units across for our polygon): look
	// for it in rings OFF_MESH_STEP apart out to OFF_MESH_MAX
	// breath (the client's, RoF2 eqgame.exe: its breath routine at 0x00584ac0, maximum air at 0x00581da0,
	// "can I breathe" at 0x00448b00)
	constexpr uint64 BREATH_MS           = 1000;    // it runs once a second (when over 1000 ms have passed)
	constexpr double DROWN_SHARE         = 0.125;   // drowning: this share of maximum hit points a second
	constexpr int    AIR_FLOOR           = 10;      // maximum air: (Stamina - 30) x lungs / 100, at least this ...
	constexpr int    LUNGS_DEFAULT       = 100;     // ... and at most the lungs: spell effect 246, else this
	constexpr float  HEAD_SHARE          = 0.75f;   // the head is this share of the character's size above it
	constexpr float  SURFACE_UP          = 2000.0f; // a surface further up than this isn't one to swim to (Dagnor's Cauldron's lake is 350 deep)
	// zones where being under water doesn't take air (the client's list: Erudin, the Southern Desert of Ro,
	// Kerra Isle), and two with an air supply of their own (the Sleeper's Tomb, the Catacombs of Dranik)
	constexpr uint32 BREATHE_ANYWAY[]    = {24, 35, 74};
	constexpr uint32 SLEEPER_ZONE = 128, SLEEPER_AIR = 127, DRANIK_CATACOMBS_ZONE = 330, DRANIK_AIR = 256;
	// the mover takes the links added for headless characters (PathingHeadless) as well as the zone's own
	constexpr int    MOVER_FLAGS         = PathingNotDisabled | PathingHeadless;
	// a route's step is a swim link's when its ends are this near the link's
	constexpr float  SWIM_MATCH          = 3.0f;
	constexpr float  OFF_MESH_STEP       = 10.0f;
	constexpr float  OFF_MESH_MAX        = 60.0f;
	constexpr int    OFF_MESH_DIRECTIONS = 12;

	float Flat(const glm::vec3 &a, const glm::vec3 &b)
	{
		return glm::length(glm::vec3(a.x - b.x, a.y - b.y, 0.0f));
	}
	// lifts: platform door type, how long the platform takes to come when called, how fast it
	// rides, how near the platform's centre we stand, and when a lift is worth it over walking
	constexpr uint8  LIFT_OPEN_TYPE    = 59;
	constexpr uint64 LIFT_CALL_MS      = 4000;
	constexpr float  LIFT_SPEED        = 12.0f;   // units per second
	constexpr float  PLATFORM_RADIUS   = 12.0f;
	constexpr float  LIFT_MIN_CLIMB    = 30.0f;   // floors at least this far apart
	constexpr float  LIFT_SAVING       = 0.75f;   // ride when it's under 3/4 of the walk (or no walk)
	constexpr float  LIFT_RIDE_COST    = 60.0f;   // a ride "costs" this many units of walking

	uint64 NowMs()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()
		).count();
	}
}

HeadlessMover::HeadlessMover(NullStream *stream, DoneCallback on_done)
	: m_stream(stream), m_on_done(std::move(on_done))
{
	m_breath_timer = std::make_unique<EQ::Timer>(BREATH_MS, true, [this](EQ::Timer *) { Breathe(); });
}

HeadlessMover::~HeadlessMover() = default;

bool HeadlessMover::PlanRoute(const glm::vec3 &start, const glm::vec3 &destination, bool through_zone_lines)
{
	m_path.clear();
	m_stop_at_end = false;
	m_lift_phase = LiftPhase::None;
	if (zone->HeadlessPathing()) {
		bool partial = false;
		bool stuck   = false;
		auto route   = zone->HeadlessPathing()->FindRoute(start, destination, partial, stuck, MOVER_FLAGS);
		float direct = 0.0f;
		const glm::vec3 *last = nullptr;
		for (auto &node : route) {
			if (last) {
				direct += glm::length(node.pos - *last);
			}
			last = &node.pos;
		}
		// with no navmesh path at all, FindRoute answers with just the destination (NPCs walk
		// straight there); a real route starts where we stand, so it has two nodes or more
		const bool fallback = route.size() == 1 && glm::length(route.front().pos - destination) < 0.1f &&
			glm::length(glm::vec3(destination.x - start.x, destination.y - start.y, 0.0f)) > PARTIAL_OK;
		if (fallback) {
			route.clear();
		}
		const bool walkable = !route.empty() && !stuck && !partial;
		bool via_lift = false;
		if (!through_zone_lines && std::fabs(destination.z - start.z) >= LIFT_MIN_CLIMB) {
			via_lift = PlanWithLift(start, destination, walkable ? direct : -1.0f);
		}
		for (auto &node : route) {
			if (via_lift) {
				break; // PlanWithLift has the path
			}
			if (node.teleport) {
				// a zone line (or teleporter): walk up to it, and into it when zoning
				if (through_zone_lines) {
					m_path.push_back(node.pos);
				}
				m_stop_at_end = true;
				break;
			}
			m_path.push_back(node.pos);
		}
		// a partial route ends wherever the navmesh gets closest, which can be a cliff top above
		// the destination or a ledge beside it: walking it isn't getting there
		const bool short_of_it = partial && !m_stop_at_end && !m_path.empty() &&
			(glm::length(glm::vec3(m_path.back().x - destination.x, m_path.back().y - destination.y, 0.0f)) > PARTIAL_OK ||
			 std::fabs(m_path.back().z - destination.z) > PARTIAL_OK_Z);
		m_partial = false;
		if (short_of_it && !stuck) {
			const glm::vec3 &end      = m_path.back();
			const float      left     = Flat(end, destination);
			const bool       progress = Flat(start, destination) - left >= PARTIAL_PROGRESS;
			const bool       cliff    = left < CLIFF_NEAR && std::fabs(end.z - destination.z) > CLIFF_DZ;
			if (progress && !cliff) {
				m_partial = true; // walk it, then plan again from its end
				FollowSwims(start);
				return true;
			}
		}
		if (!via_lift && (m_path.empty() || stuck || short_of_it)) {
			if (short_of_it) {
				LogHeadlessClient(
					"[{}] no full route to [{:.1f}, {:.1f}, {:.1f}]: the nearest it gets is [{:.1f}, {:.1f}, {:.1f}]",
					m_stream->GetClient() ? m_stream->GetClient()->GetCleanName() : "?", destination.x, destination.y, destination.z,
					m_path.back().x, m_path.back().y, m_path.back().z
				);
			}
			m_path.clear();
			return false;
		}
		if (!via_lift) {
			FollowSwims(start);
		}
	}
	else {
		m_path.push_back(destination); // no navmesh: walk straight, following the floor
	}
	return true;
}

const std::vector<HeadlessMover::Swim> &HeadlessMover::Swims()
{
	// the file doesn't change while a zone is up: read it once per zone
	static std::vector<Swim> cached;
	static std::string       cached_zone;
	const std::string name = zone->GetShortName();
	if (name == cached_zone) {
		return cached;
	}
	cached.clear();
	cached_zone = name;
	std::ifstream in(fmt::format("{}/maps/nav/{}.swims.json", PathManager::Instance()->GetServerPath(), name));
	if (!in) {
		return cached;
	}
	try {
		const auto j  = nlohmann::json::parse(in);
		auto       eq = [](const nlohmann::json &p) {
			return glm::vec3(p.at(0).get<float>(), p.at(1).get<float>(), p.at(2).get<float>());
		};
		for (const auto &s : j.value("swims", nlohmann::json::array())) {
			Swim sw;
			sw.from = eq(s.at("from"));
			sw.to   = eq(s.at("to"));
			for (const auto &p : s.at("points")) {
				sw.points.push_back(eq(p));
			}
			cached.push_back(std::move(sw));
		}
		LogHeadlessClient("[{}] [{}] swims through water", name, cached.size());
	}
	catch (const std::exception &e) {
		LogError("[{}] can't read its swims: {}", name, e.what());
	}
	return cached;
}

void HeadlessMover::FollowSwims(const glm::vec3 &start)
{
	const auto &swims = Swims();
	if (swims.empty() || m_path.empty()) {
		return;
	}
	auto same = [](const glm::vec3 &a, const glm::vec3 &b) { return glm::length(a - b) < SWIM_MATCH; };
	std::vector<glm::vec3> out;
	glm::vec3              prev = start;
	for (const auto &p : m_path) {
		for (const auto &s : swims) {
			if (s.points.size() < 3) {
				continue; // (nothing between the ends)
			}
			if (same(prev, s.from) && same(p, s.to)) {
				out.insert(out.end(), s.points.begin() + 1, s.points.end() - 1);
				break;
			}
			if (same(prev, s.to) && same(p, s.from)) {
				out.insert(out.end(), s.points.rbegin() + 1, s.points.rend() - 1);
				break;
			}
		}
		out.push_back(p);
		prev = p;
	}
	m_path = std::move(out);
}

bool HeadlessMover::StraightWalkable(const glm::vec3 &from, const glm::vec3 &to)
{
	if (!zone->zonemap) {
		return false;
	}
	const float total = Flat(from, to);
	if (total < 1.0f) {
		return true;
	}
	glm::vec3 prev(from.x, from.y, FloorBelow(from));
	for (float d = STRAIGHT_STEP; d < total + STRAIGHT_STEP; d += STRAIGHT_STEP) {
		const float     f = std::min(d, total) / total;
		const glm::vec3 above(from.x + (to.x - from.x) * f, from.y + (to.y - from.y) * f, prev.z + STRAIGHT_RISE + 2.0f);
		const float     floor = FloorBelow(above);
		const float rise = floor - prev.z;
		if (floor == above.z || rise > STRAIGHT_RISE || rise < -STRAIGHT_DROP) {
			LogHeadlessClient("straight walk to [{:.0f}, {:.0f}]: {} at [{:.0f}, {:.0f}] ({:.0f} of {:.0f} units, floor {:.1f} after {:.1f})",
				to.x, to.y, floor == above.z ? "no floor" : (rise > 0 ? "a step too high" : "too far down"), above.x, above.y, d, total, floor, prev.z);
			return false; // no floor (a drop) or a step too high (a wall or a cliff)
		}
		const glm::vec3 here(above.x, above.y, floor);
		// stepping down off a ledge: look level from where we are (the ledge's own edge isn't in the way)
		const glm::vec3 look = rise < 0.0f ? glm::vec3(here.x, here.y, prev.z) : here;
		if (!zone->zonemap->CheckLoS(prev + glm::vec3(0, 0, BODY_HEIGHT), look + glm::vec3(0, 0, BODY_HEIGHT))) {
			LogHeadlessClient("straight walk to [{:.0f}, {:.0f}]: something in the way at [{:.0f}, {:.0f}, {:.0f}] ({:.0f} of {:.0f} units)",
				to.x, to.y, here.x, here.y, here.z, d, total);
			return false; // something in the way
		}
		prev = here;
	}
	return true;
}

bool HeadlessMover::StepOntoNavmesh(const glm::vec3 &pos, const glm::vec3 &destination)
{
	if (!zone->HeadlessPathing()) {
		return false;
	}
	// FindRoute looks only 5 units across for the polygon we stand on: try rings around us,
	// nearest first, each spot one we can walk straight to and a route starts from
	for (float r = OFF_MESH_STEP; r <= OFF_MESH_MAX; r += OFF_MESH_STEP) {
		for (int i = 0; i < OFF_MESH_DIRECTIONS; ++i) {
			const float a = i * 6.2831853f / OFF_MESH_DIRECTIONS;
			glm::vec3 spot(pos.x + r * std::cos(a), pos.y + r * std::sin(a), pos.z + STRAIGHT_RISE);
			spot.z = FloorBelow(spot);
			if (spot.z == pos.z + STRAIGHT_RISE || !StraightWalkable(pos, spot) || !PlanRoute(spot, destination, m_through)) {
				continue;
			}
			LogHeadlessClient("[{}] off the navmesh at [{:.0f}, {:.0f}, {:.0f}]: stepping [{:.0f}] units onto it",
				m_stream->GetClient() ? m_stream->GetClient()->GetCleanName() : "?", pos.x, pos.y, pos.z, r);
			Note("off_mesh", pos, spot);
			m_path.insert(m_path.begin(), spot);
			return true;
		}
	}
	return false;
}

void HeadlessMover::Note(const char *kind, const glm::vec3 &at, const glm::vec3 &to, const glm::vec3 &closest)
{
	m_notes.push_back({kind, at, to, closest});
}

void HeadlessMover::NoteNoRoute(const glm::vec3 &from, const glm::vec3 &to)
{
	if (!zone->HeadlessPathing()) {
		return;
	}
	// FindRoute answers nothing at all when a point has no navmesh polygon under it
	bool partial = false;
	bool stuck   = false;
	if (zone->HeadlessPathing()->FindRoute(from, from, partial, stuck, MOVER_FLAGS).empty()) {
		Note("no_mesh_here", from, to);
	}
	else if (zone->HeadlessPathing()->FindRoute(to, to, partial, stuck, MOVER_FLAGS).empty()) {
		Note("no_mesh_there", from, to);
	}
	else {
		auto route = zone->HeadlessPathing()->FindRoute(from, to, partial, stuck, MOVER_FLAGS);
		Note("not_connected", from, to, route.empty() ? from : route.back().pos);
	}
}

// the sum of a spell effect's base values over the character's buffs and its worn items' worn effects (the
// client sums spell effects this way; the server doesn't keep water breathing or air as bonuses)
static int EffectTotal(Client *me, uint16 effect)
{
	int total = 0;
	auto add = [&](uint16 spell_id) {
		if (!IsValidSpell(spell_id)) {
			return;
		}
		for (int i = 0; i < EFFECT_COUNT; ++i) {
			if (spells[spell_id].effect_id[i] == effect) {
				total += spells[spell_id].base_value[i];
			}
		}
	};
	Buffs_Struct *buffs = me->GetBuffs();
	for (int i = 0; buffs && i < me->GetMaxBuffSlots(); ++i) {
		if (buffs[i].spellid != SPELL_UNKNOWN) {
			add(buffs[i].spellid);
		}
	}
	for (int slot = EQ::invslot::EQUIPMENT_BEGIN; slot <= EQ::invslot::EQUIPMENT_END; ++slot) {
		const EQ::ItemInstance *inst = me->GetInv().GetItem(slot);
		if (inst && inst->GetItem() && inst->GetItem()->Worn.Effect > 0 && inst->GetItem()->Worn.Type == EQ::item::ItemEffectWorn) {
			add(inst->GetItem()->Worn.Effect);
		}
	}
	return total;
}

int HeadlessMover::MaxAir(Client *me)
{
	int lungs = EffectTotal(me, SpellEffect::SetBreathLevel);
	if (lungs < 1) {
		lungs = LUNGS_DEFAULT;
	}
	const int air = (me->GetSTA() - 30) * lungs / 100;
	if (zone && zone->GetZoneID() == SLEEPER_ZONE) {
		return SLEEPER_AIR;
	}
	if (zone && zone->GetZoneID() == DRANIK_CATACOMBS_ZONE) {
		return DRANIK_AIR;
	}
	return air < AIR_FLOOR ? AIR_FLOOR : std::min(air, lungs);
}

bool HeadlessMover::CanBreathe(Client *me)
{
	if (!zone || !zone->HasWaterMap()) {
		return true;
	}
	for (uint32 z : BREATHE_ANYWAY) {
		if (zone->GetZoneID() == z) {
			return true;
		}
	}
	const glm::vec3 head(me->GetX(), me->GetY(), me->GetZ() + me->GetSize() * HEAD_SHARE);
	if (!zone->watermap->InLiquid(head)) {
		return true;
	}
	return EffectTotal(me, SpellEffect::WaterBreathing) > 0;
}

int HeadlessMover::Air()
{
	if (m_air >= 0) {
		return m_air;
	}
	Client *me = m_stream->GetClient();
	return me ? MaxAir(me) : 0;
}

void HeadlessMover::Breathe()
{
	Client *me = m_stream->GetClient();
	if (!me || me->GetHP() <= 0) {
		return;
	}
	if (CanBreathe(me)) {
		m_air = MaxAir(me);
		return;
	}
	if (m_air < 0) {
		m_air = MaxAir(me); // (came in under water: a full breath, as the server gives one on zoning in)
	}
	if (m_air > 0) {
		--m_air;
		return;
	}
	if (!RuleB(HeadlessClients, Drowning)) {
		return;
	}
	const int damage = static_cast<int>(std::lround(me->GetMaxHP() * DROWN_SHARE));
	if (damage <= 0) {
		return;
	}
	// as the client reports it: the server takes it from there (GM status, invulnerability, the rules)
	auto app = new EQApplicationPacket(OP_EnvDamage, sizeof(EnvDamage2_Struct));
	auto *ed = reinterpret_cast<EnvDamage2_Struct *>(app->pBuffer);
	ed->id       = me->GetID();
	ed->damage   = damage;
	ed->dmgtype  = EQ::constants::EnvironmentalDamage::Drowning;
	ed->constant = 0xFFFF;
	m_stream->Inject(app);
	m_env_damage    = EQ::constants::GetEnvironmentalDamageName(EQ::constants::EnvironmentalDamage::Drowning);
	m_env_damage_at = Timer::GetCurrentTime();
	LogHeadlessClient("[{}] drowning: [{}] of [{}] hit points", me->GetCleanName(), damage, me->GetMaxHP());
}

std::string HeadlessMover::RecentEnvironmentDamage(uint64 within_ms) const
{
	return m_env_damage_at && Timer::GetCurrentTime() - m_env_damage_at <= within_ms ? m_env_damage : std::string();
}

float HeadlessMover::Check(const glm::vec3 &destination)
{
	Client *me = m_stream->GetClient();
	if (!me) {
		return -1.0f;
	}
	// plan without walking: keep a walk that's under way as it is
	const auto path  = m_path;
	const auto stop  = m_stop_at_end;
	const auto board = m_board;
	const auto btn   = m_lift_button;
	const auto phase = m_lift_phase;
	const auto part  = m_partial;
	const glm::vec3 start(me->GetX(), me->GetY(), me->GetZ());
	// as a walk would go: a partial route's legs, each planned from where the last one ends
	float     length = 0.0f;
	glm::vec3 from   = start;
	bool      there  = false;
	for (int leg = 0; leg <= PARTIAL_LEGS; ++leg) {
		if (!PlanRoute(from, destination, false) || m_path.empty()) {
			break;
		}
		glm::vec3 prev = from;
		for (const auto &p : m_path) {
			length += glm::length(p - prev);
			prev = p;
		}
		from = m_path.back();
		if (!m_partial || Flat(from, destination) <= PARTIAL_OK) {
			there = true;
			break;
		}
	}
	if (!there) {
		length = -1.0f;
	}
	m_path        = path;
	m_stop_at_end = stop;
	m_board       = board;
	m_lift_button = btn;
	m_lift_phase  = phase;
	m_partial     = part;
	return length;
}

std::string HeadlessMover::Start(const glm::vec3 &destination, bool through_zone_lines)
{
	Client *me = m_stream->GetClient();
	if (!me) {
		return "character is not in the zone";
	}

	if (m_moving) {
		Stop(Result::Stopped);
	}
	m_notes.clear();

	const glm::vec3 start(me->GetX(), me->GetY(), me->GetZ());
	m_through = through_zone_lines;
	m_final   = destination;
	m_legs    = 0;
	if (!PlanRoute(start, destination, through_zone_lines)) {
		if (StepOntoNavmesh(start, destination)) {
			// a few steps onto the navmesh, then the way from there
		}
		else if (through_zone_lines && Flat(start, destination) <= ZONE_GAP && std::fabs(start.z - destination.z) <= ZONE_GAP_DZ &&
			StraightWalkable(start, destination)) {
			LogHeadlessClient("[{}] no navmesh route to the zone line [{:.0f}] units away: walking straight",
				me->GetCleanName(), Flat(start, destination));
			Note("straight_walk", start, destination);
			m_path = {destination};
			m_partial = false;
		}
		else {
			NoteNoRoute(start, destination);
			m_on_done(Result::NoPath, start);
			return "";
		}
	}

	Go(me, start);

	float route_length = 0.0f;
	glm::vec3 prev = start;
	for (const auto &p : m_path) {
		route_length += glm::length(glm::vec3(p.x - prev.x, p.y - prev.y, 0.0f));
		prev = p;
	}

	LogHeadlessClient(
		"[{}] moving to [{:.1f}, {:.1f}, {:.1f}] via [{}] points ([{:.0f}] units), navmesh [{}], run speed [{}] = [{:.1f}] units/s",
		me->GetCleanName(),
		destination.x,
		destination.y,
		destination.z,
		m_path.size(),
		route_length,
		zone->HeadlessPathing() ? "yes" : "no",
		me->GetRunspeed(),
		RunSpeed(me)
	);
	return "";
}

void HeadlessMover::Go(Client *me, const glm::vec3 &start)
{
	// a real client stands up to walk
	if (me->IsSitting()) {
		auto app = new EQApplicationPacket(OP_SpawnAppearance, sizeof(SpawnAppearance_Struct));
		auto *sa = reinterpret_cast<SpawnAppearance_Struct *>(app->pBuffer);
		sa->spawn_id  = me->GetID();
		sa->type      = AppearanceType::Animation;
		sa->parameter = Animation::Standing;
		m_stream->Inject(app);
	}

	m_next         = 0;
	m_moving       = true;
	m_pos          = start;
	m_progress_pos = start;
	m_progress_ms  = NowMs();
	m_timer        = std::make_unique<EQ::Timer>(TICK_MS, true, [this](EQ::Timer *) { Tick(); });
}

std::string HeadlessMover::Surface()
{
	Client *me = m_stream->GetClient();
	if (!me) {
		return "character is not in the zone";
	}
	if (!zone || !zone->HasWaterMap()) {
		return "no water here";
	}
	const glm::vec3 start(me->GetX(), me->GetY(), me->GetZ());
	const float     head = me->GetSize() * HEAD_SHARE;
	if (!zone->watermap->InLiquid(start + glm::vec3(0.0f, 0.0f, head))) {
		return "not under water";
	}
	// straight up to where the water ends, with the head just out of it
	float top = -1.0f;
	for (float up = 1.0f; up <= SURFACE_UP; up += 1.0f) {
		if (!zone->watermap->InLiquid(start + glm::vec3(0.0f, 0.0f, up))) {
			top = up;
			break;
		}
	}
	if (top < 0.0f) {
		return "no surface above";
	}
	const glm::vec3 target(start.x, start.y, start.z + std::max(0.0f, top - head + 1.0f));
	if (zone->zonemap && !zone->zonemap->CheckLoS(start + glm::vec3(0.0f, 0.0f, head), target + glm::vec3(0.0f, 0.0f, head))) {
		return "something solid above";
	}
	if (m_moving) {
		Stop(Result::Stopped);
	}
	m_notes.clear();
	m_path        = {target};
	m_through     = false;
	m_final       = target;
	m_legs        = 0;
	m_partial     = false;
	m_stop_at_end = false;
	m_lift_phase  = LiftPhase::None;
	Go(me, start);
	LogHeadlessClient("[{}] surfacing: [{:.0f}] units up, [{}] seconds of air left", me->GetCleanName(), target.z - start.z, Air());
	return "";
}

void HeadlessMover::Stop(Result why)
{
	if (!m_moving) {
		return;
	}

	m_moving = false;
	if (m_timer) {
		m_timer->Stop(); // safe inside the timer's own callback; the object stays alive
	}

	// where we are is where we last told the server we are (it may not have processed it yet)
	if (Client *me = m_stream->GetClient()) {
		SendUpdate(m_pos, me->GetHeading(), 0); // standing still
	}

	m_on_done(why, m_pos);
}

std::string HeadlessMover::Face(const glm::vec3 &point)
{
	Client *me = m_stream->GetClient();
	if (!me) {
		return "character is not in the zone";
	}
	if (m_moving) {
		return "can't turn while walking";
	}

	const glm::vec3 pos(me->GetX(), me->GetY(), me->GetZ());
	SendUpdate(pos, me->CalculateHeadingToTarget(point.x, point.y), 0);
	return "";
}

float HeadlessMover::RunSpeed(const Mob *me)
{
	return me->GetRunspeed() * SPEED_TO_UNITS;
}

void HeadlessMover::Place(const glm::vec3 &pos, float heading)
{
	Stop(Result::Stopped);
	m_pos = pos;
	SendUpdate(pos, heading, 0);
}

void HeadlessMover::Tick()
{
	Client *me = m_stream->GetClient();
	if (!m_moving || !me || !me->Connected()) {
		return;
	}

	if (me->IsSitting()) {
		Stop(Result::Stopped);
		return;
	}

	// the server moved us a long way (summon, port, knockback): stop instead of fighting it.
	// Smaller differences are just updates the server hasn't processed yet.
	const glm::vec3 server_pos(me->GetX(), me->GetY(), me->GetZ());
	if (glm::length(glm::vec3(server_pos.x - m_pos.x, server_pos.y - m_pos.y, 0.0f)) > 50.0f) {
		m_pos = server_pos;
		Stop(Result::Stopped);
		return;
	}

	const uint64 now   = NowMs();

	if (m_lift_phase != LiftPhase::None && HandleLift(now)) {
		m_progress_ms  = now; // waiting for or riding a lift isn't being stuck
		m_progress_pos = m_pos;
		return;
	}
	if (!m_moving) {
		return;
	}

	if (HandleDoors(now)) {
		m_progress_ms = now; // waiting for a door isn't being stuck
		return;
	}
	if (!m_moving) {
		return; // HandleDoors gave up on a door
	}

	const float  speed = RunSpeed(me); // units per second; 0 when rooted/stunned
	float        step  = speed * (TICK_MS / 1000.0f);

	glm::vec3 pos = m_pos;
	while (step > 0.0f && m_next < m_path.size()) {
		if ((m_lift_phase == LiftPhase::ToButton && m_next == m_board) ||
			(m_lift_phase == LiftPhase::Boarding && m_next == m_board + 1)) {
			break; // at the button, or on the platform: HandleLift takes over
		}
		const glm::vec3 &target = m_path[m_next];
		// on land the floor gives our height; in water we swim, up and down as well as across
		const bool swimming = zone->HasWaterMap() && zone->watermap->InLiquid(pos);
		glm::vec3  to(target.x - pos.x, target.y - pos.y, swimming ? target.z - pos.z : 0.0f);
		const float dist = glm::length(to);

		if (dist <= step || dist < ARRIVE_EPSILON) {
			pos = glm::vec3(target.x, target.y, target.z);
			step -= dist;
			m_next++;
		}
		else {
			pos += (to / dist) * step;
			step = 0.0f;
		}
	}

	// keep to the floor, as the client's physics would; but on a platform (and stepping on or off
	// one) the floor is the platform, which the zone's map doesn't have
	const bool on_platform =
		(m_lift_phase == LiftPhase::Boarding) ||
		(m_lift_phase == LiftPhase::Off && m_board + 1 < m_path.size() &&
		 glm::length(glm::vec3(pos.x - m_path[m_board + 1].x, pos.y - m_path[m_board + 1].y, 0.0f)) < PLATFORM_RADIUS * 2);
	if (on_platform) {
		pos.z = m_path[std::min(m_next, m_path.size() - 1)].z;
	}
	else {
		pos.z = me->GetFixedZ(pos);
	}

	// the client notices zone lines itself (the server knows them from the water map's regions):
	// walk into one only when zoning, otherwise stop short of it
	if (zone->HasWaterMap() && zone->watermap->InZoneLine(pos) && !zone->watermap->InZoneLine(m_pos)) {
		if (m_through) {
			m_pos = pos;
			SendUpdate(pos, me->GetHeading(), 0);
		}
		Stop(Result::ZoneLine);
		return;
	}
	m_pos = pos;

	const bool  arrived = m_next >= m_path.size();
	const auto &facing  = arrived ? m_path.back() : m_path[m_next];
	const float heading = (facing.x == pos.x && facing.y == pos.y) ? me->GetHeading() : me->CalculateHeadingToTarget(facing.x, facing.y);

	SendUpdate(pos, heading, arrived ? 0 : static_cast<uint32>(me->GetRunspeed()));

	if (arrived && m_partial) {
		// the end of a partial route: carry on towards where we were going
		m_pos = pos;
		if (Flat(pos, m_final) <= PARTIAL_OK) {
			Stop(Result::Arrived);
			return;
		}
		const bool too_many = ++m_legs > PARTIAL_LEGS; // (the search's limit, not the navmesh's)
		if (too_many || !PlanRoute(pos, m_final, m_through)) {
			if (m_through && Flat(pos, m_final) <= ZONE_GAP && std::fabs(pos.z - m_final.z) <= ZONE_GAP_DZ &&
				StraightWalkable(pos, m_final)) {
				LogHeadlessClient("[{}] the navmesh ends [{:.0f}] units short of the zone line: walking the rest",
					me->GetCleanName(), Flat(pos, m_final));
				Note("straight_walk", pos, m_final);
				m_path = {m_final};
				m_partial = false;
				m_next = 0;
				return;
			}
			if (m_through) {
				LogHeadlessClient("[{}] no way to the zone line from [{:.0f}, {:.0f}, {:.0f}]: [{:.0f}] units short, and the map shows no straight way",
					me->GetCleanName(), pos.x, pos.y, pos.z, Flat(pos, m_final));
			}
			if (!too_many) {
				NoteNoRoute(pos, m_final);
			}
			Stop(Result::NoPath);
			return;
		}
		m_next = 0;
		return;
	}
	if (arrived) {
		Stop(m_stop_at_end ? Result::ZoneLine : Result::Arrived);
		return;
	}

	// no real progress for a while (rooted, blocked by geometry)
	if (glm::length(glm::vec3(pos.x - m_progress_pos.x, pos.y - m_progress_pos.y, 0.0f)) >= STUCK_DISTANCE) {
		m_progress_pos = pos;
		m_progress_ms  = now;
	}
	else if (now - m_progress_ms > STUCK_MS) {
		if (RunSpeed(me) > 0.0f) {
			Note("stuck", pos, m_path[m_next]); // walking into something the navmesh says isn't there
		}
		Stop(Result::Stuck);
	}
}

bool HeadlessMover::HandleDoors(uint64 now)
{
	Client *me = m_stream->GetClient();

	// waiting on a door we clicked
	if (m_door_entity) {
		auto *door = entity_list.GetDoorsByID(m_door_entity);
		if (!door || door->IsDoorOpen()) {
			m_door_entity = 0;
			return false;
		}
		if (now > m_door_wait_ms) {
			LogHeadlessClient("[{}] door [{}] didn't open; giving up", me->GetCleanName(), door->GetDoorID());
			Note("door_wont_open", m_pos, glm::vec3(door->GetPosition())); // (a quest's, a trigger's, a lever's)
			m_door_entity = 0;
			Stop(Result::Stuck);
		}
		return m_door_entity != 0;
	}

	if (m_next >= m_path.size()) {
		return false;
	}

	// the stretch of route just ahead of us
	const glm::vec3 &next = m_path[m_next];
	glm::vec3 dir(next.x - m_pos.x, next.y - m_pos.y, 0.0f);
	const float len = glm::length(dir);
	if (len < 0.01f) {
		return false;
	}
	const glm::vec3 ahead = m_pos + (dir / len) * std::min(len, DOOR_LOOKAHEAD);

	for (auto &[id, door] : entity_list.GetDoorsList()) {
		if (!door || door->IsDoorOpen() || door->IsDoorBlacklisted()) {
			continue;
		}
		// never "click" teleporters (PoK books, portals): that would zone the character
		if (door->HasDestinationZone()) {
			continue;
		}
		const auto &dp = door->GetPosition();
		if (std::fabs(dp.z - m_pos.z) > DOOR_HEIGHT_RANGE ||
			glm::length(glm::vec3(dp.x - m_pos.x, dp.y - m_pos.y, 0.0f)) > DOOR_LOOKAHEAD) {
			continue;
		}
		if (!door->IsDoorBetween(glm::vec4(m_pos, 0.0f), glm::vec4(ahead, 0.0f), door->GetSize())) {
			continue;
		}

		if (!CanOpen(me, door)) {
			LogHeadlessClient("[{}] door [{}] ({}) is locked and we can't open it", me->GetCleanName(), door->GetDoorID(), door->GetDoorName());
			Note("locked", m_pos, glm::vec3(dp)); // (not the navmesh's fault: this character has no key)
			Stop(Result::Locked);
			return true;
		}

		// click it, as a player would; the server checks distance, keys and locks
		auto app = new EQApplicationPacket(OP_ClickDoor, sizeof(ClickDoor_Struct));
		auto *cd = reinterpret_cast<ClickDoor_Struct *>(app->pBuffer);
		cd->doorid    = door->GetDoorID();
		cd->player_id = me->GetID();
		m_stream->Inject(app);

		m_door_entity  = door->GetEntityID();
		m_door_wait_ms = now + DOOR_WAIT_MS;
		SendUpdate(m_pos, me->GetHeading(), 0); // stop in front of it
		LogHeadlessClient("[{}] clicking door [{}] ({})", me->GetCleanName(), door->GetDoorID(), door->GetDoorName());
		return true;
	}

	return false;
}

bool HeadlessMover::CanOpen(Client *me, Doors *door)
{
	if (me->GetGM()) {
		return true;
	}
	if (door->GetGuildID() > 0) {
		return door->GetGuildID() == me->GuildID();
	}
	const uint32 key = door->GetKeyItem();
	if (key) {
		if (me->KeyRingCheck(key)) {
			return true;
		}
		if (RuleB(Doors, RequireKeyOnCursor)) {
			const auto *cursor = me->GetInv().GetItem(EQ::invslot::slotCursor);
			return cursor && cursor->GetID() == key;
		}
		return me->GetInv().HasItem(key, 1) != INVALID_INDEX;
	}
	// a lock that only picks open: that needs lockpicks on the cursor and the skill (not yet)
	return door->GetLockpick() == 0;
}

// the floor under a point (the zone's map), or the point's own height if there's no map
static float FloorBelow(const glm::vec3 &p)
{
	if (!zone->zonemap) {
		return p.z;
	}
	glm::vec3   at(p.x, p.y, p.z + 5.0f);
	const float z = zone->zonemap->FindBestZ(at, nullptr);
	return z == BEST_Z_INVALID ? p.z : z;
}

std::vector<HeadlessMover::Lift> HeadlessMover::FindLifts()
{
	// doors don't move between zone boots: work the lifts out once per zone
	static std::vector<Lift> cached;
	static uint32            cached_zone = 0;
	static uint16            cached_instance = 0;
	if (cached_zone == zone->GetZoneID() && cached_instance == zone->GetInstanceID()) {
		return cached;
	}

	struct Found {
		Doors              *platform = nullptr;
		std::vector<Doors *> buttons;
	};
	std::map<uint8, Found> platforms;
	for (auto &[id, d] : entity_list.GetDoorsList()) {
		if (!d || d->GetOpenType() != LIFT_OPEN_TYPE || d->GetTriggerDoorID() == 0) {
			continue;
		}
		Doors *platform = entity_list.FindDoor(d->GetTriggerDoorID());
		if (!platform || platform->GetOpenType() != LIFT_OPEN_TYPE || platform->GetDoorParam() <= 0) {
			continue;
		}
		auto &f = platforms[platform->GetDoorID()];
		f.platform = platform;
		f.buttons.push_back(d);
	}

	std::vector<Lift> lifts;
	for (auto &[id, f] : platforms) {
		if (f.buttons.size() < 2) {
			continue;
		}
		const auto  &pp   = f.platform->GetPosition();
		const float rest  = pp.z;
		const float risen = pp.z + f.platform->GetDoorParam();
		Doors *low  = *std::min_element(f.buttons.begin(), f.buttons.end(), [&](Doors *a, Doors *b) {
			return std::fabs(a->GetPosition().z - rest) < std::fabs(b->GetPosition().z - rest);
		});
		Doors *high = *std::min_element(f.buttons.begin(), f.buttons.end(), [&](Doors *a, Doors *b) {
			return std::fabs(a->GetPosition().z - risen) < std::fabs(b->GetPosition().z - risen);
		});
		if (low == high || high->GetPosition().z - low->GetPosition().z < LIFT_MIN_CLIMB) {
			continue;
		}
		Lift lift;
		lift.bottom_button  = low->GetDoorID();
		lift.top_button     = high->GetDoorID();
		// the floors: below each call button (they're at hand height on the landings)
		const auto &lb      = low->GetPosition();
		const auto &hb      = high->GetPosition();
		lift.bottom_landing = glm::vec3(lb.x, lb.y, FloorBelow(glm::vec3(lb.x, lb.y, lb.z)));
		lift.top_landing    = glm::vec3(hb.x, hb.y, FloorBelow(glm::vec3(hb.x, hb.y, hb.z)));
		// where to stand: a platform model's origin is off to one side (Kelethin's are ~100 units
		// out), but its call buttons sit either side of the shaft, so stand between them
		const glm::vec2 shaft((lb.x + hb.x) / 2.0f, (lb.y + hb.y) / 2.0f);
		lift.bottom = glm::vec3(shaft.x, shaft.y, lift.bottom_landing.z);
		lift.top    = glm::vec3(shaft.x, shaft.y, lift.top_landing.z);
		// the map should have a hole there at the top (the platform isn't in it)
		const float under = FloorBelow(glm::vec3(shaft.x, shaft.y, lift.top_landing.z));
		LogHeadlessClient(
			"lift [{}]: [{:.0f}, {:.0f}] from z [{:.0f}] to [{:.0f}], buttons [{}] / [{}]{}",
			f.platform->GetDoorID(), shaft.y, shaft.x, lift.bottom.z, lift.top.z, lift.bottom_button, lift.top_button,
			under < lift.top_landing.z - LIFT_MIN_CLIMB ? "" : " (no shaft in the map there?)"
		);
		lifts.push_back(lift);
	}
	cached          = lifts;
	cached_zone     = zone->GetZoneID();
	cached_instance = zone->GetInstanceID();
	return lifts;
}

std::vector<glm::vec3> HeadlessMover::Route(const glm::vec3 &from, const glm::vec3 &to, float *length)
{
	std::vector<glm::vec3> out;
	*length = -1.0f;
	if (!zone->HeadlessPathing()) {
		return out;
	}
	bool partial = false;
	bool stuck   = false;
	auto route   = zone->HeadlessPathing()->FindRoute(from, to, partial, stuck, MOVER_FLAGS);
	if (route.empty() || stuck || partial || (route.size() == 1 && glm::length(from - to) > PARTIAL_OK)) {
		return out; // (one node: FindRoute's straight-line answer when there's no path)
	}
	float total = 0.0f;
	for (auto &node : route) {
		if (node.teleport) {
			return {}; // through a zone line: not a way to a lift
		}
		if (!out.empty()) {
			total += glm::length(node.pos - out.back());
		}
		out.push_back(node.pos);
	}
	*length = total;
	return out;
}

bool HeadlessMover::PlanWithLift(const glm::vec3 &start, const glm::vec3 &destination, float direct_length)
{
	struct Best {
		float                  length = -1.0f;
		std::vector<glm::vec3> path;
		size_t                 board  = 0;
		uint8                  button = 0;
	} best;

	for (const auto &lift : FindLifts()) {
		const float mid = (lift.bottom.z + lift.top.z) / 2.0f;
		bool up;
		if (start.z < mid && destination.z > mid) {
			up = true;
		}
		else if (start.z > mid && destination.z < mid) {
			up = false;
		}
		else {
			continue;
		}
		const glm::vec3 &on_landing  = up ? lift.bottom_landing : lift.top_landing;
		const glm::vec3 &off_landing = up ? lift.top_landing : lift.bottom_landing;
		const glm::vec3 &on          = up ? lift.bottom : lift.top;
		const glm::vec3 &off         = up ? lift.top : lift.bottom;

		float to_lift, from_lift;
		auto  leg1 = Route(start, on_landing, &to_lift);
		auto  leg2 = Route(off_landing, destination, &from_lift);
		if (to_lift < 0.0f || from_lift < 0.0f) {
			continue;
		}
		const float length = to_lift + from_lift + LIFT_RIDE_COST +
			glm::length(on - on_landing) + glm::length(off - off_landing);
		if (best.length >= 0.0f && length >= best.length) {
			continue;
		}
		best.length = length;
		best.path   = leg1;
		best.path.push_back(on_landing);
		best.board  = best.path.size();
		best.path.push_back(on);
		best.path.push_back(off);
		best.path.push_back(off_landing);
		best.path.insert(best.path.end(), leg2.begin(), leg2.end());
		best.button = up ? lift.bottom_button : lift.top_button;
	}

	if (best.length < 0.0f || (direct_length >= 0.0f && best.length > direct_length * LIFT_SAVING)) {
		return false;
	}
	m_path        = std::move(best.path);
	m_board       = best.board;
	m_lift_button = best.button;
	m_lift_phase  = LiftPhase::ToButton;
	LogHeadlessClient("lift: [{:.0f}] units via door [{}] instead of [{:.0f}] walking", best.length, m_lift_button, direct_length);
	return true;
}

bool HeadlessMover::HandleLift(uint64 now)
{
	Client *me = m_stream->GetClient();
	switch (m_lift_phase) {
		case LiftPhase::ToButton:
			if (m_next != m_board) {
				return false;
			}
			{
				// press the call button, as a player would; the platform comes to us
				auto app = new EQApplicationPacket(OP_ClickDoor, sizeof(ClickDoor_Struct));
				auto *cd = reinterpret_cast<ClickDoor_Struct *>(app->pBuffer);
				cd->doorid    = m_lift_button;
				cd->player_id = me->GetID();
				m_stream->Inject(app);
			}
			LogHeadlessClient("[{}] calling the lift (door [{}])", me->GetCleanName(), m_lift_button);
			m_lift_phase = LiftPhase::Calling;
			m_lift_ms    = now + LIFT_CALL_MS;
			SendUpdate(m_pos, me->GetHeading(), 0);
			return true;
		case LiftPhase::Calling:
			if (now < m_lift_ms) {
				return true;
			}
			m_lift_phase = LiftPhase::Boarding;
			return false;
		case LiftPhase::Boarding:
			if (m_next != m_board + 1) {
				return false;
			}
			m_lift_phase = LiftPhase::Riding;
			m_lift_ms    = now;
			return true;
		case LiftPhase::Riding: {
			const glm::vec3 &from = m_path[m_board];
			const glm::vec3 &to   = m_path[m_board + 1];
			const float      dz   = to.z - from.z;
			const float      t    = std::min(1.0f, (now - m_lift_ms) / 1000.0f * LIFT_SPEED / std::max(std::fabs(dz), 1.0f));
			m_pos = glm::vec3(from.x, from.y, from.z + dz * t);
			SendUpdate(m_pos, me->GetHeading(), 0);
			if (t >= 1.0f) {
				m_next       = m_board + 2; // step off towards the landing
				m_lift_phase = LiftPhase::Off;
			}
			return true;
		}
		default:
			return false;
	}
}

void HeadlessMover::SendUpdate(const glm::vec3 &pos, float heading, uint32 animation)
{
	Client *me = m_stream->GetClient();
	if (!me) {
		return;
	}

	auto app = new EQApplicationPacket(OP_ClientUpdate, sizeof(PlayerPositionUpdateClient_Struct));
	auto *ppu = reinterpret_cast<PlayerPositionUpdateClient_Struct *>(app->pBuffer);
	ppu->sequence   = m_sequence++;
	ppu->spawn_id   = me->GetID();
	ppu->vehicle_id = 0;
	ppu->x_pos      = pos.x;
	ppu->y_pos      = pos.y;
	ppu->z_pos      = pos.z;
	ppu->heading    = FloatToEQ12(heading);
	ppu->animation  = animation;
	// deltas left at 0: observers get a fresh position every 100ms, as for NPCs
	m_stream->Inject(app);
}
