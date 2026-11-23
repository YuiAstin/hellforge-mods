# Destroyer: a shotgun berserker (design, not built)

Destroyer is a Sangvis Ferri Doll in Girls' Frontline 2: Exilium. She is an Elite Sentinel who uses shotguns, and her
phase is Freeze. She came out on the CN server in November 2026. Her whole kit is built around paying health: each
skill costs part of her HP, hitting enemies heals her back, and she grows stronger the lower she gets. She is extra
dangerous to machines.

Source: https://dandegate.net/dolls/destroyer/skills (CN text, translated). IOP Wiki has her page with the skills not
yet filled in. The source names Hunting Order, Ammo Loaded, Offense Only and Denied Exit without saying what they do,
so what each one does here is this kit's reading.

The Freeze phase is only a team-building label. Nothing in her skill text freezes or slows anyone, so this kit does not
freeze or slow.

## Her kit in GFL2

| Skill | In GFL2 |
|---|---|
| Bite (basic) | A shotgun shot at one enemy within 6 tiles. |
| Annihilation Directive (S1) | Costs 20% of her starting HP. She leaps up to 5 tiles and blasts the nearest enemy. A hit lets her use it again and move 6 tiles. V2 adds a bonus hit on the third hit to the same target. Two variants (I and II) blast a 3x1 line or a 2-tile fan instead, knock enemies back, and break cover. |
| Doom Barrage (S2) | Costs 30% of her starting HP. A grenade barrage over a 5x3 area ahead of her that breaks cover. It spends all her Grenades points: 3 or more raise crit damage, and each extra point adds damage. Gives Fang. |
| Party Time (ultimate, cooldown 3) | Leaps up to 6 tiles and hits everything in a 3-tile-wide path. Refills Grenades. Each enemy hit gives Frenzy and heals 20% of her max HP; a boss gives 3 Frenzy and heals 100%. |
| Ammo Frenzy (passive, used with 3 Fang) | A blast of 400% over 8 tiles around any enemy on the map, shared evenly among everyone it hits. Applies Hunting Order for 2 turns. |
| Sangvis Ferri (passive) | Grenades: 3 points, more with other Sangvis Dolls, up to 6. S1 spends 1 point, S2 spends all of them, and each point spent adds +40% damage. |
| Machines (passive) | +5% damage per machine on the field (up to 50%). Her skills add 10% of a non-boss machine's max HP. She takes 30% less damage from machines. Fatal damage gives Denied Exit. V6: each 1% HP she is missing gives +1% damage, +1% healing received and 0.5% less damage taken. |

## Her kit in Helldivers terms (proposal)

All numbers are first defaults, each one a setting. Default keys Z, X, C. Solo or host.

### The T-Doll package
The same as every GFL2 kit: more health and stamina, and running her stamina dry sets off a Hellbomb on her (it can
be switched off).

### Blood price (passive)
- Every skill costs a share of her **max** health. That's GFL2's "initial HP", with the health multiplier included.
  It can never take her below 1 HP.
- **Berserk:** each 1% of health she is missing gives +1% damage and 0.5% less damage taken, so at 10% health she
  deals about +90%.
- **Denied Exit:** a hit that would kill her leaves her at 1 HP instead, with 2 s of no damage. Its cooldown is 120 s,
  and when it comes off cooldown she gets all her health back (her V5).

### Sangvis Ferri: machines (passive)
- **Machines** are Automatons, plus anything armoured on any front: tanks, hulks, harvesters and walkers.
- Her skill hits on a non-boss machine add 10% of its max health.
- She takes 30% less damage from machines.

### Grenades (points)
- She has 3 points, starts with all 3, and holds at most 3.
- Annihilation Directive spends 1 point and Doom Barrage spends all of them. Each point spent adds +40% to that skill.
- Party Time refills them.

### Annihilation Directive (Z): the leap
- Costs 15% of her max health.
- She leaps onto the enemy nearest her crosshair, up to 20 m away. It's an aimed skill, so it follows the camera,
  because players see what they hit.
- A shotgun-blast AoE (about 4 m) hits where she lands and knocks the enemies there back.
- **Chain:** a hit lets her leap again for free within 3 s, up to 3 leaps in a row. That's GFL2's "use it again and
  move 6 tiles".
- If it hits the same enemy 3 times in one chain, the third leap adds a big extra hit.

### Doom Barrage (X): grenade barrage
- Costs 25% of her max health.
- Grenade blasts fall across a 15 m wide, 10 m deep area ahead of her crosshair. There are 5 blasts, plus 2 more for
  each Grenades point spent.
- With 3 points spent, the barrage does a lot more damage.
- Gives 1 Fang.

### Party Time (C): the ultimate
- Free, with a cooldown of about 45 s.
- She dashes up to 25 m straight ahead (the way she is facing, not the camera's way, following the movement rule).
  Every enemy within 3 m of her path takes heavy damage.
- Each enemy hit heals 20% of her max health and gives 1 Fang. A heavy enemy (Hulk, Charger, Harvester, tank, and so
  on) heals 100% and gives 3 Fang.
- Refills Grenades.

### Ammo Frenzy (needs 3 Fang): the payoff
- With 3 Fang, her next press of Doom Barrage (X) becomes Ammo Frenzy instead.
- It's a wide blast, about 16 m around the enemy at her crosshair (it can reach far). Its damage is split among
  everyone it hits, so it's strongest against one big target.
- Hunting Order: enemies it hits take +20% damage from her for 10 s.

## Open questions

1. **Machines:** Automatons only, or anything armoured on any front?
2. **Ammo Frenzy's key:** folded into X when she has 3 Fang (as above), or a fourth key?
3. **Leap target:** the enemy at her crosshair (as above), or a fixed distance where she aims?
4. **Health costs:** keep them (the core of her kit), or make them optional?
5. **Model:** a Destroyer character model is not part of the kit (as with the other kits).
