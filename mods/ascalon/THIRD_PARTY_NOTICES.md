# Notices

Ascalon is built on the kit base (`src/kitbase/`, shared by the author's character kits). Its hit-result layout, the
hooked game functions and their instruction guards, the entity descriptor lookup and the scene-graph position read are
taken from **Helldiver Survival Rework 0.3.2** by enochlophobianumerophobia (https://hf-mod.com/Mods/Details/14), MIT
licensed: its licence is kept in LICENSE-Helldiver-Survival-Rework.txt. Their research in turn used **HD2Runtime by
SkyeShade** (https://github.com/SkyeShade/HD2Runtime).

How she is hidden follows what the **CAMO-EX01 Experimental Optical Cloak Pack** does, learned by reading it: a
Helldiver is the avatar unit plus one unit per armour part sharing its root; a unit handle is its scene id times four;
its "compatibility" cloak hides each visible mesh and hides it again as the game shows it; and enemies' visual
detection is the per-entity visibility row at game.dll+0x3326540 (base +0, floor +12). No code, material or other file
of that mod is included. Forgetting her uses the behaviour and perception records the author's Lost Contact works on.

The HUD uses HD2Runtime's
public overlay API and the engine's Lua API.

Ascalon is a character of Arknights (Hypergryph / Yostar). This is an unofficial fan mod; no character model is included.
