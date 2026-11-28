# Freezing what is in flight (research, from 2026-11-26)

Goal: a true time stop, with the shots already in the air held where they are and let go as they were. The kit base's
pose hold (`kitbase/kb_hold.h`) already stops enemies one at a time; this is the projectile half. Game build 25480438;
addresses are `game.dll` RVAs. Read from the game's code (the dump) and HellForge's shot rules
(`native/mod-loader/actions.c`, YuiAstin/hellforge at c01f36b). Shot Freeze Probe (`probes/shot-freeze-probe`) is the
test: 0.1.0 for the shot pool (played 2026-11-27), 0.2.0 for grenades, missiles, rockets and fuses (not played yet).

## The shot pool

`[game+0x347CEA8]` (the SDK's `hf_global_ProjectileSystem`), 2048 slots, a ring (`slot = n & 0x7FF`):

| Where | What |
|---|---|
| `+0x28` u8 | 1 while shots run (in a mission) |
| `+0x30` u32 | shots fired so far (HellForge's new-shot counter) |
| `+0x203C + 2·slot` u16 | flags: `2` live, `1` ended, `4` exempt from the slow-shot rule [I], `0x8000` to be removed |
| `+0x3040 + 0x70·slot` | the flight record (below) |
| `+0x3B040 + 0x24·slot` | its source (`+0x0C` the shooter entity) |
| `+0x4D040 + 0xC8·slot` | its hit record (`+0x00` creditor, `+0x34` damage and penetration multipliers) |
| `+0xE5040 + 4·slot` u32 | its type (the ProjectileSettings row) |
| `+0xE7040` u32, `+0xE7044…` 0x20 each | this frame's moved shots: slot, and where it was before the move (`+0x10`) |

### The flight record

| Off | What |
|---|---|
| `+0x00` vec3 | position |
| `+0x0C` vec3 | velocity |
| `+0x18` f32 | time carried over (the step adds the frame's time and zeroes it) |
| `+0x1C` f32 | gravity |
| `+0x24`, `+0x28` f32 | drag terms (passed to the integrator) |
| `+0x2C` f32 | starting speed (HellForge scales it with the speed) |
| `+0x30` f32 | distance flown |
| `+0x34` f32 | lifetime left |
| `+0x48` u32 | substeps per frame |
| `+0x50` u32 | a unit it rides (0 none; its position then follows the unit) |
| `+0x58` | an effect handle (-1 none) |

## The step, `0x13AB0E0` (every frame, all slots)

- **The frame's time is `[game+0x3326348]+4`. When it is 0 the whole step is skipped,** which is how the game's own pause
  holds shots. A mod can't use that for one side of a fight: it stops every shot.
- Per live shot: the time carried at `+0x18` plus the frame's time, split over `+0x48` substeps, integrated by
  `0x13AAE30` (velocity, gravity `+0x1C`, drag `+0x24`/`+0x28`). A shot riding a unit (`+0x50`) follows it instead.
- **The slow-shot rule (`0x13AB6D5`):** a shot slower than **0.1 ×** its starting speed `+0x2C` is ended (unless flag
  `4`, or it rides a unit). So zeroing a shot's velocity ends it.
- **Lifetime (`0x13AB82D`):** `+0x34` goes down by the frame's time, moving or not; at 0 the shot ends (its expiry
  explosion, if it has one).
- The distance moved is added to `+0x30`, and the shot is listed at `+0xE7040` for `0x13AC1F0`: the hits along that
  frame's segment, then the fly-by sounds.

## The freeze (as the probe does it)

Data writes only, on the game thread; no code patched:

- **Freeze:** save the record. Set the velocity and the starting speed `+0x2C` both to a millionth of what they were (the
  slow-shot rule reads the same ratio; a 900 m/s round moves under 1 mm/s), and gravity `+0x1C` to 0.
- **Each frame while frozen:** write the saved lifetime back to `+0x34`, and the scaled velocity back (drag would slow it
  under the rule's tenth).
- **Release:** write the saved velocity, gravity, starting speed and lifetime back. It should go on exactly as it was.
- A frozen shot still checks hits along its (near zero) segment, so something that walks into it should still be hit.
  For a time stop that is fine: the enemies are held too.

## What 0.1.0 found (played 2026-11-27, four sessions)

- **Method A works.** 12 of 12 shots held 3 s with 0.003 m drift, and on release flew on at their old speed
  (a Liberator round 226 m in 0.3 s). Enemy and player shots in flight at the time were held too (up to 24 at once).
- **Method B ends the shot**, as the slow-shot rule predicts (all four ended 0.02 s in). An explosive round that ends
  goes off.
- Its unattended test rounds (four shots upward every 12 s from 20 s into a mission, Eruptor rounds in cycles 3, 4, 7
  and 8) were the explosions Yui kept seeing 45 s to 1:45 into every mission. 0.2.0 has no unattended rounds.

## Status (0.2.0)

| What | Where it lives | Freeze | Tested |
|---|---|---|---|
| Bullets, Eruptor rounds (shot pool) | projectile pool | velocity and starting speed x 1e-6, gravity 0, lifetime held | **Works** (0.1.0, 2026-11-27): 12 of 12 held 3 s, drift 0.003 m, flew on at full speed |
| Same, velocity set to 0 | projectile pool | — | **Ends the shot** (the slow-shot rule); an explosive round goes off |
| Rockets (EAT, Recoilless, RL-77), grenade launcher rounds | projectile pool (assumed: they have pool projectile types) | method A plus the position written back every tick | 0.2.0, not yet played |
| Thrown grenades and knives | physics bodies (Throwable component) | velocity 0 and position written back every tick through the engine's actor table; the old velocity given back | 0.2.0, not yet played |
| Fuses (grenades, missiles, anything with an Explosive component) | Explosive component | the three countdowns written back every tick | 0.2.0, not yet played |
| Seeking missiles (Spear, P-33, W.A.S.P.) | SeekingMissile component | the game's own engine switch off, the clocks held | 0.2.0, not yet played |

0.1.0 ran ten test rounds on its own in every mission (four shots fired upward, Eruptor rounds in half of them). Those
were the explosions 45 s to 1:45 into each mission (Yui, 2026-11-27). 0.2.0 does nothing until Ctrl+F11.

## Thrown grenades and knives

Throwable manager `[game+0x33264C0]` (component 259; ThrowableUpdate 0x6C3AE0):
- +0x20 count; +0x48 headers, 8 bytes each (header +0 the entity's resource, +8 the entity, +0xC its unit);
- +0x50 instances, 0x78 bytes each: +0 the physics actor (-1 none), +4 the actor to make, +0x4C its last velocity;
- +0x60 flags, 0x2C bytes each: the update moves one only while bytes +0 and +1 are both set (0x6C3BD5).

The engine's actor table `[game+0x3326338]` (a table of engine functions, filled at start):

| Slot | What | Read from |
|---|---|---|
| +0x50 | position(actor, out vec3) | 0x5A0C7E (a distance from it) |
| +0x60 | set position(actor, vec3) | 0x5B66A6, 0x6C4803 |
| +0xA0 | set velocity(actor, linear, angular) | 0x5B6696 (zeros to stop a body), 0x6C4819 |
| +0xA8 | velocity(actor, linear out, angular out or NULL) | 0x6C3C77, 0x6C4792 |

The actor swap at 0x6C4792..0x6C4819 copies +0x50 and +0xA8 of the old actor into +0x60 and +0xA0 of the new one.

## Fuses

Explosive manager `[game+0x3326728]` (component 255; the world update calls 0x8CAFF0 for each, 0x573612):
+0x28 count, +0x50 headers, +0x60 instances, 0x40 bytes each:

| Off | What |
|---|---|
| +0x00 | u8 done (the update does nothing more) |
| +0x02 | u8 active; the world update then sets +0x15 |
| +0x04 | f32 countdown; at 0 it calls 0x8CA7E0 |
| +0x08 | f32 countdown (modes other than 0 to 3) |
| +0x0C | u8 fuse lit |
| +0x10 | f32 fuse left; when it reaches 0 with +0x0C set, the explosion (record +0x24) is requested (0x8CB1FA) and +0x0C cleared |
| +0x38 | u32 mode |

## Seeking missiles

See [lock-on.md](lock-on.md) for the manager `[game+0x3326DF0]`. For a freeze: the update 0x642760 adds the frame time
to the age (clocks +8, 0x64286C) and moves a missile only while state +0x35 is set (0x6429B9). It turns the engine
on once, when the age passes record +0x38 (0x642878, one-shot flag clocks +2). SetMissileEngine (0x645B80) is the
game's own switch, as a silo remote uses it.

## Still not covered

- Beams, arcs and explosions already going off.
- **Co-op:** each machine likely steps its own copy of the shots; not read.

## What 0.3.0 found (played 2026-11-28): damage held back while frozen

Yui used only gas, fire and arc weapons during the stops (Hina's kit active). HellForge's damage-pre cancel removes the
amount but the hit still happens, so hit markers keep appearing.

| Element | Kind | What | Seen |
|---|---|---|---|
| 5 | 2 (damage over time) | gas (Veterancy's `gas_element = 5`, learned from gas armaments) | 2,530 ticks of about 8, 19,386 in all |
| 3 | 4 (explosion) and 6 (ability) | Arc Thrower: kind 4 hits of 0, kind 6 hits of 75 | 14 + 6 hits |
| 7 | 6 (ability) | fire (by elimination: the only other weapon used); about 1,000 per hit before armour | 11 hits, 11,250 |

No burning damage over time (kind 2, element 7) showed up; the flamer's hits were withheld before any burn.
HellForge has no call to deal the withheld damage later, so for now it is lost, not stored.
