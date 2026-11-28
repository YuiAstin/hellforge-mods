# Docs

## Research

| File | What |
|---|---|
| [research/game-notes.md](research/game-notes.md) | What the GFL2 kits established: HellForge's read-only mod folders, damage-event kinds, the hit hooks and record layout, arc rows, explosions and their packages, Helldiver health / stamina / teleport / raycast, Mod Options Menu limits. |
| [research/native-tables.md](research/native-tables.md) | The game's settings lists (explosion, arc, damage, projectile, stratagem), how More Plasma Stratagems adds explosions of its own, and how Leva 0.5.0 adds a private explosion and arc for her lockdown. Also its other reusable patterns (typed writes, private entities, Lua mod options, HellForge-built icons). |
| [research/ai-modding.md](research/ai-modding.md) | AI modding in HellForge: friendly AI (sentries, guard dogs), enemy AI and the tools each relies on, tagged proven / static / speculative. |
| [research/teleport-blink.md](research/teleport-blink.md) | How to move a Helldiver from a C mod (blink / teleport), from the LIFT-182 Warp Pack's own code. |
| [research/flight.md](research/flight.md) | Flight for the player's Helldiver: gravity (one world constant), the airborne flag, SetVelocity every frame, the Hover Pack's data, and the probes to run. |
| [research/ias-stutter.md](research/ias-stutter.md) | Why HD2Runtime + IAS stutter together, and the garbage-collector tuning that became Smooth GC. |
| [research/one-handed.md](research/one-handed.md) | What makes a weapon one-handed (Equipment +0xC4 bit 0, +0xC0 grip), where the records are, and the code that uses it. |
| [research/shot-freeze.md](research/shot-freeze.md) | Time stop, the in-flight half (works, as played): shots, rockets, grenades, the G-60 seeker, fuses, missiles, fire/gas/arc damage, particles and arcs held (Shot Freeze Probe). Combine with the pose freeze for a true time stop. |
| [research/particle-freeze.md](research/particle-freeze.md) | Slowing particle effects (flames, gas, smoke) for a time stop: the engine's particle world update at 2% of its dt (played; at 0 nothing is emitted). |
| [research/arc-freeze.md](research/arc-freeze.md) | Holding Arc Thrower bolts for a time stop: the arc system's update and post pass skipped while frozen (played). |
| [research/homing.md](research/homing.md) | Homing shots: what works (a game-guided seeking missile handed a target) and what didn't (HellForge's `homing` rule on ordinary shots), notes for the Destroyer. |
| [research/lock-on.md](research/lock-on.md) | The game's lock-on and seeking missiles (static analysis plus the in-game recipe): where a missile's target lives and how to hand one to a sentry's or launcher's missiles. |

The research notes were written while working, so their file paths point at a scratch folder (`<scratch>`, or `S` / `HF` / `RT` abbreviations defined at the top of each); the findings and addresses are what matter.

## Design

- [kit-base.md](kit-base.md): the shared kit base: what the four GFL2 kits have in common, the layers, and the extraction steps.
- [leva-design.md](leva-design.md): Leva's original kit plan.
- [leva-future.md](leva-future.md): ideas parked for later (a Lenna + Leva duo arc, a Wise Support rework).
- [executor-design.md](executor-design.md): Executor the Ex Foedere's kit (built as `mods/executor`, a test build).
- [hina-design.md](hina-design.md): Hina's kit from Blue Archive (built as `mods/hina` on branch `hina-kit`, not played yet).
