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

#include <unordered_map>

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
	// a door another headless character clicked this long ago or less is opening: waited for, not clicked again. A click toggles a door (open
	// when shut, shut when open: Doors::HandleClick), and two leaders walking in step through Permafrost's door 39 clicked it in the same
	// instant: it opened and shut, and both gave up ("didn't open"), the first raid scouts with them, 9 Nov
	constexpr uint64 DOOR_SHARED_MS    = 2500;
	std::unordered_map<uint32, uint64> g_door_clicked;  // door entity id -> when a headless character last clicked it
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
	constexpr float  SURFACE_STEP        = 10.0f;   // a route through water looked at this often for the floor, to swim above it
	constexpr float  SURFACE_UP          = 2000.0f; // a surface further up than this isn't one to swim to (Dagnor's Cauldron's lake is 350 deep)
	// a route may keep the head under water for at most this share of a full breath at a stretch: the
	// runner's breath watch (soldiers/breath.py) comes up at half, and a route it breaks off for air is
	// planned again the same way. The navmesh has lake floors, and Dagnor's Cauldron's is 350 down: walks
	// along it drowned five characters a night
	constexpr float  UNDER_SHARE         = 0.45f;  // (was 0.4, the breath watch comes up at 0.5: Iceclad's succor point is in the sea, and the way out keeps the head under for 328 units against 325: refused as too deep, 24 times a day)
	constexpr float  UNDER_STEP          = 4.0f;    // the route looked at this often for water over the head
	// zones where being under water doesn't take air (the client's list: Erudin, the Southern Desert of Ro,
	// Kerra Isle), and two with an air supply of their own (the Sleeper's Tomb, the Catacombs of Dranik)
	constexpr uint32 BREATHE_ANYWAY[]    = {24, 35, 74};
	constexpr uint32 SLEEPER_ZONE = 128, SLEEPER_AIR = 127, DRANIK_CATACOMBS_ZONE = 330, DRANIK_AIR = 256;
	// the mover takes the links added for headless characters (PathingHeadless) as well as the zone's own
	constexpr int    MOVER_FLAGS         = PathingNotDisabled | PathingHeadless;
	constexpr float  PORTAL_CLIMB_FLAT   = 60.0f; // a navmesh portal link no further across than this is a ladder or a drop (PlanRoute)
	constexpr float  PORTAL_CLIMB_UP     = 120.0f; // ... and no higher than this (512 of the 912 in the stock navmeshes; Charasis' pits
	                                               // and Ssraeshza's 258-unit one stay teleporters)
	// a route's step is a swim link's when its ends are this near the link's
	constexpr float  SWIM_MATCH          = 3.0f;
	constexpr float  FLOOR_BELOW_NAV     = 6.0f;  // the map's floor this far under the route: look for the navmesh's
	constexpr float  NAV_FLOOR_DZ        = 4.0f;  // ... within this of the route's height
	constexpr float  OFF_MESH_STEP       = 10.0f;
	constexpr float  OFF_MESH_MAX        = 60.0f;
	constexpr float  SWIM_OUT_DZ         = 12.0f; // in water, a navmesh floor this far up or down from us is a way out ...
	// ... and further up: a swimmer under the navmesh (Iceclad's ocean: 33 under a floor it never reached) comes up to it, which takes
	// no air that the surface doesn't give back
	constexpr float  SWIM_UP_MAX         = 150.0f;
	constexpr int    OFF_MESH_DIRECTIONS = 12;
	// on a lake's surface the shore can be far off (Lake Rathetear: five characters afloat 160-700 units
	// out, the navmesh on the bed 190 under them, with gaps): rings this far, a spot every this many
	// units around each; past them the navmesh's nearest point at our height this far each way, or for
	// one who breathes water its nearest under us, this far down and this far each way
	constexpr float  SWIM_OUT_MAX        = 250.0f;
	constexpr float  SWIM_OUT_SPACING    = 20.0f;
	constexpr float  SWIM_FAR_MAX        = 800.0f;
	constexpr float  SINK_MAX            = 400.0f;
	constexpr float  SINK_WIDE           = 300.0f;
	// a journey keeps round the mobs that would aggro us (Threats): those this far either side of the way,
	// by their aggro radius and this much more
	constexpr float  THREAT_REACH        = 300.0f;
	constexpr float  THREAT_MARGIN       = 15.0f;
	constexpr float  BOAT_MARGIN         = 12.0f;   // round a boat's box this far past its corners

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

static int EffectTotal(Client *me, uint16 effect); // (below, with breath)

HeadlessMover::HeadlessMover(NullStream *stream, DoneCallback on_done)
	: m_stream(stream), m_on_done(std::move(on_done))
{
	m_breath_timer = std::make_unique<EQ::Timer>(BREATH_MS, true, [this](EQ::Timer *) { Breathe(); });
}

HeadlessMover::~HeadlessMover() = default;

