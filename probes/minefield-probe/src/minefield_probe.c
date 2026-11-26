/* Minefield Probe (yui.minefield-probe): research for Veterancy System's MD-6 Anti-Personnel Minefield. Not for release.
 * Game build 25480438 only.
 *
 * Two questions, before the MD-6 ranks up (Veteran: blown mines grow back; Elite: a mine an enemy comes near launches a
 * homing shot that carries its blast):
 *  1. Does the game credit the minefield's kills to it? Every mission stat the game grows is logged with its sources
 *     (HF_ON_STAT), the MD-6's (its deployer, 0xF00C349422E8E320) marked, and every MD-6 mine blast (explosion 366) with
 *     where it came from.
 *  2. Can one of the game's own mines be switched off and taken away? The minefield manager (game.dll+0x3326958: fields
 *     counted at +0x848, records at +0x880, 0x15DC bytes each; a record's mines from +0x14, 0x74 bytes each: +0 the
 *     mine's unit, +4 its place, +0x10 its state, 1 armed; W's research) is read once a second and its changes logged.
 *     In a mission, F7 takes away every other armed mine of the newest field: its state is set to 0 and its unit removed
 *     the way W removes her mines' units (the game's own unit removal). Then walk enemies, or yourself, through the field:
 *     every MD-6 blast is matched against the taken mines' places, so the log says whether a taken mine still blew. */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "hellforge_mod.h"

#define VERSION "0.1.0"
#define BUILD "25480438"
#define MD6_DEPLOYER 0xF00C349422E8E320ull
#define MD6_BLAST 366u
#define FIELD_STRIDE 0x15DCull
#define MINE_STRIDE 0x74ull
#define TAKEN 48

static const HfApi* hf;
static HfMod* self;
static uintptr_t game;
static int g_inMission, g_removeOk;
static double g_now;
static unsigned g_logs;
typedef struct Taken { float pos[3]; double at; int blew; } Taken;
static Taken g_taken[TAKEN];
static int g_nTaken;
static uint32_t g_lastFields, g_lastArmed[8], g_lastCount[8];
static unsigned g_md6Kills, g_md6Blasts, g_takenBlasts;

