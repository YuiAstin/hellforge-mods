/* Perception Probe (yui.perception-probe): research for Ascalon's always-hidden kit (docs/ascalon-design.md), following
 * docs/research/perception.md section 6. Host or solo (enemy perception runs on the host). Build 25480438 only.
 *
 * Keys (in a mission):
 *   F6         hidden mode on/off: her visibility base set to 0 (the game's own SetBase, 0x720350) every 0.25 s; the
 *              value read when it went on is put back when it goes off. Each time the base is found back above 0 it
 *              is counted (does anything rewrite it?).
 *   Ctrl+F6    noise mode: observe (log only) -> mute (her noises at loudness 0) -> 15 m (her noises scaled so their
 *              radius is at most 15 m) -> observe
 *   Shift+F6   dump now: her record, her guns' WeaponData (+0x70 suppressed, +0x84 noise type, +0x88 floor) and every
 *              enemy within 50 m in detail
 *
 * Every 2 s it logs her visibility record (base, A, B, floor, darkness factor) and GetVisibility (0x720530, a pure read),
 * and the 6 nearest enemies within 50 m: her entries in their perception record (and their squad perceiver's), sense
 * bits, line of sight (+0x30), awareness (+0x40), heard level (+0x38), flags (+0x48), whether their behaviour targets
 * her, and their alert state ([game+0x3326528] +0x50). Floor changes are logged as they happen (firing).
 * Noises (EmitNoise 0x64A890, hooked) with her as source or instigator are logged with type, radius and loudness, and
 * 0.5 s later which enemies gained a heard-only entry for her.
 *
 * Layouts: docs/research/perception.md (perception, behaviour and Lost Contact's records: Lost Contact's own source).
 * The visibility record's map is checked before SetBase is called (SetBase writes at index -1 when there is none). */
#include <ctype.h>
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

#define VERSION "0.1.0"
#define BUILD "25480438"
#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"

/* ---- the game's functions ---------------------------------------------------------------------------------------- */
typedef void (*SetBaseFn)(void*, uint32_t, float);
typedef float (*GetVisFn)(void*, uint32_t);
typedef uint64_t (*NoiseFn)(void*, uint64_t, uint64_t, const float*, uint64_t, float, uint64_t);
static SetBaseFn g_setBase;
static GetVisFn g_getVis;
static NoiseFn g_noiseOrig;
static float g_noiseRadius[30];

/* her visibility record: [game+0x3326540], map +0x20, records [mgr+0x40], 0x14 each */
typedef struct Vis { float base, a, b, floor, dark; } Vis;
static uintptr_t VisRecord(uint32_t e)
{
    uintptr_t m = Pointer(game + 0x3326540), recs;
    uint32_t i;
    if (!m || !e || !Lookup(m + 0x20, e, 1048576, &i) || i >= 262144 || !(recs = Pointer(m + 0x40))) return 0;
    return recs + 0x14ull * i;
}
static int ReadVis(uint32_t e, Vis* v)
{
    uintptr_t r = VisRecord(e);
    return r && ReadAt(r, v, sizeof *v);
}

/* ---- enemies (spawns with a behaviour that are not Helldivers, turrets or vehicles) -------------------------------- */
#define FOES 512
static uint32_t g_foe[FOES];
static uint64_t g_foeRes[FOES];
static int g_nfoe;
static uint64_t g_kindRes[512];
static uint8_t g_kindOf[512];   /* 1 enemy, 2 not */
static int KindOf(uint64_t res)
{
    unsigned h = (unsigned)((res ^ (res >> 29)) * 0x9E3779B1u) & 511;
    for (unsigned k = 0; k < 512; k++, h = (h + 1) & 511)
    {
        if (g_kindRes[h] == res) return g_kindOf[h];
        if (!g_kindRes[h])
        {
            uint32_t s;
            g_kindRes[h] = res;
            g_kindOf[h] = hf->component_record("BehaviorComponentData", res, &s) && !hf->component_record("AvatarComponentData", res, &s) &&
                          !hf->component_record("TurretComponentData", res, &s) && !hf->component_record("SeatCollectionComponentData", res, &s) ? 1 : 2;
            return g_kindOf[h];
        }
    }
    return 2;
}
/* spawns are queued by the event and sorted on the game thread (the component lookups) */
static uint32_t g_newEnt[256];
static uint64_t g_newRes[256];
static atomic_uint g_newHead;
static unsigned g_newTail;

