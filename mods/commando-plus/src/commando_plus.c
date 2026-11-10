/* Commando Plus (yui.commando-plus): Commando missions made a little more forgiving, each option an ini setting
 * (commando-plus.ini). Host or solo; a joined player's game changes nothing. Design: docs/commando-plus-design.md.
 *
 *   no_timer        the mission timer is paused (hf->freeze_mission_timer; a new mission starts it again)
 *   resupply_calls  0 to 2 free Resupply calls, granted outside the loadout at the drop-in: one entry appended to your
 *                   own stratagem record, as the game grants Reinforce and Resupply (Voymastina's Sinbreaker grant). A
 *                   Resupply already on the record with limited uses gets them added; an unlimited one is left alone.
 *   extra_calls     0 to 3 more uses on Commando's Call In Destroyer (the Super Destroyer call-in: stratagem type 123,
 *                   uses 1 on the record)
 *   every_mission   1: the options apply to every mission, not only Commando
 *   outpost_caches  0.2.0: every enemy base cleared pays out free resupply calls (cache_calls_small / _large; the game's
 *                   reward ammo rack, cache_reward 0, else Resupply uses). A base: the spawner structures within
 *                   base_radius m of each other (bug holes, bot fabricators, Illuminate spawners), noted as they spawn;
 *                   cleared: the last one closed (HF_ON_BUGHOLE), destroyed (HF_ON_KILL) or gone / at 0 health.
 *   cash_in         0.5.0 (default): instead, each base cleared banks ammo boxes (cache_boxes_small 4 / _large 8),
 *                   called in by the Outpost Cache (8 a pod) and the Small Outpost Cache (the rest) (section "the cash-in" below)
 *   resupply_to_pods  0.5.0 (off by default): a Resupply thrown in Commando lands at the Reinforcement Pods
 *   poi_extra       0.3.0: the loot caches at points of interest that hold stims or grenades come up poi_extra times as
 *                   often. The caches' tables are HellForge's own research (native/mod-loader/forge_credits.c, 0.4.0:
 *                   the loot roll game+0xF3A800, its context +0x18 -> +0x2D0 -> +0x68: tables at +0x90, 0x28 each, count
 *                   +0x98; a table: +4 weight, +8 minimum difficulty, its items at +0x18 (0x30 each, +0 the entity),
 *                   count +0x20). The roll is hooked in front of HellForge's own hook (mods' hooks run first): the weight
 *                   is raised for the roll and put back after it, so HellForge's Forge Credits chance, which saves a
 *                   table again whenever it holds something else, never builds on a raised one. Every player rolls the
 *                   caches from the mission's seed: everyone needs the same setting (joined players' games apply it too).
 *
 * Commando is told by the record (the Mission Probe, 2026-11-04): only a Commando mission puts Call In Destroyer (type
 * 123) on the player's stratagem record. The record is read 8 s after the mission starts (when the probe read it), then
 * every second until it is readable.
 *
 * The record (build 25480438; HD2Runtime's research by SkyeShade, as Voymastina, Koleda and the Veterancy System use it):
 * records at [game+0x347CE50], 0x1690 each, peer id at +0, count at +0x2D200; this machine's peer at [game+0x347CEF0]
 * +0xB398. A player's list is the record +0x38: entries 0x30 each from +0x188 (+0 type, +4 uses, 0xFFFFFFFF unlimited,
 * +9 granted, +0x10 / +0x18 / +0x20 cooldown fields, +0x20 its end on the clock [game+0x3326348]+0x18), count +0x788.
 * The code that reads and appends them is pinned (GrantPins) before anything is written. A call outside the loadout
 * needs its package loaded, or it lands as a purple question mark: the Resupply's is held for the session.
 *
 * Not included: more grenades and stims at POIs (needs a probe), patrols keeping to their outposts (Lost Contact),
 * sabotage objectives (later). */
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "hellforge_mod.h"
#include "hellforge_content.h"

#define BUILD "25480438"
#define VERSION "0.5.6"
#define CALL_IN HF_STRATAGEM_CALL_IN_DESTROYER   /* 123 */
#define RESUPPLY HF_STRATAGEM_RESUPPLY           /* 33 */
#define UNLIMITED 0xFFFFFFFFu

static const HfApi* hf;
static HfMod* self;
static uintptr_t game, exe;
static int g_ready;
static double g_now;
#define Log(...) hf->log(self, __VA_ARGS__)

static uint32_t Word(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t Quad(const uint8_t* p) { uint64_t v; memcpy(&v, p, 8); return v; }
static int AddressOk(uintptr_t a, size_t n) { return n && a >= 0x10000 && a < UINT64_C(0x7FFFFFFF0000) && n <= UINT64_C(0x7FFFFFFF0000) - a; }
static int ReadAt(uintptr_t a, void* out, size_t n) { return AddressOk(a, n) && hf->read(a, out, n) == 0; }
static uintptr_t Pointer(uintptr_t a) { uintptr_t v = 0; return ReadAt(a, &v, 8) && AddressOk(v, 1) ? v : 0; }
/* the game's hash maps (the kit base's Lookup: Helldiver Survival Rework, MIT) */
static int Lookup(uintptr_t at, uint32_t key, uint32_t limit, uint32_t* index)
{
    uint8_t h[20], row[8];
    if (!ReadAt(at, h, sizeof h)) return 0;
    uintptr_t entries = (uintptr_t)Quad(h);
    uint32_t n = Word(h + 8), empty = Word(h + 12), mult = Word(h + 16);
    if (!AddressOk(entries, 8) || !n || n > limit || (n & (n - 1)) || key == empty) return 0;
    for (unsigned i = 0; i < n && i < 128; i++)
    {
        if (!ReadAt(entries + 8u * ((key * mult + i) & (n - 1)), row, 8)) return 0;
        if (Word(row) == key) { *index = Word(row + 4); return *index != UINT32_MAX; }
        if (Word(row) == empty) return 0;
    }
    return 0;
}

/* ---- settings ------------------------------------------------------------------------------------------------------- */
static int s_noTimer = 1, s_resupplyCalls = 1, s_extraCalls = 1, s_everyMission = 0;
static int s_poiExtra = 1;   /* 0.3.1: off by default: the hooked roll is the POI crates' (support weapons, credits), not stims / grenades */
static int s_caches = 1, s_cacheSmall = 1, s_cacheLarge = 2, s_cacheReward = 0, s_baseRadius = 40;
static int s_cashIn = 1, s_boxSmall = 4, s_boxLarge = 8;   /* 0.5.0: the cash-in (the Outpost Cache) */
static int s_toPods = 0;                                     /* 0.5.0: resupply_to_pods */
static int s_destroyerWindow = 0;                            /* 0.5.1 (test): Call In Destroyer's cooldown, s; 0 the game's */
static int ReadSettings(const char* path)
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
        int x = atoi(v);
        if (!strcmp(k, "no_timer")) s_noTimer = x ? 1 : 0;
        else if (!strcmp(k, "resupply_calls")) { if (x >= 0 && x <= 2) s_resupplyCalls = x; else Log("settings: resupply_calls %d is not 0 to 2, kept %d", x, s_resupplyCalls); }
        else if (!strcmp(k, "extra_calls")) { if (x >= 0 && x <= 3) s_extraCalls = x; else Log("settings: extra_calls %d is not 0 to 3, kept %d", x, s_extraCalls); }
        else if (!strcmp(k, "every_mission")) s_everyMission = x ? 1 : 0;
        else if (!strcmp(k, "outpost_caches")) s_caches = x ? 1 : 0;
        else if (!strcmp(k, "cache_calls_small")) { if (x >= 0 && x <= 5) s_cacheSmall = x; }
        else if (!strcmp(k, "cache_calls_large")) { if (x >= 0 && x <= 5) s_cacheLarge = x; }
        else if (!strcmp(k, "cache_reward")) s_cacheReward = x ? 1 : 0;
        else if (!strcmp(k, "base_radius")) { if (x >= 10 && x <= 200) s_baseRadius = x; }
        else if (!strcmp(k, "poi_extra")) { if (x >= 1 && x <= 10) s_poiExtra = x; }
        else if (!strcmp(k, "cash_in")) s_cashIn = x ? 1 : 0;
        else if (!strcmp(k, "cache_boxes_small")) { if (x >= 0 && x <= 24) s_boxSmall = x; }
        else if (!strcmp(k, "cache_boxes_large")) { if (x >= 0 && x <= 24) s_boxLarge = x; }
        else if (!strcmp(k, "resupply_to_pods")) s_toPods = x ? 1 : 0;
        else if (!strcmp(k, "destroyer_window")) { if (x == 0 || (x > 60 && x <= 600)) s_destroyerWindow = x; }
    }
    fclose(f);
    return 1;
}
static void LoadSettings(void)
{
    char path[MAX_PATH * 2];
    snprintf(path, sizeof path, "%s\\commando-plus.ini", hf->mod_folder(self));
    if (!ReadSettings(path)) Log("settings: no commando-plus.ini, the defaults");
    Log("settings: no_timer %d, resupply_calls %d, extra_calls %d, every_mission %d (%s)", s_noTimer, s_resupplyCalls, s_extraCalls,
        s_everyMission, s_everyMission ? "every mission" : "Commando missions only");
    Log("settings: poi_extra %d (stim and grenade caches %s)", s_poiExtra, s_poiExtra > 1 ? "more often" : "as the game has them");
    if (s_cashIn) Log("settings: outpost_caches %d, cash_in 1 (small base %d, large base %d ammo boxes, called in by the Outpost Cache), base_radius %d m", s_caches, s_boxSmall, s_boxLarge, s_baseRadius);
    else Log("settings: outpost_caches %d, cash_in 0 (small base %d, large base %d calls of %s), base_radius %d m", s_caches, s_cacheSmall, s_cacheLarge,
             s_cacheReward ? "Resupply" : "the reward ammo rack", s_baseRadius);
    Log("settings: resupply_to_pods %d (%s)", s_toPods, s_toPods ? "a Resupply thrown in Commando lands at the Reinforcement Pods that have had none" : "Resupplies land where they are thrown");
}

