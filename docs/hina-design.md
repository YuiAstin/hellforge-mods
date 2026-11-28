# Hina: the Gehenna Prefect's machine gun (design)

Sorasaki Hina of Blue Archive (Gehenna Academy, head of the Prefect Team; her MG, The End: Destroyer). Her
identity here (Yui, 2026-11-27): put as many rounds down range as possible, as efficiently as possible. Built as
`mods/hina` 0.1.2 on branch `hina-kit`; not played yet (its first start, 2026-11-27, logged below).

## Her kit in Blue Archive

| Skill | In Blue Archive |
|---|---|
| EX: Denouement: Ishbosheth (cost 6) | A fan-shaped area: 636% of her attack to every enemy in it |
| Normal: Lock & Load | When her ammunition runs out she reloads at once and gains 21% attack for 16 s |
| Passive: Prefect Team Member Judgment | +14% attack speed |
| Sub: Out-and-Out | +2.7% damage to enemies not in cover |

Blue Archive has one active skill (the EX); the rest are passives. Yui kept the three passives as passives and gave her
two actives of her own (Z, X), with the EX on C.

## In Helldivers

All numbers are settings (her page on the options screen, F9, and `hina.ini`).

### Z: The End: Destroyer

- Her gun in her hands, the way W's King of Hearts gives the GL-21 (the Wield Probe's swap): a copy of the **M-1000
  Maxigun**, or of the **MG-43 Machine Gun** for a profile without the Maxigun's warbond (Voymastina's premium
  fallback; the game's own catalogue says whether this profile owns the Maxigun). `gun_base` 1: always the MG-43.
- **The Maxigun's copy comes with its backpack** (0.1.1). The game's Maxigun has no magazine: it draws its rounds from
  the Maxigun Backpack it is linked to. So a copy of the backpack, linked to her gun and holding 6000 rounds, goes on
  her back first (the inventory's take, slot 6: the backpack's), then the gun into her hands. Her own backpack comes
  back afterwards, a new one of its kind, as her support weapon does. The MG-43's copy needs none.
- **6000 rounds, 6000 rounds a minute, armour penetration 5** (the Veterancy System's Gatling upgrade numbers).
- Her gun, its round and its damage are copies of her own (kit base principle 6): an entity copy of the gun (no armory
  entry, nothing in anyone's loadout), a copy of its round dealing a copy of its damage type with armour penetration 5.
- **It goes like W's launcher:** she switches to her primary or secondary (0.5 s), drops it, spends its rounds, or
  presses Z again. A stratagem or a grenade in her hands is not a switch (Yui). Her own weapon comes back; a support
  weapon she carried is replaced by a new one of its kind, refilled.
- **Cooldown 30 s from when the gun is gone** (W's).
- **Her shield while she holds it:** 3000 (Alva's shield pool, `kb_allies.h`), refilled to 3000 every 5 minutes, never
  above 3000. Once the gun is gone the shield **drains** (100 a second) until the next refill, so switching off doesn't
  punish her at once (Yui). A new life brings a full shield with her next gun.

### X: Reload and Destroy

- The gun in her hands refilled in full (magazine and spares), whatever gun it is.
- On her own gun also **+80 ergonomics and no recoil**: weapon handling is read when a weapon is built, so a new gun
  with them, full, takes the place of the one in her hands (a short put-away and draw), and keeps them until it goes.
- Cooldown 20 s.

### C: Ishbosheth (her EX)

- Readied by the key; **her next trigger pull** (any gun) sets it off.
- Every enemy within 30 degrees of her aim (each side) out to 40 m (the 12 nearest) takes a round dealing **6.36 times**
  her gun's round (636%); 16 rounds of her gun sweep the fan. Koleda's Full Speed cone, from her trigger.
- Cooldown 30 s from when it goes off.

### Passives

- **Lock & Load:** a magazine she empties reloads at once (no spare used; not her own gun, whose rounds are its whole
  supply), and her shots deal 21% more for 16 s.
- **Prefect Team Member Judgment:** her own gun fires 14% faster (on top of its 6000).
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

## Open (to check in play)

- Whether her gun fires from her backpack copy (the game may pair a backpack with its weapon by kind: the kit points
  each copy at the other, and logs what it found).
- Whether the HUD reads the backpack's rounds (the deposit manager's live amount).
