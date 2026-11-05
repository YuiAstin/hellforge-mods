# Ascalon: the always-hidden kit (design, 2026-11-05)

Arknights' Ascalon is an Ambusher Specialist who is invisible until she attacks (the dodge and "rarely targeted" are
the Ambusher class trait, shared by the branch). In Helldivers terms she is **always hidden until she makes noise**:
melee is silent, a suppressor is heard only close by, a loud gun gives her away. Nothing here is built; the Cloak Probe (probes/cloak-probe) and the
perception research (docs/research/perception.md, when written) come first.

## Source kit (Arknights, E2 max, M3)
- Trait (the Ambusher branch's): damage to all targets in range; 50% dodge of Physical and Arts attacks; less likely to
  be targeted.
- Her own: invisible until she attacks.
- Talent **Mortal Interrogation**: hits slow 18% and deal 10% of her ATK as Arts damage a second for 25 s (3 stacks).
- Talent **Penumbral Image**: +8 ASPD (+6 more with high ground adjacent).
- S1 **Pursuit** (auto, 3 charges): the next attack at 210% ATK, twice.
- S2 **Endowment** (35 s): ATK +130%, enemies in range -60% speed, kills spread a Talent 1 stack.
- S3 **Descent** (45 s): ATK +50%, wider range, faster attacks, enemies in range -50% hit rate, taunts, heals 8% max
  HP on each miss or dodge.

## Hidden (core)
- **Always hidden** while she makes no noise: enemies lose track of her (Lost Contact's break contact, applied to every
  enemy while hidden) and, if the research finds how, never spot her in the first place. Looks: the Stalker's cloak on
  her body (Cloak Probe), with a smoke puff when it first switches on to hide any bug texture.
- **What reveals her** (detected through HF_ON_STAT's shots-fired count and her gun's ProjectileWeapon.silenced flag):

| She | Reveals | Hidden again |
|---|---|---|
| moves, stands, melee (kills too) | nothing | - |
| fires a suppressed gun | only to enemies within 15 m (they spot her; the cloak flickers) | 2 s after her last shot |
| fires a loud gun | to everyone, as normal | 5 s after her last shot |
| throws a grenade, an explosive goes off near her | everyone near the blast | 5 s |
| wears a backpack or holds a support weapon | no functional hiding at all (the user's rule); the cloak still shows | - |

- **Ambusher trait (class flavour):** while hidden, enemies that do attack her (blind fire, area attacks) miss half the time (a 50%
  dodge: the hit cancelled in the kit base's hit hook).

## Her kit in Helldivers terms
- **Mortal Interrogation (passive):** her hits (melee and guns) put a stacking mark (3) on the target: -18% speed (the
  flamer slow's build-up) and a small damage tick each second for 25 s.
- **Penumbral Image (passive):** melee and fire rate a little faster while hidden (melee speed if a field allows it,
  else +10% damage).
- **Z, Pursuit (3 charges, one back every 7 s):** her next melee hit lands twice at 210%. Silent.
- **X, Endowment (35 s):** her damage +130% and every enemy within 8 m slowed 60% (the slow status, refreshed); kills
  spread a Mortal Interrogation stack to enemies within 5 m.
- **C, Descent (45 s):** she steps out of hiding on purpose: damage +50%, her melee hits everything within 3 m in
  front of her (a cleave: extra hits on the others, like Exusiai's shots), enemies within 15 m are taunted onto her and
  miss half their attacks, each miss or dodge heals 8% of her health. Ends hidden again.
- Body: Arknights rules (no T-Doll Hellbomb); health x1.0, stamina x1.2.

## Open questions (for the research and the probes)
1. Does break contact also stop re-detection, or do enemies spot her again at once? If so, where is the perception
   check to suppress (the research agent's question).
2. Does a suppressor attachment set ProjectileWeapon.silenced on her gun's live record, and does HF_ON_STAT fire per
   shot for every weapon?
3. Can the cloak material be put on her (Cloak Probe), and can its strength be driven (a fade)?
4. Do enemies "hear" a shot as a position (the game's noise events), so a 15 m bubble can use the game's own hearing?

## Building blocks
| Part | From |
|---|---|
| Losing track | Lost Contact (break contact) |
| Cloak look, smoke | Cloak Probe |
| Shots and suppressor | HF_ON_STAT, ProjectileWeapon.silenced (live record) |
| Dodge, cleave, marks | kit base hit hook and damage events; apply_status slow |
| Taunt | Lost Contact's patrol redirect / set_destination toward her (to research) |
