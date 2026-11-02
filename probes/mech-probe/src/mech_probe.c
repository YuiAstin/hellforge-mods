/* Mech Probe (yui.mech-probe): research for Voymastina's Sinbreaker (a copy of the EXO-55 Breakthrough). It logs; on a
 * key it tries one thing on the exosuit you pilot. Build 25480438 only; everything it calls is checked first.
 *
 *   always    your seat (vehicle, role, seat node), the exosuit's identity (resource, unit, network, simulated here),
 *             the entities spawned with it (its arms) and who holds each (Wieldable), the ability ids you, the suit and
 *             its arms run, HF_ON_WEAPON while seated, and hits near or by the suit (kind, dealer, owner, target)
 *   F9        a one-second charge: the suit's physics body is pushed toward where the camera looks at 12 m/s (an
 *             impulse per update to the speed wanted), then braked; speeds logged every 0.1 s, its mass estimated
 *   F10       a 3 m warp of the suit with the warp pack's position setter (game.dll+0x5A2BB0), position logged after
 *   F11       a thrust on each of the suit's spawned children (its arms): their root node moved 0.6 m along one axis
 *             for 0.3 s, then put back; each press tries the next axis (x, y, z)
 *
 * Methods and addresses:
 *   - seats: game+0x3326E68 command bus, entry type 117 (Apache - Rise Above by Riggs, after Khi's 1.2.75 Lua)
 *   - physics: entity_resolve, unit_resolve, components, valid, physical, owner, position, velocities, impulse, each
 *     pinned by its bytes and, where the engine physics table holds it, by that slot (Apache - Rise Above / Khi)
 *   - camera: World + 0x230920 (Khi; Apache - Rise Above's turret); unit object +0x18 (EXO Beacon Launcher)
 *   - unit API: [game+0x3326308] +0x18: local_position +0x00, set_local_position +0x38 (EXO Beacon Launcher, MIT,
 *     Copyright (c) 2026 enochlophobianumerophobia)
 *   - Wieldable game+0x3326730 (hash +24, wielders +56), abilities game+0x3326640 (hash +24, records +56, stride 224):
 *     HD2Runtime by SkyeShade's research */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include <windows.h>
#include "hellforge_mod.h"

#define BUILD "25480438"
static const HfApi* hf;
static HfMod* self;
static uintptr_t game, exe;
static atomic_uint g_avatar;
static int g_ready;   /* 1 bound, -1 refused */

#define Log(...) hf->log(self, __VA_ARGS__)
static int RD(uintptr_t a, void* out, size_t n) { return a >= 0x10000 && a < 0x7FFFFFFF0000ull && hf->read(a, out, n) == 0; }
static uintptr_t P(uintptr_t a) { uintptr_t v = 0; return RD(a, &v, 8) && v >= 0x10000 && v < 0x7FFFFFFF0000ull ? v : 0; }
static uint32_t U32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }

