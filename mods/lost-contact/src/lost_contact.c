/* Lost Contact (yui.lost-contact): enemies that lose sight of you stop knowing where you are, and patrols walk between
 * their own outposts instead of being sent straight at you. Host or solo; the enemies' AI records, as found with the
 * AI Probe (docs/research/ai-modding.md, 2026-11-03/04 tests).
 *
 * Break contact: an enemy after you that, with its squad, has not sensed you for lose_after seconds loses you: your
 * entries leave its perception lists (and its squad's), its target is cleared, and it is sent to where it last saw
 * you, to search there. Anything that sees you again acquires you as usual. Without it, bots, bugs and Illuminate off
 * sight keep their target and keep coming (bots for 40 s and more in the tests; with it, 0 within 5 to 10 s).
 *
 * Patrols, as they set out (0.4.0): the game's roaming patrol is led by an entity of its own with no body (type
 * f5252be8f520f3d8, Patrol Route Probe 2026-11-08: one every 30 to 40 s, 100 to 120 m from you, its destination set at
 * spawn within 45 m of you; the patrol follows it). When the straight line from it to its destination passes within
 * pass_by m of you, its destination is moved beside you: pass_by m to the side its aim already leaned to, and 40 m past
 * you, so the patrol walks by you instead of onto you (no turning back halfway, as the rerouting below does). The game
 * aims it again later through set_destination (game+0x5B36C0, hooked): each such aim through you is moved the same way.
 * Terrain and the patrol's spread can still bring single enemies closer; enemies that sense you hunt you as usual.
 *
 * Patrols: enemies standing at ease (no target, their destination on themselves) mark the map's outposts. A walking
 * enemy with no target whose destination is within seek_radius of you is a patrol sent at you: it is given an outpost
 * at least keep_away from you instead, then the next, again every resend seconds (the game aims them again). Once it
 * has a target or senses you, it is left alone. With no outpost known yet it is sent away from you.
 *
 * Each runs on, observe (counts only, touches nothing) or off: the ini, or the keys (Ctrl+F9 / Ctrl+F10) in a
 * mission, or the MODS tab (from C, or from Lua: hud-src/lost_contact_hud.lua, rows only). Both on by default; the
 * debug HUD is not shipped (its source stays in git). LC_BREAK_CONTACT=0 builds it without break contact.
 *
 * Character kits: a kit that exports lost_contact_rules(1) (its answer: -1 when it is not the active kit, else a mask,
 * bit 0 break contact, bit 1 patrols) limits what runs while it acts. Lemuen allows patrols only (Yui: a sniper's
 * stealth is in not being walked into; enemies that saw her keep hunting her). Checked every 2 s.
 *
 * Layouts (build 25480438): behaviours [game+0x3326740]: map +0x40, count +0x20, records +0x60, 0x1F8 each; +0x18 target,
 * +0x68 the perceiver's key, +0x78 has-target, +0x98 next re-pick. Perception [game+0x3326548]: map +0x30, records
 * +0x50, 0x13F8 each; lists at +0x310 / +0x818 / +0xD20 (count, entries 0x50 each: +0 entity, +0x48 in use, +0x4C sense
 * bits), singles at +0x1228 / +0x1278 / +0x12C8. Navigation [game+0x3326418]: map +0x30, records +0x50, 0xA8 each;
 * +0x10 active, +0x54 its position, +0x60 its destination. set_destination is game+0x5B36C0(NULL, entity, &pos).
 * Credit: the behaviour and perception layouts are HD2Runtime's research (SkyeShade), HellForge's turret code and
 * Smarter Guard Dogs' (chef, ported by giblett); nothing of their code is copied. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include <windows.h>
#include "hd2_mod_options.h"
#include "hellforge_mod.h"

#define BUILD "25480438"
#define VERSION "0.4.0"
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
#define SEEN 2048
static uint32_t g_seenEnt[SEEN];
static uint64_t g_seenRes[SEEN];
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
/* ---- settings ------------------------------------------------------------------------------------------------------- */
#ifndef LC_BREAK_CONTACT
#define LC_BREAK_CONTACT 1
#endif
static int s_breakMode = LC_BREAK_CONTACT ? 1 : 0, s_patrolMode = 1;   /* 0 off, 1 on, 2 observe */
static int s_kitRules = 1;   /* 1: a character kit may turn one off while it acts (Lemuen: patrols only) */
static float s_loseAfter = 3, s_breakRange = 80;
static float s_seekRadius = 50, s_keepAway = 100, s_patrolRange = 300, s_resend = 3;
static float s_passBy = 45;   /* 0.4.0: metres a patrol's leader's straight line keeps from you (0: off) */
typedef struct { int vk, ctrl, shift, alt; char label[24]; } Key;
static Key s_keyBreak = {VK_F9, 1, 0, 0, "Ctrl+F9"}, s_keyPatrol = {VK_F10, 1, 0, 0, "Ctrl+F10"};
static int ParseKey(const char* v, Key* k)
{
    Key n = {0};
    const char* p = v;
    for (;;)
    {
        if (!_strnicmp(p, "Ctrl+", 5)) { n.ctrl = 1; p += 5; }
        else if (!_strnicmp(p, "Shift+", 6)) { n.shift = 1; p += 6; }
        else if (!_strnicmp(p, "Alt+", 4)) { n.alt = 1; p += 4; }
        else break;
    }
    if ((p[0] == 'F' || p[0] == 'f') && p[1] >= '1' && p[1] <= '9')
    {
        int f = atoi(p + 1);
        if (f < 1 || f > 24) return 0;
        n.vk = VK_F1 + f - 1;
    }
    else if (p[0] && !p[1] && ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= '0' && p[0] <= '9'))) n.vk = p[0];
    else if (p[0] >= 'a' && p[0] <= 'z' && !p[1]) n.vk = p[0] - 32;
    else return 0;
    snprintf(n.label, sizeof n.label, "%s", v);
    *k = n;
    return 1;
}
/* 0.3.0: the numbers the MODS tab (Mod Options Menu, from Lua) changed: bit per number, kept in the saved choices */
enum { SV_LOSE = 1, SV_RANGE = 2, SV_SEEK = 4, SV_KEEP = 8, SV_PRANGE = 16, SV_RESEND = 32, SV_PASS = 64 };
static unsigned g_savedNums;
static int ReadSettings(const char* path, int saved)
{
    char line[256];
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f))
    {
        char* c = line;
        while (*c == ' ' || *c == '\t') c++;
        if (*c == ';' || *c == '#' || !*c) continue;
        char* eq = strchr(c, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = c, *v = eq + 1;
        for (char* e = eq - 1; e >= k && (*e == ' ' || *e == '\t'); e--) *e = 0;
        while (*v == ' ' || *v == '\t') v++;
        for (char* e = v + strlen(v) - 1; e >= v && (*e == '\r' || *e == '\n' || *e == ' ' || *e == '\t'); e--) *e = 0;
        double x = atof(v);
        if (saved)
            g_savedNums |= !strcmp(k, "lose_after") ? SV_LOSE : !strcmp(k, "break_range") ? SV_RANGE : !strcmp(k, "seek_radius") ? SV_SEEK :
                           !strcmp(k, "keep_away") ? SV_KEEP : !strcmp(k, "patrol_range") ? SV_PRANGE : !strcmp(k, "resend") ? SV_RESEND :
                           !strcmp(k, "pass_by") ? SV_PASS : 0u;
        if (LC_BREAK_CONTACT && !strcmp(k, "break_mode")) s_breakMode = (int)x >= 0 && (int)x <= 2 ? (int)x : s_breakMode;
        else if (!strcmp(k, "lose_after")) s_loseAfter = x >= 0.5 && x <= 60 ? (float)x : s_loseAfter;
        else if (!strcmp(k, "break_range")) s_breakRange = x >= 10 && x <= 500 ? (float)x : s_breakRange;
        else if (!strcmp(k, "patrol_mode")) s_patrolMode = (int)x >= 0 && (int)x <= 2 ? (int)x : s_patrolMode;
        else if (!strcmp(k, "seek_radius")) s_seekRadius = x >= 10 && x <= 300 ? (float)x : s_seekRadius;
        else if (!strcmp(k, "keep_away")) s_keepAway = x >= 20 && x <= 600 ? (float)x : s_keepAway;
        else if (!strcmp(k, "patrol_range")) s_patrolRange = x >= 50 && x <= 1000 ? (float)x : s_patrolRange;
        else if (!strcmp(k, "kit_rules")) s_kitRules = (int)x ? 1 : 0;
        else if (!strcmp(k, "resend")) s_resend = x >= 0.5 && x <= 30 ? (float)x : s_resend;
        else if (!strcmp(k, "pass_by")) s_passBy = x >= 0 && x <= 150 ? (x < 20 ? 0 : (float)x) : s_passBy;   /* under 20: off */
        else if (!strcmp(k, "key_break")) { if (!ParseKey(v, &s_keyBreak)) Log("settings: key_break \"%s\" is not a key", v); }
        else if (!strcmp(k, "key_patrol")) { if (!ParseKey(v, &s_keyPatrol)) Log("settings: key_patrol \"%s\" is not a key", v); }
    }
    fclose(f);
    return 1;
}
/* What the MODS tab changed: HellForge keeps mod folders read-only while the game runs, so the menu's choices live in
 * %LOCALAPPDATA%\\HellForgeMods\\lost-contact.ini (the game's own local folder), read after the mod's ini */
