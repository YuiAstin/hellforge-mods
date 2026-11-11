# Changelog

## 2026-11-11: Veterancy System 0.19.1

### Fixed
- **Rocket Sentry Last Stand no longer hits allies.** It only drops on units on the other side from the sentry (the game's own faction check, the one its lock-on uses), so SEAF soldiers and other friendly units are never targets.
- **Last Stand only fires when the sentry is destroyed.** When its time runs out it now just goes.
- **No more ring or blast with no enemies near.** With no enemy within range, nothing drops and there is no blast where the sentry stood.
- **The release carries the default settings file.** The 0.19.0 upload on Nexus carried a test one (Rocket Sentry Elite at 15 kills instead of 150, Maxigun at 10 / 20, Mortar at 10 / 25, among others).

Released on Nexus (18607) and AyakaMods (5924).

## 2026-11-02: Veterancy System 0.19.0

This release also brings 0.17.1 and 0.18.0 below (the Maxigun, banner settings, the settings-file fix and the mod icon), which were not released on their own.

### Added
- **A/MLS-4X Rocket Sentry** ranks up from its own kills.
  - **Veteran (30 kills):**
    - its rockets carry the P-33 Missile Pistol's warhead: 1,000 direct (AP 5) and a 300 blast with a 12 m shockwave, instead of 525 and a 150 blast;
    - a 32-rocket magazine (16 before).
  - **Elite (150 kills):**
    - a 48-rocket magazine;
    - Last Stand: when one of your Rocket Sentries is destroyed or its time runs out, the rockets it has left come down on the enemies within 50 m. They're split evenly (10 left and 5 enemies: 2 each), each dropped from 100 m straight above its target. Then a P-33 blast goes off where it stood. With no enemies near, the rockets go out in a ring around it instead.
  - Its rank reaches the next Rocket Sentry called in.
  - The rocket's own data is shared with a manned turret and an enemy rocket soldier, so the warhead is the mod's own copy of the rocket and those stay vanilla.
  - All of it is in field-promotions.ini (rs_ settings).
- Rank badge and promotion banners for the Rocket Sentry.
- Credit: finding the enemies uses Helldiver Survival Rework's entity and position research (MIT; THIRD_PARTY_NOTICES.md).

### Notes
- Last Stand counts a sentry's rockets by its own blasts (and by the shot stats while it is your only Rocket Sentry out). A sentry destroyed soon after it lands still holds nearly its whole magazine, so it brings down a big barrage; that is intended for Elite.
- Last Stand's enemy finding reads the game's memory for build 25480438 only; on another build it falls back to the ring.
- Guided rockets were tried and dropped for now. HellForge's homing only starts looking for enemies once a homing shot is in the air, so a fast rocket finds its target only after flying past it (see the Rocket Sentry Test mod's notes).
- `mods/maxigun-test` and `mods/rocket-test` are research builds kept in this repo for their notes and logging. They are not released.

## 2026-11-01: Veterancy System 0.18.0

### Added
- **M-1000 Maxigun** ranks up from its own kills.
  - **Veteran (60 kills):**
    - a fire-rate choice in its weapon menu, 2,000 or 4,500 rpm;
    - 0.2 s spin-up and 1.5 s spin-down;
    - 40% less recoil;
    - firing on the move;
    - a 3,000-round backpack.
  - **Elite (200 kills):**
    - 6,000 rpm joins the menu;
    - a 6,000-round backpack;
    - Old Painless: after 2 s of continuous fire, its rounds pierce heavy armour (AP 4) until it stops.
  - A rank reaches the next Maxigun called in, so each rank earned makes its call-in ready at once.
  - All of it is in field-promotions.ini (mx_ settings).
- Rank badge for the Maxigun on the stratagem list, and its own promotion banners. They stay up for 7 s and carry a pulsing green box under the banner: "FRESH MAXIGUN READY - CALL IT IN" when the call-in was refunded, or "CALL IN A FRESH MAXIGUN" when it wasn't.
- Banner settings on the MODS tab (Mod Options Menu): Promotion Banners on/off, and Banner Time (2-10 s; the Maxigun's stays 2 s longer). Also in field-promotions.ini as hud_toasts and the new hud_toast_seconds.

### Notes
- Backpacks above 1,023 rounds widen the game's backpack ammo count to 14 bits, as HellForge's machine-gun-10k example does. Everyone in a lobby needs the same Veterancy System version, which HellForge's mod sync already requires.
- The call-in refund writes your own stratagem cooldown, using HD2Runtime's research. It needs HellForge mod SDK 13 or later, and it checks the game code first; if anything doesn't match, there is no refund and the mod log says why.

## 2026-11-01: Veterancy System 0.17.1

### Fixed
- Settings in field-promotions.ini were never used: the mod tried to create and update the file during a session, when HellForge keeps mod folders read-only. The release now includes a complete field-promotions.ini with every setting at its default; edit it with the game closed.
- The Machine Gun Sentry's settings no longer list the old damage shield (mg_shield, mg_shield_regen), which has not been used since the hit-counting shield replaced it.

### Added
- Mod icon on HellForge's Mods page: the Elite rank badge.
- The mod log says where the settings were read from, or that the built-in defaults are in use.

## 2026-11-01: Leva 0.4.2 · Faelynn 0.3.4 · Lenna 0.2.7

### Fixed
- The .ini settings file was never created. HellForge keeps mod folders read-only while the game runs, so the mods could not write it. Each release now includes a complete .ini with every setting at its default; edit it with the game closed.
- Settings are read correctly when the HellForge path contains non-English characters.

### Added
- Mod icons on HellForge's Mods page (Lenna's is her portrait).
- The mod log says where the settings were read from, or that the built-in defaults are in use.

## 2026-11-01: Leva 0.4.0
- First release: Rational Suppression, Orderly Disruption, Quantum Calculation and Superconduct Blitz; skill icons in the HUD.