/* ---- the engine functions (Khi's signatures, as Apache - Rise Above checks them) ------------------------------- */
typedef void* (*ResolveFn)(uint32_t);
typedef int (*ComponentsFn)(uint32_t, uint32_t, void*);
typedef int (*ActorBoolFn)(uint32_t);
typedef uint32_t (*ActorOwnerFn)(uint32_t);
typedef void (*ActorVecFn)(uint32_t, float*);
typedef void (*ActorVelFn)(uint32_t, float*, float*);
typedef void (*ActorPushFn)(uint32_t, const float*, const float*);
typedef void (*SetVecFn)(void*, uint32_t, const float*);
typedef const float* (*LocalPosFn)(uint32_t, uint32_t);
typedef void (*SetLocalPosFn)(uint32_t, uint32_t, const float*);
static struct {
    ResolveFn entity_resolve, unit_resolve;
    ComponentsFn components;
    ActorBoolFn valid, physical;
    ActorOwnerFn owner;
    ActorVecFn position;
    ActorVelFn velocities;
    ActorPushFn impulse;
    SetVecFn set_position, set_velocity;
    uint32_t (*world_id)(const void*);
    uint32_t (*raycast)(uint32_t, const void*, const void*, float, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t);
    LocalPosFn local_position;
    SetLocalPosFn set_local_position;
} fn;
static const struct { const char* label; uint32_t rva; const char* hex; int exe; uint32_t slot; void** out; } kSig[] = {
    {"entity_resolve", 0xfd9d40u, "4883ec083b0d029f4a020f849b0000004c8b15412249024533c048895c241048", 0, ~0u, (void**)&fn.entity_resolve},
    {"unit_resolve", 0x9d8c0u, "48895c24084889742410574883ec20488b351a2897018bd9488d8ed0000000ff", 1, ~0u, (void**)&fn.unit_resolve},
    {"components", 0x7e1c20u, "48895c2420554883ec208bc14d8bd025ffff3f008bea4c8d0c40488b05ff3efe", 1, ~0u, (void**)&fn.components},
    {"valid", 0x7972f0u, "4883ec28e8b7deffff33c94885c00f95c18bc14883c428c3cccccccccccccccc", 1, 0x18, (void**)&fn.valid},
    {"physical", 0x797310u, "4883ec28e897deffff4885c0740b8b401083e0014883c428c34883c428c3cccc", 1, 0x00, (void**)&fn.physical},
    {"owner", 0x799980u, "4883ec28e827b8ffff4885c074088b400c4883c428c34883c428c3cccccccccc", 1, 0x10, (void**)&fn.owner},
    {"position", 0x7999a0u, "48895c240848896c24104889742420574883ec30488bea8bf1e8fa28b20033d2", 1, 0x50, (void**)&fn.position},
    {"velocities", 0x799540u, "48895c2408574883ec40498bd8448bd9488bfae858bcffff418bcb0f57db48c1", 1, 0xA8, (void**)&fn.velocities},
    {"impulse", 0x797cf0u, "48895c241048896c2418565741564883ec60498bf08bd94c8bf2e8a945b20033", 1, 0xC0, (void**)&fn.impulse},
};
static int Hex(const char* h, uint8_t* out)
{
    int n = 0;
    for (; h[0] && h[1]; h += 2) { unsigned v; sscanf(h, "%2x", &v); out[n++] = (uint8_t)v; }
    return n;
}
static int Bind(void)
{
    uintptr_t table = P(game + 0x3326338);
    if (table != exe + 0x27CD910) { Log("REFUSED: the engine physics table is not where Khi found it"); return 0; }
    for (size_t i = 0; i < sizeof kSig / sizeof kSig[0]; i++)
    {
        uintptr_t at = (kSig[i].exe ? exe : game) + kSig[i].rva;
        uint8_t want[32], got[32];
        int n = Hex(kSig[i].hex, want);
        if (hf->read_code(at, got, n) || memcmp(got, want, n)) { Log("REFUSED: %s does not match its signature", kSig[i].label); return 0; }
        if (kSig[i].slot != ~0u && P(table + kSig[i].slot) != at) { Log("REFUSED: %s is not in the physics table's slot 0x%X", kSig[i].label, kSig[i].slot); return 0; }
        *kSig[i].out = (void*)at;
    }
    if (hf->code_matches(game + 0x5A2BB0, "48 89 5c 24 08 48 89 6c 24 10 56 57 41 54 41 56 41 57 48 83 ec 40"))
        fn.set_position = (SetVecFn)(game + 0x5A2BB0);
    if (hf->code_matches(game + 0x4A7550, "48 89 5c 24 18 57 48 83 ec 20 3b 15 ?? ?? ?? ?? 48 8b 1d"))
        fn.set_velocity = (SetVecFn)(game + 0x4A7550);   /* the motion record's velocity (as Exo Dodge drives an exosuit) */
    if (hf->code_matches(exe + 0x79F860, "33 c0 4c 8d 05 c7 b2 01 02 0f 1f 80 00 00 00 00 49 39 0c c0 74 0c ff c0 83 f8 04 75 f3") &&
        hf->code_matches(exe + 0x7F4590, "48 8b c4 f3 0f 11 58 20 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48"))
    {   /* the game's raycast, as Helldiver Survival Rework (MIT) calls it: walls and ground for the stepped charge */
        fn.world_id = (uint32_t (*)(const void*))(exe + 0x79F860);
        fn.raycast = (uint32_t (*)(uint32_t, const void*, const void*, float, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t))(exe + 0x7F4590);
    }
    else Log("the warp pack's position setter is not the reviewed code: F10 is off");
    uintptr_t script = P(game + 0x3326308), units = script ? P(script + 0x18) : 0;
    if (units && P(units + 0x00) == exe + 0x1fc450 && P(units + 0x38) == exe + 0x1fc720)
    { fn.local_position = (LocalPosFn)(exe + 0x1fc450); fn.set_local_position = (SetLocalPosFn)(exe + 0x1fc720); }
    else Log("the unit API is not where EXO Beacon Launcher found it: F11 is off");
    Log("bound: seats, physics (impulse, velocities, position), %s, %s", fn.set_position ? "the warp setter" : "no warp",
        fn.set_local_position ? "the unit API" : "no unit API");
    return 1;
}

/* ---- the game's lookups ------------------------------------------------------------------------------------------- */
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
    if (!entity) return 0;
    void* o = fn.entity_resolve(entity);
    uint8_t h[24];
    if (!o || !RD((uintptr_t)o, h, 24) || U32(h + 8) != entity) return 0;
    memcpy(&e->resource, h, 8);
    e->entity = entity; e->unit = U32(h + 12); e->network = U32(h + 16); e->authority = U32(h + 20) & 1;
    return 1;
}
static uint32_t WielderOf(uint32_t e)
{
    uintptr_t m = P(game + 0x3326730), arr;
    uint32_t i, by = 0;
    if (!m || !Lookup(m + 24, e, &i) || i >= 0x10000 || !(arr = P(m + 56)) || !RD(arr + 4u * i, &by, 4)) return 0;
    return by;
}
static int Ability(uint32_t e, uint32_t* id, int* active)
{
    uintptr_t m = P(game + 0x3326640), recs;
    uint32_t i;
    uint8_t r[17];
    if (!m || !Lookup(m + 24, e, &i) || i >= 0x10000 || !(recs = P(m + 56)) || !RD(recs + 224u * i, r, 17)) return 0;
    *id = U32(r); *active = r[16] != 0;
    return 1;
}
static const char* MechName(uint64_t r)
{
    return r == 0x79E4B3D2DA5E45E3ull ? "EXO-45 Patriot" : r == 0xC2D449ECF7FACAB1ull ? "EXO-49 Emancipator" :
           r == 0x7B2326F6FD9C8069ull ? "EXO-51 Lumberer" : r == 0x35DBF54F016F3624ull ? "EXO-55 Breakthrough" : NULL;
}

