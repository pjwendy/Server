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
#include "common/eq_packet_structs.h"
#include "common/guilds.h"
#include "common/skill_caps.h"
#include "common/zone_store.h"
#include "world/client.h"
#include "world/sof_char_create_data.h"
#include "world/wguild_mgr.h"
#include "world/world_server_cli.h"
#include "world/worlddb.h"
#include "common/database_schema.h" // (needs <string>, <map> and <vector> included first)

#include "fmt/format.h"

extern std::vector<RaceClassAllocation> character_create_allocations;
extern std::vector<RaceClassCombos>     character_create_race_class_combos;

namespace {
	// what world loads at boot and character creation reads
	bool LoadCreationData()
	{
		ZoneStore::Instance()->LoadZones(content_db);
		SkillCaps::Instance()->SetContentDatabase(&content_db)->LoadSkillCaps();
		std::string hotfix_name;
		database.GetVariable("hotfix_name", hotfix_name);
		if (!content_db.LoadItems(hotfix_name)) {
			std::cout << "Couldn't load items (is shared memory built? ./bin/shared_memory)" << std::endl;
			return false;
		}
		content_db.LoadCharacterCreateAllocations();
		content_db.LoadCharacterCreateCombos();
		return true;
	}

	uint32 AccountID(const std::string &account_name)
	{
		auto results = database.QueryDatabase(
			fmt::format("SELECT `id` FROM `account` WHERE `name` = '{}'", Strings::Escape(account_name))
		);
		if (!results.Success() || results.RowCount() != 1) {
			return 0;
		}
		return Strings::ToUnsignedInt(results.begin()[0]);
	}

	uint32 CharacterID(const std::string &name)
	{
		return database.GetCharacterID(name);
	}

	// the checks the client's name approval goes through (world/client.cpp)
	std::string NameProblem(const std::string &name)
	{
		if (name.size() < 4 || name.size() > 15) {
			return "must be 4 to 15 letters";
		}
		if (!isupper(name[0])) {
			return "must start with a capital letter";
		}
		for (size_t i = 0; i < name.size(); ++i) {
			if (!isalpha(name[i]) || (i > 0 && isupper(name[i]))) {
				return "letters only, and only the first one a capital";
			}
		}
		if (!database.CheckNameFilter(name)) {
			return "is blocked by the name filter";
		}
		return "";
	}
}

void WorldserverCLI::HeadlessCharacterCreate(int argc, char **argv, argh::parser &cmd, std::string &description)
{
	description = "Creates a character as the client's character creation does (start zone, skills, languages, starting items)";

	std::vector<std::string> arguments = {"{account_name}", "{character_name}", "{race_id}", "{class_id}", "{start_zone}"};
	std::vector<std::string> options   = {"--gender=<0 male|1 female>", "--deity=<id>"};

	if (cmd[{"-h", "--help"}]) {
		return;
	}

	EQEmuCommand::ValidateCmdInput(arguments, options, cmd, argc, argv);

	const std::string account_name = cmd(2).str();
	const std::string name         = cmd(3).str();
	const uint32      race         = Strings::ToUnsignedInt(cmd(4).str());
	const uint32      class_id     = Strings::ToUnsignedInt(cmd(5).str());
	const std::string zone_name    = cmd(6).str();
	const uint32      gender       = Strings::ToUnsignedInt(cmd("--gender").str(), 0);
	const uint32      deity_wanted = Strings::ToUnsignedInt(cmd("--deity").str(), 0);

	const uint32 account_id = AccountID(account_name);
	if (!account_id) {
		std::cout << fmt::format("Account [{}] not found (or not unique)", account_name) << std::endl;
		return;
	}
	if (const auto problem = NameProblem(name); !problem.empty()) {
		std::cout << fmt::format("Name [{}] {}", name, problem) << std::endl;
		return;
	}
	if (!LoadCreationData()) {
		return;
	}
	const uint32 zone_id = ZoneID(zone_name);
	if (!zone_id) {
		std::cout << fmt::format("No zone called [{}]", zone_name) << std::endl;
		return;
	}

	// a combination the character creation screen offers: race, class, deity, start zone
	const RaceClassCombos *combo = nullptr;
	for (const auto &c : character_create_race_class_combos) {
		if (c.Race == race && c.Class == class_id && c.Zone == zone_id && (!deity_wanted || c.Deity == deity_wanted)) {
			combo = &c;
			break;
		}
	}
	if (!combo) {
		std::cout << fmt::format("Race [{}] class [{}] can't start in [{}]{}", race, class_id, zone_name,
			deity_wanted ? fmt::format(" with deity [{}]", deity_wanted) : "") << std::endl;
		return;
	}
	const RaceClassAllocation *alloc = nullptr;
	for (const auto &a : character_create_allocations) {
		if (a.Index == combo->AllocationIndex) {
			alloc = &a;
			break;
		}
	}
	if (!alloc) {
		std::cout << "No starting stats for that combination" << std::endl;
		return;
	}

	CharCreate_Struct cc{};
	cc.race       = race;
	cc.class_     = class_id;
	cc.gender     = gender;
	cc.deity      = combo->Deity;
	cc.start_zone = zone_id;
	cc.tutorial   = 0;
	// the stats the creation screen starts with: base plus its default spread of the bonus points
	// (BaseStats and DefaultPointAllocation are STR, DEX, AGI, STA, INT, WIS, CHA)
	cc.STR = alloc->BaseStats[0] + alloc->DefaultPointAllocation[0];
	cc.DEX = alloc->BaseStats[1] + alloc->DefaultPointAllocation[1];
	cc.AGI = alloc->BaseStats[2] + alloc->DefaultPointAllocation[2];
	cc.STA = alloc->BaseStats[3] + alloc->DefaultPointAllocation[3];
	cc.INT = alloc->BaseStats[4] + alloc->DefaultPointAllocation[4];
	cc.WIS = alloc->BaseStats[5] + alloc->DefaultPointAllocation[5];
	cc.CHA = alloc->BaseStats[6] + alloc->DefaultPointAllocation[6];

	if (!CheckCharCreateInfoSoF(&cc)) {
		std::cout << "The server's character creation checks refused it (see the world log)" << std::endl;
		return;
	}
	if (!database.ReserveName(account_id, name)) {
		std::cout << fmt::format("Name [{}] is taken", name) << std::endl;
		return;
	}
	if (!Client::CreateCharacter(account_id, 0, name.c_str(), &cc, EQ::versions::ClientVersionBitmask::bitRoF2)) {
		database.DeleteCharacter(name);
		std::cout << fmt::format("Creating [{}] failed (the name is free again)", name) << std::endl;
		return;
	}

	std::cout << fmt::format(
		"Created [{}] (character {}) on account [{}]: race [{}] class [{}] deity [{}] in [{}]",
		name, CharacterID(name), account_name, race, class_id, combo->Deity, zone_name
	) << std::endl;
}

