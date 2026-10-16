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
#include "world/headless/headless_tokens.h"

#include "common/database.h"
#include "common/eqemu_logsys.h"
#include "common/strings.h"

#include "fmt/format.h"
#include <sodium.h>

#include <cctype>
#include <cstring>

namespace {
	constexpr const char *TOKEN_PREFIX     = "hc_";
	constexpr size_t      TOKEN_RAW_BYTES  = 32;
	constexpr size_t      TOKEN_HEX_LENGTH = TOKEN_RAW_BYTES * 2;

	bool SodiumReady()
	{
		static const bool ready = sodium_init() >= 0;
		return ready;
	}

	std::string ToHex(const unsigned char *data, size_t length)
	{
		std::string hex(length * 2 + 1, '\0');
		sodium_bin2hex(hex.data(), hex.size(), data, length);
		hex.resize(length * 2);
		return hex;
	}

	std::string HashToken(const std::string &token)
	{
		unsigned char digest[crypto_hash_sha256_BYTES];
		crypto_hash_sha256(digest, reinterpret_cast<const unsigned char *>(token.data()), token.size());
		return ToHex(digest, sizeof(digest));
	}

	bool IsWellFormed(const std::string &token)
	{
		if (token.size() != strlen(TOKEN_PREFIX) + TOKEN_HEX_LENGTH || !token.starts_with(TOKEN_PREFIX)) {
			return false;
		}

		for (size_t i = strlen(TOKEN_PREFIX); i < token.size(); ++i) {
			if (!isxdigit(static_cast<unsigned char>(token[i]))) {
				return false;
			}
		}

		return true;
	}

	std::string Column(char *value)
	{
		return value ? value : "";
	}
}

bool HeadlessTokens::EnsureSchema(Database &db)
{
	auto results = db.QueryDatabase(
		R"(
CREATE TABLE IF NOT EXISTS `headless_client_tokens` (
  `id`           int(10) unsigned NOT NULL AUTO_INCREMENT,
  `account_id`   int(10) unsigned NOT NULL,
  `token_hash`   char(64)         NOT NULL,
  `label`        varchar(64)      NOT NULL DEFAULT '',
  `created_at`   datetime         NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `last_used_at` datetime         NULL DEFAULT NULL,
  `revoked`      tinyint(1) unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  UNIQUE KEY `token_hash` (`token_hash`),
  KEY `account_id` (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
)"
	);

	if (!results.Success()) {
		LogError("Failed to create headless_client_tokens table [{}]", results.ErrorMessage());
		return false;
	}

	return true;
}

std::string HeadlessTokens::Create(Database &db, uint32 account_id, const std::string &label, uint32 &token_id)
{
	token_id = 0;

	if (!SodiumReady()) {
		LogError("libsodium failed to initialise; cannot create headless token");
		return {};
	}

	unsigned char raw[TOKEN_RAW_BYTES];
	randombytes_buf(raw, sizeof(raw));
	const std::string token = TOKEN_PREFIX + ToHex(raw, sizeof(raw));
	sodium_memzero(raw, sizeof(raw));

	auto results = db.QueryDatabase(
		fmt::format(
			"INSERT INTO `headless_client_tokens` (`account_id`, `token_hash`, `label`) VALUES ({}, '{}', '{}')",
			account_id,
			HashToken(token),
			Strings::Escape(label.substr(0, 64))
		)
	);

	if (!results.Success()) {
		LogError("Failed to store headless token [{}]", results.ErrorMessage());
		return {};
	}

	token_id = results.LastInsertedID();
	return token;
}

std::vector<HeadlessTokens::Token> HeadlessTokens::List(Database &db)
{
	std::vector<Token> tokens;

	auto results = db.QueryDatabase(
		"SELECT t.`id`, t.`account_id`, IFNULL(a.`name`, ''), t.`label`, t.`created_at`, "
		"IFNULL(t.`last_used_at`, ''), t.`revoked` "
		"FROM `headless_client_tokens` t LEFT JOIN `account` a ON a.`id` = t.`account_id` "
		"ORDER BY t.`id`"
	);

	for (auto row : results) {
		tokens.push_back(
			Token{
				.id           = Strings::ToUnsignedInt(row[0]),
				.account_id   = Strings::ToUnsignedInt(row[1]),
				.account_name = Column(row[2]),
				.label        = Column(row[3]),
				.created_at   = Column(row[4]),
				.last_used_at = Column(row[5]),
				.revoked      = Strings::ToUnsignedInt(row[6]) != 0,
			}
		);
	}

	return tokens;
}

bool HeadlessTokens::Revoke(Database &db, uint32 token_id)
{
	auto results = db.QueryDatabase(
		fmt::format("UPDATE `headless_client_tokens` SET `revoked` = 1 WHERE `id` = {}", token_id)
	);

	return results.Success() && results.RowsAffected() > 0;
}

std::optional<HeadlessTokens::Verified> HeadlessTokens::Verify(Database &db, const std::string &token)
{
	// reject anything that isn't shaped like one of our tokens before touching the database
	if (!IsWellFormed(token) || !SodiumReady()) {
		return std::nullopt;
	}

	auto results = db.QueryDatabase(
		fmt::format(
			"SELECT `id`, `account_id` FROM `headless_client_tokens` WHERE `token_hash` = '{}' AND `revoked` = 0 LIMIT 1",
			HashToken(token)
		)
	);

	if (!results.Success() || results.RowCount() == 0) {
		return std::nullopt;
	}

	auto row = results.begin();
	Verified v{
		.token_id   = Strings::ToUnsignedInt(row[0]),
		.account_id = Strings::ToUnsignedInt(row[1]),
	};

	db.QueryDatabase(
		fmt::format("UPDATE `headless_client_tokens` SET `last_used_at` = NOW() WHERE `id` = {}", v.token_id)
	);

	return v;
}
