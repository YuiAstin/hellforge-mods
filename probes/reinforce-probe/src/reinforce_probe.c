/* Reinforce Probe (yui.reinforce-probe): research for Commando Plus. Reads only; host or solo.
 *
 * Commando has no Reinforce budget of the usual kind: the Reinforcement Pods objective (pods in the world, a code to
 * connect them) unlocks 3 reinforcements, and fallen Helldivers drop from the nearest pods. Where is that count kept,
 * and what are the pods? This probe takes snapshots of the memory the count most likely lives in and logs the small
 * whole numbers that changed between two snapshots:
 *
 *   regions   the local player's record ([game+0x347CE50], 0x1690 per peer; HD2Runtime's research), the block after
 *             the records (team-wide fields near their count, +0x2D200), the session object [game+0x347CEF0] around
 *             +0xB398, and the first 0x400 bytes of every heap object a global of game.dll's two manager blocks points
 *             to (game+0x3326000..0x3327000 and +0x347C000..0x347E000)
 *   when      15 s after the mission starts (the baseline), 1 s after a player goes down, 1 s after one lands (dropped
 *             in or reinforced), 1 s after any stratagem's beacon, and on Ctrl+Alt+F2 (press it just before and just
 *             after connecting the pods)
 *   logged    each dword that went from one whole number 0..200 to another, by at most 20; at mission end, the
 *             addresses whose every change came at those moments, with their history (the count should read -1 per
 *             reinforcement and +3 at the pods)
 *
 * Ctrl+Alt+F2 also lists what stands within 30 m of you (entity resources, from the spawns seen this mission): stand
 * at the pods and press it to name them. Credit: the records' and the entity / unit lookups' layouts are HD2Runtime's
 * research (SkyeShade), as Commando Plus and Lost Contact use them; nothing of its code is copied. */
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "hellforge_mod.h"

#define BUILD "25480438"
#define VERSION "0.1.2"
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
static float Dist(const float a[3], const float b[3]) { float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2]; return sqrtf(dx * dx + dy * dy + dz * dz); }

/* ---- snapshots -------------------------------------------------------------------------------------------------- */
#define OBJ 0x400u
#define MAXREG 1600
typedef struct { uint32_t key; uintptr_t base; uint32_t size, at; } Region;   /* at: its bytes in the snapshot's buffer */
typedef struct { Region reg[MAXREG]; int n; uint8_t* buf; size_t used, cap; char label[48]; double t; } Snap;
static Snap g_snap[2];
static int g_cur = -1;   /* the newest snapshot, -1 none this mission */
static void AddRegion(Snap* s, uint32_t key, uintptr_t base, uint32_t size)
{
    if (s->n >= MAXREG || !base) return;
    if (s->used + size > s->cap)
    {
        size_t cap = s->cap ? s->cap * 2 : (size_t)4 << 20;
        while (cap < s->used + size) cap *= 2;
        uint8_t* b = (uint8_t*)realloc(s->buf, cap);
        if (!b) return;
        s->buf = b; s->cap = cap;
    }
    if (!ReadAt(base, s->buf + s->used, size)) return;
    Region* r = &s->reg[s->n++];
    r->key = key; r->base = base; r->size = size; r->at = (uint32_t)s->used;
    s->used += size;
}
static uintptr_t LocalRecord(uintptr_t* recs)
{
    uintptr_t rs = Pointer(game + 0x347CE50u), peerObj = Pointer(game + 0x347CEF0u);
    uint64_t me = 0;
    uint32_t n = 0;
    *recs = rs;
    if (!rs || !peerObj || !ReadAt(peerObj + 0xB398, &me, 8) || !me || !ReadAt(rs + 0x2D200, &n, 4) || !n || n > 32) return 0;
    for (uint32_t r = 0; r < n; r++)
    {
        uint64_t peer = 0;
        uintptr_t rec = rs + (uintptr_t)r * 0x1690u;
        if (ReadAt(rec, &peer, 8) && peer == me) return rec;
    }
    return 0;
}
/* keys: 1 the record, 2 after the records, 3 the session object, 0x10000000 | global offset: an object a global points to */
static void Take(Snap* s, const char* label)
{
    s->n = 0; s->used = 0;
    snprintf(s->label, sizeof s->label, "%s", label);
    s->t = g_now;
    uintptr_t recs = 0, rec = LocalRecord(&recs), peerObj = Pointer(game + 0x347CEF0u);
    if (rec) AddRegion(s, 1, rec, 0x1690);
    if (recs) AddRegion(s, 2, recs + 0x2D200 - 0x40, 0x400);
    if (peerObj) AddRegion(s, 3, peerObj + 0xB000, 0x800);
    static const struct { uint32_t from, to; } blocks[] = {{0x3326000, 0x3327000}, {0x347C000, 0x347E000}};
    for (unsigned b = 0; b < 2; b++)
        for (uint32_t off = blocks[b].from; off < blocks[b].to; off += 8)
        {
            uintptr_t p = Pointer(game + off);
            if (p && (p < game || p >= game + 0x5000000)) AddRegion(s, 0x10000000u | off, p, OBJ);
        }
}
static const Region* FindRegion(const Snap* s, uint32_t key)
{
    for (int i = 0; i < s->n; i++) if (s->reg[i].key == key) return &s->reg[i];
    return NULL;
}
static void KeyName(uint32_t key, char* out, size_t n)
{
    if (key == 1) snprintf(out, n, "record");
    else if (key == 2) snprintf(out, n, "records+0x2D1C0");
    else if (key == 3) snprintf(out, n, "session+0xB000");
    else snprintf(out, n, "[game+0x%X]", key & 0xFFFFFFF);
}

