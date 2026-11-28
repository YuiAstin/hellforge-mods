# Hina: the Gehenna Prefect's machine gun (design)

Sorasaki Hina of Blue Archive (Gehenna Academy, head of the Prefect Team; her MG, The End: Destroyer). Her
identity here (Yui, 2026-11-27): put as many rounds down range as possible, as efficiently as possible. First released as
`mods/hina` 0.1.4 (2026-11-28), played by Yui on HellForge 0.5.

## Her kit in Blue Archive

| Skill | In Blue Archive |
|---|---|
| EX: Denouement: Ishbosheth (cost 6) | A fan-shaped area: 636% of her attack to every enemy in it |
| Normal: Lock & Load | When her ammunition runs out she reloads at once and gains 21% attack for 16 s |
| Passive: Prefect Team Member Judgment | +14% attack speed |
| Sub: Out-and-Out | +2.7% damage to enemies not in cover |

Blue Archive has one active skill (the EX); the rest are passives. Yui kept the three passives as passives and gave her
two actives of her own (Z, X), with the EX on C.

## In Helldivers (0.1.4, as released)

All numbers are settings (her page on the options screen, F9, and `hina.ini`).

### Z: The End: Destroyer

- Her gun in her hands, the way W's King of Hearts gives the GL-21 (the Wield Probe's swap). **An MG-43 copy by
  default** (her MG42: Yui, 2026-11-28); a copy of the **M-1000 Maxigun** as an option for a profile that owns it
  (`gun_base` 0; it comes with a copy of its backpack, which holds its rounds).
