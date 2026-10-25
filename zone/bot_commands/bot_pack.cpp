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

// A bot's pack: its ten general slots and the bags in them, carried for its owner (nothing in it is
// worn, so it changes none of the bot's stats). Every line this command says starts with "pack", so
// a program reading them can tell them apart: "pack <slot> <item id> <count> <name>" for what's in it.
void bot_command_pack(Client* c, const Seperator* sep)
{
	if (helper_command_alias_fail(c, "bot_command_pack", sep->arg[0], "pack")) {
		c->Message(Chat::White, "note: Your bot carries things for you: give it what's on your cursor, list its pack, take things back.");

		return;
	}

	const std::string what = sep->arg[1];
	if (helper_is_help_or_usage(sep->arg[1]) || (what != "give" && what != "list" && what != "take" && what != "room")) {
		BotCommandHelpParams p;

		p.description = { "Your bot carries things for you in its pack: its ten general slots and the bags in them. Nothing in it is worn: it changes none of the bot's stats." };
		p.notes = {
			"- give: what's on your cursor goes into its pack (a stack onto one of the same, a bag into a free slot, anything else into a bag with room). A bag must be empty.",
			"- list: every slot in its pack, as \"pack <slot> <item id> <count> <name>\", then how much room is left.",
			"- take <slot>: that slot's item comes back to your cursor (a bag only when it's empty).",
			"- room: how many more things it can carry."
		};
		p.example_format = { fmt::format("{} [give | list | take <slot> | room] [actionable: target | byname <name>]", sep->arg[0]) };
		p.examples_one = { "To give Muley what's on your cursor:", fmt::format("{} give byname Muley", sep->arg[0]) };
		p.examples_two = { "To take back what's in slot 4011:", fmt::format("{} take 4011 byname Muley", sep->arg[0]) };
		p.actionables = { "target, byname" };

		std::string popup_text = c->SendBotCommandHelpWindow(p);
		popup_text = DialogueWindow::Table(popup_text);

		c->SendPopupToClient(sep->arg[0], popup_text.c_str());

		return;
	}

	int ab_arg = 2;
	int16 take_slot = INVALID_INDEX;
	if (what == "take") {
		if (!sep->IsNumber(2)) {
			c->Message(Chat::White, "pack: take which slot? (pack list says)");
			return;
		}
		take_slot = static_cast<int16>(Strings::ToInt(sep->arg[2]));
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
		c->Message(Chat::White, "pack: no bot");
		return;
	}

	if (what == "list" || what == "room") {
		if (what == "list") {
			for (int16 slot : bot->PackSlots()) {
				const auto* inst = bot->GetBotItem(slot);
				if (inst && inst->GetItem()) {
					c->Message(Chat::White, fmt::format("pack {} {} {} {}", slot, inst->GetID(), inst->IsStackable() ? inst->GetCharges() : 1,
														inst->GetItem()->Name).c_str());
				}
			}
		}
		c->Message(Chat::White, fmt::format("pack {}: room for {} more", bot->GetCleanName(), bot->PackRoom()).c_str());
		return;
	}

	if (what == "give") {
		const auto* inst = c->GetInv().GetItem(EQ::invslot::slotCursor);
		if (!inst || !inst->GetItem()) {
			c->Message(Chat::White, "pack: nothing on your cursor");
			return;
		}
		if (inst->IsClassBag()) {
			for (uint8 i = EQ::invbag::SLOT_BEGIN; i < inst->GetItem()->BagSlots; ++i) {
				if (inst->GetItem(i)) {
					c->Message(Chat::White, fmt::format("pack: empty {} first", inst->GetItem()->Name).c_str());
					return;
				}
			}
		}
		const int16 slot = bot->PackSlotFor(*inst);
		if (slot == INVALID_INDEX) {
			c->Message(Chat::White, fmt::format("pack {}: no room for {}", bot->GetCleanName(), inst->GetItem()->Name).c_str());
			return;
		}
		const std::string name = inst->GetItem()->Name;
		const int16 count = inst->IsStackable() ? inst->GetCharges() : 1;
		if (!bot->PackPut(slot, *inst)) {
			c->Message(Chat::White, fmt::format("pack {}: couldn't put {} in slot {}", bot->GetCleanName(), name, slot).c_str());
			return;
		}
		c->DeleteItemInInventory(EQ::invslot::slotCursor, 0, true);
		c->Message(Chat::White, fmt::format("pack {}: took {} {} into slot {}", bot->GetCleanName(), count, name, slot).c_str());
		return;
	}

	// take
	if (!Bot::IsPackSlot(take_slot)) {
		c->Message(Chat::White, fmt::format("pack: {} isn't a pack slot", take_slot).c_str());
		return;
	}
	const auto* inst = bot->GetBotItem(take_slot);
	if (!inst || !inst->GetItem()) {
		c->Message(Chat::White, fmt::format("pack {}: nothing in slot {}", bot->GetCleanName(), take_slot).c_str());
		return;
	}
	if (inst->IsClassBag()) {
		for (uint8 i = EQ::invbag::SLOT_BEGIN; i < inst->GetItem()->BagSlots; ++i) {
			if (bot->GetBotItem(EQ::InventoryProfile::CalcSlotId(take_slot, i))) {
				c->Message(Chat::White, fmt::format("pack {}: {} isn't empty", bot->GetCleanName(), inst->GetItem()->Name).c_str());
				return;
			}
		}
	}
	if (c->CheckLoreConflict(inst->GetItem())) {
		c->Message(Chat::White, fmt::format("pack: you already have {} (lore)", inst->GetItem()->Name).c_str());
		return;
	}
	const std::string name = inst->GetItem()->Name;
	const int16 count = inst->IsStackable() ? inst->GetCharges() : 1;
	if (!c->PushItemOnCursor(*inst, true)) {
		c->Message(Chat::White, "pack: your cursor won't take it");
		return;
	}
	bot->RemoveBotItemBySlot(take_slot);
	c->Message(Chat::White, fmt::format("pack {}: gave back {} {} from slot {}", bot->GetCleanName(), count, name, take_slot).c_str());
}
