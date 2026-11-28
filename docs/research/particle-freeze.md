# Freezing particle effects (time stop research, 2026-11-28)

Goal: during a time stop, hold the visuals of particle-based effects (flamethrower flames, gas clouds, Arc Thrower arcs,
burning ground) where they are, still drawn, and let them go on as they were on release. This is the particle half of
[shot-freeze.md](shot-freeze.md).

Static analysis only, nothing played yet. Sources: the game.dll dump (build 25480438, RVA = file offset) and the
helldivers2.exe image dump (the Stingray engine). Addresses are **game.dll RVAs** unless they say **exe**.

Confidence tags:
- **[C]** read directly from code.
- **[I]** inferred from code: call patterns, Lua binding names, data flow.
- **[?]** unknown or a guess.

The exe dump was taken with the image at `0x7FF6CD930000`. Pointers in its `.rdata` (vtables) are absolute, so
subtract that base. Its `.data` is zero: the C API tables are filled at start-up by `lea rax, fn; mov [table+off], rax`
runs, which is how the slots below were resolved.

## Short answer

- **There is no per-effect time scale.** Not in the engine's World C API, not in the Lua `World` bindings (the full
  particle list is below), and not in the effect instance. Each live effect's age is advanced by the particle world's
  one `dt`. **[C]**
- **The whole particle world can be frozen while everything else runs.** Each World owns one ParticleWorld object, and
  `World::update_scene` calls its virtual `update(this, float dt)` once per frame. Called with `dt = 0`, the particles
  stop moving, aging and spawning, and the particle code is written for it:
  - its own code guards `1/dt` against a dt under 1.19e-7;
  - game.dll's frame entry guards `1/dt` the same way;
  - the game's own pause passes the frame time (0) straight to the world update.

  The commands that create, destroy, move or link effects are applied in a separate step, so they still work. The
  render path is separate too, so particles stay drawn. **[C]** for the call chain and the guards, **[I]** that the
  picture holds still.
- **Recommended route:** hook exe `0x1875A0` (ParticleWorld::update). While frozen, call the original with `dt = 0`
  when `this` is the gameplay world's ParticleWorld. It freezes every particle in that world: flames, gas, arcs, fire,
  smoke, blood, muzzle flashes. That fits a time stop. Effects created during the freeze would not start until the
  release. Confidence that it works: **medium-high** (the code supports it; it has not been played).
- **What it does not hold by itself:** gameplay code that destroys an effect, stops it or moves its emitter keeps
  doing so through the command queue. A gas cloud whose entity timer runs out during the freeze would still be removed.
  An arc whose gameplay ends would still be destroyed. Those timers belong to the gameplay freeze, not to this hook.
  **[I]**

## The engine side (helldivers2.exe)

### World and ParticleWorld

| What | Where | Tag |
|---|---|---|
| World → its ParticleWorld | `*(World + 0x230888)` (only one writer, exe 0x1A5E7B, at world creation) | [C] |
| World's particle command queue | `World + 0x170040`: `+0` the ParticleWorld, then 0x60-byte commands from `+0x48`; count `World + 0x230048` (`0x170040 + 0xC0008`), capacity 0x2000 (a full queue writes to address 0, a deliberate crash) | [C] |
| World time (Lua `World.time`) | `World + 0x230874` f32 | [C] |
| ParticleWorld constructor | exe 0x179620 (0x300-byte object); vtable exe `.rdata` 0x16711C8 | [C] |
| Dummy ParticleWorld (no-particles worlds) | vtable exe 0x1670E98 | [C] |
| **ParticleWorld::update(this, float dt)** | vtable `+0x148` → **exe 0x1875A0**; the slot's own address is exe 0x1671310 | [C] |
| Its one caller | exe 0x1B12CC in World::update_scene (exe 0x1B0B10): `mov rcx,[rdi+0x230888]; test; mov rax,[rcx]; movaps xmm1,xmm6; call [rax+0x148]` | [C] |
| The command flush | exe 0x2F1DF0(World + 0x170040), called from exe 0x1B28D0 at 0x1B2B06, **before** the update at 0x1B12DE. A jump table at exe 0x2F1F98 runs each command's ParticleWorld method | [C] |

