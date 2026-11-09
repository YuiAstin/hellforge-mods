/* Stratagem Probe (yui.stratagem-probe): research for Commando Plus's "a Resupply at the reinforcement pods".
 * Host or solo, build 25480438 only.
 *
 * A stratagem is delivered where its beacon was activated: game.dll+0x6ABDB0 (ActivateStratagemBeacon, SDK reference:
 * "hook or call"; HellForge's HF_ON_STRATAGEM comes from it) takes the beacon's position and starts the delivery.
 * This probe hooks it and, for every Resupply (type 33) thrown, in the mode stratagem-probe.ini sets (mode = 0, 1 or 2):
 *   0 observe    logs the call (its arguments, the beacon record's first bytes before and after) and changes nothing
 *   1 redirect   passes another position: the nearest reinforcement-pod site that has had none (Commando), else 20 m
 *                east of the beacon (any mission). Does the pod land there instead?
 *   2 copies     calls it as the game did, then once more for every pod site (Commando), else once 20 m east. Do more
 *                pods arrive from one throw?
 * (default 1). Every other stratagem is only logged.
 *
 * 0.2.1: every Resupply is taken (0.2.0 took only "yours" and compared the thrower, a player entity, with your Helldiver:
 * none was ever yours), and the mode is the ini's (Ctrl+Alt+F3 flipped by itself while stratagems were being called).
 * After each beacon it follows what spawns within 60 m of the beacon and of the position(s) it asked for, for 25 s:
 * where each lands (stops moving) and how far from the asked position. Everything that spawns within 8 m of you is
 * logged too (the thrown ball, for a later "call it without throwing" test).
 *
 * 0.2.0, Yui's idea: Call In Destroyer is a stratagem too, and it can be called while the Destroyer is away. Two test
 * stratagems in the loadout picker (supply section), each a reward ammo rack with 5 uses, 5 s cooldown:
 *   Rack Test D (D U D U D U L)  a copy of Call In Destroyer whose delivery, payload, package, traits and beacon flags are
 *                                the reward rack's (written into its row once the game has loaded the stratagems)
 *   Rack Test O (D U D U D U R)  a copy of the reward rack (Ammo Rack President Reward) given Call In Destroyer's
 *                                origin (+0x70) and call-in type (+0x74), the fields most likely to say "needs the ship"
 * Both rows are logged before and after (delivery, origin, call-in type, cooldown type, payload, traits...). In Commando:
 * take them in the loadout and try them after the Destroyer has left. Credit: the entity and unit lookups'
 * layouts are HD2Runtime's research (SkyeShade), as Commando Plus uses them; the function is in HellForge's SDK
 * reference (from HD2Runtime 0.40.1's research). */
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "hellforge_mod.h"

#define BUILD "25480438"
#define VERSION "0.3.2"
static const HfApi* hf;
static HfMod* self;
static uintptr_t game, exe;
static int g_ready;
static double g_now;
static unsigned g_lines;
#define LOG_CAP 2000
#define Log(...) do { if (g_lines++ < LOG_CAP) hf->log(self, __VA_ARGS__); else if (g_lines == LOG_CAP + 1) hf->log(self, "log cap reached: quiet from here"); } while (0)

