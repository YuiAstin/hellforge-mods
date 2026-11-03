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
  - **shatter** (her payoff): a frozen enemy takes bonus damage from the next heavy hit;
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
- **Showing a shield on an ally.** There is no shield effect on the model to borrow. A HUD marker that follows an
  entity on screen needs a world-to-screen projection. What the research has (docs/research/game-notes.md, "The
  camera"): the camera's pose, a 4x4 matrix at World + 0x230920 (right, forward, up, position), as
  `World.debug_camera_pose` returns it. A projection also needs the field of view and the screen's aspect. Two routes:
  - **in the HUD's Lua:** the engine's own camera API (Stingray's `Camera.world_to_screen`) if HD2Runtime's overlay
    can reach the game's camera object; not documented in HD2Runtime's ui-overlay.md, so a probe first;
  - **in the DLL:** project with the pose and the field of view (read from the game, or the settings value), and
    export screen points for the HUD to draw at; check against a known point (her own head) before trusting it.
  Until then: her HUD shows how many allies are shielded and their total pool, with a sound or small blast when one
  breaks.
