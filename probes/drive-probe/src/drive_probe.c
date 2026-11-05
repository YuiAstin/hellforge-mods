/* Drive Probe (yui.drive-probe): research for Koleda's Sinner, an exosuit that walks with her on its own. Exo Companion
 * (M!ni$try0fSc!eиce; docs/research/exo-companion.md) drives an empty suit through the game's own locomotion: the stick
 * input in its locomotion record, the physics mover a pilot leaves behind, the suit's state. This probe finds those
 * fields in our build and tries them. Host or solo; turn it off when done.
 *
 *   F7  (while you pilot a suit) 12 s of recording: the suit's two locomotion records (the one Exo Companion's `vloco`
 *       signature reads: [game+0x3326558] map +0x48A0, records +0x48D0, 0xA4 each; and the `loco_en` one:
 *       [game+0x3326CF8] map +0x20, records +0x50, 0xA0 each) every 0.1 s, each changed field logged with the keys you
 *       hold. Walk forward (W) a few seconds, stand, strafe (A, D), turn. At the end: the fields that follow W / A / D,
 *       and the suit's state (+0x50) and mover flag while piloted.
 *   F8  (after you climb out, near the suit) 5 s of driving the nearest exosuit: the mover on (game+0x6FE480), its state
 *       set to the piloted one (game+0x59CA90), and the field F7 found for W written each frame at its W value; where
 *       the suit is logged every 0.25 s (does it walk?). Then everything written is put back.
 *   F9  stop now (and put back).
 * Credit: the method is Exo Companion's (read from its strings and signatures); the entity and scene-graph reads are
 * the kit base's (Helldiver Survival Rework, MIT; HD2Runtime by SkyeShade). */
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

#define BUILD "25480438"
#define VERSION "0.1.0"
#include "kitbase/kb_core.h"

static atomic_uint g_avatar;
static int g_inMission;
static double g_now;

/* exosuits seen spawning */
static const uint64_t kExo[4] = {HF_ENTITY_EXO45_PATRIOT_EXOSUIT, HF_ENTITY_EXO49_EMANCIPATOR_EXOSUIT, HF_ENTITY_EXO51_LUMBERER_EXOSUIT,
                                 HF_ENTITY_EXO55_BREAKTHROUGH_EXOSUIT};
static uint32_t g_exo[32];
static uint64_t g_exoRes[32];
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    for (int k = 0; k < 4; k++)
        if (s->resource == kExo[k])
        {
            for (int i = 0; i < 32; i++) if (!g_exo[i] || g_exo[i] == s->entity) { g_exo[i] = s->entity; g_exoRes[i] = s->resource; break; }
            Log("exosuit spawned: entity %u (resource %016llx)", s->entity, (unsigned long long)s->resource);
        }
}
static void OnPlayer(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    if (p->local && info->kind == HF_ON_PLAYER_SPAWN && p->entity) atomic_store(&g_avatar, p->entity);
}
static int Pos(uint32_t e, float out[3])
{
    uint8_t d[24];
    return Descriptor(e, d) && UnitNode(Word(d + 12), 0, out);
}
static uint32_t NearestExo(float within, float* dist)
{
    float me[3], p[3];
    uint32_t best = 0;
    float bd = within * within;
    if (!Pos(atomic_load(&g_avatar), me)) return 0;
    for (int i = 0; i < 32; i++)
    {
        uint8_t d[24];
        if (!g_exo[i]) continue;
        if (!Descriptor(g_exo[i], d) || Quad(d) != g_exoRes[i]) { g_exo[i] = 0; continue; }
        if (!Pos(g_exo[i], p)) continue;
        float d2 = Dist2(me, p);
        if (d2 < bd) { bd = d2; best = g_exo[i]; }
    }
    if (dist) *dist = sqrtf(bd);
    return best;
}

/* the records */
#define VL_SIZE 0xA4
#define LC_SIZE 0xA0
static uintptr_t VlocoRec(uint32_t e)
{
    uintptr_t m = Pointer(game + 0x3326558), recs;
    uint32_t i;
    if (!m || !Lookup(m + 0x48A0, e, 1048576, &i) || i > 65535 || !(recs = Pointer(m + 0x48D0))) return 0;
    return recs + (uintptr_t)VL_SIZE * i;
}
static uintptr_t LocoRec(uint32_t e)
{
    uintptr_t m = Pointer(game + 0x3326CF8), recs;
    uint32_t i;
    if (!m || !Lookup(m + 0x20, e, 1048576, &i) || i > 65535 || !(recs = Pointer(m + 0x50))) return 0;
    return recs + (uintptr_t)LC_SIZE * i;
}
static uintptr_t MoverRec(uint32_t e)
{
    uintptr_t m = Pointer(game + 0x3326668), recs;
    uint32_t i;
    if (!m || !Lookup(m + 0x38, e, 1048576, &i) || i > 65535 || !(recs = Pointer(m + 0x68))) return 0;
    return recs + (uintptr_t)0xD28 * i;
}