/* ---- the addresses that changed, and when ------------------------------------------------------------------------ */
#define TRACK 4096
typedef struct { uint32_t key, off; int changes, outside; char hist[160]; } Track;
static Track g_track[TRACK];
static int g_ntrack;
static Track* TrackOf(uint32_t key, uint32_t off)
{
    for (int i = 0; i < g_ntrack; i++) if (g_track[i].key == key && g_track[i].off == off) return &g_track[i];
    if (g_ntrack >= TRACK) return NULL;
    Track* t = &g_track[g_ntrack++];
    memset(t, 0, sizeof *t);
    t->key = key; t->off = off;
    return t;
}
static void Diff(const Snap* a, const Snap* b, int moment)
{
    int lines = 0, total = 0;
    for (int i = 0; i < b->n; i++)
    {
        const Region* rb = &b->reg[i];
        const Region* ra = FindRegion(a, rb->key);
        if (!ra || ra->base != rb->base || ra->size != rb->size) continue;
        const uint8_t *pa = a->buf + ra->at, *pb = b->buf + rb->at;
        for (uint32_t o = 0; o + 4 <= rb->size; o += 4)
        {
            int32_t va = (int32_t)Word(pa + o), vb = (int32_t)Word(pb + o);
            /* 0.1.2: -1 too (unlimited uses read -1; a count may start or end there) */
            if (va == vb || va < -1 || vb < -1 || va > 200 || vb > 200 || (va >= 0 && vb >= 0 && abs(vb - va) > 20)) continue;
            total++;
            char k[40];
            KeyName(rb->key, k, sizeof k);
            if (moment && lines++ < 60) Log("  %s +0x%X: %d -> %d", k, o, va, vb);
            Track* t = TrackOf(rb->key, o);
            if (!t) continue;
            t->changes++;
            if (!moment) t->outside++;
            size_t l = strlen(t->hist);
            if (l + 24 < sizeof t->hist) snprintf(t->hist + l, sizeof t->hist - l, " %s:%+d", b->label, vb - va);
        }
    }
    if (moment) Log("  %d changed (%s -> %s, %.1f s)%s", total, a->label, b->label, b->t - a->t, lines > 60 ? "; the first 60 above" : "");
}
/* a snapshot, and what changed since the last one; moment 0: a periodic one (changes there are noise) */
static void Snapshot(const char* label, int moment)
{
    int next = g_cur < 0 ? 0 : 1 - g_cur;
    Take(&g_snap[next], label);
    if (moment) Log("snapshot \"%s\": %d regions, %u KB", label, g_snap[next].n, (unsigned)(g_snap[next].used >> 10));
    if (g_cur >= 0) Diff(&g_snap[g_cur], &g_snap[next], moment);
    g_cur = next;
}
static void TrackSummary(void)
{
    int n = 0;
    Log("mission end: %d addresses changed; those that changed only at the moments (down, land, beacon, key), most changes first:", g_ntrack);
    for (int want = 12; want >= 1 && n < 40; want--)
        for (int i = 0; i < g_ntrack && n < 40; i++)
        {
            const Track* t = &g_track[i];
            if (t->outside || t->changes != want) continue;
            char k[40];
            KeyName(t->key, k, sizeof k);
            Log("  %s +0x%X: %d changes:%s", k, t->off, t->changes, t->hist);
            n++;
        }
}

/* ---- what is near you ------------------------------------------------------------------------------------------- */
#define SEEN 16384
static uint32_t g_seenEnt[SEEN];
static uint64_t g_seenRes[SEEN];
static atomic_uint g_nseen;
static atomic_uint g_avatar;
static void Nearby(void)
{
    float me[3], p[3];
    uint32_t a = atomic_load(&g_avatar);
    if (!a || !Position(a, me)) { Log("near you: your Helldiver's position does not read"); return; }
    struct { uint64_t res; int count; float nearest; uint32_t entity; } g[40];
    int ng = 0;
    unsigned n = atomic_load(&g_nseen);
    if (n > SEEN) n = SEEN;
    for (unsigned i = 0; i < n; i++)
    {
        uint32_t e = g_seenEnt[i];
        if (e == a || !Position(e, p)) continue;
        float d = Dist(me, p);
        if (d > 30) continue;
        int k;
        for (k = 0; k < ng; k++) if (g[k].res == g_seenRes[i]) break;
        if (k == ng) { if (ng == 40) continue; g[ng].res = g_seenRes[i]; g[ng].count = 0; g[ng].nearest = 1e9f; ng++; }
        g[k].count++;
        if (d < g[k].nearest) { g[k].nearest = d; g[k].entity = e; }
    }
    Log("near you (30 m, at %.0f %.0f %.0f): %d kinds", me[0], me[1], me[2], ng);
    for (int k = 0; k < ng; k++)
        Log("  %016llx x%d, nearest %.1f m (entity %u)", (unsigned long long)g[k].res, g[k].count, (double)g[k].nearest, g[k].entity);
}

