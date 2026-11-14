/* Where she may land: the landing checks of Voymastina 0.4's teleports (mods/voymastina/src/voymastina.c), copied as they
 * stand there: the surfaces under a spot, whether a Helldiver fits on one, whether it is a real floor, the best spot at
 * a distance along a direction. The position setter is the LIFT-182 Warp Pack's call as the Warp Probe found it. */
typedef void (*SetVecFn)(void*, uint32_t, const float*);
static SetVecFn g_setPosition, g_setVelocity;
static float s_lpClimb = 2.5f, s_lpDrop = 8;
/* The nearest static obstruction along a ray (static actors only, so enemies are not walls): its distance, len + 1
 * when clear, or -1 when the query cannot be made */
static float RayWith(uint32_t unit, const float from[3], const float dir[3], float len, uint32_t filter, uint32_t flags, unsigned* count);
static float g_rayPrefer = -1;
static float RayWith(uint32_t unit, const float from[3], const float dir[3], float len, uint32_t filter, uint32_t flags, unsigned* count)
{
    uintptr_t world = Pointer(game + 0x346BFA0);
    if (!world || !g_raycast || !g_worldId) return -1;
    uint32_t id = g_worldId((const void*)world);
    if (id >= 4) return -1;
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
        if (Word(hits + 44u * i + 28) == unit || !isfinite(d) || d < 0.02f) continue;
        if (g_rayPrefer >= 0 ? fabsf(d - g_rayPrefer) < fabsf(best - g_rayPrefer) : d < best) best = d;
    }
    return best;
}

/* The units of the enemies near a dash (refreshed per dash): their bodies are no floor, and a spot under one is taken */
static int g_underFoe;
static uint32_t g_foeUnits[256];
static int g_nFoeUnits;
static int IsFoeUnit(uint32_t u)
{
    for (int i = 0; i < g_nFoeUnits; i++) if (g_foeUnits[i] == u) return 1;
    return 0;
}

/* Every surface under (x, y) from lp_climb m above `ref` down to lp_drop m below it (terrain, rocks, floors, roofs),
 * highest first; how many */
static int Surfaces(uint32_t unit, float x, float y, float ref, float* z, int max)
{
    float top[3] = {x, y, ref + s_lpClimb}, down[3] = {0, 0, -1}, len = s_lpClimb + s_lpDrop;
    uintptr_t world = Pointer(game + 0x346BFA0);
    if (!world || !g_raycast || !g_worldId) return 0;
    uint32_t id = g_worldId((const void*)world);
    if (id >= 4) return 0;
    uint8_t hits[16 * 44];
    memset(hits, 0, sizeof hits);
    uint32_t n = g_raycast(id, top, down, len, 2, 1, 0, 0x80000009u, unit, hits, 16);
    if (n > 16) return 0;
    int k = 0;
    for (uint32_t i = 0; i < n && k < max; i++)
    {
        float d;
        memcpy(&d, hits + 44u * i + 24, 4);
        uint32_t hu = Word(hits + 44u * i + 28);
        if (hu == unit || !isfinite(d) || d < 0.02f || d > len) continue;
        uint32_t filt = ActorFilter(Word(hits + 44u * i + 32), hu); if (filt == FILTER_FOLIAGE || (hu && (filt == 0x236122F0u || filt == 0xD5B33D56u))) continue;   /* leaves, canopies and bodies (enemies, corpses: filters seen in Voymastina 0.2.1's probe) are no floor */
        if (IsFoeUnit(hu)) { g_underFoe = 1; continue; }                              /* nor is an enemy's back */
        z[k++] = top[2] - d;
    }
    for (int i = 1; i < k; i++)        /* highest first */
        for (int j = i; j > 0 && z[j] > z[j - 1]; j--) { float t = z[j]; z[j] = z[j - 1]; z[j - 1] = t; }
    return k;
}
/* Whether a Helldiver fits standing at (x, y, z): nothing within 1.8 m above her feet, nothing within 0.4 m around
 * her at knee and chest height; 1 fits */
