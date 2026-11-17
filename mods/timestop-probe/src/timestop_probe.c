/* Time Stop Probe (yui.timestop-probe): a probe, not a release. It answers one question for the Hakumen plan
 * (docs/hakumen-design.md): can the enemies around the player be held in the pose they are in?
 *
 * One key (key in timestop-probe.ini, F7) starts a stop of `seconds` on every living enemy within `range` m. Each press
 * takes the next method:
 *   1  stun only: STUN_LARGE on each, again every 0.5 s (the baseline: what the kits can already do)
 *   2  stun, and each enemy's animation state machine switched off for the stop (its pose stays: 0.1.0's finding)
 *   3  the state machine off alone
 * 0.2.0: in 2 and 3 each enemy is also held where it stood (hold 1): 0.1.0's enemies kept their pose and slid on (one
 * 20 m in 5 s). Its destination is set to its own spot, its velocity zeroed every frame, and it is put back when it has
 * drifted (the warp setter Koleda's pull uses, on enemies with a navigation record only).
 * This DLL finds the enemies, stuns them and measures how far each moved. The pose part is the HUD mod's
 * (hud-src/timestop_probe.lua, an HD2Runtime mod): the engine's animation calls are only reachable from the game's Lua.
 * It reads the targets from timestop_probe_state() and writes what it found into this log (timestop_probe_log).
 *
 * Solo or host. Research: entity, unit and health reads from HD2Runtime by SkyeShade and Helldiver Survival Rework by
 * enochlophobianumerophobia (MIT), through the kit base. */
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <windows.h>
#include "hellforge_mod.h"
#include "hellforge_components.h"
#include "hellforge_content.h"

#define VERSION "0.2.0"
#define BUILD "25480438"

#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"

#define TARGETS 48
#define METHODS 3

static Key s_key = {VK_F7, 0, "F7"};
static float s_range = 50, s_seconds = 5;
static int s_rareMax = 4, s_method = 0, s_hold = 1;

/* what the HUD mod reads (layout version 1; the same struct in timestop_probe.lua) */
typedef struct TsTarget { uint32_t entity, unit; uint64_t object; float pos[3]; uint32_t pad; } TsTarget;
typedef struct TsState
{
    uint32_t version, seq, active, method, count, rare_max;
    uint32_t player_unit, in_mission;
    uint64_t player_object;
    float player_pos[3], left;
    TsTarget t[TARGETS];
} TsState;
static TsState g_state = {.version = 1};
static double g_until, g_stunAt, g_destAt;
static unsigned g_pins, g_noNav;
/* the warp setter and the velocity setter (Voymastina's and Koleda's: game.dll+0x5A2BB0, +0x4A7550) and set_destination
 * (game.dll+0x5B36C0, as Lost Contact calls it) */
typedef void (*SetVecFn)(void*, uint32_t, const float*);
static SetVecFn g_setPosition, g_setVelocity, g_setDest;
static int Boundary(uintptr_t at)
{
    uint8_t b[2];
    return hf->size >= offsetof(HfApi, read_code) + sizeof hf->read_code && hf->read_code && !hf->read_code(at - 1, b, 2) &&
           (b[0] == 0xCC || b[0] == 0xC3 || b[0] == 0x90) && b[1] != 0xCC;
}
/* an entity with an active navigation record ([game+0x3326418]: map +0x30, records +0x50, 0xA8 each, +0x10 active): the
 * warp setter crashed on entities without one */
static int Walks(uint32_t e)
{
    uintptr_t m = Pointer(game + 0x3326418), recs;
    uint32_t index, active = 0;
    if (!m || !Lookup(m + 0x30, e, 1048576, &index) || index > 65535 || !(recs = Pointer(m + 0x50))) return 0;
    return ReadAt(recs + 0xA8u * index + 0x10, &active, 4) && (active & 0xFF) != 0;
}
static float g_startPos[TARGETS][3];
static unsigned g_lines;

