/* HellForge hot-reload socket: a generic host mod that loads a mod's real logic (the "plug", a DLL built outside the
 * mods folder) and reloads it in the running game whenever the plug file is rebuilt. Same source for every mod; the
 * per-mod part is the config below (or hr_config.h next to this file, or hotreload.ini in the mod folder).
 *
 * What the socket owns, so it survives a plug reload:
 *   - event subscriptions (subscribe, on, on_game_thread): made once with HellForge, forwarded to the current plug;
 *   - game hooks (hook, hook_ex): installed once per function through a fixed thunk; a reload re-points the thunk at
 *     the new plug's detour; with no detour it jumps straight to the original. The "original" a plug gets is a second
 *     thunk that always jumps through HellForge's live original pointer, so a plug may keep it in a static;
 *   - startup-only registrations (add_projectile, add_damage, register_entity, ... and the deferred edits): recorded on
 *     the first load, and on later loads answered from the record in call order (not sent to HellForge again);
 *   - exports the HUD (LuaJIT ffi) and the Mod Options Menu look up by name: the socket exports them and forwards; a
 *     state export (a HUD struct) is copied into a socket buffer so the pointer the HUD keeps stays valid;
 *   - a persistent state block for the plug (HrHost.state) and named shared memory (HrHost.shared).
 * Reloads run on the game thread, in the socket's on_game_thread callback, never mid-event. Old plug DLLs are not freed
 * (by default): another thread may still be inside one of its hooks, and HellForge may keep pointers to the first
 * plug's static data. Each load runs from a unique copy of the plug file, so the file itself stays writable. */
#define HR_SOCKET 1
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "hellforge_mod.h"
#include "hotreload_plug.h"

/* ---- config ------------------------------------------------------------------------------------------------------ */
#if defined(__has_include)
#if __has_include("hr_config.h")
#include "hr_config.h"
#endif
#endif

/* The plug DLL (UTF-8). Relative paths are taken from the mod folder. hotreload.ini in the mod folder overrides it:
 *     [hotreload]
 *     plug=Z:\HD2HF\dev\leva\leva_plug.dll */
#ifndef HR_PLUG_PATH
#define HR_PLUG_PATH "plug.dll"
#endif
/* How often the file is checked, and how long it must stay unchanged before it is loaded (seconds). */
#ifndef HR_POLL_SECONDS
#define HR_POLL_SECONDS 1.0
#endif
#ifndef HR_STABLE_SECONDS
#define HR_STABLE_SECONDS 0.5
#endif
/* Event kinds (bit 1u << HF_ON_...) to subscribe at init even if the first plug does not ask for them, so a later
 * plug can start listening without a restart. Each kind hooks its source in HellForge, so keep it to what you use. */
#ifndef HR_PRESUBSCRIBE_MASK
#define HR_PRESUBSCRIBE_MASK 0u
#endif
/* Seconds after a reload before an old plug DLL is freed (and its copy deleted); 0: never (the safe default). The
 * first plug is never freed. */
#ifndef HR_FREE_OLD_SECONDS
#define HR_FREE_OLD_SECONDS 0
#endif
/* 1: deferred edits (set_field, copy_field, set_tag, mount, edit_entity) made during a reload's Load are answered from
 * the first load's record, as add_* are. 0: they go to HellForge again (re-queued, so changed values apply). */
#ifndef HR_REPLAY_EDITS
#define HR_REPLAY_EDITS 1
#endif
/* Exports forwarded to the plug, as X(kind, name, size):
 *   STATE    void* name(void)                       the plug's pointer; `size` bytes (0: the plug's hr_size_<name>)
 *                                                   are copied into a socket buffer, whose address is returned
 *   OPT_GET  int name(const char*, double*)         0 when no plug is loaded
 *   OPT_SET  int name(const char*, double)          0 when no plug is loaded
 *   INT0     int name(void)                         0 when no plug is loaded
 *   VOID0    void name(void)
 * e.g. #define HR_EXPORTS(X) X(STATE, leva_hud_state, 0) X(OPT_GET, leva_option_get, 0) X(OPT_SET, leva_option_set, 0) */
#ifndef HR_EXPORTS
#define HR_EXPORTS(X)
#endif
#ifndef HR_STATE_MAX
#define HR_STATE_MAX 4096
#endif

#define HR_MAX_HOOKS 64
#define HR_MAX_SUBS 48
#define HR_MAX_LISTENERS 16
#define HR_MAX_ON 32
#define HR_MAX_TICKS 16
#define HR_MAX_RECORDS 1024
#define HR_MAX_SHOTS 64
#define HR_MAX_SHARED 16
#define HR_MAX_OLD 256
#define HR_STATE_RESERVE (64u * 1024u)

/* ---- globals ----------------------------------------------------------------------------------------------------- */
static const HfApi* g_real;
static HfMod* g_mod;
static HfApi g_proxy;
static CRITICAL_SECTION g_lock;
static double g_now;
static int g_inInit;

static WCHAR g_plugPath[MAX_PATH * 2];
static char g_plugPathUtf8[MAX_PATH * 3];
static WCHAR g_plugDir[MAX_PATH * 2], g_plugStem[MAX_PATH];

