/* Equip Probe (yui.equip-probe): research for an invisibility ability whose price is travelling light (it works on
 * enemies only with no backpack and no support weapon carried). Log only: nothing is changed. Host or solo.
 *
 * What it logs, in a mission:
 *   on every change  the local Helldiver's inventory record ([game+0x3326738]: map +0x28, records +0x50, 0x30 each;
 *                    HD2Runtime's layout, as Stim Probe read it) field by field, each entity it names with its resource:
 *                    Stim Probe's logs showed +0 the primary, +4 the secondary, +8 a support weapon once picked up,
 *                    +0x10 a held throwable or stim, +0x1C the selection; where a backpack sits is the question
 *   every spawn      kept for 10 s; on each record change (and on Ctrl+Alt+F11) the spawns of the last 3 s within 6 m of
 *                    her are logged with their resources: a backpack put on or a weapon picked up may spawn an entity
 *   what she holds   HF_ON_WEAPON's selection and resource
 * The test (you): land, then pick up and drop a support weapon, put on and drop a backpack (a Supply Pack, a jump pack,
 * a Guard Dog), call one in from the loadout if you can, and press Ctrl+Alt+F11 while wearing each.
 * Credit: the inventory layout and the entity and scene-graph reads are HD2Runtime's research (SkyeShade) and Helldiver
 * Survival Rework's (MIT), as the kit base reads them. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include <windows.h>
#include "hellforge_mod.h"

#define BUILD "25480438"
#define VERSION "0.1.0"
static const HfApi* hf;
static HfMod* self;
static uintptr_t game, exe;
static atomic_uint g_avatar;
static int g_ready;
static double g_t;
#define Log(...) hf->log(self, __VA_ARGS__)
static int RD(uintptr_t a, void* out, size_t n) { return a >= 0x10000 && a < 0x7FFFFFFF0000ull && hf->read(a, out, n) == 0; }
static uintptr_t P(uintptr_t a) { uintptr_t v = 0; return RD(a, &v, 8) && v >= 0x10000 && v < 0x7FFFFFFF0000ull ? v : 0; }
static uint32_t W(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t Q(const uint8_t* p) { uint64_t v; memcpy(&v, p, 8); return v; }

static int Lookup(uintptr_t at, uint32_t key, uint32_t limit, uint32_t* index)
{
    uint8_t h[20], row[8];
    if (!RD(at, h, sizeof h)) return 0;
    uintptr_t entries = (uintptr_t)Q(h);
    uint32_t n = W(h + 8), empty = W(h + 12), mult = W(h + 16);
    if (!entries || !n || n > limit || (n & (n - 1)) || key == empty) return 0;
    for (unsigned i = 0; i < n && i < 128; i++)
    {
        if (!RD(entries + 8u * ((key * mult + i) & (n - 1)), row, 8)) return 0;
        if (W(row) == key) { *index = W(row + 4); return *index != UINT32_MAX; }
        if (W(row) == empty) return 0;
    }
    return 0;
}
/* an entity's descriptor: resource +0, entity +8, unit +12 */
static int Descriptor(uint32_t entity, uint8_t out[24])
{
    if (!game || !entity || entity == UINT32_MAX || entity == 0x7FFF) return 0;
    uintptr_t root = P(game + 0x346BF98);
    uint32_t index;
    if (!root || !Lookup(root + 0xF1AEB0, entity, 1048576, &index) || index >= 262144) return 0;
    return RD(root + 0xF32F18 + 24u * index, out, 24) && W(out + 8) == entity;
}
static int UnitRoot(uint32_t unit, float out[3])
{
    uintptr_t registry = P(exe + 0x1A100F0), generations, objects, object, nodes;
    uint32_t count, id, slot = unit & 0x3FFFFF;
    uint8_t generation;
    if (!registry || !RD(registry + 0x98, &count, 4) || count > 0x400000 || slot >= count ||
        !(generations = P(registry + 0xA0)) || !RD(generations + slot, &generation, 1) || generation != (uint8_t)(unit >> 22) ||
        !(objects = P(registry + 0x88)) || !(object = P(objects + 8u * slot)) || !RD(object + 8, &id, 4) || id != unit ||
        !RD(object + 0x70, &count, 4) || !count || count > 4096 || !(nodes = P(object + 0x88)))
        return 0;
    return RD(nodes + 48, out, 12);
}
static int EntityPos(uint32_t e, float out[3]) { uint8_t d[24]; return Descriptor(e, d) && UnitRoot(W(d + 12), out); }
static const char* Res(uint32_t e, char* buf, size_t n)
{
    uint8_t d[24];
    if (!e) { snprintf(buf, n, "-"); return buf; }
    if (Descriptor(e, d)) snprintf(buf, n, "%u (%016llx)", e, (unsigned long long)Q(d));
    else snprintf(buf, n, "%u (no descriptor)", e);
    return buf;
}

