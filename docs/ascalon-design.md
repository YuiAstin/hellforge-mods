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
- S1 **Pursuit** (auto, 3 charges): the next attack at 210% ATK, twice; like all her attacks it hits everyone around
  her (the trait), and Mortal Interrogation adds the DoT and slow.
- S2 **Endowment** (35 s): ATK +130%, enemies in range -60% speed, kills spread a Talent 1 stack.
- S3 **Descent** (45 s): ATK +50%, wider range, faster attacks, enemies in range -50% hit rate, taunts, heals 8% max
  HP on each miss or dodge. (Ported as immunity instead: a 50% dodge on a melee-only kit is a slow death.)

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
- **Pursuit (auto, 3 charges, one back every 7 s; no key):** as in Arknights it fires by itself: her next melee hit
  with a charge sets off a gas burst around her (Faelynn's gas blast, own-blast guarded so it spares her): burst damage
  to everyone within ~4 m, then the gas status ticks (the DoT) and a slow (the flamer slow's build-up) on each one hit.
  It counts as melee: silent, she stays hidden. Mortal Interrogation stacks on everyone it hits.
- **Z, free** (Pursuit needs no key; Z is spare for a later idea).
- **X, Endowment (35 s):** her damage +130% and every enemy within 8 m slowed 60% (the slow status, refreshed); kills
  spread a Mortal Interrogation stack to enemies within 5 m.
- **C, Descent:** she steps out of hiding on purpose and goes all in: **5 s of total damage immunity** (every hit on her
  nulled in the kit base's hit hook: no damage, knockdown, stagger or push), damage +50%, and her melee hits everything
  within 3 m in front of her (the Ambusher trait's hit-everyone, as a cleave). **Each kill extends it** (`descent_mode`:
  1 adds `descent_per_kill` 2 s on top of what is left, up to `descent_cap` 15 s; 0 resets it to 5 s): to be tuned in
  playtest. Enemies within 15 m are drawn to her while it lasts. Ends with her hidden again (2 s grace). The point:
  her usual game is hiding; Descent rewards diving in and chaining kills instead.
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

## Built so far
- **0.1.0 (2026-11-12), the hidden core** (mods/ascalon): the visibility row cut (visual_cut 95%), enemies within
  forget_range forgetting her every forget_every s (Lost Contact's records, no wait, sensed or not), reveals from
  HF_ON_STAT shots (ProjectileWeapon.silenced of the held gun: quiet or loud) and HF_ON_EXPLOSION near her, the gear
  rule (backpack: her fourth item slot; support weapon: held selection 3), the 50% dodge in the hit hook, and the HUD
  side: her units' meshes hidden and re-hidden four times a second, a status line at the bottom middle.
- **The look is plain invisibility, not the Stalker's cloak.** Cloak Probe findings (0.1.5 to 0.1.14): a Helldiver is
  the avatar unit plus a unit per armour part at one root, gear units hang elsewhere; the armour shader ignores
  opacity / opacity_stealth; Unit.set_material with the Stalker's material id crashes (helldivers2.exe+0x4145F1);
  hiding meshes works but the game shows them again, so it is repeated. A shimmer needs our own material shipped in
  the data patch (as the Optical Cloak Pack's content/optical_cloak/materials/cloak): the final polish.
- **Not built:** Mortal Interrogation, Penumbral Image, Pursuit, Endowment, Descent, the skill panel, key bindings and
  Mod Options Menu rows.
- **0.1.1 (2026-11-13): the game's own senses decide (the user's idea).** The invented reveals (shots stat, the
  silenced flag, explosions, 15 m) are gone. Each forget pass reads the sense bits (+0x4C) of her entries in an enemy's
  perception record (and its squad perceiver's): set = it senses her now and keeps her; clear = it only remembers or
  targets her and forgets. She is "revealed" (drawn, no dodge) while any enemy senses her and reveal_hold s after. The
  visibility cut stays on while revealed, so sight alone does not hold her. The log names the first 40 sensings with
  distance and bits: which bit is hearing and which sight is still to learn.
