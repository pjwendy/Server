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
#include "common/servertalk.h"
#include "common/strings.h"
#include "common/util/uuid.h"
#include "common/zone_store.h"
#include "world/cliententry.h"
#include "world/clientlist.h"
#include "world/headless/headless_tokens.h"
#include "world/worlddb.h"
#include "world/zonelist.h"
#include "world/zoneserver.h"

#include "fmt/format.h"

#include <chrono>

namespace {
	constexpr uint32 MAX_FRAME_BYTES      = 64 * 1024;
	constexpr uint64 HANDSHAKE_TIMEOUT_MS = 10000;
	constexpr uint64 ZONE_BOOT_TIMEOUT_MS = 30000;
	constexpr uint64 SPAWN_TIMEOUT_MS     = 30000;
	constexpr uint64 TICK_MS              = 500;
	// TCPConnection::Disconnect() closes the socket immediately, which can cancel a
	// pending write; give the final frame a moment to flush first
	constexpr uint64 CLOSE_DELAY_MS       = 250;
	constexpr uint32 LOOPBACK_IP          = 0x0100007F; // 127.0.0.1, network byte order

	uint64 NowMs()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()
		).count();
	}

	std::string ReasonName(eqheadless::RefusedReason reason)
	{
		// _Name() returns std::string or string_view depending on protobuf version
		return std::string(eqheadless::RefusedReason_Name(reason));
	}

	bool ZoneInList(const std::string &list, const std::string &short_name)
	{
		for (auto &z : Strings::Split(list, ',')) {
			if (Strings::ToLower(Strings::Trim(z)) == Strings::ToLower(short_name)) {
				return true;
			}
		}
		return false;
	}
}

HeadlessGateway *HeadlessGateway::s_instance = nullptr;

HeadlessGateway::HeadlessGateway(const std::string &ip, int port)
{
	s_instance = this;

	m_server = std::make_unique<EQ::Net::TCPServer>();
	m_server->Listen(
		ip, port, false, [this](std::shared_ptr<EQ::Net::TCPConnection> connection) {
			OnConnection(connection);
		}
	);

	m_tick = std::make_unique<EQ::Timer>(TICK_MS, true, [this](EQ::Timer *) { OnTick(); });
}

HeadlessGateway::~HeadlessGateway()
{
	s_instance = nullptr;
}

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
			if (session->phase == Phase::Handshake) {
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

	// the character stays in the zone until the zone hears the session is gone
	SendDespawn(s);
	m_sessions.erase(s->id); // destroys s
}

void HeadlessGateway::OnTick()
{
	const bool allowed = RuleB(HeadlessClients, AllowHeadlessClients);
	const uint64 now   = NowMs();

	for (auto &[id, session] : m_sessions) {
		Session *s = session.get();
		if (s->closing || s->phase == Phase::Handshake) {
			continue;
		}

		// turning the rule off at runtime removes every headless character
		if (!allowed) {
			SendDespawn(s);
			Refuse(s, eqheadless::REFUSED_DISABLED, "headless clients were disabled on this server");
			continue;
		}

		switch (s->phase) {
			case Phase::WaitingForZone:
				if (ZSList::Instance()->FindByZoneID(s->zone_id)) {
					TrySpawn(s);
				}
				else if (now > s->deadline_ms) {
					Refuse(s, eqheadless::REFUSED_ZONE_UNAVAILABLE, fmt::format("zone {} did not start", ZoneName(s->zone_id, true)));
				}
				break;
			case Phase::Spawning:
				if (now > s->deadline_ms) {
					SendDespawn(s);
					Refuse(s, eqheadless::REFUSED_SPAWN_FAILED, "timed out waiting for the zone to load the character");
				}
				break;
			case Phase::InGame:
				// the zone process went away (crash or shutdown)
				if (!ZSList::Instance()->FindByID(s->zone_server_id)) {
					LogInfo("Headless session [{}] lost its zone server; closing", s->id);
					CloseSoon(s);
				}
				break;
			default:
				break;
		}
	}
}

void HeadlessGateway::HandleFrame(Session *s, const eqheadless::ClientFrame &frame)
{
	if (s->phase == Phase::Handshake) {
		if (frame.payload_case() != eqheadless::ClientFrame::kHello) {
			Refuse(s, eqheadless::REFUSED_BAD_REQUEST, "first frame must be Hello");
			return;
		}

		HandleHello(s, frame.hello());
		return;
	}

	// commands arrive in M2; until then anything after Hello is ignored
	LogHeadlessClientDetail("Ignoring frame on [{}]: commands are not implemented yet", s->id);
}

