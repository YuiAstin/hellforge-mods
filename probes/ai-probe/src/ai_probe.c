/* AI Probe (yui.ai-probe): does the AI the sentries and guard dogs use (docs/research/ai-modding.md: perception records,
 * behaviour records) also run the enemies, and can it hide her from them? Research only; host or solo.
 *
 *   Shift+F7  survey: every enemy within 60 m, its behaviour record (id, stage, target, re-pick time) and perception
 *             record (list sizes, her entry and its sense bits), and who each one is after
 *   F7        a watch: the survey's tally (how many enemies are after her) once a second for 15 s
 *   Ctrl+F7   forget (0.2.3): for 10 s, four times a second, an enemy that cannot sense her right now (none of her
 *             entries has sense bits; its squad's record too) loses her entries altogether: they are taken out of its
 *             perception lists (the entries after them moved up, the count lowered), and its re-pick is forced.
 *             The idea: the game refreshes an entry while she is seen, so it is removed only once they lost sight
 *   Ctrl+Alt+F7  (0.3.0) auto break-contact on / off: see AutoPass. Ctrl+Shift+Alt+F7 is 0.2.6's hard forget by hand:
 *   hard forget (0.2.6): as Ctrl+F7, and an enemy after her that cannot sense her also has its behaviour
 *             target cleared (+0x18 and has-target +0x78 to 0) and is sent to where it stands (set_destination to its
 *             own position: it stops). Breach spawns came through smoke after a plain forget (0.2.5's test): this
 *             shows whether their target or their destination is what keeps them on her. The tally adds how many
 *             are after her with no perception entry for her at all ("by other means": a breach's hand-off)
 *   Ctrl+Shift+F7  0.2.2's hide: for 10 s, her entries in nearby enemies' perception lists lose their sense bits (as
 *             Smarter Guard Dogs hides targets from sentries) and their re-pick time is set to 0 (a re-pick now); the
 *             tally runs alongside, so the log shows whether they lose her
 *   Alt+F7    patrols (0.2.0): for 30 s, every 2 s, the navigation record (Nav Probe's [game+0x3326418], 0xA8 bytes) of
 *             each active enemy within 150 m, as floats, with every float triple's distance to her: the triple that
 *             tracks her position as she moves (a patrol walking to her) is its destination. Lemuen's sniper nest.
 *   Shift+Alt+F7  redirect: every active nav record within 150 m (up to 12) gets set_destination
 *             (game 0x5B36C0, as Nav Probe) to a point 80 m off to her side; the trace above then shows if they go.
 *             0.2.4: the point is kept per enemy and sent again on every trace (2 s) for the 30 s, and the trace
 *             logs how far each one's destination (+0x60) and the enemy are from it, and whether it is after her
 *
 * Layouts (ai-modding.md, from HellForge's behaviours.c, HD2Runtime's research and Smarter Guard Dogs; build 25480438):
 *   behaviours  [game+0x3326740]: map +0x40 (entity -> index), count +0x20, owners +0x58, records +0x60, 0x1F8 each;
 *               record +0 behaviour id, +0x08 stage, +0x18 target entity (a copy of a perception entry from +0x18),
 *               +0x68 the perceiver's key, +0x78 has-target, +0x98 next re-pick time
 *   perception  [game+0x3326548]: map +0x30, owners +0x48, records +0x50, 0x13F8 each; lists A (count +0x310, entries
 *               +0x318), B (+0x818 / +0x820), C (+0xD20 / +0xD28), 0x50 per entry (+0 entity, +0x48 flags, +0x4C sense
 *               bits), singles at +0x1228 / +0x1278 / +0x12C8; +0x13E8 its own faction mask
 * Credit: these layouts are HD2Runtime's research (SkyeShade), HellForge's turret code and Smarter Guard Dogs' (chef,
 * ported by giblett); nothing of their code is copied. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include <windows.h>
#include "hellforge_mod.h"

#define BUILD "25480438"
#define VERSION "0.3.1"
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
static uint64_t U64(const uint8_t* p) { uint64_t v; memcpy(&v, p, 8); return v; }

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

/* ---- entities: resource, unit, position (as Nav Probe) ------------------------------------------------------------- */
typedef void* (*ResolveFn)(uint32_t);
typedef const float* (*LocalPosFn)(uint32_t, uint32_t);
static ResolveFn g_entityResolve, g_unitResolve;
static LocalPosFn g_localPosition;
typedef struct { uint64_t resource; uint32_t entity, unit; } Ent;
static int Resolve(uint32_t entity, Ent* e)
{
    if (!entity || !g_entityResolve) return 0;
    void* o = g_entityResolve(entity);
    uint8_t h[24];
    if (!o || !RD((uintptr_t)o, h, 24) || U32(h + 8) != entity) return 0;
    e->resource = U64(h); e->entity = entity; e->unit = U32(h + 12);
    return 1;
}
static int RootOf(uint32_t unit, float out[3])
{
    if (!g_localPosition || !g_unitResolve || !g_unitResolve(unit)) return 0;
    const float* p = g_localPosition(unit, 0);
    return p && RD((uintptr_t)p, out, 12) && isfinite(out[0]) && isfinite(out[2]);
}
static int Hostile(uint64_t res)
{
    uint32_t stride;
    return res && hf->component_record("BehaviorComponentData", res, &stride) && !hf->component_record("AvatarComponentData", res, &stride) &&
           !hf->component_record("TurretComponentData", res, &stride) && !hf->component_record("SeatCollectionComponentData", res, &stride);
}
#define AVATAR UINT64_C(0x4D1C334D294DFA97)
static const char* Who(uint32_t target, uint32_t me)
{
    Ent t;
    if (!target) return "nobody";
    if (target == me) return "HER";
    if (!Resolve(target, &t)) return "unknown";
    if (t.resource == AVATAR) return "another Helldiver";
    uint32_t stride;
    if (hf->component_record("TurretComponentData", t.resource, &stride)) return "a turret";
    if (hf->component_record("SeatCollectionComponentData", t.resource, &stride)) return "a vehicle";
    return Hostile(t.resource) ? "an enemy (?)" : "something else";
}