/* ---- events: from their threads to the game thread ---------------------------------------------------------------- */
#define PEND 16
static char g_pendLabel[PEND][48];
static double g_pendAt[PEND];
static atomic_uint g_pendHead;
static unsigned g_pendTail;
static void Pend(const char* label, double delay)
{
    unsigned h = atomic_fetch_add(&g_pendHead, 1) % PEND;
    snprintf(g_pendLabel[h], sizeof g_pendLabel[h], "%s", label);
    g_pendAt[h] = g_now + delay;
}
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* e = (const HfSpawnEvent*)info;
    unsigned i = atomic_fetch_add(&g_nseen, 1);
    if (i < SEEN) { g_seenEnt[i] = e->entity; g_seenRes[i] = e->resource; }
}
static void OnWeapon(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfWeaponEvent* w = (const HfWeaponEvent*)info;
    if (w->avatar) atomic_store(&g_avatar, w->avatar);
}
static void OnDown(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    Log("player down: peer %016llx entity %u%s", (unsigned long long)p->peer, p->entity, p->local ? " (you)" : "");
    Pend(p->local ? "you down" : "player down", 1.0);
}
static void OnLand(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    Log("player on the ground: peer %016llx entity %u%s", (unsigned long long)p->peer, p->entity, p->local ? " (you)" : "");
    if (p->local && p->entity) atomic_store(&g_avatar, p->entity);
    Pend(p->local ? "you landed" : "player landed", 1.0);
}
static void OnStratagem(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfStratagemEvent* s = (const HfStratagemEvent*)info;
    char l[48];
    Log("beacon: stratagem type %u by entity %u at %.0f %.0f %.0f", s->type, s->thrower, s->position[0], s->position[1], s->position[2]);
    snprintf(l, sizeof l, "beacon %u", s->type);
    Pend(l, 1.0);
}

/* ---- the game thread ---------------------------------------------------------------------------------------------- */
static int g_inMission;
static double g_baselineAt;
static int Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
static void Tick(HfMod* mod, float dt, void* user)
{
    g_now += dt > 0 && dt < 1 ? dt : 0;
    if (g_ready != 1) return;
    int m = hf->game_state(self) == HF_STATE_MISSION;
    if (m != g_inMission)
    {
        g_inMission = m;
        if (m)
        {
            Log("mission start: the baseline snapshot in 15 s; Ctrl+Alt+F2 for one now (and what is near you)");
            g_cur = -1; g_ntrack = 0; g_baselineAt = g_now + 15;
            g_pendTail = atomic_load(&g_pendHead);
        }
        else
        {
            if (g_cur >= 0) TrackSummary();
            Log("mission end");
            g_cur = -1;
            atomic_store(&g_nseen, 0);
            atomic_store(&g_avatar, 0);
        }
    }
    if (!m) return;
    if (g_baselineAt > 0 && g_now >= g_baselineAt) { g_baselineAt = 0; Snapshot("baseline", 1); }
    while (g_pendTail != atomic_load(&g_pendHead))
    {
        unsigned i = g_pendTail % PEND;
        if (g_now < g_pendAt[i]) break;
        g_pendTail++;
        if (g_cur >= 0 || g_baselineAt == 0) Snapshot(g_pendLabel[i], 1);
    }
    static int was;
    int key = Down(VK_F2) && Down(VK_CONTROL) && Down(VK_MENU);
    if (key && !was) { Snapshot("key", 1); Nearby(); }
    was = key;
    /* every 20 s a quiet snapshot: what changes there is not the count */
    static double quiet;
    if (g_cur >= 0 && g_now >= quiet) { quiet = g_now + 20; Snapshot("quiet", 0); }
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gs = 0, xs = 0;
    game = hf->module_base("game.dll", &gs);
    exe = hf->module_base("helldivers2.exe", &xs);
    if (!game || !exe || gs != 74727424u || xs != 60719104u) { g_ready = -1; Log("REFUSED: not build " BUILD "'s modules"); return; }
    g_ready = 1;
    Log("ready (build " BUILD ")");
}
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 15) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    Log("Reinforce Probe " VERSION " (research, reads only)");
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    if (!api->on_game_thread(mod, Tick, NULL)) { Log("the game thread could not be hooked"); return 1; }
    if (!api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) || !api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) ||
        !api->subscribe(mod, HF_ON_PLAYER_DOWN, OnDown, NULL, 0) || !api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnLand, NULL, 0) ||
        !api->subscribe(mod, HF_ON_STRATAGEM, OnStratagem, NULL, 0))
        Log("some events are unavailable");
    return 0;
}