bool HeadlessMover::PlanRoute(const glm::vec3 &start, const glm::vec3 &destination, bool through_zone_lines)
{
	m_path.clear();
	m_climbs.clear();
	m_stop_at_end = false;
	m_lift_phase = LiftPhase::None;
	if (zone->HeadlessPathing()) {
		bool partial = false;
		bool stuck   = false;
		auto threats = m_avoid ? Threats(start, destination) : std::vector<glm::vec4>();
		// a way round the mobs refused as too deep: past them instead, the boats still kept round (a fight beats no way at all.
		// Ilvara's walk from Iceclad's succor point to the Eastern Wastes, 3 November: the straight way swims on top, the one round
		// the mobs went through the deep water, and the walk failed with no way, a petition back to the same spot)
		const size_t mobs = threats.size(); // (before the boats go in)
		auto past_them = [&]() {
			Client *me = m_stream->GetClient();
			LogHeadlessClient("[{}] the way round [{}] mobs is too deep: past them instead", me ? me->GetCleanName() : "?", mobs);
			m_avoid    = false;
			m_too_deep = false;
			return PlanRoute(start, destination, through_zone_lines);
		};
		// the boats' boxes: round them whatever the walk (a circle round each: FindRouteAvoiding leaves one round the start or the destination)
		for (const auto &b : Boats()) {
			if (b.to != m_boat_to) {
				const float cx = (b.x1 + b.x2) / 2.0f, cy = (b.y1 + b.y2) / 2.0f;
				threats.emplace_back(cx, cy, b.z, std::hypot((b.x2 - b.x1) / 2.0f, (b.y2 - b.y1) / 2.0f) + BOAT_MARGIN);
			}
		}
		auto route   = threats.empty() ? zone->HeadlessPathing()->FindRoute(start, destination, partial, stuck, MOVER_FLAGS)
			: zone->HeadlessPathing()->FindRouteAvoiding(start, destination, partial, stuck, MOVER_FLAGS, threats);
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
		for (auto it = route.begin(); it != route.end(); ++it) {
			const auto &node = *it;
			if (via_lift) {
				break; // PlanWithLift has the path
			}
			if (node.teleport) {
				// the pathfinder's "teleport": the step before this marker starts an off-mesh link the zone's navmesh flags a
				// portal, and the step after it ends it (the marker itself has no place). In the stock navmeshes those are the map
				// makers' ladders and drops, a few units across and up or down (all of Upper Guk's: NPCs are put at the other end,
				// and the route stopped here as at a zone line, with the rest of it dropped: Velthrin turned back and forth between
				// two ladders' feet, Bifar and the survey never got below the first floor, 5 November). A short one is climbed, as
				// a ladder; a long one is a teleporter: the route ends at it, as before (zone lines aren't links: a walk into one is
				// the walk's end)
				const auto       next = std::next(it);
				const glm::vec3 &from = m_path.empty() ? start : m_path.back();  // (a route that starts on the link: where we stand)
				if (next != route.end() && !next->teleport && Flat(from, next->pos) <= PORTAL_CLIMB_FLAT &&
					std::fabs(from.z - next->pos.z) <= PORTAL_CLIMB_UP) {
					m_climbs.push_back({from, next->pos});
					continue;  // (its far end is the next step)
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
				SwimOnSurface(start);
				if (TooLongUnder(start) || !MarkJumps(start)) {
					m_path.clear();
					m_jump_at.clear();
					m_partial = false;
					return m_too_deep && mobs ? past_them() : false;
				}
				return true;
			}
		}
		if (!via_lift && (m_path.empty() || stuck || short_of_it)) {
			if (mobs) {
				// no way round the mobs at all (a tunnel full of them: every way out of the Echo Caverns' south went through 25 that
				// would aggro, and Malcor stood "427 units short" of every zone line, 11 Nov): past them, as for a way round too deep
				Client *me = m_stream->GetClient();
				LogHeadlessClient("[{}] no way round [{}] mobs to [{:.1f}, {:.1f}, {:.1f}]: past them instead", me ? me->GetCleanName() : "?",
					mobs, destination.x, destination.y, destination.z);
				return past_them();
			}
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
			SwimOnSurface(start);
			if (TooLongUnder(start) || !MarkJumps(start)) {
				m_path.clear();
				m_jump_at.clear();
				return m_too_deep && mobs ? past_them() : false;
			}
		}
	}
	else {
		m_path.push_back(destination); // no navmesh: walk straight, following the floor
	}
	if (m_jump_at.size() != m_path.size()) {
		m_jump_at.assign(m_path.size(), false); // (a lift's route, or no navmesh: no jumps on it)
	}
	return true;
}

const std::vector<HeadlessMover::Boat> &HeadlessMover::Boats()
{
	// the zone's: the file has every zone's, and doesn't change while the zone is up
	static std::vector<Boat> cached;
	static std::string       cached_zone;
	const std::string name = zone->GetShortName();
	if (name == cached_zone) {
		return cached;
	}
	cached.clear();
	cached_zone = name;
	std::ifstream in(fmt::format("{}/maps/nav/boats.json", PathManager::Instance()->GetServerPath()));
	if (!in) {
		return cached;
	}
	try {
		const auto j = nlohmann::json::parse(in);
		for (const auto &b : j.value("boats", nlohmann::json::array())) {
			if (b.at("zone").get<std::string>() != name) {
				continue;
			}
			Boat boat;
			const auto &box = b.at("box");
			boat.x1   = box.at(0).get<float>();
			boat.x2   = box.at(1).get<float>();
			boat.y1   = box.at(2).get<float>();
			boat.y2   = box.at(3).get<float>();
			boat.z    = b.at("at").at(2).get<float>();
			boat.to   = ZoneID(b.at("to").get<std::string>().c_str());
			const auto &land = b.at("land");
			boat.land = glm::vec3(land.at(0).get<float>(), land.at(1).get<float>(), land.at(2).get<float>());
			if (boat.to) {
				cached.push_back(boat);
			}
		}
		if (!cached.empty()) {
			LogHeadlessClient("[{}] [{}] boats kept round (walk into one only to ride it)", name, cached.size());
		}
	}
	catch (const std::exception &e) {
		LogError("[{}] can't read its boats: {}", name, e.what());
	}
	return cached;
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

const std::vector<HeadlessMover::Jump> &HeadlessMover::Jumps()
{
	return Links("jumps");
}

const std::vector<HeadlessMover::Jump> &HeadlessMover::Ladders()
{
	return Links("ladders");
}

const std::vector<HeadlessMover::Jump> &HeadlessMover::Links(const std::string &kind)
{
	// the file doesn't change while a zone is up: read it once per zone
	static std::map<std::string, std::pair<std::string, std::vector<Jump>>> cache; // kind -> (zone, links)
	auto &[cached_zone, cached] = cache[kind];
	const std::string name = zone->GetShortName();
	if (name == cached_zone) {
		return cached;
	}
	cached.clear();
	cached_zone = name;
	std::ifstream in(fmt::format("{}/maps/nav/{}.{}.json", PathManager::Instance()->GetServerPath(), name, kind));
	if (!in) {
		return cached;
	}
	try {
		const auto j  = nlohmann::json::parse(in);
		auto       eq = [](const nlohmann::json &p) {
			return glm::vec3(p.at(0).get<float>(), p.at(1).get<float>(), p.at(2).get<float>());
		};
		for (const auto &s : j.value(kind, nlohmann::json::array())) {
			cached.push_back({eq(s.at("from")), eq(s.at("to"))}); // (one way: a link both ways has both)
		}
		LogHeadlessClient("[{}] [{}] {}", name, cached.size(), kind);
	}
	catch (const std::exception &e) {
		LogError("[{}] can't read its {}: {}", name, kind, e.what());
	}
	return cached;
}

const std::vector<HeadlessMover::Jump> &HeadlessMover::Teleports()
{
	return Links("teleports");
}

bool HeadlessMover::OnTeleport(size_t i) const
{
	if (i == 0 || i >= m_path.size()) {
		return false;  // (from where it starts: the route reaches the pad first)
	}
	auto same = [](const glm::vec3 &a, const glm::vec3 &b) { return glm::length(a - b) < SWIM_MATCH; };
	for (const auto &t : Teleports()) {
		if (same(m_path[i], t.to) && same(m_path[i - 1], t.from)) {
			return true;
		}
	}
	return false;
}

bool HeadlessMover::OnLadder(size_t i) const
{
	if (i >= m_path.size()) {
		return false;
	}
	auto same = [](const glm::vec3 &a, const glm::vec3 &b) { return glm::length(a - b) < SWIM_MATCH; };
	for (const auto &c : m_climbs) {  // (a portal link of the zone's navmesh, climbed as a ladder: PlanRoute)
		if (same(m_path[i], c.to) &&
			(i > 0 ? same(m_path[i - 1], c.from) : glm::length(m_pos - c.from) <= glm::length(c.to - c.from) + SWIM_MATCH)) {
			return true;
		}
	}
	for (const auto &l : Ladders()) {
		if (!same(m_path[i], l.to)) {
			continue;
		}
		// (the first step of a route: from where we stand, on the ladder or at its foot)
		if (i > 0 ? same(m_path[i - 1], l.from) : glm::length(m_pos - l.from) <= glm::length(l.to - l.from) + SWIM_MATCH) {
			return true;
		}
	}
	return false;
}

int HeadlessMover::JumpCost(Client *me)
{
	return me->GetLevel() < 20 ? 225 * me->GetLevel() / 100 : 50;
}

bool HeadlessMover::MarkJumps(const glm::vec3 &start)
{
	m_jump_at.assign(m_path.size(), false);
	const auto &jumps = Jumps();
	if (jumps.empty() || m_path.empty()) {
		return true;
	}
	auto same = [](const glm::vec3 &a, const glm::vec3 &b) { return glm::length(a - b) < SWIM_MATCH; };
	int       count = 0;
	glm::vec3 prev  = start;
	for (size_t i = 0; i < m_path.size(); ++i) {
		for (const auto &j : jumps) {
			if (same(prev, j.from) && same(m_path[i], j.to)) {
				m_jump_at[i] = true;
				++count;
				break;
			}
		}
		prev = m_path[i];
	}
	if (!count) {
		return true;
	}
	Client *me = m_stream->GetClient();
	if (RuleR(HeadlessClients, JumpHeight) <= 0.0f || !me || me->GetEndurance() < count * JumpCost(me)) {
		LogHeadlessClient("[{}] the route takes [{}] jump(s) and we can't make them (jumping [{}], endurance [{}] of [{}] needed)",
			me ? me->GetCleanName() : "?", count, RuleR(HeadlessClients, JumpHeight) > 0.0f ? "on" : "off",
			me ? me->GetEndurance() : 0, me ? count * JumpCost(me) : 0);
		m_cant_jump = true;
		return false;
	}
	return true;
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

bool HeadlessMover::WaterTop(const glm::vec3 &p, float &top) const
{
	if (!zone || !zone->HasWaterMap() || !zone->watermap->InLiquid(p)) {
		return false;
	}
	// (4 units at a time, then back to the unit: a route's every step through deep water asks)
	for (float up = 4.0f; up <= SURFACE_UP + 4.0f; up += 4.0f) {
		if (!zone->watermap->InLiquid(p + glm::vec3(0.0f, 0.0f, up))) {
			for (top = up - 3.0f; top < up && zone->watermap->InLiquid(p + glm::vec3(0.0f, 0.0f, top)); top += 1.0f) {
			}
			return true;
		}
	}
	return false;
}

void HeadlessMover::SwimOnSurface(const glm::vec3 &start)
{
	m_swim = 0.0f;
	Client *me = m_stream->GetClient();
	if (!me || !zone || !zone->HasWaterMap() || m_path.empty()) {
		return;
	}
	// the navmesh lies on the bed, so a route through water walked the bottom with the head under (Lake
	// Rathetear's way to South Karana drowned characters; five more came up for air mid-lake, far from any
	// navmesh). A player swims on top unless what they're after is down there
	const float head = me->GetSize() * HEAD_SHARE;
	auto lifted = [&](const glm::vec3 &p, glm::vec3 &up) {
		float top = 0.0f;
		const glm::vec3 eye = p + glm::vec3(0.0f, 0.0f, head);
		if (!WaterTop(eye, top)) {
			return false; // (the head's out already)
		}
		up = glm::vec3(p.x, p.y, p.z + top + 1.0f);
		return !zone->zonemap || zone->zonemap->CheckLoS(eye, up + glm::vec3(0.0f, 0.0f, head)); // (a flooded tunnel's roof)
	};
	// a link's ends stay put (a ladder out of the water, a pad, a jump: the steps are known by them)
	auto link_end = [&](const glm::vec3 &p) {
		for (const auto *links : {&Ladders(), &Teleports(), &Jumps(), static_cast<const std::vector<Jump> *>(&m_climbs)}) {
			for (const auto &l : *links) {
				if (glm::length(p - l.from) < SWIM_MATCH || glm::length(p - l.to) < SWIM_MATCH) {
					return true;
				}
			}
		}
		return false;
	};
	std::vector<glm::vec3> out;
	glm::vec3 prev = start;  // (the last step put out)
	glm::vec3 from = start;  // (the route's last step)
	size_t    lifts = 0;
	bool      afloat = false; // (the last step put out was a swim's)
	// dive: a step between the corners whose way up is clear, but not the swim to it from the last step put out (an ice floe
	// in between, in Iceclad's sea): the route goes under it there, on the bed, and the steps after are looked at from there.
	// Else each was looked at from that last step, through the floe, none was lifted, and the bed corner beyond kept the head
	// under for 763 units: refused as too deep, and Ilvara petitioned back to the same spot all day (E-156, 3 November)
	auto take = [&](const glm::vec3 &q, bool dive) {
		glm::vec3 up;
		if (!lifted(q, up)) {
			return false;
		}
		if (!zone->zonemap || zone->zonemap->CheckLoS(prev + glm::vec3(0.0f, 0.0f, head), up + glm::vec3(0.0f, 0.0f, head))) {
			if (afloat) {
				m_swim += glm::length(up - prev);
			}
			out.push_back(up);
			prev   = up;
			afloat = true;
			++lifts;
			return true;
		}
		if (dive) {
			out.push_back(q);
			prev   = q;
			afloat = false;
		}
		return false;
	};
	for (size_t i = 0; i < m_path.size(); ++i) {
		const glm::vec3 &p    = m_path[i];
		// the destination: down to it at the end (a partial route's end isn't one: planned on from)
		const bool       last = i + 1 == m_path.size() && !m_partial;
		// a step's way between the route's corners, every SURFACE_STEP on the floor under it, as a player
		// wades in till the water's over the head and swims from there (the route's corners alone: a bank,
		// then the bed hundreds of units out, its line from the bank through the bank itself; Aldric's 5,000
		// units over Lake Rathetear to the Rathe Mountains, refused as too deep)
		if (!link_end(from) && !link_end(p)) {
			const float len = glm::length(p - from);
			const int   n   = static_cast<int>(std::ceil(len / SURFACE_STEP));
			for (int k = 1; k < n; ++k) {
				const glm::vec3 s = from + (p - from) * (static_cast<float>(k) / n);
				take(glm::vec3(s.x, s.y, FloorBelow(glm::vec3(s.x, s.y, std::max(from.z, p.z)))), true);
			}
		}
		from = p;
		if (!link_end(p) && take(p, false) && !last) { // (a corner that isn't lifted is put out below, as it is)
			continue; // (the last lifted too: then straight down to it)
		}
		out.push_back(p);
		prev   = p;
		afloat = false;
	}
	if (lifts) {
		LogHeadlessClient("[{}] swimming on the surface: [{}] of [{}] steps lifted out of the water", me->GetCleanName(), lifts, m_path.size());
		m_path = std::move(out);
	}
}

bool HeadlessMover::TooLongUnder(const glm::vec3 &start)
{
	Client *me = m_stream->GetClient();
	if (!me || !zone || !zone->HasWaterMap() || m_path.empty()) {
		return false;
	}
	for (uint32 z : BREATHE_ANYWAY) {
		if (zone->GetZoneID() == z) {
			return false;
		}
	}
	const float speed = AirSpeed(me);
	if (speed <= 0.0f || EffectTotal(me, SpellEffect::WaterBreathing) > 0) {
		return false;
	}
	const float head  = me->GetSize() * HEAD_SHARE;
	const float limit = MaxAir(me) * UNDER_SHARE * speed; // units with the head under, at a stretch
	auto wet = [&](const glm::vec3 &p) { return zone->watermap->InLiquid(p + glm::vec3(0.0f, 0.0f, head)); };
	// the stretch we're in already doesn't count: under water now, the way out is what we want
	bool  first = wet(start);
	float under = 0.0f;
	glm::vec3 prev = start;
	for (const auto &p : m_path) {
		const float len   = glm::length(p - prev);
		const int   steps = std::max(1, static_cast<int>(std::ceil(len / UNDER_STEP)));
		for (int i = 1; i <= steps; ++i) {
			const glm::vec3 at = prev + (p - prev) * (static_cast<float>(i) / steps);
			if (!wet(at)) {
				first = false;
				under = 0.0f;
				continue;
			}
			under += len / steps;
			if (!first && under > limit) {
				LogHeadlessClient("[{}] the route keeps the head under water over [{:.0f}] units from [{:.0f}, {:.0f}, {:.0f}] ([{}] seconds of air: [{:.0f}] units at a stretch)",
					me->GetCleanName(), under, at.x, at.y, at.z, MaxAir(me), limit);
				m_too_deep = true;
				return true;
			}
		}
		prev = p;
	}
	return false;
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
	// nearest first, each spot one we can walk straight to and a route starts from. In water we swim
	// there instead: a spot on the navmesh near our height with nothing in the way (Maahoud, in North
	// Qeynos's canal, the bank 8 units off: walked from the canal's bed, the bank was a wall)
	const bool wet = zone->HasWaterMap() && zone->watermap->InLiquid(pos);
	const char *who = m_stream->GetClient() ? m_stream->GetClient()->GetCleanName() : "?";
	for (float r = OFF_MESH_STEP; r <= (wet ? SWIM_OUT_MAX : OFF_MESH_MAX); r += OFF_MESH_STEP) {
		const int directions = wet ? std::max(OFF_MESH_DIRECTIONS, static_cast<int>(std::ceil(6.2831853f * r / SWIM_OUT_SPACING)))
								   : OFF_MESH_DIRECTIONS;
		for (int i = 0; i < directions; ++i) {
			const float a = i * 6.2831853f / directions;
			glm::vec3 spot(pos.x + r * std::cos(a), pos.y + r * std::sin(a), pos.z + STRAIGHT_RISE);
			if (wet) {
				float h = 0.0f;
				// a floor from SWIM_OUT_DZ below us to SWIM_UP_MAX above (the window's middle and half-height)
				if (!zone->HeadlessPathing()->NavHeight(glm::vec3(spot.x, spot.y, pos.z + (SWIM_UP_MAX - SWIM_OUT_DZ) / 2),
														(SWIM_UP_MAX + SWIM_OUT_DZ) / 2, h, MOVER_FLAGS) ||
					h < pos.z - SWIM_OUT_DZ || h > pos.z + SWIM_UP_MAX ||
					// (at the bank's height: from under the water a line up to it hits the canal's own wall,
					// where a swimmer comes up first, then over the edge)
					(zone->zonemap && !zone->zonemap->CheckLoS(glm::vec3(pos.x, pos.y, std::max(pos.z, h) + BODY_HEIGHT),
															   glm::vec3(spot.x, spot.y, h + BODY_HEIGHT))) ||
					!PlanRoute(glm::vec3(spot.x, spot.y, h), destination, m_through)) {
					continue;
				}
				spot.z = h;
			}
			else if ((spot.z = FloorBelow(spot)) == pos.z + STRAIGHT_RISE || !StraightWalkable(pos, spot) ||
					 !PlanRoute(spot, destination, m_through)) {
				continue;
			}
			LogHeadlessClient("[{}] off the navmesh at [{:.0f}, {:.0f}, {:.0f}]: {} [{:.0f}] units onto it",
				who, pos.x, pos.y, pos.z, wet ? "swimming" : "stepping", r);
			Note("off_mesh", pos, spot);
			m_path.insert(m_path.begin(), spot);
			m_jump_at.insert(m_jump_at.begin(), false);
			return true;
		}
	}
	if (!wet) {
		return false;
	}
	// no shore in the rings: the navmesh's nearest point at our height further off (a swim on the
	// surface takes no air), then, for one who breathes water (or where air doesn't matter), its nearest
	// under us (the lake bed: its navmesh has gaps, so rarely straight down). Swum to in a straight line
	Client *me = m_stream->GetClient();
	const bool breathes = me && (EffectTotal(me, SpellEffect::WaterBreathing) > 0 ||
								 std::find(std::begin(BREATHE_ANYWAY), std::end(BREATHE_ANYWAY), zone->GetZoneID()) != std::end(BREATHE_ANYWAY));
	struct Box { glm::vec3 at, half; const char *what; };
	const Box boxes[] = {
		{pos + glm::vec3(0.0f, 0.0f, (SWIM_UP_MAX - SWIM_OUT_DZ) / 2), glm::vec3(SWIM_FAR_MAX, SWIM_FAR_MAX, (SWIM_UP_MAX + SWIM_OUT_DZ) / 2), "the shore"},
		{pos - glm::vec3(0.0f, 0.0f, SINK_MAX / 2), glm::vec3(SINK_WIDE, SINK_WIDE, SINK_MAX / 2), "the bed"},
	};
	for (const auto &b : boxes) {
		glm::vec3 to;
		if (!zone->HeadlessPathing()->NavNearest(b.at, b.half, to, MOVER_FLAGS) ||
			(&b != &boxes[0] && to.z >= pos.z) ||
			// the bed is down there: for one who can't breathe water only if the swim there fits in the breath (as a route's
			// stretch under water does: UNDER_SHARE of the air at our speed), else it needs water breathing (an item or spell)
			(&b != &boxes[0] && !breathes && (!me || glm::length(to - pos) > MaxAir(me) * UNDER_SHARE * AirSpeed(me))) ||
			(zone->zonemap && !zone->zonemap->CheckLoS(pos, to + glm::vec3(0.0f, 0.0f, BODY_HEIGHT))) ||
			!PlanRoute(to, destination, m_through)) {
			continue;
		}
		LogHeadlessClient("[{}] afloat off the navmesh at [{:.0f}, {:.0f}, {:.0f}]: swimming [{:.0f}] units to {} at [{:.0f}, {:.0f}, {:.0f}]",
			who, pos.x, pos.y, pos.z, glm::length(to - pos), b.what, to.x, to.y, to.z);
		Note("off_mesh", pos, to);
		m_path.insert(m_path.begin(), to);
		m_jump_at.insert(m_jump_at.begin(), false);
		return true;
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
	m_checked_swim   = 0.0f;
	m_checked_path.clear();
	for (int leg = 0; leg <= PARTIAL_LEGS; ++leg) {
		if (!PlanRoute(from, destination, false) || m_path.empty()) {
			break;
		}
		m_checked_swim += m_swim;
		m_checked_path.insert(m_checked_path.end(), m_path.begin(), m_path.end());
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

std::vector<glm::vec4> HeadlessMover::Threats(const glm::vec3 &from, const glm::vec3 &to) const
{
	std::vector<glm::vec4> out;
	Client *me = m_stream->GetClient();
	if (!me) {
		return out;
	}
	// (the way's surroundings: a route round the mobs can stray from the straight line, but not far)
	const glm::vec2 a(from.x, from.y), b(to.x, to.y);
	const float     reach = glm::length(b - a) / 2.0f + THREAT_REACH;
	const glm::vec2 mid   = (a + b) / 2.0f;
	for (const auto &[id, npc] : entity_list.GetNPCList()) {
		if (!npc || npc->IsCorpse() || npc->GetHP() <= 0 || npc->GetOwner() || npc->GetAggroRange() <= 0.0f ||
			glm::length(glm::vec2(npc->GetX(), npc->GetY()) - mid) > reach) {
			continue;
		}
		// as the server's CheckWillAggro, ahead of time: hostile to us, not grey to us (the undead aggro
		// anyway), and able to see us (invisible, it's a way through)
		const FACTION_VALUE faction = me->GetReverseFactionCon(npc);
		if ((faction != FACTION_SCOWLS && faction != FACTION_THREATENINGLY) || me->IsInvisible(npc) ||
			(me->GetLevelCon(npc->GetLevel()) == ConsiderColor::Gray && npc->GetBodyType() != BodyType::Undead)) {
			continue;
		}
		out.emplace_back(npc->GetX(), npc->GetY(), npc->GetZ(), npc->GetAggroRange() + THREAT_MARGIN);
	}
	return out;
}

std::string HeadlessMover::Start(const glm::vec3 &destination, bool through_zone_lines, bool avoid)
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
	m_too_deep  = false;
	m_cant_jump = false;
	m_jumping   = SIZE_MAX;
	m_through = through_zone_lines;
	m_avoid   = through_zone_lines || avoid;
	m_final   = destination;
	m_legs    = 0;
	if (!PlanRoute(start, destination, through_zone_lines)) {
		if (StepOntoNavmesh(start, destination)) {
			// a few steps onto the navmesh, then the way from there
		}
		else if (!m_too_deep && !m_cant_jump && through_zone_lines && Flat(start, destination) <= ZONE_GAP && std::fabs(start.z - destination.z) <= ZONE_GAP_DZ &&
			StraightWalkable(start, destination)) {
			LogHeadlessClient("[{}] no navmesh route to the zone line [{:.0f}] units away: walking straight",
				me->GetCleanName(), Flat(start, destination));
			Note("straight_walk", start, destination);
			m_path = {destination};
			m_jump_at.clear();
			m_partial = false;
		}
		else {
			if (m_too_deep) {
				Note("too_deep", start, destination); // (this character's air, not the navmesh's fault)
			}
			else if (m_cant_jump) {
				Note("cant_jump", start, destination); // (this character's endurance, or jumping off: not the navmesh's)
			}
			else {
				NoteNoRoute(start, destination);
			}
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

	if (m_avoid) {
		LogHeadlessClient("[{}] keeping round [{}] mobs that would aggro us", me->GetCleanName(), Threats(start, destination).size());
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
	m_jump_at.clear();
	m_jumping     = SIZE_MAX;
	m_through     = false;
	m_avoid       = false;
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

// a stretch under water is planned at the speed we're sure to keep the whole way: not over the unbuffed run. Spirit of Wolf
// (40 against 28) let Ilvara plan Iceclad's eastern sea, 755 units under against 752; it wore off, and at 16 units a second
// the same stretch took half as long again: up for air with 35 seconds left, where no navmesh is in reach, a petition (E-165)
float HeadlessMover::AirSpeed(const Mob *me)
{
	return std::min(me->GetRunspeed(), me->GetBaseRunspeed()) * SPEED_TO_UNITS;
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
		if (OnTeleport(m_next)) {
			// a teleport pad: where it puts us, at once (costs none of the step)
			LogHeadlessClient("[{}] taking the teleport pad at [{:.0f}, {:.0f}, {:.0f}] to [{:.0f}, {:.0f}, {:.0f}]",
				me->GetCleanName(), pos.x, pos.y, pos.z, target.x, target.y, target.z);
			pos = target;
			m_next++;
			continue;
		}
		// on land the floor gives our height; in water we swim, and on a ladder climb, up and down as
		// well as across
		const bool swimming = zone->HasWaterMap() && zone->watermap->InLiquid(pos);
		const bool climbing = OnLadder(m_next);
		glm::vec3  to(target.x - pos.x, target.y - pos.y, swimming || climbing ? target.z - pos.z : 0.0f);
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
	// a jump (up a ledge, across a gap): off the floor, on an arc from where we jumped to where we land,
	// as high as the client's jump goes; the server takes its endurance as for a client's (OP_Jump)
	const bool jumping = m_next < m_path.size() && m_next < m_jump_at.size() && m_jump_at[m_next];
	if (jumping && m_jumping != m_next) {
		m_jumping   = m_next;
		m_jump_from = m_next > 0 ? m_path[m_next - 1] : m_pos;
		m_stream->Inject(new EQApplicationPacket(OP_Jump, 0));
		LogHeadlessClient("[{}] jumping from [{:.1f}, {:.1f}, {:.1f}] to [{:.1f}, {:.1f}, {:.1f}]", me->GetCleanName(),
			m_jump_from.x, m_jump_from.y, m_jump_from.z, m_path[m_next].x, m_path[m_next].y, m_path[m_next].z);
	}
	else if (!jumping) {
		m_jumping = SIZE_MAX;
	}
	if (on_platform) {
		pos.z = m_path[std::min(m_next, m_path.size() - 1)].z;
	}
	else if (jumping) {
		const glm::vec3 &land = m_path[m_next];
		const float      span = std::max(Flat(m_jump_from, land), 0.01f);
		const float      t    = std::clamp(1.0f - Flat(pos, land) / span, 0.0f, 1.0f);
		pos.z = m_jump_from.z + (land.z - m_jump_from.z) * t + 4.0f * RuleR(HeadlessClients, JumpHeight) * t * (1.0f - t);
	}
	else if (OnLadder(m_next)) {
		// on a ladder: the height the climb has got to (the floor under us is the one we left or the
		// one we're going to)
	}
	else {
		pos.z = me->GetFixedZ(pos);
		// a floor the navmesh has and the server's collision map doesn't (Crushbone's watchtowers, the
		// navmesh built from the client's geometry: off a ladder onto one, the map's floor was the ground
		// under it): where the map's floor is far below the route, the navmesh's floor at the route's
		// height, if it has one there (over a dip the route's line runs above the ground, and the navmesh
		// lies on the ground: none at that height, and the map's floor stands)
		const glm::vec3 &to   = m_path[std::min(m_next, m_path.size() - 1)];
		const glm::vec3 &from = m_next > 0 && m_next < m_path.size() ? m_path[m_next - 1] : to;
		const float      span = Flat(from, to);
		const float      t    = span > 0.01f ? std::clamp(1.0f - Flat(pos, to) / span, 0.0f, 1.0f) : 1.0f;
		const float      line = from.z + (to.z - from.z) * t;
		float            nav  = 0.0f;
		// (not in water: swimming up to the surface, the navmesh's floor under us put us back on it every
		// tick, and the walk was "stuck": Grukmar in Paludal, three hours)
		const bool       afloat = zone->HasWaterMap() && zone->watermap->InLiquid(pos);
		if (!afloat && pos.z < line - FLOOR_BELOW_NAV && zone->HeadlessPathing() &&
			zone->HeadlessPathing()->NavHeight(glm::vec3(pos.x, pos.y, line), NAV_FLOOR_DZ, nav, MOVER_FLAGS)) {
			pos.z = nav;
		}
	}

	// the client notices zone lines itself (the server knows them from the water map's regions):
	// walk into one only when zoning, otherwise stop short of it
	if (zone->HasWaterMap() && zone->watermap->InZoneLine(pos) && !zone->watermap->InZoneLine(m_pos)) {
		LogHeadlessClient("[{}] at a zone line (the water map's) at [{:.1f}, {:.1f}, {:.1f}]", me->GetCleanName(), pos.x, pos.y, pos.z);
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
		m_too_deep  = false;
		m_cant_jump = false;
		m_jumping   = SIZE_MAX;
		if (too_many || !PlanRoute(pos, m_final, m_through)) {
			if (!m_too_deep && !m_cant_jump && m_through && Flat(pos, m_final) <= ZONE_GAP && std::fabs(pos.z - m_final.z) <= ZONE_GAP_DZ &&
				StraightWalkable(pos, m_final)) {
				LogHeadlessClient("[{}] the navmesh ends [{:.0f}] units short of the zone line: walking the rest",
					me->GetCleanName(), Flat(pos, m_final));
				Note("straight_walk", pos, m_final);
				m_path = {m_final};
				m_jump_at.clear();
				m_partial = false;
				m_next = 0;
				return;
			}
			if (m_through) {
				LogHeadlessClient("[{}] no way to the zone line from [{:.0f}, {:.0f}, {:.0f}]: [{:.0f}] units short, and the map shows no straight way",
					me->GetCleanName(), pos.x, pos.y, pos.z, Flat(pos, m_final));
			}
			if (m_too_deep) {
				Note("too_deep", pos, m_final);
			}
			else if (m_cant_jump) {
				Note("cant_jump", pos, m_final);
			}
			else if (!too_many) {
				NoteNoRoute(pos, m_final);
			}
			Stop(Result::NoPath);
			return;
		}
		m_next = 0;
		return;
	}
	if (arrived) {
		if (m_stop_at_end) {
			LogHeadlessClient("[{}] the route ends at a zone line (or teleporter) at [{:.1f}, {:.1f}, {:.1f}]",
				me->GetCleanName(), pos.x, pos.y, pos.z);
		}
		Stop(m_stop_at_end ? Result::ZoneLine : Result::Arrived);
		return;
	}

	// no real progress for a while (rooted, blocked by geometry)
	// (up and down count: surfacing goes straight up, and counted flat it was "stuck" every 3 seconds and
	// stopped, coming up at 60% of its speed: Elandor drowned 50 units short from 340 down)
	if (glm::length(pos - m_progress_pos) >= STUCK_DISTANCE) {
		m_progress_pos = pos;
		m_progress_ms  = now;
	}
	else if (now - m_progress_ms > STUCK_MS) {
		const glm::vec3 &to = m_path[std::min(m_next, m_path.size() - 1)];
		LogHeadlessClient("[{}] stuck at [{:.1f}, {:.1f}, {:.1f}] (server [{:.1f}, {:.1f}, {:.1f}]), on step [{}] of [{}] to [{:.1f}, {:.1f}, {:.1f}], in water [{}], speed [{:.1f}]",
			me->GetCleanName(), pos.x, pos.y, pos.z, me->GetX(), me->GetY(), me->GetZ(), m_next, m_path.size(), to.x, to.y, to.z,
			zone->HasWaterMap() && zone->watermap->InLiquid(pos) ? "yes" : "no", RunSpeed(me));
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

		// clicked a moment ago by another of us: it's opening, wait for it rather than shut it again
		auto &clicked = g_door_clicked[door->GetEntityID()];
		if (now - clicked < DOOR_SHARED_MS) {
			m_door_entity  = door->GetEntityID();
			m_door_wait_ms = clicked + DOOR_WAIT_MS;
			SendUpdate(m_pos, me->GetHeading(), 0);
			LogHeadlessClient("[{}] door [{}] ({}) just clicked by another: waiting for it", me->GetCleanName(), door->GetDoorID(), door->GetDoorName());
			return true;
		}
		clicked = now;

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
