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

// ^feign: a monk (by skill), a necromancer or a shadowknight (by spell) lies feigned to be forgotten by what has it, and stands again when nothing near is after it
void bot_command_feign(Client *c, const Seperator *sep)
{
	if (helper_command_alias_fail(c, "bot_command_feign", sep->arg[0], "feign")) {
		return;
	}
	if (helper_is_help_or_usage(sep->arg[1])) {
		c->Message(Chat::White, "usage: %s [actionable: byname | ownergroup | ownerraid | namesgroup | healrotation | mmr | byclass | byrace | default: spawned] ([actionable_name])", sep->arg[0]);
		return;
	}
	const int ab_mask = (ActionableBots::ABM_Target | ActionableBots::ABM_Type2);

	std::string class_race_arg = sep->arg[1];
	bool class_race_check = false;
	if (!class_race_arg.compare("byclass") || !class_race_arg.compare("byrace")) {
		class_race_check = true;
	}

	std::vector<Bot*> sbl;
	if (ActionableBots::PopulateSBL(c, sep->arg[1], sbl, ab_mask, !class_race_check ? sep->arg[2] : nullptr, class_race_check ? atoi(sep->arg[2]) : 0) == ActionableBots::ABT_None) {
		return;
	}

	sbl.erase(std::remove(sbl.begin(), sbl.end(), nullptr), sbl.end());
	int feigned = 0;
	for (auto bot_iter : sbl) {
		if (bot_iter->StartFeignHold()) {
			++feigned;
		}
	}
	c->Message(Chat::White, "%i of your bots %s feigning.", feigned, feigned == 1 ? "is" : "are");
}
