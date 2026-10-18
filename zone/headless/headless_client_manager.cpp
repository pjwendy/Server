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
#include "zone/headless/headless_client_manager.h"

#include "common/eqemu_logsys.h"
#include "common/servertalk.h"
#include "common/strings.h"
#include "zone/client.h"
#include "zone/entity.h"
#include "zone/headless/null_stream.h"
#include "zone/worldserver.h"

extern EntityList  entity_list;
extern WorldServer worldserver;

void HeadlessClientManager::HandleSpawn(ServerPacket *pack)
{
	if (pack->size != sizeof(ServerHeadlessSpawn_Struct)) {
		LogError("Wrong size on ServerOP_HeadlessSpawn: got [{}] expected [{}]", pack->size, sizeof(ServerHeadlessSpawn_Struct));
		return;
	}

	auto *s = reinterpret_cast<ServerHeadlessSpawn_Struct *>(pack->pBuffer);
	const std::string session_id(s->session_id, strnlen(s->session_id, sizeof(s->session_id)));
	const std::string character(s->character, strnlen(s->character, sizeof(s->character)));

	if (m_streams.count(session_id) || entity_list.GetClientByName(character.c_str())) {
		SendSpawned(session_id, 0, fmt::format("[{}] is already in this zone", character));
		return;
	}

	// world sent ServerOP_ZoneIncClient first, so Handle_Connect_OP_ZoneEntry's auth check passes
	auto *stream = new NullStream(session_id, character);
	auto *client = new Client(stream);
	stream->SetClient(client);
	m_streams[session_id] = stream;
	entity_list.AddClient(client);

	LogHeadlessClient("Spawning headless character [{}] for session [{}]", character, session_id);
}

void HeadlessClientManager::HandleDespawn(ServerPacket *pack)
{
	if (pack->size != sizeof(ServerHeadlessSession_Struct)) {
		LogError("Wrong size on ServerOP_HeadlessDespawn: got [{}] expected [{}]", pack->size, sizeof(ServerHeadlessSession_Struct));
		return;
	}

	auto *s = reinterpret_cast<ServerHeadlessSession_Struct *>(pack->pBuffer);
	const std::string session_id(s->session_id, strnlen(s->session_id, sizeof(s->session_id)));

	auto it = m_streams.find(session_id);
	if (it == m_streams.end()) {
		return;
	}

	// the stream now reports not-established, so Client::Process() runs the normal
	// linkdead/logout path, which saves the character and removes it from the zone
	LogHeadlessClient("Session [{}] ended; logging its character out", session_id);
	it->second->Close();
}

void HeadlessClientManager::ReportSpawned(const std::string &session_id, uint32 spawn_id)
{
	LogInfo("Headless character for session [{}] is in the zone as spawn [{}]", session_id, spawn_id);
	SendSpawned(session_id, spawn_id, "");
}

void HeadlessClientManager::OnStreamDestroyed(const std::string &session_id, bool was_spawned)
{
	m_streams.erase(session_id);

	if (!was_spawned) {
		SendSpawned(session_id, 0, "character failed to enter the zone");
		return;
	}

	auto pack = new ServerPacket(ServerOP_HeadlessGone, sizeof(ServerHeadlessSession_Struct));
	auto *s   = reinterpret_cast<ServerHeadlessSession_Struct *>(pack->pBuffer);
	strn0cpy(s->session_id, session_id.c_str(), sizeof(s->session_id));
	worldserver.SendPacket(pack);
	safe_delete(pack);
}

void HeadlessClientManager::SendSpawned(const std::string &session_id, uint32 spawn_id, const std::string &error)
{
	auto pack = new ServerPacket(ServerOP_HeadlessSpawned, sizeof(ServerHeadlessSpawned_Struct));
	auto *s   = reinterpret_cast<ServerHeadlessSpawned_Struct *>(pack->pBuffer);
	strn0cpy(s->session_id, session_id.c_str(), sizeof(s->session_id));
	s->spawn_id = spawn_id;
	strn0cpy(s->error, error.c_str(), sizeof(s->error));
	worldserver.SendPacket(pack);
	safe_delete(pack);
}