/* ---- the local player's stratagem record ---------------------------------------------------------------------------- */
static uintptr_t LocalStratagems(void)
{
    uintptr_t recs = Pointer(game + 0x347CE50u), peerObj = Pointer(game + 0x347CEF0u);
    uint64_t me = 0;
    uint32_t n = 0;
    if (!recs || !peerObj || !ReadAt(peerObj + 0xB398, &me, 8) || !me || !ReadAt(recs + 0x2D200, &n, 4) || !n || n > 32) return 0;
    for (uint32_t r = 0; r < n; r++)
    {
        uint64_t peer = 0;
        uintptr_t rec = recs + (uintptr_t)r * 0x1690u;
        if (ReadAt(rec, &peer, 8) && peer == me) return rec + 0x38;
    }
    return 0;
}
static int GrantPins(void)
{
    static int ok = -1;
    if (ok >= 0) return ok;
    static const struct { uintptr_t at; const char* bytes; } k[] = {
        {0x1834AE6, "4C 8B 0D 63 83 C4 01"},   /* the HUD reads the records list */
        {0x1834B07, "48 05 90 16 00 00"},      /* ... 0x1690 a record */
        {0x66D24A, "48 8B 8E A0 01 00 00"},    /* availability reads an entry's cooldown end */
        {0x66F2A7, "89 96 88 01 00 00"},       /* the game's append: entry +0 type */
        {0x66F2CD, "89 86 8C 01 00 00"},       /* +4 uses */
        {0x66F2D7, "88 86 91 01 00 00"},       /* +9 granted */
        {0x66F2EB, "41 89 87 88 07 00 00"}};   /* count + 1 */
    ok = 1;
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++)
        if (!hf->code_matches(game + k[i].at, k[i].bytes))
        { Log("REFUSED: game.dll+0x%llX is not as researched: the stratagem record is not touched", (unsigned long long)k[i].at); ok = 0; break; }
    return ok;
}
/* The list (its address, count) if readable; 0 not */
static int List(uintptr_t* st, uint32_t* count)
{
    *st = LocalStratagems();
    return *st && ReadAt(*st + 0x788, count, 4) && *count <= 16;
}
/* The slot of a type, -1 none, -2 unreadable */
static int Find(uintptr_t st, uint32_t count, uint32_t type)
{
    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t t = 0;
        if (!ReadAt(st + 0x188 + i * 0x30u, &t, 4)) return -2;
        if (t == type) return (int)i;
    }
    return -1;
}
static const char* UsesText(uint32_t u, char* buf, size_t n)
{
    if (u == UNLIMITED) snprintf(buf, n, "unlimited");
    else snprintf(buf, n, "%u", u);
    return buf;
}
/* One line per slot: what the mission gave (the first test log tells Commando from not) */
static void LogList(const char* why, uintptr_t st, uint32_t count)
{
    char line[512];
    int at = snprintf(line, sizeof line, "%s: %u stratagems:", why, count);
    for (uint32_t i = 0; i < count && at > 0 && at < (int)sizeof line - 32; i++)
    {
        uint8_t e[0x10];
        char u[16];
        if (!ReadAt(st + 0x188 + i * 0x30u, e, sizeof e)) break;
        at += snprintf(line + at, sizeof line - at, " %u(uses %s%s)", Word(e), UsesText(Word(e + 4), u, sizeof u), e[9] ? "" : ", not granted");
    }
    Log("%s", line);
}
/* Sets a slot's uses and reads it back. 1 done */
static int SetUses(uintptr_t st, int slot, uint32_t uses, const char* what)
{
    uintptr_t at = st + 0x188 + (uint32_t)slot * 0x30u;
    uint32_t was = 0, back = 0;
    char a[16], b[16], c[16];
    ReadAt(at + 4, &was, 4);
    if (hf->write(at + 4, &uses, 4)) { Log("%s: the uses write failed (slot %d)", what, slot); return 0; }
    ReadAt(at + 4, &back, 4);
    Log("%s: slot %d uses %s -> %s, reads back %s%s", what, slot, UsesText(was, a, sizeof a), UsesText(uses, b, sizeof b), UsesText(back, c, sizeof c),
        back == uses ? "" : " (NOT as written)");
    return back == uses;
}
/* Appends an entry (type, uses, granted), as the game grants Reinforce and Resupply: a ready granted entry is the
 * template for the fields not set, its cooldown fields zeroed. Reads it back. 1 done */
static int Append(uintptr_t st, uint32_t count, uint32_t type, uint32_t uses, const char* what)
{
    uintptr_t clockObj = Pointer(game + 0x3326348u);
    uint64_t now = 0, zero = 0;
    if (!clockObj || !ReadAt(clockObj + 0x18, &now, 8)) { Log("%s: the clock is unreadable: not granted", what); return 0; }
    if (count >= 16) { Log("%s: the stratagem list is full (16): not granted", what); return 0; }
    int tmpl = -1;
    for (uint32_t i = 0; i < count && tmpl < 0; i++)
    {
        uint8_t e[0x30];
        if (ReadAt(st + 0x188 + i * 0x30u, e, sizeof e) && e[9] == 1 && Quad(e + 0x20) <= now) tmpl = (int)i;
    }
    uint8_t e[0x30] = {0};
    if (tmpl >= 0) ReadAt(st + 0x188 + (uint32_t)tmpl * 0x30u, e, sizeof e);
    memcpy(e, &type, 4);
    memcpy(e + 4, &uses, 4);
    e[9] = 1;
    memcpy(e + 0x10, &zero, 8);
    memcpy(e + 0x18, &zero, 8);
    memcpy(e + 0x20, &zero, 8);
    uint32_t next = count + 1;
    if (hf->write(st + 0x188 + count * 0x30u, e, sizeof e) || hf->write(st + 0x788, &next, 4)) { Log("%s: the write failed: not granted", what); return 0; }
    uint8_t back[0x10];
    uint32_t backCount = 0;
    char u[16];
    int ok = ReadAt(st + 0x188 + count * 0x30u, back, sizeof back) && ReadAt(st + 0x788, &backCount, 4);
    Log("%s: granted outside the loadout as slot %u (template slot %d); reads back type %u, uses %s, granted %u, count %u%s", what, count, tmpl,
        ok ? Word(back) : 0, UsesText(ok ? Word(back + 4) : 0, u, sizeof u), ok ? back[9] : 0, backCount,
        ok && Word(back) == type && Word(back + 4) == uses && back[9] == 1 && backCount == next ? "" : " (NOT as written)");
    return ok && Word(back) == type && backCount == next;
}

/* ---- the Resupply's package, held for the session ------------------------------------------------------------------- */
static int g_pkgState;   /* 0 not asked, 1 loading, 2 resident, 3 none / failed */
static int g_rackPkg;    /* the reward ammo rack's, the same states */
static void HoldPackage(uint32_t type, const char* name, int* state, double* next)
{
    if (*state >= 2 || g_now < *next) return;
    *next = g_now + 2;
    uintptr_t row = hf->stratagem_row ? hf->stratagem_row(type) : 0;
    uint64_t pkg = 0;
    if (!row) return;   /* the stratagem table is not loaded yet */
    if (!ReadAt(row + 0xA8, &pkg, 8) || !pkg || pkg == ~0ull) { *state = 3; Log("%s: its stratagem names no package (a call may land as a question mark)", name); return; }
    int r = hf->load_package(self, pkg);
    if (r == HF_PACKAGE_RESIDENT) { *state = 2; Log("%s: its package %016llx is loaded", name, (unsigned long long)pkg); }
    else if (!r) { *state = 3; Log("%s: its package %016llx could not be held: a granted call may land as a purple question mark", name, (unsigned long long)pkg); }
    else *state = 1;
}
static void ResupplyPackage(void)
{
    static double a, b;
    if (s_resupplyCalls || (s_caches && s_cacheReward)) HoldPackage(RESUPPLY, "Resupply", &g_pkgState, &a);
    if (s_caches && !s_cacheReward) HoldPackage(HF_STRATAGEM_AMMO_RACK_PRESIDENT_REWARD, "the reward ammo rack", &g_rackPkg, &b);
}


/* ---- 0.2.0: outpost caches ------------------------------------------------------------------------------------------ */
/* the spawner structures (HD2Runtime's enemy catalogue, kind "structure"); large: a base of its own size */
static const struct { uint64_t res; int large; const char* name; } kSpawners[] = {
    {0x0DF874E208040D2Full, 0, "bug hole"}, {0x8C31B749759CBD61ull, 0, "bug hole (warriors)"}, {0xBC2AF8548C6D5E06ull, 0, "bug hole (hunters)"},
    {0x9B58C95349D051F9ull, 0, "bug hole (spitters)"}, {0x97DD3178E9F0AB70ull, 0, "bug hole (prowlers)"}, {0x8901F188DB366B4Bull, 0, "bug hole (scavengers)"},
    {0xF78BF0FF5C62140Dull, 0, "bug hole (boomers)"}, {0x36CC8EAD2BB18D78ull, 0, "bug hole (hive guards)"}, {0xB6A181ADCF547AEBull, 0, "bug hole (ceiling)"},
    {0x4776A1CF3F19A13Bull, 1, "stalker lair hole"}, {0x3A2CEF12ED32A088ull, 1, "bile titan hole"}, {0x095686275A113614ull, 1, "shrieker hole"},
    {0xA36F5938D78A0349ull, 1, "shrieker nest"}, {0xAFC719AF96F10DC3ull, 1, "mega shrieker nest"},
    {0xF0B98FB953B13960ull, 0, "fabricator"}, {0x4FB8EB356AC8553Eull, 0, "fabricator (standard)"}, {0x22CC0ED4CEB9CB68ull, 0, "fabricator (assault)"},
    {0x5158E582FBEB26BDull, 0, "fabricator (jump pack)"}, {0xA1A7B76B29088843ull, 0, "fabricator (phalanx)"}, {0x4232EE48E2CFD24Eull, 1, "bulk fabricator"},
    {0xA8AE6952B375EF6Cull, 1, "gunship fabricator"}, {0x5336583C1F8BA745ull, 1, "command bunker"},
    {0x7DF314C744F0D935ull, 0, "warp gateway"}, {0xA8B999A49716BF41ull, 0, "Illuminate spawner (staff)"}, {0xB3C9CDB79DC17937ull, 1, "Illuminate spawner ship"},
};
static int SpawnerKind(uint64_t res)
{
    for (unsigned i = 0; i < sizeof kSpawners / sizeof kSpawners[0]; i++) if (kSpawners[i].res == res) return (int)i;
    return -1;
}
/* spawns and deaths, from the event threads to the game thread */
#define RING 256
static uint32_t g_ringEnt[RING], g_ringKind[RING];
static atomic_uint g_ringHead;
static unsigned g_ringTail;
static void Push(uint32_t entity, uint32_t kind)
{
    unsigned h = atomic_fetch_add(&g_ringHead, 1);
    g_ringEnt[h % RING] = entity;
    g_ringKind[h % RING] = kind;
}
#define SPAWNERS 256
#define BASES 96
typedef struct { uint32_t entity; int kind, base, dead, placed, seenUp; float pos[3]; } Spawner;   /* seenUp: its health read above 0 once */
/* destroyed: members brought down by a kill, a bug hole closed or 0 health (0.3.1: a spawner that is only gone, streamed
 * out as the mission loads or ends, leaves its base instead; a base pays only when one of its members was destroyed) */
