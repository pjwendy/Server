# Underfoot Client Opcodes and Structures

This file lists all opcodes, their direction, hex value, associated structure, the full structure definition, and the full ENCODE or DECODE section for the Underfoot client protocol.

## OP_Action (0x0f14)
- **Direction:** outgoing
- **Structure:** ActionAlt_Struct

**Structure Definition:**
```cpp
struct ActionAlt_Struct
{
/*00*/	uint16 target;			// id of target
/*02*/	uint16 source;			// id of caster
/*04*/	uint16 level;			// level of caster for spells, OSX dump says attack rating, guess spells use it for level
/*06*/  uint32 unknown06;		// OSX dump says base_damage, was used for bard mod too, this is 0'd :(
/*10*/	float instrument_mod;
/*14*/  float force;
/*18*/  float hit_heading;
/*22*/  float hit_pitch;
/*26*/  uint8 type;				// 231 (0xE7) for spells, skill
/*27*/  uint32 damage;			// OSX says min_damage
/*31*/  uint16 unknown31;		// OSX says tohit
/*33*/	uint16 spell;			// spell id being cast
/*35*/	uint8 spell_level;		// level of caster again? Or maybe the castee
/*36*/	uint8 effect_flag;		// if this is 4, the effect is valid: or if two are sent at the same time?
/*37*/	uint8 spell_gem;
/*38*/	uint8 padding38[2];
/*40*/	uint32 slot[5];
/*60*/	uint32 item_cast_type;	// ItemSpellTypes enum from MQ2
/*64*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_Action)
	{
		ENCODE_LENGTH_EXACT(Action_Struct);
		SETUP_DIRECT_ENCODE(Action_Struct, structs::ActionAlt_Struct);

		OUT(target);
		OUT(source);
		OUT(level);
		eq->instrument_mod = 1.0f + (emu->instrument_mod - 10) / 10.0f;
		OUT(force);
		OUT(hit_heading);
		OUT(hit_pitch);
		OUT(type);
		OUT(spell);
		OUT(spell_level);
		OUT(effect_flag);
		eq->spell_gem = 0;
		eq->slot[0] = -1; // type
		eq->slot[1] = -1; // slot
		eq->slot[2] = -1; // sub index
		eq->slot[3] = -1; // aug index
		eq->slot[4] = -1; // unknown
		eq->item_cast_type = 0;

		FINISH_ENCODE();
	}
```

---

## OP_AdventureMerchantSell (0x179d)
- **Direction:** outgoing
- **Structure:** Adventure_Sell_Struct

**Structure Definition:**
```cpp
struct Adventure_Sell_Struct {
/*000*/	uint32	unknown000;	//0x01 - Stack Size/Charges?
/*004*/	uint32	npcid;
/*008*/	uint32	slot;
/*012*/	uint32	charges;
/*016*/	uint32	sell_price;
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_AdventureMerchantSell)
	{
		ENCODE_LENGTH_EXACT(Adventure_Sell_Struct);
		SETUP_DIRECT_ENCODE(Adventure_Sell_Struct, structs::Adventure_Sell_Struct);

		eq->unknown000 = 1;
		OUT(npcid);
		eq->slot = ServerToUFSlot(emu->slot);
		OUT(charges);
		OUT(sell_price);

		FINISH_ENCODE();
	}
```

---

## OP_AltCurrency (0x659e)
- **Direction:** outgoing
- **Structure:** AltCurrencyPopulate_Struct

**Structure Definition:**
```cpp
struct AltCurrencyPopulate_Struct {
/*000*/ uint32 opcode; //8 for populate
/*004*/ uint32 count; //number of entries
/*008*/ AltCurrencyPopulateEntry_Struct entries[0];
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_AltCurrency)
	{
		EQApplicationPacket *in = *p;
		*p = nullptr;

		unsigned char *emu_buffer = in->pBuffer;
		uint32 opcode = *((uint32*)emu_buffer);

		if (opcode == AlternateCurrencyMode::Populate) {
			AltCurrencyPopulate_Struct *populate = (AltCurrencyPopulate_Struct*)emu_buffer;

			auto outapp = new EQApplicationPacket(
			    OP_AltCurrency, sizeof(structs::AltCurrencyPopulate_Struct) +
						sizeof(structs::AltCurrencyPopulateEntry_Struct) * populate->count);
			structs::AltCurrencyPopulate_Struct *out_populate = (structs::AltCurrencyPopulate_Struct*)outapp->pBuffer;

			out_populate->opcode = populate->opcode;
			out_populate->count = populate->count;
			for (uint32 i = 0; i < populate->count; ++i) {
				out_populate->entries[i].currency_number = populate->entries[i].currency_number;
				out_populate->entries[i].currency_number2 = populate->entries[i].currency_number2;
				out_populate->entries[i].item_id = populate->entries[i].item_id;
				out_populate->entries[i].item_icon = populate->entries[i].item_icon;
				out_populate->entries[i].stack_size = populate->entries[i].stack_size;
				out_populate->entries[i].unknown00 = populate->entries[i].unknown00;
			}

			dest->FastQueuePacket(&outapp, ack_req);
		}
		else {
			auto outapp = new EQApplicationPacket(OP_AltCurrency, sizeof(AltCurrencyUpdate_Struct));
			memcpy(outapp->pBuffer, emu_buffer, sizeof(AltCurrencyUpdate_Struct));
			dest->FastQueuePacket(&outapp, ack_req);
		}

		//dest->FastQueuePacket(&outapp, ack_req);
		delete in;
	}
```

---

## OP_AltCurrencySell (0x14cf)
- **Direction:** outgoing
- **Structure:** AltCurrencySellItem_Struct

**Structure Definition:**
```cpp
struct AltCurrencySellItem_Struct {
/*000*/ uint32 merchant_entity_id;
/*004*/ uint32 slot_id;
/*008*/ uint32 charges;
/*012*/ uint32 cost;
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_AltCurrencySell)
	{
		ENCODE_LENGTH_EXACT(AltCurrencySellItem_Struct);
		SETUP_DIRECT_ENCODE(AltCurrencySellItem_Struct, structs::AltCurrencySellItem_Struct);

		OUT(merchant_entity_id);
		eq->slot_id = ServerToUFSlot(emu->slot_id);
		OUT(charges);
		OUT(cost);

		FINISH_ENCODE();
	}
```

---

## OP_ApplyPoison (0x5cd3)
- **Direction:** outgoing
- **Structure:** ApplyPoison_Struct

**Structure Definition:**
```cpp
struct ApplyPoison_Struct {
	uint32 inventorySlot;
	uint32 success;
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_ApplyPoison)
	{
		ENCODE_LENGTH_EXACT(ApplyPoison_Struct);
		SETUP_DIRECT_ENCODE(ApplyPoison_Struct, structs::ApplyPoison_Struct);

		eq->inventorySlot = ServerToUFSlot(emu->inventorySlot);
		OUT(success);

		FINISH_ENCODE();
	}
```

---

## OP_AugmentInfo (0x31b1)
- **Direction:** outgoing
- **Structure:** AugmentInfo_Struct

**Structure Definition:**
```cpp
struct AugmentInfo_Struct
{
/*000*/ uint32	itemid;			// id of the solvent needed
/*004*/ uint32	window;			// window to display the information in
/*008*/ char	augment_info[64];	// total packet length 76, all the rest were always 00
/*072*/ uint32	unknown072;
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_AugmentInfo)
	{
		ENCODE_LENGTH_EXACT(AugmentInfo_Struct);
		SETUP_DIRECT_ENCODE(AugmentInfo_Struct, structs::AugmentInfo_Struct);

		OUT(itemid);
		OUT(window);
		strn0cpy(eq->augment_info, emu->augment_info, 64);

		FINISH_ENCODE();
	}
```

---

## OP_Barter (0x6db5)
- **Direction:** outgoing
- **Structure:** (unknown)

**Structure Definition:**
```cpp
Structure definition not found.
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_Barter)
	{
		EQApplicationPacket *in = *p;
		*p = nullptr;

		char *Buffer = (char *)in->pBuffer;

		uint32 SubAction = VARSTRUCT_DECODE_TYPE(uint32, Buffer);

		if (SubAction != Barter_BuyerAppearance)
		{
			dest->FastQueuePacket(&in, ack_req);

			return;
		}

		unsigned char *__emu_buffer = in->pBuffer;

		in->size = 80;

		in->pBuffer = new unsigned char[in->size];

		char *OutBuffer = (char *)in->pBuffer;

		char Name[64];

		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, SubAction);
		uint32 EntityID = VARSTRUCT_DECODE_TYPE(uint32, Buffer);
		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, EntityID);
		uint8 Toggle = VARSTRUCT_DECODE_TYPE(uint8, Buffer);
		VARSTRUCT_DECODE_STRING(Name, Buffer);
		VARSTRUCT_ENCODE_STRING(OutBuffer, Name);
		OutBuffer = (char *)in->pBuffer + 72;
		VARSTRUCT_ENCODE_TYPE(uint8, OutBuffer, Toggle);

		delete[] __emu_buffer;
		dest->FastQueuePacket(&in, ack_req);
	}
```

---

## OP_BazaarSearch (0x550f)
- **Direction:** outgoing
- **Structure:** UFBazaarTraderBuyerActions

**Structure Definition:**
```cpp
Structure definition not found.
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_BazaarSearch)
	{
		EQApplicationPacket *in = *p;
		*p = nullptr;

		uint32 action = *(uint32 *)in->pBuffer;

		switch (action) {
			case BazaarSearch: {
				LogTrading(
					"Encode OP_BazaarSearch(UF) BazaarSearch action <green>[{}]",
					action
				);
				std::vector<BazaarSearchResultsFromDB_Struct> results {};
				auto bsms = (BazaarSearchMessaging_Struct *)in->pBuffer;
				EQ::Util::MemoryStreamReader ss(
					reinterpret_cast<char *>(bsms->payload),
					in->size - sizeof(BazaarSearchMessaging_Struct)
				);
				cereal::BinaryInputArchive ar(ss);
				ar(results);

				auto  size    = results.size() * sizeof(BazaarSearchResults_Struct);
				auto  buffer  = new uchar[size];
				uchar *bufptr = buffer;
				memset(buffer, 0, size);

				for (auto row = results.begin(); row != results.end(); ++row) {
					VARSTRUCT_ENCODE_TYPE(uint32, bufptr, structs::UFBazaarTraderBuyerActions::BazaarSearch);
					VARSTRUCT_ENCODE_TYPE(uint32, bufptr, row->trader_entity_id);
					strn0cpy(reinterpret_cast<char *>(bufptr), row->trader_name.c_str(), 64);
					bufptr += 64;
					VARSTRUCT_ENCODE_TYPE(uint32, bufptr, 1);
					VARSTRUCT_ENCODE_TYPE(int32, bufptr, row->item_id);
					VARSTRUCT_ENCODE_TYPE(int32, bufptr, row->serial_number);
					bufptr += 4;
					if (row->stackable) {
						strn0cpy(
							reinterpret_cast<char *>(bufptr),
							fmt::format("{}({})", row->item_name.c_str(), row->charges).c_str(),
							64
						);
					}
					else {
						strn0cpy(
							reinterpret_cast<char *>(bufptr),
							fmt::format("{}({})", row->item_name.c_str(), row->count).c_str(),
							64
						);
					}
					bufptr += 64;
					VARSTRUCT_ENCODE_TYPE(uint32, bufptr, row->cost);
					VARSTRUCT_ENCODE_TYPE(uint32, bufptr, row->item_stat);
				}

				auto outapp = new EQApplicationPacket(OP_BazaarSearch, size);
				memcpy(outapp->pBuffer, buffer, size);
				dest->FastQueuePacket(&outapp);

				safe_delete(outapp);
				safe_delete_array(buffer);
				safe_delete(in);
				break;
			}
			case BazaarInspect:
			case WelcomeMessage: {
				LogTrading(
					"Encode OP_BazaarSearch(UF) BazaarInspect/WelcomeMessage action <green>[{}]",
					action
				);
				dest->FastQueuePacket(&in, ack_req);
				break;
			}
			default: {
				LogTrading(
					"Encode OP_BazaarSearch(UF) unhandled action <red>[{}]",
					action
				);
				dest->FastQueuePacket(&in, ack_req);
			}
		}
	}
```

---

## OP_BecomeTrader (0x0a1d)
- **Direction:** outgoing
- **Structure:** BecomeTrader_Struct

**Structure Definition:**
```cpp
struct BecomeTrader_Struct {
	uint32 entity_id;
	uint32 action;
	char   trader_name[64];
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_BecomeTrader)
	{
		uint32 action = *(uint32 *)(*p)->pBuffer;

		switch (action)
		{
			case TraderOff:
			{
				ENCODE_LENGTH_EXACT(BecomeTrader_Struct);
				SETUP_DIRECT_ENCODE(BecomeTrader_Struct, structs::BecomeTrader_Struct);
				LogTrading(
                    "Encode OP_BecomeTrader(UF) TraderOff action <green>[{}] entity_id <green>[{}] trader_name "
                    "<green>[{}]",
                    emu->action,
                    emu->entity_id,
                    emu->trader_name
                );
				eq->action    = structs::UFBazaarTraderBuyerActions::Zero;
				eq->entity_id = emu->entity_id;
				FINISH_ENCODE();
				break;
			}
			case TraderOn:
			{
				ENCODE_LENGTH_EXACT(BecomeTrader_Struct);
				SETUP_DIRECT_ENCODE(BecomeTrader_Struct, structs::BecomeTrader_Struct);
				LogTrading(
                    "Encode OP_BecomeTrader(UF) TraderOn action <green>[{}] entity_id <green>[{}] trader_name "
                    "<green>[{}]",
                    emu->action,
                    emu->entity_id,
                    emu->trader_name
                );
				eq->action    = structs::UFBazaarTraderBuyerActions::BeginTraderMode;
				eq->entity_id = emu->entity_id;
				strn0cpy(eq->trader_name, emu->trader_name, sizeof(eq->trader_name));
				FINISH_ENCODE();
				break;
			}
			default:
			{
				LogTrading(
					"Encode OP_BecomeTrader(UF) unhandled action <red>[{}] Sending packet as is.",
					action
				);
				EQApplicationPacket *in = *p;
				*p = nullptr;
				dest->FastQueuePacket(&in, ack_req);
			}
		}
	}
```

---

## OP_Buff (0x0d1d)
- **Direction:** outgoing
- **Structure:** SpellBuffPacket_Struct

**Structure Definition:**
```cpp
struct SpellBuffPacket_Struct {
/*000*/	uint32 entityid;	// Player id who cast the buff
/*004*/	SpellBuff_Struct buff;
/*080*/	uint32 slotid;
/*084*/	uint32 bufffade;
/*088*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_Buff)
	{
		ENCODE_LENGTH_EXACT(SpellBuffPacket_Struct);
		SETUP_DIRECT_ENCODE(SpellBuffPacket_Struct, structs::SpellBuffPacket_Struct);

		OUT(entityid);
		OUT(buff.effect_type);
		OUT(buff.level);
		// just so we're 100% sure we get a 1.0f ...
		eq->buff.bard_modifier = emu->buff.bard_modifier == 10 ? 1.0f : emu->buff.bard_modifier / 10.0f;
		OUT(buff.spellid);
		OUT(buff.duration);
		OUT(buff.num_hits);
		// TODO: implement slot_data stuff
		eq->slotid = ServerToUFBuffSlot(emu->slotid);
		OUT(bufffade);	// Live (October 2011) sends a 2 rather than 0 when a buff is created, but it doesn't seem to matter.

		FINISH_ENCODE();
	}
```

---

## OP_BuffCreate (0x2121)
- **Direction:** outgoing
- **Structure:** BuffIcon_Struct

**Structure Definition:**
```cpp
struct BuffIcon_Struct {
/*000*/ uint32 entity_id;
/*004*/ uint32 unknown004;
/*008*/ uint8  all_buffs; // 1 when updating all buffs, 0 when doing one
/*009*/ uint16 count;
/*011*/ BuffIconEntry_Struct entires[0];
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_BuffCreate)
	{
		SETUP_VAR_ENCODE(BuffIcon_Struct);

		uint32 sz = 12 + (17 * emu->count) + emu->name_lengths; // 17 includes nullterm
		__packet->size = sz;
		__packet->pBuffer = new unsigned char[sz];
		memset(__packet->pBuffer, 0, sz);

		__packet->WriteUInt32(emu->entity_id);
		__packet->WriteUInt32(emu->tic_timer);
		__packet->WriteUInt8(emu->all_buffs); // 1 = all buffs, 0 = 1 buff
		__packet->WriteUInt16(emu->count);

		for (int i = 0; i < emu->count; ++i)
		{
			__packet->WriteUInt32(emu->type == 0 ? ServerToUFBuffSlot(emu->entries[i].buff_slot) : emu->entries[i].buff_slot);
			__packet->WriteUInt32(emu->entries[i].spell_id);
			__packet->WriteUInt32(emu->entries[i].tics_remaining);
			__packet->WriteUInt32(emu->entries[i].num_hits);
			__packet->WriteString(emu->entries[i].caster);
		}
		__packet->WriteUInt8(emu->type);

		FINISH_ENCODE();
		/*
		uint32 write_var32 = 60;
		uint8 write_var8 = 1;
		ss.write((const char*)&emu->entity_id, sizeof(uint32));
		ss.write((const char*)&write_var32, sizeof(uint32));
		ss.write((const char*)&write_var8, sizeof(uint8));
		ss.write((const char*)&emu->count, sizeof(uint16));
		write_var32 = 0;
		write_var8 = 0;
		for(uint16 i = 0; i < emu->count; ++i)
		{
		if(emu->entries[i].buff_slot >= 25 && emu->entries[i].buff_slot < 37)
		{
		emu->entries[i].buff_slot += 5;
		}
		else if(emu->entries[i].buff_slot >= 37)
		{
		emu->entries[i].buff_slot += 14;
		}
		ss.write((const char*)&emu->entries[i].buff_slot, sizeof(uint32));
		ss.write((const char*)&emu->entries[i].spell_id, sizeof(uint32));
		ss.write((const char*)&emu->entries[i].tics_remaining, sizeof(uint32));
		ss.write((const char*)&write_var32, sizeof(uint32));
		ss.write((const char*)&write_var8, sizeof(uint8));
		}
		ss.write((const char*)&write_var8, sizeof(uint8));
		*/
	}
```

---

## OP_CancelTrade (0x527e)
- **Direction:** outgoing
- **Structure:** CancelTrade_Struct

**Structure Definition:**
```cpp
struct CancelTrade_Struct {
/*00*/	uint32 fromid;
/*04*/	uint32 action;
/*08*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_CancelTrade)
	{
		ENCODE_LENGTH_EXACT(CancelTrade_Struct);
		SETUP_DIRECT_ENCODE(CancelTrade_Struct, structs::CancelTrade_Struct);

		OUT(fromid);
		OUT(action);

		FINISH_ENCODE();
	}
```

---

## OP_ChannelMessage (0x2e79)
- **Direction:** outgoing
- **Structure:** ChannelMessage_Struct

**Structure Definition:**
```cpp
struct ChannelMessage_Struct
{
/*000*/	char	targetname[64];		// Tell recipient
/*064*/	char	sender[64];			// The senders name (len might be wrong)
/*128*/	uint32	language;			// Language
/*132*/	uint32	chan_num;			// Channel
/*136*/	uint32	cm_unknown4[2];		// ***Placeholder
/*144*/	uint32	skill_in_language;	// The players skill in this language? might be wrong
/*148*/	char	message[0];			// Variable length message
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_ChannelMessage)
	{
		EQApplicationPacket *in = *p;
		*p = nullptr;

		ChannelMessage_Struct *emu = (ChannelMessage_Struct *)in->pBuffer;

		unsigned char *__emu_buffer = in->pBuffer;

		std::string old_message = emu->message;
		std::string new_message;
		ServerToUFSayLink(new_message, old_message);

		//in->size = strlen(emu->sender) + 1 + strlen(emu->targetname) + 1 + strlen(emu->message) + 1 + 36;
		in->size = strlen(emu->sender) + strlen(emu->targetname) + new_message.length() + 39;

		in->pBuffer = new unsigned char[in->size];

		char *OutBuffer = (char *)in->pBuffer;

		VARSTRUCT_ENCODE_STRING(OutBuffer, emu->sender);
		VARSTRUCT_ENCODE_STRING(OutBuffer, emu->targetname);
		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, 0);	// Unknown
		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, emu->language);
		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, emu->chan_num);
		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, 0);	// Unknown
		VARSTRUCT_ENCODE_TYPE(uint8, OutBuffer, 0);	// Unknown
		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, emu->skill_in_language);
		VARSTRUCT_ENCODE_STRING(OutBuffer, new_message.c_str());

		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, 0);	// Unknown
		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, 0);	// Unknown
		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, 0);	// Unknown
		VARSTRUCT_ENCODE_TYPE(uint16, OutBuffer, 0);	// Unknown
		VARSTRUCT_ENCODE_TYPE(uint8, OutBuffer, 0);	// Unknown

		delete[] __emu_buffer;
		dest->FastQueuePacket(&in, ack_req);
	}
```

---

## OP_CharInventory (0x47ae)
- **Direction:** outgoing
- **Structure:** (unknown)

**Structure Definition:**
```cpp
Structure definition not found.
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_CharInventory)
	{
		//consume the packet
		EQApplicationPacket* in = *p;
		*p = nullptr;

		if (!in->size) {
			in->size = 4;
			in->pBuffer = new uchar[in->size];
			memset(in->pBuffer, 0, in->size);

			dest->FastQueuePacket(&in, ack_req);
			return;
		}

		//store away the emu struct
		uchar* __emu_buffer = in->pBuffer;

		int item_count = in->size / sizeof(EQ::InternalSerializedItem_Struct);
		if (!item_count || (in->size % sizeof(EQ::InternalSerializedItem_Struct)) != 0) {
			Log(Logs::General, Logs::Netcode, "[STRUCTS] Wrong size on outbound %s: Got %d, expected multiple of %d",
				opcodes->EmuToName(in->GetOpcode()), in->size, sizeof(EQ::InternalSerializedItem_Struct));

			delete in;
			return;
		}

		EQ::InternalSerializedItem_Struct* eq = (EQ::InternalSerializedItem_Struct*)in->pBuffer;

		EQ::OutBuffer ob;
		EQ::OutBuffer::pos_type last_pos = ob.tellp();

		ob.write((const char*)&item_count, sizeof(uint32));

		for (int index = 0; index < item_count; ++index, ++eq) {
			SerializeItem(ob, (const EQ::ItemInstance*)eq->inst, eq->slot_id, 0);
			if (ob.tellp() == last_pos)
				LogNetcode("UF::ENCODE(OP_CharInventory) Serialization failed on item slot [{}] during OP_CharInventory.  Item skipped", eq->slot_id);

			last_pos = ob.tellp();
		}

		in->size = ob.size();
		in->pBuffer = ob.detach();

		delete[] __emu_buffer;

		dest->FastQueuePacket(&in, ack_req);
	}
```

---

## OP_ClientUpdate (0x7062)
- **Direction:** outgoing
- **Structure:** PlayerPositionUpdateServer_Struct

**Structure Definition:**
```cpp
struct PlayerPositionUpdateServer_Struct
{
/*0000*/	uint16		spawn_id;
/*0002*/	signed		padding0000 : 12;	// ***Placeholder
			signed		delta_x : 13;		// change in x
			signed		padding0005 : 7;	// ***Placeholder
/*0006*/	signed		delta_heading : 10;	// change in heading
			signed		delta_y : 13;		// change in y
			signed		padding0006 : 9;	// ***Placeholder
/*0010*/	signed		y_pos : 19;			// y coord
			signed		animation : 10;		// animation
			signed		padding0010 : 3;	// ***Placeholder
/*0014*/	unsigned	heading : 12;		// heading
			signed		x_pos : 19;			// x coord
			signed		padding0014 : 1;	// ***Placeholder
/*0018*/	signed		z_pos : 19;			// z coord
			signed		delta_z : 13;		// change in z
/*0022*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_ClientUpdate)
	{
		ENCODE_LENGTH_EXACT(PlayerPositionUpdateServer_Struct);
		SETUP_DIRECT_ENCODE(PlayerPositionUpdateServer_Struct, structs::PlayerPositionUpdateServer_Struct);

		OUT(spawn_id);
		OUT(x_pos);
		OUT(delta_x);
		OUT(delta_y);
		OUT(z_pos);
		OUT(delta_heading);
		OUT(y_pos);
		OUT(delta_z);
		OUT(animation);
		OUT(heading);

		FINISH_ENCODE();
	}
```

---

## OP_Consider (0x3c2d)
- **Direction:** outgoing
- **Structure:** Consider_Struct

**Structure Definition:**
```cpp
struct Consider_Struct{
/*000*/ uint32	playerid;               // PlayerID
/*004*/ uint32	targetid;               // TargetID
/*008*/ uint32	faction;                // Faction
/*012*/ uint32	level;					// Level
/*016*/ uint8	pvpcon;					// Pvp con flag 0/1
/*017*/ uint8	unknown017[3];			//
/*020*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_Consider)
	{
		ENCODE_LENGTH_EXACT(Consider_Struct);
		SETUP_DIRECT_ENCODE(Consider_Struct, structs::Consider_Struct);

		OUT(playerid);
		OUT(targetid);
		OUT(faction);
		OUT(level);
		OUT(pvpcon);

		FINISH_ENCODE();
	}
```

---

## OP_Damage (0x631a)
- **Direction:** outgoing
- **Structure:** CombatDamage_Struct

**Structure Definition:**
```cpp
struct CombatDamage_Struct
{
/* 00 */	uint16	target;
/* 02 */	uint16	source;
/* 04 */	uint8	type;			//slashing, etc.  231 (0xE7) for spells
/* 05 */	uint16	spellid;
/* 07 */	int32	damage;
/* 11 */	float	force;		// cd cc cc 3d
/* 15 */	float	hit_heading;		// see above notes in Action_Struct
/* 19 */	float	hit_pitch;
/* 23 */	uint8	secondary;	// 0 for primary hand, 1 for secondary
/* 24 */	uint32	special; // 2 = Rampage, 1 = Wild Rampage
/* 28 */
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_Damage)
	{
		ENCODE_LENGTH_EXACT(CombatDamage_Struct);
		SETUP_DIRECT_ENCODE(CombatDamage_Struct, structs::CombatDamage_Struct);

		OUT(target);
		OUT(source);
		OUT(type);
		OUT(spellid);
		OUT(damage);
		OUT(force);
		OUT(hit_heading);
		OUT(hit_pitch);
		OUT(special);

		FINISH_ENCODE();
	}
```

---

## OP_DeleteCharge (0x4ca1)
- **Direction:** outgoing
- **Structure:** (unknown)

**Structure Definition:**
```cpp
Structure definition not found.
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_DeleteCharge)
	{
		Log(Logs::Detail, Logs::Netcode, "UF::ENCODE(OP_DeleteCharge)");

		ENCODE_FORWARD(OP_MoveItem);
	}
```

---

## OP_DeleteItem (0x66e0)
- **Direction:** outgoing
- **Structure:** DeleteItem_Struct

**Structure Definition:**
```cpp
struct DeleteItem_Struct {
/*0000*/ uint32	from_slot;
/*0004*/ uint32	to_slot;
/*0008*/ uint32	number_in_stack;
/*0012*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_DeleteItem)
	{
		ENCODE_LENGTH_EXACT(DeleteItem_Struct);
		SETUP_DIRECT_ENCODE(DeleteItem_Struct, structs::DeleteItem_Struct);

		eq->from_slot = ServerToUFSlot(emu->from_slot);
		eq->to_slot = ServerToUFSlot(emu->to_slot);
		OUT(number_in_stack);

		FINISH_ENCODE();
	}
```

---

## OP_DisciplineUpdate (0x6ed3)
- **Direction:** outgoing
- **Structure:** Disciplines_Struct

**Structure Definition:**
```cpp
struct Disciplines_Struct {
	uint32 values[MAX_PP_DISCIPLINES];
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_DisciplineUpdate)
	{
		ENCODE_LENGTH_EXACT(Disciplines_Struct);
		SETUP_DIRECT_ENCODE(Disciplines_Struct, structs::Disciplines_Struct);

		memcpy(&eq->values, &emu->values, sizeof(Disciplines_Struct));

		FINISH_ENCODE();
	}
```

---

## OP_DzChooseZone (0x65e1)
- **Direction:** outgoing
- **Structure:** DynamicZoneChooseZone_Struct

**Structure Definition:**
```cpp
struct DynamicZoneChooseZone_Struct
{
/*000*/ uint32 client_id;
/*004*/ uint32 count;
/*008*/ DynamicZoneChooseZoneEntry_Struct choices[0];
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_DzChooseZone)
	{
		SETUP_VAR_ENCODE(DynamicZoneChooseZone_Struct);

		SerializeBuffer buf;
		buf.WriteUInt32(emu->client_id);
		buf.WriteUInt32(emu->count);

		for (uint32 i = 0; i < emu->count; ++i)
		{
			buf.WriteUInt16(emu->choices[i].dz_zone_id);
			buf.WriteUInt16(emu->choices[i].dz_instance_id);
			buf.WriteUInt32(emu->choices[i].unknown_id1);
			buf.WriteUInt32(emu->choices[i].dz_type);
			buf.WriteUInt32(emu->choices[i].unknown_id2);
			buf.WriteString(emu->choices[i].description);
			buf.WriteString(emu->choices[i].leader_name);
		}

		__packet->size = buf.size();
		__packet->pBuffer = new unsigned char[__packet->size];
		memcpy(__packet->pBuffer, buf.buffer(), __packet->size);

		FINISH_ENCODE();
	}
```