#define SEEN 2048
static uint32_t g_seenEnt[SEEN];
static uint64_t g_seenRes[SEEN];
static uint8_t g_seenKind[SEEN];   /* 0 not checked yet, 1 an enemy, 2 not one or gone */
static unsigned g_nseen;
/* 0.2.1: an enemy among the spawns seen, its entity and position. 0.2.0 asked the component tables (four string lookups)
 * about every one of up to 2048 spawns on each pass, and Ctrl+F7 ran a pass every frame: seconds a frame (the freeze).
 * Now each spawn is asked once, and one that has gone is dropped. */
static int g_classified;   /* new spawn types asked about in this pass */
/* 0.2.5: the answer kept per resource (an enemy type), not per spawn: a wave of one type is asked about once. 0.2.4
 * still stalled (22 to 72 ms passes) asking the component tables about each new spawn */
static uint64_t g_kindRes[1024];
static uint8_t g_kindOf[1024];
static int KindOf(uint64_t res)
{
    unsigned h = (unsigned)((res ^ (res >> 29)) * 0x9E3779B1u) & 1023;
    for (unsigned k = 0; k < 1024; k++, h = (h + 1) & 1023)
    {
        if (g_kindRes[h] == res) return g_kindOf[h];
        if (!g_kindRes[h])
        {
            if (g_classified >= 4) return 0;   /* ask the rest on a later pass */
            g_classified++;
            g_kindRes[h] = res;
            g_kindOf[h] = Hostile(res) ? 1 : 2;
            return g_kindOf[h];
        }
    }
    return 2;
}
/* 0.3.0: the live enemies, kept as they spawn (new spawns classified a few types a pass), dead ones dropped as they
 * are met. Passes walk this list, not all 2048 spawns (0.2.x's passes stalled 20 to 70 ms walking the whole ring) */
#define LIVE 512
static uint32_t g_live[LIVE];
static uint64_t g_liveRes[LIVE];
static double g_liveUnseen[LIVE];      /* auto: when it last stopped sensing her (0: sensing, or not after her) */
static float g_liveLast[LIVE][3];      /* auto: where she was when it last sensed her */
static uint8_t g_liveHasLast[LIVE];
static int g_nlive;
static unsigned g_cursor;
static void Refresh(void)
{
    unsigned end = g_nseen;
    if (end - g_cursor > SEEN) g_cursor = end - SEEN;
    while (g_cursor != end)
    {
        unsigned i = g_cursor % SEEN;
        int k = KindOf(g_seenRes[i]);
        if (!k) break;   /* this type next pass */
        g_cursor++;
        if (k == 1 && g_nlive < LIVE)
        {
            g_live[g_nlive] = g_seenEnt[i]; g_liveRes[g_nlive] = g_seenRes[i];
            g_liveUnseen[g_nlive] = 0; g_liveHasLast[g_nlive] = 0;
            g_nlive++;
        }
    }
}
/* the live enemy at j, its entity and position; a gone one is dropped (the last moved into its place: walk j down) */
static int LiveEnemy(int j, Ent* e, float p[3])
{
    if (!Resolve(g_live[j], e) || e->resource != g_liveRes[j])
    {
        int l = --g_nlive;
        g_live[j] = g_live[l]; g_liveRes[j] = g_liveRes[l]; g_liveUnseen[j] = g_liveUnseen[l];
        memcpy(g_liveLast[j], g_liveLast[l], sizeof g_liveLast[j]); g_liveHasLast[j] = g_liveHasLast[l];
        return 0;
    }
    return RootOf(e->unit, p);
}

