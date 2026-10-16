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
#include "world/headless/headless_gateway.h"

#include "common/eqemu_logsys.h"
#include "common/rulesys.h"
#include "common/strings.h"
#include "common/util/uuid.h"
#include "world/headless/headless_tokens.h"
#include "world/worlddb.h"

#include "fmt/format.h"

namespace {
	constexpr uint32 MAX_FRAME_BYTES      = 64 * 1024;
	constexpr uint64 HANDSHAKE_TIMEOUT_MS = 10000;
	// TCPConnection::Disconnect() closes the socket immediately, which can cancel a
	// pending write; give the final frame a moment to flush first
	constexpr uint64 CLOSE_DELAY_MS       = 250;

	std::string ReasonName(eqheadless::RefusedReason reason)
	{
		// _Name() returns std::string or string_view depending on protobuf version
		return std::string(eqheadless::RefusedReason_Name(reason));
	}
}

HeadlessGateway::HeadlessGateway(const std::string &ip, int port)
{
	m_server = std::make_unique<EQ::Net::TCPServer>();
	m_server->Listen(
		ip, port, false, [this](std::shared_ptr<EQ::Net::TCPConnection> connection) {
			OnConnection(connection);
		}
	);
}

HeadlessGateway::~HeadlessGateway() = default;

void HeadlessGateway::OnConnection(std::shared_ptr<EQ::Net::TCPConnection> connection)
{
	auto s = std::make_unique<Session>();
	s->id         = EQ::Util::UUID::Generate().ToString();
	s->remote     = fmt::format("{}:{}", connection->RemoteIP(), connection->RemotePort());
	s->connection = connection;

	Session *session = s.get();
	m_sessions.emplace(s->id, std::move(s));

	connection->OnRead(
		[this, session](EQ::Net::TCPConnection *, const unsigned char *data, size_t length) {
			OnRead(session, data, length);
		}
	);
	connection->OnDisconnect([this, session](EQ::Net::TCPConnection *) { OnDisconnect(session); });

	// a client that never says Hello gets dropped
	session->handshake_timer = std::make_unique<EQ::Timer>(
		HANDSHAKE_TIMEOUT_MS, false, [this, session](EQ::Timer *) {
			if (!session->handshake_done) {
				Refuse(session, eqheadless::REFUSED_TIMEOUT, "no Hello received");
			}
		}
	);

	connection->Start();

	LogHeadlessClient("Connection [{}] from [{}]", session->id, session->remote);
}

void HeadlessGateway::OnRead(Session *s, const unsigned char *data, size_t length)
{
	if (s->closing) {
		return;
	}

	s->buffer.append(reinterpret_cast<const char *>(data), length);

	while (s->buffer.size() >= 4 && !s->closing) {
		const auto *b = reinterpret_cast<const unsigned char *>(s->buffer.data());
		const uint32 frame_length = (uint32(b[0]) << 24) | (uint32(b[1]) << 16) | (uint32(b[2]) << 8) | uint32(b[3]);

		if (frame_length > MAX_FRAME_BYTES) {
			Refuse(s, eqheadless::REFUSED_BAD_REQUEST, fmt::format("frame of {} bytes exceeds {}", frame_length, MAX_FRAME_BYTES));
			return;
		}

		if (s->buffer.size() < 4 + frame_length) {
			return; // wait for the rest of the frame
		}

		eqheadless::ClientFrame frame;
		const bool parsed = frame.ParseFromArray(s->buffer.data() + 4, static_cast<int>(frame_length));
		s->buffer.erase(0, 4 + frame_length);

		if (!parsed) {
			Refuse(s, eqheadless::REFUSED_BAD_REQUEST, "frame is not a valid ClientFrame");
			return;
		}

		HandleFrame(s, frame);
	}
}

void HeadlessGateway::OnDisconnect(Session *s)
{
	LogHeadlessClient("Connection [{}] from [{}] closed", s->id, s->remote);
	m_sessions.erase(s->id); // destroys s
}

