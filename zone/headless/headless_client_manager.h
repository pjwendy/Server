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

#include "common/types.h"

#include <map>
#include <string>

class NullStream;
class ServerPacket;

// Zone-side lifecycle of headless characters. World authorises the character with the normal
// ServerOP_ZoneIncClient, then asks for it with ServerOP_HeadlessSpawn; the character is an
// ordinary Client whose stream is a NullStream.
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

	// called by NullStream
	void ReportSpawned(const std::string &session_id, uint32 spawn_id);
	void OnStreamDestroyed(const std::string &session_id, bool was_spawned);

private:
	void SendSpawned(const std::string &session_id, uint32 spawn_id, const std::string &error);

	std::map<std::string, NullStream *> m_streams; // session id -> stream (owned by its Client)
};