static uint32_t Word(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t Quad(const uint8_t* p) { uint64_t v; memcpy(&v, p, 8); return v; }
static int AddressOk(uintptr_t a, size_t n) { return n && a >= 0x10000 && a < UINT64_C(0x7FFFFFFF0000) && n <= UINT64_C(0x7FFFFFFF0000) - a; }
static int ReadAt(uintptr_t a, void* out, size_t n) { return AddressOk(a, n) && hf->read(a, out, n) == 0; }
static uintptr_t Pointer(uintptr_t a) { uintptr_t v = 0; return ReadAt(a, &v, 8) && AddressOk(v, 1) ? v : 0; }
static int Lookup(uintptr_t at, uint32_t key, uint32_t limit, uint32_t* index)
{
    uint8_t h[20], row[8];
    if (!ReadAt(at, h, sizeof h)) return 0;
    uintptr_t entries = (uintptr_t)Quad(h);
    uint32_t n = Word(h + 8), empty = Word(h + 12), mult = Word(h + 16);
    if (!AddressOk(entries, 8) || !n || n > limit || (n & (n - 1)) || key == empty) return 0;
    for (unsigned i = 0; i < n && i < 128; i++)
    {
        if (!ReadAt(entries + 8u * ((key * mult + i) & (n - 1)), row, 8)) return 0;
        if (Word(row) == key) { *index = Word(row + 4); return *index != UINT32_MAX; }
        if (Word(row) == empty) return 0;
    }
    return 0;
}
static uint32_t UnitOf(uint32_t entity)
{
    uintptr_t root = Pointer(game + 0x346BF98);
    uint32_t index;
    uint8_t d[24];
    if (!root || !entity || entity == 0x7FFF || entity == UINT32_MAX || !Lookup(root + 0xF1AEB0, entity, 1048576, &index) ||
        index >= 262144 || !ReadAt(root + 0xF32F18 + 24u * index, d, 24) || Word(d + 8) != entity) return 0;
    return Word(d + 12);
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
static uintptr_t NodeOf(uint32_t entity)
{
    uint32_t unit = UnitOf(entity);
    uintptr_t registry = Pointer(exe + 0x1A100F0), objects, object, nodes;
    uint32_t count, id, slot = unit & 0x3FFFFF;
    if (!unit || !registry || !ReadAt(registry + 0x98, &count, 4) || slot >= count || !(objects = Pointer(registry + 0x88)) ||
        !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit || !(nodes = Pointer(object + 0x88)))
        return 0;
    return nodes;
}
static float Dist(const float a[3], const float b[3]) { float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2]; return sqrtf(dx * dx + dy * dy + dz * dz); }
static float Flat(const float a[3], const float b[3]) { float dx = a[0] - b[0], dy = a[1] - b[1]; return sqrtf(dx * dx + dy * dy); }

/* ---- spawns (any thread) -> the game thread ---------------------------------------------------------------------- */
#define RING 2048
static uint32_t g_ringEnt[RING];
static uint64_t g_ringRes[RING];
static atomic_uint g_ringHead;
static unsigned g_ringTail;
static atomic_uint g_avatar;
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* e = (const HfSpawnEvent*)info;
    unsigned i = atomic_fetch_add(&g_ringHead, 1) % RING;
    g_ringEnt[i] = e->entity;
    g_ringRes[i] = e->resource;
}
static void OnWeapon(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfWeaponEvent* w = (const HfWeaponEvent*)info;
    if (w->avatar) atomic_store(&g_avatar, w->avatar);
}
static void OnLand(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    if (p->local && p->entity) atomic_store(&g_avatar, p->entity);
}
static void OnStratagem(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfStratagemEvent* s = (const HfStratagemEvent*)info;
    Log("HF_ON_STRATAGEM: type %u by entity %u at %.1f %.1f %.1f", s->type, s->thrower, s->position[0], s->position[1], s->position[2]);
}

/* ---- the reinforcement-pod sites (Commando) ------------------------------------------------------------------------ */
#define POD_SWITCH UINT64_C(0xE8EF41881DAB0E9F)
#define INFIL_POD UINT64_C(0x6B5D00CD9AB8CB74)
#define RESPAWN_POD UINT64_C(0x5FAD42DF0A24C7ED)
#define RESUPPLY 33u
#define MAXSITE 8
typedef struct { uint32_t entity; float pos[3]; int placed, served; } Site;
static Site g_site[MAXSITE];
static int g_nsite;
static void SiteTarget(const Site* s, float out[3]) { out[0] = s->pos[0] + 6; out[1] = s->pos[1]; out[2] = s->pos[2]; }

/* ---- watches: positions a delivery was asked for, and what spawned near them ----------------------------------- */
#define MAXWATCH 12
typedef struct { float pos[3]; double until; char label[40]; } Watch;
static Watch g_watch[MAXWATCH];
static unsigned g_nwatch;
static void AddWatch(const float p[3], const char* label)
{
    Watch* w = &g_watch[g_nwatch++ % MAXWATCH];
    memcpy(w->pos, p, 12);
    w->until = g_now + 25;
    snprintf(w->label, sizeof w->label, "%s", label);
}
#define MAXTRACK 48
typedef struct { uint32_t entity; uint64_t res; float first[3], last[3]; double t0, lastMove, next; int watch, moved, done; } Track;
static Track g_track[MAXTRACK];
static unsigned g_ntrack;