void HeadlessGateway::HandleHello(Session *s, const eqheadless::Hello &hello)
{
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
			"SELECT c.`id`, c.`name`, c.`zone_id`, c.`zone_instance`, a.`name`, a.`ls_id`, a.`lsaccount_id`, a.`status` "
			"FROM `character_data` c JOIN `account` a ON a.`id` = c.`account_id` "
			"WHERE c.`name` = '{}' AND c.`account_id` = {} AND c.`deleted_at` IS NULL LIMIT 1",
			Strings::Escape(hello.character()),
			verified->account_id
		)
	);
	if (!results.Success() || results.RowCount() == 0) {
		Refuse(s, eqheadless::REFUSED_UNKNOWN_CHARACTER, "no such character on this token's account");
		return;
	}

	auto row = results.begin();
	s->account_id   = verified->account_id;
	s->character_id = Strings::ToUnsignedInt(row[0]);
	s->character    = row[1];
	s->zone_id      = Strings::ToUnsignedInt(row[2]);
	const uint32 instance_id = Strings::ToUnsignedInt(row[3]);
	s->account_name = row[4] ? row[4] : "";
	s->ls_id        = row[5] ? row[5] : "";
	s->lsaccount_id = Strings::ToUnsignedInt(row[6]);
	s->admin        = static_cast<int16>(Strings::ToInt(row[7]));

	// limits; this session isn't counted yet
	const int max_total = RuleI(HeadlessClients, MaxTotal);
	if (max_total <= 0 || CountActive() >= max_total) {
		Refuse(s, eqheadless::REFUSED_LIMIT, fmt::format("HeadlessClients:MaxTotal is {}", max_total));
		return;
	}
	const int max_per_account = RuleI(HeadlessClients, MaxPerAccount);
	if (CountActive(s->account_id) >= max_per_account) {
		Refuse(s, eqheadless::REFUSED_LIMIT, fmt::format("HeadlessClients:MaxPerAccount is {}", max_per_account));
		return;
	}

	// already online, as a real client or another headless session
	auto *cle = ClientList::Instance()->FindCharacter(s->character.c_str());
	if (cle && cle->Online() >= CLE_Status::Zoning) {
		Refuse(s, eqheadless::REFUSED_ALREADY_ONLINE, fmt::format("[{}] is already online", s->character));
		return;
	}
	for (auto &[id, other] : m_sessions) {
		if (other.get() != s && other->phase != Phase::Handshake && other->character_id == s->character_id) {
			Refuse(s, eqheadless::REFUSED_ALREADY_ONLINE, fmt::format("[{}] already has a headless session", s->character));
			return;
		}
	}

	if (instance_id > 0) {
		Refuse(s, eqheadless::REFUSED_NOT_IMPLEMENTED, "characters in instances are not supported yet");
		return;
	}

	if (!CheckZoneAllowed(s)) {
		return;
	}

	s->phase       = Phase::WaitingForZone;
	s->deadline_ms = NowMs() + ZONE_BOOT_TIMEOUT_MS;

	if (ZSList::Instance()->FindByZoneID(s->zone_id)) {
		TrySpawn(s);
		return;
	}

	LogHeadlessClient("Booting [{}] for headless character [{}]", ZoneName(s->zone_id, true), s->character);
	if (ZSList::Instance()->TriggerBootup(s->zone_id, 0) == 0) {
		Refuse(s, eqheadless::REFUSED_ZONE_UNAVAILABLE, "no zone server available to boot the character's zone");
	}
	// otherwise OnTick() spawns once the zone is up
}

bool HeadlessGateway::CheckZoneAllowed(Session *s)
{
	const std::string short_name = ZoneName(s->zone_id, true);

	if (ZoneInList(RuleS(HeadlessClients, BlockedZones), short_name)) {
		Refuse(s, eqheadless::REFUSED_ZONE_BLOCKED, fmt::format("zone [{}] is blocked for headless characters", short_name));
		return false;
	}

	std::string allowed = RuleS(HeadlessClients, AllowedZones);
	if (!Strings::Trim(allowed).empty() && !ZoneInList(allowed, short_name)) {
		Refuse(s, eqheadless::REFUSED_ZONE_BLOCKED, fmt::format("zone [{}] is not in HeadlessClients:AllowedZones", short_name));
		return false;
	}

	return true;
}

