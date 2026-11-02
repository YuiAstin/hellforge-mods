/* Nav Probe (yui.nav-probe): tries the navigation queries CowboyBingus recorded (his notes, Nov 2026; addresses may be
 * outdated, so each is checked before use), for Koleda's Sinner (a mech walking on its own) and Voymastina's landings.
 *
 *   bind      logs the nav query service's slots and the code of each function (for reading the arguments later)
 *   F8        closest navmesh point (exe 0x7D89F0, slot +0x10 of the service at [game+0x3326330]; nav world
 *             [[game+0x33266C8]+0x22D0]): where you stand (radius 5 m) and 10 m ahead of the camera (radius 3 m)
 *   Ctrl+F8   set_destination (game 0x5B36C0(NULL, entity, &pos)) on the nearest live enemy with a navigation record
 *             ([game+0x3326418]): it is sent to where you stand; its distance to you is logged every second for 10 s
 *   Shift+F8  logs the code again and which nearby enemies have a navigation record
 *   Alt+F8    the stratagem ball's "is this on the navmesh?" check (slot +8, called as game 0x9305C0 calls it) where you
 *             stand and 10 m ahead
 * 0.1.1, from 0.1.0's code dumps: the nav world is a 32-bit id, not a pointer (0x7D89F0 takes it in ecx and indexes a
 * table of 128-byte worlds; game 0x930690 reads it from [game+0x3326D58]+0x10); the navigation records' hash is at
 * +0x30 (count +0x38, empty +0x3C, multiplier +0x40), records at +0x50, 0xA8 bytes each, "active" at +0x10 and a
 * position at +0x54 (set_destination 0x5B36C0 reads them so, and an entity without one indexes record 0xFFFFFFFF: the
 * crash Bingus saw). The road A* (0xFC6D60) is only dumped for now. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include <windows.h>
#include "hellforge_mod.h"

#define BUILD "25480438"
#define VERSION "0.1.2"
static const HfApi* hf;
static HfMod* self;
static uintptr_t game, exe;
static atomic_uint g_avatar;
static int g_ready;
static double g_now;
#define Log(...) hf->log(self, __VA_ARGS__)
static int RD(uintptr_t a, void* out, size_t n) { return a >= 0x10000 && a < 0x7FFFFFFF0000ull && hf->read(a, out, n) == 0; }
static uintptr_t P(uintptr_t a) { uintptr_t v = 0; return RD(a, &v, 8) && v >= 0x10000 && v < 0x7FFFFFFF0000ull ? v : 0; }
static uint32_t U32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }


typedef void* (*ResolveFn)(uint32_t);
typedef const float* (*LocalPosFn)(uint32_t, uint32_t);
typedef uint32_t (*ClosestFn)(uint32_t world, const float* pos, float radius, float* out, void* extra, uint32_t mask);
typedef uint32_t (*OnMeshFn)(uint32_t world, const float* pos, const void* extents, float a, float b, float* out, uint32_t mask);
typedef void (*SetDestFn)(void*, uint32_t, const float*);
static struct {
    ResolveFn entity_resolve, unit_resolve;
    LocalPosFn local_position;
    ClosestFn closest;
    OnMeshFn on_mesh;
    SetDestFn set_destination;
} fn;
static int Lookup(uintptr_t at, uint32_t key, uint32_t* index)
{
    uint8_t h[20], row[8];
    if (!RD(at, h, sizeof h)) return 0;
    uintptr_t entries; memcpy(&entries, h, 8);
    uint32_t n = U32(h + 8), empty = U32(h + 12), mult = U32(h + 16);
    if (!entries || !n || n > 1048576 || (n & (n - 1)) || key == empty) return 0;
    for (unsigned i = 0; i < n && i < 128; i++)
    {
        if (!RD(entries + 8u * ((key * mult + i) & (n - 1)), row, 8)) return 0;
        if (U32(row) == key) { *index = U32(row + 4); return *index != UINT32_MAX; }
        if (U32(row) == empty) return 0;
    }
    return 0;
}
typedef struct { uint64_t resource; uint32_t entity, unit, network, authority; } Ent;
static int Resolve(uint32_t entity, Ent* e)
{
    if (!entity || !fn.entity_resolve) return 0;
    void* o = fn.entity_resolve(entity);
    uint8_t h[24];
    if (!o || !RD((uintptr_t)o, h, 24) || U32(h + 8) != entity) return 0;
    memcpy(&e->resource, h, 8);
    e->entity = entity; e->unit = U32(h + 12); e->network = U32(h + 16); e->authority = U32(h + 20) & 1;
    return 1;
}
static int RootOf(uint32_t unit, float out[3])
{
    if (!fn.local_position || !fn.unit_resolve || !fn.unit_resolve(unit)) return 0;
    const float* p = fn.local_position(unit, 0);
    return p && RD((uintptr_t)p, out, 12) && isfinite(out[0]) && isfinite(out[2]);
}
static int CameraAt(uintptr_t w, float m[16])
{
    if (!w || !RD(w + 0x230920, m, 64)) return 0;
    for (int k = 0; k < 16; k++) if (!isfinite(m[k])) return 0;
    if (fabsf(m[15] - 1) > 1e-3f || fabsf(m[3]) > 1e-3f || fabsf(m[7]) > 1e-3f) return 0;
    for (int r = 0; r < 3; r++) if (fabsf(m[4 * r] * m[4 * r] + m[4 * r + 1] * m[4 * r + 1] + m[4 * r + 2] * m[4 * r + 2] - 1) > 0.02f) return 0;
    return 1;
}
static int CameraForward(uint32_t unit, float f[3])
{
    uintptr_t registry = P(exe + 0x1A100F0), objects, object;
    uint32_t slot = unit & 0x3FFFFF, id = 0;
    float m[16];
    if (!registry || !(objects = P(registry + 0x88)) || !(object = P(objects + 8u * slot)) || !RD(object + 8, &id, 4) || id != unit) return 0;
    if (!CameraAt(P(object + 0x18), m)) return 0;
    memcpy(f, m + 4, 12);
    return 1;
}

#define SEEN 2048
static uint32_t g_seenEnt[SEEN];
static uint64_t g_seenRes[SEEN];
static unsigned g_nseen;
static int HealthOf(uint32_t entity, int32_t* health)
{
    uintptr_t m = P(game + 53634696u);
    uint8_t h[88];
    if (!entity || !m || !RD(m + 4112, h, sizeof h)) return 0;
    uint32_t live = U32(h + 16), cap = U32(h + 40), empty = U32(h + 44), mult = U32(h + 48);
    uintptr_t buckets, records;
    memcpy(&buckets, h + 32, 8); memcpy(&records, h + 72, 8);
    if (!buckets || !records || !cap || cap > 65536 || (cap & (cap - 1)) || live > 4096) return 0;
    for (uint32_t k = 0; k < cap; k++)
    {
        uint8_t b[8];
        if (!RD(buckets + 8u * ((entity * mult + k) & (cap - 1)), b, 8)) return 0;
        uint32_t key = U32(b), index = U32(b + 4);
        if (key == empty) return 0;
        if (key != entity) continue;
        if (index >= live) return 0;
        uint8_t r[24];
        if (!RD(records + 440u * index, r, sizeof r)) return 0;
        memcpy(health, r + 20, 4);
        return 1;
    }
    return 0;
}
static int HostileRaw(uint64_t res)
{
    uint32_t stride;
    return res && hf->component_record("BehaviorComponentData", res, &stride) && !hf->component_record("AvatarComponentData", res, &stride) &&
           !hf->component_record("TurretComponentData", res, &stride) && res != 0x79E4B3D2DA5E45E3ull && res != 0xC2D449ECF7FACAB1ull && res != 0x7B2326F6FD9C8069ull && res != 0x35DBF54F016F3624ull && res != 0x75BE82ED8592A6B3ull;
}
static int Hostile(uint64_t res)
{
    static uint64_t keys[512];
    static int8_t vals[512];
    if (!res) return 0;
    unsigned h = (unsigned)(res ^ (res >> 31)) & 511;
    for (unsigned i = 0; i < 512; i++)
    {
        unsigned k = (h + i) & 511;
        if (keys[k] == res) return vals[k];
        if (!keys[k]) { keys[k] = res; vals[k] = (int8_t)HostileRaw(res); return vals[k]; }
    }
    return HostileRaw(res);
}

/* ---- binding ------------------------------------------------------------------------------------------------------- */
static void Dump(const char* label, uintptr_t at, int n)
{
    uint8_t b[512];
    if (n > 512) n = 512;
    if (hf->read_code(at, b, n)) { Log("code %s (+%llx): unreadable", label, (unsigned long long)(at - (at >= game && at < game + 0x8000000 ? game : exe))); return; }
    for (int o = 0; o < n; o += 64)
    {
        char hex[129];
        for (int k = 0; k < 64 && o + k < n; k++) snprintf(hex + 2 * k, 3, "%02x", b[o + k]);
        Log("code %s +%03x: %s", label, o, hex);
    }
}
static uintptr_t g_svc;
static int WorldA(uint32_t* w) { uintptr_t h = P(game + 0x33266C8); return h && RD(h + 0x22D0, w, 4); }
static int WorldB(uint32_t* w) { uintptr_t h = P(game + 0x3326D58); return h && RD(h + 0x10, w, 4); }
static int World(uint32_t* w)
{
    uint32_t a = 0, b = 0;
    int ha = WorldA(&a), hb = WorldB(&b);
    if (ha && hb && a != b) { static int told; if (told++ < 3) Log("the two nav world ids differ (%u, %u): using %u", a, b, a); }
    *w = ha ? a : b;
    return (ha || hb) && *w < 64;
}
static int Boundary(uintptr_t at)
{   /* a function start: the byte before it is padding or a return, and it is not padding itself */
    uint8_t b[2];
    return !hf->read_code(at - 1, b, 2) && (b[0] == 0xCC || b[0] == 0xC3 || b[0] == 0x90) && b[1] != 0xCC;
}
static void DumpAll(void)
{
    Dump("closest (exe 0x7D89F0)", exe + 0x7D89F0, 512);
    Dump("on-navmesh body (exe 0x7DA990)", exe + 0x7DA990, 256);
    Dump("on-navmesh (exe 0x7DA820)", exe + 0x7DA820, 256);
    Dump("deploy check (game 0x9305C0)", game + 0x9305C0, 384);
    Dump("set_destination (game 0x5B36C0)", game + 0x5B36C0, 320);
    Dump("road A* entry (game 0xFC6D60)", game + 0xFC6D60, 192);
}
static int Bind(void)
{
    if (!hf->code_matches(game + 0xFD9D40, "48 83 ec 08 3b 0d 02 9f 4a 02 0f 84 9b 00 00 00") ||
        !hf->code_matches(exe + 0x9D8C0, "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20")) { Log("REFUSED: entity/unit lookups are not the reviewed code"); return 0; }
    fn.entity_resolve = (ResolveFn)(game + 0xFD9D40);
    fn.unit_resolve = (ResolveFn)(exe + 0x9D8C0);
    uintptr_t script = P(game + 0x3326308), units = script ? P(script + 0x18) : 0;
    if (!units || P(units) != exe + 0x1fc450) { Log("the unit API is not filled yet"); return 0; }
    fn.local_position = (LocalPosFn)(exe + 0x1fc450);

    g_svc = P(game + 0x3326330);
    uintptr_t slots[8] = {0}, vt = g_svc ? P(g_svc) : 0;
    for (int i = 0; i < 8; i++) slots[i] = g_svc ? P(g_svc + 8u * i) : 0;
    Log("nav service [game+0x3326330] = %llx; slots +0..+0x38: %llx %llx %llx %llx %llx %llx %llx %llx", (unsigned long long)g_svc,
        (unsigned long long)slots[0], (unsigned long long)slots[1], (unsigned long long)slots[2], (unsigned long long)slots[3],
        (unsigned long long)slots[4], (unsigned long long)slots[5], (unsigned long long)slots[6], (unsigned long long)slots[7]);
    uintptr_t want = exe + 0x7D89F0, want2 = exe + 0x7DA820;
    int direct = slots[2] == want, viaVt = vt && P(vt + 0x10) == want;
    Log("slot +0x10 %s exe+0x7D89F0 (%s); slot +8 %s exe+0x7DA820", direct || viaVt ? "is" : "is NOT", direct ? "in the service itself" : viaVt ? "through its first pointer" : "-",
        (slots[1] == want2 || (vt && P(vt + 8) == want2)) ? "is" : "is NOT");
    if ((direct || viaVt) && Boundary(want)) fn.closest = (ClosestFn)want;
    else Log("closest navmesh point: off (the service does not hold it where the notes say)");
    if (Boundary(game + 0x5B36C0)) fn.set_destination = (SetDestFn)(game + 0x5B36C0);
    else Log("set_destination: off (game+0x5B36C0 is not a function start)");
    if ((slots[1] == want2 || (vt && P(vt + 8) == want2)) && Boundary(want2)) fn.on_mesh = (OnMeshFn)want2;
    uint32_t a = 0, b = 0;
    int ha = WorldA(&a), hb = WorldB(&b);
    Log("nav world id: [[game+0x33266C8]+0x22D0] = %s%u, [[game+0x3326D58]+0x10] = %s%u; nav records [game+0x3326418] = %llx", ha ? "" : "? ", a,
        hb ? "" : "? ", b, (unsigned long long)P(game + 0x3326418));
    DumpAll();
    Log("bound: F8 %s, Alt+F8 %s, Ctrl+F8 %s", fn.closest ? "on" : "off", fn.on_mesh ? "on" : "off", fn.set_destination ? "on" : "off");
    return 1;
}

