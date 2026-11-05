/* Seat Probe (yui.seat-probe): research for Koleda's FRV Sinner. Can a mod put the player straight into a vehicle seat,
 * and where is the FRV gun's muzzle? Static research (scratchpad frv-seat-research.md, from the game.dll dump of build
 * 25480438) found the game's own seat code; nothing here has run yet. Host or solo; turn it off when done.
 *
 *   always  your seat entry is logged whenever it changes (sit in each FRV seat by hand: that maps seat -> role -> node)
 *   F5      the nearest FRV (and its gun): for each seat index 0..7, its role (0x636E70) and node (0x636A30)
 *   Shift+F5 enter it the game's way: 0x63ACF0(NULL, you, frv, seat) (as a finished "get in" interaction; you play the
 *           get-in). The driver seat (role 1), or the seat Alt+F5 picked (each press: the next index, then the driver's again)
 *   Ctrl+F5 enter it instantly: the game's own spawn-already-seated sequence (0x639830's): set_entering with instant 1,
 *           the seat node, the peers told, one seater update
 *   Insert  the FRV gun: its muzzle node hash (ProjectileWeapon +0x21C), its nodes' positions from its root, the one
 *           furthest along its barrel. Shift+Insert turns one of its nodes 45 degrees (the next node each press) and logs
 *           whether it held half a second later (can the mod aim the gun with nobody in it?)
 *
 * Credit: the seat tables as Khi and RSNLRiggs (Apache - Rise Above) read them; Unit.set_local_rotation and
 * unit_resolve as Apache pins them; the entity and scene-graph reads are the kit base's (Helldiver Survival Rework,
 * MIT; HD2Runtime by SkyeShade). */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <windows.h>
#include "hellforge_mod.h"
#include "hellforge_content.h"

#define BUILD "25480438"
#define VERSION "0.1.0"
#include "kitbase/kb_core.h"

static atomic_uint g_avatar;
static int g_inMission;
static double g_now;

/* ---- FRVs and their guns, from the spawn event ------------------------------------------------------------------ */
#define FRV_M102 UINT64_C(0xCC21C7FFD3EBEFB9)
static int IsFrv(uint64_t r) { return r == FRV_M102 || r == HF_ENTITY_M103_SUPPLY_FRV || r == HF_ENTITY_M104_INCINERATOR_FRV; }
static uint32_t g_newEnt[128];
static uint64_t g_newRes[128];
static atomic_uint g_newHead;
static unsigned g_newTail;
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    unsigned h = atomic_load(&g_newHead);
    g_newEnt[h % 128] = s->entity;
    g_newRes[h % 128] = s->resource;
    atomic_store(&g_newHead, h + 1);
}
typedef struct { uint32_t frv, gun; uint64_t frvRes, gunRes; int after; } Car;
static Car g_car[16];
static int g_cars;
static void SortSpawns(void)
{
    for (int k = 0; k < 32 && g_newTail != atomic_load(&g_newHead); k++, g_newTail++)
    {
        unsigned i = g_newTail % 128;
        uint32_t e = g_newEnt[i], st;
        uint64_t r = g_newRes[i];
        /* the spawns right after an FRV: its gun is the first with a projectile weapon */
        for (int c = 0; c < g_cars; c++)
            if (g_car[c].after > 0)
            {
                g_car[c].after--;
                Log("  spawned after FRV %u: entity %u, resource %016llx%s%s", g_car[c].frv, e, (unsigned long long)r,
                    hf->component_record("ProjectileWeaponComponentData", r, &st) ? " (a projectile weapon)" : "",
                    hf->component_record("SeatCollectionComponentData", r, &st) ? " (seats)" : "");
                if (!g_car[c].gun && hf->component_record("ProjectileWeaponComponentData", r, &st)) { g_car[c].gun = e; g_car[c].gunRes = r; Log("  -> its gun"); }
            }
        if (IsFrv(r))
        {
            Car* c = &g_car[g_cars < 16 ? g_cars++ : 15];
            memset(c, 0, sizeof *c);
            c->frv = e; c->frvRes = r; c->after = 8;
            Log("FRV spawned: entity %u (resource %016llx)", e, (unsigned long long)r);
        }
    }
}
static void OnWeaponAv(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfWeaponEvent* w = (const HfWeaponEvent*)info;
    if (w->avatar && w->avatar != atomic_load(&g_avatar)) { atomic_store(&g_avatar, w->avatar); Log("your Helldiver: entity %u (weapon event)", w->avatar); }
}
static void OnPlayer(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    if (p->local && info->kind == HF_ON_PLAYER_SPAWN && p->entity) { atomic_store(&g_avatar, p->entity); Log("your Helldiver: entity %u", p->entity); }
}
static uint32_t UnitOf(uint32_t e) { uint8_t d[24]; return Descriptor(e, d) ? Word(d + 12) : 0; }
static int Alive(const Car* c) { uint8_t d[24]; return c->frv && Descriptor(c->frv, d) && Quad(d) == c->frvRes; }
static int Pos(uint32_t e, float out[3]) { uint32_t u = UnitOf(e); return u && UnitNode(u, 0, out); }
static float g_lastMe[3];
static Car* Nearest(float* dist)
{
    float me[3], p[3], bd = 1e12f;
    Car* best = NULL;
    if (Pos(atomic_load(&g_avatar), me)) memcpy(g_lastMe, me, 12);
    else memcpy(me, g_lastMe, 12);
    for (int i = 0; i < g_cars; i++)
    {
        if (!Alive(&g_car[i]) || !Pos(g_car[i].frv, p)) continue;
        float d2 = Dist2(me, p);
        if (d2 < bd) { bd = d2; best = &g_car[i]; }
    }
    if (dist) *dist = best ? sqrtf(bd) : -1;
    if (!best) Log("  no FRV known this mission (none seen spawning, or all gone)");
    return best;
}