---

## OP_DzExpeditionEndsWarning (0x6ac2)
- **Direction:** outgoing
- **Structure:** ExpeditionExpireWarning

**Structure Definition:**
```cpp
struct ExpeditionExpireWarning
{
/*000*/ uint32 client_id;
/*004*/ uint32 unknown004;
/*008*/ uint32 minutes_remaining;
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_DzExpeditionEndsWarning)
	{
		ENCODE_LENGTH_EXACT(ExpeditionExpireWarning);
		SETUP_DIRECT_ENCODE(ExpeditionExpireWarning, structs::ExpeditionExpireWarning);

		OUT(minutes_remaining);

		FINISH_ENCODE();
	}
```

---

## OP_DzExpeditionInfo (0x1150)
- **Direction:** outgoing
- **Structure:** DynamicZoneInfo_Struct

**Structure Definition:**
```cpp
struct DynamicZoneInfo_Struct
{
/*000*/ uint32 client_id;
/*004*/ uint32 unknown004;
/*008*/ uint32 assigned; // padded bool
/*012*/ uint32 max_players;
/*016*/ char   dz_name[128];
/*144*/ char   leader_name[64];
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_DzExpeditionInfo)
	{
		ENCODE_LENGTH_EXACT(DynamicZoneInfo_Struct);
		SETUP_DIRECT_ENCODE(DynamicZoneInfo_Struct, structs::DynamicZoneInfo_Struct);

		OUT(client_id);
		OUT(assigned);
		OUT(max_players);
		strn0cpy(eq->dz_name, emu->dz_name, sizeof(eq->dz_name));
		strn0cpy(eq->leader_name, emu->leader_name, sizeof(eq->leader_name));

		FINISH_ENCODE();
	}
```

---

## OP_DzExpeditionInvite (0x3c5e)
- **Direction:** outgoing
- **Structure:** ExpeditionInvite_Struct

**Structure Definition:**
```cpp
struct ExpeditionInvite_Struct
{
/*000*/ uint32 client_id;            // unique character id
/*004*/ uint32 unknown004;
/*008*/ char   inviter_name[64];
/*072*/ char   expedition_name[128];
/*200*/ uint8  swapping;             // 0: adding 1: swapping
/*201*/ char   swap_name[64];        // if swapping, swap name being removed
/*265*/ uint8  padding[3];
/*268*/ uint16 dz_zone_id;           // dz_id zone/instance pair, sent back in reply
/*270*/ uint16 dz_instance_id;
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_DzExpeditionInvite)
	{
		ENCODE_LENGTH_EXACT(ExpeditionInvite_Struct);
		SETUP_DIRECT_ENCODE(ExpeditionInvite_Struct, structs::ExpeditionInvite_Struct);

		OUT(client_id);
		strn0cpy(eq->inviter_name, emu->inviter_name, sizeof(eq->inviter_name));
		strn0cpy(eq->expedition_name, emu->expedition_name, sizeof(eq->expedition_name));
		OUT(swapping);
		strn0cpy(eq->swap_name, emu->swap_name, sizeof(eq->swap_name));
		OUT(dz_zone_id);
		OUT(dz_instance_id);

		FINISH_ENCODE();
	}
```

---

## OP_DzExpeditionLockoutTimers (0x70d8)
- **Direction:** outgoing
- **Structure:** ExpeditionLockoutTimers_Struct

**Structure Definition:**
```cpp
struct ExpeditionLockoutTimers_Struct
{
/*000*/ uint32 client_id;
/*004*/ uint32 count;
/*008*/ ExpeditionLockoutTimerEntry_Struct timers[0];
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_DzExpeditionLockoutTimers)
	{
		SETUP_VAR_ENCODE(ExpeditionLockoutTimers_Struct);

		SerializeBuffer buf;
		buf.WriteUInt32(emu->client_id);
		buf.WriteUInt32(emu->count);
		for (uint32 i = 0; i < emu->count; ++i)
		{
			buf.WriteString(emu->timers[i].expedition_name);
			buf.WriteUInt32(emu->timers[i].seconds_remaining);
			buf.WriteInt32(emu->timers[i].event_type);
			buf.WriteString(emu->timers[i].event_name);
		}

		__packet->size = buf.size();
		__packet->pBuffer = new unsigned char[__packet->size];
		memcpy(__packet->pBuffer, buf.buffer(), __packet->size);

		FINISH_ENCODE();
	}
```

---

## OP_DzMemberList (0x15c4)
- **Direction:** outgoing
- **Structure:** DynamicZoneMemberList_Struct

**Structure Definition:**
```cpp
struct DynamicZoneMemberList_Struct
{
/*000*/ uint32 client_id;
/*004*/ uint32 member_count; // number of players in window
/*008*/ DynamicZoneMemberEntry_Struct members[0]; // variable length
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_DzMemberList)
	{
		SETUP_VAR_ENCODE(DynamicZoneMemberList_Struct);

		SerializeBuffer buf;
		buf.WriteUInt32(emu->client_id);
		buf.WriteUInt32(emu->member_count);
		for (uint32 i = 0; i < emu->member_count; ++i)
		{
			buf.WriteString(emu->members[i].name);
			buf.WriteUInt8(emu->members[i].online_status);
		}

		__packet->size = buf.size();
		__packet->pBuffer = new unsigned char[__packet->size];
		memcpy(__packet->pBuffer, buf.buffer(), __packet->size);

		FINISH_ENCODE();
	}
```

---

## OP_DzMemberListName (0x2d17)
- **Direction:** outgoing
- **Structure:** DynamicZoneMemberListName_Struct

**Structure Definition:**
```cpp
struct DynamicZoneMemberListName_Struct
{
/*000*/ uint32 client_id;
/*004*/ uint32 unknown004;
/*008*/ uint32 add_name;   // padded bool, 0: remove name, 1: add name with unknown status
/*012*/ char   name[64];
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_DzMemberListName)
	{
		ENCODE_LENGTH_EXACT(DynamicZoneMemberListName_Struct);
		SETUP_DIRECT_ENCODE(DynamicZoneMemberListName_Struct, structs::DynamicZoneMemberListName_Struct);

		OUT(client_id);
		OUT(add_name);
		strn0cpy(eq->name, emu->name, sizeof(eq->name));

		FINISH_ENCODE();
	}
```

---

## OP_DzMemberListStatus (0x0d98)
- **Direction:** outgoing
- **Structure:** DynamicZoneMemberList_Struct

**Structure Definition:**
```cpp
struct DynamicZoneMemberList_Struct
{
/*000*/ uint32 client_id;
/*004*/ uint32 member_count; // number of players in window
/*008*/ DynamicZoneMemberEntry_Struct members[0]; // variable length
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_DzMemberListStatus)
	{
		auto emu = reinterpret_cast<DynamicZoneMemberList_Struct*>((*p)->pBuffer);
		if (emu->member_count == 1)
		{
			ENCODE_FORWARD(OP_DzMemberList);
		}
	}
```

---

## OP_DzSetLeaderName (0x2caf)
- **Direction:** outgoing
- **Structure:** DynamicZoneLeaderName_Struct

**Structure Definition:**
```cpp
struct DynamicZoneLeaderName_Struct
{
/*000*/ uint32 client_id;
/*004*/ uint32 unknown004;
/*008*/ char   leader_name[64];
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_DzSetLeaderName)
	{
		ENCODE_LENGTH_EXACT(DynamicZoneLeaderName_Struct);
		SETUP_DIRECT_ENCODE(DynamicZoneLeaderName_Struct, structs::DynamicZoneLeaderName_Struct);

		OUT(client_id);
		strn0cpy(eq->leader_name, emu->leader_name, sizeof(eq->leader_name));

		FINISH_ENCODE();
	}
```

---

## OP_Emote (0x3164)
- **Direction:** outgoing
- **Structure:** Emote_Struct

**Structure Definition:**
```cpp
struct Emote_Struct {
/*0000*/	uint32 unknown01;
/*0004*/	char message[1024]; // was 1024
/*1028*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_Emote)
	{
		EQApplicationPacket *in = *p;
		*p = nullptr;

		Emote_Struct *emu = (Emote_Struct *)in->pBuffer;

		unsigned char *__emu_buffer = in->pBuffer;

		std::string old_message = emu->message;
		std::string new_message;
		ServerToUFSayLink(new_message, old_message);

		//if (new_message.length() > 512) // length restricted in packet building function due vari-length name size (no nullterm)
		//	new_message = new_message.substr(0, 512);

		in->size = new_message.length() + 5;
		in->pBuffer = new unsigned char[in->size];

		char *OutBuffer = (char *)in->pBuffer;

		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, emu->type);
		VARSTRUCT_ENCODE_STRING(OutBuffer, new_message.c_str());

		delete[] __emu_buffer;
		dest->FastQueuePacket(&in, ack_req);
	}
```

---

## OP_ExpansionInfo (0x7e4d)
- **Direction:** outgoing
- **Structure:** ExpansionInfo_Struct

**Structure Definition:**
```cpp
struct ExpansionInfo_Struct {
/*000*/	char	Unknown000[64];
/*064*/	uint32	Expansions;
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_ExpansionInfo)
	{
		ENCODE_LENGTH_EXACT(ExpansionInfo_Struct);
		SETUP_DIRECT_ENCODE(ExpansionInfo_Struct, structs::ExpansionInfo_Struct);

		OUT(Expansions);

		FINISH_ENCODE();
	}
```

---

## OP_FormattedMessage (0x3b52)
- **Direction:** outgoing
- **Structure:** FormattedMessage_Struct

**Structure Definition:**
```cpp
struct FormattedMessage_Struct{
	uint32	unknown0;
	uint32	string_id;
	uint32	type;
	char	message[0];
//*0???*/ uint8  unknown0[8];         // ***Placeholder
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_FormattedMessage)
	{
		EQApplicationPacket *in = *p;
		*p = nullptr;

		FormattedMessage_Struct *emu = (FormattedMessage_Struct *)in->pBuffer;

		unsigned char *__emu_buffer = in->pBuffer;

		char *old_message_ptr = (char *)in->pBuffer;
		old_message_ptr += sizeof(FormattedMessage_Struct);

		std::string old_message_array[9];

		for (int i = 0; i < 9; ++i) {
			if (*old_message_ptr == 0) { break; }
			old_message_array[i] = old_message_ptr;
			old_message_ptr += old_message_array[i].length() + 1;
		}

		uint32 new_message_size = 0;
		std::string new_message_array[9];

		for (int i = 0; i < 9; ++i) {
			if (old_message_array[i].length() == 0) { break; }
			ServerToUFSayLink(new_message_array[i], old_message_array[i]);
			new_message_size += new_message_array[i].length() + 1;
		}

		in->size = sizeof(FormattedMessage_Struct) + new_message_size + 1;
		in->pBuffer = new unsigned char[in->size];

		char *OutBuffer = (char *)in->pBuffer;

		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, emu->unknown0);
		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, emu->string_id);
		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, emu->type);

		for (int i = 0; i < 9; ++i) {
			if (new_message_array[i].length() == 0) { break; }
			VARSTRUCT_ENCODE_STRING(OutBuffer, new_message_array[i].c_str());
		}

		VARSTRUCT_ENCODE_TYPE(uint8, OutBuffer, 0);

		delete[] __emu_buffer;
		dest->FastQueuePacket(&in, ack_req);
	}
```

---

## OP_GroundSpawn (0x5c85)
- **Direction:** outgoing
- **Structure:** Object_Struct

**Structure Definition:**
```cpp
struct Object_Struct {
/*00*/	uint32	linked_list_addr[2];// They are, get this, prev and next, ala linked list
/*08*/	uint32	unknown008;			// Something related to the linked list?
/*12*/	uint32	drop_id;			// Unique object id for zone
/*16*/	uint16	zone_id;			// Redudant, but: Zone the object appears in
/*18*/	uint16	zone_instance;		//
/*20*/	uint32	unknown020;			// 00 00 00 00
/*24*/	uint32	unknown024;			// 53 9e f9 7e - same for all objects in the zone?
/*40*/	float	heading;			// heading
/*00*/	float	x_tilt;				//Tilt entire object on X axis
/*00*/	float	y_tilt;				//Tilt entire object on Y axis
/*28*/	float	size;			// Size - default 1
/*44*/	float	z;					// z coord
/*48*/	float	x;					// x coord
/*52*/	float	y;					// y coord
/*56*/	char	object_name[32];	// Name of object, usually something like IT63_ACTORDEF was [20]
/*88*/	uint32	unknown088;			// unique ID?  Maybe for a table that includes the contents?
/*92*/	uint32	object_type;		// Type of object, not directly translated to OP_OpenObject
/*96*/	uint8	unknown096[4];		// ff ff ff ff
/*100*/	uint32	spawn_id;			// Spawn Id of client interacting with object
/*104*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_GroundSpawn)
	{
		// We are not encoding the spawn_id field here, or a size but it doesn't appear to matter.
		//
		EQApplicationPacket *in = *p;
		*p = nullptr;

		Object_Struct *emu = (Object_Struct *)in->pBuffer;
		unsigned char *__emu_buffer = in->pBuffer;
		in->size = strlen(emu->object_name) + 58;
		in->pBuffer = new unsigned char[in->size];
		char *OutBuffer = (char *)in->pBuffer;

		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, emu->drop_id);
		VARSTRUCT_ENCODE_STRING(OutBuffer, emu->object_name);
		VARSTRUCT_ENCODE_TYPE(uint16, OutBuffer, emu->zone_id);
		VARSTRUCT_ENCODE_TYPE(uint16, OutBuffer, emu->zone_instance);
		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, 0);	// Unknown, observed 0x00006762
		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, 0);	// Unknown, observer 0x7fffbb64
		VARSTRUCT_ENCODE_TYPE(float, OutBuffer, emu->heading);
		// This next field is actually a float. There is a groundspawn in freeportwest (sack of money sitting on some barrels) which requires this
		// field to be set to (float)255.0 to appear at all, and also the size field below to be 5, to be the correct size. I think SoD has the same
		// issue.
		VARSTRUCT_ENCODE_TYPE(float, OutBuffer, emu->tilt_x); //X tilt
		VARSTRUCT_ENCODE_TYPE(float, OutBuffer, emu->tilt_y);	//Y tilt
		VARSTRUCT_ENCODE_TYPE(float, OutBuffer, emu->size != 0 && (float)emu->size < 5000.f ? (float)((float)emu->size / 100.0f) : 1.f );	// This appears to be the size field. Hackish logic because some PEQ DB items were corrupt.
		VARSTRUCT_ENCODE_TYPE(float, OutBuffer, emu->y);
		VARSTRUCT_ENCODE_TYPE(float, OutBuffer, emu->x);
		VARSTRUCT_ENCODE_TYPE(float, OutBuffer, emu->z);
		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, emu->object_type);	// Unknown, observed 0x00000014
		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, 0xffffffff);	// Unknown, observed 0xffffffff
		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, 0);	// Unknown, observed 0x00000014
		VARSTRUCT_ENCODE_TYPE(uint8, OutBuffer, 0);	// Unknown, observed 0x00

		delete[] __emu_buffer;
		dest->FastQueuePacket(&in, ack_req);
	}
```

---

## OP_GroupCancelInvite (0x2736)
- **Direction:** outgoing
- **Structure:** GroupCancel_Struct

**Structure Definition:**
```cpp
struct GroupCancel_Struct {
/*000*/	char	name1[64];
/*064*/	char	name2[64];
/*128*/	uint8	unknown128[20];
/*148*/	uint32	toggle;
/*152*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_GroupCancelInvite)
	{
		ENCODE_LENGTH_EXACT(GroupCancel_Struct);
		SETUP_DIRECT_ENCODE(GroupCancel_Struct, structs::GroupCancel_Struct);

		memcpy(eq->name1, emu->name1, sizeof(eq->name1));
		memcpy(eq->name2, emu->name2, sizeof(eq->name2));
		OUT(toggle);

		FINISH_ENCODE();
	}
```

---

## OP_GroupFollow (0x7f2b)
- **Direction:** outgoing
- **Structure:** GroupFollow_Struct

**Structure Definition:**
```cpp
struct GroupFollow_Struct { // Underfoot Follow Struct
/*0000*/	char	name1[64];	// inviter
/*0064*/	char	name2[64];	// invitee
/*0128*/	uint32	unknown0128;	// Seen 0
/*0132*/	uint32	unknown0132;	// Group ID or member level?
/*0136*/	uint32	unknown0136;	// Maybe Voice Chat Channel or Group ID?
/*0140*/	uint32	unknown0140;	// Seen 0
/*0144*/	uint32	unknown0144;	// Seen 0
/*0148*/	uint32	unknown0148;
/*0152*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_GroupFollow)
	{
		ENCODE_LENGTH_EXACT(GroupGeneric_Struct);
		SETUP_DIRECT_ENCODE(GroupGeneric_Struct, structs::GroupFollow_Struct);

		memcpy(eq->name1, emu->name1, sizeof(eq->name1));
		memcpy(eq->name2, emu->name2, sizeof(eq->name2));

		FINISH_ENCODE();
	}
```

---

## OP_GroupFollow2 (0x6c16)
- **Direction:** outgoing
- **Structure:** GroupFollow_Struct

**Structure Definition:**
```cpp
struct GroupFollow_Struct { // Underfoot Follow Struct
/*0000*/	char	name1[64];	// inviter
/*0064*/	char	name2[64];	// invitee
/*0128*/	uint32	unknown0128;	// Seen 0
/*0132*/	uint32	unknown0132;	// Group ID or member level?
/*0136*/	uint32	unknown0136;	// Maybe Voice Chat Channel or Group ID?
/*0140*/	uint32	unknown0140;	// Seen 0
/*0144*/	uint32	unknown0144;	// Seen 0
/*0148*/	uint32	unknown0148;
/*0152*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_GroupFollow2)
	{
		ENCODE_LENGTH_EXACT(GroupGeneric_Struct);
		SETUP_DIRECT_ENCODE(GroupGeneric_Struct, structs::GroupFollow_Struct);

		memcpy(eq->name1, emu->name1, sizeof(eq->name1));
		memcpy(eq->name2, emu->name2, sizeof(eq->name2));

		FINISH_ENCODE();
	}
```

---

## OP_GroupInvite (0x4f60)
- **Direction:** outgoing
- **Structure:** GroupInvite_Struct

**Structure Definition:**
```cpp
struct GroupInvite_Struct {
/*0000*/	char	invitee_name[64];
/*0064*/	char	inviter_name[64];
/*0128*/	uint32	unknown0128;
/*0132*/	uint32	unknown0132;
/*0136*/	uint32	unknown0136;
/*0140*/	uint32	unknown0140;
/*0144*/	uint32	unknown0144;
/*0148*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_GroupInvite)
	{
		ENCODE_LENGTH_EXACT(GroupGeneric_Struct);
		SETUP_DIRECT_ENCODE(GroupGeneric_Struct, structs::GroupInvite_Struct);

		memcpy(eq->invitee_name, emu->name1, sizeof(eq->invitee_name));
		memcpy(eq->inviter_name, emu->name2, sizeof(eq->inviter_name));

		FINISH_ENCODE();
	}
```

---

## OP_GroupUpdate (0x5331)
- **Direction:** outgoing
- **Structure:** GroupGeneric_Struct

**Structure Definition:**
```cpp
struct GroupGeneric_Struct {
/*0000*/	char	name1[64];
/*0064*/	char	name2[64];
/*0128*/	uint32	unknown0128;
/*0132*/	uint32	unknown0132;
/*0136*/	uint32	unknown0136;
/*0140*/	uint32	unknown0140;
/*0144*/	uint32	unknown0144;
/*0148*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_GroupUpdate)
	{
		//Log.LogDebugType(Logs::General, Logs::Netcode, "[ERROR] OP_GroupUpdate");
		EQApplicationPacket *in = *p;
		GroupJoin_Struct *gjs = (GroupJoin_Struct*)in->pBuffer;

		//Log.LogDebugType(Logs::General, Logs::Netcode, "[ERROR] Received outgoing OP_GroupUpdate with action code %i", gjs->action);
		if ((gjs->action == groupActLeave) || (gjs->action == groupActDisband))
		{
			if ((gjs->action == groupActDisband) || !strcmp(gjs->yourname, gjs->membername))
			{
				//Log.LogDebugType(Logs::General, Logs::Netcode, "[ERROR] Group Leave, yourname = %s, member_name = %s", gjs->yourname, gjs->member_name);

				auto outapp =
				    new EQApplicationPacket(OP_GroupDisbandYou, sizeof(structs::GroupGeneric_Struct));

				structs::GroupGeneric_Struct *ggs = (structs::GroupGeneric_Struct*)outapp->pBuffer;
				memcpy(ggs->name1, gjs->yourname, sizeof(ggs->name1));
				memcpy(ggs->name2, gjs->membername, sizeof(ggs->name1));
				dest->FastQueuePacket(&outapp);

				// Make an empty GLAA packet to clear out their useable GLAAs
				//
				outapp = new EQApplicationPacket(OP_GroupLeadershipAAUpdate, sizeof(GroupLeadershipAAUpdate_Struct));

				dest->FastQueuePacket(&outapp);

				delete in;
				return;
			}
			//if(gjs->action == groupActLeave)
			//	Log.LogDebugType(Logs::General, Logs::Netcode, "[ERROR] Group Leave, yourname = %s, member_name = %s", gjs->yourname, gjs->member_name);

			auto outapp =
			    new EQApplicationPacket(OP_GroupDisbandOther, sizeof(structs::GroupGeneric_Struct));

			structs::GroupGeneric_Struct *ggs = (structs::GroupGeneric_Struct*)outapp->pBuffer;
			memcpy(ggs->name1, gjs->yourname, sizeof(ggs->name1));
			memcpy(ggs->name2, gjs->membername, sizeof(ggs->name2));
			//Log.Hex(Logs::Netcode, outapp->pBuffer, outapp->size);
			dest->FastQueuePacket(&outapp);

			delete in;
			return;
		}

		if (in->size == sizeof(GroupUpdate2_Struct))
		{
			// Group Update2
			//Log.LogDebugType(Logs::General, Logs::Netcode, "[ERROR] Struct is GroupUpdate2");

			unsigned char *__emu_buffer = in->pBuffer;
			GroupUpdate2_Struct *gu2 = (GroupUpdate2_Struct*)__emu_buffer;

			//Log.LogDebugType(Logs::General, Logs::Netcode, "[ERROR] Yourname is %s", gu2->yourname);

			int MemberCount = 1;
			int PacketLength = 8 + strlen(gu2->leadersname) + 1 + 22 + strlen(gu2->yourname) + 1;

			for (int i = 0; i < 5; ++i)
			{
				//Log.LogDebugType(Logs::General, Logs::Netcode, "[ERROR] Membername[%i] is %s", i,  gu2->member_name[i]);
				if (gu2->membername[i][0] != '\0')
				{
					PacketLength += (22 + strlen(gu2->membername[i]) + 1);
					++MemberCount;
				}
			}

			//Log.LogDebugType(Logs::General, Logs::Netcode, "[ERROR] Leadername is %s", gu2->leadersname);

			auto outapp = new EQApplicationPacket(OP_GroupUpdateB, PacketLength);

			char *Buffer = (char *)outapp->pBuffer;

			// Header
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0);	// Think this should be SpawnID, but it doesn't seem to matter
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, MemberCount);
			VARSTRUCT_ENCODE_STRING(Buffer, gu2->leadersname);

			// Leader
			//

			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0);
			VARSTRUCT_ENCODE_STRING(Buffer, gu2->yourname);
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, 0);
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, 0);
			//VARSTRUCT_ENCODE_STRING(Buffer, "");
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, 0);	// This is a string
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0x46);	// Observed 0x41 and 0x46 here
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, 0);
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0);
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0);
			VARSTRUCT_ENCODE_TYPE(uint16, Buffer, 0);

			int MemberNumber = 1;

			for (int i = 0; i < 5; ++i)
			{
				if (gu2->membername[i][0] == '\0')
					continue;

				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, MemberNumber++);
				VARSTRUCT_ENCODE_STRING(Buffer, gu2->membername[i]);
				VARSTRUCT_ENCODE_TYPE(uint8, Buffer, 0);
				VARSTRUCT_ENCODE_TYPE(uint8, Buffer, 0);
				//VARSTRUCT_ENCODE_STRING(Buffer, "");
				VARSTRUCT_ENCODE_TYPE(uint8, Buffer, 0);	// This is a string
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0x41);	// Observed 0x41 and 0x46 here
				VARSTRUCT_ENCODE_TYPE(uint8, Buffer, 0);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0);	// Low byte is Main Assist Flag
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0);
				VARSTRUCT_ENCODE_TYPE(uint16, Buffer, 0);
			}

			//Log.Hex(Logs::Netcode, outapp->pBuffer, outapp->size);
			dest->FastQueuePacket(&outapp);

			outapp = new EQApplicationPacket(OP_GroupLeadershipAAUpdate, sizeof(GroupLeadershipAAUpdate_Struct));

			GroupLeadershipAAUpdate_Struct *GLAAus = (GroupLeadershipAAUpdate_Struct*)outapp->pBuffer;

			GLAAus->NPCMarkerID = gu2->NPCMarkerID;
			memcpy(&GLAAus->LeaderAAs, &gu2->leader_aas, sizeof(GLAAus->LeaderAAs));

			dest->FastQueuePacket(&outapp);
			delete in;
			return;
		}
		//Log.LogDebugType(Logs::General, Logs::Netcode, "[ERROR] Generic GroupUpdate, yourname = %s, member_name = %s", gjs->yourname, gjs->member_name);
		ENCODE_LENGTH_EXACT(GroupJoin_Struct);
		SETUP_DIRECT_ENCODE(GroupJoin_Struct, structs::GroupJoin_Struct);

		memcpy(eq->membername, emu->membername, sizeof(eq->membername));

		auto outapp =
		    new EQApplicationPacket(OP_GroupLeadershipAAUpdate, sizeof(GroupLeadershipAAUpdate_Struct));
		GroupLeadershipAAUpdate_Struct *GLAAus = (GroupLeadershipAAUpdate_Struct*)outapp->pBuffer;

		GLAAus->NPCMarkerID = emu->NPCMarkerID;

		memcpy(&GLAAus->LeaderAAs, &emu->leader_aas, sizeof(GLAAus->LeaderAAs));
		//Log.Hex(Logs::Netcode, __packet->pBuffer, __packet->size);

		FINISH_ENCODE();

		dest->FastQueuePacket(&outapp);
	}
```

---

## OP_GuildMemberList (0x51bc)
- **Direction:** outgoing
- **Structure:** GuildMembers_Struct

**Structure Definition:**
```cpp
struct GuildMembers_Struct {	//just for display purposes, this is not actually used in the message encoding.
	char	player_name[1];		//variable length.
	uint32	count;				//network byte order
	GuildMemberEntry_Struct member[0];
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_GuildMemberList)
	{
		//consume the packet
		EQApplicationPacket *in = *p;
		*p = nullptr;

		//store away the emu struct
		unsigned char *__emu_buffer = in->pBuffer;
		Internal_GuildMembers_Struct *emu = (Internal_GuildMembers_Struct *)in->pBuffer;

		//make a new EQ buffer.
		uint32 pnl = strlen(emu->player_name);
		uint32 length = sizeof(structs::GuildMembers_Struct) + pnl +
			emu->count*sizeof(structs::GuildMemberEntry_Struct)
			+ emu->name_length + emu->note_length;
		in->pBuffer = new uint8[length];
		in->size = length;
		//no memset since we fill every byte.

		uint8 *buffer;
		buffer = in->pBuffer;

		//easier way to setup GuildMembers_Struct
		//set prefix name
		strcpy((char *)buffer, emu->player_name);
		buffer += pnl;
		*buffer = '\0';
		buffer++;

		//add member count.
		*((uint32 *)buffer) = htonl(emu->count);
		buffer += sizeof(uint32);

		if (emu->count > 0) {
			Internal_GuildMemberEntry_Struct *emu_e = emu->member;
			const char *emu_name = (const char *)(__emu_buffer +
				sizeof(Internal_GuildMembers_Struct)+ //skip header
				emu->count * sizeof(Internal_GuildMemberEntry_Struct)	//skip static length member data
				);
			const char *emu_note = (emu_name +
				emu->name_length + //skip name contents
				emu->count	//skip string terminators
				);

			structs::GuildMemberEntry_Struct *e = (structs::GuildMemberEntry_Struct *) buffer;

			uint32 r;
			for (r = 0; r < emu->count; r++, emu_e++) {

				//the order we set things here must match the struct

				//nice helper macro
				/*#define SlideStructString(field, str) \
				strcpy(e->field, str.c_str()); \
				e = (GuildMemberEntry_Struct *) ( ((uint8 *)e) + str.length() )*/
#define SlideStructString(field, str) \
			{ \
				int sl = strlen(str); \
				memcpy(e->field, str, sl+1); \
				e = (structs::GuildMemberEntry_Struct *) ( ((uint8 *)e) + sl ); \
				str += sl + 1; \
			}
#define PutFieldN(field) e->field = htonl(emu_e->field)

				SlideStructString(name, emu_name);
				PutFieldN(level);
				PutFieldN(banker);
				PutFieldN(class_);
				//Translate older ranks to new values* /
				switch (emu_e->rank) {
					case GUILD_SENIOR_MEMBER:
					case GUILD_MEMBER:
					case GUILD_JUNIOR_MEMBER:
					case GUILD_INITIATE:
					case GUILD_RECRUIT: {
						emu_e->rank = GUILD_MEMBER_TI;
						break;
					}
					case GUILD_OFFICER:
					case GUILD_SENIOR_OFFICER: {
						emu_e->rank = GUILD_OFFICER_TI;
						break;
					}
					case GUILD_LEADER: {
						emu_e->rank = GUILD_LEADER_TI;
						break;
					}
					default: {
						emu_e->rank = GUILD_RANK_NONE_TI;
						break;
					}
				}
				PutFieldN(rank);
				PutFieldN(time_last_on);
				PutFieldN(tribute_enable);
				PutFieldN(total_tribute);
				PutFieldN(last_tribute);
				e->unknown_one = htonl(1);
				SlideStructString(public_note, emu_note);
				e->zoneinstance = 0;
				e->zone_id = htons(emu_e->zone_id);

#undef SlideStructString
#undef PutFieldN

				e++;
			}
		}

		delete[] __emu_buffer;
		dest->FastQueuePacket(&in, ack_req);
	}
```

