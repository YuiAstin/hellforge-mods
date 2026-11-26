/* kitbase layer 2: the world around her. What a thing is and where, its health, line of sight through the game's
 * raycast, her camera, the shape of a body from its skeleton, and the setters that move an entity.
 *
 * Code shared by the character kits. Source of truth: kitbase/ at the repo root. The copy in a kit's src/kitbase/ is
 * written by tools/kitbase_sync.py, so edit kitbase/, never the copy. See docs/kit-base.md.
 * Lifted 2026-11-26 from the kits that carried their own copies: ActorFilter (11 kits, word for word), FoeUnit, WorldId,
 * Off (Lemuen 0.1.6 and the kits after her), HealthRec (Koleda's, then Destroyer, Executor, W), UnitObject (Leva, Ascalon,
 * W), BodyOffset (Exusiai's, with Yui's structure fix of 2026-11-11), UnitTop (W 0.8.0), View and InSight (Lemuen 0.1.6b,
 * Alva, Koleda, Vector), FirstWall (the superset: Koleda's second unit to see through, Lemuen's diagnostics), RayWith
 * (Faelynn's and Voymastina's, with Ascalon 0.4.14's zone faces), Walks (the Time Stop Probe, W).
 *
 * A kit calls KbWorldInstall() in its GAME_LOADED handler, after game and exe are set. Every block whose game code is
 * not the reviewed one stays off and says so: the raycast (g_raycast 0: InSight answers 1, FirstWall is not called), the
 * position, velocity and destination setters (0: nothing is moved). Include after kb_core.h, kb_player.h and
 * kb_friends.h. */
#ifndef KB_WORLD_H
#define KB_WORLD_H

#include "kb_core.h"
#include "kb_player.h"
#include "kb_friends.h"

/* ---- code pins ------------------------------------------------------------------------------------------------- */
/* a function starts at `at`: the byte before it ends another (int3, ret or nop) and its first is not padding. For code
 * with no stable pattern to match (the destination setter) */
KB_FN int Boundary(uintptr_t at)
{
    uint8_t b[2];
    return hf->read_code && !hf->read_code(at - 1, b, 2) && (b[0] == 0xCC || b[0] == 0xC3 || b[0] == 0x90) && b[1] != 0xCC;
}

/* ---- the game's raycast (exe+0x7F4590) and the world id it takes (exe+0x79F860) ----------------------------------- */
typedef uint32_t (*WorldFn)(const void*);
typedef uint32_t (*RayFn)(uint32_t, const void*, const void*, float, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t);
KB_VAR WorldFn g_worldId;
KB_VAR RayFn g_raycast;
/* The setters the LIFT-182 Warp Pack moves a Helldiver with (game.dll+0x5A2BB0 position, +0x4A7550 velocity; Voymastina's
 * research), and the destination an enemy walks to (+0x5B36C0, the Time Stop Probe's). All take (NULL, entity, vec3). */
typedef void (*SetVecFn)(void*, uint32_t, const float*);
KB_VAR SetVecFn g_setPosition, g_setVelocity, g_setDest;

