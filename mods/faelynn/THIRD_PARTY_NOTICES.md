# Notices

The hit-result layout (knockdown flag at result +0x1E), the hooked game functions and their instruction guards, the
entity descriptor lookup and the scene-graph position read are taken from **Helldiver Survival Rework 0.3.2** by
enochlophobianumerophobia (https://hf-mod.com/Mods/Details/14), MIT licensed: its licence is kept in
LICENSE-Helldiver-Survival-Rework.txt. The head and head-aim scene-graph nodes (90 and 91) come from Scope of
Authority's Apex knockdown notes. Their research in turn used **HD2Runtime by SkyeShade**
(https://github.com/SkyeShade/HD2Runtime). The HUD uses HD2Runtime's public overlay API.

Loyal Dash's teleport calls the game's own position and velocity setters the way the LIFT-182 Warp Pack does
(game.dll+0x5A2BB0 and +0x4A7550). The functions were located from **HD2Runtime by SkyeShade**'s warp research, and
their arguments were confirmed with a research probe logging live Warp Pack calls. The wall and ground checks call
the game's raycast (helldivers2.exe+0x7F4590, world id +0x79F860) with the arguments and scheduler-idle guard used by
**Helldiver Survival Rework 0.3.2**'s cover check (MIT, see above).

Knife-Eared Hunt keeps her stamina full by writing it where the game keeps it ([game.dll+0x3326D20] + 0x547854, 0x78
per Helldiver, found with Cheat Engine), by HellForge's own per-Helldiver action index. The settings rows use **Mod
Options Menu** by CowboyBingus (0BSD) through its public API. The HUD follows the stratagem list through **HD2Runtime**'s
slot overlay (internal modules) and is built with HD2Runtime's SDK.

Loyal Dash's landing check skips leaves and canopies by their collision filter, read the way **Helldiver Survival
Rework 0.3.2**'s cover check reads it (MIT, see above).

Last Bark reads her health from the game's health records (game.dll+0x3326F88, the entity hash and record layout from
**HD2Runtime by SkyeShade**'s research). Corrosion's armour pierce raises a hit's armour penetration (hit +0x9C..+0xA4,
found with a research probe) inside the hooked hit calculation from **Helldiver Survival Rework 0.3.2** (MIT, see above).
The Hellbomb on exhaustion reads her stamina at the same place Knife-Eared Hunt writes it.