---

## OP_GuildsList (0x5b0b)
- **Direction:** outgoing
- **Structure:** GuildsListMessaging_Struct

**Structure Definition:**
```cpp
Structure definition not found.
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_GuildsList)
	{
		EQApplicationPacket* in = *p;
		*p = nullptr;

		GuildsListMessaging_Struct   glms{};
		EQ::Util::MemoryStreamReader ss(reinterpret_cast<char *>(in->pBuffer), in->size);
		cereal::BinaryInputArchive   ar(ss);
		ar(glms);

		auto packet_size = 64 + 4 + glms.guild_detail.size() * 4 + glms.string_length;
		auto buffer      = new uchar[packet_size];
		auto buf_pos     = buffer;

		memset(buf_pos, 0, 64);
		buf_pos += 64;

		VARSTRUCT_ENCODE_TYPE(uint32, buf_pos, glms.no_of_guilds);

		for (auto const& g : glms.guild_detail) {
			if (g.guild_id < UF::constants::MAX_GUILD_ID) {
				VARSTRUCT_ENCODE_TYPE(uint32, buf_pos, g.guild_id);
				strn0cpy((char *) buf_pos, g.guild_name.c_str(), g.guild_name.length() + 1);
				buf_pos += g.guild_name.length() + 1;
			}
		}

		auto outapp = new EQApplicationPacket(OP_GuildsList);

		outapp->size    = packet_size;
		outapp->pBuffer = buffer;

		dest->FastQueuePacket(&outapp);
	}
```

---

## OP_GuildMemberAdd (0x7337)
- **Direction:** outgoing
- **Structure:** GuildMemberAdd_Struct

**Structure Definition:**
```cpp
struct GuildMemberAdd_Struct {
	/*000*/ uint32 guild_id;
	/*004*/ uint32 unknown04;
	/*008*/ uint32 unknown08;
	/*012*/ uint32 unknown12;
	/*016*/ uint32 level;
	/*020*/ uint32 class_;
	/*024*/ uint32 rank_;
	/*028*/ uint32 zone_id;
	/*032*/ uint32 last_on;
	/*036*/ char   player_name[64];
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_GuildMemberAdd)
	{
		ENCODE_LENGTH_EXACT(GuildMemberAdd_Struct)
		SETUP_DIRECT_ENCODE(GuildMemberAdd_Struct, structs::GuildMemberAdd_Struct)

		OUT(guild_id)
		OUT(level)
		OUT(class_)
		switch (emu->rank_) {
			case GUILD_SENIOR_MEMBER:
			case GUILD_MEMBER:
			case GUILD_JUNIOR_MEMBER:
			case GUILD_INITIATE:
			case GUILD_RECRUIT: {
				eq->rank_ = GUILD_MEMBER_TI;
				break;
			}
			case GUILD_OFFICER:
			case GUILD_SENIOR_OFFICER: {
				eq->rank_ = GUILD_OFFICER_TI;
				break;
			}
			case GUILD_LEADER: {
				eq->rank_ = GUILD_LEADER_TI;
				break;
			}
			default: {
				eq->rank_ = GUILD_RANK_NONE_TI;
				break;
			}
		}
		OUT(zone_id)
		OUT(last_on)
		OUT_str(player_name)

		FINISH_ENCODE()
	}
```

---

## OP_SendGuildTributes (0x45b3)
- **Direction:** outgoing
- **Structure:** GuildTributeAbility_Struct

**Structure Definition:**
```cpp
struct GuildTributeAbility_Struct {
	uint32	guild_id;
	TributeAbility_Struct ability;
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_SendGuildTributes)
	{
		ENCODE_LENGTH_ATLEAST(structs::GuildTributeAbility_Struct)
		SETUP_VAR_ENCODE(GuildTributeAbility_Struct)
		ALLOC_VAR_ENCODE(structs::GuildTributeAbility_Struct, sizeof(GuildTributeAbility_Struct) + strlen(emu->ability.name))

		eq->guild_id           = emu->guild_id;
		eq->ability.tribute_id = emu->ability.tribute_id;
		eq->ability.tier_count = emu->ability.tier_count;
		strncpy(eq->ability.name, emu->ability.name, strlen(emu->ability.name));
		for (int i = 0; i < ntohl(emu->ability.tier_count); i++) {
			eq->ability.tiers[i].cost            = emu->ability.tiers[i].cost;
			eq->ability.tiers[i].level           = emu->ability.tiers[i].level;
			eq->ability.tiers[i].tribute_item_id = emu->ability.tiers[i].tribute_item_id;
		}
		FINISH_ENCODE()
	}
```

---

## OP_GuildMemberRankAltBanker (0x4ffe)
- **Direction:** outgoing
- **Structure:** GuildMemberRank_Struct

**Structure Definition:**
```cpp
struct GuildMemberRank_Struct {
	/*000*/ uint32 guild_id;
	/*004*/ uint32 unknown_004;
	/*008*/ uint32 rank_;
	/*012*/ char   player_name[64];
	/*076*/ uint32 alt_banker; //Banker/Alt bit 00 - none 10 - Alt 11 - Alt and Banker 01 - Banker.  Banker not functional for RoF2+
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_GuildMemberRankAltBanker)
	{
		ENCODE_LENGTH_EXACT(GuildMemberRank_Struct)
		SETUP_DIRECT_ENCODE(GuildMemberRank_Struct, structs::GuildMemberRank_Struct)

		OUT(guild_id)
		OUT(alt_banker)
		OUT_str(player_name)

		switch (emu->rank_) {
			case GUILD_SENIOR_MEMBER:
			case GUILD_MEMBER:
			case GUILD_JUNIOR_MEMBER:
			case GUILD_INITIATE:
			case GUILD_RECRUIT: {
				eq->rank_ = GUILD_MEMBER_TI;
				break;
			}
			case GUILD_OFFICER:
			case GUILD_SENIOR_OFFICER: {
				eq->rank_ = GUILD_OFFICER_TI;
				break;
			}
			case GUILD_LEADER: {
				eq->rank_ = GUILD_LEADER_TI;
				break;
			}
			default: {
				eq->rank_ = GUILD_RANK_NONE_TI;
				break;
			}
		}
		FINISH_ENCODE()
	}
```

---

## OP_GuildTributeDonateItem (0x3683)
- **Direction:** outgoing
- **Structure:** GuildTributeDonateItemReply_Struct

**Structure Definition:**
```cpp
struct GuildTributeDonateItemReply_Struct {
/*000*/ uint32	slot;
/*004*/ uint32	quantity;
/*008*/ uint32	unknown8;
/*012*/	uint32	favor;
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_GuildTributeDonateItem)
	{
		SETUP_DIRECT_ENCODE(GuildTributeDonateItemReply_Struct, structs::GuildTributeDonateItemReply_Struct);

		Log(Logs::Detail, Logs::Netcode, "UF::ENCODE(OP_GuildTributeDonateItem)");

		OUT(quantity)
		OUT(favor)
		eq->unknown8 = 0;
		eq->slot     = ServerToUFSlot(emu->slot);

		FINISH_ENCODE()
	}
```

---

## OP_Illusion (0x231f)
- **Direction:** outgoing
- **Structure:** Illusion_Struct

**Structure Definition:**
```cpp
struct Illusion_Struct {  //size: 256
/*000*/	uint32	spawnid;
/*004*/	char	charname[64];	//
/*068*/	uint16	race;			//
/*070*/	char	unknown006[2];	// Weird green name
/*072*/	uint8	gender;
/*073*/	uint8	texture;
/*074*/	uint8	unknown074;		//
/*075*/	uint8	unknown075;		//
/*076*/	uint8	helmtexture;	//
/*077*/	uint8	unknown077;		//
/*078*/	uint8	unknown078;		//
/*079*/	uint8	unknown079;		//
/*080*/	uint32	face;			//
/*084*/	uint8	hairstyle;		// Some Races don't change Hair Style Properly in SoF
/*085*/	uint8	haircolor;		//
/*086*/	uint8	beard;			//
/*087*/	uint8	beardcolor;		//
/*088*/ float	size;			//
/*092*/	uint8	unknown092[148];
/*240*/ uint32	unknown240;		// Removes armor?
/*244*/ uint32	drakkin_heritage;	//
/*248*/ uint32	drakkin_tattoo;		//
/*252*/ uint32	drakkin_details;	//
/*256*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_Illusion)
	{
		ENCODE_LENGTH_EXACT(Illusion_Struct);
		SETUP_DIRECT_ENCODE(Illusion_Struct, structs::Illusion_Struct);

		OUT(spawnid);
		OUT_str(charname);
		OUT(race);
		OUT(unknown006[0]);
		OUT(unknown006[1]);
		OUT(gender);
		OUT(texture);
		OUT(helmtexture);
		OUT(face);
		OUT(hairstyle);
		OUT(haircolor);
		OUT(beard);
		OUT(beardcolor);
		OUT(size);
		OUT(drakkin_heritage);
		OUT(drakkin_tattoo);
		OUT(drakkin_details);

		FINISH_ENCODE();
	}
```

---

## OP_InspectBuffs (0x105b)
- **Direction:** outgoing
- **Structure:** InspectBuffs_Struct

**Structure Definition:**
```cpp
struct InspectBuffs_Struct {
/*000*/ uint32 spell_id[BUFF_COUNT];
/*120*/ int32 tics_remaining[BUFF_COUNT];
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_InspectBuffs)
	{
		ENCODE_LENGTH_EXACT(InspectBuffs_Struct);
		SETUP_DIRECT_ENCODE(InspectBuffs_Struct, structs::InspectBuffs_Struct);

		// we go over the internal 25 instead of the packet's since no entry is 0, which it will be already
		for (int i = 0; i < BUFF_COUNT; i++) {
			OUT(spell_id[i]);
			OUT(tics_remaining[i]);
		}

		FINISH_ENCODE();
	}
```

---

## OP_InspectRequest (0x7c94)
- **Direction:** outgoing
- **Structure:** Inspect_Struct

**Structure Definition:**
```cpp
struct Inspect_Struct {
	uint32 TargetID;
	uint32 PlayerID;
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_InspectRequest)
	{
		ENCODE_LENGTH_EXACT(Inspect_Struct);
		SETUP_DIRECT_ENCODE(Inspect_Struct, structs::Inspect_Struct);

		OUT(TargetID);
		OUT(PlayerID);

		FINISH_ENCODE();
	}
```

---

## OP_ItemLinkResponse (0x695c)
- **Direction:** outgoing
- **Structure:** (unknown)

**Structure Definition:**
```cpp
Structure definition not found.
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_ItemLinkResponse) { ENCODE_FORWARD(OP_ItemPacket); }
```

---

## OP_ItemPacket (0x7b6e)
- **Direction:** outgoing
- **Structure:** InternalSerializedItem_Struct

**Structure Definition:**
```cpp
Structure definition not found.
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_ItemPacket)
	{
		//consume the packet
		EQApplicationPacket* in = *p;
		*p = nullptr;

		//store away the emu struct
		uchar* __emu_buffer = in->pBuffer;

		EQ::InternalSerializedItem_Struct* int_struct = (EQ::InternalSerializedItem_Struct*)(&__emu_buffer[4]);

		EQ::OutBuffer ob;
		EQ::OutBuffer::pos_type last_pos = ob.tellp();

		ob.write((const char*)__emu_buffer, 4);

		SerializeItem(ob, (const EQ::ItemInstance*)int_struct->inst, int_struct->slot_id, 0);
		if (ob.tellp() == last_pos) {
			LogNetcode("UF::ENCODE(OP_ItemPacket) Serialization failed on item slot [{}]", int_struct->slot_id);
			delete in;
			return;
		}

		in->size = ob.size();
		in->pBuffer = ob.detach();

		delete[] __emu_buffer;

		dest->FastQueuePacket(&in, ack_req);
	}
```

---

## OP_ItemVerifyReply (0x21c7)
- **Direction:** outgoing
- **Structure:** ItemVerifyReply_Struct

**Structure Definition:**
```cpp
struct ItemVerifyReply_Struct {
/*000*/	int32	slot;		// Slot being Right Clicked
/*004*/	uint32	spell;		// Spell ID to cast if different than item effect
/*008*/	uint32	target;		// Target Entity ID
/*012*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_ItemVerifyReply)
	{
		ENCODE_LENGTH_EXACT(ItemVerifyReply_Struct);
		SETUP_DIRECT_ENCODE(ItemVerifyReply_Struct, structs::ItemVerifyReply_Struct);

		eq->slot = ServerToUFSlot(emu->slot);
		OUT(spell);
		OUT(target);

		FINISH_ENCODE();
	}
```

---

## OP_LeadershipExpUpdate (0x074f)
- **Direction:** outgoing
- **Structure:** LeadershipExpUpdate_Struct

**Structure Definition:**
```cpp
struct LeadershipExpUpdate_Struct {
/*00*/	double	group_leadership_exp;
/*08*/	uint32	group_leadership_points;
/*12*/	uint32	Unknown12;
/*16*/	double	raid_leadership_exp;
/*24*/	uint32	raid_leadership_points;
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_LeadershipExpUpdate)
	{
		SETUP_DIRECT_ENCODE(LeadershipExpUpdate_Struct, structs::LeadershipExpUpdate_Struct);

		OUT(group_leadership_exp);
		OUT(group_leadership_points);
		OUT(raid_leadership_exp);
		OUT(raid_leadership_points);

		FINISH_ENCODE();
	}
```

---

## OP_LogServer (0x6f79)
- **Direction:** outgoing
- **Structure:** LogServer_Struct

**Structure Definition:**
```cpp
struct LogServer_Struct {
// Op_Code OP_LOGSERVER
/*000*/	uint32	unknown000;
/*004*/	uint8	enable_pvp;
/*005*/	uint8	unknown005;
/*006*/	uint8	unknown006;
/*007*/	uint8	unknown007;
/*008*/	uint8	enable_FV;
/*009*/	uint8	unknown009;
/*010*/	uint8	unknown010;
/*011*/	uint8	unknown011;
/*012*/	uint32	unknown012;	// htonl(1) on live
/*016*/	uint32	unknown016;	// htonl(1) on live
/*020*/	uint8	unknown020[12];
/*032*/ uint32	unknown032;
/*036*/	char	worldshortname[32];
/*068*/	uint8	unknown064[32];
/*100*/	char	unknown096[16];	// 'pacman' on live
/*116*/	char	unknown112[16];	// '64.37,148,36' on live
/*132*/	uint8	unknown128[48];
/*180*/	uint32	unknown176;	// htonl(0x00002695)
/*184*/	char	unknown180[80];	// 'eqdataexceptions@mail.station.sony.com' on live
/*264*/	uint8	unknown260;	// 0x01 on live
/*265*/	uint8	enablevoicemacros;
/*266*/	uint8	enablemail;
/*267*/	uint8	unknown263[41];
/*308*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_LogServer)
	{
		ENCODE_LENGTH_EXACT(LogServer_Struct);
		SETUP_DIRECT_ENCODE(LogServer_Struct, structs::LogServer_Struct);

		strcpy(eq->worldshortname, emu->worldshortname);

		OUT(enablevoicemacros);
		OUT(enablemail);
		OUT(enable_pvp);
		OUT(enable_FV);

		eq->unknown016 = 1;
		eq->unknown020[0] = 1;

		// These next two need to be set like this for the Tutorial Button to work.
		eq->unknown263[0] = 0;
		eq->unknown263[2] = 1;
		eq->unknown263[4] = 1;
		eq->unknown263[5] = 1;
		eq->unknown263[6] = 1;
		eq->unknown263[9] = 8;
		eq->unknown263[19] = 0x80;
		eq->unknown263[20] = 0x3f;
		eq->unknown263[23] = 0x80;
		eq->unknown263[24] = 0x3f;
		eq->unknown263[33] = 1;

		FINISH_ENCODE();
	}
```

---

## OP_LootItem (0x5960)
- **Direction:** outgoing
- **Structure:** LootingItem_Struct

**Structure Definition:**
```cpp
struct LootingItem_Struct {
/*000*/	uint32	lootee;
/*004*/	uint32	looter;
/*008*/	uint32	slot_id;
/*012*/	int32	auto_loot;
/*016*/	uint32	unknown16;
/*020*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_LootItem)
	{
		ENCODE_LENGTH_EXACT(LootingItem_Struct);
		SETUP_DIRECT_ENCODE(LootingItem_Struct, structs::LootingItem_Struct);

		Log(Logs::Detail, Logs::Netcode, "UF::ENCODE(OP_LootItem)");

		OUT(lootee);
		OUT(looter);
		eq->slot_id = ServerToUFCorpseSlot(emu->slot_id);
		OUT(auto_loot);

		FINISH_ENCODE();
	}
```

---

## OP_MercenaryDataResponse (0x0eaa)
- **Direction:** outgoing
- **Structure:** MercenaryMerchantList_Struct

**Structure Definition:**
```cpp
struct MercenaryMerchantList_Struct {
/*0000*/	uint32	MercTypeCount;			// Number of Merc Types to follow
/*0004*/	uint32	MercTypes[1];			// Count varies, but hard set to 3 for now - From dbstr_us.txt - Apprentice (330000100), Journeyman (330000200), Master (330000300)
/*0016*/	uint32	MercCount;				// Number of MercenaryInfo_Struct to follow
/*0020*/	MercenaryListEntry_Struct Mercs[0];	// Data for individual mercenaries in the Merchant List
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_MercenaryDataResponse)
	{
		//consume the packet
		EQApplicationPacket *in = *p;
		*p = nullptr;

		//store away the emu struct
		unsigned char *__emu_buffer = in->pBuffer;
		MercenaryMerchantList_Struct *emu = (MercenaryMerchantList_Struct *)__emu_buffer;

		char *Buffer = (char *)in->pBuffer;

		int PacketSize = sizeof(structs::MercenaryMerchantList_Struct) - 4 + emu->MercTypeCount * 4;
		PacketSize += (sizeof(structs::MercenaryListEntry_Struct) - sizeof(structs::MercenaryStance_Struct)) * emu->MercCount;

		uint32 r;
		uint32 k;
		for (r = 0; r < emu->MercCount; r++)
		{
			PacketSize += sizeof(structs::MercenaryStance_Struct) * emu->Mercs[r].StanceCount;
		}

		auto outapp = new EQApplicationPacket(OP_MercenaryDataResponse, PacketSize);
		Buffer = (char *)outapp->pBuffer;

		VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->MercTypeCount);

		for (r = 0; r < emu->MercTypeCount; r++)
		{
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->MercGrades[r]);
		}

		VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->MercCount);

		for (r = 0; r < emu->MercCount; r++)
		{
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->Mercs[r].MercID);
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->Mercs[r].MercType);
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->Mercs[r].MercSubType);
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->Mercs[r].PurchaseCost);
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->Mercs[r].UpkeepCost);
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->Mercs[r].AltCurrencyCost);
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->Mercs[r].AltCurrencyUpkeep);
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->Mercs[r].AltCurrencyType);
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->Mercs[r].MercUnk01);
			VARSTRUCT_ENCODE_TYPE(int32, Buffer, emu->Mercs[r].TimeLeft);
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->Mercs[r].MerchantSlot);
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->Mercs[r].MercUnk02);
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->Mercs[r].StanceCount);
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->Mercs[r].MercUnk03);
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->Mercs[r].MercUnk04);
			for (k = 0; k < emu->Mercs[r].StanceCount; k++)
			{
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->Mercs[r].Stances[k].StanceIndex);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->Mercs[r].Stances[k].Stance);
			}
		}

		dest->FastQueuePacket(&outapp, ack_req);
		delete in;
	}
```

---

## OP_MercenaryDataUpdate (0x57f2)
- **Direction:** outgoing
- **Structure:** MercenaryDataUpdate_Struct

**Structure Definition:**
```cpp
struct MercenaryDataUpdate_Struct {
/*0000*/	int32	MercStatus;					// Seen 0 with merc and -1 with no merc hired
/*0004*/	uint32	MercCount;					// Seen 1 with 1 merc hired and 0 with no merc hired
/*0008*/	MercenaryData_Struct MercData[0];	// Data for individual mercenaries in the Merchant List
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_MercenaryDataUpdate)
	{
		//consume the packet
		EQApplicationPacket *in = *p;
		*p = nullptr;

		//store away the emu struct
		unsigned char *__emu_buffer = in->pBuffer;
		MercenaryDataUpdate_Struct *emu = (MercenaryDataUpdate_Struct *)__emu_buffer;

		char *Buffer = (char *)in->pBuffer;

		EQApplicationPacket *outapp;

		uint32 PacketSize = 0;

		// There are 2 different sized versions of this packet depending if a merc is hired or not
		if (emu->MercStatus >= 0)
		{
			PacketSize += sizeof(structs::MercenaryDataUpdate_Struct) + (sizeof(structs::MercenaryData_Struct) - sizeof(structs::MercenaryStance_Struct)) * emu->MercCount;

			uint32 r;
			uint32 k;
			for (r = 0; r < emu->MercCount; r++)
			{
				PacketSize += sizeof(structs::MercenaryStance_Struct) * emu->MercData[r].StanceCount;
			}

			outapp = new EQApplicationPacket(OP_MercenaryDataUpdate, PacketSize);
			Buffer = (char *)outapp->pBuffer;

			VARSTRUCT_ENCODE_TYPE(int32, Buffer, emu->MercStatus);
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->MercCount);

			for (r = 0; r < emu->MercCount; r++)
			{
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->MercData[r].MercID);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->MercData[r].MercType);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->MercData[r].MercSubType);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->MercData[r].PurchaseCost);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->MercData[r].UpkeepCost);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->MercData[r].AltCurrencyCost);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->MercData[r].AltCurrencyUpkeep);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->MercData[r].AltCurrencyType);
				VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->MercData[r].MercUnk01);
				VARSTRUCT_ENCODE_TYPE(int32, Buffer, emu->MercData[r].TimeLeft);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->MercData[r].MerchantSlot);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->MercData[r].MercUnk02);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->MercData[r].StanceCount);
				VARSTRUCT_ENCODE_TYPE(int32, Buffer, emu->MercData[r].MercUnk03);
				VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->MercData[r].MercUnk04);
				for (k = 0; k < emu->MercData[r].StanceCount; k++)
				{
					VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->MercData[r].Stances[k].StanceIndex);
					VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->MercData[r].Stances[k].Stance);
				}
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->MercData[r].MercUnk05);
			}
		}
		else
		{
			PacketSize += sizeof(structs::NoMercenaryHired_Struct);

			outapp = new EQApplicationPacket(OP_MercenaryDataUpdate, PacketSize);
			Buffer = (char *)outapp->pBuffer;

			VARSTRUCT_ENCODE_TYPE(int32, Buffer, emu->MercStatus);
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->MercCount);
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 1);
		}

		dest->FastQueuePacket(&outapp, ack_req);
		delete in;
	}
```

---

## OP_MoveItem (0x2641)
- **Direction:** outgoing
- **Structure:** MoveItem_Struct

**Structure Definition:**
```cpp
struct MoveItem_Struct
{
/*0000*/ uint32	from_slot;
/*0004*/ uint32	to_slot;
/*0008*/ uint32	number_in_stack;
/*0012*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_MoveItem)
	{
		ENCODE_LENGTH_EXACT(MoveItem_Struct);
		SETUP_DIRECT_ENCODE(MoveItem_Struct, structs::MoveItem_Struct);

		Log(Logs::Detail, Logs::Netcode, "UF::ENCODE(OP_MoveItem)");

		eq->from_slot = ServerToUFSlot(emu->from_slot);
		eq->to_slot = ServerToUFSlot(emu->to_slot);
		OUT(number_in_stack);

		FINISH_ENCODE();
	}
```

---

## OP_NewSpawn (0x429b)
- **Direction:** outgoing
- **Structure:** (unknown)

**Structure Definition:**
```cpp
Structure definition not found.
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_NewSpawn) { ENCODE_FORWARD(OP_ZoneSpawns); }
```

---

## OP_NewZone (0x43ac)
- **Direction:** outgoing
- **Structure:** NewZone_Struct

**Structure Definition:**
```cpp
struct NewZone_Struct {
/*0000*/	char	char_name[64];			// Character Name
/*0064*/	char	zone_short_name[32];	// Zone Short Name
/*0096*/	char    unknown0096[96];
/*0192*/	char	zone_long_name[278];	// Zone Long Name
/*0470*/	uint8	ztype;					// Zone type (usually FF)
/*0471*/	uint8	fog_red[4];				// Zone fog (red)
/*0475*/	uint8	fog_green[4];				// Zone fog (green)
/*0479*/	uint8	fog_blue[4];				// Zone fog (blue)
/*0483*/	uint8	unknown323;
/*0484*/	float	fog_minclip[4];
/*0500*/	float	fog_maxclip[4];
/*0516*/	float	gravity;
/*0520*/	uint8	time_type;
/*0521*/    uint8   rain_chance[4];
/*0525*/    uint8   rain_duration[4];
/*0529*/    uint8   snow_chance[4];
/*0533*/    uint8   snow_duration[4];
/*0537*/    uint8   unknown537[33];
/*0570*/	uint8	sky;					// Sky Type
/*0571*/	uint8	unknown571[13];			// ***Placeholder
/*0584*/	float	zone_exp_multiplier;	// Experience Multiplier
/*0588*/	float	safe_y;					// Zone Safe Y
/*0592*/	float	safe_x;					// Zone Safe X
/*0596*/	float	safe_z;					// Zone Safe Z
/*0600*/	float	min_z;					// Guessed - NEW - Seen 0
/*0604*/	float	max_z;					// Guessed
/*0608*/	float	underworld;				// Underworld, min z (Not Sure?)
/*0612*/	float	minclip;				// Minimum View Distance
/*0616*/	float	maxclip;				// Maximum View DIstance
/*0620*/	uint8	unknown620[84];		// ***Placeholder
/*0704*/	char	zone_short_name2[96];	//zone file name? excludes instance number which can be in previous version.
/*0800*/	int32	unknown800;	//seen -1
/*0804*/	char	unknown804[40]; //
/*0844*/	int32  unknown844;	//seen 600
/*0848*/	int32  unknown848;
/*0852*/	uint16 zone_id;
/*0854*/	uint16 zone_instance;
/*0856*/	uint32 scriptNPCReceivedanItem;
/*0860*/	uint32 bCheck;					// padded bool
/*0864*/	uint32 scriptIDSomething;
/*0868*/	uint32 underworld_teleport_index; // > 0 teleports w/ zone point index, invalid succors, -1 affects some collisions
/*0872*/	uint32 scriptIDSomething3;
/*0876*/	uint32 suspend_buffs;
/*0880*/	uint32 lava_damage;	//seen 50
/*0884*/	uint32 min_lava_damage;	//seen 10
/*0888*/	uint8  unknown888;	//seen 1
/*0889*/	uint8  unknown889;	//seen 0 (POK) or 1 (rujj)
/*0890*/	uint8  unknown890;	//seen 1
/*0891*/	uint8  unknown891;	//seen 0
/*0892*/	uint8  unknown892;	//seen 0
/*0893*/	uint8  unknown893;	//seen 0 - 00
/*0894*/	uint8  fall_damage;	// 0 = Fall Damage on, 1 = Fall Damage off
/*0895*/	uint8  unknown895;	//seen 0 - 00
/*0896*/	uint32 fast_regen_hp;	//seen 180
/*0900*/	uint32 fast_regen_mana;	//seen 180
/*0904*/	uint32 fast_regen_endurance;	//seen 180
/*0908*/	uint32 unknown908;	//seen 2
/*0912*/	uint32 unknown912;	//seen 2
/*0916*/	float  fog_density;	//Of about 10 or so zones tested, all but one have this set to 0.33 Blightfire had 0.16
/*0920*/	uint32 unknown920;	//seen 0
/*0924*/	uint32 unknown924;	//seen 0
/*0928*/	uint32 unknown928;	//seen 0
/*0932*/	uint8  unknown932[12];
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_NewZone)
	{
		SETUP_DIRECT_ENCODE(NewZone_Struct, structs::NewZone_Struct);

		OUT_str(char_name);
		OUT_str(zone_short_name);
		OUT_str(zone_long_name);
		OUT(ztype);
		int r;
		for (r = 0; r < 4; r++) {
			OUT(fog_red[r]);
			OUT(fog_green[r]);
			OUT(fog_blue[r]);
			OUT(fog_minclip[r]);
			OUT(fog_maxclip[r]);
		}
		OUT(gravity);
		OUT(time_type);
		for (r = 0; r < 4; r++) {
			OUT(rain_chance[r]);
		}
		for (r = 0; r < 4; r++) {
			OUT(rain_duration[r]);
		}
		for (r = 0; r < 4; r++) {
			OUT(snow_chance[r]);
		}
		for (r = 0; r < 4; r++) {
			OUT(snow_duration[r]);
		}
		for (r = 0; r < 32; r++) {
			eq->unknown537[r] = 0xFF;	//observed
		}
		OUT(sky);
		OUT(zone_exp_multiplier);
		OUT(safe_y);
		OUT(safe_x);
		OUT(safe_z);
		OUT(max_z);
		OUT(underworld);
		OUT(minclip);
		OUT(maxclip);
		OUT_str(zone_short_name2);
		OUT(zone_id);
		OUT(zone_instance);
		OUT(suspend_buffs);
		OUT(fast_regen_hp);
		OUT(fast_regen_mana);
		OUT(fast_regen_endurance);
		OUT(underworld_teleport_index);

		eq->fog_density = emu->fog_density;

		/*fill in some unknowns with observed values, hopefully it will help */
		eq->unknown800 = -1;
		eq->unknown844 = 600;
		OUT(lava_damage);
		OUT(min_lava_damage);
		eq->unknown888 = 1;
		eq->unknown889 = 0;
		eq->unknown890 = 1;
		eq->unknown891 = 0;
		eq->unknown892 = 0;
		eq->unknown893 = 0;
		eq->fall_damage = 0;	// 0 = Fall Damage on, 1 = Fall Damage off
		eq->unknown895 = 0;
		eq->unknown908 = 2;
		eq->unknown912 = 2;

		FINISH_ENCODE();
	}
```

