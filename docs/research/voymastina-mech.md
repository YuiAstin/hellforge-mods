# Voymastina: mech mode and the Sinbreaker (plan and research)

Plan for Voymastina's second stage. Her kit moves onto an exosuit, and she gets an exosuit stratagem of her own, the
**Sinbreaker**. This note records what is known (with sources), what has to be probed live first, and the build order.
Game build **25480438**; HD2Runtime's research build F5FEE03DCFDB is the same build
(`HD2Runtime/schemas/build_profile.json`). RVAs are `game.dll` unless marked.

## The character (GFL2)

- **Sinbreaker (断罪者):** a custom exoskeleton built for her by Nele. 3.25 m tall, 6.8 m with its weapon raised, and
  armed with a pile bunker. Neural fortification changes its look: yellow details and a purple-accented pile bunker at
  V2, a light-grey pile bunker at V4 ([IOP wiki](https://iopwiki.com/wiki/Voymastina)).
- **Her kit as Prydwen lists it** ([Prydwen](https://www.prydwen.gg/gfl-exilium/characters/voymastina)):
  - **Howl from the Firmament (Sirius Fall):** 120% ATK area hit. Its passive gives allies' targeted hits a 3-tile
    Action Support.
  - **Eye of the White Mastiff:** 2 rounds of Wolf Eye. Lock On deals 80% ATK through cover, at most 6 per round. Its
    passive hits the 5 nearest enemies at battle start, and she is immune until that passive triggers.
  - **Pile Bunker:** a 200% ATK melee hit. Its passive charges to the target when an ally hits it, dealing 150% ATK.
  - **Mastiff's Vow:**
    - immune to displacement;
    - damage taken −30% (−80% while her Stability holds);
    - ignores 50% of the target's defence;
    - the Confectance Index fills into Predation Protocol.
  - **V6:** Pursuer (+45% damage), Hunting Rhythm stacks.
- **What the mod already has (0.2.x, on foot):** all of the above, except the V-level effects and the ally-triggered
  passives, which are hers alone in a solo game.
- **Possible later additions from the kit:**
  - **Dismay** (on Lock On targets): enemies slowed, their attack lowered;
  - **Frightened** (from Pile Bunker): targets take more damage from her;
  - **Coordinated Hunting** stacks.

## What the mech mode is

When she pilots an exosuit (any exosuit, or only the Sinbreaker, a choice still to make), her kit runs from the mech:

| Kit piece | On foot (now) | In the mech |
|---|---|---|
| Sirius Fall | her next hit sets off a recoilless blast | the mech's next hit does |
| Support Action, the charge | her shots and melee | the mech's arms' hits |
| Eye of the White Mastiff | railcannon Lock On, she is immune | the same from the mech; the mech is immune |
| Pile Bunker | she lunges, then a strike line and a railcannon | the **mech** lunges (or stays put if a vehicle cannot be moved: see the probe), same strikes |
| Mastiff's Pledge | no knockdown, damage kept off, Stability | the same on the **mech's** hits (its body and both arms) |
| Bare hands | her fists | none (the mech has no melee) |
| T-Doll body | health / stamina | unchanged (she is inside) |

## Known (from HellForge SDK 15 and HD2Runtime)

### Finding the exosuit
- **No event for entering or leaving a vehicle:** HellForge has none (events 1–15, `hellforge_mod.h:501-517`), and
  HD2Runtime has none either.
- **Telling which exosuit is hers:** `HF_ON_SPAWN` with resource `HF_ENTITY_EXO45_PATRIOT_EXOSUIT`
  (`0x79E4B3D2DA5E45E3`), `HF_ENTITY_EXO49_EMANCIPATOR_EXOSUIT` (`0xC2D449ECF7FACAB1`), or the Sinbreaker's own id.
  Another source is the transport Pelican's Transport component (`game+0x3326518`, 0x40-byte elements: +0 cargo
  resource, +8 the spawned cargo entity).
- **What a Patriot owns** (HD2Runtime `enemy_controls`, `combat_walker`):
  - Health, Mount, Motion, Locomotion, VehicleLocomotion, Rotation;
  - **SeatCollection**;
  - Interactable, Tag, Faction, Behavior, Unit.

  Nobody has mapped SeatCollection's manager or layout yet.

### Position and moving it
- **Reading its position:** read the unit's root pose the same way the mod reads hers (`UnitNode(unit, 0)`). The unit
  id is at descriptor +12.
- **Teleporting:** the warp position setter `0x5A2BB0` and the velocity setter `0x4A7550` are only known on a
  Helldiver.
  - The velocity setter writes the Motion manager (`game+0x3326558`, stride 0x84). It does not check that a record
    exists, so use it only on an entity that has a Motion component. The Patriot has one.
  - Whether either function moves a mech, or carries its pilot along, is **unknown**.

### Arms and hits
- **The arms are separate weapon entities** in the Mount component: 5 slots of 24 bytes, slot 0 `left_gun`, slot 1
  `right_gun` (`hellforge_components.h:308-327`, HD2Runtime `vehicle-weapons.md`).
- **How a shot's owner is set:** the shot's owner is the wielder of the firing weapon (Wieldable, `game+0x3326730`,
  hash +0x18, u32 wielders at +0x38; the fire path at 0x612A18..0x616502). Who wields a mech arm (the mech, a seat or
  the pilot) has **not been traced**.
- **What this means for the hit check:**
  - Her hit attribution (0.2.11: held by her first, then her items) will not count mech-arm hits until the probe shows
    their wielder.
  - Expected fix: a hit counts as hers when its holder is the mech she is piloting.
  - Fallback if credit is missing: `hf->credit_to_player(entity)` covers "a vehicle a mod spawned", solo host only.

### Health
- **Health manager:** `game+0x3326688` (53634696). The value in the code is right; one comment in the mods' source
  wrongly said 0x3326F88.
- **Patriot / Emancipator:** 1800 main health, armour 4. Zones include cockpit_rear, hips, leg_right, leg_left. Each
  arm has its own health.
- **Hits on the mech:** expected to name the mech or an arm as the target, not her. So the damage reduction and the
  no-knockdown rule must match the mech's id and both arm ids.

### The Sinbreaker stratagem (HellForge SDK)
- **Entity:** `add_entity` with `HfEntity`, `base` = Patriot or Emancipator, plus its own `name`, `title`,
  `designation` and `description` (`hellforge_mod.h:102-124`).
  - `hf_mount(mod, entity, slot, carried)` swaps an arm.
  - Entities and weapons share 64 slots across all mods.
- **Stratagem:** `add_stratagem` with `HfStratagem` (`hellforge_mod.h:213-251`):
  - `base`: `HF_STRATAGEM_EXO45_PATRIOT` (27, a Pelican drop) or EXO-49 (10);
  - `payload`: {Sinbreaker, `HF_ENTITY_PELICAN_TRANSPORT` 0x75BE82ED8592A6B3}, `payload_count` 2;
  - `flags` 0 keeps the traits (`HF_TRAIT_VEHICLE` 0x20, `HF_TRAIT_EXOSUIT` 0x100000);
  - `cooldown`, `uses`, `code`, `icon`.

  Vanilla: 420 s cooldown, 3 uses. Room for 48 new stratagems across all mods. HD2Runtime's custom-stratagem API
  refuses vehicles, so this has to go through HellForge.
- **Its arms:**
  - projectile swaps are proven live on the Patriot minigun (projectile 128);
  - both Emancipator autocannons are listed hosts;
  - magazine capacity is at WeaponMagazine +136, up to 2047;
  - fire rate is at ProjectileWeapon +8;
  - turn rate is in the Rotation component;
  - top speed is not in the entity data.

## Bug holes (for her fist, 0.2.12+)

- **What a hole is:** a bug hole is a `bug_spawner_*` entity with a **Destructible** component and **no health**. It
  closes when the destruction system raises its stage from 0 (HD2Runtime `events.md` "Bug holes").
  - The Destructible manager is at `game+0x3326550`: count +0xC, descriptor pointers at +0x4030 + 8i.
  - The destruction world is at `game+0x346D528`: 12-byte records at +0xC5F78, with the stage at +7.
- **Knowing it closed:** HellForge has `HF_ON_BUGHOLE` (14), which fires when a hole closes.
- **Fallback if the blast can't reach:** if the Railcannon fist still fails on a rim, find the bug holes within 3 m of
  the punch (the Destructible manager's descriptors, then their unit positions) and set the fist's blast off at the
  hole's centre.

## Probe first (Mech Probe, one mission with a Patriot)

All of this needs one live run, logged in the mod (no game changes):
1. **Pilot signal:** each frame while near an exosuit, log:
   - the arms' Wieldable wielders (`game+0x3326730`);
   - `HF_ON_WEAPON` events;
   - her avatar's root position against the mech's;
   - every component manager in `game+0x3320000..0x3330000` whose hash holds the mech (the Sweeper probe's method), to
     find SeatCollection and its occupant field.
2. **Hits:** the dealer, owner and creditor of the arms' hits on enemies (`HF_ON_DAMAGE`), and the targets of enemy hits
   on the mech.
3. **Moving it:** on a key, call the warp setter on the mech for a 3 m move. Log whether the mech moves, and whether
   she moves with it.

## Build order

1. **Mech Probe** (above). Output: the pilot test, the arms' wielder and the move result.
2. **Mech mode in Voymastina:** the kit's checks take "her" to mean her body *or* the mech she pilots (hits, the
   damage reduction, Eye immunity, Pile Bunker's origin). Pile Bunker teleports the mech if the probe shows that works,
   else it strikes from where the mech stands.
3. **Sinbreaker:**
   - an entity copy and a stratagem, with its own name, text and code;
   - an icon once one exists;
   - arm and stat changes as decided.
4. **HUD:** while piloting, the same skills, plus the mech's health in the bottom bar.

## Decisions for Yui

- **Mech mode:** any exosuit, or only the Sinbreaker?
- **Sinbreaker's base:** Patriot (minigun + rockets) or Emancipator (two autocannons)? Its arms could also become
  something closer to a pile bunker:
  - a heavy slug arm (for example the Recoilless round on a slow fire rate);
  - a Railcannon-blast arm.
- **Pile Bunker in the mech:** a real mech lunge (if it can be moved), or strikes from where it stands?
- **Stratagem limits:** keep the vanilla 420 s cooldown / 3 uses, or make it hers (one call per mission with a shorter
  cooldown)?