static uintptr_t Behaviour(uint32_t e)
{
    uintptr_t m = Pointer(game + 0x3326740), recs;
    uint32_t i, count;
    if (!m || !Lookup(m + 0x40, e, 1048576, &i) || !ReadAt(m + 0x20, &count, 4) || i >= count || i >= 16384 || !(recs = Pointer(m + 0x60))) return 0;
    return recs + 0x1F8ull * i;
}
static uintptr_t Perception(uint32_t e)
{
    uintptr_t m = Pointer(game + 0x3326548), recs;
    uint32_t i;
    if (!m || !e || !Lookup(m + 0x30, e, 1048576, &i) || i >= 16384 || !(recs = Pointer(m + 0x50))) return 0;
    return recs + 0x13F8ull * i;
}
static int AlertState(uint32_t e)
{
    uintptr_t m = Pointer(game + 0x3326528), st;
    uint32_t i, v;
    if (!m || !Lookup(m + 0x20, e, 1048576, &i) || i >= 65536 || !(st = Pointer(m + 0x50)) || !ReadAt(st + 4ull * i, &v, 4)) return -1;
    return (int)v;
}
static const uint32_t kListCount[3] = {0x310, 0x818, 0xD20}, kListEntries[3] = {0x318, 0x820, 0xD28};
static const uint32_t kSingles[3] = {0x1228, 0x1278, 0x12C8};
/* her entries in a perception record, summed up */
typedef struct Seen { int n; uint32_t sense, flags; uint8_t los; float aware, heard; char where[24]; } Seen;
static void HerEntries(uintptr_t pr, uint32_t me, Seen* s)
{
    static const char* names[3] = {"A", "B", "C"};
    for (int l = 0; l < 3 && pr; l++)
    {
        uint32_t n = 0;
        if (!ReadAt(pr + kListCount[l], &n, 4) || n > 16) continue;
        for (uint32_t k = 0; k < n; k++)
        {
            uint8_t ent[0x50];
            if (!ReadAt(pr + kListEntries[l] + 0x50u * k, ent, sizeof ent)) break;
            if (Word(ent) != me) continue;
            if (!s->n)
            {
                s->sense = Word(ent + 0x4C); s->flags = Word(ent + 0x48); s->los = ent[0x30];
                memcpy(&s->aware, ent + 0x40, 4); memcpy(&s->heard, ent + 0x38, 4);
            }
            else s->sense |= Word(ent + 0x4C);
            s->n++;
            size_t len = strlen(s->where);
            if (len < sizeof s->where - 3) snprintf(s->where + len, sizeof s->where - len, "%s", names[l]);
        }
    }
    for (int k = 0; k < 3 && pr; k++)
    {
        uint8_t ent[0x50];
        if (!ReadAt(pr + kSingles[k], ent, sizeof ent) || !Word(ent + 0x48) || Word(ent) != me) continue;
        if (!s->n)
        {
            s->sense = Word(ent + 0x4C); s->flags = Word(ent + 0x48); s->los = ent[0x30];
            memcpy(&s->aware, ent + 0x40, 4); memcpy(&s->heard, ent + 0x38, 4);
        }
        else s->sense |= Word(ent + 0x4C);
        s->n++;
        size_t len = strlen(s->where);
        if (len < sizeof s->where - 3) snprintf(s->where + len, sizeof s->where - len, "s%d", k + 1);
    }
}
typedef struct Near { uint32_t e; float d; } Near;
static int ByDist(const void* a, const void* b) { float x = ((const Near*)a)->d, y = ((const Near*)b)->d; return x < y ? -1 : x > y; }
/* enemies within r of her, nearest first */
static int Nearby(const float me[3], float r, Near* out, int cap)
{
    int n = 0;
    for (int j = 0; j < g_nfoe; j++)
    {
        uint8_t d[24];
        float p[3];
        if (!Descriptor(g_foe[j], d) || Quad(d) != g_foeRes[j]) { g_foe[j] = g_foe[g_nfoe - 1]; g_foeRes[j] = g_foeRes[g_nfoe - 1]; g_nfoe--; j--; continue; }
        if (!UnitNode(Word(d + 12), 0, p)) continue;
        float d2 = Dist2(p, me);
        if (d2 <= r * r && n < cap) { out[n].e = g_foe[j]; out[n].d = sqrtf(d2); n++; }
    }
    qsort(out, n, sizeof *out, ByDist);
    return n;
}
static void LogFoe(uint32_t e, float dist, uint32_t me, int detail)
{
    Seen own = {0}, squad = {0};
    uintptr_t b = Behaviour(e);
    uint32_t target = 0, has = 0, perceiver = 0;
    if (b) { ReadAt(b + 0x18, &target, 4); ReadAt(b + 0x78, &has, 1); ReadAt(b + 0x68, &perceiver, 4); }
    HerEntries(Perception(e), me, &own);
    if (perceiver && perceiver != e) HerEntries(Perception(perceiver), me, &squad);
    char sq[96] = "";
    if (squad.n) snprintf(sq, sizeof sq, "; squad %u: %d [%s] sense %x los %u aware %.2f", perceiver, squad.n, squad.where, squad.sense, squad.los, (double)squad.aware);
    Log("  enemy %u %.1f m: alert %d, target %s%s, her entries %d [%s] sense %x los %u aware %.2f heard %.2f flags %x%s", e, (double)dist,
        AlertState(e), target == me ? "HER" : target ? "other" : "none", has & 1 ? "" : " (no target flag)", own.n, own.where, own.sense, own.los,
        (double)own.aware, (double)own.heard, own.flags, sq);
    if (detail && !own.n && !squad.n) Log("    (no entry for her; perceiver %u)", perceiver);
}

