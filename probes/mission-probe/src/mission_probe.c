/* Mission Probe (yui.mission-probe): research for Commando Plus and Lost Contact. Host or solo; reads only, except the
 * dump file it writes to the session's logs folder.
 *
 *   at mission start  which hive mind entity the mission spawned: one per mission mode (HD2Runtime's missions domain:
 *                     a HiveMindComponentData owner per faction and mode, which holds the mission's timer), so a
 *                     Commando mission names its mode here
 *   after landing     the local player's stratagem record (game+0x347CE50, a 0x1690 record per peer, entries 0x30 each
 *                     from +0x188 of +0x38, count at +0x788: type, uses, granted, cooldown end), once 8 s after the
 *                     drop-in: what Commando limits and how (the Super Destroyer call-in)
 *   Ctrl+F11          the stratagem record again, now
 *   Ctrl+Shift+F11    game.dll's image as it is loaded, to <logs>\game.dll.image (for the despawn research: the game
 *                     code is not on disk). About 75 MB; unreadable pages are written as zeros.
 *
 * Credit: the stratagem record's layout is HD2Runtime's research (SkyeShade), as the Veterancy System and Voymastina use
 * it; the hive mind per mission mode is from HD2Runtime's missions domain. Nothing of their code is copied. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "hellforge_mod.h"

#define BUILD "25480438"
#define VERSION "0.1.0"
static const HfApi* hf;
static HfMod* self;
static uintptr_t game;
static size_t g_gameSize;
static int g_ready;
static double g_now;
#define Log(...) hf->log(self, __VA_ARGS__)
static int RD(uintptr_t a, void* out, size_t n) { return a >= 0x10000 && a < 0x7FFFFFFF0000ull && hf->read(a, out, n) == 0; }
static uintptr_t P(uintptr_t a) { uintptr_t v = 0; return RD(a, &v, 8) && v >= 0x10000 && v < 0x7FFFFFFF0000ull ? v : 0; }

/* ---- the hive mind: which mission mode ------------------------------------------------------------------------------ */
static volatile uint64_t g_hive[8];
static volatile unsigned g_nhive;
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    (void)mod; (void)user;
    const HfSpawnEvent* e = (const HfSpawnEvent*)info;
    uint32_t stride;
    if (!e->resource || !hf->component_record("HiveMindComponentData", e->resource, &stride)) return;
    unsigned n = g_nhive;
    for (unsigned i = 0; i < n && i < 8; i++) if (g_hive[i] == e->resource) return;
    if (n < 8) { g_hive[n] = e->resource; g_nhive = n + 1; }
}

/* ---- the stratagem record ------------------------------------------------------------------------------------------- */
static uintptr_t LocalStratagems(void)
{
    uintptr_t recs = P(game + 0x347CE50u), peerObj = P(game + 0x347CEF0u);
    uint64_t me = 0;
    uint32_t n = 0;
    if (!recs || !peerObj || !RD(peerObj + 0xB398, &me, 8) || !me || !RD(recs + 0x2D200, &n, 4) || !n || n > 32) return 0;
    for (uint32_t r = 0; r < n; r++)
    {
        uint64_t peer = 0;
        uintptr_t rec = recs + (uintptr_t)r * 0x1690u;
        if (RD(rec, &peer, 8) && peer == me) return rec + 0x38;
    }
    return 0;
}
static int Record(const char* why)
{
    uintptr_t st = LocalStratagems(), clockObj = P(game + 0x3326348u);
    uint32_t count = 0;
    uint64_t now = 0;
    if (!st || !RD(st + 0x788, &count, 4) || count > 16) { Log("%s: the stratagem record is not readable yet", why); return 0; }
    if (clockObj) RD(clockObj + 0x18, &now, 8);
    Log("%s: %u stratagems (clock %llu)", why, count, (unsigned long long)now);
    for (uint32_t i = 0; i < count; i++)
    {
        uint8_t e[0x30];
        if (!RD(st + 0x188 + i * 0x30u, e, sizeof e)) break;
        uint32_t type, uses;
        uint64_t q10, q18, q20;
        memcpy(&type, e, 4); memcpy(&uses, e + 4, 4);
        memcpy(&q10, e + 0x10, 8); memcpy(&q18, e + 0x18, 8); memcpy(&q20, e + 0x20, 8);
        char hex[0x30 * 3 + 1];
        for (int k = 0; k < 0x30; k++) sprintf(hex + k * 3, "%02x ", e[k]);
        Log("  slot %2u: type %3u, uses %s%u, granted %u, +8 %02x, +0x10 %llu, +0x18 %llu, cooldown end +0x20 %llu (%s); %s", i, type,
            uses == 0xFFFFFFFFu ? "unlimited " : "", uses == 0xFFFFFFFFu ? 0 : uses, e[9], e[8], (unsigned long long)q10, (unsigned long long)q18,
            (unsigned long long)q20, q20 > now ? "cooling down" : "ready", hex);
    }
    return 1;
}