/* ---- the game's seat code (scratchpad frv-seat-research.md; pinned below) ---------------------------------------- */
typedef int (*SeatRoleFn)(void* C, uint32_t coll, uint32_t unused, uint32_t entrance);
typedef uint32_t (*SeatNodeFn)(void* C, uint32_t coll, uint32_t seater, uint32_t entrance);
typedef void (*BeginEnterFn)(void* ignored, uint32_t seater, uint32_t coll, uint32_t entrance);
typedef void (*SetEnteringFn)(void* S, uint32_t index, uint32_t coll, uint32_t entrance, uint8_t instant);
typedef void (*RpcEnteringFn)(int peers, uint32_t seater, uint32_t coll, uint32_t entrance);
typedef void (*SetSeatNodeFn)(void* S, uint64_t unused, uint32_t coll, uint32_t seater, uint32_t node);
typedef void (*RpcSeatNodeFn)(int peers, uint32_t coll, uint32_t seater, uint32_t node);
typedef void (*SeaterUpdateFn)(void* S, uint32_t index);
static SeatRoleFn g_role;
static SeatNodeFn g_node;
static BeginEnterFn g_begin;
static SetEnteringFn g_setEntering;
static RpcEnteringFn g_rpcEntering;
static SetSeatNodeFn g_setNode;
static RpcSeatNodeFn g_rpcNode;
static SeaterUpdateFn g_update;
static uintptr_t SeaterMgr(void) { return Pointer(game + 0x3326D78); }
static uintptr_t CollMgr(void) { return Pointer(game + 0x3326D88); }
/* the seater manager as Apache reaches it (the command bus, entry type 117): logged once beside the direct read */
static uintptr_t BusSeaterMgr(void)
{
    uintptr_t bus = Pointer(game + 0x3326E68);
    uint32_t count = 0;
    if (!bus || !ReadAt(bus + 7948, &count, 4) || count > 512) return 0;
    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t type = 0;
        if (ReadAt(bus + 7952 + i * 16ull + 8, &type, 4) && type == 117) return Pointer(bus + 7952 + i * 16ull);
    }
    return 0;
}
static int SeaterIndex(uint32_t e, uint32_t* idx)
{
    uintptr_t S = SeaterMgr();
    return S && Lookup(S + 0x20, e, 1048576, idx) && *idx < 65536;
}
static uintptr_t SeatEntry(uint32_t e)
{
    uint32_t i;
    uintptr_t S = SeaterMgr(), entries;
    if (!SeaterIndex(e, &i) || !(entries = Pointer(S + 0x48))) return 0;
    return entries + 64ull * i;
}

