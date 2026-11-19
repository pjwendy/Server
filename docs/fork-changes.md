# What this fork changes

This fork is [EQEmu](https://github.com/EQEmu/Server) with one addition, the **headless character
module** ([headless.md](headless.md)), on the `headless` branch. `master` follows upstream.

Building with `EQEMU_BUILD_HEADLESS=OFF` leaves the module out. A few changes outside it are in every
build: fixes to upstream bugs found while running the module (each its own commit, so it can be
offered upstream), and changes to bots, pathing and rules. They're listed here. Every rule they add
defaults to upstream's behaviour unless noted.

## Bug fixes to upstream code

| Commit | Fix |
|---|---|
| [3697f71](https://github.com/pjwendy/Server/commit/3697f7187) | A zone crashed when a raid-grouped bot with no stored raid finished a group spell. |
| [4e30240](https://github.com/pjwendy/Server/commit/4e302400a) | A zone crashed on a character with an empty mail key. |
| [8a7a5d4](https://github.com/pjwendy/Server/commit/8a7a5d404) | Bots: an instant spell was refused by the buff stack check (a shaman's Cannibalize under Clarity or a regen buff). |
| [701171f](https://github.com/pjwendy/Server/commit/701171fc5) | `Group::IsLeader(name)` checked membership, not leadership. |
| [ec5a8d7](https://github.com/pjwendy/Server/commit/ec5a8d7f9) | A zone change was sent to a pending summon's coordinates in another zone. |
| [c9851fc](https://github.com/pjwendy/Server/commit/c9851fc18) | A character is kept where it was when the zone it's going to isn't ready. |
| [72414c9](https://github.com/pjwendy/Server/commit/72414c97c) | A client's cached clean name is reset when zone entry sets its name. |
| [1f759db](https://github.com/pjwendy/Server/commit/1f759dba0) | Deleting a character deleted corpses and guilds that only shared its id. |
| [68170d9](https://github.com/pjwendy/Server/commit/68170d929) | Pathing: a route's ends snapped to a floor far above or below the point, not the one at its own level. |
| [1335e04](https://github.com/pjwendy/Server/commit/1335e04f3) | Pathing: the same level snap in `FindRoute`, and when the narrow search box finds nothing. |
| [f1ea6a7](https://github.com/pjwendy/Server/commit/f1ea6a779) | Pathing: long off-mesh links are joined at their far end when a navmesh loads. |
| [a27eeb5](https://github.com/pjwendy/Server/commit/a27eeb5c9) | `command.h` includes `<map>` for the `std::map` it declares. |
| [91fd942](https://github.com/pjwendy/Server/commit/91fd942a3) | Database credentials were written to the log. |

## Bots

For every player's bots, not only headless characters'. The commands and behaviour are described in
[headless.md, Bots](headless.md#bots).

| Commit | Change |
|---|---|
| [7044d09](https://github.com/pjwendy/Server/commit/7044d09cf), [bc828f1](https://github.com/pjwendy/Server/commit/bc828f12b) | `^pack`: a bot carries things for its owner. |
| [9675ad4](https://github.com/pjwendy/Server/commit/9675ad461) | `^bank`: a bot has a bank, at a banker. |
| [c2458c4](https://github.com/pjwendy/Server/commit/c2458c408), [a9ca94c](https://github.com/pjwendy/Server/commit/a9ca94c48), [1371432](https://github.com/pjwendy/Server/commit/13714327c), [9b13e15](https://github.com/pjwendy/Server/commit/9b13e1550) | `^corpsedrag`: a bot fetches its owner's corpse, feigning or sneaking, with no fixed time limit. |
| [d31933e](https://github.com/pjwendy/Server/commit/d31933ee3), [dc6b82f](https://github.com/pjwendy/Server/commit/dc6b82f18) | `^feign`, and feigning by itself at low health; a dying pet is called off. |
| [ed089c6](https://github.com/pjwendy/Server/commit/ed089c679) | `Bots:StrategicCombat` (default off): bots spare mobs the group has mezzed or rooted. |
| [caf232a](https://github.com/pjwendy/Server/commit/caf232a80), [54d2626](https://github.com/pjwendy/Server/commit/54d26262b), [f3caaa3](https://github.com/pjwendy/Server/commit/f3caaa388), [be33abd](https://github.com/pjwendy/Server/commit/be33abdc5) | `Bots:UseOwnerReagents` (default off): a bot's spells use their components, from its owner; a summoned item goes to the owner. |
| [5fdbeb6](https://github.com/pjwendy/Server/commit/5fdbeb6c8), [7e78385](https://github.com/pjwendy/Server/commit/7e7838516), [f7578b9](https://github.com/pjwendy/Server/commit/f7578b909) | A bot stands still to cast, as a player must (bards sing on the move). |
| [1ea6e83](https://github.com/pjwendy/Server/commit/1ea6e831c) | A puller says what it's pulling, and that it's coming back with it. |
| [12a917e](https://github.com/pjwendy/Server/commit/12a917efa), [f583c09](https://github.com/pjwendy/Server/commit/f583c095b) | Routine casts aren't announced; a commanded spell no bot could cast says why. |
| [1b5baf5](https://github.com/pjwendy/Server/commit/1b5baf57e) | `Bots:UseHeadlessNavmesh` (default on): bots and pets route on the zone's headless navmesh, where there is one. |

## Pathing and navmeshes

| Commit | Change |
|---|---|
| [2305ddd](https://github.com/pjwendy/Server/commit/2305ddd19) | `PathingHeadless`: navmesh links only headless characters take. |
| [1e41c1d](https://github.com/pjwendy/Server/commit/1e41c1d08), [af28a57](https://github.com/pjwendy/Server/commit/af28a5758) | A navmesh of their own for headless characters (`maps/nav/headless/`), loaded on first use and only with the module on. |
| [f9e8487](https://github.com/pjwendy/Server/commit/f9e848701) | A headless character's route searches up to 65,535 nodes. |
| [2bae915](https://github.com/pjwendy/Server/commit/2bae9140b) | Lava next to never for a headless route. |

## Rules

| Commit | Change |
|---|---|
| [da5f211](https://github.com/pjwendy/Server/commit/da5f21179) | `Zone:RespawnMultiplier` (default 1.0): every spawn point's respawn time multiplied, with a 10-second floor. |

## Build

| Commit | Change |
|---|---|
| [989df15](https://github.com/pjwendy/Server/commit/989df152a) | protobuf, and the `EQEMU_BUILD_HEADLESS` option. |
| [60bcf53](https://github.com/pjwendy/Server/commit/60bcf5329), [dd8721a](https://github.com/pjwendy/Server/commit/dd8721ad4) | The `win-msvc` preset targets Visual Studio 2026; compile fixes in the UF patch and the zone client. |
| [ac933bb](https://github.com/pjwendy/Server/commit/ac933bb34), [3d2dbc4](https://github.com/pjwendy/Server/commit/3d2dbc4c0) | Extra debug logging carried over from earlier work on this fork. |