/* ---- the two records ------------------------------------------------------------------------------------------------- */
static uintptr_t Behaviour(uint32_t e)
{
    uintptr_t m = P(game + 0x3326740), recs;
    uint32_t i, count;
    if (!m || !Lookup(m + 0x40, e, &i) || !RD(m + 0x20, &count, 4) || i >= count || i >= 16384 || !(recs = P(m + 0x60))) return 0;
    return recs + 0x1F8ull * i;
}
static uintptr_t Perception(uint32_t e)
{
    uintptr_t m = P(game + 0x3326548), recs;
    uint32_t i;
    if (!m || !Lookup(m + 0x30, e, &i) || i >= 16384 || !(recs = P(m + 0x50))) return 0;
    return recs + 0x13F8ull * i;
}
static const struct { uint32_t count, entries; const char* name; } kLists[3] = {{0x310, 0x318, "A"}, {0x818, 0x820, "B"}, {0xD20, 0xD28, "C"}};
static const uint32_t kSingles[3] = {0x1228, 0x1278, 0x12C8};
/* her entries in a perception record: how many, and the sense bits of the first */
static int HerEntries(uintptr_t pr, uint32_t me, uint32_t* sense, int counts[3])
{
    int found = 0;
    for (int l = 0; l < 3; l++)
    {
        uint32_t n = 0;
        counts[l] = RD(pr + kLists[l].count, &n, 4) && n <= 16 ? (int)n : -1;
        for (uint32_t k = 0; counts[l] > 0 && k < n; k++)
        {
            uint8_t ent[0x50];
            if (!RD(pr + kLists[l].entries + 0x50u * k, ent, sizeof ent)) break;
            if (U32(ent) == me) { if (!found) *sense = U32(ent + 0x4C); found++; }
        }
    }
    for (int s = 0; s < 3; s++)
    {
        uint8_t ent[0x50];
        if (RD(pr + kSingles[s], ent, sizeof ent) && U32(ent + 0x48) && U32(ent) == me) { if (!found) *sense = U32(ent + 0x4C); found++; }
    }
    return found;
}
/* 0.2.3: her entries taken out, only while none of them has sense bits (the perceiver cannot sense her now): 1 per
 * entry removed, 0 when she is sensed or has no entry. A list entry is removed by moving the ones after it up a place
 * and lowering the count; a single slot by clearing its in-use flag (+0x48). */
static int ForgetIn(uintptr_t pr, uint32_t me)
{
    uint32_t sense = 0;
    int counts[3];
    if (!HerEntries(pr, me, &sense, counts)) return 0;
    for (int l = 0; l < 3; l++)
        for (int k = 0; k < counts[l]; k++)
        {
            uint8_t ent[0x50];
            if (RD(pr + kLists[l].entries + 0x50u * k, ent, sizeof ent) && U32(ent) == me && U32(ent + 0x4C)) return 0;
        }
    for (int s = 0; s < 3; s++)
    {
        uint8_t ent[0x50];
        if (RD(pr + kSingles[s], ent, sizeof ent) && U32(ent + 0x48) && U32(ent) == me && U32(ent + 0x4C)) return 0;
    }
    int n = 0;
    for (int l = 0; l < 3; l++)
    {
        uint32_t c = 0;
        if (!RD(pr + kLists[l].count, &c, 4) || c > 16) continue;
        uint8_t list[16 * 0x50];
        if (c && !RD(pr + kLists[l].entries, list, 0x50u * c)) continue;
        uint32_t keep = 0;
        for (uint32_t k = 0; k < c; k++)
            if (U32(list + 0x50u * k) != me) { if (keep != k) memmove(list + 0x50u * keep, list + 0x50u * k, 0x50); keep++; }
        if (keep == c) continue;
        if (keep && hf->write(pr + kLists[l].entries, list, 0x50u * keep)) continue;
        if (hf->write(pr + kLists[l].count, &keep, 4)) continue;
        n += (int)(c - keep);
    }
    for (int s = 0; s < 3; s++)
    {
        uintptr_t at = pr + kSingles[s];
        uint32_t id = 0, used = 0;
        static const uint32_t zero = 0;
        if (RD(at, &id, 4) && id == me && RD(at + 0x48, &used, 4) && used && !hf->write(at + 0x48, &zero, 4)) n++;
    }
    return n;
}
/* her entries' sense bits cleared: 1 per entry cleared */
static int HideIn(uintptr_t pr, uint32_t me)
{
    int n = 0;
    static const uint32_t zero = 0;
    for (int l = 0; l < 3; l++)
    {
        uint32_t c = 0;
        if (!RD(pr + kLists[l].count, &c, 4) || c > 16) continue;
        for (uint32_t k = 0; k < c; k++)
        {
            uintptr_t at = pr + kLists[l].entries + 0x50u * k;
            uint32_t id = 0, sense = 0;
            if (RD(at, &id, 4) && id == me && RD(at + 0x4C, &sense, 4) && sense && !hf->write(at + 0x4C, &zero, 4)) n++;
        }
    }
    for (int s = 0; s < 3; s++)
    {
        uintptr_t at = pr + kSingles[s];
        uint32_t id = 0, sense = 0;
        if (RD(at, &id, 4) && id == me && RD(at + 0x4C, &sense, 4) && sense && !hf->write(at + 0x4C, &zero, 4)) n++;
    }
    return n;
}