typedef struct { int members, alive, large, paid, destroyed; float pos[3]; } Base;
static Spawner g_sp[SPAWNERS];
static Base g_base[BASES];
static int g_nsp, g_nbase;
static unsigned g_basesPaid, g_callsPaid;
static atomic_uint g_avatar;      /* the local Helldiver (HF_ON_WEAPON) */
static double g_missionAt;         /* g_now at mission start */
static unsigned g_spLogged, g_goneLogged;
static void CachesReset(void) { g_nsp = g_nbase = 0; g_basesPaid = g_callsPaid = 0; g_spLogged = g_goneLogged = 0; }
static uint32_t UnitOf(uint32_t entity)
{
    uintptr_t root = Pointer(game + 0x346BF98);
    uint32_t index;
    uint8_t d[24];
    if (!root || !entity || entity == 0x7FFF || !Lookup(root + 0xF1AEB0, entity, 1048576, &index) || index >= 262144 ||
        !ReadAt(root + 0xF32F18 + 24u * index, d, 24) || Word(d + 8) != entity) return 0;
    return Word(d + 12);
}
static int Position(uint32_t entity, float out[3])
{
    uint32_t unit = UnitOf(entity);
    uintptr_t registry = Pointer(exe + 0x1A100F0), objects, object, nodes;
    uint32_t count, id, slot = unit & 0x3FFFFF;
    if (!unit || !registry || !ReadAt(registry + 0x98, &count, 4) || slot >= count || !(objects = Pointer(registry + 0x88)) ||
        !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit || !ReadAt(object + 0x70, &count, 4) || !count ||
        count > 4096 || !(nodes = Pointer(object + 0x88)) || !ReadAt(nodes + 48, out, 12))
        return 0;
    for (int i = 0; i < 3; i++) if (!isfinite(out[i]) || fabsf(out[i]) > 100000) return 0;
    return 1;
}
static int Health(uint32_t entity, int32_t* hp)
{
    uintptr_t m = Pointer(game + 53634696u);
    uint8_t h[88];
    if (!entity || !m || !ReadAt(m + 4112, h, sizeof h)) return 0;
    uint32_t live = Word(h + 16), cap = Word(h + 40), empty = Word(h + 44), mult = Word(h + 48);
    uintptr_t buckets = (uintptr_t)Quad(h + 32), records = (uintptr_t)Quad(h + 72);
    if (!buckets || !records || !cap || cap > 65536 || (cap & (cap - 1)) || live > 4096) return 0;
    for (uint32_t k = 0; k < cap; k++)
    {
        uint8_t b[8];
        if (!ReadAt(buckets + 8u * ((entity * mult + k) & (cap - 1)), b, 8)) return 0;
        if (Word(b) == empty) return 0;
        if (Word(b) != entity) continue;
        if (Word(b + 4) >= live) return 0;
        return ReadAt(records + 416u * Word(b + 4) + 20, hp, 4);
    }
    return 0;
}
static int Find(uintptr_t st, uint32_t count, uint32_t type);
static int SetUses(uintptr_t st, int slot, uint32_t uses, const char* what);
static int Append(uintptr_t st, uint32_t count, uint32_t type, uint32_t uses, const char* what);
static int List(uintptr_t* st, uint32_t* count);
static int GrantPins(void);
static int g_applied, g_commando;
static int Bank(int n, const char* why);
static int g_cacheRow, g_bank;
static unsigned g_boxesBanked, g_boxesCalled;
/* n free calls: the reward ammo rack (or Resupply), added to a limited slot or appended */
static int PayOut(int n, const char* why)
{
    uintptr_t st;
    uint32_t count = 0, uses = 0;
    if (n <= 0) return 1;
    if (!GrantPins() || !List(&st, &count)) { Log("outpost cache (%s): the stratagem record is unreadable: not paid", why); return 0; }
    uint32_t type = s_cacheReward || g_rackPkg == 3 ? RESUPPLY : HF_STRATAGEM_AMMO_RACK_PRESIDENT_REWARD;
    const char* name = type == RESUPPLY ? "Resupply" : "the reward ammo rack";
    int slot = Find(st, count, type);
    char what[96];
    snprintf(what, sizeof what, "outpost cache (%s): %s", why, name);
    if (slot >= 0 && ReadAt(st + 0x188 + (uint32_t)slot * 0x30u + 4, &uses, 4))
    {
        if (uses == UNLIMITED)
        {
            if (type != RESUPPLY) { Log("%s is unlimited already: Resupply instead", what); s_cacheReward = 1; return PayOut(n, why); }
            Log("%s is unlimited on this mission: nothing to add", what);
            return 1;
        }
        return SetUses(st, slot, uses + (uint32_t)n, what);
    }
    if (slot == -2) return 0;
    return Append(st, count, type, (uint32_t)n, what);
}
#define VANISH_NEAR 150.0f   /* 0.3.1: a spawner gone this close to you was destroyed (a wreck may replace it) */
#define SETTLE_S 20.0         /* 0.3.1: the first seconds of a mission: spawners come and go as the map streams in */
static const char* kDeath[] = {"alive", "killed", "closed", "gone", "at 0 health", "destroyed (gone near you)"};
static float Dist(const float a[3], const float b[3]) { float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2]; return sqrtf(dx * dx + dy * dy + dz * dz); }
static int Me(float out[3]) { uint32_t a = atomic_load(&g_avatar); return a && Position(a, out); }
/* one spawner down: destroyed (it counts for its base), or only gone (it leaves its base) */
static int g_burst;   /* this pass: 3 or more spawners gone at once (the map streaming, or the mission ending) */
static void SpawnerDown(Spawner* sp, int how)
{
    float me[3];
    int found = Me(me), settled = g_now - g_missionAt >= SETTLE_S;
    float d = found && sp->placed ? Dist(me, sp->pos) : -1;
    if (how == 6 || (how == 3 && (!settled || g_burst || d < 0 || d > VANISH_NEAR)))
    {
        sp->dead = 3;
        if (sp->placed && sp->base >= 0)
        {
            Base* b = &g_base[sp->base];
            b->members--;
            if (g_goneLogged++ < 12)
                Log("%s (entity %u) of base %d is gone (%s): it leaves the base (%d left in it)", kSpawners[sp->kind].name, sp->entity, sp->base,
                    how == 6 ? "its health never read above 0" : !settled ? "the mission is loading" : g_burst ? "with several others at once" : d < 0 ? "you were not found" : "far from you", b->members);
            sp->base = -1;
        }
        return;
    }
    sp->dead = how == 3 ? 5 : how;
    if (!sp->placed || sp->base < 0) return;
    Base* b = &g_base[sp->base];
    b->destroyed++;
    int left = 0;
    for (int j = 0; j < g_nsp; j++) if (g_sp[j].base == sp->base && !g_sp[j].dead) left++;
    char at[32] = "";
    if (d >= 0) snprintf(at, sizeof at, ", %.0f m from you", (double)d);
    Log("base %d: %s %s%s; %d of %d still up", sp->base, kSpawners[sp->kind].name, kDeath[sp->dead], at, left, b->members);
    for (int j = 0, n = 0; j < g_nsp && left && n < 4; j++)
    {
        const Spawner* o = &g_sp[j];
        if (o->base != sp->base || o->dead) continue;
        n++;
        if (found) Log("  still up: %s (entity %u), %.0f m from you", kSpawners[o->kind].name, o->entity, (double)Dist(me, o->pos));
        else Log("  still up: %s (entity %u)", kSpawners[o->kind].name, o->entity);
    }
}
static void CachesFrame(void)
{
    /* the new spawns and deaths */
    while (g_ringTail != atomic_load(&g_ringHead))
    {
        unsigned i = g_ringTail++ % RING;
        uint32_t e = g_ringEnt[i], k = g_ringKind[i];
        if (k < 1000)
        {
            if (g_nsp >= SPAWNERS) continue;
            Spawner* sp = &g_sp[g_nsp++];
            memset(sp, 0, sizeof *sp);
            sp->entity = e; sp->kind = (int)k; sp->base = -1;
        }
        else
            for (int j = 0; j < g_nsp; j++) if (g_sp[j].entity == e && !g_sp[j].dead) SpawnerDown(&g_sp[j], k == 1001 ? 2 : 1);
    }
    static double next;
    if (g_now < next) return;
    next = g_now + 1;
    /* placed into bases once their position reads; down when gone or at 0 health */
    int gone = 0;
    for (int j = 0; j < g_nsp; j++) if (g_sp[j].placed && !g_sp[j].dead && !UnitOf(g_sp[j].entity)) gone++;
    g_burst = gone >= 3;
    for (int j = 0; j < g_nsp; j++)
    {
        Spawner* sp = &g_sp[j];
        if (!sp->placed && !sp->dead && Position(sp->entity, sp->pos))
        {
            int b = -1;
            for (int q = 0; q < g_nsp && b < 0; q++)
            {
                const Spawner* o = &g_sp[q];
                if (q != j && o->placed && o->base >= 0 && !o->dead && !g_base[o->base].paid && Dist(o->pos, sp->pos) <= (float)s_baseRadius) b = o->base;
            }
            if (b < 0 && g_nbase < BASES) { b = g_nbase++; memset(&g_base[b], 0, sizeof g_base[b]); memcpy(g_base[b].pos, sp->pos, 12); }
            if (b < 0) continue;
            sp->placed = 1; sp->base = b;
            g_base[b].members++;
            if (kSpawners[sp->kind].large) g_base[b].large = 1;
            if (g_spLogged++ < 80)
                Log("spawner: %s (entity %u) at %.0f %.0f %.0f: base %d (%d in it)", kSpawners[sp->kind].name, sp->entity, sp->pos[0], sp->pos[1], sp->pos[2], b, g_base[b].members);
        }
        if (sp->placed && !sp->dead)
        {
            int32_t hp = 1;
            if (!UnitOf(sp->entity)) SpawnerDown(sp, 3);
            else if (Health(sp->entity, &hp))
            {
                /* 0.3.1: a fabricator just spawned reads 0 until its health is set (10 bases "destroyed" 2 s into a
                 * mission, 1100 m from you): 0 health counts only after it was seen above 0 */
                /* 0.4.1: 0 health counts only once it read above 0, and only near you (within VANISH_NEAR): far off, a
                 * fabricator's health read 0 as it streamed out (a base 328 m away paid out, 2026-11-08), and Phalanx
                 * fabricators read 0 before they are up; those wait for their kill */
                float me[3];
                if (hp > 0) sp->seenUp = 1;
                else if (sp->seenUp && Me(me) && Dist(me, sp->pos) <= VANISH_NEAR) SpawnerDown(sp, 4);
            }
        }
    }
    if (g_now - g_missionAt < SETTLE_S) return;
    /* a base whose spawners are all down, one of them destroyed, pays out (once, when the options apply) */
    for (int b = 0; b < g_nbase; b++)
    {
        Base* base = &g_base[b];
        int alive = 0;
        for (int j = 0; j < g_nsp; j++) if (g_sp[j].base == b && !g_sp[j].dead) alive++;
        base->alive = alive;
        if (base->paid || alive || base->members <= 0 || !base->destroyed) continue;
        base->paid = 1;
        int large = base->large || base->members >= 3, n = s_cashIn ? (large ? s_boxLarge : s_boxSmall) : large ? s_cacheLarge : s_cacheSmall;
        char why[64];
        snprintf(why, sizeof why, "a %s base of %d spawner%s cleared", large ? "large" : "small", base->members, base->members == 1 ? "" : "s");
        if (!s_caches || !g_applied) { Log("base %d, %s at %.0f %.0f: no cache (%s)", b, why, base->pos[0], base->pos[1], !s_caches ? "outpost_caches 0" : "the options do not apply this mission"); continue; }
        if (s_cashIn) { if (Bank(n, why)) { g_basesPaid++; Log("base %d, %s at %.0f %.0f: %d ammo boxes banked", b, why, base->pos[0], base->pos[1], n); } }
        else if (PayOut(n, why)) { g_basesPaid++; g_callsPaid += (unsigned)n; Log("base %d, %s at %.0f %.0f: %d free call%s", b, why, base->pos[0], base->pos[1], n, n == 1 ? "" : "s"); }
    }
}

