/* hotreload_plug.h: the ABI between the HellForge hot-reload socket (socket/src/socket.c) and a plug (a mod's real
 * logic, built as its own DLL outside the mods folder), plus the shim that compiles the same plug source as a normal
 * standalone HellForge mod for release (-DHR_RELEASE).
 *
 * A plug is written like a normal mod whose hellforge_mod_init became a Load function:
 *
 *     #include "hotreload_plug.h"
 *     typedef struct State { uint32_t kills; } State;     // survives reloads (zeroed on the first load)
 *     static const HfApi* hf; static HfMod* self;
 *     static int  Load(const HfApi* api, HfMod* mod, State* st, int first) { hf = api; self = mod; ...; return 0; }
 *     static void Unload(const HfApi* api, HfMod* mod, State* st) { ... }
 *     HR_PLUG("my-mod", "0.1.0", State, 1, Load, Unload)
 *
 * Rules for Load (they are what make one source work both ways):
 *   - Use the `api` it gets (keep it in a static), never HF_API(mod): in plug mode `api` is the socket's proxy table,
 *     whose on / subscribe / on_game_thread / hook / hook_ex / add_* / register_entity / edits go through the socket.
 *   - Do everything hellforge_mod_init did, every time: registrations (add_projectile...) return the ids of the first
 *     load on later loads, subscriptions and hooks are re-pointed at the new code, load events that already fired
 *     (HF_EVENT_GAME_LOADED, HF_EVENT_PROJECTILES_READY...) are delivered again right after Load returns.
 *   - `first` is 1 on the session's first load (always 1 in release mode).
 *   - Exported functions the HUD / Mod Options Menu call (leva_hud_state, leva_option_get/set) stay HF_MOD_EXPORT in
 *     the plug; the socket exports the same names and forwards to them.
 */
#ifndef HOTRELOAD_PLUG_H
#define HOTRELOAD_PLUG_H

#include <stddef.h>
#include <stdint.h>
#include "hellforge_mod.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HR_ABI_VERSION 1

/* What a plug says about itself (hr_plug_info). Read on every load, before the old plug is unloaded. */
typedef struct HrPlugInfo
{
    uint32_t abi;            /* HR_ABI_VERSION */
    uint32_t size;           /* sizeof(HrPlugInfo) */
    const char* name;
    const char* version;     /* logged on every reload */
    uint32_t state_size;     /* bytes of persistent state the plug wants (sizeof its State) */
    uint32_t state_version;  /* bump it when State's layout changes: the socket zeroes the block when it differs */
} HrPlugInfo;

/* What the socket hands a plug on load and unload. Valid for the session (the socket owns it). */
typedef struct HrHost
{
    uint32_t abi;            /* HR_ABI_VERSION */
    uint32_t size;           /* sizeof(HrHost) */
    const HfApi* api;        /* the socket's proxy table: use this one */
    const HfApi* real_api;   /* HellForge's own table (bypasses the socket: nothing it registers survives a reload) */
    HfMod* mod;              /* the socket mod's HfMod */
    void* state;             /* persistent block, at least info.state_size bytes, the same pointer across reloads */
    uint32_t state_size;
    uint32_t state_fresh;    /* 1: the block was zeroed for this load (first load, or state_version changed) */
    uint32_t generation;     /* 1 on the first successful load, +1 per reload */
    uint32_t first;          /* 1: registrations go to HellForge (the first load); 0: they replay the first load's results */
    uint32_t fired;          /* bit (1u << HfEvent) for each load event (HF_EVENT_...) that has fired */
    uint32_t reserved;
    /* Socket-owned memory by name, zeroed when first asked for, the same pointer for the session (for anything
     * another module keeps a pointer to). NULL if out of room. */
    void* (*shared)(const char* name, size_t size);
    const char* plug_path;   /* the plug file the socket watches (UTF-8) */
} HrHost;

typedef const HrPlugInfo* (*HrPlugInfoFn)(void);
typedef int (*HrPlugLoadFn)(const HrHost* host);      /* 0: loaded */
typedef void (*HrPlugUnloadFn)(const HrHost* host);

#define HR_PLUG_INFO_SYMBOL "hr_plug_info"
#define HR_PLUG_LOAD_SYMBOL "hr_plug_load"
#define HR_PLUG_UNLOAD_SYMBOL "hr_plug_unload"
/* Optional per forwarded state export: size_t hr_size_<export>(void), the bytes the socket copies (HR_STATE_SIZE). */
#define HR_SIZE_PREFIX "hr_size_"

/* ---- plug side ------------------------------------------------------------------------------------------------- */
#ifndef HR_SOCKET

/* The host of this load (NULL in release mode, and before the first load). */
static const HrHost* hr_host;

/* 1 when built with -DHR_RELEASE (a normal standalone HellForge mod). */
#ifdef HR_RELEASE
#define HR_IS_RELEASE 1
#else
#define HR_IS_RELEASE 0
#endif

/* Socket memory for a pointer another module keeps (plug mode), or `fallback` (release mode / no socket). */
static inline void* hr_shared(const char* name, size_t size, void* fallback)
{
    void* p = hr_host && hr_host->shared ? hr_host->shared(name, size) : NULL;
    return p ? p : fallback;
}

#ifdef HR_RELEASE

/* Release: a normal mod. hellforge_mod_init runs Load once with a static, zeroed State; Unload is never called. */
#define HR_PLUG(NAME, VERSION, STATE_T, STATE_VERSION, LOAD, UNLOAD)                                              \
    static STATE_T hr_state_storage_;                                                                             \
    HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)                                            \
    {                                                                                                             \
        (void)(UNLOAD);                                                                                           \
        return (LOAD)(api, mod, &hr_state_storage_, 1);                                                           \
    }
#define HR_STATE_SIZE(EXPORT, TYPE)

#else

/* Plug: the three entry points the socket looks up. */
#define HR_PLUG(NAME, VERSION, STATE_T, STATE_VERSION, LOAD, UNLOAD)                                              \
    HF_MOD_EXPORT const HrPlugInfo* hr_plug_info(void)                                                            \
    {                                                                                                             \
        static const HrPlugInfo info = {HR_ABI_VERSION, sizeof(HrPlugInfo), (NAME), (VERSION),                    \
                                        (uint32_t)sizeof(STATE_T), (uint32_t)(STATE_VERSION)};                    \
        return &info;                                                                                             \
    }                                                                                                             \
    HF_MOD_EXPORT int hr_plug_load(const HrHost* host)                                                            \
    {                                                                                                             \
        hr_host = host;                                                                                           \
        return (LOAD)(host->api, host->mod, (STATE_T*)host->state, (int)host->first);                             \
    }                                                                                                             \
    HF_MOD_EXPORT void hr_plug_unload(const HrHost* host)                                                         \
    {                                                                                                             \
        (UNLOAD)(host->api, host->mod, (STATE_T*)host->state);                                                    \
        hr_host = NULL;                                                                                           \
    }
/* Tells the socket how many bytes of a forwarded state export (a HUD struct) to copy: HR_STATE_SIZE(leva_hud_state,
 * LevaHud). Optional when the socket's config names the size. Nothing in release mode. */
#define HR_STATE_SIZE(EXPORT, TYPE)                                                                               \
    HF_MOD_EXPORT size_t hr_size_##EXPORT(void) { return sizeof(TYPE); }

#endif /* HR_RELEASE */
#endif /* !HR_SOCKET */

#ifdef __cplusplus
}
#endif

#endif