static char g_savePath[MAX_PATH * 2];
static void LoadSettings(void)
{
    char path[MAX_PATH * 2], local[MAX_PATH];
    snprintf(path, sizeof path, "%s\\lost-contact.ini", hf->mod_folder(self));
    if (!ReadSettings(path, 0)) Log("settings: no lost-contact.ini, the defaults");
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", local, sizeof local);
    if (n && n < sizeof local)
    {
        snprintf(g_savePath, sizeof g_savePath, "%s\\HellForgeMods\\lost-contact.ini", local);
        if (ReadSettings(g_savePath, 1)) Log("settings: the MODS tab's choices from %s", g_savePath);
    }
}
static void SaveChoices(void)
{
    if (!g_savePath[0]) return;
    char dir[MAX_PATH * 2];
    snprintf(dir, sizeof dir, "%s", g_savePath);
    char* cut = strrchr(dir, '\\');
    if (cut) { *cut = 0; CreateDirectoryA(dir, NULL); }
    FILE* f = fopen(g_savePath, "w");
    if (!f) { Log("settings: the MODS tab's choices could not be saved to %s", g_savePath); return; }
    fprintf(f, "; Lost Contact: the MODS tab's choices (read after the mod's lost-contact.ini). Delete this file to go back to the ini.\n"
               "break_mode = %d\npatrol_mode = %d\nkit_rules = %d\n", s_breakMode, s_patrolMode, s_kitRules);
    /* 0.3.0: a number only once the tab changed it (else the mod's lost-contact.ini keeps it) */
    if (g_savedNums & SV_LOSE) fprintf(f, "lose_after = %g\n", (double)s_loseAfter);
    if (g_savedNums & SV_RANGE) fprintf(f, "break_range = %g\n", (double)s_breakRange);
    if (g_savedNums & SV_SEEK) fprintf(f, "seek_radius = %g\n", (double)s_seekRadius);
    if (g_savedNums & SV_KEEP) fprintf(f, "keep_away = %g\n", (double)s_keepAway);
    if (g_savedNums & SV_PRANGE) fprintf(f, "patrol_range = %g\n", (double)s_patrolRange);
    if (g_savedNums & SV_RESEND) fprintf(f, "resend = %g\n", (double)s_resend);
    if (g_savedNums & SV_PASS) fprintf(f, "pass_by = %g\n", (double)s_passBy);
    fclose(f);
}
/* ---- what the active character kit allows ---------------------------------------------------------------------- */
typedef int (*RulesFn)(unsigned version);
static const char* const kKits[] = {"lemuen.dll", "voymastina.dll", "exusiai.dll", "lenna.dll", "leva.dll", "faelynn.dll"};
static int g_rules = 3;          /* bit 0 break contact, bit 1 patrols */
static const char* g_rulesBy;
static int g_kitAsk = 3;
static const char* g_kitBy;
static void KitRules(void)
{
    int rules = 3;
    const char* by = NULL;
    for (unsigned i = 0; i < sizeof kKits / sizeof kKits[0]; i++)
    {
        HMODULE m = GetModuleHandleA(kKits[i]);
        RulesFn f = m ? (RulesFn)(void*)GetProcAddress(m, "lost_contact_rules") : NULL;
        int r = f ? f(1) : -1;
        if (r >= 0) { rules &= r; by = kKits[i]; }
    }
    g_kitAsk = rules; g_kitBy = by;
    if (!s_kitRules) { rules = 3; }
    if (rules != g_rules || by != g_rulesBy)
        Log("rules: break contact %s, patrols %s (%s)", rules & 1 ? "allowed" : "OFF", rules & 2 ? "allowed" : "OFF",
            !by ? "no kit limits them" : s_kitRules ? by : "a kit asked, kit_rules 0 keeps them");
    g_rules = rules; g_rulesBy = by;
}
static const char* ModeName(int m) { return m == 1 ? "on" : m == 2 ? "observe" : "off"; }

/* ---- the live enemies: kept as they spawn (types classified a few a pass), dropped once gone ------------------------- */
#define LIVE 512
typedef struct
{
    uint32_t id;
    uint64_t res;
    double unseen, forgot;   /* break contact: when it stopped sensing you; when it last lost you */
    float last[3];           /* where you were when it last sensed you */
    uint8_t hasLast, routed;
    int16_t to;              /* patrols: the outpost it is sent to (-1: away from you) */
    double sentAt;
    float away[3];           /* patrols: the point away from you, when no outpost is known */
    /* 0.1.1: what became of it (despawn research): a patrol is one that was ever aimed at you */
    uint8_t patrol;
    float lastPos[3], lastDist;
    double lastSeen;
} Foe;
/* 0.1.1: entities the game reported killed (HF_ON_KILL), so a foe that is gone can be told killed from despawned */
#define KILLS 256
static uint32_t g_kills[KILLS];
static volatile unsigned g_nkills;
static int WasKilled(uint32_t id)
{
    unsigned n = g_nkills < KILLS ? g_nkills : KILLS;
    for (unsigned i = 0; i < n; i++) if (g_kills[i] == id) return 1;
    return 0;
}
static struct { int patrolDespawned, routedDespawned, patrolKilled, otherDespawned; } g_gone;   /* this mission */
static Foe g_foes[LIVE];
static int g_nfoes;
static unsigned g_cursor;
static void Refresh(void)
{
    if (g_classified > 0) g_classified = 0;
    unsigned end = g_nseen;
    if (end - g_cursor > SEEN) g_cursor = end - SEEN;
    while (g_cursor != end)
    {
        unsigned i = g_cursor % SEEN;
        int k = KindOf(g_seenRes[i]);
        if (!k) break;
        g_cursor++;
        if (k == 1 && g_nfoes < LIVE)
        {
            Foe* f = &g_foes[g_nfoes++];
            memset(f, 0, sizeof *f);
            f->id = g_seenEnt[i]; f->res = g_seenRes[i]; f->to = -1;
        }
    }
}
/* the enemy at j and where it stands; a gone one is dropped (the last moved into its place: walk j down) */
static void Gone(const Foe* f)
{
    if (WasKilled(f->id)) { if (f->patrol) g_gone.patrolKilled++; return; }
    if (!f->patrol) { g_gone.otherDespawned++; return; }
    g_gone.patrolDespawned++;
    if (f->routed) g_gone.routedDespawned++;
    static unsigned told;
    if (told++ < 30)
        hf->log(self, "despawned: patrol %u (type %016llx), last %.0f m from you %.1f s ago, %s%s", f->id, (unsigned long long)f->res,
                (double)f->lastDist, g_now - f->lastSeen,
                f->routed ? (s_patrolMode == 1 ? "routed" : "would be routed (observe)") : "not routed (it had a target, or it walked elsewhere)", "");
}
static int FoeAt(int j, Ent* e, float p[3])
{
    if (!Resolve(g_foes[j].id, e) || e->resource != g_foes[j].res) { Gone(&g_foes[j]); g_foes[j] = g_foes[--g_nfoes]; return 0; }
    if (!RootOf(e->unit, p)) return 0;
    memcpy(g_foes[j].lastPos, p, 12);
    g_foes[j].lastSeen = g_now;
    return 1;
}
static float Dist2D(const float* a, const float* b) { return sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1])); }
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
typedef void (*SetDestFn)(void*, uint32_t, const float*);
static SetDestFn g_setDest;
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
/* ---- binding, events, the game thread --------------------------------------------------------------------------------- */
/* ---- you ------------------------------------------------------------------------------------------------------------ */
static int Me(uint32_t* id, float p[3])
{
    Ent me;
    *id = atomic_load(&g_avatar);
    return *id && Resolve(*id, &me) && RootOf(me.unit, p);
}
/* does it (or its squad, whose record it perceives through) sense you right now? */
static int Senses(uint32_t id, uint32_t perceiver, uint32_t me)
{
    uint32_t sense = 0, sqSense = 0;
    int counts[3];
    uintptr_t pr = Perception(id), sq = perceiver && perceiver != id ? Perception(perceiver) : 0;
    int mine = pr ? HerEntries(pr, me, &sense, counts) : 0, sqMine = sq ? HerEntries(sq, me, &sqSense, counts) : 0;
    return (mine && sense) || (sqMine && sqSense);
}