World::update_scene's callers include World C API slots `+0x3E0` (exe 0x333BD0, update), `+0x418` and `+0x420` (exe
0x334120, update_scene). `dt` arrives in `xmm1` and is kept in `xmm6`. **[C]**

### Inside ParticleWorld::update (exe 0x1875A0)

- `dt` is saved in xmm6 and copied into every job it starts:
  - **"Advance live effects":** exe 0x18A556 starts a parallel job. Its kernel, exe 0x196140, jumps to **exe
    0x18B050**, which reads dt at `[[rcx+8] + 0x10]`.
  - **Per-cloud "particle_simulate" jobs:** exe 0x18A7D0..0x18A8BF build one job record per (effect resource, cloud
    type) with `dt` at `+0x10`. The kernel is **exe 0x182E20**; it reads `[r9+0x10]`.
  - **[C]**
- **The `1/dt` guard (exe 0x18B072..0x18B0B6):** `1/dt` is computed only when `dt > 1.1920929e-07` (constant at exe
  0x16A98A8) and is 0 otherwise. Only six divisions exist in exe 0x182E20..0x1A0000; none divides by a raw dt. **[C]**
- **The per-effect instance:** found through the lookup exe 0x178E40 (a hash map at ParticleWorld `+0x108`, 0x20-byte
  entries, `+0x118` count, `+0x11C` buckets). The entry's `+8` is the instance:

| Instance off | What | Tag |
|---|---|---|
| `+0xCC` f32 | age. Exe 0x18B25B: `age += dt` each update | [C] |
| `+0xD0` f32 | the age at which spawning ends; ParticleWorld `+0xA0` sets it (exe 0x181D30) | [C] |
| `+0xD4` u8 | spawning (stop_spawning clears it: exe 0x181C92; also cleared when age > `+0xD0`: exe 0x18B2B6) | [C] |
| `+0xD5` u8 | set by ParticleWorld `+0x98` (exe 0x181D1B); cleared, with age reset to 0, by `+0xA8` (exe 0x181DBB). When set, a per-instance step is skipped at exe 0x1857CF (it looks like the emission step) | [C] the writes, [?] the meaning |

The age is advanced from the one world `dt`, with no per-instance scale. Holding `+0xCC` by writing it back each frame
would hold the effect's own timeline (emission curves, end of spawning) **but not the particles**: their positions and
ages live in the clouds' buffers, which the simulate jobs advance with the world `dt`. **[I]**

### The World C API's particle slots

The table is `*(*(game+0x3326308) + 8)` (exe 0x27CAB40, 246 slots). Each function takes the `World*` first. Queued
calls only add a command; the flush applies it before the next update.

| Slot | exe fn | What | Tag |
|---|---|---|---|
| +0x268 | 0x337D00 | (world, bool): the whole particle world's visibility (Lua `set_particles_visible`) | [C] call, [I] name |
| +0x270 | 0x337D20 | set_particle_observer (ParticleWorld `+0x140`) | [C]/[I] |
| **+0x280** | 0x337D60 | **create_particles(world, u64 effect, const float m[16]) → u32 id** (queued cmd 0; the id is allocated at once) | [C] |
| +0x290 | 0x337FC0 | batch create (n effects) | [C] |
| +0x298 | 0x3380B0 | destroy now (ParticleWorld `+0x70`; Lua destroy_particles) | [C] |
| **+0x2A0** | 0x3380C0 | **destroy_particles(world, id)**, queued (cmd 1) | [C] |
| +0x2A8 | 0x338130 | stop_spawning now (ParticleWorld `+0x90`) | [C] |
| **+0x2B0** | 0x338150 | **stop_spawning_particles(world, id)**, queued (cmd 2) | [C] |
| +0x2B8 | 0x338270 | are_particles_playing(world, id) → bool | [C] |
| +0x2C8 | 0x3382B0 | ParticleWorld `+0x98`: sets instance `+0xD5`. game.dll never calls it | [C] / [?] |
| +0x2D0 | 0x3382F0 | ParticleWorld `+0xA8`: age = 0, `+0xD5` = 0 (a restart?). game.dll never calls it | [C] / [?] |
| +0x2D8 | 0x3381C0 | destroy_all_particles | [C] |
| +0x2F8 | 0x337E50 | queued cmd 4 → ParticleWorld `+0x58`(id, int, vec3* or NULL, vec3* or NULL). start_effect calls it after a move | [C] / [?] name |
| **+0x330** | 0x337F10 | **move_particles(world, id, const float m[16])**, queued (cmd 5 → ParticleWorld `+0xC8`, the instance pose at entry `+0x38`) | [C] |
| +0x340 | 0x3383C0 | link_particles(world, id, unit, node, m[16], int, bool), queued (cmd 3) | [C] |
| +0x358 | 0x3384A0 | find_particles_variable(world, u64 effect, u32 name) → index (ParticleWorld `+0xF8`) | [C] |
| +0x360 | 0x3384C0 | set_particles_variable now (ParticleWorld `+0x100`; the Lua binding uses it) | [C] |
| **+0x368** | 0x3384E0 | **set_particles_variable(world, id, index, const vec3*)**, queued (cmd 6). game.dll's usual form, after find_particles_variable | [C] |
| +0x370 | 0x338570 | queued cmd 7 → ParticleWorld `+0x158`(id) | [C] / [?] |

