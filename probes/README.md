# Probes

Research code with no gameplay purpose, kept for what it found. Build 25480438 only.

| Probe | What it found |
|---|---|
| `warp-probe/` | A HellForge mod that hooks the LIFT-182 Warp Pack's position setter (`game.dll+0x5A2BB0`) and launch flag (`+0x5A3D80`) with pass-through detours and logs every call. It showed the warp calls the setter from `+0x88D795` with (NULL, the Helldiver, the destination) and then zeroes velocity, which is how Faelynn's Loyal Dash and Lenna's Leaping Pursuit teleport. Drop the folder into `HellForge/mods` to run it. |
| `perception-probe/` | Ascalon research (docs/research/perception.md section 6): logs her visibility record and each nearby enemy's perception of her every 2 s; F6 sets her visibility base to 0 (hidden), Ctrl+F6 mutes or caps her noises at 15 m through an EmitNoise hook, Shift+F6 dumps everything. |
| `groundat.c` | The ground finder (a downward ray through the game's own raycast, skipping the caster and slivers) that keeps dashes and leaps out of walls and from landing underground. A snippet from the mods, not a standalone file. |
| `keytest.c` | A desktop test of the .ini key parser (`Shift+Q`, `Ctrl+Alt+F`, `F5`, `Mouse4`, `#81`). Build with any C compiler and run. |
| `data/faelynn-hit-probe.log` | A Faelynn probe build's log of raw hit records before and after the hit calculation; it located the armour-penetration floats at `+0x9C/+0xA0/+0xA4` that Corrosion raises. |

Leva's lockdown damage probe (every hit and kill near her first lockdown of a mission) is still in `mods/leva/src/leva.c`; its findings on arc hits are in [docs/research/game-notes.md](../docs/research/game-notes.md).