void WorldserverCLI::HeadlessGuildCreate(int argc, char **argv, argh::parser &cmd, std::string &description)
{
	description = "Creates a guild led by a character, with the server's default ranks, and adds members (restart world and zones to see it)";

	std::vector<std::string> arguments = {"{guild_name}", "{leader}"};
	std::vector<std::string> options   = {"--members=<Name,Name,...>"};

	if (cmd[{"-h", "--help"}]) {
		return;
	}

	EQEmuCommand::ValidateCmdInput(arguments, options, cmd, argc, argv);

	const std::string guild_name = cmd(2).str();
	const std::string leader     = cmd(3).str();
	const uint32      leader_id  = CharacterID(leader);
	if (!leader_id) {
		std::cout << fmt::format("No character called [{}]", leader) << std::endl;
		return;
	}

	guild_mgr.SetDatabase(&database)->SetContentDatabase(&content_db);
	guild_mgr.LoadGuilds();

	uint32 guild_id = guild_mgr.GetGuildIDByName(guild_name.c_str());
	if (guild_id == GUILD_NONE) {
		guild_id = guild_mgr.CreateGuild(guild_name, leader_id);
		if (guild_id == GUILD_NONE) {
			std::cout << fmt::format("Couldn't create guild [{}]", guild_name) << std::endl;
			return;
		}
		std::cout << fmt::format("Created guild [{}] ({})", guild_name, guild_id) << std::endl;
	}
	else {
		std::cout << fmt::format("Guild [{}] ({}) already exists", guild_name, guild_id) << std::endl;
	}
	// (SetGuildLeader fails when nothing changes, so only when it's someone else)
	if ((!guild_mgr.IsGuildLeader(guild_id, leader_id) && !guild_mgr.SetGuildLeader(guild_id, leader_id)) ||
		!guild_mgr.SetGuild(leader_id, guild_id, GUILD_LEADER)) {
		std::cout << fmt::format("Couldn't make [{}] the leader", leader) << std::endl;
		return;
	}
	std::cout << fmt::format("  [{}] leads it", leader) << std::endl;

	for (const auto &member : Strings::Split(cmd("--members").str(), ',')) {
		auto name = member;
		Strings::Trim(name);
		if (name.empty() || Strings::EqualFold(name, leader)) {
			continue;
		}
		const uint32 id = CharacterID(name);
		if (!id) {
			std::cout << fmt::format("  no character called [{}]", name) << std::endl;
			continue;
		}
		std::cout << fmt::format("  [{}] {}", name, guild_mgr.SetGuild(id, guild_id, GUILD_MEMBER) ? "joined" : "couldn't join") << std::endl;
	}
}

