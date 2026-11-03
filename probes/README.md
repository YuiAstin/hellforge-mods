# Probes

Research code with no gameplay purpose, kept for what it found. Build 25480438 only.

| Probe | What it found |
|---|---|
| `warp-probe/` | A HellForge mod that hooks the LIFT-182 Warp Pack's position setter (`game.dll+0x5A2BB0`) and launch flag (`+0x5A3D80`) with pass-through detours and logs every call. It showed the warp calls the setter from `+0x88D795` with (NULL, the Helldiver, the destination) and then zeroes velocity, which is how Faelynn's Loyal Dash and Lenna's Leaping Pursuit teleport. Drop the folder into `HellForge/mods` to run it. |
| `ai-probe/` | (0.2.5, to run) Whether the sentries' AI records (perception lists, behaviour records: docs/research/ai-modding.md) also run the enemies: Shift+F7 surveys the enemies within 60 m (who each is after, whether she is in its perception lists), F7 counts the enemies after her each second, Ctrl+F7 blanks her entries' sense bits and forces re-picks for 10 s (Smarter Guard Dogs' hiding, tried on enemies). 0.2.0, patrols (Lemuen's sniper nest): Alt+F7 traces the navigation records of enemies within 150 m for 30 s, logging which fields sit within 30 m of her (move around: the destination field follows her); Shift+Alt+F7 sends those enemies to a point 80 m to her side with set_destination, then traces whether they go. |
| `gun-probe/` | (0.1.0, to run) A HUD-only data mod (HD2Runtime addon, source in `hud-src/`): whether an addon can spawn one of the game's own gun models (the Guard Dog's rifle, the machine guns) as a visual only, through `World.spawn_unit`, and keep it hovering beside the view with `Unit.set_local_position` / `set_local_rotation`. For Exusiai's summoned guns. F6 spawns two for 10 s, Shift+F6 logs which engine calls and gun models the addon can reach. |
| `groundat.c` | The ground finder (a downward ray through the game's own raycast, skipping the caster and slivers) that keeps dashes and leaps out of walls and from landing underground. A snippet from the mods, not a standalone file. |
| `keytest.c` | A desktop test of the .ini key parser (`Shift+Q`, `Ctrl+Alt+F`, `F5`, `Mouse4`, `#81`). Build with any C compiler and run. |
| `data/faelynn-hit-probe.log` | A Faelynn probe build's log of raw hit records before and after the hit calculation; it located the armour-penetration floats at `+0x9C/+0xA0/+0xA4` that Corrosion raises. |

Leva's lockdown damage probe (every hit and kill near her first lockdown of a mission) is still in `mods/leva/src/leva.c`; its findings on arc hits are in [docs/research/game-notes.md](../docs/research/game-notes.md).
