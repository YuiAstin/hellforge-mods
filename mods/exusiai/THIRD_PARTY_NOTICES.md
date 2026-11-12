# Notices

Exusiai is built on the kit base (`src/kitbase/`, shared by the author's character kits). Its hit-result layout, the
hooked game functions and their instruction guards, the entity descriptor lookup and the scene-graph position read are
taken from **Helldiver Survival Rework 0.3.2** by enochlophobianumerophobia (https://hf-mod.com/Mods/Details/14), MIT
licensed: its licence is kept in LICENSE-Helldiver-Survival-Rework.txt. The head and head-aim scene-graph nodes (90 and
91) come from Scope of Authority's Apex knockdown notes. Their research in turn used **HD2Runtime by SkyeShade**
(https://github.com/SkyeShade/HD2Runtime).

The summoned guns fire the AX/AR-23 Guard Dog's round (projectile type 7), found in **HD2Runtime**'s attack catalogue;
the weapons' fire-rate field is the one HellForge's SDK names (ProjectileWeapon +0x04). Her stamina is read from the
game's action manager ([game.dll+0x3326D20] + 0x547854, 0x78 per Helldiver).

The settings rows use **Mod Options Menu** by CowboyBingus (0BSD) through its public API. The HUD uses HD2Runtime's
public overlay API, and its skill panel follows the stratagem list through **HD2Runtime**'s slot overlay (internal
modules).

Exusiai is a character of Arknights (Hypergryph / Yostar). This is an unofficial fan mod; no character model is included.
The three skill icons (hud-src/images) and the mod icon are Arknights art, © Hypergryph / Yostar.
