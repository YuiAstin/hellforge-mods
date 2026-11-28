# Freezing arc bolts (time stop research, 2026-11-28)

Goal: during a time stop, an Arc Thrower (or other arc weapon) bolt that is visible when time stops stays drawn and holds
still until the release, then goes on or ends normally. This is the arc part of [particle-freeze.md](particle-freeze.md)
and [shot-freeze.md](shot-freeze.md).

This is static analysis only; nothing has been played yet. Sources: the game.dll dump (build 25480438, RVA = file
offset) and the helldivers2.exe image dump. Addresses are **game.dll RVAs** unless marked **exe**.

Tags: **[C]** read from code, **[I]** inferred, **[?]** unknown or a guess.

## Short answer

- **Each arc bolt is drawn with two things:**
  - **A particle effect.** The game writes its start and end points into two particle variables each frame.
  - **A unit.** This is a mesh that the game places at the start point, turns toward the end point and scales along
    its Y axis to the bolt's length, each frame.

  Both are created from the arc's settings row: the particle effect from row `+0x30`, the unit from row `+0x50`. **[C]**
- **Game code does not randomise the bolt's look.** Each frame, the arc code only:
  - writes the two endpoints into the particle variables (World API `+0x368`);
  - sets the unit's local position, rotation and scale (World API `+0x590`/`+0x598`/`+0x5A0`, engine unit commands
    0xC/0xD/0xE).

  The rotation is a look-at with a fixed up vector, with no random roll. **[C]** Any shimmer on a bolt that holds still
  comes from the engine: the particle simulation (already frozen by the ParticleWorld hook) or the unit's material or
  animation (see the last section).
- **Why the bolt keeps going during a stop:** the arc gameplay is still running. Every frame:
  - the bolt's head moves forward;
  - hits create the next hop of the chain (new particle effect, new unit);
  - an arc that has ended is handed to a "linger" record, whose timer counts down;
  - after that the bolt shrinks toward its end at the arc's velocity and is destroyed.

  All of it uses the game frame time `[game+0x3326348]+4`, read once at the start of each of the two arc functions. The
  ParticleWorld freeze cannot hold any of this. **[C]**
- **Recommended route (medium-high confidence):** hook the arc system's two per-frame functions, `ArcUpdate`
  **0x13B4EB0** and `ArcPost` **0x13B5510**. While frozen, return without calling the original, deciding once per frame
  so that both are skipped in the same frames.
  - Nothing in the arc system changes: positions, linger timers, query ids, effects and units stay as they are.
  - The engine keeps drawing the unit where it was last put. The unit commands are retained state, not redrawn per
    frame. **[I]**
  - The particles are held by the existing ParticleWorld `dt = 0` hook.
  - On release, both functions carry on from the same state, so the arc resumes or ends normally.
  - Arc damage also stops, because hits are applied in `ArcPost`.
- **Do not freeze the arcs with `dt = 0` (for example by zeroing the Clock around the calls).** In `ArcUpdate` the step
  is `min(remaining range, |velocity|·dt)`, and a step of 0 or less ends the arc (0x13B5155..0x13B51EB). Every arc in
  flight would end on the first frozen frame. **[C]**
- **An arc fired during the stop:** `ArcCreate` (0x13B3910) still runs from the weapon. Its record starts with
  start = head (0x13B3C8C..0x13B3CAE), so the unit is placed with length 0, and its particle effect does not emit while
  the particle world is frozen. **It is not drawn until the release, then flies normally.** That is the acceptable case.
  **[C]** for the start = head and the placement, **[I]** that a unit scaled to 0 on Y is invisible. Drawing it frozen
  as well ("ideal") is possible only for the unit part, and only with a more involved partial run (Option B below).

## Where the arc system is