/* ---- the seat (Khi's reading, as Apache - Rise Above does it) ---------------------------------------------------- */
typedef struct { int found; uint32_t avatar, vehicle, role, node; int changing; uint8_t raw[64]; } Seat;
static Seat ReadSeat(void)
{
    Seat s = {0};
    uintptr_t bus = P(game + 0x3326E68), manager = 0, seaters, entries;
    uint32_t count = 0;
    if (!bus || !RD(bus + 7948, &count, 4) || count > 512) return s;
    for (uint32_t i = 0; i < count && !manager; i++)
    {
        uint32_t type = 0;
        if (RD(bus + 7952 + i * 16ull + 8, &type, 4) && type == 117) manager = P(bus + 7952 + i * 16ull);
    }
    if (!manager || !RD(manager + 12, &count, 4) || count > 4096 || !(seaters = P(manager + 56)) || !(entries = P(manager + 72))) return s;
    for (uint32_t i = 0; i < count; i++)
    {
        uintptr_t object = P(seaters + i * 8ull);
        uint32_t flags = 0, avatar = 0;
        if (!object || !RD(object + 20, &flags, 4) || !(flags & 1) || !RD(object + 8, &avatar, 4)) continue;
        if (!RD(entries + i * 64ull, s.raw, 64)) return s;
        s.found = 1; s.avatar = avatar;
        s.vehicle = U32(s.raw); s.role = U32(s.raw + 8); s.node = U32(s.raw + 20); s.changing = s.raw[48] != 0;
        return s;
    }
    return s;
}

/* ---- the suit, its children and its physics body ------------------------------------------------------------------ */
typedef struct { uint32_t entity; uint64_t resource; double at; } Spawned;
static Spawned g_spawn[256];
static unsigned g_nspawn;
static double g_now;
#define SEEN 2048
static uint32_t g_seenEnt[SEEN];
static uint64_t g_seenRes[SEEN];
static unsigned g_nseen;
static uint32_t g_mech, g_mechUnit, g_actor, g_children[8];
static int g_nchildren;
static uint64_t g_mechRes;
static double g_mechSpawnAt;
static int RootOf(uint32_t unit, float out[3])
{
    if (!fn.local_position || !fn.unit_resolve || !fn.unit_resolve(unit)) return 0;
    const float* p = fn.local_position(unit, 0);
    return p && RD((uintptr_t)p, out, 12) && isfinite(out[0]) && isfinite(out[2]);
}
/* every physical actor the unit owns, logged with its position; the one nearest the unit's root is the suit's body */
static uint32_t FindActor(uint32_t unit)
{
    float root[3];
    int haveRoot = RootOf(unit, root);
    uint32_t best = 0;
    float bestD = 1e30f;
    if (!fn.unit_resolve(unit)) return 0;
    _Alignas(8) uint8_t buf[128] = {0};
    if (fn.components(unit, 127, buf) <= 0) return 0;
    for (int list = 0; list < 7; list++)
    {
        uint32_t count = U32(buf + 56 + list * 4);
        uintptr_t ids; memcpy(&ids, buf + list * 8, 8);
        if (count > 64) return 0;
        for (uint32_t k = 0; k < count && ids; k++)
        {
            uint32_t id;
            if (!RD(ids + k * 4ull, &id, 4)) return 0;
            if ((id & 0x20000000u) && fn.valid(id) && fn.owner(id) == unit && fn.physical(id))
            {
                float p[4] = {0};
                fn.position(id, p);
                float d = haveRoot ? (p[0] - root[0]) * (p[0] - root[0]) + (p[1] - root[1]) * (p[1] - root[1]) + (p[2] - root[2]) * (p[2] - root[2]) : 0;
                Log("  physics actor %08x at %.1f %.1f %.1f%s", id, p[0], p[1], p[2], haveRoot ? "" : " (no root to compare)");
                if (!best || d < bestD) { best = id; bestD = d; }
            }
        }
    }
    if (haveRoot) Log("  the suit's root is at %.1f %.1f %.1f; chosen actor %08x (%.1f m off)", root[0], root[1], root[2], best, sqrtf(bestD));
    if (haveRoot && bestD > 9.0f) { Log("  no actor within 3 m of the suit's root: F9 is off for this suit"); return 0; }
    return best;
}
static void Children(void)
{
    g_nchildren = 0;
    for (unsigned i = 0; i < 256 && g_nchildren < 8; i++)
    {
        Spawned* s = &g_spawn[i];
        if (!s->entity || s->entity == g_mech || fabs(s->at - g_mechSpawnAt) > 3.0) continue;
        Ent e;
        if (!Resolve(s->entity, &e)) continue;
        uint32_t by = WielderOf(s->entity);
        g_children[g_nchildren++] = s->entity;
        Log("  child entity %u resource %016llx unit %08x network %u: held by %u%s", s->entity, (unsigned long long)s->resource, e.unit,
            e.network, by, by == g_mech ? " (the suit)" : by == atomic_load(&g_avatar) ? " (you)" : by ? "" : " (nobody known)");
    }
}

/* ---- the camera (where the reticle points) --------------------------------------------------------------------- */
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

/* ---- the tests ------------------------------------------------------------------------------------------------------ */
static double g_chargeUntil, g_chargeLog;
static int g_chargeMode;   /* 0 velocity, 1 stepped, 2 jump */
static float g_chargeDir[2];
/* The nearest static thing along a ray (the suit's own unit skipped): its distance, len + 1 when clear, -1 when the
 * query can't be made (the physics jobs are running, or no raycast) */
