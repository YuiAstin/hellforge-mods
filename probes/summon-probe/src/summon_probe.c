/* Summon & Warp Probe (yui.summon-probe): research only, no gameplay effect. Build 25480438 only.
 *
 * Warp: how the LIFT-182 Warp Pack decides where you land. At game start it logs the warp's code around its call to
 * the position setter (as shipped, before any hook), the setter and the stand check ConsistentVaulting uses. It hooks
 * the position setter (game.dll+0x5A2BB0), the launch flag (+0x5A3D80) and the engine's raycast (exe+0x7F4590) with
 * pass-through detours that only store a few integers. When your Helldiver is moved, it logs every raycast made in the
 * 2 s before (who made them, how many, when) and the launch flag's timing: the checks and the delay before a warp.
 *
 * Summon: what a mech the mod drives itself needs. It logs the code of the game's entity spawn (+0xFDC140, HD2Runtime:
 * "takes spawn parameters and network replication that are not proven") so its arguments can be read offline, the
 * engine's unit function table (where an animation call would be), and every 10 s in a mission the 5 nearest spawned
 * entities that are not yours (type, distance, simulated here): the targeting a summon would use. It also logs the
 * game's entity destroy (+0xFDC820), to remove a summon again.
 *
 * Addresses: HD2Runtime by SkyeShade (warp call site, the spawn), ConsistentVaulting (0BSD: mover table, stand check),
 * Helldiver Survival Rework (MIT: the raycast's signature), EXO Beacon Launcher (MIT: the unit table). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include <windows.h>
#include <intrin.h>
#include "hellforge_mod.h"

#define BUILD "25480438"
#define VERSION "0.1.0"
static const HfApi* hf;
static HfMod* self;
static uintptr_t game, exe;
static size_t gameSize, exeSize;
static atomic_uint g_avatar;
static double g_tscPerMs;

#define Log(...) hf->log(self, __VA_ARGS__)
static int Plausible(uint64_t p) { return p >= 0x10000 && p < 0x7FFFFFFF0000ull && !(p & 3); }
static uintptr_t P(uintptr_t a) { uintptr_t v = 0; return hf->read(a, &v, 8) == 0 && Plausible(v) ? v : 0; }
static const char* Where(uint64_t p, char* out, size_t n)
{
    if (p >= game && p < game + gameSize) snprintf(out, n, "game+0x%llX", (unsigned long long)(p - game));
    else if (p >= exe && p < exe + exeSize) snprintf(out, n, "exe+0x%llX", (unsigned long long)(p - exe));
    else snprintf(out, n, "0x%llX", (unsigned long long)p);
    return out;
}
static void Dump(const char* what, uintptr_t base, uintptr_t rva, size_t n)
{
    uint8_t buf[0x200];
    for (size_t at = 0; at < n; at += sizeof buf)
    {
        size_t m = n - at < sizeof buf ? n - at : sizeof buf;
        if (hf->read_code(base + rva + at, buf, m)) { Log("%s +0x%llX: unreadable", what, (unsigned long long)(rva + at)); return; }
        if (!at) Log("%s at %s+0x%llX, %u bytes:", what, base == game ? "game" : "exe", (unsigned long long)rva, (unsigned)n);
        for (size_t o = 0; o < m; o += 32)
        {
            char line[80];
            size_t k = 0;
            for (size_t i = o; i < o + 32 && i < m; i++) k += (size_t)snprintf(line + k, sizeof line - k, "%02x", buf[i]);
            Log("  %llX %s", (unsigned long long)(rva + at + o), line);
        }
    }
}

/* ---- detours: integer stores only, then the game's function unchanged ------------------------------------------- */
typedef uintptr_t (*Fn8)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
typedef uint32_t (*RayFn)(uint32_t, const void*, const void*, float, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t);
typedef struct { volatile uint64_t tsc, ret, a1, a2, a3; volatile uint32_t f[3], floats; } Call;
#define RING 64
static Call g_set[RING], g_flag[RING];
static volatile uint64_t g_setN, g_flagN;
static Fn8 original_set, original_flag;
static RayFn original_ray;
/* every raycast: when and who asked (1M entries cover several seconds of a busy mission) */
#define RAYS (1u << 20)
typedef struct { volatile uint64_t tsc, ret; volatile float len; volatile uint32_t flags; } Ray;
static Ray* g_rays;
static volatile uint64_t g_rayN;