typedef struct Plug
{
    HMODULE module;
    WCHAR copy[MAX_PATH * 2];
    const HrPlugInfo* info;
    HrPlugLoadFn load;
    HrPlugUnloadFn unload;
    uint32_t generation;
} Plug;
static Plug g_plug;                       /* the current plug (module NULL: none) */
static volatile LONG g_exportGen;         /* bumped on every plug change: the export forwarders re-resolve */
static volatile HMODULE g_exportModule;   /* the module the forwarders call into */
static uint32_t g_generation, g_copies;
static int g_recorded;                    /* the first load's registrations are on record */
static HrHost g_host;

static struct { HMODULE module; WCHAR copy[MAX_PATH * 2]; double freeAt; } g_old[HR_MAX_OLD];
static int g_nold;

static unsigned char* g_state;
static uint32_t g_stateCap, g_stateSize, g_stateVersion;
static int g_stateSet;

static struct { char name[64]; void* p; size_t size; } g_shared[HR_MAX_SHARED];

/* the file watch */
static uint64_t g_loadedStamp, g_loadedSize, g_pendStamp, g_pendSize;
static double g_pendSince, g_nextPoll;
static int g_pending;

static void Log(const char* fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (g_real && g_real->log) g_real->log(g_mod, "[hotreload] %s", line);
}

#define HAS(api, f) ((api)->size >= offsetof(HfApi, f) + sizeof((api)->f) && (api)->f)

/* ---- hooks: fixed thunks ------------------------------------------------------------------------------------------
 * hr_slots[i] = {detour, original}. HellForge gets hr_entry_i as the detour and &hr_slots[i].original as the original
 * to keep up to date. hr_entry_i jumps to the plug's detour, or to the original when none is set. The plug gets
 * hr_orig_i as its original: a jump through hr_slots[i].original, so it always follows HellForge's chain. Both are tail
 * jumps (no stack, every argument register untouched), so they fit any function signature. */
typedef struct HrSlot { void* volatile detour; void* volatile original; } HrSlot;
__attribute__((used)) HrSlot hr_slots[HR_MAX_HOOKS];
static struct { uintptr_t target; int claimed; } g_hook[HR_MAX_HOOKS];
static int g_nhooks;

#define HR_REPEAT64(X)                                                                                                 \
    X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12) X(13) X(14) X(15) X(16) X(17) X(18) X(19)      \
    X(20) X(21) X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31) X(32) X(33) X(34) X(35) X(36) X(37)       \
    X(38) X(39) X(40) X(41) X(42) X(43) X(44) X(45) X(46) X(47) X(48) X(49) X(50) X(51) X(52) X(53) X(54) X(55)       \
    X(56) X(57) X(58) X(59) X(60) X(61) X(62) X(63)

#define HR_THUNK_ASM(i)                                                                                                \
    ".p2align 4\n"                                                                                                     \
    "hr_entry_" #i ":\n"                                                                                               \
    "    movq hr_slots+16*" #i "(%rip), %rax\n"                                                                        \
    "    testq %rax, %rax\n"                                                                                           \
    "    jz 1f\n"                                                                                                      \
    "    jmpq *%rax\n"                                                                                                 \
    "1:  jmpq *hr_slots+16*" #i "+8(%rip)\n"                                                                           \
    ".p2align 4\n"                                                                                                     \
    "hr_orig_" #i ":\n"                                                                                                \
    "    jmpq *hr_slots+16*" #i "+8(%rip)\n"
#define HR_THUNK_GLOBL(i) ".globl hr_entry_" #i "\n.globl hr_orig_" #i "\n"
__asm__(".text\n" HR_REPEAT64(HR_THUNK_GLOBL) HR_REPEAT64(HR_THUNK_ASM));
#define HR_THUNK_DECL(i) extern void hr_entry_##i(void); extern void hr_orig_##i(void);
HR_REPEAT64(HR_THUNK_DECL)
#define HR_ENTRY_PTR(i) (void*)hr_entry_##i,
#define HR_ORIG_PTR(i) (void*)hr_orig_##i,
static void* const g_entry[HR_MAX_HOOKS] = {HR_REPEAT64(HR_ENTRY_PTR)};
static void* const g_orig[HR_MAX_HOOKS] = {HR_REPEAT64(HR_ORIG_PTR)};
_Static_assert(sizeof(HrSlot) == 16, "the thunks address 16-byte slots");

static int HookImpl(HfMod* mod, uintptr_t target, void* detour, void** original, int32_t priority, int ex)
{
    if (!target || !detour) return 1;
    for (int i = 0; i < g_nhooks; i++)
        if (g_hook[i].target == target && !g_hook[i].claimed)
        {
            g_hook[i].claimed = 1;
            hr_slots[i].detour = detour;
            if (original) *original = g_orig[i];
            return 0;
        }
    if (g_nhooks >= HR_MAX_HOOKS) { Log("hook 0x%llx refused: all %d hook slots are used", (unsigned long long)target, HR_MAX_HOOKS); return 1; }
    int i = g_nhooks;
    hr_slots[i].original = NULL;
    hr_slots[i].detour = detour;
    int r = ex ? g_real->hook_ex(mod, target, g_entry[i], (void**)&hr_slots[i].original, priority)
               : g_real->hook(mod, target, g_entry[i], (void**)&hr_slots[i].original);
    if (r) { hr_slots[i].detour = NULL; return r; }
    g_hook[i].target = target;
    g_hook[i].claimed = 1;
    g_nhooks++;
    if (original) *original = g_orig[i];
    if (!g_inInit && g_generation > 1) Log("hook 0x%llx placed during a reload (slot %d)", (unsigned long long)target, i);
    return 0;
}
static int P_hook(HfMod* mod, uintptr_t target, void* detour, void** original) { return HookImpl(mod, target, detour, original, 0, 0); }
static int P_hook_ex(HfMod* mod, uintptr_t target, void* detour, void** original, int32_t priority) { return HookImpl(mod, target, detour, original, priority, 1); }