/* the game's setters (Exo Companion's `mover` and the state setter its `vloco` signature leads to) */
typedef void (*MoverFn)(void*, uint32_t, uint8_t);
typedef void (*StateFn)(void*, uint32_t, uint32_t);
static MoverFn g_mover;
static StateFn g_state;

/* ---- F7: recording -------------------------------------------------------------------------------------------- */
#define SAMPLES 130
#define SLOTS ((VL_SIZE + LC_SIZE) / 4)
static float g_sample[SAMPLES][SLOTS];
static uint32_t g_raw[SAMPLES][SLOTS];
static int g_keys[SAMPLES];
static int g_nsample;
static uint32_t g_recExo;
static double g_recUntil, g_recNext;
static uint8_t g_prev[VL_SIZE + LC_SIZE];
static int g_havePrev;
static unsigned g_diffLines;
/* what F7 found */
static int g_fwdSlot = -1, g_sideSlot = -1;
static float g_fwdValue, g_sideValue;
static uint32_t g_pilotState;
static uint8_t g_pilotMover;
enum { K_W = 1, K_A = 2, K_S = 4, K_D = 8, K_SPACE = 16, K_SHIFT = 32 };
static int Keys(void)
{
    int k = 0;
    if (GetAsyncKeyState('W') & 0x8000) k |= K_W;
    if (GetAsyncKeyState('A') & 0x8000) k |= K_A;
    if (GetAsyncKeyState('S') & 0x8000) k |= K_S;
    if (GetAsyncKeyState('D') & 0x8000) k |= K_D;
    if (GetAsyncKeyState(VK_SPACE) & 0x8000) k |= K_SPACE;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) k |= K_SHIFT;
    return k;
}
static const char* SlotName(int s, char* buf, size_t n)
{
    if (s * 4 < VL_SIZE) snprintf(buf, n, "vloco+0x%02X", s * 4);
    else snprintf(buf, n, "loco+0x%02X", s * 4 - VL_SIZE);
    return buf;
}
static void RecordStart(void)
{
    float d;
    uint32_t e = NearestExo(4, &d);
    if (!e) { Log("F7: no exosuit within 4 m of you (pilot one first)"); return; }
    uintptr_t v = VlocoRec(e), l = LocoRec(e), mv = MoverRec(e);
    Log("F7: recording exosuit %u (%.1f m): vloco record %s, loco record %s, mover record %s; walk forward, stand, strafe, turn",
        e, d, v ? "found" : "NOT found", l ? "found" : "NOT found", mv ? "found" : "NOT found");
    if (!v && !l) return;
    g_recExo = e;
    g_nsample = 0;
    g_havePrev = 0;
    g_diffLines = 0;
    g_recUntil = g_now + 12;
    g_recNext = g_now;
}
static void RecordFrame(void)
{
    if (!g_recExo || g_now < g_recNext) return;
    g_recNext = g_now + 0.1;
    uint8_t cur[VL_SIZE + LC_SIZE];
    memset(cur, 0, sizeof cur);
    uintptr_t v = VlocoRec(g_recExo), l = LocoRec(g_recExo), mv = MoverRec(g_recExo);
    if (v) ReadAt(v, cur, VL_SIZE);
    if (l) ReadAt(l, cur + VL_SIZE, LC_SIZE);
    int keys = Keys();
    if (mv) ReadAt(mv + 0xD18, &g_pilotMover, 1);
    if (v) ReadAt(v + 0x50, &g_pilotState, 4);
    if (g_nsample < SAMPLES)
    {
        for (int s = 0; s < SLOTS; s++) { memcpy(&g_raw[g_nsample][s], cur + 4 * s, 4); memcpy(&g_sample[g_nsample][s], cur + 4 * s, 4); }
        g_keys[g_nsample++] = keys;
    }
    if (g_havePrev)
        for (int s = 0; s < SLOTS && g_diffLines < 600; s++)
        {
            uint32_t a, b;
            memcpy(&a, g_prev + 4 * s, 4);
            memcpy(&b, cur + 4 * s, 4);
            if (a == b) continue;
            float fa, fb;
            memcpy(&fa, &a, 4);
            memcpy(&fb, &b, 4);
            char nb[24];
            g_diffLines++;
            Log("  t%.1f keys %c%c%c%c%c%c %s: %08X -> %08X (%g -> %g)", 12 - (g_recUntil - g_now), keys & K_W ? 'W' : '-', keys & K_A ? 'A' : '-',
                keys & K_S ? 'S' : '-', keys & K_D ? 'D' : '-', keys & K_SPACE ? 'J' : '-', keys & K_SHIFT ? 'B' : '-', SlotName(s, nb, sizeof nb), a, b,
                isfinite(fa) && fabsf(fa) < 1e7 ? fa : 0.0, isfinite(fb) && fabsf(fb) < 1e7 ? fb : 0.0);
        }
    memcpy(g_prev, cur, sizeof cur);
    g_havePrev = 1;
    if (g_now < g_recUntil) return;
    /* the fields that follow the keys: the mean while a key is held against while it is not */
    Log("F7 done: %d samples; piloted state +0x50 = %08X, mover flag %u", g_nsample, g_pilotState, g_pilotMover);
    const int keyOf[3] = {K_W, K_A, K_D};
    const char* nameOf[3] = {"W", "A", "D"};
    for (int k = 0; k < 3; k++)
    {
        int bestSlot = -1;
        double best = 0, bestOn = 0;
        for (int s = 0; s < SLOTS; s++)
        {
            double on = 0, off = 0;
            int non = 0, noff = 0, sane = 1;
            for (int i = 0; i < g_nsample; i++)
            {
                float f = g_sample[i][s];
                if (!isfinite(f) || fabsf(f) > 1000) { sane = 0; break; }
                if (g_keys[i] & keyOf[k]) { on += f; non++; } else { off += f; noff++; }
            }
            if (!sane || non < 3 || noff < 3) continue;
            on /= non; off /= noff;
            double gap = fabs(on - off);
            char nb[24];
            if (gap > 0.2) Log("  %s held: %s %.3f (not held %.3f)", nameOf[k], SlotName(s, nb, sizeof nb), on, off);
            if (gap > best) { best = gap; bestSlot = s; bestOn = on; }
        }
        if (k == 0 && bestSlot >= 0) { g_fwdSlot = bestSlot; g_fwdValue = (float)bestOn; }
        if (k == 2 && bestSlot >= 0) { g_sideSlot = bestSlot; g_sideValue = (float)bestOn; }
        char nb[24];
        Log("F7: %s follows %s best (%.3f while held)", nameOf[k], bestSlot >= 0 ? SlotName(bestSlot, nb, sizeof nb) : "nothing", bestOn);
    }
    if (g_fwdSlot < 0) Log("F7: no field follows W: hold W for a few seconds next time");
    g_recExo = 0;
}