/* ---- spawns, kept 10 s ----------------------------------------------------------------------------------------- */
#define RING 256
typedef struct { uint32_t e; uint64_t res; double t; } Spawn;
static Spawn g_ring[RING];
static unsigned g_ringN;
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    (void)mod; (void)user;
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    Spawn* r = &g_ring[g_ringN++ % RING];
    r->e = s->entity; r->res = s->resource; r->t = g_t;
}
static void NearSpawns(const char* why, double within)
{
    float me[3], p[3];
    if (!EntityPos(atomic_load(&g_avatar), me)) { Log("  spawns (%s): her position is unknown", why); return; }
    int n = 0;
    for (unsigned k = 0; k < RING; k++)
    {
        Spawn* r = &g_ring[k];
        if (!r->e || g_t - r->t > within || !EntityPos(r->e, p)) continue;
        float dx = p[0] - me[0], dy = p[1] - me[1], dz = p[2] - me[2], d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d > 6) continue;
        Log("  spawn %.1f s ago, %.1f m from her: entity %u, resource %016llx", g_t - r->t, d, r->e, (unsigned long long)r->res);
        n++;
    }
    Log("  spawns (%s): %d within 6 m in the last %.0f s", why, n, within);
}

/* ---- her inventory record -------------------------------------------------------------------------------------- */
static uint8_t g_rec[0x30];
static int g_have;
static int Record(uint8_t rec[0x30], uint8_t cnt[8])
{
    uint32_t me = atomic_load(&g_avatar), i;
    uintptr_t m = P(game + 0x3326738);
    if (!me || !m || !Lookup(m + 0x28, me, 1048576, &i) || i > 4096) return 0;
    uintptr_t recs = P(m + 0x50), counts = P(m + 0x58);
    return recs && counts && RD(recs + 0x30ull * i, rec, 0x30) && RD(counts + 8ull * i, cnt, 8);
}
static void LogRecord(const char* why, const uint8_t rec[0x30], const uint8_t cnt[8])
{
    static const char* kName[7] = {"+0 (primary)", "+4 (secondary)", "+8 (support?)", "+0xC", "+0x10 (held item?)", "+0x14", "+0x18"};
    char b[64];
    Log("[%s] record: selection +0x1C %u, +0x20 %u, +0x24 %u, +0x28 %016llx; counts %u / %u", why, W(rec + 0x1C), W(rec + 0x20), W(rec + 0x24),
        (unsigned long long)Q(rec + 0x28), W(cnt), W(cnt + 4));
    for (int k = 0; k < 7; k++)
    {
        uint32_t v = W(rec + 4 * k);
        int changed = g_have && v != W(g_rec + 4 * k);
        if (v || changed) Log("  %s: %s%s", kName[k], Res(v, b, sizeof b), changed ? "  <- changed" : "");
    }
}
static void Inventory(void)
{
    uint8_t rec[0x30], cnt[8];
    if (!Record(rec, cnt)) return;
    /* the held-item slot (+0x10) and the selection change with every throw and stim: only the other fields count here */
    uint8_t a[0x30], b[0x30];
    memcpy(a, rec, 0x30); memcpy(b, g_rec, 0x30);
    memset(a + 0x10, 0, 4); memset(b + 0x10, 0, 4); memset(a + 0x1C, 0, 4); memset(b + 0x1C, 0, 4);
    if (g_have && !memcmp(a, b, 0x30)) { memcpy(g_rec, rec, 0x30); return; }
    LogRecord(g_have ? "changed" : "first", rec, cnt);
    if (g_have) NearSpawns("at the change", 3);
    memcpy(g_rec, rec, 0x30);
    g_have = 1;
}

/* ---- events ------------------------------------------------------------------------------------------------------ */
static void OnWeapon(HfMod* mod, const HfEventInfo* info, void* user)
{
    (void)mod; (void)user;
    const HfWeaponEvent* w = (const HfWeaponEvent*)info;
    if (w->avatar) atomic_store(&g_avatar, w->avatar);
    if (w->entity != w->previous)
        Log("holding: selection %u, entity %u (%016llx) (before: selection %u, %u)", w->selection, w->entity, (unsigned long long)w->resource,
            w->previous_selection, w->previous);
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    (void)mod; (void)event; (void)user;
    size_t gs = 0, xs = 0;
    game = hf->module_base("game.dll", &gs);
    exe = hf->module_base("helldivers2.exe", &xs);
    if (!game || !exe || gs != 74727424u || xs != 60719104u) { g_ready = -1; Log("REFUSED: not build " BUILD "'s modules"); return; }
    g_ready = 1;
}
static void Tick(HfMod* mod, float dt, void* user)
{
    (void)mod; (void)user;
    g_t += dt > 0 && dt < 1 ? dt : 0;
    static int inMission;
    int m = g_ready == 1 && hf->game_state(self) == HF_STATE_MISSION;
    if (m != inMission) { inMission = m; g_have = 0; Log(m ? "mission start" : "mission end"); }
    if (!m) return;
    Inventory();
    static int prev;
    int d = (GetAsyncKeyState(VK_F11) & 0x8000) && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000);
    if (d && !prev)
    {
        uint8_t rec[0x30], cnt[8];
        if (Record(rec, cnt)) LogRecord("Ctrl+Alt+F11", rec, cnt);
        NearSpawns("Ctrl+Alt+F11", 10);
    }
    prev = d;
}
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 15 || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    Log("Equip Probe " VERSION ": log only; her inventory record on each change, the spawns near her then, what she holds; Ctrl+Alt+F11 logs them now");
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) && api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) &&
             api->on_game_thread(mod, Tick, NULL);
    if (!ok) Log("events or the game thread are unavailable");
    return 0;
}