---

## OP_OnLevelMessage (0x24cb)
- **Direction:** outgoing
- **Structure:** OnLevelMessage_Struct

**Structure Definition:**
```cpp
struct OnLevelMessage_Struct {
/*0000*/	char    Title[128];
/*0128*/	char    Text[4096];
/*4224*/	char	ButtonName0[25];	// If Buttons = 1, these two are the text for the left and right buttons respectively
/*4249*/	char	ButtonName1[25];
/*4274*/	uint8	Buttons;
/*4275*/	uint8	SoundControls;	// Something to do with audio controls
/*4276*/	uint32  Duration;
/*4280*/	uint32  PopupID;	// If none zero, a response packet with 00 00 00 00 <PopupID> is returned on clicking the left button
/*4284*/	uint32  NegativeID;	// If none zero, a response packet with 01 00 00 00 <NegativeID> is returned on clicking the right button
/*4288*/	uint32  Unknown4288;
/*4292*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_OnLevelMessage)
	{
		ENCODE_LENGTH_EXACT(OnLevelMessage_Struct);
		SETUP_DIRECT_ENCODE(OnLevelMessage_Struct, structs::OnLevelMessage_Struct);

		memcpy(eq->Title, emu->Title, sizeof(eq->Title));
		memcpy(eq->Text, emu->Text, sizeof(eq->Text));
		OUT(Buttons);
		OUT(SoundControls);
		OUT(Duration);
		OUT(PopupID);
		OUT(NegativeID);
		// These two field names are used if Buttons == 1.
		OUT_str(ButtonName0);
		OUT_str(ButtonName1);

		FINISH_ENCODE();
	}
```

---

## OP_PetBuffWindow (0x7b87)
- **Direction:** outgoing
- **Structure:** PetBuff_Struct

**Structure Definition:**
```cpp
Structure definition not found.
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_PetBuffWindow)
	{
		EQApplicationPacket *in = *p;
		*p = nullptr;

		unsigned char *__emu_buffer = in->pBuffer;

		PetBuff_Struct *emu = (PetBuff_Struct *)__emu_buffer;

		int PacketSize = 12 + (emu->buffcount * 17);

		in->size = PacketSize;
		in->pBuffer = new unsigned char[in->size];

		char *Buffer = (char *)in->pBuffer;

		VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->petid);
		VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0);
		VARSTRUCT_ENCODE_TYPE(uint8, Buffer, 1);
		VARSTRUCT_ENCODE_TYPE(uint16, Buffer, emu->buffcount);

		for (unsigned int i = 0; i < PET_BUFF_COUNT; ++i)
		{
			if (emu->spellid[i])
			{
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, i);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->spellid[i]);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->ticsremaining[i]);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0); // numhits
				VARSTRUCT_ENCODE_TYPE(uint8, Buffer, 0);	// This is a string. Name of the caster of the buff.
			}
		}
		VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->buffcount); /// I think this is actually some sort of type

		delete[] __emu_buffer;
		dest->FastQueuePacket(&in, ack_req);
	}
```

---

## OP_PlayerProfile (0x6022)
- **Direction:** outgoing
- **Structure:** PlayerProfile_Struct

**Structure Definition:**
```cpp
struct PlayerProfile_Struct
{
/*00000*/ uint32  checksum;				//
//BEGIN SUB-STRUCT used for shrouding stuff...
/*00004*/ uint32  gender;				// Player Gender - 0 Male, 1 Female
/*00008*/ uint32  race;					// Player race
/*00012*/ uint32  class_;				// Player class
/*00016*/ uint8  unknown00016[40];		// #### uint32  unknown00016;   in Titanium ####uint8[40]
/*00056*/ uint8   level;				// Level of player
/*00057*/ uint8   level1;				// Level of player (again?)
/*00058*/ uint8   unknown00058[2];		// ***Placeholder
/*00060*/ BindStruct binds[5];			// Bind points (primary is first)
/*00160*/ uint32  deity;				// deity
/*00164*/ uint32  intoxication;			// Alcohol level (in ticks till sober?)
/*00168*/ uint32  spellSlotRefresh[spells::SPELL_GEM_COUNT]; // Refresh time (millis) - 4 Octets Each
/*00208*/ uint8   unknown00208[6];		// Seen 00 00 00 00 00 00 00 00 00 00 00 00 02 01
/*00222*/ uint32  abilitySlotRefresh;
/*00226*/ uint8   haircolor;			// Player hair color
/*00227*/ uint8   beardcolor;			// Player beard color
/*00228*/ uint8   eyecolor1;			// Player left eye color
/*00229*/ uint8   eyecolor2;			// Player right eye color
/*00230*/ uint8   hairstyle;			// Player hair style
/*00231*/ uint8   beard;				// Player beard type
/*00232*/ uint8	  unknown00232[4];		// was 14
/*00236*/ TextureProfile equipment;
/*00344*/ uint8 unknown00344[168];		// Underfoot Shows [160]
/*00512*/ TintProfile item_tint;		// RR GG BB 00
/*00548*/ AA_Array  aa_array[MAX_PP_AA_ARRAY];	// [3600] AAs 12 bytes each
/*04148*/ uint32  points;				// Unspent Practice points - RELOCATED???
/*04152*/ uint32  mana;					// Current mana
/*04156*/ uint32  cur_hp;				// Current HP without +HP equipment
/*04160*/ uint32  STR;					// Strength - 6e 00 00 00 - 110
/*04164*/ uint32  STA;					// Stamina - 73 00 00 00 - 115
/*04168*/ uint32  CHA;					// Charisma - 37 00 00 00 - 55
/*04172*/ uint32  DEX;					// Dexterity - 50 00 00 00 - 80
/*04176*/ uint32  INT;					// Intelligence - 3c 00 00 00 - 60
/*04180*/ uint32  AGI;					// Agility - 5f 00 00 00 - 95
/*04184*/ uint32  WIS;					// Wisdom - 46 00 00 00 - 70
/*04188*/ uint8   unknown04188[28];		//
/*04216*/ uint8   face;					// Player face - Actually uint32?
/*04217*/ uint8   unknown04217[147];		// was [175]
/*04364*/ uint32   spell_book[spells::SPELLBOOK_SIZE];	// List of the Spells in spellbook 720 = 90 pages [2880] was [1920]
/*07244*/ uint32   mem_spells[spells::SPELL_GEM_COUNT]; // List of spells memorized
/*07284*/ uint8   unknown07284[20];		//#### uint8 unknown04396[32]; in Titanium ####[28]
/*07312*/ uint32  platinum;				// Platinum Pieces on player
/*07316*/ uint32  gold;					// Gold Pieces on player
/*07320*/ uint32  silver;				// Silver Pieces on player
/*07324*/ uint32  copper;				// Copper Pieces on player
/*07328*/ uint32  platinum_cursor;		// Platinum Pieces on cursor
/*07332*/ uint32  gold_cursor;			// Gold Pieces on cursor
/*07336*/ uint32  silver_cursor;		// Silver Pieces on cursor
/*07340*/ uint32  copper_cursor;		// Copper Pieces on cursor
/*07344*/ uint32  skills[MAX_PP_SKILL];	// [400] List of skills	// 100 dword buffer
/*07744*/ uint32  InnateSkills[MAX_PP_INNATE_SKILL];
/*07844*/ uint8   unknown07644[36];
/*07880*/ uint32  toxicity;				// Potion Toxicity (15=too toxic, each potion adds 3)
/*07884*/ uint32  thirst_level;			// Drink (ticks till next drink)
/*07888*/ uint32  hunger_level;			// Food (ticks till next eat)
/*07892*/ SpellBuff_Struct buffs[BUFF_COUNT];	// [2280] Buffs currently on the player (30 Max) - (Each Size 76)
/*10172*/ Disciplines_Struct  disciplines;	// [400] Known disciplines
/*10972*/ uint32  recastTimers[MAX_RECAST_TYPES]; // Timers (UNIX Time of last use)
/*11052*/ uint8   unknown11052[160];		// Some type of Timers
/*11212*/ uint32  endurance;			// Current endurance
/*11216*/ uint8   unknown11216[20];		// ?
/*11236*/ uint32  aapoints_spent;		// Number of spent AA points
/*11240*/ uint32  aapoints;				// Unspent AA points
/*11244*/ uint8 unknown11244[4];
/*11248*/ Bandolier_Struct bandoliers[profile::BANDOLIERS_SIZE]; // [6400] bandolier contents
/*17648*/ PotionBelt_Struct  potionbelt;	// [360] potion belt 72 extra octets by adding 1 more belt slot
/*18008*/ uint8 unknown18008[8];
/*18016*/ uint32 available_slots;
/*18020*/ uint8 unknown18020[80];		//
//END SUB-STRUCT used for shrouding.
/*18100*/ char    name[64];				// Name of player
/*18164*/ char    last_name[32];		// Last name of player
/*18196*/ uint8   unknown18196[8];  //#### Not In Titanium #### new to SoF[12]
/*18204*/ uint32   guild_id;            // guildid
/*18208*/ uint32  birthday;       // character birthday
/*18212*/ uint32  lastlogin;       // character last save time
/*18216*/ uint32  account_startdate;       // Date the Account was started - New Field for Underfoot***
/*18220*/ uint32  timePlayedMin;      // time character played
/*18224*/ uint8   pvp;                // 1=pvp, 0=not pvp
/*18225*/ uint8   anon;               // 2=roleplay, 1=anon, 0=not anon
/*18226*/ uint8   gm;                 // 0=no, 1=yes (guessing!)
/*18227*/ uint8    guildrank;        // 0=member, 1=officer, 2=guildleader -1=no guild
/*18228*/ uint32  guildbanker;
/*18232*/ uint8 unknown18232[4];  //was [8]
/*18236*/ uint32  exp;                // Current Experience
/*18240*/ uint8 unknown18240[8];
/*18248*/ uint32  timeentitledonaccount;
/*18252*/ uint8   languages[MAX_PP_LANGUAGE]; // List of languages
/*18277*/ uint8   unknown18277[7];    //#### uint8   unknown13109[4]; in Titanium ####[7]
/*18284*/ float     y;                  // Players y position (NOT positive about this switch)
/*18288*/ float     x;                  // Players x position
/*18292*/ float     z;                  // Players z position
/*18296*/ float     heading;            // Players heading
/*18300*/ uint8   unknown18300[4];    // ***Placeholder
/*18304*/ uint32  platinum_bank;      // Platinum Pieces in Bank
/*18308*/ uint32  gold_bank;          // Gold Pieces in Bank
/*18312*/ uint32  silver_bank;        // Silver Pieces in Bank
/*18316*/ uint32  copper_bank;        // Copper Pieces in Bank
/*18320*/ uint32  platinum_shared;    // Shared platinum pieces
/*18324*/ uint8 unknown18324[1036];    // was [716]
/*19360*/ uint32  expansions;         // Bitmask for expansions ff 7f 00 00 - SoD
/*19364*/ uint8 unknown19364[12];
/*19376*/ uint32  autosplit;          // 0 = off, 1 = on
/*19380*/ uint8 unknown19380[16];
/*19396*/ uint16  zone_id;             // see zones.h
/*19398*/ uint16  zoneInstance;       // Instance id
/*19400*/ char      groupMembers[MAX_GROUP_MEMBERS][64];// 384 all the members in group, including self
/*19784*/ char      groupLeader[64];    // Leader of the group ?
/*19848*/ uint8 unknown19848[540];  // was [348]
/*20388*/ uint32  entityid;
/*20392*/ uint32  leadAAActive;       // 0 = leader AA off, 1 = leader AA on
/*20396*/ uint8 unknown20396[4];
/*20400*/ int32  ldon_points_guk;    // Earned GUK points
/*20404*/ int32  ldon_points_mir;    // Earned MIR points
/*20408*/ int32  ldon_points_mmc;    // Earned MMC points
/*20412*/ int32  ldon_points_ruj;    // Earned RUJ points
/*20416*/ int32  ldon_points_tak;    // Earned TAK points
/*20420*/ int32  ldon_points_available;  // Available LDON points
/*20424*/ uint32  unknown20424[7];
/*20452*/ uint32  unknown20452;
/*20456*/ uint32  unknown20456;
/*20460*/ uint8 unknown20460[4];
/*20464*/ uint32  unknown20464[6];
/*20488*/ uint8 unknown20488[72]; // was [136]
/*20560*/ float  tribute_time_remaining;        // Time remaining on tribute (millisecs)
/*20564*/ uint32  career_tribute_points;      // Total favor points for this char
/*20568*/ uint32  unknown20546;        // *** Placeholder
/*20572*/ uint32  tribute_points;     // Current tribute points
/*20576*/ uint32  unknown20572;        // *** Placeholder
/*20580*/ uint32  tribute_active;      // 0 = off, 1=on
/*20584*/ Tribute_Struct tributes[MAX_PLAYER_TRIBUTES]; // [40] Current tribute loadout
/*20624*/ uint8 unknown20620[4];
/*20628*/ double group_leadership_exp;     // Current group lead exp points
/*20636*/ double raid_leadership_exp;      // Current raid lead AA exp points
/*20644*/ uint32  group_leadership_points; // Unspent group lead AA points
/*20648*/ uint32  raid_leadership_points;  // Unspent raid lead AA points
/*20652*/ LeadershipAA_Struct leader_abilities; // [128]Leader AA ranks 19332
/*20780*/ uint8 unknown20776[128];		// was [128]
/*20908*/ uint32  air_remaining;       // Air supply (seconds)
/*20912*/ uint32  PVPKills;
/*20916*/ uint32  PVPDeaths;
/*20920*/ uint32  PVPCurrentPoints;
/*20924*/ uint32  PVPCareerPoints;
/*20928*/ uint32  PVPBestKillStreak;
/*20932*/ uint32  PVPWorstDeathStreak;
/*20936*/ uint32  PVPCurrentKillStreak;
/*20940*/ PVPStatsEntry_Struct PVPLastKill;		// size 88
/*21028*/ PVPStatsEntry_Struct PVPLastDeath;	// size 88
/*21116*/ uint32  PVPNumberOfKillsInLast24Hours;
/*21120*/ PVPStatsEntry_Struct PVPRecentKills[50];	// size 4400 - 88 each
/*25520*/ uint32 expAA;               // Exp earned in current AA point
/*25524*/ uint8 unknown25524[40];
/*25564*/ uint32 currentRadCrystals;  // Current count of radiant crystals
/*25568*/ uint32 careerRadCrystals;   // Total count of radiant crystals ever
/*25572*/ uint32 currentEbonCrystals; // Current count of ebon crystals
/*25576*/ uint32 careerEbonCrystals;  // Total count of ebon crystals ever
/*25580*/ uint8  groupAutoconsent;    // 0=off, 1=on
/*25581*/ uint8  raidAutoconsent;     // 0=off, 1=on
/*25582*/ uint8  guildAutoconsent;    // 0=off, 1=on
/*25583*/ uint8  unknown25583;     // ***Placeholder (6/29/2005)
/*25584*/ uint32 level3;		// SoF looks at the level here to determine how many leadership AA you can bank.
/*25588*/ uint32 showhelm;            // 0=no, 1=yes
/*25592*/ uint32 RestTimer;
/*25596*/ uint8   unknown25596[1036]; // ***Placeholder (2/13/2007) was[1028]or[940]or[1380] - END of Struct
/*26632*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_PlayerProfile)
	{
		SETUP_DIRECT_ENCODE(PlayerProfile_Struct, structs::PlayerProfile_Struct);

		uint32 r;

		eq->available_slots = 0xffffffff;
		memset(eq->unknown07284, 0xff, sizeof(eq->unknown07284));

		//	OUT(checksum);
		OUT(gender);
		OUT(race);
		OUT(class_);
		//	OUT(unknown00016);
		OUT(level);
		eq->level1 = emu->level;
		//	OUT(unknown00022[2]);
		for (r = 0; r < 5; r++) {
			OUT(binds[r].zone_id);
			OUT(binds[r].x);
			OUT(binds[r].y);
			OUT(binds[r].z);
			OUT(binds[r].heading);
		}
		OUT(deity);
		OUT(intoxication);
		OUT_array(spellSlotRefresh, spells::SPELL_GEM_COUNT);
		OUT(abilitySlotRefresh);
		OUT(points); // Relocation Test
		//	OUT(unknown0166[4]);
		OUT(haircolor);
		OUT(beardcolor);
		OUT(eyecolor1);
		OUT(eyecolor2);
		OUT(hairstyle);
		OUT(beard);
		//	OUT(unknown00178[10]);
		for (r = EQ::textures::textureBegin; r < EQ::textures::materialCount; r++) {
			eq->equipment.Slot[r].Material = emu->item_material.Slot[r].Material;
			eq->equipment.Slot[r].Unknown1 = 0;
			eq->equipment.Slot[r].EliteMaterial = 0;
			//eq->colors[r].color = emu->colors[r].color;
		}
		for (r = 0; r < 7; r++) {
			OUT(item_tint.Slot[r].Color);
		}
		//	OUT(unknown00224[48]);
		//NOTE: new client supports 300 AAs, our internal rep/PP
		//only supports 240..
		for (r = 0; r < MAX_PP_AA_ARRAY; r++) {
			eq->aa_array[r].AA = emu->aa_array[r].AA;
			eq->aa_array[r].value = emu->aa_array[r].value;
			eq->aa_array[r].charges = emu->aa_array[r].charges;
		}

		//	OUT(unknown02220[4]);

		OUT(mana);
		OUT(cur_hp);
		OUT(STR);
		OUT(STA);
		OUT(CHA);
		OUT(AGI);
		OUT(INT);
		OUT(DEX);
		OUT(WIS);
		OUT(face);
		//	OUT(unknown02264[47]);

		if (spells::SPELLBOOK_SIZE <= EQ::spells::SPELLBOOK_SIZE) {
			for (uint32 r = 0; r < spells::SPELLBOOK_SIZE; r++) {
				if (emu->spell_book[r] <= spells::SPELL_ID_MAX)
					eq->spell_book[r] = emu->spell_book[r];
				else
					eq->spell_book[r] = 0xFFFFFFFFU;
			}
		}
		else {
			for (uint32 r = 0; r < EQ::spells::SPELLBOOK_SIZE; r++) {
				if (emu->spell_book[r] <= spells::SPELL_ID_MAX)
					eq->spell_book[r] = emu->spell_book[r];
				else
					eq->spell_book[r] = 0xFFFFFFFFU;
			}
			// invalidate the rest of the spellbook slots
			memset(&eq->spell_book[EQ::spells::SPELLBOOK_SIZE], 0xFF, (sizeof(uint32) * (spells::SPELLBOOK_SIZE - EQ::spells::SPELLBOOK_SIZE)));
		}

		//	OUT(unknown4184[128]);
		OUT_array(mem_spells, spells::SPELL_GEM_COUNT);
		//	OUT(unknown04396[32]);
		OUT(platinum);
		OUT(gold);
		OUT(silver);
		OUT(copper);
		OUT(platinum_cursor);
		OUT(gold_cursor);
		OUT(silver_cursor);
		OUT(copper_cursor);

		OUT_array(skills, structs::MAX_PP_SKILL);	// 1:1 direct copy (100 dword)
		OUT_array(InnateSkills, structs::MAX_PP_INNATE_SKILL);  // 1:1 direct copy (25 dword)

		//	OUT(unknown04760[236]);
		OUT(toxicity);
		OUT(thirst_level);
		OUT(hunger_level);
		//PS this needs to be figured out more; but it was 'good enough'
		for (r = 0; r < BUFF_COUNT; r++)
		{
			if (emu->buffs[r].spellid != 0xFFFF && emu->buffs[r].spellid != 0)
			{
				eq->buffs[r].bard_modifier = 1.0f + (emu->buffs[r].bard_modifier - 10) / 10.0f;
				eq->buffs[r].effect_type= 2;
				eq->buffs[r].player_id = 0x000717fd;
			}
			else
			{
				eq->buffs[r].effect_type = 0;
				eq->buffs[r].bard_modifier = 1.0f;
			}
			OUT(buffs[r].effect_type);
			OUT(buffs[r].level);
			OUT(buffs[r].unknown003);
			OUT(buffs[r].spellid);
			OUT(buffs[r].duration);
			OUT(buffs[r].num_hits);
			OUT(buffs[r].player_id);
		}
		for (r = 0; r < MAX_PP_DISCIPLINES; r++) {
			OUT(disciplines.values[r]);
		}
		OUT_array(recastTimers, structs::MAX_RECAST_TYPES);
		//	OUT(unknown08124[360]);
		OUT(endurance);
		OUT(aapoints_spent);
		OUT(aapoints);

		//	OUT(unknown06160[4]);

		// Copy bandoliers where server and client indices converge
		for (r = 0; r < EQ::profile::BANDOLIERS_SIZE && r < profile::BANDOLIERS_SIZE; ++r) {
			OUT_str(bandoliers[r].Name);
			for (uint32 k = 0; k < profile::BANDOLIER_ITEM_COUNT; ++k) { // Will need adjusting if 'server != client' is ever true
				OUT(bandoliers[r].Items[k].ID);
				OUT(bandoliers[r].Items[k].Icon);
				OUT_str(bandoliers[r].Items[k].Name);
			}
		}
		// Nullify bandoliers where server and client indices diverge, with a client bias
		for (r = EQ::profile::BANDOLIERS_SIZE; r < profile::BANDOLIERS_SIZE; ++r) {
			eq->bandoliers[r].Name[0] = '\0';
			for (uint32 k = 0; k < profile::BANDOLIER_ITEM_COUNT; ++k) { // Will need adjusting if 'server != client' is ever true
				eq->bandoliers[r].Items[k].ID = 0;
				eq->bandoliers[r].Items[k].Icon = 0;
				eq->bandoliers[r].Items[k].Name[0] = '\0';
			}
		}

		//	OUT(unknown07444[5120]);

		// Copy potion belt where server and client indices converge
		for (r = 0; r < EQ::profile::POTION_BELT_SIZE && r < profile::POTION_BELT_SIZE; ++r) {
			OUT(potionbelt.Items[r].ID);
			OUT(potionbelt.Items[r].Icon);
			OUT_str(potionbelt.Items[r].Name);
		}
		// Nullify potion belt where server and client indices diverge, with a client bias
		for (r = EQ::profile::POTION_BELT_SIZE; r < profile::POTION_BELT_SIZE; ++r) {
			eq->potionbelt.Items[r].ID = 0;
			eq->potionbelt.Items[r].Icon = 0;
			eq->potionbelt.Items[r].Name[0] = '\0';
		}

		//	OUT(unknown12852[8]);
		//	OUT(unknown12864[76]);

		OUT_str(name);
		OUT_str(last_name);
		OUT(guild_id);
		OUT(birthday);
		OUT(lastlogin);
		OUT(timePlayedMin);
		OUT(pvp);
		OUT(anon);
		OUT(gm);
		//Translate older ranks to new values* /
		switch (emu->guildrank) {
			case GUILD_SENIOR_MEMBER:
			case GUILD_MEMBER:
			case GUILD_JUNIOR_MEMBER:
			case GUILD_INITIATE:
			case GUILD_RECRUIT: {
				emu->guildrank = GUILD_MEMBER_TI;
				break;
			}
			case GUILD_OFFICER:
			case GUILD_SENIOR_OFFICER: {
				emu->guildrank = GUILD_OFFICER_TI;
				break;
			}
			case GUILD_LEADER: {
				emu->guildrank = GUILD_LEADER_TI;
				break;
			}
			default: {
				emu->guildrank = GUILD_RANK_NONE_TI;
				break;
			}
		}
		OUT(guildrank);
		OUT(guildbanker);
		//	OUT(unknown13054[12]);
		OUT(exp);
		//	OUT(unknown13072[8]);
		OUT(timeentitledonaccount);
		OUT_array(languages, structs::MAX_PP_LANGUAGE);
		//	OUT(unknown13109[7]);
		OUT(y); //reversed x and y
		OUT(x);
		OUT(z);
		OUT(heading);
		//	OUT(unknown13132[4]);
		OUT(platinum_bank);
		OUT(gold_bank);
		OUT(silver_bank);
		OUT(copper_bank);
		OUT(platinum_shared);
		//	OUT(unknown13156[84]);
		OUT(expansions);
		//eq->expansions = 0x1ffff;
		//	OUT(unknown13244[12]);
		OUT(autosplit);
		//	OUT(unknown13260[16]);
		OUT(zone_id);
		OUT(zoneInstance);
		for (r = 0; r < structs::MAX_GROUP_MEMBERS; r++) {
			OUT_str(groupMembers[r]);
		}
		strcpy(eq->groupLeader, emu->groupMembers[0]);
		//	OUT_str(groupLeader);
		//	OUT(unknown13728[660]);
		OUT(entityid);
		OUT(leadAAActive);
		//	OUT(unknown14392[4]);
		OUT(ldon_points_guk);
		OUT(ldon_points_mir);
		OUT(ldon_points_mmc);
		OUT(ldon_points_ruj);
		OUT(ldon_points_tak);
		OUT(ldon_points_available);
		//	OUT(unknown14420[132]);
		OUT(tribute_time_remaining);
		OUT(career_tribute_points);
		//	OUT(unknown7208);
		OUT(tribute_points);
		//	OUT(unknown7216);
		OUT(tribute_active);
		for (r = 0; r < structs::MAX_PLAYER_TRIBUTES; r++) {
			OUT(tributes[r].tribute);
			OUT(tributes[r].tier);
		}
		//	OUT(unknown14616[8]);
		OUT(group_leadership_exp);
		//	OUT(unknown14628);
		OUT(raid_leadership_exp);
		OUT(group_leadership_points);
		OUT(raid_leadership_points);
		OUT_array(leader_abilities.ranks, structs::MAX_LEADERSHIP_AA_ARRAY);
		//	OUT(unknown14772[128]);
		OUT(air_remaining);
		OUT(PVPKills);
		OUT(PVPDeaths);
		OUT(PVPCurrentPoints);
		OUT(PVPCareerPoints);
		OUT(PVPBestKillStreak);
		OUT(PVPWorstDeathStreak);
		OUT(PVPCurrentKillStreak);
		//	OUT(unknown17892[4580]);
		OUT(expAA);
		//	OUT(unknown19516[40]);
		OUT(currentRadCrystals);
		OUT(careerRadCrystals);
		OUT(currentEbonCrystals);
		OUT(careerEbonCrystals);
		OUT(groupAutoconsent);
		OUT(raidAutoconsent);
		OUT(guildAutoconsent);
		//	OUT(unknown19575[5]);
		eq->level3 = emu->level;
		eq->showhelm = emu->showhelm;
		OUT(RestTimer);
		//	OUT(unknown19584[4]);
		//	OUT(unknown19588);

		const uint8 bytes[] = {
			0xa3, 0x02, 0x00, 0x00, 0x95, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00,
			0x19, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
			0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x1F, 0x85, 0xEB, 0x3E, 0x33, 0x33, 0x33, 0x3F,
			0x04, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
		};

		memcpy(eq->unknown18020, bytes, sizeof(bytes));

		//set the checksum...
		CRC32::SetEQChecksum(__packet->pBuffer, sizeof(structs::PlayerProfile_Struct) - 4);

		FINISH_ENCODE();
	}
```

---

## OP_RaidJoin (0x0000)
- **Direction:** outgoing
- **Structure:** RaidGeneral_Struct