/* ---- F8: closest navmesh point ---------------------------------------------------------------------------------------- */
static int Physics(void)
{   /* the same quiet moment the raycast needs: no physics jobs running */
    uintptr_t scheduler = P(game + 0x347D7E0);
    uint32_t count, jobs[72];
    if (!scheduler || !RD(scheduler, &count, 4) || count || !RD(scheduler + 0x40008, jobs, sizeof jobs)) return 0;
    for (unsigned i = 0; i < 24; i++) if (jobs[3 * i + 2] != 1) return 0;
    return 1;
}
static void Closest(const char* what, const float at[3], float radius)
{
    uint32_t world;
    if (!World(&world)) { Log("F8 %s: no nav world id", what); return; }
    _Alignas(16) float pos[4] = {at[0], at[1], at[2], 0};
    _Alignas(16) float out[16];
    _Alignas(16) uint8_t extra[64];
    for (int i = 0; i < 16; i++) out[i] = -12345.0f;
    memset(extra, 0xAB, sizeof extra);
    uint32_t r = fn.closest(world, pos, radius, out, extra, 0);
    int changed = 0;
    for (int i = 0; i < 64; i++) if (extra[i] != 0xAB) changed++;
    float dx = out[0] - at[0], dy = out[1] - at[1], dz = out[2] - at[2];
    Log("F8 %s at %.2f %.2f %.2f (radius %.0f, world %u): returned %x; out %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f -> %.2f m away (%.2f up); extra: %d of 64 bytes written, first %08x %08x",
        what, at[0], at[1], at[2], (double)radius, world, r, out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
        sqrtf(dx * dx + dy * dy + dz * dz), dz, changed, U32(extra), U32(extra + 4));
}
/* 0.1.1 found no navmesh anywhere with the stratagem ball's own arguments (0.5, 15, its agent hash 0x49B287D9): 0.1.2
 * tries four variants, without the agent filter and with a 5 m first float, and logs the third argument's data */