| What | Where | Tag |
|---|---|---|
| **ArcSystem pointer** | `*(game+0x346D5B0)`, written once at 0x13F2AF0 (`lea rax,[rcx+0x19E55F8]; mov [rip→0x346D5B0], rax`). Its readers are the weapon's fire path 0x80DC2E, 0x13B4833 and 0x13C140F | [C] |
| It is embedded in the gameplay object | `HfGame-ish + 0x2A09818` (0xAB5FD0: `lea rcx,[r14+0x2A09818]; call ArcPost`). The shot pool is the same object at `+0x44F7E30` | [C] |
| Init and clear (per mission) | 0x13B4DE0 (from 0x13F62D6 and 0x13F7394): ends every active slot and destroys every lingering effect and unit | [C] |
| **ArcUpdate(sys)** | **0x13B4EB0**. Its one caller is 0x13F7660, inside the gameplay systems update 0x13F73F0, which 0xAB55AF calls. The same function then tail-jumps to the shot step 0x13AB0E0 | [C] |
| **ArcPost(sys)** | **0x13B5510**. Callers: 0xAB5FD7 (frame update, after the physics step; the shot hit pass 0x13AC1F0 follows at 0xAB5FEF), and 0x13F76B8 (in 0x13F7690, which has no direct caller; probably another mode) | [C] |
| ArcCreate | **0x13B3910** `(sys, const vec3* origin, const vec3* dir, const ArcSettingsRow* row, u32 shooter [rsp+20], u32 [rsp+28], u64 [rsp+30], u32 [rsp+38], u8 [rsp+40]) → u32 slot`. Called by the arc weapon (0x80DC35), by chaining (0x13B7454) and by 0x13C1416 | [C] call shape, [I] arg names |
| ArcEnd(sys, slot, bool keep_visual) | **0x13B45C0**. With `keep_visual` set, it moves the bolt's effect and unit into the slot's linger record V; otherwise it destroys them. It then tail-jumps to 0x62E500 with R+0xD8 (looks like audio) | [C] / [?] audio |
| PlaceBoltUnit(sys, u32 unit, const vec3* from, const vec3* to) | **0x13B4380**: World API `+0x590` local position = from; `+0x598` rotation = look-at(to − from, up = `[0x330E190]`); `+0x5A0` scale = (1, \|to − from\|, 1); node 0. These queue engine unit commands 0xC/0xD/0xE (exe 0x339020/0x339070/0x3390B0: World `+0x10038` + 0x58·n, count `World+0x170038`) | [C] |

### The frame time

Each function reads it once at entry:

- ArcUpdate: 0x13B4EF5 `mov rax,[rip→0x3326348]` … 0x13B4F23 `movss xmm13,[rax+4]`.
- ArcPost: 0x13B5586 … 0x13B55AF `movss xmm1,[rax+4]`, kept at `[rbp-0x28]`.

No callee reads the Clock for the arc logic. **[C]**

## ArcSystem layout (`sys = *(game+0x346D5B0)`)

| Off | What | Tag |
|---|---|---|
| `+0x00` | World* (the first argument to every World API call) | [C] |
| `+0x18` | pointer; its `+0x90` and `+0x98` u32 are the collision filters for the two query kinds | [C] reads, [I] meaning |
| `+0x2C` u32 | allocation counter. `slot = counter & 0xFF`; busy slots are skipped, and when all 256 are busy the slot is evicted with ArcEnd (0x13B3BC8..0x13B3C5E) | [C] |
| `+0x30 + i` u8 | **active** (256 slots) | [C] |
| `+0x130 + 0x118·i` | **ArcRecord R** (the bolt in flight), 256 of them | [C] |
| `+0x11930 + 0x38·i` | **ArcVisual V** (the lingering or shrinking bolt after the arc ended), 256 of them | [C] |
| `+0x15130 + 4·i` u32 | settings row index. The row is `*(game+0x37C5F90 + 8·idx)`; index 0 is the zero row at 0x348DF98 | [C] |

### ArcRecord R (0x118 bytes; ArcCreate zeroes it and fills it)