/* ---- always: your seat entry, logged when it changes ------------------------------------------------------------- */
static uint8_t g_lastEntry[64];
static int g_haveEntry = -1;
static double g_watchUntil, g_nextWatchLog;
static void HexOf(const uint8_t* b, int n, char* out)
{
    for (int i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", b[i]);
}
static void WatchSeat(void)
{
    uint32_t me = atomic_load(&g_avatar);
    uintptr_t at = me ? SeatEntry(me) : 0;
    uint8_t e[64];
    int have = at && ReadAt(at, e, 64);
    if (!have) { if (g_haveEntry == 1) Log("seat: your seat entry no longer reads"); g_haveEntry = 0; return; }
    if (g_haveEntry == 1 && !memcmp(e, g_lastEntry, 64)) return;
    int watching = g_now < g_watchUntil;
    if (!watching && g_now < g_nextWatchLog && g_haveEntry == 1) return;   /* at most 4 a second outside a test */
    g_nextWatchLog = g_now + 0.25;
    char hex[129];
    HexOf(e, 64, hex);
    uint32_t vehicle = Word(e), role = Word(e + 8), entrance = Word(e + 0x10), node = Word(e + 0x14);
    Log("seat%s: vehicle %u, role %u, entrance %u, +0x14 %08x, +0x1C %08x, changing %u, instant %u | %s", watching ? " (test)" : "", vehicle, role, entrance,
        node, Word(e + 0x1C), e[0x30], e[0x32], hex);
    memcpy(g_lastEntry, e, 64);
    g_haveEntry = 1;
}

/* ---- F5: the seats of the nearest FRV and of its gun ---------------------------------------------------------------- */
static void Survey(uint32_t coll, const char* what)
{
    uintptr_t C = CollMgr();
    uint32_t idx = 0, me = atomic_load(&g_avatar);
    if (!C || !g_role || !g_node) { Log("F5: the seat code is not pinned: no survey"); return; }
    if (Lookup(C + 0x20, coll, 1048576, &idx) && idx < 65536)
    {
        uintptr_t recs = Pointer(C + 0x48), occ = Pointer(C + 0x50);
        uint32_t kind = 0;
        uint8_t mask[12] = {0};
        if (recs) ReadAt(recs + 0x64ull * idx, &kind, 4);
        if (occ) ReadAt(occ + 12ull * idx, mask, 12);
        char hex[25];
        HexOf(mask, 12, hex);
        Log("F5: %s %u: seat-collection record %u, kind %u, occupancy %s", what, coll, idx, kind, hex);
    }
    else { Log("F5: %s %u has no seat collection", what, coll); return; }
    for (uint32_t e = 0; e < 8; e++)
    {
        int role = g_role((void*)C, coll, 0, e);
        if (role == -1) { Log("  seat %u: none", e); continue; }
        uint32_t node = g_node((void*)C, coll, me, e);
        Log("  seat %u: role %d, node %u", e, role, node);
    }
}
static void SurveyNearest(void)
{
    float d;
    Car* c = Nearest(&d);
    if (!c) return;
    Log("F5: FRV %u (%016llx), %.1f m away; its gun %u (%016llx)", c->frv, (unsigned long long)c->frvRes, d, c->gun, (unsigned long long)c->gunRes);
    Survey(c->frv, "the FRV");
    if (c->gun) Survey(c->gun, "its gun");
}

/* ---- Shift+F5 / Ctrl+F5: entering ---------------------------------------------------------------------------------------- */
static int g_pick = -1;   /* -1: the driver's (role 1); else this seat index */
static int ChooseSeat(uint32_t coll, uint32_t* entrance, int* role)
{
    uintptr_t C = CollMgr();
    if (!C || !g_role) return 0;
    if (g_pick >= 0) { *entrance = (uint32_t)g_pick; *role = g_role((void*)C, coll, 0, *entrance); return *role != -1; }
    for (uint32_t e = 0; e < 8; e++) { int r = g_role((void*)C, coll, 0, e); if (r == 1) { *entrance = e; *role = r; return 1; } }
    return 0;
}
static int Ready(Car** out, uint32_t* entrance, int* role, uint32_t* idx, const char* key)
{
    uint32_t me = atomic_load(&g_avatar);
    float d;
    if (!me) { Log("%s: your Helldiver is not known yet (fire once)", key); return 0; }
    Car* c = Nearest(&d);
    if (!c) return 0;
    uintptr_t at = SeatEntry(me);
    uint32_t in = 0;
    if (!at || !SeaterIndex(me, idx)) { Log("%s: you have no seater entry (seater manager %p)", key, (void*)SeaterMgr()); return 0; }
    ReadAt(at, &in, 4);
    if (in && in == c->frv) { Log("%s: you are already in that FRV (get out first)", key); return 0; }
    if (!ChooseSeat(c->frv, entrance, role)) { Log("%s: FRV %u has no %s", key, c->frv, g_pick >= 0 ? "such seat" : "driver seat (role 1)"); return 0; }
    Log("%s: FRV %u, %.1f m away, seat %u (role %d); you are seater %u (entry +0 was %u)", key, c->frv, d, *entrance, *role, *idx, in);
    *out = c;
    return 1;
}
static void EnterGameWay(void)
{
    Car* c;
    uint32_t e, idx;
    int role;
    if (!g_begin) { Log("Shift+F5: 0x63ACF0 is not the reviewed code: not called"); return; }
    if (!Ready(&c, &e, &role, &idx, "Shift+F5")) return;
    g_watchUntil = g_now + 3;
    g_haveEntry = -1;
    g_begin(NULL, atomic_load(&g_avatar), c->frv, e);
    Log("Shift+F5: 0x63ACF0 called; your seat entry follows for 3 s");
}
static void EnterInstant(void)
{
    Car* c;
    uint32_t e, idx, me = atomic_load(&g_avatar);
    int role;
    if (!g_setEntering || !g_rpcEntering || !g_node || !g_setNode || !g_rpcNode || !g_update) { Log("Ctrl+F5: part of the instant sequence is not the reviewed code: not called"); return; }
    if (!Ready(&c, &e, &role, &idx, "Ctrl+F5")) return;
    uintptr_t S = SeaterMgr(), C = CollMgr(), at;
    g_watchUntil = g_now + 3;
    g_haveEntry = -1;
    g_setEntering((void*)S, idx, c->frv, e, 1);
    g_rpcEntering(-2, me, c->frv, e);
    if ((at = SeatEntry(me))) hf->write(at + 0x10, &e, 4);
    uint32_t node = g_node((void*)C, c->frv, me, e);
    g_setNode((void*)S, 0, c->frv, me, node);
    g_rpcNode(-2, c->frv, me, node);
    g_update((void*)S, idx);
    Log("Ctrl+F5: the instant sequence ran (seat node %u); your seat entry follows for 3 s", node);
}

/* ---- Insert: the gun's nodes and muzzle; Shift+Insert: turn one node -------------------------------------------------- */
typedef void (*SetLocalRotFn)(uint32_t unit, uint32_t node, const float* xyzw);
typedef uintptr_t (*UnitResolveFn)(uint32_t unit);
static SetLocalRotFn g_setRot;
static UnitResolveFn g_resolve;
static int UnitNodes(uint32_t unit, uint32_t* count, uintptr_t* nodes)
{
    uintptr_t registry = Pointer(exe + 0x1A100F0), objects, object;
    uint32_t n, slot = unit & 0x3FFFFF, id;
    if (!registry || !ReadAt(registry + 0x98, &n, 4) || slot >= n || !(objects = Pointer(registry + 0x88)) || !(object = Pointer(objects + 8u * slot)) ||
        !ReadAt(object + 8, &id, 4) || id != unit || !ReadAt(object + 0x70, count, 4) || *count > 4096 || !(*nodes = Pointer(object + 0x88)))
        return 0;
    return 1;
}
static void GunNodes(void)
{
    float d;
    Car* c = Nearest(&d);
    if (!c) return;
    if (!c->gun) { Log("Insert: FRV %u: its gun was not seen spawning", c->frv); return; }
    uint32_t st = 0, fire = 0, unit = UnitOf(c->gun), count = 0;
    uintptr_t pw = hf->component_record("ProjectileWeaponComponentData", c->gunRes, &st), nodes = 0;
    if (pw) ReadAt(pw + 0x21C, &fire, 4);
    Log("Insert: gun %u (%016llx) of FRV %u: fire node hash %08x (\"muzzle\" would be 2c8ece2b), unit %08x", c->gun, (unsigned long long)c->gunRes, c->frv, fire, unit);
    if (!unit || !UnitNodes(unit, &count, &nodes)) { Log("Insert: its unit's nodes do not read"); return; }
    float m0[16];
    if (!ReadAt(nodes, m0, 64)) return;
    int tip = -1;
    float best = -1e9f;
    for (uint32_t i = 0; i < count && i < 96; i++)
    {
        float m[16];
        if (!ReadAt(nodes + 64ull * i, m, 64)) break;
        float dx = m[12] - m0[12], dy = m[13] - m0[13], dz = m[14] - m0[14];
        float along = dx * m0[4] + dy * m0[5] + dz * m0[6];   /* along the root's forward row */
        Log("  node %u: %+.2f %+.2f %+.2f from the root (%.2f along its forward), forward %+.2f %+.2f %+.2f", i, dx, dy, dz, along, m[4], m[5], m[6]);
        if (along > best) { best = along; tip = (int)i; }
    }
    Log("Insert: %u nodes; the furthest along the root's forward: node %d (%.2f m)", count, tip, best);
}
static uint32_t g_turnNode;
static uint32_t g_turnUnit;
static float g_turnBefore[16];
static double g_turnCheckAt;
static void TurnNode(void)
{
    float d;
    Car* c = Nearest(&d);
    uint32_t count = 0;
    uintptr_t nodes = 0;
    if (!c || !c->gun) { Log("Shift+Insert: no gun known"); return; }
    if (!g_setRot || !g_resolve) { Log("Shift+Insert: Unit.set_local_rotation is not the reviewed code: not called"); return; }
    uint32_t unit = UnitOf(c->gun);
    if (!unit || !g_resolve(unit) || !UnitNodes(unit, &count, &nodes)) { Log("Shift+Insert: the gun's unit does not resolve"); return; }
    g_turnNode = g_turnNode + 1 < count ? g_turnNode + 1 : 1;
    ReadAt(nodes + 64ull * g_turnNode, g_turnBefore, 64);
    const float q[4] = {0, 0, 0.3826834f, 0.9238795f};   /* 45 degrees about its local Z */
    g_setRot(unit, g_turnNode, q);
    g_turnUnit = unit;
    g_turnCheckAt = g_now + 0.5;
    Log("Shift+Insert: gun node %u of %u turned 45 degrees about its Z (watch the gun)", g_turnNode, count);
}
static void TurnCheck(void)
{
    if (!g_turnCheckAt || g_now < g_turnCheckAt) return;
    g_turnCheckAt = 0;
    uint32_t count = 0;
    uintptr_t nodes = 0;
    float m[16];
    if (!UnitNodes(g_turnUnit, &count, &nodes) || g_turnNode >= count || !ReadAt(nodes + 64ull * g_turnNode, m, 64)) { Log("  (the node no longer reads)"); return; }
    float dot = m[4] * g_turnBefore[4] + m[5] * g_turnBefore[5] + m[6] * g_turnBefore[6];
    Log("  0.5 s later node %u forward %+.2f %+.2f %+.2f (was %+.2f %+.2f %+.2f): %s", g_turnNode, m[4], m[5], m[6], g_turnBefore[4], g_turnBefore[5], g_turnBefore[6],
        dot < 0.99f ? "turned and held" : "unchanged (overwritten, or this node's turn does not show here)");
}

/* ---- the frame -------------------------------------------------------------------------------------------------- */
static void Tick(HfMod* mod, float dt, void* user)
{
    g_now += dt > 0 && dt < 1 ? dt : 0;
    int in = hf->game_state(self) == HF_STATE_MISSION;
    if (in != g_inMission)
    {
        g_inMission = in;
        g_cars = 0;
        g_haveEntry = -1;
        g_turnCheckAt = 0;
        g_newTail = atomic_load(&g_newHead);
        if (in)
        {
            static int told;
            uintptr_t a = SeaterMgr(), b = BusSeaterMgr();
            if (!told++ || a != b) Log("mission: seater manager %p directly, %p through the command bus (%s); seat collections %p", (void*)a, (void*)b, a == b ? "the same" : "DIFFERENT", (void*)CollMgr());
        }
    }
    if (!in || !game) return;
    SortSpawns();
    { float me[3]; if (Pos(atomic_load(&g_avatar), me)) memcpy(g_lastMe, me, 12); }
    WatchSeat();
    TurnCheck();
    static int prev[2];
    int focused = Focused(), shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0, ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0,
        alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    int k[2] = {focused && (GetAsyncKeyState(VK_F5) & 0x8000), focused && (GetAsyncKeyState(VK_INSERT) & 0x8000)};
    if (k[0] && !prev[0])
    {
        if (alt) { g_pick = g_pick < 7 ? g_pick + 1 : -1; if (g_pick < 0) Log("Alt+F5: the seat to enter: the driver's (role 1)"); else Log("Alt+F5: the seat to enter: index %d", g_pick); }
        else if (ctrl) EnterInstant();
        else if (shift) EnterGameWay();
        else SurveyNearest();
    }
    if (k[1] && !prev[1]) { if (shift) TurnNode(); else GunNodes(); }
    memcpy(prev, k, sizeof prev);
}
static void Pin(void** slot, uintptr_t at, const char* pattern, const char* what)
{
    if (hf->code_matches(at, pattern)) *slot = (void*)at;
    else Log("%s is not the reviewed code: not called", what);
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gs = 0, xs = 0;
    uintptr_t g = hf->module_base("game.dll", &gs), x = hf->module_base("helldivers2.exe", &xs);
    if (!g || !x || gs != 74727424u || xs != 60719104u) { Log("REFUSED: not build " BUILD); return; }
    game = g;
    exe = x;
    Pin((void**)&g_begin, game + 0x63ACF0, "40 53 41 57 48 83 ec 38 3b 15 22 8f e4 02 45 8b f9 48 8b 1d 70 c0 ce 02", "begin entering (0x63ACF0)");
    Pin((void**)&g_setEntering, game + 0x63A7B0, "40 53 55 56 57 41 55 41 56 41 57 48 81 ec c0 00 00 00 48 8b 05 47 18 00 02", "set_entering (0x63A7B0)");
    Pin((void**)&g_role, game + 0x636E70, "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57 41 56 41 57 48 83 ec 20 33 ff 45 8b f1", "seat role (0x636E70)");
    Pin((void**)&g_node, game + 0x636A30, "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48 89 7c 24 20 41 55 41 56 41 57 48 83 ec 20 33 f6", "seat node (0x636A30)");
    Pin((void**)&g_setNode, game + 0x63E1A0, "45 85 c0 0f 84 91 03 00 00 4c 8b dc 53 57 41 54 48 83 ec 70 48 8b 05 55 de ff 01", "set seat node (0x63E1A0)");
    Pin((void**)&g_update, game + 0x639B40, "48 89 5c 24 18 55 56 57 41 54 41 55 41 56 41 57 48 81 ec 00 01 00 00 0f 29 b4 24 f0 00 00 00", "seater update (0x639B40)");
    Pin((void**)&g_rpcEntering, game + 0xBEA400, "48 89 5c 24 10 44 89 4c 24 20 57 48 83 ec 60 48 8b 05 fa 1b a5 01", "entering RPC (0xBEA400)");
    Pin((void**)&g_rpcNode, game + 0xBE22B0, "48 89 5c 24 10 44 89 4c 24 20 57 48 83 ec 60 48 8b 05 4a 9d a5 01", "seat node RPC (0xBE22B0)");
    /* Apache's pins: the Lua binding of Unit.set_local_rotation up to its call of the native, and unit_resolve */
    if (hf->code_matches(exe + 0x406740, "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 ba 01 00 00 00 48 8b f9 ff 15 3b 13 ff 00") &&
        hf->code_matches(exe + 0x406740 + 0x50, "e8 0b 60 df ff"))
        g_setRot = (SetLocalRotFn)(exe + 0x1FC7A0);
    else Log("Unit.set_local_rotation is not the code Apache decoded: Shift+Insert off");
    Pin((void**)&g_resolve, exe + 0x9D8C0, "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 48 8b 35 1a 28 97 01 8b d9 48 8d 8e d0 00 00 00", "unit_resolve (exe+0x9D8C0)");
    Log("ready: your seat is logged when it changes; F5 the nearest FRV's seats, Shift+F5 / Ctrl+F5 enter it (game way / instant), Alt+F5 the seat to enter, Insert its gun");
}
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 15 || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    Log("Seat Probe " VERSION ": research for Koleda's FRV Sinner (enter a seat from a mod; the FRV gun's muzzle)");
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_WEAPON, OnWeaponAv, NULL, 0) && api->on_game_thread(mod, Tick, NULL);
    if (!ok) Log("events or the game thread are unavailable");
    return 0;
}