/* ---- break contact ---------------------------------------------------------------------------------------------- */
static struct { int nearby, chasing, sensing, unsensed, lost, blind; } g_bc;   /* this pass; lost and blind: since set */
static struct { double nearby, chasing, sensing, unsensed; int passes, lost, blind; } g_bcSum;
static void BreakPass(void)
{
    uint32_t me;
    float mp[3], p[3];
    if (!Me(&me, mp)) return;
    Refresh();
    int nNear = 0, nChase = 0, nSense = 0, nUnsensed = 0;
    for (int j = g_nfoes - 1; j >= 0; j--)
    {
        Ent e;
        Foe* f = &g_foes[j];
        if (!FoeAt(j, &e, p)) continue;
        f = &g_foes[j];
        if (Dist2D(p, mp) > s_breakRange) { f->unseen = 0; continue; }
        nNear++;
        uintptr_t br = Behaviour(f->id);
        uint8_t b[0xA0];
        if (!br || !RD(br, b, sizeof b)) continue;
        uint32_t target = U32(b + 0x18), perceiver = U32(b + 0x68);
        if (Senses(f->id, perceiver, me))
        {
            memcpy(f->last, mp, sizeof mp); f->hasLast = 1;
            f->unseen = 0;
            if (target == me) { nChase++; nSense++; }
            continue;
        }
        if (target != me) { f->unseen = 0; continue; }
        nChase++; nUnsensed++;
        if (f->forgot > 0 && g_now - f->forgot < 15)
        {
            /* after you again within 15 s of losing you, without sensing you (bugs did this: Stalkers?) */
            static unsigned told;
            uint64_t pres = 0;
            Ent pe;
            if (perceiver && perceiver != f->id && Resolve(perceiver, &pe)) pres = pe.resource;
            if (told++ < 40)
                Log("break contact: entity %u (type %016llx) %.0f m is after you again %.1f s after losing you, unsensed; perceives through %s %u (type %016llx)",
                    f->id, (unsigned long long)f->res, Dist2D(p, mp), g_now - f->forgot, perceiver == f->id ? "itself" : "its squad", perceiver,
                    (unsigned long long)pres);
            g_bc.blind++;
            f->forgot = 0;
        }
        if (!f->unseen) { f->unseen = g_now; continue; }
        if (g_now - f->unseen < s_loseAfter) continue;
        g_bc.lost++;
        f->unseen = 0;
        if (s_breakMode == 2) { f->unseen = g_now; continue; }   /* observe: counted as "would lose you" */
        uintptr_t pr = Perception(f->id), sq = perceiver && perceiver != f->id ? Perception(perceiver) : 0;
        if (pr) ForgetIn(pr, me);
        if (sq) ForgetIn(sq, me);
        static const uint32_t z = 0;
        static const uint64_t z8 = 0;
        hf->write(br + 0x18, &z, 4); hf->write(br + 0x78, &z, 4); hf->write(br + 0x98, &z8, 8);
        if (g_setDest)
        {
            const float* to = f->hasLast ? f->last : p;
            _Alignas(16) float at[4] = {to[0], to[1], to[2], 0};
            g_setDest(NULL, f->id, at);
        }
        f->forgot = g_now;
    }
    g_bc.nearby = nNear; g_bc.chasing = nChase; g_bc.sensing = nSense; g_bc.unsensed = nUnsensed;
    g_bcSum.nearby += nNear; g_bcSum.chasing += nChase; g_bcSum.sensing += nSense; g_bcSum.unsensed += nUnsensed; g_bcSum.passes++;
}

/* ---- outposts: where enemies stand at ease ---------------------------------------------------------------------- */
#define POSTS 48
typedef struct { float at[3]; int seen, best; double last; } Post;   /* best: the most standing there at once */
static Post g_posts[POSTS];
static int g_nposts;
static int PostReal(const Post* o) { return o->best >= 2; }
static int g_realPosts;
static void NotePost(const float p[3], int* here)
{
    int k;
    for (k = 0; k < g_nposts; k++) if (Dist2D(g_posts[k].at, p) < 35) break;
    if (k == g_nposts)
    {
        if (g_nposts == POSTS) return;
        memset(&g_posts[k], 0, sizeof g_posts[k]);
        memcpy(g_posts[k].at, p, 12);
        g_nposts++;
    }
    Post* o = &g_posts[k];
    o->seen++;
    for (int i = 0; i < 3; i++) o->at[i] += (p[i] - o->at[i]) / (float)(o->seen < 20 ? o->seen : 20);
    o->last = g_now;
    here[k]++;
}
/* the nearest real outpost to p at least keep_away from you, not `not`; -1 if none */
static int PostFor(const float p[3], const float mp[3], int not)
{
    int best = -1;
    float bd = 1e30f;
    for (int k = 0; k < g_nposts; k++)
    {
        if (k == not || !PostReal(&g_posts[k]) || Dist2D(g_posts[k].at, mp) < s_keepAway) continue;
        float d = Dist2D(g_posts[k].at, p);
        if (d < bd) { bd = d; best = k; }
    }
    return best;
}

