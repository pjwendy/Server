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
#include "zone/headless/null_stream.h"

#include "common/eq_packet_structs.h"
#include "common/eqemu_logsys.h"
#include "common/opcodemgr.h"
#include "common/rulesys.h"
#include "common/strings.h"
#include "zone/client.h"
#include "zone/headless/headless_client_manager.h"

NullStream::NullStream(const std::string &session_id, const std::string &character)
	: m_session_id(session_id), m_character(character)
{
}

NullStream::~NullStream()
{
	// the Client deletes its stream when it leaves the zone (or failed to enter it)
	HeadlessClientManager::Instance()->OnStreamDestroyed(m_session_id, m_step == Step::Done);
}

uint32 NullStream::GetRemoteIP() const
{
	return 0x0100007F; // 127.0.0.1 in network byte order
}

OpcodeManager *NullStream::GetOpcodeManager() const
{
	// only used for packet logging; resolves every opcode to 0
	static NullOpcodeManager null_opcodes;
	return &null_opcodes;
}

void NullStream::FastQueuePacket(EQApplicationPacket **p, bool ack_req)
{
	// FastQueuePacket transfers ownership
	if (p && *p) {
		delete *p;
		*p = nullptr;
	}
}

EQApplicationPacket *NullStream::PopPacket()
{
	if (m_state != ESTABLISHED) {
		return nullptr;
	}

	// Client::Process() drains PopPacket() in a loop; hand out one handshake step per tick so
	// each handler's effects (and the floor-Z fix) settle before the next one runs
	if (m_yield) {
		m_yield = false;
		return nullptr;
	}

	switch (m_step) {
		case Step::ZoneEntry: {
			auto app = new EQApplicationPacket(OP_ZoneEntry, sizeof(ClientZoneEntry_Struct));
			auto cze = reinterpret_cast<ClientZoneEntry_Struct *>(app->pBuffer);
			strn0cpy(cze->char_name, m_character.c_str(), sizeof(cze->char_name));
			m_step  = Step::ReqClientSpawn;
			m_yield = true;
			return app;
		}
		case Step::ReqClientSpawn:
			m_step  = Step::WorldObjectsSent;
			m_yield = true;
			return new EQApplicationPacket(OP_ReqClientSpawn, 0);
		case Step::WorldObjectsSent:
			PrepareForSpawnBroadcast();
			m_step  = Step::ClientReady;
			m_yield = true;
			return new EQApplicationPacket(OP_WorldObjectsSent, 0);
		case Step::ClientReady:
			m_step  = Step::ReportSpawn;
			m_yield = true;
			return new EQApplicationPacket(OP_ClientReady, 0);
		case Step::ReportSpawn:
			if (m_client && m_client->Connected()) {
				m_step = Step::Done;
				HeadlessClientManager::Instance()->ReportSpawned(m_session_id, m_client->GetID());
			}
			return nullptr;
		case Step::Done:
		default:
			return nullptr;
	}
}

void NullStream::PrepareForSpawnBroadcast()
{
	if (!m_client) {
		return;
	}

	// client parity: a real client settles onto the floor itself; we do it before others see us
	const auto &pos   = m_client->GetPosition();
	const float new_z = m_client->GetFixedZ(glm::vec3(pos.x, pos.y, pos.z));
	if (new_z != pos.z) {
		LogHeadlessClient(
			"[{}] floor fix z [{:.2f}] -> [{:.2f}] at [{:.2f}, {:.2f}]",
			m_client->GetCleanName(),
			pos.z,
			new_z,
			pos.x,
			pos.y
		);
		m_client->SetPosition(pos.x, pos.y, new_z);
	}

	// headless characters are always visibly tagged
	std::string tag = RuleS(HeadlessClients, DisplayTag);
	if (tag.empty()) {
		tag = "AI";
	}
	m_client->SetTitleSuffix(tag);
}
