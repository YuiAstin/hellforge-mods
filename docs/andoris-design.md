# Andoris: a Bulwark with her stick bugs (design)

Andoris is the Assault Rifle Doll of Girls' Frontline 2: Exilium, a Bulwark: she takes the hits, mends herself and
her side, and sets enemies up for Electric damage. Her stick bugs, the Self-Patrolling Turrets, stand with her. This
is her design; nothing is built yet. Source for her GFL2 kit: https://www.prydwen.gg/gfl-exilium/characters/andoris
(the site's own unofficial translation of the CN kit, at V6).

Decided (Yui, 2026-11-26):

- Andoris over Mostima as the next kit.
- **Her turrets become companions that take hits.** In GFL2 they add little to damage and are there to soak shots;
  here a stick bug walks with her, draws fire and takes it. The body under it is the game's SEAF trooper, which
  HELLBOT Companion (nicerjd, Nexus 19043) shows can be a companion in HellForge (method only; none of its code or
  assets); the Escort Probe (branch `escort-probe`) tests ours.
- **Yui models and animates the stick bugs himself**, as a first model. Its block-out is
  `tools/blender/andoris_stickbug.py` (below).
- **Her stick bugs walk**, as they do in GFL2 (the source's "can't move" is the grid rule, not how they look).
- **Her rifle stays Physical.** Only what the kit says is Electric is Electric: Simplified Decision's shot, the stick
  bugs' bolts and the chain of Negative Charge.
- As every GFL2 kit: three skills on keys, the T-Doll package (stamina run to 0 sets off a Hellbomb on her; more health
  and stamina), solo or host. The Confectance Index isn't ported (Leva's rule): what GFL2 pays in Confectance, a
  cooldown pays here.

## Her kit in GFL2

| Skill | In GFL2 (at V6) |
|---|---|
| Test Fire Practice (basic) | A rifle shot, 80% of her attack |
| Tender Offensive | A Self-Patrolling Turret on an empty tile (range 6). 3 at most; a 4th takes the oldest away. Each destroyed turret: +1 Confectance. **Momentum Support:** when an enemy in a turret's range is about to take a targeted Electric hit from an ally, the turret strikes it too (100% of her attack, Electric) and applies Negative Charge. +10% to an ally's Electric hit per turret out |
| Simplified Decision | An Electric shot, 110% of her attack, applies Electrocharge (2 turns): the attacker heals 20% of what she deals to it. At turn end it spreads to the enemies within 3 tiles; Electrocharged enemies take +15% from Electric hits |
| Reinforced Program (ultimate) | She restores her Stability, heals 20%, sheds her debuffs and takes Reinforced Posture (20% less damage, 2 rounds). Every friendly humanoid gets Cover and Positive Charge; with Positive Charge they move further and deal 20% more |
| Delayed Response (passive) | **Self-Repair:** once, armed after 3 turns: fatal damage leaves her at 60% health instead. Taking damage heals her 5% and stacks Dullness on her and her turrets (+10% defence a stack, 8 at most). Attacking a Negative-Charged enemy heals her 20%. She shares 45% of the damage a Positive-Charged ally takes |
| Turret | 50% of her health and attack, 100% of her defence. Can't move. Can apply Negative Charge |

The charges: **Negative Charge** on an enemy: when any Negative-Charged enemy is struck by a targeted Electric hit,
every Negative-Charged enemy takes 30% of that hit. **Positive Charge** on an ally: it heals at turn end (more with
another charged ally near) and deals +35% Electric damage to Negative-Charged enemies.

## Her kit in Helldivers terms

All numbers are first defaults, each a setting on her options page (F9). GFL2's turns are read as 5 s. Default keys
Z, X, C.

### Delayed Response (passive)

- **Self-Repair:** once a mission (again after each reinforce), armed 30 s after she lands: a hit that would kill her
  is cancelled and she is set to 60% health, her bleeding and fire put out. A shield flash and a sound on the HUD.
- **Dullness:** each hit she takes gives a stack, 8 at most, each 3% less damage taken (24% at 8), one stack lost every
  2 s without being hit. Her stick bugs stack it the same way.
- **Taking damage heals:** 5% of her health back after a hit, at most once a second (a cap, so a flamethrower doesn't
  turn it into immunity).
- **Striking a Negative-Charged enemy** with any hit of hers heals her 20%, at most once every 3 s.
- **Shared damage** (co-op): 45% of the damage a Positive-Charged Helldiver takes comes off them and onto her. Later:
  only in co-op, and her own copy of the hit runs through her own reductions.

### Z: Simplified Decision

Her next shot, from any weapon, is an Electric shot: 110% of its damage as Electric, an arc on the hit (Leva's), and
**Electrocharge** on the enemy for 10 s: what she deals to it heals her by 20%, it takes 15% more from her Electric
hits, and every 5 s it spreads to the enemies within 3 m. Cooldown 6 s.

It is also the trigger for her stick bugs (Momentum Support, below).

### X: Tender Offensive

A stick bug: one more of her companions, placed where she aims (6 m at most) or beside her, then walking with her.
3 at most; a 4th takes the oldest away.

- **Takes hits:** 600 health and armour 4, a sentry's (Yui: GFL2's "half her health" is a turn-based number, about
  62 here; and they are made of metal), plus her Dullness and her damage reduction. Enemies are drawn to it. Its own
  copy of the SEAF trooper (add_entity) carries the numbers, so the game's troopers keep theirs (kit base principle 6).
- **A weak gun of its own** (it soaks; it doesn't carry fights).
- **Momentum Support:** when her Simplified Decision lands, every stick bug within 25 m of that enemy strikes it with
  an Electric bolt (an arc from its muzzle, 100% of her shot's damage) and puts **Negative Charge** on it for 10 s.
- **Negative Charge:** when a Negative-Charged enemy takes one of her Electric hits, every Negative-Charged enemy takes
  30% of it (a chain arc to each, Leva's). Reapplied, it stays.
- **Destroyed:** the cooldown of Tender Offensive is cut by 10 s (GFL2's +1 Confectance).
- Cooldown 20 s; a call during the cooldown says how long is left.

### C: Reinforced Program (ultimate)

- She heals 20% and sheds her bleeding and fire; **Reinforced Posture** for 10 s: 20% less damage, 35% less from an
  Electrocharged attacker. Her stick bugs take the same.
- **Positive Charge** for 15 s on her, every Helldiver within 30 m, the SEAF near her and her stick bugs: they heal
  4% a second (5% with another charged one within 3 m), move faster and deal 20% more; their hits on a
  Negative-Charged enemy deal 35% more.
- Cooldown 60 s.

### T-Doll package

As every GFL2 kit: more health and stamina; stamina run to 0 sets off a Hellbomb on her (on/off on the options page).

## What the kit reuses

| Part | From |
|---|---|
| Damage before it lands (reduction, cancel, a fatal hit) | Voymastina (Mastiff's Pledge, Drop-in), Faelynn (Last Bark), Koleda (Sinveiler). Four copies today; Andoris is the reason to lift the one damage dispatcher into the kit base |
| Electric arcs, chains | Leva |
| Marks on enemies, with HUD signs | W (D12, the stun sign) |
| Healing | HellForge's heal action |
| Spawning a unit, taking it away, a cap with the oldest replaced | the kit base's `kb_spawn.h` (W's mines) |
| Leading a unit (its destination) | the kit base's `kb_world.h` setter (the pose hold, Lost Contact) |
| Allies (who gets Positive Charge) | `kb_friends.h` |
| Options page, HUD, keys | every kit |

## New

1. **Her companions.** The Escort Probe (branch `escort-probe`), the game's SEAF trooper as a companion.
   - Played: it spawns as a soldier, **fights** (0.2.1: its combat filled the log before any order) and **follows**,
     also on the comm wheel's orders (0.2.2). 0.4.0 made the stick bug its own trooper copy (600 health, armour 4).
   - **SEAF can hurt the player** (Yui). The kit cancels any hit dealt by one of her stick bugs that lands on her,
     before it lands (`kb_guard.h`'s cancel, the hit's dealer or owner one of her bugs' network ids).
   - Open, Escort Probe 0.10.0 (installed, not yet played): the hits a stick bug deals (one on her cancelled and
     flagged), how many enemies target each stick bug and her every 2 s, and the stick bug copy in a mission.
2. **Her stick bug model on that unit.** How a custom model reaches the game through HellForge (archive, materials,
   the unit it replaces) isn't known yet: tried with a plain cube before the real model.
3. **Drawing fire to a stick bug.** First whether enemies shoot SEAF by themselves (they do in the game). If not
   enough, a taunt (Yui: Lost Contact's research shows the enemies' lists): Lost Contact takes the player *out* of an
   enemy's perception lists and clears its target; the taunt does the reverse: an entry for the stick bug in the
   enemy's perceiver's list, and the enemy's behaviour target set to it (`docs/research/ai-modding.md`: the route is
   unproven, the next perception refresh may drop it). Escort Probe 0.10.0's End key tries it on every enemy targeting
   her and logs for 10 s whether they keep the stick bug.

## The stick bug model

Yui's description: a cylinder spine between two ball joints (`--o-----o`): the gun, a small head, sticks forward from
the front joint; each joint carries a pair of two-segment legs. Mostly legs and a torso, metal.

`tools/blender/andoris_stickbug.py` builds the block-out in Blender 4.x/5.x (Scripting tab, Run Script): one rigidly
weighted mesh and its rig: root, spine, two joints, the head, 8 leg segments, a `muzzle` bone (where her bolts start)
and an IK target on the ground for each leg, so the feet stay planted while the body moves. Its sizes are numbers at
its top; it replaces its own objects when run again. Tested in Blender 5.3 (the body lowered 10 cm, the feet held).

Animations, easiest first: aim (the head), fire (the head's kick), idle (the spine's bob, legs by IK), deploy (the
legs unfolding).

## Open

- The numbers: all first defaults, for play tests.
- Shared damage: co-op only; left for later.
