/* Held Probe (yui.held-probe): research for Hina's gun (Destroyer of Denouement). The kit takes the gun away when she
 * switches off it or drops it, the way W's launcher goes, but calling a stratagem or throwing a grenade must not count
 * (Yui: it is not switching off the gun). What does the game say she holds during each?
 *
 * Logged, with the time since the mission started and the keys held at that moment (Ctrl, G, Shift, the mouse
 * buttons, 1 to 4):
 *   - every change of her inventory record (the inventory manager's 48-byte record for her: +0x00..+0x0C her items by
 *     slot, primary, secondary, support, backpack; +0x1C her selection, as W's King of Hearts reads it), each changed
 *     word with its old and new value;
 *   - every weapon event of hers (HellForge's HF_ON_WEAPON: the held entity and resource, the selection, 1 primary,
 *     2 secondary, 3 support, 4/5 held item, 0 none);
 *   - how long each selection lasted, when it changes.
 * Nothing is changed. Solo or host.
 *
 * What to do in a mission, a few seconds apart so each shows on its own: hold a support weapon, then (1) call a
 * stratagem (hold Ctrl, enter a code, throw the ball), (2) throw a grenade (G), (3) switch to the primary and back,
 * (4) switch to the secondary and back, (5) drop the support weapon (hold X: the game's drop), and (6) pick it up. */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <windows.h>
#include "hellforge_mod.h"
#include "hellforge_globals.h"

#define VERSION "0.1.0"
#define BUILD "25480438"
#include "kitbase/kb_core.h"

static atomic_uint g_avatar;
static int g_inMission;
static double g_now, g_missionAt, g_selAt;
static uint32_t g_rec[12];
static int g_have;
static unsigned g_lines;

static double T(void) { return g_now - g_missionAt; }
static void Keys(char* out, size_t n)
{
    static const struct { int vk; const char* name; } k[] = {
        {VK_CONTROL, "Ctrl"}, {'G', "G"}, {VK_SHIFT, "Shift"}, {VK_LBUTTON, "LMB"}, {VK_RBUTTON, "RMB"},
        {'1', "1"}, {'2', "2"}, {'3', "3"}, {'4', "4"}, {'X', "X"}, {'E', "E"}, {'R', "R"}};
    out[0] = 0;
    size_t len = 0;
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        if (GetAsyncKeyState(k[i].vk) & 0x8000) len += (size_t)snprintf(out + len, len < n ? n - len : 0, "%s%s", len ? "+" : "", k[i].name);
    if (!len) snprintf(out, n, "-");
}
static uintptr_t InventoryRecord(uint32_t e)
{
    uintptr_t m = (uintptr_t)hf_global_InventoryManager(self), records;
    uint32_t i;
    if (!m || !Lookup(m + 0x28, e, 1048576, &i) || i >= 65536 || !(records = Pointer(m + 0x50))) return 0;
    return records + 0x30ull * i;
}
static uint64_t ResOf(uint32_t e) { uint8_t d[24]; return e && Descriptor(e, d) ? Quad(d) : 0; }

static void RecordFrame(void)
{
    uint32_t me = atomic_load(&g_avatar), now[12];
    uintptr_t rec = InventoryRecord(me);
    if (!rec || !ReadAt(rec, now, sizeof now)) return;
    if (!g_have)
    {
        g_have = 1;
        memcpy(g_rec, now, sizeof now);
        g_selAt = g_now;
        Log("%7.2f her record: items %u %u %u %u (%016llx %016llx %016llx %016llx), selection %u; words %08x %08x %08x %08x %08x %08x %08x %08x",
            T(), now[0], now[1], now[2], now[3], (unsigned long long)ResOf(now[0]), (unsigned long long)ResOf(now[1]), (unsigned long long)ResOf(now[2]),
            (unsigned long long)ResOf(now[3]), now[7], now[4], now[5], now[6], now[7], now[8], now[9], now[10], now[11]);
        return;
    }
    if (!memcmp(now, g_rec, sizeof now)) return;
    char keys[96];
    Keys(keys, sizeof keys);
    for (int i = 0; i < 12 && g_lines < 3000; i++)
    {
        if (now[i] == g_rec[i]) continue;
        g_lines++;
        if (i == 7) Log("%7.2f selection %u -> %u after %.2f s   [keys %s]", T(), g_rec[i], now[i], g_now - g_selAt, keys);
        else if (i < 4) Log("%7.2f slot %d (+0x%02X) item %u -> %u (%016llx -> %016llx)   [keys %s]", T(), i, 4 * i, g_rec[i], now[i],
                            (unsigned long long)ResOf(g_rec[i]), (unsigned long long)ResOf(now[i]), keys);
        else Log("%7.2f word +0x%02X %08x -> %08x   [keys %s]", T(), 4 * i, g_rec[i], now[i], keys);
    }
    if (now[7] != g_rec[7]) g_selAt = g_now;
    memcpy(g_rec, now, sizeof now);
}

static void OnWeapon(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfWeaponEvent* w = (const HfWeaponEvent*)info;
    if (!w->avatar || w->avatar != atomic_load(&g_avatar) || g_lines >= 3000) return;
    char keys[96];
    Keys(keys, sizeof keys);
    g_lines++;
    Log("%7.2f weapon event: selection %u -> %u, held %u -> %u (%016llx -> %016llx)   [keys %s]", T(), w->previous_selection, w->selection,
        w->previous, w->entity, (unsigned long long)w->previous_resource, (unsigned long long)w->resource, keys);
}
static void OnPlayer(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    if (!p->local) return;
    if (info->kind == HF_ON_PLAYER_SPAWN && p->entity)
    {
        atomic_store(&g_avatar, p->entity);
        g_have = 0;
        Log("%7.2f her Helldiver: entity %u", T(), p->entity);
    }
    else if (info->kind == HF_ON_PLAYER_DOWN) Log("%7.2f she is down", T());
}
static void Tick(HfMod* mod, float dt, void* user)
{
    g_now += dt > 0 && dt < 1 ? dt : 0;
    int in = hf->game_state(self) == HF_STATE_MISSION;
    if (in != g_inMission)
    {
        g_inMission = in;
        g_missionAt = g_now;
        g_have = 0;
        Log("---- mission %s ----", in ? "start" : "end");
    }
    if (g_inMission && game && atomic_load(&g_avatar)) RecordFrame();
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gameSize = 0, exeSize = 0;
    uintptr_t g = hf->module_base("game.dll", &gameSize), x = hf->module_base("helldivers2.exe", &exeSize);
    if (!g || !x || gameSize != 74727424u || exeSize != 60719104u) { Log("REFUSED: the game's modules are not build " BUILD "'s"); return; }
    game = g;
    exe = x;
    Log("ready: in a mission, with a support weapon in hand, call a stratagem, throw a grenade, switch away and back, drop it");
}
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 16 || api->size < offsetof(HfApi, global) + sizeof api->global || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    Log("Held Probe " VERSION ": research for Hina's gun (what she holds during a stratagem, a grenade, a switch, a drop)");
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) && api->on_game_thread(mod, Tick, NULL);
    if (!ok) Log("events or the game thread are unavailable");
    return 0;
}