namespace {
	// every row of a bot: its pets, heal rotations and settings, then the bot
	void DeleteBot(uint32 bot_id)
	{
		const std::vector<std::string> by_pet = {"bot_pet_buffs", "bot_pet_inventories"};
		for (const auto &table : by_pet) {
			database.QueryDatabase(fmt::format(
				"DELETE FROM `{}` WHERE `pets_index` IN (SELECT `pets_index` FROM `bot_pets` WHERE `bot_id` = {})", table, bot_id));
		}
		database.QueryDatabase(fmt::format(
			"DELETE FROM `bot_heal_rotation_targets` WHERE `heal_rotation_index` IN "
			"(SELECT `heal_rotation_index` FROM `bot_heal_rotations` WHERE `bot_id` = {})", bot_id));
		const std::vector<std::string> by_bot = {
			"bot_blocked_buffs", "bot_buffs", "bot_heal_rotation_members", "bot_heal_rotations", "bot_inspect_messages",
			"bot_inventories", "bot_pets", "bot_settings", "bot_spell_settings", "bot_stances", "bot_timers", "bot_data"
		};
		for (const auto &table : by_bot) {
			database.QueryDatabase(fmt::format("DELETE FROM `{}` WHERE `bot_id` = {}", table, bot_id));
		}
	}
}

void WorldserverCLI::HeadlessCharacterDelete(int argc, char **argv, argh::parser &cmd, std::string &description)
{
	description = "Deletes headless characters for good (their bots, items, corpses and the rest), ignoring Character:SoftDeletes; "
		"only characters on accounts with a headless token";

	std::vector<std::string> arguments = {"{Name,Name,...}"};
	std::vector<std::string> options   = {"--guild=<name> (delete this guild too)"};

	if (cmd[{"-h", "--help"}]) {
		return;
	}

	EQEmuCommand::ValidateCmdInput(arguments, options, cmd, argc, argv);

	if (const std::string guild_name = cmd("--guild").str(); !guild_name.empty()) {
		guild_mgr.SetDatabase(&database)->SetContentDatabase(&content_db);
		guild_mgr.LoadGuilds();
		const uint32 guild_id = guild_mgr.GetGuildIDByName(guild_name.c_str());
		if (guild_id == GUILD_NONE) {
			std::cout << fmt::format("No guild called [{}]", guild_name) << std::endl;
		}
		else {
			std::cout << fmt::format("Guild [{}] ({}) {}", guild_name, guild_id,
				guild_mgr.DeleteGuild(guild_id) ? "deleted" : "couldn't be deleted") << std::endl;
		}
	}

	for (auto name : Strings::Split(cmd(2).str(), ',')) {
		Strings::Trim(name);
		if (name.empty()) {
			continue;
		}
		const uint32 id = CharacterID(name);
		if (!id) {
			std::cout << fmt::format("[{}] no such character", name) << std::endl;
			continue;
		}
		auto results = database.QueryDatabase(fmt::format(
			"SELECT COUNT(*) FROM `headless_client_tokens` t JOIN `character_data` c ON c.`account_id` = t.`account_id` "
			"WHERE c.`id` = {}", id));
		if (!results.Success() || !Strings::ToUnsignedInt(results.begin()[0])) {
			std::cout << fmt::format("[{}] isn't on a headless account: left alone", name) << std::endl;
			continue;
		}

		int bots = 0;
		results = database.QueryDatabase(fmt::format("SELECT `bot_id` FROM `bot_data` WHERE `owner_id` = {}", id));
		for (auto row : results) {
			DeleteBot(Strings::ToUnsignedInt(row[0]));
			++bots;
		}
		database.QueryDatabase(fmt::format("DELETE FROM `bot_owner_options` WHERE `owner_id` = {}", id));

		// Database::DeleteCharacter's hard delete (it only renames when Character:SoftDeletes is on)
		database.QueryDatabase(fmt::format(
			"DELETE FROM `character_corpse_items` WHERE `corpse_id` IN (SELECT `id` FROM `character_corpses` WHERE `charid` = {})", id));
		for (const auto &t : DatabaseSchema::GetCharacterTables()) {
			database.QueryDatabase(fmt::format("DELETE FROM `{}` WHERE `{}` = {}", t.first, t.second, id));
		}
		std::cout << fmt::format("[{}] ({}) deleted, with {} bot{}", name, id, bots, bots == 1 ? "" : "s") << std::endl;
	}
}
