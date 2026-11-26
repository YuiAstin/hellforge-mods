# Kit base: shared code for character kits

`kitbase/` holds the code the character kits have in common, so a new kit is mostly her skills plus wiring. It began
in mid-November 2026 with the four GFL2 kits of the time (Lenna, Faelynn, Leva, Voymastina). Thirteen kits are built
on it now: Alva, Ascalon, Destroyer, Executor, Exusiai, Faelynn, Koleda, Lemuen, Lenna, Leva, Vector, Voymastina and W.
On 2026-11-26 a second round lifted what those kits had grown since (see "The 2026-11-26 lift" below).

## Principles

1. **Extract, don't invent.** The base is code the shipped kits already run, moved with its comments and its history.
   A block moves in once two kits carry it; a one-kit block moves in only when asked (W's weapon swap).
2. **Compiled into each kit, not a shared DLL.** No version conflicts, nothing extra to install. Everything is `static`
   and marked unused, so a kit pays only for what it calls and `-Wall -Werror` stays clean.
3. **Every game-address block says whether it is available** and turns itself off cleanly after a game update: each
   pin is checked with `hf->code_matches` (or `Boundary`) and logs what is off.
4. **A kit changes a block's behaviour through callbacks and variables it sets in `hellforge_mod_init`,** never by
   editing the block (`kb_hellbomb_veto`, `kb_hit_before`, `kb_status_refuse`, `kb_strike_done`, `kb_sight_skip`, ...).
5. **Released kits keep the copy they shipped with.** A change in `kitbase/` reaches a kit at its next update, when its
   copies are written and it gets its in-game check. Nothing in the base is a reason on its own to re-release a kit.

## Layout

```
kitbase/                 source of truth (header-only)
  kb_core.h              layer 0: memory, lookups, entities, units, log, keys and their names
  kb_player.h            layer 0: her avatar, items and weapons, which hits are hers, stamina
  kb_friends.h           layer 1: the friend list (what her blasts spare, what is never a target) and the foe list
  kb_allies.h            layer 1: shields on allies (a pool per ally that absorbs damage)
  kb_body.h              layer 1: the T-Doll body (health, stamina, the stamina Hellbomb)
  kb_fit.h               layer 1: where a body fits (the game's own placement check), for teleports
  kb_hits.h              layer 1: the damage pipeline on the game's hit code (her hits and hits on her)
  kb_damage.h            layer 1: HellForge's damage event routed to the kit, with shared helpers
  kb_settings.h          layer 1: the settings table, .ini loader, skill keys and Mod Options Menu calls
  kb_input.h             layer 1: her skill keys held, the game's binding rows, one edge per press      (2026-11-26)
  kb_roster.h            several kits installed together: the shared roster and the active kit
  kb_options.h           the kits' options screen over the game (F9; HellForge's screens, SDK 17)
  kb_world.h             layer 2: what and where things are, health, rays, her view, body shapes       (2026-11-26)
  kb_spawn.h             layer 2: explosions that wait for their packages, entities, bare units         (2026-11-26)
  kb_hold.h              layer 2: the pose hold (stopped time, per enemy), with hud/hold.lua            (2026-11-26)
  kb_stealth.h           layer 2: her visibility cut, enemies forgetting her                            (2026-11-26)
  kb_status.h            layer 2: statuses as the game adds them, with a refusal callback               (2026-11-26)
  kb_swap.h              layer 3: a weapon of the kit's in her hands, hers back after (one kit: W)      (2026-11-26)
  hud/bindings.lua       HUD: key bindings (Mod Bindings Menu), always-show-cooldowns and the roster picker
  hud/list.lua           HUD: finding the open stratagem list to sit beside
  hud/draw.lua           HUD: lettering (rgba, shadowed text, key names as the DLL labels them)      (2026-11-26)
  hud/world.lua          HUD: her camera, world points on the overlay                                (2026-11-26)
  hud/hold.lua           HUD: the pose hold's Lua half and the stunned sign (needs world.lua)        (2026-11-26)
  hud/images/stun.png    the stunned sign (hold.lua), written into a kit's HUD as images/kb_stun.png
mods/<kit>/src/kitbase/  the copies a kit carries: the headers it includes, written by tools/kitbase_sync.py
```

HellForge builds a mod from its own `src/` folder (every `.c` there), so each kit carries a copy of the headers it
includes, and the headers those include. Edit `kitbase/`, never a copy.

### tools/kitbase_sync.py

```
python tools/kitbase_sync.py KIT...            write those kits' copies (w, lemuen, ...)
python tools/kitbase_sync.py --all             write every kit's copies (rewrites released kits: only when meant)
python tools/kitbase_sync.py --check [KIT...]  exit 1 if a copy is missing or stale
```

`tools/package.py` and `tools/deploy.py` run the check for the kits they handle, so a kit being packaged has to be on
the current base: that is the moment a kit adopts the base's changes. A kit's copies are only the headers it includes
(since 2026-11-26: before, every kit got every header), so a new header never touches a kit that does not use it. The
HUD regions work the same way: a kit's HUD Lua marks `-- kitbase:<name>.lua begin` ... `end`, the tool writes the
region from `kitbase/hud/<name>.lua`, and the images a region needs (`HUD_ASSETS`) into the kit's `hud-src/images/` as
`kb_<file>`. After writing a kit's HUD, rebuild it with HD2Runtime's SDK and copy `mod/9ba626afa44a3aa3.patch_0*` into
its `data/`.

Today every released kit except W shows as stale in `--check`: they shipped before the base's latest fixes (the friend
list, the hit pipeline, the weapon-hit fix below). That is expected; each catches up at its next update.