/* ---- game events (subscribe) ------------------------------------------------------------------------------------- */
typedef struct HrSub
{
    uint32_t kind;
    int32_t priority;
    int n;
    struct { HfListener fn; void* user; } l[HR_MAX_LISTENERS];
} HrSub;
static HrSub g_sub[HR_MAX_SUBS];
static int g_nsubs;

static void SubForward(HfMod* mod, const HfEventInfo* event, void* user)
{
    HrSub* s = (HrSub*)user;
    for (int i = 0; i < s->n; i++) s->l[i].fn(mod, event, s->l[i].user);
}

static HrSub* SubFor(HfMod* mod, uint32_t kind, int32_t priority)
{
    for (int i = 0; i < g_nsubs; i++)
        if (g_sub[i].kind == kind && g_sub[i].priority == priority) return &g_sub[i];
    if (g_nsubs >= HR_MAX_SUBS) { Log("subscribe %u refused: out of subscription slots", kind); return NULL; }
    HrSub* s = &g_sub[g_nsubs];
    memset(s, 0, sizeof *s);
    s->kind = kind;
    s->priority = priority;
    if (!g_real->subscribe(mod, kind, SubForward, s, priority))
    {
        Log("subscribe to event kind %u (priority %d) refused by HellForge%s", kind, (int)priority,
            g_inInit ? "" : ": new event kinds may need a restart (or HR_PRESUBSCRIBE_MASK)");
        return NULL;
    }
    if (!g_inInit) Log("subscribed to event kind %u during a reload", kind);
    g_nsubs++;
    return s;
}

static int P_subscribe(HfMod* mod, uint32_t kind, HfListener listener, void* user, int32_t priority)
{
    if (!listener) return 0;
    HrSub* s = SubFor(mod, kind, priority);
    if (!s) return 0;
    for (int i = 0; i < s->n; i++)
        if (s->l[i].fn == listener && s->l[i].user == user) return 1;
    if (s->n >= HR_MAX_LISTENERS) { Log("subscribe %u: too many listeners", kind); return 0; }
    s->l[s->n].fn = listener;
    s->l[s->n].user = user;
    s->n++;
    return 1;
}

/* ---- load events (on) -------------------------------------------------------------------------------------------- */
static struct
{
    int registered;
    int n;
    struct { HfEventCallback fn; void* user; } cb[HR_MAX_LISTENERS];
} g_on[HR_MAX_ON];
static volatile LONG g_fired;  /* bit per HfEvent */
static struct { uint32_t event; HfEventCallback fn; void* user; } g_onQueue[HR_MAX_ON * 4];
static int g_nOnQueue;

typedef enum { PH_RUN, PH_RECORD, PH_REPLAY } Phase;
static Phase g_phase = PH_RUN;
static int g_cursor;

static void OnForward(HfMod* mod, HfEvent event, void* user)
{
    uint32_t e = (uint32_t)(uintptr_t)user;
    EnterCriticalSection(&g_lock);
    InterlockedOr(&g_fired, (LONG)(1u << e));
    Phase saved = g_phase;
    g_phase = PH_RUN;  /* what a load-event handler does is never a startup registration to record or replay */
    for (int i = 0; i < g_on[e].n; i++) g_on[e].cb[i].fn(mod, event, g_on[e].cb[i].user);
    g_phase = saved;
    LeaveCriticalSection(&g_lock);
}

static int P_on(HfMod* mod, HfEvent event, HfEventCallback callback, void* user)
{
    uint32_t e = (uint32_t)event;
    if (e >= HR_MAX_ON || !callback) return 1;
    EnterCriticalSection(&g_lock);
    int fired = (g_fired >> e) & 1;
    if (g_on[e].n < HR_MAX_LISTENERS)
    {
        g_on[e].cb[g_on[e].n].fn = callback;
        g_on[e].cb[g_on[e].n].user = user;
        g_on[e].n++;
    }
    if (fired)
    {
        /* HellForge calls back at once for an event that already happened: during a plug's Load, after it returns */
        if (g_phase != PH_RUN && g_nOnQueue < (int)(sizeof g_onQueue / sizeof g_onQueue[0]))
        {
            g_onQueue[g_nOnQueue].event = e;
            g_onQueue[g_nOnQueue].fn = callback;
            g_onQueue[g_nOnQueue].user = user;
            g_nOnQueue++;
        }
        else if (g_phase == PH_RUN) callback(mod, event, user);
    }
    int r = 0;
    if (!g_on[e].registered)
    {
        g_on[e].registered = 1;
        r = g_real->on(mod, event, OnForward, (void*)(uintptr_t)e);
        if (r) { g_on[e].registered = 0; Log("on(%u) refused by HellForge", e); }
    }
    LeaveCriticalSection(&g_lock);
    return r;
}

/* ---- the game thread --------------------------------------------------------------------------------------------- */
static struct { HfTickCallback fn; void* user; } g_tick[HR_MAX_TICKS];
static int g_nticks;
static int P_on_game_thread(HfMod* mod, HfTickCallback callback, void* user)
{
    if (!callback) return 0;
    for (int i = 0; i < g_nticks; i++)
        if (g_tick[i].fn == callback && g_tick[i].user == user) return 1;
    if (g_nticks >= HR_MAX_TICKS) return 0;
    g_tick[g_nticks].fn = callback;
    g_tick[g_nticks].user = user;
    g_nticks++;
    return 1;
}

