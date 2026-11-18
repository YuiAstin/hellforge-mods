/* Corpse Probe (yui.corpse-probe): a probe, not a release. It answers one question: does switching the physics off on
 * dead enemies give frames back in a long fight?
 *
 * One key (key in corpse-probe.ini, F6) turns the freeze on and off. While it is on, every enemy that dies has its
 * unit's physics switched off `delay` s after its death (Unit.disable_physics): the body stays where it fell, as scenery.
 * Off again, the bodies frozen so far get their physics back (restore 1). Every `report` s the log has the frame rate over
 * that time, the worst frame, the enemies alive and the bodies frozen, so a fight with it on can be set beside one
 * with it off.
 *
 * This DLL sees the deaths and measures the frames. The physics part is the HUD mod's (hud-src/corpse_probe.lua, an
 * HD2Runtime mod): the engine's unit calls are only reachable from the game's Lua. It reads the deaths from
 * corpse_probe_state() and reports through corpse_probe_report and corpse_probe_log.
 *
 * Research: entity, unit and health reads from HD2Runtime by SkyeShade and Helldiver Survival Rework by
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

#define VERSION "0.1.0"
#define BUILD "25480438"

#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"

#define RING 256

static Key s_key = {VK_F6, 0, "F6"};
static float s_delay = 3, s_report = 5, s_batch = 2;
static int s_on = 0, s_restore = 1;

/* what the HUD mod reads (layout version 1; the same struct in corpse_probe.lua). A death has a serial, counted from 1;
 * it sits at dead[serial % 256]. ready: the last serial whose delay has passed */
typedef struct CpDead { uint32_t serial, unit; uint64_t res; float pos[3]; uint32_t pad; } CpDead;
typedef struct CpState
{
    uint32_t version, enabled, in_mission, restore;
    uint32_t last, ready;
    float batch, pad;
    CpDead dead[RING];
} CpState;
static CpState g_state = {.version = 1};
static double g_dueAt[RING];
static uint32_t g_foeUnit[FOES];
static unsigned g_lines;
/* what the HUD mod has done so far */
static uint32_t g_frozen, g_gone, g_failed, g_restored, g_none;
static float g_scanMs;

HF_MOD_EXPORT CpState* corpse_probe_state(void) { return &g_state; }
HF_MOD_EXPORT int corpse_probe_log(unsigned version, const char* text)
{
    if (version != 1 || !text) return 0;
    if (g_lines++ < 800) Log("[lua] %.400s", text);
    return 1;
}
HF_MOD_EXPORT int corpse_probe_report(unsigned version, unsigned frozen, unsigned gone, unsigned failed, unsigned restored, unsigned none, float scanMs)
{
    if (version != 1) return 0;
    g_frozen = frozen; g_gone = gone; g_failed = failed; g_restored = restored; g_none = none; g_scanMs = scanMs;
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

static __attribute__((unused)) void DefaultIni(FILE* f)
{
    fprintf(f, "; Corpse Probe %s (a probe, not a release). key: a letter, digit, F1-F24, Mouse3-5, with Shift+, Ctrl+ or Alt+.\n"
               "; The key turns the freeze on and off: while on, each enemy that dies has its body's physics switched off delay s\n"
               "; after its death. on: 1 starts each mission with it on. restore: 1 gives the frozen bodies their physics back\n"
               "; when it is turned off. report: the log's frame-rate line every that many s. batch: the bodies are looked up at\n"
               "; most once every that many s (each look-up walks every unit in the world).\n"
               "key = %s\non = %d\ndelay = %g\nrestore = %d\nreport = %g\nbatch = %g\n",
            VERSION, s_key.label, s_on, (double)s_delay, s_restore, (double)s_report, (double)s_batch);
}
static void LoadIni(void)
{
    char path[MAX_PATH * 2], line[256];
    snprintf(path, sizeof path, "%s\\corpse-probe.ini", hf->mod_folder(self));
    FILE* f = OpenUtf8(path, L"r");
    if (!f) { Log("settings: no corpse-probe.ini, so the defaults"); return; }
    while (fgets(line, sizeof line, f))
    {
        char* c = strchr(line, ';');
        if (c) *c = 0;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char key[64];
        if (sscanf(line, " %63s", key) != 1) continue;
        if (!strcmp(key, "key")) { Key k; if (ParseKey(eq + 1, &k)) s_key = k; else Log("corpse-probe.ini: key could not be read; %s stays", s_key.label); continue; }
        double v = strtod(eq + 1, NULL);
        if (!strcmp(key, "on")) s_on = v != 0;
        else if (!strcmp(key, "restore")) s_restore = v != 0;
        else if (!strcmp(key, "delay")) s_delay = (float)(v < 0 ? 0 : v > 60 ? 60 : v);
        else if (!strcmp(key, "report")) s_report = (float)(v < 1 ? 1 : v > 60 ? 60 : v);
        else if (!strcmp(key, "batch")) s_batch = (float)(v < 0.25 ? 0.25 : v > 30 ? 30 : v);
    }
    fclose(f);
}

static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    if (s->resource != AVATAR)
    {
        uint32_t stride;
        int foe = FoeKind(s->resource);
        if (!foe && !FriendRes(s->resource) && !hf->component_record("AvatarComponentData", s->resource, &stride))
            foe = hf->component_record("AiEnemyComponentData", s->resource, &stride) && hf->component_record("HealthComponentData", s->resource, &stride);
        if (foe) AddFoe(s->entity, s->resource);
        return;
    }
    if (!s->authority) return;
    g_spawnEntity[g_spawnNext % 8] = s->entity;
    g_spawnNet[g_spawnNext % 8] = s->network;
    g_spawnNext++;
}
/* an enemy died: its body joins the ring, due `delay` s from now */
static unsigned g_deaths, g_noUnit;
static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    uint32_t victim = ((const HfKillEvent*)info)->victim;
    for (int i = 0; i < FOES; i++)
    {
        if (g_foe[i] != victim) continue;
        uint8_t d[24];
        uint32_t unit = Descriptor(victim, d) ? Word(d + 12) : g_foeUnit[i];
        uint64_t res = g_foeRes[i];
        g_foe[i] = 0;
        g_foeUnit[i] = 0;
        if (!g_inMission) return;
        g_deaths++;
        if (!unit || unit == UINT32_MAX) { g_noUnit++; return; }
        uint32_t serial = g_state.last + 1;
        CpDead* c = &g_state.dead[serial % RING];
        c->serial = serial; c->unit = unit; c->res = res;
        if (!UnitNode(unit, 0, c->pos)) memset(c->pos, 0, 12);
        g_dueAt[serial % RING] = g_now + s_delay;
        g_state.last = serial;
        return;
    }
}
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

