# Changelog

## (release date): Voymastina 0.4.1 · Lemuen 0.1.5

### Voymastina 0.4.1
- A free Sinbreaker for players without the Exo Experts warbond: the same kit on the EXO-49 Emancipator (or the EXO-45 Patriot, sin_free_base 1). With it, pressing Pile Bunker's key again during its cooldown fires the follow-up in place of the shield bash. sin_fallback 2 gives the free one even with the warbond; 0 gives none.
- Several character kits can be installed together: with two or more, Mod Options Menu → GFL2 Kits HUD → Active Character picks the one that acts, from the next mission start. Kits that do not know the roster yet (Lenna, Leva and Faelynn as released) still act alongside: run them on their own for now.
- Built on the shared kit base (no change in how she plays).

### Lemuen 0.1.5
- First release: Lemuen's sniper kit from Arknights.
  - Persecution: elite enemies she keeps in her sights for a few seconds are marked (up to 5; a new mark replaces the oldest) and take more damage from her. Marked enemies carry a marker on screen, with their distance.
  - Modus Extraditio: her shots deal more damage a while after she lands.
  - Salve Etiam (S1): her sniper or marksman rifle's shots deal x3.10 until she reloads.
  - Honesta Missio (S2): in first person, the enemy held in her scope takes rising damage from her next shot (x1.8 to x4.25 over 3.5 s), until she reloads; locks out to 600 m.
  - Memento Mori (S3): an Orbital 120mm shell on each marked enemy.
  - Damage numbers over her hits, by Arknights' rule: only hits her on-hit multipliers raised by half or more (Honesta Missio, the marks).
- Built on the shared kit base; joins the character roster.

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