/* ---- the survey ------------------------------------------------------------------------------------------------------ */
typedef struct { int enemies, withBehaviour, withPerception, afterHer, seeHer, hidden, kicked, squads, sensed, blind, cleared; } Tally;
typedef void (*SetDestFn)(void*, uint32_t, const float*);
static SetDestFn g_setDest;
static int g_forget, g_hard;   /* 0.2.3: the hide is the forget (Ctrl+F7), else 0.2.2's blanking (Ctrl+Shift+F7) */
static Tally Pass(int detail, int hide)
{
    g_classified = detail ? -100000 : 0;
    Tally t = {0};
    Ent me, e;
    float mp[3], p[3];
    uint32_t meId = atomic_load(&g_avatar);
    if (!Resolve(meId, &me) || !RootOf(me.unit, mp)) { if (detail) Log("survey: your position is unknown"); return t; }
    Refresh();
    int told = 0;
    for (int j = g_nlive - 1; j >= 0; j--)
    {
        uint32_t id = g_live[j];
        if (!LiveEnemy(j, &e, p)) continue;
        float d = sqrtf((p[0] - mp[0]) * (p[0] - mp[0]) + (p[1] - mp[1]) * (p[1] - mp[1]));
        if (d > 60) continue;
        t.enemies++;
        uintptr_t br = Behaviour(id), pr = Perception(id);
        uint8_t b[0xA0] = {0};
        int hb = br && RD(br, b, sizeof b);
        uint32_t target = hb ? U32(b + 0x18) : 0, sense = 0;
        int counts[3] = {-1, -1, -1}, mine = pr ? HerEntries(pr, meId, &sense, counts) : 0;
        if (hb) t.withBehaviour++;
        if (pr) t.withPerception++;
        if (hb && target == meId) t.afterHer++;
        uint32_t sqSense = 0;
        int sqCounts[3], sqMine = 0;
        {
            uint32_t pv = hb ? U32(b + 0x68) : 0;
            uintptr_t sp = pv && pv != id ? Perception(pv) : 0;
            if (sp) sqMine = HerEntries(sp, meId, &sqSense, sqCounts);
        }
        if (hb && target == meId && !mine && !sqMine) t.blind++;
        if (mine && sense) t.seeHer++;
        if (hide)
        {
            /* 0.2.2: squad members perceive through their squad's record (perceiver +0x68 another entity, their own
             * lists empty in 0.2.1's test): hers there too */
            uint32_t perceiver = hb ? U32(b + 0x68) : 0;
            uintptr_t sq = perceiver && perceiver != id ? Perception(perceiver) : 0;
            if (hide == 2)
            {
                int f = (pr ? ForgetIn(pr, meId) : 0) + (sq ? ForgetIn(sq, meId) : 0);
                if (f) { t.hidden += f; if (sq) t.squads++; }
                else if ((mine && sense) || (sqMine && sqSense)) t.sensed++;
                /* hard: after her, and neither it nor its squad senses her now */
                if (g_hard && hb && target == meId && !(mine && sense) && !(sqMine && sqSense))
                {
                    static const uint32_t z = 0;
                    int w1 = hf->write(br + 0x18, &z, 4), w2 = hf->write(br + 0x78, &z, 4), okw = !w1 && !w2;
                    if (g_setDest) { _Alignas(16) float here[4] = {p[0], p[1], p[2], 0}; g_setDest(NULL, id, here); }
                    if (okw) t.cleared++;
                }
            }
            else
            {
                if (pr) t.hidden += HideIn(pr, meId);
                if (sq) { t.hidden += HideIn(sq, meId); t.squads++; }
            }
            static const uint64_t zero = 0;
            if (hb && target == meId && !hf->write(br + 0x98, &zero, 8)) t.kicked++;
        }
        if (detail && told < 30)
        {
            told++;
            uint32_t mask = 0;
            if (pr) RD(pr + 0x13E8, &mask, 4);
            Log("  enemy %u (type %016llx) %.0f m: behaviour %s (id %u, stage %u, has-target %u, re-pick %llu, perceiver %u) after %s (%u); "
                "perception %s (lists A %d B %d C %d, faction %08x), her entries %d (sense bits %08x)",
                id, (unsigned long long)e.resource, d, hb ? "yes" : "none", hb ? U32(b) : 0, hb ? U32(b + 0x08) : 0, hb ? U32(b + 0x78) : 0,
                hb ? (unsigned long long)U64(b + 0x98) : 0ull, hb ? U32(b + 0x68) : 0, hb ? Who(target, meId) : "-", target,
                pr ? "yes" : "none", counts[0], counts[1], counts[2], mask, mine, sense);
        }
    }
    return t;
}
static void Report(const char* what, Tally t)
{
    Log("%s: %d enemies within 60 m; %d with a behaviour record, %d with a perception record; %d after her, %d see her%s", what,
        t.enemies, t.withBehaviour, t.withPerception, t.afterHer, t.seeHer, "");
    if (t.hidden || t.kicked || t.sensed)
        Log("  %s: %d of her perception entries %s (%d through a squad's record), %d re-picks forced%s", g_forget ? "forget" : "hide",
            t.hidden, g_forget ? "removed" : "blanked", t.squads, t.kicked, "");
    if (g_forget && t.sensed) Log("  forget: %d enemies left alone (they sense her now)", t.sensed);
    if (t.blind) Log("  %d after her with no perception entry for her (by other means)", t.blind);
    if (t.cleared) Log("  hard forget: %d targets cleared and stopped where they stand", t.cleared);
}