/* ---- patrols ---------------------------------------------------------------------------------------------------- */
static struct { int seeking, routed, reaimed, sent; } g_pt;   /* seeking, routed: this pass; reaimed, sent: since set */
static struct { double seeking, routed; int passes, reaimed, sent; } g_ptSum;
static void Send(Foe* f, const float to[3])
{
    if (!g_setDest) return;
    /* a little apart per enemy, so a squad does not stand in one spot */
    float a = (float)(f->id % 16) * 0.3927f;
    _Alignas(16) float at[4] = {to[0] + 4 * cosf(a), to[1] + 4 * sinf(a), to[2], 0};
    g_setDest(NULL, f->id, at);
    f->sentAt = g_now;
    g_pt.sent++;
}
static void PatrolPass(void)
{
    uint32_t me;
    float mp[3], p[3];
    if (!Me(&me, mp)) return;
    Refresh();
    int here[POSTS] = {0}, nSeek = 0, nRouted = 0;
    for (int j = g_nfoes - 1; j >= 0; j--)
    {
        Ent e;
        if (!FoeAt(j, &e, p)) continue;
        Foe* f = &g_foes[j];
        f->lastDist = Dist2D(p, mp);
        if (f->lastDist > s_patrolRange) { f->routed = 0; continue; }
        uintptr_t br = Behaviour(f->id);
        uint32_t target = 0, perceiver = 0, k;
        if (br) { RD(br + 0x18, &target, 4); RD(br + 0x68, &perceiver, 4); }
        uintptr_t rec = NavIndex(f->id, &k) ? NavRecord(k) : 0;
        float nav[3], dest[3];
        uint32_t active = 0;
        if (!rec || !RD(rec + 0x10, &active, 4) || !RD(rec + 0x54, nav, 12) || !RD(rec + 0x60, dest, 12) || !isfinite(dest[0])) continue;
        if (target)
        {
            f->routed = 0;   /* in a fight (with you or anyone): left alone */
            continue;
        }
        float walk = Dist2D(dest, p);
        if (walk < 4) { NotePost(p, here); f->routed = 0; continue; }   /* standing at ease: an outpost */
        if (Senses(f->id, perceiver, me)) { f->routed = 0; continue; }
        /* searching where it lost you (break contact sent it there): left to search for 20 s */
        if (f->forgot > 0 && g_now - f->forgot < 20) continue;
        int seeking = Dist2D(dest, mp) < s_seekRadius;
        if (seeking) { nSeek++; f->patrol = 1; }
        if (f->routed)
        {
            nRouted++;
            if (seeking && g_now - f->sentAt > 0.5) g_pt.reaimed++;   /* the game aimed it at you again */
            /* at its outpost: on to the next */
            if (f->to >= 0 && f->to < g_nposts && Dist2D(p, g_posts[f->to].at) < 15)
            {
                int next = PostFor(g_posts[f->to].at, mp, f->to);
                if (next >= 0) f->to = (int16_t)next;
            }
            if (s_patrolMode == 1 && (seeking || g_now - f->sentAt >= s_resend))
                Send(f, f->to >= 0 && f->to < g_nposts ? g_posts[f->to].at : f->away);
            continue;
        }
        if (!seeking) continue;
        /* a patrol sent at you */
        f->routed = 1;
        nRouted++;
        f->to = (int16_t)PostFor(p, mp, -1);
        if (f->to < 0)
        {
            /* no outpost known: away from you, keep_away past where it is */
            float dx = p[0] - mp[0], dy = p[1] - mp[1], h = sqrtf(dx * dx + dy * dy);
            if (h < 1) { dx = 1; dy = 0; h = 1; }
            f->away[0] = mp[0] + dx / h * (h + s_keepAway); f->away[1] = mp[1] + dy / h * (h + s_keepAway); f->away[2] = p[2];
        }
        static unsigned told;
        if (told++ < 30)
            Log("patrol: entity %u (type %016llx) %.0f m away, walking to %.0f m from you: %s", f->id, (unsigned long long)f->res, Dist2D(p, mp),
                Dist2D(dest, mp), s_patrolMode == 1 ? (f->to >= 0 ? "sent to an outpost" : "sent away (no outpost known)") : "observed");
        if (s_patrolMode == 1) Send(f, f->to >= 0 ? g_posts[f->to].at : f->away);
    }
    for (int k = 0; k < g_nposts; k++) if (here[k] > g_posts[k].best) g_posts[k].best = here[k];
    g_realPosts = 0;
    for (int k = 0; k < g_nposts; k++) g_realPosts += PostReal(&g_posts[k]);
    g_pt.seeking = nSeek; g_pt.routed = nRouted;
    g_ptSum.seeking += nSeek; g_ptSum.routed += nRouted; g_ptSum.passes++;
}
static void HookDest(void);
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
    HookDest();
    Log("patrols: navigation records [game+0x3326418] = %llx, set_destination %s", (unsigned long long)P(game + 0x3326418), g_setDest ? "on" : "off (not a function start)");
    return bm && pm;
}
/* ---- 0.4.0: patrols as they set out (their leader) ------------------------------------------------------------ */
#define PATROL_LEADER 0xF5252BE8F520F3D8ull   /* the roaming patrol's bodiless leader (Patrol Route Probe, 2026-11-08) */
#define LEADERS 64
typedef struct { uint32_t entity; double at; int checked, moved; float to[3]; double checkAt; } Leader;
static Leader g_lead[LEADERS];
static atomic_uint g_leadNext;
static float g_mePos[3];
static atomic_int g_meOk;
static struct { int seen, through, moved, again, kept; } g_side;   /* this mission */
static Leader* LeaderOf(uint32_t e)
{
    for (int i = 0; i < LEADERS; i++) if (g_lead[i].entity == e) return &g_lead[i];
    return NULL;
}
static float SegDist2D(const float a[3], const float b[3], const float p[3])
{
    float vx = b[0] - a[0], vy = b[1] - a[1], wx = p[0] - a[0], wy = p[1] - a[1], vv = vx * vx + vy * vy;
    float t = vv > 1e-6f ? (wx * vx + wy * vy) / vv : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float dx = a[0] + vx * t - p[0], dy = a[1] + vy * t - p[1];
    return sqrtf(dx * dx + dy * dy);
}
/* from (the leader) aimed at dest: when that line passes within pass_by of you, a destination beside you (to the side
 * the aim leans to, 40 m past you) whose line keeps pass_by from you; 0 when the aim does not come through you */
