# Hakumen: two supers and an Astral (plan)

Hakumen is the white-armoured swordsman of BlazBlue, a character built on counters. This kit takes only his super
moves: his two Distortion Drives, **Shippu** and **Yukikaze**, and his Astral Heat, **Akumetsu**. A full kit would need
a sword moveset and its animations, so there is none: he keeps the Helldiver's own guns and movement, and his three keys
are the supers. Nothing here is built yet.

Scope decided (2026-11-17):

- Three keys, plain cooldowns. **No Magatama gauge.**
- **No passive**, and no body line (no health or stamina change).
- Akumetsu deals **5000 damage**, not an instant kill (killing an enemy that still has health risks a crash), to
  **every enemy in a radius around him**.
- **Explosions can be parried**, like any other hit.
- Solo or host, like the other kits.
- No character model is included.

## His moves, in short

| Move | In BlazBlue |
|---|---|
| Shippu (Distortion Drive) | A charged overhead slash that sends a wave forward. Held longer, it hits harder. |
| Yukikaze (Distortion Drive) | A counter stance. Struck during it, he answers with a dash slash through the attacker. |
| Akumetsu (Astral Heat) | A counter stance. Struck during it, time stops, the screen is painted over in black brush strokes, and the opponent is cut down as time resumes. |

## His kit in Helldivers terms

All numbers are first defaults, each a setting in `hakumen.ini`. Default keys Z, X, C.

### Shippu (Z)

- Hold the key to charge, up to `shippu_charge` (2 s); release to send the wave.
- The wave runs along the ground ahead of him in a line of `shippu_range` (40 m), `shippu_width` (4 m) wide, as a row of
  blasts stepping outward. Each enemy is hit once.
- Damage from `shippu_min` (400) at a tap to `shippu_max` (1500) at full charge.
- Cooldown `shippu_cooldown` (20 s), from release.
- Needs no animation. It never hurts him or his allies.

### Yukikaze (X)

- A counter window of `yukikaze_window` (1.5 s).
- A hit on him inside the window is cancelled. The attacker takes `yukikaze_damage` (800), and so does every enemy in a
  short line behind it (`yukikaze_line`, 8 m).
- If nothing hits him, the move whiffs. Cooldown `yukikaze_cooldown` (15 s) either way.
- **No dash in the first build.** In the source he dashes through the attacker. Moving the player is the riskiest part
  of this kit, so it is a later step (`yukikaze_dash` 0).

### Akumetsu (C)

1. **Stance.** A counter window of `akumetsu_window` (1.5 s). If nothing hits him it whiffs and goes on cooldown.
2. **Time stop.** On a hit: the hit is cancelled and, for `akumetsu_stop` (2.5 s), every enemy within `akumetsu_range`
   (60 m) is held in place and nothing can hurt him.
3. **Brush strokes.** Black ink strokes sweep across the screen one after another over about a second, and stay until
   the stop ends. The strokes are our own art.
4. **The cut.** The strokes clear, time resumes, and every enemy within `akumetsu_radius` (25 m) of him takes
   `akumetsu_damage` (5000) on that frame.

- Cooldown `akumetsu_cooldown` (120 s).
- Target: **a radius around him**, not the attacker alone (the source is a duel; Helldivers fights are crowds). The
  attacker only triggers the move: if it stands outside the radius it is not cut.

## How the time stop works

The game is not paused: a true pause would stop him and the camera too. Time keeps running and everything that is not
him is stopped, one thing at a time:

- **Enemies:** their animation switched off (the pose they are in stays) and each held where it stands: see "Probed"
  below.
- **Their hits:** every hit on him is cancelled for the duration.
- **The payoff:** the 5000 lands as time resumes.

Known gaps:

- Enemies without a navigation record (some flyers, turrets, an enemy mid-leap) keep their pose but are not held in place.
- A held enemy can still turn on the spot, and nothing yet stops it firing.
- Projectiles already in flight and stratagem beams keep moving. His immunity makes this harmless to him, not to allies.
- The game's sound keeps playing.
- His own pose is whatever the game gives him: there is no slash animation.

Probed (Time Stop Probe, branch `timestop-probe`, 2026-11-17 and 18):

- **A pose freeze works.** The game's Lua can switch an enemy's animation state machine off and on again
  (`Unit.disable_animation_state_machine`): it keeps the pose it was in. Every enemy's unit is found from its unit id (a
  Lua unit value is the id * 4 + 1), crowds of 40 and more included.
- **They must also be held in place**, or they slide on in their frozen pose (one went 20 m in 5 s). Pointing each one's
  path at its own spot, zeroing its velocity every frame and putting it back when it drifts (the warp setter of
  Koleda's pull) holds nearly all of them within 0.25 m. Enemies without a navigation record are not held.
- **The stun is not needed**: the pose freeze with the hold did as well without it, and without the stun's own animation.
- **After the stop the bots stand a moment as if restarting.** Nothing switches their AI off; it is a side effect, and
  it looks natural on them, so it is kept: it is the beat in which the cut lands. Not yet seen on Terminids. Probe 0.3.0
  logs what each enemy's AI held when it resumed (its target, whether it sensed the player) and how long it took to move.

So Akumetsu's stop is: the animation off, held in place, no stun. Still to probe: whether the game has a time scale that
could slow everything but him (the names are logged, none called).

## What counts as a parry

For both counters, a "hit on him" is any enemy damage event aimed at him inside the window: melee, bullets, and by
explosions too (decided: explosions are parryable). `parry_blasts` (1) is the setting; with 0 explosions and fire go
through as normal damage and do not trigger the counter. When the hit has no living attacker (a mine, a fire patch),
Yukikaze cancels the hit and does nothing further; Akumetsu runs as usual, since it cuts by radius.

## HUD

- His three skills beside the stratagem list, with cooldowns, like the other kits.
- A stance indicator while a counter window is open, and a charge bar for Shippu.
- The brush strokes: full-screen images drawn in sequence by the HUD mod.

## Open

- Whether the Yukikaze dash is wanted at all once the counter works without it.
- Akumetsu's radius (25 m is a first guess) and whether an attacker outside it should still be cut.

## Build order

1. Probe on its own branch: one key holds every enemy within range for a few seconds; log what animation-speed and
   time-scale reads are found.
2. Yukikaze without the dash (the counter window and the hit cancel are shared with Akumetsu).
3. Akumetsu: stance, stop, strokes, cut.
4. Shippu.
5. HUD, then the Yukikaze dash if wanted.