/* ---- F8: driving ---------------------------------------------------------------------------------------------- */
static uint32_t g_drvExo;
static double g_drvUntil, g_drvNext;
static uint32_t g_saveFwd, g_saveState;
static uint8_t g_saveMover;
static int g_moverSet, g_stateSet, g_fwdSet;
static float g_drvStart[3];
static uintptr_t SlotAddr(uint32_t e, int s)
{
    if (s < 0) return 0;
    if (s * 4 < VL_SIZE) { uintptr_t v = VlocoRec(e); return v ? v + 4u * s : 0; }
    uintptr_t l = LocoRec(e);
    return l ? l + (4u * s - VL_SIZE) : 0;
}
static void DriveStop(const char* why)
{
    if (!g_drvExo) return;
    uint32_t e = g_drvExo;
    float p[3];
    if (g_fwdSet) { uintptr_t a = SlotAddr(e, g_fwdSlot); if (a) hf->write(a, &g_saveFwd, 4); }
    if (g_stateSet && g_state) g_state(NULL, e, g_saveState);
    if (g_moverSet && g_mover) g_mover(NULL, e, g_saveMover);
    if (Pos(e, p)) Log("F8 %s: the suit moved %.1f m in all; put back", why, sqrtf(Dist2(p, g_drvStart)));
    g_drvExo = 0;
}
static void DriveStart(void)
{
    float d;
    uint32_t e = NearestExo(30, &d);
    if (!e) { Log("F8: no exosuit within 30 m"); return; }
    uintptr_t v = VlocoRec(e), mv = MoverRec(e);
    Log("F8: driving exosuit %u (%.1f m away) for 5 s: forward field %d (value %.3f), mover %s, state %s", e, d, g_fwdSlot, g_fwdValue,
        g_mover ? "on" : "unavailable", g_state && g_pilotState ? "set" : "left");
    g_drvExo = e;
    g_moverSet = g_stateSet = g_fwdSet = 0;
    Pos(e, g_drvStart);
    if (mv) ReadAt(mv + 0xD18, &g_saveMover, 1);
    if (v) ReadAt(v + 0x50, &g_saveState, 4);
    if (g_mover) { g_mover(NULL, e, 1); g_moverSet = 1; }
    if (g_state && g_pilotState && v && g_pilotState != g_saveState) { g_state(NULL, e, g_pilotState); g_stateSet = 1; }
    uintptr_t a = SlotAddr(e, g_fwdSlot);
    if (a && ReadAt(a, &g_saveFwd, 4)) g_fwdSet = 1;
    uint8_t after = 0;
    uint32_t st = 0;
    if (mv) ReadAt(mv + 0xD18, &after, 1);
    if (v) ReadAt(v + 0x50, &st, 4);
    Log("F8: mover flag %u -> %u, state %08X -> %08X", g_saveMover, after, g_saveState, st);
    g_drvUntil = g_now + 5;
    g_drvNext = g_now;
}
static void DriveFrame(void)
{
    if (!g_drvExo) return;
    if (g_fwdSet)
    {
        uintptr_t a = SlotAddr(g_drvExo, g_fwdSlot);
        if (a) hf->write(a, &g_fwdValue, 4);
    }
    if (g_now >= g_drvNext)
    {
        g_drvNext = g_now + 0.25;
        float p[3];
        uintptr_t v = VlocoRec(g_drvExo);
        uint32_t st = 0;
        if (v) ReadAt(v + 0x50, &st, 4);
        if (Pos(g_drvExo, p))
            Log("  t%.2f suit at %.2f %.2f %.2f (%.2f m from the start), state %08X", 5 - (g_drvUntil - g_now), p[0], p[1], p[2], sqrtf(Dist2(p, g_drvStart)), st);
    }
    if (g_now >= g_drvUntil) DriveStop("done");
}

