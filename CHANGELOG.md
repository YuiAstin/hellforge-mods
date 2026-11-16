# Changelog

## 2026-11-17: Koleda 0.4.1 (first release)

- First release: Koleda's kit from Girls' Frontline 2 (AK-12, a Vanguard) for your Helldiver. Solo or host; no character model included.
- The Sinveiler: Spectre (C) makes the M-103 Supply FRV from her loadout her Sinveiler (without one: a Patriot exosuit). It takes 80% of the damage aimed at her, is never destroyed by a hit, heals when hit, is kept steady under fire and set upright when it rolls over.
- Gears: D by default; her skills set N, S or S+ for 15 s; P while it stands still. Its damage and what it takes follow its gear.
- Upshift: the Sinveiler's actions give her stacks, +15% damage each (up to 5); at 5 she takes 30% less damage. Post-Move Support: while it stands, her damage and its are x1.3.
- On foot: Snow Wolf's Eye (Z) fires heavy rounds at the enemy in her crosshair, stronger per Upshift stack, with Turbo Boost (x1.45 for 6 s), and sets S+. Tactical Routebook (X) gives Kinetic Recovery (x1.25 for 10 s) and sets S. With the Sinveiler standing, C gets her in (Let's Go For a Spin!): nothing can hurt her inside.
- In the Sinveiler her keys are its skills: Let's Go Full Speed! (Z: 6 s without damage and harder rams), Let's Go for a Drift! (X: waves of EMS bursts out to 20 m) and The Winner (C: it is restored, gear N).
- At speed it fires HE shells at the enemies ahead and sprays tracers, and its rams hit harder the faster it goes. In S and S+ it fires HE shells all around it every few seconds (Kinetic Release).
- HUD: her skills beside the stratagem list with icons and cooldowns (they change in the Sinveiler), her passive, Upshift, the Sinveiler's gear and health, her buffs, the low-stamina warning, key bindings in the game's binding pages and every number on the MODS tab.
- Needs HellForge 0.4 or later; the HUD needs Bingus Shared Loader and HD2Runtime. The FRV Sinveiler needs the M-103 Supply FRV in her loadout.

## 2026-11-16: Ascalon 0.7.0 (first release) · Voymastina 0.4.3 · Faelynn 0.4.1 · Lenna 0.3.1

### Ascalon 0.7.0
- First release: Ascalon's kit from Arknights for your Helldiver, an ambusher who fights from hiding. Solo or host; no character model included.
- Hidden until she makes noise: enemies see her far less and forget her, half the hits on her miss, and her body and gear shimmer out of sight (the look needs Bingus Shared Loader and HD2Runtime). Firing a gun reveals her; melee does not. With a backpack on or a support weapon in hand she is not hidden.
- Ambusher: every hit of hers also strikes every enemy within 15 m.
- Mortal Interrogation: her hits mark enemies (3 stacks, 25 s) with a bleed every second, 10% of the marking hit a stack, that goes through armour, and a slow.
- Pursuit (by itself, 3 charges): 210% damage, lands twice, slows.
- Endowment (X): 35 s of +130% damage, enemies around her slowed, kills spread marks.
- Descent (C): she steps out of hiding, no hit on her lands, +50% damage, a 25 m reach and faster swings; each kill extends it.
- Parry: with a melee weapon in hand, a right-click just before a hit nulls it, puts her behind the attacker, stuns it and counters. No clash sound is included; 16-bit .wav files named parry_clash_01.wav to parry_clash_09.wav in the mod's sounds folder are played if you add your own.
- Dash (Z): through the enemies in the way, striking them; the way she moves, or the way she aims when she stands still. Where she lands is checked with the game's own fit test (the Warp Pack's).
- Penumbral Image: her melee swings are faster while she is hidden.
- HUD: her skills beside the stratagem list with icons and cooldowns, damage numbers for her skill-raised hits, key bindings in the game's binding pages and every number on the MODS tab.