| Off | What | Tag |
|---|---|---|
| `+0x00` vec3 | **start**: the origin at creation. ArcPost then takes it each frame from the source (R+0x40 unit, or R+0x38 entity with node R+0xE8; 0x13B6F95..0x13B7126) | [C] |
| `+0x0C` vec3 | **head** (the end of the bolt); starts equal to start; ArcUpdate moves it by `dir·step` (0x13B5315..0x13B5349) | [C] |
| `+0x18` vec3 | velocity = dir × row.velocity (`row+0x04`) | [C] |
| `+0x24` u32 | id of this frame's ray/sweep query (0x175CA60; the queue is at `[game+0x3326340]+0x793A600`, 64-byte entries) | [C] |
| `+0x28` u32 | id of the chain-search overlap query (0x175BB40; at `[game+0x3326340]+0x799AB58`, 128-byte entries); issued only while it is 0 | [C] |
| `+0x2C` u32 | hop index in the chain | [C] |
| `+0x30` u8, `+0x31` u8 | phase flags: `+0x31 = 0` travelling; 1 searching for the next target (set at 0x13B6F0D) | [C] writes, [I] names |
| `+0x32` u8 | first-frame flag (1 at creation) | [C] |
| `+0x33` u8 | `row+0x5C` bit 0 | [C] |
| `+0x34` f32 | distance travelled (`+= step`, 0x13B51FE) | [C] |
| `+0x38` u32 | shooter entity (ArcWeapons is notified through 0x80CDA0) | [C] / [I] |
| `+0x3C` u32, `+0x40` u32 | another entity (skipped as a target), and the unit the start point follows | [I] |
| `+0xD0` u64, `+0xD8` u32 | passed on to each chained arc; `+0xD8` goes to 0x62E3B0 and 0x62E500 (audio?) | [C] / [?] |
| `+0xDC` u32 | **particle effect id** (create `+0x280` from `row+0x30`, at 0x13B41DF) | [C] |
| `+0xE0`, `+0xE4` u32 | particle variable indexes for hash **0x9BC68A40** (start) and **0x72ACD249** (end) | [C] |
| `+0xE8` u32 | source node index (-1 none) | [C] |
| `+0xEC` u32 | **bolt unit id** (spawn `+0x40` from `row+0x50` with an identity pose, at 0x13B42D4) | [C] |
| `+0xF0` f32, `+0xF4` f32, `+0xF8` f32, `+0xFC` u32, `+0x100` u32 | range multiplier, two more multipliers, extra hops (from the shooter's modifiers 0x73E280, 0x73E480 and 0x73E530) | [C] writes, [I] names |
| `+0x104..` u32[], count `+0x114` | entities already hit (skipped by the chain) | [C] |

### ArcVisual V (0x38 bytes; ArcEnd copies it from R)

| Off | From | What | Tag |
|---|---|---|---|
| `+0x00` u32 | R+0xDC | particle effect id | [C] |
| `+0x04` u32 | R+0xE0 | start variable index | [C] |
| `+0x08` u32 | R+0xE4 | end variable index | [C] |
| `+0x0C` vec3 | R+0x00 | start | [C] |
| `+0x18` vec3 | R+0x0C | end | [C] |
| `+0x24` f32 | `row+0x38` | **linger time left** | [C] |
| `+0x28` u32 | R+0x38 | entity | [C] |
| `+0x2C` u32 | R+0x40 | unit | [C] |
| `+0x30` u32 | R+0xE8 | node | [C] |
| `+0x34` u32 | R+0xEC | **bolt unit id** | [C] |

What ArcPost does with V each frame (0x13B5607..0x13B5CAE):

- It skips the record when both `+0x00` and `+0x34` are 0.
- **While `+0x24 > 0`:** `+0x24 -= dt`. Start is taken again from the source, the start variable is written, and the
  unit is placed from start to end.
- **Once `+0x24 <= 0`:** start moves toward end at `row.velocity` (0x173CD10(out, &start, &end, speed, dt)), and the
  variable and the unit are updated.
- When start is within the distance at 0x23C6630 of end, it destroys the effect (`+0x2A0`, queued), destroys the unit
  (World API `+0x60`), and clears the record.

**[C]**

### ArcSettings row, beyond SDK 20's `HfArcSettings`

These fields sit in its `_unknown_28` block:

| Off | What | Tag |
|---|---|---|
| `+0x00` u32 | the row's own index (stored at `sys+0x15130`; row 15 is a special case at 0x13B415F) | [C] |
| `+0x30` u64 | bolt particle effect resource | [C] |
| `+0x38` f32 | linger time after the arc ends | [C] |
| `+0x50` u64 | bolt unit resource (0 means no unit) | [C] |
| `+0x5C` u8 | bit 0 → R+0x33 | [C] |
| `+0x64` u8 | bit 0: hits on R+0x3C are allowed | [I] |

The rows are on the heap, so their values are not in the dump.

## What ArcUpdate and ArcPost do per frame

The loops run over all 256 slots; the bound is `cmp r15d,0x100` at 0x13B5497 and `cmp edi,0x100` at 0x13B74EE.

**ArcUpdate (0x13B4EB0), for each active slot:**

- **Travelling** (`R+0x31 = 0`):
  1. `step = min(range·hopfrac·R+0xF0 − R+0x34, |R+0x18|·dt)`.
  2. If `step <= 0` it calls ArcEnd(keep visual) and clears active.
  3. Otherwise it issues a sweep from the head along the velocity for `step`, then `R+0x34 += step` and the head moves
     on.
- **Searching** (`R+0x31 = 1`): it issues the overlap query, once.

**ArcPost (0x13B5510), for each slot:**

1. The V record (above).
2. If the slot is active, the R record:
   - consume the sweep or overlap results: pick hits, apply damage, call 0x13B28A0 and 0x656A50;
   - start the next hop through ArcCreate at 0x13B7454, then ArcEnd the current one;
   - impact sparks (a fire-and-forget create at 0x13B6C05);
   - take start again from the source;
   - write both particle variables (0x13B6E8F, 0x13B7158);
   - place the unit (0x13B6EE2 → 0x13B4380).

**[C]** for the flow, **[I]** for which callee applies the damage.

Queries are issued in ArcUpdate and consumed in ArcPost of the same frame, with the physics step in between. **[I]**

## Option A (recommended): skip both functions while frozen

```c
typedef void (*ArcFn)(void* sys);
static ArcFn arc_update_orig, arc_post_orig;
static volatile LONG g_freeze;          /* the mod's time stop flag */
static int g_arc_frozen_frame;          /* latched once per frame in ArcUpdate */

static void arc_update_detour(void* sys)
{
    g_arc_frozen_frame = g_freeze;                     /* latch: ArcPost this frame uses the same answer */
    if (g_arc_frozen_frame && sys == *(void**)(game + 0x346D5B0)) return;
    arc_update_orig(sys);
}
static void arc_post_detour(void* sys)
{
    if (g_arc_frozen_frame && sys == *(void**)(game + 0x346D5B0)) return;
    arc_post_orig(sys);
}
```

- **The latch matters.** If the update ran and issued queries, the post in the same frame must consume them. If the
  update was skipped, the post must be skipped too, or it would read the previous frame's query ids (the sweep id at
  R+0x24 is rewritten by each travelling update: 0x13B50DB and 0x13B529E). So the freeze flag is read only in
  ArcUpdate. **[C]** the id writes, **[I]** that the ids are only good within their own frame.