**Structure Definition:**
```cpp
struct RaidGeneral_Struct {
/*00*/	uint32		action;
/*04*/	char		player_name[64];
/*68*/	uint32		unknown68;
/*72*/	char		leader_name[64];
/*136*/	uint32		parameter;
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_RaidJoin)
	{
		EQApplicationPacket* inapp = *p;
		*p = nullptr;
		unsigned char* __emu_buffer = inapp->pBuffer;
		RaidCreate_Struct* emu = (RaidCreate_Struct*)__emu_buffer;

		auto outapp = new EQApplicationPacket(OP_RaidUpdate, sizeof(structs::RaidGeneral_Struct));
		structs::RaidGeneral_Struct* general = (structs::RaidGeneral_Struct*)outapp->pBuffer;

		general->action = raidCreate;
		general->parameter = RaidCommandAcceptInvite;
		strn0cpy(general->leader_name, emu->leader_name, sizeof(emu->leader_name));
		strn0cpy(general->player_name, emu->leader_name, sizeof(emu->leader_name));

		dest->FastQueuePacket(&outapp);

		safe_delete(inapp);

	}
```

---

## OP_RaidUpdate (0x4d8b)
- **Direction:** outgoing
- **Structure:** RaidAddMember_Struct

**Structure Definition:**
```cpp
struct RaidAddMember_Struct {
/*000*/ RaidGeneral_Struct raidGen; //param = (group num-1); 0xFFFFFFFF = no group
/*136*/ uint8 _class;
/*137*/	uint8 level;
/*138*/	uint8 isGroupLeader;
/*139*/	uint8 flags[5]; //no idea if these are needed...
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_RaidUpdate)
	{
		EQApplicationPacket* inapp = *p;
		*p = nullptr;
		unsigned char* __emu_buffer = inapp->pBuffer;
		RaidGeneral_Struct* raid_gen = (RaidGeneral_Struct*)__emu_buffer;

		switch (raid_gen->action)
		{
		case raidAdd:
		{
			RaidAddMember_Struct* emu = (RaidAddMember_Struct*)__emu_buffer;

			auto outapp = new EQApplicationPacket(OP_RaidUpdate, sizeof(structs::RaidAddMember_Struct));
			structs::RaidAddMember_Struct* eq = (structs::RaidAddMember_Struct*)outapp->pBuffer;

			OUT(raidGen.action);
			OUT(raidGen.parameter);
			OUT_str(raidGen.leader_name);
			OUT_str(raidGen.player_name);
			OUT(_class);
			OUT(level);
			OUT(isGroupLeader);
			OUT(flags[0]);
			OUT(flags[1]);
			OUT(flags[2]);
			OUT(flags[3]);
			OUT(flags[4]);

			dest->FastQueuePacket(&outapp);
			break;
		}
		case raidSetMotd:
		{
			RaidMOTD_Struct* emu = (RaidMOTD_Struct*)__emu_buffer;

			auto outapp = new EQApplicationPacket(OP_RaidUpdate, sizeof(structs::RaidMOTD_Struct));
			structs::RaidMOTD_Struct* eq = (structs::RaidMOTD_Struct*)outapp->pBuffer;

			OUT(general.action);
			OUT_str(general.player_name);
			OUT_str(general.leader_name);
			OUT_str(motd);

			dest->FastQueuePacket(&outapp);
			break;
		}
		case raidSetLeaderAbilities:
		case raidMakeLeader:
		{
			RaidLeadershipUpdate_Struct* emu = (RaidLeadershipUpdate_Struct*)__emu_buffer;

			auto outapp = new EQApplicationPacket(OP_RaidUpdate, sizeof(structs::RaidLeadershipUpdate_Struct));
			structs::RaidLeadershipUpdate_Struct* eq = (structs::RaidLeadershipUpdate_Struct*)outapp->pBuffer;

			OUT(action);
			OUT_str(player_name);
			OUT_str(leader_name);
			memcpy(&eq->raid, &emu->raid, sizeof(RaidLeadershipAA_Struct));

			dest->FastQueuePacket(&outapp);
			break;
		}
		case raidSetNote:
		{
			auto emu = (RaidNote_Struct*)__emu_buffer;

			auto outapp = new EQApplicationPacket(OP_RaidUpdate, sizeof(structs::RaidNote_Struct));
			auto eq = (structs::RaidNote_Struct*)outapp->pBuffer;

			OUT(general.action);
			OUT_str(general.leader_name);
			OUT_str(general.player_name);
			OUT_str(note);

			dest->FastQueuePacket(&outapp);
			break;
		}
		case raidNoRaid:
		{
			dest->QueuePacket(inapp);
			break;
		}
		default:
		{
			RaidGeneral_Struct* emu = (RaidGeneral_Struct*)__emu_buffer;

			auto outapp = new EQApplicationPacket(OP_RaidUpdate, sizeof(structs::RaidGeneral_Struct));
			structs::RaidGeneral_Struct* eq = (structs::RaidGeneral_Struct*)outapp->pBuffer;

			OUT(action);
			OUT(parameter);
			OUT_str(leader_name);
			OUT_str(player_name);

			dest->FastQueuePacket(&outapp);
			break;
		}
		}
		safe_delete(inapp);
	}
```

---

## OP_ReadBook (0x465e)
- **Direction:** outgoing
- **Structure:** BookRequest_Struct

**Structure Definition:**
```cpp
struct BookRequest_Struct {
/*0000*/ uint32 window;       // where to display the text (0xFFFFFFFF means new window).
/*0004*/ uint32 invslot;      // The inventory slot the book is in
/*0008*/ uint32 type;         // 0 = Scroll, 1 = Book, 2 = Item Info. Possibly others
/*0012*/ uint32 target_id;
/*0016*/ uint8 can_cast;
/*0017*/ uint8 can_scribe;
/*0018*/ char txtfile[8194];
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_ReadBook)
	{
		ENCODE_LENGTH_ATLEAST(BookText_Struct);
		SETUP_DIRECT_ENCODE(BookText_Struct, structs::BookRequest_Struct);

		if (emu->window == 0xFF)
			eq->window = 0xFFFFFFFF;
		else
			eq->window = emu->window;
		OUT(type);
		eq->invslot = ServerToUFSlot(emu->invslot);
		OUT(target_id);
		OUT(can_cast);
		OUT(can_scribe);
		strn0cpy(eq->txtfile, emu->booktext, sizeof(eq->txtfile));

		FINISH_ENCODE();
	}
```

---

## OP_RespondAA (0x1fbd)
- **Direction:** outgoing
- **Structure:** AATable_Struct

**Structure Definition:**
```cpp
struct AATable_Struct {
/*00*/ int32	aa_spent;	// Total AAs Spent
/*04*/ int32	aa_assigned;	// Assigned: field in the AA window.
/*08*/ int32	aa_spent3;	// Unknown. Same as aa_spent in observed packets.
/*12*/ int32	unknown012;
/*16*/ int32	unknown016;
/*20*/ int32	unknown020;
/*24*/ AA_Array aa_list[MAX_PP_AA_ARRAY];
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_RespondAA)
	{
		SETUP_DIRECT_ENCODE(AATable_Struct, structs::AATable_Struct);

		eq->aa_spent = emu->aa_spent;
		eq->aa_assigned = emu->aa_spent;
		eq->aa_spent3 = 0;
		eq->unknown012 = 0;
		eq->unknown016 = 0;
		eq->unknown020 = 0;

		for (uint32 i = 0; i < MAX_PP_AA_ARRAY; ++i)
		{
			eq->aa_list[i].AA = emu->aa_list[i].AA;
			eq->aa_list[i].value = emu->aa_list[i].value;
			eq->aa_list[i].charges = emu->aa_list[i].charges;
		}

		FINISH_ENCODE();
	}
```

---

## OP_SendAATable (0x6ef9)
- **Direction:** outgoing
- **Structure:** SendAA_Struct

**Structure Definition:**
```cpp
struct SendAA_Struct {
/*0000*/	uint32 id;
/*0004*/	uint8 unknown004;		// uint32 unknown004; set to 1.
/*0005*/	uint32 hotkey_sid;
/*0009*/	uint32 hotkey_sid2;
/*0013*/	uint32 title_sid;
/*0017*/	uint32 desc_sid;
/*0021*/	uint32 class_type;
/*0025*/	uint32 cost;
/*0029*/	uint32 seq;
/*0033*/	uint32 current_level; //1s, MQ2 calls this AARankRequired
/*0037*/	uint32 prereq_skill;		//is < 0, abs() is category #
/*0041*/	uint32 prereq_minpoints; //min points in the prereq
/*0045*/	uint32 type;
/*0049*/	uint32 spellid;
/*0053*/	uint32 spell_type;
/*0057*/	uint32 spell_refresh;
/*0061*/	uint32 classes;
/*0065*/	uint32 max_level;
/*0069*/	uint32 last_id;
/*0073*/	uint32 next_id;
/*0077*/	uint32 cost2;
/*0081*/	uint8 unknown81;
/*0082*/	uint8 grant_only; // VetAAs, progression, etc
/*0083*/	uint8 unknown83; // 1 for skill cap increase AAs, Mystical Attuning, and RNG attack inc, doesn't seem to matter though
/*0084*/	uint32 expendable_charges; // max charges of the AA
/*0088*/	uint32 aa_expansion;
/*0092*/	uint32 special_category;
/*0096*/	uint8 shroud;
/*0097*/	uint8 unknown97;
/*0098*/	uint8 reset_on_death; // timer is reset on death
/*0099*/	uint8 unknown99;
/*0100*/	uint32 total_abilities;
/*0104*/	AA_Ability abilities[0];
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_SendAATable)
	{
		EQApplicationPacket *inapp = *p;
		*p = nullptr;
		AARankInfo_Struct *emu = (AARankInfo_Struct*)inapp->pBuffer;

		auto outapp = new EQApplicationPacket(
		    OP_SendAATable, sizeof(structs::SendAA_Struct) + emu->total_effects * sizeof(structs::AA_Ability));
		structs::SendAA_Struct *eq = (structs::SendAA_Struct*)outapp->pBuffer;

		inapp->SetReadPosition(sizeof(AARankInfo_Struct));
		outapp->SetWritePosition(sizeof(structs::SendAA_Struct));

		eq->id = emu->id;
		eq->unknown004 = 1;
		eq->id = emu->id;
		eq->hotkey_sid = emu->upper_hotkey_sid;
		eq->hotkey_sid2 = emu->lower_hotkey_sid;
		eq->desc_sid = emu->desc_sid;
		eq->title_sid = emu->title_sid;
		eq->class_type = emu->level_req;
		eq->cost = emu->cost;
		eq->seq = emu->seq;
		eq->current_level = emu->current_level;
		eq->type = emu->type;
		eq->spellid = emu->spell;
		eq->spell_type = emu->spell_type;
		eq->spell_refresh = emu->spell_refresh;
		eq->classes = emu->classes;
		eq->max_level = emu->max_level;
		eq->last_id = emu->prev_id;
		eq->next_id = emu->next_id;
		eq->cost2 = emu->total_cost;
		eq->grant_only = emu->grant_only;
		eq->expendable_charges = emu->charges;
		eq->aa_expansion = emu->expansion;
		eq->special_category = emu->category;
		eq->total_abilities = emu->total_effects;

		for(auto i = 0; i < eq->total_abilities; ++i) {
			eq->abilities[i].skill_id = inapp->ReadUInt32();
			eq->abilities[i].base_value = inapp->ReadUInt32();
			eq->abilities[i].limit_value = inapp->ReadUInt32();
			eq->abilities[i].slot = inapp->ReadUInt32();
		}

		if(emu->total_prereqs > 0) {
			eq->prereq_skill = inapp->ReadUInt32();
			eq->prereq_minpoints = inapp->ReadUInt32();
		}

		dest->FastQueuePacket(&outapp);
		delete inapp;
	}
```

---

## OP_SendCharInfo (0x4200)
- **Direction:** outgoing
- **Structure:** CharacterSelect_Struct

**Structure Definition:**
```cpp
struct CharacterSelect_Struct
{
/*0000*/	uint32 CharCount;	//number of chars in this packet
/*0004*/	uint32 TotalChars;	//total number of chars allowed?
/*0008*/	CharacterSelectEntry_Struct Entries[0];
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_SendCharInfo)
	{
		ENCODE_LENGTH_ATLEAST(CharacterSelect_Struct);
		SETUP_VAR_ENCODE(CharacterSelect_Struct);

		// Zero-character count shunt
		if (emu->CharCount == 0) {
			ALLOC_VAR_ENCODE(structs::CharacterSelect_Struct, sizeof(structs::CharacterSelect_Struct));
			eq->CharCount = emu->CharCount;
			eq->TotalChars = emu->TotalChars;

			if (eq->TotalChars > constants::CHARACTER_CREATION_LIMIT)
				eq->TotalChars = constants::CHARACTER_CREATION_LIMIT;

			// Special Underfoot adjustment - field should really be 'AdditionalChars' or 'BonusChars'
			uint32 adjusted_total = eq->TotalChars - 8; // Yes, it rolls under for '< 8' - probably an int32 field
			eq->TotalChars = adjusted_total;

			FINISH_ENCODE();
			return;
		}

		unsigned char *emu_ptr = __emu_buffer;
		emu_ptr += sizeof(CharacterSelect_Struct);
		CharacterSelectEntry_Struct *emu_cse = (CharacterSelectEntry_Struct *)nullptr;

		size_t names_length = 0;
		size_t character_count = 0;
		for (; character_count < emu->CharCount && character_count < constants::CHARACTER_CREATION_LIMIT; ++character_count) {
			emu_cse = (CharacterSelectEntry_Struct *)emu_ptr;
			names_length += strlen(emu_cse->Name);
			emu_ptr += sizeof(CharacterSelectEntry_Struct);
		}

		size_t total_length = sizeof(structs::CharacterSelect_Struct)
			+ character_count * sizeof(structs::CharacterSelectEntry_Struct)
			+ names_length;

		ALLOC_VAR_ENCODE(structs::CharacterSelect_Struct, total_length);
		structs::CharacterSelectEntry_Struct *eq_cse = (structs::CharacterSelectEntry_Struct *)nullptr;

		eq->CharCount = character_count;
		eq->TotalChars = emu->TotalChars;

		if (eq->TotalChars > constants::CHARACTER_CREATION_LIMIT)
			eq->TotalChars = constants::CHARACTER_CREATION_LIMIT;

		// Special Underfoot adjustment - field should really be 'AdditionalChars' or 'BonusChars' in this client
		uint32 adjusted_total = eq->TotalChars - 8; // Yes, it rolls under for '< 8' - probably an int32 field
		eq->TotalChars = adjusted_total;

		emu_ptr = __emu_buffer;
		emu_ptr += sizeof(CharacterSelect_Struct);

		unsigned char *eq_ptr = __packet->pBuffer;
		eq_ptr += sizeof(structs::CharacterSelect_Struct);

		for (int counter = 0; counter < character_count; ++counter) {
			emu_cse = (CharacterSelectEntry_Struct *)emu_ptr;
			eq_cse = (structs::CharacterSelectEntry_Struct *)eq_ptr; // base address

			eq_cse->Level = emu_cse->Level;
			eq_cse->HairStyle = emu_cse->HairStyle;
			eq_cse->Gender = emu_cse->Gender;

			strcpy(eq_cse->Name, emu_cse->Name);
			eq_ptr += strlen(emu_cse->Name);
			eq_cse = (structs::CharacterSelectEntry_Struct *)eq_ptr; // offset address (base + name length offset)
			eq_cse->Name[0] = '\0'; // (offset)eq_cse->Name[0] = (base)eq_cse->Name[strlen(emu_cse->Name)]

			eq_cse->Beard = emu_cse->Beard;
			eq_cse->HairColor = emu_cse->HairColor;
			eq_cse->Face = emu_cse->Face;

			for (int equip_index = EQ::textures::textureBegin; equip_index < EQ::textures::materialCount; equip_index++) {
				eq_cse->Equip[equip_index].Material = emu_cse->Equip[equip_index].Material;
				eq_cse->Equip[equip_index].Unknown1 = emu_cse->Equip[equip_index].Unknown1;
				eq_cse->Equip[equip_index].EliteMaterial = emu_cse->Equip[equip_index].EliteModel;
				eq_cse->Equip[equip_index].Color = emu_cse->Equip[equip_index].Color;
			}

			eq_cse->PrimaryIDFile = emu_cse->PrimaryIDFile;
			eq_cse->SecondaryIDFile = emu_cse->SecondaryIDFile;
			eq_cse->Tutorial = emu_cse->Tutorial;
			eq_cse->Unknown15 = emu_cse->Unknown15;
			eq_cse->Deity = emu_cse->Deity;
			eq_cse->Zone = emu_cse->Zone;
			eq_cse->Unknown19 = emu_cse->Unknown19;
			eq_cse->Race = emu_cse->Race;
			eq_cse->GoHome = emu_cse->GoHome;
			eq_cse->Class = emu_cse->Class;
			eq_cse->EyeColor1 = emu_cse->EyeColor1;
			eq_cse->BeardColor = emu_cse->BeardColor;
			eq_cse->EyeColor2 = emu_cse->EyeColor2;
			eq_cse->DrakkinHeritage = emu_cse->DrakkinHeritage;
			eq_cse->DrakkinTattoo = emu_cse->DrakkinTattoo;
			eq_cse->DrakkinDetails = emu_cse->DrakkinDetails;

			emu_ptr += sizeof(CharacterSelectEntry_Struct);
			eq_ptr += sizeof(structs::CharacterSelectEntry_Struct);
		}

		FINISH_ENCODE();
	}
```

---

## OP_SendZonepoints (0x2370)
- **Direction:** outgoing
- **Structure:** ZonePoints

**Structure Definition:**
```cpp
struct ZonePoints {
/*0000*/	uint32	count;
/*0004*/	struct	ZonePoint_Entry zpe[0]; // Always add one extra to the end after all zonepoints
//*0xxx*/    uint8     unknown0xxx[24]; //New from SEQ
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_SendZonepoints)
	{
		SETUP_VAR_ENCODE(ZonePoints);
		ALLOC_VAR_ENCODE(structs::ZonePoints, sizeof(structs::ZonePoints) + sizeof(structs::ZonePoint_Entry) * (emu->count + 1));

		eq->count = emu->count;
		for (uint32 i = 0; i < emu->count; ++i)
		{
			eq->zpe[i].iterator = emu->zpe[i].iterator;
			eq->zpe[i].x = emu->zpe[i].x;
			eq->zpe[i].y = emu->zpe[i].y;
			eq->zpe[i].z = emu->zpe[i].z;
			eq->zpe[i].heading = emu->zpe[i].heading;
			eq->zpe[i].zoneid = emu->zpe[i].zoneid;
			eq->zpe[i].zoneinstance = emu->zpe[i].zoneinstance;
		}

		FINISH_ENCODE();
	}
```

---

## OP_SetGuildRank (0x4ffe)
- **Direction:** outgoing
- **Structure:** GuildSetRank_Struct

**Structure Definition:**
```cpp
struct GuildSetRank_Struct
{
	/*00*/	uint32	unknown00;
	/*04*/	uint32	unknown04;
	/*08*/	uint32	rank;
	/*72*/	char	member_name[64];
	/*76*/	uint32	banker;
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_SetGuildRank)
	{
		ENCODE_LENGTH_EXACT(GuildSetRank_Struct);
		SETUP_DIRECT_ENCODE(GuildSetRank_Struct, structs::GuildSetRank_Struct);

		eq->unknown00 = 0;
		eq->unknown04 = 0;

		switch (emu->rank) {
			case GUILD_SENIOR_MEMBER:
			case GUILD_MEMBER:
			case GUILD_JUNIOR_MEMBER:
			case GUILD_INITIATE:
			case GUILD_RECRUIT: {
				emu->rank = GUILD_MEMBER_TI;
				break;
			}
			case GUILD_OFFICER:
			case GUILD_SENIOR_OFFICER: {
				emu->rank = GUILD_OFFICER_TI;
				break;
			}
			case GUILD_LEADER: {
				emu->rank = GUILD_LEADER_TI;
				break;
			}
			default: {
				emu->rank = GUILD_RANK_NONE_TI;
				break;
			}
		}

		memcpy(eq->member_name, emu->member_name, sizeof(eq->member_name));
		OUT(banker);

		FINISH_ENCODE();
	}
```

---

## OP_ShopPlayerBuy (0x436a)
- **Direction:** outgoing
- **Structure:** Merchant_Sell_Struct

**Structure Definition:**
```cpp
struct Merchant_Sell_Struct {
/*000*/	uint32	npcid;			// Merchant NPC's entity id
/*004*/	uint32	playerid;		// Player's entity id
/*008*/	uint32	itemslot;
/*012*/	uint32	unknown12;
/*016*/	uint32	quantity;
/*020*/	uint32	Unknown020;
/*024*/	uint32	price;
/*028*/	uint32	pricehighorderbits;	// It appears the price is 64 bits in Underfoot+
/*032*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_ShopPlayerBuy)
	{
		ENCODE_LENGTH_EXACT(Merchant_Sell_Struct);
		SETUP_DIRECT_ENCODE(Merchant_Sell_Struct, structs::Merchant_Sell_Struct);

		OUT(npcid);
		OUT(playerid);
		OUT(itemslot);
		OUT(quantity);
		OUT(price);

		FINISH_ENCODE();
	}
```

---

## OP_ShopPlayerSell (0x0b27)
- **Direction:** outgoing
- **Structure:** Merchant_Purchase_Struct

**Structure Definition:**
```cpp
struct Merchant_Purchase_Struct {
/*000*/	uint32	npcid;			// Merchant NPC's entity id
/*004*/	uint32	itemslot;		// Player's entity id
/*008*/	uint32	quantity;
/*012*/	uint32	price;
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_ShopPlayerSell)
	{
		ENCODE_LENGTH_EXACT(Merchant_Purchase_Struct);
		SETUP_DIRECT_ENCODE(Merchant_Purchase_Struct, structs::Merchant_Purchase_Struct);

		OUT(npcid);
		eq->itemslot = ServerToUFSlot(emu->itemslot);
		OUT(quantity);
		OUT(price);

		FINISH_ENCODE();
	}
```

---

## OP_ShopRequest (0x442a)
- **Direction:** outgoing
- **Structure:** MerchantClick_Struct

**Structure Definition:**
```cpp
struct MerchantClick_Struct {
/*000*/ uint32	npc_id;			// Merchant NPC's entity id
/*004*/ uint32	player_id;
/*008*/ uint32	command;		//1=open, 0=cancel/close
/*012*/ float	rate;			//cost multiplier, dosent work anymore
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_ShopRequest)
	{
		ENCODE_LENGTH_EXACT(MerchantClick_Struct);
		SETUP_DIRECT_ENCODE(MerchantClick_Struct, structs::MerchantClick_Struct);

		OUT(npc_id);
		OUT(player_id);
		OUT(command);
		OUT(rate);

		FINISH_ENCODE();
	}
```

---

## OP_SomeItemPacketMaybe (0x2c27)
- **Direction:** outgoing
- **Structure:** Arrow_Struct

**Structure Definition:**
```cpp
struct Arrow_Struct
{
/*000*/	float	src_y;
/*004*/	float	src_x;
/*008*/	float	src_z;
/*012*/	uint8	unknown012[12];
/*024*/	float	velocity;		//4 is normal, 20 is quite fast
/*028*/	float	launch_angle;	//0-450ish, not sure the units, 140ish is straight
/*032*/	float	tilt;		//on the order of 125
/*036*/	uint8	unknown036[8];
/*044*/	float	arc;
/*048*/	uint32	source_id;
/*052*/	uint32	target_id;	//entity ID
/*056*/	uint32	item_id;
/*060*/	uint8	unknown060[10];
/*070*/	uint8	unknown070;
/*071*/	uint8	item_type;
/*072*/	uint8	skill;
/*073*/	uint8	unknown073[13];
/*086*/	char	model_name[30];
/*116*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_SomeItemPacketMaybe)
	{
		// This Opcode is not named very well. It is used for the animation of arrows leaving the player's bow
		// and flying to the target.
		//

		ENCODE_LENGTH_EXACT(Arrow_Struct);
		SETUP_DIRECT_ENCODE(Arrow_Struct, structs::Arrow_Struct);

		OUT(src_y);
		OUT(src_x);
		OUT(src_z);
		OUT(velocity);
		OUT(launch_angle);
		OUT(tilt);
		OUT(arc);
		OUT(source_id);
		OUT(target_id);
		OUT(item_id);

		eq->unknown070 = 135; // This needs to be set to something, else we get a 1HS animation instead of ranged.

		OUT(item_type);
		OUT(skill);

		strcpy(eq->model_name, emu->model_name);

		FINISH_ENCODE();
	}
```

---

## OP_SpawnAppearance (0x3e17)
- **Direction:** outgoing
- **Structure:** SpawnAppearance_Struct

**Structure Definition:**
```cpp
struct SpawnAppearance_Struct
{
/*0000*/ uint16 spawn_id;          // ID of the spawn
/*0002*/ uint16 type;              // Values associated with the type
/*0004*/ uint32 parameter;         // Type of data sent
/*0008*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_SpawnAppearance)
	{
		ENCODE_LENGTH_EXACT(SpawnAppearance_Struct);
		SETUP_DIRECT_ENCODE(SpawnAppearance_Struct, structs::SpawnAppearance_Struct);

		OUT(spawn_id);
		OUT(type);
		OUT(parameter);
		switch (emu->type) {
			case AppearanceType::GuildRank: {
				//Translate new ranks to old values* /
				switch (emu->parameter) {
					case GUILD_SENIOR_MEMBER:
					case GUILD_MEMBER:
					case GUILD_JUNIOR_MEMBER:
					case GUILD_INITIATE:
					case GUILD_RECRUIT: {
						eq->parameter = GUILD_MEMBER_TI;
						break;
					}
					case GUILD_OFFICER:
					case GUILD_SENIOR_OFFICER: {
						eq->parameter = GUILD_OFFICER_TI;
						break;
					}
					case GUILD_LEADER: {
						eq->parameter = GUILD_LEADER_TI;
						break;
					}
					default: {
						eq->parameter = GUILD_RANK_NONE_TI;
						break;
					}
				}
				break;
			}
			case AppearanceType::GuildShow: {
				FAIL_ENCODE();
				return;
			}
			default: {
				break;
			}
		}

		FINISH_ENCODE();
	}
```

---

## OP_SpawnDoor (0x6f2b)
- **Direction:** outgoing
- **Structure:** Door_Struct

**Structure Definition:**
```cpp
struct Door_Struct
{
/*0000*/ char    name[32];            // Filename of Door // Was 10char long before... added the 6 in the next unknown to it: Daeken M. BlackBlade
/*0032*/ float   yPos;               // y loc
/*0036*/ float   xPos;               // x loc
/*0040*/ float   zPos;               // z loc
/*0044*/ float	 heading;
/*0048*/ uint32   incline;	// rotates the whole door
/*0052*/ uint32   size;			// 100 is normal, smaller number = smaller model
/*0054*/ uint8    unknown0054[4]; // 00 00 00 00
/*0060*/ uint8   doorId;             // door's id #
/*0061*/ uint8   opentype;
/*0062*/ uint8  state_at_spawn;
/*0063*/ uint8  invert_state;	// if this is 1, the door is normally open
/*0064*/ uint32  door_param; // normally ff ff ff ff (-1)
/*0068*/ uint32	unknown0068; // 00 00 00 00
/*0072*/ uint32	unknown0072; // 00 00 00 00
/*0076*/ uint8	unknown0076; // seen 1 or 0
/*0077*/ uint8	unknown0077; // seen 1 (always?)
/*0078*/ uint8	unknown0078; // seen 0 (always?)
/*0079*/ uint8	unknown0079; // seen 1 (always?)
/*0080*/ uint8	unknown0080; // seen 0 (always?)
/*0081*/ uint8	unknown0081; // seen 1 or 0 or rarely 2C or 90 or ED or 2D or A1
/*0082*/ uint8  unknown0082; // seen 0 or rarely FF or FE or 10 or 5A or 82
/*0083*/ uint8  unknown0083; // seen 0 or rarely 02 or 7C
/*0084*/ uint8  unknown0084[8]; // mostly 0s, the last 3 bytes are something tho
/*0092*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_SpawnDoor)
	{
		SETUP_VAR_ENCODE(Door_Struct);
		int door_count = __packet->size / sizeof(Door_Struct);
		int total_length = door_count * sizeof(structs::Door_Struct);
		ALLOC_VAR_ENCODE(structs::Door_Struct, total_length);

		int r;
		for (r = 0; r < door_count; r++) {
			strcpy(eq[r].name, emu[r].name);
			eq[r].xPos = emu[r].xPos;
			eq[r].yPos = emu[r].yPos;
			eq[r].zPos = emu[r].zPos;
			eq[r].heading = emu[r].heading;
			eq[r].incline = emu[r].incline;
			eq[r].size = emu[r].size;
			eq[r].doorId = emu[r].doorId;
			eq[r].opentype = emu[r].opentype;
			eq[r].state_at_spawn = emu[r].state_at_spawn;
			eq[r].invert_state = emu[r].invert_state;
			eq[r].door_param = emu[r].door_param;
			eq[r].unknown0076 = 0;
			eq[r].unknown0077 = 1; // Both must be 1 to allow clicking doors
			eq[r].unknown0078 = 0;
			eq[r].unknown0079 = 1; // Both must be 1 to allow clicking doors
			eq[r].unknown0080 = 0;
			eq[r].unknown0081 = 0;
			eq[r].unknown0082 = 0;
		}

		FINISH_ENCODE();
	}
```

---

## OP_SpecialMesg (0x016c)
- **Direction:** outgoing
- **Structure:** (unknown)