/* the frame-rate line */
static double g_winTime, g_winWorst, g_unitAt;
static unsigned g_winFrames, g_winSlow;
static void Report(void)
{
    int alive = 0;
    for (int i = 0; i < FOES; i++) if (g_foe[i] && Alive(g_foe[i])) alive++;
    Log("frames: %.1f fps over %.1f s (worst frame %.1f ms, %u over 33 ms); freeze %s; enemies alive %d; deaths %u; bodies frozen %u, gone before "
        "their turn %u, without physics to switch %u, failed %u, restored %u; last look-up %.1f ms",
        g_winTime > 0 ? g_winFrames / g_winTime : 0.0, g_winTime, g_winWorst * 1000, g_winSlow, g_state.enabled ? "ON" : "off", alive, g_deaths, g_frozen,
        g_gone, g_none, g_failed, g_restored, (double)g_scanMs);
    g_winTime = g_winWorst = 0;
    g_winFrames = g_winSlow = 0;
}
static void Tick(HfMod* mod, float dt, void* user)
{
    double step = dt > 0 && dt < 1 ? dt : 0;
    g_now += step;
    int inMission = hf->game_state(self) == HF_STATE_MISSION;
    if (inMission != g_inMission)
    {
        g_inMission = inMission;
        if (inMission)
        {
            g_state.enabled = (uint32_t)s_on;
            g_deaths = g_noUnit = 0;
            g_winTime = g_winWorst = 0; g_winFrames = g_winSlow = 0;
            Log("mission: the freeze starts %s (%s turns it); a body is frozen %.1f s after its death", s_on ? "ON" : "off", s_key.label, (double)s_delay);
        }
        else
        {
            if (g_deaths) Log("mission over: %u enemy deaths (%u without a unit to look up)", g_deaths, g_noUnit);
            memset(g_foe, 0, sizeof g_foe); memset(g_foeRes, 0, sizeof g_foeRes); memset(g_foeUnit, 0, sizeof g_foeUnit);
        }
    }
    g_state.in_mission = (uint32_t)g_inMission;
    g_state.restore = (uint32_t)s_restore;
    g_state.batch = s_batch;
    if (!g_inMission || !game) return;
    /* each enemy's unit, kept for the moment it dies */
    if (g_now >= g_unitAt)
    {
        g_unitAt = g_now + 1.0;
        for (int i = 0; i < FOES; i++)
        {
            uint8_t d[24];
            if (g_foe[i] && Descriptor(g_foe[i], d)) g_foeUnit[i] = Word(d + 12);
        }
    }
    while (g_state.ready < g_state.last && g_dueAt[(g_state.ready + 1) % RING] <= g_now) g_state.ready++;
    static int prev;
    int focused = Focused();
    int mods = focused ? ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? KM_SHIFT : 0) | ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? KM_CTRL : 0) |
                         ((GetAsyncKeyState(VK_MENU) & 0x8000) ? KM_ALT : 0) : 0;
    int own = s_key.vk == VK_SHIFT ? KM_SHIFT : s_key.vk == VK_CONTROL ? KM_CTRL : s_key.vk == VK_MENU ? KM_ALT : 0;
    int down = focused && (GetAsyncKeyState(s_key.vk) & 0x8000) != 0 && (mods & ~own) == s_key.mods;
    if (down && !prev)
    {
        Report();
        g_state.enabled = !g_state.enabled;
        Log("the freeze is now %s", g_state.enabled ? "ON" : "off");
    }
    prev = down;
    if (step > 0)
    {
        g_winTime += step;
        g_winFrames++;
        if (step > g_winWorst) g_winWorst = step;
        if (step > 0.033) g_winSlow++;
        if (g_winTime >= s_report) Report();
    }
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gameSize = 0, exeSize = 0;
    uintptr_t g = hf->module_base("game.dll", &gameSize), x = hf->module_base("helldivers2.exe", &exeSize);
    if (!g || !x || gameSize != 74727424u || exeSize != 60719104u) { Log("REFUSED: the game's modules are not build " BUILD "'s: the probe stays off"); return; }
    game = g;
    exe = x;
    Log("ready: %s turns the corpse freeze on and off (starts %s; %.1f s after a death; a frame-rate line every %.0f s)", s_key.label, s_on ? "ON" : "off",
        (double)s_delay, (double)s_report);
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 13 || api->size < offsetof(HfApi, code_matches) + sizeof api->code_matches) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    LoadIni();
    api->log(mod, "Corpse Probe %s: key %s", VERSION, s_key.label);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) && api->on_game_thread(mod, Tick, NULL);
    if (!ok) api->log(mod, "events or the game thread are unavailable: the probe stays off");
    return 0;
}
