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
#include "zone/headless/headless_mover.h"

#include <array>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Client;
namespace EQ { struct ItemData; }
class EQApplicationPacket;
class Mob;
class NullStream;
class ServerPacket;

namespace eqheadless {
	class Command;
	class Item;
	class ServerFrame;
	class Spawn;
}

// Zone-side lifecycle of headless characters. World authorises the character with the normal
// ServerOP_ZoneIncClient, then asks for it with ServerOP_HeadlessSpawn; the character is an
// ordinary Client whose stream is a NullStream. Once it's in the zone, its state is published
// back through world to the SDK (a Snapshot, then StateDelta messages).
class HeadlessClientManager {
public:
	static HeadlessClientManager *Instance()
	{
		// intentionally never destroyed: Clients (and their streams) can be torn down during
		// process shutdown after function-local statics are gone
		static auto *instance = new HeadlessClientManager();
		return instance;
	}

	void HandleSpawn(ServerPacket *pack);   // ServerOP_HeadlessSpawn
	void HandleDespawn(ServerPacket *pack); // ServerOP_HeadlessDespawn
	void HandleCommand(ServerPacket *pack); // ServerOP_HeadlessCommand

	// called by NullStream
	void ReportSpawned(const std::string &session_id, uint32 spawn_id);
	void OnStreamDestroyed(const std::string &session_id, bool was_spawned);
	void OnOutgoing(const std::string &session_id, const EQApplicationPacket *app);

	static bool IsHeadless(Mob *mob);

private:
	// what the SDK last saw of a spawn, to send only changes
	struct SeenSpawn {
		float  x, y, z, heading;
		uint32 hp_pct;
		uint32 level;
		std::string name;
		uint32 target_id;
		uint32 mana_pct;
	};

	struct Session {
		NullStream                              *stream = nullptr; // owned by its Client
		std::unique_ptr<EQ::Timer>               publish_timer;
		std::unique_ptr<EQ::Timer>               history_timer; // OP_FloatListThing, as real clients send
		std::unique_ptr<HeadlessMover>           mover;
		std::unordered_map<uint32, SeenSpawn>    seen;
		std::string                              last_self; // serialized Self last sent
		bool                                     zoning      = false; // the server let us go to zone_to
		uint32                                   zone_to     = 0;
		uint32                                   instance_to = 0;
		uint32                                   zone_line_to = 0; // ZoneTo: the zone whose line we're walking to
		// casting and memorising, as the client tracks them
		std::array<uint64, 12>                   gem_ready_ms{};   // per gem: refreshing until
		uint64                                   mem_until_ms = 0; // memorising until
		std::unique_ptr<EQ::Timer>               mem_timer;
		// looting: the corpse we asked to loot, and the coin it had
		uint32                                   loot_corpse = 0;
		uint32                                   loot_coin[4] = {};
		// the corpse's items still to loot, one at a time: a corpse refuses a loot within 10ms of
		// the last (m_loot_cooldown_timer), so a real client's Loot All spaces them out too
		std::deque<std::pair<int16, std::string>> loot_queue; // (loot slot, item name)
		std::vector<std::string>                 loot_names;  // what we looted, for the event
		std::unique_ptr<EQ::Timer>               loot_timer;
		std::string                              last_book;      // serialized SpellBook last sent
		std::string                              last_inventory; // serialized Inventory last sent
		std::string                              last_bots;      // serialized OwnedBots last sent
	};

	// returns "" when accepted, otherwise why not
	std::string Execute(const std::string &session_id, Session &session, Client *me, const eqheadless::Command &cmd);
	void SendMovementHistory(Session &session);
	void SendResult(const std::string &session_id, uint32 command_id, const std::string &error);

	void Publish(const std::string &session_id, bool snapshot);
	void FillSpawn(eqheadless::Spawn *out, Mob *mob);
	void SendFrame(const std::string &session_id, const eqheadless::ServerFrame &frame);
	void SendSpawned(const std::string &session_id, uint32 spawn_id, const std::string &error);
	void SendZoneEvent(const std::string &session_id, int status, uint32 zone_id, const std::string &detail);
	void RequestZoneLine(const std::string &session_id, Session &session, const glm::vec3 &where);
	static void FillItem(eqheadless::Item *out, const EQ::ItemData *item, Mob *holder);

	std::map<std::string, Session> m_sessions; // session id -> session
};
