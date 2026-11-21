# Vector: a fire support (plan and first build)

Vector is the SMG Doll of Girls' Frontline 2: Exilium, a Burn support: she sets enemies burning, then makes everything
that burns hurt more. Her kit is on branch `vector-kit` as `mods/vector` (0.1.0, compiled, not yet played). Source for
her GFL2 kit: https://gfl2.help/en/characters/Vector .

Decided (2026-11-21):

- Three skills on keys as usual, her passive and her Confectance Index always on.
- **Fire does nothing to her at all** (total immunity), through the path Alva's EMS immunity uses.
- The Confectance Index stays.
- Solo or host. No character model is included.

## Her kit in GFL2

| Skill | In GFL2 |
|---|---|
| Dead End Meltdown | A Burn shot. On a target that already burns it also hits the enemies around it and applies Overburn. +2 Confectance Index. |
| Portent of Doom | A Burn shot that applies Smolder. +2 Confectance Index. |
| Searing Finale (ultimate) | A wide Burn blast that leaves Incineration tiles, gives every ally Accelerant and grants an Extra Command. |
| Perception Block (passive) | No harm from Burn tiles. Overburned targets take more Burn damage. A full Confectance Index is spent for an attack bonus. |

The source names Overburn, Smolder and Accelerant without defining them, so what each does here is this kit's reading.

## Her kit in Helldivers terms

All numbers are first defaults, each a setting in `vector.ini`. Default keys Z, X, C.

### Perception Block (passive)

- **Fireproof.** The fire statuses (fire, fire panic, heavy burning, the flamer's slow) never land on her, and fire's
  damage on her is cancelled. She can stand in her own flames.
- **Overburn:** an Overburned enemy takes `overburn_mult` (x1.3) fire damage, from anyone.

### Confectance Index (passive)

- Each skill she uses gives `conf_cast` (2) points, up to `conf_max` (6).
- At the maximum they are spent: her hits deal `conf_mult` (x1.25) for `conf_seconds` (12 s). Three skills fill it.

### Dead End Meltdown (Z)

- Cooldown `s1_cooldown` (20 s). For `s1_seconds` (10 s) her shots set what they hit on fire.
- A shot on an enemy that is **already burning** sets off a fire burst on it and Overburns it for `overburn_seconds`
  (8 s). One burst an enemy every `s1_burst_every` (2 s), `burst_rate` (4) a second in all.
- The burst is an explosion of the game's (`s1_burst` 26, the EAT-700's fire shrapnel: small, 5 m; 69 the G-13
  Incendiary Impact's and 409 the G-142 Pyrotech's are the alternatives).
- It rewards a fast weapon: an SMG keeps a pack burning and bursting; any incendiary weapon starts the burn for her.

### Portent of Doom (X)

- Cooldown `s2_cooldown` (15 s). The enemy under her crosshair (in sight) is set on fire and **Smolders** for
  `smolder_seconds` (12 s): fire damage on it is `smolder_mult` (x1.5), from anyone.

### Searing Finale (C)

- Cooldown `s3_cooldown` (90 s). Where she aims, `s3_shells` (3) napalm shells fall (the Orbital Napalm Barrage's
  shell): the first on the mark, the rest within `s3_spread` (8 m). They leave the ground burning.
- **Accelerant** for `accelerant_seconds` (15 s): fire damage on enemies is `accelerant_mult` (x1.3). In GFL2 it is a
  buff on every ally; solo it is folded into fire damage as a whole.
- **Extra Command:** her other two skills are ready again.

Smolder, Overburn and Accelerant multiply together: a Smoldering, Overburned enemy during Accelerant takes x2.5 fire.

## How it works

- **Setting on fire:** the game's own fire status, applied to the enemy (as the kits apply stuns).
- **What burns:** every status the game adds passes one function (the hook of Alva's EMS immunity). A fire status seen
  there, on anything and from anyone, marks that enemy as burning for `burn_seconds` (5 s); so does a tick of fire
  damage. A flamethrower's or a napalm strike's fire counts like her own.
- **Fire's damage** is told by its damage element, which is not known ahead: it is learned from the first damage over
  time on something a fire status just landed on, and logged (`fire_element` in the ini skips the learning). Until then
  fire's damage is neither boosted nor cancelled on her; its statuses are refused on her from the first moment.
- **Her own blasts** (the bursts, the shells' impact) do nothing to her, as with every kit.

## HUD

- Her three skills beside the stratagem list with their cooldowns, as the other kits.
- Bottom middle: the Confectance Index as pips (all lit while its boost runs), and what is on: MELTDOWN, ACCELERANT,
  and how many enemies are BURNING, SMOLDER and OVERBURN.
- A word over each Smoldering or Overburned enemy.
- No skill icons yet: the tiles show S1, S2, S3.

## Not known until played

- Whether a fire burst spawned as a bare explosion shows its flames and leaves fire (Alva's EMS bursts needed their
  shell fired to look right).
- Whether the napalm shell fired straight down behaves as the barrage's.
- Fire's damage element: if it comes as element 0, only its damage over time can be told from a bullet, and a direct
  flame hit (a Hulk's flamer) would still hurt her while nothing sets her alight.
- Her bursts and shells are not kept off allies: fire is friendly fire, as the game's own.
- Balance: every multiplier and cooldown.

## Later

- Skill icons, a mod icon.
- Fire kept off allies near her own bursts (the ally guard Alva has).
- Co-op, with the others.

## Playtest notes

- 0.1.1: Portent of Doom takes what the crosshair is on first (the body the aim ray meets, or the enemy a hit part belongs
  to), and only then the enemy nearest the crosshair. Before, a tall enemy could only be picked by aiming at its centre
  point, which sits in the empty space between its legs.
- Fire's damage element is 1 (learned in the first run).
- 0.1.7: her own burn. Tanks, the War Strider and gunships take no fire damage from the game's fire (150,000 build-up
  poured on them changed nothing; soldiers, berserkers and lieutenants burn normally). A kind that takes no fire damage
  within 2 s of her fire is remembered for the session, and her fire on it burns by her hand instead: own_burn_damage (12,
  half a soldier's 25) health every 0.25 s for own_burn_seconds (8), straight off its health past armour ("it cooks the
  crew"), multiplied by Smolder, Overburn and Accelerant. At 1 health a fire burst is set on it and that hit kills it the
  game's way. Fabricators and dropships are left out.