HF_MOD_EXPORT TsState* timestop_probe_state(void) { return &g_state; }
HF_MOD_EXPORT int timestop_probe_log(unsigned version, const char* text)
{
    if (version != 1 || !text) return 0;
    if (g_lines++ < 600) Log("[lua] %.400s", text);
    return 1;
}

/* an entity's health record (HD2Runtime's research, as the kits read it): health at +20, life at +412 (0 alive) */
static uintptr_t HealthRec(uint32_t entity)
{
    uintptr_t m = Pointer(game + 53634696u);
    uint8_t h[88];
    if (!entity || !m || !ReadAt(m + 4112, h, sizeof h)) return 0;
    uint32_t live = Word(h + 16), cap = Word(h + 40), empty = Word(h + 44), mult = Word(h + 48);
    uintptr_t buckets = (uintptr_t)Quad(h + 32), records = (uintptr_t)Quad(h + 72);
    if (!buckets || !records || !cap || cap > 65536 || (cap & (cap - 1)) || live > 4096) return 0;
    uint32_t start = entity * mult;
    for (uint32_t k = 0; k < cap; k++)
    {
        uint8_t b[8];
        if (!ReadAt(buckets + 8u * ((start + k) & (cap - 1)), b, 8)) return 0;
        uint32_t key = Word(b), index = Word(b + 4);
        if (key == empty) return 0;
        if (key != entity) continue;
        if (index >= live) return 0;
        return records + 440u * index;
    }
    return 0;
}
static int Alive(uint32_t entity)
{
    uintptr_t r = HealthRec(entity);
    uint8_t b[416];
    return r && ReadAt(r, b, sizeof b) && (int32_t)Word(b + 20) > 0 && Word(b + 412) == 0;
}
/* a unit's object in the engine's unit registry (as UnitNode finds it): 0 when the unit is gone */
static uintptr_t UnitObject(uint32_t unit)
{
    if (!exe) return 0;
    uintptr_t registry = Pointer(exe + 0x1A100F0), generations, objects, object;
    uint32_t count, id, slot = unit & 0x3FFFFF;
    uint8_t generation;
    if (!registry || !ReadAt(registry + 0x98, &count, 4) || count > 0x400000 || slot >= count ||
        !(generations = Pointer(registry + 0xA0)) || !ReadAt(generations + slot, &generation, 1) || generation != (uint8_t)(unit >> 22) ||
        !(objects = Pointer(registry + 0x88)) || !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit)
        return 0;
    return object;
}

static __attribute__((unused)) void DefaultIni(FILE* f)
{
    fprintf(f, "; Time Stop Probe %s (a probe, not a release). key: a letter, digit, F1-F24, Mouse3-5, with Shift+, Ctrl+ or Alt+.\n"
               "; Each press stops the enemies within range m for seconds s, with the next method: 1 stun only, 2 stun and the\n"
               "; animation state machine off (the pose freeze), 3 the state machine off alone. method: 0 takes them in turn,\n"
               "; 1-3 always that one.\n"
               "key = %s\nrange = %g\nseconds = %g\nmethod = %d\n"
               "; hold: in methods 2 and 3, 1 also holds each enemy where it stood (0: as 0.1.0, they slide).\nhold = %d\n"
               "; rare_max: when a unit cannot be found by its id, the HUD mod looks for it by its place among unit types with at\n"
               "; most this many units in the world (reading the place of a unit of a mass-instanced type has crashed the game).\n"
               "rare_max = %d\n",
            VERSION, s_key.label, (double)s_range, (double)s_seconds, s_method, s_hold, s_rareMax);
}
static void LoadIni(void)
{
    char path[MAX_PATH * 2], line[256];
    snprintf(path, sizeof path, "%s\\timestop-probe.ini", hf->mod_folder(self));
    FILE* f = OpenUtf8(path, L"r");
    if (!f) { Log("settings: no timestop-probe.ini, so the defaults"); return; }
    while (fgets(line, sizeof line, f))
    {
        char* c = strchr(line, ';');
        if (c) *c = 0;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char key[64];
        if (sscanf(line, " %63s", key) != 1) continue;
        if (!strcmp(key, "key")) { Key k; if (ParseKey(eq + 1, &k)) s_key = k; else Log("timestop-probe.ini: key could not be read; %s stays", s_key.label); continue; }
        double v = strtod(eq + 1, NULL);
        if (!strcmp(key, "range")) s_range = (float)(v < 5 ? 5 : v > 300 ? 300 : v);
        else if (!strcmp(key, "seconds")) s_seconds = (float)(v < 1 ? 1 : v > 30 ? 30 : v);
        else if (!strcmp(key, "method")) s_method = (int)(v < 0 ? 0 : v > METHODS ? METHODS : v);
        else if (!strcmp(key, "hold")) s_hold = v != 0;
        else if (!strcmp(key, "rare_max")) s_rareMax = (int)(v < 1 ? 1 : v > 64 ? 64 : v);
    }
    fclose(f);
}

