# Kit base: shared code for character kits

Plan for pulling the code Lenna, Faelynn, Leva and Voymastina each carry a copy of into one shared base, so the next
kit (AK-12) is mostly her skills plus wiring, and a JSON layer can sit on top later.

## Why

A function-by-function comparison of the four kit sources (Voymastina 0.4.0, Leva 0.5.4, Faelynn 0.3.5, Lenna 0.2.10):

- **31 functions, about 330 lines, are identical in all four.** Another 18 are identical in two or three.
- **16 more exist in all four but have drifted** (2 to 4 variants each): settings, the damage hook, the T-Doll body,
  blasts, the frame loop. Fixes made in one kit never reached the others.
- **The HUD Lua is about 80% the same** in Lenna, Faelynn and Leva (64% in Voymastina).
- Code shared with at least one other kit: Lenna 83%, Faelynn 83%, Leva 68%, Voymastina 44% (she has about
  1,700 lines of her own: the exosuit, the stratagem grant, the two kits).

## Principles

1. **Extract, don't invent.** The base is code the shipped kits already run. A block moves in once two kits need it.
2. **C library first, JSON later.** JSON will describe numbers, which blocks a kit uses, its triggers and its HUD
   layout. Logic stays in C (or Lua through HD2Runtime). No home-made scripting language.
3. **Compiled into each kit, not a shared DLL.** No version conflicts, nothing extra to install. A shared runtime
   mod only once other modders build on it.
4. **Every game-address block says whether it is available** and turns itself off cleanly after a game update.
5. **AK-12 is the test.** If her kit is mostly blocks plus her own skills, the base is right. If the base keeps
   changing to fit her, it is not ready.

## Layout

```
kitbase/                 source of truth (header-only)
  kb_core.h              layer 0: memory, lookups, entities, units, log, keys
  kb_player.h            layer 0: her avatar, items and weapons, hook bookkeeping, stamina
  kb_friends.h           layer 1: the friend list (what her blasts spare, what is never a target)
  kb_body.h              layer 1: the T-Doll body (health, stamina, the stamina Hellbomb)
  kb_hits.h              layer 1: the damage pipeline on the game's hit code (her hits and hits on her)
  ...                    later layers as they are extracted
mods/<kit>/src/kitbase/  copies, kept in sync by tools/kitbase_sync.py
```

HellForge builds a mod from its own `src/` folder (every `.c` there), so each kit carries a copy of the headers.
`tools/kitbase_sync.py` writes the copies; `--check` fails if any is stale, and deploy and package run that check.
The headers are `static` and marked unused, so a kit only pays for what it calls and `-Wall -Werror` stays clean.

A kit includes the base right after the SDK headers, in place of its own `hf`, `self`, `game`, `exe`:

```c
#include "hellforge_content.h"
#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"
#include "kitbase/kb_body.h"
```

A kit changes a block's behaviour through callbacks it sets in `hellforge_mod_init`, never by editing the block. The
T-Doll body has two: `kb_hellbomb_veto` (Faelynn holds the Hellbomb back while Last Bark has her) and
`kb_hellbomb_fired` (Voymastina lets that one blast through her immunity).

## Layers

### Layer 0: identical everywhere (move as-is)

| Group | Functions |
|---|---|
| Memory and lookups | `Word` `Quad` `AddressOk` `ReadAt` `Pointer` `Lookup` `HashFind` `Descriptor` `NetOk` |
| Entities and units | `UnitNode` `PlayerPose` `WielderOf` `QueryIdle` `Focused` `Dist2` |
| Her avatar | `LearnNet` `MyItems` `MyItem` `Mine` `MineHit` `Observe` `StaminaNow` `OnWeapon` |
| Utilities | `Log` `Act` `OpenUtf8` `ParseKey` (with `Key`) |

Identical in two or three kits, to follow: `ActorFilter` `EntityPos` `FoePos` `RayWith` `Fits` `FoeAbove`
`IsFoeUnit` `Occupied` `Roofed` `SpotAt` `Unshred`.

