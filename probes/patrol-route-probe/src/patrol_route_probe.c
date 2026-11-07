/* Patrol Route Probe (yui.patrol-route-probe): research for Lost Contact. Reads only (one pass-through hook).
 *
 * Lost Contact turns patrols away once they walk at you; steering them past you when the game first aims them needs
 * the code that aims them. Two views of it:
 *
 *   set_destination (game+0x5B36C0, (NULL, entity, &pos); Lost Contact's own call) is hooked, passing every call on
 *     unchanged: each call's caller (its return address in game.dll), the entity, whether it has a target, whether it
 *     spawned in the last 15 s, and how far the destination is from you. Logged: calls that land within 80 m of you
 *     (the first 150), and per caller at mission end: calls, how many landed near you, on fresh enemies, on enemies
 *     with a target.
 *   enemies that spawn after the first 60 s of a mission (patrols, drops, breaches; not the map's population) are
 *     watched for 15 s: each change of their navigation destination (record +0x60) is logged, with whether a
 *     set_destination call for it came just before (and from where). A destination set at spawn without one comes
 *     from other code.
 *
 * Layouts: navigation [game+0x3326418] map +0x30, records +0x50, 0xA8 each, +0x54 position, +0x60 destination;
 * behaviours [game+0x3326740] map +0x40, count +0x20, records +0x60, 0x1F8 each, +0x78 has-target (Lost Contact's,
 * from the AI Probe). Unit and position reads as Commando Plus (HD2Runtime's research, SkyeShade). */
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "hellforge_mod.h"

#define BUILD "25480438"
#define VERSION "0.1.0"
#define SET_DEST 0x5B36C0u
static const HfApi* hf;
static HfMod* self;
static uintptr_t game, exe;
static size_t gameSize;
static int g_ready;
static double g_now;
#define Log(...) hf->log(self, __VA_ARGS__)

