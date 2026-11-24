# Homing projectiles (research, 2026-11-24)

Goal: shots that curve into targets (the Destroyer kit wants her projectiles to home; the Rocket Sentry's Veteran level
tried a homing warhead). Sources:
- HellForge's own homing, read from its source: `native/mod-loader/actions.c`, YuiAstin/hellforge fork at 2d2e8b47,
  2026-11-21.
- HD2Runtime's `docs/projectile-homing.md`.
- Rocket Sentry Test 0.3.x (`mods/rocket-test`, local source folder).

## HellForge: `HfApi.homing` (SDK 11)

```c
uint32_t homing(HfMod* mod, const HfHoming* rule);   /* a rule id, 0 refused; stop_shots(mod, id) ends it */
typedef struct HfHoming {
    uint32_t size, projectile;     /* one projectile type (HF_PROJECTILE row) */
    const uint64_t* sources;       /* entity resources that must have fired it, 0-terminated (max 8); NULL any */
    float turn_rate;               /* deg/s, 1..1440 */
    float cone;                    /* half-angle for acquiring, 1..180 deg */
    float range;                   /* m, 1..500 */
    float arm_distance;            /* m flown before steering, 0..100 */
    float aim_height;              /* m above the target's root, -5..10 */
    uint32_t flags;                /* HF_SHOT_MULTIPLAYER 0x1, HF_HOMING_NO_RETARGET 0x2, HF_HOMING_FRIENDLY 0x4 */
} HfHoming;
```

How it works:

- **Which shots.** A new shot counts only if all of these hold:
  - Its hit record's creditor is this machine's peer (the local player's own shots).
  - It's in flight and no impact has been requested.
  - It isn't unit-driven (flight +0x50 = 0).
  - Its source entity's resource is in `sources`, if any are listed.
  - The game is solo, unless the rule has HF_SHOT_MULTIPLAYER.
- **Limits:**
  - At most 16 shots are tracked at once (ACT_TRACKED); more fly straight.
  - A shot is followed for at most 10 s.
  - Each projectile type takes one homing rule across all mods. A second rule for the same type is refused, so two
    mods can't both make the same projectile home.
- **Targets.** By default, living enemies from the health records. With HF_HOMING_FRIENDLY, the other players' living
  Helldivers instead.
  - The enemy list is built a piece at a time: 512 hash slots and 12 position refreshes per update. Candidates are
    capped at 256.
  - Positions older than 0.5 s are ignored.
  - **The scan only runs while a tracked homing shot is in the air.** So the first shot after a quiet spell starts with
    an empty or stale list.
- **Steering (ActSteer).**
  - Past `arm_distance`, and while above 1 m/s, the shot acquires the candidate with the smallest angle off its current
    direction, inside the cone and range.
  - Then each update it turns its velocity (flight record +0x0C) toward the target by at most `turn_rate` × dt. The turn
    is a slerp, and speed is kept.
  - It aims straight at the target's current position plus `aim_height`, with no lead.
  - It re-acquires every 0.1 s unless the rule has HF_HOMING_NO_RETARGET.
- **What's left to the game.** Gravity, drag, hits and penetration stay the game's. The integrator starts each step
  from the turned vector and hit-tests the new path (HD2Runtime's proof, below).

## HD2Runtime: `hd2.projectiles.homing(weapon, opts)`

- Lua. On HellForge it maps onto `HfApi.homing` (HellForge `docs/lua-bridge.md`).
- It takes a weapon name rather than a projectile row, and follows every projectile type the weapon fires.
- Options: `target` ('enemy' or 'friendly'), `turn_rate` (default 90), `cone` (30), `range` (100), `arm_distance` (2),
  `aim_height` (1), `retarget`, `multiplayer`.
- On its own (live game), it scans enemies while the player holds a configured weapon, or a configured shot flew in the
  last 5 s.
  - That warm-up is what HellForge's port lacks: HellForge scans only while a shot is in the air.
- Its doc has the proof of why one velocity write per update is enough: the integrator, the hit test, and the
  slow-shot cutoff never triggering because speed is kept.
- Status there: offline-proven, not live-tested (0.30.0).

## Rocket Sentry Test results (0.3.0 to 0.3.2)

- **Turn rate.** At 100 m/s, 120°/s is a turn about 48 m across: the rockets curved late and swung past. 540°/s
  (about 21 m) worked better.
  - Turn diameter ≈ 2 × speed / turn rate in rad/s, so slower shots or higher rates give tighter turns.
- **Settings that worked best:**
  - 540°/s
  - keep the first target (NO_RETARGET)
  - armed after 3 m
  - a 30° cone
  - aim 0.6 m above the feet
- **Late acquisition.** Because of the scan-only-in-flight rule, each rocket found its target 0.1–1 s after launch.
  That's 10–100 m at 100 m/s, already past most targets. Slowing the warhead rocket to 60 m/s (its own copied row)
  gave the scan time.
- **Outcome.** The Veterancy System's Rocket Sentry shipped without homing ("finds its target too late for a sentry
  rocket"; `rocket_sentry.c`).
- **Still open:** whether a sentry's shots count as the player's own (`homing_sources` 1 vs 0). They do only if the
  game credits the sentry's rocket to the player's peer.

## For the Destroyer (planning notes, not tested)

- **Grenade launchers** are slow and arcing, so they suit HellForge's homing:
  - Slow speed means a tight turn even at modest rates.
  - Gravity is re-applied by the game every step, so a high turn rate is needed to hold a flat path.
  - Pair it with the 0-arming-distance change and keep `arm_distance` low.
- **Fast bullets** (≈900 m/s) need rates near the 1440°/s cap to curve at all, and hit before the first acquisition
  anyway. Late acquisition is the real limit there, not the turn rate.
- **Ways around late acquisition:**
  - (a) Accept that the first shot is a warm-up.
  - (b) Steer the shots herself from the kit, writing flight +0x0C each update from the kit base's own enemy list (or
    SDK 21 `find_enemies`). That's the same write HellForge makes, but with targets known before she fires.
  - (c) Ask upstream for HD2Runtime's "holding a configured weapon" warm-up.
- **One rule per projectile type, shared across mods.** Rules for common rounds can clash with another mod's homing
  rule on the same type. Copying the round to a private row (`add_projectile`, as the Rocket Sentry did) avoids that,
  and leaves other users of the row alone.
- **Solo only by default.** Co-op needs HF_SHOT_MULTIPLAYER, which steers only the local player's shots.
