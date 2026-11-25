# Probes

Research code with no gameplay purpose, kept for what it found. Build 25480438 only.

| Probe | What it found |
|---|---|
| `warp-probe/` | A HellForge mod that hooks the LIFT-182 Warp Pack's position setter (`game.dll+0x5A2BB0`) and launch flag (`+0x5A3D80`) with pass-through detours and logs every call. It showed the warp calls the setter from `+0x88D795` with (NULL, the Helldiver, the destination) and then zeroes velocity, which is how Faelynn's Loyal Dash and Lenna's Leaping Pursuit teleport. Drop the folder into `HellForge/mods` to run it. |
| `groundat.c` | The ground finder (a downward ray through the game's own raycast, skipping the caster and slivers) that keeps dashes and leaps out of walls and from landing underground. A snippet from the mods, not a standalone file. |
| `keytest.c` | A desktop test of the .ini key parser (`Shift+Q`, `Ctrl+Alt+F`, `F5`, `Mouse4`, `#81`). Build with any C compiler and run. |
| `data/faelynn-hit-probe.log` | A Faelynn probe build's log of raw hit records before and after the hit calculation; it located the armour-penetration floats at `+0x9C/+0xA0/+0xA4` that Corrosion raises. |
| `frv-package-probe/` | The call-in package at stratagem row +0xA8 of the three FRVs, read from copies of their stratagems on HellForge 0.5.0-beta (2026-11-25): row 26 M-103 Supply `0x149F685717737CAE`, row 105 M-102 Gunner `0x0BDF199F7AC14F43`, row 135 M-104 Incinerator `0x2CEE40DFBD0B174D`; row +0xF8 is 0 on all three. |
| `wield-probe/` | In progress (W's King of Hearts): a GL-21 into the Helldiver's hands and her weapon back. Seen so far: the game's weapon switch is `SwitchWeapon(selection)` then `Wield(wielder, slot 0, item, 0)` and `SetWielder`; a held item is selection 5 at inventory record +0x10. |

Leva's lockdown damage probe (every hit and kill near her first lockdown of a mission) is still in `mods/leva/src/leva.c`; its findings on arc hits are in [docs/research/game-notes.md](../docs/research/game-notes.md).