void HeadlessGateway::TrySpawn(Session *s)
{
	auto *zs = ZSList::Instance()->FindByZoneID(s->zone_id);
	if (!zs) {
		return;
	}

	// the world-side "online" record, created the way login does for real clients;
	// its id is the character's world id (wid), which the zone reports back in UpdateWho
	auto *cle = ClientList::Instance()->CLEAdd(
		s->lsaccount_id,
		s->ls_id.c_str(),
		s->account_name.c_str(),
		"",
		s->admin,
		LOOPBACK_IP,
		1
	);
	cle->SetOnline(CLE_Status::Zoning);
	s->wid            = cle->GetID();
	s->zone_server_id = zs->GetID();

	// 1. the normal pre-auth every arriving character gets
	auto inc = std::make_unique<ServerPacket>(ServerOP_ZoneIncClient, sizeof(ServerZoneIncomingClient_Struct));
	auto *szic = reinterpret_cast<ServerZoneIncomingClient_Struct *>(inc->pBuffer);
	szic->zoneid     = s->zone_id;
	szic->instanceid = 0;
	szic->ip         = LOOPBACK_IP;
	szic->wid        = s->wid;
	szic->accid      = s->account_id;
	szic->admin      = s->admin;
	szic->charid     = s->character_id;
	szic->lsid       = s->lsaccount_id;
	szic->tellsoff   = false;
	strn0cpy(szic->charname, s->character.c_str(), sizeof(szic->charname));
	strn0cpy(szic->lskey, "", sizeof(szic->lskey));
	zs->SendPacket(inc.get());

	// 2. spawn it with no network client
	auto spawn = std::make_unique<ServerPacket>(ServerOP_HeadlessSpawn, sizeof(ServerHeadlessSpawn_Struct));
	auto *hs = reinterpret_cast<ServerHeadlessSpawn_Struct *>(spawn->pBuffer);
	strn0cpy(hs->session_id, s->id.c_str(), sizeof(hs->session_id));
	strn0cpy(hs->character, s->character.c_str(), sizeof(hs->character));
	hs->character_id = s->character_id;
	zs->SendPacket(spawn.get());

	s->phase       = Phase::Spawning;
	s->deadline_ms = NowMs() + SPAWN_TIMEOUT_MS;

	LogHeadlessClient(
		"Asked zone [{}] (server {}) to spawn [{}] wid [{}] for session [{}]",
		ZoneName(s->zone_id, true),
		s->zone_server_id,
		s->character,
		s->wid,
		s->id
	);
}

void HeadlessGateway::SendDespawn(Session *s)
{
	if (s->phase != Phase::Spawning && s->phase != Phase::InGame) {
		return;
	}

	auto *zs = ZSList::Instance()->FindByID(s->zone_server_id);
	if (!zs) {
		return;
	}

	auto pack = std::make_unique<ServerPacket>(ServerOP_HeadlessDespawn, sizeof(ServerHeadlessSession_Struct));
	auto *d = reinterpret_cast<ServerHeadlessSession_Struct *>(pack->pBuffer);
	strn0cpy(d->session_id, s->id.c_str(), sizeof(d->session_id));
	zs->SendPacket(pack.get());

	// only once per session
	s->phase = Phase::Handshake;
}

void HeadlessGateway::HandleSpawned(ServerPacket *pack)
{
	if (pack->size != sizeof(ServerHeadlessSpawned_Struct)) {
		LogError("Wrong size on ServerOP_HeadlessSpawned: got [{}] expected [{}]", pack->size, sizeof(ServerHeadlessSpawned_Struct));
		return;
	}

	auto *r = reinterpret_cast<ServerHeadlessSpawned_Struct *>(pack->pBuffer);
	Session *s = FindByPacketSession(r->session_id, sizeof(r->session_id));
	if (!s || s->closing || s->phase != Phase::Spawning) {
		return;
	}

	if (r->spawn_id == 0) {
		Refuse(s, eqheadless::REFUSED_SPAWN_FAILED, std::string(r->error, strnlen(r->error, sizeof(r->error))));
		return;
	}

	s->phase    = Phase::InGame;
	s->spawn_id = r->spawn_id;

	eqheadless::ServerFrame frame;
	auto *welcome = frame.mutable_welcome();
	welcome->set_session_id(s->id);
	welcome->set_spawn_id(s->spawn_id);
	welcome->set_protocol_version(eqheadless::PROTOCOL_VERSION_CURRENT);
	Send(s, frame);

	LogInfo("Headless session [{}] is playing [{}] as spawn [{}] in [{}]", s->id, s->character, s->spawn_id, ZoneName(s->zone_id, true));
}

void HeadlessGateway::HandleGone(ServerPacket *pack)
{
	if (pack->size != sizeof(ServerHeadlessSession_Struct)) {
		LogError("Wrong size on ServerOP_HeadlessGone: got [{}] expected [{}]", pack->size, sizeof(ServerHeadlessSession_Struct));
		return;
	}

	auto *g = reinterpret_cast<ServerHeadlessSession_Struct *>(pack->pBuffer);
	Session *s = FindByPacketSession(g->session_id, sizeof(g->session_id));
	if (!s || s->closing) {
		return;
	}

	LogInfo("Headless character [{}] left the zone; closing session [{}]", s->character, s->id);
	s->phase = Phase::Handshake; // nothing to despawn any more
	CloseSoon(s);
}

HeadlessGateway::Session *HeadlessGateway::FindByPacketSession(const char *session_id, size_t length)
{
	auto it = m_sessions.find(std::string(session_id, strnlen(session_id, length)));
	return it == m_sessions.end() ? nullptr : it->second.get();
}

int HeadlessGateway::CountActive(uint32 account_id) const
{
	int n = 0;
	for (const auto &[id, s] : m_sessions) {
		if (s->closing || s->phase == Phase::Handshake) {
			continue;
		}
		if (account_id == 0 || s->account_id == account_id) {
			n++;
		}
	}
	return n;
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
	if (s->close_timer) {
		return;
	}

	s->closing     = true;
	s->close_timer = std::make_unique<EQ::Timer>(
		CLOSE_DELAY_MS, false, [s](EQ::Timer *) {
			// Disconnect() triggers OnDisconnect asynchronously, which erases the session
			s->connection->Disconnect();
		}
	);
}