static int SidePass(uint32_t id, const float from[3], const float dest[3], const float me[3], float out[3])
{
    if (s_passBy <= 0 || SegDist2D(from, dest, me) >= s_passBy) return 0;
    float ux = me[0] - from[0], uy = me[1] - from[1], h = sqrtf(ux * ux + uy * uy);
    if (h < 1) { ux = 1; uy = 0; h = 1; }
    ux /= h; uy /= h;
    float nx = -uy, ny = ux, side = (dest[0] - me[0]) * nx + (dest[1] - me[1]) * ny;
    float sgn = side > 0.5f ? 1.f : side < -0.5f ? -1.f : (id & 1) ? 1.f : -1.f;
    for (int t = 0; t < 8; t++)
    {
        float s = (t & 1) ? -sgn : sgn, r = s_passBy * (1.f + 0.3f * (float)(t / 2));
        float c[3] = {me[0] + nx * s * r + ux * 40, me[1] + ny * s * r + uy * 40, dest[2]};
        if (SegDist2D(from, c, me) >= s_passBy) { memcpy(out, c, 12); return 1; }
    }
    return 0;
}
/* set_destination, hooked: the game aiming a patrol's leader through you again */
typedef void (*SetDestFn2)(void*, uint32_t, const float*);
static SetDestFn2 g_destOrig;
static void DestDetour(void* a, uint32_t entity, const float* pos)
{
    Leader* l = pos && s_patrolMode && s_passBy > 0 && (g_rules & 2) && atomic_load(&g_meOk) ? LeaderOf(entity) : NULL;
    if (l)
    {
        uint32_t k;
        uintptr_t rec = NavIndex(entity, &k) ? NavRecord(k) : 0;
        float from[3], me[3], to[3];
        memcpy(me, g_mePos, 12);
        if (rec && RD(rec + 0x54, from, 12) && SidePass(entity, from, pos, me, to))
        {
            g_side.again++;
            static unsigned told;
            if (told++ < 20)
                Log("patrol leader %u aimed through you again (%.0f m from you): %s %.0f m beside you", entity, Dist2D(pos, me),
                    s_patrolMode == 1 ? "moved" : "would move (observe)", SegDist2D(from, to, me));
            if (s_patrolMode == 1)
            {
                _Alignas(16) float at[4] = {to[0], to[1], to[2], 0};
                memcpy(l->to, to, 12); l->moved = 1;
                g_destOrig(a, entity, at);
                return;
            }
        }
    }
    g_destOrig(a, entity, pos);
}
static void HookDest(void)
{
    static int done;
    if (done || !g_setDest) return;
    done = 1;
    if (!hf->code_matches(game + 0x5B36C0, "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57 48 83 ec 50"))
    { Log("patrols as they set out: off (set_destination is not the reviewed code)"); return; }
    if (hf->hook_ex(self, game + 0x5B36C0, (void*)DestDetour, (void**)&g_destOrig, 0) || !g_destOrig)
    { g_destOrig = NULL; Log("patrols as they set out: set_destination could not be hooked (leaders are still moved as they set out)"); g_destOrig = (SetDestFn2)g_setDest; return; }
    g_setDest = (SetDestFn)g_destOrig;   /* its own sends go past the hook */
    Log("patrols as they set out: set_destination hooked (pass_by %g m)", (double)s_passBy);
}
/* each frame: leaders that spawned, once their destination reads; 15 s later, where they went */
static void LeaderFrame(void)
{
    uint32_t meId;
    float me[3];
    if (!Me(&meId, me)) { atomic_store(&g_meOk, 0); return; }
    memcpy(g_mePos, me, 12);
    atomic_store(&g_meOk, 1);
    for (int i = 0; i < LEADERS; i++)
    {
        Leader* l = &g_lead[i];
        if (!l->entity) continue;
        uint32_t k;
        uintptr_t rec = NavIndex(l->entity, &k) ? NavRecord(k) : 0;
        float from[3], dest[3], to[3];
        if (!rec || !RD(rec + 0x54, from, 12) || !RD(rec + 0x60, dest, 12) || !isfinite(dest[0]))
        {
            if (g_now - l->at > 5) l->entity = 0;   /* gone, or never had a navigation record */
            continue;
        }
        if (!l->checked)
        {
            if (!dest[0] && !dest[1] && !dest[2] && g_now - l->at < 2) continue;   /* not aimed yet */
            l->checked = 1;
            g_side.seen++;
            if (!s_patrolMode || !(g_rules & 2) || s_passBy <= 0) continue;
            if (!SidePass(l->entity, from, dest, me, to)) { g_side.kept++; continue; }
            g_side.through++;
            static unsigned told;
            if (told++ < 30)
                Log("patrol leader %u set out %.0f m from you, aimed %.0f m from you (its line %.0f m from you): %s %.0f m beside you", l->entity,
                    Dist2D(from, me), Dist2D(dest, me), SegDist2D(from, dest, me), s_patrolMode == 1 ? "moved" : "would move (observe)", SegDist2D(from, to, me));
            if (s_patrolMode == 1 && g_destOrig)
            {
                _Alignas(16) float at[4] = {to[0], to[1], to[2], 0};
                g_destOrig(NULL, l->entity, at);
                memcpy(l->to, to, 12); l->moved = 1;
                g_side.moved++;
                l->checkAt = g_now + 15;
            }
            continue;
        }
        if (l->moved && l->checkAt > 0 && g_now >= l->checkAt)
        {
            static unsigned told;
            l->checkAt = 0;
            if (told++ < 12)
                Log("patrol leader %u 15 s later: %.0f m from you, %.0f m from where it was sent (destination now %.0f m from that)", l->entity,
                    Dist2D(from, me), Dist2D(from, l->to), Dist2D(dest, l->to));
        }
        if (g_now - l->at > 300) l->entity = 0;
    }
}
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* e = (const HfSpawnEvent*)info;
    unsigned i = g_nseen % SEEN;
    g_seenEnt[i] = e->entity; g_seenRes[i] = e->resource; g_nseen++;
    if (e->resource == PATROL_LEADER)
    {
        unsigned k = atomic_fetch_add(&g_leadNext, 1) % LEADERS;
        g_lead[k].entity = 0;
        g_lead[k].at = g_now; g_lead[k].checked = 0; g_lead[k].moved = 0; g_lead[k].checkAt = 0;
        g_lead[k].entity = e->entity;
    }
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
static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    (void)mod; (void)user;
    const HfKillEvent* k = (const HfKillEvent*)info;
    unsigned i = g_nkills;
    g_kills[i % KILLS] = k->victim;
    g_nkills = i + 1;
}
static int Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
/* ---- the game thread ---------------------------------------------------------------------------------------------- */
static int KeyPressed(const Key* k)
{
    return Down(k->vk) && Down(VK_CONTROL) == k->ctrl && Down(VK_SHIFT) == k->shift && Down(VK_MENU) == k->alt;
}
static int g_inMission;
static void MissionStart(void)
{
    g_nfoes = 0;
    memset(&g_side, 0, sizeof g_side);
    g_nposts = g_realPosts = 0;
    memset(&g_bc, 0, sizeof g_bc); memset(&g_bcSum, 0, sizeof g_bcSum);
    memset(&g_pt, 0, sizeof g_pt); memset(&g_ptSum, 0, sizeof g_ptSum);
    memset(&g_gone, 0, sizeof g_gone);
}

/* ---- the MODS tab (a menu that publishes hd2_mod_options.h, HD2_MOD_OPTIONS_v1: giblett's Vanilla Plus Megapack
 * build): the modes, whether a kit may limit them, and what runs now and why, so a player knows what is off and where
 * to turn it back on. 0.2.1: the public Vanilla Plus Megapack (0.2.0) does not publish it; its own menu API
 * (vpm_mom_api, include/vpm_mom.h) has no public header yet, so it is only named in the log, never called. Without a
 * menu the log says so once, after 30 s; rows the menu refuses are named and asked for again a few times. 0.3.0: with
 * the public pack the rows come from Lua instead (lost_contact_menu and below); only one of the two registers. ------ */
#define OPT_MOD "Lost Contact"
#define OPT_ROWS 5
#define OPT_TRIES 4          /* registrations of refused rows, the first included */
#define OPT_RETRY 10.0       /* seconds between them */
static const char* const kModes[] = {"Off", "On", "Observe (log only)"};
static const char* const kStatus[] = {"Running", "Off (your setting)", "Off: the active kit (Character kits can limit)", "Observing"};
static int g_optsDone;       /* 1: every row is on the tab, or the tries ran out */
static const Hd2ModOptionsApi* g_optsApi;   /* the menu the rows went to */
static uint8_t g_rowOk[OPT_ROWS];
static int g_optTries;
static double g_optNextTry, g_optCheck;
#define OPT_READDS 3         /* rows added again for one menu address, at most */
static const Hd2ModOptionsApi* g_checkFor;  /* the menu address the two below are for */
static int g_noCheck;        /* 1: the 10 s check is off for that menu (its get() did not find a row just added, or the cap) */
static int g_readds;         /* rows added again for that menu */
static int g_shownStatus[2] = {-1, -1};
/* 0.3.0: which menu has the rows: 0 none yet, 1 this file's (HD2_MOD_OPTIONS_v1), 2 the Lua Mod Options Menu (the HUD
 * addon, through lost_contact_menu). Only one registers. g_optLooked: Options() has looked for the C menu once, so the
 * Lua side waits for that before it claims the rows. */
static atomic_int g_menuOwner, g_optLooked;
static int ModeIndex(int m) { return m == 1 ? 1 : m == 2 ? 2 : 0; }
static int StatusOf(int mode, int bit)
{
    if (!mode) return 1;
    if (!(g_rules & bit)) return 2;
    return mode == 2 ? 3 : 0;
}
/* hd2_mod_options() with its refusals told: a menu of another process, or one whose version, size or functions this
 * build does not accept, is logged (once per distinct value) instead of being skipped in silence. The table is read
 * through hf->read and its functions checked (each inside a loaded module) only when the published value changes. */
