# Executor the Ex Foedere (design, 2026-11-04)

Arknights' Laterano Guard (Reaper) with a shotgun: AoE hits, a heal on every enemy hit, skills counted in rounds.
Kit from arknights.wiki.gg/wiki/Executor_the_Ex_Foedere, fitted to Helldivers 2. Built on the kit base; nearly every
piece is already proven (Lemuen's rounds-until-done skills, Exusiai's free rounds, the damage pipeline, the roster).

"Ammo" is the skill's count of shots, as Lemuen's "until she reloads": no magazine writes, no fire-rate changes.

## Kit

| Arknights | In HD2 | Built from |
|---|---|---|
| **Trait:** attacks deal AoE; recovers 50 HP per enemy hit, up to his Block | Every enemy his shot hits heals him (the cap: 2 or 3 enemies a shot, the ini). Shotgun pellets give the AoE | Hit events; HellForge's `heal` (the local Helldiver) |
| **Trait:** cannot be healed by allies | Stims heal him less, or not at all (the ini) | The heal event, counter-damage |
| **The Chosen One:** 20% chance to attack twice; +5% per round spent while a skill is on | Each shot has a chance to fire a free extra round, rising while a skill runs | Exusiai's free rounds |
| **Empathy by Shotgun:** +1 skill ammo per Laterano operator deployed (up to +4) | +1 skill round per other Laterano kit installed (Exusiai, Lemuen, Exusiai the New Covenant), from the roster | The roster |
| **S1, Testament Exacted:** 8 rounds, ATK +50%, ignores 400 DEF | 8 shots x1.5, more armour penetration | The damage pipeline; the hit pipeline's AP edit |
| **S2, Close Combat:** 12 rounds, ATK and DEF +80%, Block +1; 40% to dodge a melee hit and reload a round | 12 shots x1.8, 80% less damage taken, heal cap +1; a melee hit: 40% to be negated and give a round back | The damage-taken hook (as Voymastina's Stability) |
| **S3, Damnatus Ex Foedere:** 16 rounds, ATK +180%, +6% per round spent (30 stacks), heal x3; at the end, 250% ATK to every enemy hit during it | 16 shots x2.8, +6% a shot, heal x3; when it ends, a blast on every enemy it hit | The damage pipeline; Lemuen's S3 (one on each target of a list) |

**Not carried over:** S3's longer attack interval (+0.5 s). In Arknights it only offsets the skill's ATK; in HD2 the
gun's fire rate and pump already set the pace (Yui). If he is too strong, the ini's numbers answer it.

## Open items

1. The heal cap per shot (2 or 3 enemies), and the heal's size against HD2's health (50 HP in Arknights is small).
2. Whether "cannot be healed by allies" stays (stims weaker, or none) or is an ini choice off by default.
3. What counts as a melee hit for S2's dodge (the damage kind: impact or ability).
