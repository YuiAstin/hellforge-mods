# HellForge mods by Yui

Code mods for HellForge (offline Helldivers 2). Each folder under `mods/` is one HellForge mod, laid out exactly as it sits in HellForge's `mods` folder; HellForge builds the C sources itself before each session.

| Mod | Folder | What it does |
|---|---|---|
| Lenna | `mods/lenna` | Lenna's kit from Girls' Frontline 2: Wild Extinction, Leaping Pursuit, Hunting Strategy. T-Doll body. |
| Faelynn | `mods/faelynn` | Faelynn's gas kit from GFL2: Triple Maul, Knife-Eared Hunt, Loyal Dash and Pursuit chains, Corrosion armour-piercing. T-Doll body. |
| Leva | `mods/leva` | Leva's arc / area-lockdown kit from GFL2: Rational Suppression, Orderly Disruption, Quantum Calculation, Superconduct Blitz. T-Doll body. |
| Voymastina | `mods/voymastina` | Voymastina's kit from GFL2 in her own exosuit, the Sinbreaker (Sirius Fall, Eye of the White Mastiff, Pile Bunker), and her AK-15 kit on foot. A free Sinbreaker for players without the Exo Experts warbond. |
| Lemuen | `mods/lemuen` | Lemuen's sniper kit from Arknights: Persecution marks, Salve Etiam, Honesta Missio's scope build-up, Memento Mori's 120mm shells on marked enemies; markers and damage numbers on the HUD. |
| W | `mods/w` | W's kit from Arknights: King of Hearts puts a 40-round GL-21 in her hands whose blasts freeze enemies in their pose, Jack in the Box lays mines under enemies from 5 charges, D12 marks enemies, bug holes and structures and a second key blows the marks; her shots burst where they land, and enemies lose her while she holds her fire. |
| Executor | `mods/executor` | Executor the Ex Foedere's kit from Arknights: every enemy his shot hits heals him, The Chosen One's free rounds, area rounds at every enemy in a square in front of him, Empathy by Shotgun, and Testament Exacted, Close Combat and Damnatus Ex Foedere counted in rounds. |
| Exusiai | `mods/exusiai` | Exusiai's kit from Arknights: Charging Mode (every 4th burst sets off free bursts), Shooting Mode (two guns at her shoulders that fire with her), Overloading Mode (four more guns, each hunting its own target), Fast Cartridge and Angel's Blessings; skills and the guns' models on the HUD. |
| Koleda | `mods/koleda` | Koleda's kit from Girls' Frontline 2 (AK-12): the Sinveiler (her M-103 Supply FRV, or a Patriot) that takes her hits, fights by its gear and builds her Upshift; Snow Wolf's Eye, Tactical Routebook and Spectre on foot; Full Speed, Drift and The Winner at its wheel; skill panel with icons on the HUD. |
| Alva | `mods/alva` | Alva's support kit from Girls' Frontline 2 (AN-94): a shield for her and every ally around her, freezing fields and bursts, shatter damage on frozen enemies; Confectance. T-Doll body. |
| Vector | `mods/vector` | Vector's fire kit from Girls' Frontline 2: fire does nothing to her; Dead End Meltdown's igniting shots and fire bursts, Portent of Doom's napalm blast and Smolder, Searing Finale's napalm shells; fire that scales with the enemy's health and her own burn on machines the game's fire cannot hurt; Confectance Index. T-Doll body. Her own options screen where HellForge has screens over the game. |
| Ascalon | `mods/ascalon` | Ascalon's kit from Arknights: hidden until she makes noise (a shimmer cloak), every hit strikes everyone within 15 m, Mortal Interrogation's bleed marks, Pursuit, Endowment and Descent, a parry and a dash; skill panel and damage numbers on the HUD. |
| Lost Contact | `mods/lost-contact` | Enemies that lose sight of you stop tracking you and search where they last saw you; patrols walk between their outposts instead of straight at you. On / observe / off, in lost-contact.ini, on keys or on the MODS tab; a kit can limit them (Lemuen: patrols only). |
| Veterancy System | `mods/field-promotions` | Per-mission gear progression: your gear levels up as you use it, for that mission only. |
| Fire Mission | `mods/fire-mission` | A stratagem that calls mortar sentries onto a spot. |
| Primed Sure Footed | `mods/primed-sure-footed` | No knockdown or ragdoll on your Helldiver; flinch and push stay. |
| Stat Tuner | `mods/stat-tuner` | Per-item stat sliders on the in-game MODS tab, with presets. |
| Commando Plus | `mods/commando-plus` | Commando missions a little more forgiving: no mission timer, extra Super Destroyer call-ins, cleared outposts bank ammo boxes called in by the Outpost Cache, and a Super Destroyer window that grows with every base cleared (each an ini setting). |

The character kits and Veterancy System have HUDs drawn with HD2Runtime (Bingus Shared Loader + HD2Runtime). Their Lua sources sit in each mod's `hud-src/` (Veterancy System's in `huds/field-promotions-hud/`); the built HUD ships in the mod's `data/`. Lost Contact's `hud-src/` holds no HUD, only its MODS tab rows for Mod Options Menu (Lua, the Vanilla Plus Megapack's menu); it ships the same way.

## Working on them

```
python tools/deploy.py leva          # copy into ../HellForge/mods (game closed); your own leva.ini is kept
python tools/inigen.py leva          # regenerate mods/leva/leva.ini (the defaults) after changing settings in the source
python tools/precheck.py leva        # before a release: manifest, icons, options page, HUD, skill icons, .ini, build
python tools/package.py leva         # ../Releases/leva-<version>.zip, ready for HellForge's ADD MODS (runs precheck first)
python tools/release_art.py Vector portrait.png s1.png s2.png s3.png   # page images for a release (docs/release-format.md)
```

`--hellforge <path>` and `--out <path>` point the scripts elsewhere.

**Never change a mod folder while the game runs.** HellForge holds mod folders read-only during a session and switches off a mod whose folder gained files; the mods therefore never write their own `.ini`, and each release ships one with the defaults.

## Tools

- `tools/deploy.py`, `tools/package.py`, `tools/inigen.py`: above.
- `tools/hotreload/`: an experimental hot-reload host (a fixed "socket" mod that reloads a mod's logic DLL while the game runs). Untested in game.

## Docs and probes

- [`docs/`](docs/README.md): research notes (game addresses, damage events, arcs, explosions, HellForge's rules for mods, AI modding, teleporting, the IAS stutter) and design notes.
- [`probes/`](probes/README.md): research code kept for what it found, such as the Warp Probe mod that traced the Warp Pack's teleport.

## Credits

Hit, entity, scene-graph and raycast research from **Helldiver Survival Rework** by enochlophobianumerophobia (MIT) and **HD2Runtime** by SkyeShade; settings rows through **Mod Options Menu** by CowboyBingus. Each mod that uses it carries `THIRD_PARTY_NOTICES.md` and the MIT licence text.
