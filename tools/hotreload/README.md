# HellForge hot-reload socket

Rebuild a code mod's logic and have it reload in the running game, without quitting.

- **Socket**: a small generic mod (`socket/src/socket.c`) that sits in the mods folder in place of the mod's real
  source. HellForge builds it like any mod. You rarely rebuild it.
- **Plug**: the mod's real logic, built by you with zig into a dev folder **outside** `mods/`. The socket watches that
  file and reloads it whenever it changes.
- **Release**: the same plug source built with `-DHR_RELEASE` is a normal standalone HellForge mod (one DLL, no socket).

```
hotreload/
  hotreload_plug.h        plug ABI + release shim (header-only; the socket includes it too)
  socket/src/socket.c     the socket (generic)
  example/plug.c          example plug (kill counter in persistent state, one HUD-style export)
  example/hr_config.h     socket config for the example
  leva/hr_config.h        socket config for Leva
  check.sh                compile check of all of it
```

## Installing the socket for a mod (dev setup)

Do this with the game closed: adding or changing files in `mods/` mid-session switches the mod off.

1. Move the mod's real source out of `mods/`, e.g. `HellForge/mods/<leva>/src/leva.c` → `Z:\HD2HF\dev\leva\leva.c`.
   HellForge builds **every** `.c` under `src/`, so nothing but the socket may stay there.
   A kit built on the kit base (docs/kit-base.md) takes its `src/kitbase/` folder along: the plug includes
   `kitbase/kb_core.h` relative to its own `.c`.
2. Put into `HellForge/mods/<leva>/src/`:
   - `socket.c` (any name works; e.g. `leva.c`),
   - `hotreload_plug.h`,
   - `hr_config.h` (from `leva/hr_config.h`; the plug path and the exports the HUD / Mod Options Menu look up).
   Keep `hellforge-mod.json` as it is: `"dll": "leva.dll"`. The socket must be the DLL the HUD finds by name
   (`GetModuleHandleA("leva.dll")`).
   Optional: `hotreload.ini` in the mod folder overrides the plug path without rebuilding the socket:
   ```ini
   [hotreload]
   plug=Z:\HD2HF\dev\leva\leva_plug.dll
   ```
3. Build the plug once (below) so the file exists when the session starts.
4. Start a session. `mod-<id>.log` shows `[hotreload] socket for yui.leva: plug ...` then `leva 0.3.8 loaded (load #1 ...)`.

## Building a plug

```
python3 -m ziglang cc -target x86_64-windows-gnu -shared -O2 -Wall -Werror -Wno-unused-parameter ^
    -I<HellForge sdk dir> -I<dir with hotreload_plug.h> Z:\HD2HF\dev\leva\leva.c -o Z:\HD2HF\dev\leva\leva_plug.dll
```

Never name the plug like the socket (`leva.dll`): the HUD finds modules by name. The socket checks the file once a
second; once it has stayed unchanged for 0.5 s and nobody holds it open for writing, it copies it to
`leva_plug.hr<pid>-<n>.dll` next to it and loads the copy (so the original stays writable for the next build). Copies
from earlier sessions are deleted at session start. The log says `leva 0.3.9 RELOADED (load #2 ...)`.

**Release** (the mod you ship): the same source with `-DHR_RELEASE`, placed as `src/leva.c` with `hotreload_plug.h`
next to it (a local header in `src/` works, as field-promotions' does). That build has `hellforge_mod_init`, no plug
exports and no socket.

## Writing a plug

```c
#include "hotreload_plug.h"
typedef struct State { uint32_t kills; } State;     /* survives reloads */
static const HfApi* hf; static HfMod* self;
static int  Load(const HfApi* api, HfMod* mod, State* st, int first) { hf = api; self = mod; /* old init body */ return 0; }
static void Unload(const HfApi* api, HfMod* mod, State* st) { /* undo live edits etc. */ }
HR_PLUG("leva", VERSION, State, 1 /* state version */, Load, Unload)
HR_STATE_SIZE(leva_hud_state, LevaHud)              /* per forwarded HUD-struct export */
```

- Use the `api` that `Load` gets, never `HF_API(mod)`: in plug mode it is the socket's proxy table.
- `Load` runs on every load and should do everything `hellforge_mod_init` did. `first` is 1 on the first load.
- Plain statics reset on every reload (it is a fresh DLL). Put what must survive in `State` (zeroed on the first load,
  and again when you bump the state version; growing it keeps the old bytes). `hr_shared(name, size, fallback)` gives
  socket-owned memory for anything another module keeps a pointer to.
- `Unload` runs before the old plug is replaced: undo what the socket cannot (live edits via `set_live`, held
  passives, spawned things, threads, open files). Shot rules (`modify_shots`, `homing`) are stopped by the socket.

## What survives a reload, and what needs a restart

| | On reload |
|---|---|
| `subscribe`, `on_game_thread` | re-pointed at the new plug's listeners (the socket's subscriptions stay). A kind first asked for in a reload is subscribed then: HellForge's watch loop hooks its source later (checked in its loader source), but `HR_PRESUBSCRIBE_MASK` makes it certain |
| `on(HF_EVENT_...)` | load events that already fired (GAME_LOADED, NETWORK_READY, ENTITIES_READY, PROJECTILES_READY) are delivered again right after `Load` returns, so the handlers re-run: keep them idempotent |
| `hook` / `hook_ex` | the socket hooks each function once with a fixed thunk; a reload re-points the thunk; a function the new plug no longer hooks runs its original. The `original` a plug gets is a stable jump through HellForge's live pointer, so `static Fn original_x` code works unchanged. **New hook addresses work during a reload** (HellForge chains hooks any time); hooks are never removed. A changed priority on an existing hook is ignored until a restart. 64 hook slots |
| `add_projectile`, `add_damage`, `add_weapon`, `add_entity`, `add_stratagem`, `add_behaviour`, `add_ability`, `add_difficulty`, `add_stratagem_category`, `register_entity` | recorded on the first load; later loads get the same ids back **in call order**, without HellForge seeing the call. **New, removed or reordered registrations need a restart** (logged; an extra `add_*` returns 0, as HellForge would this late). A changed definition (other base / name) keeps the first load's content until a restart (logged) |
| `set_field`, `copy_field`, `set_tag`, `mount`, `edit_entity` made inside `Load` | replayed like the `add_*` calls (`HR_REPLAY_EDITS 1`, default). With `HR_REPLAY_EDITS 0` they are sent again (re-queued, so changed values apply). Use `set_live` for values you want to tune live |
| `projectile_row` / `damage_row` / `stratagem_row` edits | done again by the re-run PROJECTILES_READY handler |
| HUD / Mod Options Menu exports | the socket exports the names from `HR_EXPORTS` and forwards. A `STATE` export is copied into a socket buffer whose address never changes, so the pointer the Lua HUD caches stays valid. With no plug loaded: the last copy / 0 |
| plug statics, timers, cooldowns, queued work | lost (fresh DLL) unless kept in `State` |
| the socket itself, `hr_config.h`, `hotreload.ini`, `hellforge-mod.json`, data/ | restart (and edit them only with the game closed) |