static int g_menuRefused;    /* a menu was published, and refused: the 30 s line says so instead of "no menu" */
static char g_menuChecked[64];                /* the published value last accepted */
static const Hd2ModOptionsApi* g_menuAccepted; /* its table */
static int InModule(const void* fn)
{
    HMODULE m = NULL;
    return fn && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)fn, &m) && m;
}
static const Hd2ModOptionsApi* MenuApi(void)
{
    char text[64] = "";
    DWORD n = GetEnvironmentVariableA(HD2_MOD_OPTIONS_ENV, text, sizeof text);
    if (!n) return NULL;
    if (n >= sizeof text) snprintf(text, sizeof text, "(too long: %lu)", (unsigned long)n);   /* text was left unset */
    static char told[64];
    if (g_menuAccepted && !strcmp(g_menuChecked, text)) return g_menuAccepted;   /* the same value as last time, already checked */
    g_menuAccepted = NULL;
    static double recheck;
    if (!strcmp(told, text) && g_now < recheck) return NULL;   /* refused before: checked again only every 10 s */
    recheck = g_now + OPT_RETRY;
    unsigned long pid = 0;
    unsigned long long at = 0;
    const char* why = NULL;
    Hd2ModOptionsApi copy;
    memset(&copy, 0, sizeof copy);
    if (n >= sizeof text || sscanf(text, "%lu:%llx", &pid, &at) != 2 || !at) why = "not \"<process id>:<address>\"";
    else if (pid != GetCurrentProcessId()) why = "another process's";
    else if (!RD((uintptr_t)at, &copy, 8)) why = "its address cannot be read";
    else if (copy.version < 1) why = "an unknown version";
    else if (copy.size < sizeof(Hd2ModOptionsApi)) why = "a smaller table than this build's";
    else if (!RD((uintptr_t)at, &copy, sizeof copy)) why = "its table cannot be read";
    else if (!InModule((const void*)copy.register_option) || !InModule((const void*)copy.get) || !InModule((const void*)copy.set))
        why = "a function of its table is not in a loaded module";
    if (!why)
    {
        snprintf(g_menuChecked, sizeof g_menuChecked, "%s", text);
        g_menuAccepted = (const Hd2ModOptionsApi*)(uintptr_t)at;
        return g_menuAccepted;
    }
    g_menuRefused = 1;
    if (strcmp(told, text))
    {
        snprintf(told, sizeof told, "%s", text);
        Log("MODS tab: %s = \"%s\" is refused: %s (version %u, size %u; this build needs version 1 or later, size %u or more)",
            HD2_MOD_OPTIONS_ENV, text, why, copy.version, copy.size, (unsigned)sizeof(Hd2ModOptionsApi));
    }
    return NULL;
}
static void OptChanged(void* user, const char* id, int32_t v)
{
    (void)user;
    const Hd2ModOptionsApi* api = g_optsApi;
    if (!strcmp(id, "break")) { s_breakMode = v == 1 ? 1 : v == 2 ? 2 : 0; Log("MODS tab: break contact %s", ModeName(s_breakMode)); }
    else if (!strcmp(id, "patrols")) { s_patrolMode = v == 1 ? 1 : v == 2 ? 2 : 0; Log("MODS tab: patrols %s", ModeName(s_patrolMode)); }
    else if (!strcmp(id, "kits")) { s_kitRules = v ? 1 : 0; Log("MODS tab: character kits %s limit them", s_kitRules ? "may" : "may not"); KitRules(); }
    else if (api) { if (!strcmp(id, "break_now")) api->set(OPT_MOD, id, g_shownStatus[0]); else if (!strcmp(id, "patrols_now")) api->set(OPT_MOD, id, g_shownStatus[1]); return; }   /* the status rows only show */
    else return;
    SaveChoices();
    g_shownStatus[0] = g_shownStatus[1] = -1;
}
/* the public Vanilla Plus Megapack, for the 30 s line: in the session's mods (hf->mods, SDK 10) by its id, and its DLL
 * by its manifest's code.dll name (vanilla_plus_megapack.dll, which HellForge loads it under) */
#define VPM_ID "cowboybingus.vanilla-plus-megapack"
#define VPM_DLL "vanilla_plus_megapack.dll"
static const char* PublicPack(void)
{
    int listed = -1;   /* -1: hf->mods is not there */
    if (hf->size >= offsetof(HfApi, mods) + sizeof hf->mods && hf->mods)
    {
        listed = 0;
        HfModInfo info;
        for (uint32_t i = 0; i < 512; i++)
        {
            memset(&info, 0, sizeof info);
            info.size = sizeof info;
            if (!hf->mods(self, i, &info)) break;
            if (info.id && !strcmp(info.id, VPM_ID)) { listed = info.loaded ? 2 : 1; break; }
        }
    }
    HMODULE vpm = GetModuleHandleA(VPM_DLL);
    int mom = vpm && GetProcAddress(vpm, "vpm_mom_api");
    if (listed == 0 && !vpm) return "the Vanilla Plus Megapack (" VPM_ID ") is not installed";
    if (listed == 1) return "the Vanilla Plus Megapack (" VPM_ID ") is installed but did not load";
    if (!vpm) return listed == 2 ? "the Vanilla Plus Megapack is loaded, but not as " VPM_DLL ", so its exports were not checked"
                                 : VPM_DLL " is not loaded";
    return mom ? "the Vanilla Plus Megapack that is loaded offers only vpm_mom_api, which this release does not use yet"
               : "the Vanilla Plus Megapack that is loaded publishes neither";
}
/* the mission keys, as the log names them (break contact's only when it is built in) */
static const char* KeysText(void)
{
    static char keys[64];
    if (LC_BREAK_CONTACT) snprintf(keys, sizeof keys, "%s / %s", s_keyBreak.label, s_keyPatrol.label);
    else snprintf(keys, sizeof keys, "%s", s_keyPatrol.label);
    return keys;
}
/* no usable menu 30 s after start: one line naming what was looked for and where the settings are without it */
static void NoMenuYet(void)
{
    static int told;
    if (told || g_optsApi || g_now < 30 || atomic_load(&g_menuOwner) == 2) return;
    told = 1;
    Log("MODS tab: %s after 30 s: looked for %s (published by giblett's Vanilla Plus Megapack build); %s. Without it: "
        "lost-contact.ini, or %s in a mission (or Mod Options Menu, from Lua: needs HD2Runtime and Bingus Shared Loader)",
        g_menuRefused ? "no usable menu (one was refused, see above)" : "no menu", HD2_MOD_OPTIONS_ENV, PublicPack(), KeysText());
}
/* right after the rows are added: a get() that does not find the first row (a stub, or rows keyed otherwise) would make
 * the 10 s check re-add them for ever, so that menu address gets no check */