static void Log(const char* fmt, ...)
{
    char text[900];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    hf->log(self, "%s", text);
}
static int ReadAt(uintptr_t a, void* out, size_t n) { return a > 0x10000 && hf->read(a, out, n) == 0; }
static uintptr_t Pointer(uintptr_t a) { uintptr_t v = 0; return ReadAt(a, &v, 8) ? v : 0; }
static uint32_t Word(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static int Focused(void)
{
    DWORD pid = 0;
    HWND w = GetForegroundWindow();
    if (!w) return 0;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

/* the game's unit removal, as W 0.7.8 takes her mines' units away (checked against the reviewed code in Loaded) */
static int RemoveUnit(uint32_t handle)
{
    typedef void (*DestroyFn)(void* world, uint32_t unit);
    uintptr_t api, table;
    DestroyFn destroy;
    void* world;
    if (!handle || !g_removeOk || !(api = Pointer(game + 0x3326308)) || !(table = Pointer(api + 8)) || !(destroy = (DestroyFn)Pointer(table + 0x60)) ||
        !(world = (void*)Pointer(game + 0x346BFA0)))
        return 0;
    destroy(world, handle);
    return 1;
}

static uintptr_t Fields(uint32_t* count)
{
    uintptr_t m = Pointer(game + 0x3326958);
    *count = 0;
    if (!m || !ReadAt(m + 0x848, count, 4) || *count > 128) return 0;
    return Pointer(m + 0x880);
}

/* once a second: the fields and their armed mines, logged when they change */
static void Watch(void)
{
    uint32_t n;
    uintptr_t recs = Fields(&n);
    if (!recs) return;
    if (n != g_lastFields) { Log("fields: %u (was %u)", n, g_lastFields); g_lastFields = n; }
    for (uint32_t f = 0; f < n && f < 8; f++)
    {
        uintptr_t r = recs + FIELD_STRIDE * f;
        uint8_t mine[MINE_STRIDE];
        uint32_t count = 0, armed = 0, units = 0;
        float c[4];
        if (!ReadAt(r, c, 16) || !ReadAt(r + 0x10, &count, 4) || count > 48) continue;
        uint32_t states[4] = {0};
        for (uint32_t k = 0; k < count; k++)
            if (ReadAt(r + 0x14 + MINE_STRIDE * k, mine, sizeof mine))
            {
                uint32_t s = Word(mine + 0x10);
                armed += s == 1; units += Word(mine) != 0;
                states[s < 3 ? s : 3]++;
            }
        if (armed != g_lastArmed[f] || count != g_lastCount[f])
        {
            Log("field %u at %.1f %.1f %.1f (radius %.1f): %u mines, %u armed, %u with a unit (states: 0 %u, 1 %u, 2 %u, other %u)", f, (double)c[0], (double)c[1], (double)c[2],
                (double)c[3], count, armed, units, states[0], states[1], states[2], states[3]);
            g_lastArmed[f] = armed; g_lastCount[f] = count;
        }
    }
}

/* F7: every other armed mine of the newest field taken away */
static void TakeHalf(void)
{
    uint32_t n;
    uintptr_t recs = Fields(&n);
    if (!recs || !n) { Log("F7: no minefield out"); return; }
    uintptr_t r = recs + FIELD_STRIDE * (n - 1);
    uint32_t count = 0;
    if (!ReadAt(r + 0x10, &count, 4) || count > 48) { Log("F7: the newest field could not be read"); return; }
    int took = 0, removed = 0, odd = 0;
    for (uint32_t k = 0; k < count && g_nTaken < TAKEN; k++)
    {
        uintptr_t at = r + 0x14 + MINE_STRIDE * k;
        uint8_t mine[MINE_STRIDE];
        if (!ReadAt(at, mine, sizeof mine) || Word(mine + 0x10) != 1) continue;
        if (odd++ & 1) continue;
        uint32_t zero = 0;
        int wrote = hf->write(at + 0x10, &zero, 4) == 0;
        int gone = RemoveUnit(Word(mine));
        Taken* t = &g_taken[g_nTaken++];
        memcpy(t->pos, mine + 4, 12);
        t->at = g_now; t->blew = 0;
        took += wrote; removed += gone;
        if (took <= 4) Log("F7: mine %u at %.1f %.1f %.1f: state %s, its unit %08x %s", k, (double)t->pos[0], (double)t->pos[1], (double)t->pos[2], wrote ? "set to 0" : "NOT written",
            Word(mine), gone ? "removed" : "kept (no removal)");
    }
    Log("F7: %d mines of field %u switched off, %d of their units removed. Walk enemies through the field: a blast on a taken mine's place is logged as one", took, n - 1, removed);
}

static void OnStat(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfStatEvent* e = (const HfStatEvent*)info;
    for (uint32_t i = 0; i < e->count && i < HF_STAT_SOURCES; i++)
    {
        const HfStatSource* s = &e->sources[i];
        int md6 = s->resource == MD6_DEPLOYER;
        if (md6 && e->stat == HF_STAT_KILLS) g_md6Kills += s->added;
        if (g_logs < 80 || md6)
        {
            g_logs++;
            Log("stat %s +%d from %016llX%s", e->stat == HF_STAT_SHOTS ? "shots" : e->stat == HF_STAT_KILLS ? "kills" : e->stat == HF_STAT_HITS ? "hits" : "damage", s->added,
                (unsigned long long)s->resource, md6 ? " (MD-6 Anti-Personnel Minefield)" : "");
        }
    }
    if (e->unattributed && e->stat == HF_STAT_KILLS) Log("stat kills: +%d with no source", e->unattributed);
}

static void OnExplosion(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfExplosionEvent* e = (const HfExplosionEvent*)info;
    if (e->type != MD6_BLAST) return;
    g_md6Blasts++;
    int match = -1;
    float best = 2.25f;
    for (int i = 0; i < g_nTaken; i++)
    {
        float dx = e->position[0] - g_taken[i].pos[0], dy = e->position[1] - g_taken[i].pos[1], dz = e->position[2] - g_taken[i].pos[2], d = dx * dx + dy * dy + dz * dz;
        if (d < best) { best = d; match = i; }
    }
    if (match >= 0) { g_taken[match].blew++; g_takenBlasts++; }
    if (g_md6Blasts <= 60 || match >= 0)
        Log("MD-6 blast %u at %.1f %.1f %.1f from %u (owner %u, creditor %llx)%s", g_md6Blasts, (double)e->position[0], (double)e->position[1], (double)e->position[2], e->source,
            e->owner, (unsigned long long)e->creditor, match >= 0 ? ": ON A TAKEN MINE'S PLACE (the switched-off mine still blew)" : "");
}

static void OnMission(HfMod* mod, const HfEventInfo* info, void* user)
{
    if (info->kind == HF_ON_MISSION_START) { g_inMission = 1; g_nTaken = 0; g_md6Kills = g_md6Blasts = g_takenBlasts = 0; Log("mission start"); return; }
    g_inMission = 0;
    Log("mission end: the MD-6 was credited %u kills; %u MD-6 blasts, %u of them on the %d taken mines' places", g_md6Kills, g_md6Blasts, g_takenBlasts, g_nTaken);
}

static void Tick(HfMod* mod, float dt, void* user)
{
    static double next;
    static int was;
    g_now += dt;
    if (!game || !g_inMission) return;
    if (g_now >= next) { next = g_now + 1.0; Watch(); }
    int down = Focused() && (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
    if (down && !was) TakeHalf();
    was = down;
}

static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t size = 0;
    uintptr_t g = hf->module_base("game.dll", &size);
    if (!g || size != 74727424u) { Log("REFUSED: game.dll is not build " BUILD "'s"); return; }
    game = g;
    g_removeOk = hf->code_matches(game + 0x9DDA5F, "48 8b 05 a2 88 94 02 8b 16 48 8b 0d 31 e5 a8 02 4c 8b 40 08 41 ff 50 60");
    Log("ready: call in an MD-6 Anti-Personnel Minefield; in a mission F7 switches off every other armed mine of the newest field%s",
        g_removeOk ? "" : " (the unit removal is not the reviewed code: their units stay)");
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 11 || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    Log("Minefield Probe " VERSION ": research for the Veterancy System's MD-6 (its kills, and switching off the game's own mines)");
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_STAT, OnStat, NULL, 0) && api->subscribe(mod, HF_ON_EXPLOSION, OnExplosion, NULL, 0) &&
             api->subscribe(mod, HF_ON_MISSION_START, OnMission, NULL, 0) && api->subscribe(mod, HF_ON_MISSION_END, OnMission, NULL, 0) &&
             api->on_game_thread(mod, Tick, NULL);
    if (!ok) Log("events or the game thread are unavailable");
    return 0;
}
