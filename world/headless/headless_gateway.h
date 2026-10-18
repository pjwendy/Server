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
#include "common/net/tcp_server.h"

#include "headless.pb.h"

#include <map>
#include <memory>
#include <string>

class ServerPacket;

// World-side endpoint for headless clients (server.world.headless in eqemu_config.json).
// Speaks length-prefixed protobuf (common/proto/headless.proto). Every connection is
// checked against the HeadlessClients:* rules; nothing is accepted while
// HeadlessClients:AllowHeadlessClients is false.
//
// After a valid Hello, world authorises the character with the normal ServerOP_ZoneIncClient
// and asks the character's zone to spawn it (ServerOP_HeadlessSpawn); the zone answers with
// ServerOP_HeadlessSpawned and the SDK gets a Welcome.
class HeadlessGateway {
public:
	HeadlessGateway(const std::string &ip, int port);
	~HeadlessGateway();

	// the running gateway, or nullptr when the listener isn't enabled
	static HeadlessGateway *Instance() { return s_instance; }

	// from zones (world/zoneserver.cpp)
	void HandleSpawned(ServerPacket *pack); // ServerOP_HeadlessSpawned
	void HandleGone(ServerPacket *pack);    // ServerOP_HeadlessGone
	void HandleZoneFrame(ServerPacket *pack); // ServerOP_HeadlessFrame: relay to the SDK as-is

private:
	enum class Phase {
		Handshake,      // waiting for / checking Hello
		WaitingForZone, // authenticated; the character's zone is booting
		Spawning,       // zone asked to spawn the character
		InGame,         // Welcome sent
	};

	struct Session {
		std::string                             id;
		std::string                             remote;
		std::shared_ptr<EQ::Net::TCPConnection> connection;
		std::string                             buffer;
		Phase                                   phase   = Phase::Handshake;
		bool                                    closing = false;
		std::unique_ptr<EQ::Timer>              handshake_timer;
		std::unique_ptr<EQ::Timer>              close_timer; // separate: Refuse() can run inside handshake_timer's callback

		// set once authenticated
		uint32      account_id     = 0;
		std::string account_name;
		std::string ls_id;
		uint32      lsaccount_id   = 0;
		int16       admin          = 0;
		uint32      character_id   = 0;
		std::string character;
		uint32      zone_id        = 0;
		uint32      zone_server_id = 0; // ZSList id once known
		uint32      wid            = 0; // ClientListEntry id
		uint32      spawn_id       = 0;
		uint64      deadline_ms    = 0; // for WaitingForZone / Spawning
	};

	void OnConnection(std::shared_ptr<EQ::Net::TCPConnection> connection);
	void OnRead(Session *s, const unsigned char *data, size_t length);
	void OnDisconnect(Session *s);
	void OnTick();

	void HandleFrame(Session *s, const eqheadless::ClientFrame &frame);
	void HandleHello(Session *s, const eqheadless::Hello &hello);
	bool CheckZoneAllowed(Session *s);
	void TrySpawn(Session *s);
	void SendDespawn(Session *s);

	Session *FindByPacketSession(const char *session_id, size_t length);
	int CountActive(uint32 account_id = 0) const;

	void Send(Session *s, const eqheadless::ServerFrame &frame);
	void SendBody(Session *s, const char *body, size_t length); // length-prefix a serialized frame
	void Refuse(Session *s, eqheadless::RefusedReason reason, const std::string &detail);
	void CloseSoon(Session *s);

	static HeadlessGateway *s_instance;

	std::unique_ptr<EQ::Net::TCPServer>              m_server;
	std::unique_ptr<EQ::Timer>                       m_tick;
	std::map<std::string, std::unique_ptr<Session>> m_sessions;
};
