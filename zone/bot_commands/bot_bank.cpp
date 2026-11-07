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

// A bot's bank: the bank's 24 slots and the bags in them, kept for its owner as its pack is (^pack). Things go in from its pack and come
// back to its pack, and only at a banker, as a player's bank: the bot has to be near one. Every line this command says starts with "bank",
// so a program reading them can tell them apart: "bank <slot> <item id> <count> <bag|item> <name>" for what's in it.
static constexpr float BOT_BANK_REACH = 60.0f;  // a banker this near the bot

static bool bot_at_banker(Bot* bot)
{
	uint32 d2 = 0;
	const NPC* banker = entity_list.GetClosestBanker(bot, d2);
	return banker && d2 <= static_cast<uint32>(BOT_BANK_REACH * BOT_BANK_REACH);
}

void bot_command_bank(Client* c, const Seperator* sep)
{
	if (helper_command_alias_fail(c, "bot_command_bank", sep->arg[0], "bank")) {
		c->Message(Chat::White, "note: Your bot banks for you: from its pack into its bank and back, at a banker.");

		return;
	}

	const std::string what = sep->arg[1];
	if (helper_is_help_or_usage(sep->arg[1]) || (what != "put" && what != "list" && what != "take" && what != "room")) {
		BotCommandHelpParams p;

		p.description = { "Your bot has a bank: 24 slots and the bags in them. Things go in from its pack (^pack) and come back to it, at a banker." };
		p.notes = {
			"- put <pack slot>: that pack slot's item into its bank (a stack onto one of the same, a bag into a free slot, anything else into a bag with room). A bag must be empty. Near a banker only.",
			"- take <bank slot>: that bank slot's item back into its pack (a bag only when it's empty). Near a banker only.",
			"- list: every slot in its bank, as \"bank <slot> <item id> <count> <bag|item> <name>\", then how much room is left.",
			"- room: how many more things its bank holds."
		};
		p.example_format = { fmt::format("{} [put <pack slot> | take <bank slot> | list | room] [actionable: target | byname <name>]", sep->arg[0]) };
		p.examples_one = { "To bank what's in Muley's pack slot 4011:", fmt::format("{} put 4011 byname Muley", sep->arg[0]) };
		p.examples_two = { "To take back what's in his bank slot 2003:", fmt::format("{} take 2003 byname Muley", sep->arg[0]) };
		p.actionables = { "target, byname" };

		std::string popup_text = c->SendBotCommandHelpWindow(p);
		popup_text = DialogueWindow::Table(popup_text);

		c->SendPopupToClient(sep->arg[0], popup_text.c_str());

		return;
	}

	int ab_arg = 2;
	int16 slot = INVALID_INDEX;
	if (what == "put" || what == "take") {
		if (!sep->IsNumber(2)) {
			c->Message(Chat::White, fmt::format("bank: {} which slot? ({} says)", what, what == "put" ? "^pack list" : "^bank list").c_str());
			return;
		}
		slot = static_cast<int16>(Strings::ToInt(sep->arg[2]));
		ab_arg = 3;
	}

	if (c->GetTradeskillObject() || (c->trade->state == Trading)) {
		c->MessageString(Chat::Tell, MERCHANT_BUSY);
		return;
	}

	std::vector<Bot*> sbl;
	if (ActionableBots::PopulateSBL(c, sep->arg[ab_arg], sbl, ActionableBots::ABM_Target | ActionableBots::ABM_ByName,
									sep->arg[ab_arg + 1]) == ActionableBots::ABT_None) {
		return;
	}

	auto* bot = sbl.front();
	if (!bot) {
		c->Message(Chat::White, "bank: no bot");
		return;
	}

	if (what == "list" || what == "room") {
		if (what == "list") {
			for (int16 s : bot->BankSlots()) {
				const auto* inst = bot->GetBotItem(s);
				if (inst && inst->GetItem()) {
					c->Message(Chat::White, fmt::format("bank {} {} {} {} {}", s, inst->GetID(), inst->IsStackable() ? inst->GetCharges() : 1,
														inst->IsClassBag() ? "bag" : "item", inst->GetItem()->Name).c_str());
				}
			}
		}
		c->Message(Chat::White, fmt::format("bank {}: room for {} more", bot->GetCleanName(), bot->BankRoom()).c_str());
		return;
	}

	if (!bot_at_banker(bot)) {
		c->Message(Chat::White, fmt::format("bank {}: no banker near", bot->GetCleanName()).c_str());
		return;
	}

	const bool put = what == "put";
	if (put ? !Bot::IsPackSlot(slot) : !Bot::IsBankSlot(slot)) {
		c->Message(Chat::White, fmt::format("bank: {} isn't a {} slot", slot, put ? "pack" : "bank").c_str());
		return;
	}
	const auto* inst = bot->GetBotItem(slot);
	if (!inst || !inst->GetItem()) {
		c->Message(Chat::White, fmt::format("bank {}: nothing in slot {}", bot->GetCleanName(), slot).c_str());
		return;
	}
	if (inst->IsClassBag()) {
		for (uint8 i = EQ::invbag::SLOT_BEGIN; i < inst->GetItem()->BagSlots; ++i) {
			if (bot->GetBotItem(EQ::InventoryProfile::CalcSlotId(slot, i))) {
				c->Message(Chat::White, fmt::format("bank {}: {} isn't empty", bot->GetCleanName(), inst->GetItem()->Name).c_str());
				return;
			}
		}
	}
	const EQ::ItemInstance copy(*inst);  // (the slot's own goes when it's moved)
	const std::string name = copy.GetItem()->Name;
	const int16 count = copy.IsStackable() ? copy.GetCharges() : 1;
	const int16 to = put ? bot->BankSlotFor(copy) : bot->PackSlotFor(copy);
	if (to == INVALID_INDEX) {
		c->Message(Chat::White, fmt::format("bank {}: no room in its {} for {}", bot->GetCleanName(), put ? "bank" : "pack", name).c_str());
		return;
	}
	if (!(put ? bot->BankPut(to, copy) : bot->PackPut(to, copy))) {
		c->Message(Chat::White, fmt::format("bank {}: couldn't put {} in slot {}", bot->GetCleanName(), name, to).c_str());
		return;
	}
	bot->RemoveBotItemBySlot(slot);
	c->Message(Chat::White, fmt::format("bank {}: {} {} {} from slot {} to slot {}", bot->GetCleanName(), put ? "banked" : "took out", count, name,
										slot, to).c_str());
}
