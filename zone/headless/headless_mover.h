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

class Client;
class Doors;
class NullStream;

// The client's half of walking somewhere. A real client simulates its own movement and
// reports positions; the server's Handle_OP_ClientUpdate then runs anti-cheat speed checks,
// aggro, proximity and broadcasts to other players. The mover does the client's part: it
// follows a navmesh route at the character's run speed, keeps to the floor, and injects the
// same OP_ClientUpdate packets a real client sends. It opens doors on the way (when it has the
// key), and rides lifts when that's much shorter than walking round: click the call button, wait
// for the platform, step on, ride it up or down, step off. In water it swims, as the client does: in any
// direction, up and down too; a route that takes a swim link (the navmesh's way through water its
// polygons don't cover, from puppeteer's tools/navsweep.py) follows the way through the water written
// beside the zone's navmesh (maps/nav/<zone>.swims.json).
class HeadlessMover {
public:
	enum class Result { Arrived, Stopped, Stuck, NoPath, ZoneLine, Locked };

	using DoneCallback = std::function<void(Result, const glm::vec3 &where)>;

	HeadlessMover(NullStream *stream, DoneCallback on_done);
	~HeadlessMover();

	// plans a route and starts walking; returns "" or why it can't. A walk stops at a zone line
	// (Result::ZoneLine) without stepping in, unless through_zone_lines: then it walks into it.
	std::string Start(const glm::vec3 &destination, bool through_zone_lines = false);
	void Stop(Result why);
	// plan a route without walking (a walk under way carries on): its length, or -1 if there's
	// no way there
	float Check(const glm::vec3 &destination);
	// under water: swim straight up to the surface (a client's player does when short of air); "" or why not
	std::string Surface();
	// turn on the spot to face a point, as the client's /face; returns "" or why it can't
	std::string Face(const glm::vec3 &point);
	// accept a server-side move within the zone (#goto, summon), as the client does
	void Place(const glm::vec3 &pos, float heading);
	bool Moving() const { return m_moving; }
	// where the last walk found the navmesh wanting: why it failed, or a gap it went round (for
	// fixing the zone's navmesh: MoveEvent.nav). kind: "no_mesh_here", "no_mesh_there",
	// "not_connected" (closest: where the navmesh gets closest), "stuck", "off_mesh",
	// "straight_walk"; and doors: "locked" (to: the door; this character can't open it), "door_wont_open"
	struct NavNote {
		std::string kind;
		glm::vec3   at{0.0f};
		glm::vec3   to{0.0f};
		glm::vec3   closest{0.0f};
	};
	const std::vector<NavNote> &Notes() const { return m_notes; }
	// how fast a walk goes for this character now, in units per second (0 when rooted, stunned or mezzed)
	static float RunSpeed(const Mob *me);
	// breath, as the client keeps it (RoF2's eqgame.exe, read with Ghidra: the breath routine runs once a
	// second): air goes down a second at a time while the head is under water, unless something lets the
	// character breathe there (spell effect 14), and is back to full the moment it can breathe; at none
	// left, drowning takes an eighth of the character's hit points a second, reported to the server as
	// the client does (OP_EnvDamage, drowning). Air in seconds, now and at most
	int Air(); // (not counted yet, just in: full)
	// what last hurt us that wasn't a mob (drowning), if it was within the last few seconds: a death
	// with no killer names it
	std::string RecentEnvironmentDamage(uint64 within_ms = 3000) const;
	static int MaxAir(Client *me);
	static bool CanBreathe(Client *me);

private:
	void Tick();
	void Breathe(); // once a second
	void Go(Client *me, const glm::vec3 &start); // m_path planned: stand up and start walking it
	// fills m_path (and the lift plan) for a walk from start; false if there's no way there
	bool PlanRoute(const glm::vec3 &start, const glm::vec3 &destination, bool through_zone_lines);
	void SendUpdate(const glm::vec3 &pos, float heading, uint32 animation);
	// a real client clicks closed doors it walks into; returns true while we wait for one to open
	bool HandleDoors(uint64 now);
	// what the server's click handler would let this character open (key, keyring, guild, GM)
	static bool CanOpen(Client *me, Doors *door);

