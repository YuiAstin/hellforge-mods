# Destroyer: the leaping shotgunner (plan)

Destroyer is the Sangvis Ferri Ringleader, playable in Girls' Frontline 2 (CN only so far): an Elite **Sentinel** with a
shotgun and the Freeze element. Her kit pays for its skills with her own health, chains a leap into two blasts, banks
Fang, and cashes it in for one strike over a huge area. Her passive is built around **mechanical** enemies, which in
Helldivers 2 is the whole Automaton front. Nothing here is built yet. Source: dandegate.net/dolls/destroyer/skills
(read 2026-11-16).

**Freeze is only her damage's element.** Nothing in her kit freezes, slows or holds an enemy, so nothing here does
either: her blasts are plain explosive damage. What her skills do besides damage: knockback (the two Annihilation
stages), destroying cover, Hunting Order and Claw Mark.

## Her official kit, in short

| Skill | What it does |
|---|---|
| Bite (basic) | One target within 6 tiles, 80% ATK. |
| Annihilation Directive (S1) | 20% of her initial HP. Lands on a tile in a cross of 1-5 tiles, 60% ATK to the nearest enemy within 1 tile. On a hit it can be used again and she may move 6 tiles. |
| Annihilation Directive I | 20% HP. 80% ATK in a 3x1 line, knockback 1 tile, destroys cover. On a hit: stage II. |
| Annihilation Directive II | 20% HP. 100% ATK in a 2-tile fan, knockback 2 tiles, destroys cover, Ammo Loaded. On a hit her other skills open again. |
| (V2) | Three Annihilation hits on one target in a turn: +200% ATK on it. Against Hunting Order: ignores 30% defense. |
| Doom Barrage (S2) | 30% HP. 30% ATK in a 5x3 area, destroys cover, +1 Fang. Spends all Grenades: +20% damage each (V4: +50%), 3 or more: +50% crit damage. |
| Party Time (ult) | Lands on a tile in a cross of 6 tiles, 150% ATK in a 3-tile-wide path. Grenades back to max. Each target hit: +1 Frenzy and 20% max HP back (a Boss: 3 Frenzy, 100%). +1 Fang. Cooldown 3 turns. |
| Frenzy (V3) | Starts with 3. Annihilation Directive or Doom Barrage spends one: +60% damage and +1 Grenade. |
| Ammo Frenzy (passive, used at 3 Fang) | 400% ATK shared evenly among every enemy within 8 tiles of one target. Hunting Order for 2 turns. Destroys cover. Extra Command. |
| Grenades | Starts with 3 (more with other Sangvis Ferri allies, up to 6). Annihilation spends 1, Doom Barrage all: +40% each. |
| Mechanical enemies | +5% damage for each on the field (up to +50%). Active-skill hits on a non-Boss one: fixed damage, 10% of its max HP. 30% less damage from them. Claw Mark on the nearest at battle start. |
| Denied Exit | Fatal damage does not kill her (once, on a cooldown at V5). |
| (V6) | Each 1% of HP lost: +1% Freeze damage, +1% healing received, -0.5% damage taken. |

## Her kit in Helldivers terms

Tiles become metres at about 3 m a tile, turns become seconds. All numbers are first defaults, each a setting.

### Resources

- **Health as the price.** Each skill takes a share of her max health (`s1_cost` 20%, `s2_cost` 30%). **A skill never
  takes her below `cost_floor` (10%)**: under it the skill still fires and costs what is above the floor. (Proposed; the
  official kit has no floor.)
- **Grenades** (`grenades_max` 3): spent by her skills for damage (`grenade_mult` +40% each), refilled by Party Time.
  On the HUD as pips.
- **Fang** (0-3): +1 from Doom Barrage, +1 from Party Time. At 3: Ammo Frenzy.
- **Frenzy**: from Party Time's hits (and 3 at mission start). Her next Annihilation Directive or Doom Barrage spends one:
  `frenzy_mult` +60% and +1 Grenade.

### Passive: Sangvis Ferri

- **Mechanical = Automatons.** Each living Automaton within `mech_range` (40 m): `mech_each` +5% of her damage, up to
  `mech_max` +50%.
- Her skill hits on an Automaton that is not a boss-sized one add `mech_hp` 10% of its max health (once per hit).
- She takes `mech_taken` 30% less damage from Automatons.
- **Denied Exit**: a killing hit leaves her at 1 health instead, then `denied_cooldown` (180 s) before it can again.
- **Lost health is power** (V6, `rage` 1): each 1% of health missing: +1% of her skill damage and -0.5% damage taken.
- Claw Mark: left out (it only makes one enemy count as mechanical).