static void CachesSummary(const char* why)
{
    int alive = 0;
    for (int b = 0; b < g_nbase; b++) if (!g_base[b].paid) alive++;
    Log("%s: %d spawners in %d bases (%d not cleared); %u bases paid out, %u free calls", why, g_nsp, g_nbase, alive, g_basesPaid, g_callsPaid);
    if (s_cashIn && g_cacheRow == 2) Log("%s: the cash-in: %u ammo boxes banked, %u called in, %d left", why, g_boxesBanked, g_boxesCalled, g_bank);
    for (int b = 0, n = 0; b < g_nbase && n < 16; b++)
    {
        const Base* base = &g_base[b];
        if (base->paid || base->members <= 0) continue;
        n++;
        char kinds[200] = "";
        size_t at = 0;
        for (int j = 0; j < g_nsp && at + 40 < sizeof kinds; j++)
            if (g_sp[j].base == b && !g_sp[j].dead) at += (size_t)snprintf(kinds + at, sizeof kinds - at, "%s%s", at ? ", " : "", kSpawners[g_sp[j].kind].name);
        Log("  base %d at %.0f %.0f not cleared: %d of %d destroyed; still up: %s", b, base->pos[0], base->pos[1], base->destroyed, base->members, at ? kinds : "-");
    }
}
static void Watch(uint32_t entity, uint64_t res);
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    int k = SpawnerKind(s->resource);
    if (k >= 0) Push(s->entity, (uint32_t)k);
    Watch(s->entity, s->resource);
}
static void OnKill(HfMod* mod, const HfEventInfo* info, void* user) { Push(((const HfKillEvent*)info)->victim, 1000); }
static void OnWeapon(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfWeaponEvent* w = (const HfWeaponEvent*)info;
    if (w->avatar) atomic_store(&g_avatar, w->avatar);
}
static void OnBughole(HfMod* mod, const HfEventInfo* info, void* user) { Push(((const HfBugholeEvent*)info)->entity, 1001); }


/* ---- 0.3.0: more stims and grenades at points of interest (the loot caches' roll) ------------------------------------ */
#define LOOT_ROLL 0xF3A800u
#define STIMS_WORLD 0xB4CA4C5B922F7965ull    /* pickup/v1/health-pack-world (HD2Runtime's pickup catalogue) */
#define GRENADE_BOX 0x97AF34FBF093409Cull    /* pickup/v1/grenade-box */
typedef uintptr_t (*LootRollFn)(uintptr_t params, uintptr_t unused, uintptr_t context, uintptr_t r9);
static LootRollFn g_lootOriginal;
static atomic_int g_lootCommando = -1;   /* -1 not known yet this mission, 0 no, 1 yes */
static atomic_uint g_lootRolls, g_lootBoosts;
static int LootTables(uintptr_t context, uintptr_t* tables, uint32_t* count)
{
    uintptr_t lg, settings, gen;
    if (!context || !(lg = Pointer(context + 0x18)) || !(settings = Pointer(lg + 0x2D0)) || !(gen = Pointer(settings + 0x68)) ||
        !(*tables = Pointer(gen + 0x90)) || !ReadAt(gen + 0x98, count, 4) || !*count || *count > 32) return 0;
    return 1;
}
/* what a table can drop: 1 stims, 2 grenades (or both) */
static int LootHolds(uintptr_t table, char* list, size_t n)
{
    uintptr_t items = Pointer(table + 0x18);
    uint32_t count = 0;
    int what = 0;
    size_t at = 0;
    if (list && n) list[0] = 0;
    if (!items || !ReadAt(table + 0x20, &count, 4) || count > 256) return 0;
    for (uint32_t i = 0; i < count; i++)
    {
        uint64_t e = 0;
        if (!ReadAt(items + 0x30ull * i, &e, 8)) break;
        if (e == STIMS_WORLD) what |= 1;
        if (e == GRENADE_BOX) what |= 2;
        if (list && at + 20 < n && i < 12) at += (size_t)snprintf(list + at, n - at, " %016llx", (unsigned long long)e);
    }
    return what;
}
static int LootApplies(void)
{
    if (s_everyMission) return 1;
    int c = atomic_load(&g_lootCommando);
    if (c >= 0) return c;
    uintptr_t st;
    uint32_t count = 0;
    if (!List(&st, &count) || !count) return 0;   /* not known yet: this roll as the game has it */
    c = Find(st, count, CALL_IN) >= 0;
    atomic_store(&g_lootCommando, c);
    return c;
}
static uintptr_t LootRoll(uintptr_t params, uintptr_t unused, uintptr_t context, uintptr_t r9)
{
    uintptr_t tables = 0;
    uint32_t count = 0;
    struct { uintptr_t at; int32_t was; } saved[32];
    int ns = 0;
    unsigned roll = atomic_fetch_add(&g_lootRolls, 1);
    if (LootTables(context, &tables, &count))
    {
        int applies = s_poiExtra > 1 && LootApplies();
        for (uint32_t i = 0; i < count; i++)
        {
            uintptr_t t = tables + 0x28ull * i;
            int32_t w = 0, m = 0;
            uint32_t id = 0;
            char list[260];
            int holds = LootHolds(t, list, sizeof list);
            if (!ReadAt(t, &id, 4) || !ReadAt(t + 4, &w, 4) || !ReadAt(t + 8, &m, 4)) continue;
            if (roll < 2) Log("loot roll %u: table %u (%08x) weight %d, minimum difficulty %d,%s%s items:%s", roll, i, id, w, m,
                              holds & 1 ? " STIMS" : "", holds & 2 ? " GRENADES" : "", list);
            if (!applies || !holds || w <= 0) continue;
            int64_t up = (int64_t)w * s_poiExtra;
            int32_t want = up > 0x7FFFFFFF ? 0x7FFFFFFF : (int32_t)up;
            if (hf->write(t + 4, &want, 4)) continue;
            saved[ns].at = t + 4;
            saved[ns].was = w;
            ns++;
        }
        if (roll < 2) Log("loot roll %u: %u tables; %s", roll, count, applies ? "stim / grenade caches raised" : s_poiExtra > 1 ? "not raised (not Commando, or not known yet)" : "poi_extra 1");
        if (ns) atomic_fetch_add(&g_lootBoosts, 1);
    }
    else if (roll < 2) Log("loot roll %u: its tables do not read (context %p)", roll, (void*)context);
    uintptr_t r = g_lootOriginal(params, unused, context, r9);
    for (int k = 0; k < ns; k++) hf->write(saved[k].at, &saved[k].was, 4);   /* as they were: HellForge never sees a raised one */
    return r;
}
static void LootInstall(void)
{
    if (s_poiExtra <= 1) { Log("poi_extra 1: the loot roll is not hooked"); return; }
    if (!hf->code_matches(game + LOOT_ROLL, "4c 8b dc 53 56 57 41 54 41 57 48 81 ec 30 01 00 00"))
    { Log("poi_extra off: game.dll+0x%X is not the loot roll HellForge researched", LOOT_ROLL); return; }
    if (hf->hook(self, game + LOOT_ROLL, (void*)LootRoll, (void**)&g_lootOriginal)) { Log("poi_extra off: the loot roll could not be hooked (the log above says why)"); return; }
    Log("poi_extra: the loot roll hooked (stim and grenade caches x%d in Commando%s)", s_poiExtra, s_everyMission ? " and every mission" : "");
}

