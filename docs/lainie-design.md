# Lainie (design, 2026-11-04)

UMP40's GFL2 counterpart (GFL2: Exilium; Sentinel, SMG, clone specialty). The third UMP after Lenna (UMP9) and Leva
(UMP45), with a role of her own: **she breaks durable armour** (durable damage), and fights beside a hologram copy of
herself. Kit from Prydwen (prydwen.gg/gfl-exilium/characters/lainie) and IOPWiki (iopwiki.com/wiki/Lainie,
Lainie's_Simulacrum), fitted to Helldivers 2 with Yui's changes. Built on the kit base.

GFL2 is turn-based: a "round" here is a few seconds.

## Her kit

| GFL2 | In HD2 | Built from |
|---|---|---|
| **Confectance Index (CI):** her active attacks earn it; the Simulacrum costs 3 a round | A HUD bar: her skills and her hits on fully shredded targets fill it; the Simulacrum drains it every few seconds and leaves when it is out | Kit base HUD |
| **Precognition Foresight:** enemies near her -10% DEF; each attack adds Under My Umbrella; extra damage on DEF 0 targets from her max HP | Enemies near her take more **durable** damage from her; each hit adds a stack; at full stacks, bonus damage from her HP | Below |
| **Under My Umbrella:** -15% DEF a stack (3, or 6 fortified) | **+durable damage a stack** on that enemy (her hits on its armoured, durable parts land harder); a per-target record, as Persecution's marks | Damage row edit, set and restored around her hit |
| **Combat Algorithm:** single target, 140% ATK | Her next shot x1.4; the Simulacrum answers with a splash at its spot | Damage pipeline, explosion spawn |
| **Cognition Overload:** area 120% ATK, slows | A burst at her target that slows the enemies there; the Simulacrum answers | Explosion spawn, the EMS stun status |
| **Simulated Ally:** summons the Simulacrum, which mirrors her attacks and can swap places | Ultimate: the Simulacrum; S3 again swaps places with it | Voymastina's teleport |
| **Fortified passive:** with the Simulacrum out, her first fatal hit restores her, and it leaves | Her first lethal hit with it out is cancelled, she is healed, it is gone | Death prevention in the damage hook, a heal |

## The Simulacrum: waits for the in-world model

Not a HUD sprite (Yui: it would look poor). The Simulacrum is a copy of her Helldiver's model **in the game's own
world** (option B of 2026-11-04: lit and occluded like anything else), posed as she is each frame. Its rounds are real
spawned projectiles from its position at her target (Exusiai's guns), so hits and kills are the game's own.

Research first (a model probe): spawn her armour's unit in the main world, copy her pose onto it (animation, or bone
transforms each frame), and check it stays a visual (no physics, not seen by the AI). Lainie is built once that works.

## Durable damage per stack

Durable damage is not in the hit event (its amount is the standard damage): it is a field of the hit's DamageInfo row
(+8). The kit base already sets a hit's armour penetration before the game reads the hit and restores it after
(kb_hits: kb_hit_before, restore). Durable can go the same way: her hit's row's durable damage raised by her stacks on
that target, read by the game, restored. Open: where the hit record points at its damage row (the hit layout is
Helldiver Survival Rework's research).

## Open items

1. The model probe: her armour in the world, posed as she is, visual only.
2. The durable edit: the hit record's damage row.
3. Her stack numbers (durable bonus a stack, 3 or 6 stacks, how long they last).
4. CI's rates (what fills it, how fast the Simulacrum drains it).
