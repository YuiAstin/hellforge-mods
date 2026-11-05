# Koleda: the Sinner kit (design)

Koleda is AK-12 in Girls' Frontline 2: Exilium (DEFY, with Voymastina / AK-15 and Alva / AN-94), a Vanguard with an
assault rifle. She is the test case the kit base was planned around (docs/kit-base.md, step 5): her kit should be the
kit base's blocks plus her own skills and the Sinner.

**Status: nothing here has been tested in game.** Every line below marked (untested) is written but has never run.
The kit is built so that the on-foot part keeps working when any Sinner part fails: each Sinner block checks its own
game code and turns itself off with a log line.

## Her GFL2 kit

- **Ultimate, Hallucination:** summons the Sinner (the Sinveiler). Used again while the Sinner stands, it turns into
  the follow-up **Winner!**, which restores the Sinner fully (health and stability) and cleanses its debuffs.
- **Passive:** the Sinner takes 50% of the damage aimed at her (80% at V3). Each Sinner action gives her Upshift stacks
  (one more in S+ gear).
- **Her skills switch the Sinner's Gear:** her basic attack: N (defensive); **Eye of the Snow Wolf** (a big single hit,
  +1 Upshift): S+ (offensive); **Tactical Playbook** (+2 Upshift, Kinetic Recovery on both: attack, crit and healing
  up): S.
- **The Sinner's moves:** KANSEI DORIFTO (an area pull with damage), Speedy Speed Doll (a line attack, stronger in S and
  S+), Let's Go For A Ride! (buffs an ally; Koleda gains its attack).

## The rules for GFL2 kits (Yui)

- The T-Doll package: the kit base's defaults (hp_mult 1.5, stamina_mult 2, a Hellbomb on her when her stamina runs dry).
- Team resources (Confectance) are not ported. Upshift is her own stack, so it stays.
- Dash and movement skills do not follow the camera; aimed skills may.
- Ship only what is tested.

## In Helldivers terms

### The Sinner: an empty exosuit that drives itself (untested)

- **What it is:** her own exosuit, an `add_entity` copy of the EXO-45 Patriot (`yui/koleda/sinner`, titled "Sinner",
  designation "EXO-45K"), with its own stratagem (`add_stratagem` copy of the Patriot's, a Pelican drop; code RRLLD).
  Vanilla Patriot look for now; Killerco's Exosuit Concept Mech, re-exported under our own name, is the planned model
  (docs/permissions.md).
- **How it is called (key C, Hallucination; untested):** `call_mode 1` (the default): each press with no Sinner standing
  puts one Sinner call in her stratagem list (Voymastina's grant: an entry appended to her own stratagem record, as the
  game grants Reinforce; a later press refills that entry to one use) and starts the 90 s cooldown. She throws it with
  its code as any stratagem; the Pelican drops it. `call_mode 0`: the call is granted once at the drop, unlimited, with
  the stratagem's own 90 s cooldown (the Sinbreaker's way, proven in Voymastina 0.4.x), and C only sets off Winner!.
  The call's package is held for the session (`load_package`, as the Sinbreaker's).
- **Which exosuit is hers:** the first spawn of the Sinner's resource after she was granted a call. It counts as landed
  once it has stood still (under 0.3 m in 1.5 s) on the ground (a ray finds ground within 2.5 m under it) at least
  5 s after it spawned.
- **Driving (behind `sinner_drive.h`; stubbed until the probe):** as Exo Companion drives a suit (research on branch
  `koleda`, docs/research/exo-companion.md): the stick input written into its VehicleLocomotion record, engine and
  physics mover on, its own arm weapons through the game's trigger. All of it sits behind five calls in
  `mods/koleda/src/sinner_drive.h`: `SinnerDriveReady`, `SinnerDriveStart`, `SinnerDriveStick`, `SinnerDriveFire`,
  `SinnerDriveStop`. The stick write is written and reads its record layout from the ini (`loco_manager`, `loco_map`,
  `loco_records`, `loco_stride`, `stick_offset_fwd`, `stick_offset_side`, `stick_offset_turn`; -1 unknown, so off).
  Engine, mover and trigger return 0 until the probe gives their signatures. `drive_test 1` lets the stick writes run
  without the engine and mover calls (to see whether the stick alone moves it).