static int FoeAbove(uint32_t unit, const float from[3], float len)
{
    uintptr_t world = Pointer(game + 0x346BFA0);
    if (!world || !g_raycast || !g_worldId) return 0;
    uint32_t id = g_worldId((const void*)world);
    if (id >= 4) return 0;
    uint8_t hits[16 * 44];
    float up[3] = {0, 0, 1};
    memset(hits, 0, sizeof hits);
    uint32_t n = g_raycast(id, from, up, len, 2, 1, 0, 0x80000009u, unit, hits, 16);
    for (uint32_t i = 0; i < n && i < 16; i++) if (IsFoeUnit(Word(hits + 44u * i + 28))) return 1;
    return 0;
}
static int Fits(uint32_t unit, float x, float y, float z)
{
    float up[3] = {0, 0, 1}, foot[3] = {x, y, z + 0.1f};
    float r = RayWith(unit, foot, up, 1.8f, 0, 0x80000009u, NULL);
    if (r < 0 || r <= 1.8f) return 0;
    if (g_nFoeUnits && FoeAbove(unit, foot, 12.0f)) return 0;   /* under a Bile Titan, a Charger, a hulk: taken */
    for (int h = 0; h < 2; h++)
        for (int k = 0; k < 4; k++)
        {
            float a = 1.5708f * (float)k + 0.785f;
            float c[3] = {x, y, z + (h ? 1.3f : 0.6f)}, dir[3] = {cosf(a), sinf(a), 0};
            float w = RayWith(unit, c, dir, 0.4f, 0, 0x80000009u, NULL);
            if (w < 0 || w <= 0.4f) return 0;
        }
    return 1;
}

static int Occupied(const float at[3])
{
    float p[3];
    uint8_t d[24];
    for (int i = 0; i < FOES; i++)
    {
        if (!g_foe[i] || !Descriptor(g_foe[i], d) || Quad(d) != g_foeRes[i] || !UnitNode(Word(d + 12), 0, p)) continue;
        float dx = p[0] - at[0], dy = p[1] - at[1], dz = p[2] - at[2];
        if (dx * dx + dy * dy <= 1.5f * 1.5f && dz > -2 && dz < 2.5f) return 1;
    }
    return 0;
}
/* Whether a surface with another one `gap` m above it is a real floor under a real roof (a bridge, an arch, a nest):
 * looking up from it must meet that roof. Terrain and many rocks are solid only from above, so under them the look up
 * meets nothing: that floor is underground (a buried mesh, a rock's base). 1 a real floor */
static int Roofed(uint32_t unit, float x, float y, float z, float gap)
{
    float up[3] = {0, 0, 1}, foot[3] = {x, y, z + 0.1f};
    float r = RayWith(unit, foot, up, gap + 0.5f, 0, 0x80000009u, NULL);
    return r >= 0 && r <= gap + 0.5f && fabsf(r - (gap - 0.1f)) < 0.6f;
}
static int g_buried;   /* spots skipped as underground (for the log) */
/* The best spot at a distance along her direction: the highest surface there she fits on; *crowded when one fitted
 * but an enemy stood on it. 1 found */
static int SpotAt(uint32_t unit, const float pos[3], const float dir[2], float dist, float out[3], int* crowded, int upFirst)
{
    float z[16];
    float x = pos[0] + dir[0] * dist, y = pos[1] + dir[1] * dist;
    g_underFoe = 0;
    int n = Surfaces(unit, x, y, pos[2], z, 16);
    if (g_underFoe && crowded) *crowded = 1;
    float high[16];
    memcpy(high, z, sizeof(float) * (n > 0 ? n : 0));   /* highest first */
    if (!upFirst)   /* the surface nearest her own height first, then outwards */
        for (int i = 1; i < n; i++)
            for (int j = i; j > 0 && fabsf(z[j] - pos[2]) < fabsf(z[j - 1] - pos[2]); j--) { float t = z[j]; z[j] = z[j - 1]; z[j - 1] = t; }
    for (int i = 0; i < n; i++)
    {
        float above = 1e9f;   /* the lowest surface over this one */
        for (int j = 0; j < n; j++) if (high[j] > z[i] + 0.3f && high[j] < above) above = high[j];
        if (above < 1e8f && !Roofed(unit, x, y, z[i], above - z[i])) { g_buried++; continue; }
        if (!Fits(unit, x, y, z[i])) { if (g_nFoeUnits && crowded && FoeAbove(unit, (float[3]){x, y, z[i] + 0.1f}, 12.0f)) *crowded = 1; continue; }
        float at[3] = {x, y, z[i]};
        if (Occupied(at)) { if (crowded) *crowded = 1; continue; }
        memcpy(out, at, sizeof at);
        return 1;
    }
    return 0;
}

