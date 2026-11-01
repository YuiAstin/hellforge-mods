# Blink / teleport for a HellForge C mod: research notes

Target: game build 25480438 (game.dll 2E2C3B7C..., HD2Runtime's "F5FEE03DCFDB" profile), HellForge 0.3.0-beta, SDK 15.
All RVAs are game.dll unless marked `exe+`. Date: 2026-10-30.

Status labels: **PROVEN (code)** means another project traced the instruction bytes on this build. **PROVEN (live)** means
it was shown working in game. **SPECULATIVE** means inferred and not checked.

Credits:
- [HD2R]: HD2Runtime 0.40.x/0.50.1, `scripts/research_player_avatar_actions.py`, `docs/backpack-authoring.md`,
  `scripts/research_equipment_coverage.py`. Its licence requires visible credit if these findings are reused.
- [CV]: Vanilla Plus megapack, ConsistentVaulting component, `src/windows_api.lua` and `src/slope_assist.lua` (0BSD).
- [HSR]: Helldiver Survival Rework, `src/cover_ragdoll.c` (MIT).
- [HF]: HellForge, `native/mod-loader/actions.c` and `abilities.c`.

Limitation: game.dll is unpacked only at runtime. The `helldivers2.exe` on disk is WinLicense-packed (every section has
entropy 8.0), and no unpacked game.dll or memory snapshot is available here. So no new disassembly was possible.
Everything below comes from other projects' traced pins. Step 0 explains how to close the gaps with one probe session.

---

## 1. Warp Pack's warp through try_start_action: probably not possible

What the sources show:
- The Warp Pack has **no ability id** anywhere in its decoded data. HD2R's equipment table lists `otherAbility` /
  `selfAbility` only for deposit packs. The Supply Pack's 2629 is the `+0x24` of its deposit definition. The Warp Pack
  entry has only `equipmentType = 24`. Its `DisplacementComponentData` (component 274, 632 B, two records, one owner)
  is decoded as: `+56` arrival ExplosionType, `+72` max survivable UnitSize, `+120` distance 10, `+128` upward bias
  1.4, `+132` downward bias 4, `+140/+144` safe/unsafe heat 15/45 %, `+148` heat per use 33 %, `+152` cooldown 6 %/s,
  `+160` HeatInjuryInfo[12] (stride 24). There is no ability field. [HD2R]
- **PROVEN (code)** [HD2R]: the teleport itself is a direct call in game code: `0x88D790: call 0x5A2BB0`
  ("Warp Pack: SetPosition"), then `0x88D7B1: call 0x4A7550` (SetVelocity, zero). The warp runs in the
  Displacement component's own update (the 0x88Dxxx region). It does not go through the ability tick dispatcher
  `0x11509E0`, which is a 2867-entry id table [HF abilities.c].
- Conclusion (**strong inference**): no ability id reproduces the warp. If `try_start_action` (0xA431D0) gets an id
  that is not an action, the game would most likely refuse it. Worst case it starts an unrelated action. It would not
  warp.
- A "hidden warp component" is not practical:
  - The Displacement update is keyed to the pack entity (the record's heat state and owner).
  - Giving it to the avatar means equipping a Warp Pack in the backpack slot (inventory record `+0x0C` [HF actions.c]).
  - HfApi has no "give backpack" call. `mount` is a deferred data edit for carried entities.
  - **SPECULATIVE**: the warp might be triggered by locating the Displacement manager's per-instance activation flag
    and setting it. Neither the manager global nor the instance layout is identified. It would also require the pack.
- Pack-based shortcut (this works today, but only while wearing the pack): `set_live` / stats on
  `warp.distance` (HfDisplacement +0x78). Its range is 0..100. HD2R marks it "native_correlated, not shown in game",
  and HF marks it `[respawn?]`.

## 2. Setting the avatar's position directly

### 2a. The game's own SetPosition at 0x5A2BB0 (recommended)
- **PROVEN (code)** [HD2R]: the Warp Pack calls it to teleport the wearer (call site `0x88D790`).
- **PROVEN (code)** [HD2R]: it reads the MotionComponent manager's descriptors (`0x5A2CC3: mov rax,[r14+0x48B8]`),
  so it resolves the entity through the same manager that SetVelocity uses:
  - Manager: `[game+0x3326558]`.
  - Entity hash at `+0x48A0`.
  - Capacity at `+0x4880`.
  - Descriptors at `+0x48B8` (descriptor `+8` = entity, `+0x14` bit 0 = owned).
  - Records at `+0x48C8`, stride 0x84, velocity at +0.
  - Second array at `+0x48D0`, stride 0xA4. This is the **mover record**: `+0x0C` velocity, `+0x48` speed,
    `+76` mover name, `+88` engine mover handle [CV].
- Neighbours: SetVelocity `0x4A7550` is `(void* ignored, u32 entity, const float v[3])` and `0x5A3D80` sets the motion
  record flag `+0x0D`. Both are in the same subsystem.
- **SPECULATIVE signature**: `void SetPosition(void* mgr_or_ignored, uint32_t entity, const float pos[3])`.
  - This is a guess by analogy with SetVelocity. The extra `r14` manager suggests it may load the manager itself, like
    SetVelocity does.
  - Possibly a 4th argument (rotation / flags).
  - Verify with Step 0 before calling it.
- **UNKNOWN**: whether it checks that the entity has a motion record. SetVelocity does not check, and an unknown
  entity indexes record -1. So the caller must check that the record exists and names the avatar, as HF's
  ActVelocityNow already does.

### 2b. Engine Mover API (stingray mover through the exe's C API table)
- **PROVEN (code + used live)** [CV]: `[game+0x3326388]` points to the exe table `exe+0x27CDC30`. Entries:
  - `+0x00`: `uint32_t mover(uint32_t unit, uint32_t mover_name)` at `exe+0x7CE220`.
  - `+0x28`: `void position(uint32_t mover_id, float out[3])` at `exe+0x7CF050`.
  - `+0x68`: `void* dimensions(uint32_t mover_id)` at `exe+0x7CFE10`. Its 5 floats include the radius at [3] and a
    minimum at [4].
  - The mover name comes from the motion second-array record `+76`.
- **SPECULATIVE**: Autodesk's stingray `MoverCApi` order is `unit, set_position, position, move, separate, fits_at, ...`.
  In HD2, `position` sits at index 5, which suggests a 3-entry prefix. That would put **set_position at table +0x20**,
  `move` at +0x30, and **fits_at at +0x40**. Check that the pointers resolve into `exe+0x7CExxx..0x7CFxxx` and look at
  their prologues before using any of them.
- Caveat: moving only the engine mover may be overwritten by the game layer, or desync it. Prefer 2a, which is what
  the game does for the warp.

### 2c. Not useful
- Writing scene-graph node 0 (`exe+0x1A100F0` registry, nodes +0x88, world pose +48) is cosmetic. The mover and motion
  system rewrite it the next frame.
- Respawn / hellpod placement code was not traced in any of the sources. The only known reinforcement code is HSR's
  budget hooks (`0x8F3A20`/`0x8F3B10`/`0x8F39F0`), which handle counts, not placement.

### 2d. Fallback "dash" instead of teleport (fixes the add_velocity problem)
- **PROVEN (code)** [HD2R]: the game's own avatar launch (`0xA511A8..0xA51226`) does:
  - `SetVelocity` (5.8 m/s along a direction plus 3.3 m/s up) at `0xA5121A`,
  - then `call 0x5A3D80` at `0xA51226`, which sets the motion record flag `+0x0D = 1` (an airborne/launched state).
- Without that flag, grounded locomotion overwrites the velocity every frame, which matches the 0.01-0.4 m you
  measured.
- **SPECULATIVE**: the signature of `0x5A3D80` is probably `(void*, u32 entity[, u8])`. Read `0xA51200..0xA51230` in
  Step 0 to see its argument setup.

## 3. Existing teleport mods / research
- No mod in the user's mods folder, HellForge, HD2R, HSR or VPM teleports the player.
- No web results either (searches on Nexus / general).
- The only traced teleport is HD2R's Warp Pack call-site note above.
- SGD's `sgd_read.c` only *detects* teleports (respawn/ragdoll jumps) and ignores them.

---

## Recommended approach

**Step 0: one probe session.** This is a tiny C mod with no gameplay effect. It turns the speculative parts into proven.
1. On `HF_EVENT_GAME_LOADED`, `hf->read_code` these ranges and hex-log them. Then disassemble them offline with
   capstone (it is installed in the scratchpad `pyenv`).
   - `0x5A2BB0`, 0x200 bytes: SetPosition.
   - `0x88D700..0x88D7D0`: the warp's call site, which shows the argument setup and how the target is computed.
   - `0x5A3D80`, 0x80 bytes.
   - `0xA51200..0xA51240`.
   - Optionally `0xA9D560`, 0x100 bytes: [CV]'s "exit validator", `int32 classify(void* unused, u32 entity, const
     float point[3], const float quat[4])`, used for vault exits. It may be a ready-made "can the avatar stand here"
     check (**SPECULATIVE**).
2. Also log the 8-byte pointers at `[[game+0x3326388]] + 0x00..0x80`. That gives the Mover table layout.
3. `hook_ex` `0x5A2BB0` with a pass-through detour that logs rcx, edx, r8, r9 and the 3-4 floats at r8 (and at r9 if
   it is a pointer), then calls the original. Wear a Warp Pack and warp once. The log then shows the real signature and
   argument values, and how far the target is from the avatar's position.

**Step 1: the blink, called from `on_game_thread`.** Raycasts and SetPosition must run on the game thread.
1. Guards. Reuse HF's ActLocalAvatar pattern:
   - in a mission;
   - the avatar is the local one, owned, alive and not downed (health record `+0x19C` life < 1);
   - the motion record exists, its descriptor names the avatar, and it is owned (`+0x14` bit 0);
   - the action flags at `actions(+0x3326D20) + 0x53E880 + ai*0x1238` are clear of busy, ragdoll and vehicle states.
     Reuse resupply's masks: `f0 & 2` set, `f0 & 0x0000000100010000` clear, `f1 & 0x1F9D9AF7875B0200` clear. [CV]
     uses a similar eligibility mask.
   - `code_matches` on every function's prologue.
2. Start point: the mover position (2b `position`) or scene-graph node 0. Direction: the `head_aim` node (91) forward.
   Flatten it, or keep some pitch.
3. Collision. Raycast from chest height along the aim for the blink distance, using [HSR]'s raycast:
   - `uint32 raycast(uint32 world_id, const float from[3], const float dir[3], float len, 2, 1, 0x393D9518,
     0x80000009, uint32 ignore_unit, void* hits /*44 B each*/, uint32 max)` at `exe+0x7F4590`.
   - World id from `exe+0x79F860` on `[game+0x346BFA0]`.
   - Hit +24 is the distance and +28 the unit.
   - Only call it while the scheduler is idle (`[game+0x347D7E0]` count 0 and every job state == 1, as HSR's
     `query_idle` does).
   - Stop about 0.6 m (the mover radius plus a margin) before the first static hit.
4. Ground. Copy the warp's own biases: from target + 1.4 m, raycast down up to 4 m. If no ground is found, cancel the
   blink or fall back to a shorter one. Optionally check the spot with fits_at or 0xA9D560 once those are verified.
5. Teleport. Call `SetPosition(…, avatar, target)` with the signature Step 0 found. Then call
   `SetVelocity(NULL, avatar, {0,0,0})`, as the warp does at 0x88D7B1.
6. Optional effect: spawn the warp's arrival explosion (HF reference 254, "LIFT-182 Warp Pack (displacement)").
   - Its package is `packages/generated/loadout/displacement_backpack`, so `load_package` it first.
   - Whether it deals damage is not checked.

**Risks / open points**
- Co-op: avatar movement is simulated by its owner (inferred [HD2R]), so a teleport on the owner should replicate.
  This has not been traced.
- SetPosition's 4th argument, or the internal state it expects (the warp may set a state flag before it), is unknown
  until Step 0.
- Teleporting during vault/climb/ragdoll/vehicle states may break the movement state machine. Keep the guards strict.
- RVAs are build-locked: pin them with `code_matches`.
