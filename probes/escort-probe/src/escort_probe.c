/* Escort Probe (yui.escort-probe): research for Andoris's bodyguards. Can a mod spawn the game's own SEAF trooper
 * (cha_battlefront_seaf, resource 0x75F7E96AF7DCD303; HD2Runtime's entity list) beside your Helldiver, keep it with
 * you, and does it fight and take hits for you?
 *
 * HELLBOT Companion by nicerjd shows a SEAF-based companion works in HellForge. None of its code or assets is used here;
 * this is our own way: the kit base's spawn by resource (RequestEntitySpawn, after its package loads) and the
 * destination setter the pose hold uses (game.dll+0x5B36C0) to lead it.
 *
 * In a mission (solo or host):
 *   Insert   a trooper 3 m beside you (at most 2: a third takes the oldest away, as her turrets do)
 *   Home     following on / off (on: one more than 5 m away is sent to a point 2.5 m behind you, every second)
 *   Delete   takes them all away
 * Every 2 s the log has each one's distance, health and whether it walks; and the hits each takes (from what, how much),
 * and when one dies. Spawns within 3 s of one of ours are logged too (does it come with a squad?).
 *
 * 0.2.0, finding the game's own "follow me": the SEAF answer the comm wheel's Follow (comm_follow), so the order lands
 * somewhere in their records. For the newest escort the probe snapshots its behaviour record ([game+0x3326740], 0x1F8
 * bytes; +0x68 the entity it perceives through: its squad) and its squad's record (AiSquadManager [game+0x33265A0],
 * 0x2510 bytes) four times a second, and logs every 4-byte word that changes, after a 4 s baseline that marks the words
 * that change by themselves (timers, positions) as noise. A value that is your Helldiver's entity, unit or network id is
 * flagged. Following now starts OFF. Test: Insert, stand still 6 s, comm wheel Follow, walk 10 s; Hold; Move. */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <windows.h>
#include "hellforge_mod.h"
#include "hellforge_content.h"
#include "hellforge_functions.h"
#include "hellforge_globals.h"

#define BUILD "25480438"
#define VERSION "0.2.0"
#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"
#include "kitbase/kb_world.h"
#include "kitbase/kb_spawn.h"

#define SEAF UINT64_C(0x75F7E96AF7DCD303)
#define ESCORTS 2

typedef struct Escort { uint32_t entity, net; double since; int hits, taken, maxHp, lastHp, dead, sent; } Escort;
static Escort g_esc[ESCORTS];
static void Watch(uint32_t e);
static int g_follow = 0, g_want, g_spawns;
static double g_wantSince, g_lastSpawnAt = -100;

