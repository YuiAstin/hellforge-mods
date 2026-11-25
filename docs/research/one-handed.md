# One-handed weapons (research, 2026-11-24)

Goal: make a two-handed weapon held one-handed (the Destroyer kit wants every weapon in her hands one-handed; the kit
base's One-Handed Grip upgrade wants it for any kit that buys it). Game build 25480438.

## What makes a weapon one-handed

The weapon's Equipment record (EquipmentComponentData, component 233) has two fields for it:

| Offset | Type | Meaning |
|---|---|---|
| +0xC4 | u32 flags | bit 0 is one-handed: `0x00010001` on every one-handed weapon, `0x00010000` on every two-handed one |
| +0xC0 | u32 | the grip pose the Helldiver holds it in |

Grip poses seen:

| Value | Weapons |
|---|---|
| 70 | the one-handed primaries (the SMGs: Knight, Defender, Pummeler) |
| 1 | most pistols |
| 42 | blades (melee) |
| 10, 15, 80, 90 | two-handed rifles and shotguns |

All 34 weapons the game labels ONE HANDED (pistols, melee, Knight, Defender, Pummeler, ...) have bit 0 set, and every
two-handed one has it clear. Found by the Equipment Probe (`probes/equip-probe`, branch `equip-probe`) over all 81
weapons.

The ONE HANDED trait shown in the armory is separate presentation data (HD2Runtime `weapon-presentation.md`, the
`traits` list). Changing it changes the label only; it isn't what makes the Helldiver hold the weapon one-handed.

## Where the records are

```
table  = [[game.dll + 0x346BF98] + 0xF12478 + 8 * 233]
index  = table + 16 * i,  i < 688:  { u64 resource, u32 record, u32 0 }
record = table + 11008 + 232 * record
```

- Find a weapon by its entity resource in the index rows, then go to its record.
- Records can be shared: more than one resource can point at the same record. Changing it changes every weapon that
  uses it, so log the sharers (One-Handed Test does).
- A change reaches a weapon equipped after it. A weapon already in her hands keeps its old grip until it's equipped
  again.
- The table exists once entities are ready, so wait for the mission start before writing.
- The addresses are for build 25480438 only: both users check `build_id()` and change nothing on another build.

## Code that uses it

- **One-Handed Test** (`yui.one-hand-test`, `mods/one-hand-test`; in the local source folder, and on the
  `sure-footed-booster` branch): sets bit 0, and optionally the grip, on the weapons listed in `one-hand-test.ini`
  (`weapon = <resource hex> [grip]`). Research only, not for release.
- **Kit base One-Handed Grip** (`kitbase/kb_upgrades.h`, branch `kits-upgrade-test`): an upgrade bought for 5000
  requisition on the kits' UPGRADES tab. While the picked kit owns it, every record at exactly `0x00010000` gets bit 0
  and grip 70. The original grips are remembered and put back when a kit without the upgrade is picked. Only one kit (the
  options screen's owner) writes the table.

## Open questions

- In-game results: how a two-handed rifle looks and shoots in grip 70 (or grip 1) hasn't been written down yet. Fill
  this in after the next test.
- Whether support weapons and heavy weapons (backpack-fed, crew-served) follow the same bit.
- Whether there's a per-Helldiver route (a change for her only) instead of the shared table, which changes the weapon
  for every player holding it on this machine.