/* What a physics actor is tagged as (its collision filter's name): leaves, bodies, the level's zone faces */
#define FILTER_FOLIAGE 0x04A8FBF9u
#define FILTER_BODY 0x236122F0u
#define FILTER_RAGDOLL 0xD5B33D56u
#define FILTER_ZONE 0xEDC5EBE0u   /* faces some 2 m over the ground across a whole area: a look meets them, nothing stops on them */
KB_FN uint32_t ActorFilter(uint32_t id, uint32_t unit)
{
    uint32_t world = id >> 30, layout, stride, key_offset, data_offset, key, index, name = 0;
    uintptr_t pool = exe + 0x2369B00 + 64u * ((id >> 28 & 3) + 10 * world);
    uint8_t header[56], record[40], body[160];
    if (!ReadAt(pool, header, 56)) return 0;
    layout = Word(header + 28); stride = layout & 65535; key_offset = layout >> 16 & 255; data_offset = layout >> 24;
    index = id & Word(header + 40);
    if (index >= Word(header + 36) || !(id & Word(header + 52)) || stride < 40 || stride > 512 || key_offset + 4 > stride || data_offset + 40 > stride) return 0;
    uintptr_t entry = (uintptr_t)Quad(header) + (uintptr_t)index * stride;
    if (!ReadAt(entry + key_offset, &key, 4) || key != id || !ReadAt(entry + data_offset, record, 40) || Word(record + 12) != unit || !(Word(record + 16) & 1)) return 0;
    uintptr_t physics = Pointer(exe + 0x27BA8A8 + 176u * world), bodies = physics ? Pointer(physics + 24) : 0;
    uint32_t body_index = Word(record + 20) & 0xFFFFFF;
    if (!bodies || body_index >= 262144 || !ReadAt(bodies + 160u * body_index, body, 160) || Word(body + 144) != id || Word(body + 148) != unit) return 0;
    uintptr_t filters = Pointer(exe + 0x27C5E48), names;
    uint32_t count, filter = Word(body + 108) & 127;
    if (!filters || !ReadAt(filters + 248, &count, 4) || count > 128 || filter >= count || !(names = Pointer(filters + 256)) || !ReadAt(names + 4u * filter, &name, 4)) return 0;
    return name;
}
/* The raycast's world id for the main world (0 to 3), or 0 when the raycast is off */
KB_FN int WorldId(uint32_t* id)
{
    if (!g_raycast || !g_worldId) return 0;
    uintptr_t world = Pointer(game + 0x346BFA0);
    if (!world) return 0;
    *id = g_worldId((const void*)world);
    return *id < 4;
}

KB_FN void KbWorldInstall(void)
{
    if (hf->code_matches(exe + 0x79F860, "33 c0 4c 8d 05 c7 b2 01 02 0f 1f 80 00 00 00 00 49 39 0c c0 74 0c ff c0 83 f8 04 75 f3") &&
        hf->code_matches(exe + 0x7F4590, "48 8b c4 f3 0f 11 58 20 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48"))
    {
        g_worldId = (WorldFn)(exe + 0x79F860);
        g_raycast = (RayFn)(exe + 0x7F4590);
    }
    else Log("line of sight off: the raycast is not the reviewed code (looks go through walls)");
    if (hf->code_matches(game + 0x5A2BB0, "48 89 5c 24 08 48 89 6c 24 10 56 57 41 54 41 56 41 57 48 83 ec 40") &&
        hf->code_matches(game + 0x4A7550, "48 89 5c 24 18 57 48 83 ec 20 3b 15 ?? ?? ?? ?? 48 8b 1d"))
    {
        g_setPosition = (SetVecFn)(game + 0x5A2BB0);
        g_setVelocity = (SetVecFn)(game + 0x4A7550);
    }
    else Log("the warp setters are not the reviewed code: nothing is moved");
    if (Boundary(game + 0x5B36C0)) g_setDest = (SetVecFn)(game + 0x5B36C0);
    Log("world: raycast %s, warp setters %s, destination setter %s", g_raycast ? "ready" : "off", g_setPosition ? "ready" : "off", g_setDest ? "ready" : "off");
}

