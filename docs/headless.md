# Headless characters

Headless characters are EverQuest characters with no game client. Each one is an ordinary `Client`
inside the zone process, so the game treats it as a player in every way: it has a place in the
world, fights, casts, loots, groups, zones and saves as a player does. What drives it is a program,
connected to `world` over TCP, instead of someone at a keyboard.

This lets a server run characters controlled by scripts or AI: helpers, test characters,
companions, or a whole guild of them. It is **opt-in for the server operator**. Nothing works until
the operator builds the module, turns it on and issues tokens. A server that hasn't done all three
can't be scripted this way.

Programs talk to the server through a small protobuf protocol (`common/proto/headless.proto`). A
Python SDK, `eqmq`, wraps it in MacroQuest-style names (`bot.Me.PctHPs`, `bot.Spawn("npc radius 50")`,
`/target`, `#event`). See [Using it from Python](#using-it-from-python).

- [How it works](#how-it-works)
- [Building](#building)
- [Turning it on](#turning-it-on)
- [Tokens and characters](#tokens-and-characters)
- [Rules](#rules)
- [What a headless character can do](#what-a-headless-character-can-do)
- [The protocol](#the-protocol)
- [Safety and fair play](#safety-and-fair-play)
- [Troubleshooting](#troubleshooting)
- [Using it from Python](#using-it-from-python)

## How it works

```
 your program ── TCP 9100 ──> world: HeadlessGateway ── ServerTalk ──> zone: HeadlessClientManager
 (eqmq, or any            handshake, tokens, limits,              a Client on a NullStream,
  protobuf client)        routing, zoning                         state out, commands in
```

- **world** listens for connections (the `HeadlessGateway`). It checks each one's token and
  character, applies the limits, asks the character's zone to spawn it, and relays frames between
  the program and the zone. When the character zones, world performs the handoff as it does for a
  real client. The program's connection stays open throughout and it sees a zone change.
- **zone** creates the character as a `Client` whose network stream is a stub (`NullStream`). The
  stub replays the client's half of the zone-in handshake in memory, so the character enters
  through the server's own code. From then on it is in `entity_list` like any player.
- **Commands are the packets a real client would send.** `target`, `attack`, `cast`, `loot`, `buy`
  and the rest are injected into the stub as the client's packets (`OP_TargetCommand`,
  `OP_CastSpell`, ...), so the server's normal handlers run them with all their usual checks: range,
  line of sight, mana, refresh timers, faction, money. The module adds no new game rules and
  nothing that would have to be kept in step with them.
- **Movement** is the client's job in EverQuest, so the module does it for the character: it follows
  the zone's navmesh at the character's run speed (buffs, snares and GM speed included), keeps to
  the floor, opens doors it has keys for, rides lifts, walks through zone lines, and reports
  positions the way the client does, so the server's own anti-cheat checks still apply.
- **State goes out** four times a second: the character (HP, mana, endurance, position, target,
  buffs, gems, coin, weight, experience, run speed, known disciplines), the zone and its exits, every spawn in the zone,
  the spell book, inventory and bank, and the character's EQEmu bots. The first frame is a full
  snapshot; after that only what changed. Chat, game messages, deaths, skill-ups, merchant windows,
  loot and moves arrive as events.

## Building

The module is on the `headless` branch of this fork. It needs protobuf, which vcpkg already builds
for this tree (`vcpkg.json`), and is controlled by one CMake option:

| Option | Default | |
|---|---|---|
| `EQEMU_BUILD_HEADLESS` | `ON` | Builds the gateway into `world` and the manager into `zone`. `OFF` builds exactly what upstream builds. |

Build as usual. In akk-stack: `make build` in the server container, then restart the server.

Building the module turns nothing on: the rule below is off by default.

## Turning it on

Two switches, both needed.

**1. The listener** (`eqemu_config.json`, next to telnet):

```json
"server": {
  "world": {
    "headless": { "enabled": "true", "ip": "127.0.0.1", "port": "9100" }
  }
}
```

The defaults are `enabled: false` and `127.0.0.1:9100`. Keep it on loopback, or on a private
address your scripts can reach. **Never expose it to the internet**: the tokens are the only
protection on it. In akk-stack, world listens on `0.0.0.0` inside the container and compose publishes
it on `${HEADLESS_IP:-127.0.0.1}:${HEADLESS_PORT:-9100}`.

**2. The rules**, at least these two (the defaults let nothing in):

```sql
INSERT INTO rule_values (ruleset_id, rule_name, rule_value, notes) VALUES
 (1, 'HeadlessClients:AllowHeadlessClients', 'true', ''),
 (1, 'HeadlessClients:MaxTotal', '10', '')
ON DUPLICATE KEY UPDATE rule_value = VALUES(rule_value);
```

Restart world (or `#reload rules`) for rule changes to take effect.

## Tokens and characters

A program logs a character in with a **token**. A token belongs to one account and works only for
that account's characters. Tokens are `hc_` followed by 64 hex characters (32 random bytes). The
server stores only their SHA-256 and shows each token once, when it's made. Revoke one and any
session using it can't log in again.

Everything is done with `world` CLI commands (run in the server directory, e.g. `./bin/world ...`):

| Command | What it does |
|---|---|
| `headless:token-create {account} [--label=text]` | Issues a token for an account and prints it (once). |
| `headless:token-list` | Lists tokens: id, account, label, created, last used, revoked. |
| `headless:token-revoke {token_id}` | Revokes a token. |
| `headless:character-create {account} {name} {race_id} {class_id} {start_zone} [--gender=0\|1] [--deity=id]` | Creates a character as the client's character creation does: start zone, skills, languages, starting items. |
| `headless:guild-create {guild} {leader} [--members=Name,Name,...]` | Creates a guild with the server's default ranks and adds members (restart world and zones to see it). |
| `headless:character-delete {Name,Name,...} [--guild=name]` | Deletes characters for good: their bots, items, corpses and the rest, ignoring `Character:SoftDeletes`. Optionally deletes a guild too. |

Any character on the token's account can be driven, including one made in the game. Give
headless characters accounts of their own, at status 0 unless they genuinely need GM commands.
`HeadlessClients:MaxPerAccount` (default 1) limits how many of an account's characters can be online
at once, the same as one player with one client.

A character can't be online through a real client and a headless session at the same time.

## Rules

Category `HeadlessClients`:

| Rule | Default | Meaning |
|---|---|---|
| `AllowHeadlessClients` | `false` | The master switch. While false, every connection is refused. |
| `MaxTotal` | `0` | Headless characters online at once, server-wide. 0 allows none, so it must be raised on purpose. |
| `MaxPerAccount` | `1` | Headless characters online at once per account. |
| `AllowedZones` | `""` | Comma-separated zone short names they may enter. Empty allows every zone. |
| `BlockedZones` | `""` | Comma-separated zone short names they may never enter (checked at login and when zoning). |
| `AllowBots` | `true` | They may spawn and command EQEmu bots (`^` commands). Also needs `Bots:Enabled`. |
| `AllowMercenaries` | `true` | Reserved: there are no mercenary commands yet. |
| `AllowGroupWithPlayers` | `true` | They may invite real players to their groups. |
| `AllowTradeWithPlayers` | `false` | Reserved: there are no trade commands yet. |
| `AllowTellsFromPlayers` | `true` | Reserved: not enforced yet. |
| `DisplayTag` | `AI` | The suffix shown on headless characters (in their name plate and `/who`). |
| `ShowDisplayTag` | `true` | Show `DisplayTag`. When false it's left off, and removed from characters that already carry it. They stay flagged as headless in the database either way. |
| `CommandRateLimit` | `20` | Commands per second per character. More are refused. |

## What a headless character can do

| Command | |
|---|---|
| `target`, `face`, `attack` | Target by spawn id (with the client's range and line-of-sight checks), face, auto-attack on or off. |
| `chat` | Say, tell, group, guild, raid, shout, OOC, auction. A say starting with `#` is a GM command (if the account's status allows it). One starting with `^` is an EQEmu bot command (if `AllowBots`). |
| `sit` | Sit or stand. |
| `move_to`, `stop` | Walk to a point on the navmesh, or only plan the walk (`plan_only`: the route's length, or no route). |
| `zone_to` | Walk to the zone line for a neighbouring zone and through it. |
| `cast`, `stop_cast`, `memorize`, `scribe` | Cast from a gem, interrupt, memorise a spell, scribe a scroll into the book. |
| `consider` | `/consider` a spawn: its con colour and faction standing. |
| `loot` | Loot a corpse (everything the character may take). |
| `merchant`, `buy`, `sell` | Open a merchant's window, buy, sell. |
| `move_item` | Move items between slots, bags and the bank: equip, unequip, or put something on the cursor to hand to one of the character's bots with `^inventorygive`. |
| `ability` | A combat ability as the client's buttons send it: kick, bash, backstab, the monk strikes, frenzy, intimidation and taunt on the target; sneak, hide and feign death on yourself. |
| `pet` | Command your pet, as `/pet`: attack your target, back off, follow, guard, taunt, sit, hold and the rest. |
| `discipline` | Use a discipline the character knows (they're in its state), as the combat abilities window. |
| `use_item` | Right-click an item: its click effect, a potion or food, or a tome (which teaches its discipline). |
| `parcel_send`, `parcel_retrieve` | Parcels, as a parcel merchant's parcel tab (open the window first, within reach). Send an item to a character by name, with a note; not no-drop or attuned items, and one parcel per `Parcel:ParcelDeliveryDelay`. Opening a parcel merchant lists the parcels waiting for the character (`MerchantEvent.parcels`); retrieve takes one into the bags. Needs `Parcel:EnableParcelMerchants`. |
| `invite`, `disband` | Invite to the group (the character's own bots, or real players if allowed), leave it. |

`CommandResult.ok` means the server accepted the command. What happens next (the spell lands, the
mob dies, the walk arrives) comes back as state and events, just as a player sees it on screen.

Ending a session logs the character out at once: it's saved and removed, with no linkdead time.

## The protocol

One TCP connection per character. Each frame is a 4-byte big-endian length followed by a protobuf
message. Frames are capped at 64 KB. The schema is `common/proto/headless.proto`.

1. The program sends `ClientFrame{hello}`: the token, the character's name, `protocol_version`
   (`PROTOCOL_VERSION_CURRENT`) and free-text `sdk_version`. It has 10 seconds to do so.
2. world answers `ServerFrame{refused}` with a reason and closes, or `ServerFrame{welcome}` with a
   session id and the character's spawn id.
3. From then on the program sends `ClientFrame{command}` (each with an `id`). The server sends
   `result` (per command id), `snapshot` (on entering a zone), `delta` (four times a second, only
   what changed) and `event`.

The checks run in this order: the rule (a disabled server reveals nothing else), the protocol
version, the token, the character, the limits, the zone.

Any language with protobuf can be a client. `eqmq` is the reference one.

## Safety and fair play

- **Off by default, twice over**: the listener (config) and the rule both have to be turned on, and
  `MaxTotal` starts at 0.
- **Tokens are per account** and are stored hashed. Keep the gateway on loopback or a private
  network.
- **No new powers.** Commands go through the same handlers as a real client's packets, movement
  through the server's own anti-cheat checks, and GM commands need the account's GM status as they
  always do.
- **Visible**: `DisplayTag` marks headless characters for everyone, and the database flags them
  even when the tag is hidden.
- **Limits**: characters per server and per account, commands per second, allowed and blocked
  zones, and switches for bots, grouping with players and trading with them.
- **Logs**: the `HeadlessClient` log category records sessions, spawns and every walk (its route,
  length and speed).

Running scripted characters is the operator's decision. If you publish the module or the SDK,
please only point them at servers whose operators have chosen to allow it.

## Troubleshooting

| Refused | Means |
|---|---|
| `DISABLED` | `HeadlessClients:AllowHeadlessClients` is false (or rules weren't reloaded). |
| `BAD_TOKEN` | Unknown or revoked token: `headless:token-list`. |
| `UNKNOWN_CHARACTER` | No such character on the token's account. |
| `LIMIT` | `MaxTotal` or `MaxPerAccount` reached. |
| `ALREADY_ONLINE` | The character is logged in already (a client, or another session). |
| `ZONE_BLOCKED` | The character is in a zone `AllowedZones` / `BlockedZones` keeps it out of. |
| `ZONE_UNAVAILABLE` | Its zone couldn't be started (no free dynamic zone, or the zone won't boot). |
| `SPAWN_FAILED` | The zone couldn't load the character. Its zone log says why. |
| `TIMEOUT` | No `Hello` within 10 seconds. |
| `PROTOCOL_VERSION` | The program speaks a different protocol version: update it. |

Connection refused outright (no `Refused` frame): the listener isn't on. Check
`server.world.headless` and the port mapping.

Walks that end `NO_PATH` or `STUCK` usually point at the zone's navmesh (`maps/nav/<zone>.nav`). The
`HeadlessClient` log shows each route as it was planned.

## Using it from Python

`eqmq` is an asyncio SDK with MacroQuest's vocabulary: TLOs such as `Me`, `Target`, `Spawn`, `Group`
and `Zone`, slash commands through `DoCommand`, `#event` patterns, and plugins. It also includes a
live web map of the character's zone. See its README for the details. A taste:

```python
import asyncio
import eqmq

async def main():
    bot = await eqmq.connect("127.0.0.1", 9100, token="hc_...", character="Bifar")
    print(bot.Me.Name, bot.Me.Level, bot.Zone.ShortName)

    @bot.event("#1# tells you, '#2#'")
    async def tell(sender, text):
        await bot.tell(sender, "Hello! I'm a scripted character.")

    rat = bot.Spawn("npc radius 200 rat")
    if rat:
        await bot.moveto(rat.X, rat.Y, rat.Z, wait=True, timeout=bot.walk_time(rat.Distance))
        await bot.target(rat)
        await bot.face(rat)
        await bot.attack(True)
    await bot.wait_closed()

asyncio.run(main())
```
