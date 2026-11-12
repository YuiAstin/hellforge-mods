/* Cloak Probe (yui.cloak-probe): research for an invisibility ability built on the Stalker's cloak (the Terminid
 * Stalker's shimmer). The materials are the HUD side's (hud-src/cloak_probe_hud.lua, HD2Runtime's engine API); this DLL
 * finds the Stalkers and you, takes the keys and sets off the smoke. Host or solo.
 *   F12        the HUD side logs the nearest Stalker's meshes and materials and the engine's material calls
 *   Ctrl+F12   the HUD side puts the Stalker's materials on your Helldiver; a smoke puff on you first (Eagle Smoke's
 *              round, projectile 16, dropped from 3 m; smoke_round in no ini: change kSmoke)
 *   Shift+F12  the HUD side puts your own materials back
 * Every explosion within 15 m of you is logged with its row (the first 40), to learn the G-3 Smoke's own.
 * Export cloak_probe_state(1, out[12]): her position (3), the nearest Stalker's root (3), found, the three key counts,
 * the Stalker's distance, its entity. */
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

#define VERSION "0.1.12"
#define BUILD "25480438"
#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"

#define STALKER UINT64_C(0xF540CA9D9D4A422E)   /* HD2Runtime's enemy catalogue: Stalker */
static const uint32_t kSmoke = 16;            /* Eagle Smoke Strike's round (HD2Runtime attack outputs) */
static uint32_t g_stalker[64];
static unsigned g_keys[3];
static int dumped;
static float g_out[12];

static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    if (s->resource == STALKER)
    {
        for (int i = 0; i < 64; i++) if (!g_stalker[i] || g_stalker[i] == s->entity) { g_stalker[i] = s->entity; break; }
        static unsigned told;
        if (told++ < 10) Log("a Stalker spawned: entity %u", s->entity);
        return;
    }
    if (s->resource != AVATAR || !s->authority) return;
    g_spawnEntity[g_spawnNext % 8] = s->entity;
    g_spawnNet[g_spawnNext % 8] = s->network;
    g_spawnNext++;
    LearnNet();
}
static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    uint32_t v = ((const HfKillEvent*)info)->victim;
    for (int i = 0; i < 64; i++) if (g_stalker[i] == v) g_stalker[i] = 0;
}
static void OnPlayer(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    if (!p->local) return;
    if (info->kind == HF_ON_PLAYER_SPAWN && p->entity) { atomic_store(&g_avatar, p->entity); g_alive = 1; LearnNet(); }
    else g_alive = 0;
}
static unsigned g_booms;
static void OnExplosion(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfExplosionEvent* x = (const HfExplosionEvent*)info;
    float me[3];
    if (g_booms >= 40 || !PlayerPose(me, NULL) || Dist2(me, x->position) > 15 * 15) return;
    g_booms++;
    Log("explosion row %u near you (%.1f m; from entity %u, owner %u)", x->type, sqrtf(Dist2(me, x->position)), x->source, x->owner);
}
static void Smoke(void)
{
    float me[3], down[3] = {0, 0, -1};
    if (!PlayerPose(me, NULL)) return;
    float from[3] = {me[0], me[1], me[2] + 3};
    int r = hf->spawn_projectile(self, kSmoke, from, down);
    Log("smoke puff: projectile %u from 3 m above you: %s", kSmoke, r == HF_ACT_WAITING ? "waiting for its package (try again)" : r ? "sent" : "refused");
}
static void Tick(HfMod* mod, float dt, void* user)
{
    g_now += dt > 0 && dt < 1 ? dt : 0;
    int in = hf->game_state(self) == HF_STATE_MISSION;
    if (in != g_inMission) { g_inMission = in; if (!in) { memset(g_stalker, 0, sizeof g_stalker); g_booms = 0; dumped = 0; } }
    memset(g_out, 0, sizeof g_out);
    if (!in || !game) return;
    float me[3], p[3];
    if (PlayerPose(me, NULL)) memcpy(g_out, me, 12);
    float bd = 1e12f;
    uint32_t best = 0;
    for (int i = 0; i < 64; i++)
    {
        uint8_t d[24];
        if (!g_stalker[i]) continue;
        if (!Descriptor(g_stalker[i], d) || Quad(d) != STALKER) { g_stalker[i] = 0; continue; }
        if (!UnitNode(Word(d + 12), 0, p)) continue;
        float d2 = Dist2(p, me);
        if (d2 < bd) { bd = d2; best = g_stalker[i]; memcpy(g_out + 3, p, 12); }
    }
    g_out[6] = best ? 1.0f : 0.0f;
    /* 0.1.1: the first Stalker within 40 m each mission is dumped by itself (they died before F12 last time) */
    if (best && !dumped && bd < 40 * 40) { dumped = 1; g_keys[0]++; Log("a Stalker %.0f m away: dumped by itself (its materials kept for Ctrl+F12)", sqrtf(bd)); }
    g_out[10] = best ? sqrtf(bd) : 0;
    g_out[11] = (float)best;
    static int prev;
    int focused = Focused(), f12 = focused && (GetAsyncKeyState(VK_F12) & 0x8000);
    if (f12 && !prev)
    {
        int k = (GetAsyncKeyState(VK_CONTROL) & 0x8000) ? 1 : (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 2 : 0;
        g_keys[k]++;
        if (k == 1) Smoke();
        Log("%s (the nearest Stalker: %s, %.0f m)", k == 0 ? "F12: dump" : k == 1 ? "Ctrl+F12: cloak" : "Shift+F12: uncloak", best ? "found" : "none", best ? sqrtf(bd) : 0.0);
    }
    prev = f12;
    for (int k = 0; k < 3; k++) g_out[7 + k] = (float)g_keys[k];
}
HF_MOD_EXPORT int cloak_probe_state(unsigned version, float* out)
{
    if (version != 1 || !out) return 0;
    memcpy(out, g_out, sizeof g_out);
    return 1;
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gs = 0, xs = 0;
    uintptr_t g = hf->module_base("game.dll", &gs), x = hf->module_base("helldivers2.exe", &xs);
    if (!g || !x || gs != 74727424u || xs != 60719104u) { Log("REFUSED: not build " BUILD); return; }
    game = g;
    exe = x;
    Log("ready: F12 dump the nearest Stalker, Ctrl+F12 cloak, Shift+F12 uncloak (the HUD side does the materials)");
}
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 13 || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    Log("Cloak Probe " VERSION ": research for an invisibility ability on the Stalker's cloak");
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_EXPLOSION, OnExplosion, NULL, 0) && api->on_game_thread(mod, Tick, NULL);
    if (!ok) Log("events or the game thread are unavailable");
    return 0;
}
