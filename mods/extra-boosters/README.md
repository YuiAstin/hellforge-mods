# Extra Boosters

Adds a 22nd booster to Helldivers 2 under HellForge: booster value 21, a copy of a vanilla booster (Muscle
Enhancement by default) under an item id of its own. It is owned as soon as you log in and shows in the booster
picker. HellForge has no `add_booster` yet, so this mod moves the game's booster table itself to make room. It is
the first step towards adding that to HellForge.

**Version 0.2.0 is a test build.** 0.1.0 was played: the booster table moved, the new booster was owned and showed in
the picker. 0.2.0 makes it the **Primed Sure Footed** booster: its own name and description (and optionally another
booster's icon). A new booster has no vanilla effect; the effect comes from the
[Primed Sure Footed](../primed-sure-footed) mod 0.2.0, which stops knockdowns only while this booster is active.

### For other mods

The DLL (`extra_boosters.dll`) exports two functions, found with `GetModuleHandleA` / `GetProcAddress`:

- `int extra_boosters_value(uint32_t item_id)`: the booster value of a booster this mod added (21), or -1.
- `int extra_boosters_active(uint32_t item_id)`: 1 while the game has it active in the mission (game thread only).

### Text

The name and description are text ids of the mod's own, served by a link in the engine's text function chain: the
same slot HellForge's loader hooks for its mods' text, linked once the game reaches the ship, after HellForge's own link.
Any other text id goes on down the chain.

## Co-op

Every player in the session needs this mod, with the same settings. The game sends a booster choice as the raw
booster number and stores what it receives without checking it, so a player without the mod who receives booster 21
reads past the end of their own table. HellForge's mod sync offers the mod to the other players; don't play with
someone who declines it.

## Known limits

- **Other mods that write booster values** (HellForge's `set_live` on boosters, so Stat Tuner's Boosters page, and
  Personal Ship Support's ranked boosters) still write to the game's original table, which the game no longer reads
  once this mod has moved it. Their booster tuning has no effect while Extra Boosters is on. Mods that only read the
  table (Diver Kit) still see the 21 vanilla boosters, but not the new one.
- **Saved loadout:** if you log in without the mod after equipping the new booster, the game resets your booster to
  none.

## Settings (`extra-boosters.ini`)

Shipped with the mod (a mod never writes its own folder: HellForge holds it read-only in a session).

| Setting | Default | |
|---|---|---|
| `enabled` | `1` | `0` writes nothing. |
| `base` | `3` | The row copied: 1 Vitality, 2 Stamina, 3 Muscle Enhancement … 20 Surplus EAT Allocation. |
| `icon_from` | `0` | Another vanilla booster's icon (same numbers); `0` keeps the base's. |
| `id` | `0x1041BB56` | The new booster's item id. Change it only if another mod uses the same id. |
| `value` | (empty) | The booster's tuning value (row `+0x08`); empty keeps the base's. |
| `name` | `PrimedSureFooted` | Its enum name, where the game prints booster names. |
| `title`, `description` | Primed Sure Footed text | Its text in the picker and loadout; empty keeps the base's. |

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
