# Flight for the player's Helldiver (research, 2026-11-28)

Static reading of the game.dll dump (build 25480438), HD2Runtime's backpack notes and the JumpTest / Jump Probe mods.
Nothing here is played yet; the probe plan is at the end. Verified = read in the code; guessed = marked.

## The short of it

JumpTest 0.2.5 already holds the core: the game's own **SetVelocity `0x4A7550`** then **airborne flag
`0x5A3D80`(NULL, id, 1)** on her own avatar lifts the body and steers it, and gravity brings it down until the flag
clears. Flight is that pair every frame.

## Gravity

- **One world value, in code.** The motion update `0x59CED0` reads a constant vector (0, 0, -9.82) at
  `game+0x328CC60` (scalar `+0x328CC68`, read-only .rdata), loaded at `0x59D6B2` / `0x59D7DC` and added as
  velocity x dt while airborne (`0x59DBC5..0x59DBEB`). No gravity field in AvatarComponentData or any stat; patching the
  constant changes it for everything that moves, enemies too. Verified.
- **No per-entity scale.** Motion record byte `+0x0E` swaps gravity for zero (`0x59D893`, skipped at `0x59DA82`) and
  then looks the entity up in `[game+0x3326A80]` (stride 0x1C) to rewrite its height: a water/swim path (guessed). Not to
  be set: an entity missing there is indexed unchecked.
- **Motion records:** manager `[game+0x3326558]`, records at `+0x48C8`, stride 0x84: `+0x00` velocity, `+0x0D`
  launched / airborne flag, `+0x0E` the flag above, `+0x64` time off walkable ground (HD2Runtime).
- **Scaling gravity for one body = adding lift:** +9.82 x k x dt to its vertical velocity each frame (the Hover Pack's
  way: 9.8 lift against 9.82 holds height; HD2Runtime `docs/backpack-authoring.md`).

## In the air

- **The game's launch** `0xA50890`: SetVelocity (5.8 m/s along a direction, 3.3 up), then the airborne flag. Verified.
- **What clears the flag** (4 callers): the locomotion update `0xA4FFB0` at `0xA505AE`, on a ground hit or when the
  motion speed (`+0x48`) is under 0.25 m/s (the second guessed: a still hover may be dropped, so set the flag every
  frame); state / action exits `0xA51783`, `0xA64D0F`.
- **Air steering** exists only for the jump packs (`0x9B4790`, JumppackComponentData); without a pack the mod steers.

## Ways to fly, best first

1. **SetVelocity every frame + the flag** (parts proven, the combination new): each tick vz = climb input x speed +
   9.82 x dt, vxy = move input / aim x speed, airborne(1); on landing clear it and play `land` (JumpTest's landing code).
   Guards as JumpTest / Lenna: her own avatar only, prologue bytes checked, not ragdolled, climbing or in a vehicle
   (action-flag masks in [teleport-blink.md](teleport-blink.md)). Risks: fall damage on a fast touchdown (cap landing
   at about -4 m/s until the threshold is known), an explosion knocking her out of it, the in-air pose.
2. **The Hover Pack, data only** (HD2Runtime's reading, live-tested there): `hover.vertical_acceleration_low_speed`
   above 9.82 climbs, `hover.max_horizontal_speed` (3.5), `hover.fuel_rate_*` -1 no fuel. Needs the pack worn (only the
   LIFT-850 showed in the last `arsenal.tsv`). Looks and lands like the real thing.
3. **Jump Pack tuning** (`jump.sustain_*`, `jump.air_control_*`): longer powered jumps, not free flight.
4. **The warp setter `0x5A2BB0` every frame:** safe on her own avatar (Lenna, Faelynn; the crashes were on weapons,
   riders and corpses), but no collision (raycasts needed) and it looks like sliding: a fallback.
5. Calling the launch `0xA50890` itself: not advised (a whole state change; 1 already makes its two calls).
6. `jump_generic` / `jump_loco`: take-off and landing only; a `fall` event exists (Jump Probe), a possible in-air pose.

Co-op: each machine runs its own avatar's motion (HD2Runtime); position should replicate, Lua animation events
probably not (unchecked).

## To probe in game

1. SetVelocity (0, 0, 9.82 x dt) + the flag each frame for 10 s: log the height drift and `+0x0D` each frame; does
   `0xA505AE` clear it while hovering?
2. `animation_get_state` layers while flying with the Jump Pack and the Hover Pack, and falling: pick an in-air pose.
3. Drops from 5 / 10 / 20 m: touchdown speed against damage, for the fall-damage threshold.
4. An explosion mid-flight: the ragdoll takes over cleanly and the flag ends clear.
5. Whether the Hover Pack is in HellForge's arsenal; then lift 15 and fuel -1.