/* ---- noise hook ---------------------------------------------------------------------------------------------------- */
static atomic_int g_noiseMode;     /* 0 observe, 1 mute, 2 15 m */
typedef struct NoiseCall { uint32_t type, src, inst, mode, how; float loud, sent, pos[3]; } NoiseCall;
#define NRING 64
static NoiseCall g_ring[NRING];
static atomic_uint g_ringHead;
static unsigned g_ringTail;
static atomic_uint g_noiseAll;
static uint64_t NoiseHook(void* rcx, uint64_t type, uint64_t src, const float* pos, uint64_t mode, float loud, uint64_t inst)
{
    uint32_t me = atomic_load(&g_avatar);
    atomic_fetch_add(&g_noiseAll, 1);
    uint32_t s = (uint32_t)src, in = (uint32_t)inst;
    if (me && (s == me || in == me))
    {
        uint8_t t = (uint8_t)type;
        int m = atomic_load(&g_noiseMode);
        float sent = loud;
        if (m == 1) sent = 0;
        else if (m == 2 && t < 30 && g_noiseRadius[t] > 15 && loud * g_noiseRadius[t] > 15) sent = 15 / g_noiseRadius[t];
        unsigned i = atomic_fetch_add(&g_ringHead, 1) % NRING;
        NoiseCall* c = &g_ring[i];
        c->type = t; c->src = s; c->inst = in; c->mode = (uint32_t)(mode & 0xFF); c->how = (uint32_t)m; c->loud = loud; c->sent = sent;
        if (pos) memcpy(c->pos, pos, 12); else memset(c->pos, 0, 12);
        loud = sent;
    }
    return g_noiseOrig(rcx, type, src, pos, mode, loud, inst);
}
/* 0.5 s after a noise of hers: who gained a heard-only entry (flags 4) for her */
static double g_heardCheckAt = -1;
static uint32_t g_heardBefore[64];
static int g_nHeardBefore;
static int HeardOnly(uint32_t e, uint32_t me)
{
    Seen s = {0};
    HerEntries(Perception(e), me, &s);
    return s.n && s.flags == 4;
}

