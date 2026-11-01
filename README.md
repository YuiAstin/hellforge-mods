# HellForge mods by Yui

Code mods for HellForge (offline Helldivers 2). Each folder under `mods/` is one HellForge mod, laid out exactly as it sits in HellForge's `mods` folder; HellForge builds the C sources itself before each session.

| Mod | Folder | What it does |
|---|---|---|
| Lenna | `mods/lenna` | Lenna's kit from Girls' Frontline 2: Wild Extinction, Leaping Pursuit, Hunting Strategy. T-Doll body. |
| Faelynn | `mods/faelynn` | Faelynn's gas kit from GFL2: Triple Maul, Knife-Eared Hunt, Loyal Dash and Pursuit chains, Corrosion armour-piercing. T-Doll body. |
| Leva | `mods/leva` | Leva's arc / area-lockdown kit from GFL2: Rational Suppression, Orderly Disruption, Quantum Calculation, Superconduct Blitz. T-Doll body. |
| Veterancy System | `mods/field-promotions` | Per-mission gear progression: your gear levels up as you use it, for that mission only. |
| Fire Mission | `mods/fire-mission` | A stratagem that calls mortar sentries onto a spot. |
| Primed Sure Footed | `mods/primed-sure-footed` | No knockdown or ragdoll on your Helldiver; flinch and push stay. |
| Stat Tuner | `mods/stat-tuner` | Per-item stat sliders on the in-game MODS tab, with presets. |

The GFL2 kits and Veterancy System have HUDs drawn with HD2Runtime (Bingus Shared Loader + HD2Runtime). Their Lua sources sit in each mod's `hud-src/` (Veterancy System's in `huds/field-promotions-hud/`); the built HUD ships in the mod's `data/`.

## Working on them

```
python tools/deploy.py leva          # copy into ../HellForge/mods (game closed); your own leva.ini is kept
python tools/inigen.py leva          # regenerate mods/leva/leva.ini (the defaults) after changing settings in the source
python tools/package.py leva         # ../Releases/leva-<version>.zip, ready for HellForge's ADD MODS
```

`--hellforge <path>` and `--out <path>` point the scripts elsewhere.

**Never change a mod folder while the game runs.** HellForge holds mod folders read-only during a session and switches off a mod whose folder gained files; the mods therefore never write their own `.ini`, and each release ships one with the defaults.

## Tools

- `tools/deploy.py`, `tools/package.py`, `tools/inigen.py`: above.
- `tools/hotreload/`: an experimental hot-reload host (a fixed "socket" mod that reloads a mod's logic DLL while the game runs). Untested in game.

## Credits

Hit, entity, scene-graph and raycast research from **Helldiver Survival Rework** by enochlophobianumerophobia (MIT) and **HD2Runtime** by SkyeShade; settings rows through **Mod Options Menu** by CowboyBingus. Each mod that uses it carries `THIRD_PARTY_NOTICES.md` and the MIT licence text.