/* ---- the game.dll image ------------------------------------------------------------------------------------------- */
static void Dump(void)
{
    const char* folder = hf->logs_folder ? hf->logs_folder(self) : NULL;
    if (!folder || !folder[0] || !game || !g_gameSize) { Log("dump: no logs folder or no game.dll"); return; }
    char path[1024];
    snprintf(path, sizeof path, "%s\\game.dll.image", folder);
    FILE* f = fopen(path, "wb");
    if (!f) { Log("dump: %s cannot be written", path); return; }
    static uint8_t page[0x10000];
    size_t bad = 0;
    for (size_t off = 0; off < g_gameSize; off += sizeof page)
    {
        size_t n = g_gameSize - off < sizeof page ? g_gameSize - off : sizeof page;
        if (hf->read(game + off, page, n))
        {
            /* a block with an unreadable page: page by page */
            for (size_t p = 0; p < n; p += 0x1000)
                if (hf->read(game + off + p, page + p, 0x1000)) { memset(page + p, 0, 0x1000); bad++; }
        }
        fwrite(page, 1, n, f);
    }
    fclose(f);
    Log("dump: game.dll (base %llx, %zu bytes) written to %s, %zu unreadable pages as zeros", (unsigned long long)game, g_gameSize, path, bad);
}

/* ---- the game thread ---------------------------------------------------------------------------------------------- */
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    (void)mod; (void)event; (void)user;
    size_t gs = 0, xs = 0;
    game = hf->module_base("game.dll", &gs);
    uintptr_t exe = hf->module_base("helldivers2.exe", &xs);
    if (!game || !exe || gs != 74727424u || xs != 60719104u) { g_ready = -1; Log("REFUSED: not build " BUILD "'s modules"); return; }
    g_gameSize = gs;
    g_ready = 1;
}
static int Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
static void Tick(HfMod* mod, float dt, void* user)
{
    (void)mod; (void)user;
    g_now += dt > 0 && dt < 1 ? dt : 0;
    if (g_ready != 1) return;
    static int inMission;
    static double recordAt;
    static unsigned hiveTold;
    int m = hf->game_state(self) == HF_STATE_MISSION;
    if (m != inMission)
    {
        inMission = m;
        if (m) { recordAt = g_now + 8; hiveTold = 0; g_nhive = 0; Log("mission start"); }
        else Log("mission end");
    }
    if (m)
    {
        unsigned n = g_nhive;
        for (; hiveTold < n && hiveTold < 8; hiveTold++)
            Log("hive mind: %016llx (the mission's mode; tell which mission this was)", (unsigned long long)g_hive[hiveTold]);
        if (recordAt > 0 && g_now >= recordAt) { if (Record("after landing")) recordAt = 0; else recordAt = g_now + 3; }
    }
    static int prev;
    int d = Down(VK_F11) && Down(VK_CONTROL);
    if (d && !prev)
    {
        if (Down(VK_SHIFT)) Dump();
        else Record("Ctrl+F11");
    }
    prev = d;
}
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 15 || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    Log("Mission Probe " VERSION ": research only; the mission's hive mind at start, the stratagem record after landing; Ctrl+F11 the record, Ctrl+Shift+F11 dumps game.dll");
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->on_game_thread(mod, Tick, NULL);
    if (!ok) Log("events or the game thread are unavailable");
    return 0;
}