### Annihilation Directive (key_s1, Z): leap, line, cone

One key, three stages; each costs `s1_cost` and spends 1 Grenade if she has one.

1. **Leap.** To where she aims, up to `s1_leap` (15 m): Ascalon's dash (`as_dash.h`, the game's fit test, `kb_fit.h`).
   A small blast where she lands (`s1_radius` 3 m). If it hits anything, stage I opens for `s1_window` (3 s).
2. **Stage I (Z again).** A line of blasts `s1_line` (9 m) ahead of her that **pushes enemies back**. A hit opens
   stage II for `s1_window`.
3. **Stage II (Z again).** A cone of blasts (`s1_cone` 6 m) with a **bigger push**. Then the chain is over and the
   cooldown (`s1_cooldown` 10 s) starts. A missed stage also ends the chain and starts it.

- The same enemy hit by all three stages takes a bonus hit (`s1_triple`, 2x a stage's damage).
- Against Hunting Order her hits pierce more armour.
- After each stage that hits: `s1_haste` 2 s of faster sprint (her "can move 6 tiles").

### Doom Barrage (key_s2, X)

Costs `s2_cost`. A grid of grenade blasts in a box in front of her (`s2_length` 15 m by `s2_width` 9 m), set off in
rows from near to far. Spends **all** her Grenades: each adds `s2_grenade` +20% damage, 3 or more: critical (`s2_crit`
1.5x). +1 Fang. Cooldown `s2_cooldown` 18 s.

### Party Time (key_s3, C)

Leaps up to `s3_leap` (18 m) to where she aims, blasting a `s3_width` (9 m) wide path from where she stood to where she
lands. Grenades back to max. Each enemy hit: +1 Frenzy and `s3_heal` 20% of her max health (a boss-sized one: 3 Frenzy
and a full heal). +1 Fang. Cooldown `s3_cooldown` 90 s.

### Ammo Frenzy (key_frenzy, its own key)

Needs 3 Fang, spends them. One strike centred on the enemy in her crosshair: a ring of blasts over `frenzy_radius`
(24 m) around it. Its total damage (`frenzy_damage`) is **shared evenly** among every enemy in reach (one enemy takes it
all; twenty take a twentieth each), paid once per enemy from the damage event. Each gets **Hunting Order** for
`hunting_seconds` (10 s): `hunting_taken` +20% of her damage on it. Her skills' cooldowns lose `frenzy_refund` (5 s),
her "Extra Command".

Its own key (proposed, as Ascalon's dash got Z) so a full Fang bar never blocks Party Time.

## How it would be built

| Piece | From |
|---|---|
| Leaps (S1, ult) | Ascalon's dash: `as_dash.h`, `kb_fit.h`, the open-ground-under-foliage rule. |
| Blasts | `spawn_explosion` rows, with the own-blast guard (`kb_hits.h`) sparing her and the friend list (`kb_friends.h`) her allies. |
| Rows of blasts over time (Doom Barrage, the ult's path, Ammo Frenzy's ring) | Koleda's wave queue (rings of bursts under a rate limit). |
| A fixed amount once per enemy (Ammo Frenzy's share, +10% max health) | Koleda's pay-once table on kind-4 hits; max health from the game's health table. |
| Health as the price, the floor, Denied Exit | The kit base's body block (health read / set), Koleda's never-fatal clamp on the Sinner. |
| Pips for Grenades, Fang, Frenzy | The HUD state struct and chips of the other kits. |
| A fourth key | A key read by the kit itself (the kit base has three). |

## Decided (2026-11-23)

- **Knockback:** the explosion's own push, with its stagger raised for the two pushing stages (no warp). Heavy enemies
  do not move.
- **Boss-sized:** anything with heavy armour or above, the same test as Lemuen's.
- **Blasts:** the Orbital Precision Strike's explosion for the small ones (the leap's landing, the line, the cone, the
  barrage's grid, Party Time's path), the 500 kg bomb's for the big one (Ammo Frenzy). Her damage comes from the kit,
  not the row.
- **The health price:** tuned in play tests (first defaults: 20% and 30%, floor 10%).

## Still open

- **Telling Automatons from the rest.** By faction record or by a list of their kinds (as the friend list is); to check
  what the game exposes.

## Co-op

Every skill is a blast or a leap, and only the host may spawn. So she is built on the message pattern of Lemuen's co-op
branch (`lemuen-coop`) from the start: a joiner's press sends the skill, her position and aim to the host's copy, which
sets off the blasts and pays the damage; her own leap, health price and heals stay on her machine. Solo runs the same
path through the loopback.