/* ---- what a thing is ------------------------------------------------------------------------------------------- */
KB_FN uint32_t UnitOf(uint32_t e)
{
    uint8_t d[24];
    return e && e != UINT32_MAX && Descriptor(e, d) ? Word(d + 12) : 0;
}
KB_FN uint64_t ResOf(uint32_t e)
{
    uint8_t d[24];
    return e && Descriptor(e, d) ? Quad(d) : 0;
}
/* A unit's engine object (the unit registry: generation-checked), 0 when it is gone */
KB_FN uintptr_t UnitObject(uint32_t unit)
{
    if (!exe || !unit) return 0;
    uintptr_t registry = Pointer(exe + 0x1A100F0), generations, objects, object;
    uint32_t count, id, slot = unit & 0x3FFFFF;
    uint8_t generation;
    if (!registry || !ReadAt(registry + 0x98, &count, 4) || count > 0x400000 || slot >= count ||
        !(generations = Pointer(registry + 0xA0)) || !ReadAt(generations + slot, &generation, 1) || generation != (uint8_t)(unit >> 22) ||
        !(objects = Pointer(registry + 0x88)) || !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit)
        return 0;
    return object;
}
/* Where an entity stands (its unit's root); not her own Helldiver */
KB_FN int EntityPos(uint32_t e, float p[3])
{
    uint8_t id[24];
    return e && Descriptor(e, id) && Quad(id) != AVATAR && UnitNode(Word(id + 12), 0, p);
}
/* One of the kit's foes' units (g_foe, kept by kb_friends.h): a quarter-second cache, since FirstWall asks per hit */
KB_VAR uint32_t g_foeUnit[FOES];
KB_VAR int g_nfoeUnit;
KB_VAR double g_foeUnitAt = -1;
KB_FN int FoeUnit(uint32_t u)
{
    if (g_now - g_foeUnitAt > 0.25 || g_foeUnitAt < 0)
    {
        g_foeUnitAt = g_now;
        g_nfoeUnit = 0;
        for (int k = 0; k < FOES; k++) if (g_foe[k]) { uint32_t fu = UnitOf(g_foe[k]); if (fu) g_foeUnit[g_nfoeUnit++] = fu; }
    }
    for (int i = 0; i < g_nfoeUnit; i++) if (g_foeUnit[i] == u) return 1;
    return 0;
}

/* ---- health ---------------------------------------------------------------------------------------------------- */
/* An entity's health record (Koleda's: the health manager at game+0x3326688, its open-addressed entity hash, 440-byte
 * records with health at +20 and life at +412, 0 alive): its address, 0 unknown */
KB_FN uintptr_t HealthRec(uint32_t entity)
{
    uintptr_t m = Pointer(game + 53634696u);
    uint8_t h[88];
    if (!entity || !m || !ReadAt(m + 4112, h, sizeof h)) return 0;
    uint32_t live = Word(h + 16), cap = Word(h + 40), empty = Word(h + 44), mult = Word(h + 48);
    uintptr_t buckets = (uintptr_t)Quad(h + 32), records = (uintptr_t)Quad(h + 72);
    if (!buckets || !records || !cap || cap > 65536 || (cap & (cap - 1)) || live > 4096) return 0;
    uint32_t start = entity * mult;
    for (uint32_t k = 0; k < cap; k++)
    {
        uint8_t b[8];
        if (!ReadAt(buckets + 8u * ((start + k) & (cap - 1)), b, 8)) return 0;
        uint32_t key = Word(b), index = Word(b + 4);
        if (key == empty) return 0;
        if (key != entity) continue;
        if (index >= live) return 0;
        return records + 440u * index;
    }
    return 0;
}
KB_FN int HealthOf(uint32_t entity, int32_t* health, uint32_t* life)
{
    uintptr_t r = HealthRec(entity);
    uint8_t b[416];
    if (!r || !ReadAt(r, b, sizeof b)) return 0;
    *health = (int32_t)Word(b + 20);
    if (life) *life = Word(b + 412);
    return 1;
}
/* Health above 0 and its life state 0 (dying and dead enemies keep some health for a moment) */
KB_FN int Alive(uint32_t entity)
{
    int32_t hp = 0;
    uint32_t life = 1;
    return HealthOf(entity, &hp, &life) && hp > 0 && life == 0;
}
/* Whether it walks: 0 no navigation record (a structure, a rider, something dead), 1 a record but stopped (a stunned
 * enemy's), 2 walking. The warp setter crashed on a rider and on a dead enemy: hold only what has a record. */
KB_FN int Walks(uint32_t e)
{
    uintptr_t m = Pointer(game + 0x3326418), recs;
    uint32_t index, active = 0;
    if (!m || !Lookup(m + 0x30, e, 1048576, &index) || index > 65535 || !(recs = Pointer(m + 0x50))) return 0;
    if (!ReadAt(recs + 0xA8u * index + 0x10, &active, 4)) return 0;
    return (active & 0xFF) != 0 ? 2 : 1;
}

/* ---- a body's shape, from its skeleton --------------------------------------------------------------------------- */
/* The nodes of a unit (its object's +0x70 count, +0x88 the nodes' world poses, 64 bytes each, the place at float 12),
 * as offsets from `root`, those within `reach` m across and from 2 m under to `up` m over it. Returns how many. */