static void Start(void)
{
    float me[3];
    uint8_t d[24];
    uint32_t avatar = atomic_load(&g_avatar);
    if (!PlayerPose(me, NULL) || !Descriptor(avatar, d)) { Log("no stop: the player's place could not be read"); return; }
    static int turn;
    int method = s_method ? s_method : 1 + turn++ % METHODS;
    /* the nearest living enemies within range */
    struct { uint32_t e; uint64_t res; float d2, pos[3]; uint32_t unit; } best[TARGETS];
    int n = 0, seen = 0;
    for (int i = 0; i < FOES; i++)
    {
        uint32_t e = g_foe[i];
        uint8_t ed[24];
        float p[3];
        if (!e || !Descriptor(e, ed) || !Alive(e) || !UnitNode(Word(ed + 12), 0, p)) continue;
        float d2 = Dist2(p, me);
        if (d2 > s_range * s_range) continue;
        seen++;
        int at = n < TARGETS ? n++ : -1;
        if (at < 0) { for (int k = 0; k < TARGETS; k++) if (best[k].d2 > d2 && (at < 0 || best[k].d2 > best[at].d2)) at = k; if (at < 0) continue; }
        best[at].e = e; best[at].res = g_foeRes[i]; best[at].d2 = d2; best[at].unit = Word(ed + 12); memcpy(best[at].pos, p, 12);
    }
    g_state.player_unit = Word(d + 12);
    g_state.player_object = UnitObject(g_state.player_unit);
    memcpy(g_state.player_pos, me, 12);
    g_state.method = (uint32_t)method;
    g_state.count = (uint32_t)n;
    g_state.rare_max = (uint32_t)s_rareMax;
    for (int k = 0; k < n; k++)
    {
        TsTarget* t = &g_state.t[k];
        t->entity = best[k].e; t->unit = best[k].unit; t->object = UnitObject(best[k].unit); memcpy(t->pos, best[k].pos, 12);
        memcpy(g_startPos[k], best[k].pos, 12);
    }
    g_until = g_now + s_seconds;
    g_stunAt = g_destAt = 0;
    g_pins = g_noNav = 0;
    g_state.seq++;
    g_state.active = 1;
    Log("stop %u: method %d (%s), %d of %d enemies within %.0f m, for %.1f s; the player's unit %08x (object %p)", g_state.seq, method,
        method == 1 ? "stun only" : method == 2 ? (s_hold ? "stun, the state machine off, held in place" : "stun and the state machine off") :
        s_hold ? "the state machine off, held in place" : "the state machine off alone", n, seen, (double)s_range,
        (double)s_seconds, g_state.player_unit, (void*)(uintptr_t)g_state.player_object);
    for (int k = 0; k < n && k < 12; k++)
        Log("  target %d: entity %u, kind %016llx, unit %08x (object %p), %.1f m away", k, best[k].e, (unsigned long long)best[k].res, best[k].unit,
            (void*)(uintptr_t)g_state.t[k].object, (double)sqrtf(best[k].d2));
}
static void End(void)
{
    int alive = 0, gone = 0, still = 0;
    float most = 0, sum = 0;
    for (unsigned k = 0; k < g_state.count; k++)
    {
        TsTarget* t = &g_state.t[k];
        uint8_t ed[24];
        float p[3];
        if (!Descriptor(t->entity, ed) || Word(ed + 12) != t->unit || !Alive(t->entity) || !UnitNode(t->unit, 0, p)) { gone++; continue; }
        float moved = sqrtf(Dist2(p, g_startPos[k]));
        alive++;
        sum += moved;
        if (moved > most) most = moved;
        if (moved < 0.25f) still++;
        if (k < 12) Log("  target %u (entity %u): moved %.2f m in the stop", k, t->entity, (double)moved);
    }
    if (g_state.method >= 2 && s_hold)
        Log("the hold: %u put-backs in the stop; %u enemies without a navigation record were not held (the warp setter %s)", g_pins, g_noNav,
            g_setPosition ? "on" : "off");
    Log("stop %u over (method %u): %d alive (%d moved under 0.25 m; mean %.2f m, most %.2f m), %d dead or gone", g_state.seq, g_state.method, alive,
        still, alive ? (double)(sum / (float)alive) : 0.0, (double)most, gone);
    g_state.active = 0;
}
/* 0.2.0: each enemy kept where it stood */
static void HoldFrame(void)
{
    static const float zero[3] = {0, 0, 0};
    if (!g_setPosition || !QueryIdle()) return;
    int dest = g_setDest && g_now >= g_destAt;
    if (dest) g_destAt = g_now + 1.0;
    unsigned noNav = 0;
    for (unsigned k = 0; k < g_state.count; k++)
    {
        TsTarget* t = &g_state.t[k];
        uint8_t d[24];
        float p[3];
        uint32_t e = t->entity;
        /* only while it lives, is still the same unit and is this machine's (a dead one crashed the warp setter) */
        if (!e || e >= 0x400000u || !Descriptor(e, d) || Word(d + 12) != t->unit || !(Word(d + 20) & 1) || !Alive(e) || !UnitNode(t->unit, 0, p)) continue;
        if (!Walks(e)) { noNav++; continue; }
        if (dest) g_setDest(NULL, e, g_startPos[k]);
        g_setVelocity(NULL, e, zero);
        if (Dist2(p, g_startPos[k]) > 0.03f * 0.03f) { g_setPosition(NULL, e, g_startPos[k]); g_pins++; }
    }
    g_noNav = noNav;
}
static void StopFrame(void)
{
    if (!g_state.active) return;
    g_state.left = (float)(g_until - g_now);
    if (g_now >= g_until || !g_alive) { End(); return; }
    if (g_state.method >= 2 && s_hold) HoldFrame();
    if (g_state.method == 3 || g_now < g_stunAt || !hf->apply_status) return;
    g_stunAt = g_now + 0.5;
    int ok = 0, tried = 0;
    for (unsigned k = 0; k < g_state.count; k++)
    {
        uint32_t e = g_state.t[k].entity;
        if (!Alive(e)) continue;
        tried++;
        int r = hf->apply_status(self, e, HF_STATUS_STUN_LARGE, 100.0f);
        if (r == HF_ACT_DONE || r == HF_ACT_REQUESTED || r == HF_ACT_QUEUED) ok++;
    }
    static unsigned told;
    if (told++ < 12) Log("stun: applied to %d of %d", ok, tried);
}

