/* kitbase/kb_fit.h: whether a Helldiver has room at a spot, asked of the game itself. Needs kb_core.h (Pointer, ReadAt,
 * Word, Log, hf, game). A teleport's landing checks look for the ground, keep off ground that is underground and keep a
 * clear way; whether she fits there is this test's to say where it can be asked (Ascalon 0.6.8: the Warp Pack sets a
 * Helldiver down in the middle of a swarm by it). */
#ifndef KB_FIT_H
#define KB_FIT_H
/* The game's own fit test (from Ascalon 0.4.6; the LIFT-182 Warp Pack's, from the code dump: its update takes a candidate only when
 * game.dll+0x68C340 says the wearer's mover shape overlaps nothing there): FitsAt(manager [game+0x3326598], out
 * {u32 ok; float pos[3]}, an entity record of which only the id at +8 and the unit at +0xC are read, mode 0, filter
 * 0x48D6C53A, the feet position, tolerance 0.1). It looks the entity's mover up without a check (a missing entity
 * indexes -1), so everything it reads is read here first and the call is skipped unless all of it is there:
 * the entity in the manager's hash, its descriptor (whose unit must be her unit), the descriptor's resource in the
 * mover table, the mover and its shape (the physics table's own two calls), and the query slot.
 * 1 fits, 0 does not, -1 not asked (the caller keeps its own checks) */
typedef struct { uint32_t ok; float pos[3]; } GameFit;
typedef GameFit* (*FitsAtFn)(void*, GameFit*, const void*, uint32_t, uint32_t, const float*, float);
typedef uint32_t (*MoverFn)(uint32_t, uint32_t);
typedef void* (*ShapeFn)(uint32_t);
static FitsAtFn g_fitsAt;
static int kb_gameFit = 1;   /* the game_fit setting: 0 leaves her room to the mod's own looks */
static unsigned g_fitAsked, g_fitNo, g_fitSkipped;
static int KbGameFits(uint32_t entity, uint32_t unit, const float pos[3])
{
    static unsigned told;
    const char* miss = NULL;
    uintptr_t mgr, ents, descs, desc, em, tab, phys, f0, f60, qt;
    uint32_t invalid = 0, cap = 0, empty = 0, mult = 0, idx = 0xFFFFFFFFu, name = 0, dunit = 0;
    uint64_t res = 0;
    if (!kb_gameFit || !g_fitsAt) return -1;
    if (!(mgr = Pointer(game + 0x3326598)) || !ReadAt(game + 0x3483C20, &invalid, 4) || entity == invalid) miss = "the manager";
    else if (!(ents = Pointer(mgr + 0x18)) || !ReadAt(mgr + 0x20, &cap, 4) || !ReadAt(mgr + 0x24, &empty, 4) || !ReadAt(mgr + 0x28, &mult, 4) ||
             !cap || (cap & (cap - 1)) || cap > (1u << 20)) miss = "the manager's hash";
    else
    {
        for (uint32_t i = 0; i < cap; i++)
        {
            uint32_t e[2];
            if (!ReadAt(ents + 8ull * ((i + entity * mult) & (cap - 1)), e, 8)) break;
            if (e[0] == entity) { idx = e[1]; break; }
            if (e[0] == empty) break;
        }
        if (idx == 0xFFFFFFFFu || idx > (1u << 20)) miss = "her entity in the manager";
        else if (!(descs = Pointer(mgr + 0x30)) || !(desc = Pointer(descs + 8ull * idx)) || !ReadAt(desc, &res, 8) || !res ||
                 !ReadAt(desc + 0xC, &dunit, 4) || dunit != unit) miss = "her descriptor";
        else if (!(em = Pointer(game + 0x346BF98)) || !(tab = Pointer(em + 0xF12840))) miss = "the mover table";
        else
        {
            int found = 0;
            for (uint32_t k = 0, b = (uint32_t)(res % 44); k < 44 && !found; k++, b = (b + 1) % 44)
            {
                uint8_t bucket[16];
                uint64_t key;
                if (!ReadAt(tab + 16ull * b, bucket, 16)) break;
                memcpy(&key, bucket, 8);
                if (!key) break;
                if (key == res) { found = ReadAt(tab + 0x2C0 + 0xF0ull * Word(bucket + 8) + 0x48, &name, 4); break; }
            }
            if (!found) miss = "her mover's record";
            else if (!(phys = Pointer(game + 0x3326388)) || !(f0 = Pointer(phys)) || !(f60 = Pointer(phys + 0x60)) ||
                     !(qt = Pointer(game + 0x3326328)) || !Pointer(qt + 0x80)) miss = "the physics tables";
            else
            {
                uint32_t mover = ((MoverFn)f0)(unit, name);
                if (!mover || mover == 0xFFFFFFFFu || !((ShapeFn)f60)(mover)) miss = "her mover or its shape";
            }
        }
    }
    if (miss)
    {
        g_fitSkipped++;
        if (told++ < 6) Log("the game's fit test is not asked: %s is not there (her own checks decide)", miss);
        return -1;
    }
    struct { uint64_t unused; uint32_t id, unit; } ent = {0, entity, unit};
    GameFit out = {0};
    g_fitsAt((void*)mgr, &out, &ent, 0, 0x48D6C53Au, pos, 0.1f);
    g_fitAsked++;
    if (!out.ok) g_fitNo++;
    return out.ok ? 1 : 0;
}
/* Once, when the mod starts: the test is used only where the game's code is the reviewed code */
static void KbFitInit(void)
{
    if (hf->code_matches(game + 0x68C340, "40 55 53 56 57 41 56 48 8d ac 24 e0 fe ff ff 48 81 ec 20 02 00 00 0f 29") &&
        hf->code_matches(game + 0x68C230, "40 53 48 83 ec 30 3b 15 e4 79 df 02 4c 8b d1 48 89 7c 24 50"))
        g_fitsAt = (FitsAtFn)(game + 0x68C340);
    else Log("the game's fit test is not the reviewed code: the mod's own checks decide where she may land");
}
/* 1 when the test can be asked at all (the room looks of the mod are then left out) */
__attribute__((unused)) static int KbFitUsed(void) { return kb_gameFit && g_fitsAt != NULL; }
/* The spot a surface gives, a little above it too (on a slope the shape set exactly on the surface touches the uphill
 * side): 1 fits (out[2] set to where), 0 does not, -1 not asked */
static int KbFitsOn(uint32_t entity, uint32_t unit, float out[3])
{
    for (int k = 0; k < 3; k++)
    {
        float at[3] = {out[0], out[1], out[2] + (k == 0 ? 0.05f : k == 1 ? 0.2f : 0.4f)};
        int fit = KbGameFits(entity, unit, at);
        if (fit < 0) return -1;
        if (fit) { out[2] = at[2] - 0.05f; return 1; }
    }
    return 0;
}
#endif