	// a lift: a platform door (open type 59) that moves `door_param` units up from where it
	// rests, called by buttons at the bottom and the top
	struct Lift {
		uint8     bottom_button = 0;
		uint8     top_button    = 0;
		glm::vec3 bottom{0.0f};  // standing on the platform, down
		glm::vec3 top{0.0f};     // ... and up
		glm::vec3 bottom_landing{0.0f}; // the call buttons, on each floor
		glm::vec3 top_landing{0.0f};
	};
	static std::vector<Lift> FindLifts();
	// the zone's swims: a navmesh link through water, and the way through the water between its ends
	struct Swim {
		glm::vec3              from{0.0f};
		glm::vec3              to{0.0f};
		std::vector<glm::vec3> points; // from and to included
	};
	static const std::vector<Swim> &Swims();
	// m_path (planned from start) takes a swim link: its ends become the way through the water
	void FollowSwims(const glm::vec3 &start);
	// the zone's jumps: a navmesh link up a ledge higher than a stair, or across a gap, that a player
	// jumps (puppeteer's sweep adds them where nothing walks: maps/nav/<zone>.jumps.json)
	struct Jump {
		glm::vec3 from{0.0f};
		glm::vec3 to{0.0f};
	};
	static const std::vector<Jump> &Jumps();
	// the zone's ladders: a link a player climbs straight up, or drops down (maps/nav/<zone>.ladders.json,
	// from puppeteer's navpatch: Cabilis East's guild hall, a ladder a floor)
	static const std::vector<Jump> &Ladders();
	// jumps or ladders ("jumps", "ladders"): the zone's maps/nav/<zone>.<kind>.json, read once per zone
	static const std::vector<Jump> &Links(const std::string &kind);
	// the step to m_path[i] climbs (or drops down) a ladder: up and down as well as across, off the floor
	bool OnLadder(size_t i) const;
	// the zone's teleport pads (maps/nav/<zone>.teleports.json: a zone point into the zone itself, Erudin's
	// between the upper city and the harbour): a link from beside the pad to where it puts you
	static const std::vector<Jump> &Teleports();
	// the step to m_path[i] is a teleport pad's: taken at once, as the pad moves a player
	bool OnTeleport(size_t i) const;
	// marks the steps of m_path (planned from start) that take a jump link (m_jump_at); false, and
	// m_cant_jump, when it takes one this character can't jump now (jumping off, or too tired)
	bool MarkJumps(const glm::vec3 &start);
	// what a jump costs in endurance, as the server takes it for a client's (Handle_OP_Jump)
	static int JumpCost(Client *me);
	// m_path (planned from start) keeps the head under water longer at a stretch than this character's
	// air allows (UNDER_SHARE of it), with no water breathing: sets m_too_deep
	bool TooLongUnder(const glm::vec3 &start);
	// replaces m_path with a route through a lift when that's much shorter; true if it did
	bool PlanWithLift(const glm::vec3 &start, const glm::vec3 &destination, float direct_length);
	// the lift part of a walk; returns true while the lift has us (calling it, riding it)
	bool HandleLift(uint64 now);
	// the zone's map shows a way straight there: floor all along, no step higher than a stair, and
	// nothing in the way at body height
	static bool StraightWalkable(const glm::vec3 &from, const glm::vec3 &to);
	// standing in a hole in the navmesh (a rock, a corner it left out): step straight to the nearest
	// spot a route starts from; true if m_path now goes there and on
	bool StepOntoNavmesh(const glm::vec3 &pos, const glm::vec3 &destination);
	void Note(const char *kind, const glm::vec3 &at, const glm::vec3 &to, const glm::vec3 &closest = glm::vec3(0.0f));
	// no route from `from` to `to`: notes which of no_mesh_here, no_mesh_there, not_connected
	void NoteNoRoute(const glm::vec3 &from, const glm::vec3 &to);
	// a navmesh route, or {} with *length = -1 if there isn't one
	static std::vector<glm::vec3> Route(const glm::vec3 &from, const glm::vec3 &to, float *length);

	NullStream                 *m_stream;
	DoneCallback               m_on_done;
	std::unique_ptr<EQ::Timer> m_timer;

	std::vector<glm::vec3> m_path;
	glm::vec3              m_pos{0.0f};       // our simulated position, like a real client's
	size_t                 m_next        = 0;
	bool                   m_moving      = false;
	bool                   m_stop_at_end = false; // the route ends at a zone line / teleport
	bool                   m_through     = false; // walk into zone lines (ZoneTo)
	uint16                 m_sequence    = 0;
	std::vector<NavNote>   m_notes;
	std::unique_ptr<EQ::Timer> m_breath_timer;
	int                    m_air = -1; // seconds of air left (-1: not counted yet, so full)
	bool                   m_too_deep = false; // the last plan was refused for keeping the head under too long
	bool                   m_cant_jump = false; // ... or for a jump this character can't make
	std::vector<bool>      m_jump_at;           // m_jump_at[i]: the step to m_path[i] is a jump
	size_t                 m_jumping = SIZE_MAX; // the step being jumped (m_path index), and from where
	glm::vec3              m_jump_from{0.0f};
	uint64                 m_env_damage_at = 0; // when something not a mob last hurt us (RecentEnvironmentDamage)
	std::string            m_env_damage;        // ... what it was

	// stuck detection
	glm::vec3 m_progress_pos{0.0f};
	uint64    m_progress_ms = 0;

	// the door we clicked and are waiting on
	uint32 m_door_entity  = 0;
	uint64 m_door_wait_ms = 0;

	// a lift on the route: m_path[m_board] is the platform where we get on, m_path[m_board + 1]
	// the same platform at the other floor, m_path[m_board - 1] the call button's landing
	// a partial route being walked in legs towards m_final
	glm::vec3 m_final{0.0f};
	bool      m_partial = false;
	int       m_legs    = 0;

	enum class LiftPhase { None, ToButton, Calling, Boarding, Riding, Off };
	LiftPhase m_lift_phase  = LiftPhase::None;
	size_t    m_board       = 0;
	uint8     m_lift_button = 0;
	uint64    m_lift_ms     = 0;   // calling: when the platform should be here; riding: when we got on
};