/* ---- startup registrations: record on the first load, replay after ---------------------------------------------- */
enum
{
    R_WEAPON = 1, R_PROJECTILE, R_DAMAGE, R_STRATAGEM, R_ENTITY, R_BEHAVIOUR, R_DIFFICULTY, R_REGISTER_ENTITY,
    R_ABILITY, R_STRATAGEM_CATEGORY, R_EDIT_ENTITY, R_COPY_FIELD, R_SET_FIELD, R_SET_TAG, R_MOUNT
};
static const char* const kRecName[] = {"", "add_weapon", "add_projectile", "add_damage", "add_stratagem", "add_entity",
    "add_behaviour", "add_difficulty", "register_entity", "add_ability", "add_stratagem_category", "edit_entity",
    "copy_field", "set_field", "set_tag", "mount"};
static struct { uint16_t fn; uint64_t key, result; } g_rec[HR_MAX_RECORDS];
static int g_nrec, g_recWarned;

static uint64_t Fnv(uint64_t h, const void* data, size_t n)
{
    const unsigned char* p = (const unsigned char*)data;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001B3ull; }
    return h;
}
static uint64_t FnvStr(uint64_t h, const char* s) { return s ? Fnv(h, s, strlen(s)) : Fnv(h, "\0", 1); }
#define FNV0 0xCBF29CE484222325ull

static int IsEdit(int fn) { return fn >= R_EDIT_ENTITY; }

/* 1: answered from the record (*out); 0: make the real call (then Record). */
static int Replay(int fn, uint64_t key, uint64_t* out)
{
    if (g_phase == PH_RUN || g_phase == PH_RECORD) return 0;
    if (IsEdit(fn) && !HR_REPLAY_EDITS) return 0;
    if (g_cursor < g_nrec && g_rec[g_cursor].fn == fn)
    {
        if (g_rec[g_cursor].key != key)
            Log("%s #%d changed since the first load: it keeps the first load's result until the game restarts", kRecName[fn], g_cursor);
        *out = g_rec[g_cursor++].result;
        return 1;
    }
    if (!g_recWarned++)
        Log("the startup registrations differ from the first load's at #%d (%s): new or reordered registrations need a game restart",
            g_cursor, kRecName[fn]);
    if (IsEdit(fn)) return 0;  /* edits are valid later too: let HellForge take it */
    *out = 0;                  /* add_* outside hellforge_mod_init: refused, as HellForge would */
    return 1;
}
static void Record(int fn, uint64_t key, uint64_t result)
{
    if (g_phase != PH_RECORD) return;
    if (g_nrec >= HR_MAX_RECORDS) { Log("registration record is full: later ones will not replay"); return; }
    g_rec[g_nrec].fn = (uint16_t)fn;
    g_rec[g_nrec].key = key;
    g_rec[g_nrec].result = result;
    g_nrec++;
}

#define HR_REG(RT, NAME, FN, KEY, PARAMS, ARGS)                                                                        \
    static RT P_##NAME PARAMS                                                                                          \
    {                                                                                                                  \
        uint64_t key = (KEY), r;                                                                                       \
        if (Replay(FN, key, &r)) return (RT)r;                                                                         \
        RT v = g_real->NAME ARGS;                                                                                      \
        Record(FN, key, (uint64_t)v);                                                                                  \
        return v;                                                                                                      \
    }
static uint64_t FieldKey(uint64_t h, const HfField* f)
{
    if (!f) return h;
    h = FnvStr(h, f->component);
    return Fnv(h, &f->offset, sizeof f->offset);
}
HR_REG(uint32_t, add_weapon, R_WEAPON, w ? FnvStr(FNV0, w->unit) : 0, (HfMod * m, const HfWeapon* w), (m, w))
HR_REG(uint32_t, add_projectile, R_PROJECTILE, p ? FnvStr(Fnv(FNV0, &p->base, 4), p->name) : 0, (HfMod * m, const HfProjectile* p), (m, p))
HR_REG(uint32_t, add_damage, R_DAMAGE, d ? FnvStr(Fnv(FNV0, &d->base, 4), d->name) : 0, (HfMod * m, const HfDamage* d), (m, d))
HR_REG(uint32_t, add_stratagem, R_STRATAGEM, s ? FnvStr(FNV0, s->name) : 0, (HfMod * m, const HfStratagem* s), (m, s))
HR_REG(uint64_t, add_entity, R_ENTITY, e ? FnvStr(FNV0, e->name) : 0, (HfMod * m, const HfEntity* e), (m, e))
HR_REG(uint32_t, add_behaviour, R_BEHAVIOUR, b ? Fnv(FNV0, &b->base, 4) : 0, (HfMod * m, const HfBehaviour* b), (m, b))
HR_REG(uint32_t, add_difficulty, R_DIFFICULTY, d ? FnvStr(FNV0, d->name) : 0, (HfMod * m, const HfDifficulty* d), (m, d))
HR_REG(HfRef, register_entity, R_REGISTER_ENTITY, e ? FnvStr(FNV0, e->name) : 0, (HfMod * m, const HfEntityDef* e), (m, e))
HR_REG(uint32_t, add_ability, R_ABILITY, a ? Fnv(FNV0, &a->base, 4) : 0, (HfMod * m, const HfAbility* a), (m, a))
HR_REG(uint32_t, add_stratagem_category, R_STRATAGEM_CATEGORY, c ? FnvStr(FNV0, c->name) : 0, (HfMod * m, const HfStratagemCategory* c), (m, c))
HR_REG(int, edit_entity, R_EDIT_ENTITY, e ? Fnv(FNV0, &e->entity, 8) : 0, (HfMod * m, const HfEditDef* e), (m, e))
HR_REG(int, copy_field, R_COPY_FIELD, FieldKey(Fnv(Fnv(FNV0, &entity, 8), &from, 8), field), (HfMod * m, HfRef entity, HfRef from, const HfField* field), (m, entity, from, field))
HR_REG(int, set_field, R_SET_FIELD, FieldKey(Fnv(FNV0, &entity, 8), field), (HfMod * m, HfRef entity, const HfField* field, const HfValue* value), (m, entity, field, value))
HR_REG(int, set_tag, R_SET_TAG, Fnv(Fnv(FNV0, &entity, 8), &tag, 8), (HfMod * m, HfRef entity, uint64_t tag, int present), (m, entity, tag, present))
HR_REG(int, mount, R_MOUNT, Fnv(Fnv(FNV0, &entity, 8), &slot, 4), (HfMod * m, HfRef entity, uint32_t slot, HfRef carried), (m, entity, slot, carried))

