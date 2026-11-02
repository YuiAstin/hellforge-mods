# Changelog

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