static float Ray(uint32_t skip, const float from[3], const float dir[3], float len)
{
    uintptr_t world = P(game + 0x346BFA0), scheduler = P(game + 0x347D7E0);
    uint32_t count, jobs[72];
    if (!world || !fn.raycast || !fn.world_id || !scheduler || !RD(scheduler, &count, 4) || count || !RD(scheduler + 0x40008, jobs, sizeof jobs))
        return -1;
    for (unsigned i = 0; i < 24; i++) if (jobs[3 * i + 2] != 1) return -1;
    uint32_t id = fn.world_id((const void*)world), pilot = 0;
    if (id >= 4) return -1;
    Ent me;
    if (Resolve(atomic_load(&g_avatar), &me)) pilot = me.unit;   /* she sits inside the suit: her own body is not a wall */
    uint32_t parts[8];   /* nor are the suit's own arms (the shield sits 0.6 m ahead at knee height) */
    int nparts = 0;
    for (int i = 0; i < g_nchildren && nparts < 8; i++) { Ent c; if (Resolve(g_children[i], &c)) parts[nparts++] = c.unit; }
    uint8_t hits[16 * 44];
    memset(hits, 0, sizeof hits);
    uint32_t n = fn.raycast(id, from, dir, len, 2, 1, 0, 0x80000009u, skip, hits, 16);
    if (n > 16) return -1;
    float best = len + 1;
    for (uint32_t i = 0; i < n; i++)
    {
        float d;
        memcpy(&d, hits + 44u * i + 24, 4);
        uint32_t u = U32(hits + 44u * i + 28);
        int own = u == skip || (pilot && u == pilot);
        for (int k = 0; k < nparts && !own; k++) own = u == parts[k];
        if (own || !isfinite(d) || d < 0.3f) continue;
        if (d < best) best = d;
    }
    return best;
}
/* F9: a stepped charge, as the game settles a Helldiver after a warp: the suit's position set a little further every
 * update (an exosuit walks on a character mover, not a physics body: pushing its bodies moved nothing) */
static void ChargeFrame(void)
{
    if (!g_chargeUntil) return;
    static double last;
    float p[4] = {0};
    if (!fn.set_position || !RootOf(g_mechUnit, p)) { g_chargeUntil = 0; Log("F9 charge: the suit's position is unknown"); return; }
    double dt = last > 0 ? g_now - last : 0;
    last = g_now;
    if (g_now >= g_chargeUntil) { g_chargeUntil = 0; last = 0; Log("F9 %s: done at %.1f %.1f %.1f", g_chargeMode == 2 ? "jump" : "charge", p[0], p[1], p[2]); return; }
    if (g_chargeMode != 1)
    {
        if (!fn.set_velocity) { g_chargeUntil = 0; Log("F9: the velocity setter is not the reviewed code"); return; }
        float v[3] = {g_chargeMode == 2 ? 0 : g_chargeDir[0] * 20, g_chargeMode == 2 ? 0 : g_chargeDir[1] * 20, g_chargeMode == 2 ? 9.0f : 0};
        fn.set_velocity(NULL, g_mech, v);
        if (g_now >= g_chargeLog) { g_chargeLog = g_now + 0.1; Log("F9 %s: velocity %.1f %.1f %.1f set; the suit at %.1f %.1f %.1f", g_chargeMode == 2 ? "jump" : "charge", v[0], v[1], v[2], p[0], p[1], p[2]); }
        if (g_chargeMode == 2) g_chargeUntil = g_now;   /* one kick upward */
        return;
    }
    if (dt <= 0 || dt > 0.1) return;
    float step = (float)(20.0 * dt);   /* 20 m/s */
    float dir[3] = {g_chargeDir[0], g_chargeDir[1], 0}, down[3] = {0, 0, -1};
    /* walls: knee and chest height, the step plus the suit's half width ahead */
    for (int h = 0; h < 2; h++)
    {
        float from[3] = {p[0], p[1], p[2] + (h ? 2.4f : 1.0f)};
        float w = Ray(g_mechUnit, from, dir, step + 1.5f);
        if (w < 0) return;   /* not this update: the physics is busy */
        if (w <= step + 1.5f) { g_chargeUntil = 0; last = 0; Log("F9 charge: stopped by a wall %.1f m ahead (%s height)", w, h ? "chest" : "knee"); return; }
    }
    float dest[3] = {p[0] + dir[0] * step, p[1] + dir[1] * step, p[2]};
    /* ground: from 2.5 m above the next spot down to 2.5 m below its height */
    float top[3] = {dest[0], dest[1], p[2] + 2.5f};
    float g = Ray(g_mechUnit, top, down, 5.0f);
    if (g < 0) return;
    if (g > 5.0f) { g_chargeUntil = 0; last = 0; Log("F9 charge: stopped at an edge (no ground within 2.5 m below)"); return; }
    dest[2] = top[2] - g;
    fn.set_position(NULL, g_mech, dest);
    if (g_now >= g_chargeLog) { g_chargeLog = g_now + 0.1; Log("F9 charge: at %.1f %.1f %.1f, step %.2f m", p[0], p[1], p[2], step); }
}
static double g_thrustUntil;
/* F11: one node inside the suit's arms at a time, nudged 0.4 m along its local x for 0.4 s (set every update, since the
 * arm's animation sets it back each frame), then left to the animation again; each press moves to the next node */
