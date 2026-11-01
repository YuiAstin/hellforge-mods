# Docs

## Research

| File | What |
|---|---|
| [research/game-notes.md](research/game-notes.md) | What the GFL2 kits established: HellForge's read-only mod folders, damage-event kinds, the hit hooks and record layout, arc rows, explosions and their packages, Helldiver health / stamina / teleport / raycast, Mod Options Menu limits. |
| [research/native-tables.md](research/native-tables.md) | The game's settings lists (explosion, arc, damage, projectile, stratagem), how More Plasma Stratagems adds explosions of its own, and how Leva 0.5.0 adds a private explosion and arc for her lockdown. Also its other reusable patterns (typed writes, private entities, Lua mod options, HellForge-built icons). |
| [research/ai-modding.md](research/ai-modding.md) | AI modding in HellForge: friendly AI (sentries, guard dogs), enemy AI and the tools each relies on, tagged proven / static / speculative. |
| [research/teleport-blink.md](research/teleport-blink.md) | How to move a Helldiver from a C mod (blink / teleport), from the LIFT-182 Warp Pack's own code. |
| [research/ias-stutter.md](research/ias-stutter.md) | Why HD2Runtime + IAS stutter together, and the garbage-collector tuning that became Smooth GC. |

The research notes were written while working, so their file paths point at a scratch folder (`<scratch>`, or `S` / `HF` / `RT` abbreviations defined at the top of each); the findings and addresses are what matter.

## Design

- [leva-design.md](leva-design.md): Leva's original kit plan.
- [leva-future.md](leva-future.md): ideas parked for later (a Lenna + Leva duo arc, a Wise Support rework).