/* ---- the mission ---------------------------------------------------------------------------------------------------- */
static int g_inMission, g_checkTries, g_timerTries;
static double g_checkAt, g_timerAt, g_recheckAt;
static void FreezeTimer(void)
{
    int r = hf->freeze_mission_timer(self, 1);
    const char* why = r ? "" : hf->last_log ? hf->last_log(self) : "";
    if (r == HF_ACT_DONE || r == HF_ACT_REQUESTED) { Log("no_timer: mission timer paused (answer %d)", r); g_timerAt = 0; return; }
    if (r == HF_ACT_QUEUED) { Log("no_timer: pause queued for the next game update (answer %d)", r); g_timerAt = 0; return; }
    if (++g_timerTries >= 5) { Log("no_timer: the pause was refused 5 times (%s): the timer runs", why && why[0] ? why : "no reason logged"); g_timerAt = 0; return; }
    Log("no_timer: the pause was refused (answer %d: %s), again in 2 s", r, why && why[0] ? why : "no reason logged");
    g_timerAt = g_now + 2;
}
static void Apply(uintptr_t st, uint32_t count)
{
    char u[16];
    /* 1. the timer */
    if (s_noTimer) FreezeTimer();
    else Log("no_timer: off in the ini: the timer runs");

    if (!GrantPins()) return;
    /* 3. the Super Destroyer call-in */
    if (!s_extraCalls) Log("extra_calls: 0 in the ini: Call In Destroyer left as the mission gave it");
    else
    {
        int slot = Find(st, count, CALL_IN);
        uint32_t uses = 0;
        if (slot < 0) Log("extra_calls: no Call In Destroyer on the record (not a Commando mission): nothing to raise");
        else if (!ReadAt(st + 0x188 + (uint32_t)slot * 0x30u + 4, &uses, 4)) Log("extra_calls: the slot is unreadable");
        else if (uses == UNLIMITED) Log("extra_calls: Call In Destroyer is already unlimited");
        else SetUses(st, slot, uses + (uint32_t)s_extraCalls, "extra_calls: Call In Destroyer");
    }
    /* 2. free Resupply */
    if (!s_resupplyCalls) Log("resupply_calls: 0 in the ini: no free Resupply");
    else
    {
        int slot = Find(st, count, RESUPPLY);
        uint32_t uses = 0;
        if (slot == -2) Log("resupply_calls: the record is unreadable: no free Resupply");
        else if (slot >= 0 && ReadAt(st + 0x188 + (uint32_t)slot * 0x30u + 4, &uses, 4))
        {
            if (uses == UNLIMITED) Log("resupply_calls: Resupply is already on the record (uses %s): nothing granted", UsesText(uses, u, sizeof u));
            else SetUses(st, slot, uses + (uint32_t)s_resupplyCalls, "resupply_calls: Resupply already on the record");
        }
        else
        {
            static const char* const kPkg[] = {"not asked", "still loading", "loaded", "NOT loaded"};
            Log("resupply_calls: Resupply's package is %s", kPkg[g_pkgState]);
            Append(st, count, RESUPPLY, (uint32_t)s_resupplyCalls, "resupply_calls: Resupply");
        }
    }
}
/* the record read until readable; Commando told by its Call In Destroyer slot */
static void Check(void)
{
    uintptr_t st;
    uint32_t count = 0;
    if (!List(&st, &count) || !count)
    {
        if (++g_checkTries >= 30) { g_checkAt = 0; Log("the stratagem record never became readable (30 s): nothing applied"); return; }
        if (g_checkTries == 1 || g_checkTries % 10 == 0) Log("the stratagem record is not readable yet (try %d), again each second", g_checkTries);
        g_checkAt = g_now + 1;
        return;
    }
    g_checkAt = 0;
    LogList("after landing", st, count);
    int slot = Find(st, count, CALL_IN);
    g_commando = slot >= 0;
    if (atomic_load(&g_lootRolls) && atomic_load(&g_lootCommando) < 0) Log("poi_extra: %u loot rolls came before this check (as the game had them)", atomic_load(&g_lootRolls));
    atomic_store(&g_lootCommando, g_commando);
    Log("Commando %s (Call In Destroyer, type %u, %s the record)%s", g_commando ? "DETECTED" : "not detected", CALL_IN,
        g_commando ? "on" : "not on", g_commando ? "" : s_everyMission ? ": every_mission 1, the options apply anyway" : ": nothing applied (every_mission 0)");
    if (!g_commando && !s_everyMission) return;
    HfSession s = {0};
    if (hf->session) hf->session(self, &s);
    if (s.role == HF_SESSION_JOINER) { Log("joined another player's game: nothing applied (host or solo only)"); return; }
    g_applied = 1;
    Apply(st, count);
    if (s_caches) Log("outpost caches: %d spawners in %d bases so far (more are noted as they spawn)", g_nsp, g_nbase);
    g_recheckAt = g_now + 10;
}
/* 10 s later: the record again, to see the game kept what was written */
static void Recheck(void)
{
    uintptr_t st;
    uint32_t count = 0;
    char a[16] = "absent", b[16] = "absent";
    g_recheckAt = 0;
    if (!List(&st, &count)) { Log("10 s later: the stratagem record is unreadable"); return; }
    int d = Find(st, count, CALL_IN), r = Find(st, count, RESUPPLY);
    uint32_t du = 0, ru = 0;
    if (d >= 0 && ReadAt(st + 0x188 + (uint32_t)d * 0x30u + 4, &du, 4)) UsesText(du, a, sizeof a);
    if (r >= 0 && ReadAt(st + 0x188 + (uint32_t)r * 0x30u + 4, &ru, 4)) UsesText(ru, b, sizeof b);
    Log("10 s later (kept?): %u stratagems; Call In Destroyer uses %s; Resupply uses %s", count, a, b);
}

/* ---- 0.5.0: the cash-in (Yui, 2026-11-09; Stratagem Probe 0.5.1) ---------------------------------------------------- */
/* Every base cleared banks ammo boxes (cache_boxes_small / _large); two stratagems call them in: the Outpost Cache, a
 * pod of 8 (uses: the full 8s banked), and the Small Outpost Cache, a pod of the rest (1 use while there is a rest;
 * Yui 2026-11-10: which one is lit tells what a throw brings). In Commando they can be thrown only while the Super Destroyer is overhead (without it the game unlocks only
 * Call In Destroyer and Reinforce, by type): calling the Destroyer is cashing in.
 *   The stratagem: a copy of the B-1 Supply Pack (HellForge copies only pickable stratagems; the Resupply is not one)
 *   whose settings row becomes the Resupply's (found once by its type and stable id), all but its type, id, names, code,
 *   uses and cooldown: the probe's Rack Test D, which brought a Resupply pod. The Resupply's row is hidden from the
 *   loadout (+0x80 1), so the copy is too: it is only granted on the record.
 *   The pods: the Resupply pod (5052ec6a928ccf1a) holds its four boxes in its HellpodRackComponentData (eight 64-byte
 *   slots, spawn count +0x22C). Eight copies of it, 1 to 8 boxes: above 4, slot k takes the box and slot k-4's other 56
 *   bytes once the entities are ready (zero bytes crashed the game, probe 0.5.0); below 4, only the spawn count
 *   (unverified). The Outpost Cache's payload (pod, marker) names the 8-box pod; the Small one's the
 *   pod of the rest, switched after a call once its pod has spawned (or 14 s later), so a pod on its way never changes.
 *   The entries are appended to the record as option 2's Resupply.
 * resupply_to_pods (0.5.0, off by default; the probe's mode 1): a Resupply thrown in Commando lands at the nearest
 * Reinforcement Pods (their power switch, + 6 m) that have had none: the beacon (game.dll+0x6ABDB0, hooked behind
 * HellForge's own) follows the thrown ball (16f397ca5f51f271), so the ball's world position in the scene graph is
 * written every frame for 1.5 s after it activates. */
#define RES_POD 0x5052EC6A928CCF1Aull
#define POD_MARKER 0x73F8498BFFDCF415ull
#define AMMO_BOX 0xA94913CA014F7579ull
#define RESUPPLY_STABLE 0x33BABE96u
#define BALL 0x16F397CA5F51F271ull
#define PODS_SWITCH 0xE8EF41881DAB0E9Full
#define CACHE_COOLDOWN 15.0f
#define BOX(k) {HF_FIELD(HellpodRack, slot##k##_item), HF_U64(AMMO_BOX)}
#define COUNT(n) {HF_FIELD(HellpodRack, spawn_count), HF_U32(n)}
static const HfSet kPod1[] = {COUNT(1), {0}}, kPod2[] = {COUNT(2), {0}}, kPod3[] = {COUNT(3), {0}}, kPod4[] = {COUNT(4), {0}},
                   kPod5[] = {BOX(4), COUNT(5), {0}}, kPod6[] = {BOX(4), BOX(5), COUNT(6), {0}},
                   kPod7[] = {BOX(4), BOX(5), BOX(6), COUNT(7), {0}}, kPod8[] = {BOX(4), BOX(5), BOX(6), BOX(7), COUNT(8), {0}};
static const HfSet* const kPodSets[9] = {NULL, kPod1, kPod2, kPod3, kPod4, kPod5, kPod6, kPod7, kPod8};
static HfEntityDef g_podDef[9];
static char g_podName[9][48];
static HfRef g_pod[9];
static uint64_t g_pay[9][2];
static uint32_t g_cacheType[2];   /* the Outpost Cache (a full pod), the Small Outpost Cache (the rest) */
static int g_bigN = 8;             /* the boxes in a full pod */
/* g_cacheRow (declared above): 0 not seen, 1 hidden (the Resupply's row searched), 2 filled, -1 refused */
static int g_cachePkg;
static uint8_t g_resRow[400];
static uintptr_t g_resAt;          /* where it is: read again at each mission start */
static atomic_int g_scan;         /* 0 not started, 1 running, 2 found, 3 not found */
static int g_payN = 4, g_pending, g_pendingN;   /* g_bank (above): the boxes banked */
static double g_pendingUntil, g_usesAt;
static atomic_uint g_cacheCalls[2], g_cidCalls;
static unsigned g_cacheCallsSeen[2];