- **What holds:**
  - the bolt's start, head, length and distance travelled;
  - the linger timer;
  - the shrink;
  - the unit's last pose (no commands are queued, so it keeps the one it has);
  - the particle variables (not written).

  The start no longer follows the player's muzzle, so the bolt stays where it was. **[C]** the state, **[I]** that the
  unit stays drawn.
- **On release:**
  - a travelling arc continues from where it was;
  - a lingering one resumes counting its timer;
  - a shrinking one resumes shrinking.

  The `dt` on the first frame after release is the normal frame time (no catch-up), because the functions read the
  Clock and carry no time over. **[C]**
- **Fired during the stop:**
  - ArcCreate allocates a slot and creates the effect and the unit (the unit has length 0);
  - ArcUpdate and ArcPost do nothing with it;
  - on release it starts flying.

  If the weapon fires many times during the stop, each shot takes a slot. Past 256 the oldest is evicted (ArcEnd with
  the visual kept), which is unlikely in a few seconds. **[C]**
- **Side effects:**
  - Arc damage, hops and ArcWeapons notifications stop during the stop. This is wanted, and the mod's existing arc
    damage cancel is then redundant for in-flight arcs.
  - Other users of the arc system are frozen too: 0x13C1416 is a second ArcCreate caller (probably enemy or turret
    arcs).
  - **[C]** / [I]