The full Lua list (exe strings 0x1680748..0x16809C8): create_particles, set_particle_observer, trigger_particle_emit,
set_particles_visible, stop_spawning_particles, destroy_particles, are_particles_playing, destroy_all_particles,
link_particles, move_particles, set_particles_variable, find_particles_variable. There is no time scale, pause or
simulate call. **[C]**

ParticleWorld `+0x118` (exe 0x182C90) adds a time to an instance's age (a fast-forward). No C API slot reaches it, and
it only moves the age anyway. **[C]**

## The game side (game.dll)

### Which World the gameplay effects use

- **`game.dll+0x346BFA0`:** the effect starter's World, `start_effect` (0x1779A10, below). Its callers load `rcx` from
  it: 0x6419DA, 0x6C4A88, 0x753535 and others. It is written at 0xFDA367 and 0xFDB098, with the entity system's own
  global at 0x346BF98. **[C]**
- **`HfGame + 0x10E8`:** the World the game's frame update passes to World `update_scene` (slot `+0x420`, 0xAB6235).
  `HfGame + 0x1118` gets the full update (slot `+0x3E0`, 0xAB6250). **[C]**
- That these are the same World is **[I]**. The probe should log the ParticleWorld of each.

### dt, and the game's pause

- The frame entry 0xAB4FD0, reached through a vtable, takes `dt` in xmm1 and keeps it in xmm7.
- At 0xAB5266..0xAB52BB it writes the Clock (`[game+0x3326348]`): `+0` and `+4` dt, `+8` 1/dt (**0 when
  dt < 1e-6**), `+0x10`/`+0x18` microsecond counters, `+0x20` the frame count.