static uint32_t Word(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t Quad(const uint8_t* p) { uint64_t v; memcpy(&v, p, 8); return v; }
static int ReadAt(uintptr_t a, void* out, size_t n) { return a >= 0x10000 && a < UINT64_C(0x7FFFFFFF0000) && hf->read(a, out, n) == 0; }
static uintptr_t Pointer(uintptr_t a) { uintptr_t v = 0; return ReadAt(a, &v, 8) && v >= 0x10000 && v < UINT64_C(0x7FFFFFFF0000) ? v : 0; }
static int Lookup(uintptr_t at, uint32_t key, uint32_t* index)
{
    uint8_t h[20], row[8];
    if (!ReadAt(at, h, sizeof h)) return 0;
    uintptr_t entries = (uintptr_t)Quad(h);
    uint32_t n = Word(h + 8), empty = Word(h + 12), mult = Word(h + 16);
    if (!entries || !n || n > 1048576 || (n & (n - 1)) || key == empty) return 0;
    for (unsigned i = 0; i < n && i < 128; i++)
    {
        if (!ReadAt(entries + 8u * ((key * mult + i) & (n - 1)), row, 8)) return 0;
        if (Word(row) == key) { *index = Word(row + 4); return *index != UINT32_MAX; }
        if (Word(row) == empty) return 0;
    }
    return 0;
}
static uintptr_t NavRecord(uint32_t e)
{
    uintptr_t m = Pointer(game + 0x3326418), recs;
    uint32_t i;
    if (!m || !Lookup(m + 0x30, e, &i) || i >= 0x10000 || !(recs = Pointer(m + 0x50))) return 0;
    return recs + 0xA8ull * i;
}
static int HasTarget(uint32_t e)
{
    uintptr_t m = Pointer(game + 0x3326740), recs;
    uint32_t i, count, has = 0;
    if (!m || !Lookup(m + 0x40, e, &i) || !ReadAt(m + 0x20, &count, 4) || i >= count || i >= 16384 || !(recs = Pointer(m + 0x60))) return -1;
    return ReadAt(recs + 0x1F8ull * i + 0x78, &has, 4) ? has != 0 : -1;
}
static uint32_t UnitOf(uint32_t entity)
{
    uintptr_t root = Pointer(game + 0x346BF98);
    uint32_t index;
    uint8_t d[24];
    if (!root || !entity || !Lookup(root + 0xF1AEB0, entity, &index) || index >= 262144 || !ReadAt(root + 0xF32F18 + 24u * index, d, 24) || Word(d + 8) != entity) return 0;
    return Word(d + 12);
}
static uint64_t ResOf(uint32_t entity)
{
    uintptr_t root = Pointer(game + 0x346BF98);
    uint32_t index;
    uint8_t d[24];
    if (!root || !entity || !Lookup(root + 0xF1AEB0, entity, &index) || index >= 262144 || !ReadAt(root + 0xF32F18 + 24u * index, d, 24) || Word(d + 8) != entity) return 0;
    return Quad(d);
}
static int Position(uint32_t entity, float out[3])
{
    uint32_t unit = UnitOf(entity);
    uintptr_t registry = Pointer(exe + 0x1A100F0), objects, object, nodes;
    uint32_t count, id, slot = unit & 0x3FFFFF;
    if (!unit || !registry || !ReadAt(registry + 0x98, &count, 4) || slot >= count || !(objects = Pointer(registry + 0x88)) ||
        !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit || !ReadAt(object + 0x70, &count, 4) || !count ||
        count > 4096 || !(nodes = Pointer(object + 0x88)) || !ReadAt(nodes + 48, out, 12))
        return 0;
    for (int i = 0; i < 3; i++) if (!isfinite(out[i]) || fabsf(out[i]) > 100000) return 0;
    return 1;
}
static float Dist2(const float a[3], const float b[3]) { float dx = a[0] - b[0], dy = a[1] - b[1]; return sqrtf(dx * dx + dy * dy); }

/* ---- the hook: every call passed on, noted in a ring ------------------------------------------------------------- */
typedef void (*SetDestFn)(void*, uint32_t, const float*);
static SetDestFn g_orig;
#define RING 1024
typedef struct { uintptr_t caller; uint32_t entity; float pos[3]; double t; } Call;
static Call g_ring[RING];
static atomic_uint g_head;
static unsigned g_tail;
static void Detour(void* a, uint32_t entity, const float* pos)
{
    unsigned i = atomic_fetch_add(&g_head, 1) % RING;
    g_ring[i].caller = (uintptr_t)__builtin_return_address(0);
    g_ring[i].entity = entity;
    if (pos) memcpy(g_ring[i].pos, pos, 12); else memset(g_ring[i].pos, 0, 12);
    g_ring[i].t = g_now;
    g_orig(a, entity, pos);
}

/* ---- callers ----------------------------------------------------------------------------------------------------- */
#define CALLERS 64
typedef struct { uint32_t rva; unsigned calls, nearYou, fresh, targeted, freshNear; } Caller;
static Caller g_callers[CALLERS];
static int g_ncallers;
static Caller* CallerOf(uintptr_t at)
{
    uint32_t rva = at >= game && at < game + gameSize ? (uint32_t)(at - game) : 0;   /* 0: outside game.dll (a mod) */
    for (int i = 0; i < g_ncallers; i++) if (g_callers[i].rva == rva) return &g_callers[i];
    if (g_ncallers == CALLERS) return NULL;
    Caller* c = &g_callers[g_ncallers++];
    memset(c, 0, sizeof *c);
    c->rva = rva;
    return c;
}

/* ---- spawns, and the fresh ones watched ------------------------------------------------------------------------- */
#define SEEN 8192
static uint32_t g_seenEnt[SEEN];
static uint64_t g_seenRes[SEEN];
static double g_seenAt[SEEN];
static atomic_uint g_nseen;
static double g_missionAt;
#define WATCH 256
typedef struct { uint32_t entity; uint64_t res; double at; float dest[3]; int changes; uintptr_t lastCaller; double lastCallAt; } Watch;
static Watch g_watch[WATCH];
static atomic_uint g_avatar;
static unsigned g_nearLogged, g_watchLogged;
static int FreshIndex(uint32_t e)
{
    unsigned n = atomic_load(&g_nseen);
    if (n > SEEN) n = SEEN;
    for (unsigned i = n; i-- > 0;) if (g_seenEnt[i] == e) return g_now - g_seenAt[i] < 15 ? (int)i : -1;
    return -1;
}
static Watch* WatchOf(uint32_t e)
{
    for (int i = 0; i < WATCH; i++) if (g_watch[i].entity == e) return &g_watch[i];
    return NULL;
}
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    unsigned i = atomic_fetch_add(&g_nseen, 1);
    if (i < SEEN) { g_seenEnt[i] = s->entity; g_seenRes[i] = s->resource; g_seenAt[i] = g_now; }
}
static void OnWeapon(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfWeaponEvent* w = (const HfWeaponEvent*)info;
    if (w->avatar) atomic_store(&g_avatar, w->avatar);
}