## Option B (only to show arcs fired during the stop, frozen): run new slots only

The aim is to let an arc created during the stop fly to its first stop and then hold it. In each detour, before calling
the original:

1. For every slot that existed when the stop began, save and clear `sys+0x30+i` (active). Its R is then skipped by both
   functions.
2. Save and zero that slot's V `+0x00` and `+0x34`, so ArcPost skips its V.
3. Call the original.
4. Restore all of it.

When a new slot ends (ArcEnd moves it into V), add it to the held set.

The catches:

- Its hits and hops run during the stop, so damage has to be cancelled separately.
- Its particle effect still does not emit (the particle world is frozen), so only the unit mesh would show.
- The latch applies per slot.

Medium-low confidence and much more code, so it is not recommended unless the unit alone looks right.

## What Option A does not cover: shimmer on a bolt that holds still

Game code sets no material values or randomness on the bolt unit after it spawns. **[C]** If the held bolt still
shimmers with Option A and the ParticleWorld hook both on, the cause is engine-side:

- **The unit's material is animated by time.** The renderer's time for a World comes from World time:
  - exe 0x8A128..0x8A15D: per render frame, `World+0x230878 = World+0x230874 − World+0x23087C`, then
    `+0x23087C` is set to the current time, and both are passed to exe 0x108780;
  - World time `+0x230874` is advanced by the World update (exe 0x1B067F, `time += dt`).

  **[C]** the arithmetic, **[I]** that this is the shader time. Holding the World's time (writing `+0x230874` back each
  frame, or hooking the World update to hold it) would freeze every time-animated material in that World, which suits a
  time stop. But World time may feed other engine systems: **[?]**, not recommended until tested.
- **The unit has its own animation.** It would be advanced by the World update `dt`. No per-unit pause is known. **[?]**

## Recommended probe: Arc Freeze (add to the Shot and Particle Freeze probe; Ctrl+F11; never F5 or F8)

### Setup, once the game's code is ready

