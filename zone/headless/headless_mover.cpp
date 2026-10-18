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
#include "zone/client.h"
#include "zone/doors.h"
#include "zone/entity.h"
#include "zone/headless/null_stream.h"
#include "zone/pathfinder_interface.h"
#include "zone/zone.h"

#include "glm/geometric.hpp"

#include <chrono>

extern Zone       *zone;
extern EntityList entity_list;

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
}

HeadlessMover::~HeadlessMover() = default;

std::string HeadlessMover::Start(const glm::vec3 &destination)
{
	Client *me = m_stream->GetClient();
	if (!me) {
		return "character is not in the zone";
	}

	if (m_moving) {
		Stop(Result::Stopped);
	}

	const glm::vec3 start(me->GetX(), me->GetY(), me->GetZ());
	m_path.clear();
	m_stop_at_end = false;

	if (zone->pathing) {
		bool partial = false;
		bool stuck   = false;
		auto route   = zone->pathing->FindRoute(start, destination, partial, stuck);
		for (auto &node : route) {
			if (node.teleport) {
				// zone lines and teleporters need zoning (M5); walk up to them and stop
				m_stop_at_end = true;
				break;
			}
			m_path.push_back(node.pos);
		}
		if (m_path.empty() || stuck) {
			m_on_done(Result::NoPath, start);
			return "";
		}
	}
	else {
		m_path.push_back(destination); // no navmesh: walk straight, following the floor
	}

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
		zone->pathing ? "yes" : "no",
		me->GetRunspeed(),
		me->GetRunspeed() * SPEED_TO_UNITS
	);
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

	if (HandleDoors(now)) {
		m_progress_ms = now; // waiting for a door isn't being stuck
		return;
	}
	if (!m_moving) {
		return; // HandleDoors gave up on a door
	}

	const float  speed = me->GetRunspeed() * SPEED_TO_UNITS; // units per second; 0 when rooted/stunned
	float        step  = speed * (TICK_MS / 1000.0f);

	glm::vec3 pos = m_pos;
	while (step > 0.0f && m_next < m_path.size()) {
		const glm::vec3 &target = m_path[m_next];
		glm::vec3 to(target.x - pos.x, target.y - pos.y, 0.0f);
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

	// keep to the floor, as the client's physics would
	pos.z = me->GetFixedZ(pos);
	m_pos = pos;

	const bool  arrived = m_next >= m_path.size();
	const auto &facing  = arrived ? m_path.back() : m_path[m_next];
	const float heading = (facing.x == pos.x && facing.y == pos.y) ? me->GetHeading() : me->CalculateHeadingToTarget(facing.x, facing.y);

	SendUpdate(pos, heading, arrived ? 0 : static_cast<uint32>(me->GetRunspeed()));

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