### Voymastina 0.4.3 · Faelynn 0.4.1 · Lenna 0.3.1
- Their teleports land better (Voymastina's dashes and leaps, Faelynn's Loyal Dash, Lenna's Leaping Pursuit): whether she has room at a spot is now asked of the game's own fit test (the Warp Pack's) instead of the mod's own measurements.
- Fixed: some maps have invisible level sheets about 2 m over the ground; a teleport could set her on one. They are no longer ground or walls.
- Voymastina and Faelynn: an enemy standing beside the landing spot no longer counts as a wall, and the ground under foliage is no longer mistaken for buried ground.
- Lenna: leaves, canopies and enemies' bodies are no longer ground to land on; with no room at a spot she lands closer.
- Voymastina: less work each frame with many enemies about (her list of enemies is pruned of the dead and the gone).

## 2026-11-13: Exusiai 0.3.1
- The summoned guns are now called Shooting Mode, her skill's own name: on the HUD, the MODS tab, the settings file's comments and the logs. The key setting is still key_guns, so your exusiai.ini keeps working.
- Charging Mode counts every gun: weapons that run on heat or fire a beam (the Sickle, the Talon) and the Grenade Pistol charged nothing before, because only a dropping magazine was counted. A gun with no round to copy fires the guns' own round for its free bursts.
- Shooting Mode's pair answer explosions: a blast from the gun in her hands (the Grenade Pistol's, say) now calls them onto whoever it hurt.
- Overloading Mode no longer gives up on walkers: a gun that is dealing damage stays on its target even while the target's health number does not move (War Striders). Thanks to ElectroDasher for the reports and logs.

## 2026-11-12: Exusiai 0.3.0 (first release)
- Exusiai's kit from Arknights for your Helldiver. Solo or host; no character model included.
- Charging Mode (passive): every 4th burst of her gun sets off 3 free bursts.
- The summoned guns (S2): a pair hover at her shoulders and fire her primary's rounds at what she hits.
- Overloading Mode (S3): four more guns for 15 s, each with its own target and rounds: a shotgun for the nearest, heavy-penetration rounds for the most armoured, staggering rounds for whoever last hit her, quick rounds for the weakest. They only pick what they have a clear shot at.
- Fast Cartridge (faster-firing guns) and Angel's Blessings (more health and damage).
- HUD (Bingus Shared Loader and HD2Runtime): her skills beside the stratagem list with icons and cooldowns, the guns' models and their firing sounds.

## 2026-11-10: Commando Plus 0.7.0 (first release)
- Commando missions made a little more forgiving; every option is a setting in commando-plus.ini. Host or solo.
- No mission timer in Commando, and one extra use of Call In Destroyer (0 to 3).
- Outpost caches: every enemy base you clear (bug holes, fabricators, Illuminate spawners) banks ammo boxes, 4 for a small base and 8 for a large one. Two stratagems on your list call them in: the Outpost Cache drops a pod of 8 boxes, the Small Outpost Cache a pod of the rest. In Commando they work while the Super Destroyer is overhead, so calling it in is cashing in.
- The Super Destroyer's window grows with the bases cleared: the game's 60 s plus 15 s a base, at most 3 minutes, after the drop-in and every call. Past the game's 60 s a banner at the top of the screen counts the time bought down (needs Bingus Shared Loader and HD2Runtime).
- every_mission 1 applies the options to every mission, not only Commando.

## 2026-11-08: Voymastina 0.4.2 · Executor 0.3.0 (first release) · Lost Contact 0.4.1 · Lemuen 0.1.11

### Voymastina 0.4.2
- Fixed: players with the Exo Experts warbond got the free Sinbreaker (on the EXO-49 Emancipator) instead of the one on the EXO-55 Breakthrough. The 0.4.1 download's voymastina.ini had a test setting (sin_fallback = 2); it is back to 1. If you keep your 0.4.1 ini, set sin_fallback = 1 yourself.

### Lost Contact 0.4.1
- Patrols setting out now walk past you instead of onto you: when a patrol's leader is aimed so that its route would pass within pass_by m of you (50 by default), it is aimed to pass you at pass_by m instead, as it sets out and whenever the game aims it again. Patrols already sent at you still walk between their own outposts.
- New setting pass_by (0 off, or 20 to 150 m): lost-contact.ini, or Pass By on the MODS tab.

### Lemuen 0.1.11
- Honesta Missio holds the first enemy under her crosshair: an enemy behind it no longer takes the lock, and another enemy takes it only after 0.3 s in her sights, so the build-up no longer resets while she aims.
- Big enemies can be held and marked wherever she aims on them: a Factory Strider's hull, a War Strider's legs and armour.
- War Striders are her foes from the moment they spawn, so Persecution marks them and Memento Mori hits them without casting Honesta Missio first.

### Executor 0.3.0
- First release: Executor the Ex Foedere's kit from Arknights, a Laterano Guard with a shotgun.
- Every enemy his shot hits heals him half his health (up to 3 a shot) and mends his injured limbs.
- The Chosen One fires free extra rounds. Each shot also fires a round of his gun at every enemy in a 15 m square in front of him (25 m during Damnatus Ex Foedere). Empathy by Shotgun adds a round to each skill for every other Laterano kit installed.
- Skills last a number of shots: Testament Exacted (more damage and armour penetration), Close Combat (more damage, 80% less damage taken, a chance to dodge a hit and get a round back), Damnatus Ex Foedere (rising damage, triple heals, then a blast on every enemy it hit).
- HUD: his skill icons and damage numbers. Keys in executor.ini (default Z, X, C); numbers on the MODS tab (Mod Options Menu). Needs Bingus Shared Loader and HD2Runtime for the HUD. Installs beside the other character kits (pick one on the MODS tab).

## 2026-11-07: Alva 0.2.4
- Her HUD now shows the low-stamina warning: below 25% stamina, "T-DOLL CATASTROPHIC FAILURE IMMINENT" flashes red above her status bar, faster as it empties, with a stamina bar under it.
- The exhaustion Hellbomb now kills her even behind Brumal Barrier: her own shield gives way when it goes off. Allies' shields still hold.

## 2026-11-06: Alva 0.2.3 (first release)
- Alva's kit from Girls' Frontline 2: a support who shields every ally around her and freezes enemies.
- Brumal Barrier: every skill (and every 20 s) gives every ally in range (Helldivers, sentries, emplacements, dogs, exosuits, SEAF, the FRV, the Pelican) a 500 shield that stacks with no cap.
- Freezing Touch and chill: her hits build Confectance and chill enemies until they freeze. A frozen enemy takes no damage except from explosions (not EMS): that hit shatters it for 8x damage, once, and thaws it. Snow fields (Frosted Echo's nitrogen field, and a 10 m field where each burst lands) freeze anyone standing in them, faster where fields overlap.
- Snow Wolf's Heart shoots any enemy that hits an ally near her; Frosted Echo drops EMS mortar bursts and a nitrogen field where she aims; Nix Requiem fires heavy rounds at her target and freezes it at 5 Confectance.
- HUD: her skill icons, cyan !damage! numbers on a shatter, and her passive's icon over every frozen enemy ready to shatter. Keys in alva.ini; numbers on the MODS tab (Mod Options Menu). Needs Bingus Shared Loader and HD2Runtime for the HUD.

## 2026-11-06: Lost Contact 0.3.0
- The MODS tab now works with the Vanilla Plus Megapack's Mod Options Menu: ESC > OPTIONS > MODS > Lost Contact has Break contact and Patrols rerouted (on, off or observe), Character kits can limit, what runs now and why, and the six numbers of lost-contact.ini (Lose After, Range, Seek Radius, Keep Away, Patrol Range, Resend). The rows need HD2Runtime and Bingus Shared Loader (Lost Contact's own data registers them), unless giblett's Vanilla Plus Megapack build is installed, which offers the tab without them.
- The tab's choices are saved in %LOCALAPPDATA%\HellForgeMods\lost-contact.ini and kept across restarts; the rows follow Ctrl+F9 / Ctrl+F10 and a kit limiting them. Without the menu, lost-contact.ini and the keys work as before.

## 2026-11-05: Lost Contact 0.2.1
- The MODS tab needs a menu that publishes HD2_MOD_OPTIONS_v1 (giblett's Vanilla Plus Megapack build). The public Vanilla Plus Megapack (0.2.0) does not offer it yet, so with it there is no Lost Contact tab: use lost-contact.ini, or Ctrl+F9 / Ctrl+F10 in a mission. The log now says so once, about 30 s after start, naming what it looked for and whether the public pack is installed and loaded.
- A menu that is found but refused (another version, table size, or functions outside any loaded module) is named in the log instead of being skipped in silence, and the line for added rows names the menu used.
- Rows the menu refuses are named in the log and asked for again up to 4 times, 10 s apart (0.2.0 asked once). A menu set up again while the game runs gets the rows again.

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
