# Notices

W is built on the kit base (`src/kitbase/`, shared by the author's character kits). The entity descriptor lookup, the
scene-graph position read and the health record read are taken from **Helldiver Survival Rework 0.3.2** by
enochlophobianumerophobia (https://hf-mod.com/Mods/Details/14), MIT licensed: its licence is kept in
LICENSE-Helldiver-Survival-Rework.txt. The head scene-graph nodes come from Scope of Authority's Apex knockdown notes.
Their research in turn used **HD2Runtime by SkyeShade** (https://github.com/SkyeShade/HD2Runtime).

The inventory, wielder, magazine, avatar, package, unit and world functions and globals are **HellForge**'s mappings
(SkyeShade), from HD2Runtime's research. Frozen enemies are held with the game's own position and velocity setters,
the way the LIFT-182 Warp Pack moves a Helldiver, located from HD2Runtime's warp research.

The rule for reading a unit's place only among rare unit types is **Exo Autopilot**'s finding (marti): the method only,
none of their code is included.

The HUD uses HD2Runtime's public overlay API, and its skill panel follows the stratagem list through **HD2Runtime**'s
slot overlay (internal modules). The markers over her mines, her marks and the enemies she holds: parts adapted from
**Chef Shared Loader by Th3chef** (https://github.com/Th3chef/Chef-Shared-Loader): finding the view camera, projecting
a world point to the screen and the behind-the-camera check, used with Th3chef's permission. None of the loader itself
is included.

W is a character of **Arknights** (Hypergryph / Yostar). This is an unofficial fan mod; no character model is included.
The three skill icons (hud-src/images) and the mod icon are Arknights art, © Hypergryph / Yostar.
