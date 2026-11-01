# Stat Tuner

Per-item stat sliders on the in-game **MODS** tab (Esc > Options > MODS), with 8 preset slots.

## Needs
- **Bingus Vanilla Megapack REFORGED** (Beelzebub) — it contains the Universal Mod Menu, the HellForge version of
  Mod Options Menu. The standalone Lua "Mod Options Menu v1.2" does not talk to code mods.
- Without the menu the mod still applies `stat-tuner-current.ini` every session.

## Use
- Pages: Weapons (Primaries 1-4, Secondaries 1-2, Throwables 1-2), Stratagems (Support weapons 1-3, Offensive 1-2,
  Defensive 1-2, Backpacks, Vehicles), Diver and presets (Presets, Helldiver, Boosters).
- On a page: pick the **Item**, press **APPLY**, then move the sliders and press **APPLY** again.
- Sliders are percentages of the game's value (100% = vanilla). Armor penetration / armor are +/- steps.
  Uses at 1000% = unlimited.
- When changes apply: weapon fields on next equip/call-in, damage/projectile rows on the next shot, stratagem
  settings from the next mission.
- Damage and projectile rows are often shared: weapons firing the same round change together.
- The F8 editor's saved changes (Local Stats) win over this mod on the same field.

## Files (in this folder)
- `stat-tuner-current.ini` — your current values (`Item name|slot = value`), loaded every session.
- `stat-tuner-preset-1.ini` … `-8.ini` — presets. Plain text: copy, edit, share.
- `tools/gen_stat_data.py` regenerates `src/stat_data.h` from `mods/hellforge-editor/editor-catalog.json` after a
  HellForge update: `python tools/gen_stat_data.py ..\hellforge-editor\editor-catalog.json src\stat_data.h`

## Co-op
It is a gameplay mod: friends need the same version to join you.