#define RECORD(ring, n, r1, r2, r3)                                                           \
    do {                                                                                      \
        uint64_t i_ = __atomic_fetch_add(&n, 1, __ATOMIC_RELAXED);                            \
        Call* c_ = &ring[i_ % RING];                                                          \
        c_->tsc = __rdtsc();                                                                  \
        c_->ret = (uint64_t)(uintptr_t)__builtin_return_address(0);                           \
        c_->a1 = r1; c_->a2 = r2; c_->a3 = r3; c_->floats = 0;                                \
        if (Plausible(r3))                                                                    \
        {                                                                                     \
            const volatile uint32_t* p_ = (const volatile uint32_t*)(uintptr_t)r3;            \
            c_->f[0] = p_[0]; c_->f[1] = p_[1]; c_->f[2] = p_[2]; c_->floats = 3;             \
        }                                                                                     \
    } while (0)

__attribute__((noinline)) static uintptr_t SetDetour(uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5,
    uintptr_t a6, uintptr_t a7, uintptr_t a8)
{
    RECORD(g_set, g_setN, a1, a2, a3);
    return original_set(a1, a2, a3, a4, a5, a6, a7, a8);
}
__attribute__((noinline)) static uintptr_t FlagDetour(uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5,
    uintptr_t a6, uintptr_t a7, uintptr_t a8)
{
    RECORD(g_flag, g_flagN, a1, a2, a3);
    return original_flag(a1, a2, a3, a4, a5, a6, a7, a8);
}
__attribute__((noinline)) static uint32_t RayDetour(uint32_t world, const void* from, const void* dir, float len, uint32_t a5,
    uint32_t a6, uint32_t filter, uint32_t flags, uint32_t unit, void* hits, uint32_t max)
{
    if (g_rays)
    {
        uint64_t i = __atomic_fetch_add(&g_rayN, 1, __ATOMIC_RELAXED);
        Ray* r = &g_rays[i & (RAYS - 1)];
        r->tsc = __rdtsc();
        r->ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
        r->len = len;
        r->flags = flags;
    }
    return original_ray(world, from, dir, len, a5, a6, filter, flags, unit, hits, max);
}

/* ---- the warp report: what ran in the 2 s before your Helldiver moved --------------------------------------------- */
typedef struct { uint64_t ret; unsigned n; double first, last; float len; uint32_t flags; } Caller;
static void WarpReport(uint64_t moved)
{
    if (!g_rays || g_tscPerMs <= 0) return;
    uint64_t end = __atomic_load_n(&g_rayN, __ATOMIC_RELAXED), from = end > RAYS ? end - RAYS : 0;
    uint64_t window = (uint64_t)(2000 * g_tscPerMs), after = (uint64_t)(200 * g_tscPerMs);
    static Caller c[64];
    int nc = 0;
    unsigned total = 0;
    uint64_t oldest = UINT64_MAX;
    for (uint64_t i = from; i < end; i++)
    {
        Ray r = g_rays[i & (RAYS - 1)];
        if (r.tsc < oldest) oldest = r.tsc;
        if (r.tsc + window < moved || r.tsc > moved + after) continue;
        total++;
        double ms = ((double)r.tsc - (double)moved) / g_tscPerMs;
        int k = 0;
        while (k < nc && c[k].ret != r.ret) k++;
        if (k == nc) { if (nc == 64) continue; nc++; c[k].ret = r.ret; c[k].n = 0; c[k].first = ms; c[k].len = r.len; c[k].flags = r.flags; }
        c[k].n++;
        c[k].last = ms;
    }
    Log("warp: %u raycasts from %d places in the 2 s before you moved (the ring reaches %.0f ms back)", total, nc,
        ((double)moved - (double)oldest) / g_tscPerMs);
    for (int pass = 0; pass < 25 && nc; pass++)
    {
        int best = 0;
        for (int k = 1; k < nc; k++) if (c[k].n > c[best].n) best = k;
        char w[48];
        Log("  %s: %u calls, %+.0f .. %+.0f ms, length %.2f, flags %08X%s", Where(c[best].ret, w, sizeof w), c[best].n, c[best].first,
            c[best].last, (double)c[best].len, c[best].flags,
            c[best].ret >= game + 0x880000 && c[best].ret < game + 0x8A0000 ? "  <- the warp's own code region" : "");
        c[best] = c[--nc];
    }
}
static void Report(const char* name, Call* ring, volatile uint64_t* n, uint64_t* seen, int isSet)
{
    uint64_t now = *n;
    if (now - *seen > RING) *seen = now - RING;
    uint32_t me = atomic_load(&g_avatar);
    static unsigned others;
    while (*seen < now)
    {
        Call c = ring[*seen % RING];
        (*seen)++;
        int mine = me && (uint32_t)c.a2 == me;
        if (!mine && others++ >= 10) continue;
        char r[48];
        float f[3] = {0};
        for (int i = 0; i < 3 && i < (int)c.floats; i++) { uint32_t v = c.f[i]; memcpy(&f[i], &v, 4); }
        Log("%s%s from %s: entity %u, to %.2f %.2f %.2f", name, mine ? " (YOUR HELLDIVER)" : "", Where(c.ret, r, sizeof r),
            (uint32_t)c.a2, (double)f[0], (double)f[1], (double)f[2]);
        if (mine && isSet)
        {
            static uint64_t lastFlag;
            if (g_flagN)
            {
                Call fl = g_flag[(g_flagN - 1) % RING];
                if (fl.tsc != lastFlag && fl.tsc < c.tsc)
                {
                    lastFlag = fl.tsc;
                    Log("warp: the launch flag was set %.0f ms before the move", ((double)c.tsc - (double)fl.tsc) / g_tscPerMs);
                }
            }
            WarpReport(c.tsc);
        }
    }
}