KB_FN int CmpF(const void* a, const void* b) { float x = *(const float*)a, y = *(const float*)b; return x < y ? -1 : x > y; }
KB_FN int UnitNodes(uint32_t unit, const float root[3], float reach, float up, float out[3][256])
{
    uintptr_t object = UnitObject(unit), nodes;
    uint32_t count = 0;
    static float m[256 * 16];
    int used = 0;
    if (!object || !ReadAt(object + 0x70, &count, 4) || !count || count > 4096 || !(nodes = Pointer(object + 0x88))) return 0;
    if (count > 256) count = 256;
    if (!ReadAt(nodes, m, (size_t)count * 64)) return 0;
    for (uint32_t i = 0; i < count; i++)
    {
        const float* t = m + 16 * i + 12;
        if (!isfinite(t[0]) || !isfinite(t[1]) || !isfinite(t[2]) || fabsf(t[0] - root[0]) > reach || fabsf(t[1] - root[1]) > reach || t[2] < root[2] - 2 ||
            t[2] > root[2] + up) continue;
        for (int k = 0; k < 3; k++) out[k][used] = t[k] - root[k];
        used++;
    }
    return used;
}
/* The middle of its body over its root: the median of its nodes (the box's centre overshot small enemies: a few nodes far
 * up, a weapon, an effect). A tall thing whose nodes cluster at its foot (a structure: Yui, 2026-11-11, the guns shot the
 * floor under an Illuminate spawner) gets half its height. Cached a second per unit. */
#define KB_BODIES 64
typedef struct KbBody { uint32_t unit; double at; float off[3]; } KbBody;
KB_VAR KbBody g_body[KB_BODIES];
KB_FN int BodyOffset(uint32_t unit, const float root[3], float off[3])
{
    unsigned h = (unit * 2654435761u) % KB_BODIES;
    if (g_body[h].unit == unit && g_now - g_body[h].at < 1.0) { memcpy(off, g_body[h].off, 12); return 1; }
    static float v[3][256];
    int used = UnitNodes(unit, root, 20, 20, v);
    if (used < 2) return 0;
    for (int k = 0; k < 3; k++) { qsort(v[k], (size_t)used, sizeof(float), CmpF); off[k] = v[k][used / 2]; }
    float top = v[2][(used * 9) / 10];
    if (top > 3.0f && off[2] < 0.35f * top) off[2] = 0.5f * top;
    if (off[2] < 0.3f) off[2] = 0.3f;
    g_body[h].unit = unit; g_body[h].at = g_now; memcpy(g_body[h].off, off, 12);
    return 1;
}
/* How far its highest part is over its root (the 95th percentile of its nodes' heights, so a stray effect node far up
 * does not count): where a sign over it goes. 0 when it cannot be read. */
KB_FN float UnitTop(uint32_t unit, const float root[3])
{
    static float v[3][256];
    int used = UnitNodes(unit, root, 25, 30, v);
    if (used < 2) return 0;
    qsort(v[2], (size_t)used, sizeof(float), CmpF);
    int at = (used * 19) / 20;
    float top = v[2][at >= used ? used - 1 : at];
    return top < 0.3f ? 0.3f : top;
}
/* Where to aim at an entity: its body's middle (BodyOffset), else a metre over its root. res 0: any resource. */
KB_FN int TargetPos(uint32_t e, uint64_t res, float out[3])
{
    uint8_t d[24];
    if (!Descriptor(e, d) || (res && Quad(d) != res) || !UnitNode(Word(d + 12), 0, out)) return 0;
    float off[3];
    if (BodyOffset(Word(d + 12), out, off)) { for (int k = 0; k < 3; k++) out[k] += off[k]; }
    else out[2] += 1.0f;
    return 1;
}

/* ---- rays ------------------------------------------------------------------------------------------------------ */
/* The last thing FirstWall stopped on (for a kit's log) */
KB_VAR uint32_t g_lastWallFilter, g_lastWallUnit;
/* The first wall along a ray (its distance; len + 1 when clear), skipping `me` (within 1.2 m of the start is her own gun
 * or scope), `skip` (a second unit to see through, Koleda's Sinner), leaves and bodies (FoeUnit, ragdolls). *hitTarget:
 * the target's unit `tu` was met first. More hits than room is a wall (Lemuen 0.1.6d). */