static void Line(Escort* e, const char* why)
{
    float me[3], p[3];
    int32_t hp = -1;
    uint32_t life = 9;
    double dist = PlayerPose(me, NULL) && EntityPos(e->entity, p) ? sqrt(Dist2(me, p)) : -1;
    HealthOf(e->entity, &hp, &life);
    Log("%s: escort %u: %.1f m from you, health %d of %d (life %u), walks %d, %d hits (%d damage), sent to you %d times, %.0f s old", why, e->entity, dist, hp,
        e->maxHp, life, Walks(e->entity), e->hits, e->taken, e->sent, g_now - e->since);
}
static void Remove(Escort* e, const char* why)
{
    if (!e->entity) return;
    Line(e, why);
    if (!e->dead) Log("  taken away: %s", KbRemoveEntity(e->entity, SEAF) ? "yes" : "refused (not in a mission, or the id is no longer a trooper)");
    memset(e, 0, sizeof *e);
}
static void SpawnFrame(void)
{
    if (!g_want) return;
    int r = KbLoadFor(SEAF);
    if (r != HF_PACKAGE_RESIDENT)
    {
        if (!r) { Log("the trooper's package was refused"); g_want = 0; }
        else if (g_now - g_wantSince > 8) { Log("the trooper's package did not load in 8 s"); g_want = 0; }
        return;
    }
    g_want = 0;
    float me[3], aim[3], at[3];
    if (!PlayerPose(me, aim)) { Log("Insert: your Helldiver is not readable"); return; }
    at[0] = me[0] - aim[1] * 3; at[1] = me[1] + aim[0] * 3; at[2] = me[2] + 0.5f;   /* 3 m to your left */
    int slot = -1;
    for (int i = 0; i < ESCORTS; i++) if (!g_esc[i].entity) { slot = i; break; }
    if (slot < 0)
    {
        slot = g_esc[0].since <= g_esc[1].since ? 0 : 1;
        Remove(&g_esc[slot], "a third one asked for: the oldest goes");
    }
    uint32_t e = KbSpawnEntity(SEAF, at, "a SEAF trooper");
    if (!e) return;
    Escort* x = &g_esc[slot];
    memset(x, 0, sizeof *x);
    x->entity = e; x->since = g_now;
    int32_t hp = 0; uint32_t life = 0;
    if (HealthOf(e, &hp, &life)) x->maxHp = x->lastHp = hp;
    g_spawns++;
    g_lastSpawnAt = g_now;
    Watch(e);
    Log("Insert: a SEAF trooper, entity %u, at %.1f %.1f %.1f; health %d; friend table says %s; walks %d", e, (double)at[0], (double)at[1], (double)at[2], hp,
        FriendRes(SEAF) ? "a friend" : "NOT a friend", Walks(e));
}
static void FollowFrame(void)
{
    static double next, report;
    float me[3], aim[3], p[3];
    if (g_now < next) return;
    next = g_now + 1.0;
    int say = g_now >= report;
    if (say) report = g_now + 2.0;
    if (!PlayerPose(me, aim)) return;
    for (int i = 0; i < ESCORTS; i++)
    {
        Escort* e = &g_esc[i];
        if (!e->entity || e->dead) continue;
        int32_t hp = 0; uint32_t life = 0;
        if (!HealthOf(e->entity, &hp, &life) || hp <= 0 || life != 0)
        {
            e->dead = 1;
            Log("escort %u is DOWN after %.0f s: %d hits taken (%d damage), health was %d of %d", e->entity, g_now - e->since, e->hits, e->taken, e->lastHp, e->maxHp);
            continue;
        }
        if (hp > e->maxHp) e->maxHp = hp;
        e->lastHp = hp;
        if (say) Line(e, "every 2 s");
        if (!g_follow || !g_setDest || !EntityPos(e->entity, p) || Dist2(me, p) < 25) continue;
        float to[3] = {me[0] - aim[0] * 2.5f + (i ? aim[1] : -aim[1]) * 1.5f, me[1] - aim[1] * 2.5f + (i ? -aim[0] : aim[0]) * 1.5f, me[2]};
        if (Walks(e->entity)) { g_setDest(NULL, e->entity, to); e->sent++; }
    }
}
static void OnDamage(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfDamageEvent* d = (const HfDamageEvent*)info;
    for (int i = 0; i < ESCORTS; i++)
    {
        Escort* e = &g_esc[i];
        if (!e->entity || d->target != e->entity) continue;
        e->hits++;
        if (d->amount > 0) e->taken += d->amount;
        if (e->hits <= 30)
            Log("  escort %u hit: kind %u, element %u, %d damage, by %s (dealer %u, owner %u)", e->entity, d->kind, d->element, d->amount,
                d->dealer == g_avatarNet || d->owner == g_avatarNet ? "YOU" : "something else", d->dealer, d->owner);
    }
}
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    for (int i = 0; i < ESCORTS; i++) if (g_esc[i].entity == s->entity) g_esc[i].net = s->network;
    if (g_now - g_lastSpawnAt > 3) return;
    static unsigned shown;
    if (shown++ < 40) Log("  spawned near our call: entity %u, resource %016llx%s", s->entity, (unsigned long long)s->resource, s->resource == SEAF ? " (a trooper)" : "");
}

/* ---- 0.2.0: what an order changes ---------------------------------------------------------------------------------- */
#define BEH 0x1F8
#define SQ 0x2510
static uint32_t g_watch, g_squad;
static double g_watchSince;
static uint8_t g_beh[2][BEH], g_sq[2][SQ], g_noiseB[BEH / 4], g_noiseS[SQ / 4];
static int g_haveSnap, g_cur, g_baselineSaid;
static unsigned g_diffLines;

