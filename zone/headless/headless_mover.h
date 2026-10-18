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
#pragma once

#include "common/event/timer.h"
#include "common/types.h"

#include "glm/vec3.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class NullStream;

// The client's half of walking somewhere. A real client simulates its own movement and
// reports positions; the server's Handle_OP_ClientUpdate then runs anti-cheat speed checks,
// aggro, proximity and broadcasts to other players. The mover does the client's part: it
// follows a navmesh route at the character's run speed, keeps to the floor, and injects the
// same OP_ClientUpdate packets a real client sends.
class HeadlessMover {
public:
	enum class Result { Arrived, Stopped, Stuck, NoPath, ZoneLine };

	using DoneCallback = std::function<void(Result, const glm::vec3 &where)>;

	HeadlessMover(NullStream *stream, DoneCallback on_done);
	~HeadlessMover();

	// plans a route and starts walking; returns "" or why it can't
	std::string Start(const glm::vec3 &destination);
	void Stop(Result why);
	// turn on the spot to face a point, as the client's /face; returns "" or why it can't
	std::string Face(const glm::vec3 &point);
	bool Moving() const { return m_moving; }

private:
	void Tick();
	void SendUpdate(const glm::vec3 &pos, float heading, uint32 animation);
	// a real client clicks closed doors it walks into; returns true while we wait for one to open
	bool HandleDoors(uint64 now);

	NullStream                 *m_stream;
	DoneCallback               m_on_done;
	std::unique_ptr<EQ::Timer> m_timer;

	std::vector<glm::vec3> m_path;
	glm::vec3              m_pos{0.0f};       // our simulated position, like a real client's
	size_t                 m_next        = 0;
	bool                   m_moving      = false;
	bool                   m_stop_at_end = false; // the route ends at a zone line / teleport
	uint16                 m_sequence    = 0;

	// stuck detection
	glm::vec3 m_progress_pos{0.0f};
	uint64    m_progress_ms = 0;

	// the door we clicked and are waiting on
	uint32 m_door_entity  = 0;
	uint64 m_door_wait_ms = 0;
};
