# Texas the Omertosa: the parry kit (design, 2026-11-05)

Arknights' Texas the Omertosa is a melee Specialist (Executor): fast redeploy, three passive skills that fire on
deployment, a sword rain. Helldivers has no melee duelling, so her kit is built around one new mechanic, **the parry**,
and her "deployment" becomes the moment she appears behind an enemy after one. Nothing here is built except the probe
(probes/parry-probe); every value is a starting point.

## Source kit (Arknights, E2 max, M3)
- Trait: much faster redeployment.
- Talent **Texas Tradition**: +20% ATK while a skill runs; the first kill fully heals her and restarts the skill.
- Talent **Texas Swordmanship**: until her first kill after each deployment, +10 ASPD and 30% less damage taken.
- S1 **Silent Drizzle** (12 s): ATK +70%, hits Silence for 10 s and deal 400 Arts damage a second.
- S2 **Unrelenting Downpour** (10 s): on deployment 240% Arts burst around her and -30% RES; ATK +55%, hits twice.
- S3 **Torrential Sword Rain** (8 s): two 165% bursts around her that stun 2 s, then every second a sword rain on up
  to 4 enemies (130%, 0.2 s stun).

## The parry (core)
- **Armed** while she holds a melee weapon: the CQC stratagem and armory melee weapons (Breaching Hammer, Entrenchment
  Tool, Defoliation Tool, One True Flag, Stun Lance, Saber, Stun Baton, Machete, Combat Hatchet). Right-click today
  only raises the weapon; now its **press** (not the hold) is the parry.
- **Perfect parry:** a hit landing within the window after the press (0.15 s by design; the probe starts at 0.2 s
  and measures real presses) is nulled in the game's hit calculation (no damage, knockdown, stagger, push).
- **Counter ("deployment"):** she is teleported 2.2 m behind the attacker (its facing; ground found by a raycast;
  only within 25 m) and strikes: a heavy hit from behind and a stun. Explosions and attackers out of reach: the hit is
  still parried, no teleport.
- **Miss:** a press that the hit beats (inside 0.4 s) gives nothing; a 0.5 s lockout after a whiffed press stops
  mashing. (Optional later: 0.15-0.4 s blocks half.)
- **Open:** her facing after the teleport (she lands looking where she looked; turning the player is a probe item);
  hits with no warning (Charger rams) are pure reaction.

## Her kit in Helldivers terms
- **Executor (trait):** each perfect parry counts as a deployment: it restarts Swordmanship and, while a skill runs,
  sets off that skill's deployment effect at the counter.
- **Texas Swordmanship (passive):** from each deployment (and each spawn) until her next kill: 30% less damage taken
  and faster melee (melee speed if a field allows it, else +15% melee damage).
- **Texas Tradition (passive):** while a skill runs her melee hits +20%; her first kill during it heals her fully
  (hf->heal) and restarts the skill's timer (once per cast).
- **Z, Silent Drizzle** (12 s): melee +70%; each hit puts a Stun Small build-up on the target (the silence: it stops
  attacking) and marks it for damage over time (a light round every 0.5 s for 4 s from above, Exusiai-style).
- **X, Unrelenting Downpour** (10 s): a sword burst around her on cast and at every counter (an arc/EMS-style blast
  ring, her own blasts spare her); melee +55% and each hit lands a second time (a point-blank round at the target).
- **C, Torrential Sword Rain** (8 s): two bursts around her that stun nearby enemies (Stun Medium), then every
  second a sword rain on up to 4 enemies within 20 m (rounds spawned from above, as Lemuen's shells and Leva's carrier
  bullets fall, with a brief stun). A counter during it adds an extra rain on the parried enemy.
- **T-Doll rules don't apply** (she is Arknights): no Hellbomb on exhaustion; her body is a Sankta-style one like
  Exusiai's (health x1.1, normal stamina).
- **Fast redeploy:** not ported for now (reinforcements are the game's); revisit once the kit plays.

## Building blocks
| Part | From |
|---|---|
| Hit nulling, attacker entity | kit base hit hook (kb_hits.h kb_hit_taken; dealer +0x80, owner +0x100, origin +0x40) |
| Teleport and ground | Faelynn's Loyal Dash (game+0x5A2BB0 / +0x4A7550, raycast exe+0x7F4590) |
| Attacker facing | its root node matrix (forward row) |
| Counter round, rains | spawn_projectile (APW-1 copy; rains like Lemuen's) |
| Stuns | apply_status (Stun Small / Medium) |
| Bursts | spawn_explosion with Own() registration (Alva's) |
| Melee buffs | kb_damage_dealt ScaleDamage on her melee hits |

## Probe (probes/parry-probe, 0.1.0)
Logs every hit on her with the time since her last right-click press, nulls hits inside the window, teleports her
behind the attacker and fires a counter round with a stun. F10 cycles the window (0.15 / 0.2 / 0.3 / 0.5 s),
Ctrl+F10 lets any weapon arm it. What to read from it: how early real presses land before hits (sets the window),
whether the nulled hit leaves anything (status, sound, ragdoll), whether the attacker's facing is right (she lands
behind it, not beside), and how far behind a melee bug's swing the hit lands.

## Reading her attacks (visuals)
The game has no sword-slash effect. Plan (2026-11-05):
- **Slashes (counter, melee procs):** a fan of harmless laser bolts across the target, copies of the LAS-99 Quasar
  Cannon's (Talon bolts are pistol-sized), no damage, no explosion. Parry Probe 0.1.1 tests three looks (F11).
- **Sword rain (S3), Drizzle ticks (S1):** drawn by her HUD, since no game object fits: blades streaking down onto
  each target, projected to the screen as Alva's and Lemuen's markers are (world-to-screen, Chef Shared Loader's way,
  with permission). The damage stays the game's (spawned rounds); the HUD only shows it.
- **Later:** a custom sword-trail particle (HfProjectile spawn_effect) if a particle pipeline appears.
