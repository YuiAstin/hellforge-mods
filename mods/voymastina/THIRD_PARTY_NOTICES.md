# Notices

The hit-result layout (knockdown flag at result +0x1E), the hooked game functions and their instruction guards, the
entity descriptor lookup and the scene-graph position read are taken from **Helldiver Survival Rework 0.3.2** by
enochlophobianumerophobia (https://hf-mod.com/Mods/Details/14), MIT licensed: its licence is kept in
LICENSE-Helldiver-Survival-Rework.txt. The head and head-aim scene-graph nodes (90 and 91) come from Scope of
Authority's Apex knockdown notes. Their research in turn used **HD2Runtime by SkyeShade**
(https://github.com/SkyeShade/HD2Runtime). The HUD uses HD2Runtime's public overlay API.

Pile Bunker's lunge, its follow-up dash and the Pile Bunker charge call the game's own position and velocity setters
the way the LIFT-182 Warp Pack does (game.dll+0x5A2BB0 and +0x4A7550). The functions were located from **HD2Runtime by
SkyeShade**'s warp research, and their arguments were confirmed with a research probe logging live Warp Pack calls. The
wall and ground checks call the game's raycast (helldivers2.exe+0x7F4590, world id +0x79F860) with the arguments and
scheduler-idle guard used by **Helldiver Survival Rework 0.3.2**'s cover check (MIT, see above); the landing check skips
leaves and canopies by their collision filter, read the way that cover check reads it.

Her extra armour penetration raises a hit's armour penetration (hit +0x9C..+0xA4, found with a research probe) inside
the hooked hit calculation from **Helldiver Survival Rework 0.3.2** (MIT, see above). The explosions' packages are
taken from HellForge's mappings (references.json). The Hellbomb on exhaustion reads her stamina where the game keeps it
([game.dll+0x3326D20] + 0x547854, 0x78 per Helldiver, found with Cheat Engine), by HellForge's own per-Helldiver action
index. The settings rows use **Mod Options Menu** by CowboyBingus (0BSD) through its public API. The HUD follows the
stratagem list through **HD2Runtime**'s slot overlay (internal modules) and is built with HD2Runtime's SDK.

## The Sinbreaker and her two kits (0.4.0)

- **Her stratagem outside the loadout:** each mission one entry is added to her own stratagem record, as the game grants
  Reinforce and Resupply. The record's layout and the game's append (game.dll+0x66F190) are **HD2Runtime by SkyeShade**'s
  research (https://github.com/SkyeShade/HD2Runtime); the record is read as the Veterancy System's Maxigun refund reads
  it. The Sinbreaker is a copy of the EXO-55 Breakthrough (premium: the Exo Experts warbond), and the grant follows
  HellForge's premium lock: a player who has not bought it does not get it.
- **Who pilots what:** the seat reading (the command bus, entry 117) follows **Apache - Rise Above** by RSNLRiggs, after
  Khi's research, and the camera pose read (World + 0x230920) follows its gunner turret, reused with permission. The
  engine physics functions used to push corpses are bound and checked as Apache - Rise Above binds them.
- **The suit's position:** the unit API (local_position) is found as **EXO Beacon Launcher** by
  enochlophobianumerophobia (MIT) found it.
- **The Breakthrough's package and the stratagem facts** come from **HD2Runtime**'s stratagem and entity catalogues;
  the friend list (Helldiver- and Super Earth-side units) from its enemy_controls catalogue.
