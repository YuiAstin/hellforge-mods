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
#define VERSION "0.2.0"
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
static int s_caches = 1, s_cacheSmall = 1, s_cacheLarge = 2, s_cacheReward = 0, s_baseRadius = 40;
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
    Log("settings: outpost_caches %d (small base %d, large base %d calls of %s), base_radius %d m", s_caches, s_cacheSmall, s_cacheLarge,
        s_cacheReward ? "Resupply" : "the reward ammo rack", s_baseRadius);
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
typedef struct { uint32_t entity; int kind, base, dead, placed; float pos[3]; } Spawner;
typedef struct { int members, alive, large, paid; float pos[3]; } Base;
static Spawner g_sp[SPAWNERS];
static Base g_base[BASES];
static int g_nsp, g_nbase;
static unsigned g_basesPaid, g_callsPaid;
static void CachesReset(void) { g_nsp = g_nbase = 0; g_basesPaid = g_callsPaid = 0; }
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
static int g_applied;
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
            for (int j = 0; j < g_nsp; j++) if (g_sp[j].entity == e && !g_sp[j].dead) { g_sp[j].dead = k == 1001 ? 2 : 1; }
    }
    static double next;
    if (g_now < next) return;
    next = g_now + 1;
    /* placed into bases once their position reads; dead when gone or at 0 health */
    for (int j = 0; j < g_nsp; j++)
    {
        Spawner* sp = &g_sp[j];
        if (!sp->placed && !sp->dead && Position(sp->entity, sp->pos))
        {
            float r2 = (float)s_baseRadius * (float)s_baseRadius;
            int b = -1;
            for (int q = 0; q < g_nsp && b < 0; q++)
            {
                const Spawner* o = &g_sp[q];
                float dx = o->pos[0] - sp->pos[0], dy = o->pos[1] - sp->pos[1], dz = o->pos[2] - sp->pos[2];
                if (q != j && o->placed && o->base >= 0 && dx * dx + dy * dy + dz * dz <= r2 && !g_base[o->base].paid) b = o->base;
            }
            if (b < 0 && g_nbase < BASES) { b = g_nbase++; memset(&g_base[b], 0, sizeof g_base[b]); memcpy(g_base[b].pos, sp->pos, 12); }
            if (b < 0) continue;
            sp->placed = 1; sp->base = b;
            g_base[b].members++;
            if (kSpawners[sp->kind].large) g_base[b].large = 1;
        }
        if (sp->placed && !sp->dead)
        {
            int32_t hp = 1;
            if (!UnitOf(sp->entity)) sp->dead = 3;
            else if (Health(sp->entity, &hp) && hp <= 0) sp->dead = 4;
        }
    }
    /* a base whose spawners are all down pays out (once, when the options apply) */
    for (int b = 0; b < g_nbase; b++)
    {
        Base* base = &g_base[b];
        int alive = 0;
        for (int j = 0; j < g_nsp; j++) if (g_sp[j].base == b && !g_sp[j].dead) alive++;
        base->alive = alive;
        if (base->paid || alive || !base->members) continue;
        base->paid = 1;
        int large = base->large || base->members >= 3, n = large ? s_cacheLarge : s_cacheSmall;
        char why[64];
        snprintf(why, sizeof why, "a %s base of %d spawner%s cleared", large ? "large" : "small", base->members, base->members == 1 ? "" : "s");
        if (!s_caches || !g_applied) { Log("%s at %.0f %.0f: no cache (%s)", why, base->pos[0], base->pos[1], !s_caches ? "outpost_caches 0" : "the options do not apply this mission"); continue; }
        if (PayOut(n, why)) { g_basesPaid++; g_callsPaid += (unsigned)n; Log("%s at %.0f %.0f: %d free call%s", why, base->pos[0], base->pos[1], n, n == 1 ? "" : "s"); }
    }
}
static void CachesSummary(const char* why)
{
    int alive = 0;
    for (int b = 0; b < g_nbase; b++) if (!g_base[b].paid) alive++;
    Log("%s: %d spawners in %d bases (%d not cleared); %u bases paid out, %u free calls", why, g_nsp, g_nbase, alive, g_basesPaid, g_callsPaid);
}
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    int k = SpawnerKind(s->resource);
    if (k >= 0) Push(s->entity, (uint32_t)k);
}
static void OnKill(HfMod* mod, const HfEventInfo* info, void* user) { Push(((const HfKillEvent*)info)->victim, 1000); }
static void OnBughole(HfMod* mod, const HfEventInfo* info, void* user) { Push(((const HfBugholeEvent*)info)->entity, 1001); }

/* ---- the mission ---------------------------------------------------------------------------------------------------- */
static int g_inMission, g_checkTries, g_timerTries;
static double g_checkAt, g_timerAt, g_recheckAt;
static int g_commando;
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

static void Tick(HfMod* mod, float dt, void* user)
{
    g_now += dt > 0 && dt < 1 ? dt : 0;
    if (g_ready != 1) return;
    ResupplyPackage();
    int m = hf->game_state(self) == HF_STATE_MISSION;
    if (m != g_inMission)
    {
        g_inMission = m;
        if (!m) { CachesSummary("mission end"); CachesReset(); g_ringTail = atomic_load(&g_ringHead); }   /* (not at the start: the base's spawners spawn while it loads) */
        g_checkTries = g_applied = g_commando = g_timerTries = 0;
        g_timerAt = g_recheckAt = 0;
        g_checkAt = m ? g_now + 8 : 0;
        Log(m ? "mission start: the stratagem record is read in 8 s" : "mission end");
    }
    if (!m) return;
    if (g_checkAt > 0 && g_now >= g_checkAt) Check();
    if (g_timerAt > 0 && g_now >= g_timerAt) FreezeTimer();
    if (g_recheckAt > 0 && g_now >= g_recheckAt) Recheck();
    CachesFrame();
}

static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gs = 0, xs = 0;
    game = hf->module_base("game.dll", &gs);
    exe = hf->module_base("helldivers2.exe", &xs);
    if (!game || !exe || gs != 74727424u || xs != 60719104u) { g_ready = -1; Log("REFUSED: not build " BUILD "'s modules: nothing applied"); return; }
    g_ready = 1;
    Log("ready: game.dll and helldivers2.exe are build " BUILD "'s");
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 15 || api->size < offsetof(HfApi, stats_source) + sizeof api->stats_source) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    Log("Commando Plus " VERSION " (TEST BUILD, untested in game; SDK %u, game " BUILD ")", api->version);
    LoadSettings();
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) { Log("REFUSED: the game-loaded event"); return 1; }
    if (!api->on_game_thread(mod, Tick, NULL)) Log("the game thread could not be hooked: the mod stays off");
    if (!api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) || !api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) ||
        !api->subscribe(mod, HF_ON_BUGHOLE, OnBughole, NULL, 0))
        Log("outpost caches: the spawn / kill / bug hole events are unavailable: no caches");
    return 0;
}