If a new plug cannot be loaded (not a plug, wrong ABI), the old one keeps running. If its `Load` returns non-zero, no
plug runs until the next build. A crash in a plug crashes the game, as in any mod (the crash note names the
`leva_plug.hr...dll` copy).

## Leva migration checklist (`leva/src/leva.c`)

1. Move `leva.c` to the dev folder; install the socket with `leva/hr_config.h` (`leva_hud_state`, `leva_option_get`,
   `leva_option_set`).
2. `#include "hotreload_plug.h"` after `hellforge_mod.h`.
3. `HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)` →
   `static int Load(const HfApi* api, HfMod* mod, LevaState* st, int first)`. The body stays as it is: `hf = api`
   (now the proxy), the build checks, `LoadSettings()`, both `api->on(...)`, `add_damage` + 2x `add_projectile`, the 7
   `subscribe`s and `on_game_thread(Tick)`. End the file with
   `HR_PLUG("leva", VERSION, LevaState, 1, Load, Unload)`.
4. After `leva_hud_state`: `HR_STATE_SIZE(leva_hud_state, LevaHud)` (or put 156, `sizeof(LevaHud)` v2, in
   `hr_config.h`). `g_hud` can stay a plug static: the socket copies it.
5. Add `Unload`: if `g_inMission`, call `Reset("mission end")`; it undoes `Body`, `ArcLimits` and `Stealth` (the
   `set_live` edits the next plug's statics would no longer know about). Also undo the weapon `set_live` edit around
   line 422 if it is on.
6. Nothing else moves. What happens on reload: `Loaded` re-runs (code checks, the 3 hooks at game.dll+0x129DC30 /
   0x129D180 / 0x129DD30 re-pointed, `g_hooked` set again); `ProjectilesReady` re-runs on the same replayed
   `g_shellCopy` / `g_bolt` / `g_boltDamage` ids; listeners and `Tick` re-pointed; `Tick` sees `g_inMission == 0`
   against a mission and runs `Reset("mission start")` (body, arc limits, packages again). So a mid-mission reload is a
   fresh mission start for Leva: cooldowns, Code, zones and fields start over.
7. Optional, to keep more across reloads: settings the Mod Options Menu changed revert to `leva.ini` (LoadSettings
   re-runs). Keep them in `LevaState` (e.g. `double opt[64]` filled from `kOptions` in `Unload`, written back after
   `LoadSettings()` when `!first`). Likewise `g_now` with the cooldown stamps and `g_code` if you want those to carry over.
8. Adding a projectile/damage, changing its base or name, or reordering the `add_*` calls needs a game restart.
9. Release: build the same file with `-DHR_RELEASE` into the mod folder as `src/leva.c` + `hotreload_plug.h`.

faelynn.c and lenna.c have the same shape (lenna uses ENTITIES_READY instead of PROJECTILES_READY: it re-runs too).

## Config (`hr_config.h`, all optional)

| Define | Default | |
|---|---|---|
| `HR_PLUG_PATH` | `"plug.dll"` | UTF-8; relative = mod folder. `hotreload.ini` `[hotreload] plug=` overrides |
| `HR_EXPORTS(X)` | none | `X(STATE, name, size)`, `X(OPT_GET, name, 0)`, `X(OPT_SET, name, 0)`, `X(INT0, name, 0)`, `X(VOID0, name, 0)`. STATE size 0 = the plug's `HR_STATE_SIZE` (up to `HR_STATE_MAX` 4096) |
| `HR_PRESUBSCRIBE_MASK` | 0 | `(1u << HF_ON_...)` kinds subscribed at init even if the plug does not ask yet |
| `HR_POLL_SECONDS` / `HR_STABLE_SECONDS` | 1.0 / 0.5 | file watch |
| `HR_FREE_OLD_SECONDS` | 0 (never) | free old plug DLLs this long after a reload. The first plug is never freed (HellForge may hold pointers to its static data); others stay loaded by default because another thread may still be inside one of their hooks. Each reload keeps one more copy mapped until the session ends |
| `HR_REPLAY_EDITS` | 1 | see the table above |

`check.sh` compiles the socket (no config, example config, Leva config, each staged as a mod `src/` folder) and the
example plug in both modes.