/* ---- shot rules: stopped when the plug that made them goes ------------------------------------------------------- */
static uint32_t g_shots[HR_MAX_SHOTS];
static int g_nshots;
static void ShotAdd(uint32_t id) { if (id && g_nshots < HR_MAX_SHOTS) g_shots[g_nshots++] = id; }
static uint32_t P_modify_shots(HfMod* m, const HfShotRule* r) { uint32_t id = g_real->modify_shots(m, r); ShotAdd(id); return id; }
static uint32_t P_homing(HfMod* m, const HfHoming* r) { uint32_t id = g_real->homing(m, r); ShotAdd(id); return id; }
static int P_stop_shots(HfMod* m, uint32_t id)
{
    for (int i = 0; i < g_nshots; i++)
        if (g_shots[i] == id) { g_shots[i] = g_shots[--g_nshots]; break; }
    return g_real->stop_shots(m, id);
}

/* ---- the proxy table --------------------------------------------------------------------------------------------- */
static void BuildProxy(const HfApi* api)
{
    size_t n = api->size < sizeof(HfApi) ? api->size : sizeof(HfApi);
    memset(&g_proxy, 0, sizeof g_proxy);
    memcpy(&g_proxy, api, n);
    g_proxy.size = (uint32_t)n;
#define OVR(f) if (HAS(api, f)) g_proxy.f = P_##f
    OVR(on);
    OVR(hook);
    OVR(hook_ex);
    OVR(subscribe);
    OVR(on_game_thread);
    OVR(add_weapon);
    OVR(add_projectile);
    OVR(add_damage);
    OVR(add_stratagem);
    OVR(add_entity);
    OVR(add_behaviour);
    OVR(add_difficulty);
    OVR(register_entity);
    OVR(add_ability);
    OVR(add_stratagem_category);
    OVR(edit_entity);
    OVR(copy_field);
    OVR(set_field);
    OVR(set_tag);
    OVR(mount);
    OVR(modify_shots);
    OVR(homing);
    OVR(stop_shots);
#undef OVR
}

/* ---- persistent and shared memory -------------------------------------------------------------------------------- */
static void* Shared(const char* name, size_t size)
{
    if (!name || !size) return NULL;
    for (int i = 0; i < HR_MAX_SHARED; i++)
        if (g_shared[i].p && !strncmp(g_shared[i].name, name, sizeof g_shared[i].name - 1))
        {
            if (size > g_shared[i].size) { Log("shared(%s): %u bytes asked, %u kept (restart to grow it)", name, (unsigned)size, (unsigned)g_shared[i].size); return NULL; }
            return g_shared[i].p;
        }
    for (int i = 0; i < HR_MAX_SHARED; i++)
        if (!g_shared[i].p)
        {
            void* p = calloc(1, size);
            if (!p) return NULL;
            snprintf(g_shared[i].name, sizeof g_shared[i].name, "%s", name);
            g_shared[i].p = p;
            g_shared[i].size = size;
            return p;
        }
    return NULL;
}

/* The block keeps its address unless a plug asks for more than it holds. */
static int PrepareState(const HrPlugInfo* info, uint32_t* fresh)
{
    uint32_t want = info->state_size;
    *fresh = 0;
    if (!g_state || want > g_stateCap)
    {
        uint32_t cap = want > HR_STATE_RESERVE ? want : HR_STATE_RESERVE;
        unsigned char* p = (unsigned char*)calloc(1, cap);
        if (!p) return 0;
        if (g_state) { memcpy(p, g_state, g_stateSize); Log("state block grew to %u bytes: its address changed", cap); }
        free(g_state);
        g_state = p;
        g_stateCap = cap;
    }
    if (!g_stateSet || info->state_version != g_stateVersion)
    {
        if (g_stateSet) Log("state_version %u -> %u: the persistent state starts over", g_stateVersion, info->state_version);
        memset(g_state, 0, g_stateCap);
        *fresh = 1;
    }
    else if (want > g_stateSize) memset(g_state + g_stateSize, 0, want - g_stateSize);
    g_stateSet = 1;
    g_stateSize = want;
    g_stateVersion = info->state_version;
    return 1;
}