`Body`, `OnSpawn` and `Emit` are identical too but call drifted code (`s_hpMult`, `FoeKind`, `Steady`), so they move
with layer 1.

### Layer 1: core, drifted per kit (unify, then move)

| Block | Today | Becomes |
|---|---|---|
| Friend list | `FoeKind` / `AddFoe`, 2 variants | Voymastina's `FriendTable` / `FriendRes` (the superset) for everyone |
| T-Doll body | `Body`, `Exhaustion` (3 variants) | health and stamina multipliers and the stamina Hellbomb, as settings |
| Damage pipeline | `Calculate` + `Steady` + `OnDamagePre` (4 variants) | one dispatcher; Stability, own-blast immunity, Pledge, on-hit procs as ordered handlers |
| Strikes | `Blast` / `Blast_` (3 variants) | strike spawner (kind, position, owner) with friendly filtering built in |
| Settings and binds | `Setting` / `LoadSettings` / `*_option_*` / `*_key` | one table drives the ini, Mod Options rows, Mod Bindings and defaults |
| Lifecycle | `Tick` / `Reset` / `Loaded` / `OnPlayer` / init | the base owns the frame loop and mission reset; kits register callbacks |
| HUD | `*_hud_state` + `*_hud.lua` | one generic addon reading slots (icon, cooldown, ready, label) plus one bar |
| Kit modes | Voymastina's `KitFrame` / `KitKey` / `KitReset` | a character can hold several kits with enter and leave triggers; HUD, keys and passives swap with the mode |

### Layer 2: effect blocks (shared by two or three kits)

- Movement: `DashFrame` (dashes never follow the camera), `Speed`, `HoldFor` / `HoldFrame`
- Area: `NearPulse`, `Surfaces`
- Targeting: `AimTarget`, `AimFlat`, `MarkOf` / `MarkEnemy`
- Armour: `Shred` / `Unshred`
- Native damage rows: the `Nt*` family (Lenna, Leva). Most fragile block; needs a clear unavailable state.

### Layer 3: advanced, opt-in (Voymastina only so far)

- Stratagem grant outside the loadout: `LocalStratagems` `GrantPins` `GrantFrame` `SinOwned` `SinPackage`
- Exosuit: `ReadSeat` `MechSpawn` `MechParts` `MechRay` `MechWalkFrame` `RaiseArmour`
- Live weapon tuning: `TuneWeapon`

### Stays in each kit

The named skills (`WildExtinction`, `TripleMaul`, `SuperconductBlitz`, `PileBunker`, `Howl`, ...) and the wiring
between blocks.

## Steps

1. **Done.** **Layer 0** into `kitbase/`; all four kits include it. Check: each kit's translation unit holds exactly the same
   function definitions and globals as before (only moved), and all four build with `-Wall -Werror`.
2. **Done (needs its in-game check).** **Friend list and T-Doll body.** Every kit now uses Voymastina's friend list:
   exosuits, vehicles, dog drones, SEAF, the Pelican, mod copies of any of them, and anything a Helldiver holds are
   never taken for enemies. The body is Lenna's code with the two callbacks above.
3. **Damage dispatcher, part 1: the hit hooks (done, on branch `kitbase-damage`, needs its in-game check).**
   `kb_hits.h` owns the three hooks, their code pins and one pipeline (see its header comment). Kits plug in with
   `kb_hit_before` / `kb_hit_after` (Faelynn's Shred; Voymastina's hand and Shred) and `kb_hit_taken` (Voymastina's
   attacker record, Hellbomb pass-through and Predation armour). Defaults per kit: `KB_OWN_SLOTS` (Leva 64,
   Voymastina 16), `KB_STEADY` (Faelynn, Voymastina 1), `KB_UNSHAKEN` (Voymastina 1).
   Part 2: the HellForge damage event (`OnDamagePre`, 4 variants up to 115 lines) as ordered handlers.
4. **Settings table and the generic HUD.**
5. **AK-12 on the base.** Layer 2 and 3 blocks move only when she needs them.

Steps 2 onward change behaviour, so each kit gets an in-game check before release, and kits ship on the base only
once tested.