static int NodeCount(uint32_t unit)
{
    uintptr_t registry = P(exe + 0x1A100F0), objects, object;
    uint32_t count = 0, id, slot = unit & 0x3FFFFF;
    if (!registry || !RD(registry + 0x98, &count, 4) || slot >= count || !(objects = P(registry + 0x88)) ||
        !(object = P(objects + 8u * slot)) || !RD(object + 8, &id, 4) || id != unit || !RD(object + 0x70, &count, 4) || count > 4096)
        return 0;
    return (int)count;
}
static int g_nudgeArm, g_nudgeNode = 0;
static uint32_t g_nudgeUnit;
static float g_nudgeBase[3];
static void ThrustStart(void)
{
    if (!fn.set_local_position || !fn.local_position) { Log("F11: no unit API"); return; }
    uint32_t arms[8];
    int n = 0;
    for (int i = 0; i < g_nchildren && n < 8; i++)
    {
        Ent e;
        if (Resolve(g_children[i], &e) && WielderOf(g_children[i]) == atomic_load(&g_avatar) && fn.unit_resolve(e.unit)) arms[n++] = e.unit;
    }
    if (!n) { Log("F11: no arm of yours on this suit"); return; }
    for (int tries = 0; tries < 64; tries++)
    {
        if (g_nudgeArm >= n) { g_nudgeArm = 0; g_nudgeNode = 0; }
        uint32_t unit = arms[g_nudgeArm];
        int count = NodeCount(unit);
        if (++g_nudgeNode >= count) { g_nudgeArm++; g_nudgeNode = 0; continue; }
        const float* lp = fn.local_position(unit, (uint32_t)g_nudgeNode);
        if (!lp || !isfinite(lp[0])) continue;
        g_nudgeUnit = unit;
        memcpy(g_nudgeBase, lp, 12);
        g_thrustUntil = g_now + 0.4;
        Log("F11: arm %d (unit %08x) node %d of %d, local %.2f %.2f %.2f: nudged 0.4 m along x", g_nudgeArm, unit, g_nudgeNode, count,
            lp[0], lp[1], lp[2]);
        return;
    }
    Log("F11: no node found to nudge");
}
static void ThrustFrame(void)
{
    if (!g_thrustUntil || !g_nudgeUnit || !fn.unit_resolve(g_nudgeUnit)) return;
    if (g_now < g_thrustUntil)
    {
        float v[4] = {g_nudgeBase[0] + 0.4f, g_nudgeBase[1], g_nudgeBase[2], 0};
        fn.set_local_position(g_nudgeUnit, (uint32_t)g_nudgeNode, v);
        return;
    }
    float v[4] = {g_nudgeBase[0], g_nudgeBase[1], g_nudgeBase[2], 0};
    fn.set_local_position(g_nudgeUnit, (uint32_t)g_nudgeNode, v);
    g_thrustUntil = 0;
}
static double g_warpCheck;
static void Warp(void)
{
    float p[4] = {0}, f[3] = {1, 0, 0};
    if (!fn.set_position || !RootOf(g_mechUnit, p)) { Log("F10: no warp setter or the suit's position is unknown"); return; }
    if (!CameraForward(g_mechUnit, f)) Log("F10: camera not found, warping along +x");
    float h = sqrtf(f[0] * f[0] + f[1] * f[1]);
    if (h < 0.05f) { f[0] = 1; f[1] = 0; h = 1; }
    float dest[3] = {p[0] + f[0] / h * 3, p[1] + f[1] / h * 3, p[2] + 0.1f};
    Log("F10 warp: the suit (entity %u) from %.1f %.1f %.1f to %.1f %.1f %.1f", g_mech, p[0], p[1], p[2], dest[0], dest[1], dest[2]);
    fn.set_position(NULL, g_mech, dest);
    g_warpCheck = g_now + 0.2;
}

/* ---- events ----------------------------------------------------------------------------------------------------- */
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* e = (const HfSpawnEvent*)info;
    Spawned* s = &g_spawn[g_nspawn++ % 256];
    s->entity = e->entity; s->resource = e->resource; s->at = g_now;
    g_seenEnt[g_nseen % SEEN] = e->entity; g_seenRes[g_nseen % SEEN] = e->resource; g_nseen++;
    if (MechName(e->resource)) Log("spawn: %s, entity %u network %u (simulated here: %s)", MechName(e->resource), e->entity, e->network, e->authority ? "yes" : "no");
}
static void OnPlayer(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    if (p->local && info->kind == HF_ON_PLAYER_SPAWN && p->entity) atomic_store(&g_avatar, p->entity);
}
static void OnWeapon(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfWeaponEvent* w = (const HfWeaponEvent*)info;
    if (w->avatar) atomic_store(&g_avatar, w->avatar);   /* the Helldiver (a player-spawn entity may not be) */
    static unsigned told;
    if (g_mech && told++ < 30)
        Log("weapon event: avatar %u holds entity %u (%016llx, selection %u), was %u (%016llx)", w->avatar, w->entity,
            (unsigned long long)w->resource, w->selection, w->previous, (unsigned long long)w->previous_resource);
}
static void OnDamage(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfDamageEvent* d = (const HfDamageEvent*)info;
    static unsigned told;
    if (!g_mech || told >= 80) return;
    Ent m;
    if (!Resolve(g_mech, &m)) return;
    int on = d->target == g_mech, byMech = d->dealer == m.network || d->owner == m.network, child = 0;
    for (int i = 0; i < g_nchildren; i++) { Ent c; if ((d->target == g_children[i]) || (Resolve(g_children[i], &c) && (d->dealer == c.network))) child = 1; }
    if (!on && !byMech && !child && d->kind != 6 && d->kind != 5) return;
    float p[4] = {0};
    if (g_actor) fn.position(g_actor, p);
    float dx = d->position[0] - p[0], dy = d->position[1] - p[1], dz = d->position[2] - p[2];
    told++;
    Log("hit: kind %u element %u amount %d, target %u%s, dealer %u owner %u creditor %llx (the suit's network %u)%s, %.1f m from the suit",
        d->kind, d->element, d->amount, d->target, on ? " (the suit)" : "", d->dealer, d->owner, (unsigned long long)d->creditor, m.network,
        child ? " [an arm involved]" : "", sqrtf(dx * dx + dy * dy + dz * dz));
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gs = 0, xs = 0;
    game = hf->module_base("game.dll", &gs);
    exe = hf->module_base("helldivers2.exe", &xs);
    if (!game || !exe || gs != 74727424u || xs != 60719104u) { g_ready = -1; Log("REFUSED: not build " BUILD "'s modules"); return; }
    g_ready = 0;   /* bound in the first mission: the engine tables are filled after game load */
}