**Structure Definition:**
```cpp
Structure definition not found.
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_SpecialMesg)
	{
		EQApplicationPacket *in = *p;
		*p = nullptr;

		SerializeBuffer buf(in->size);
		buf.WriteInt8(in->ReadUInt8()); // speak mode
		buf.WriteInt8(in->ReadUInt8()); // journal mode
		buf.WriteInt8(in->ReadUInt8()); // language
		buf.WriteInt32(in->ReadUInt32()); // message type
		buf.WriteInt32(in->ReadUInt32()); // target spawn id

		std::string name;
		in->ReadString(name); // NPC names max out at 63 chars

		buf.WriteString(name);

		buf.WriteInt32(in->ReadUInt32()); // loc
		buf.WriteInt32(in->ReadUInt32());
		buf.WriteInt32(in->ReadUInt32());

		std::string old_message;
		std::string new_message;

		in->ReadString(old_message);

		ServerToUFSayLink(new_message, old_message);

		buf.WriteString(new_message);

		auto outapp = new EQApplicationPacket(OP_SpecialMesg, buf);

		dest->FastQueuePacket(&outapp, ack_req);
		delete in;
	}
```

---

## OP_Stun (0x3d00)
- **Direction:** outgoing
- **Structure:** Stun_Struct

**Structure Definition:**
```cpp
struct Stun_Struct { // 8 bytes total
/*000*/	uint32	duration; // Duration of stun
/*004*/	uint8	unknown004; // seen 0
/*005*/	uint8	unknown005; // seen 163
/*006*/	uint8	unknown006; // seen 67
/*007*/	uint8	unknown007; // seen 0
/*008*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_Stun)
	{
		ENCODE_LENGTH_EXACT(Stun_Struct);
		SETUP_DIRECT_ENCODE(Stun_Struct, structs::Stun_Struct);

		OUT(duration);
		eq->unknown005 = 163;
		eq->unknown006 = 67;

		FINISH_ENCODE();
	}
```

---

## OP_TargetBuffs (0x3f24)
- **Direction:** outgoing
- **Structure:** (unknown)

**Structure Definition:**
```cpp
Structure definition not found.
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_TargetBuffs) { ENCODE_FORWARD(OP_BuffCreate); }
```

---

## OP_TaskDescription (0x156c)
- **Direction:** outgoing
- **Structure:** TaskDescriptionHeader_Struct

**Structure Definition:**
```cpp
struct TaskDescriptionHeader_Struct {
	uint32	SequenceNumber; // The order the tasks appear in the journal. 0 for first task, 1 for second, etc.
	uint32	TaskID;
	uint32	unknown2;
	uint32	unknown3;
	uint8	unknown4;
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_TaskDescription)
	{
		EQApplicationPacket *in = *p;
		*p = nullptr;

		unsigned char *__emu_buffer = in->pBuffer;

		char *InBuffer = (char *)in->pBuffer;
		char *block_start = InBuffer;

		InBuffer += sizeof(TaskDescriptionHeader_Struct);
		uint32 title_size = strlen(InBuffer) + 1;
		InBuffer += title_size;
		InBuffer += sizeof(TaskDescriptionData1_Struct);
		uint32 description_size = strlen(InBuffer) + 1;
		InBuffer += description_size;
		InBuffer += sizeof(TaskDescriptionData2_Struct);

		uint32 reward_size = strlen(InBuffer) + 1;
		InBuffer += reward_size;

		std::string old_message = InBuffer; // start item link string
		std::string new_message;
		ServerToUFSayLink(new_message, old_message);

		in->size = sizeof(TaskDescriptionHeader_Struct) + sizeof(TaskDescriptionData1_Struct)+
			sizeof(TaskDescriptionData2_Struct) + sizeof(TaskDescriptionTrailer_Struct)+
			title_size + description_size + reward_size + new_message.length() + 1;

		in->pBuffer = new unsigned char[in->size];

		char *OutBuffer = (char *)in->pBuffer;

		memcpy(OutBuffer, block_start, (InBuffer - block_start));
		OutBuffer += (InBuffer - block_start);

		VARSTRUCT_ENCODE_STRING(OutBuffer, new_message.c_str());

		InBuffer += strlen(InBuffer) + 1;

		memcpy(OutBuffer, InBuffer, sizeof(TaskDescriptionTrailer_Struct));

		delete[] __emu_buffer;
		dest->FastQueuePacket(&in, ack_req);
	}
```

---

## OP_Track (0x709d)
- **Direction:** outgoing
- **Structure:** Track_Struct

**Structure Definition:**
```cpp
struct Track_Struct {
	uint16 entityid;
	uint16 y;
	uint16 x;
	uint16 z;
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_Track)
	{
		EQApplicationPacket *in = *p;
		*p = nullptr;

		unsigned char *__emu_buffer = in->pBuffer;
		Track_Struct *emu = (Track_Struct *)__emu_buffer;

		int EntryCount = in->size / sizeof(Track_Struct);

		if (EntryCount == 0 || ((in->size % sizeof(Track_Struct))) != 0)
		{
			LogNetcode("[STRUCTS] Wrong size on outbound [{}]: Got [{}], expected multiple of [{}]", opcodes->EmuToName(in->GetOpcode()), in->size, sizeof(Track_Struct));
			delete in;
			return;
		}

		int PacketSize = 2;

		for (int i = 0; i < EntryCount; ++i, ++emu)
			PacketSize += (12 + strlen(emu->name));

		emu = (Track_Struct *)__emu_buffer;

		in->size = PacketSize;
		in->pBuffer = new unsigned char[in->size];

		char *Buffer = (char *)in->pBuffer;

		VARSTRUCT_ENCODE_TYPE(uint16, Buffer, EntryCount);

		for (int i = 0; i < EntryCount; ++i, ++emu)
		{
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->entityid);
			VARSTRUCT_ENCODE_TYPE(float, Buffer, emu->distance);
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->level);
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->is_npc);
			VARSTRUCT_ENCODE_STRING(Buffer, emu->name);
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->is_merc);
		}

		delete[] __emu_buffer;
		dest->FastQueuePacket(&in, ack_req);
	}
```

---

## OP_Trader (0x0c08)
- **Direction:** outgoing
- **Structure:** Trader_ShowItems_Struct

**Structure Definition:**
```cpp
struct Trader_ShowItems_Struct{
	uint32 action;
	uint32 entity_id;
	uint32 unknown08[3];
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_Trader)
	{
		auto action = *(uint32 *) (*p)->pBuffer;

		switch (action) {
			case TraderOn: {
				ENCODE_LENGTH_EXACT(Trader_ShowItems_Struct);
				SETUP_DIRECT_ENCODE(Trader_ShowItems_Struct, structs::Trader_ShowItems_Struct);
				LogTrading(
					"Encode OP_Trader BeginTraderMode action <green>[{}]",
					action
				);

				eq->action = structs::UFBazaarTraderBuyerActions::BeginTraderMode;
				OUT(entity_id);

				FINISH_ENCODE();
				break;
			}
			case TraderOff: {
				ENCODE_LENGTH_EXACT(Trader_ShowItems_Struct);
				SETUP_DIRECT_ENCODE(Trader_ShowItems_Struct, structs::Trader_ShowItems_Struct);
				LogTrading(
					"Encode OP_Trader EndTraderMode action <green>[{}]",
					action
				);

				eq->action = structs::UFBazaarTraderBuyerActions::EndTraderMode;
				OUT(entity_id);

				FINISH_ENCODE();
				break;
			}
			case ListTraderItems: {
				ENCODE_LENGTH_EXACT(Trader_Struct);
				SETUP_DIRECT_ENCODE(Trader_Struct, structs::Trader_Struct);
				LogTrading(
					"Encode OP_Trader ListTraderItems action <green>[{}]",
					action
				);

				eq->action = structs::UFBazaarTraderBuyerActions::ListTraderItems;
				std::copy_n(emu->items, UF::invtype::BAZAAR_SIZE, eq->item_id);
				std::copy_n(emu->item_cost, UF::invtype::BAZAAR_SIZE, eq->item_cost);

				FINISH_ENCODE();
				break;
			}
			case BuyTraderItem: {
				ENCODE_LENGTH_EXACT(TraderBuy_Struct);
				SETUP_DIRECT_ENCODE(TraderBuy_Struct, structs::TraderBuy_Struct);
				LogTrading(
					"Encode OP_Trader item_id <green>[{}] price <green>[{}] quantity <green>[{}] trader_id <green>[{}]",
					eq->item_id,
					eq->price,
					eq->quantity,
					eq->trader_id
				);

				eq->action = structs::UFBazaarTraderBuyerActions::BuyTraderItem;
				OUT(price);
				OUT(trader_id);
				OUT(item_id);
				OUT(already_sold);
				OUT(quantity);
				strn0cpy(eq->item_name, emu->item_name, sizeof(eq->item_name));

				FINISH_ENCODE();
				break;
			}
			case ItemMove: {
				LogTrading(
					"Encode OP_Trader ItemMove action <green>[{}]",
					action
				);
				EQApplicationPacket *in = *p;
				*p = nullptr;
				dest->FastQueuePacket(&in, ack_req);
				break;
			}
			default: {
				EQApplicationPacket *in = *p;
				*p                      = nullptr;

				dest->FastQueuePacket(&in, ack_req);
				LogError("Unknown Encode OP_Trader action <red>{} received.  Unhandled.", action);
			}
		}
	}
```

---

## OP_TraderBuy (0x3672)
- **Direction:** outgoing
- **Structure:** TraderBuy_Struct

**Structure Definition:**
```cpp
struct TraderBuy_Struct {
	uint32 action;
	uint32 unknown_004;
	uint32 price;
	uint32 unknown_008;    // Probably high order bits of a 64 bit price.
	uint32 trader_id;
	char   item_name[64];
	uint32 unknown_076;
	uint32 item_id;
	uint32 already_sold;
	uint32 quantity;
	uint32 unknown_092;
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_TraderBuy)
	{
		ENCODE_LENGTH_EXACT(TraderBuy_Struct);
		SETUP_DIRECT_ENCODE(TraderBuy_Struct, structs::TraderBuy_Struct);
		LogTrading(
			"Encode OP_TraderBuy item_id <green>[{}] price <green>[{}] quantity <green>[{}] trader_id <green>[{}]",
			emu->item_id,
			emu->price,
			emu->quantity,
			emu->trader_id
		);

		OUT(action);
		OUT(price);
		OUT(trader_id);
		OUT(item_id);
		OUT(already_sold);
		OUT(quantity);
		OUT_str(item_name);

		FINISH_ENCODE();
	}
```

---

## OP_TraderShop (0x2881)
- **Direction:** outgoing
- **Structure:** TraderClick_Struct

**Structure Definition:**
```cpp
struct TraderClick_Struct{
	uint32 trader_id;
	uint32 action;
	uint32 unknown_004;
	uint32 approval;
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_TraderShop)
	{
		auto action = *(uint32 *)(*p)->pBuffer;

		switch (action) {
			case ClickTrader: {
				ENCODE_LENGTH_EXACT(TraderClick_Struct);
				SETUP_DIRECT_ENCODE(TraderClick_Struct, structs::TraderClick_Struct);
				LogTrading(
					"ClickTrader action <green>[{}] trader_id <green>[{}]",
					action,
					emu->TraderID
				);

				eq->action    = 0;
				eq->trader_id = emu->TraderID;
				eq->approval  = emu->Approval;

				FINISH_ENCODE();
				break;
			}
			case BuyTraderItem: {
				ENCODE_LENGTH_EXACT(TraderBuy_Struct);
				SETUP_DIRECT_ENCODE(TraderBuy_Struct, structs::TraderBuy_Struct);
				LogTrading(
					"Encode OP_TraderShop item_id <green>[{}] price <green>[{}] quantity <green>[{}] trader_id <green>[{}]",
					eq->item_id,
					eq->price,
					eq->quantity,
					eq->trader_id
				);

				eq->action = structs::UFBazaarTraderBuyerActions::BuyTraderItem;
				OUT(price);
				OUT(trader_id);
				OUT(item_id);
				OUT(already_sold);
				OUT(quantity);
				strn0cpy(eq->item_name, emu->item_name, sizeof(eq->item_name));

				FINISH_ENCODE();
				break;
			}
			default: {
				EQApplicationPacket *in = *p;
				*p = nullptr;

				dest->FastQueuePacket(&in, ack_req);
				LogError("Unknown Encode OP_TraderShop action <red>[{}] received.  Unhandled.", action);
			}
		}
	}
```

---

## OP_TributeItem (0x0b89)
- **Direction:** outgoing
- **Structure:** TributeItem_Struct

**Structure Definition:**
```cpp
struct TributeItem_Struct {
	uint32	slot;
	uint32	quantity;
	uint32	tribute_master_id;
	int32	tribute_points;
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_TributeItem)
	{
		ENCODE_LENGTH_EXACT(TributeItem_Struct);
		SETUP_DIRECT_ENCODE(TributeItem_Struct, structs::TributeItem_Struct);

		eq->slot = ServerToUFSlot(emu->slot);
		OUT(quantity);
		OUT(tribute_master_id);
		OUT(tribute_points);

		FINISH_ENCODE();
	}
```

---

## OP_VetRewardsAvaliable (0x0baa)
- **Direction:** outgoing
- **Structure:** VeteranReward

**Structure Definition:**
```cpp
struct VeteranRewardItem
{
/*000*/	uint32 item_id;
/*004*/	uint32 charges;
/*008*/	char item_name[64];
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_VetRewardsAvaliable)
	{
		EQApplicationPacket *inapp = *p;
		unsigned char * __emu_buffer = inapp->pBuffer;

		uint32 count = ((*p)->Size() / sizeof(InternalVeteranReward));
		*p = nullptr;

		auto outapp_create =
		    new EQApplicationPacket(OP_VetRewardsAvaliable, (sizeof(structs::VeteranReward) * count));
		uchar *old_data = __emu_buffer;
		uchar *data = outapp_create->pBuffer;
		for (unsigned int i = 0; i < count; ++i)
		{
			structs::VeteranReward *vr = (structs::VeteranReward*)data;
			InternalVeteranReward *ivr = (InternalVeteranReward*)old_data;

			vr->claim_count = ivr->claim_count;
			vr->claim_id = ivr->claim_id;
			vr->number_available = ivr->number_available;
			for (int x = 0; x < 8; ++x)
			{
				vr->items[x].item_id = ivr->items[x].item_id;
				strcpy(vr->items[x].item_name, ivr->items[x].item_name);
				vr->items[x].charges = ivr->items[x].charges;
			}

			old_data += sizeof(InternalVeteranReward);
			data += sizeof(structs::VeteranReward);
		}

		dest->FastQueuePacket(&outapp_create);
		delete inapp;
	}
```

---

## OP_WearChange (0x0400)
- **Direction:** outgoing
- **Structure:** WearChange_Struct

**Structure Definition:**
```cpp
struct WearChange_Struct{
/*000*/ uint16 spawn_id;
/*002*/ uint32 material;
/*006*/ uint32 unknown06;
/*010*/ uint32 elite_material;	// 1 for Drakkin Elite Material
/*014*/ Tint_Struct color;
/*018*/ uint8 wear_slot_id;
/*019*/
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_WearChange)
	{
		ENCODE_LENGTH_EXACT(WearChange_Struct);
		SETUP_DIRECT_ENCODE(WearChange_Struct, structs::WearChange_Struct);

		OUT(spawn_id);
		OUT(material);
		OUT(unknown06);
		OUT(elite_material);
		OUT(color.Color);
		OUT(wear_slot_id);

		FINISH_ENCODE();
	}
```

---

## OP_WhoAllResponse (0x6ffa)
- **Direction:** outgoing
- **Structure:** (unknown)

**Structure Definition:**
```cpp
Structure definition not found.
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_WhoAllResponse)
	{
		EQApplicationPacket *in = *p;
		*p = nullptr;

		char *InBuffer = (char *)in->pBuffer;

		WhoAllReturnStruct *wars = (WhoAllReturnStruct*)InBuffer;

		int Count = wars->playercount;

		auto outapp = new EQApplicationPacket(OP_WhoAllResponse, in->size + (Count * 4));

		char *OutBuffer = (char *)outapp->pBuffer;

		memcpy(OutBuffer, InBuffer, sizeof(WhoAllReturnStruct));

		OutBuffer += sizeof(WhoAllReturnStruct);
		InBuffer += sizeof(WhoAllReturnStruct);

		for (int i = 0; i < Count; ++i)
		{
			uint32 x;

			x = VARSTRUCT_DECODE_TYPE(uint32, InBuffer);

			VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, x);

			InBuffer += 4;
			VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, 0);
			VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, 0xffffffff);

			char Name[64];


			VARSTRUCT_DECODE_STRING(Name, InBuffer);	// Char Name
			VARSTRUCT_ENCODE_STRING(OutBuffer, Name);

			x = VARSTRUCT_DECODE_TYPE(uint32, InBuffer);
			VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, x);

			VARSTRUCT_DECODE_STRING(Name, InBuffer);	// Guild Name
			VARSTRUCT_ENCODE_STRING(OutBuffer, Name);

			for (int j = 0; j < 7; ++j)
			{
				x = VARSTRUCT_DECODE_TYPE(uint32, InBuffer);
				VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, x);
			}

			VARSTRUCT_DECODE_STRING(Name, InBuffer);		// Account
			VARSTRUCT_ENCODE_STRING(OutBuffer, Name);

			x = VARSTRUCT_DECODE_TYPE(uint32, InBuffer);
			VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, x);
		}

		//Log.Hex(Logs::Netcode, outapp->pBuffer, outapp->size);
		dest->FastQueuePacket(&outapp);
		delete in;
	}
```

---

## OP_ZoneEntry (0x4b61)
- **Direction:** outgoing
- **Structure:** (unknown)

**Structure Definition:**
```cpp
Structure definition not found.
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_ZoneEntry) { ENCODE_FORWARD(OP_ZoneSpawns); }
```

---

## OP_ZonePlayerToBind (0x382c)
- **Direction:** outgoing
- **Structure:** ZonePlayerToBind_Struct

**Structure Definition:**
```cpp
struct ZonePlayerToBind_Struct {
/*000*/	uint16	bind_zone_id;
/*002*/	uint16	bind_instance_id;
/*004*/	float	x;
/*008*/	float	y;
/*012*/	float	z;
/*016*/	float	heading;
/*020*/	char	zone_name[1];  // Or "Bind Location"
/*000*/	uint8	unknown021;	// Seen 1 - Maybe 0 would be to force a rezone and 1 is just respawn
/*000*/	uint32	unknown022;	// Seen 32 or 59
/*000*/	uint32	unknown023;	// Seen 0
/*000*/	uint32	unknown024;	// Seen 21 or 43
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_ZonePlayerToBind)
	{
		SETUP_VAR_ENCODE(ZonePlayerToBind_Struct);
		ALLOC_LEN_ENCODE(sizeof(structs::ZonePlayerToBind_Struct) + strlen(emu->zone_name));

		__packet->SetWritePosition(0);
		__packet->WriteUInt16(emu->bind_zone_id);
		__packet->WriteUInt16(emu->bind_instance_id);
		__packet->WriteFloat(emu->x);
		__packet->WriteFloat(emu->y);
		__packet->WriteFloat(emu->z);
		__packet->WriteFloat(emu->heading);
		__packet->WriteString(emu->zone_name);
		__packet->WriteUInt8(1); // save items
		__packet->WriteUInt32(0); // hp
		__packet->WriteUInt32(0); // mana
		__packet->WriteUInt32(0); // endurance

		FINISH_ENCODE();
	}
```

---

## OP_ZoneServerInfo (0x1190)
- **Direction:** outgoing
- **Structure:** ZoneServerInfo_Struct

**Structure Definition:**
```cpp
struct ZoneServerInfo_Struct
{
/*0000*/	char	ip[128];
/*0128*/	uint16	port;
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_ZoneServerInfo)
	{
		SETUP_DIRECT_ENCODE(ZoneServerInfo_Struct, ZoneServerInfo_Struct);

		OUT_str(ip);
		OUT(port);

		FINISH_ENCODE();
	}
```

---

## OP_ZoneSpawns (0x7114)
- **Direction:** outgoing
- **Structure:** Spawn_Struct

**Structure Definition:**
```cpp
struct Spawn_Struct_Bitfields
{

	unsigned   ispet:1;		// Could be 'is summoned pet' rather than just is pet.
	unsigned   afk:1;		// 0=no, 1=afk
	unsigned   sneak:1;
	unsigned   lfg:1;
	unsigned   padding5:1;
	unsigned   invis:12;		// there are 3000 different (non-GM) invis levels
	unsigned   gm:1;
	unsigned   anon:2;		// 0=normal, 1=anon, 2=roleplay
	unsigned   gender:2;		// Gender (0=male, 1=female, 2=monster)
	unsigned   linkdead:1;		// Toggles LD on or off after name
	unsigned   betabuffed:1;
	unsigned   showhelm:1;
	unsigned   padding26:1;
	unsigned   targetable:1;	// 1 = Targetable 0 = Not Targetable (is_npc?)
	unsigned   targetable_with_hotkey:1;	// is_npc?
	unsigned   showname:1;
	unsigned   statue:1;
	unsigned   trader:1;
	unsigned   buyer:1;
};
```

**Full outgoing Section:**
```cpp
	ENCODE(OP_ZoneSpawns)
	{
		//consume the packet
		EQApplicationPacket *in = *p;
		*p = nullptr;

		//store away the emu struct
		unsigned char *__emu_buffer = in->pBuffer;
		Spawn_Struct *emu = (Spawn_Struct *)__emu_buffer;

		//determine and verify length
		int entrycount = in->size / sizeof(Spawn_Struct);
		if (entrycount == 0 || (in->size % sizeof(Spawn_Struct)) != 0) {
			LogNetcode("[STRUCTS] Wrong size on outbound [{}]: Got [{}], expected multiple of [{}]", opcodes->EmuToName(in->GetOpcode()), in->size, sizeof(Spawn_Struct));
			delete in;
			return;
		}

		//Log.LogDebugType(Logs::General, Logs::Netcode, "[STRUCTS] Spawn name is [%s]", emu->name);

		emu = (Spawn_Struct *)__emu_buffer;

		//Log.LogDebugType(Logs::General, Logs::Netcode, "[STRUCTS] Spawn packet size is %i, entries = %i", in->size, entrycount);

		char *Buffer = (char *)in->pBuffer;

		int r;
		int k;
		for (r = 0; r < entrycount; r++, emu++) {

			int PacketSize = sizeof(structs::Spawn_Struct);

			PacketSize += strlen(emu->name);
			PacketSize += strlen(emu->lastName);

			if (strlen(emu->title))
				PacketSize += strlen(emu->title) + 1;

			if (strlen(emu->suffix))
				PacketSize += strlen(emu->suffix) + 1;

			if (emu->DestructibleObject || emu->class_ == Class::LDoNTreasure)
			{
				if (emu->DestructibleObject)
					PacketSize = PacketSize - 4;	// No bodytype

				PacketSize += 53;	// Fixed portion
				PacketSize += strlen(emu->DestructibleModel) + 1;
				PacketSize += strlen(emu->DestructibleName2) + 1;
				PacketSize += strlen(emu->DestructibleString) + 1;
			}

			bool ShowName = emu->show_name;
			if (emu->bodytype >= 66)
			{
				emu->race = 127;
				emu->bodytype = 11;
				emu->gender = 0;
				ShowName = 0;
			}

			float SpawnSize = emu->size;
			if (!((emu->NPC == 0) || (emu->race <= Race::Gnome) || (emu->race == Race::Iksar) ||
					(emu->race == Race::VahShir) || (emu->race == Race::Froglok2) || (emu->race == Race::Drakkin))
				)
			{
				PacketSize -= (sizeof(structs::Texture_Struct) * EQ::textures::materialCount);

				if (emu->size == 0)
				{
					emu->size = 6;
					SpawnSize = 6;
				}
			}

			if (SpawnSize == 0)
			{
				SpawnSize = 3;
			}

			auto outapp = new EQApplicationPacket(OP_ZoneEntry, PacketSize);
			Buffer = (char *)outapp->pBuffer;

			VARSTRUCT_ENCODE_STRING(Buffer, emu->name);
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->spawnId);
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->level);

			if (emu->DestructibleObject)
			{
				VARSTRUCT_ENCODE_TYPE(float, Buffer, 10);	// was int and 0x41200000
			}
			else
			{
				VARSTRUCT_ENCODE_TYPE(float, Buffer, SpawnSize - 0.7);	// Eye Height?
			}

			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->NPC);

			structs::Spawn_Struct_Bitfields *Bitfields = (structs::Spawn_Struct_Bitfields*)Buffer;

			Bitfields->afk = 0;
			Bitfields->linkdead = 0;
			Bitfields->gender = emu->gender;

			Bitfields->invis = emu->invis;
			Bitfields->sneak = 0;
			Bitfields->lfg = emu->lfg;
			Bitfields->gm = emu->gm;
			Bitfields->anon = emu->anon;
			Bitfields->showhelm = emu->showhelm;
			Bitfields->targetable = 1;
			Bitfields->targetable_with_hotkey = emu->targetable_with_hotkey ? 1 : 0;
			Bitfields->statue = 0;
			Bitfields->trader = emu->trader ? 1 : 0;
			Bitfields->buyer = 0;

			Bitfields->showname = ShowName;

			if (emu->DestructibleObject)
			{
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0x1d600000);
				Buffer = Buffer - 4;
			}

			Bitfields->ispet = emu->is_pet;

			Buffer += sizeof(structs::Spawn_Struct_Bitfields);

			uint8 OtherData = 0;

			if (emu->class_ == Class::LDoNTreasure) //Ldon chest
			{
				OtherData = OtherData | 0x01;
			}

			if (strlen(emu->title)) {
				OtherData = OtherData | 0x04;
			}
			if (strlen(emu->suffix)) {
				OtherData = OtherData | 0x08;
			}
			if (emu->DestructibleObject) {
				OtherData = OtherData | 0xd1;	// Live has 0xe1 for OtherData
			}
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, OtherData);

			if (emu->DestructibleObject)
			{
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0x00000000);
			}
			else
			{
				VARSTRUCT_ENCODE_TYPE(float, Buffer, -1);	// unknown3
			}
			VARSTRUCT_ENCODE_TYPE(float, Buffer, 0);	// unknown4

			if (emu->DestructibleObject || emu->class_ == Class::LDoNTreasure)
			{
				VARSTRUCT_ENCODE_STRING(Buffer, emu->DestructibleModel);
				VARSTRUCT_ENCODE_STRING(Buffer, emu->DestructibleName2);
				VARSTRUCT_ENCODE_STRING(Buffer, emu->DestructibleString);

				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->DestructibleAppearance);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->DestructibleUnk1);

				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->DestructibleID1);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->DestructibleID2);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->DestructibleID3);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->DestructibleID4);

				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->DestructibleUnk2);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->DestructibleUnk3);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->DestructibleUnk4);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->DestructibleUnk5);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->DestructibleUnk6);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->DestructibleUnk7);
				VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->DestructibleUnk8);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->DestructibleUnk9);
			}

			VARSTRUCT_ENCODE_TYPE(float, Buffer, emu->size);
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->face);
			VARSTRUCT_ENCODE_TYPE(float, Buffer, emu->walkspeed);
			VARSTRUCT_ENCODE_TYPE(float, Buffer, emu->runspeed);
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->race);
			/*
			if(emu->bodytype >=66)
			{
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, 0);	// showname
			}
			else
			{
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, 1);	// showname
			}*/


			if (!emu->DestructibleObject)
			{
				// Setting this next field to zero will cause a crash. Looking at ShowEQ, if it is zero, the bodytype field is not
				// present. Will sort that out later.
				VARSTRUCT_ENCODE_TYPE(uint8, Buffer, 1);	// This is a properties count field
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->bodytype);
			}
			else
			{
				VARSTRUCT_ENCODE_TYPE(uint8, Buffer, 0);
			}

			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->curHp);
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->haircolor);
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->beardcolor);
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->eyecolor1);
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->eyecolor2);
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->hairstyle);
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->beard);
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->drakkin_heritage);
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->drakkin_tattoo);
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->drakkin_details);
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, 0);	// ShowEQ calls this 'Holding'
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->deity);
			if (emu->NPC)
			{
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0xFFFFFFFF);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0x00000000);
			}
			else
			{
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->guildID);
				//Translate older ranks to new values* /
				switch (emu->guildrank) {
					case GUILD_SENIOR_MEMBER:
					case GUILD_MEMBER:
					case GUILD_JUNIOR_MEMBER:
					case GUILD_INITIATE:
					case GUILD_RECRUIT: {
						emu->guildrank = GUILD_MEMBER_TI;
						break;
					}
					case GUILD_OFFICER:
					case GUILD_SENIOR_OFFICER: {
						emu->guildrank = GUILD_OFFICER_TI;
						break;
					}
					case GUILD_LEADER: {
						emu->guildrank = GUILD_LEADER_TI;
						break;
					}
					default: {
						emu->guildrank = GUILD_RANK_NONE_TI;
						break;
					}
				}
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->guildrank);
			}
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->class_);
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, 0);	// pvp
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->StandState);	// standstate
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->light);
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->flymode);
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->equip_chest2); // unknown8
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, 0); // unknown9
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, 0); // unknown10
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->helm); // unknown11
			VARSTRUCT_ENCODE_STRING(Buffer, emu->lastName);
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0);	// aatitle
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, 0); // unknown12
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->petOwnerId);
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, 0); // unknown13
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->PlayerState);
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0); // unknown15
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0); // unknown16
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0); // unknown17
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0xffffffff); // unknown18
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0xffffffff); // unknown19

			structs::Spawn_Struct_Position *Position = (structs::Spawn_Struct_Position*)Buffer;

			Position->deltaX = emu->deltaX;
			Position->deltaHeading = emu->deltaHeading;
			Position->deltaY = emu->deltaY;
			Position->y = emu->y;
			Position->animation = emu->animation;
			Position->heading = emu->heading;
			Position->x = emu->x;
			Position->z = emu->z;
			Position->deltaZ = emu->deltaZ;

			Buffer += sizeof(structs::Spawn_Struct_Position);

			if ((emu->NPC == 0) || (emu->race <= Race::Gnome) || (emu->race == Race::Iksar) ||
					(emu->race == Race::VahShir) || (emu->race == Race::Froglok2) || (emu->race == Race::Drakkin)
				)
			{
				for (k = EQ::textures::textureBegin; k < EQ::textures::materialCount; ++k)
				{
					{
						VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->equipment_tint.Slot[k].Color);
					}
				}
			}
			else
			{
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0);

				if (emu->equipment.Primary.Material > 99999) {
					VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 63);
				} else {
					VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->equipment.Primary.Material);
				}

				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0);

				if (emu->equipment.Secondary.Material > 99999) {
					VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 63);
				} else {
					VARSTRUCT_ENCODE_TYPE(uint32, Buffer, emu->equipment.Secondary.Material);
				}

				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0);
				VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0);
			}

			if ((emu->NPC == 0) || (emu->race <= Race::Gnome) || (emu->race == Race::Iksar) ||
					(emu->race == Race::VahShir) || (emu->race == Race::Froglok2) || (emu->race == Race::Drakkin)
				)
			{
				structs::Texture_Struct *Equipment = (structs::Texture_Struct *)Buffer;

				for (k = EQ::textures::textureBegin; k < EQ::textures::materialCount; k++) {
					if (emu->equipment.Slot[k].Material > 99999) {
						Equipment[k].Material = 63;
					} else {
						Equipment[k].Material = emu->equipment.Slot[k].Material;
					}
					Equipment[k].Unknown1 = emu->equipment.Slot[k].Unknown1;
					Equipment[k].EliteMaterial = emu->equipment.Slot[k].EliteModel;
				}

				Buffer += (sizeof(structs::Texture_Struct) * EQ::textures::materialCount);
			}
			if (strlen(emu->title))
			{
				VARSTRUCT_ENCODE_STRING(Buffer, emu->title);
			}

			if (strlen(emu->suffix))
			{
				VARSTRUCT_ENCODE_STRING(Buffer, emu->suffix);
			}
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0); // Unknown;
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, 0); // Unknown;
			VARSTRUCT_ENCODE_TYPE(uint8, Buffer, emu->IsMercenary); //IsMercenary
			Buffer += 28; // Unknown;

			dest->FastQueuePacket(&outapp, ack_req);
		}

		delete in;
	}
```

