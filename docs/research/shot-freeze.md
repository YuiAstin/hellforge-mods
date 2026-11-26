# Freezing shots in flight (research, 2026-11-26)

Goal: a true time stop, with the shots already in the air held where they are and let go as they were. The kit base's
pose hold (`kitbase/kb_hold.h`) already stops enemies one at a time; this is the projectile half. Game build 25480438;
addresses are `game.dll` RVAs. Read from the game's code (the dump) and HellForge's shot rules
(`native/mod-loader/actions.c`, YuiAstin/hellforge at c01f36b). **Not tested in game yet:** Shot Freeze Probe 0.1.0
(`probes/shot-freeze-probe`) is the test.

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

## What the probe answers

1. Do frozen shots stay alive and in place for 3 s (method A), and does zeroing the velocity end them, as the slow-shot
   rule predicts (method B)?
2. On release, do they fly on at their old speed, with their lifetime as it was?
3. Do tracers and effects stay drawn where the shot is?
4. By hand (Ctrl+F11): your own fire and enemy fire, frozen as it leaves the barrel.

## Not covered

- **Grenades and thrown items:** physics entities, not pool shots (the kit base's velocity setters are the route).
- **Seeking missiles:** their own manager, `[game+0x3326DF0]` (see [lock-on.md](lock-on.md)): speed at local instance
  `+0x28`/`+0x30`, age at clocks `+8`.
- Beams, arcs and explosions already going off.
- **Co-op:** each machine likely steps its own copy of the shots; not read.
