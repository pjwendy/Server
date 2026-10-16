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
#include "world/world_server_cli.h"
#include "world/worlddb.h"

#include "fmt/format.h"

void WorldserverCLI::HeadlessTokenCreate(int argc, char **argv, argh::parser &cmd, std::string &description)
{
	description = "Issues a headless client token for an account (the token is only shown once)";

	std::vector<std::string> arguments = {"{account_name}"};
	std::vector<std::string> options   = {"--label=<text>"};

	if (cmd[{"-h", "--help"}]) {
		return;
	}

	EQEmuCommand::ValidateCmdInput(arguments, options, cmd, argc, argv);

	if (!HeadlessTokens::EnsureSchema(database)) {
		return;
	}

	const std::string account_name = cmd(2).str();
	auto results = database.QueryDatabase(
		fmt::format("SELECT `id` FROM `account` WHERE `name` = '{}'", Strings::Escape(account_name))
	);
	if (!results.Success() || results.RowCount() == 0) {
		std::cout << fmt::format("Account [{}] not found", account_name) << std::endl;
		return;
	}
	if (results.RowCount() > 1) {
		std::cout << fmt::format("Account name [{}] matches more than one account", account_name) << std::endl;
		return;
	}

	const uint32 account_id = Strings::ToUnsignedInt(results.begin()[0]);
	uint32       token_id   = 0;
	const auto   token      = HeadlessTokens::Create(database, account_id, cmd("--label").str(), token_id);
	if (token.empty()) {
		std::cout << "Failed to create token" << std::endl;
		return;
	}

	std::cout << fmt::format("Created headless token [{}] for account [{}] ({})", token_id, account_name, account_id) << std::endl;
	std::cout << std::endl << "  " << token << std::endl << std::endl;
	std::cout << "Store it now; it cannot be shown again. Revoke with: world headless:token-revoke " << token_id << std::endl;
}

void WorldserverCLI::HeadlessTokenList(int argc, char **argv, argh::parser &cmd, std::string &description)
{
	description = "Lists headless client tokens";

	if (cmd[{"-h", "--help"}]) {
		return;
	}

	if (!HeadlessTokens::EnsureSchema(database)) {
		return;
	}

	auto tokens = HeadlessTokens::List(database);
	if (tokens.empty()) {
		std::cout << "No headless tokens" << std::endl;
		return;
	}

	std::cout << fmt::format("{:>5}  {:<20} {:<24} {:<20} {:<20} {}", "id", "account", "label", "created", "last used", "status") << std::endl;
	for (const auto &t : tokens) {
		std::cout << fmt::format(
			"{:>5}  {:<20} {:<24} {:<20} {:<20} {}",
			t.id,
			fmt::format("{} ({})", t.account_name, t.account_id),
			t.label,
			t.created_at,
			t.last_used_at.empty() ? "never" : t.last_used_at,
			t.revoked ? "revoked" : "active"
		) << std::endl;
	}
}

void WorldserverCLI::HeadlessTokenRevoke(int argc, char **argv, argh::parser &cmd, std::string &description)
{
	description = "Revokes a headless client token";

	std::vector<std::string> arguments = {"{token_id}"};
	std::vector<std::string> options   = {};

	if (cmd[{"-h", "--help"}]) {
		return;
	}

	EQEmuCommand::ValidateCmdInput(arguments, options, cmd, argc, argv);

	if (!HeadlessTokens::EnsureSchema(database)) {
		return;
	}

	const uint32 token_id = Strings::ToUnsignedInt(cmd(2).str());
	std::cout << (HeadlessTokens::Revoke(database, token_id)
		? fmt::format("Revoked headless token [{}]", token_id)
		: fmt::format("No headless token [{}]", token_id)) << std::endl;
}