/* ---- state -------------------------------------------------------------------------------------------------------- */
static int g_hidden, g_hookOk;
static float g_savedBase = 1.0f;
static unsigned g_rewrites, g_setCalls;
static double g_nextSet, g_nextLog, g_rewriteSince;
static float g_lastFloor = -1;
static uint64_t g_loggedWeapon[4];

static void LogWeapons(int always)
{
    static const char* names[4] = {"", "primary", "secondary", "support"};
    for (int s = 1; s <= 3; s++)
    {
        uint64_t res = g_weaponRes[s];
        if (!res || (!always && res == g_loggedWeapon[s])) continue;
        g_loggedWeapon[s] = res;
        uint32_t stride = 0;
        uintptr_t w = hf->component_record("WeaponDataComponentData", res, &stride);
        uint8_t sup = 0, type = 0;
        float floor = -1;
        if (!w || !ReadAt(w + 0x70, &sup, 1) || !ReadAt(w + 0x84, &type, 1) || !ReadAt(w + 0x88, &floor, 4))
        {
            Log("%s %016llx: no WeaponData record", names[s], (unsigned long long)res);
            continue;
        }
        Log("%s %016llx: suppressed %u, noise type %u (radius %.0f m), firing floor %.3f", names[s], (unsigned long long)res, sup, type,
            type < 30 ? (double)g_noiseRadius[type] : -1.0, (double)floor);
    }
}
static void LogMe(const char* why)
{
    uint32_t me = atomic_load(&g_avatar);
    Vis v;
    if (!ReadVis(me, &v)) { Log("%s: no visibility record for her (entity %u)", why, me); return; }
    float got = g_getVis ? g_getVis(NULL, me) : -1;
    Log("%s: base %.3f A %.3f B %.3f floor %.3f dark %.3f -> visibility %.3f | hidden %s, rewrites %u, noise mode %s", why, (double)v.base,
        (double)v.a, (double)v.b, (double)v.floor, (double)v.dark, (double)got, g_hidden ? "on" : "off", g_rewrites,
        atomic_load(&g_noiseMode) == 1 ? "mute" : atomic_load(&g_noiseMode) == 2 ? "15 m" : "observe");
}
static void Dump(int nearest, float range, int detail)
{
    float me[3];
    uint32_t her = atomic_load(&g_avatar);
    if (!PlayerPose(me, NULL)) return;
    Near list[64];
    int n = Nearby(me, range, list, 64);
    if (!n) { if (detail) Log("  no enemy within %.0f m (%d tracked)", (double)range, g_nfoe); return; }
    for (int i = 0; i < n && i < nearest; i++) LogFoe(list[i].e, list[i].d, her, detail);
    if (n > nearest) Log("  (%d more within %.0f m)", n - nearest, (double)range);
}