/* ---- the Pile Bunker follow-up's drag, tested on foot or in a suit: Ctrl+F10 drags the nearest live enemy 10 m toward
 * you (the stepped move, as F9 moves the suit), Ctrl+F11 pushes the nearest corpse toward you (physics impulses on
 * every body of its ragdoll) */
static uint32_t g_dead[256];
static unsigned g_ndead;
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
           !hf->component_record("TurretComponentData", res, &stride) && !MechName(res) && res != 0x75BE82ED8592A6B3ull;
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
static uint32_t g_dragEnt, g_dragUnit;
static float g_dragDir[2], g_dragLeft;
static double g_dragUntil, g_dragLast;
static uint32_t g_pushActors[64];
static int g_npush;
static float g_pushDir[3];
static double g_pushUntil;
static void Drag(void)
{
    Ent me, e;
    float mp[3], best[3];
    if (!Resolve(atomic_load(&g_avatar), &me) || !RootOf(me.unit, mp)) { Log("Ctrl+F10: your position is unknown"); return; }
    uint32_t pick = 0, unit = 0;
    float bd = 1e30f;
    unsigned n = g_nseen < SEEN ? g_nseen : SEEN;
    for (unsigned i = 0; i < n; i++)
    {
        int32_t hp;
        float p[3];
        if (!Hostile(g_seenRes[i]) || !HealthOf(g_seenEnt[i], &hp) || hp <= 0 || !Resolve(g_seenEnt[i], &e) || !RootOf(e.unit, p)) continue;
        float d = (p[0] - mp[0]) * (p[0] - mp[0]) + (p[1] - mp[1]) * (p[1] - mp[1]);
        if (d < bd && d > 9.0f) { bd = d; pick = g_seenEnt[i]; unit = e.unit; memcpy(best, p, sizeof best); }
    }
    if (!pick) { Log("Ctrl+F10: no live enemy found (more than 3 m away)"); return; }
    float dx = mp[0] - best[0], dy = mp[1] - best[1], h = sqrtf(dx * dx + dy * dy);
    g_dragDir[0] = dx / h; g_dragDir[1] = dy / h;
    g_dragLeft = h - 3 < 10 ? h - 3 : 10;
    g_dragEnt = pick; g_dragUnit = unit; g_dragUntil = g_now + 0.5; g_dragLast = g_now;
    Log("Ctrl+F10: dragging enemy %u (%.1f m away) %.1f m toward you", pick, (double)h, (double)g_dragLeft);
}
static void DragFrame(void)
{
    if (!g_dragUntil) return;
    float p[3];
    double dt = g_now - g_dragLast;
    g_dragLast = g_now;
    if (!RootOf(g_dragUnit, p)) { g_dragUntil = 0; Log("Ctrl+F10: the enemy is gone"); return; }
    if (g_now >= g_dragUntil || g_dragLeft <= 0) { g_dragUntil = 0; Log("Ctrl+F10: done, the enemy is at %.1f %.1f %.1f", p[0], p[1], p[2]); return; }
    if (dt <= 0 || dt > 0.1) return;
    float step = (float)(20.0 * dt);
    if (step > g_dragLeft) step = g_dragLeft;
    float dest[3] = {p[0] + g_dragDir[0] * step, p[1] + g_dragDir[1] * step, p[2]}, top[3] = {dest[0], dest[1], p[2] + 2.0f}, down[3] = {0, 0, -1};
    float g = Ray(g_dragUnit, top, down, 4.0f);
    if (g < 0) return;
    if (g <= 4.0f) dest[2] = top[2] - g;
    fn.set_position(NULL, g_dragEnt, dest);
    g_dragLeft -= step;
}
static int Bodies(uint32_t unit, uint32_t* out, int max)
{
    int n = 0;
    _Alignas(8) uint8_t buf[128] = {0};
    if (!fn.unit_resolve(unit) || fn.components(unit, 127, buf) <= 0) return 0;
    for (int list = 0; list < 7; list++)
    {
        uint32_t count = U32(buf + 56 + list * 4);
        uintptr_t ids; memcpy(&ids, buf + list * 8, 8);
        for (uint32_t k = 0; k < count && k < 64 && ids && n < max; k++)
        {
            uint32_t id;
            if (!RD(ids + k * 4ull, &id, 4)) break;
            if ((id & 0x20000000u) && fn.valid(id) && fn.owner(id) == unit && fn.physical(id)) out[n++] = id;
        }
    }
    return n;
}
/* the nearest corpse within 30 m that still has a ragdoll (bodies far away or long dead have none) */
static void Push(void)
{
    Ent me, e;
    float mp[3];
    if (!Resolve(atomic_load(&g_avatar), &me) || !RootOf(me.unit, mp)) { Log("Ctrl+F11: your position is unknown"); return; }
    uint32_t pick = 0, unit = 0;
    float bd = 30.0f * 30.0f, cp[3] = {0};
    unsigned n = g_nseen < SEEN ? g_nseen : SEEN, skipped = 0;
    uint32_t tmp[64];
    for (unsigned i = 0; i < n; i++)
    {
        float p[3];
        int32_t hp = 1;
        if (!Hostile(g_seenRes[i]) || (HealthOf(g_seenEnt[i], &hp) && hp > 0) || !Resolve(g_seenEnt[i], &e) || !RootOf(e.unit, p)) continue;
        float d = (p[0] - mp[0]) * (p[0] - mp[0]) + (p[1] - mp[1]) * (p[1] - mp[1]);
        if (d >= bd) continue;
        if (!Bodies(e.unit, tmp, 64)) { skipped++; continue; }
        bd = d; pick = g_seenEnt[i]; unit = e.unit; memcpy(cp, p, sizeof cp);
    }
    if (!pick) { Log("Ctrl+F11: no corpse with a ragdoll within 30 m (%u without one)", skipped); return; }
    g_npush = Bodies(unit, g_pushActors, 64);
    float dx = mp[0] - cp[0], dy = mp[1] - cp[1], h = sqrtf(dx * dx + dy * dy);
    if (h < 0.1f) h = 0.1f;
    g_pushDir[0] = dx / h; g_pushDir[1] = dy / h; g_pushDir[2] = 0.3f;
    g_pushUntil = g_now + 0.3;
    Log("Ctrl+F11: pushing corpse %u (%.1f m away) toward you: %d physics bodies (%u nearer ones had none)", pick, (double)sqrtf(bd), g_npush, skipped);
}
static void PushFrame(void)
{
    if (!g_pushUntil) return;
    if (g_now >= g_pushUntil)
    {
        g_pushUntil = 0;
        if (g_npush)
        {
            float pp[4] = {0}, lv[4] = {0}, av[4] = {0};
            fn.position(g_pushActors[0], pp);
            fn.velocities(g_pushActors[0], lv, av);
            Log("Ctrl+F11: done; its first body at %.1f %.1f %.1f, moving %.1f %.1f %.1f", pp[0], pp[1], pp[2], lv[0], lv[1], lv[2]);
        }
        return;
    }
    for (int i = 0; i < g_npush; i++)
    {
        if (!fn.valid(g_pushActors[i])) continue;
        float j[4] = {g_pushDir[0] * 30, g_pushDir[1] * 30, g_pushDir[2] * 30, 0};   /* per body per update (6 gave 1 m/s after 0.3 s) */
        fn.impulse(g_pushActors[i], j, NULL);
    }
}
static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfKillEvent* k = (const HfKillEvent*)info;
    g_dead[g_ndead++ % 256] = k->victim;
}