/* ---- the hook --------------------------------------------------------------------------------------------------- */
#define BALL UINT64_C(0x16F397CA5F51F271)
static uint32_t g_balls[8];
static unsigned g_nballs;
static uint32_t g_moved;
static float g_movedTo[3];
static double g_movedAt;
typedef uint64_t (*ActivateFn)(void*, int32_t, uint32_t, const float*, void*, uint32_t, uint64_t, uint64_t, uint64_t, uint64_t);
static ActivateFn g_orig;
static int s_mode = 1;
static const char* const kMode[3] = {"observe", "redirect", "copies"};
static void RecordBytes(const char* when, void* record)
{
    uint8_t b[96];
    char hex[96 * 3 + 1];
    if (!record || !ReadAt((uintptr_t)record, b, sizeof b)) { Log("  record %s: unreadable (%p)", when, record); return; }
    for (unsigned i = 0; i < sizeof b; i++) snprintf(hex + 3 * i, 4, "%02x ", b[i]);
    Log("  record %s: %s", when, hex);
}
static int NearestUnserved(const float from[3])
{
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < g_nsite; i++)
        if (g_site[i].placed && !g_site[i].served)
        {
            float d = Flat(g_site[i].pos, from);
            if (d < bd) { bd = d; best = i; }
        }
    return best;
}
static uint64_t Detour(void* manager, int32_t element, uint32_t thrower, const float* position, void* record, uint32_t type,
                       uint64_t s7, uint64_t s8, uint64_t s9, uint64_t s10)
{
    float p[3] = {0, 0, 0};
    if (position) memcpy(p, position, 12);
    int mine = 1;   /* every Resupply (solo or host) */
    Log("beacon: type %u by entity %u%s at %.1f %.1f %.1f | manager %p element %d record %p | %llx %llx %llx %llx", type, thrower,
        "", p[0], p[1], p[2], manager, element, record, (unsigned long long)s7, (unsigned long long)s8,
        (unsigned long long)s9, (unsigned long long)s10);
    AddWatch(p, "the beacon");
    if (type != RESUPPLY || !mine || s_mode == 0 || !position)
    {
        RecordBytes("before", record);
        uint64_t r = g_orig(manager, element, thrower, position, record, type, s7, s8, s9, s10);
        RecordBytes("after", record);
        Log("  -> %llx (as the game called it)", (unsigned long long)r);
        return r;
    }
    if (s_mode == 1)
    {
        /* 0.3.0: 0.2.2 passed another position: the game reported it, but the pod and its boxes landed at the ball
         * (the beacon follows the thrown ball, 16f397ca5f51f271). So: the game's own call, then the ball is moved
         * with the game's warp setter (the one Voymastina moves her Helldiver and exosuit with). */
        float to[3] = {p[0] + 20, p[1], p[2]};
        int site = NearestUnserved(p);
        if (site >= 0) { SiteTarget(&g_site[site], to); g_site[site].served = 1; }
        uint64_t r = g_orig(manager, element, thrower, position, record, type, s7, s8, s9, s10);
        uint32_t ball = 0;
        float bp[3], bd = 3;
        for (unsigned k = 0; k < 8; k++)
        {
            float q[3];
            if (g_balls[k] && Position(g_balls[k], q) && Dist(q, p) < bd) { bd = Dist(q, p); ball = g_balls[k]; memcpy(bp, q, 12); }
        }
        if (!ball) { Log("  move: the ball was not found within 3 m of the beacon: nothing moved"); return r; }
        /* 0.3.1: the warp setter crashed on the ball (0.3.0, game.dll+0x5a2c69: like a Strider's pilot in Voymastina);
         * now the ball's world position in the scene graph is written, every frame for 1.5 s */
        Log("  move: the ball (entity %u) at %.1f %.1f %.1f, written to %.1f %.1f %.1f (%s, %.0f m) for 1.5 s", ball, bp[0], bp[1], bp[2],
            to[0], to[1], to[2], site >= 0 ? "a pod site" : "20 m east", Flat(to, p));
        AddWatch(to, site >= 0 ? "the pod site" : "20 m east");
        g_moved = ball; memcpy(g_movedTo, to, 12); g_movedAt = g_now;
        return r;
    }
    /* copies: the game's call, then one more per site (or one 20 m east) */
    RecordBytes("before", record);
    uint64_t r = g_orig(manager, element, thrower, position, record, type, s7, s8, s9, s10);
    RecordBytes("after", record);
    Log("  -> %llx (the game's own call)", (unsigned long long)r);
    int copies = 0;
    for (int i = 0; i < g_nsite && copies < 4; i++)
    {
        if (!g_site[i].placed) continue;
        float to[3];
        SiteTarget(&g_site[i], to);
        AddWatch(to, "a copy at a pod site");
        uint64_t c = g_orig(manager, element, thrower, to, record, type, s7, s8, s9, s10);
        Log("  copy %d: at %.1f %.1f %.1f (pod site %d, %.0f m away) -> %llx", ++copies, to[0], to[1], to[2], i, Flat(to, p), (unsigned long long)c);
    }
    if (!copies)
    {
        float to[3] = {p[0] + 20, p[1], p[2]};
        AddWatch(to, "a copy 20 m east");
        uint64_t c = g_orig(manager, element, thrower, to, record, type, s7, s8, s9, s10);
        Log("  copy: at %.1f %.1f %.1f (20 m east) -> %llx", to[0], to[1], to[2], (unsigned long long)c);
    }
    RecordBytes("after the copies", record);
    return r;
}