## Using the base in a kit

Include the base right after the C and SDK headers, in place of the kit's own `hf`, `self`, `game`, `exe`:

```c
#include "hellforge_mod.h"
#include "hellforge_functions.h"
#include "hellforge_content.h"
#define KB_KEYS 4                   /* only with more than three skill keys */
#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"
#include "kitbase/kb_hits.h"        /* before kb_spawn.h: strikes then count as her own blasts */
#include "kitbase/kb_world.h"
#include "kitbase/kb_spawn.h"
#include "kitbase/kb_hold.h"        /* the blocks the kit uses, nothing else */
#include "kitbase/kb_settings.h"
#include "kitbase/kb_input.h"       /* after kb_settings.h (its exports read s_keys) */
#include "kitbase/kb_roster.h"
#include "kitbase/kb_options.h"
```

| Where | What the kit does |
|---|---|
| `hellforge_mod_init` | `kb_ini`, `kb_key_names`, `KB_SETTINGS`, `KB_OPTIONS`, `LoadSettings()`, `RosterJoin(id, name)`, `KbOptionsStart(...)`; its callbacks (`kb_status_refuse`, `kb_strike_done`, ...) |
| GAME_LOADED | `game`, `exe`; `HitsInstall`, `KbWorldInstall`, `KbSpawnInstall`, `KbSwapInstall`, `KbStatusInstall` as used |
| Mission start | `KitDecide()` |
| Each frame | `KbOptionsTick()`; `if (!g_inMission \|\| !kb_kit_on) return;` `MyItems()`; `int presses = KbBindPresses();` `KbPressed(i, KbKeyDown(&s_keys[i]), presses)` per key; `KbStrikeFrame()`, `KbHoldFrame()`, `KbSwapFrame()` as used |
| Mission end | `KbHoldClear`, `KbStrikesClear`, `KbSwapAbort`, `KbVisCut(0, 0)` as used |
| Once, at file scope | `KB_ROSTER_EXPORTS(id)`, `KB_OPTIONS_EXPORTS(id)`, `KB_INPUT_EXPORTS(id, n)`, `KB_VIEW_EXPORTS(id)` (world markers), `KB_HOLD_EXPORTS(id)` (the hold) |

