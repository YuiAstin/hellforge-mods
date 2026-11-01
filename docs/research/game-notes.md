# Game and HellForge notes from the GFL2 kits

What building Lenna, Faelynn and Leva established about Helldivers 2 (build **25480438**) and HellForge (SDK 15). Addresses are RVAs in `game.dll` unless marked `exe+`. "Live" means seen working in game; "static" means read from code only.

For broader topics see [ai-modding.md](ai-modding.md) (sentries, guard dogs, enemy AI), [teleport-blink.md](teleport-blink.md) (moving a Helldiver) and [ias-stutter.md](ias-stutter.md) (HD2Runtime + IAS frame spikes).

## HellForge behaviour

- **Mod folders are read-only during a session (live).** A mod cannot create or change files in its own folder while the game runs (`_wfopen` fails with error 13). After the session, a mod whose folder gained files is switched off until re-enabled on the Mods page. So mods must ship their settings file, never write it.
- **The game's AppData is per session (live, HellForge newer than 2026-10-26).** `%LOCALAPPDATA%` inside the game points at `logs/sessions/<id>/game/environment/Local`; anything written there is gone next session.
- **Paths are UTF-8.** `mod_folder()` returns UTF-8; open files with `MultiByteToWideChar(CP_UTF8)` + `_wfopen`, not `fopen`.
- **Zipped mods** are unpacked to `mods/.unpacked/<zip name>-<content hash>/<mod>/`, and that is the folder `mod_folder()` returns.
- **Source mods** are built by HellForge into `temp/mod-builds/<id>/<hash>/` before each session; only `src/` is compiled, every `.c` in it.
- **Manifest `"icon"`**: a PNG/JPEG in the mod folder shown on the Mods page.
- **Ending check:** a session is over when `logs/sessions/<id>/session.log` ends with a `Session result:` line. Only then is it safe to change mod files.
- **`HF_ON_DAMAGE_PRE`:** setting `amount = 0` is *not* honoured (the hit lands at full damage). To remove a hit, set `cancel = 1` (live).
- **`spawn_explosion` loads the explosion's package; bullet impacts don't.** An explosion set off by a projectile's impact has no sound or visual unless its package is loaded first: call `load_package` at mission start (live).

## Damage events

`HfDamageEvent.kind`: 0 projectile, 1 ricochet, 2 damage over time, 3 single-shot beam, 4 explosion, 5 impact, 6 ability, 7 flat, 8 drowning, 9 bleeding out.

Electric hits from the GL-52 De-Escalator burst (explosion 41), live:

| What | kind | element | Notes |
|---|---|---|---|
| The burst's blast | 4 | 3 (often 0 damage) | at the burst point |
| Its arcs, first strike and every chain jump | **0** | **2** | come in as *projectile* hits by the player, 3–15 m and ~0.05–0.1 s after the burst; damage in steps of 45 (90, 135, 225, 270…) |
| Some arc hits | 4 | 2 | 0 damage |

So a mod that wants to tell its arcs from the player's gun must treat element-2 projectile hits near its own bursts as arcs. One burst's chain/split hits the same enemy 4–6 times within a few hundredths of a second.

`dealer`/`owner` are network ids (0x7FFF: none); the local Helldiver's can be learned from the hit hooks below.

## Hit hooks (static + live)

Pinned by the bytes in each mod's `code_matches` table:

| RVA | Role |
|---|---|
| `0x129DC30` | hit calculation (Calculate) |
| `0x129D180` | explosion dispatch (Blast) |
| `0x129DD30` | emission (Emit) |

Hit record: `+0x20` target entity, `+0x80` dealer, `+0x100` owner, `+0x9C/+0xA0/+0xA4` armour penetration floats by impact angle (`+0xA8` the extreme angle). Raising those three is how Faelynn's Corrosion adds AP to every hit on an enemy. Result: `+0` damage float, `+4` armour share. (Layout from Helldiver Survival Rework, MIT.)

## Arcs (ArcSettings rows, live)

| Row | Velocity | Range | Spread (first) | Angle (first) | Chain | Split | Used by |
|---|---|---|---|---|---|---|---|
| 6 | 2000 | 10 | 6 (8) | 180 (180) | 2 | 2 | GL-52 burst: the fan that deals the damage |
| 7 | 300 | 55 | 40 (40) | 60 (3) | 1 | 1 | ARC-3 Arc Thrower |
| 8 | 300 | 55 | 40 (40) | 60 (3) | 2 | 5 | |
| 15 | | | | | | | GL-52 burst's second set (0/0 here changed nothing) |

- **Too many arcs at once leave their lightning on the field for good,** as do arcs that hit props instead of terrain or enemies. Arcs that find a target clean up. Rate-limit arcs (Leva: `arc_rate` per second, half-second burst at most); count chain/split children too.
- Giving the burst row 6 the Arc Thrower's shape removes its damage.

## Explosions and packages

| Explosion | What | Package (load at mission start) |
|---|---|---|
| 41 | GL-52 De-Escalator impact burst (element 3, releases arcs; 300 direct / 146 jumps) | `0x4FEE9CC13C0B4CEC` |
| 157 | G-31 Arc grenade detonation (its lightning tends to stay on the field) | `0xDDCEA5FDD4071767` |
| 125 | B-100 Portable Hellbomb | `0x346D785D6CA13FF4` |

**Carrier bullet trick (Leva, live):** to set an explosion off at a point with its sound and arcs, fire a copy of a Liberator bullet (projectile 276, `add_projectile`) straight down from 3 m with `impact_explosion = 41`. The GL-52 shell itself (projectile 222) bounces. Zero the copy's own damage row (`add_damage` copy of 108, then `damage_row` after `HF_EVENT_PROJECTILES_READY`) or it hits for Liberator damage.

## The Helldiver

- **Health:** the health manager at `0x3326F88` (HD2Runtime research); `HealthOf` per entity.
- **Stamina (0..1):** action manager `[0x3326D20]`, per Helldiver at `+0x547854 + index * 0x78`. The game's stamina write at `0xA50E2D` is `f3 42 0f 11 8c 11 54 78 54 00`.
- **Teleport:** the LIFT-182 Warp Pack's own position setter `0x5A2BB0` (called from `0x88D795` with NULL, the entity, the destination), then velocity zeroed with `0x4A7550`. Found with the Warp Probe (`probes/warp-probe`). Details in [teleport-blink.md](teleport-blink.md).
- **Raycast:** `exe+0x79F860` world id, `exe+0x7F4590` raycast (as Helldiver Survival Rework's cover check calls it), world at `[0x346BFA0]`, filter `0x80000009`. Hits are 44 bytes: `+24` distance, `+28` unit. `probes/groundat.c` is the ground finder built on it.
- **Status:** `apply_status(HF_STATUS_STUN_LARGE /* 39 */, seconds)` paralyses; renew it to hold an enemy.
- **Aim** sways with the head; average it over ~0.2 s before laying lines or fields along it.

## Mod Options Menu (live)

- At most **32 options per mod**; rows past 32 are dropped silently.
- A slider with a **negative minimum** reads back as 0. Keep ranges at 0 and above.
- Saved menu values are pushed to the DLL at load (they override the .ini for that session).

## HUDs (HD2Runtime)

- Skill icons: `images/<id>.png`, 256×256. An orange-and-white picture on a transparent background works; the build converts it to the game's R/G masks.
- The Lua HUD reads the DLL's exported state struct through LuaJIT's ffi; keep the struct layout versioned.