- The same xmm7 goes to the World update at 0xAB6227/0xAB6235.
- So when the game pauses (frame time 0), the particle world is already updated with `dt = 0` every frame. That is the
  case a mod's freeze would reproduce for the particle world alone. **[C]** the code, **[I]** that a pause gives 0
  (shot-freeze.md's reading of the shot step).

### How the game makes effects

- **start_effect (0x1779A10):** `(World*, u32 unit, const EffectDesc*, …) → u32 id`.
  - The desc's `+0x00` is the effect resource (u64; 0 means none), `+0x20` a node hash, `+0x24` an int passed to slot
    `+0x2F8`, `+0x28` the link mode, and `+0x2C` flags: 4 world pose, 8 local offset, 2 link to the unit.
  - It either creates directly (slot `+0x280`) or goes through 0x1302600, which looks like a pool of reusable
    effects. A pooled effect is then moved (`+0x330`) and given cmd 4 (`+0x2F8`). It links with `+0x340`.
  - It has 35 callers: missile engines 0x645C95, throwables 0x6C4AA5, explosive fuses 0x8CA602 and 0x8CB155, and
    others.
  - **[C]** the call shape; **[I]** the field meanings.
- **Every particle call by function:** 193 game.dll functions use the slots above, listed with call sites in the
  session scratchpad (`/tmp/claude-0/-home-claude/83df7915-a41e-51e7-8756-7e1e82cec6e8/scratchpad/pf/particle_callers.txt`,
  not kept). Some:
  - ProjectileFire 0x6128B0: move and cmd 4;
  - SpawnProjectile 0x13A9830: create, find_var, set_var;
  - ProcessExplosion 0x13C0D10: create;
  - SetMissileEngine 0x645B80: are_playing, stop_spawning;
  - WeaponChargeUpdate 0x73C8F0: create, link, stop_spawning.
- **The arc system (0x13B4EB0, the arc update loop):** each arc keeps particle handles in its record at
  `arc_system + 0x11930`.
  - `+0x00` the effect id, `+0x04` a variable index, `+0x0C` vec3 the value written each frame with set_var
    (`+0x368`, 0x13B5A7B), and `+0x34` a second effect id.
  - The arc's effect is destroyed with `+0x2A0` (0x13B5C78), and the field is cleared, when the arc ends.
  - Impact sparks are fire-and-forget creates (0x13B6C05; the id is not kept).
  - **[C]** for the offsets read at those sites; the record stride and the system global were not pinned.
- **Flamer hits come from particle collisions:**
  - The spray hit system's global is **`game.dll+0x3326AB0`**. Its hit list is at `+0x10` (8-byte entries: entity,
    float), its count at `+0x2010` (capacity 0x400), and the per-entity repeat table at `+0x8018`.
  - Hits are added by 0x6F7AD0 and by the callback **0x6F58F0**. The callback's record has `+0` the hit entity, `+4`
    the source entity, `+0x20` a position (vec3), `+0x2C` a second vec3, `+0x48` a float.
  - The callback is handed to the API at `[game+0x3326320]` slot `+0x30`. Those calls are at 0x6F4297 and
    0x6F42BE, and at 0x569BA3 inside the component world update, every frame.
  - **[C]** the writers; **[I]** that the API is the particle collision event stream (not resolved in the exe).
  - With the particle world frozen, no particles move, so no new collisions, so no flamer damage until the release.
    **[I]**
