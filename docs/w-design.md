# W (Arknights): kit design

Status: first builds (mods/w, 0.2.1): King of Hearts played 2026-11-25, with her page on the options screen. The rest is design.
Source kit: W, Sniper (Artilleryman), at rank 10 of each skill. Her module is left out, as Ascalon's is.

## Decisions (Yui, 2026-11-25)

- Skill damage: the grenade launcher's own damage, multiplied. No fixed blast, no health floor.
- King of Hearts hands her a GL-21 with a 40-round magazine. Each round does skill damage and freezes what it hits in
  its pose (as the time stop does, so that Bile Titans and the like are held too), not a stun. When the 40 are spent
  she gets back the weapon she held, with full ammunition: only which weapon it was is kept, nothing else of its state.
  A support weapon she carries is deleted and a new one of its kind made for her afterwards.
- Plain GL-21 grenades, not homing.
- Jack in the Box holds up to 5 charges (her own rule, not Arknights'). A press spends one charge and places one mine:
  under an enemy in front of her, in a box 15 m wide and 40 m long from her (numbers to start with); with no enemy in
  the box, at a random spot inside it.
  With several enemies in the box: the one nearest her aim line. A charge comes back every 8 s. No limit on mines by
  design; a cap of 30 out at once by default, for safety (the oldest goes first).
- D12 marks enemies or structures; a separate key detonates the marks. Default B, rebindable on the options screen (F9).
- Ambush: both versions built, to choose between by playing them: on 10 s after she lands (and after each reinforce),
  or on after 10 s without firing.

## Passives

| | Arknights | Here |
|---|---|---|
| Artilleryman (trait) | Her attacks deal area physical damage | Her hits also strike the enemies near the one hit, at reduced damage (Executor's and Ascalon's area hits) |
| Insult to Injury | Enemies in range take +18% physical damage while stunned | Enemies held by her skills take 18% more from her |
| Ambush | 10 s after deployment: 60% dodge, less likely to be targeted | 60% of hits on her miss and enemies notice her less (Ascalon's hidden state without the invisibility); the trigger in two versions, above |

## Skills

| Key | Skill | Arknights | Here |
|---|---|---|---|
| Z | King of Hearts | 16 SP. A grenade: 350% ATK to all hit, stun 3 s | The GL-21 in her hands, 40 rounds, each a multiple of its damage and a pose freeze. The cooldown starts when the launcher is put away. Z again, a weapon switch or her death ends it early |
| X | Jack in the Box | 8 SP, automatic. A mine in range, 120 s; an enemy passing sets it off after 1.5 s: 280% ATK around it, stun 2.2 s | 5 charges, one mine a press, placed as above. An enemy near it sets it off after 1.5 s: the blast and the hold |
| C | D12 | 33 SP (20 to start). Bombs on up to 4 enemies, highest health first; after 3 s each deals 310% ATK around it, stun 5 s | A mark on the enemy or structure under her crosshair, up to a cap. The cooldown starts at the detonation |
| B | Detonate | | Every mark blows: the blast and the hold |

## What is known to work

- The weapon swap, played in three cases (no support weapon; one on her back; one in her hands): the Wield Probe's
  0.9.0. The weapon's entity spawned, taken into the support slot by the game's own pickup, selected by the
  Helldiver's own switch request; the reverse, and a new support weapon of the kind she had. 40 rounds hold in a
  10-round GL-21.
- The pose freeze: the Time Stop Probe's method 2 with the hold (branch `timestop-probe`): a stun, the enemy's
  animation state machine switched off from the HUD mod's Lua, and the enemy held where it stands. Yui's choice of the
  three methods for the time stop. In W 0.2.0 it runs per enemy, each with its own end, and held a Bile Titan (Yui).
  An enemy a round kills is left to die as the game has it: the hold takes only those a blast left alive (0.2.1).
- King of Hearts' damage: 3.5 times the launcher's reads right in play (Yui).
- Her own blasts spare her and what she carries: Destroyer's handling.

## Built, not yet played (0.3.0)

- Artilleryman: a shot of hers that lands on an enemy sets off a small blast there (the PLAS-1 Scorcher's explosion by
  default), not while the launcher is out nor within 5 m of her. A blast, not Ascalon's area strikes: it is an
  Artilleryman's.
- Insult to Injury: 1.18 times her damage on an enemy her skills hold.
- Ambush, both triggers: 60% of the hits on her are cancelled, and enemies within 80 m that do not sense her at the
  moment forget her (their target and their perception entries of her: Ascalon's forgetting).
- The mine census: what an MD-6 Anti-Personnel Minefield's deployer spawns, and its mines' explosion types, logged, to
  find the game's own mine for Jack in the Box.

## Built in 0.4.0

- Jack in the Box with mines the kit keeps itself (the minefield's deployer spawned nothing mine-like through the spawn
  event, so the game's own mine is not found yet). 5 charges, one back every 8 s; a press lays one under the living
  enemy nearest her aim line in the 15 m x 40 m box, or at a random spot in it at her height. An enemy within 3 m sets
  it off after 1.5 s: the MD-6 mine's own explosion (type 366), its damage 2.8 times, enemies within 5 m held 2.2 s. At
  most 30 out, the oldest goes; one lasts 120 s.
- Still probing: where a mine lies is shown by an entity spawned bare there, by default the game's CONTACT MINE entity
  (the MD-6's explosion is named ContactMine), and only once the mission has spawned one itself or its package is
  resident. If that entity is a live mine it blows by itself: the kit counts that as the mine's blast, and logs it. The
  census now lists every explosion for 3 minutes after a minefield's deployer spawns.

## Open

- How long the freeze lasts (3 s to start). An enemy already held and then killed by a later round.
- Mines: the game's own mine entity spawned (its own trigger and blast, the kit adds the hold), or spots the kit
  tracks itself with a marker.
- Whether a kit blast destroys bug holes and fabricators (D12 on structures).
- A weapon switch by the player while the launcher is out.
- A backpack-fed support weapon: the new gun and the backpack she still wears.
- The numbers: the damage multiples, the splash radius, the mark cap, the mines out at once.

## 0.4.1

- The game's mines are not entities: a minefield's blasts (type 366) come from its deployer, and the game's code has a
  MinefieldComponent (128 a field) and a MineSpawnerComponent (game.dll 0x9DB8A0 to 0x9E2600). Laying one of the game's
  own mines means adding to a deployer's field: not researched yet. Until then a mine is a marker on the HUD.
- An enemy that died while held stood dead in its pose (its state machine was off as it died). The HUD now switches its
  machine on and asks for a death event, and logs which events and animation functions the game's Lua has: to be read.
