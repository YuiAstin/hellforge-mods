# Extra Boosters

Adds a 22nd booster to Helldivers 2 under HellForge: booster value 21, a copy of a vanilla booster (Muscle
Enhancement by default) under an item id of its own. It is owned as soon as you log in and shows in the booster
picker. HellForge has no `add_booster` yet, so this mod moves the game's booster table itself to make room. It is
the first step towards adding that to HellForge.

**Version 0.1.0 is a test build.** It has not been run in game yet. The new booster has the base booster's name,
icon, description and effect, so in the picker it looks like a second Muscle Enhancement. Custom text, icons and
effects come later.

## Co-op

Every player in the session needs this mod, with the same settings. The game sends a booster choice as the raw
booster number and stores what it receives without checking it, so a player without the mod who receives booster 21
reads past the end of their own table. HellForge's mod sync offers the mod to the other players; don't play with
someone who declines it.

## Settings (`extra-boosters.ini`)

Written with the defaults on first launch.

| Setting | Default | |
|---|---|---|
| `enabled` | `1` | `0` writes nothing. |
| `base` | `3` | The booster copied: 1 Vitality, 2 Stamina, 3 Muscle Enhancement … 20 Surplus EAT Allocation. |
| `id` | `0x1041BB56` | The new booster's item id. Change it only if another mod uses the same id. |
| `value` | (empty) | The booster's tuning value (row `+0x08`); empty keeps the base's. |
| `name` | `ExtraBooster1` | Its enum name, where the game prints booster names. |

## How it works

Research: [docs/research/booster-table.md](../../docs/research/booster-table.md). Build 25480438 only.

- The 21 booster rows (56 bytes each) are copied into memory within 2 GB above `game.dll`, followed by the new row
  and an empty row.
- 117 instructions that address the table are re-pointed to the copy.
- 112 instructions that hold the booster count 21 (guards, loop ends, the "none / not found" value and the picker's
  clamp) are raised to 22.
- The booster name list is copied with the new name, and its two readers are re-pointed.
- The new booster is made owned in game, as HellForge does for its own new items: a copy of the base booster's
  catalogue record under the new id, the catalogue sorted again, and one added to the inventory.

Every instruction is checked against its reviewed bytes before anything is written, and none may sit under another
mod's hook. If one doesn't match (another mod moved the table, or a different game build), nothing is written and the
log says why.

## Testing

`modloader.log` / `mod-yui.extra-boosters.log` should show:

1. `booster table moved to …: 117 sites re-pointed, 112 bounds raised to 22, names moved`
2. `booster 21 owned (item 0x1041BB56, …)` once you reach the main menu
3. In a mission, after 10 seconds: `mission: active boosters: …`, with `(the new one included)` when booster 21 is
   picked.

Then check that the picker shows the extra booster, that picking it sticks, and that it shows on the loadout and in
the mission HUD.

## Licence

GPL-3.0 (`LICENSE`): the ownership code is ported from HellForge's mod loader (`native/mod-loader/ownership.c`, by
SkyeShade, GPL-3.0). The rest of this repository stays MIT.