static uintptr_t SquadRecord(uint32_t squad)
{
    uintptr_t m = Pointer(game + 0x33265A0), recs;
    uint32_t i, count;
    if (!m || !squad || !Lookup(m + 0x20, squad, 1048576, &i) || !ReadAt(m + 0xC, &count, 4) || i >= 4096 || !(recs = Pointer(m + 0x40))) return 0;
    return recs + 0x2510ull * i;
}
static uintptr_t BehaviourOf(uint32_t e)
{
    uintptr_t m = Pointer(game + 0x3326740), recs;
    uint32_t i, count;
    if (!m || !Lookup(m + 0x40, e, 1048576, &i) || !ReadAt(m + 0x20, &count, 4) || i >= count || i >= 16384 || !(recs = Pointer(m + 0x60))) return 0;
    return recs + 0x1F8ull * i;
}
static const char* Whose(uint32_t v)
{
    uint8_t d[24];
    uint32_t me = atomic_load(&g_avatar);
    if (!v) return "";
    if (v == me) return "  <- YOUR HELLDIVER (entity)";
    if (g_avatarNet && v == g_avatarNet) return "  <- your Helldiver's network id";
    if (me && Descriptor(me, d) && v == Word(d + 12)) return "  <- your Helldiver's unit";
    if (v == g_watch) return "  <- the escort itself";
    if (v == g_squad) return "  <- its squad";
    return "";
}
static void Watch(uint32_t e)
{
    g_watch = e; g_squad = 0; g_haveSnap = 0; g_baselineSaid = 0; g_watchSince = g_now;
    memset(g_noiseB, 0, sizeof g_noiseB); memset(g_noiseS, 0, sizeof g_noiseS);
    uintptr_t br = BehaviourOf(e);
    if (br) ReadAt(br + 0x68, &g_squad, 4);
    uint8_t d[24];
    Log("watching escort %u: behaviour record %s, squad entity %u (%s), squad record %s", e, br ? "found" : "NOT found", g_squad,
        g_squad && Descriptor(g_squad, d) ? "readable" : "unreadable", SquadRecord(g_squad) ? "found" : "NOT found");
}
static void Diff(const char* what, const uint8_t* a, const uint8_t* b, size_t n, uint8_t* noise, int baseline)
{
    for (size_t o = 0; o + 4 <= n; o += 4)
    {
        uint32_t x = Word(a + o), y = Word(b + o);
        if (x == y) continue;
        if (baseline) { if (noise[o / 4] < 255) noise[o / 4]++; continue; }
        if (noise[o / 4]) continue;
        float fx, fy;
        memcpy(&fx, &x, 4); memcpy(&fy, &y, 4);
        if (g_diffLines++ < 400)
            Log("  %s +0x%04zx: %08x -> %08x (u %u -> %u, f %g -> %g)%s", what, o, x, y, x, y, (double)fx, (double)fy, Whose(y));
    }
}
static void WatchFrame(void)
{
    static double next;
    if (!g_watch || g_now < next) return;
    next = g_now + 0.25;
    uintptr_t br = BehaviourOf(g_watch), sr;
    uint32_t sq = 0;
    if (!br) { Log("escort %u: its behaviour record is gone (dead or taken away): watch ended", g_watch); g_watch = 0; return; }
    ReadAt(br + 0x68, &sq, 4);
    if (sq != g_squad) { Log("  escort %u now perceives through %u (was %u)%s", g_watch, sq, g_squad, Whose(sq)); g_squad = sq; g_haveSnap = 0; }
    int cur = g_cur, prev = !g_cur;
    if (!ReadAt(br, g_beh[cur], BEH)) return;
    sr = SquadRecord(g_squad);
    if (!sr || !ReadAt(sr, g_sq[cur], SQ)) memset(g_sq[cur], 0, SQ);
    int baseline = g_now - g_watchSince < 4.0;
    if (g_haveSnap)
    {
        Diff("behaviour", g_beh[prev], g_beh[cur], BEH, g_noiseB, baseline);
        Diff("squad", g_sq[prev], g_sq[cur], SQ, g_noiseS, baseline);
    }
    if (!baseline && !g_baselineSaid) { g_baselineSaid = 1; Log("baseline done for escort %u: from now on, changes only (give your orders)", g_watch); }
    g_haveSnap = 1;
    g_cur = prev;
}

static void Tick(HfMod* mod, float dt, void* user)
{
    static int prev[3];
    g_now += dt;
    if (!game) return;
    uintptr_t pool = Pointer(game + 0x347CEA8);
    uint8_t running = 0;
    g_inMission = pool && ReadAt(pool + 0x28, &running, 1) && running == 1;
    if (!g_inMission)
    {
        for (int i = 0; i < ESCORTS; i++) if (g_esc[i].entity) { Log("mission over: escort %u forgotten", g_esc[i].entity); memset(&g_esc[i], 0, sizeof g_esc[i]); }
        return;
    }
    int f = Focused(), k[3] = {f && (GetAsyncKeyState(VK_INSERT) & 0x8000), f && (GetAsyncKeyState(VK_HOME) & 0x8000), f && (GetAsyncKeyState(VK_DELETE) & 0x8000)};
    if (k[0] && !prev[0] && !g_want) { g_want = 1; g_wantSince = g_now; Log("Insert: asking for the trooper's package (%016llx; 0: it names none, spawned as is)", (unsigned long long)KbPackageOf(SEAF)); }
    if (k[1] && !prev[1]) { g_follow = !g_follow; Log("Home: following %s", g_follow ? "ON" : "OFF"); }
    if (k[2] && !prev[2]) for (int i = 0; i < ESCORTS; i++) Remove(&g_esc[i], "Delete");
    memcpy(prev, k, sizeof prev);
    SpawnFrame();
    FollowFrame();
    WatchFrame();
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gs = 0, xs = 0;
    uintptr_t g = hf->module_base("game.dll", &gs), x = hf->module_base("helldivers2.exe", &xs);
    if (!g || !x || gs != 74727424u || xs != 60719104u) { Log("REFUSED: not build " BUILD); return; }
    game = g;
    exe = x;
    KbWorldInstall();
    Log("ready: in a mission, Insert a SEAF trooper beside you (2 at most), Home following on/off, Delete takes them away");
}
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 16 || api->size < offsetof(HfApi, global) + sizeof api->global || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    Log("Escort Probe " VERSION ": research for Andoris (a SEAF trooper as a bodyguard)");
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) && api->subscribe(mod, HF_ON_DAMAGE, OnDamage, NULL, 0) &&
             api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->on_game_thread(mod, Tick, NULL);
    if (!ok) Log("events or the game thread are unavailable");
    return 0;
}
