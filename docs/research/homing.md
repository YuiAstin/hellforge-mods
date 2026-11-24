# Homing projectiles (research, 2026-11-24)

Goal: shots that curve into targets. The Destroyer kit wants her projectiles to home, and the Rocket Sentry's Veteran
level wanted homing rockets. Game build 25480438.

**What works:** fire a projectile that already has the game's own homing (a seeking missile, the P-33 Missile Pistol's),
and hand it a lock-on target yourself. Speeding, slowing and steering an ordinary shot with HellForge's `homing` rule
never worked in game.

## What works: a game-guided missile with a target handed to it

Proven in game with Lock-on & Stim Test 0.1.1–0.1.3 (`mods/lock-stim-test`, 2026-11-07):
- solo, one Rocket Sentry, a mixed crowd;
- 17 missiles, 32 target hand-offs, none left without a target.

The full recipe, with the record layouts and code pins, is in [lock-on.md](lock-on.md). In short:

1. **Load the missile's package** (`load_package(mod, 0x64C37A90C0D906DE)`, `pistol_smart_missile`). Wait until it's
   resident, or the missile shows as the purple "?" placeholder.
2. **Make the weapon fire the missile entity.** Set ProjectileWeapon `projectile_entity` to the P-33's guided missile,
   `0xA1470A4DAB237264`. A sentry called in afterwards fires it. The missile still carries the weapon's own shot, so its
   damage and blast stay the weapon's.
3. **The missile's targeting mode must be 2** (follow an entity). The P-33 missile's SeekingMissile record already is.
4. **Hand it a target from the game thread, every tick.** A missile launched with no lock-on has target `0x7FFF` (none)
   and flies straight. Find it in the seeking-missile manager `[game+0x3326DF0]` and write its state:
   - `+0x00` = the target's network id, from `game.dll+0xFD9AF0`;
   - `+0x04` = `0x10`, the target's default aim point;
   - `+0x34` = `1`, guidance on.

   The game re-reads `+0x00` every update, so changing the target mid-flight works.
5. **Pick targets.** The sentry's own target is readable (Behavior record `+0x18`). Otherwise use the nearest living
   enemy within 150 m and 75° of the launch direction.
6. **Re-target only when the target dies.** The game clears `+0x00` itself for the final approach, and forcing guidance
   back on then fights it.

From there the game's own missile guidance does the rest: turn rates, speed ramp, proximity release and lifetime.

Why the target has to be handed over: the game's lock-on is screen-space against the local camera, so a sentry has no
lock and its missile gets none (lock-on.md, section 3).

**Lock-on & Stim Test 0.2.0** makes the GL-21 Grenade Launcher fire the P-33 missile too. Each missile is handed the
nearest living enemy within `launcher_cone` of where it was aimed, and carries the launcher's own grenade. This is the
route for the Destroyer's homing grenade launchers. Its in-game result is not recorded yet.

**Not tested:**
- other missiles (W.A.S.P., Spear, Commando, Warrant);
- several sentries at once;
- a teammate's sentry (the hand-off runs on the host);
- the P-33 pistol's own guided mode while the edit is on.

## What didn't work: HellForge's `homing` rule on ordinary shots

Rocket Sentry Test 0.3.0–0.3.2 (`mods/rocket-test`) put a HellForge homing rule (`HfApi.homing`, SDK 11) on the Rocket
Sentry's warhead rocket. It never homed usefully. Tuning didn't fix it:
- turn rates of 120 and 540°/s;
- keep the first target;
- arming distance, cone and aim height;
- slowing the rocket to 60 m/s.

The Veterancy System's Rocket Sentry shipped without it.

Why, from HellForge's source (`native/mod-loader/actions.c`, YuiAstin/hellforge fork at 2d2e8b47):
- **It finds targets late.** The enemy list is built a piece at a time, and only while a homing shot is already flying.
  Each update reads 512 health-record slots and refreshes 12 positions, and ignores positions older than 0.5 s. So a
  shot gets its first target 0.1–1 s after it's fired: 10–100 m at 100 m/s, past most targets.
- **It steers crudely.** Each update it turns the shot's velocity (flight record `+0x0C`) toward the target's current
  position by at most `turn_rate` × dt, keeping its speed. It doesn't lead the target, and has no speed ramp or
  final-approach logic like a seeking missile's.
- **Limits:**
  - only the local player's own shots (the creditor must be this machine's peer);
  - solo unless HF_SHOT_MULTIPLAYER;
  - at most 16 shots at once, followed for at most 10 s;
  - one homing rule per projectile type across all mods.
- **Still open:** whether a sentry's shots count as the player's own, since the rule takes the player's own shots only.

HD2Runtime's `hd2.projectiles.homing` is the same idea, ported to HellForge onto `HfApi.homing`. Its doc says it's
offline-proven but not live-tested. On its own, it also starts its enemy scan while the player holds a configured weapon,
which HellForge's version doesn't.

## For the Destroyer

- Use the seeking-missile route, not `homing`.
- Lock-on & Stim Test 0.2.0's GL-21 hand-off is the test to run first. It's the closest thing to "any grenade launcher
  in her hands homes".
- Targets come from the kit's own enemy list, with the kit base's enemy rule or the faction test so SEAF and other allies
  are never picked.
- This all runs on the game thread. The missile carries the launcher's grenade, so the zero-arming-distance change
  applies to that grenade's row.
