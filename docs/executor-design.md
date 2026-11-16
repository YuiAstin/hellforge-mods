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

## Built: Executor 0.1.0 (test build, 2026-11-06, untested in game)

- **No stims: the fallback.** Refusing the stim is not proven (Stim Probe on branch `stim-probe` logs the action
  pipeline but has not been run; no stim ability id or stim count is pinned). `stim_undo 1` (the default) writes his
  health back down right after any heal that is not his own (the heal event, minus the heals he gave himself in the last
  second); the stim is spent. His own stim is not told apart from a teammate's: both are undone.
- **Shots:** counted from the magazine the HUD reports (`executor_ammo`, as Lemuen's reloads), else from HellForge's
  shots stat (pellets of the gun in hand / its pellets a shot).
- **A shot's enemies:** his shot hits within `shot_gap` (0.12 s) of its first hit; each new enemy among them heals, up to
  the cap (3, 4 in S2).
- **Open item 3:** a hit over time (damage over time, bleeding, drowning) is not rolled for S2's dodge unless
  `s2_dodge_dot 1`; explosions are.
- **Cooldowns** start when a skill ends; one skill runs at a time. S3's end blast is the AC-8 autocannon impact (row 404,
  as Voymastina's support fire), x2.5 on enemies.
- **Empathy by Shotgun** counts roster ids `exusiai*`, `lemuen` and any id with `covenant` (Exusiai the New Covenant's id
  is not chosen yet).

## Built: Executor 0.2.0 (test build, 2026-11-06, untested in game)

- **0.1.1:** stims heal him as anyone (`stim_undo 0`, the default): the undo showed the stim's effect and took back his
  own late heals.
- **Area rounds** (Yui: "spawn additional round matching his gun to every enemies in a 7 meter square in front of him,
  expands to 15 meters when using S3"). Each of his shots (the same count that spends skill rounds) fires one round of
  his gun's own projectile, every pellet (The Chosen One's spawn path), at each living enemy in a square in front of
  him: from where he stands, `area_size` (7) m along his aim's horizontal direction and 7 m across it, centred on the
  aim line; `area_size_s3` (15) while Damnatus Ex Foedere runs; within the square's size above or below him. At most
  `area_max` (12) enemies a shot, nearest first, each with a clear line from his muzzle; rounds fly from near his muzzle
  toward the middle of the enemy's body (Exusiai's node median). The enemies: the kit base's foe list, living = health
  above 0 and the health record's life state 0 (a dead Illuminate keeps health, Exusiai 0.2.12). `area_rounds 0` turns
  it off.
- **Their limits:** the pellets share `max_rounds` (pellets a second, 30) with The Chosen One, so a shotgun of 8 pellets
  reaches about 3-4 enemies a shot at the default; raise `max_rounds` for more.
- **No chaining:** spawned rounds are not the gun in hand's shots (the magazine and the shots stat do not count them):
  they spend no skill round, roll no Chosen One and fire no volley of their own.
- **Healing:** an area round's hit heals as one of the shot's hits: once per enemy, not again for an enemy the shot
  itself healed for, and the shot's heals and its volley's together stay within the heal cap (3, 4 in S2). They take
  the skills' damage multipliers and count among S3's enemies hit, as his shots' hits do.
