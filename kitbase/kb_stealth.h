/* kitbase layer 2: being seen. Her visibility cut (how far enemies see her) and enemies forgetting her.
 *
 * Code shared by the character kits. Source of truth: kitbase/ at the repo root. The copy in a kit's src/kitbase/ is
 * written by tools/kitbase_sync.py, so edit kitbase/, never the copy. See docs/kit-base.md.
 * Lifted 2026-11-26 from Ascalon (her hiding: the visibility row) and W 0.9.0 (Ambush), both on Lost Contact 0.4.1's
 * records: behaviour game+0x3326740 (map +0x40, count +0x20, records +0x60, 0x1F8 each: target +0x18, its perceiver
 * +0x68), perception game+0x3326548 (map +0x30, records +0x50, 0x13F8 each: three lists of what it senses, 0x50 per
 * entry with the sense bits at +0x4C, and three single entries).
 *
 * KbVisCut(on, percent) every frame it should hold (it writes only when the game rewrote the row); KbVisCut(0, 0) puts
 * the row back. KbForgetPass(range) for the enemies within range m that do not sense her right now: they forget her. */
#ifndef KB_STEALTH_H
#define KB_STEALTH_H

#include "kb_core.h"
#include "kb_player.h"
#include "kb_friends.h"

/* ---- her visibility row (game.dll+0x3326540: map +0x20, rows +0x40, 20 bytes each: base +0, floor +12) ---------- */
KB_VAR float g_visOldBase, g_visWroteBase, g_visWroteFloor, g_visOldFloor;
KB_VAR int g_visCut;
KB_FN uintptr_t VisRow(void)
{
    uint32_t me = atomic_load(&g_avatar), i;
    uintptr_t m = Pointer(game + 0x3326540), rows;
    if (!me || !m || !Lookup(m + 0x20, me, 1048576, &i) || i >= 262144 || !(rows = Pointer(m + 0x40))) return 0;
    return rows + 20ull * i;
}
KB_FN void KbVisCut(int on, float percent)
{
    uintptr_t row = VisRow();
    float base, floor;
    if (!row || !ReadAt(row, &base, 4) || !ReadAt(row + 12, &floor, 4)) { g_visCut = 0; return; }
    float mult = 1 - percent / 100;
    if (on)
    {
        if (!(base >= 0 && base <= 10) || !(floor >= 0 && floor <= 10)) return;
        if (!g_visCut || fabsf(base - g_visWroteBase) > 1e-6f)
        {
            if (!g_visCut && !(base >= 0.1f)) return;                       /* not a row the research knows */
            if (!g_visCut) Log("her visibility %.3f -> %.3f (floor %.3f -> %.3f)", (double)base, (double)(base * mult), (double)floor, (double)(floor * mult));
            g_visOldBase = base; g_visWroteBase = base * mult;
            hf->write(row, &g_visWroteBase, 4);
        }
        if (!g_visCut || fabsf(floor - g_visWroteFloor) > 1e-6f)
        {
            g_visOldFloor = floor; g_visWroteFloor = floor * mult;
            hf->write(row + 12, &g_visWroteFloor, 4);
        }
        g_visCut = 1;
    }
    else if (g_visCut)
    {
        if (fabsf(base - g_visWroteBase) <= 1e-6f) hf->write(row, &g_visOldBase, 4);
        if (fabsf(floor - g_visWroteFloor) <= 1e-6f) hf->write(row + 12, &g_visOldFloor, 4);
        g_visCut = 0;
    }
}

