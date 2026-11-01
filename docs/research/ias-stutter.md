# HD2Runtime + IAS stutter: findings (2026-10-31)

Research only. Nothing on the PC was changed.

## Bottom line

- **No log shows the stutter.** None of the logs on the PC has frame-time data, perf warnings or repeating rescan/refresh lines with timestamps. So nothing in the logs shows its cadence or its cause. (Details are in "Log evidence".)
- **The code shows no direct conflict.** The two mods don't scan the same tables, don't react to each other's writes, and don't compete for any native hook. HD2Runtime wraps `update` above IAS. IAS does its heavy work in `render`.
- **Two likely mechanisms, both unproven:**
  1. **Stacked frame budget.** IAS has its own periodic spikes: a full `World.units()` scan of the mission world every 2.0 s, a re-query of empty armor profiles every 0.25 s, and a file open every 120 frames. HD2Runtime adds per-frame and periodic work of its own. Neither one alone may push a frame over budget, but the two together can.
  2. **Whole-VM GC policy.** IAS sets `collectgarbage("setpause", 400)` for the whole Lua VM (logged, see below). IAS creates a lot of garbage every frame. HD2Runtime has a large resident heap. So the GC cycles get bigger and spikier.
- **What we can ship.** We can measure IAS without patching it. IAS still reads diagnostic globals that a companion mod can set. After that, the fixes are a GC override and, if the scan spikes are the cause, a small patch to our copy of IAS that slows its scan intervals.

## What each side does every frame

### IAS (Lua pack, extracted to `scratchpad/ias/lua/0_70217982548cf034.lua`, 7905 lines)

- **Hooks both `update` and `render`** (L6509-6520, L7183-7210).
  - The update wrapper is cheap: `restore_pending()`, then the game's update. Preview update is off by default.
  - The render wrapper (`render_frame`, L6529-7180) does all the heavy work before it calls the game's render:
    - `active_avatar_worlds()` (L1381) runs every frame for every `Application.worlds()` world. Each world costs `num_units`, `units_by_resource(avatar)`, 2× `Id64.from_hex` + `units_by_resource` (ship display, officer), and `fp_graphs`.
    - It reads root poses of every avatar.
    - `discover_armour_units` (L1538-1697) runs every frame. It re-queries every profile whose resource is present.
    - Full bone reads, retargeting and native submission via `C.begin_frame/bind_unit/end_frame`, then `pose_batch.run`.
- **Periodic work in IAS:**

  | Interval | What | Lines |
  |---|---|---|
  | 0.25 s per empty profile | `Id64.from_hex` + `World.units_by_resource` for **every** declared profile with no unit present (negative cache). | L1576-1585 |
  | 2.0 s (gameplay world) | **Full `World.units(world)` enumeration plus `debug_name()` on every unit in the mission world.** `debug_name` allocates a closure, a `tostring` and a `string.lower` per unit (L594-596). Only 256 names are memoised. | L1554-1563, L1612-1682 (`negative.next_full_scan=now+2.0` at L1682) |
  | 60 frames | Full scan for non-gameplay (small/preview) worlds; immediate when population changes. | L1459, L1554, L1559 |
  | 120 frames (~2 s at 60 fps) | `io.open(%APPDATA%/hd2_independent_rig_mode.txt)` on the game thread. The file doesn't exist on this PC, so it is a failed open each time. | L2029-2035 |
  | 300 frames | `load_bridge` retry until the native bridge is up. | L601-603 |
  | 1800 frames | Status / PhysBone status formatting via `get_status`. The text goes to the internal `log()`, which writes to disk only for error lines, at most 16. | L7070-7157, L170-185 |

- **Allocation pressure.** There are 126 `try(function() ... end)` sites, and each call allocates a closure. Many of them run per avatar or per unit per frame. On top of that come `pack()` tables and `ffi.new("float[12]")` per binding per frame (L7038).
- **GC policy** (L7212-7305):
  - IAS sets `setpause 400` once for the whole VM.
  - It delegates if `_G.HD2SmoothBoot`, `_G.HD2IRGcPolicyTest` or `_G.HD2IRGcPolicy` already exist. It also delegates if the resources `mods/codex/smoothboot` or `mods/hd2ir/gc_policy_test` exist.
  - It leaves alone any change made later ("later external policy changes are preserved").
- **Native side** (`HD2IndependentRig.addon64`, ReShade addon "HD2IAS", plus `HD2NativeHandoffFull_w64_release.dll`, plus `HD2PreviewRuntime.addon64`):
  - The ReShade panel has: Teammate armor system, Physics (all / player / teammate / corpse), Freeze corpse custom physics, Weapon grip correction, and "Rescan and recover all (F8)". The panel strings say the rescan is manual and bounded to 30 s.
  - The binary imports `SuspendThread`, `GetThreadContext`, `SetThreadContext` and `Thread32First`, and uses a thread pool. I could not tell from strings what it suspends, or when.
  - The UI text says: "Teammate armor system … Enabling it in multiplayer severely reduces frame rates. Brief stutters when teammates join, change armor or respawn are normal." and "High frame rates may cause weapon jitter."

