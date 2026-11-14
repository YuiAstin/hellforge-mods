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
- **0.1.2 (2026-11-13): hearing = an acquisition without sight, after a shot.** Two test missions (loud, then
  suppressed) showed sense bits are sight only (always 1; 1 to 27 m; never from gunfire), so in 0.1.1 no gun revealed
  her and enemies were forgotten 3550 times in one mission. Now an enemy that targets her or gains an entry of her
  without seeing her, within noise_window (1 s) of her last shot (HF_ON_STAT shots), heard it and keeps her heard_hold
  (5 s); the same with no shot is forgotten. Who hears which gun, and how far, stays the game's call.
- **0.1.3 (2026-11-13): her units by the engine's links.** 0.1.1 / 0.1.2 left the gear drawn: the weapons' entity
  units were found by id but what is drawn hangs under them as linked units elsewhere. The DLL now walks the unit
  links (first child Unit +0x1D8, next sibling +0x1E0, guarded by the two instructions the Optical Cloak Pack pins)
  from her Helldiver's unit and from each of her ten equipment slots the attach manager says hangs on her, and hands
  the HUD side the ids (AscalonHud.wanted).
- **0.2.0 (2026-11-14): the Ambusher trait and Pursuit, as Yui set them after a second look at the wiki.** Kept out
  on purpose: the modules (a later system), Descent's long form (ours stays: the short immunity dive). In: every hit
  of hers strikes every enemy within 15 m (always on; spawned rounds as the Executor's area rounds, the amount set in
  the damage event, not noise); Pursuit with no gas: 210%, twice, and the slow (HF_STATUS_FLAMER_SLOWED) on everyone
  struck; 3 charges, one every 7 s. Correction to "Source kit" above: in Arknights she is not invisible; "always
  hidden" is this port's own idea. Not built: Mortal Interrogation's stacks and damage a second, Penumbral Image,
  Endowment, Descent.