- **1200 rounds at 1200 rounds a minute** (a minute's fire), **armour penetration 5**, **durable damage 53** (the
  MG-43's own 23 felt terrible: Yui). Her passive's 14% on the rate.
- **Belts:** a 250-round box by default, or a 50-round drum (`gun_drum`) with +30 ergonomics, the Liberator's drum
  model, reloads on the move and the Liberator Concussive's reload animation. Her rounds are the belt in the gun and
  spare belts (the game's own reloads; 31 spares at most, the network field's 5 bits).
- **Scope** (`gun_optic`): the 1.5x red dot (the MG-43's own), the 4x Combat Scope or the 10x Sniper Scope.
- **Fire rates in the weapon menu:** the MG-43's own 630 and 900 around hers (1200, the default), each with her
  passive's share. The game has three slots only.
- **It goes like W's launcher:** she switches to her primary or secondary (0.5 s), drops it, spends its rounds, or
  presses Z again. A stratagem or a grenade in her hands is not a switch. Her old support weapon is dropped where her
  gun fell (by a SEAF trooper's hands, spawned out of sight and removed); a weapon she picked up stays in her hands.
- **Cooldown 30 s from when the gun is gone.**
- **Her shield while she holds it:** 3000, refilled every 5 minutes, draining 100 a second once the gun is gone.

### X: Reload and Destroy

- The gun in her hands refilled in full (magazine and spares), whatever gun it is: in place when the kit can fill its
  magazine, else a fresh gun of its kind, full, takes its place.
- On her own gun also **+80 ergonomics and no recoil** (a new gun with the handling, full).
- Cooldown 20 s.

### C: Ishbosheth (her EX)

- Readied by the key; **her next shot** (any gun) sets it off: a **10 s barrage** that fires only while she is firing
  (Blue Archive's Hina cannot move during it; the timer runs regardless).
- Every 0.1 s a volley of tracers across the fan, **2 in each of its 3 sections** (two outer, one inner), slowed to
  200 m/s so the trails are seen: each deals her gun's round (90, AP 5).
- Every 0.5 s the 12 nearest enemies in the fan out to 40 m each take a tracer dealing **6.36 times** her gun's round
  (572 / 337 durable, AP 5).
- The fan is 30 degrees each side of her aim, **10 while she aims down her sights** (the game's own aim state: hold or
  toggle).
- Cooldown 30 s from its end.

### Passives

- **Lock & Load:** a magazine she empties reloads at once (no spare used) and her shots deal 21% more for 16 s.
  **Once each 60 s for each gun** (by its kind). Magazine guns, guns loaded by rounds (the Sweeper) and her MG-43 copy
  (its belt; her round count unchanged); not weapons of one or two rounds (a launcher), not the Maxigun copy (fed by
  its backpack: the buff only). A gun's time is spent only when the reload lands.
- **Prefect Team Member Judgment:** her own gun fires 14% faster.
- **Out-and-Out:** her shots deal 2.7% more while she is crouched or prone (her head lower than 1.2 m over her feet; the
  game has no "in cover" to read).

The damage bonuses are on her own hits (the damage event), as every kit's buffs; nothing of the game's own weapons,
rounds or damage types is changed.

## What it reuses

| Part | From |
|---|---|
| The gun in her hands, her weapon back | W's King of Hearts (the Wield Probe) |
| The shield pool | Alva (`kitbase/kb_allies.h`) |
| The fan of fire | Koleda's Full Speed cone |
| The premium fallback | Voymastina |
| Her copies' numbers | the Veterancy System's Gatling (AP and fire rate edits on rows of its own) |
| Ishbosheth's tracers | Koleda's Full Speed tracers (copies of the APW-1's round, slowed) |
| Her old weapon dropped | the Escort Probe's SEAF trooper as a hand |
| Options screen, HUD, keys, roster | every kit |

## Research

- **Held Probe** (branch `held-probe`, `probes/held-probe`): what her inventory selection reads while she calls a
  stratagem, throws a grenade, switches weapons and drops her support weapon. The kit takes only a primary or secondary
  selection as a switch; the probe's log says whether a stratagem or a grenade reads as one of those.

- **Maxi Mag Probe** (branch `maxi-mag-probe`, `probes/maxi-mag-probe`): a Maxigun with a magazine of its own, two
  tries (the Maxigun with the MG-43's magazine added; the MG-43 wearing the Maxigun's model and firing record). If one
  fires from its own magazine, her gun needs no backpack.

## From the game's code (2026-11-27)

- **The Maxigun has no magazine** (HD2Runtime's weapon catalogue: backpack-dependent, a linked-ammo record, no magazine
  or feed): hence the backpack.
- **The inventory's take** (0x9AE030) takes slots 1 primary, 2 secondary, 3 support, 4 and 5 the held item, **6 the
  backpack**; the item in that slot is dropped first.
- **6000 rounds:** adding rounds (0x76E080) stores a 32-bit count with no cap; 2047 is only the count sent to the other
  players (in co-op, theirs would read wrong).
- **The fire rate is capped by the frame rate:** a weapon's frame (0x616AC0) fires at most one shot (its timer is the
  60 / rpm interval), so 6000 rpm (100 a second) needs 100 frames a second; at 60 fps she fires about 3600. A way past
  it, not built: her round's own row could fire two projectiles a shot (ProjectileSettings `pellet_count`).
- **The Maxigun check:** its loadout id is its catalogue item (as the MG-43's is), and the check (0x136FB40) reads the
  player's holdings of it.

## From its first start (2026-11-27, logs only: the game crashed on the ship before a mission)

- All three copies registered (both guns, the backpack). HellForge gives each a loadout record, so `arsenal.tsv` lists
  them (as support weapons and a backpack), but a support weapon or backpack reaches a player only through a
  stratagem, and they have none: no loadout shows them (Yui). Kept that way: no stratagem for them (were one ever
  added, Koleda's way keeps a stratagem out of the loadout).
- The Maxigun's copy took her round; **the MG-43's refused it** ("not one HellForge's catalogue knows the packages
  of"), so it fired the game's own round (AP 3). 0.1.2 writes the round into the gun's own record, and asks for the
  gun's package from the mission's start (her rounds' look, and Ishbosheth's rounds before any gun is out).
- The Maxigun's linked-ammo record (56 bytes) names no backpack, and the backpack's feed field names no weapon: the game
  pairs them by kind, so her backpack copy should feed her gun as it is. Its 6000 rounds were set.
- Maxi Mag Probe: try A refused (no network type for that set of parts); try B registered, with a magazine of its own
  (6000 set) firing the Maxigun's round at 1500 rpm.

## From play (0.1.3 to 0.1.4, 2026-11-28)

The game's code notes (addresses, layouts) are in [research/game-notes.md](research/game-notes.md#weapons-magazines-rounds-and-aim-build-25480438).

- **Her avatar comes from her weapon events:** the player spawn event fires only on the ship, so the kit learns her
  avatar from the first weapon event that names her, and her network id from the player list.
- **A magazine's rounds are an 11-bit network field (2047 at most), spare magazines 5 bits (31):** her rounds as belts
  (gun_rounds = her rate) fit, where 6000 in one magazine did not.
- **AddMagazineRounds crashes on a weapon not in its map** (the Sweeper): the kit looks first. Guns loaded by rounds keep
  theirs in another manager, filled by the game's own set-loaded-rounds.
- **A gun spawned straight from its entity builds no attachments:** its scope drew the missing-model mark until the
  spawn was handed the attachments' list the game's pickup spawn builds.
- **The MG-43's round copy:** the round it fires is its record's 148, not the pattern's.
- **HellForge 0.5 moved the catalogue's fields,** so its HasCatalogueItem is refused; the kit's own copy reads
  ownership by the item's mixId (by itemId the Maxigun reads 0).
- **The warp setter (0x5A2BB0) crashes on a weapon;** a SEAF trooper's hands drop one instead: take it into its slot 3,
  drop it, clear the slot's entry, remove the trooper the next frame (without the clear, the trooper takes the gun
  with it).
- **Bug found in play:** a clear meant for the mission's start sat in X's fresh-gun step, so every X reset the reload
  passive's per-gun times.