### HD2Runtime 0.50.1 (`scratchpad/ext/hd2rt`)

- **One `update` wrapper** (`runtime/scheduler.lua` L83-93). It ticks every watch each update.
  - **With "Faster application (0.40.0)" OFF, every settled ensure is ticked every frame** (L42; `docs/runtime-performance.md` L394-410).
  - This session logged `settings Faster application (0.40.0) OFF` (`HD2Runtime.log`).
- **Event sources** (`runtime/event_sources.lua`):
  - `game_state` is polled every update with a few guarded reads (L32, `event_world.lua` L239).
  - `stats` and `weapons` run at 10 Hz (L307, L375). `stats` reads the player list and the stat blocks.
  - Logged this session: "event source game_state started", "event source stats started".
- **Peer channel** (`runtime/peer_channel.lua` L46, L271-288): retries every 0.5 s while a value is pending. This session four values from PSS were pending (`psoas`, `pssrfit`, `psoxp`, `psofin`). Every one stayed "post waiting (NOT_JOINED)" because the game is solo, so the retry presumably runs all session.
- **Mouse wheel `WH_GETMESSAGE` hook** (`runtime/mouse_wheel.lua`): installed only while something queries the wheel or input blocking, and removed 1 s after the last query (L32-33, L327-338). **No "native message hook installed" line appears in this session's `HD2Runtime.log`, so it was not active.** Install option "Mouse wheel hook off" turns it off completely (`docs/ui-overlay.md` L243-249).
- **Performance instrumentation** (`perf_watch`, Global view): runs only while F10 > Performance is visible. Global temporarily wraps the update/render chains, IAS's included (`docs/runtime-performance.md` "Global and Runtime views").
- **HD2Runtime never calls `collectgarbage`** (grep over `runtime/`, `core/`: no hits). It never enumerates World units (the only use is `pelican_gatling.lua` L499-503). On HellForge it writes through the HellForge bridge (`runtime/hellforge.lua`).
- Its HUD clients this session were `yui/lenna_hud` (game icons and custom images), `yui/field_promotions_hud` (Veterancy HUD), `codex/ship_support` and PSS. Each game-icon draw re-reads the atlas sprite record (`runtime/image_resources.lua` L495-523).

### Does either one contend with the other?

| Possible contention | Verdict | Evidence |
|---|---|---|
| Both chaining global `update` | Fine | Load order (Bingus, `BingusSharedLoader.log`): `mods/hd2ir/runtime` loads before `mods/skyeshade/hd2runtime`. HD2Runtime wraps IAS's wrapper and passes returns through. HD2Runtime only unhooks if it is still the top (`scheduler.lua` L65). |
| Both chaining `render` | No | HD2Runtime does not wrap `render`. Only the dev `render_probe.lua` and the Global perf view do. |
| Shared unit/entity scans | No | IAS uses engine `World`/`Unit` calls. HD2Runtime uses guarded memory reads and the HellForge bridge. |
| Rescans triggered by the other's writes | No | IAS rescans on avatar-set or population changes in small worlds. HD2Runtime writes stat/weapon fields through HellForge, not units. |
| Extra worlds from HD2Runtime multiplying IAS per-world work | No | Only the dev `render_probe` creates worlds (`engine_gui.script_world`). |
| JIT trace flush / churn | Not in this session | Bingus: "LuaJIT cache: expanded 16384 KB / 8000 traces; flushes 0, growth 0; watcher on". The watcher logs on a flush (`bsl/src/jit_budget.lua` L89-99) and no later line exists. The churn guard logged nothing either. |
| Wheel hook / native input block | Not active this session | No install line in `HD2Runtime.log`. |
| GC: one VM, IAS's pause 400 | **Possible** | `HD2IRGcPolicy.log`: `status=applied_once`, `requested_pause=400`, `previous_pause=200`, `scope=whole Lua VM`. |
| Frame budget | **Possible** | IAS: 2 s full scan, 0.25 s profile re-queries, 120-frame file open. HD2Runtime: per-frame ensure ticks (Faster application OFF), 0.1 s event sources, 0.5 s peer retry. |

## Log evidence (latest session `f1d5cdecaffc4ed39d165884df7c7262`, 2026-10-31 01:28-01:32 local)

- **`mod-lua.hd2runtime.log`** is 0 bytes, and every recent session's copy is 0 bytes too. The real HD2Runtime log is `userdata/environment/Local/CowboyBingus/Helldivers2/Logs/HD2Runtime.log`.
  - It has no `PERFORMANCE:` lines. The grep was across all sessions and the Bingus logs.
  - That is expected: HD2Runtime only writes perf lines while the Performance tab is open.