/* ---- nearest entities (a summon's targeting) ------------------------------------------------------------------------ */
typedef void* (*ResolveFn)(uint32_t);
typedef const float* (*LocalPosFn)(uint32_t, uint32_t);
static ResolveFn g_resolve;
static LocalPosFn g_localPos;
typedef struct { uint32_t entity; uint64_t resource; uint32_t authority; } Seen;
static Seen g_seen[2048];
static unsigned g_nseen;
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    Seen* e = &g_seen[g_nseen++ % 2048];
    e->entity = s->entity; e->resource = s->resource; e->authority = s->authority;
}
static void OnPlayer(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    if (p->local && p->entity) atomic_store(&g_avatar, p->entity);
}
static int PosOf(uint32_t entity, float out[3])
{
    if (!g_resolve || !g_localPos || !entity) return 0;
    void* o = g_resolve(entity);
    uint8_t h[24];
    uint32_t id, unit;
    if (!o || hf->read((uintptr_t)o, h, 24)) return 0;
    memcpy(&id, h + 8, 4); memcpy(&unit, h + 12, 4);
    if (id != entity || !unit) return 0;
    const float* p = g_localPos(unit, 0);
    return p && hf->read((uintptr_t)p, out, 12) == 0 && isfinite(out[0]) && isfinite(out[2]);
}
static void Nearest(void)
{
    float me[3];
    if (!PosOf(atomic_load(&g_avatar), me)) return;
    struct { float d; Seen s; } best[5];
    int nb = 0;
    unsigned n = g_nseen < 2048 ? g_nseen : 2048;
    for (unsigned i = 0; i < n; i++)
    {
        Seen s = g_seen[i];
        float p[3];
        if (s.entity == atomic_load(&g_avatar) || !PosOf(s.entity, p)) continue;
        float d = sqrtf((p[0] - me[0]) * (p[0] - me[0]) + (p[1] - me[1]) * (p[1] - me[1]) + (p[2] - me[2]) * (p[2] - me[2]));
        if (d < 0.5f || d > 150) continue;
        int k;
        if (nb < 5) k = nb++;
        else if (d < best[4].d) k = 4;
        else continue;
        best[k].d = d; best[k].s = s;
        for (; k > 0 && best[k].d < best[k - 1].d; k--) { __typeof__(best[0]) t = best[k]; best[k] = best[k - 1]; best[k - 1] = t; }
    }
    if (!nb) return;
    Log("nearest spawned entities (not you):");
    for (int i = 0; i < nb; i++)
        Log("  %.1f m: entity %u, type %016llX, simulated here %s", (double)best[i].d, best[i].s.entity, (unsigned long long)best[i].s.resource,
            best[i].s.authority ? "yes" : "no");
}

static void Tick(HfMod* mod, float dt, void* user)
{
    static LARGE_INTEGER q0, qf;
    static uint64_t t0;
    if (!qf.QuadPart) { QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&q0); t0 = __rdtsc(); }
    else
    {
        LARGE_INTEGER q;
        QueryPerformanceCounter(&q);
        double ms = (double)(q.QuadPart - q0.QuadPart) * 1000.0 / (double)qf.QuadPart;
        if (ms > 500) g_tscPerMs = (double)(__rdtsc() - t0) / ms;
    }
    static uint64_t seenSet, seenFlag;
    if (original_flag) Report("LaunchFlag", g_flag, &g_flagN, &seenFlag, 0);
    if (original_set) Report("SetPosition", g_set, &g_setN, &seenSet, 1);
    static double clock, next = 10;
    clock += dt > 0 && dt < 1 ? dt : 0;
    if (clock >= next && hf->game_state(self) == HF_STATE_MISSION) { next = clock + 10; Nearest(); }
}

