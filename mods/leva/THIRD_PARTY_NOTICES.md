# Notices

The hit-result layout, the hooked game functions and their instruction guards, the entity descriptor lookup and the
scene-graph position read are taken from **Helldiver Survival Rework 0.3.2** by enochlophobianumerophobia
(https://hf-mod.com/Mods/Details/14), MIT licensed: its licence is kept in LICENSE-Helldiver-Survival-Rework.txt. The
head and head-aim scene-graph nodes (90 and 91) come from Scope of Authority's Apex knockdown notes. Their research in
turn used **HD2Runtime by SkyeShade** (https://github.com/SkyeShade/HD2Runtime). The HUD uses HD2Runtime's public
overlay API, follows the stratagem list through HD2Runtime's slot overlay (internal modules) and is built with
HD2Runtime's SDK.

Her arcs find the ground with the game's raycast (helldivers2.exe+0x7F4590, world id +0x79F860), called with the
arguments and scheduler-idle guard used by **Helldiver Survival Rework 0.3.2**'s cover check, skipping leaves by their
collision filter as that check reads it (MIT, see above).

The Hellbomb on exhaustion reads her stamina where the game keeps it ([game.dll+0x3326D20] + 0x547854, 0x78 per
Helldiver, found with Cheat Engine), by HellForge's own per-Helldiver action index. The settings rows use **Mod Options
Menu** by CowboyBingus (0BSD) through its public API.

Her lockdown's own explosion and arc (`native_rows`) extend the game's explosion and arc lists the way **More Plasma
Stratagems 1.1.0 by HadePasianax** (https://hf-mod.com/Mods/Details/114, MIT licensed as its mod page states) extends
the explosion list: the list's address, size and reader guards, and its method of copying the list near game.dll and
re-pointing every decoded site and bound (its `native_plasma_explosions.h`), adapted to a second list. Its licence is
kept in LICENSE-More-Plasma-Stratagems.txt. The explosion-to-arc link (ExplosionInfo +120) and the arc's damage
(ArcInfo +36) are from HD2Runtime's research. The damage list's current place is read from the first site in
HellForge's own damage_sites.h.