/* ---- loading and unloading --------------------------------------------------------------------------------------- */
static void ClearPlugHooks(void)
{
    /* re-pointed by the next plug's hook calls; until then (or if it never hooks again) the original runs */
    for (int i = 0; i < g_nhooks; i++) { hr_slots[i].detour = NULL; g_hook[i].claimed = 0; }
    for (int i = 0; i < g_nsubs; i++) g_sub[i].n = 0;
    for (int i = 0; i < HR_MAX_ON; i++) g_on[i].n = 0;
    g_nticks = 0;
    g_nOnQueue = 0;
}

static void FillHost(uint32_t fresh)
{
    g_host.abi = HR_ABI_VERSION;
    g_host.size = sizeof g_host;
    g_host.api = &g_proxy;
    g_host.real_api = g_real;
    g_host.mod = g_mod;
    g_host.state = g_state;
    g_host.state_size = g_stateSize;
    g_host.state_fresh = fresh;
    g_host.generation = g_generation;
    g_host.first = !g_recorded;
    g_host.fired = (uint32_t)g_fired;
    g_host.shared = Shared;
    g_host.plug_path = g_plugPathUtf8;
}

static void Retire(HMODULE module, const WCHAR* copy, int pinned)
{
    if (!module) return;
    if (g_nold >= HR_MAX_OLD) return;  /* leaked: still loaded, never freed */
    g_old[g_nold].module = module;
    wcsncpy(g_old[g_nold].copy, copy, MAX_PATH * 2 - 1);
    g_old[g_nold].freeAt = pinned || HR_FREE_OLD_SECONDS <= 0 ? -1 : g_now + HR_FREE_OLD_SECONDS;
    g_nold++;
}

static void FreeOld(void)
{
    for (int i = 0; i < g_nold; i++)
        if (g_old[i].freeAt >= 0 && g_now >= g_old[i].freeAt)
        {
            FreeLibrary(g_old[i].module);
            DeleteFileW(g_old[i].copy);
            g_old[i] = g_old[--g_nold];
            i--;
        }
}

static void UnloadCurrent(const char* why)
{
    if (!g_plug.module) return;
    ClearPlugHooks();
    Phase saved = g_phase;
    g_phase = PH_RUN;
    if (g_plug.unload) g_plug.unload(&g_host);
    g_phase = saved;
    for (int i = 0; i < g_nshots; i++) g_real->stop_shots(g_mod, g_shots[i]);
    g_nshots = 0;
    InterlockedIncrement(&g_exportGen);
    g_exportModule = NULL;
    Retire(g_plug.module, g_plug.copy, g_plug.generation == 1);
    Log("unloaded %s %s (%s)", g_plug.info->name ? g_plug.info->name : "?", g_plug.info->version ? g_plug.info->version : "?", why);
    memset(&g_plug, 0, sizeof g_plug);
}