static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    game = hf->module_base("game.dll", &gameSize);
    exe = hf->module_base("helldivers2.exe", &exeSize);
    if (!game || !exe || gameSize != 74727424u || exeSize != 60719104u) { Log("REFUSED: not build " BUILD "'s modules"); return; }
    /* warp */
    Dump("the warp's update, around its move (0x88D795)", game, 0x88C800, 0x1000);
    Dump("SetPosition", game, 0x5A2BB0, 0x200);
    Dump("LaunchFlag", game, 0x5A3D80, 0x100);
    Dump("ConsistentVaulting's stand check", game, 0xA9D560, 0x100);
    /* summon */
    Dump("the entity spawn (HD2Runtime's 0xFDC140)", game, 0xFDC140, 0x400);
    Dump("the entity destroy (HD2Runtime's 0xFDC820)", game, 0xFDC820, 0x200);
    uintptr_t script = P(game + 0x3326308), units = script ? P(script + 0x18) : 0;
    if (units)
    {
        char w[48];
        Log("the unit function table at %s (EXO Beacon: +0x00 local_position, +0x38 set_local_position):", Where(units, w, sizeof w));
        for (int i = 0; i < 160; i++)
        {
            uintptr_t f = P(units + 8u * i);
            if (!f || f < exe || f >= exe + exeSize) { Log("  +0x%03X ends the table (%s)", i * 8, Where(f, w, sizeof w)); break; }
            uint8_t b[16];
            char hex[40] = "";
            if (!hf->read_code(f, b, 16)) for (int k = 0; k < 16; k++) snprintf(hex + 2 * k, 3, "%02x", b[k]);
            Log("  +0x%03X %s %s", i * 8, Where(f, w, sizeof w), hex);
        }
        if (P(units) == exe + 0x1fc450) g_localPos = (LocalPosFn)(exe + 0x1fc450);
    }
    uintptr_t worldApi = script ? P(script + 0x08) : 0;
    if (worldApi)
    {
        char w[48];
        Log("the world function table at %s (EXO Beacon: +0x280 create particles, +0x2A0 destroy them; a spawn_unit would be here):",
            Where(worldApi, w, sizeof w));
        for (int i = 0; i < 160; i++)
        {
            uintptr_t f = P(worldApi + 8u * i);
            if (!f || f < exe || f >= exe + exeSize) { Log("  +0x%03X ends the table (%s)", i * 8, Where(f, w, sizeof w)); break; }
            uint8_t b[16];
            char hex[40] = "";
            if (!hf->read_code(f, b, 16)) for (int k = 0; k < 16; k++) snprintf(hex + 2 * k, 3, "%02x", b[k]);
            Log("  +0x%03X %s %s", i * 8, Where(f, w, sizeof w), hex);
        }
    }
    if (hf->code_matches(game + 0xFD9D40, "48 83 ec 08 3b 0d 02 9f 4a 02 0f 84 9b 00 00 00")) g_resolve = (ResolveFn)(game + 0xFD9D40);
    /* hooks */
    if (hf->hook(self, game + 0x5A2BB0, (void*)SetDetour, (void**)&original_set)) { original_set = NULL; Log("SetPosition: hook refused"); }
    if (hf->hook(self, game + 0x5A3D80, (void*)FlagDetour, (void**)&original_flag)) { original_flag = NULL; Log("LaunchFlag: hook refused"); }
    if (hf->code_matches(exe + 0x7F4590, "48 8b c4 f3 0f 11 58 20 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48"))
    {
        g_rays = (Ray*)VirtualAlloc(NULL, sizeof(Ray) * RAYS, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!g_rays || hf->hook(self, exe + 0x7F4590, (void*)RayDetour, (void**)&original_ray)) { original_ray = NULL; Log("raycast: hook refused"); }
    }
    else Log("the raycast is not the reviewed code: no raycast timing");
    Log("probing: SetPosition %s, LaunchFlag %s, raycast %s, positions %s. Wear a LIFT-182 Warp Pack and warp a few times.",
        original_set ? "hooked" : "OFF", original_flag ? "hooked" : "OFF", original_ray ? "hooked" : "OFF", g_resolve && g_localPos ? "yes" : "no");
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 15 || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    Log("Summon & Warp Probe " VERSION " (research only, no gameplay effect)");
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    if (!api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) || !api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) ||
        !api->on_game_thread(mod, Tick, NULL))
        Log("events or the game thread are unavailable");
    return 0;
}