/* ---- 0.2.0: the two test racks ---------------------------------------------------------------------------- */
static uint32_t g_typeD, g_typeO;
static int g_rowsDone;
static void Row(const char* name, uintptr_t row)
{
    uint8_t r[400];
    if (!row || !ReadAt(row, r, sizeof r)) { Log("%s: row unreadable", name); return; }
    uint64_t pay = Quad(r + 0x98), pk = Quad(r + 0xA8);
    uint32_t n = Word(r + 0xA0);
    char ents[200] = "";
    for (uint32_t i = 0; i < n && i < 8; i++)
    {
        uint64_t e = 0;
        if (pay && ReadAt((uintptr_t)pay + 8u * i, &e, 8)) snprintf(ents + strlen(ents), sizeof ents - strlen(ents), " %016llx", (unsigned long long)e);
    }
    float cit, cd;
    memcpy(&cit, r + 0x54, 4); memcpy(&cd, r + 0x68, 4);
    Log("%s: type %u delivery %u uses %d call-in %.1f s cooldown %.0f s origin %u call-in type %u cooldown type %u hud %u traits %08x beacon %02x "
        "package %016llx payload %u:%s", name, Word(r), Word(r + 0x3C), (int)Word(r + 0x50), cit, cd, Word(r + 0x70), Word(r + 0x74),
        Word(r + 0x94), Word(r + 0xB8), Word(r + 0x104), r[0x170], (unsigned long long)pk, n, ents);
}
/* 0.2.2: HellForge copies only stratagems players can pick (0.2.0's copies of Call In Destroyer and of the reward rack
 * were refused), so both test racks are copies of a pickable stratagem (0.3.0: the B-1 Supply Pack; the Resupply is
 * not pickable either), and the vanilla rows of Call In Destroyer (123) and the
 * reward rack (109) are found next to them (the table's 400-byte rows; checked by type and stable id) and copied in. */
/* 0.3.1: the copies' rows sit in HellForge's own table, not beside the game's (0.3.0 found nothing within 400 rows), so
 * Call In Destroyer's row is found by scanning the process's readable memory for its type and stable id (once). */