static int Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
/* ---- the movement input (a lead from EXO Beacon Launcher, whose suit steering copies 3 floats from the avatar manager's
 * per-avatar data: [game+0x3326D20] + 0x150 + index * 0xA7AEC + 0xA7AE0, index from the manager's hash at +0xF8). Logged
 * with the keys held (W A S D), the camera's forward and her actual movement, so the log shows whether it is the stick */
static void InputProbe(void)
{
    static double next;
    static unsigned lines;
    static float lastV[3], lastPos[3];
    static double lastPosT;
    if (g_now < next || lines >= 400) return;
    next = g_now + 0.2;
    uint32_t me = atomic_load(&g_avatar), i;
    uintptr_t avatars = P(game + 0x3326D20);
    float v[3];
    if (!me || !avatars || !Lookup(avatars + 0xF8, me, &i) || i >= 8 || !RD(avatars + 0x150 + (uintptr_t)i * 0xA7AECu + 0xA7AE0u, v, 12)) return;
    char keys[5] = {Down('W') ? 'W' : '-', Down('A') ? 'A' : '-', Down('S') ? 'S' : '-', Down('D') ? 'D' : '-', 0};
    Ent e;
    float pos[4] = {0}, cam[3] = {0, 0, 0}, mv[2] = {0, 0};
    if (Resolve(me, &e))
    {
        uintptr_t registry = P(exe + 0x1A100F0), objects, object, nodes;
        uint32_t id = 0, slot = e.unit & 0x3FFFFF;
        if (registry && (objects = P(registry + 0x88)) && (object = P(objects + 8u * slot)) && RD(object + 8, &id, 4) && id == e.unit &&
            (nodes = P(object + 0x88)) && RD(nodes + 48, pos, 12))
        {
            if (lastPosT > 0 && g_now - lastPosT > 0.05) { mv[0] = (float)((pos[0] - lastPos[0]) / (g_now - lastPosT)); mv[1] = (float)((pos[1] - lastPos[1]) / (g_now - lastPosT)); }
            memcpy(lastPos, pos, 12); lastPosT = g_now;
        }
        CameraForward(e.unit, cam);
    }
    int changed = fabsf(v[0] - lastV[0]) + fabsf(v[1] - lastV[1]) + fabsf(v[2] - lastV[2]) > 0.02f;
    static char lastKeys[5];
    if (!changed && !strcmp(keys, lastKeys)) return;
    memcpy(lastV, v, 12); memcpy(lastKeys, keys, 5);
    lines++;
    Log("input? avatar %u index %u: %.3f %.3f %.3f | keys %s | camera forward %.2f %.2f | moving %.1f %.1f m/s", me, i, v[0], v[1], v[2], keys,
        cam[0], cam[1], mv[0], mv[1]);
}
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
        else if (++tries >= 5) { g_ready = -1; Log("REFUSED after 5 tries in a mission"); }
        if (g_ready != 1) return;
    }
    InputProbe();
    static double next;
    if (g_now >= next)
    {
        next = g_now + 0.25;
        Seat s = ReadSeat();
        static uint8_t lastRaw[64];
        static int lastFound = -1;
        if (s.found != lastFound || (s.found && memcmp(s.raw, lastRaw, 64)))
        {
            lastFound = s.found;
            memcpy(lastRaw, s.raw, 64);
            if (!s.found) Log("seat: none of yours");
            else
            {
                char hex[129];
                for (int k = 0; k < 64; k++) snprintf(hex + 2 * k, 3, "%02x", s.raw[k]);
                Log("seat: avatar %u, vehicle %u, role %u, seat node %08x%s; raw %s", s.avatar, s.vehicle, s.role, s.node, s.changing ? " (changing)" : "", hex);
            }
        }
        Ent v;
        uint32_t mech = 0;
        if (s.found && !s.changing && s.node != 0xFFFFFFFFu && s.vehicle && Resolve(s.vehicle, &v) && MechName(v.resource)) mech = s.vehicle;
        if (mech != g_mech)
        {
            g_mech = mech;
            g_actor = 0;
            g_nchildren = 0;
            if (mech)
            {
                g_mechRes = v.resource; g_mechUnit = v.unit;
                g_mechSpawnAt = -1e9;
                for (unsigned i = 0; i < 256; i++) if (g_spawn[i].entity == mech) g_mechSpawnAt = g_spawn[i].at;
                g_actor = FindActor(v.unit);
                Log("piloting: %s, entity %u unit %08x network %u, simulated here %s, physics body %08x; role %u", MechName(v.resource), mech, v.unit,
                    v.network, v.authority ? "yes" : "no", g_actor, s.role);
                Children();
            }
            else Log("piloting: no exosuit");
        }
        /* abilities: yours, the suit's, its children's (logged when they change) */
        if (g_mech)
        {
            uint32_t who[10] = {atomic_load(&g_avatar), g_mech};
            int n = 2;
            for (int i = 0; i < g_nchildren && n < 10; i++) who[n++] = g_children[i];
            static uint32_t lastId[10];
            static int lastAct[10];
            for (int i = 0; i < n; i++)
            {
                uint32_t id = 0; int act = 0;
                if (!Ability(who[i], &id, &act)) continue;
                if (id != lastId[i] || act != lastAct[i])
                {
                    lastId[i] = id; lastAct[i] = act;
                    Log("ability: %s %u runs %u (%s)", i == 0 ? "you" : i == 1 ? "the suit" : "child", who[i], id, act ? "active" : "idle");
                }
            }
        }
    }
    if (g_warpCheck && g_now >= g_warpCheck && g_actor)
    {
        float p[4] = {0};
        if (RootOf(g_mechUnit, p)) Log("F10 warp: the suit is now at %.1f %.1f %.1f", p[0], p[1], p[2]);
        g_warpCheck = 0;
    }
    ChargeFrame();
    ThrustFrame();
    DragFrame();
    PushFrame();
    static int prev[3];
    int keys[3] = {VK_F9, VK_F10, VK_F11};
    for (int k = 0; k < 3; k++)
    {
        int d = Down(keys[k]);
        if (d && !prev[k] && k > 0 && Down(VK_CONTROL)) { if (k == 1) Drag(); else Push(); }
        else if (d && !prev[k])
        {
            if (!g_mech) Log("F%d: you are not piloting an exosuit", 9 + k);
            else if (k == 0)
            {
                float f[3] = {1, 0, 0};
                if (!CameraForward(g_mechUnit, f)) Log("F9: camera not found, charging along +x");
                float h = sqrtf(f[0] * f[0] + f[1] * f[1]);
                if (h < 0.05f) { f[0] = 1; f[1] = 0; h = 1; }
                g_chargeDir[0] = f[0] / h; g_chargeDir[1] = f[1] / h;
                g_chargeMode = Down(VK_CONTROL) ? 2 : Down(VK_SHIFT) ? 0 : 1;   /* F9 stepped (with wall and ground checks), Shift velocity, Ctrl jump */
                g_chargeUntil = g_now + (g_chargeMode == 2 ? 0.05 : 0.5); g_chargeLog = 0;
                Log("F9 %s toward %.2f %.2f", g_chargeMode == 2 ? "jump (Ctrl: 9 m/s up)" : g_chargeMode == 1 ? "stepped charge (positions, 20 m/s, 0.5 s, stops at walls and edges)" :
                    "velocity charge (Shift: 20 m/s for 0.5 s through the motion record)", g_chargeDir[0], g_chargeDir[1]);
            }
            else if (k == 1) Warp();
            else ThrustStart();
        }
        prev[k] = d;
    }
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 15 || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    Log("Mech Probe 0.1.9: research only; logs a movement-input lead; F9 stepped charge (Shift: velocity, Ctrl: jump), F10 warp, F11 arm node nudge while you pilot an exosuit; Ctrl+F10 drags the nearest enemy, Ctrl+F11 pushes the nearest corpse");
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) && api->subscribe(mod, HF_ON_DAMAGE, OnDamage, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->on_game_thread(mod, Tick, NULL);
    if (!ok) Log("events or the game thread are unavailable");
    return 0;
}
