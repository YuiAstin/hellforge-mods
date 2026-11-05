# Changelog

## 2026-11-05: Voymastina 0.4.1 · Lemuen 0.1.6 · Lenna 0.3.0 · Leva 0.6.0 · Faelynn 0.4.0 · Lost Contact 0.2.0

### All five character kits
- Built on one shared kit base, and they install together: Mod Options Menu → GFL2 Kits HUD → Active Character picks the one that acts, from the next mission start. The others stand by.
- With many code mods installed, HellForge gives at most 16 of them a turn each game update. Every kit takes one, so turn off mods you are not using if the MODS tab or a kit goes missing.

### Voymastina 0.4.1
- A free Sinbreaker for players without the Exo Experts warbond: the same kit on the EXO-49 Emancipator (or the EXO-45 Patriot, sin_free_base 1). With it, pressing Pile Bunker's key again during its cooldown fires the follow-up in place of the shield bash. sin_fallback 2 gives the free one even with the warbond; 0 gives none.

### Lemuen 0.1.6
- First release: Lemuen's sniper kit from Arknights.
  - Persecution: elite enemies she keeps in her sights for a few seconds are marked, and in first person the enemy under her crosshair is marked at once. Up to 5 marks; a new one replaces the oldest. Marks last until replaced or killed. A rider and its vehicle (a strider's walker and pilot) share one mark, and enemy turrets (base cannons, AA guns) can be marked. Marked enemies take more damage from her and carry a marker on screen with their distance.
  - Marks and Honesta Missio's lock need a clear line of sight: no marking through walls or terrain (line_of_sight 0 turns it off).
  - Modus Extraditio: her shots deal more damage a while after she lands.
  - Salve Etiam (S1): her sniper or marksman rifle's shots deal x3.10 until she reloads. Also works with Scope of Authority's R-40 Sightliner and LAS-44 Nova.
  - Honesta Missio (S2): in first person, the enemy held in her scope takes rising damage from her next shot (x1.8 to x4.25 over 3.5 s), until she reloads; locks out to 600 m.
  - Memento Mori (S3): an Orbital 120mm shell on each marked enemy.
  - Damage numbers over her hits, by Arknights' rule: only hits her on-hit multipliers raised by half or more (Honesta Missio, the marks).
  - Skill icons on the HUD.
  - With Lost Contact installed, only its patrol redirect runs while she is the active kit: enemies that saw her keep hunting her.

### Lenna 0.3.0 · Faelynn 0.4.0
- Her dash goes the way she is moving, so kiting carries her away from what she shoots at; standing still or just hit, it goes where she faces (dash_move 0: always where she faces).

### Leva 0.6.0
- Her lines and fields follow the game camera, where the reticle points, with no head sway (aim_camera 0: her head's aim, averaged).
- The lockdown's burst is requested from the game directly, so its lightning starts at the burst instead of running to and from her; the burst has its own arc again. Explosion request adapted from Beelzebub's Strat Pack (with permission); the explosion kick guard from HadePasianax's Explosives Library (MIT); the camera read after Apache - Rise Above (with permission).

### Lost Contact 0.2.0
- First release.
  - Break contact: an enemy after you that, with its squad, has not sensed you for 3 s loses you and searches where it last saw you, instead of tracking you across the map. Seeing you again brings it back. Works against bots and Illuminate; bugs (Stalkers) still find you.
  - Patrols walk between their own outposts instead of being sent straight at you: a patrol the game aims at you is sent to an outpost at least 100 m from you; one that finds you fights as usual.
  - Each on, observe (counts in the log only) or off: lost-contact.ini, or Ctrl+F9 / Ctrl+F10 in a mission. Host or solo.
  - A character kit can limit them while it is the active kit: Lemuen allows patrols only.
  - On the escape menu's MODS tab (Vanilla Plus Megapack): both modes, whether a kit may limit them, and two Now rows showing what runs and, if something is off, why. Choices there are saved and kept.

## 2026-11-03: Voymastina 0.4.0
- First release: the Sinbreaker (her own exosuit, a copy of the EXO-55 Breakthrough, granted every mission outside the loadout; needs the Exo Experts warbond) with her GFL2 kit (Sirius Fall, Eye of the White Mastiff, Pile Bunker), the Pile Bunker Hellbomb follow-up on her shield bash, her AK-15 kit on foot (Rage, Eye with overload, Blood-Curdling Howl) with +50 ergonomics, half reload time and 30% less recoil on her guns, Mastiff's Pledge on her and the suit, and a HUD that follows her kit.

## 2026-11-02: Leva 0.5.4 · Faelynn 0.3.5 · Lenna 0.2.10

### Added
- Skills on cooldown now show with the stratagem list closed, one tile per skill, placed clear of Reinforce and the stratagem cooldowns. Mod Options Menu → GFL2 Kits HUD → "<Kit>: Always Show Cooldowns" turns it off.
- In-game key rebinding on the MODS tab through the Mod Bindings Menu (Vanilla Plus Megapack). Keys start unbound there; the .ini keys keep working.

### Fixed
- Shots from sentries and guard dogs no longer count as hers. A hit is hers when she, or a weapon she holds or carries, dealt it.

## Leva 0.5.3 · Lenna 0.2.9 (published on HellForge; not recorded here at the time)

- Leva: the lockdown bursts as an explosion of her own (a copy of the GL-52 burst), so rs_damage scales its blast in the game's data. Wise Support's chain hits inside a lockdown are kept to rs_damage, and stray arcs inside it are cut.
- Lenna: her arcs set off an explosion of her own, the GL-52 burst's look and sound with the G-31 Arc grenade's damage, at most arc_rate a second. Her own arcs no longer hit her.

## 2026-11-01: Leva 0.4.2 · Faelynn 0.3.4 · Lenna 0.2.7

### Fixed
- The .ini settings file was never created. HellForge keeps mod folders read-only while the game runs, so the mods could not write it. Each release now includes a complete .ini with every setting at its default; edit it with the game closed.
- Settings are read correctly when the HellForge path contains non-English characters.

### Added
- Mod icons on HellForge's Mods page (Lenna's is her portrait).
- The mod log says where the settings were read from, or that the built-in defaults are in use.

## 2026-11-01: Leva 0.4.0
- First release: Rational Suppression, Orderly Disruption, Quantum Calculation and Superconduct Blitz; skill icons in the HUD.
