# Probes

Research code with no gameplay purpose, kept for what it found. Build 25480438 only.

| Probe | What it found |
|---|---|
| `warp-probe/` | A HellForge mod that hooks the LIFT-182 Warp Pack's position setter (`game.dll+0x5A2BB0`) and launch flag (`+0x5A3D80`) with pass-through detours and logs every call. It showed the warp calls the setter from `+0x88D795` with (NULL, the Helldiver, the destination) and then zeroes velocity, which is how Faelynn's Loyal Dash and Lenna's Leaping Pursuit teleport. Drop the folder into `HellForge/mods` to run it. |
| `booster-table-probe/` | A HellForge mod that logs the game's 56-byte booster rows (`game.dll+0x32FDA90`) and every instruction and pointer that addresses them, for adding a new booster. Drop the folder into `HellForge/mods` and send `modloader.log`. |
| `groundat.c` | The ground finder (a downward ray through the game's own raycast, skipping the caster and slivers) that keeps dashes and leaps out of walls and from landing underground. A snippet from the mods, not a standalone file. |
| `keytest.c` | A desktop test of the .ini key parser (`Shift+Q`, `Ctrl+Alt+F`, `F5`, `Mouse4`, `#81`). Build with any C compiler and run. |
| `data/faelynn-hit-probe.log` | A Faelynn probe build's log of raw hit records before and after the hit calculation; it located the armour-penetration floats at `+0x9C/+0xA0/+0xA4` that Corrosion raises. |

Leva's lockdown damage probe (every hit and kill near her first lockdown of a mission) is still in `mods/leva/src/leva.c`; its findings on arc hits are in [docs/research/game-notes.md](../docs/research/game-notes.md).