static uintptr_t ScanRow(uint32_t type, uint32_t stable)
{
    uint8_t* a = (uint8_t*)0x10000;
    MEMORY_BASIC_INFORMATION mi;
    unsigned regions = 0;
    double t0 = (double)GetTickCount64();
    while ((uintptr_t)a < UINT64_C(0x7FFFFFFF0000) && VirtualQuery(a, &mi, sizeof mi) == sizeof mi)
    {
        uint8_t* base = (uint8_t*)mi.BaseAddress;
        size_t size = mi.RegionSize;
        int ok = mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                 (mi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) && size < (512u << 20);
        if (ok)
        {
            regions++;
            static uint8_t buf[(1u << 20) + 400];
            for (size_t off = 0; off < size; off += 1u << 20)
            {
                size_t n = size - off < (1u << 20) + 400 ? size - off : (1u << 20) + 400;
                if (n < 400 || hf->read((uintptr_t)(base + off), buf, n)) continue;
                for (size_t i = 0; i + 400 <= n; i += 4)
                {
                    const uint8_t* r = buf + i;
                    if (Word(r) == type && Word(r + 4) == stable && Word(r + 0x3C) <= 7 && Word(r + 0x48) >= 1 && Word(r + 0x48) <= 12)
                    {
                        Log("scan: row of type %u found at %p (%u regions, %.0f ms)", type, (void*)(base + off + i), regions, (double)GetTickCount64() - t0);
                        return (uintptr_t)(base + off + i);
                    }
                }
            }
        }
        a = base + size;
    }
    Log("scan: no row of type %u (%u regions, %.0f ms)", type, regions, (double)GetTickCount64() - t0);
    return 0;
}
static int PutRange(uintptr_t to, uintptr_t from, uint32_t a, uint32_t b, const char* what)
{
    uint8_t buf[400];
    if (b <= a || b > 400 || !ReadAt(from + a, buf, b - a) || hf->write(to + a, buf, b - a)) { Log("  %s: could not be written", what); return 0; }
    return 1;
}
static void TestRows(void)
{
    if (g_rowsDone || !g_typeD || !g_typeO) return;
    uintptr_t d = hf->stratagem_row(g_typeD), o = hf->stratagem_row(g_typeO);
    if (!d || !o) return;   /* not loaded yet */
    g_rowsDone = 1;
    Row("Rack Test D as added", d);
    Row("Rack Test O as added", o);
    uintptr_t call = ScanRow(123, 0xf8f90965u);
    if (!call) { Log("Call In Destroyer's row was not found: the test racks stay plain (the Resupply's boxes by hellpod)"); return; }
    Row("Call In Destroyer", call);
    /* 0.3.2: the vanilla rows beside it, to compare: what can be called while the Destroyer is away (Reinforce, the
     * Destroyer call) and what cannot (the Resupply, the reward rack) */
    static const struct { uint32_t type, stable; const char* name; } kCmp[] = {
        {124, 0x87147bdbu, "Reinforcement Beacon (callable without the Destroyer)"},
        {33, 0x33babe96u, "Resupply (locked without it)"},
        {109, 0x4d36b84cu, "the reward rack (locked without it)"},
        {101, 0x71b6d7dau, "B-1 Supply Pack (the copies' base)"}};
    for (unsigned i = 0; i < sizeof kCmp / sizeof kCmp[0]; i++)
    {
        uint8_t h[8];
        uintptr_t r = 0;
        for (int k = -300; k <= 300 && !r; k++)
        {
            uintptr_t at = call + (intptr_t)k * 400;
            if (ReadAt(at, h, 8) && Word(h) == kCmp[i].type && Word(h + 4) == kCmp[i].stable) r = at;
        }
        if (r) Row(kCmp[i].name, r);
        else Log("%s: not within 300 rows of Call In Destroyer's", kCmp[i].name);
    }
    /* 0.3.1's O (Call In Destroyer's origin, call-in type and cooldown type) was still locked out, and its D (all of
     * Call In Destroyer's row but the drop) did not show in the loadout. Now: D gets only the HUD section "mission"
     * (Call In Destroyer's: 1; the copies had 2, supply), O gets that and the origin, call-in type and cooldown type. */
    int ok = PutRange(d, call, 0xB8, 0xBC, "D hud");
    ok &= PutRange(o, call, 0x70, 0x78, "O origin and call-in type") & PutRange(o, call, 0x94, 0x98, "O cooldown type") & PutRange(o, call, 0xB8, 0xBC, "O hud");
    Log(ok ? "the test racks are set" : "the test racks: some writes failed (see above)");
    Row("Rack Test D now", d);
    Row("Rack Test O now", o);
}