static void OnMesh(const char* what, const float at[3])
{
    uint32_t world;
    float a, b;
    if (!World(&world)) { Log("Alt+F8 %s: no nav world id", what); return; }
    if (!RD(game + 0x23C6AD0, &a, 4) || !RD(game + 0x23C75B0, &b, 4)) { Log("Alt+F8: the deploy check's constants are unreadable"); return; }
    static int toldArg;
    float third[8] = {0};
    if (!toldArg++ && RD(game + 0x3290530, third, sizeof third))
        Log("Alt+F8: the third argument (game+0x3290530) holds %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f (as hex %08x %08x)", third[0], third[1], third[2],
            third[3], third[4], third[5], third[6], third[7], U32((const uint8_t*)third), U32((const uint8_t*)third + 4));
    const struct { float a, b; uint32_t mask; const char* label; } v[4] = {
        {a, b, 0x49B287D9u, "as the game calls it"}, {a, b, 0, "no agent filter"}, {5.0f, b, 0, "5 m, no filter"}, {5.0f, b, 0x49B287D9u, "5 m, ball's agent"}};
    for (int k = 0; k < 4; k++)
    {
        _Alignas(16) float pos[4] = {at[0], at[1], at[2], 0}, out[8];
        for (int i = 0; i < 8; i++) out[i] = -12345.0f;
        uint32_t r = fn.on_mesh(world, pos, (const void*)(game + 0x3290530), v[k].a, v[k].b, out, v[k].mask);
        float dx = out[0] - at[0], dy = out[1] - at[1];
        Log("Alt+F8 %s, %s (world %u, %.2f %.2f, agent %08x): returned %u; nearest %.2f %.2f %.2f, %.2f m across, %.2f up", what, v[k].label, world,
            (double)v[k].a, (double)v[k].b, v[k].mask, r, out[0], out[1], out[2], out[0] == -12345.0f ? -1.0f : sqrtf(dx * dx + dy * dy),
            out[0] == -12345.0f ? 0.0f : out[2] - at[2]);
    }
}
static int g_f8Pending, g_f8Mode;
static void F8Frame(void)
{
    if (!g_f8Pending) return;
    if (!Physics() && g_f8Pending++ < 60) return;
    g_f8Pending = 0;
    Ent me;
    float p[3], f[3] = {1, 0, 0};
    if (!Resolve(atomic_load(&g_avatar), &me) || !RootOf(me.unit, p)) { Log("F8: your position is unknown"); return; }
    int cam = CameraForward(me.unit, f);
    float h = sqrtf(f[0] * f[0] + f[1] * f[1]);
    float ahead[3] = {p[0] + (h > 0.05f ? f[0] / h : 1) * 10, p[1] + (h > 0.05f ? f[1] / h : 0) * 10, p[2]};
    if (g_f8Mode == 1) { OnMesh("where you stand", p); OnMesh(cam ? "10 m ahead" : "10 m along +x", ahead); return; }
    Closest("where you stand", p, 5);
    Closest(cam ? "10 m ahead" : "10 m along +x", ahead, 3);
    float up[3] = {p[0], p[1], p[2] + 4};
    Closest("4 m above you", up, 6);
}

