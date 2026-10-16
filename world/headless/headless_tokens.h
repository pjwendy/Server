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

#include <optional>
#include <string>
#include <vector>

class Database;

// Operator-issued credentials for headless client connections.
// Tokens are shown once at creation; only their SHA-256 hash is stored.
namespace HeadlessTokens {
	struct Token {
		uint32      id;
		uint32      account_id;
		std::string account_name;
		std::string label;
		std::string created_at;
		std::string last_used_at;
		bool        revoked;
	};

	struct Verified {
		uint32 token_id;
		uint32 account_id;
	};

	// creates the headless_client_tokens table if it doesn't exist
	bool EnsureSchema(Database &db);

	// returns the plaintext token (format "hc_" + 64 hex chars), or empty on failure
	std::string Create(Database &db, uint32 account_id, const std::string &label, uint32 &token_id);

	std::vector<Token> List(Database &db);
	bool Revoke(Database &db, uint32 token_id);

	// looks up a non-revoked token and stamps last_used_at
	std::optional<Verified> Verify(Database &db, const std::string &token);
}