---

## OP_AdventureMerchantSell (0x179d)
- **Direction:** incoming
- **Structure:** Adventure_Sell_Struct

**Structure Definition:**
```cpp
struct Adventure_Sell_Struct {
/*000*/	uint32	unknown000;	//0x01 - Stack Size/Charges?
/*004*/	uint32	npcid;
/*008*/	uint32	slot;
/*012*/	uint32	charges;
/*016*/	uint32	sell_price;
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_AdventureMerchantSell)
	{
		DECODE_LENGTH_EXACT(structs::Adventure_Sell_Struct);
		SETUP_DIRECT_DECODE(Adventure_Sell_Struct, structs::Adventure_Sell_Struct);

		IN(npcid);
		emu->slot = UFToServerSlot(eq->slot);
		IN(charges);
		IN(sell_price);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_AltCurrencySell (0x14cf)
- **Direction:** incoming
- **Structure:** AltCurrencySellItem_Struct

**Structure Definition:**
```cpp
struct AltCurrencySellItem_Struct {
/*000*/ uint32 merchant_entity_id;
/*004*/ uint32 slot_id;
/*008*/ uint32 charges;
/*012*/ uint32 cost;
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_AltCurrencySell)
	{
		DECODE_LENGTH_EXACT(structs::AltCurrencySellItem_Struct);
		SETUP_DIRECT_DECODE(AltCurrencySellItem_Struct, structs::AltCurrencySellItem_Struct);

		IN(merchant_entity_id);
		emu->slot_id = UFToServerSlot(eq->slot_id);
		IN(charges);
		IN(cost);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_AltCurrencySellSelection (0x322a)
- **Direction:** incoming
- **Structure:** AltCurrencySelectItem_Struct

**Structure Definition:**
```cpp
struct AltCurrencySelectItem_Struct {
    uint32 merchant_entity_id;
    uint32 slot_id;
    uint32 unknown008;
    uint32 unknown012;
    uint32 unknown016;
    uint32 unknown020;
    uint32 unknown024;
    uint32 unknown028;
    uint32 unknown032;
    uint32 unknown036;
    uint32 unknown040;
    uint32 unknown044;
    uint32 unknown048;
    uint32 unknown052;
    uint32 unknown056;
    uint32 unknown060;
    uint32 unknown064;
    uint32 unknown068;
    uint32 unknown072;
    uint32 unknown076;
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_AltCurrencySellSelection)
	{
		DECODE_LENGTH_EXACT(structs::AltCurrencySelectItem_Struct);
		SETUP_DIRECT_DECODE(AltCurrencySelectItem_Struct, structs::AltCurrencySelectItem_Struct);

		IN(merchant_entity_id);
		emu->slot_id = UFToServerSlot(eq->slot_id);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_ApplyPoison (0x5cd3)
- **Direction:** incoming
- **Structure:** ApplyPoison_Struct

**Structure Definition:**
```cpp
struct ApplyPoison_Struct {
	uint32 inventorySlot;
	uint32 success;
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_ApplyPoison)
	{
		DECODE_LENGTH_EXACT(structs::ApplyPoison_Struct);
		SETUP_DIRECT_DECODE(ApplyPoison_Struct, structs::ApplyPoison_Struct);

		emu->inventorySlot = UFToServerSlot(eq->inventorySlot);
		IN(success);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_AugmentInfo (0x31b1)
- **Direction:** incoming
- **Structure:** AugmentInfo_Struct

**Structure Definition:**
```cpp
struct AugmentInfo_Struct
{
/*000*/ uint32	itemid;			// id of the solvent needed
/*004*/ uint32	window;			// window to display the information in
/*008*/ char	augment_info[64];	// total packet length 76, all the rest were always 00
/*072*/ uint32	unknown072;
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_AugmentInfo)
	{
		DECODE_LENGTH_EXACT(structs::AugmentInfo_Struct);
		SETUP_DIRECT_DECODE(AugmentInfo_Struct, structs::AugmentInfo_Struct);

		IN(itemid);
		IN(window);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_AugmentItem (0x7c87)
- **Direction:** incoming
- **Structure:** AugmentItem_Struct

**Structure Definition:**
```cpp
struct AugmentItem_Struct {
/*00*/	int16	container_slot;
/*02*/	char	unknown02[2];
/*04*/	int32	augment_slot;
/*08*/
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_AugmentItem)
	{
		DECODE_LENGTH_EXACT(structs::AugmentItem_Struct);
		SETUP_DIRECT_DECODE(AugmentItem_Struct, structs::AugmentItem_Struct);

		emu->container_slot = UFToServerSlot(eq->container_slot);
		emu->augment_slot = eq->augment_slot;

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_BazaarSearch (0x550f)
- **Direction:** incoming
- **Structure:** UFBazaarTraderBuyerActions

**Structure Definition:**
```cpp
Structure definition not found.
```

**Full incoming Section:**
```cpp
	DECODE(OP_BazaarSearch)
	{
		uint32 action = *(uint32 *) __packet->pBuffer;

		switch (action) {
			case structs::UFBazaarTraderBuyerActions::BazaarSearch: {
				DECODE_LENGTH_EXACT(structs::BazaarSearch_Struct);
				SETUP_DIRECT_DECODE(BazaarSearchCriteria_Struct, structs::BazaarSearch_Struct);

				emu->action           = eq->Beginning.Action;
				emu->item_stat        = eq->ItemStat;
				emu->max_cost         = eq->MaxPrice;
				emu->min_cost         = eq->MinPrice;
				emu->max_level        = eq->MaxLlevel;
				emu->min_level        = eq->Minlevel;
                emu->race             = eq->Race;
                emu->slot             = eq->Slot;
                emu->type             = eq->Type == UINT32_MAX ? UINT8_MAX : eq->Type;
                emu->trader_entity_id = eq->TraderID;
                emu->trader_id        = 0;
                emu->_class           = eq->Class_;
                emu->search_scope     = eq->TraderID > 0 ? NonRoFBazaarSearchScope : Local_Scope;
                emu->max_results      = RuleI(Bazaar, MaxSearchResults);
                strn0cpy(emu->item_name, eq->Name, sizeof(emu->item_name));

				FINISH_DIRECT_DECODE();
				break;
			}
			case structs::UFBazaarTraderBuyerActions::BazaarInspect: {
				SETUP_DIRECT_DECODE(BazaarInspect_Struct, structs::BazaarInspect_Struct);

				IN(action);
				memcpy(emu->player_name, eq->player_name, sizeof(emu->player_name));
				IN(serial_number);

				FINISH_DIRECT_DECODE();
				break;
			}
			case structs::UFBazaarTraderBuyerActions::WelcomeMessage: {
				break;
			}
			default: {
				LogTrading("(UF) Unhandled action <red>[{}]", action);
			}
		}
	}
```

---

## OP_BookButton (0x018e)
- **Direction:** incoming
- **Structure:** BookButton_Struct

**Structure Definition:**
```cpp
struct BookButton_Struct
{
/*0000*/ int32 invslot;
/*0004*/ int32 target_id; // client's target when using the book
/*0008*/ int32 unused;    // always 0 from button packets
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_BookButton)
	{
		DECODE_LENGTH_EXACT(structs::BookButton_Struct);
		SETUP_DIRECT_DECODE(BookButton_Struct, structs::BookButton_Struct);

		emu->invslot = static_cast<int16_t>(UFToServerSlot(eq->invslot));
		IN(target_id);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_Buff (0x0d1d)
- **Direction:** incoming
- **Structure:** SpellBuffPacket_Struct

**Structure Definition:**
```cpp
struct SpellBuffPacket_Struct {
/*000*/	uint32 entityid;	// Player id who cast the buff
/*004*/	SpellBuff_Struct buff;
/*080*/	uint32 slotid;
/*084*/	uint32 bufffade;
/*088*/
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_Buff)
	{
		DECODE_LENGTH_EXACT(structs::SpellBuffPacket_Struct);
		SETUP_DIRECT_DECODE(SpellBuffPacket_Struct, structs::SpellBuffPacket_Struct);

		IN(entityid);
		IN(buff.effect_type);
		IN(buff.level);
		IN(buff.unknown003);
		IN(buff.spellid);
		IN(buff.duration);
		emu->slotid = UFToServerBuffSlot(eq->slotid);
		IN(slotid);
		IN(bufffade);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_BuffRemoveRequest (0x4065)
- **Direction:** incoming
- **Structure:** BuffRemoveRequest_Struct

**Structure Definition:**
```cpp
struct BuffRemoveRequest_Struct
{
/*00*/ uint32 SlotID;
/*04*/ uint32 EntityID;
/*08*/
 };

struct GMTrainee_Struct
{
	/*000*/ uint32 npcid;
	/*004*/ uint32 playerid;
	/*008*/ uint32 skills[PACKET_SKILL_ARRAY_SIZE];
	/*408*/ uint8 unknown408[40];
	/*448*/
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_BuffRemoveRequest)
	{
		// This is to cater for the fact that short buff box buffs start at 30 as opposed to 25 in prior clients.
		//
		DECODE_LENGTH_EXACT(structs::BuffRemoveRequest_Struct);
		SETUP_DIRECT_DECODE(BuffRemoveRequest_Struct, structs::BuffRemoveRequest_Struct);

		emu->SlotID = UFToServerBuffSlot(eq->SlotID);

		IN(EntityID);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_CastSpell (0x50c2)
- **Direction:** incoming
- **Structure:** CastSpell_Struct

**Structure Definition:**
```cpp
struct CastSpell_Struct
{
	uint32	slot;
	uint32	spell_id;
	uint32	inventoryslot;  // slot for clicky item, 0xFFFF = normal cast
	uint32	target_id;
	uint32  cs_unknown1;
	uint32  cs_unknown2;
 	float   y_pos;
 	float   x_pos;
	float   z_pos;
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_CastSpell)
	{
		DECODE_LENGTH_EXACT(structs::CastSpell_Struct);
		SETUP_DIRECT_DECODE(CastSpell_Struct, structs::CastSpell_Struct);

		emu->slot = static_cast<uint32>(UFToServerCastingSlot(static_cast<spells::CastingSlot>(eq->slot)));

		IN(spell_id);
		emu->inventoryslot = UFToServerSlot(eq->inventoryslot);
		IN(target_id);
		IN(cs_unknown1);
		IN(cs_unknown2);
		IN(y_pos);
		IN(x_pos);
		IN(z_pos);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_ChannelMessage (0x2e79)
- **Direction:** incoming
- **Structure:** ChannelMessage_Struct

**Structure Definition:**
```cpp
struct ChannelMessage_Struct
{
/*000*/	char	targetname[64];		// Tell recipient
/*064*/	char	sender[64];			// The senders name (len might be wrong)
/*128*/	uint32	language;			// Language
/*132*/	uint32	chan_num;			// Channel
/*136*/	uint32	cm_unknown4[2];		// ***Placeholder
/*144*/	uint32	skill_in_language;	// The players skill in this language? might be wrong
/*148*/	char	message[0];			// Variable length message
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_ChannelMessage)
	{
		unsigned char *__eq_buffer = __packet->pBuffer;

		char *InBuffer = (char *)__eq_buffer;

		char Sender[64];
		char Target[64];

		VARSTRUCT_DECODE_STRING(Sender, InBuffer);
		VARSTRUCT_DECODE_STRING(Target, InBuffer);

		InBuffer += 4;

		uint32 Language = VARSTRUCT_DECODE_TYPE(uint32, InBuffer);
		uint32 Channel = VARSTRUCT_DECODE_TYPE(uint32, InBuffer);

		InBuffer += 5;

		uint32 Skill = VARSTRUCT_DECODE_TYPE(uint32, InBuffer);

		std::string old_message = InBuffer;
		std::string new_message;
		UFToServerSayLink(new_message, old_message);

		//__packet->size = sizeof(ChannelMessage_Struct)+strlen(InBuffer) + 1;
		__packet->size = sizeof(ChannelMessage_Struct) + new_message.length() + 1;

		__packet->pBuffer = new unsigned char[__packet->size];
		ChannelMessage_Struct *emu = (ChannelMessage_Struct *)__packet->pBuffer;

		strn0cpy(emu->targetname, Target, sizeof(emu->targetname));
		strn0cpy(emu->sender, Target, sizeof(emu->sender));
		emu->language = Language;
		emu->chan_num = Channel;
		emu->skill_in_language = Skill;
		strcpy(emu->message, new_message.c_str());

		delete[] __eq_buffer;
	}
```

---

## OP_CharacterCreate (0x1b85)
- **Direction:** incoming
- **Structure:** CharCreate_Struct

**Structure Definition:**
```cpp
struct CharCreate_Struct
{
/*0000*/	uint32	class_;
/*0004*/	uint32	haircolor;
/*0008*/	uint32	beard;
/*0012*/	uint32	beardcolor;
/*0016*/	uint32	gender;
/*0020*/	uint32	race;
/*0024*/	uint32	start_zone;
/*0028*/	uint32	hairstyle;
/*0032*/	uint32	deity;
/*0036*/	uint32	STR;
/*0040*/	uint32	STA;
/*0044*/	uint32	AGI;
/*0048*/	uint32	DEX;
/*0052*/	uint32	WIS;
/*0056*/	uint32	INT;
/*0060*/	uint32	CHA;
/*0064*/	uint32	face;		// Could be unknown0076
/*0068*/	uint32	eyecolor1;	//its possiable we could have these switched
/*0073*/	uint32	eyecolor2;	//since setting one sets the other we really can't check
/*0076*/	uint32	tutorial;
/*0080*/	uint32	drakkin_heritage;
/*0084*/	uint32	drakkin_tattoo;
/*0088*/	uint32	drakkin_details;
/*0092*/
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_CharacterCreate)
	{
		DECODE_LENGTH_EXACT(structs::CharCreate_Struct);
		SETUP_DIRECT_DECODE(CharCreate_Struct, structs::CharCreate_Struct);

		IN(class_);
		IN(beardcolor);
		IN(beard);
		IN(hairstyle);
		IN(gender);
		IN(race);
		IN(start_zone);
		IN(haircolor);
		IN(deity);
		IN(STR);
		IN(STA);
		IN(AGI);
		IN(DEX);
		IN(WIS);
		IN(INT);
		IN(CHA);
		IN(face);
		IN(eyecolor1);
		IN(eyecolor2);
		IN(tutorial);
		IN(drakkin_heritage);
		IN(drakkin_tattoo);
		IN(drakkin_details);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_ClientUpdate (0x7062)
- **Direction:** incoming
- **Structure:** PlayerPositionUpdateServer_Struct

**Structure Definition:**
```cpp
struct PlayerPositionUpdateServer_Struct
{
/*0000*/	uint16		spawn_id;
/*0002*/	signed		padding0000 : 12;	// ***Placeholder
			signed		delta_x : 13;		// change in x
			signed		padding0005 : 7;	// ***Placeholder
/*0006*/	signed		delta_heading : 10;	// change in heading
			signed		delta_y : 13;		// change in y
			signed		padding0006 : 9;	// ***Placeholder
/*0010*/	signed		y_pos : 19;			// y coord
			signed		animation : 10;		// animation
			signed		padding0010 : 3;	// ***Placeholder
/*0014*/	unsigned	heading : 12;		// heading
			signed		x_pos : 19;			// x coord
			signed		padding0014 : 1;	// ***Placeholder
/*0018*/	signed		z_pos : 19;			// z coord
			signed		delta_z : 13;		// change in z
/*0022*/
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_ClientUpdate)
	{
		// for some odd reason, there is an extra byte on the end of this on occasion..
		DECODE_LENGTH_ATLEAST(structs::PlayerPositionUpdateClient_Struct);
		SETUP_DIRECT_DECODE(PlayerPositionUpdateClient_Struct, structs::PlayerPositionUpdateClient_Struct);

		IN(spawn_id);
		IN(sequence);
		IN(x_pos);
		IN(y_pos);
		IN(z_pos);
		IN(heading);
		IN(delta_x);
		IN(delta_y);
		IN(delta_z);
		IN(delta_heading);
		IN(animation);
		emu->vehicle_id = 0;

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_Consider (0x3c2d)
- **Direction:** incoming
- **Structure:** Consider_Struct

**Structure Definition:**
```cpp
struct Consider_Struct{
/*000*/ uint32	playerid;               // PlayerID
/*004*/ uint32	targetid;               // TargetID
/*008*/ uint32	faction;                // Faction
/*012*/ uint32	level;					// Level
/*016*/ uint8	pvpcon;					// Pvp con flag 0/1
/*017*/ uint8	unknown017[3];			//
/*020*/
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_Consider)
	{
		DECODE_LENGTH_EXACT(structs::Consider_Struct);
		SETUP_DIRECT_DECODE(Consider_Struct, structs::Consider_Struct);

		IN(playerid);
		IN(targetid);
		IN(faction);
		IN(level);
		//emu->cur_hp = 1;
		//emu->max_hp = 2;
		//emu->pvpcon = 0;

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_ConsiderCorpse (0x0a18)
- **Direction:** incoming
- **Structure:** (unknown)

**Structure Definition:**
```cpp
Structure definition not found.
```

**Full incoming Section:**
```cpp
	DECODE(OP_ConsiderCorpse) { DECODE_FORWARD(OP_Consider); }
```

---

## OP_Consume (0x24c5)
- **Direction:** incoming
- **Structure:** Consume_Struct

**Structure Definition:**
```cpp
struct Consume_Struct
{
/*0000*/ uint32	slot;
/*0004*/ uint32	auto_consumed; // 0xffffffff when auto eating e7030000 when right click
/*0008*/ uint8	c_unknown1[4];
/*0012*/ uint8	type; // 0x01=Food 0x02=Water
/*0013*/ uint8	unknown13[3];
/*0016*/
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_Consume)
	{
		DECODE_LENGTH_EXACT(structs::Consume_Struct);
		SETUP_DIRECT_DECODE(Consume_Struct, structs::Consume_Struct);

		emu->slot = UFToServerSlot(eq->slot);
		IN(auto_consumed);
		IN(type);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_Damage (0x631a)
- **Direction:** incoming
- **Structure:** CombatDamage_Struct

**Structure Definition:**
```cpp
struct CombatDamage_Struct
{
/* 00 */	uint16	target;
/* 02 */	uint16	source;
/* 04 */	uint8	type;			//slashing, etc.  231 (0xE7) for spells
/* 05 */	uint16	spellid;
/* 07 */	int32	damage;
/* 11 */	float	force;		// cd cc cc 3d
/* 15 */	float	hit_heading;		// see above notes in Action_Struct
/* 19 */	float	hit_pitch;
/* 23 */	uint8	secondary;	// 0 for primary hand, 1 for secondary
/* 24 */	uint32	special; // 2 = Rampage, 1 = Wild Rampage
/* 28 */
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_Damage)
	{
		DECODE_LENGTH_EXACT(structs::CombatDamage_Struct);
		SETUP_DIRECT_DECODE(CombatDamage_Struct, structs::CombatDamage_Struct);

		IN(target);
		IN(source);
		IN(type);
		IN(spellid);
		IN(damage);
		IN(hit_heading);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_DeleteItem (0x66e0)
- **Direction:** incoming
- **Structure:** DeleteItem_Struct

**Structure Definition:**
```cpp
struct DeleteItem_Struct {
/*0000*/ uint32	from_slot;
/*0004*/ uint32	to_slot;
/*0008*/ uint32	number_in_stack;
/*0012*/
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_DeleteItem)
	{
		DECODE_LENGTH_EXACT(structs::DeleteItem_Struct);
		SETUP_DIRECT_DECODE(DeleteItem_Struct, structs::DeleteItem_Struct);

		emu->from_slot = UFToServerSlot(eq->from_slot);
		emu->to_slot = UFToServerSlot(eq->to_slot);
		IN(number_in_stack);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_DzAddPlayer (0x3657)
- **Direction:** incoming
- **Structure:** ExpeditionCommand_Struct

**Structure Definition:**
```cpp
struct ExpeditionCommand_Struct
{
/*000*/ uint32 unknown000;
/*004*/ uint32 unknown004;
/*008*/ char   name[64];
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_DzAddPlayer)
	{
		DECODE_LENGTH_EXACT(structs::ExpeditionCommand_Struct);
		SETUP_DIRECT_DECODE(ExpeditionCommand_Struct, structs::ExpeditionCommand_Struct);

		strn0cpy(emu->name, eq->name, sizeof(emu->name));

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_DzChooseZoneReply (0xa682)
- **Direction:** incoming
- **Structure:** DynamicZoneChooseZoneReply_Struct

**Structure Definition:**
```cpp
struct DynamicZoneChooseZoneReply_Struct
{
/*000*/ uint32 unknown000;     // ff ff ff ff
/*004*/ uint32 unknown004;     // seen 69 00 00 00
/*008*/ uint32 unknown008;     // ff ff ff ff
/*012*/ uint32 unknown_id1;    // from choose zone entry message
/*016*/ uint16 dz_zone_id;     // dz_id pair
/*018*/ uint16 dz_instance_id;
/*020*/ uint32 dz_type;        // 1: Expedition, 2: Tutorial, 3: Task, 4: Mission, 5: Quest
/*024*/ uint32 unknown_id2;    // from choose zone entry message
/*028*/ uint32 unknown028;     // 00 00 00 00
/*032*/ uint32 unknown032;     // always same as unknown044
/*036*/ uint32 unknown036;
/*040*/ uint32 unknown040;
/*044*/ uint32 unknown044;     // always same as unknown032
/*048*/ uint32 unknown048;     // seen 01 00 00 00 and 02 00 00 00
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_DzChooseZoneReply)
	{
		DECODE_LENGTH_EXACT(structs::DynamicZoneChooseZoneReply_Struct);
		SETUP_DIRECT_DECODE(DynamicZoneChooseZoneReply_Struct, structs::DynamicZoneChooseZoneReply_Struct);

		IN(unknown000);
		IN(unknown004);
		IN(unknown008);
		IN(unknown_id1);
		IN(dz_zone_id);
		IN(dz_instance_id);
		IN(dz_type);
		IN(unknown_id2);
		IN(unknown028);
		IN(unknown032);
		IN(unknown036);
		IN(unknown040);
		IN(unknown044);
		IN(unknown048);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_DzExpeditionInviteResponse (0x1154)
- **Direction:** incoming
- **Structure:** ExpeditionInviteResponse_Struct

**Structure Definition:**
```cpp
struct ExpeditionInviteResponse_Struct
{
/*000*/ uint32 unknown000;
/*004*/ uint32 unknown004;
/*008*/ uint16 dz_zone_id;     // dz_id pair sent in invite
/*010*/ uint16 dz_instance_id;
/*012*/ uint8  accepted;       // 0: declined 1: accepted
/*013*/ uint8  swapping;       // 0: adding 1: swapping (sent in invite)
/*014*/ char   swap_name[64];  // swap name sent in invite
/*078*/ uint8  unknown078;     // padding garbage?
/*079*/ uint8  unknown079;     // padding garbage?
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_DzExpeditionInviteResponse)
	{
		DECODE_LENGTH_EXACT(structs::ExpeditionInviteResponse_Struct);
		SETUP_DIRECT_DECODE(ExpeditionInviteResponse_Struct, structs::ExpeditionInviteResponse_Struct);

		IN(dz_zone_id);
		IN(dz_instance_id);
		IN(accepted);
		IN(swapping);
		strn0cpy(emu->swap_name, eq->swap_name, sizeof(emu->swap_name));

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_DzMakeLeader (0x226f)
- **Direction:** incoming
- **Structure:** ExpeditionCommand_Struct

**Structure Definition:**
```cpp
struct ExpeditionCommand_Struct
{
/*000*/ uint32 unknown000;
/*004*/ uint32 unknown004;
/*008*/ char   name[64];
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_DzMakeLeader)
	{
		DECODE_LENGTH_EXACT(structs::ExpeditionCommand_Struct);
		SETUP_DIRECT_DECODE(ExpeditionCommand_Struct, structs::ExpeditionCommand_Struct);

		strn0cpy(emu->name, eq->name, sizeof(emu->name));

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_DzRemovePlayer (0x054e)
- **Direction:** incoming
- **Structure:** ExpeditionCommand_Struct

**Structure Definition:**
```cpp
struct ExpeditionCommand_Struct
{
/*000*/ uint32 unknown000;
/*004*/ uint32 unknown004;
/*008*/ char   name[64];
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_DzRemovePlayer)
	{
		DECODE_LENGTH_EXACT(structs::ExpeditionCommand_Struct);
		SETUP_DIRECT_DECODE(ExpeditionCommand_Struct, structs::ExpeditionCommand_Struct);

		strn0cpy(emu->name, eq->name, sizeof(emu->name));

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_DzSwapPlayer (0x4661)
- **Direction:** incoming
- **Structure:** ExpeditionCommandSwap_Struct

**Structure Definition:**
```cpp
struct ExpeditionCommandSwap_Struct
{
/*000*/ uint32 unknown000;
/*004*/ uint32 unknown004;
/*008*/ char   add_player_name[64]; // swap to (player must confirm)
/*072*/ char   rem_player_name[64]; // swap from
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_DzSwapPlayer)
	{
		DECODE_LENGTH_EXACT(structs::ExpeditionCommandSwap_Struct);
		SETUP_DIRECT_DECODE(ExpeditionCommandSwap_Struct, structs::ExpeditionCommandSwap_Struct);

		strn0cpy(emu->add_player_name, eq->add_player_name, sizeof(emu->add_player_name));
		strn0cpy(emu->rem_player_name, eq->rem_player_name, sizeof(emu->rem_player_name));

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_Emote (0x3164)
- **Direction:** incoming
- **Structure:** Emote_Struct

**Structure Definition:**
```cpp
struct Emote_Struct {
/*0000*/	uint32 unknown01;
/*0004*/	char message[1024]; // was 1024
/*1028*/
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_Emote)
	{
		unsigned char *__eq_buffer = __packet->pBuffer;

		std::string old_message = (char *)&__eq_buffer[4]; // unknown01 offset
		std::string new_message;
		UFToServerSayLink(new_message, old_message);

		__packet->size = sizeof(Emote_Struct);
		__packet->pBuffer = new unsigned char[__packet->size];

		char *InBuffer = (char *)__packet->pBuffer;

		memcpy(InBuffer, __eq_buffer, 4);
		InBuffer += 4;
		strcpy(InBuffer, new_message.substr(0, 1023).c_str());
		InBuffer[1023] = '\0';

		delete[] __eq_buffer;
	}
```

---

## OP_EnvDamage (0x2730)
- **Direction:** incoming
- **Structure:** EnvDamage2_Struct

**Structure Definition:**
```cpp
struct EnvDamage2_Struct {
/*0000*/	uint32 id;
/*0004*/	uint16 unknown4;
/*0006*/	uint32 damage;
/*0010*/	float unknown10;	// New to Underfoot - Seen 1
/*0014*/	uint8 unknown14[12];
/*0026*/	uint8 dmgtype;		// FA = Lava; FC = Falling
/*0027*/	uint8 unknown27[4];
/*0031*/	uint16 unknown31;	// New to Underfoot - Seen 66
/*0033*/	uint16 constant;		// Always FFFF
/*0035*/	uint16 unknown35;
/*0037*/
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_EnvDamage)
	{
		DECODE_LENGTH_EXACT(structs::EnvDamage2_Struct);
		SETUP_DIRECT_DECODE(EnvDamage2_Struct, structs::EnvDamage2_Struct);

		IN(id);
		IN(damage);
		IN(dmgtype);
		emu->constant = 0xFFFF;

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_FaceChange (0x37a7)
- **Direction:** incoming
- **Structure:** FaceChange_Struct

**Structure Definition:**
```cpp
struct FaceChange_Struct {
/*000*/	uint8	haircolor;
/*001*/	uint8	beardcolor;
/*002*/	uint8	eyecolor1;
/*003*/	uint8	eyecolor2;
/*004*/	uint8	hairstyle;
/*005*/	uint8	beard;
/*006*/	uint8	face;
/*007*/ uint8   unused_padding;
/*008*/ uint32	drakkin_heritage;
/*012*/ uint32	drakkin_tattoo;
/*016*/ uint32	drakkin_details;
/*020*/ uint32  entity_id;
/*024*/
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_FaceChange)
	{
		DECODE_LENGTH_EXACT(structs::FaceChange_Struct);
		SETUP_DIRECT_DECODE(FaceChange_Struct, structs::FaceChange_Struct);

		IN(haircolor);
		IN(beardcolor);
		IN(eyecolor1);
		IN(eyecolor2);
		IN(hairstyle);
		IN(beard);
		IN(face);
		IN(drakkin_heritage);
		IN(drakkin_tattoo);
		IN(drakkin_details);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_FindPersonRequest (0x1e04)
- **Direction:** incoming
- **Structure:** FindPersonRequest_Struct

**Structure Definition:**
```cpp
struct FindPersonRequest_Struct
{
/*000*/	uint32	unknown000;
/*004*/	uint32	npc_id;
/*008*/	FindPerson_Point client_pos;
/*020*/	uint32	unknown020;
/*024*/
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_FindPersonRequest)
	{
		DECODE_LENGTH_EXACT(structs::FindPersonRequest_Struct);
		SETUP_DIRECT_DECODE(FindPersonRequest_Struct, structs::FindPersonRequest_Struct);

		IN(npc_id);
		IN(client_pos.x);
		IN(client_pos.y);
		IN(client_pos.z);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_GroupCancelInvite (0x2736)
- **Direction:** incoming
- **Structure:** GroupCancel_Struct

**Structure Definition:**
```cpp
struct GroupCancel_Struct {
/*000*/	char	name1[64];
/*064*/	char	name2[64];
/*128*/	uint8	unknown128[20];
/*148*/	uint32	toggle;
/*152*/
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_GroupCancelInvite)
	{
		DECODE_LENGTH_EXACT(structs::GroupCancel_Struct);
		SETUP_DIRECT_DECODE(GroupCancel_Struct, structs::GroupCancel_Struct);

		memcpy(emu->name1, eq->name1, sizeof(emu->name1));
		memcpy(emu->name2, eq->name2, sizeof(emu->name2));
		IN(toggle);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_GroupDisband (0x54e8)
- **Direction:** incoming
- **Structure:** GroupGeneric_Struct

**Structure Definition:**
```cpp
struct GroupGeneric_Struct {
/*0000*/	char	name1[64];
/*0064*/	char	name2[64];
/*0128*/	uint32	unknown0128;
/*0132*/	uint32	unknown0132;
/*0136*/	uint32	unknown0136;
/*0140*/	uint32	unknown0140;
/*0144*/	uint32	unknown0144;
/*0148*/
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_GroupDisband)
	{
		//EQApplicationPacket *in = __packet;
		//Log.LogDebugType(Logs::General, Logs::Netcode, "[ERROR] Received incoming OP_Disband");
		//Log.Hex(Logs::Netcode, in->pBuffer, in->size);
		DECODE_LENGTH_EXACT(structs::GroupGeneric_Struct);
		SETUP_DIRECT_DECODE(GroupGeneric_Struct, structs::GroupGeneric_Struct);

		memcpy(emu->name1, eq->name1, sizeof(emu->name1));
		memcpy(emu->name2, eq->name2, sizeof(emu->name2));

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_GroupFollow (0x7f2b)
- **Direction:** incoming
- **Structure:** GroupFollow_Struct

**Structure Definition:**
```cpp
struct GroupFollow_Struct { // Underfoot Follow Struct
/*0000*/	char	name1[64];	// inviter
/*0064*/	char	name2[64];	// invitee
/*0128*/	uint32	unknown0128;	// Seen 0
/*0132*/	uint32	unknown0132;	// Group ID or member level?
/*0136*/	uint32	unknown0136;	// Maybe Voice Chat Channel or Group ID?
/*0140*/	uint32	unknown0140;	// Seen 0
/*0144*/	uint32	unknown0144;	// Seen 0
/*0148*/	uint32	unknown0148;
/*0152*/
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_GroupFollow)
	{
		//EQApplicationPacket *in = __packet;
		//Log.LogDebugType(Logs::General, Logs::Netcode, "[ERROR] Received incoming OP_GroupFollow");
		//Log.Hex(Logs::Netcode, in->pBuffer, in->size);
		DECODE_LENGTH_EXACT(structs::GroupFollow_Struct);
		SETUP_DIRECT_DECODE(GroupGeneric_Struct, structs::GroupFollow_Struct);

		memcpy(emu->name1, eq->name1, sizeof(emu->name1));
		memcpy(emu->name2, eq->name2, sizeof(emu->name2));

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_GroupFollow2 (0x6c16)
- **Direction:** incoming
- **Structure:** GroupFollow_Struct

**Structure Definition:**
```cpp
struct GroupFollow_Struct { // Underfoot Follow Struct
/*0000*/	char	name1[64];	// inviter
/*0064*/	char	name2[64];	// invitee
/*0128*/	uint32	unknown0128;	// Seen 0
/*0132*/	uint32	unknown0132;	// Group ID or member level?
/*0136*/	uint32	unknown0136;	// Maybe Voice Chat Channel or Group ID?
/*0140*/	uint32	unknown0140;	// Seen 0
/*0144*/	uint32	unknown0144;	// Seen 0
/*0148*/	uint32	unknown0148;
/*0152*/
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_GroupFollow2)
	{
		//EQApplicationPacket *in = __packet;
		//Log.LogDebugType(Logs::General, Logs::Netcode, "[ERROR] Received incoming OP_GroupFollow2");
		//Log.Hex(Logs::Netcode, in->pBuffer, in->size);
		DECODE_LENGTH_EXACT(structs::GroupFollow_Struct);
		SETUP_DIRECT_DECODE(GroupGeneric_Struct, structs::GroupFollow_Struct);

		memcpy(emu->name1, eq->name1, sizeof(emu->name1));
		memcpy(emu->name2, eq->name2, sizeof(emu->name2));

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_GroupInvite (0x4f60)
- **Direction:** incoming
- **Structure:** GroupInvite_Struct

**Structure Definition:**
```cpp
struct GroupInvite_Struct {
/*0000*/	char	invitee_name[64];
/*0064*/	char	inviter_name[64];
/*0128*/	uint32	unknown0128;
/*0132*/	uint32	unknown0132;
/*0136*/	uint32	unknown0136;
/*0140*/	uint32	unknown0140;
/*0144*/	uint32	unknown0144;
/*0148*/
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_GroupInvite)
	{
		//EQApplicationPacket *in = __packet;
		//Log.LogDebugType(Logs::General, Logs::Netcode, "[ERROR] Received incoming OP_GroupInvite");
		//Log.Hex(Logs::Netcode, in->pBuffer, in->size);
		DECODE_LENGTH_EXACT(structs::GroupInvite_Struct);
		SETUP_DIRECT_DECODE(GroupGeneric_Struct, structs::GroupInvite_Struct);

		memcpy(emu->name1, eq->invitee_name, sizeof(emu->name1));
		memcpy(emu->name2, eq->inviter_name, sizeof(emu->name2));

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_GroupInvite2 (0x5251)
- **Direction:** incoming
- **Structure:** (unknown)

**Structure Definition:**
```cpp
Structure definition not found.
```

**Full incoming Section:**
```cpp
	DECODE(OP_GroupInvite2)
	{
		//Log.LogDebugType(Logs::General, Logs::Netcode, "[ERROR] Received incoming OP_GroupInvite2. Forwarding");
		DECODE_FORWARD(OP_GroupInvite);
	}
```

---

## OP_GuildDemote (0x457d)
- **Direction:** incoming
- **Structure:** GuildDemoteStruct

**Structure Definition:**
```cpp
struct GuildDemoteStruct{
	char	name[64];
	char	target[64];
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_GuildDemote)
	{
		DECODE_LENGTH_EXACT(structs::GuildDemoteStruct);
		SETUP_DIRECT_DECODE(GuildDemoteStruct, structs::GuildDemoteStruct);

		memcpy(emu->name, eq->name, sizeof(emu->name));
		memcpy(emu->target, eq->target, sizeof(emu->target));
		emu->rank = GUILD_MEMBER;

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_GuildTributeDonateItem (0x3683)
- **Direction:** incoming
- **Structure:** GuildTributeDonateItemReply_Struct

**Structure Definition:**
```cpp
struct GuildTributeDonateItemReply_Struct {
/*000*/ uint32	slot;
/*004*/ uint32	quantity;
/*008*/ uint32	unknown8;
/*012*/	uint32	favor;
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_GuildTributeDonateItem)
	{
		DECODE_LENGTH_EXACT(structs::GuildTributeDonateItemRequest_Struct);
		SETUP_DIRECT_DECODE(GuildTributeDonateItemRequest_Struct, structs::GuildTributeDonateItemRequest_Struct);

		Log(Logs::Detail, Logs::Netcode, "UF::DECODE(OP_GuildTributeDonateItem)");

		IN(quantity);
		IN(tribute_master_id);
		IN(guild_id);

		emu->slot = UFToServerSlot(eq->slot);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_InspectRequest (0x7c94)
- **Direction:** incoming
- **Structure:** Inspect_Struct

**Structure Definition:**
```cpp
struct Inspect_Struct {
	uint32 TargetID;
	uint32 PlayerID;
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_InspectRequest)
	{
		DECODE_LENGTH_EXACT(structs::Inspect_Struct);
		SETUP_DIRECT_DECODE(Inspect_Struct, structs::Inspect_Struct);

		IN(TargetID);
		IN(PlayerID);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_ItemLinkClick (0x3c66)
- **Direction:** incoming
- **Structure:** ItemViewRequest_Struct

**Structure Definition:**
```cpp
struct	ItemViewRequest_Struct {
/*000*/	uint32	item_id;
/*004*/	uint32	augments[5];
/*024*/ uint32	link_hash;
/*028*/	uint32	unknown028;	//seems to always be 4 on SoF client
/*032*/	char	unknown032[12];	//probably includes loregroup & evolving info. see Client::MakeItemLink() in zone/inventory.cpp:469
/*044*/	uint16	icon;
/*046*/	char	unknown046[2];
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_ItemLinkClick)
	{
		DECODE_LENGTH_EXACT(structs::ItemViewRequest_Struct);
		SETUP_DIRECT_DECODE(ItemViewRequest_Struct, structs::ItemViewRequest_Struct);
		MEMSET_IN(ItemViewRequest_Struct);

		IN(item_id);
		int r;
		for (r = 0; r < 5; r++) {
			IN(augments[r]);
		}
		IN(link_hash);
		IN(icon);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_ItemVerifyRequest (0x101e)
- **Direction:** incoming
- **Structure:** ItemVerifyRequest_Struct

**Structure Definition:**
```cpp
struct ItemVerifyRequest_Struct {
/*000*/	int32	slot;		// Slot being Right Clicked
/*004*/	uint32	target;		// Target Entity ID
/*008*/
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_ItemVerifyRequest)
	{
		DECODE_LENGTH_EXACT(structs::ItemVerifyRequest_Struct);
		SETUP_DIRECT_DECODE(ItemVerifyRequest_Struct, structs::ItemVerifyRequest_Struct);

		emu->slot = UFToServerSlot(eq->slot);
		IN(target);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_LoadSpellSet (0x6617)
- **Direction:** incoming
- **Structure:** LoadSpellSet_Struct

**Structure Definition:**
```cpp
struct LoadSpellSet_Struct {
      uint8  spell[spells::SPELL_GEM_COUNT];      // 0 if no action
      uint32 unknown;	// there seems to be an extra field in this packet...
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_LoadSpellSet)
	{
		DECODE_LENGTH_EXACT(structs::LoadSpellSet_Struct);
		SETUP_DIRECT_DECODE(LoadSpellSet_Struct, structs::LoadSpellSet_Struct);

		for (unsigned int i = 0; i < EQ::spells::SPELL_GEM_COUNT; ++i)
			if (eq->spell[i] == 0)
				emu->spell[i] = 0xFFFFFFFF;
			else
				emu->spell[i] = eq->spell[i];

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_LootItem (0x5960)
- **Direction:** incoming
- **Structure:** LootingItem_Struct

**Structure Definition:**
```cpp
struct LootingItem_Struct {
/*000*/	uint32	lootee;
/*004*/	uint32	looter;
/*008*/	uint32	slot_id;
/*012*/	int32	auto_loot;
/*016*/	uint32	unknown16;
/*020*/
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_LootItem)
	{
		DECODE_LENGTH_EXACT(structs::LootingItem_Struct);
		SETUP_DIRECT_DECODE(LootingItem_Struct, structs::LootingItem_Struct);

		Log(Logs::Detail, Logs::Netcode, "UF::DECODE(OP_LootItem)");

		IN(lootee);
		IN(looter);
		emu->slot_id = UFToServerCorpseSlot(eq->slot_id);
		IN(auto_loot);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_MoveItem (0x2641)
- **Direction:** incoming
- **Structure:** MoveItem_Struct

**Structure Definition:**
```cpp
struct MoveItem_Struct
{
/*0000*/ uint32	from_slot;
/*0004*/ uint32	to_slot;
/*0008*/ uint32	number_in_stack;
/*0012*/
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_MoveItem)
	{
		DECODE_LENGTH_EXACT(structs::MoveItem_Struct);
		SETUP_DIRECT_DECODE(MoveItem_Struct, structs::MoveItem_Struct);

		Log(Logs::Detail, Logs::Netcode, "UF::DECODE(OP_MoveItem)");

		emu->from_slot = UFToServerSlot(eq->from_slot);
		emu->to_slot = UFToServerSlot(eq->to_slot);
		IN(number_in_stack);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_PetCommands (0x7706)
- **Direction:** incoming
- **Structure:** PetCommand_Struct

**Structure Definition:**
```cpp
struct PetCommand_Struct {
/*000*/ uint32	command;
/*004*/ uint32	target;
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_PetCommands)
	{
		DECODE_LENGTH_EXACT(structs::PetCommand_Struct);
		SETUP_DIRECT_DECODE(PetCommand_Struct, structs::PetCommand_Struct);

		IN(command);
		IN(target);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_RaidInvite (0x60b5)
- **Direction:** incoming
- **Structure:** RaidGeneral_Struct

**Structure Definition:**
```cpp
struct RaidGeneral_Struct {
/*00*/	uint32		action;
/*04*/	char		player_name[64];
/*68*/	uint32		unknown68;
/*72*/	char		leader_name[64];
/*136*/	uint32		parameter;
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_RaidInvite)
	{
		DECODE_LENGTH_ATLEAST(structs::RaidGeneral_Struct);

		RaidGeneral_Struct* rgs = (RaidGeneral_Struct*)__packet->pBuffer;

		switch (rgs->action)
		{
		case raidSetMotd:
		{
			SETUP_VAR_DECODE(RaidMOTD_Struct, structs::RaidMOTD_Struct, motd);

			IN(general.action);
			IN(general.parameter);
			IN_str(general.leader_name);
			IN_str(general.player_name);
			IN_str(motd);

			FINISH_VAR_DECODE();
			break;
		}
		case raidSetNote:
		{
			SETUP_VAR_DECODE(RaidNote_Struct, structs::RaidNote_Struct, note);

			IN(general.action);
			IN(general.parameter);
			IN_str(general.leader_name);
			IN_str(general.player_name);
			IN_str(note);

			FINISH_VAR_DECODE();
			break;
		}
		default:
		{
			SETUP_DIRECT_DECODE(RaidGeneral_Struct, structs::RaidGeneral_Struct);
			IN(action);
			IN(parameter);
			IN_str(leader_name);
			IN_str(player_name);

			FINISH_DIRECT_DECODE();
			break;
		}
		}
	}
```

---

## OP_ReadBook (0x465e)
- **Direction:** incoming
- **Structure:** BookRequest_Struct

**Structure Definition:**
```cpp
struct BookRequest_Struct {
/*0000*/ uint32 window;       // where to display the text (0xFFFFFFFF means new window).
/*0004*/ uint32 invslot;      // The inventory slot the book is in
/*0008*/ uint32 type;         // 0 = Scroll, 1 = Book, 2 = Item Info. Possibly others
/*0012*/ uint32 target_id;
/*0016*/ uint8 can_cast;
/*0017*/ uint8 can_scribe;
/*0018*/ char txtfile[8194];
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_ReadBook)
	{
		DECODE_LENGTH_EXACT(structs::BookRequest_Struct);
		SETUP_DIRECT_DECODE(BookRequest_Struct, structs::BookRequest_Struct);

		IN(type);
		emu->invslot = static_cast<int16_t>(UFToServerSlot(eq->invslot));
		IN(target_id);
		emu->window = (uint8)eq->window;
		strn0cpy(emu->txtfile, eq->txtfile, sizeof(emu->txtfile));

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_Save (0x6618)
- **Direction:** incoming
- **Structure:** Save_Struct

**Structure Definition:**
```cpp
struct Save_Struct {
	uint8	unknown00[192];
	uint8	unknown0192[176];
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_Save)
	{
		DECODE_LENGTH_EXACT(structs::Save_Struct);
		SETUP_DIRECT_DECODE(Save_Struct, structs::Save_Struct);

		memcpy(emu->unknown00, eq->unknown00, sizeof(emu->unknown00));

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_SetServerFilter (0x2d74)
- **Direction:** incoming
- **Structure:** SetServerFilter_Struct

**Structure Definition:**
```cpp
struct SetServerFilter_Struct {
	uint32 filters[34];		//see enum eqFilterType [31]
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_SetServerFilter)
	{
		DECODE_LENGTH_EXACT(structs::SetServerFilter_Struct);
		SETUP_DIRECT_DECODE(SetServerFilter_Struct, structs::SetServerFilter_Struct);

		int r;
		for (r = 0; r < 29; r++) {
			IN(filters[r]);
		}

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_ShopPlayerBuy (0x436a)
- **Direction:** incoming
- **Structure:** Merchant_Sell_Struct

**Structure Definition:**
```cpp
struct Merchant_Sell_Struct {
/*000*/	uint32	npcid;			// Merchant NPC's entity id
/*004*/	uint32	playerid;		// Player's entity id
/*008*/	uint32	itemslot;
/*012*/	uint32	unknown12;
/*016*/	uint32	quantity;
/*020*/	uint32	Unknown020;
/*024*/	uint32	price;
/*028*/	uint32	pricehighorderbits;	// It appears the price is 64 bits in Underfoot+
/*032*/
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_ShopPlayerBuy)
	{
		DECODE_LENGTH_EXACT(structs::Merchant_Sell_Struct);
		SETUP_DIRECT_DECODE(Merchant_Sell_Struct, structs::Merchant_Sell_Struct);

		IN(npcid);
		IN(playerid);
		IN(itemslot);
		IN(quantity);
		IN(price);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_ShopPlayerSell (0x0b27)
- **Direction:** incoming
- **Structure:** Merchant_Purchase_Struct

**Structure Definition:**
```cpp
struct Merchant_Purchase_Struct {
/*000*/	uint32	npcid;			// Merchant NPC's entity id
/*004*/	uint32	itemslot;		// Player's entity id
/*008*/	uint32	quantity;
/*012*/	uint32	price;
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_ShopPlayerSell)
	{
		DECODE_LENGTH_EXACT(structs::Merchant_Purchase_Struct);
		SETUP_DIRECT_DECODE(Merchant_Purchase_Struct, structs::Merchant_Purchase_Struct);

		IN(npcid);
		emu->itemslot = UFToServerSlot(eq->itemslot);
		IN(quantity);
		IN(price);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_ShopRequest (0x442a)
- **Direction:** incoming
- **Structure:** MerchantClick_Struct

**Structure Definition:**
```cpp
struct MerchantClick_Struct {
/*000*/ uint32	npc_id;			// Merchant NPC's entity id
/*004*/ uint32	player_id;
/*008*/ uint32	command;		//1=open, 0=cancel/close
/*012*/ float	rate;			//cost multiplier, dosent work anymore
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_ShopRequest)
	{
		DECODE_LENGTH_EXACT(structs::MerchantClick_Struct);
		SETUP_DIRECT_DECODE(MerchantClick_Struct, structs::MerchantClick_Struct);

		IN(npc_id);
		IN(player_id);
		IN(command);
		IN(rate);
		emu->tab_display = 0;
		emu->unknown020 = 0;

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_Trader (0x0c08)
- **Direction:** incoming
- **Structure:** Trader_ShowItems_Struct

**Structure Definition:**
```cpp
struct Trader_ShowItems_Struct{
	uint32 action;
	uint32 entity_id;
	uint32 unknown08[3];
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_Trader)
	{
		auto action = (uint32) __packet->pBuffer[0];

		switch (action) {
			case structs::UFBazaarTraderBuyerActions::BeginTraderMode: {
				DECODE_LENGTH_EXACT(structs::BeginTrader_Struct);
				SETUP_DIRECT_DECODE(ClickTrader_Struct, structs::BeginTrader_Struct);
				LogTrading(
					"Decode OP_Trader BeginTraderMode action <red>[{}]",
					action
				);

				emu->action      = TraderOn;
				emu->unknown_004 = 0;
				std::copy_n(eq->serial_number, UF::invtype::BAZAAR_SIZE, emu->serial_number);
				std::copy_n(eq->cost, UF::invtype::BAZAAR_SIZE, emu->item_cost);

				FINISH_DIRECT_DECODE();
				break;
			}
			case structs::UFBazaarTraderBuyerActions::EndTraderMode: {
				DECODE_LENGTH_EXACT(structs::Trader_ShowItems_Struct);
				SETUP_DIRECT_DECODE(Trader_ShowItems_Struct, structs::Trader_ShowItems_Struct);
				LogTrading(
					"Decode OP_Trader(UF) EndTraderMode action <red>[{}]",
					action
				);

				emu->action = TraderOff;
				IN(entity_id);

				FINISH_DIRECT_DECODE();
				break;
			}
			case structs::UFBazaarTraderBuyerActions::PriceUpdate:
			case structs::UFBazaarTraderBuyerActions::ItemMove:
			case structs::UFBazaarTraderBuyerActions::EndTransaction:
			case structs::UFBazaarTraderBuyerActions::ListTraderItems: {
				LogTrading(
					"Decode OP_Trader(UF) Price/ItemMove/EndTransaction/ListTraderItems action <red>[{}]",
					action
				);
				break;
			}
			case structs::UFBazaarTraderBuyerActions::ReconcileItems: {
				break;
			}
			default: {
				LogError("Unhandled(UF) action <red>[{}] received.", action);
			}
		}
	}
```

---

## OP_TraderBuy (0x3672)
- **Direction:** incoming
- **Structure:** TraderBuy_Struct

**Structure Definition:**
```cpp
struct TraderBuy_Struct {
	uint32 action;
	uint32 unknown_004;
	uint32 price;
	uint32 unknown_008;    // Probably high order bits of a 64 bit price.
	uint32 trader_id;
	char   item_name[64];
	uint32 unknown_076;
	uint32 item_id;
	uint32 already_sold;
	uint32 quantity;
	uint32 unknown_092;
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_TraderBuy)
	{
		DECODE_LENGTH_EXACT(structs::TraderBuy_Struct);
		SETUP_DIRECT_DECODE(TraderBuy_Struct, structs::TraderBuy_Struct);
		LogTrading(
			"Decode OP_TraderBuy(UF) item_id <green>[{}] price <green>[{}] quantity <green>[{}] trader_id <green>[{}]",
			eq->item_id,
			eq->price,
			eq->quantity,
			eq->trader_id
		);

		emu->action = BuyTraderItem;
		IN(price);
		IN(trader_id);
		IN(item_id);
		IN(quantity);
		IN(already_sold);
		strn0cpy(emu->item_name, eq->item_name, sizeof(eq->item_name));

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_TraderShop (0x2881)
- **Direction:** incoming
- **Structure:** TraderClick_Struct

**Structure Definition:**
```cpp
struct TraderClick_Struct{
	uint32 trader_id;
	uint32 action;
	uint32 unknown_004;
	uint32 approval;
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_TraderShop)
	{
		DECODE_LENGTH_EXACT(structs::TraderClick_Struct);
		SETUP_DIRECT_DECODE(TraderClick_Struct, structs::TraderClick_Struct);
		LogTrading(
			"(UF) action <green>[{}] trader_id <green>[{}] approval <green>[{}]",
			eq->action,
			eq->trader_id,
			eq->approval
		);

		emu->Code     = ClickTrader;
		emu->TraderID = eq->trader_id;
		emu->Approval = eq->approval;

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_TradeSkillCombine (0x4212)
- **Direction:** incoming
- **Structure:** NewCombine_Struct

**Structure Definition:**
```cpp
struct NewCombine_Struct {
/*00*/	int16	container_slot;
/*02*/	int16	guildtribute_slot;
/*04*/
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_TradeSkillCombine)
	{
		DECODE_LENGTH_EXACT(structs::NewCombine_Struct);
		SETUP_DIRECT_DECODE(NewCombine_Struct, structs::NewCombine_Struct);

		emu->container_slot = UFToServerSlot(eq->container_slot);
		IN(guildtribute_slot);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_TributeItem (0x0b89)
- **Direction:** incoming
- **Structure:** TributeItem_Struct

**Structure Definition:**
```cpp
struct TributeItem_Struct {
	uint32	slot;
	uint32	quantity;
	uint32	tribute_master_id;
	int32	tribute_points;
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_TributeItem)
	{
		DECODE_LENGTH_EXACT(structs::TributeItem_Struct);
		SETUP_DIRECT_DECODE(TributeItem_Struct, structs::TributeItem_Struct);

		emu->slot = UFToServerSlot(eq->slot);
		IN(quantity);
		IN(tribute_master_id);
		IN(tribute_points);

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_WearChange (0x0400)
- **Direction:** incoming
- **Structure:** WearChange_Struct

**Structure Definition:**
```cpp
struct WearChange_Struct{
/*000*/ uint16 spawn_id;
/*002*/ uint32 material;
/*006*/ uint32 unknown06;
/*010*/ uint32 elite_material;	// 1 for Drakkin Elite Material
/*014*/ Tint_Struct color;
/*018*/ uint8 wear_slot_id;
/*019*/
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_WearChange)
	{
		DECODE_LENGTH_EXACT(structs::WearChange_Struct);
		SETUP_DIRECT_DECODE(WearChange_Struct, structs::WearChange_Struct);

		IN(spawn_id);
		IN(material);
		IN(unknown06);
		IN(elite_material);
		IN(color.Color);
		IN(wear_slot_id);
		emu->hero_forge_model = 0;
		emu->unknown18 = 0;

		FINISH_DIRECT_DECODE();
	}
```

---

## OP_WhoAllRequest (0x177a)
- **Direction:** incoming
- **Structure:** Who_All_Struct

**Structure Definition:**
```cpp
struct Who_All_Struct { // 76 length total
/*000*/	char	whom[64];
/*064*/	uint32	wrace;		// FF FF = no race

/*068*/	uint32	wclass;		// FF FF = no class
/*072*/	uint32	lvllow;		// FF FF = no numbers
/*076*/	uint32	lvlhigh;	// FF FF = no numbers
/*080*/	uint32	gmlookup;	// FF FF = not doing /who all gm
/*084*/	uint32	guildid;	// Also used for Buyer/Trader/LFG
/*088*/	uint8	unknown088[64];
/*156*/	uint32	type;		// 0 = /who 3 = /who all
};
```

**Full incoming Section:**
```cpp
	DECODE(OP_WhoAllRequest)
	{
		DECODE_LENGTH_EXACT(structs::Who_All_Struct);
		SETUP_DIRECT_DECODE(Who_All_Struct, structs::Who_All_Struct);

		memcpy(emu->whom, eq->whom, sizeof(emu->whom));
		IN(wrace);
		IN(wclass);
		IN(lvllow);
		IN(lvlhigh);
		IN(gmlookup);
		IN(guildid);
		IN(type);

		FINISH_DIRECT_DECODE();
	}
```

---