/* ---- the frame -------------------------------------------------------------------------------------------------- */
static void Tick(HfMod* mod, float dt, void* user)
{
    g_now += dt > 0 && dt < 1 ? dt : 0;
    int in = hf->game_state(self) == HF_STATE_MISSION;
    if (in != g_inMission)
    {
        g_inMission = in;
        if (!in) { DriveStop("mission end"); g_recExo = 0; memset(g_exo, 0, sizeof g_exo); }
    }
    if (!in || !game) return;
    static int prev[3];
    int focused = Focused();
    int k[3] = {focused && (GetAsyncKeyState(VK_F7) & 0x8000), focused && (GetAsyncKeyState(VK_F8) & 0x8000), focused && (GetAsyncKeyState(VK_F9) & 0x8000)};
    if (k[0] && !prev[0]) RecordStart();
    if (k[1] && !prev[1]) { DriveStop("restarted"); DriveStart(); }
    if (k[2] && !prev[2]) { DriveStop("stopped (F9)"); if (g_recExo) { g_recExo = 0; Log("F7 stopped"); } }
    memcpy(prev, k, sizeof prev);
    RecordFrame();
    DriveFrame();
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gs = 0, xs = 0;
    uintptr_t g = hf->module_base("game.dll", &gs), x = hf->module_base("helldivers2.exe", &xs);
    if (!g || !x || gs != 74727424u || xs != 60719104u) { Log("REFUSED: not build " BUILD); return; }
    game = g;
    exe = x;
    if (hf->code_matches(game + 0x6FE480, "48 89 5c 24 10 48 89 6c 24 20 44 88 44 24 18 48 89 4c 24 08")) g_mover = (MoverFn)(game + 0x6FE480);
    else Log("the mover setter is not the reviewed code: not called");
    if (hf->code_matches(game + 0x59CA90, "41 56 3b 15 88 71 ee 02 45 8b f0 4c 8b 1d b6 9a d8 02 75 07")) g_state = (StateFn)(game + 0x59CA90);
    else Log("the state setter is not the reviewed code: not called");
    Log("ready: F7 records a suit you pilot, F8 drives the nearest empty one, F9 stops");
}
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 15 || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    Log("Drive Probe " VERSION ": research for Koleda's Sinner (an exosuit that walks with nobody in it)");
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->on_game_thread(mod, Tick, NULL);
    if (!ok) Log("events or the game thread are unavailable");
    return 0;
}