KB_FN float FirstWall(uint32_t id, const float from[3], const float dir[3], float len, uint32_t me, uint32_t tu, uint32_t skip, int* hitTarget)
{
    uint8_t hits[16 * 44];
    memset(hits, 0, sizeof hits);
    uint32_t n = g_raycast(id, from, dir, len, 2, 1, 0, 0x80000009u, me, hits, 16);
    if (n > 16) n = 16;
    float best = len + 1;
    int target = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        float d;
        memcpy(&d, hits + 44u * i + 24, 4);
        uint32_t hu = Word(hits + 44u * i + 28);
        if (!isfinite(d) || d < 1.2f || d >= best || (hu && (hu == me || (skip && hu == skip)))) continue;
        if (hu && tu && hu == tu) { best = d; target = 1; continue; }
        uint32_t filt = ActorFilter(Word(hits + 44u * i + 32), hu);
        if (filt == FILTER_FOLIAGE || (hu && (filt == FILTER_BODY || filt == FILTER_RAGDOLL || FoeUnit(hu)))) continue;
        best = d; target = 0;
        g_lastWallFilter = filt; g_lastWallUnit = hu;
    }
    if (hitTarget) *hitTarget = target;
    return best;
}
/* A ray with the kit's own filter and flags, skipping `unit` and the zone faces (-1: the raycast is off or the hits
 * overflowed). g_rayBodies: bodies and foes do not stop it either. g_rayPrefer >= 0: the hit nearest that distance
 * instead of the first (a ledge under a dash's end). */
KB_VAR int g_rayBodies;
KB_VAR float g_rayPrefer = -1;
KB_FN float RayWith(uint32_t unit, const float from[3], const float dir[3], float len, uint32_t filter, uint32_t flags, unsigned* count)
{
    uint32_t id;
    if (!WorldId(&id)) return -1;
    uint8_t hits[16 * 44];
    memset(hits, 0, sizeof hits);
    uint32_t n = g_raycast(id, from, dir, len, 2, 1, filter, flags, unit, hits, 16);
    if (count) *count = n;
    if (n > 16) return -1;
    float best = len + 1;
    for (uint32_t i = 0; i < n; i++)
    {
        float d;
        memcpy(&d, hits + 44u * i + 24, 4);
        uint32_t hu = Word(hits + 44u * i + 28);
        if (hu == unit || !isfinite(d) || d < 0.02f) continue;
        uint32_t rf = ActorFilter(Word(hits + 44u * i + 32), hu);
        if (rf == FILTER_ZONE) continue;
        if (g_rayBodies && (rf == FILTER_BODY || rf == FILTER_RAGDOLL || FoeUnit(hu))) continue;
        if (g_rayPrefer >= 0 ? fabsf(d - g_rayPrefer) < fabsf(best - g_rayPrefer) : d < best) best = d;
    }
    return best;
}

/* ---- her view -------------------------------------------------------------------------------------------------- */
/* How far off the view's forward a point is, in degrees (180: at the camera), and its distance */
KB_FN float Off(const float cam[3], const float fwd[3], const float p[3], float* dist)
{
    float v[3] = {p[0] - cam[0], p[1] - cam[1], p[2] - cam[2]};
    float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    *dist = n;
    if (!(n > 0.5f)) return 180;
    float c = (v[0] * fwd[0] + v[1] * fwd[1] + v[2] * fwd[2]) / n;
    return acosf(c < -1 ? -1 : c > 1 ? 1 : c) * 57.29578f;
}
/* Her camera: where it is and where it looks (her unit's world, +0x230920: a 4x4 with forward in row 1, place in row 3);
 * *fp when it sits within kb_fp_distance m of her head (first person). g_viewWhy says why it failed. */