static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    if (s->resource != AVATAR)
    {
        uint32_t stride;
        int foe = FoeKind(s->resource);
        /* also what the kit base's test leaves out: an enemy's turret, anything with the AI's enemy record and health */
        if (!foe && !FriendRes(s->resource) && !hf->component_record("AvatarComponentData", s->resource, &stride))
            foe = (hf->component_record("TurretComponentData", s->resource, &stride) && hf->component_record("BehaviorComponentData", s->resource, &stride)) ||
                  (hf->component_record("AiEnemyComponentData", s->resource, &stride) && hf->component_record("HealthComponentData", s->resource, &stride));
        if (foe) AddFoe(s->entity, s->resource);
        return;
    }
    if (!s->authority) return;
    g_spawnEntity[g_spawnNext % 8] = s->entity;
    g_spawnNet[g_spawnNext % 8] = s->network;
    g_spawnNext++;
}
static void OnKill(HfMod* mod, const HfEventInfo* info, void* user) { DropFoe(((const HfKillEvent*)info)->victim); }
static void OnPlayer(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    if (!p->local) return;
    if (info->kind == HF_ON_PLAYER_SPAWN)
    {
        if (p->entity && p->entity != atomic_load(&g_avatar)) { atomic_store(&g_avatar, p->entity); g_avatarNet = 0; }
        g_alive = 1;
    }
    else g_alive = 0;
}
static void Tick(HfMod* mod, float dt, void* user)
{
    double step = dt > 0 && dt < 1 ? dt : 0;
    g_now += step;
    int inMission = hf->game_state(self) == HF_STATE_MISSION;
    if (inMission != g_inMission)
    {
        g_inMission = inMission;
        if (g_state.active) { g_state.active = 0; Log("stop %u cut short: the mission ended", g_state.seq); }
        if (!inMission) { memset(g_foe, 0, sizeof g_foe); memset(g_foeRes, 0, sizeof g_foeRes); }
    }
    g_state.in_mission = (uint32_t)g_inMission;
    if (!g_inMission || !game) return;
    static int prev;
    int focused = Focused();
    int mods = focused ? ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? KM_SHIFT : 0) | ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? KM_CTRL : 0) |
                         ((GetAsyncKeyState(VK_MENU) & 0x8000) ? KM_ALT : 0) : 0;
    int own = s_key.vk == VK_SHIFT ? KM_SHIFT : s_key.vk == VK_CONTROL ? KM_CTRL : s_key.vk == VK_MENU ? KM_ALT : 0;
    int down = focused && (GetAsyncKeyState(s_key.vk) & 0x8000) != 0 && (mods & ~own) == s_key.mods;
    if (down && !prev && !g_state.active && g_alive && atomic_load(&g_avatar)) Start();
    prev = down;
    StopFrame();
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gameSize = 0, exeSize = 0;
    uintptr_t g = hf->module_base("game.dll", &gameSize), x = hf->module_base("helldivers2.exe", &exeSize);
    if (!g || !x || gameSize != 74727424u || exeSize != 60719104u) { Log("REFUSED: the game's modules are not build " BUILD "'s: the probe stays off"); return; }
    game = g;
    exe = x;
    if (hf->code_matches(game + 0x5A2BB0, "48 89 5c 24 08 48 89 6c 24 10 56 57 41 54 41 56 41 57 48 83 ec 40") &&
        hf->code_matches(game + 0x4A7550, "48 89 5c 24 18 57 48 83 ec 20 3b 15 ?? ?? ?? ?? 48 8b 1d"))
    {
        g_setPosition = (SetVecFn)(game + 0x5A2BB0);
        g_setVelocity = (SetVecFn)(game + 0x4A7550);
    }
    if (Boundary(game + 0x5B36C0)) g_setDest = (SetVecFn)(game + 0x5B36C0);
    Log("the hold: the warp setter %s, set_destination %s", g_setPosition ? "ready" : "off (not the reviewed code): enemies are not held in place",
        g_setDest ? "on" : "off");
    Log("ready: %s stops the enemies within %.0f m for %.1f s (method %s)", s_key.label, (double)s_range, (double)s_seconds,
        s_method ? "fixed" : "1, 2, 3 in turn");
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 13 || api->size < offsetof(HfApi, code_matches) + sizeof api->code_matches) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    LoadIni();
    api->log(mod, "Time Stop Probe %s: key %s", VERSION, s_key.label);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) && api->on_game_thread(mod, Tick, NULL);
    if (!ok) api->log(mod, "events or the game thread are unavailable: the probe stays off");
    return 0;
}