/* ---- forgetting her -------------------------------------------------------------------------------------------- */
KB_FN uintptr_t Behaviour(uint32_t e)
{
    uintptr_t m = Pointer(game + 0x3326740), recs;
    uint32_t i, count;
    if (!m || !Lookup(m + 0x40, e, 1048576, &i) || !ReadAt(m + 0x20, &count, 4) || i >= count || i >= 16384 || !(recs = Pointer(m + 0x60))) return 0;
    return recs + 0x1F8ull * i;
}
KB_FN uintptr_t Perception(uint32_t e)
{
    uintptr_t m = Pointer(game + 0x3326548), recs;
    uint32_t i;
    if (!m || !Lookup(m + 0x30, e, 1048576, &i) || i >= 16384 || !(recs = Pointer(m + 0x50))) return 0;
    return recs + 0x13F8ull * i;
}
KB_VAR const struct { uint32_t count, entries; } kSenseLists[3] = {{0x310, 0x318}, {0x818, 0x820}, {0xD20, 0xD28}};
KB_VAR const uint32_t kSenseSingles[3] = {0x1228, 0x1278, 0x12C8};
/* the sense bits of her entries in a perception record, or'd: nonzero while that perceiver senses her now */
KB_FN uint32_t SensedIn(uintptr_t pr, uint32_t me)
{
    uint32_t bits = 0;
    for (int l = 0; l < 3; l++)
    {
        uint32_t c = 0;
        uint8_t ent[0x50];
        if (!ReadAt(pr + kSenseLists[l].count, &c, 4) || c > 16) continue;
        for (uint32_t k = 0; k < c; k++)
            if (ReadAt(pr + kSenseLists[l].entries + 0x50u * k, ent, sizeof ent) && Word(ent) == me) bits |= Word(ent + 0x4C);
    }
    return bits;
}
/* does a perception record hold an entry of her at all? */
KB_FN int HoldsIn(uintptr_t pr, uint32_t me)
{
    for (int l = 0; l < 3; l++)
    {
        uint32_t c = 0, id = 0;
        if (!ReadAt(pr + kSenseLists[l].count, &c, 4) || c > 16) continue;
        for (uint32_t k = 0; k < c; k++) if (ReadAt(pr + kSenseLists[l].entries + 0x50u * k, &id, 4) && id == me) return 1;
    }
    for (int k = 0; k < 3; k++)
    {
        uint32_t id = 0, used = 0;
        if (ReadAt(pr + kSenseSingles[k], &id, 4) && id == me && ReadAt(pr + kSenseSingles[k] + 0x48, &used, 4) && used) return 1;
    }
    return 0;
}
/* her entries taken out of a perception record (the caller has checked nobody there senses her): entries removed */
KB_FN int ForgetIn(uintptr_t pr, uint32_t me)
{
    int n = 0;
    for (int l = 0; l < 3; l++)
    {
        uint32_t c = 0;
        if (!ReadAt(pr + kSenseLists[l].count, &c, 4) || c > 16 || !c) continue;
        uint8_t list[16 * 0x50];
        if (!ReadAt(pr + kSenseLists[l].entries, list, 0x50u * c)) continue;
        uint32_t keep = 0;
        for (uint32_t k = 0; k < c; k++)
            if (Word(list + 0x50u * k) != me) { if (keep != k) memmove(list + 0x50u * keep, list + 0x50u * k, 0x50); keep++; }
        if (keep == c) continue;
        if (keep && hf->write(pr + kSenseLists[l].entries, list, 0x50u * keep)) continue;
        if (hf->write(pr + kSenseLists[l].count, &keep, 4)) continue;
        n += (int)(c - keep);
    }
    for (int s = 0; s < 3; s++)
    {
        uintptr_t at = pr + kSenseSingles[s];
        uint32_t id = 0, used = 0;
        static const uint32_t zero = 0;
        if (ReadAt(at, &id, 4) && id == me && ReadAt(at + 0x48, &used, 4) && used && !hf->write(at + 0x48, &zero, 4)) n++;
    }
    return n;
}
/* Every foe within `range` m that does not sense her now (itself or its perceiver) forgets her: its target and its
 * entries of her. One that senses her keeps her. Returns how many forgot; *sensing how many sense her. */
KB_FN int KbForgetPass(float range, int* sensing)
{
    uint32_t me = atomic_load(&g_avatar);
    float mp[3], p[3];
    int forgot = 0, sense = 0;
    if (sensing) *sensing = 0;
    if (!me || !PlayerPose(mp, NULL)) return 0;
    for (int j = 0; j < FOES; j++)
    {
        uint32_t id = g_foe[j];
        uint8_t d[24], b[0xA0];
        if (!id || !Descriptor(id, d) || Quad(d) != g_foeRes[j] || !UnitNode(Word(d + 12), 0, p) || Dist2(p, mp) > range * range) continue;
        uintptr_t br = Behaviour(id), pr = Perception(id), sq = 0;
        uint32_t perceiver = 0, bits = 0;
        int target = 0, had = 0;
        if (br && ReadAt(br, b, sizeof b)) { perceiver = Word(b + 0x68); target = Word(b + 0x18) == me; }
        if (perceiver && perceiver != id) sq = Perception(perceiver);
        if (pr) bits |= SensedIn(pr, me);
        if (sq) bits |= SensedIn(sq, me);
        if (bits) { sense++; continue; }
        if (target)
        {
            static const uint32_t z = 0;
            static const uint64_t z8 = 0;
            hf->write(br + 0x18, &z, 4); hf->write(br + 0x78, &z, 4); hf->write(br + 0x98, &z8, 8);
            had = 1;
        }
        if (pr && ForgetIn(pr, me)) had = 1;
        if (sq && ForgetIn(sq, me)) had = 1;
        forgot += had;
    }
    if (sensing) *sensing = sense;
    return forgot;
}

#endif