static int ResupplyRow(const uint8_t* r)
{
    return Word(r) == RESUPPLY && Word(r + 4) == RESUPPLY_STABLE && Word(r + 0x3C) <= 7 && Word(r + 0x48) >= 1 && Word(r + 0x48) <= 12;
}
/* its payload as researched: (the Resupply pod, its marker) */
static int TakeResupplyRow(uintptr_t at, const char* where)
{
    uint8_t r[400];
    uint64_t pay[2] = {0, 0};
    if (!ReadAt(at, r, 400) || !ResupplyRow(r)) return 0;
    /* 0.5.1: its payload is three (pod, marker) pairs (6 entries; 0.5.0 asked for 2 and refused it) */
    uint32_t n = Word(r + 0xA0);
    if (n < 2 || n > 8 || (n & 1) || !ReadAt((uintptr_t)Quad(r + 0x98), pay, 16) || pay[0] != RES_POD)
    {
        Log("cash-in: the Resupply's row (%s, %p) has payload %u x %016llx, not (the Resupply pod, its marker) pairs: refused", where, (void*)at, n, (unsigned long long)pay[0]);
        return -1;
    }
    memcpy(g_resRow, r, 400);
    g_resAt = at;
    for (int n = 1; n <= 8; n++) { g_pay[n][0] = g_pod[n]; g_pay[n][1] = pay[1]; }
    if (pay[1] != POD_MARKER) Log("cash-in: the Resupply's marker is %016llx (researched %016llx): the game's taken", (unsigned long long)pay[1], (unsigned long long)POD_MARKER);
    Log("cash-in: the Resupply's row found (%s)", where);
    return 1;
}
/* the process's readable memory, scanned once (about 30 s) on a thread of its own */
static DWORD WINAPI ScanThread(LPVOID unused)
{
    static uint8_t buf[(1u << 20) + 400];
    uint8_t* a = (uint8_t*)0x10000;
    MEMORY_BASIC_INFORMATION mi;
    ULONGLONG t0 = GetTickCount64();
    int r = 0;
    while (r <= 0 && (uintptr_t)a < UINT64_C(0x7FFFFFFF0000) && VirtualQuery(a, &mi, sizeof mi) == sizeof mi)
    {
        uint8_t* base = (uint8_t*)mi.BaseAddress;
        size_t size = mi.RegionSize;
        if (mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && size < (512u << 20) &&
            (mi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
            for (size_t off = 0; off < size && r <= 0; off += 1u << 20)
            {
                size_t m = size - off < (1u << 20) + 400 ? size - off : (1u << 20) + 400;
                if (m < 400 || hf->read((uintptr_t)(base + off), buf, m)) continue;
                for (size_t i = 0; i + 400 <= m && r <= 0; i += 4)
                    if (Word(buf + i) == RESUPPLY && Word(buf + i + 4) == RESUPPLY_STABLE && ResupplyRow(buf + i))
                    {
                        int t = TakeResupplyRow((uintptr_t)(base + off + i), "scanned");
                        if (t) r = t;
                    }
            }
        a = base + size;
    }
    Log("cash-in: the memory scan took %.1f s", (double)(GetTickCount64() - t0) / 1000);
    atomic_store(&g_scan, r > 0 ? 2 : 3);
    return 0;
}
static void FindResupplyRow(void)
{
    /* first where the game keeps its list (game.dll+0x37CB600, docs/internals.md "Stratagems"), as pointers or rows */
    uintptr_t list = game + 0x37CB600u, p = Pointer(list + 8u * RESUPPLY);
    if ((p && TakeResupplyRow(p, "the game's list, by pointer") > 0) || TakeResupplyRow(list + 400u * RESUPPLY, "the game's list") > 0)
    { atomic_store(&g_scan, 2); return; }
    atomic_store(&g_scan, 1);
    HANDLE t = CreateThread(NULL, 0, ScanThread, NULL, 0, NULL);
    if (t) { CloseHandle(t); Log("cash-in: looking for the Resupply's row (a memory scan, about 30 s, at the menu)"); }
    else { atomic_store(&g_scan, 3); Log("cash-in: the scan thread could not start"); }
}
static int PayloadOurs(uintptr_t row)
{
    uint64_t p = 0;
    return ReadAt(row + 0x98, &p, 8) && p >= (uint64_t)(uintptr_t)g_pay[1] && p <= (uint64_t)(uintptr_t)g_pay[8];
}
static const char* const kCacheName[2] = {"the Outpost Cache", "the Small Outpost Cache"};
static int FillCacheRow(uintptr_t row, int pod)
{
    uint8_t r[400], own[400];
    if (!ReadAt(row, own, 400)) return 0;
    memcpy(r, g_resRow, 400);
    memcpy(r, own, 8);                    /* type, stable id */
    memcpy(r + 0x28, own + 0x28, 12);     /* name and description texts */
    memcpy(r + 0x40, own + 0x40, 0x14);   /* code, code length, uses */
    float cd = CACHE_COOLDOWN;
    memcpy(r + 0x68, &cd, 4);
    uint64_t p = (uint64_t)(uintptr_t)g_pay[pod];
    uint32_t two = 2;
    memcpy(r + 0x98, &p, 8);
    memcpy(r + 0xA0, &two, 4);
    return !hf->write(row, r, 400);
}
/* the Small Outpost Cache's pod: n boxes (1 to 7, the boxes past the last full pod) */
static void SetPod(int n)
{
    uintptr_t row = g_cacheRow == 2 ? hf->stratagem_row(g_cacheType[1]) : 0;
    if (n < 1) n = 1;
    if (n > 7) n = 7;
    while (n > 4 && !g_pod[n]) n--;   /* a pod without a record of its own is not used */
    uint64_t p = (uint64_t)(uintptr_t)g_pay[n];
    if (row && !hf->write(row + 0x98, &p, 8)) g_payN = n;
}
static int Rest(void) { return g_bank % g_bigN; }
static void DestroyerRow(void);
/* the two stratagems' rows, once the game has loaded its stratagems (menu and mission) */
static void CacheRowFrame(void)
{
    static double next;
    if (g_now < next) return;
    next = g_now + 1;
    if (hf->stratagem_row(g_cacheType[0])) DestroyerRow();   /* the stratagems are loaded */
    if (!g_cacheType[0] || !g_cacheType[1] || g_cacheRow < 0) return;
    uintptr_t row[2] = {hf->stratagem_row(g_cacheType[0]), hf->stratagem_row(g_cacheType[1])};
    if (!row[0] || !row[1]) return;
    if (g_cacheRow == 0)
    {
        uint32_t one = 1;
        for (int i = 0; i < 2; i++)
            Log("cash-in: %s (type %u) %s from the loadout", kCacheName[i], g_cacheType[i], !hf->write(row[i] + 0x80, &one, 4) ? "hidden" : "could NOT be hidden");
        g_cacheRow = 1;
        FindResupplyRow();
    }
    if (g_cacheRow == 1)
    {
        int s = atomic_load(&g_scan);
        if (s == 3) { g_cacheRow = -1; Log("cash-in: the Resupply's row was not found: cleared bases pay out reward ammo rack calls instead (a call for every 4 boxes)"); return; }
        if (s != 2) return;
        g_bigN = 8;
        while (g_bigN > 4 && !g_pod[g_bigN]) g_bigN--;
        if (!FillCacheRow(row[0], g_bigN) || !FillCacheRow(row[1], 4)) { g_cacheRow = -1; Log("cash-in: the caches' rows could not be written: reward ammo rack calls instead"); return; }
        g_cacheRow = 2;
        g_payN = 4;
        float cit = 0;
        memcpy(&cit, g_resRow + 0x54, 4);
        Log("cash-in: the Outpost Cache (%d boxes a pod) and the Small Outpost Cache (the rest) are the Resupply's delivery (call-in %.1f s), %.0f s cooldown",
            g_bigN, cit, CACHE_COOLDOWN);
        return;
    }
    static double pkgNext;
    HoldPackage(g_cacheType[0], "the Outpost Cache", &g_cachePkg, &pkgNext);
    for (int i = 0; i < 2; i++)
        if (!PayloadOurs(row[i]))   /* the game reloaded its stratagems */
        {
            int ok = FillCacheRow(row[i], i ? g_payN : g_bigN);
            Log("cash-in: %s's row was reset by the game: %s", kCacheName[i], ok ? "filled again" : "NOT filled again");
            if (!ok) g_cacheRow = -1;
        }
}
/* at each mission start: the Resupply's row read again (its pointers as the mission has them) and both rows filled */
static void CacheRefill(void)
{
    uint8_t r[400];
    if (g_cacheRow != 2 || !g_resAt || !ReadAt(g_resAt, r, 400) || !ResupplyRow(r)) return;
    memcpy(g_resRow, r, 400);
    for (int i = 0; i < 2; i++)
    {
        uintptr_t row = hf->stratagem_row(g_cacheType[i]);
        if (!row || !FillCacheRow(row, i ? g_payN : g_bigN)) { Log("cash-in: %s's row could not be filled at the mission start", kCacheName[i]); g_cacheRow = -1; return; }
    }
}

/* 0.5.1: the Super Destroyer's window (Yui: how long is it?). Call In Destroyer's row (type 123, stable id f8f90965;
 * the probe: cooldown 120 s, +0x60 6.0, +0x64 5.0) and its entry on the record: at a call, +0x10 / +0x18 / +0x20 move
 * (0.4.1's log: +0x18 = the call + ~119 s, +0x20 = the call + 5 s, on the clock [game+0x3326348]+0x18). The guess: the
 * Destroyer stays while Call In Destroyer cools down (+0x68). Logged: the row at the menu, every call with each field
 * as seconds from it, and when each is reached. destroyer_window > 0 writes that many seconds into +0x68 (a test). */
#define CID_STABLE 0xF8F90965u
static uintptr_t g_cidRow;
static void DestroyerRow(void)
{
    static int done;
    if (done) return;
    uintptr_t p = Pointer(game + 0x37CB600u + 8u * CALL_IN);
    uint8_t r[400];
    if (!p || !ReadAt(p, r, 400)) return;
    done = 1;
    if (Word(r) != CALL_IN || Word(r + 4) != CID_STABLE) { Log("destroyer window: Call In Destroyer's row is not in the game's list (type %u, id %08x)", Word(r), Word(r + 4)); return; }
    g_cidRow = p;
    float f[8];
    memcpy(f, r + 0x54, 4); memcpy(f + 1, r + 0x58, 4); memcpy(f + 2, r + 0x60, 4); memcpy(f + 3, r + 0x64, 4); memcpy(f + 4, r + 0x68, 4); memcpy(f + 5, r + 0x6C, 4);
    Log("destroyer window: Call In Destroyer's row: call-in %.1f s, +0x58 %.1f, +0x60 (linger) %.1f, +0x64 %.1f, cooldown %.1f s, +0x6C %.1f; uses %d, cooldown type %u",
        f[0], f[1], f[2], f[3], f[4], f[5], (int)Word(r + 0x50), Word(r + 0x94));
    char line[400];
    int at = 0;
    for (unsigned o = 0x50; o < 0x190 && at < (int)sizeof line - 24; o += 4)
    {
        float v;
        memcpy(&v, r + o, 4);
        if (isfinite(v) && v >= 1.0f && v <= 3600.0f && v == (float)(int)v) at += snprintf(line + at, sizeof line - at, " +0x%X=%.0f", o, v);
    }
    Log("destroyer window: every whole number of seconds (1 to 3600) in its row:%s", at ? line : " none");
}
static uint64_t ClockNow(void)
{
    uintptr_t o = Pointer(game + 0x3326348u);
    uint64_t t = 0;
    return o && ReadAt(o + 0x18, &t, 8) ? t : 0;
}
static double g_tickRate;           /* clock ticks a second, measured */
static uint64_t g_cidMarks[3];      /* +0x10, +0x18, +0x20 at the last call */
static int g_cidTold[3];
static double g_cidCallAt;          /* g_now at the last Call In Destroyer call (0: none this mission) */
static void DestroyerFrame(void)
{
    static double next, rateAt;
    static uint64_t rateT;
    if (g_now < next) return;
    next = g_now + 0.25;
    uint64_t now = ClockNow();
    if (!now) return;
    if (!g_tickRate)
    {
        if (!rateT) { rateT = now; rateAt = g_now; return; }
        if (g_now - rateAt < 3) return;
        g_tickRate = (double)(now - rateT) / (g_now - rateAt);
        Log("destroyer window: the game's clock runs %.0f ticks a second", g_tickRate);
        if (g_tickRate <= 0) g_tickRate = 0;
        return;
    }
    uintptr_t st;
    uint32_t count = 0;
    if (!List(&st, &count)) return;
    int slot = Find(st, count, CALL_IN);
    uint8_t e[0x30];
    if (slot < 0 || !ReadAt(st + 0x188 + (uint32_t)slot * 0x30u, e, sizeof e)) return;
    uint64_t m[3] = {Quad(e + 0x10), Quad(e + 0x18), Quad(e + 0x20)};
    if (m[0] != g_cidMarks[0] && m[0])
    {
        memcpy(g_cidMarks, m, sizeof m);
        g_cidTold[0] = g_cidTold[1] = g_cidTold[2] = 0;
        g_cidCallAt = g_now - ((double)now - (double)m[0]) / g_tickRate;
        Log("destroyer window: Call In Destroyer called (%.0f s into the mission): its entry +0x18 is %.1f s after the call, +0x20 %.1f s (uses left %u)",
            g_cidCallAt - g_missionAt, ((double)m[1] - (double)m[0]) / g_tickRate, ((double)m[2] - (double)m[0]) / g_tickRate, Word(e + 4));
    }
    for (int k = 1; k < 3; k++)
        if (g_cidMarks[0] && !g_cidTold[k] && now >= g_cidMarks[k])
        {
            g_cidTold[k] = 1;
            Log("destroyer window: +0x%X reached, %.0f s after the call (%.0f s into the mission)", k == 1 ? 0x18 : 0x20, g_now - g_cidCallAt, g_now - g_missionAt);
        }
}
/* 0.5.6 (Yui: both calls lasted exactly 60 s; 0.5.5's bytes never read set): the check itself is hooked instead.
 * game.dll+0x671440 (rcx the stratagem manager, edx the player, r8d the type, r9 an optional reason out) returns 1 for
 * usable; the availability check (+0x66D333), the beacon (+0x6AA4E3) and the HUD (+0xA904BB) call it. In Commando it
 * returns 0 for a stratagem that needs the Destroyer when it is away, or while the lockout above runs (5 s after the
 * drop-in and each call). destroyer_window > 0: from the end of each lockout until that many seconds after it, every
 * stratagem but Call In Destroyer and Reinforce answers usable (their uses and cooldowns are checked before, by the
 * game). The game's own window (60 s) still opens and closes as it does; this only reaches past it. */
typedef uint8_t (*UsableFn)(void*, uint32_t, uint32_t, uint32_t*);
static UsableFn g_usable;
static double g_holdFrom, g_holdUntil;
static unsigned g_holdTold;
static uint8_t Usable(void* mgr, uint32_t player, uint32_t type, uint32_t* reason)
{
    uint8_t r = g_usable(mgr, player, type, reason);
    if (r || !g_applied || type == CALL_IN || type == 124 || g_holdUntil <= 0 || g_now < g_holdFrom || g_now >= g_holdUntil) return r;
    if (reason) *reason = 0;
    if (g_holdTold++ < 3) Log("destroyer window: type %u answered usable, %.0f s after the window opened (destroyer_window %d)", type, g_now - g_holdFrom, s_destroyerWindow);
    return 1;
}
/* (0.5.5: this float is the lockout before the window, not the window; see Usable) 0.5.3: the lock itself (game.dll+0x671440, the availability check;
 * read in the game.dll dump, 2026-11-10): in a mission without the Destroyer (its state's +0x2D), a stratagem other
 * than Call In Destroyer / Reinforce is usable only while the player's float at [game+0x3326340] + 0x90 x slot +
 * 0x1FAE4 is above 0 (the HUD reads +0x1FAE0 / +0x1FAE8 beside it). So: the Destroyer's time left, per player slot.
 * Logged as it rises and runs out; destroyer_window > 0 sets it (and +0x1FAE8) to that many seconds when it rises. */
#define DESTROYER_STATE 0x3326340u
static int g_dsOk = -1;
static float g_dsLast[4];
static double g_dsSince[4], g_dsTold[4];
static void DestroyerTimer(void)
{
    if (g_dsOk < 0)
        g_dsOk = hf->code_matches(game + 0x6715CC, "48 8B 05 6D 4D CB 02 4B 8D 0C FF 48 03 C9 0F 57 C0 F3 0F 10 8C C8 E4 FA 01 00") ? 1 : 0;
    if (!g_dsOk) { static int told; if (!told++) Log("destroyer window: game.dll+0x6715CC is not as researched: the window is not read"); return; }
    uintptr_t base = Pointer(game + DESTROYER_STATE);
    if (!base) return;
    for (int i = 0; i < 4; i++)
    {
        uint8_t b[0x14];
        if (!ReadAt(base + 0x90u * i + 0x1FAD8, b, sizeof b)) return;
        float left, other;
        memcpy(&left, b + 0xC, 4);
        memcpy(&other, b + 0x10, 4);
        if (!isfinite(left)) continue;
        if (left > 0 && g_dsLast[i] <= 0)
        {
            g_dsSince[i] = g_dsTold[i] = g_now;
            Log("destroyer window: player slot %d: a stratagem lockout of %.1f s (+0x1FAE0 %u, +0x1FAE8 %.1f), %.0f s into the mission", i, left, b[8], other, g_now - g_missionAt);
            if (g_usable && s_destroyerWindow > 0 && left <= 30)   /* (the drop-in comes before Commando is told: Usable checks g_applied) */
            {
                g_holdFrom = g_now + left;
                g_holdUntil = g_holdFrom + s_destroyerWindow;
                g_holdTold = 0;
                Log("destroyer window: the window runs to %d s after the lockout (destroyer_window)", s_destroyerWindow);
            }
        }
        else if (left > 0 && g_now - g_dsTold[i] >= 15) { g_dsTold[i] = g_now; Log("destroyer window: slot %d: lockout %.1f s left (+0x1FAE8 %.1f)", i, left, other); }
        else if (left <= 0 && g_dsLast[i] > 0) Log("destroyer window: slot %d: the lockout is over, %.0f s after it began", i, g_now - g_dsSince[i]);
        g_dsLast[i] = left;
    }
    if (g_holdUntil > 0 && g_now >= g_holdUntil) { Log("destroyer window: destroyer_window's %d s are over (%.0f s into the mission)", s_destroyerWindow, g_now - g_missionAt); g_holdUntil = 0; }
}
static void OnDestroyerCall(const HfStratagemEvent* s)
{
    Log("destroyer window: a Call In Destroyer beacon activated (%.0f s into the mission)", g_now - g_missionAt);
}
static void CacheUses(const char* why)
{
    uintptr_t st;
    uint32_t count = 0;
    if (!GrantPins() || !List(&st, &count)) { Log("the caches (%s): the stratagem record is unreadable", why); return; }
    for (int i = 0; i < 2; i++)
    {
        uint32_t uses = 0, want = i ? (Rest() ? 1u : 0u) : (uint32_t)(g_bank / g_bigN);
        char what[112];
        snprintf(what, sizeof what, "%s (%s, %d boxes banked)", kCacheName[i], why, g_bank);
        int slot = Find(st, count, g_cacheType[i]);
        if (slot >= 0 && ReadAt(st + 0x188 + (uint32_t)slot * 0x30u + 4, &uses, 4)) { if (uses != want) SetUses(st, slot, want, what); }
        else if (slot == -1 && want && Append(st, count, g_cacheType[i], want, what)) slot = (int)count++;
        /* 0.5.2 (played 2026-11-10): the HUD kept showing 1 with 3 on the record, and a cache at 0 uses could still be
         * thrown. So the row's uses (+0x50, which the HUD may show) follow the record, and an empty cache is made not
         * granted (+9), as a used-up stratagem is greyed out; granted again when it has a use */
        uintptr_t row = hf->stratagem_row(g_cacheType[i]);
        uint32_t shown = want ? want : 1, was = 0;
        if (row && ReadAt(row + 0x50, &was, 4) && was != shown) hf->write(row + 0x50, &shown, 4);
        if (slot >= 0)
        {
            uint8_t g = want ? 1 : 0, had = 0;
            uintptr_t at = st + 0x188 + (uint32_t)slot * 0x30u + 9;
            if (ReadAt(at, &had, 1) && had != g && !hf->write(at, &g, 1)) Log("%s: %s", what, g ? "usable again (granted)" : "empty: not usable (not granted)");
        }
    }
}
/* n boxes from a cleared base. 1 paid */
static int Bank(int n, const char* why)
{
    if (n <= 0) return 1;
    if (g_cacheRow != 2) return PayOut((n + 3) / 4, why);
    g_bank = g_bank + n > 160 ? 160 : g_bank + n;
    g_boxesBanked += (unsigned)n;
    if (!g_pending && Rest()) SetPod(Rest());
    CacheUses(why);
    return 1;
}
static void OnStratagem(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfStratagemEvent* s = (const HfStratagemEvent*)info;
    for (int i = 0; i < 2; i++) if (g_cacheType[i] && s->type == g_cacheType[i]) atomic_fetch_add(&g_cacheCalls[i], 1);
    if (s->type == CALL_IN) atomic_fetch_add(&g_cidCalls, 1);
}
/* spawns watched for the cash-in and resupply_to_pods (event threads -> the game thread) */
#define WATCHED 64
static uint32_t g_wEnt[WATCHED];
static uint64_t g_wRes[WATCHED];
static atomic_uint g_wHead;
static unsigned g_wTail;
static void Watch(uint32_t entity, uint64_t res)
{
    int ours = res == BALL || res == PODS_SWITCH;
    for (int n = 1; n <= 8 && !ours; n++) ours = g_pod[n] && res == g_pod[n];
    if (!ours) return;
    unsigned i = atomic_fetch_add(&g_wHead, 1) % WATCHED;
    g_wEnt[i] = entity; g_wRes[i] = res;
}
#define MAXSITE 8
typedef struct { uint32_t entity; float pos[3]; int placed, served; } Site;
static Site g_site[MAXSITE];
static int g_nsite;
static uint32_t g_balls[8];
static unsigned g_nballs;
static uint32_t g_moved;
static float g_movedTo[3];
static double g_movedAt;
static uintptr_t NodeOf(uint32_t entity)
{
    uint32_t unit = UnitOf(entity);
    uintptr_t registry = Pointer(exe + 0x1A100F0), objects, object;
    uint32_t count, id, slot = unit & 0x3FFFFF;
    if (!unit || !registry || !ReadAt(registry + 0x98, &count, 4) || slot >= count || !(objects = Pointer(registry + 0x88)) ||
        !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit) return 0;
    return Pointer(object + 0x88);
}
static float Flat(const float a[3], const float b[3]) { float dx = a[0] - b[0], dy = a[1] - b[1]; return sqrtf(dx * dx + dy * dy); }
typedef uint64_t (*ActivateFn)(void*, int32_t, uint32_t, const float*, void*, uint32_t, uint64_t, uint64_t, uint64_t, uint64_t);
static ActivateFn g_activate;
static uint64_t Activate(void* manager, int32_t element, uint32_t thrower, const float* position, void* record, uint32_t type,
                         uint64_t s7, uint64_t s8, uint64_t s9, uint64_t s10)
{
    uint64_t r = g_activate(manager, element, thrower, position, record, type, s7, s8, s9, s10);
    if (type != RESUPPLY || !s_toPods || !g_applied || !g_commando || !position) return r;
    float p[3];
    memcpy(p, position, 12);
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < g_nsite; i++)
        if (g_site[i].placed && !g_site[i].served && Flat(g_site[i].pos, p) < bd) { bd = Flat(g_site[i].pos, p); best = i; }
    if (best < 0) { Log("resupply_to_pods: every Reinforcement Pods site has had its Resupply: this one lands where it was thrown"); return r; }
    uint32_t ball = 0;
    float bp[3] = {0, 0, 0}, closest = 3;
    for (unsigned k = 0; k < 8; k++)
    {
        float q[3];
        if (g_balls[k] && Position(g_balls[k], q) && Dist(q, p) < closest) { closest = Dist(q, p); ball = g_balls[k]; memcpy(bp, q, 12); }
    }
    if (!ball) { Log("resupply_to_pods: the thrown ball was not found at the beacon: the Resupply lands where it was thrown"); return r; }
    g_site[best].served = 1;
    g_movedTo[0] = g_site[best].pos[0] + 6; g_movedTo[1] = g_site[best].pos[1]; g_movedTo[2] = g_site[best].pos[2];
    g_moved = ball; g_movedAt = g_now;
    Log("resupply_to_pods: a Resupply thrown at %.0f %.0f goes to the Reinforcement Pods %d at %.0f %.0f (%.0f m away)", bp[0], bp[1], best,
        g_movedTo[0], g_movedTo[1], Flat(g_movedTo, p));
    return r;
}
static void CashReset(int end)
{
    g_bank = g_pending = 0;
    g_boxesBanked = g_boxesCalled = 0;
    for (int i = 0; i < 2; i++) g_cacheCallsSeen[i] = atomic_load(&g_cacheCalls[i]);
    g_usesAt = 0;
    if (g_cacheRow == 2) SetPod(4);
    if (!end) return;   /* at the start: the pods' switches may have spawned as the mission loaded */
    g_nsite = 0; g_nballs = 0; g_moved = 0;
    memset(g_site, 0, sizeof g_site);
    memset(g_balls, 0, sizeof g_balls);
    g_wTail = atomic_load(&g_wHead);
}
static void CashFrame(void)
{
    while (g_wTail != atomic_load(&g_wHead))
    {
        unsigned i = g_wTail++ % WATCHED;
        uint32_t e = g_wEnt[i];
        uint64_t res = g_wRes[i];
        if (res == BALL) g_balls[g_nballs++ % 8] = e;
        else if (res == PODS_SWITCH) { if (g_nsite < MAXSITE) { memset(&g_site[g_nsite], 0, sizeof g_site[0]); g_site[g_nsite++].entity = e; } }
        else if (g_pending && res == g_pod[g_pendingN])
        {
            g_pending = 0;
            Log("cash-in: the Small Outpost Cache's pod of %d boxes has spawned; %d boxes left in the bank", g_pendingN, g_bank);
            if (Rest()) SetPod(Rest());
        }
    }
    for (int i = 0; i < g_nsite; i++)
        if (!g_site[i].placed && Position(g_site[i].entity, g_site[i].pos))
        {
            g_site[i].placed = 1;
            if (s_toPods) Log("resupply_to_pods: Reinforcement Pods %d at %.0f %.0f %.0f", i, g_site[i].pos[0], g_site[i].pos[1], g_site[i].pos[2]);
        }
    if (g_moved && g_now - g_movedAt < 1.5)
    {
        uintptr_t n = NodeOf(g_moved);
        if (n) hf->write(n + 48, g_movedTo, 12);
    }
    else g_moved = 0;
    for (int i = 0; i < 2; i++)
    {
        unsigned calls = atomic_load(&g_cacheCalls[i]);
        if (calls == g_cacheCallsSeen[i]) continue;
        g_cacheCallsSeen[i] = calls;
        int n = i ? Rest() : g_bigN;
        if (i && !n) n = g_payN;   /* (its use should have been 0) */
        if (n > g_bank) n = g_bank;
        g_bank -= n;
        g_boxesCalled += (unsigned)n;
        g_usesAt = g_now + 1;
        if (i) { g_pending = 1; g_pendingN = g_payN; g_pendingUntil = g_now + 14; }
        Log("cash-in: %s called: a pod of %d boxes; %d left in the bank", kCacheName[i], i ? g_payN : g_bigN, g_bank);
    }
    if (g_pending && g_now >= g_pendingUntil)
    {
        g_pending = 0;
        Log("cash-in: the Small Outpost Cache's pod of %d boxes was not seen to spawn in 14 s; %d boxes left in the bank", g_pendingN, g_bank);
        if (Rest()) SetPod(Rest());
    }
    if (g_usesAt > 0 && g_now >= g_usesAt) { g_usesAt = 0; CacheUses("after a call"); }
    static unsigned cidSeen;
    unsigned cid = atomic_load(&g_cidCalls);
    if (cid != cidSeen) { cidSeen = cid; g_cidCallAt = g_now; OnDestroyerCall(NULL); }
    DestroyerFrame();
    DestroyerTimer();
}
static void FixPods(void)
{
    for (unsigned n = 5; n <= 8; n++)
    {
        HfHellpodRack* r = g_pod[n] ? hf_record(self, g_pod[n], HfHellpodRack) : NULL;
        if (!r) { Log("cash-in: the pod of %u boxes has no record of its own: it is not used", n); g_pod[n] = 0; continue; }
        uint8_t* b = (uint8_t*)r;
        for (unsigned k = 4; k < n; k++) memcpy(b + 64 * k + 8, b + 64 * (k - 4) + 8, 56);
    }
}
static void RegisterCache(void)
{
    for (int n = 1; n <= 8; n++)
    {
        snprintf(g_podName[n], sizeof g_podName[n], "yui/commando-plus/cache-pod-%d", n);
        g_podDef[n].size = sizeof(HfEntityDef);
        g_podDef[n].name = g_podName[n];
        g_podDef[n].base = RES_POD;
        g_podDef[n].set = kPodSets[n];
        g_pod[n] = hf->register_entity(self, &g_podDef[n]);
        if (!g_pod[n]) { Log("cash-in: the pod of %d boxes was refused (the log above says why): no Outpost Cache", n); return; }
    }
    HfStratagem s = {sizeof s};
    s.base = HF_STRATAGEM_B1_SUPPLY_PACK;
    s.name = "yui/commando-plus/outpost-cache";
    s.title = "Outpost Cache";
    s.description = "A pod of 8 ammo boxes, from the boxes banked by the enemy bases you cleared. In Commando, while the Super Destroyer is overhead.";
    s.code = "DUDUDUR";
    s.uses = 1;
    s.cooldown = CACHE_COOLDOWN;
    g_cacheType[0] = hf->add_stratagem(self, &s);
    s.name = "yui/commando-plus/small-outpost-cache";
    s.title = "Small Outpost Cache";
    s.description = "The ammo boxes banked past the last full Outpost Cache (4 with the default settings), in one pod.";
    s.code = "DUDUDUL";
    g_cacheType[1] = hf->add_stratagem(self, &s);
    Log("cash-in: the Outpost Cache is stratagem type %u (D U D U D U R), the Small Outpost Cache type %u (D U D U D U L)%s", g_cacheType[0], g_cacheType[1],
        g_cacheType[0] && g_cacheType[1] ? "" : ": REFUSED (the log above says why)");
}

static void Tick(HfMod* mod, float dt, void* user)
{
    g_now += dt > 0 && dt < 1 ? dt : 0;
    if (g_ready != 1) return;
    ResupplyPackage();
    CacheRowFrame();
    int m = hf->game_state(self) == HF_STATE_MISSION;
    if (m != g_inMission)
    {
        g_inMission = m;
        if (!m)
        {
            CachesSummary("mission end");
            CachesReset();
            CashReset(1);
            g_ringTail = atomic_load(&g_ringHead);
            if (atomic_load(&g_lootRolls)) Log("mission end: %u loot rolls, %u with stim / grenade caches raised", atomic_load(&g_lootRolls), atomic_load(&g_lootBoosts));
            atomic_store(&g_lootCommando, -1);
            atomic_store(&g_lootRolls, 0);
            atomic_store(&g_lootBoosts, 0);
        }   /* (not at the start: the base's spawners spawn while it loads) */
        g_checkTries = g_applied = g_commando = g_timerTries = 0;
        g_timerAt = g_recheckAt = 0;
        g_checkAt = m ? g_now + 8 : 0;
        if (m) { CashReset(0); CacheRefill(); g_missionAt = g_now; memset(g_cidMarks, 0, sizeof g_cidMarks); memset(g_dsLast, 0, sizeof g_dsLast); g_cidCallAt = 0; g_holdFrom = g_holdUntil = 0; }
        Log(m ? "mission start: the stratagem record is read in 8 s" : "mission end");
    }
    if (!m) return;
    if (g_checkAt > 0 && g_now >= g_checkAt) Check();
    if (g_timerAt > 0 && g_now >= g_timerAt) FreezeTimer();
    if (g_recheckAt > 0 && g_now >= g_recheckAt) Recheck();
    CachesFrame();
    CashFrame();
}

static void EntitiesReady(HfMod* mod, HfEvent event, void* user) { FixPods(); }
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gs = 0, xs = 0;
    game = hf->module_base("game.dll", &gs);
    exe = hf->module_base("helldivers2.exe", &xs);
    if (!game || !exe || gs != 74727424u || xs != 60719104u) { g_ready = -1; Log("REFUSED: not build " BUILD "'s modules: nothing applied"); return; }
    g_ready = 1;
    Log("ready: game.dll and helldivers2.exe are build " BUILD "'s");
    LootInstall();
    if (s_destroyerWindow > 0)
    {
        if (!hf->code_matches(game + 0x671440, "48 89 5C 24 18 48 89 6C 24 20 56 57 41 56 48 83 EC 20 3B 15 DC 27 E1 02 4D 8B F1 41 8B E8 8B DA 48 8B F1"))
            Log("destroyer_window: REFUSED: the stratagem check (game.dll+0x671440) is not the reviewed code");
        else if (hf->hook_ex(self, game + 0x671440, (void*)Usable, (void**)&g_usable, 0) || !g_usable)
        { g_usable = NULL; Log("destroyer_window: REFUSED: the stratagem check could not be hooked"); }
        else Log("destroyer_window: %d s: the stratagem check hooked", s_destroyerWindow);
    }
    if (s_toPods)
    {
        if (!hf->code_matches(game + 0x6ABDB0, "48 89 5c 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8d ac 24 b0 fc ff ff 48 81 ec 50 04 00 00"))
            Log("resupply_to_pods: REFUSED: ActivateStratagemBeacon (game.dll+0x6ABDB0) is not the reviewed code");
        else if (hf->hook_ex(self, game + 0x6ABDB0, (void*)Activate, (void**)&g_activate, 0) || !g_activate)
        { g_activate = NULL; Log("resupply_to_pods: REFUSED: ActivateStratagemBeacon could not be hooked"); }
        else Log("resupply_to_pods: ActivateStratagemBeacon hooked");
    }
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 15 || api->size < offsetof(HfApi, stats_source) + sizeof api->stats_source) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    Log("Commando Plus " VERSION " (TEST BUILD; SDK %u, game " BUILD ")", api->version);
    LoadSettings();
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) { Log("REFUSED: the game-loaded event"); return 1; }
    RegisterCache();
    api->on(mod, HF_EVENT_ENTITIES_READY, EntitiesReady, NULL);
    if (!api->on_game_thread(mod, Tick, NULL)) Log("the game thread could not be hooked: the mod stays off");
    if (!api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) || !api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) ||
        !api->subscribe(mod, HF_ON_BUGHOLE, OnBughole, NULL, 0) || !api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) ||
        !api->subscribe(mod, HF_ON_STRATAGEM, OnStratagem, NULL, 0))
        Log("outpost caches: the spawn / kill / bug hole events are unavailable: no caches");
    return 0;
}
