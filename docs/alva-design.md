# Alva: the support kit (plan)

Alva is AN-94 in Girls' Frontline 2 (DEFY, with Koleda / AK-12 and Voymastina / AK-15): a Support whose passive shields
her allies and whose skills deal freeze damage. She is the second test case for the kit base (docs/kit-base.md, on the
`kitbase` branches), after AK-12: the first kit that works on allies as well as enemies. Nothing here is built yet.

## Her kit in Helldivers terms

- **Ally shield (her passive).** Every ally gets a shield pool that soaks damage before it reaches their health, and
  refills after a few seconds without a hit (as Voymastina's Stability). Allies:
  - **Helldivers**: hers and the other players' (the Helldiver resource);
  - **NPCs and turrets**: SEAF, sentries, guard dogs, exosuits, the FRV and mods' copies of them: the kit base's friend
    list (`kb_friends.h`), built to keep her blasts off friendlies, is the same list.
- **Freeze damage.** The SDK has no freeze or cryo status, so freeze is made of what exists:
  - **chill**: the slow the game uses for flamers (`HF_STATUS_FLAMER_SLOWED`), built up by her freezing hits;
  - **frozen solid**: a stun on top once the chill fills (`HF_STATUS_STUN_*`, as Lenna's);
  - **shatter** (her payoff, 0.2.0): a frozen enemy shrugs off every hit but an explosion's. The first explosion (not
    her own EMS bursts) deals 8x (`shatter_mult`) and thaws it, with a cyan "!damage!" number on the HUD. So the loop is
    freeze, then blow it up: her role is setting up big hits for grenades, launchers and stratagems;
  - **snow fields** (0.2.0): Frosted Echo's nitrogen field and a small field where each of its bursts lands freeze
    anything standing in them after 3 s (`field_freeze_seconds`), divided by how many fields overlap there;
  - **the look**: her blasts recoloured in ice tones (the same particle-copy route as the planned Railcannon recolour).

## How the shield works

- **Where:** the damage event (`kb_damage.h`) sees every hit in the mission, not only hits on her. A hit on an ally with
  shield left takes from the pool first; only the rest reaches their health (or none: cancelled).
- **Pools:** one per entity, kept in a table like Lenna's and Faelynn's marks (entity, pool, max, last hit, refill).
- **New in the kit base:** an ally-pool block, so any later support kit can use it.

## Open questions (to test before building on them)

- **Other players' Helldivers in co-op.** The kit runs on the host. Hits on the host's own Helldiver, the sentries and
  the NPCs go through the host. Whether hits on a client's Helldiver are decided on the host or on that player's machine
  is unknown: if on theirs, the shield covers everything but other divers unless they run the mod too. One co-op
  session with a damage log answers it.
- **Showing a shield on an ally: a marker that follows them on screen.** Solved by others in Lua, through the game
  engine's own camera calls (see "World markers" below). Until it is built: her HUD shows how many allies are shielded
  and their total pool, with a sound or small blast when one breaks.

## World markers: how other mods do it (research, 2026-11-03)

**Treasure Radar** (Th3chef, github.com/Th3chef/Treasure-Radar) floats an icon and a distance over each nearby pickup and
enemy. It runs on **Chef Shared Loader** (Th3chef, github.com/Th3chef/Chef-Shared-Loader, 1.5.1 read, commit 2680b77),
whose `chef.world_to_screen(x, y, z)` does the projection with the engine's Lua API (src/loader.lua, around line 12819
and 14930):

- **Finding the view camera:** walk `World.units(main_world)` (500 a frame) for a living unit with
  `Unit.num_cameras(u) > 0`; that unit's `Unit.camera(u, 1)` is the camera. Keep the unit and ask it again each frame
  (`Unit.alive` first); search again when it is gone.
- **Its pose:** `Camera.world_position(c)`, `Camera.world_rotation(c)` -> `Quaternion.forward(rot)`,
  `Camera.vertical_fov(c)`.
- **The projection:** `Camera.world_to_screen(c, Vector3(x, y, z))` gives screen pixels (bottom-left origin, like the
  game's GUIs). The engine's answer is 2D, so a point behind the camera is the caller's check: skip it when
  `dot(point - camera, forward) <= 0.5`.
- **Its care:** every engine call guarded (pcall) until it has worked once; nothing drawn while the camera falls fast
  (the Hellpod drop).

**What this means for us:** HD2Runtime's overlay draws in the Ui World with the same engine, so the HUD's Lua should
reach the same calls (`Unit`, `Camera`, `Vector3`, `Quaternion`, `World` globals). A probe in one kit's HUD first:
find the camera, project her own head (the DLL knows its position: export it), draw a dot there, check that it sits on
her head while she moves and the camera turns.

**Licences:** both are "all rights reserved"; reading is allowed. Chef Shared Loader also lets mods copy parts of it to
work with it, with the credit line "Parts adapted from Chef Shared Loader by Th3chef". We do not run on it, so we
write our own code against the engine's public calls listed above (the API, not their code), and credit Th3chef's
research in the notices if we use what we learned.