/* ---- the game thread -------------------------------------------------------------------------------------------- */
static int g_inMission;
static void NewSpawns(void)
{
    unsigned head = atomic_load(&g_ringHead);
    if (head - g_ringTail > RING) g_ringTail = head - RING;
    float me[3];
    uint32_t av = atomic_load(&g_avatar);
    int haveMe = av && Position(av, me);
    static unsigned nearTold;
    for (; g_ringTail != head; g_ringTail++)
    {
        unsigned i = g_ringTail % RING;
        uint32_t e = g_ringEnt[i];
        uint64_t res = g_ringRes[i];
        float p[3];
        if (res == POD_SWITCH && g_nsite < MAXSITE)
        {
            Site* s = &g_site[g_nsite++];
            memset(s, 0, sizeof *s);
            s->entity = e;
        }
        if (res == BALL) g_balls[g_nballs++ % 8] = e;
        if (!Position(e, p)) continue;
        int w = -1;
        float wd = 1e9f;
        for (unsigned k = 0; k < MAXWATCH && k < g_nwatch; k++)
            if (g_now < g_watch[k].until)
            {
                float d = Flat(p, g_watch[k].pos);
                if (d < 60 && d < wd) { wd = d; w = (int)k; }
            }
        int nearMe = haveMe && Dist(p, me) < 8 && e != av;
        if (w < 0 && !nearMe) continue;
        if (w < 0 && nearTold++ >= 80) continue;
        if (w >= 0)
        {
            static uint64_t seenRes[64];
            static unsigned seenN[64];
            unsigned h = (unsigned)(res ^ (res >> 29)) % 64;
            if (seenRes[h] != res) { seenRes[h] = res; seenN[h] = 0; }
            if (++seenN[h] > 6) continue;
        }
        if (w >= 0) Log("spawn: entity %u (%016llx) at %.1f %.1f %.1f, %.0f m across from %s", e, (unsigned long long)res, p[0], p[1], p[2], wd, g_watch[w].label);
        else Log("spawn near you: entity %u (%016llx) at %.1f %.1f %.1f, %.1f m from you", e, (unsigned long long)res, p[0], p[1], p[2], Dist(p, me));
        Track* t = &g_track[g_ntrack++ % MAXTRACK];
        memset(t, 0, sizeof *t);
        t->entity = e; t->res = res; t->watch = w;
        memcpy(t->first, p, 12); memcpy(t->last, p, 12);
        t->t0 = t->lastMove = g_now; t->next = g_now + 0.5;
    }
}
static void Follow(void)
{
    for (unsigned k = 0; k < MAXTRACK && k < g_ntrack; k++)
    {
        Track* t = &g_track[k];
        if (!t->entity || t->done || g_now < t->next) continue;
        t->next = g_now + 0.5;
        float p[3];
        if (!Position(t->entity, p)) { t->done = 1; Log("  entity %u (%016llx) gone after %.1f s", t->entity, (unsigned long long)t->res, g_now - t->t0); continue; }
        if (Dist(p, t->last) > 0.3f) { t->moved = 1; t->lastMove = g_now; memcpy(t->last, p, 12); continue; }
        if (g_now - t->lastMove >= 2 || g_now - t->t0 > 30)
        {
            t->done = 1;
            if (t->watch >= 0) Log("  entity %u (%016llx) %s at %.1f %.1f %.1f (%.1f s): %.1f m across from %s", t->entity, (unsigned long long)t->res,
                                   t->moved ? "came to rest" : "stays", p[0], p[1], p[2], g_now - t->t0, Flat(p, g_watch[t->watch].pos), g_watch[t->watch].label);
            else Log("  entity %u (%016llx) %s at %.1f %.1f %.1f (%.1f s), %.1f m from where it spawned", t->entity, (unsigned long long)t->res,
                     t->moved ? "came to rest" : "stays", p[0], p[1], p[2], g_now - t->t0, Dist(p, t->first));
        }
    }
}
static void Tick(HfMod* mod, float dt, void* user)
{
    g_now += dt > 0 && dt < 1 ? dt : 0;
    if (g_ready != 1) return;
    int m = hf->game_state(self) == HF_STATE_MISSION;
    if (m != g_inMission)
    {
        g_inMission = m;
        g_nsite = 0; g_nwatch = 0; g_ntrack = 0;
        memset(g_site, 0, sizeof g_site);
        memset(g_watch, 0, sizeof g_watch);
        memset(g_track, 0, sizeof g_track);
        g_ringTail = atomic_load(&g_ringHead);
        Log(m ? "mission start: mode %d (%s; stratagem-probe.ini)" : "mission end", s_mode, kMode[s_mode]);
    }
    TestRows();
    if (!m) return;
    for (int i = 0; i < g_nsite; i++)
        if (!g_site[i].placed && Position(g_site[i].entity, g_site[i].pos))
        {
            g_site[i].placed = 1;
            Log("pod site %d: the pods' switch (entity %u) at %.0f %.0f %.0f", i, g_site[i].entity, g_site[i].pos[0], g_site[i].pos[1], g_site[i].pos[2]);
        }
    NewSpawns();
    Follow();
    if (g_moved && g_now - g_movedAt < 1.5)
    {
        uintptr_t n = NodeOf(g_moved);
        if (n) hf->write(n + 48, g_movedTo, 12);
    }
    if (g_moved && g_now - g_movedAt < 8)
    {
        static double next;
        float q[3];
        if (g_now >= next) { next = g_now + 1; if (Position(g_moved, q)) Log("  the moved ball at %.1f %.1f %.1f: %.1f m from where it was sent", q[0], q[1], q[2], Flat(q, g_movedTo)); else Log("  the moved ball: gone"); }
    }
    else g_moved = 0;
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gs = 0, xs = 0;
    game = hf->module_base("game.dll", &gs);
    exe = hf->module_base("helldivers2.exe", &xs);
    if (!game || !exe || gs != 74727424u || xs != 60719104u) { g_ready = -1; Log("REFUSED: not build " BUILD "'s modules"); return; }
    if (!hf->code_matches(game + 0x6ABDB0, "48 89 5c 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8d ac 24 b0 fc ff ff 48 81 ec 50 04 00 00"))
    { g_ready = -1; Log("REFUSED: ActivateStratagemBeacon (game.dll+0x6ABDB0) is not the reviewed code"); return; }
    if (hf->hook_ex(self, game + 0x6ABDB0, (void*)Detour, (void**)&g_orig, 0) || !g_orig)
    { g_ready = -1; g_orig = NULL; Log("REFUSED: ActivateStratagemBeacon could not be hooked"); return; }
    g_ready = 1;
    Log("ready: ActivateStratagemBeacon hooked; mode %d (%s)", s_mode, kMode[s_mode]);
}
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 15) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    Log("Stratagem Probe " VERSION " (research: where a Resupply is delivered)");
    {
        char path[1024], line[256];
        snprintf(path, sizeof path, "%s\\stratagem-probe.ini", api->mod_folder(mod));
        FILE* f = fopen(path, "r");
        if (f)
        {
            while (fgets(line, sizeof line, f))
            {
                int v;
                if (sscanf(line, " mode = %d", &v) == 1 && v >= 0 && v <= 2) s_mode = v;
            }
            fclose(f);
        }
        Log("settings: %s %s: mode %d (%s)", f ? "read" : "no", path, s_mode, kMode[s_mode]);
    }
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    {
        HfStratagem d = {sizeof d}, o = {sizeof o};
        d.base = 101; d.name = "yui/stratagem-probe/rack-test-d"; d.title = "Rack Test D"; d.code = "DUDUDUL"; d.uses = 5; d.cooldown = 5;
        d.description = "Research: Call In Destroyer's settings, dropping the reward ammo rack.";
        o.base = 101; o.name = "yui/stratagem-probe/rack-test-o"; o.title = "Rack Test O"; o.code = "DUDUDUR"; o.uses = 5; o.cooldown = 5;
        o.description = "Research: the reward ammo rack with Call In Destroyer's origin.";
        static const uint64_t boxes[4] = {UINT64_C(0xA94913CA014F7579), UINT64_C(0xA94913CA014F7579), UINT64_C(0xA94913CA014F7579), UINT64_C(0xA94913CA014F7579)};
        d.payload = o.payload = boxes;   /* the Resupply's (and the reward rack's) four boxes, by hellpod */
        d.payload_count = o.payload_count = 4;
        g_typeD = api->add_stratagem(mod, &d);
        g_typeO = api->add_stratagem(mod, &o);
        Log("test racks: Rack Test D type %u (DUDUDUL), Rack Test O type %u (DUDUDUR)%s", g_typeD, g_typeO,
            g_typeD && g_typeO ? "" : " (0: refused, the log above says why)");
    }
    if (!api->on_game_thread(mod, Tick, NULL)) { Log("the game thread could not be hooked"); return 1; }
    if (!api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) || !api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) ||
        !api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnLand, NULL, 0) || !api->subscribe(mod, HF_ON_STRATAGEM, OnStratagem, NULL, 0))
        Log("some events are unavailable");
    return 0;
}