static void CheckGet(const Hd2ModOptionsApi* api)
{
    int32_t v;
    if (g_noCheck || api->get(OPT_MOD, "break", &v)) return;
    g_noCheck = 1;
    Log("MODS tab: the menu's get() does not find a row it just added; its rows are not checked again for this menu");
}
static void Options(void)
{
    const Hd2ModOptionsApi* api = MenuApi();
    atomic_store(&g_optLooked, 1);
    if (atomic_load(&g_menuOwner) == 2)
    {
        static int told;
        if (!told++) Log("MODS tab: the rows are on Mod Options Menu (Lua, through Lost Contact's HUD addon)%s", api ? "; " HD2_MOD_OPTIONS_ENV " is not used" : "");
        return;
    }
    if (!api) { g_optsApi = NULL; NoMenuYet(); return; }
    int none = 0;
    if (!atomic_compare_exchange_strong(&g_menuOwner, &none, 1) && none != 1) return;   /* gone: a menu published again, even at the same address, gets the rows */
    if (api != g_optsApi)
    {
        /* a menu for the first time, or one set up again (a reload): every row is asked for */
        g_optsApi = api;
        if (api != g_checkFor) { g_checkFor = api; g_noCheck = 0; g_readds = 0; }
        g_optsDone = 0; g_optTries = 0; g_optNextTry = 0;
        memset(g_rowOk, 0, sizeof g_rowOk);
        g_shownStatus[0] = g_shownStatus[1] = -1;
    }
    else if (g_optsDone && g_rowOk[0] && !g_noCheck && g_now >= g_optCheck)
    {
        /* every 10 s once registered: get() of the first row tells a menu set up again in place (HellForge's live mods:
         * a taken-out mod's DLL is never freed, so its table stays readable and only its rows are gone). Only for a menu
         * whose get() found that row right after it was added (CheckGet), and at most OPT_READDS re-adds per address.
         * The table is checked first only as a guard against calling into a module that really was unloaded. */
        Hd2ModOptionsApi copy;
        int32_t v;
        g_optCheck = g_now + OPT_RETRY;
        if (!RD((uintptr_t)api, &copy, sizeof copy) || !InModule((const void*)copy.get) || !InModule((const void*)copy.register_option) ||
            !InModule((const void*)copy.set))
        {
            Log("MODS tab: the menu's table or functions are no longer in a loaded module; looking for a menu again");
            g_menuAccepted = NULL; g_menuChecked[0] = 0; g_optsApi = NULL;
            return;
        }
        if (!api->get(OPT_MOD, "break", &v))
        {
            if (g_readds >= OPT_READDS)
            {
                g_noCheck = 1;
                Log("MODS tab: the menu has lost Lost Contact's rows %d times; not checking or adding them again for this menu", g_readds + 1);
                return;
            }
            g_readds++;
            Log("MODS tab: the menu no longer has Lost Contact's rows (set up again?); adding them again (%d of %d)", g_readds, OPT_READDS);
            g_optsDone = 0; g_optTries = 0; g_optNextTry = 0;
            memset(g_rowOk, 0, sizeof g_rowOk);
            g_shownStatus[0] = g_shownStatus[1] = -1;
        }
    }
    /* (at most 400 characters: the menu refuses longer descriptions) */
    static const char* const kText =
        "Break contact: an enemy that loses sight of you stops tracking you. Patrols rerouted: a patrol sent at you walks "
        "to an outpost away from you. A character kit can turn one off while it is your active kit (Lemuen: patrols only). "
        "Turn Character kits can limit off to keep both as set here. The Now rows show what runs.";
    if (!g_optsDone && g_now >= g_optNextTry)
    {
        Hd2ModOption rows[OPT_ROWS] = {
            {sizeof(Hd2ModOption), OPT_MOD, "break", "Break contact", kText, HD2_MOD_OPTION_CHOICE, 3, kModes, ModeIndex(s_breakMode), OptChanged, NULL},
            {sizeof(Hd2ModOption), OPT_MOD, "patrols", "Patrols rerouted", kText, HD2_MOD_OPTION_CHOICE, 3, kModes, ModeIndex(s_patrolMode), OptChanged, NULL},
            {sizeof(Hd2ModOption), OPT_MOD, "kits", "Character kits can limit", kText, HD2_MOD_OPTION_TOGGLE, 0, NULL, s_kitRules, OptChanged, NULL},
            {sizeof(Hd2ModOption), OPT_MOD, "break_now", "Break contact now", kText, HD2_MOD_OPTION_CHOICE, 4, kStatus, 0, OptChanged, NULL},
            {sizeof(Hd2ModOption), OPT_MOD, "patrols_now", "Patrols now", kText, HD2_MOD_OPTION_CHOICE, 4, kStatus, 0, OptChanged, NULL},
        };
        int added = 0, fresh = 0;
        char refused[96] = "";
        for (int k = 0; k < OPT_ROWS; k++)
        {
            if (!g_rowOk[k] && (g_rowOk[k] = api->register_option(&rows[k]) != 0)) fresh++;
            if (g_rowOk[k]) added++;
            else snprintf(refused + strlen(refused), sizeof refused - strlen(refused), "%s%s", refused[0] ? ", " : "", rows[k].id);
        }
        g_optTries++;
        g_optNextTry = g_now + OPT_RETRY;
        if (fresh) g_shownStatus[0] = g_shownStatus[1] = -1;   /* a Now row added now shows its status */
        if (added == OPT_ROWS)
        {
            g_optsDone = 1; g_optCheck = g_now + OPT_RETRY;
            Log("MODS tab: %d of %d rows added (menu: %s, version %u)", added, OPT_ROWS, HD2_MOD_OPTIONS_ENV, api->version);
            CheckGet(api);
        }
        else if (g_optTries < OPT_TRIES)
            Log("MODS tab: %d of %d rows added (menu: %s, version %u); refused: %s; asking again in %.0f s (try %d of %d)", added, OPT_ROWS,
                HD2_MOD_OPTIONS_ENV, api->version, refused, OPT_RETRY, g_optTries, OPT_TRIES);
        else
        {
            g_optsDone = 1;
            g_optCheck = g_now + OPT_RETRY;
            if (g_rowOk[0]) CheckGet(api);
            if (!added) { atomic_store(&g_menuOwner, 0); g_optsApi = NULL; }   /* none on it: the Lua menu may take them */
            Log("MODS tab: %d of %d rows added after %d tries (menu: %s, version %u); refused: %s (the menu may be full: 8 mods, 31 rows each). "
                "Those settings stay in lost-contact.ini and on %s", added, OPT_ROWS, g_optTries, HD2_MOD_OPTIONS_ENV, api->version, refused,
                KeysText());
        }
    }
    int st[2] = {StatusOf(LC_BREAK_CONTACT ? s_breakMode : 0, 1), StatusOf(s_patrolMode, 2)};
    if (st[0] != g_shownStatus[0]) { g_shownStatus[0] = st[0]; api->set(OPT_MOD, "break_now", st[0]); if (st[0] == 2) Log("break contact is off while %s is your active kit (MODS tab: Character kits can limit)", g_kitBy ? g_kitBy : "a kit"); }
    if (st[1] != g_shownStatus[1]) { g_shownStatus[1] = st[1]; api->set(OPT_MOD, "patrols_now", st[1]); if (st[1] == 2) Log("patrols are off while %s is your active kit (MODS tab: Character kits can limit)", g_kitBy ? g_kitBy : "a kit"); }
}
static void Tick(HfMod* mod, float dt, void* user)
{
    (void)mod; (void)user;
    g_now += dt > 0 && dt < 1 ? dt : 0;
    static double optNext, kitNext;
    if (g_now >= optNext)
    {
        optNext = g_now + 0.5;
        if (g_now >= kitNext) { kitNext = g_now + 2; KitRules(); }
        Options();
    }
    if (g_ready < 0) return;
    int inMission = hf->game_state(self) == HF_STATE_MISSION;
    if (inMission != g_inMission)
    {
        g_inMission = inMission;
        if (!inMission && (g_bc.lost || g_pt.sent))
            Log("mission end: break contact lost you %d times (%d after you again unsensed); patrols: %d sends, %d re-aimed at you, %d outposts; %d patrols despawned (%d routed), %d killed",
                g_bc.lost, g_bc.blind, g_pt.sent, g_pt.reaimed, g_realPosts, g_gone.patrolDespawned, g_gone.routedDespawned, g_gone.patrolKilled);
        if (!inMission && g_side.seen)
            Log("mission end: %d patrols set out, %d aimed through you (%s), %d aimed through you again later, %d already passing by", g_side.seen,
                g_side.through, s_patrolMode == 1 ? "moved beside you" : "observed", g_side.again, g_side.kept);
        if (inMission) MissionStart();
    }
    if (!inMission) return;
    if (g_ready == 0)
    {
        static double nextTry;
        static int tries;
        if (g_now < nextTry) return;
        nextTry = g_now + 3;
        if (Bind()) { g_ready = 1; if (LC_BREAK_CONTACT) Log("ready: break contact %s, patrols %s (%s / %s)", ModeName(s_breakMode), ModeName(s_patrolMode), s_keyBreak.label, s_keyPatrol.label); else Log("ready: patrols %s (%s)", ModeName(s_patrolMode), s_keyPatrol.label); }
        else if (++tries >= 10) { g_ready = -1; Log("REFUSED after 10 tries in a mission"); }
        if (g_ready != 1) return;
    }
    /* keys: each cycles on -> observe -> off */
    static int prevB, prevP;
    static double keyAfter;
    int kb = KeyPressed(&s_keyBreak), kp = KeyPressed(&s_keyPatrol);
    if (LC_BREAK_CONTACT && kb && !prevB && g_now >= keyAfter)
    {
        keyAfter = g_now + 0.5;
        s_breakMode = s_breakMode == 1 ? 2 : s_breakMode == 2 ? 0 : 1;
        g_bc.lost = g_bc.blind = 0;
        for (int j = 0; j < g_nfoes; j++) g_foes[j].unseen = 0;
        Log("break contact: %s", ModeName(s_breakMode));
    }
    if (kp && !prevP && g_now >= keyAfter)
    {
        keyAfter = g_now + 0.5;
        s_patrolMode = s_patrolMode == 1 ? 2 : s_patrolMode == 2 ? 0 : 1;
        g_pt.sent = g_pt.reaimed = 0;
        for (int j = 0; j < g_nfoes; j++) g_foes[j].routed = 0;
        Log("patrols: %s", ModeName(s_patrolMode));
    }
    prevB = kb; prevP = kp;
    static double bcNext, ptNext, told;
    static double rulesNext;
    if (g_now >= rulesNext) { rulesNext = g_now + 2; KitRules(); }
    if (LC_BREAK_CONTACT && s_breakMode && (g_rules & 1) && g_now >= bcNext) { bcNext = g_now + 0.25; BreakPass(); }
    if (s_patrolMode && (g_rules & 2)) LeaderFrame();
    if (s_patrolMode && (g_rules & 2) && g_now >= ptNext) { ptNext = g_now + 1.0; PatrolPass(); }
    if (g_now - told >= 5)
    {
        told = g_now;
        if (s_breakMode && g_bcSum.passes)
        {
            double n = g_bcSum.passes;
            Log("break contact (%s): within %.0f m %.1f, after you %.1f (sensing you %.1f, not %.1f); %d %s, %d after you again unsensed", ModeName(s_breakMode),
                (double)s_breakRange, g_bcSum.nearby / n, g_bcSum.chasing / n, g_bcSum.sensing / n, g_bcSum.unsensed / n, g_bc.lost,
                s_breakMode == 2 ? "would have lost you" : "lost you", g_bc.blind);
        }
        if (s_patrolMode && g_ptSum.passes)
        {
            double n = g_ptSum.passes;
            Log("patrols (%s): %d outposts known; aimed at you %.1f, %s %.1f; %d sends, %d re-aimed at you; this mission %d patrols despawned (%d routed), %d killed, %d other enemies despawned",
                ModeName(s_patrolMode), g_realPosts, g_ptSum.seeking / n, s_patrolMode == 2 ? "would be routed" : "routed", g_ptSum.routed / n, g_pt.sent,
                g_pt.reaimed, g_gone.patrolDespawned, g_gone.routedDespawned, g_gone.patrolKilled, g_gone.otherDespawned);
        }
        memset(&g_bcSum, 0, sizeof g_bcSum);
        memset(&g_ptSum, 0, sizeof g_ptSum);
    }
}