- **The fallback while driving is unavailable (untested):** the Sinner is moved with Voymastina's warp setter
  (game.dll+0x5A2BB0) toward its follow point a little each frame (a slide: no leg animation), following the ground and
  stopping at walls, and it fires spawned projectiles from its shoulders (Exusiai's summoned guns' approach): copies of
  the AR-23 Liberator's round (`add_projectile`, `sinner_round` 276).
  - **The warp setter's guard:** it crashed on entities without a movement record (a strider's rider, a dead enemy).
    With `warp_needs_walks 1` (the default) the Sinner is moved only if `Walks()` finds an active navigation record
    for it, it is this machine's (descriptor authority bit) and numbered under 0x400000. An empty exosuit may well have
    no navigation record: then the log says so once and the Sinner stands where it landed (and still fires). Whether
    the warp setter is safe on an empty exosuit is a question for the probe (`warp_needs_walks 0` skips the check, at
    the risk of that crash).
- **Leash (untested):** it walks toward her when more than `leash_start` (10) m away and stops within `leash_stop` (6).
  More than `leash_warp` (40) m away, or stuck (moving but under 0.5 m in `stuck_seconds`, 4), it is warped beside her
  (3 m to her side, on the ground, same guard).
- **Piloting:** if she gets in it herself (the seat read, as Voymastina's), its own driving, firing and the damage share
  pause.

### Passive: the Sinner takes her hits (untested)

- In `kb_damage_taken` (a hit on her), `share_pct` (50) of the hit is taken off her hit and off the Sinner's health
  instead, while it stands within `share_range` (60) m and is not piloted. Drowning and bleeding out are left alone.
- The Sinner's health is written on the game thread (the damage event queues the amount). It never drops below 1 from
  the share: only real hits on it kill it. Each write is logged (the first 12) and read back.
- **The health record:** the health manager `[game+0x3326688]` (HD2Runtime's research; every kit's `HealthOf`): an
  open-addressed entity map at +4112, 440-byte records, **health at +20** (i32), and +412 is the `life` word (0 while
  alive; teleport-blink.md reads it as "life < 1: not downed"). +412 is not a maximum. The maximum is the Sinner's own
  `Health.health` (`get_live` on its resource; the Patriot's 1800), else the most health seen on it.

### Upshift (her stack; untested)

- At most `upshift_max` (5); after `upshift_decay` (10) s without a gain, one stack is lost, and one more every
  `upshift_decay` s after.
- **Gains:** a Sinner action (a volley it fires, at most one gain every `upshift_every` 3 s; its pull) +1, and +1 more in
  S+; Eye of the Snow Wolf +1; Tactical Playbook +2.
- **What it does:** her hits deal +`upshift_damage` (6%) per stack (the Sinner's spawned rounds are hers too).

### Gears (untested)

- **N (defensive; her default, and after Winner!):** it stays within `guard_start` (6) m of her and shoots whatever last
  hit her (the hit hook's dealer or owner, fresh within 5 s), else the nearest enemy within `guard_range` (20) m of her.
  No taunt (the game has none to borrow).
- **S (balanced; Tactical Playbook):** the leash above; it shoots the nearest enemy within `fire_range` (40) m of it.
- **S+ (offensive; Eye of the Snow Wolf):** it pushes toward her target (Eye of the Snow Wolf's, else what she last
  shot, fresh within 10 s), standing `plus_distance` (8) m short of it but never more than `plus_leash` (25) m from her,
  and fires faster (its interval x `plus_rate`, 0.6).
- Its rounds spare her allies and itself (hits of hers near a Sinner aim point on a friend, or on the Sinner, are
  cancelled).

### Her skills

| Key | Skill | What it does (all untested) |
|---|---|---|
| Z | **Eye of the Snow Wolf** | Aimed: the enemy under her crosshair (within `s1_cone` 10 degrees, `s1_range` 200 m, in sight; Alva's aim) takes `s1_rounds` (2) heavy rounds (a copy of the APW-1's, 226; Alva's Nix Requiem round) from her head. +1 Upshift. Sinner to S+, on that target. Cooldown 12 s. Refused (no cooldown) with no target. |
| X | **Tactical Playbook** | +2 Upshift. Kinetic Recovery for `kr_seconds` (10): her damage x`kr_mult` (1.2), she heals `kr_heal` (25) at once (`hf->heal`), the Sinner fires x`kr_mult` faster and gets `kr_sinner_heal` (10%) of its health back. Sinner to S. Cooldown 20 s. |
| C | **Hallucination** / **Winner!** | No Sinner: the call (above), cooldown `s3_cooldown` (90). Sinner standing, once per Sinner: Winner! restores its health to its maximum (the health write above) and removes its debuffs (below); its gear goes back to N. |

**Winner!'s cleanse (untested; off until pinned):** the game's own status functions (static research,
docs/research/game-notes.md on branch `alva`): has `game.dll+0x69B1F0 (manager, entity, status, 0) -> al` and remove
`game.dll+0x69AEC0 (manager, entity, status)`, manager `[game+0x3326620]`, for statuses 5, 6, 32 (fire), 37, 38, 39
(stun), 42-45 (gas), 67 (the flamer slow), each checked with "has" first. Their first bytes are not known yet (the dump
is not in this checkout), so `cleanse 1` refuses to call them and the log prints their first 32 bytes at load for the
pins; `cleanse 2` calls them unpinned (the build check by module size still holds).

### The Sinner's specials

- **KANSEI DORIFTO (the pull; `pull_on 0`, off by default; untested):** in S+, every `pull_every` (12) s with an enemy
  near, living enemies within `pull_radius` (8) m of the Sinner are dragged to a ring 2 m around it over half a second
  (Voymastina's Pile Bunker drag, with its guards: alive, this machine's, numbered under 0x400000, `Walks()`), and an
  optional explosion (`pull_explosion`, 0: none) goes off on it. +Upshift as a Sinner action.
- **Speedy Speed Doll, Let's Go For A Ride!:** not ported (no line attack or ally buff yet; they wait for the driving).

### The HUD (`hud-src/koleda_hud.lua`, not built yet)

- The skills beside the stratagem list (EYE OF THE SNOW WOLF / TACTICAL PLAYBOOK / HALLUCINATION, the third reading
  WINNER! while it is ready).
- The bottom-middle bar: Upshift pips, the gear (N / S / S+), the Sinner's health bar and its leash state.
- A marker over the Sinner (`koleda_marks`: position and health).
- The DLL's exports: `koleda_hud_state` (layout version 1), `koleda_marks`, `koleda_view`, `koleda_input`, `koleda_key`,
  `koleda_option_get` / `_set`, the roster's `koleda_roster` / `koleda_pick`.

## Game addresses the kit uses

| Address | What | Source | Guard |
|---|---|---|---|
| game+0x129DC30 / +0x129D180 / +0x129DD30 | the hit hooks | kit base (kb_hits.h) | kit base pins |
| game+0x5A2BB0 | the warp setter (slide, leash warp, pull) | Voymastina (LIFT-182 Warp Pack's call) | pinned as Voymastina; `Walks()`, authority, QueryIdle |
| game+0x4A7550 | the velocity setter (after a leash warp) | Voymastina | pinned as Voymastina |
| exe+0x79F860 / exe+0x7F4590 | world id and raycast (sight, ground, walls) | Lemuen / Alva (Helldiver Survival Rework) | pinned |
| game+0x3326688 | the health manager: read, and the Sinner's health write (+20) | HD2Runtime research, every kit | record checks, read back |
| game+0x3326418 | navigation records (`Walks()`) | Voymastina 0.4.1 (Lost Contact's layout) | read only |
| game+0x3326E68 | the command bus, seat read (is she piloting it) | Voymastina (Khi's reading) | read only |
| game+0x347CE50 / +0x347CEF0 / +0x3326348 | her stratagem record, peer, clock (the call grant) | Voymastina (HD2Runtime research) | Voymastina's GrantPins |
| game+0x3326620 | the status manager (Winner!'s cleanse) | static research (branch `alva`) | read only |
| game+0x69B1F0 / +0x69AEC0 | status has / remove | static research (branch `alva`) | **no pin yet**: off unless `cleanse 2` |
| game+0x699E40 | status add (not called; its pin is checked as a build sanity check for the two above) | Alva 0.1.2 | pinned |

## Open questions for the probe

1. VehicleLocomotion: which manager holds it (`[game+?]`), where its entity map and records sit in it, the record
   stride (0xA4?) and the stick offsets (`in_x`, `in_y`, `in_z`: forward, side, turn).
2. The engine setter (`eng_set` 0xFD9D40), the physics mover (`mover` 0x6FE480) and the weapon trigger (`wtrig` near
   0x11B602A): their entries, signatures and pins.
3. Does the warp setter work on an empty exosuit (one nobody piloted)? Does an empty exosuit have a navigation record
   (`Walks()`), or a movement record of another kind to check instead?
4. Do enemies attack an empty exosuit; do hits on it come through the damage event with it as the target?
5. Does refilling the call's entry (uses 1, cooldown 0) bring the stratagem back in her list, or does a used-up entry
   need a fresh append?
6. The first bytes of game+0x69B1F0 and +0x69AEC0 (the log prints them) for the cleanse's pins.
7. The Patriot's maximum health as `get_live` reads it on the copy (expected 1800).