Its HUD Lua names the kit once (`local KIT = {name, id, module, proc}`), then carries the regions it uses in this
order: `bindings.lua`, `list.lua`, `draw.lua`, `world.lua`, `hold.lua`. The newer regions each make one table (`KBD`,
`KBW`, `KBH`), since a HUD's main chunk runs near Lua's 200-locals limit (W's did).

## The blocks

### Layer 0 and 1 (moved in November)

| Header | Blocks |
|---|---|
| kb_core.h | `Word` `Quad` `ReadAt` `Pointer` `Lookup` `HashFind` `Descriptor` (the game's own lookup once HellForge moved the records) `UnitNode` `Dist2` `NetOk` `Log` `Act` `QueryIdle` `Focused` `WielderOf` `ParseKey` and the key names, `OpenUtf8` |
| kb_player.h | `PlayerPose` `LearnNet` `MyItems` `MyItem` `Mine` `MineHit` `Observe` `StaminaNow` `OnWeapon` |
| kb_friends.h | `FriendTable` `FriendRes` `FoeKind` `AddFoe` `DropFoe`, the foe list `g_foe` / `g_foeRes` |
| kb_allies.h | `AllySpawn` `AllyGrant` `AllyAbsorb` `AllyShielded` |
| kb_body.h | `Body` `Exhaustion` (`kb_hellbomb_veto`, `kb_hellbomb_fired`) |
| kb_fit.h | `KbFitInit` `KbFitsOn` `KbGameFits` |
| kb_hits.h | the three hit hooks and their pins, `OwnBlast` `Steady` `Unshred` (`kb_hit_before`, `kb_hit_after`, `kb_hit_taken`; `KB_OWN_SLOTS`, `KB_STEADY`, `KB_UNSHAKEN`) |
| kb_damage.h | `OnDamagePre` routed to `kb_damage_taken` / `kb_damage_dealt` / `kb_damage_seen`; `IsShot` `ScaleDamage` `NoteTarget` |
| kb_settings.h | the settings table, the .ini, Mod Options Menu, the skill keys |
| kb_roster.h, kb_options.h | the roster and the options screen (below) |

### The 2026-11-26 lift

A function-by-function comparison of the thirteen kits found the blocks below in two kits or more (most word for word),
none of them in the base. Each was moved with the newest variant, or the superset of the variants, and its history.

| Header | Blocks | From |
|---|---|---|
| kb_world.h | `ActorFilter` (`FILTER_FOLIAGE`, `FILTER_BODY`, `FILTER_RAGDOLL`, `FILTER_ZONE`), `WorldId`, `KbWorldInstall` (the raycast, the warp setters, the destination setter) | ActorFilter word for word in 11 kits; the raycast pins in all 13 |
| | `UnitOf` `ResOf` `UnitObject` `EntityPos` `FoeUnit` | 6, 3, 3 and 6 kits |
| | `HealthRec` `HealthOf` (health and life state) `Alive` `Walks` | 8 kits had a health reader (3 variants); Walks from the Time Stop Probe and W |
| | `UnitNodes` `BodyOffset` (median, Yui's structure fix) `UnitTop` (95th percentile) `TargetPos` | Destroyer, Executor, Exusiai, Koleda; W 0.8.0 |
| | `FirstWall` (Koleda's second unit to see through, Lemuen's diagnostics) `RayWith` (Ascalon's zone faces, Faelynn's bodies) `Off` `View` `InSight` (`kb_sight`, `kb_sight_slack`, `kb_sight_skip`, `kb_fp_distance`) `KB_VIEW_EXPORTS` | Alva, Koleda, Lemuen, Vector; Destroyer, Faelynn, Lenna, Voymastina |
| kb_spawn.h | `KbStrike` / `KbStrikeFrame` (asks again while HF_ACT_WAITING, `kb_strike_done`, her own blast with kb_hits.h) | Destroyer's BlastFrame; 11 kits retried waiting explosions their own way |
| | `KbPackageOf` `KbLoadFor` `KbSpawnEntity` `KbSpawnNear` `KbRemoveEntity` | W (the launcher, the markers) |
| | `KbUnitLoaded` `KbSpawnUnit` (the name passed by pointer, never NULL) `KbUnitRemove` | W 0.7 (the game's own mines on her mines) |
| kb_hold.h | `KbHoldAdd` `KbHeld` `KbHoldFrame` `KbHoldClear` `KB_HOLD_EXPORTS`; hud/hold.lua (`KBH.step`) | W 0.9.0 (the Time Stop Probe, per enemy) |
| kb_stealth.h | `KbVisCut` `Behaviour` `Perception` `SensedIn` `HoldsIn` `ForgetIn` `KbForgetPass` | Ascalon, W (Lost Contact's records) |
| kb_status.h | `KbStatusInstall` with `kb_status_refuse` | Alva, Koleda, Vector |
| kb_input.h | `KB_INPUT_EXPORTS` `KbBindPresses` `KbKeyDown` `KbPressed` | the `<kit>_input` / `_key` exports in all 13 kits; W 0.6.0's edge |
| kb_swap.h | `KbSwapStart` `KbSwapFrame` `KbSwapEnd` `KbSwapAbort`, `InventoryRecord` `SwitchObject` `KbAskSelection` `Rounds` `FillUp` | W 0.9.0 (King of Hearts); one kit |
| kb_player.h | `MineHit` also takes a hit she owns dealt by a weapon she holds or carries | W 0.5.3: without the hit hook, her gunfire was never hers |
| hud/world.lua | `KBW.view` (the camera nearest her view) `KBW.project` | Lemuen, Alva, Ascalon, Executor, Koleda, Vector, W |
| hud/draw.lua | `KBD.rgba` `KBD.heavy` `KBD.key` | every kit's HUD (key names now as the DLL labels them, not `#32`) |

Left in the kits, because each kit's version is its own skill rather than a shared mechanism: the marks (`MarkFrame`,
`MarkOf`: Ascalon, Lemuen, W, Faelynn, Lenna, Leva each mark differently), the skill tiles (`ability`, `abilities`: 13
variants), the bottom-middle bars, the HUD state structs, the dashes and leaps (`DashFrame`, `LeapFrame`), the native
damage rows (`Nt*`), Voymastina's exosuit and stratagem grant. They move once a new kit wants one as it is.

## Layer plan (kept from the first round)

- **Layer 2, effect blocks** still in kits: movement (`DashFrame`, `Speed`, `HoldFor` / `HoldFrame`), area (`NearPulse`,
  `Surfaces`), targeting (`AimTarget`, `AimFlat`, `MarkOf` / `MarkEnemy`), armour (`Shred` / `Unshred`), native damage
  rows (`Nt*`: the most fragile block; it needs a clear unavailable state).
- **Layer 3, advanced and opt-in:** Voymastina's stratagem grant (`LocalStratagems` `GrantPins` `GrantFrame`, also in
  Commando Plus and Koleda) and exosuit (`ReadSeat` `MechSpawn` ...), live weapon tuning; kb_swap.h is the first here.
- **A generic HUD** (one state struct of slots: icon, cooldown, ready, label, count) is still open: the thirteen skill
  tile functions differ in what they show on a tile. Texas, the next kit, is the test of what they share.

## Several kits installed (kb_roster.h)

There is no base mod to install. Each kit joins a roster kept in named shared memory, one per game process
(`Local\yui.kitbase.roster.<pid>`, a mutex beside it), which every kit DLL in the game opens.

- **One kit:** it acts, as before; nothing changes and no picker appears.
- **Two or more:** the options screen's character column (F9) and the Mod Options Menu's Active Character row offer
  the installed kits. Only the picked kit acts; with none picked, the first by id. A pick takes effect at the next
  mission start (`KitDecide()`), so a body edit is never left half applied.
- **Standing by** (`kb_kit_on` 0): the kit's frame returns early (no keys, no skills, no Hellbomb), its body stays
  untouched, the damage event and the hit pipeline's kit callbacks skip it (its hooks stay in HellForge's chain and pass
  every hit through), the status hook passes every status through, its HUD hides, and its log says so once.
- **A kit:** `RosterJoin("id", "Name")` in hellforge_mod_init, `KitDecide()` at mission start, the frame's
  `if (!g_inMission || !kb_kit_on) return;`, the HUD state's `in_mission` gated on `kb_kit_on`, and
  `KB_ROSTER_EXPORTS(id)` once. Event handlers with side effects of their own return when it is off.
- **Co-op:** each machine picks for its own Helldiver.

## The options screen (kb_options.h)

One screen for all the installed kits, on a HellForge with screens over the game (SDK 17; the manifest of the kit that
adds it must ask for 17). F9 opens it. At the left, the shown kit's settings: switches, sliders, fields to type in, a
choice's buttons. At the right, the characters with their mod icons and an Off; a click picks the kit and shows its
settings.

- The kits meet in a hub, a second block of shared memory (`Local\yui.kitbase.options.<pid>`): each puts in its icon
  and the function that draws its rows. The first kit HellForge lets add a screen owns it; a refused kit leaves it to
  the next.
- A kit's rows and icon are generated: `python tools/optgen.py <kit>` writes `src/kit_options.h` from the `OPTIONS`
  table of its HUD and `src/kit_icon.h` from `icon.png`; `--check` says when they are stale. A kit whose HUD has no
  `OPTIONS` table writes `kit_options.h` by hand (W).
- The Advanced section (collapsed under its own bar): a kit's `options.json` lists every other setting of its .ini a
  player may change, with its group, label and tip, and the ones left off (`hidden`). Every setting of the kit's table
  must be on the screen, advanced or hidden: `optgen.py` stops and names the rest. `when` marks a setting that takes
  effect only at the next mission or after a restart; `rename` gives a row another group or label.
- Her skill keys head the list (`kOpKeyLabels`, as many as the kit has keys; W has four): click the box, press the key
  (Shift, Ctrl or Alt held for a combo; Mouse3/4/5). Another of her keys is swapped; F5, F8 and the screen's own key
  are refused; a key the game uses too is allowed with a note. Saved like the other choices (`key_s1 = Shift+Q`).
- Key names (kb_core.h `kb_key_table`): the .ini and the screen take Space, Tab, Enter, CapsLock, the arrows, Insert,
  Delete, Home, End, PageUp, PageDown, Num0-Num9, NumPlus, Semicolon, Comma and the other punctuation keys; the HUD
  shows the short labels (Space, Num0, `;`, M4). A `#32` from before still reads.
- Nothing is drawn or called on the game thread's behalf from the render thread: changes wait and `KbOptionsTick` sets
  them. Choices are saved to `%LOCALAPPDATA%\HellForgeMods\<kit>.ini` at the screen's close, the pick to `kits.ini`.
- A kit on the screen answers 1 from `<kit>_options_screen(1)`, and its HUD then leaves its rows off the Mod Options
  Menu. On a HellForge without screens nothing changes: the Mod Options Menu, as released.

## History

1. **2026-11-15, layer 0** into `kitbase/` from Lenna, Faelynn, Leva and Voymastina (31 functions identical in all four).
2. **The friend list and the T-Doll body** (Voymastina's friend table for everyone; Lenna's body with two callbacks).
3. **The damage pipeline:** the hit hooks (`kb_hits.h`) and the damage event (`kb_damage.h`).
4. **The settings table and the shared HUD regions** (`bindings.lua`, `list.lua`).
5. **Several kits installed** (`kb_roster.h`) and **the options screen** (`kb_options.h`, 2026-11-21 to 11-23).
6. **2026-11-23:** a friend is never kept as a foe; entity records through the game's own lookup (HellForge 0.5).
7. **2026-11-26, the second lift** (this page's table): kb_world, kb_spawn, kb_hold, kb_stealth, kb_status, kb_input,
   kb_swap, the weapon-hit fix in kb_player.h, the HUD regions draw, world and hold; the sync tool writes only what a
   kit includes, one kit at a time. No kit was moved onto it: each adopts it at its next update, with its in-game check.
   Every header builds alone and together with `-Wall -Wextra -Werror`, and W 0.9.0's own code rebuilt on the new
   headers builds (not committed: W adopts them at its next update).
