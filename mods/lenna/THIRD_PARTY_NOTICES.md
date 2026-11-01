# Notices

The hit-result layout (knockdown flag at result +0x1E), the hooked game functions and their instruction guards, the
entity descriptor lookup and the scene-graph position read are taken from **Helldiver Survival Rework 0.3.2** by
enochlophobianumerophobia (https://hf-mod.com/Mods/Details/14), MIT licensed: its licence is kept in
LICENSE-Helldiver-Survival-Rework.txt. The head and head-aim scene-graph nodes (90 and 91) come from Scope of
Authority's Apex knockdown notes. Their research in turn used **HD2Runtime by SkyeShade**
(https://github.com/SkyeShade/HD2Runtime). The HUD uses HD2Runtime's public overlay API.

Leaping Pursuit's teleport calls the game's own position and velocity setters the way the LIFT-182 Warp Pack does
(game.dll+0x5A2BB0 and +0x4A7550). The functions were located from **HD2Runtime by SkyeShade**'s warp research, and
their arguments were confirmed with a research probe logging live Warp Pack calls. The wall and ground checks call
the game's raycast (helldivers2.exe+0x7F4590, world id +0x79F860) with the arguments and scheduler-idle guard used by
**Helldiver Survival Rework 0.3.2**'s cover check (MIT, see above).

Her stamina is read from the game's action manager ([game.dll+0x3326D20] + 0x547854, 0x78 per Helldiver), found with
Cheat Engine for this mod; the per-Helldiver index comes from HellForge's own action-record lookup. The settings rows use
**Mod Options Menu** by CowboyBingus (0BSD) through its public API. The HUD's skill panel follows the stratagem list
through **HD2Runtime**'s slot overlay (internal modules), and its icons are built with HD2Runtime's SDK.