/* ---- events ------------------------------------------------------------------------------------------------------- */
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    if (s->resource == AVATAR && s->authority)
    {
        g_spawnEntity[g_spawnNext % 8] = s->entity;
        g_spawnNet[g_spawnNext % 8] = s->network;
        g_spawnNext++;
        return;
    }
    if (!s->authority) return;
    unsigned h = atomic_load(&g_newHead);
    g_newEnt[h % 256] = s->entity;
    g_newRes[h % 256] = s->resource;
    atomic_store(&g_newHead, h + 1);
}
static void OnPlayer(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    if (!p->local) return;
    if (info->kind == HF_ON_PLAYER_SPAWN && p->entity) { atomic_store(&g_avatar, p->entity); g_alive = 1; Log("her Helldiver: entity %u", p->entity); }
    else g_alive = 0;
}
static int KeyDown(int vk, int* prev)
{
    int now = Focused() && (GetAsyncKeyState(vk) & 0x8000);
    int edge = now && !*prev;
    *prev = now;
    return edge;
}
static void Tick(HfMod* mod, float dt, void* user)
{
    g_now += dt > 0 && dt < 1 ? dt : 0;
    int in = hf->game_state(self) == HF_STATE_MISSION;
    if (in != g_inMission)
    {
        g_inMission = in;
        g_nfoe = 0;
        g_newTail = atomic_load(&g_newHead);
        if (!in && g_hidden) { g_hidden = 0; Log("mission over: hidden mode off (%u rewrites of her base seen)", g_rewrites); }
        g_rewrites = 0;
        memset(g_loggedWeapon, 0, sizeof g_loggedWeapon);
    }
    if (!in || !game) return;
    /* new spawns: enemies kept (four lookups per new type, at most 32 spawns a frame) */
    for (int k = 0; k < 32 && g_newTail != atomic_load(&g_newHead); k++, g_newTail++)
    {
        unsigned i = g_newTail % 256;
        if (KindOf(g_newRes[i]) == 1 && g_nfoe < FOES) { g_foe[g_nfoe] = g_newEnt[i]; g_foeRes[g_nfoe] = g_newRes[i]; g_nfoe++; }
    }
    uint32_t me = atomic_load(&g_avatar);
    LogWeapons(0);
    /* keys */
    static int pF6;
    if (KeyDown(VK_F6, &pF6))
    {
        int ctrl = GetAsyncKeyState(VK_CONTROL) & 0x8000, shift = GetAsyncKeyState(VK_SHIFT) & 0x8000;
        if (ctrl)
        {
            int m = (atomic_load(&g_noiseMode) + 1) % 3;
            atomic_store(&g_noiseMode, m);
            Log("Ctrl+F6: noise mode %s%s", m == 1 ? "mute" : m == 2 ? "15 m" : "observe", g_hookOk ? "" : " (the hook is not placed: observe only does nothing)");
        }
        else if (shift)
        {
            Log("Shift+F6: dump");
            LogMe("  her record");
            LogWeapons(1);
            Dump(64, 50, 1);
        }
        else
        {
            Vis v;
            if (!g_setBase) Log("F6: SetBase is not available (the code check failed)");
            else if (!ReadVis(me, &v)) Log("F6: no visibility record for her yet");
            else if (!g_hidden)
            {
                g_hidden = 1;
                g_savedBase = v.base > 0 ? v.base : 1.0f;
                g_rewrites = 0; g_setCalls = 0; g_rewriteSince = g_now;
                g_setBase(NULL, me, 0.0f);
                g_nextSet = g_now + 0.25;
                Log("F6: hidden ON (base was %.3f)", (double)v.base);
                LogMe("  now");
            }
            else
            {
                g_hidden = 0;
                g_setBase(NULL, me, g_savedBase);
                Log("F6: hidden OFF (base back to %.3f; %u rewrites in %.0f s, %u sets)", (double)g_savedBase, g_rewrites, g_now - g_rewriteSince, g_setCalls);
                LogMe("  now");
            }
        }
    }
    /* hidden: keep the base at 0, counting when something put it back */
    if (g_hidden && g_now >= g_nextSet && g_setBase)
    {
        g_nextSet = g_now + 0.25;
        Vis v;
        if (ReadVis(me, &v))
        {
            if (v.base != 0)
            {
                g_rewrites++;
                if (g_rewrites <= 20) Log("hidden: her base was back at %.3f (rewrite %u)", (double)v.base, g_rewrites);
            }
            g_setBase(NULL, me, 0.0f);
            g_setCalls++;
        }
    }
    /* floor changes (firing) */
    Vis v;
    if (ReadVis(me, &v) && v.floor != g_lastFloor)
    {
        static unsigned told;
        if (told++ < 200) Log("floor %.3f -> %.3f (visibility %.3f)", (double)g_lastFloor, (double)v.floor, g_getVis ? (double)g_getVis(NULL, me) : -1.0);
        g_lastFloor = v.floor;
    }
    /* her noises */
    unsigned head = atomic_load(&g_ringHead);
    if (head - g_ringTail > NRING) g_ringTail = head - NRING;
    for (; g_ringTail != head; g_ringTail++)
    {
        NoiseCall c = g_ring[g_ringTail % NRING];
        static unsigned told;
        float r = c.type < 30 ? g_noiseRadius[c.type] : 0;
        if (told++ < 300)
            Log("noise: type %u (table %.0f m) %s %s, mode %u, loudness %.2f -> %.2f (radius %.1f -> %.1f m), %s", c.type, (double)r,
                c.src == me ? "from her" : "", c.inst == me ? "instigated by her" : "", c.mode, (double)c.loud, (double)c.sent, (double)(c.loud * r),
                (double)(c.sent * r), c.how == 1 ? "muted" : c.how == 2 ? "15 m mode" : "observed");
        if (g_heardCheckAt < 0)
        {
            float p[3];
            Near list[64];
            g_nHeardBefore = 0;
            if (PlayerPose(p, NULL))
            {
                int n = Nearby(p, 160, list, 64);
                for (int i = 0; i < n; i++) if (HeardOnly(list[i].e, me) && g_nHeardBefore < 64) g_heardBefore[g_nHeardBefore++] = list[i].e;
            }
            g_heardCheckAt = g_now + 0.5;
        }
    }
    if (g_heardCheckAt >= 0 && g_now >= g_heardCheckAt)
    {
        g_heardCheckAt = -1;
        float p[3];
        Near list[64];
        if (PlayerPose(p, NULL))
        {
            int n = Nearby(p, 160, list, 64), gained = 0;
            char buf[400] = "";
            for (int i = 0; i < n; i++)
            {
                if (!HeardOnly(list[i].e, me)) continue;
                int had = 0;
                for (int k = 0; k < g_nHeardBefore; k++) if (g_heardBefore[k] == list[i].e) had = 1;
                if (had) continue;
                gained++;
                size_t len = strlen(buf);
                if (len < sizeof buf - 24) snprintf(buf + len, sizeof buf - len, " %u@%.0fm", list[i].e, (double)list[i].d);
            }
            Log("  0.5 s after: %d enemies newly heard her (heard-only entry)%s", gained, buf);
        }
    }
    /* every 2 s */
    if (g_now >= g_nextLog)
    {
        g_nextLog = g_now + 2;
        LogMe("her");
        Dump(6, 50, 0);
    }
}