/* the HUD (hud-src) reads this every frame: out[0] break contact's mode (0 off, 1 on, 2 observe), out[1..4] this pass's
 * enemies within break_range / after you / of those sensing you / not, out[5] lost you (or would have) since the mode
 * was set, out[6] after you again unsensed, out[7] patrols' mode, out[8] outposts known, out[9] patrols aimed at you,
 * out[10] routed, out[11] re-aimed at you since set, out[12] 1 in a mission, bound. Answers how many it wrote. */
HF_MOD_EXPORT int lost_contact_hud(unsigned version, int* out, int n)
{
    if (version != 1 || !out || n < 13) return 0;
    out[0] = s_breakMode; out[1] = g_bc.nearby; out[2] = g_bc.chasing; out[3] = g_bc.sensing; out[4] = g_bc.unsensed;
    out[5] = g_bc.lost; out[6] = g_bc.blind; out[7] = s_patrolMode; out[8] = g_realPosts; out[9] = g_pt.seeking;
    out[10] = g_pt.routed; out[11] = g_pt.reaimed; out[12] = g_ready == 1 && g_inMission;
    return 13;
}
/* ---- 0.3.0: the MODS tab from Lua (hud-src/lost_contact_hud.lua: Mod Options Menu through HD2Runtime and Bingus
 * Shared Loader, the menu the public Vanilla Plus Megapack has). The addon claims the rows (lost_contact_menu), reads
 * each value (lost_contact_option_get) and pushes the player's changes (lost_contact_option), which are saved as the C
 * menu's are: %LOCALAPPDATA%\\HellForgeMods\\lost-contact.ini. Keys: break, patrols (0 off, 1 on, 2 observe), kits
 * (0 / 1), the six numbers by their ini names, and break_now / patrols_now (read only: 0 running, 1 off by your
 * setting, 2 off by the active kit, 3 observing). ----------------------------------------------------------------- */
/* claim 1: the Lua side asks for the rows. 1: they are its own (register them); 0: not now (the C menu has them, or
 * it has not been looked for yet: ask again); -1: an unknown version. claim 0 only answers. */
HF_MOD_EXPORT int lost_contact_menu(unsigned version, int claim)
{
    if (version != 1) return -1;
    int owner = atomic_load(&g_menuOwner);
    if (owner == 2) return 1;
    if (!claim || owner == 1 || !atomic_load(&g_optLooked)) return 0;
    int none = 0;
    if (atomic_compare_exchange_strong(&g_menuOwner, &none, 2)) return 1;
    return none == 2;
}
typedef struct { const char* key; float* at; float lo, hi; unsigned bit; } NumOpt;
static const NumOpt kNums[] = {
    {"lose_after", &s_loseAfter, 0.5f, 60, SV_LOSE}, {"break_range", &s_breakRange, 10, 500, SV_RANGE},
    {"seek_radius", &s_seekRadius, 10, 300, SV_SEEK}, {"keep_away", &s_keepAway, 20, 600, SV_KEEP},
    {"patrol_range", &s_patrolRange, 50, 1000, SV_PRANGE}, {"resend", &s_resend, 0.5f, 30, SV_RESEND},
    {"pass_by", &s_passBy, 0, 150, SV_PASS},
};
HF_MOD_EXPORT int lost_contact_option_get(unsigned version, const char* key, double* out)
{
    if (version != 1 || !key || !out) return 0;
    if (!strcmp(key, "break")) *out = s_breakMode;
    else if (!strcmp(key, "patrols")) *out = s_patrolMode;
    else if (!strcmp(key, "kits")) *out = s_kitRules;
    else if (!strcmp(key, "break_now")) *out = StatusOf(LC_BREAK_CONTACT ? s_breakMode : 0, 1);
    else if (!strcmp(key, "patrols_now")) *out = StatusOf(s_patrolMode, 2);
    else
    {
        for (unsigned i = 0; i < sizeof kNums / sizeof kNums[0]; i++)
            if (!strcmp(key, kNums[i].key)) { *out = *kNums[i].at; return 1; }
        return 0;
    }
    return 1;
}
/* 1 applied (and saved), 0 refused: an unknown or read-only key, or a value out of range */
HF_MOD_EXPORT int lost_contact_option(unsigned version, const char* key, double value)
{
    if (version != 1 || !key || !isfinite(value)) return 0;
    int m = (int)value;
    if (!strcmp(key, "break") || !strcmp(key, "patrols"))
    {
        if (m < 0 || m > 2 || m != value || (!LC_BREAK_CONTACT && key[0] == 'b')) return 0;
        int* at = key[0] == 'b' ? &s_breakMode : &s_patrolMode;
        if (*at != m)
        {
            *at = m;
            if (key[0] == 'b') { g_bc.lost = g_bc.blind = 0; for (int j = 0; j < g_nfoes; j++) g_foes[j].unseen = 0; }
            else { g_pt.sent = g_pt.reaimed = 0; for (int j = 0; j < g_nfoes; j++) g_foes[j].routed = 0; }
            Log("MODS tab: %s %s", key[0] == 'b' ? "break contact" : "patrols", ModeName(m));
        }
    }
    else if (!strcmp(key, "kits"))
    {
        if (value != 0 && value != 1) return 0;
        if (s_kitRules != m) { s_kitRules = m; Log("MODS tab: character kits %s limit them", m ? "may" : "may not"); KitRules(); }
    }
    else
    {
        unsigned i;
        for (i = 0; i < sizeof kNums / sizeof kNums[0] && strcmp(key, kNums[i].key); i++) {}
        if (i == sizeof kNums / sizeof kNums[0] || value < kNums[i].lo || value > kNums[i].hi) return 0;
        if (kNums[i].at == &s_passBy && value < 20) value = 0;   /* pass_by under 20: off */
        if (*kNums[i].at != (float)value) { *kNums[i].at = (float)value; Log("MODS tab: %s %g", key, value); }
        g_savedNums |= kNums[i].bit;
    }
    SaveChoices();
    return 1;
}
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 15 || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    LoadSettings();
    if (LC_BREAK_CONTACT)
        Log("Lost Contact " VERSION ": break contact %s (lose after %.0f s, within %.0f m), patrols %s (seek %.0f m, keep away %.0f m); keys %s / %s",
            ModeName(s_breakMode), (double)s_loseAfter, (double)s_breakRange, ModeName(s_patrolMode), (double)s_seekRadius, (double)s_keepAway,
            s_keyBreak.label, s_keyPatrol.label);
    else
        Log("Lost Contact " VERSION ": patrols %s (seek %.0f m, keep away %.0f m); key %s", ModeName(s_patrolMode), (double)s_seekRadius,
            (double)s_keepAway, s_keyPatrol.label);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) &&
             api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) && api->on_game_thread(mod, Tick, NULL);
    if (!ok) Log("events or the game thread are unavailable");
    return 0;
}