/* ---- Ctrl+F8: set_destination ------------------------------------------------------------------------------------- */
static int NavIndex(uint32_t e, uint32_t* i)
{
    uintptr_t m = P(game + 0x3326418);
    return m && Lookup(m + 0x30, e, i) && *i < 0x10000;
}
/* a record: "active" (+0x10, set_destination does nothing when 0) and its position (+0x54) */
static int NavRecord(uint32_t i, uint32_t* active, float pos[3])
{
    uintptr_t m = P(game + 0x3326418), recs = m ? P(m + 0x50) : 0;
    return recs && RD(recs + 0xA8ull * i + 0x10, active, 4) && RD(recs + 0xA8ull * i + 0x54, pos, 12);
}
/* every entity with a navigation record, read off the hash's buckets */
static int NavAll(uint32_t* ents, uint32_t* idx, int max)
{
    uintptr_t m = P(game + 0x3326418), entries;
    uint8_t h[20];
    if (!m || !RD(m + 0x30, h, sizeof h)) return 0;
    memcpy(&entries, h, 8);
    uint32_t n = U32(h + 8), empty = U32(h + 12);
    if (!entries || !n || n > 65536 || (n & (n - 1))) return 0;
    int k = 0;
    for (uint32_t i = 0; i < n && k < max; i++)
    {
        uint8_t row[8];
        if (!RD(entries + 8ull * i, row, 8)) return k;
        if (U32(row) == empty || U32(row + 4) == UINT32_MAX || U32(row + 4) >= 0x10000) continue;
        ents[k] = U32(row); idx[k] = U32(row + 4); k++;
    }
    return k;
}
static uint32_t g_sent;
static float g_sentTo[3];
static double g_sentNext, g_sentUntil;
static void Send(void)
{
    Ent me, e;
    float mp[3], p[3];
    if (!Resolve(atomic_load(&g_avatar), &me) || !RootOf(me.unit, mp)) { Log("Ctrl+F8: your position is unknown"); return; }
    uint32_t ents[512], idx[512], pick = 0, pi = 0, active = 0, nActive = 0;
    int n = NavAll(ents, idx, 512);
    float bd = 1e30f;
    for (int i = 0; i < n; i++)
    {
        if (!NavRecord(idx[i], &active, p) || !active || !isfinite(p[0])) continue;
        nActive++;
        float d = (p[0] - mp[0]) * (p[0] - mp[0]) + (p[1] - mp[1]) * (p[1] - mp[1]);
        if (d < bd && d > 100.0f && d < 150.0f * 150.0f) { bd = d; pick = ents[i]; pi = idx[i]; }
    }
    Log("Ctrl+F8: %d navigation records, %u active", n, nActive);
    if (!pick) { Log("Ctrl+F8: no active record between 10 and 150 m away"); return; }
    uint32_t check;
    if (!NavIndex(pick, &check) || check != pi) { Log("Ctrl+F8: entity %u's record moved: not sent", pick); return; }
    int known = Resolve(pick, &e);
    Log("Ctrl+F8: sending entity %u (%s%016llx, nav record %u, %.0f m away) to where you stand", pick, known ? "type " : "no unit, ",
        known ? (unsigned long long)e.resource : 0ull, pi, sqrtf(bd));
    _Alignas(16) float to[4] = {mp[0], mp[1], mp[2], 0};
    fn.set_destination(NULL, pick, to);
    Log("Ctrl+F8: set_destination returned");
    g_sent = pick; memcpy(g_sentTo, mp, 12); g_sentNext = g_now + 1; g_sentUntil = g_now + 10;
}
static void SendFrame(void)
{
    if (!g_sent || g_now < g_sentNext) return;
    g_sentNext = g_now + 1;
    Ent e;
    float p[3], q[3] = {0};
    uint32_t k, active = 0;
    int rec = NavIndex(g_sent, &k) && NavRecord(k, &active, q);
    int unit = Resolve(g_sent, &e) && RootOf(e.unit, p);
    if (!rec && !unit) { Log("  entity %u: gone", g_sent); g_sent = 0; return; }
    float* w = unit ? p : q;
    float dx = w[0] - g_sentTo[0], dy = w[1] - g_sentTo[1];
    Log("  entity %u: %.1f m from the spot (%s)%s", g_sent, sqrtf(dx * dx + dy * dy), unit ? "its unit" : "its record's position",
        rec ? "" : ", its navigation record is gone");
    if (g_now >= g_sentUntil) g_sent = 0;
}
static void Survey(void)
{
    {
        uint32_t ents[512], idx[512], active = 0;
        float mp[3] = {0}, q[3];
        Ent me, e;
        int haveMe = Resolve(atomic_load(&g_avatar), &me) && RootOf(me.unit, mp);
        int n = NavAll(ents, idx, 512), told = 0;
        Log("  %d navigation records", n);
        for (int i = 0; i < n && told < 30; i++)
        {
            if (!NavRecord(idx[i], &active, q)) continue;
            told++;
            int known = Resolve(ents[i], &e);
            float d = haveMe ? sqrtf((q[0] - mp[0]) * (q[0] - mp[0]) + (q[1] - mp[1]) * (q[1] - mp[1])) : -1;
            Log("  record %u: entity %u (%s%016llx), active %u, at %.1f %.1f %.1f (%.0f m)", idx[i], ents[i], known ? "type " : "no unit ",
                known ? (unsigned long long)e.resource : 0ull, active, q[0], q[1], q[2], d);
        }
    }
    Ent me, e;
    float mp[3], p[3];
    if (!Resolve(atomic_load(&g_avatar), &me) || !RootOf(me.unit, mp)) return;
    unsigned n = g_nseen < SEEN ? g_nseen : SEEN, told = 0;
    for (unsigned i = 0; i < n && told < 25; i++)
    {
        int32_t hp = 0; uint32_t k = 0;
        if (!Resolve(g_seenEnt[i], &e) || !RootOf(e.unit, p)) continue;
        float d = sqrtf((p[0] - mp[0]) * (p[0] - mp[0]) + (p[1] - mp[1]) * (p[1] - mp[1]));
        if (d > 60) continue;
        int nav = NavIndex(g_seenEnt[i], &k), alive = HealthOf(g_seenEnt[i], &hp);
        if (!nav && !Hostile(g_seenRes[i])) continue;
        told++;
        Log("  entity %u type %016llx %.0f m: %s, health %s%d, navigation record %s%u", g_seenEnt[i], (unsigned long long)g_seenRes[i], d,
            Hostile(g_seenRes[i]) ? "enemy" : "other", alive ? "" : "? ", hp, nav ? "#" : "none ", nav ? k : 0);
    }
}