static void Summary(void)
{
    Log("mission end: set_destination callers (rva 0 = outside game.dll, a mod's), most calls first:");
    for (int k = 0; k < g_ncallers; k++)
    {
        int best = -1;
        for (int i = 0; i < g_ncallers; i++) if (g_callers[i].calls && (best < 0 || g_callers[i].calls > g_callers[best].calls)) best = i;
        if (best < 0) break;
        Caller* c = &g_callers[best];
        Log("  game+0x%X: %u calls, %u within 80 m of you, %u on enemies spawned in the last 15 s (%u of those near you), %u on enemies with a target",
            c->rva, c->calls, c->nearYou, c->fresh, c->freshNear, c->targeted);
        c->calls = 0;
    }
}
static int g_inMission;
static void Tick(HfMod* mod, float dt, void* user)
{
    g_now += dt > 0 && dt < 1 ? dt : 0;
    if (g_ready != 1) return;
    int m = hf->game_state(self) == HF_STATE_MISSION;
    if (m != g_inMission)
    {
        g_inMission = m;
        if (m)
        {
            g_missionAt = g_now; g_ncallers = 0; g_nearLogged = g_watchLogged = 0;
            memset(g_watch, 0, sizeof g_watch);
            g_tail = atomic_load(&g_head);
            Log("mission start: watching set_destination, and enemies that spawn after the first 60 s");
        }
        else { Summary(); Log("mission end"); atomic_store(&g_nseen, 0); atomic_store(&g_avatar, 0); }
    }
    if (!m) { g_tail = atomic_load(&g_head); return; }
    float me[3];
    int haveMe = Position(atomic_load(&g_avatar), me);
    /* the calls since the last frame */
    while (g_tail != atomic_load(&g_head))
    {
        Call c = g_ring[g_tail++ % RING];
        Caller* k = CallerOf(c.caller);
        float d = haveMe ? Dist2(c.pos, me) : -1;
        int fresh = FreshIndex(c.entity) >= 0, tgt = HasTarget(c.entity);
        if (k)
        {
            k->calls++;
            if (d >= 0 && d <= 80) k->nearYou++;
            if (fresh) { k->fresh++; if (d >= 0 && d <= 80) k->freshNear++; }
            if (tgt == 1) k->targeted++;
        }
        Watch* w = WatchOf(c.entity);
        if (w) { w->lastCaller = c.caller; w->lastCallAt = g_now; }
        if (d >= 0 && d <= 80 && g_nearLogged < 150)
        {
            g_nearLogged++;
            float at[3];
            int pos = Position(c.entity, at);
            Log("set_destination from %s+0x%X: entity %u (%016llx)%s%s, %s%.0f m from you, aimed %.0f m from you",
                k && k->rva ? "game" : "outside", k ? k->rva : 0, c.entity, (unsigned long long)ResOf(c.entity), fresh ? ", spawned in the last 15 s" : "",
                tgt == 1 ? ", has a target" : tgt == 0 ? ", no target" : "", pos ? "" : "(?) ", pos ? (double)Dist2(at, me) : -1.0, (double)d);
        }
    }
    /* the fresh enemies: watch those that spawned after 60 s in */
    unsigned n = atomic_load(&g_nseen);
    if (n > SEEN) n = SEEN;
    static unsigned took;
    if (took > n) took = 0;
    for (; took < n; took++)
    {
        if (g_seenAt[took] - g_missionAt < 60) continue;
        uint32_t e = g_seenEnt[took];
        if (!NavRecord(e) && g_now - g_seenAt[took] < 1) { break; }   /* its navigation record may come a frame later */
        if (!NavRecord(e)) continue;
        for (int i = 0; i < WATCH; i++)
            if (!g_watch[i].entity || g_now - g_watch[i].at > 15)
            {
                memset(&g_watch[i], 0, sizeof g_watch[i]);
                g_watch[i].entity = e; g_watch[i].res = g_seenRes[took]; g_watch[i].at = g_seenAt[took];
                break;
            }
    }
    for (int i = 0; i < WATCH; i++)
    {
        Watch* w = &g_watch[i];
        if (!w->entity || g_now - w->at > 15) continue;
        uintptr_t nr = NavRecord(w->entity);
        float dest[3], pos[3];
        if (!nr || !ReadAt(nr + 0x60, dest, 12) || !ReadAt(nr + 0x54, pos, 12)) continue;
        if (!memcmp(dest, w->dest, 12)) continue;
        memcpy(w->dest, dest, 12);
        w->changes++;
        if (g_watchLogged >= 200 || w->changes > 4) continue;
        g_watchLogged++;
        int viaCall = w->lastCallAt > 0 && g_now - w->lastCallAt < 0.25;
        uint32_t rva = viaCall && w->lastCaller >= game && w->lastCaller < game + gameSize ? (uint32_t)(w->lastCaller - game) : 0;
        Log("fresh entity %u (%016llx, %.1f s old) at %.0f m from you: destination %.0f %.0f %.0f, %.0f m from you, %.0f m from itself; %s%s",
            w->entity, (unsigned long long)w->res, g_now - w->at, haveMe ? (double)Dist2(pos, me) : -1.0, dest[0], dest[1], dest[2],
            haveMe ? (double)Dist2(dest, me) : -1.0, (double)Dist2(dest, pos), viaCall ? "set_destination from " : "no set_destination call just before (other code)",
            viaCall ? (rva ? "game" : "outside") : "");
        if (viaCall) Log("  (+0x%X)", rva);
    }
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t xs = 0;
    game = hf->module_base("game.dll", &gameSize);
    exe = hf->module_base("helldivers2.exe", &xs);
    if (!game || !exe || gameSize != 74727424u || xs != 60719104u) { g_ready = -1; Log("REFUSED: not build " BUILD "'s modules"); return; }
    if (!hf->code_matches(game + SET_DEST, "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57 48 83 ec 50"))
    { g_ready = -1; Log("REFUSED: set_destination is not the reviewed code"); return; }
    if (hf->hook_ex(self, game + SET_DEST, (void*)Detour, (void**)&g_orig, 0) || !g_orig) { g_ready = -1; Log("REFUSED: set_destination could not be hooked"); return; }
    g_ready = 1;
    Log("ready: set_destination (game+0x%X) hooked, every call passed on unchanged", SET_DEST);
}
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 15) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    Log("Patrol Route Probe " VERSION " (research; host or solo)");
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    if (!api->on_game_thread(mod, Tick, NULL)) { Log("the game thread could not be hooked"); return 1; }
    if (!api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) || !api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0)) Log("some events are unavailable");
    return 0;
}
