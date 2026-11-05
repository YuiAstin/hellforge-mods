# Driving an empty exosuit (Exo Companion 1.0.2, read 2026-11-05)

Exo Companion by M!ni$try0fSc!eиce (Lua for BSL/MDL; ships as LuaJIT bytecode) makes the last exosuit you piloted
follow you and fire. Read from its strings, settings comments and code signatures only (not decompiled). The RVAs are
where its signatures land in our build 25480438 dump (tools/dump-analysis); which one is the function entry is still to
check in the disassembly. Credit her if any of this ships, and ask her first.

How it moves the suit:
- **The game's own locomotion, not a push.** It writes the suit's stick input (`in_x`, `in_y`, `in_z`) into the
  suit's VehicleLocomotion record, so the walker's own legs, turning and animation run ("VehicleLocomotion does it
  already"). Signature `vloco` near `0x59CA90` (record lookup, stride 0xA4).
- **Engine on** while active ("the walker keeps turn targets only with the engine on"): `eng_set` `0xFD9D40`.
- **The physics mover a pilot leaves behind** ("walking without piloting first"): `mover` `0x6FE480`. This is what lets
  a suit nobody piloted walk; the animation-state recording (`Unit.animation_get_state` / `animation_set_state`) is an
  older optional path.
- Fallback setting: physics external velocity instead (slides, no leg animation).
- Turning: `loco_en` `0x9C8C70`, `rot_en` `0x61DBB0`, `rot_set` `0x755080` (turn targets, written direction variable;
  `turn_sign` / `dir_sign` settings flip them).
- Firing its arms: weapon trigger `wtrig` near `0x11B602A`. Other signatures: `veh_in` `0x592040`, `seat` `0x4BD050`,
  `motion` `0x59F470`, `mount` `0x17BD524`, `ent_lookup` `0x11B53C0`, `faction` `0x926EF0`, `aienemy` `0x8D5210`.
- No pathfinding: straight at the player, jump or Exo Dodge boost when blocked (measured speed under `stuck_speed`).

For Koleda's Sinner: an exosuit of our own (the Concept Mech re-exported) driven the same way from her DLL, with its
own arm weapons firing through the game's trigger, and Voymastina's warp as the leash.
