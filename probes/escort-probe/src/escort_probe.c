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
 * flagged. Following now starts OFF. Test: Insert, stand still 6 s, comm wheel Follow, walk 10 s; Hold; Move.
 *
 * 0.4.0, the stick bug's numbers (Yui: 600 health and armour 4, a sentry's, "they are made of metal"): Insert spawns a
 * copy of the trooper under a resource of the probe's own (add_entity, "yui/escort_probe/stick_bug", its own
 * HealthComponentData), so the game's troopers keep theirs (kit base principle 6). Once the entities are in, its record
 * gets main health 600 (+0x00), main armour 4 (+0x118), and every damage zone (38 at most, 0x228 bytes from +0x208, its
 * name's hash at +0x60) armour 4 (+0xD8) and its health x4 (+0xE8), so no part of it falls to one shot. Following
 * re-sends every 0.5 s (a stick bug more than 5 m away), to a point 1.5 m beside you, half a metre along the way you're
 * moving: they trailed 10 to 40 m behind a sprinting Helldiver sent behind her once a second.
 *
 * 0.5.0, a trooper as a hand to drop a weapon (Yui, for Hina: a weapon spawned on the ground can't be picked up, the
 * game's drop makes a pickup; can a SEAF trooper take one and drop it, leaving your own slots alone?). Shift+Insert:
 * a plain SEAF trooper 2 m in front of you; 1 s later the log says whether it has an inventory record (the inventory's
 * lookup, as a Helldiver's: 48 bytes, +0x08 support, +0x1C selection) and what is in it; if it has one, a fresh MG-43 is
 * spawned at its feet, taken into its support slot (game.dll+0x9AE030), dropped (0x9A8840) and the trooper taken away.
 * Then try to pick the MG-43 up. Without a record nothing is called on it (the take on an entity with no record is
 * untested) and the trooper goes..
 *
 * 0.6.0 (Yui: out of view, and the dropped gun moved into place; why the wait before the take?): the trooper and the
 * MG-43 spawn 25 m under your feet; each frame the log says whether the trooper's record, the MG-43 and its position are
 * there yet, and the take comes the first frame all three are (the kits wait a flat 0.75 s: W's Wield Probe 0.5.1 took
 * in the very frame it asked for the spawn, and the gun stayed where it was made). The drop comes the first frame the
 * take shows; 0.1 s later the MG-43 is moved 1.5 m in front of you (the Warp Pack's position setter, its velocity
 * zeroed) and the trooper goes; its position is logged for 2 s.
 *   Played: all three were there in the very frame of the spawn (0 ms), the take showed 21 ms later and the drop went;
 *   the MG-43 then read at your feet, and the move crashed the game (the setter is for characters); the trooper had come
 *   up beside you (Yui).
 *
 * 0.7.0: the trooper at your feet (1 m in front), the take the frame both are ready, the drop the frame the take shows,
 * the trooper taken away two frames after, no move; the MG-43's position logged for 2 s. Is it a pickup, and was the
 * trooper seen?
 *   Played: the trooper was seen for a moment, and the MG-43 went with it (its position no longer read after).
 *
 * 0.8.0: the trooper stays until its support slot lets the MG-43 go (each 50 ms logged, 3 s at most): how long the
 * drop really takes. */
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
#define VERSION "0.8.0"
#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"
#include "kitbase/kb_world.h"
#include "kitbase/kb_spawn.h"

#define SEAF UINT64_C(0x75F7E96AF7DCD303)
static uint64_t g_bug;                     /* the stick bug: add_entity's copy of the trooper, 0 none */
static uint64_t BugRes(void) { return g_bug ? g_bug : SEAF; }
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
    if (!e->dead) Log("  taken away: %s", KbRemoveEntity(e->entity, BugRes()) ? "yes" : "refused (not in a mission, or the id is no longer a trooper)");
    memset(e, 0, sizeof *e);
}
static void SpawnFrame(void)
{
    if (!g_want) return;
    int r = KbLoadFor(BugRes());
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
    uint32_t e = KbSpawnEntity(BugRes(), at, g_bug ? "a stick bug (the trooper copy)" : "a SEAF trooper");
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
        FriendRes(BugRes()) ? "a friend" : "NOT a friend", Walks(e));
    if (g_bug) Log("  it is the stick bug copy (%016llx)", (unsigned long long)g_bug);
}
static void FollowFrame(void)
{
    static double next, report;
    float me[3], aim[3], p[3];
    if (g_now < next) return;
    next = g_now + 0.5;
    static float last[3];
    static int haveLast;
    int say = g_now >= report;
    if (say) report = g_now + 2.0;
    if (!PlayerPose(me, aim)) return;
    /* where she's heading: her move over the last half second, else where she faces */
    float way[3] = {aim[0], aim[1], 0};
    if (haveLast)
    {
        float dx = me[0] - last[0], dy = me[1] - last[1], m = sqrtf(dx * dx + dy * dy);
        if (m > 0.4f) { way[0] = dx / m; way[1] = dy / m; }
    }
    memcpy(last, me, sizeof last);
    haveLast = 1;
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
        float to[3] = {me[0] + way[0] * 0.5f + (i ? way[1] : -way[1]) * 1.5f, me[1] + way[1] * 0.5f + (i ? -way[0] : way[0]) * 1.5f, me[2]};
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
    if (shown++ < 40) Log("  spawned near our call: entity %u, resource %016llx%s", s->entity, (unsigned long long)s->resource, s->resource == SEAF ? " (a trooper)" : s->resource == g_bug ? " (a stick bug)" : "");
}

/* ---- 0.2.x: what an order changes ---------------------------------------------------------------------------------- */
/* 0.2.0 watched the behaviour and squad records: a lone trooper perceives through itself, has no squad, and "follow me"
 * left no trace there. 0.2.1 watches the game's friendly NPCs ([game+0x3326588]: its own 0x4A0 bytes, then the map at
 * +0x4B8 and two records a unit, 0x28 bytes at +0x4D8 and 0x20 at +0x4E0) and the commanders ([game+0x33265D8]: the map
 * at +0x20, records of 0x70 bytes at +0x40 and 0xC0 at +0x48), the escort's and your Helldiver's. (Their add helpers
 * 0x545730 and 0x54A530; growth functions 0x7B9310, 0x865A60 name FriendlyNpcComponent and CommanderComponent.) */
enum { R_BEH, R_NPCHEAD, R_NPC28, R_NPC20, R_CMD70, R_CMDC0, R_ME70, R_MEC0, R_N };
static const char* const kRegion[R_N] = {"behaviour", "friendly-npc manager", "escort npc+0x4D8", "escort npc+0x4E0", "escort commander+0x40",
                                         "escort commander+0x48", "YOUR commander+0x40", "YOUR commander+0x48"};
static const uint32_t kSize[R_N] = {0x1F8, 0x4A0, 0x28, 0x20, 0x70, 0xC0, 0x70, 0xC0};
#define MAXR 0x4A0
static uint32_t g_watch;
static double g_watchSince;
static uint8_t g_snap[2][R_N][MAXR], g_noise[R_N][MAXR / 4];
static int g_haveSnap, g_cur, g_baselineSaid;
static unsigned g_diffLines[R_N];

static uintptr_t InManager(uintptr_t cell, uint32_t mapOff, uint32_t recOff, uint32_t stride, uint32_t e)
{
    uintptr_t m = Pointer(game + cell), recs;
    uint32_t i;
    if (!m || !e || !Lookup(m + mapOff, e, 1048576, &i) || i >= 65536 || !(recs = Pointer(m + recOff))) return 0;
    return recs + (uintptr_t)stride * i;
}
static uintptr_t BehaviourOf(uint32_t e)
{
    uintptr_t m = Pointer(game + 0x3326740), recs;
    uint32_t i, count;
    if (!m || !Lookup(m + 0x40, e, 1048576, &i) || !ReadAt(m + 0x20, &count, 4) || i >= count || i >= 16384 || !(recs = Pointer(m + 0x60))) return 0;
    return recs + 0x1F8ull * i;
}
static uintptr_t Region(int r)
{
    uint32_t me = atomic_load(&g_avatar);
    switch (r)
    {
    case R_BEH: return BehaviourOf(g_watch);
    case R_NPCHEAD: return Pointer(game + 0x3326588);
    case R_NPC28: return InManager(0x3326588, 0x4B8, 0x4D8, 0x28, g_watch);
    case R_NPC20: return InManager(0x3326588, 0x4B8, 0x4E0, 0x20, g_watch);
    case R_CMD70: return InManager(0x33265D8, 0x20, 0x40, 0x70, g_watch);
    case R_CMDC0: return InManager(0x33265D8, 0x20, 0x48, 0xC0, g_watch);
    case R_ME70: return InManager(0x33265D8, 0x20, 0x40, 0x70, me);
    case R_MEC0: return InManager(0x33265D8, 0x20, 0x48, 0xC0, me);
    }
    return 0;
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
    return "";
}
/* a region's words that hold an id of yours or the escort's, at the start */
static void Ids(int r, const uint8_t* b)
{
    char line[600];
    int n = 0, any = 0;
    for (uint32_t o = 0; o + 4 <= kSize[r]; o += 4)
    {
        const char* w = Whose(Word(b + o));
        if (*w && n < 520) { n += snprintf(line + n, sizeof line - n, " +0x%x:%s", o, w + 6); any = 1; }
    }
    if (any) Log("    %s holds:%s", kRegion[r], line);
}
static void Watch(uint32_t e)
{
    g_watch = e; g_haveSnap = 0; g_baselineSaid = 0; g_watchSince = g_now;
    memset(g_diffLines, 0, sizeof g_diffLines);
    memset(g_noise, 0, sizeof g_noise);
    char found[300];
    int n = 0;
    for (int r = 0; r < R_N; r++) n += snprintf(found + n, sizeof found - n, "%s%s %s", r ? "," : "", kRegion[r], Region(r) ? "found" : "NOT found");
    Log("watching escort %u (you: entity %u, network %u): %s", e, atomic_load(&g_avatar), g_avatarNet, found);
}
static void Diff(int r, const uint8_t* a, const uint8_t* b, int baseline)
{
    for (uint32_t o = 0; o + 4 <= kSize[r]; o += 4)
    {
        uint32_t x = Word(a + o), y = Word(b + o);
        uint8_t* noise = &g_noise[r][o / 4];
        if (x == y) continue;
        if (baseline) { if (*noise < 255) (*noise)++; continue; }
        if (*noise && !*Whose(y) && !*Whose(x)) continue;   /* noise, unless an id comes or goes */
        if (r == R_BEH && !*Whose(y) && !*Whose(x)) continue; /* its fighting fills the behaviour record: only ids there (0.2.1's log) */
        float fx, fy;
        memcpy(&fx, &x, 4); memcpy(&fy, &y, 4);
        if (g_diffLines[r]++ < 300)
            Log("  %s +0x%04x: %08x -> %08x (u %u -> %u, f %g -> %g)%s%s", kRegion[r], o, x, y, x, y, (double)fx, (double)fy, Whose(y), *Whose(x) && !*Whose(y) ? "  (an id went)" : "");
    }
}
static void WatchFrame(void)
{
    static double next;
    if (!g_watch || g_now < next) return;
    next = g_now + 0.25;
    if (!BehaviourOf(g_watch)) { Log("escort %u: its behaviour record is gone (dead or taken away): watch ended", g_watch); g_watch = 0; return; }
    int cur = g_cur, prev = !g_cur, baseline = g_now - g_watchSince < 4.0;
    for (int r = 0; r < R_N; r++)
    {
        uintptr_t at = Region(r);
        if (!at || !ReadAt(at, g_snap[cur][r], kSize[r])) memset(g_snap[cur][r], 0, kSize[r]);
        if (!g_haveSnap)
        {
            Ids(r, g_snap[cur][r]);
            if (r == R_NPCHEAD || r == R_NPC28 || r == R_NPC20)
            {
                char line[400];
                int n = 0;
                for (uint32_t o = 0; o < kSize[r] && o < 0x60; o += 4) n += snprintf(line + n, sizeof line - n, " %08x", Word(g_snap[cur][r] + o));
                Log("    %s, its first words:%s", kRegion[r], line);
            }
        }
        else Diff(r, g_snap[prev][r], g_snap[cur][r], baseline);
    }
    if (!baseline && !g_baselineSaid) { g_baselineSaid = 1; Log("baseline done for escort %u: from now on, changes only (give your orders)", g_watch); }
    g_haveSnap = 1;
    g_cur = prev;
}

/* ---- 0.5.0: a trooper's hand (the inventory's take and drop, as the kit base's kb_wield.h; names of its own here: this
 * probe's kit base has kb_world.h, whose ResOf and EntityPos kb_wield.h would clash with) ------------------------------ */
#define MG43_ENTITY UINT64_C(0x11C27D3BABB38956)
typedef void (*HandTakeFn)(void* inventory, uint32_t entity, uint32_t item, uint32_t slot, uint8_t flag);
typedef void (*HandDropFn)(void* inventory, uint32_t entity, uint32_t item, uint8_t flag, uint64_t zero, uint8_t one);
static HandTakeFn g_handTake;
static HandDropFn g_handDrop;
enum { H_IDLE, H_PKG, H_READY, H_TAKEN, H_DROPPED, H_MOVED };
static int g_hand;
static double g_handAt, g_handT0, g_handLogAt;
static uint32_t g_handSeaf, g_handGun;
static float g_handTo[3];
static int g_handMoves, g_handFrames;
static void HandPins(void)
{
    if (hf->code_matches(game + 0x9AE030, "40 53 55 41 55 48 83 ec 40 48 89 7c 24 68 41 8b e8 4c 89 64 24 70 8b da")) g_handTake = (HandTakeFn)(game + 0x9AE030);
    if (hf->code_matches(game + 0x9A8840, "40 53 56 41 56 48 81 ec a0 00 00 00 48 8b 05 bd 37 c9 01 48 33 c4 48 89")) g_handDrop = (HandDropFn)(game + 0x9A8840);
    Log("Shift+Insert: the inventory's take %s, its drop %s; the warp setter %s", g_handTake ? "pinned" : "NOT the reviewed code", g_handDrop ? "pinned" : "NOT the reviewed code",
        g_setPosition ? "ready" : "off");
}
static uintptr_t HandRecord(uint32_t e)
{
    uintptr_t m = (uintptr_t)hf_global_InventoryManager(self), records;
    uint32_t i;
    if (!m || !Lookup(m + 0x28, e, 1048576, &i) || i >= 65536 || !(records = Pointer(m + 0x50))) return 0;
    return records + 0x30ull * i;
}
static uint32_t HandSupport(uint32_t e) { uintptr_t r = HandRecord(e); uint32_t v = 0; if (r) ReadAt(r + 8, &v, 4); return v; }
static double Ms(void) { return (g_now - g_handT0) * 1000; }
static void HandGunAt(const char* when)
{
    float p[3], me[3];
    if (EntityPos(g_handGun, p))
    {
        double d = PlayerPose(me, NULL) ? sqrt(Dist2(me, p)) : -1;
        Log("Shift+Insert %6.0f ms %s: MG-43 %u at %.1f %.1f %.1f, %.1f m from you", Ms(), when, g_handGun, (double)p[0], (double)p[1], (double)p[2], d);
    }
    else Log("Shift+Insert %6.0f ms %s: MG-43 %u's position unread", Ms(), when, g_handGun);
}
static void HandFrame(void)
{
    void* im = hf_global_InventoryManager(self);
    if (g_hand == H_IDLE || g_now < g_handAt) return;
    switch (g_hand)
    {
    case H_PKG:
    {
        int a = KbLoadFor(SEAF), b = KbLoadFor(MG43_ENTITY);
        if (a != HF_PACKAGE_RESIDENT || b != HF_PACKAGE_RESIDENT)
        {
            if (!a || !b || g_now - g_wantSince > 8) { Log("Shift+Insert: the trooper's or the MG-43's package did not load (%d, %d)", a, b); g_hand = H_IDLE; }
            break;
        }
        float me[3], aim[3], hid[3];
        if (!PlayerPose(me, aim) || !g_handTake || !g_handDrop || !im) { Log("Shift+Insert: not ready (your Helldiver, or the take and drop)"); g_hand = H_IDLE; break; }
        /* 0.7.0: at your feet, 1 m in front (0.6.0: spawned 25 m under you, the trooper came up beside you anyway); seen
         * for as few frames as can be: the take the frame both are ready, the drop the frame it shows, the trooper gone
         * two frames after */
        hid[0] = me[0] + aim[0]; hid[1] = me[1] + aim[1]; hid[2] = me[2];
        g_handTo[0] = me[0] + aim[0] * 1.5f; g_handTo[1] = me[1] + aim[1] * 1.5f; g_handTo[2] = me[2] + 0.3f;
        g_handT0 = g_now;
        g_handSeaf = KbSpawnEntity(SEAF, hid, "a SEAF trooper (a hand, under the ground)");
        g_handGun = KbSpawnEntity(MG43_ENTITY, hid, "a fresh MG-43 (under the ground)");
        Log("Shift+Insert: trooper %u and MG-43 %u spawned 1 m in front of you; each frame from now: are they ready?", g_handSeaf, g_handGun);
        g_hand = g_handSeaf && g_handGun ? H_READY : H_IDLE;
        g_handAt = g_now + 3;   /* the latest */
        g_handLogAt = 0;
        break;
    }
    default: break;
    }
}
/* the steps that poll each frame (HandFrame waits on g_handAt) */
static void HandPoll(void)
{
    void* im = hf_global_InventoryManager(self);
    if (g_hand == H_READY)
    {
        uint8_t d[24];
        int rec = HandRecord(g_handSeaf) != 0, gun = Descriptor(g_handGun, d) && Quad(d) == MG43_ENTITY;
        float p[3];
        int pos = EntityPos(g_handGun, p);
        if (g_now >= g_handLogAt) { g_handLogAt = g_now + 0.05; Log("Shift+Insert %6.0f ms: trooper record %d, MG-43 reads %d, its position %d", Ms(), rec, gun, pos); }
        if (rec && gun && pos)
        {
            g_handTake(im, g_handSeaf, g_handGun, 3, 0);
            Log("Shift+Insert %6.0f ms: all ready: taken into the trooper's support slot", Ms());
            g_hand = H_TAKEN;
            g_handAt = g_now + 1.5;
        }
        else if (g_now > g_handAt) { Log("Shift+Insert: not ready in 3 s: given up"); g_hand = H_IDLE; KbRemoveEntity(g_handSeaf, SEAF); }
        return;
    }
    if (g_hand == H_TAKEN)
    {
        if (HandSupport(g_handSeaf) == g_handGun)
        {
            Log("Shift+Insert %6.0f ms: in the trooper's support slot: dropped", Ms());
            g_handDrop(im, g_handSeaf, g_handGun, 0, 0, 1);
            HandGunAt("just after the drop");
            g_hand = H_DROPPED;
            g_handFrames = 0;
            g_handLogAt = 0;
        }
        else if (g_now > g_handAt) { Log("Shift+Insert: the take never showed in 1.5 s"); g_hand = H_DROPPED; g_handAt = g_now; }
        return;
    }
    /* 0.7.0: no move (0.6.0: the Warp Pack's setter on the MG-43 crashed the game, game.dll+0x5A2C69: it is for
     * characters) */
    /* 0.8.0: the trooper goes the frame its support slot no longer holds the MG-43 (0.7.0: taken away two frames after
     * the drop, it took the MG-43 with it: its position no longer read); each 50 ms until then the slot and the MG-43's
     * position are logged, 3 s at most */
    if (g_hand == H_DROPPED)
    {
        uint32_t now = HandSupport(g_handSeaf);
        g_handFrames++;
        if (now == g_handGun && Ms() < 3000)
        {
            if (g_now >= g_handLogAt) { g_handLogAt = g_now + 0.05; Log("Shift+Insert %6.0f ms (frame %d): still in the trooper's support slot", Ms(), g_handFrames); HandGunAt("meanwhile"); }
            return;
        }
        Log("Shift+Insert %6.0f ms (frame %d): the trooper's support slot now %u", Ms(), g_handFrames, now);
        HandGunAt("then");
        Log("Shift+Insert %6.0f ms: the trooper goes (%s)", Ms(), KbRemoveEntity(g_handSeaf, SEAF) ? "taken away" : "could not be taken away");
        g_hand = H_MOVED;
        g_handMoves = 0;
        g_handAt = g_now + 0.2;
        return;
    }
    if (g_hand == H_MOVED && g_now >= g_handAt)
    {
        HandGunAt("after");
        g_handAt = g_now + 0.25;
        if (++g_handMoves >= 8) { Log("Shift+Insert: done. Is the MG-43 in front of you, and can you pick it up?"); g_hand = H_IDLE; }
    }
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
        g_hand = H_IDLE;
        return;
    }
    int f = Focused(), k[3] = {f && (GetAsyncKeyState(VK_INSERT) & 0x8000), f && (GetAsyncKeyState(VK_HOME) & 0x8000), f && (GetAsyncKeyState(VK_DELETE) & 0x8000)};
    int shift = f && (GetAsyncKeyState(VK_SHIFT) & 0x8000);
    if (k[0] && !prev[0] && shift)
    {
        if (g_hand == H_IDLE) { g_hand = H_PKG; g_handAt = 0; g_wantSince = g_now; Log("Shift+Insert: a trooper as a hand (the packages first)"); }
    }
    else if (k[0] && !prev[0] && !g_want) { g_want = 1; g_wantSince = g_now; Log("Insert: asking for the trooper's package (%016llx; 0: it names none, spawned as is)", (unsigned long long)KbPackageOf(BugRes())); }
    if (k[1] && !prev[1]) { g_follow = !g_follow; Log("Home: following %s", g_follow ? "ON" : "OFF"); }
    if (k[2] && !prev[2]) for (int i = 0; i < ESCORTS; i++) Remove(&g_esc[i], "Delete");
    memcpy(prev, k, sizeof prev);
    SpawnFrame();
    HandFrame();
    HandPoll();
    FollowFrame();
    WatchFrame();
}
/* 0.3.0: the Lua half (data/, built from hud-src/addon.lua) logs through here: its lines go into this log */
HF_MOD_EXPORT int escort_lua_log(unsigned version, const char* text)
{
    static unsigned lines;
    if (version != 1 || !text) return 0;
    if (lines++ < 4000) Log("[lua] %.480s", text);
    return 1;
}
/* 0.4.0: the stick bug's health record: main 600, armour 4, every zone armour 4 and health x4 */
static void BugNumbers(HfMod* mod, HfEvent event, void* user)
{
    uint32_t stride = 0;
    uintptr_t h = g_bug && hf->component_record ? hf->component_record("HealthComponentData", g_bug, &stride) : 0;
    uintptr_t base = hf->component_record ? hf->component_record("HealthComponentData", SEAF, &stride) : 0;
    if (!h || h == base) { Log("stick bug: its own health record is not there (%s): the trooper's numbers", h ? "shared with the trooper" : "none"); return; }
    int32_t hp = 0, top = 600;
    uint32_t armour = 0, four = 4;
    ReadAt(h, &hp, 4);
    ReadAt(h + 0x118, &armour, 4);
    hf->write(h, &top, 4);
    hf->write(h + 0x118, &four, 4);
    int zones = 0;
    for (int z = 0; z < 38; z++)
    {
        uintptr_t zr = h + 0x208 + 0x228ull * z;
        uint32_t name = 0, za = 0;
        int32_t zh = 0;
        if (!ReadAt(zr + 0x60, &name, 4) || !name) continue;
        ReadAt(zr + 0xD8, &za, 4);
        ReadAt(zr + 0xE8, &zh, 4);
        int32_t zh4 = zh > 0 ? zh * 4 : zh;
        hf->write(zr + 0xD8, &four, 4);
        hf->write(zr + 0xE8, &zh4, 4);
        if (zones++ < 12) Log("  zone %d (%08x): armour %u -> 4, health %d -> %d", z, name, za, zh, zh4);
    }
    Log("stick bug %016llx: health %d -> 600, armour %u -> 4, %d zones", (unsigned long long)g_bug, hp, armour, zones);
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gs = 0, xs = 0;
    uintptr_t g = hf->module_base("game.dll", &gs), x = hf->module_base("helldivers2.exe", &xs);
    if (!g || !x || gs != 74727424u || xs != 60719104u) { Log("REFUSED: not build " BUILD); return; }
    game = g;
    exe = x;
    KbWorldInstall();
    HandPins();
    Log("ready: in a mission, Insert a SEAF trooper beside you (2 at most), Home following on/off, Delete takes them away; Shift+Insert a trooper that takes and drops an MG-43");
}
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 16 || api->size < offsetof(HfApi, global) + sizeof api->global || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    Log("Escort Probe " VERSION ": research for Andoris (a SEAF trooper as a bodyguard)");
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    if (api->add_entity)
    {
        static const char* const own[] = {"HealthComponentData", NULL};
        HfEntity bug = {sizeof bug};
        bug.base = SEAF;
        bug.name = "yui/escort_probe/stick_bug";
        bug.own_components = own;
        g_bug = api->add_entity(mod, &bug);
        Log(g_bug ? "stick bug: a copy of the SEAF trooper, %016llx" : "stick bug: the copy was refused (%016llx): the plain trooper", (unsigned long long)g_bug);
        if (g_bug && api->on(mod, HF_EVENT_ENTITIES_READY, BugNumbers, NULL)) Log("stick bug: its numbers can't be set (no ENTITIES_READY)");
    }
    int ok = api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) && api->subscribe(mod, HF_ON_DAMAGE, OnDamage, NULL, 0) &&
             api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->on_game_thread(mod, Tick, NULL);
    if (!ok) Log("events or the game thread are unavailable");
    return 0;
}