- **IAS's log** is `userdata/environment/Roaming/hd2_runtime_errors.log`, capped at 16 error lines (L170-185).
  - Its last write was 2026-10-30 11:36 UTC, an earlier session. It holds only "whole-weapon-trial child not matched" and "contact action … tp-hold-variable-unavailable" lines.
  - There is nothing periodic in it.
- **`result.json`**: `engineLogError = IO_SharingViolation_File, …\game\bin\ReShade.log`. **The engine log was not captured** (`engine-logs/` is empty), so no engine frame stats are available. HellForge's engine-log capture appears to collide with ReShade's log file. Worth a separate look.
- **`modloader.log`**: no periodic lines. The session ended with an access violation at `helldivers2.exe+0x626296` on render thread 11820 (`soa-crash.txt`). The stack runs through `nvwgf2umx.dll` and `D3D12Core.dll`. This is a separate issue, and it is not attributed to either mod.
- **ReShade.log**: both addons registered normally ("HD2IAS", "HD2 Preview Runtime", API 20). No warnings or errors.
- **IAS config** `game/bin/HD2IndependentRig/runtime.ini`: `mode=observe`, `appearance_runtime_enabled=1`, `difference_selector_enabled=1`, `difference_realtime_refresh_enabled=1`, `capture_interval_ms=10000`, `capture_copy_stats=0`. What `capture_interval_ms` and `difference_realtime_refresh_enabled` cost is unknown; there is no source for the native side. Leave them alone unless an A/B test points there.

## Existing switches that can turn the expensive parts off

- **HD2Runtime:**
  - F10 > Settings > Interface > **Faster application (0.40.0)**. It is OFF now. When ON, settled ensures sleep instead of ticking every frame. 0.40.0 had reports of the game failing to start, so treat it as an A/B test.
  - Install option **Mouse wheel hook off**. It wasn't active this session, so it is unlikely to matter.
  - Keep F10 > Performance closed while playing, because instrumentation runs only while it is open.
- **IAS (ReShade HD2IAS panel):**
  - Keep **Teammate armor system** off. It is off after every restart.
  - Try **Disable all custom physics** and **Weapon grip correction off** as A/B tests.
  - Leave **Freeze corpse custom physics** on. The panel says disabling it increases corpse cost.
  - Don't press **F8 rescan** mid-mission. It runs bounded steps for up to 30 s.
