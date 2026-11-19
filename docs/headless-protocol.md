# The headless protocol: reference

Generated from [`common/proto/headless.proto`](../common/proto/headless.proto) by `utils/scripts/headless_proto_doc.py`; don't edit by hand. How the protocol is used (framing, the handshake, the order of the checks) is in [headless.md](headless.md#the-protocol).

Every frame is a 4-byte big-endian length, then the protobuf message. The program sends [ClientFrame](#clientframe); the server sends [ServerFrame](#serverframe).

<details><summary>All messages and enums</summary>

- [ProtocolVersion](#protocolversion)
- [Hello](#hello)
- [Welcome](#welcome)
- [RefusedReason](#refusedreason)
- [Refused](#refused)
- [ChatChannel](#chatchannel)
- [TargetCommand](#targetcommand)
- [AttackCommand](#attackcommand)
- [ChatCommand](#chatcommand)
- [SitCommand](#sitcommand)
- [MoveToCommand](#movetocommand)
- [StopCommand](#stopcommand)
- [InviteCommand](#invitecommand)
- [FaceCommand](#facecommand)
- [DisbandCommand](#disbandcommand)
- [CastCommand](#castcommand)
- [StopCastCommand](#stopcastcommand)
- [MemorizeCommand](#memorizecommand)
- [ConsiderCommand](#considercommand)
- [LootCommand](#lootcommand)
- [ZoneToCommand](#zonetocommand)
- [MerchantCommand](#merchantcommand)
- [GiveCommand](#givecommand)
- [GiveItem](#giveitem)
- [BuyCommand](#buycommand)
- [CoinCommand](#coincommand)
- [SellCommand](#sellcommand)
- [MoveItemCommand](#moveitemcommand)
- [ScribeCommand](#scribecommand)
- [AbilityCommand](#abilitycommand)
- [PetCommand](#petcommand)
- [DisciplineCommand](#disciplinecommand)
- [UseItemCommand](#useitemcommand)
- [ParcelSendCommand](#parcelsendcommand)
- [ParcelRetrieveCommand](#parcelretrievecommand)
- [SurfaceCommand](#surfacecommand)
- [AaExpCommand](#aaexpcommand)
- [ClickDoorCommand](#clickdoorcommand)
- [CorpseDragCommand](#corpsedragcommand)
- [ClickObjectCommand](#clickobjectcommand)
- [CloseObjectCommand](#closeobjectcommand)
- [BuyAaCommand](#buyaacommand)
- [AugmentCommand](#augmentcommand)
- [BuffRemoveCommand](#buffremovecommand)
- [RaidInviteCommand](#raidinvitecommand)
- [RaidRemoveCommand](#raidremovecommand)
- [RaidMoveCommand](#raidmovecommand)
- [HealHoldCommand](#healholdcommand)
- [CombineCommand](#combinecommand)
- [Command](#command)
- [CommandResult](#commandresult)
- [ChatMessage](#chatmessage)
- [ServerMessage](#servermessage)
- [MoveStatus](#movestatus)
- [MoveEvent](#moveevent)
- [Pt](#pt)
- [NavNote](#navnote)
- [ZoneStatus](#zonestatus)
- [ZoneEvent](#zoneevent)
- [ConsiderEvent](#considerevent)
- [LootEvent](#lootevent)
- [MerchantEvent](#merchantevent)
- [Parcel](#parcel)
- [DeathEvent](#deathevent)
- [GameEvent](#gameevent)
- [SkillEvent](#skillevent)
- [Goodbye](#goodbye)
- [ClientFrame](#clientframe)
- [SpawnType](#spawntype)
- [Spawn](#spawn)
- [Self](#self)
- [Gem](#gem)
- [Buff](#buff)
- [BookSpell](#bookspell)
- [SpellBook](#spellbook)
- [Item](#item)
- [BotGear](#botgear)
- [Resists](#resists)
- [OwnedBots](#ownedbots)
- [Inventory](#inventory)
- [GroupMember](#groupmember)
- [RaidMember](#raidmember)
- [ZoneInfo](#zoneinfo)
- [ZoneExit](#zoneexit)
- [Snapshot](#snapshot)
- [StateDelta](#statedelta)
- [ServerFrame](#serverframe)

</details>

### ProtocolVersion

Bumped on incompatible changes. The server refuses other versions.

| Value | # | Notes |
|---|---|---|
| `PROTOCOL_VERSION_UNSPECIFIED` | 0 |  |
| `PROTOCOL_VERSION_CURRENT` | 1 |  |

### Hello

| Field | Type | # | Notes |
|---|---|---|---|
| `token` | string | 1 | issued by the operator: `world headless:token-create` |
| `character` | string | 2 | character to control; must belong to the token's account |
| `sdk_version` | string | 3 | free text, logged |
| `protocol_version` | uint32 | 4 | PROTOCOL_VERSION_CURRENT |

### Welcome

| Field | Type | # | Notes |
|---|---|---|---|
| `session_id` | string | 1 |  |
| `spawn_id` | uint32 | 2 |  |
| `protocol_version` | uint32 | 3 |  |
| `resumed` | bool | 4 | a held session taken over (HeadlessClients:HoldSeconds): the character, its bots and its fights are as they were |

### RefusedReason

| Value | # | Notes |
|---|---|---|
| `REFUSED_UNSPECIFIED` | 0 |  |
| `REFUSED_DISABLED` | 1 | HeadlessClients:AllowHeadlessClients is false |
| `REFUSED_BAD_REQUEST` | 2 | malformed or oversized frame, or first frame wasn't Hello |
| `REFUSED_PROTOCOL_VERSION` | 3 |  |
| `REFUSED_BAD_TOKEN` | 4 | unknown or revoked token |
| `REFUSED_UNKNOWN_CHARACTER` | 5 | no such character on the token's account |
| `REFUSED_LIMIT` | 6 | MaxTotal / MaxPerAccount reached |
| `REFUSED_ALREADY_ONLINE` | 7 |  |
| `REFUSED_ZONE_BLOCKED` | 8 |  |
| `REFUSED_TIMEOUT` | 9 | no Hello within the handshake timeout |
| `REFUSED_NOT_IMPLEMENTED` | 10 | e.g. a feature this server doesn't support yet |
| `REFUSED_ZONE_UNAVAILABLE` | 11 | the character's zone couldn't be started |
| `REFUSED_SPAWN_FAILED` | 12 | the zone couldn't load the character |

### Refused

| Field | Type | # | Notes |
|---|---|---|---|
| `reason` | [RefusedReason](#refusedreason) | 1 |  |
| `detail` | string | 2 | human-readable |

## Commands

 Commands are carried out the way a real client would ask for them: the server builds the packet a client would send and its normal handler runs, with all its checks. A CommandResult with ok=true means "accepted"; the outcome shows up in state (e.g. Self.target_id) and events (e.g. "You cannot see your target.").

### ChatChannel

EQ chat channel numbers

| Value | # | Notes |
|---|---|---|
| `CHAT_GUILD` | 0 |  |
| `CHAT_GROUP` | 2 |  |
| `CHAT_SHOUT` | 3 |  |
| `CHAT_AUCTION` | 4 |  |
| `CHAT_OOC` | 5 |  |
| `CHAT_BROADCAST` | 6 |  |
| `CHAT_TELL` | 7 |  |
| `CHAT_SAY` | 8 |  |
| `CHAT_GMSAY` | 11 |  |
| `CHAT_TELL_ECHO` | 14 |  |
| `CHAT_RAID` | 15 |  |

### TargetCommand

0 clears the target

| Field | Type | # | Notes |
|---|---|---|---|
| `spawn_id` | uint32 | 1 |  |

### AttackCommand

auto-attack

| Field | Type | # | Notes |
|---|---|---|---|
| `on` | bool | 1 |  |

### ChatCommand

| Field | Type | # | Notes |
|---|---|---|---|
| `channel` | [ChatChannel](#chatchannel) | 1 |  |
| `text` | string | 2 | in say, "#..." are GM commands and "^..." bot commands, as for players |
| `to` | string | 3 | tell recipient |

### SitCommand

false stands up

| Field | Type | # | Notes |
|---|---|---|---|
| `sit` | bool | 1 |  |

### MoveToCommand

Walk to a point along the zone's navmesh at the character's legal run speed, sending the same position updates a real client sends. Progress shows in Self; a MoveEvent reports the end. plan_only: don't walk, just answer with a MoveEvent (plan_only set): ARRIVED and the route's length if there's a way there, NO_PATH if not. A walk under way carries on. avoid: keep round the mobs that would aggro us where there's a way (a ZoneTo always does): a corpse run

| Field | Type | # | Notes |
|---|---|---|---|
| `x` | float | 1 |  |
| `y` | float | 2 |  |
| `z` | float | 3 |  |
| `plan_only` | bool | 4 |  |
| `avoid` | bool | 5 |  |

### StopCommand

*(no fields)*

### InviteCommand

group invite, as /invite: a player, or an EQEmu bot you own (HeadlessClients:AllowBots / AllowGroupWithPlayers). Empty name invites the current target.

| Field | Type | # | Notes |
|---|---|---|---|
| `name` | string | 1 |  |

### FaceCommand

turn to face a spawn (0: the target), as /face

| Field | Type | # | Notes |
|---|---|---|---|
| `spawn_id` | uint32 | 1 |  |

### DisbandCommand

*(no fields)*

### CastCommand

cast the spell memorised in a gem (0-based) on your current target, or yourself with none. The module enforces what the client does: not while casting, and each gem's refresh time. spell_id 0: whatever is in the gem

| Field | Type | # | Notes |
|---|---|---|---|
| `gem` | uint32 | 1 |  |
| `spell_id` | uint32 | 2 |  |

### StopCastCommand

interrupt your cast

*(no fields)*

### MemorizeCommand

memorise a spell from your book into a gem; as in the client you sit, and it takes a few seconds. spell_id 0 forgets the gem.

| Field | Type | # | Notes |
|---|---|---|---|
| `gem` | uint32 | 1 |  |
| `spell_id` | uint32 | 2 |  |

### ConsiderCommand

the answer is a ConsiderEvent

| Field | Type | # | Notes |
|---|---|---|---|
| `spawn_id` | uint32 | 1 |  |

### LootCommand

loot everything; answer: LootEvent. peek: take nothing, only see what's on it (LootEvent.left; the coin is taken, as opening it does); only: take just these items (a name each, the rest left on it): a raid's drops shared out by upgrade

| Field | Type | # | Notes |
|---|---|---|---|
| `corpse_id` | uint32 | 1 |  |
| `peek` | bool | 2 |  |
| `only` | repeated string | 3 |  |

### ZoneToCommand

walk to this zone's zone line for `zone` (a short name from ZoneInfo.exits) and through it, as a player does; a ZoneEvent follows. Plain MoveTo stops at zone lines instead. walk through a zone line to a neighbouring zone: the one with the shortest walk from here, or the zone point `number` (zone_points.number: a zone with several entrances to another) plan_only: don't walk, just answer with a MoveEvent (plan_only set), as MoveTo's: the walk to the zone line it would take, its route_length and swim_length (the levitating soldier asks before a leg)

| Field | Type | # | Notes |
|---|---|---|---|
| `zone` | string | 1 |  |
| `number` | uint32 | 2 |  |
| `plan_only` | bool | 3 |  |

### MerchantCommand

merchants, as the client's merchant window: open (stand within ~20 units), buy and sell, close. Opening sends a MerchantEvent with what it sells.

| Field | Type | # | Notes |
|---|---|---|---|
| `npc_id` | uint32 | 1 |  |
| `open` | bool | 2 |  |

### GiveCommand

hand an item (an inventory slot: a stack's quantity, 0 for all of it) to an NPC beside us, as the client's trade window does: the NPC's script takes it (a quest's hand-in, the Priest of Luclin's soulstone) more: further items in the same trade (a hand-in of several: a quest's script looks at the whole trade), up to the window's eight

| Field | Type | # | Notes |
|---|---|---|---|
| `npc_id` | uint32 | 1 |  |
| `slot` | int32 | 2 |  |
| `quantity` | uint32 | 3 |  |
| `more` | repeated [GiveItem](#giveitem) | 4 |  |

### GiveItem

| Field | Type | # | Notes |
|---|---|---|---|
| `slot` | int32 | 1 |  |
| `quantity` | uint32 | 2 |  |

### BuyCommand

| Field | Type | # | Notes |
|---|---|---|---|
| `npc_id` | uint32 | 1 |  |
| `merchant_slot` | uint32 | 2 |  |
| `quantity` | uint32 | 3 |  |

### CoinCommand

coin between what we carry and our bank, beside a banker, as the client's bank window does: this much (in copper's worth; the coins picked platinum first, then gold, silver and copper, as far as they go), to the bank or from it

| Field | Type | # | Notes |
|---|---|---|---|
| `to_bank` | bool | 1 |  |
| `copper` | uint64 | 2 |  |

### SellCommand

slot: inventory slot

| Field | Type | # | Notes |
|---|---|---|---|
| `npc_id` | uint32 | 1 |  |
| `slot` | int32 | 2 |  |
| `quantity` | uint32 | 3 |  |

### MoveItemCommand

pick an item up and put it down elsewhere (equip it, put a bag in a slot, bank it); swaps with what's there. Bank slots (2000+) need a banker within reach, as in the client.

| Field | Type | # | Notes |
|---|---|---|---|
| `from_slot` | int32 | 1 |  |
| `to_slot` | int32 | 2 |  |
| `quantity` | uint32 | 3 |  |

### ScribeCommand

scribe the spell scroll on the cursor into the spellbook (book_slot: a free page), as the client leave the group, as /disband

| Field | Type | # | Notes |
|---|---|---|---|
| `book_slot` | uint32 | 1 |  |
| `spell_id` | uint32 | 2 |  |

### AbilityCommand

a combat ability, as the client's ability buttons send it. skill_id is EQ's skill number: on the target, kick (30), bash (10), backstab (8), the monk strikes (flying kick 26, round kick 38, tiger claw 52, eagle strike 23, dragon punch / tail rake 21), frenzy (74), intimidation (71), taunt (73); on yourself, sneak (42), hide (29), feign death (25). The server applies its own checks and timers.

| Field | Type | # | Notes |
|---|---|---|---|
| `skill_id` | uint32 | 1 |  |

### PetCommand

your pet, as /pet and the pet window. command is EQEmu's pet command number (common/emu_constants.h PetCommand): 2 attack (your target), 3 queue attack, 4 follow, 5 guard here, 6 sit (toggle), 12 taunt (toggle), 13/14 taunt on/off, 15 hold, 21 spell hold, 27 feign, 28 back off, 29 get lost, 31 regroup.

| Field | Type | # | Notes |
|---|---|---|---|
| `command` | uint32 | 1 |  |

### DisciplineCommand

a discipline you know (Self.disciplines), as the combat abilities window: on your target (or you)

| Field | Type | # | Notes |
|---|---|---|---|
| `spell_id` | uint32 | 1 |  |

### UseItemCommand

right-click the item in a slot: its click effect, a potion or food, or a tome (learns its discipline)

| Field | Type | # | Notes |
|---|---|---|---|
| `slot` | int32 | 1 |  |

### ParcelSendCommand

parcels, as the parcel tab of a parcel merchant's window (stand within ~20 units; opening the window lists what's waiting for you, MerchantEvent.parcels). Send the item in a slot (not an attuned one, nor a no-drop one unless the server lets no-drop be traded: World:FVNoDropFlag) to a character by name; the server allows one parcel per Parcel:ParcelDeliveryDelay. Retrieve one of yours into your bags.

| Field | Type | # | Notes |
|---|---|---|---|
| `npc_id` | uint32 | 1 |  |
| `slot` | int32 | 2 |  |
| `quantity` | uint32 | 3 |  |
| `to` | string | 4 |  |
| `note` | string | 5 |  |

### ParcelRetrieveCommand

| Field | Type | # | Notes |
|---|---|---|---|
| `npc_id` | uint32 | 1 |  |
| `parcel_slot` | uint32 | 2 |  |
| `item_id` | uint32 | 3 |  |

### SurfaceCommand

under water: swim straight up to the surface, for air (a MoveEvent when there)

*(no fields)*

### AaExpCommand

| Field | Type | # | Notes |
|---|---|---|---|
| `percent` | uint32 | 1 |  |

### ClickDoorCommand

click a door in this zone by its door id, as the client does: a teleporter or a Plane of Knowledge book takes you where it goes (the server checks the distance, keys and locks)

| Field | Type | # | Notes |
|---|---|---|---|
| `door_id` | uint32 | 1 |  |

### CorpseDragCommand

/corpse drag a corpse by spawn id (yours, or consented), or drop what you drag (corpse_id 0: all) // the share of experience to AA (the client's slider): 100 levels no more

| Field | Type | # | Notes |
|---|---|---|---|
| `corpse_id` | uint32 | 1 |  |
| `drop` | bool | 2 |  |

### ClickObjectCommand

tradeskills: a forge, oven, kiln, brew barrel and so on is a world object (the zone's object table); the items go into it with MoveItem (to its slots, EQ::invslot::WORLD_BEGIN on) and Combine does the rest, as the client's Combine button does open a world container by its object table id (the server checks the distance), as a click on it

| Field | Type | # | Notes |
|---|---|---|---|
| `object_id` | uint32 | 1 |  |

### CloseObjectCommand

close the container opened

*(no fields)*

### BuyAaCommand

buy one rank of an alternate advancement ability with a point, as the client's AA window does (aa_ranks.id: the rank bought, one at a time); refused with why when it can't be bought

| Field | Type | # | Notes |
|---|---|---|---|
| `rank_id` | uint32 | 1 |  |

### AugmentCommand

Augments, as the client's own window does it (OP_AugmentItem): put the augment on the cursor first (MoveItem to slot 33), then Augment. item_slot is the item to be augmented, worn or carried (not banked): an equipment slot, a general slot or a slot in a bag. aug_index is the socket, 0-5. action: 0 insert the augment on the cursor, 1 remove the augment in the socket safely (it comes to the cursor; the augment's own distiller, Item.aug_distiller, must be in solvent_slot, unless that is 0), 2 swap (insert the cursor's augment, the old one comes to the cursor; the distiller as for remove), 3 destroy the augment in the socket. solvent_slot: where the distiller is, or -1. The answer is chat, and the item's new Item.augs in the next inventory frame. The server checks the socket's type against the augment's Item.aug_type (bit n-1 of it: socket type n) and that the augment's worn slots overlap the item's.

| Field | Type | # | Notes |
|---|---|---|---|
| `item_slot` | int32 | 1 |  |
| `aug_index` | uint32 | 2 |  |
| `action` | uint32 | 3 |  |
| `solvent_slot` | int32 | 4 |  |

### BuffRemoveCommand

raids, as the client's raid window. RaidInvite: a player (ungrouped, or a group's leader: the whole group comes in) or one of your bots (ungrouped, or leading a bot group), into your raid; with none, it makes one with you (and your group) in it. A headless character invited by a guildmate (or a GM) accepts by itself, as a rez is accepted; anyone else's invite is left unanswered. RaidRemove: take a member out of the raid (the leader's right; empty name: leave it yourself). RaidMove: put a member in raid group 0-11, or out of every group (12 or more), as dragging a name in the raid window. click a beneficial buff off yourself or one of your bots, by its slot (Buff.slot), as the client's buff window (OP_BuffRemoveRequest). spawn_id 0: yourself. A raid pads its buff windows with junk buffs first (a dispel takes the first slot it can).

| Field | Type | # | Notes |
|---|---|---|---|
| `spawn_id` | uint32 | 1 |  |
| `slot` | uint32 | 2 |  |

### RaidInviteCommand

| Field | Type | # | Notes |
|---|---|---|---|
| `name` | string | 1 |  |

### RaidRemoveCommand

| Field | Type | # | Notes |
|---|---|---|---|
| `name` | string | 1 |  |

### RaidMoveCommand

| Field | Type | # | Notes |
|---|---|---|---|
| `name` | string | 1 |  |
| `group` | uint32 | 2 |  |

### HealHoldCommand

no heal on this character for `seconds` (0 lifts it): a raid puller on a hate list before the main tank has its mob, since a heal on someone hated puts the healer on every hate list that has them (EntityList::AddHealAggro). Bots skip it as a heal target, and their group heals while it's in reach

| Field | Type | # | Notes |
|---|---|---|---|
| `seconds` | uint32 | 1 |  |

### CombineCommand

combine what is in a container: an inventory bag's slot, or 1000 (SLOT_TRADESKILL_EXPERIMENT_COMBINE) for the world container opened; the answer is chat (the skill-ups and the product land in the inventory)

| Field | Type | # | Notes |
|---|---|---|---|
| `container_slot` | int32 | 1 |  |

### Command

| Field | Type | # | Notes |
|---|---|---|---|
| `id` | uint32 | 1 | echoed in CommandResult |
| `target` | [TargetCommand](#targetcommand) | 2 | (one of `action`) |
| `attack` | [AttackCommand](#attackcommand) | 3 | (one of `action`) |
| `chat` | [ChatCommand](#chatcommand) | 4 | (one of `action`) |
| `sit` | [SitCommand](#sitcommand) | 5 | (one of `action`) |
| `move_to` | [MoveToCommand](#movetocommand) | 6 | (one of `action`) |
| `stop` | [StopCommand](#stopcommand) | 7 | (one of `action`) |
| `invite` | [InviteCommand](#invitecommand) | 8 | (one of `action`) |
| `disband` | [DisbandCommand](#disbandcommand) | 9 | (one of `action`) |
| `face` | [FaceCommand](#facecommand) | 10 | (one of `action`) |
| `cast` | [CastCommand](#castcommand) | 11 | (one of `action`) |
| `stop_cast` | [StopCastCommand](#stopcastcommand) | 12 | (one of `action`) |
| `memorize` | [MemorizeCommand](#memorizecommand) | 13 | (one of `action`) |
| `consider` | [ConsiderCommand](#considercommand) | 14 | (one of `action`) |
| `loot` | [LootCommand](#lootcommand) | 15 | (one of `action`) |
| `zone_to` | [ZoneToCommand](#zonetocommand) | 16 | (one of `action`) |
| `merchant` | [MerchantCommand](#merchantcommand) | 17 | (one of `action`) |
| `buy` | [BuyCommand](#buycommand) | 18 | (one of `action`) |
| `sell` | [SellCommand](#sellcommand) | 19 | (one of `action`) |
| `move_item` | [MoveItemCommand](#moveitemcommand) | 20 | (one of `action`) |
| `scribe` | [ScribeCommand](#scribecommand) | 21 | (one of `action`) |
| `ability` | [AbilityCommand](#abilitycommand) | 22 | (one of `action`) |
| `pet` | [PetCommand](#petcommand) | 23 | (one of `action`) |
| `discipline` | [DisciplineCommand](#disciplinecommand) | 24 | (one of `action`) |
| `use_item` | [UseItemCommand](#useitemcommand) | 25 | (one of `action`) |
| `parcel_send` | [ParcelSendCommand](#parcelsendcommand) | 26 | (one of `action`) |
| `parcel_retrieve` | [ParcelRetrieveCommand](#parcelretrievecommand) | 27 | (one of `action`) |
| `surface` | [SurfaceCommand](#surfacecommand) | 28 | (one of `action`) |
| `aa_exp` | [AaExpCommand](#aaexpcommand) | 29 | (one of `action`) |
| `corpse_drag` | [CorpseDragCommand](#corpsedragcommand) | 30 | (one of `action`) |
| `click_door` | [ClickDoorCommand](#clickdoorcommand) | 31 | (one of `action`) |
| `give` | [GiveCommand](#givecommand) | 32 | (one of `action`) |
| `click_object` | [ClickObjectCommand](#clickobjectcommand) | 33 | (one of `action`) |
| `close_object` | [CloseObjectCommand](#closeobjectcommand) | 34 | (one of `action`) |
| `combine` | [CombineCommand](#combinecommand) | 35 | (one of `action`) |
| `buy_aa` | [BuyAaCommand](#buyaacommand) | 36 | (one of `action`) |
| `coin` | [CoinCommand](#coincommand) | 37 | (one of `action`) |
| `augment` | [AugmentCommand](#augmentcommand) | 38 | (one of `action`) |
| `raid_invite` | [RaidInviteCommand](#raidinvitecommand) | 39 | (one of `action`) |
| `raid_remove` | [RaidRemoveCommand](#raidremovecommand) | 40 | (one of `action`) |
| `raid_move` | [RaidMoveCommand](#raidmovecommand) | 41 | (one of `action`) |
| `buff_remove` | [BuffRemoveCommand](#buffremovecommand) | 42 | (one of `action`) |
| `heal_hold` | [HealHoldCommand](#healholdcommand) | 43 | (one of `action`) |

### CommandResult

| Field | Type | # | Notes |
|---|---|---|---|
| `id` | uint32 | 1 |  |
| `ok` | bool | 2 |  |
| `error` | string | 3 |  |

## Events

### ChatMessage

| Field | Type | # | Notes |
|---|---|---|---|
| `channel` | [ChatChannel](#chatchannel) | 1 |  |
| `from` | string | 2 |  |
| `to` | string | 3 | set for tells |
| `text` | string | 4 |  |
| `language` | uint32 | 5 |  |

### ServerMessage

A line of game text. The server sends either plain text (text set) or a client string id with arguments (string_id set), which the client looks up in eqstr_us.txt.

| Field | Type | # | Notes |
|---|---|---|---|
| `type` | uint32 | 1 | chat colour / message type |
| `text` | string | 2 |  |
| `speak_mode` | uint32 | 3 | 0 raw, 1 say, 2 shout, 3 emote-alt, 4 emote, 5 group |
| `sayer` | string | 4 |  |
| `string_id` | uint32 | 5 |  |
| `args` | repeated string | 6 |  |

### MoveStatus

| Value | # | Notes |
|---|---|---|
| `MOVE_ARRIVED` | 0 |  |
| `MOVE_STOPPED` | 1 | StopCommand, a new MoveTo, or the character sat down |
| `MOVE_STUCK` | 2 | no progress (rooted, blocked) |
| `MOVE_NO_PATH` | 3 | no route to the destination |
| `MOVE_ZONE_LINE` | 4 | the route reaches a zone line or teleport; stopped there (zoning is M5) |
| `MOVE_LOCKED` | 5 | a locked door on the way that this character has no key for |

### MoveEvent

| Field | Type | # | Notes |
|---|---|---|---|
| `status` | [MoveStatus](#movestatus) | 1 |  |
| `x` | float | 2 | where the character ended up |
| `y` | float | 3 |  |
| `z` | float | 4 |  |
| `plan_only` | bool | 5 | the answer to a plan_only MoveTo: x, y, z are where we asked about |
| `route_length` | float | 6 | plan_only and ARRIVED: how far the walk would be |
| `nav` | repeated [NavNote](#navnote) | 7 | where this walk found the navmesh wanting (for fixing the zone's navmesh) |
| `swim_length` | float | 8 | plan_only and ARRIVED: how much of the walk is a swim (on the surface) |
| `path` | repeated [Pt](#pt) | 9 | plan_only and ARRIVED: the walk's points, in order (at most about 120: the 3D view's mini map draws the route) |

### Pt

| Field | Type | # | Notes |
|---|---|---|---|
| `x` | float | 1 |  |
| `y` | float | 2 |  |
| `z` | float | 3 |  |

### NavNote

a walk that failed because of the navmesh, or went round a gap in it

| Field | Type | # | Notes |
|---|---|---|---|
| `kind` | string | 1 | no_mesh_here (no navmesh where we stood), no_mesh_there (none at the destination), not_connected (both on the navmesh, no way between: closest is where it gets nearest), stuck (walked into something the navmesh says isn't there), off_mesh (stepped from a hole onto it, to), straight_walk (walked straight to a zone line the navmesh doesn't reach); doors, not the navmesh's: locked (this character can't open it; to: the door), door_wont_open (clicked, and it didn't: a quest's, a trigger's) |
| `x` | float | 2 | where it happened |
| `y` | float | 3 |  |
| `z` | float | 4 |  |
| `to_x` | float | 5 | where we were going (off_mesh: where we stepped to) |
| `to_y` | float | 6 |  |
| `to_z` | float | 7 |  |
| `closest_x` | float | 8 |  |
| `closest_y` | float | 9 |  |
| `closest_z` | float | 10 |  |

### ZoneStatus

| Value | # | Notes |
|---|---|---|
| `ZONE_LEAVING` | 0 | the server let us go; a new Welcome and Snapshot follow once we're in `zone` |
| `ZONE_REFUSED` | 1 | the zone change didn't happen; detail says why |

### ZoneEvent

zoning (e.g. GM #zone, a zone line, a port): the session survives it

| Field | Type | # | Notes |
|---|---|---|---|
| `status` | [ZoneStatus](#zonestatus) | 1 |  |
| `zone_id` | uint32 | 2 |  |
| `zone` | string | 3 | short name |
| `detail` | string | 4 |  |

### ConsiderEvent

| Field | Type | # | Notes |
|---|---|---|---|
| `spawn_id` | uint32 | 1 |  |
| `color` | uint32 | 2 | con colour: 2 green, 4 dark blue, 6 grey, 10 white, 13 red, 15 yellow, 18 light blue |
| `faction` | uint32 | 3 | 1 ally ... 5 indifferent ... 9 scowls |
| `hp` | int32 | 4 |  |
| `max_hp` | int32 | 5 |  |

### LootEvent

| Field | Type | # | Notes |
|---|---|---|---|
| `corpse_id` | uint32 | 1 |  |
| `ok` | bool | 2 |  |
| `error` | string | 3 | why not, when !ok |
| `items` | repeated string | 4 | what came off the corpse |
| `platinum` | uint32 | 5 | coin on the corpse (looted automatically) |
| `gold` | uint32 | 6 |  |
| `silver` | uint32 | 7 |  |
| `copper` | uint32 | 8 |  |
| `left` | repeated string | 9 | what's still on it (no room for it, it wouldn't come, or it wasn't asked for: peek, only) |

### MerchantEvent

| Field | Type | # | Notes |
|---|---|---|---|
| `npc_id` | uint32 | 1 |  |
| `open` | bool | 2 |  |
| `items` | repeated [Item](#item) | 3 | for sale: slot is the merchant slot, price what it costs us (copper) |
| `parcel_merchant` | bool | 4 | it takes and hands out parcels |
| `parcels` | repeated [Parcel](#parcel) | 5 | a parcel merchant: the parcels waiting for us |

### Parcel

a parcel waiting at the parcel merchants (ParcelRetrieveCommand takes slot and item_id)

| Field | Type | # | Notes |
|---|---|---|---|
| `slot` | uint32 | 1 |  |
| `item_id` | uint32 | 2 | 99990 is coin (quantity: copper) |
| `name` | string | 3 |  |
| `quantity` | uint32 | 4 |  |
| `from` | string | 5 |  |
| `note` | string | 6 |  |
| `sent` | int64 | 7 | unix time |

### DeathEvent

| Field | Type | # | Notes |
|---|---|---|---|
| `killer` | string | 1 |  |
| `corpse_id` | uint32 | 2 |  |
| `spell_id` | uint32 | 3 | 0 if not a spell |
| `damage` | uint32 | 4 |  |

### GameEvent

| Field | Type | # | Notes |
|---|---|---|---|
| `chat` | [ChatMessage](#chatmessage) | 1 | (one of `event`) |
| `message` | [ServerMessage](#servermessage) | 2 | (one of `event`) |
| `move` | [MoveEvent](#moveevent) | 3 | (one of `event`) |
| `zone` | [ZoneEvent](#zoneevent) | 4 | (one of `event`) |
| `consider` | [ConsiderEvent](#considerevent) | 5 | (one of `event`) |
| `loot` | [LootEvent](#lootevent) | 6 | (one of `event`) |
| `death` | [DeathEvent](#deathevent) | 7 | (one of `event`) ours; the server then sends us to our bind point (a zone change) |
| `merchant` | [MerchantEvent](#merchantevent) | 8 | (one of `event`) |
| `skill` | [SkillEvent](#skillevent) | 9 | (one of `event`) a skill (or language) went up: the client prints "You have become better at ..." |

### SkillEvent

| Field | Type | # | Notes |
|---|---|---|---|
| `skill_id` | uint32 | 1 | EQ skill id; 100 and up are languages (100 + language id) |
| `name` | string | 2 | "1H Blunt", "Abjuration", "Common Tongue", ... |
| `value` | uint32 | 3 | the new value |

### Goodbye

client -> server the program ends the session on purpose: the character is logged out, not held (HeadlessClients:HoldSeconds holds a character only when its program went away without saying so: a crash or a restart)

*(no fields)*

### ClientFrame

| Field | Type | # | Notes |
|---|---|---|---|
| `hello` | [Hello](#hello) | 1 | (one of `payload`) |
| `command` | [Command](#command) | 2 | (one of `payload`) |
| `goodbye` | [Goodbye](#goodbye) | 3 | (one of `payload`) |

## State

 After Welcome the server sends one Snapshot, then StateDelta messages (a few per second) whenever something changed. Positions are in-game coordinates (the same as /loc, which prints y before x). Headings use EQ's 0-512 range.

### SpawnType

| Value | # | Notes |
|---|---|---|
| `SPAWN_TYPE_UNKNOWN` | 0 |  |
| `SPAWN_TYPE_PLAYER` | 1 | real or headless player character |
| `SPAWN_TYPE_NPC` | 2 | includes pets and mercenaries |
| `SPAWN_TYPE_BOT` | 3 | EQEmu bot |
| `SPAWN_TYPE_PLAYER_CORPSE` | 4 |  |
| `SPAWN_TYPE_NPC_CORPSE` | 5 |  |

### Spawn

| Field | Type | # | Notes |
|---|---|---|---|
| `id` | uint32 | 1 |  |
| `name` | string | 2 | clean name, e.g. "a_rat" -> "a rat" |
| `type` | [SpawnType](#spawntype) | 3 |  |
| `level` | uint32 | 4 |  |
| `class_id` | uint32 | 5 |  |
| `race_id` | uint32 | 6 |  |
| `x` | float | 7 |  |
| `y` | float | 8 |  |
| `z` | float | 9 |  |
| `heading` | float | 10 |  |
| `hp_pct` | uint32 | 11 | 0-100 |
| `owner_id` | uint32 | 12 | pet / bot owner spawn id, 0 if none (bots: their owning character) |
| `headless` | bool | 13 | a headless (AI-controlled) character |
| `target_id` | uint32 | 14 | who it has targeted (for NPCs in combat: who it's hitting), 0 if none |
| `mana_pct` | uint32 | 15 | 0-100 (0 for spawns without mana) |
| `gender` | uint32 | 16 | 0 male, 1 female, 2 neutral (the model to draw) |
| `primary` | uint32 | 17 | the model of what it holds in its primary hand (IT<n>; for an NPC its d_melee_texture1), 0 if none |
| `secondary` | uint32 | 18 | ... and in its secondary |
| `casting_spell_id` | uint32 | 19 | the spell it is casting, 0 if none (to interrupt a caster) |
| `status` | uint32 | 20 | what is on it: 1 mezzed, 2 rooted, 4 stunned, 8 feared or fleeing, 16 charmed (to know a mob is parked, and that a root has broken) |
| `aggro_range` | float | 21 | what an NPC does about us (the character this stream is for), by the server's own aggro rules (Mob::CheckWillAggro): to route round it and to pull without waking it the radius it notices us in (a sphere round it); 0 for what never aggros (a pet, a merchant's guard that doesn't) |
| `assist_range` | float | 22 | the radius in which it calls others to help (Mob::GetAssistRange: its assist radius, else its aggro radius) |
| `aggro` | uint32 | 23 | bits, for an NPC with an aggro range: 1 hostile on sight (its faction toward us scowls: KOS), 2 may aggro (threatening: a quarter chance each look), |
| `buffs` | repeated [Buff](#buff) | 24 | 4 aggros even a character it cons grey to (always-aggro, undead, or at its level of aggro: the rule Aggro:MinAggroLevel). Invisible, we are not seen. what is on it (our slows, roots, mezzes, damage over time...), for an NPC that is fighting or has us on its hate list: what a fight is doing to it |
| `bot_skip` | uint32 | 25 | one of our own bots: why its AI last didn't cast what it wanted to, in the last 3 seconds (a heal's refusal outranks any other's): 0 nothing refused, 1 incapacitated (stunned, mezzed, paused), 2 held (^hold, or raid-groupless), 3 catching up with us (Bots:CatchUpDistance), 4 not enough mana, 5 the target out of the spell's range, 6 no line of sight, 7 the spell's recast, 8 we lack its reagents (Bots:UseOwnerReagents), 9 it would pull aggro, 11 silenced or amnesiad. What a raid's post-mortem counts: "this cleric skipped its heals, out of range" |
| `bot_skip_spell_type` | uint32 | 26 | the bot spell type refused (BotSpellTypes: 0 nuke, 1 regular heal, ...), 65535 when the reason isn't a spell's |

### Self

| Field | Type | # | Notes |
|---|---|---|---|
| `spawn_id` | uint32 | 1 |  |
| `name` | string | 2 |  |
| `level` | uint32 | 3 |  |
| `class_id` | uint32 | 4 |  |
| `race_id` | uint32 | 5 |  |
| `hp` | int64 | 6 |  |
| `max_hp` | int64 | 7 |  |
| `mana` | int64 | 8 |  |
| `max_mana` | int64 | 9 |  |
| `endurance` | int64 | 10 |  |
| `max_endurance` | int64 | 11 |  |
| `x` | float | 12 |  |
| `y` | float | 13 |  |
| `z` | float | 14 |  |
| `heading` | float | 15 |  |
| `target_id` | uint32 | 16 | 0 if none |
| `sitting` | bool | 17 |  |
| `auto_attack` | bool | 18 |  |
| `casting_spell_id` | uint32 | 19 | 0 if not casting |
| `group` | repeated [GroupMember](#groupmember) | 20 | empty when not grouped; includes ourselves |
| `gems` | repeated [Gem](#gem) | 21 | memorised spells |
| `buffs` | repeated [Buff](#buff) | 22 |  |
| `platinum` | uint32 | 23 | carried coin |
| `gold` | uint32 | 24 |  |
| `silver` | uint32 | 25 |  |
| `copper` | uint32 | 26 |  |
| `haters` | repeated uint32 | 27 | NPCs with us on their hate list: we're engaged while any are |
| `weight` | uint32 | 28 | carried weight (as the client shows it) |
| `capacity` | uint32 | 29 | carry more than this (= STR) and you're encumbered |
| `race_bit` | uint32 | 30 | our bit in Item.races |
| `exp_pct` | float | 31 | progress through the current level, 0-100 |
| `aa_exp_pct` | float | 32 | progress to the next AA point, 0-100 |
| `aa_points` | uint32 | 33 | unspent AA points |
| `aa_split` | uint32 | 34 | share of experience going to AA, 0-100 |
| `run_speed` | float | 35 | how fast a move_to walks now, units per second (buffs, snares, GM speed; 0 rooted) |
| `disciplines` | repeated uint32 | 36 | spell ids of the disciplines you know (DisciplineCommand) |
| `game_hour` | uint32 | 37 | the zone's EQ hour, 1-24 as the client shows it (an EQ hour is 3 real minutes; |
| `air` | uint32 | 38 | some zones spawn differently by night: spawn_events) seconds of air left under water (full whenever you can breathe) |
| `max_air` | uint32 | 39 | seconds of air at most (Stamina, Lung Capacity); at none, you drown |
| `gender` | uint32 | 40 | 0 male, 1 female, 2 neutral |
| `bank_copper` | uint64 | 41 | the coin in our bank, in copper's worth (CoinCommand moves it) |
| `raid` | repeated [RaidMember](#raidmember) | 42 | the raid we are in, everyone in it (ourselves too); empty when not in one |
| `resists` | [Resists](#resists) | 43 | our resists now: base, gear, buffs and AAs, as the client's inventory window shows them |
| `gear_resists` | [Resists](#resists) | 44 | the same without our buffs (base, gear, AAs): what a raid's readiness counts, its own buffs added |

### Gem

| Field | Type | # | Notes |
|---|---|---|---|
| `slot` | uint32 | 1 | 0-based |
| `spell_id` | uint32 | 2 |  |
| `name` | string | 3 |  |
| `ready` | bool | 4 | not refreshing |

### Buff

| Field | Type | # | Notes |
|---|---|---|---|
| `spell_id` | uint32 | 1 |  |
| `name` | string | 2 |  |
| `ticks` | int32 | 3 | 6s ticks left; -1 permanent |
| `slot` | uint32 | 4 | its slot in the buff window (0 first): a dispel takes the first it can, and a new buff goes in the first free one |

### BookSpell

| Field | Type | # | Notes |
|---|---|---|---|
| `spell_id` | uint32 | 1 |  |
| `name` | string | 2 |  |
| `level` | uint32 | 3 | the level our class gets it |
| `slot` | uint32 | 4 | its page in the book |

### SpellBook

| Field | Type | # | Notes |
|---|---|---|---|
| `spells` | repeated [BookSpell](#bookspell) | 1 |  |

### Item

| Field | Type | # | Notes |
|---|---|---|---|
| `slot` | int32 | 1 | EQEmu inventory slot id |
| `item_id` | uint32 | 2 |  |
| `name` | string | 3 |  |
| `charges` | int32 | 4 |  |
| `container` | bool | 5 |  |
| `ac` | int32 | 6 | what it is, for deciding what to wear, keep and sell |
| `hp` | int32 | 7 |  |
| `mana` | int32 | 8 |  |
| `endurance` | int32 | 9 |  |
| `str` | int32 | 10 |  |
| `sta` | int32 | 11 |  |
| `agi` | int32 | 12 |  |
| `dex` | int32 | 13 |  |
| `wis` | int32 | 14 |  |
| `int` | int32 | 15 |  |
| `cha` | int32 | 16 |  |
| `damage` | uint32 | 17 |  |
| `delay` | uint32 | 18 |  |
| `slots` | uint32 | 19 | bitmask of worn slots it fits (bit n = slot n) |
| `req_level` | uint32 | 20 |  |
| `weight` | float | 21 |  |
| `price` | uint32 | 22 | base value in copper (merchant items: what it costs us) |
| `no_drop` | bool | 23 |  |
| `lore` | bool | 24 |  |
| `magic` | bool | 25 |  |
| `bag_slots` | uint32 | 26 | containers |
| `bag_size` | uint32 | 27 | biggest item it holds: 0 tiny ... 4 giant |
| `item_type` | uint32 | 28 | EQ item type (skill), e.g. 0 1H slashing, 10 armour, 11 misc |
| `usable` | bool | 29 | its holder (us, or the bot wearing it) can equip it at its level |
| `classes` | uint32 | 30 | bitmask: bit (class - 1) |
| `races` | uint32 | 31 | bitmask of player races (EQ race bits: human 1, barbarian 2, ... dwarf 128 ...) |
| `scroll_spell` | uint32 | 32 | a spell scroll: the spell it teaches |
| `scroll_level` | uint32 | 33 | ... and the level our class can learn it at (0: not our class) |
| `bag_wr` | uint32 | 34 | containers: weight reduction, percent |
| `bag_type` | uint32 | 35 | containers: EQ bag type (0-8 storage: bag, chest, quiver...; 9 and up tradeskill: mixing bowl, kiln...) |
| `aug_type` | uint32 | 36 | augments (AugmentCommand). The stats above are the item's own; an augment's are its own, added where it sits. an augment: the socket types it fits, a bitmask (bit n-1: type n); 0 for anything else |
| `aug_restrict` | uint32 | 37 | an augment: what it may go in (0 anything, 1 armour, 2 weapons, 3 one-handed, 4 two-handed ... EQ's list) |
| `aug_distiller` | uint32 | 38 | an augment: the item that removes it without destroying it (0: no distiller needed) |
| `aug_sockets` | repeated uint32 | 39 | an item with sockets: the type of each of its six (0: no socket there); empty if it has none |
| `augs` | repeated uint32 | 40 | an item with sockets: the item id in each of the six (0: empty); only on items that have one in |

### BotGear

our EQEmu bots and what they wear; we equip them (^inventorygive)

| Field | Type | # | Notes |
|---|---|---|---|
| `spawn_id` | uint32 | 1 |  |
| `name` | string | 2 |  |
| `class_id` | uint32 | 3 |  |
| `race_id` | uint32 | 4 |  |
| `level` | uint32 | 5 |  |
| `race_bit` | uint32 | 6 | its bit in Item.races |
| `equipment` | repeated [Item](#item) | 7 | slot is the worn slot |
| `resists` | [Resists](#resists) | 8 | its resists now (gear and buffs) |
| `buffs` | repeated [Buff](#buff) | 9 | what is on it now, with each buff's slot (seen in a raid too, where Self.group is empty) |
| `gear_resists` | [Resists](#resists) | 10 | its resists without its buffs (base, gear, AAs) |

### Resists

a character's or a bot's resists (what a raid target's spells are checked against: a dragon's breath against cold, its fear against magic)

| Field | Type | # | Notes |
|---|---|---|---|
| `magic` | int32 | 1 |  |
| `cold` | int32 | 2 |  |
| `fire` | int32 | 3 |  |
| `poison` | int32 | 4 |  |
| `disease` | int32 | 5 |  |

### OwnedBots

| Field | Type | # | Notes |
|---|---|---|---|
| `bots` | repeated [BotGear](#botgear) | 1 |  |

### Inventory

carried and banked (bank slots 2000-2023 and their bags)

| Field | Type | # | Notes |
|---|---|---|---|
| `items` | repeated [Item](#item) | 1 |  |

### GroupMember

| Field | Type | # | Notes |
|---|---|---|---|
| `name` | string | 1 |  |
| `spawn_id` | uint32 | 2 | 0 if not in this zone |
| `leader` | bool | 3 |  |
| `bot` | bool | 4 | an EQEmu bot |
| `buffs` | repeated [Buff](#buff) | 5 | what is on them now (empty if they are not in this zone) |

### RaidMember

| Field | Type | # | Notes |
|---|---|---|---|
| `name` | string | 1 |  |
| `spawn_id` | uint32 | 2 | 0 if not in this zone |
| `group` | uint32 | 3 | raid group 0-11; 12 or more: in no group |
| `group_leader` | bool | 4 |  |
| `raid_leader` | bool | 5 |  |
| `bot` | bool | 6 | an EQEmu bot |
| `class_id` | uint32 | 7 |  |
| `level` | uint32 | 8 |  |

### ZoneInfo

| Field | Type | # | Notes |
|---|---|---|---|
| `id` | uint32 | 1 |  |
| `instance_id` | uint32 | 2 |  |
| `short_name` | string | 3 |  |
| `long_name` | string | 4 |  |
| `exits` | repeated [ZoneExit](#zoneexit) | 5 | zone lines out of this zone |

### ZoneExit

| Field | Type | # | Notes |
|---|---|---|---|
| `zone` | string | 1 | short name |
| `zone_id` | uint32 | 2 |  |
| `x` | float | 3 | where the zone line is; a coordinate of +-999999 means "anywhere along this axis" |
| `y` | float | 4 |  |
| `z` | float | 5 |  |

### Snapshot

| Field | Type | # | Notes |
|---|---|---|---|
| `me` | [Self](#self) | 1 |  |
| `zone` | [ZoneInfo](#zoneinfo) | 2 |  |
| `spawns` | repeated [Spawn](#spawn) | 3 | everyone else in the zone |
| `book` | [SpellBook](#spellbook) | 4 |  |
| `inventory` | [Inventory](#inventory) | 5 |  |
| `bots` | [OwnedBots](#ownedbots) | 6 |  |

### StateDelta

| Field | Type | # | Notes |
|---|---|---|---|
| `me` | [Self](#self) | 1 | always the full Self (small) |
| `spawns_changed` | repeated [Spawn](#spawn) | 2 | added or changed spawns, full records |
| `spawns_removed` | repeated uint32 | 3 |  |
| `book` | [SpellBook](#spellbook) | 4 | only when it changed |
| `inventory` | [Inventory](#inventory) | 5 | only when it changed |
| `bots` | [OwnedBots](#ownedbots) | 6 | only when it changed |

### ServerFrame

server -> client

| Field | Type | # | Notes |
|---|---|---|---|
| `welcome` | [Welcome](#welcome) | 1 | (one of `payload`) |
| `refused` | [Refused](#refused) | 2 | (one of `payload`) |
| `snapshot` | [Snapshot](#snapshot) | 3 | (one of `payload`) |
| `delta` | [StateDelta](#statedelta) | 4 | (one of `payload`) |
| `event` | [GameEvent](#gameevent) | 5 | (one of `payload`) |
| `result` | [CommandResult](#commandresult) | 6 | (one of `payload`) |