KB_VAR float kb_fp_distance = 0.45f;
KB_VAR char g_viewWhy[200];
KB_FN int View(float pos[3], float fwd[3], int* fp)
{
    uint8_t d[24];
    uint32_t avatar = atomic_load(&g_avatar);
    if (!Descriptor(avatar, d)) { snprintf(g_viewWhy, sizeof g_viewWhy, "no descriptor for her avatar (entity %u)", avatar); return 0; }
    uint32_t unit = Word(d + 12);
    uintptr_t object = UnitObject(unit), w = object ? Pointer(object + 0x18) : 0;
    float m[16], head[3];
    if (!w || !ReadAt(w + 0x230920, m, 64)) { snprintf(g_viewWhy, sizeof g_viewWhy, "the camera's read stopped: unit %08x, object %p, world %p", unit, (void*)object, (void*)w); return 0; }
    for (int k = 0; k < 16; k++) if (!isfinite(m[k])) { snprintf(g_viewWhy, sizeof g_viewWhy, "the camera's matrix is not finite (world %p)", (void*)w); return 0; }
    if (fabsf(m[15] - 1) > 1e-3f) { snprintf(g_viewWhy, sizeof g_viewWhy, "not a camera matrix at world %p + 0x230920 (row 3: %.2f)", (void*)w, (double)m[15]); return 0; }
    memcpy(fwd, m + 4, 12);
    memcpy(pos, m + 12, 12);
    float n = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    if (!(n > 0.5f)) { snprintf(g_viewWhy, sizeof g_viewWhy, "the camera's forward is too short (%.3f)", (double)n); return 0; }
    for (int k = 0; k < 3; k++) fwd[k] /= n;
    if (fp) *fp = UnitNode(unit, NODE_HEAD, head) && Dist2(pos, head) < kb_fp_distance * kb_fp_distance;
    g_viewWhy[0] = 0;
    return 1;
}
/* Whether `q` (an entity's root, `target` its entity) can be seen from the camera: any of three rays gets there, to its
 * chest, to its head, or (within 4 degrees of the crosshair) the aim itself reaching it or past it (Lemuen 0.1.6b: the
 * chest alone refused enemies Yui could plainly see). kb_sight 0 or the raycast off: always. kb_sight_skip: a second
 * unit to see through. */
KB_VAR int kb_sight = 1;
KB_VAR float kb_sight_slack = 1.5f;
KB_VAR uint32_t kb_sight_skip;
KB_VAR unsigned g_sightClear, g_sightBlocked;
KB_FN int InSight(const float cam[3], const float fwd[3], const float q[3], uint32_t target)
{
    uint32_t id;
    if (!kb_sight || !WorldId(&id)) return 1;
    uint32_t me = UnitOf(atomic_load(&g_avatar)), tu = UnitOf(target);
    for (int r = 0; r < 3; r++)
    {
        float to[3] = {q[0], q[1], q[2] + (r == 1 ? 0.7f : 0)}, dir[3], len;
        if (r < 2)
        {
            for (int k = 0; k < 3; k++) dir[k] = to[k] - cam[k];
            len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
            if (!(len > kb_sight_slack + 0.5f)) return 1;
            for (int k = 0; k < 3; k++) dir[k] /= len;
        }
        else
        {
            float d;
            if (Off(cam, fwd, q, &d) > 4) break;
            memcpy(dir, fwd, sizeof dir);
            len = d + 2;
        }
        int hitTarget = 0;
        float w = FirstWall(id, cam, dir, len, me, tu, kb_sight_skip, &hitTarget);
        float reach = r < 2 ? len : len - 2;
        if (hitTarget || w > reach - kb_sight_slack) { g_sightClear++; return 1; }
    }
    g_sightBlocked++;
    return 0;
}
/* Her camera's place for the HUD's world markers (the HUD takes the engine camera nearest it): `<kit>_view`, 3 floats.
 * Her head when the camera cannot be read. */
#define KB_VIEW_EXPORTS(kit)                                                                                               \
    HF_MOD_EXPORT int kit##_view(unsigned version, float* out)                                                             \
    {                                                                                                                      \
        float fwd[3];                                                                                                      \
        uint8_t id_[24];                                                                                                   \
        if (version != 1 || !out || !g_inMission) return 0;                                                                \
        if (View(out, fwd, NULL)) return 1;                                                                                \
        return Descriptor(atomic_load(&g_avatar), id_) && Quad(id_) == AVATAR && UnitNode(Word(id_ + 12), NODE_HEAD, out); \
    }

#endif