/* ---- patrols: navigation records (Nav Probe 0.1.1's layout) and set_destination ------------------------------------ */
static int NavIndex(uint32_t e, uint32_t* i)
{
    uintptr_t m = P(game + 0x3326418);
    return m && Lookup(m + 0x30, e, i) && *i < 0x10000;
}
static uintptr_t NavRecord(uint32_t i)
{
    uintptr_t m = P(game + 0x3326418), recs = m ? P(m + 0x50) : 0;
    return recs ? recs + 0xA8ull * i : 0;
}
static int Boundary(uintptr_t at)
{
    uint8_t b[2];
    return !hf->read_code(at - 1, b, 2) && (b[0] == 0xCC || b[0] == 0xC3 || b[0] == 0x90) && b[1] != 0xCC;
}
static double g_traceUntil, g_traceNext, g_sendUntil;
static struct { uint32_t id; float to[3]; } g_sent[32];
static int g_nsent;
static float g_traceMe[3];
static int SentIndex(uint32_t id)
{
    for (int s = 0; s < g_nsent; s++) if (g_sent[s].id == id) return s;
    return -1;
}
static void Trace(int redirect)
{
    g_classified = 0;
    Ent me, e;
    float mp[3], p[3];
    uint32_t meId = atomic_load(&g_avatar);
    if (!Resolve(meId, &me) || !RootOf(me.unit, mp)) { Log("patrols: your position is unknown"); return; }
    float moved = sqrtf((mp[0] - g_traceMe[0]) * (mp[0] - g_traceMe[0]) + (mp[1] - g_traceMe[1]) * (mp[1] - g_traceMe[1]));
    memcpy(g_traceMe, mp, sizeof mp);
    Log("patrols: you at %.1f %.1f %.1f (moved %.1f m since the last trace)", mp[0], mp[1], mp[2], moved);
    Refresh();
    int told = 0, sent = 0;
    for (int j = g_nlive - 1; j >= 0 && told < 12; j--)
    {
        uint32_t id = g_live[j], k;
        if (!LiveEnemy(j, &e, p) || !NavIndex(id, &k)) continue;
        float d = sqrtf((p[0] - mp[0]) * (p[0] - mp[0]) + (p[1] - mp[1]) * (p[1] - mp[1]));
        if (d > 150) continue;
        uintptr_t rec = NavRecord(k);
        float f[42];
        if (!rec || !RD(rec, f, sizeof f)) continue;
        uint32_t active = U32((const uint8_t*)f + 0x10);
        if (!active) continue;
        told++;
        /* every float triple at a 4-byte step, and how far it is from her (the destination follows her as she moves) */
        char close[320];
        int w = 0;
        for (int o = 0; o + 3 <= 42 && w < 280; o++)
        {
            if (!isfinite(f[o]) || !isfinite(f[o + 1]) || !isfinite(f[o + 2]) || fabsf(f[o]) > 50000 || fabsf(f[o + 1]) > 50000) continue;
            float dd = sqrtf((f[o] - mp[0]) * (f[o] - mp[0]) + (f[o + 1] - mp[1]) * (f[o + 1] - mp[1]) + (f[o + 2] - mp[2]) * (f[o + 2] - mp[2]));
            if (dd < 30) w += snprintf(close + w, sizeof close - w, " +0x%02X:%.1fm", o * 4, dd);
        }
        close[w] = 0;
        Log("  enemy %u (%016llx) %.0f m away, nav record %u: triples within 30 m of you:%s", id, (unsigned long long)e.resource, d, k, w ? close : " none");
        int s = SentIndex(id);
        if (redirect && g_setDest && s < 0 && g_nsent < 32)
        {
            /* 80 m off to her side, square to the line from it to her; kept, and sent again on every trace */
            float vx = mp[0] - p[0], vy = mp[1] - p[1], h = sqrtf(vx * vx + vy * vy);
            if (h < 1) continue;
            s = g_nsent++;
            g_sent[s].id = id;
            g_sent[s].to[0] = mp[0] - vy / h * 80; g_sent[s].to[1] = mp[1] + vx / h * 80; g_sent[s].to[2] = mp[2];
        }
        if (s >= 0 && g_now < g_sendUntil && g_setDest)
        {
            _Alignas(16) float to[4] = {g_sent[s].to[0], g_sent[s].to[1], g_sent[s].to[2], 0};
            const float* dest = f + 0x60 / 4;
            float dd = sqrtf((dest[0] - to[0]) * (dest[0] - to[0]) + (dest[1] - to[1]) * (dest[1] - to[1]));
            float de = sqrtf((p[0] - to[0]) * (p[0] - to[0]) + (p[1] - to[1]) * (p[1] - to[1]));
            uintptr_t br = Behaviour(id);
            uint32_t target = 0;
            if (br) RD(br + 0x18, &target, 4);
            Log("    sent: its destination +0x60 is %.1f m from the point, it is %.1f m from it, %s", dd, de, target == meId ? "after HER" : target ? "after something else" : "after nobody");
            g_setDest(NULL, id, to);
            sent++;
        }
    }
    if (sent) Log("patrols: %d sent to their points (set_destination)%s", sent, redirect ? ", again every 2 s for 30 s" : "");
}
/* ---- 0.3.0: auto break-contact (Ctrl+Alt+F7 toggles) ------------------------------------------------------------------
 * Four times a second, every live enemy within 80 m that is after her: while it (or its squad) senses her, where she
 * stands is kept as its last sight of her; once it has not sensed her for s_lose seconds, it loses her: her perception
 * entries removed (its own and its squad's), its target cleared, and it is sent to where it last saw her (it searches
 * there). 0.2.6's hard forget, by hand, held exactly while nothing sensed her; this runs it on its own. */