static int StatPlug(uint64_t* stamp, uint64_t* size)
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(g_plugPath, GetFileExInfoStandard, &a)) return 0;
    *stamp = ((uint64_t)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
    *size = ((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    return 1;
}

/* The linker is done with the file: nobody holds it open for writing. */
static int Writable(void)
{
    HANDLE h = CreateFileW(g_plugPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    CloseHandle(h);
    return 1;
}

/* Loads the plug file (a fresh copy of it) and swaps it in. The old plug keeps running if the new one is unusable. */
static void LoadPlug(void)
{
    uint64_t stamp = 0, size = 0;
    if (!StatPlug(&stamp, &size)) { Log("no plug at %s", g_plugPathUtf8); return; }
    g_loadedStamp = stamp;  /* tried: not again until the file changes */
    g_loadedSize = size;

    Plug p;
    memset(&p, 0, sizeof p);
    _snwprintf(p.copy, MAX_PATH * 2 - 1, L"%ls\\%ls.hr%lu-%u.dll", g_plugDir, g_plugStem, (unsigned long)GetCurrentProcessId(), ++g_copies);
    if (!CopyFileW(g_plugPath, p.copy, FALSE)) { Log("could not copy the plug (error %lu)", GetLastError()); return; }
    p.module = LoadLibraryExW(p.copy, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!p.module) { Log("could not load the plug (error %lu)", GetLastError()); DeleteFileW(p.copy); return; }
    HrPlugInfoFn infoFn = (HrPlugInfoFn)(void*)GetProcAddress(p.module, HR_PLUG_INFO_SYMBOL);
    p.load = (HrPlugLoadFn)(void*)GetProcAddress(p.module, HR_PLUG_LOAD_SYMBOL);
    p.unload = (HrPlugUnloadFn)(void*)GetProcAddress(p.module, HR_PLUG_UNLOAD_SYMBOL);
    p.info = infoFn ? infoFn() : NULL;
    if (!p.info || !p.load || p.info->abi != HR_ABI_VERSION || p.info->size < sizeof(HrPlugInfo))
    {
        Log("%ls is not a hot-reload plug of ABI %d (hr_plug_info / hr_plug_load missing or another ABI): kept the old one",
            g_plugPath, HR_ABI_VERSION);
        FreeLibrary(p.module);
        DeleteFileW(p.copy);
        return;
    }

    EnterCriticalSection(&g_lock);
    UnloadCurrent("replaced");
    uint32_t fresh = 0;
    if (!PrepareState(p.info, &fresh))
    {
        Log("no memory for %u bytes of plug state", p.info->state_size);
        LeaveCriticalSection(&g_lock);
        Retire(p.module, p.copy, 0);
        return;
    }
    p.generation = ++g_generation;
    g_plug = p;
    FillHost(fresh);
    int first = !g_recorded;
    if (first && !g_inInit) Log("first load after hellforge_mod_init: HellForge may refuse its startup registrations and new event kinds");
    g_phase = first ? PH_RECORD : PH_REPLAY;
    g_cursor = 0;
    g_recWarned = 0;
    int r = p.load(&g_host);
    g_phase = PH_RUN;
    if (first) g_recorded = 1;
    else if (g_cursor < g_nrec && !g_recWarned)
        Log("%d startup registration(s) of the first load were not made this time: what they added stays until a restart", g_nrec - g_cursor);
    if (r)
    {
        Log("%s %s: load failed (%d): no plug runs until the next build", p.info->name, p.info->version, r);
        UnloadCurrent("its load failed");
        LeaveCriticalSection(&g_lock);
        return;
    }
    /* load events that had fired before this plug asked for them */
    int nq = g_nOnQueue;
    g_nOnQueue = 0;
    for (int i = 0; i < nq; i++) g_onQueue[i].fn(g_mod, (HfEvent)g_onQueue[i].event, g_onQueue[i].user);
    g_exportModule = p.module;
    InterlockedIncrement(&g_exportGen);
    LeaveCriticalSection(&g_lock);
    Log("%s %s %s (load #%u, %d hook(s), %d registration(s) on record%s)", p.info->name ? p.info->name : "plug",
        p.info->version ? p.info->version : "?", g_generation == 1 ? "loaded" : "RELOADED", g_generation, g_nhooks, g_nrec,
        fresh ? ", fresh state" : "");
}

static void Watch(void)
{
    if (g_now < g_nextPoll) return;
    uint64_t stamp, size;
    if (!StatPlug(&stamp, &size)) { g_pending = 0; g_nextPoll = g_now + HR_POLL_SECONDS; return; }
    if (stamp == g_loadedStamp && size == g_loadedSize) { g_pending = 0; g_nextPoll = g_now + HR_POLL_SECONDS; return; }
    if (!g_pending || stamp != g_pendStamp || size != g_pendSize)
    {
        g_pending = 1;
        g_pendStamp = stamp;
        g_pendSize = size;
        g_pendSince = g_now;
    }
    else if (g_now - g_pendSince >= HR_STABLE_SECONDS && Writable())
    {
        g_pending = 0;
        LoadPlug();
    }
    g_nextPoll = g_now + HR_STABLE_SECONDS / 2;
}

static void SocketTick(HfMod* mod, float dt, void* user)
{
    g_now += dt > 0 && dt < 5 ? dt : 0;
    Watch();
    FreeOld();
    for (int i = 0; i < g_nticks; i++) g_tick[i].fn(mod, dt, g_tick[i].user);
}

/* ---- exports forwarded to the plug ------------------------------------------------------------------------------- */
typedef struct { LONG gen; FARPROC proc; size_t size; } HrExport;
__attribute__((unused)) static FARPROC ExportProc(HrExport* x, const char* name, int wantSize)
{
    LONG gen = g_exportGen;
    if (x->gen != gen)
    {
        HMODULE m = g_exportModule;
        x->proc = m ? GetProcAddress(m, name) : NULL;
        x->size = 0;
        if (m && wantSize)
        {
            char sym[160];
            snprintf(sym, sizeof sym, HR_SIZE_PREFIX "%s", name);
            size_t (*sz)(void) = (size_t (*)(void))(void*)GetProcAddress(m, sym);
            x->size = sz ? sz() : 0;
        }
        x->gen = gen;
    }
    return x->proc;
}

#define HR_EXPORT_STATE(NAME, SIZE)                                                                                    \
    static HrExport hr_x_##NAME = {-1, 0, 0};                                                                         \
    static unsigned char hr_buf_##NAME[HR_STATE_MAX] __attribute__((aligned(16)));                                    \
    HF_MOD_EXPORT void* NAME(void)                                                                                     \
    {                                                                                                                  \
        void* (*f)(void) = (void* (*)(void))(void*)ExportProc(&hr_x_##NAME, #NAME, 1);                                 \
        size_t n = (SIZE) ? (size_t)(SIZE) : hr_x_##NAME.size;                                                         \
        if (n > HR_STATE_MAX) n = HR_STATE_MAX;                                                                        \
        void* p = f ? f() : NULL;                                                                                      \
        if (p && n) memcpy(hr_buf_##NAME, p, n);                                                                       \
        return hr_buf_##NAME;                                                                                          \
    }
#define HR_EXPORT_OPT_GET(NAME, SIZE)                                                                                  \
    static HrExport hr_x_##NAME = {-1, 0, 0};                                                                         \
    HF_MOD_EXPORT int NAME(const char* name, double* out)                                                              \
    {                                                                                                                  \
        int (*f)(const char*, double*) = (int (*)(const char*, double*))(void*)ExportProc(&hr_x_##NAME, #NAME, 0);     \
        return f ? f(name, out) : 0;                                                                                   \
    }
#define HR_EXPORT_OPT_SET(NAME, SIZE)                                                                                  \
    static HrExport hr_x_##NAME = {-1, 0, 0};                                                                         \
    HF_MOD_EXPORT int NAME(const char* name, double value)                                                             \
    {                                                                                                                  \
        int (*f)(const char*, double) = (int (*)(const char*, double))(void*)ExportProc(&hr_x_##NAME, #NAME, 0);       \
        return f ? f(name, value) : 0;                                                                                 \
    }
#define HR_EXPORT_INT0(NAME, SIZE)                                                                                     \
    static HrExport hr_x_##NAME = {-1, 0, 0};                                                                         \
    HF_MOD_EXPORT int NAME(void)                                                                                       \
    {                                                                                                                  \
        int (*f)(void) = (int (*)(void))(void*)ExportProc(&hr_x_##NAME, #NAME, 0);                                     \
        return f ? f() : 0;                                                                                            \
    }
#define HR_EXPORT_VOID0(NAME, SIZE)                                                                                    \
    static HrExport hr_x_##NAME = {-1, 0, 0};                                                                         \
    HF_MOD_EXPORT void NAME(void)                                                                                      \
    {                                                                                                                  \
        void (*f)(void) = (void (*)(void))(void*)ExportProc(&hr_x_##NAME, #NAME, 0);                                   \
        if (f) f();                                                                                                    \
    }
#define HR_EXPORT_ONE(KIND, NAME, SIZE) HR_EXPORT_##KIND(NAME, SIZE)
HR_EXPORTS(HR_EXPORT_ONE)

/* A small status export for tools: the load count (0: no plug yet). */
HF_MOD_EXPORT uint32_t hr_socket_generation(void) { return g_plug.module ? g_generation : 0; }

/* ---- init -------------------------------------------------------------------------------------------------------- */
static void ResolvePlugPath(void)
{
    WCHAR folder[MAX_PATH * 2] = L"", ini[MAX_PATH * 2], value[MAX_PATH * 2] = L"";
    const char* f = g_real->mod_folder ? g_real->mod_folder(g_mod) : NULL;
    if (f) MultiByteToWideChar(CP_UTF8, 0, f, -1, folder, MAX_PATH * 2);
    _snwprintf(ini, MAX_PATH * 2 - 1, L"%ls\\hotreload.ini", folder);
    ini[MAX_PATH * 2 - 1] = 0;
    GetPrivateProfileStringW(L"hotreload", L"plug", L"", value, MAX_PATH * 2, ini);
    if (!value[0]) MultiByteToWideChar(CP_UTF8, 0, HR_PLUG_PATH, -1, value, MAX_PATH * 2);
    int absolute = (value[0] && value[1] == L':') || (value[0] == L'\\' && value[1] == L'\\');
    if (absolute) wcsncpy(g_plugPath, value, MAX_PATH * 2 - 1);
    else _snwprintf(g_plugPath, MAX_PATH * 2 - 1, L"%ls\\%ls", folder, value);
    for (WCHAR* c = g_plugPath; *c; c++) if (*c == L'/') *c = L'\\';
    WideCharToMultiByte(CP_UTF8, 0, g_plugPath, -1, g_plugPathUtf8, sizeof g_plugPathUtf8, NULL, NULL);
    /* its folder and stem: copies go next to it as <stem>.hr<pid>-<n>.dll */
    wcsncpy(g_plugDir, g_plugPath, MAX_PATH * 2 - 1);
    WCHAR* slash = wcsrchr(g_plugDir, L'\\');
    const WCHAR* name = slash ? slash + 1 : g_plugDir;
    wcsncpy(g_plugStem, name, MAX_PATH - 1);
    WCHAR* dot = wcsrchr(g_plugStem, L'.');
    if (dot) *dot = 0;
    if (slash) *slash = 0;
    else wcscpy(g_plugDir, L".");
}

/* Copies left by earlier sessions (no longer locked): deleted. */
static void CleanCopies(void)
{
    WCHAR pattern[MAX_PATH * 2], path[MAX_PATH * 2];
    _snwprintf(pattern, MAX_PATH * 2 - 1, L"%ls\\%ls.hr*.dll", g_plugDir, g_plugStem);
    pattern[MAX_PATH * 2 - 1] = 0;
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileW(pattern, &d);
    if (h == INVALID_HANDLE_VALUE) return;
    int n = 0;
    do
    {
        _snwprintf(path, MAX_PATH * 2 - 1, L"%ls\\%ls", g_plugDir, d.cFileName);
        path[MAX_PATH * 2 - 1] = 0;
        n += DeleteFileW(path) != 0;
    } while (FindNextFileW(h, &d));
    FindClose(h);
    if (n) Log("deleted %d old plug copies", n);
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    if (!api || api->version < 7 || !HAS(api, subscribe) || !HAS(api, on_game_thread) || !HAS(api, hook)) return 1;
    g_real = api;
    g_mod = mod;
    InitializeCriticalSection(&g_lock);
    BuildProxy(api);
    ResolvePlugPath();
    Log("socket for %s: plug %s", api->mod_id ? api->mod_id(mod) : "?", g_plugPathUtf8);
    CleanCopies();
    g_inInit = 1;
    for (uint32_t k = 1; k < 32; k++)
        if (HR_PRESUBSCRIBE_MASK & (1u << k)) SubFor(mod, k, 0);
    if (!api->on_game_thread(mod, SocketTick, NULL)) Log("the game thread could not be hooked: no reloads (the first plug still runs)");
    LoadPlug();
    g_inInit = 0;
    return 0;
}
