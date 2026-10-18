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

#include "common/eq_stream_intf.h"

#include <string>

class Client;

// The "connection" of a headless character. There is no network: outgoing packets are
// dropped, and PopPacket() replays a real RoF2 client's zone-in handshake (ZoneEntry,
// ReqClientSpawn, WorldObjectsSent, ClientReady) one packet per Client::Process() tick,
// so every zone-in handler runs unchanged. Owned and deleted by its Client.
class NullStream : public EQStreamInterface {
public:
	NullStream(const std::string &session_id, const std::string &character);
	~NullStream() override;

	// set right after `new Client(stream)`; needed for the floor-Z fix before the spawn broadcast
	void SetClient(Client *c) { m_client = c; }
	const std::string &SessionID() const { return m_session_id; }
	bool SpawnReported() const { return m_step == Step::Done; }

	void QueuePacket(const EQApplicationPacket *p, bool ack_req = true) override {}
	void FastQueuePacket(EQApplicationPacket **p, bool ack_req = true) override;
	EQApplicationPacket *PopPacket() override;
	void Close() override { m_state = CLOSED; }
	void ReleaseFromUse() override {}
	void RemoveData() override {}
	std::string GetRemoteAddr() const override { return "127.0.0.1"; }
	uint32 GetRemoteIP() const override;
	uint16 GetRemotePort() const override { return 0; }
	bool CheckState(EQStreamState state) override { return m_state == state; }
	std::string Describe() const override { return "headless:" + m_session_id; }
	EQStreamState GetState() override { return m_state; }
	void SetOpcodeManager(OpcodeManager **opm) override {}
	OpcodeManager *GetOpcodeManager() const override;
	const EQ::versions::ClientVersion ClientVersion() const override { return EQ::versions::ClientVersion::RoF2; }
	Stats GetStats() const override { return {}; }
	void ResetStats() override {}
	EQStreamManagerInterface *GetManager() const override { return nullptr; }

private:
	enum class Step {
		ZoneEntry,
		ReqClientSpawn,
		WorldObjectsSent, // broadcasts our spawn to other players
		ClientReady,
		ReportSpawn,
		Done,
	};

	void PrepareForSpawnBroadcast();

	std::string   m_session_id;
	std::string   m_character;
	Client        *m_client = nullptr;
	EQStreamState m_state   = ESTABLISHED;
	Step          m_step    = Step::ZoneEntry;
	bool          m_yield   = false; // one handshake packet per Process() tick
};