static int g_auto;
static const double s_lose = 3.0;
static struct { int lost, after, sensing, nearby, chasing, passes; } g_autoSum;
static void AutoPass(void)
{
    Ent me, e;
    float mp[3], p[3];
    uint32_t meId = atomic_load(&g_avatar);
    if (!Resolve(meId, &me) || !RootOf(me.unit, mp)) return;
    Refresh();
    g_autoSum.passes++;
    for (int j = g_nlive - 1; j >= 0; j--)
    {
        uint32_t id = g_live[j];
        if (!LiveEnemy(j, &e, p)) continue;
        float d = sqrtf((p[0] - mp[0]) * (p[0] - mp[0]) + (p[1] - mp[1]) * (p[1] - mp[1]));
        if (d > 80) { g_liveUnseen[j] = 0; continue; }
        g_autoSum.nearby++;
        uintptr_t br = Behaviour(id), pr = Perception(id);
        uint8_t b[0xA0];
        if (!br || !RD(br, b, sizeof b)) continue;
        uint32_t target = U32(b + 0x18), perceiver = U32(b + 0x68), sense = 0, sqSense = 0;
        uintptr_t sq = perceiver && perceiver != id ? Perception(perceiver) : 0;
        int counts[3], mine = pr ? HerEntries(pr, meId, &sense, counts) : 0, sqMine = sq ? HerEntries(sq, meId, &sqSense, counts) : 0;
        if ((mine && sense) || (sqMine && sqSense))
        {
            if (target == meId) g_autoSum.chasing++;
            memcpy(g_liveLast[j], mp, sizeof mp); g_liveHasLast[j] = 1;
            g_liveUnseen[j] = 0;
            g_autoSum.sensing++;
            continue;
        }
        if (target == meId) g_autoSum.chasing++;   /* counted before the sensing check below would skip it */
        if (target != meId) { g_liveUnseen[j] = 0; continue; }
        g_autoSum.after++;
        if (!g_liveUnseen[j]) { g_liveUnseen[j] = g_now; continue; }
        if (g_now - g_liveUnseen[j] < s_lose) continue;
        /* it lost her */
        if (pr) ForgetIn(pr, meId);
        if (sq) ForgetIn(sq, meId);
        static const uint32_t z = 0;
        static const uint64_t z8 = 0;
        hf->write(br + 0x18, &z, 4); hf->write(br + 0x78, &z, 4); hf->write(br + 0x98, &z8, 8);
        if (g_setDest)
        {
            const float* to = g_liveHasLast[j] ? g_liveLast[j] : p;
            _Alignas(16) float at[4] = {to[0], to[1], to[2], 0};
            g_setDest(NULL, id, at);
        }
        g_liveUnseen[j] = 0;
        g_autoSum.lost++;
    }
}
/* ---- binding, events, the game thread --------------------------------------------------------------------------------- */
static int Bind(void)
{
    if (!hf->code_matches(game + 0xFD9D40, "48 83 ec 08 3b 0d 02 9f 4a 02 0f 84 9b 00 00 00") ||
        !hf->code_matches(exe + 0x9D8C0, "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20")) { Log("REFUSED: entity/unit lookups are not the reviewed code"); return 0; }
    g_entityResolve = (ResolveFn)(game + 0xFD9D40);
    g_unitResolve = (ResolveFn)(exe + 0x9D8C0);
    uintptr_t script = P(game + 0x3326308), units = script ? P(script + 0x18) : 0;
    if (!units || P(units) != exe + 0x1fc450) { Log("the unit API is not filled yet"); return 0; }
    g_localPosition = (LocalPosFn)(exe + 0x1fc450);
    uintptr_t bm = P(game + 0x3326740), pm = P(game + 0x3326548);
    uint32_t bc = 0;
    if (bm) RD(bm + 0x20, &bc, 4);
    Log("bound: behaviours [game+0x3326740] = %llx (%u records), perception [game+0x3326548] = %llx", (unsigned long long)bm, bc, (unsigned long long)pm);
    if (Boundary(game + 0x5B36C0)) g_setDest = (SetDestFn)(game + 0x5B36C0);
    Log("patrols: navigation records [game+0x3326418] = %llx, set_destination %s", (unsigned long long)P(game + 0x3326418), g_setDest ? "on" : "off (not a function start)");
    return bm && pm;
}
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* e = (const HfSpawnEvent*)info;
    unsigned i = g_nseen % SEEN;
    g_seenKind[i] = 0; g_seenEnt[i] = e->entity; g_seenRes[i] = e->resource; g_nseen++;
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
static double g_watchUntil, g_watchNext, g_hideUntil;
static Tally g_hideSum;
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
    static double autoNext, autoTold, autoSlowTold;
    if (g_auto && g_now >= autoNext)
    {
        autoNext = g_now + 0.25;
        LARGE_INTEGER a0, b0, f0;
        QueryPerformanceCounter(&a0);
        AutoPass();
        QueryPerformanceCounter(&b0); QueryPerformanceFrequency(&f0);
        double ms = (double)(b0.QuadPart - a0.QuadPart) * 1000.0 / (double)f0.QuadPart;
        if (ms > 20 && g_now - autoSlowTold > 10) { autoSlowTold = g_now; Log("auto: a pass took %.0f ms (%d live enemies)", ms, g_nlive); }
        /* 0.3.1: a line every 5 s while on (0.3.0 printed only when someone was after her unsensed, and said nothing
         * for a whole bot fight): averages per pass, so the counts read as enemies */
        if (g_now - autoTold >= 5 && g_autoSum.passes)
        {
            autoTold = g_now;
            double n = g_autoSum.passes;
            Log("auto: %d live; within 80 m %.1f, after her %.1f (sensing her %.1f, not %.1f); %d lost her in the last 5 s",
                g_nlive, g_autoSum.nearby / n, g_autoSum.chasing / n, g_autoSum.sensing / n, g_autoSum.after / n, g_autoSum.lost);
            memset(&g_autoSum, 0, sizeof g_autoSum);
        }
    }
    static double hideNext;
    if (g_now < g_hideUntil && g_now >= hideNext)
    {
        hideNext = g_now + 0.25;   /* four times a second, not every frame */
        LARGE_INTEGER a, b, f;
        QueryPerformanceCounter(&a);
        Tally t = Pass(0, g_forget ? 2 : 1);
        QueryPerformanceCounter(&b); QueryPerformanceFrequency(&f);
        double ms = (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart;
        if (ms > 20) { g_hideUntil = 0; Log("hiding stopped: a pass took %.0f ms (the game would stall)", ms); }
        g_hideSum.hidden += t.hidden;
        g_hideSum.kicked += t.kicked;
        g_hideSum.squads += t.squads;
        g_hideSum.sensed += t.sensed;
        g_hideSum.cleared += t.cleared;
    }
    if (g_now < g_watchUntil && g_now >= g_watchNext)
    {
        g_watchNext = g_now + 1;
        Tally t = Pass(0, 0);
        char what[64];
        snprintf(what, sizeof what, "%s t+%.0f s", g_now < g_hideUntil ? "hiding" : "watch", 15 - (g_watchUntil - g_now));
        t.hidden = g_hideSum.hidden; t.kicked = g_hideSum.kicked; t.squads = g_hideSum.squads; t.sensed = g_hideSum.sensed; t.cleared = g_hideSum.cleared;
        g_hideSum.hidden = g_hideSum.kicked = g_hideSum.squads = g_hideSum.sensed = g_hideSum.cleared = 0;
        Report(what, t);
    }
    if (g_now < g_traceUntil && g_now >= g_traceNext) { g_traceNext = g_now + 2; Trace(0); }
    static int prev;
    static double keyAfter;
    int raw = Down(VK_F7), d = raw && !prev && g_now >= keyAfter;   /* 0.2.4: at most one press a second */
    if (d) keyAfter = g_now + 1;
    prev = raw;
    if (d && Down(VK_MENU) && Down(VK_CONTROL) && Down(VK_SHIFT))
    {
        g_forget = 1; g_hard = 1;
        Log("Ctrl+Shift+Alt+F7: hard forget (0.2.6, by hand) for 10 s (and watching for 15 s)");
        g_hideUntil = g_now + 10; g_watchUntil = g_now + 15; g_watchNext = g_now;
    }
    else if (d && Down(VK_MENU) && Down(VK_CONTROL))
    {
        g_auto = !g_auto;
        memset(&g_autoSum, 0, sizeof g_autoSum);
        for (int j = 0; j < g_nlive; j++) g_liveUnseen[j] = 0;
        Log(g_auto ? "Ctrl+Alt+F7: auto break-contact ON: an enemy after her that has not sensed her for %.0f s loses her and searches where it last saw her"
                   : "Ctrl+Alt+F7: auto break-contact OFF", s_lose);
    }
    else if (d && Down(VK_MENU))
    {
        if (Down(VK_SHIFT)) { Log("Shift+Alt+F7: sending the patrols near you away, again every 2 s for 30 s"); g_nsent = 0; g_sendUntil = g_now + 30; Trace(1); }
        else Log("Alt+F7: tracing the patrols' navigation records for 30 s (move around: the destination follows you)");
        g_traceUntil = g_now + 30; g_traceNext = g_now + 2;
    }
    else if (d)
    {
        if (Down(VK_CONTROL))
        {
            g_forget = !Down(VK_SHIFT); g_hard = 0;
            Log(g_forget ? "Ctrl+F7: forget: enemies that cannot sense her lose her entries, for 10 s (and watching for 15 s)"
                         : "Ctrl+Shift+F7: hiding her (0.2.2's blanking) for 10 s (and watching for 15 s)");
            g_hideUntil = g_now + 10; g_watchUntil = g_now + 15; g_watchNext = g_now;
        }
        else if (Down(VK_SHIFT)) { Log("Shift+F7: enemies within 60 m:"); Report("survey", Pass(1, 0)); }
        else { Log("F7: watching for 15 s"); g_watchUntil = g_now + 15; g_watchNext = g_now; }
    }
}
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 15 || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    Log("AI Probe " VERSION ": research only; Shift+F7 survey the enemies' AI records, F7 watch who is after her, Ctrl+F7 try hiding her from them");
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) && api->on_game_thread(mod, Tick, NULL);
    if (!ok) Log("events or the game thread are unavailable");
    return 0;
}