/* ---- events and the game thread -------------------------------------------------------------------------------------- */
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* e = (const HfSpawnEvent*)info;
    g_seenEnt[g_nseen % SEEN] = e->entity; g_seenRes[g_nseen % SEEN] = e->resource; g_nseen++;
}
static void OnWeapon(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfWeaponEvent* w = (const HfWeaponEvent*)info;
    if (w->avatar) atomic_store(&g_avatar, w->avatar);
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gs = 0, xs = 0;
    game = hf->module_base("game.dll", &gs);
    exe = hf->module_base("helldivers2.exe", &xs);
    if (!game || !exe || gs != 74727424u || xs != 60719104u) { g_ready = -1; Log("REFUSED: not build " BUILD "'s modules"); return; }
    g_ready = 0;
}
static int Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
static void Tick(HfMod* mod, float dt, void* user)
{
    g_now += dt > 0 && dt < 1 ? dt : 0;
    if (g_ready < 0 || hf->game_state(self) != HF_STATE_MISSION) return;
    if (g_ready == 0)
    {
        static double nextTry;
        static int tries;
        if (g_now < nextTry) return;
        nextTry = g_now + 3;
        if (Bind()) g_ready = 1;
        else if (++tries >= 10) { g_ready = -1; Log("REFUSED after 10 tries in a mission"); }
        if (g_ready != 1) return;
    }
    F8Frame();
    SendFrame();
    static int prev;
    int d = Down(VK_F8);
    if (d && !prev)
    {
        if (Down(VK_CONTROL)) { if (fn.set_destination) Send(); else Log("Ctrl+F8: set_destination is off"); }
        else if (Down(VK_MENU)) { if (fn.on_mesh) { g_f8Mode = 1; g_f8Pending = 1; } else Log("Alt+F8: the on-navmesh check is off"); }
        else if (Down(VK_SHIFT)) { DumpAll(); Log("Shift+F8: enemies and navigation records within 60 m:"); Survey(); }
        else if (fn.closest) { g_f8Mode = 0; g_f8Pending = 1; }
        else Log("F8: the closest-point query is off");
    }
    prev = d;
}
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 15 || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    Log("Nav Probe " VERSION ": research only; F8 closest navmesh point, Alt+F8 on-navmesh check, Ctrl+F8 send the nearest nav-record entity to you, Shift+F8 dump and list nav records");
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) && api->on_game_thread(mod, Tick, NULL);
    if (!ok) Log("events or the game thread are unavailable");
    return 0;
}
