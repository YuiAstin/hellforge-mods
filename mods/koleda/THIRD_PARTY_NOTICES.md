# Notices

Koleda is built on the kit base (`src/kitbase/`, shared by the author's character kits). Its hit-result layout, the
hooked game functions and their instruction guards, the engine physics calls that keep the FRV Sinveiler steady (the unit's physical actor, as Apache - Rise Above by
RSNLRiggs after Khi pushes its FRV-based body), the entity descriptor lookup and the scene-graph position read are
taken from **Helldiver Survival Rework 0.3.2** by enochlophobianumerophobia (https://hf-mod.com/Mods/Details/14), MIT
licensed: its licence is kept in LICENSE-Helldiver-Survival-Rework.txt. The head scene-graph node (90) comes from Scope
of Authority's Apex knockdown notes. Their research in turn used **HD2Runtime by SkyeShade**
(https://github.com/SkyeShade/HD2Runtime).

Her view is the camera's pose as **Apache - Rise Above** by RSNLRiggs (after Khi) reads it (the unit object's world,
+0x230920), used with permission. The seat read (is she piloting the Sinveiler) is Khi's reading of the command bus, as
Apache - Rise Above does it. The stratagem record the Sinveiler's call is granted into, and the game's append it copies,
are **HD2Runtime**'s research. The navigation records and set_destination (the Sinveiler's pathing) are from the author's
Nav Probe and Lost Contact research. The settings rows use **Mod Options Menu** by CowboyBingus (0BSD) through its
public API. The HUD uses HD2Runtime's public overlay API, and its skill panel follows the stratagem list through
**HD2Runtime**'s slot overlay (internal modules).

The Sinveiler's driving (planned, `src/sinner_drive.h`): how to drive an empty exosuit with the game's own locomotion (its
stick input, engine and physics mover) follows **Exo Companion by M!ni$try0fSc!eиce**, read from its strings and
settings. Its method only; none of its code is included, and this part does not run yet. Credit to her. The Sinveiler's fire (rounds of her
own spawned from its arms) follows the author's Exusiai kit.

The Sinveiler's planned model is **Exosuit Concept Mech - LUT by Killerco** (Killercat;
https://www.nexusmods.com/helldivers2/mods/7974), re-exported under its own name, under his blanket permission to edit
and reupload his mods with credit. This version still uses the vanilla EXO-45 Patriot.

The marker over the Sinveiler: parts adapted from **Chef Shared Loader by Th3chef**
(https://github.com/Th3chef/Chef-Shared-Loader): finding the view camera, projecting a world point to the screen and the
behind-the-camera check, used with Th3chef's permission. None of the loader itself is included.

The Sinveiler's walking (hud-src/koleda_hud.lua, 0.1.7) follows Exo Autopilot 1.2 by marti (a Chef Shared Loader mod): an
EXO-45 walks through its own animation state machine, its walk state and the move_angle / move_speed / yaw variables
held every frame, and its steering correction. Its method only; none of its code is included. Credit to marti.

Koleda is a character of Girls' Frontline 2: Exilium (MICA Team / Sunborn). This is an unofficial fan mod; no character
model is included. The skill icons (hud-src/images) and the mod icon are Girls' Frontline 2 art, © MICA Team / Sunborn.