1. `game = hf->module_base("game.dll", &size)`.
2. Check each of these, and refuse with a log line if any fails:
   - **ArcUpdate prologue:**
     `hf->code_matches(game + 0x13B4EB0, "4C 8B DC 49 89 5B 18 49 89 73 20 55 41 54 41 55 41 56 41 57 49 8D AB F8 FE FF FF 48 81 EC E0 01")`
   - **ArcUpdate's Clock read and its index array:**
     `hf->code_matches(game + 0x13B4EF5, "48 8B 05 4C 14 F7 01 4C 8D B1 30 51 01 00")`
   - **ArcUpdate's loop step (stride 0x118, 256 slots):**
     `hf->code_matches(game + 0x13B5490, "48 81 C3 18 01 00 00 41 81 FF 00 01 00 00")`
   - **ArcUpdate's step test (a step of 0 or less ends the arc):**
     `hf->code_matches(game + 0x13B5148, "F3 41 0F 59 C5 F3 0F 59 B3 D0 00 00 00 F3 0F 5C F1 F3 0F 5D F0 44 0F 2F D6 0F 82")`
   - **ArcPost prologue:**
     `hf->code_matches(game + 0x13B5510, "48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 41 54 41 55 41 56 41 57 48 8D A8 68 FA FF FF 48")`
   - **ArcPost's linger timer (V+0x24 at `sys+0x11954`):**
     `hf->code_matches(game + 0x13B5621, "F3 42 0F 10 84 23 54 19 01 00 41 0F 2F C7 0F 86")`
   - **ArcUpdate's call site:**
     `hf->code_matches(game + 0x13F7659, "48 8D 8F F8 55 9E 01 E8 4B D8 FB FF")`
   - **ArcPost's call site:**
     `hf->code_matches(game + 0xAB5FD0, "49 8D 8E 18 98 A0 02 E8 34 F5 8F 00")`
   - **The ArcSystem global's store:**
     `hf->code_matches(game + 0x13F2AE9, "48 8D 81 F8 55 9E 01 48 89 05 B9 AA 07 02")`
3. Place both hooks:
   - `hf->hook(mod, game + 0x13B4EB0, arc_update_detour, (void**)&arc_update_orig)`;
   - `hf->hook(mod, game + 0x13B5510, arc_post_detour, (void**)&arc_post_orig)`.

### Logging (game thread, about 4 Hz while frozen, and once on freeze and on release)

- `sys = *(game+0x346D5B0)` and `*(void**)sys` (the World; compare it with the particle probe's Worlds).
- For each `i` with `sys[0x30+i]` set:
  - R start `sys+0x130+0x118·i+0x00` and head `+0x0C`;
  - `+0x34` distance, `+0x31` phase;
  - `+0xDC` effect id and `+0xEC` unit id.
- For each V with `+0x00` or `+0x34` set:
  - start `+0x0C`, end `+0x18`;
  - timer `+0x24`;
  - unit id `+0x34`.
- All of these should be unchanged between log lines while frozen.

### Tests (each a 3 s hold, then release)

1. **Fire the Arc Thrower at an enemy and freeze during the bolt.** Does the bolt stay drawn in place, with no shrink
   and no new hops? Does it continue or end normally on release, and does the hop then go on?
2. **Freeze in the linger window** (just after the hit). Does it hold rather than shrink? After the release it should
   shrink as normal.
3. **Fire during the stop.** Nothing should be drawn. On release the arc should fly from where the muzzle was when
   fired.
4. **Watch a held bolt for shimmer.**
   - If there is none: done.
   - If there is some, test whether the particles or the unit cause it. Turn the ParticleWorld hook off while keeping
     the arc hooks. Then, separately, briefly hide the unit (Lua `Unit.set_unit_visibility`, or skip the bolt unit's
     spawn by zeroing row `+0x50` in a test build).
   - If the unit shimmers, that is the World-time material case above.
5. **Blitzer (`HF_ARC_ARC12_BLITZER`) and an enemy or turret arc, if one is around:** they should behave the same,
   since they share the system.
6. **Freeze and release repeatedly while firing:** check for crashes, stuck arcs or arcs that never end.
7. **Mission end during a stop:** the per-mission clear 0x13B4DE0 is not hooked, so it should still clean up.

## Unknowns

- Which of the two parts (the particle effect or the unit) makes the visible jagged bolt, and whether the unit's
  material animates with World time. **[?]** Tests 4 answers it.
- That an unchanged unit stays drawn with no commands queued each frame. This is the normal retained scene model.
  **[I]**
- That a unit scaled to 0 on Y draws nothing. **[I]**
- What 0x13C1416 creates (a second arc source). **[?]**
- Co-op: the arc system is simulated on each machine; a client freezing it locally would drift from the host. **[I]**
