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
#include "zone/bot_command.h"

void bot_command_corpse_drag(Client* c, const Seperator* sep)
{
	if (helper_command_alias_fail(c, "bot_command_corpse_drag", sep->arg[0], "corpsedrag")) {
		c->Message(Chat::White, "note: Sends a bot to drag your targeted corpse back to you.");

		return;
	}

	if (helper_is_help_or_usage(sep->arg[1])) {
		BotCommandHelpParams p;

		p.description = { "Sends a bot for your targeted corpse (yours, or one you're consented to): it goes to it, drags it back and leaves it at your feet." };
		p.notes = {
			"- Out on its own the bot is seen as a player is: what it passes may aggro on it.",
			"- A rogue goes hidden and sneaking; a monk, necromancer or shadowknight feigns death when something comes for it, and gets up once nothing near hates it.",
			"- It gives up after four minutes."
		};
		p.example_format = { fmt::format("{} [stop] [actionable]", sep->arg[0]) };
		p.examples_one = { "To send a bot named Sneaky for your targeted corpse:", fmt::format("{} byname Sneaky", sep->arg[0]) };
		p.examples_two = { "To call it off:", fmt::format("{} stop byname Sneaky", sep->arg[0]) };
		p.actionables = { "byname, byclass, spawned" };

		std::string popup_text = c->SendBotCommandHelpWindow(p);
		popup_text = DialogueWindow::Table(popup_text);

		c->SendPopupToClient(sep->arg[0], popup_text.c_str());

		return;
	}

	const int ab_mask = ActionableBots::ABM_Type2;
	int ab_arg = 1;
	bool stop = false;

	if (!std::string(sep->arg[1]).compare("stop")) {
		stop = true;
		++ab_arg;
	}

	Corpse* corpse = nullptr;
	if (!stop) {
		Mob* t = c->GetTarget();
		if (!t || !t->IsPlayerCorpse()) {
			c->Message(Chat::Yellow, "You must <target> a player's corpse to use this command.");
			return;
		}

		corpse = t->CastToCorpse();
		if (!corpse->MayMove(c)) {
			c->Message(Chat::Yellow, fmt::format("You may not move {}.", corpse->GetCleanName()).c_str());
			return;
		}
	}

	std::string class_race_arg = sep->arg[ab_arg];
	bool class_race_check = !class_race_arg.compare("byclass") || !class_race_arg.compare("byrace");

	std::vector<Bot*> sbl;
	if (ActionableBots::PopulateSBL(c, sep->arg[ab_arg], sbl, ab_mask, !class_race_check ? sep->arg[ab_arg + 1] : nullptr, class_race_check ? atoi(sep->arg[ab_arg + 1]) : 0) == ActionableBots::ABT_None) {
		return;
	}

	sbl.erase(std::remove(sbl.begin(), sbl.end(), nullptr), sbl.end());

	for (auto bot_iter : sbl) {
		if (stop) {
			if (bot_iter->GetDragCorpseID()) {
				bot_iter->StopCorpseDrag("I'm leaving it.");
			}

			continue;
		}

		if (!bot_iter->IsInGroupOrRaid(c)) {
			continue;
		}

		bot_iter->StartCorpseDrag(corpse->GetID());
		bot_iter->RaidGroupSay(fmt::format("Going for {}.", corpse->GetCleanName()).c_str());

		return;  // (one fetcher)
	}

	if (!stop) {
		c->Message(Chat::Yellow, "No bot of yours in your group could go.");
	}
}
