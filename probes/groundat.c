static int GroundAt(uint32_t unit, float x, float y, float ref, float* z)
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
    /* the surfaces under the point, as heights */
    float h[16];
    int k = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        float d;
        memcpy(&d, hits + 44u * i + 24, 4);
        if (Word(hits + 44u * i + 28) == unit || !isfinite(d) || d < 0.02f || d > len) continue;
        h[k++] = top[2] - d;
    }
    if (!k) return 0;
    /* the one nearest her height; but where something is over it within her height (a bug nest, a rock or a structure
     * standing on the terrain: the terrain runs on under it) she would be inside it: then the highest surface */
    int best = 0, top_ = 0;
    for (int i = 1; i < k; i++)
    {
        if (fabsf(h[i] - ref) < fabsf(h[best] - ref)) best = i;
        if (h[i] > h[top_]) top_ = i;
    }
    for (int i = 0; i < k; i++)
        if (h[i] > h[best] + 0.3f && h[i] < h[best] + 2.0f) { best = top_; break; }
    *z = h[best];
    return 1;
}
