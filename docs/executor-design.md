# Executor the Ex Foedere (design, 2026-11-04)

Arknights' Laterano Guard (Reaper) with a shotgun: AoE hits, a heal on every enemy hit, skills counted in rounds.
Kit from arknights.wiki.gg/wiki/Executor_the_Ex_Foedere, fitted to Helldivers 2. Built on the kit base; nearly every
piece is already proven (Lemuen's rounds-until-done skills, Exusiai's free rounds, the damage pipeline, the roster).

"Ammo" is the skill's count of shots, as Lemuen's "until she reloads": no magazine writes, no fire-rate changes.

## Kit

| Arknights | In HD2 | Built from |
|---|---|---|
| **Trait:** attacks deal AoE; recovers 50 HP per enemy hit, up to his Block | Every enemy his shot hits heals him **50% of his max health** (Helldivers are made of tissue paper), up to 2 or 3 enemies a shot (the ini). **Each heal also mends every injured limb in full**: a full health pool on a broken leg is no use | Hit events; HellForge's `heal` and `heal_limb` (the local Helldiver; `limb_state` says which are injured) |
| **Trait:** cannot be healed by allies | **He cannot stim at all** (Yui): his heal is his hits. Best: his stims are taken away (none carried, or the stim action refused); fallback: a stim's heal is undone at once (the heal event, then damage of the same amount), the stim wasted | Research: the stim count or the stim action. **If that is too hard** (Yui): his own stim stays, as a self-heal (in Arknights' terms he heals himself; allies are what cannot heal him), and only a stim from someone else is undone (co-op; solo, nobody else heals him anyway) |
| **The Chosen One:** 20% chance to attack twice; +5% per round spent while a skill is on | Each shot has a chance to fire a free extra round, rising while a skill runs | Exusiai's free rounds |
| **Empathy by Shotgun:** +1 skill ammo per Laterano operator deployed (up to +4) | +1 skill round per other Laterano kit installed (Exusiai, Lemuen, Exusiai the New Covenant), from the roster | The roster |
| **S1, Testament Exacted:** 8 rounds, ATK +50%, ignores 400 DEF | 8 shots x1.5, more armour penetration | The damage pipeline; the hit pipeline's AP edit |
| **S2, Close Combat:** 12 rounds, ATK and DEF +80%, Block +1; 40% to dodge a melee hit and reload a round | 12 shots x1.8, 80% less damage taken, heal cap +1; **any hit on him** (not only melee: HD2 would leave it idle against bots and ranged bugs): 40% to be negated and give a round back | The damage-taken hook (as Voymastina's Stability) |
| **S3, Damnatus Ex Foedere:** 16 rounds, ATK +180%, +6% per round spent (30 stacks), heal x3; at the end, 250% ATK to every enemy hit during it | 16 shots x2.8, +6% a shot, heal x3; when it ends, a blast on every enemy it hit | The damage pipeline; Lemuen's S3 (one on each target of a list) |

**Not carried over:** S3's longer attack interval (+0.5 s). In Arknights it only offsets the skill's ATK; in HD2 the
gun's fire rate and pump already set the pace (Yui). If he is too strong, the ini's numbers answer it.

## Open items

1. The heal cap per shot: **3 enemies** (Yui, 2026-11-06). The heal: 50% of max health a hit, limbs mended in full (settled).
2. No stims: where his stim count lives, or how to refuse the stim action, so a stim is not spent for nothing.
3. S2's dodge: settled as any hit. Open: whether a negated explosion or a hit over time counts (the damage kind).
