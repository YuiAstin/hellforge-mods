/* World reads shared with Executor 0.3.0 (mods/executor/src/executor.c): an entity's health and life, the game's raycast
 * (a clear line that skips leaves and bodies), a unit's body middle. Copied as they stand there. */
/* ---- reads: health, positions ------------------------------------------------------------------------------------ */
/* An entity's health record (Koleda's HealthRec: the health manager at game+0x3326688, its open-addressed entity hash,
 * 440-byte records with health at +20 and life at +412, 0 alive): its address, 0 unknown */
static uintptr_t HealthRec(uint32_t entity)
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
static int HealthOf(uint32_t entity, int32_t* health, uint32_t* life)
{
    uintptr_t r = HealthRec(entity);
    uint8_t b[416];
    if (!r || !ReadAt(r, b, sizeof b)) return 0;
    *health = (int32_t)Word(b + 20);
    *life = Word(b + 412);
    return 1;
}
/* his maximum health: the heal event's (when one has come), else his Helldiver's Health record as it stands now */
/* ---- the reticle (Exusiai 0.2.10's: the camera's ray, as Alva's aim; the game's raycast, as Lemuen's line of sight) */
typedef uint32_t (*WorldFn)(const void*);
typedef uint32_t (*RayFn)(uint32_t, const void*, const void*, float, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t);
static WorldFn g_worldId;
static RayFn g_raycast;
static uint32_t ActorFilter(uint32_t id, uint32_t unit)
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
#define FILTER_FOLIAGE 0x04A8FBF9u
static uint32_t g_foeUnit[FOES];
static int g_nfoeUnit;
static double g_foeUnitAt = -1;
static int FoeUnit(uint32_t u)
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
static int WorldId(uint32_t* id)
{
    if (!g_raycast || !g_worldId) return 0;
    uintptr_t world = Pointer(game + 0x346BFA0);
    if (!world) return 0;
    *id = g_worldId((const void*)world);
    return *id < 4;
}
/* the first wall along a ray (len + 1: clear), skipping him, leaves and bodies */
static float FirstWall(uint32_t id, const float from[3], const float dir[3], float len, uint32_t me)
{
    uint8_t hits[16 * 44];
    memset(hits, 0, sizeof hits);
    uint32_t n = g_raycast(id, from, dir, len, 2, 1, 0, 0x80000009u, me, hits, 16);
    if (n > 16) n = 16;
    float best = len + 1;
    for (uint32_t i = 0; i < n; i++)
    {
        float d;
        memcpy(&d, hits + 44u * i + 24, 4);
        uint32_t hu = Word(hits + 44u * i + 28);
        if (!isfinite(d) || d < 1.2f || d >= best || (hu && hu == me)) continue;
        uint32_t filt = ActorFilter(Word(hits + 44u * i + 32), hu);
        if (filt == FILTER_FOLIAGE || (hu && (filt == 0x236122F0u || filt == 0xD5B33D56u || FoeUnit(hu)))) continue;
        best = d;
    }
    return best;
}
/* the camera (his unit's world, its camera matrix at +0x230920: Apache - Rise Above's read) */
static int Living(uint32_t e)
{
    int32_t hp = 0;
    uint32_t life = 1;
    return HealthOf(e, &hp, &life) && hp > 0 && life == 0;
}
/* the middle of a unit's body (Exusiai 0.2.4's: the median of its scene-graph nodes, kept a second per unit) */
#define BODIES 64
static struct { uint32_t unit; double at; float off[3]; } g_body[BODIES];
static int CmpF(const void* x, const void* y) { float a = *(const float*)x, b = *(const float*)y; return a < b ? -1 : a > b; }
static int BodyOffset(uint32_t unit, const float root[3], float off[3])
{
    unsigned h = (unit * 2654435761u) % BODIES;
    if (g_body[h].unit == unit && g_now - g_body[h].at < 1.0) { memcpy(off, g_body[h].off, 12); return 1; }
    uintptr_t registry = Pointer(exe + 0x1A100F0), objects, object, nodes;
    uint32_t count = 0, id, slot = unit & 0x3FFFFF;
    static float m[256 * 16];
    if (!registry || !(objects = Pointer(registry + 0x88)) || !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit ||
        !ReadAt(object + 0x70, &count, 4) || !count || count > 4096 || !(nodes = Pointer(object + 0x88)))
        return 0;
    if (count > 256) count = 256;
    if (!ReadAt(nodes, m, (size_t)count * 64)) return 0;
    static float v[3][256];
    int used = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        const float* t = m + 16 * i + 12;
        if (!isfinite(t[0]) || !isfinite(t[1]) || !isfinite(t[2]) || fabsf(t[0] - root[0]) > 20 || fabsf(t[1] - root[1]) > 20 || t[2] < root[2] - 2 || t[2] > root[2] + 20) continue;
        for (int k = 0; k < 3; k++) v[k][used] = t[k] - root[k];
        used++;
    }
    if (used < 2) return 0;
    for (int k = 0; k < 3; k++) { qsort(v[k], used, sizeof(float), CmpF); off[k] = v[k][used / 2]; }
    if (off[2] < 0.3f) off[2] = 0.3f;
    g_body[h].unit = unit; g_body[h].at = g_now; memcpy(g_body[h].off, off, 12);
    return 1;
}
/* a clear line from his muzzle to a point (no wall between; bodies and leaves do not count: FirstWall skips them) */
static int ClearShot(const float from[3], const float q[3])
{
    uint32_t id;
    if (!WorldId(&id) || !QueryIdle()) return 1;
    float d[3] = {q[0] - from[0], q[1] - from[1], q[2] - from[2]};
    float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(n > 1.5f)) return 1;
    for (int k = 0; k < 3; k++) d[k] /= n;
    return FirstWall(id, from, d, n, UnitOf(atomic_load(&g_avatar))) > n - 1.0f;
}
