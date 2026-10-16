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

// World-side endpoint for headless clients (server.world.headless in eqemu_config.json).
// Speaks length-prefixed protobuf (common/proto/headless.proto). Every connection is
// checked against the HeadlessClients:* rules; nothing is accepted while
// HeadlessClients:AllowHeadlessClients is false.
class HeadlessGateway {
public:
	HeadlessGateway(const std::string &ip, int port);
	~HeadlessGateway();

private:
	struct Session {
		std::string                             id;
		std::string                             remote;
		std::shared_ptr<EQ::Net::TCPConnection> connection;
		std::string                             buffer;
		bool                                    handshake_done = false;
		bool                                    closing        = false;
		std::unique_ptr<EQ::Timer>              handshake_timer;
		std::unique_ptr<EQ::Timer>              close_timer; // separate: Refuse() can run inside handshake_timer's callback
	};

	void OnConnection(std::shared_ptr<EQ::Net::TCPConnection> connection);
	void OnRead(Session *s, const unsigned char *data, size_t length);
	void OnDisconnect(Session *s);

	void HandleFrame(Session *s, const eqheadless::ClientFrame &frame);
	void HandleHello(Session *s, const eqheadless::Hello &hello);

	void Send(Session *s, const eqheadless::ServerFrame &frame);
	void Refuse(Session *s, eqheadless::RefusedReason reason, const std::string &detail);
	void CloseSoon(Session *s);

	std::unique_ptr<EQ::Net::TCPServer>              m_server;
	std::map<std::string, std::unique_ptr<Session>> m_sessions;
};