- **Not located:** where the player flamer's flame effect is created, and the handle it keeps; the same for gas clouds
  and burning-ground fire (likely an Explosive/area entity's effect through start_effect). None is needed for the
  world freeze. Finding them only matters for a per-effect freeze.

## Recommended probe: Particle Freeze (add to Shot Freeze Probe, or a separate probe)

Bind it with the shot freeze to Ctrl+F11, or to a separate combination; not F5 or F8 (HellForge uses them).

### Setup, once the game's code is ready

1. `exe = hf->module_base("helldivers2.exe", &size)`.
2. Check both, and refuse with a log line if either fails:
   - `hf->code_matches(exe + 0x1875A0, "F3 0F 11 4C 24 10 48 89 4C 24 08 55 53 56 57 41 56 48 8D AC 24 B0 D7 FF FF B8 50 29 00 00 E8")`
     (ParticleWorld::update's prologue);
   - `hf->code_matches(exe + 0x1B12CC, "48 8B 8F 88 08 23 00 48 85 C9 74 18 48 8B 01 0F 28 CE FF 90 48 01 00 00")`
     (its call site: pins both the World offset 0x230888 and the vtable slot 0x148).
3. Optionally also check the C API slot that reads the same offset:
   `*(*(*(game+0x3326308)+8) + 0x298)` should point at bytes `48 8B 89 88 08 23 00 48 8B 01 48 FF 60 70`.
4. `hf->hook(mod, exe + 0x1875A0, pw_update_detour, &pw_update_orig)`.

   ```c
   typedef void (*PwUpdate)(void* pw, float dt);   /* x64: rcx = pw, xmm1 = dt */
   static PwUpdate pw_update_orig;
   static void* volatile g_frozen_pw[4];            /* ParticleWorlds to hold */
   static volatile LONG g_freeze;                   /* 0 off, 1 on */
   static float g_scale = 0.0f;                     /* 0 = hold; 0.1 = slow motion for the test */

   static void pw_update_detour(void* pw, float dt)
   {
       if (g_freeze)
           for (int i = 0; i < 4; ++i)
               if (pw == g_frozen_pw[i]) { dt *= g_scale; break; }
       pw_update_orig(pw, dt);                       /* always call it: never skip the update */
   }
   ```

   Never skip the original. The update also builds this frame's job and arena data, and render-side state may expect
   it. `dt = 0` is the case the game already runs while paused.

### Each game-thread tick

1. Read World A = `*(game+0x346BFA0)`, World B = `*(HfGame+0x10E8)`, World C = `*(HfGame+0x1118)`.
2. For each non-null World `w`: `pw = *(w + 0x230888)`.
3. Check `*(uintptr_t*)pw` is exe+0x16711C8 (the real vtable, not the dummy exe+0x1670E98), and that
   `((uintptr_t*)vt)[0x148/8]` is exe+0x1875A0.
4. Put the ParticleWorlds of A and B in `g_frozen_pw` (C only if the log shows it holds gameplay effects).
5. Log, once per mission:
   - each World's and ParticleWorld's address;
   - which ParticleWorlds the detour sees, with their dt;
   - the command count `*(u32*)(w + 0x230048)` while frozen (it should stay small: gameplay still queues commands and
     they are flushed every frame).

### Tests (one at a time, each held 3 s and then released)

- Flamer firing at a wall and at enemies: do the flames hang in the air? Do they resume? Does damage stop during the
  freeze?
- Gas grenade or gas strike cloud: does the cloud hold? Is it removed if its timer ends during the freeze (it should
  be, until the gameplay timer is held too)?
- Arc Thrower: does an arc that is drawn hold, or vanish when its gameplay arc ends?
- Burning ground (incendiary impact): same.
- Firing while frozen: do the new muzzle flashes appear only on release (expected)?
- `g_scale = 0.1` (slow motion) as a gentler first test. It should look like slow motion, which proves the hook
  reaches the right world.
- Watch for a crash on freeze or release, and for the frame time while frozen.

### What would be next if it works

Hold the gameplay timers that destroy these effects: the gas cloud's and fire's owning entities (not located yet), and
the arc system's lifetime, at `0x13B4EB0`. Then the visuals and the gameplay stop together.

## Per-effect options (lower confidence, only if a world freeze is not wanted)

| Idea | What it holds | Cost and risk | Tag |
|---|---|---|---|
| Write the instance age `+0xCC` back each frame (instance through the lookup at exe 0x178E40) | the effect's own timeline (emission curves, end of spawning) | particles still move: not a visual freeze | [C] fields, [I] effect |
| ParticleWorld `+0x98` / C API `+0x2C8` | sets `+0xD5`, which skips a per-instance step (likely emission) | does not stop motion; `+0xA8` (the inverse) also resets the age to 0 | [?] |
| Hook the job kernels exe 0x18B050 (advance live effects; dt at `[[rcx+8]+0x10]`) and exe 0x182E20 (per-cloud simulate; dt at `[r9+0x10]`), zeroing dt for chosen jobs | every instance of the chosen effect resources | jobs are per (effect resource, cloud type), not per instance; how a job maps to its resource is not pinned (job `+0x18`/`+0x20` look like the type record); the kernels run on worker threads | [?] |
| move_particles (`+0x330`) every frame | only the emitter pose | does nothing to particles already emitted | [C] |

## Not covered

- Whether HD2 simulates any particles on the GPU. That would still be driven by the update's dt. **[?]**
- Co-op: particles are drawn locally, so each machine would need its own freeze. **[I]**
- Where the API at `game+0x3326320` lives in the exe, and that its slot `+0x30` is the particle collision stream.

## As played (2026-11-28, Shot Freeze Probe 0.4.0 to 0.4.4)

- **The world freeze works:** the hook held the game's ParticleWorld (and a second real one) every frame of each stop;
  no crash. Particles already out hang. But at dt 0 a flamer or gas emitter releases nothing; one-off bursts (ignition
  sparks) appear and hang.
- **Mode B fails:** giving only the per-cloud simulate jobs (exe 0x182E20) dt 0, with the world's dt kept, stopped
  spawning as well. Emission and motion share the one dt inside the particle programs.
- **What is kept: the world at a fraction of time.** At 2% (Yui's pick of 2, 5 and 10%; 10% too fast) a thin stream
  creeps out of the barrel and what is out crawls; at release all runs at full speed again.
- Arc bolts are not particles: see [arc-freeze.md](arc-freeze.md). With the arc system skipped, an arc fired during a
  stop comes out only at the release.