- **IAS Lua mode file** `%APPDATA%\hd2_independent_rig_mode.txt` (HellForge's APPDATA is `userdata/environment/Roaming`). It accepts `observe`, `roundtrip` or `retarget_test`. It changes only the Lua pose path; discovery and scans still run (L2029-2050). It is not a fix.
- **IAS GC policy:** set one of the marker globals before IAS loads, or reset the pause after it loads (see fix 3).

## Fix proposals, ranked by risk

### 0. Measure first (no changes)

1. Turn on ReShade's frame-time display: overlay > Settings > Show frame time. `ShowFrameTime=0` now in `game/bin/ReShade.ini`.
2. In a mission, open HD2Runtime **F10 > Performance > Global**.
   - Watch the rows for `mods/hd2ir/runtime` (IAS), **HD2Runtime core** and each mod.
   - The Peak column is the largest single update per quarter second.
   - Note whether the spikes line up with a 2 s rhythm (IAS full scan) or a 0.5 s rhythm (HD2Runtime peer retry).
   - Close the tab afterwards. The Global wrappers cost time themselves.
3. Do one A/B pass with HD2Runtime "Faster application" ON, then one with the IAS panel's "Disable all custom physics" ON. Each time, note whether the hitch cadence or size changes.

### 1. Diagnostic companion: no patch to IAS, very low risk

IAS still calls diagnostic globals it sets to nil at load (L45-49). A Lua mod that loads **after** `mods/hd2ir/runtime` can define them:

- `__hd2ir_work_begin()` at render start (L6531).
- `__hd2ir_work_mark(n)` for phases 1-6 (L6922, 6972, 6976, 7051, 7063, 7169). Mark 6 is just before the game's own render.
- `__hd2ir_work_finish()` (L7177).
- `__hd2ir_scan_meter(tag, n)`. Tags: `resource_begin`, `resource_queries`, `full_scans`, `world_units_read` with the unit count, `resource_end`, `snapshot_*` (L1539-1695, L2775-2788).

Either of these is a valid host, since both load after IAS: our own Bingus entry (`yui/lenna_hud` or `yui/field_promotions_hud`), or a new one-file Bingus addon built with `ext/bsl/scripts/build_addon.py`.

The companion should log only hitch frames. Sketch:

```lua
local ffi=require('ffi')
pcall(ffi.cdef,'int QueryPerformanceCounter(int64_t*); int QueryPerformanceFrequency(int64_t*);')
local q=ffi.new('int64_t[1]'); ffi.C.QueryPerformanceFrequency(q); local f=tonumber(q[0])
local function now() ffi.C.QueryPerformanceCounter(q); return tonumber(q[0])/f end
local t0, marks, scan = 0, {}, nil
rawset(_G,'__hd2ir_work_begin', function() t0=now(); scan=nil end)
rawset(_G,'__hd2ir_work_mark', function(n) marks[n]=now() end)
rawset(_G,'__hd2ir_scan_meter', function(tag,n) if tag=='full_scans' then scan=0 elseif tag=='world_units_read' then scan=n end end)
rawset(_G,'__hd2ir_work_finish', function()
  local pre=(marks[6] or now())-t0          -- IAS's own pre-render work
  if pre>0.004 or scan then                 -- log >4 ms or any full scan
    -- write: os.clock(), pre*1000, scan, collectgarbage('count')
  end
end)
```

Also wrap `update` from the same companion. It then sits on top of HD2Runtime, so it times the whole update chain. Log any update over 8 ms with `collectgarbage('count')` before and after.

Read the log like this:

- **Hitches that line up with `full_scans` lines:** IAS's 2 s scan is the cause. Go to fix 4.
- **Hitches with a large heap drop:** GC is the cause. Go to fix 3.
- **Hitches where only the update side is large:** look at HD2Runtime's Performance tab.

### 2. Config-only (zero code, reversible)

- HD2Runtime **Faster application ON**. This is the only HD2Runtime switch that cuts per-frame work, because sleeping ensures skip their tick.
- IAS panel: **Teammate armor system** off, **Freeze corpse custom physics** on.

### 3. GC override companion (low risk, only if fix 1 shows GC spikes)

IAS respects changes made after its own one-shot setter. From any Lua mod that loads after IAS:

```lua
collectgarbage('setpause', 200)   -- LuaJIT default; IAS set 400
-- optional, if spikes remain: collectgarbage('setstepmul', 400)  -- more, smaller incremental steps
```

Or, from a mod that loads **before** IAS, `rawset(_G,'HD2IRGcPolicy',{status='delegated'})`. IAS then skips its setter (L7217). That needs a load slot ahead of `mods/hd2ir/runtime`, which is currently the first Bingus entry, so the after-load reset is easier.

**Guess:** this only helps if the hitches are GC atomic or sweep spikes. Pause 400 lowers the average GC cost but lets the heap grow to 4× the live size. Measure before shipping it.

### 4. Patch our own copy of the IAS Lua (medium risk; only if fix 1 ties the hitches to IAS scans)

We rebuild the patch with `ext/bsl/scripts/archive.py` `make_archive`. The resource key is `0x70217982548cf034`, the archive is `9ba626afa44a3aa3.patch_0`, and the envelope is the first 8 bytes before `-- HD2-Addon:`.

- L1682 `negative.next_full_scan=now+2.0` → `now+10.0`. This makes the gameplay-world full `World.units()` + `debug_name` sweep 5× rarer.
  - Risk: IAS's comment says the sweep is "a two-second audit [that] catches silent index omissions".
  - Indexed armor is still re-queried every frame, and partial or missing counts still force an immediate scan (L1599). Only silent misses get slower to recover.
- L1584 `now+.25` → `now+1.0`. This means 4× fewer `units_by_resource` queries for empty profiles. Risk: armor that appears late is picked up up to 1 s later.
- L2030 `< 120` → `< 1800`. Fewer failed `io.open` calls on the game thread. Negligible risk.

Do not touch the `identity` string on L3, the handoff relay, or anything native. The native addon checks component versions (panel: "component versions differ").

Every Arsenal reinstall of IAS 1.4.6 overwrites the patch. Ship it as a separate pack that replaces `mods/hd2ir/runtime`, and tell the author.

### Not recommended

- Don't change `game/bin/HD2IndependentRig/runtime.ini` capture or refresh keys blind. Their native meaning is unknown.
- Don't patch HD2Runtime for this. Nothing in its code was shown to interact with IAS. Its own levers are the Faster application setting and its Performance tab.

## What the user must do in game (short)

1. Turn on ReShade **Show frame time**. Go into a mission.
2. Open **F10 > Performance > Global** for about 30 s. Note which row spikes and whether the rhythm is about 2 s or about 0.5 s. Close the tab.
3. Turn **Faster application ON** (F10 > Settings > Interface). Restart, then re-check the frame-time graph.
4. If the hitch rhythm is still there, install the diagnostic companion (fix 1) and send `HD2IR-probe.log`, or whatever name the companion uses.