static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gs = 0, xs = 0;
    uintptr_t g = hf->module_base("game.dll", &gs), x = hf->module_base("helldivers2.exe", &xs);
    if (!g || !x || gs != 74727424u || xs != 60719104u) { Log("REFUSED: not build " BUILD); return; }
    game = g;
    exe = x;
    if (hf->code_matches(game + 0x720350, "48 83 ec 08 3b 15 ?? ?? ?? ?? 4c 8b 15 ?? ?? ?? ?? 75 07")) g_setBase = (SetBaseFn)(game + 0x720350);
    else Log("SetBase 0x720350 does not match: hidden mode off");
    if (hf->code_matches(game + 0x720530, "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 3b 15")) g_getVis = (GetVisFn)(game + 0x720530);
    else Log("GetVisibility 0x720530 does not match: not called");
    for (int i = 0; i < 30; i++) { float f[2]; if (ReadAt(game + 0x2140320 + 8u * i, f, 8)) g_noiseRadius[i] = f[0]; }
    if (!hf->code_matches(game + 0x64A890, "84 d2 0f 84 ad 06 00 00 4c 8b dc 57 41 56 48 81 ec 18 01 00 00"))
        Log("EmitNoise 0x64A890 does not match: noises not seen");
    else if (hf->hook(self, game + 0x64A890, (void*)NoiseHook, (void**)&g_noiseOrig))
        Log("EmitNoise: the hook could not be placed");
    else g_hookOk = 1;
    Log("ready: F6 hidden on/off, Ctrl+F6 noise mode (observe/mute/15 m), Shift+F6 dump. Noise table: type 1 %.0f m, 5 %.0f m, 10 %.0f m",
        (double)g_noiseRadius[1], (double)g_noiseRadius[5], (double)g_noiseRadius[10]);
}
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 13 || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    Log("Perception Probe " VERSION ": research for Ascalon's hidden kit (no effect until F6 / Ctrl+F6)");
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) &&
             api->on_game_thread(mod, Tick, NULL);
    if (!ok) Log("events or the game thread are unavailable");
    return 0;
}