- **0.2.1 (2026-11-14): Mortal Interrogation and damage numbers** (Yui: the trait, melee and damage work; "without the
  damage popup it is hard to know if she hit twice", and the DoT was missing). Marks: a stack a hit or strike (3), 25 s
  from the last, a tick a second of 10% of the marking hit a stack, the flamer slow 18 a stack. HellForge has no
  "damage this entity" action: a tick is a spawned round set 2.5 m from the enemy's body on her side, flying into it,
  its amount set in the damage event (no line from her needed). Numbers: the Executor's block (ring, view, projection).
- **0.2.2 (2026-11-14): the mark's damage is the game's bleed** (Yui's idea). apply_status with status 2 (bleed in
  HD2Runtime's status catalogue, domains/status_catalog.lua; our SDK's ids agree with it where both name one: 5 fire,
  37-39 stuns, 42 gas, 67 flamer_slowed), 100 a stack a second. The catalogue also has 8 slowed, 9 rooted, 70 poison,
  36 hidden: candidates for the slow and for Endowment. Untested: whether enemies take bleed, what it deals, whether it
  is credited to her. The first ten bleed hits on marked enemies are logged; a refusal falls back to 0.2.1's tick.
- **Correction to 0.2.2:** HellForge refused status 2 ("not a status HellForge applies (HF_STATUS_...)"): apply_status
  takes only the ids its header names. What Yui saw working ("even for mechanical enemies") was the fallback, 0.2.1's
  spawned-round tick. A real bleed would need the game's own add-status call (game.dll+0x699E40, Alva hooks it).
- **0.3.0 (2026-11-14): Endowment and Descent**, as designed above with these changes: Endowment's slow reaches her
  whole range (e_radius 15, not 8); Descent has no separate cleave (the trait already hits everyone within 15 m) and no
  taunt (she is simply seen: the visibility cut and the forgetting are off while it lasts). Kills counted for both:
  any enemy that dies within 30 m of her. Keys X and C from the ini; no bindings menu or skill panel yet, a second
  status line shows both skills.
- **Penumbral Image:** on hold. No animation or melee speed field is known (HellForge maps a melee weapon's stamina
  cost and hit volume only; HD2Runtime's API has none). Candidates: the engine's Lua animation calls (a probe), the
  unmapped part of MeleeWeaponComponentData. Yui chose the parry and the dash instead for now.
- **0.4.0 (2026-11-14): the parry and the dash** (Yui: "give her her parry since she is a melee fighter too"; "while
  she is moving and uses the melee button, F or, on a dedicated melee weapon only, LMB, she dashes a short distance
  forward and deals melee damage to enemies on the path"). The parry is the Parry Probe 0.1.5's (right-click within
  0.3 s before the hit, a CQC weapon in hand, 1 s with no hit after, behind the attacker, a medium stun), its counter
  her own: an area volley of parry_damage 300 instead of the probe's APW-1 round; no slash look. The dash: 6 m the way
  she moves (2 m/s or more over the last 0.15 to 0.5 s), Voymastina's SpotAt for the landing (nearer in 1 m steps, not
  more than 1.5 m up), every living enemy within 2 m of the path struck for dash_damage 150 by a round spawned beside
  it; 1.2 s between dashes. The game's own melee swing still plays and, when it lands, sets off the 15 m strikes.

### 0.4.1: the parry's clash

- The parry plays one of nine clash recordings (Yui's samples) from a sound bank of the mod's own.
- How other HellForge mods ship sound: Wwise banks as patch files in `data/`, played by event id. Scope of Authority's
  Nova bank is the Quasar's structure with new ids and the Nova's recordings; this is the same idea.
- `tools/build_bank.py <shell patch> <out patch> <wav>...`: the shell is the CQC-2 Saber's bank
  (`wwise_bank`/`wwise_dep` include of `0x550387E4C38E63B4`, built with `HellForge.Cli build-data --out`). It keeps the
  2D one-shot random container of event `FF783C88` (player bus) and its mixer chain, gives the container, sounds,
  action, event and bank new ids, and stores the recordings as PCM media (mono, with a `hash` chunk).
- Found on the way: a bank's id in `BKHD` is FNV-1 of its name xor `0xA3107038`; the bank resource is
  `[magic][u32 size][u64 murmur64 of content/audio/<name>]` then the bank; the dep resource is the path string.
- The recordings and the built patch are not in the repository (`.gitignore`): the samples are a YouTube channel's
  recreation of Sekiro's clashes, with no permission asked yet. In game: the parry worked, the sound did not (the
  engine never had the event: `Wwise.load_bank` only takes a resident resource and no package lists a bank of our own).

### 0.4.2: the sound in a resident bank, the skill panel, MODS rows

- Sound: the event, container, sounds and media are added to `obj_hellbomb` (`0xD2C617C603DE1B92`, resident in every
  snapshot of HD2Runtime's catalogue), which the mod ships as `data/9ba626afa44a3aa3.patch_1`. `build_bank.py` takes
  the Saber patch and the host patch; it carries the Saber's mixer chain, attenuations and effect the host lacks, in
  the Saber bank's own order, and checks its patch writer against HellForge's own file byte for byte.
- A patch entry's sixth field is the running total of the sizes before it, each rounded up to 256 (0 on the first).
- Skill panel beside the stratagem list (the kits' shared list and bindings parts): Pursuit's charges, Endowment and
  Descent with key, time running and cooldown. Bindings: Endowment, Descent, the dash.
- 28 settings as rows on the MODS tab.

### 0.4.3: why the sound was "not loaded", the dash's damage, getting unstuck

- `Wwise.has_event` is a lookup in the game's event metadata (`wwise_metadata` `0x11AB54001F79F739`, 28-byte records:
  id, max attenuation, max and min duration, kind, 2D/3D, one more), not in the loaded banks; HD2Runtime's
  `hd2.sounds.play` refuses an event it does not list. `build_bank.py` now also takes that resource and appends a
  record for `ascalon_parry` (the Saber one-shot's, with our durations); the HUD script posts the event itself when
  HD2Runtime still refuses. Whether 0.4.1's own bank was in fact loaded is therefore not known.
- Dash: its strikes have their own list (the same press's melee hit wiped the shared one); wider path, reaching the
  enemy she stops in front of.
- Standing still, the melee press takes her out of a spot she is stuck in (underground, or wedged for 3 s).

### 0.4.4: spots looked for from above, the event in two banks

- In game (0.4.3): `has_event` true; `hd2.sounds.play` NOT_PLAYED; the plugin's `trigger_event` on the main world
  returned 0. So the sound engine does not have the event: the Hellbomb bank was not loaded, or not taken from the
  patch. The event is now added to `stratagems_shared` too (`build_bank.py` takes any number of hosts), and the first
  parry logs a menu click and two Hellbomb bank events posted the same ways, as a baseline.
- Underground teleports: `Surfaces` looked down from 2.5 m above her; uphill that starts under the terrain ahead.
  `SpotFrom` looks from 12 m (dash, parry); the stuck way out needs a second in place and goes straight up first.

### 0.4.5: checks before the move, the clash through Windows

- Dash only while moving; `SafeSpot` before any teleport (dash, parry): a real surface she fits on seen from 40 m up,
  height limits, nothing solid on the way at two heights both ways, the spot's ground under her feet.
- Sound, what 0.4.4's log showed: a menu click through `hd2.sounds` plays; the plugin's `trigger_event` returns a
  non-zero id for a name the engine has and 0 for `ascalon_parry`, with the event in both host banks. So a bank in a
  `9ba626afa44a3aa3` patch does not replace the game's copy (the metadata in the same patch does). The game's copies
  of a bank sit in the archives of the packages that load it; game data is DSAR-packed (`bundles.NN.nxa`, LZ4 chunks;
  the 15 loose files are DSAR-wrapped archives), and which archives hold a bank is not mapped yet.
- Until then `ParrySound` plays a `.wav` of `sounds/` through winmm's PlaySound (`parry_sound`).