void HeadlessGateway::HandleFrame(Session *s, const eqheadless::ClientFrame &frame)
{
	if (!s->handshake_done) {
		if (frame.payload_case() != eqheadless::ClientFrame::kHello) {
			Refuse(s, eqheadless::REFUSED_BAD_REQUEST, "first frame must be Hello");
			return;
		}

		HandleHello(s, frame.hello());
		return;
	}

	// M0 never completes a handshake, so nothing else is accepted yet
	Refuse(s, eqheadless::REFUSED_BAD_REQUEST, "unexpected frame");
}

void HeadlessGateway::HandleHello(Session *s, const eqheadless::Hello &hello)
{
	s->handshake_done = true;

	LogHeadlessClient(
		"Hello on [{}] from [{}] character [{}] sdk [{}] protocol [{}]",
		s->id,
		s->remote,
		hello.character(),
		hello.sdk_version(),
		hello.protocol_version()
	);

	// checked first so a disabled server reveals nothing about tokens or characters
	if (!RuleB(HeadlessClients, AllowHeadlessClients)) {
		Refuse(s, eqheadless::REFUSED_DISABLED, "headless clients are disabled on this server");
		return;
	}

	if (hello.protocol_version() != eqheadless::PROTOCOL_VERSION_CURRENT) {
		Refuse(
			s,
			eqheadless::REFUSED_PROTOCOL_VERSION,
			fmt::format("server speaks protocol version {}", static_cast<int>(eqheadless::PROTOCOL_VERSION_CURRENT))
		);
		return;
	}

	auto verified = HeadlessTokens::Verify(database, hello.token());
	if (!verified) {
		Refuse(s, eqheadless::REFUSED_BAD_TOKEN, "unknown or revoked token");
		return;
	}

	auto results = database.QueryDatabase(
		fmt::format(
			"SELECT `id` FROM `character_data` WHERE `name` = '{}' AND `account_id` = {} AND `deleted_at` IS NULL LIMIT 1",
			Strings::Escape(hello.character()),
			verified->account_id
		)
	);
	if (!results.Success() || results.RowCount() == 0) {
		Refuse(s, eqheadless::REFUSED_UNKNOWN_CHARACTER, "no such character on this token's account");
		return;
	}

	if (RuleI(HeadlessClients, MaxTotal) <= 0) {
		Refuse(s, eqheadless::REFUSED_LIMIT, "HeadlessClients:MaxTotal is 0");
		return;
	}

	Refuse(s, eqheadless::REFUSED_NOT_IMPLEMENTED, "authenticated; spawning headless characters is not implemented yet");
}

void HeadlessGateway::Send(Session *s, const eqheadless::ServerFrame &frame)
{
	std::string body;
	if (!frame.SerializeToString(&body)) {
		LogError("Headless gateway failed to serialise frame for [{}]", s->id);
		return;
	}

	const uint32 n = static_cast<uint32>(body.size());
	std::string  out;
	out.reserve(4 + body.size());
	out.push_back(static_cast<char>((n >> 24) & 0xFF));
	out.push_back(static_cast<char>((n >> 16) & 0xFF));
	out.push_back(static_cast<char>((n >> 8) & 0xFF));
	out.push_back(static_cast<char>(n & 0xFF));
	out += body;

	s->connection->Write(out.data(), out.size());
}

void HeadlessGateway::Refuse(Session *s, eqheadless::RefusedReason reason, const std::string &detail)
{
	if (s->closing) {
		return;
	}

	LogInfo("Headless connection [{}] from [{}] refused [{}] [{}]", s->id, s->remote, ReasonName(reason), detail);

	eqheadless::ServerFrame frame;
	auto *refused = frame.mutable_refused();
	refused->set_reason(reason);
	refused->set_detail(detail);
	Send(s, frame);

	CloseSoon(s);
}

void HeadlessGateway::CloseSoon(Session *s)
{
	s->closing     = true;
	s->close_timer = std::make_unique<EQ::Timer>(
		CLOSE_DELAY_MS, false, [s](EQ::Timer *) {
			// Disconnect() triggers OnDisconnect asynchronously, which erases the session
			s->connection->Disconnect();
		}
	);
}
