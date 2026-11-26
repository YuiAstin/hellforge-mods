/* Shot Freeze Probe (yui.shot-freeze-probe): research towards a true time stop. Can a mod stop the shots in flight
 * where they are, hold them, and let them go on as they were?
 *
 * The game's shots (game.dll build 25480438; docs/research/shot-freeze.md has the reading):
 *   - the pool at [game+0x347CEA8]: +0x28 running, 2048 slots; flags u16 at +0x203C + 2 slot (2 live, 1 ended);
 *     the flight record at +0x3040 + 0x70 slot; the type u32 at +0xE5040 + 4 slot;
 *   - flight +0x00 position, +0x0C velocity, +0x1C gravity, +0x2C starting speed, +0x30 distance flown,
 *     +0x34 lifetime left (counts down by the frame time; the shot ends at 0), +0x50 a unit it rides (0 none);
 *   - the step (0x13AB0E0) ends a shot slower than a tenth of its starting speed (0x13AB6D5).
 *
 * So the freeze: velocity and starting speed both scaled down by a million (the slow-shot rule still reads 100%, and
 * it moves under a millimetre a second), gravity 0, its lifetime held. The release writes the saved values back.
 * Method B, for the record, zeroes the velocity only: the slow-shot rule should end those shots (the prediction).
 *
 * Unattended: in a mission, every 12 s it fires four shots of its own from 3 m over your Helldiver, upward, freezes
 * every shot in flight 0.1 s later (enemies' too), holds them 3 s and lets them go, logging what each did. The cycles
 * alternate Liberator and Eruptor rounds, and method A and B. Stand somewhere open and leave it 2 minutes.
 * Hands on: Ctrl+F11 freezes every shot in flight (method A), and every new one once it has flown 2 m, until pressed
 * again: fire your own weapon at a wall, let enemies shoot. Solo or host. */
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
#include "hellforge_globals.h"

#define BUILD "25480438"
#define VERSION "0.1.0"
#include "kitbase/kb_core.h"

static atomic_uint g_avatar;
static double g_now;

/* ---- the pool ----------------------------------------------------------------------------------------------------- */
#define SLOTS 2048
#define FLAGS 0x203C
#define FLIGHT 0x3040
#define TYPES 0xE5040
#define SCALE 1e-6f
typedef struct Flight { float pos[3], vel[3], carry, gravity, f20, f24, drag, start, flown, life; } Flight;   /* the first 0x38 bytes */
_Static_assert(sizeof(Flight) == 0x38, "flight head");

static uintptr_t Pool(void)
{
    uintptr_t p = Pointer(game + 0x347CEA8);
    uint8_t running = 0;
    return p && ReadAt(p + 0x28, &running, 1) && running == 1 ? p : 0;
}
static int Live(uintptr_t pool, int slot, uint32_t* type)
{
    uint16_t f = 0;
    return ReadAt(pool + FLAGS + 2u * slot, &f, 2) && (f & 2) && !(f & 1) && ReadAt(pool + TYPES + 4u * slot, type, 4);
}
static int ReadFlight(uintptr_t pool, int slot, Flight* f) { return ReadAt(pool + FLIGHT + 0x70u * slot, f, sizeof *f); }
static uint32_t Rides(uintptr_t pool, int slot)
{
    uint32_t u = 1;
    ReadAt(pool + FLIGHT + 0x70u * slot + 0x50, &u, 4);
    return u;
}
static void Put(uintptr_t pool, int slot, size_t off, const void* v, size_t n) { hf->write(pool + FLIGHT + 0x70u * slot + off, v, n); }
static float Len(const float v[3]) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
static float Dist(const float a[3], const float b[3]) { float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; return Len(d); }

/* ---- frozen shots --------------------------------------------------------------------------------------------- */
#define HELD 256
typedef struct Held { int slot; uint32_t type; Flight saved; float drift; int ended; float releasedAt[3]; int checked; } Held;
static Held g_held[HELD];
static int g_nheld, g_method, g_manual;
static double g_frozenAt, g_releasedAt, said;

static int IsHeld(int slot) { for (int i = 0; i < g_nheld; i++) if (g_held[i].slot == slot) return 1; return 0; }
static void Hold(uintptr_t pool, int slot, uint32_t type, const Flight* f)
{
    if (g_nheld >= HELD) return;
    Held* h = &g_held[g_nheld++];
    memset(h, 0, sizeof *h);
    h->slot = slot; h->type = type; h->saved = *f;
    float v[3] = {f->vel[0] * SCALE, f->vel[1] * SCALE, f->vel[2] * SCALE}, zero[3] = {0, 0, 0}, g = 0, start = f->start * SCALE;
    Put(pool, slot, 0x0C, g_method == 0 ? v : zero, 12);
    Put(pool, slot, 0x1C, &g, 4);
    if (g_method == 0) Put(pool, slot, 0x2C, &start, 4);
}
/* Every live shot not riding a unit (with `min_flown`: only once it has flown that far) */
static int FreezeAll(uintptr_t pool, float minFlown)
{
    static uint16_t flags[SLOTS];
    int n = 0;
    if (!ReadAt(pool + FLAGS, flags, sizeof flags)) return 0;
    for (int s = 0; s < SLOTS && g_nheld < HELD; s++)
    {
        uint32_t type;
        Flight f;
        if (!(flags[s] & 2) || (flags[s] & 1) || IsHeld(s) || !Live(pool, s, &type) || Rides(pool, s) || !ReadFlight(pool, s, &f) || f.flown < minFlown) continue;
        Hold(pool, s, type, &f);
        n++;
    }
    return n;
}
/* Each frame while frozen: the lifetime held, method A's velocity kept (drag would slow it), the drift measured */
static void HoldFrame(uintptr_t pool)
{
    for (int i = 0; i < g_nheld; i++)
    {
        Held* h = &g_held[i];
        uint32_t type;
        Flight f;
        if (h->ended) continue;
        if (!Live(pool, h->slot, &type) || type != h->type || !ReadFlight(pool, h->slot, &f))
        {
            h->ended = 1;
            Log("  shot %d (type %u) ENDED while frozen, %.2f s in (method %c)", h->slot, h->type, g_now - g_frozenAt, 'A' + g_method);
            continue;
        }
        float d = Dist(f.pos, h->saved.pos);
        if (d > h->drift) h->drift = d;
        Put(pool, h->slot, 0x34, &h->saved.life, 4);
        if (g_method == 0)
        {
            float v[3] = {h->saved.vel[0] * SCALE, h->saved.vel[1] * SCALE, h->saved.vel[2] * SCALE};
            Put(pool, h->slot, 0x0C, v, 12);
        }
    }
}
static void Release(uintptr_t pool)
{
    int alive = 0, ended = 0;
    float worst = 0;
    for (int i = 0; i < g_nheld; i++)
    {
        Held* h = &g_held[i];
        uint32_t type;
        Flight f;
        if (h->ended || !Live(pool, h->slot, &type) || type != h->type || !ReadFlight(pool, h->slot, &f)) { ended++; h->ended = 1; continue; }
        Put(pool, h->slot, 0x0C, h->saved.vel, 12);
        Put(pool, h->slot, 0x1C, &h->saved.gravity, 4);
        Put(pool, h->slot, 0x2C, &h->saved.start, 4);
        Put(pool, h->slot, 0x34, &h->saved.life, 4);
        memcpy(h->releasedAt, f.pos, 12);
        if (h->drift > worst) worst = h->drift;
        alive++;
    }
    Log("released after %.2f s (method %c): %d of %d still in flight, %d ended while frozen; most drift %.4f m", g_now - g_frozenAt, 'A' + g_method, alive,
        g_nheld, ended, (double)worst);
    g_releasedAt = g_now;
}
/* 0.3 s after the release: did each go on as it was? */
static void AfterRelease(uintptr_t pool)
{
    int shown = 0;
    for (int i = 0; i < g_nheld; i++)
    {
        Held* h = &g_held[i];
        uint32_t type;
        Flight f;
        if (h->ended) continue;
        if (!Live(pool, h->slot, &type) || type != h->type || !ReadFlight(pool, h->slot, &f))
        {
            if (shown++ < 12) Log("  shot %d (type %u): gone 0.3 s after the release (hit something or ran out)", h->slot, h->type);
            continue;
        }
        if (shown++ < 12)
            Log("  shot %d (type %u): moved %.1f m since the release, speed %.1f m/s (was %.1f), life %.2f s (was %.2f), drift while frozen %.4f m", h->slot, h->type,
                (double)Dist(f.pos, h->releasedAt), (double)Len(f.vel), (double)Len(h->saved.vel), (double)f.life, (double)h->saved.life, (double)h->drift);
    }
    g_nheld = 0;
}

/* ---- the unattended cycle --------------------------------------------------------------------------------------- */
enum { C_IDLE, C_FIRE, C_WAIT, C_HELD, C_AFTER };
static int g_cycle = C_IDLE, g_cycles, g_fired;
static double g_cycleAt = 20;   /* the first cycle 20 s into a session's mission */
static uint32_t g_type;

static int Player(float p[3])
{
    uint8_t d[24];
    uint32_t me = atomic_load(&g_avatar);
    return me && Descriptor(me, d) && UnitNode(Word(d + 12), 0, p);
}
static void FireFrame(void)
{
    float me[3];
    if (!Player(me)) { if (g_now > g_cycleAt + 3) { Log("cycle %d: your Helldiver is not readable: skipped", g_cycles); g_cycle = C_IDLE; g_cycleAt = g_now + 12; } return; }
    static const float dirs[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    while (g_fired < 4)
    {
        float at[3] = {me[0], me[1], me[2] + 3}, d[3] = {dirs[g_fired][0] * 0.5f, dirs[g_fired][1] * 0.5f, 0.866f};
        int r = hf->spawn_projectile(self, g_type, at, d);
        if (r == HF_ACT_WAITING) { if (g_now > g_cycleAt + 5) { Log("cycle %d: type %u's packages did not load in 5 s: skipped", g_cycles, g_type); g_cycle = C_IDLE; g_cycleAt = g_now + 12; } return; }
        if (r != HF_ACT_DONE && r != HF_ACT_REQUESTED) Log("cycle %d: spawn_projectile(type %u) answered %d", g_cycles, g_type, r);
        g_fired++;
    }
    g_cycle = C_WAIT;
    g_cycleAt = g_now + 0.1;
}
static void CycleFrame(uintptr_t pool)
{
    switch (g_cycle)
    {
    case C_IDLE:
        if (g_manual || g_cycles >= 10 || g_now < g_cycleAt) return;
        g_method = g_cycles & 1;
        g_type = (g_cycles >> 1) & 1 ? HF_PROJECTILE_R36_ERUPTOR : HF_PROJECTILE_AR23_LIBERATOR;
        g_fired = 0;
        g_cycle = C_FIRE;
        g_cycleAt = g_now;
        Log("cycle %d of 10: four %s rounds upward, method %c (%s)", g_cycles + 1, g_type == HF_PROJECTILE_R36_ERUPTOR ? "R-36 Eruptor" : "AR-23 Liberator",
            'A' + g_method, g_method ? "velocity 0: expected to END them" : "velocity and starting speed x 1e-6, gravity 0, lifetime held");
        g_cycles++;
        FireFrame();
        return;
    case C_FIRE: FireFrame(); return;
    case C_WAIT:
        if (g_now < g_cycleAt) return;
        g_nheld = 0;
        g_frozenAt = g_now;
        Log("frozen: %d shots in flight", FreezeAll(pool, 0));
        for (int i = 0; i < g_nheld && i < 8; i++)
        {
            const Flight* f = &g_held[i].saved;
            Log("  shot %d (type %u): at %.1f %.1f %.1f, speed %.1f m/s, starting speed %.1f, gravity %.2f, drag %.3f, flown %.1f m, life %.2f s", g_held[i].slot, g_held[i].type,
                (double)f->pos[0], (double)f->pos[1], (double)f->pos[2], (double)Len(f->vel), (double)f->start, (double)f->gravity, (double)f->drag, (double)f->flown,
                (double)f->life);
        }
        g_cycle = C_HELD;
        g_cycleAt = g_now + 3;
        return;
    case C_HELD:
        if (g_now - g_frozenAt >= 1.0 && said != g_frozenAt)
        {
            said = g_frozenAt;
            int left = 0;
            for (int i = 0; i < g_nheld; i++) left += !g_held[i].ended;
            Log("  1 s frozen: %d of %d still held", left, g_nheld);
        }
        HoldFrame(pool);
        if (g_now < g_cycleAt) return;
        Release(pool);
        g_cycle = C_AFTER;
        g_cycleAt = g_now + 0.3;
        return;
    case C_AFTER:
        if (g_now < g_cycleAt) return;
        AfterRelease(pool);
        g_cycle = C_IDLE;
        g_cycleAt = g_now + 8.6;
        if (g_cycles >= 10) Log("all 10 cycles done: the probe only acts on Ctrl+F11 now");
        return;
    }
}

/* ---- Ctrl+F11 ------------------------------------------------------------------------------------------------- */
static void ManualToggle(uintptr_t pool)
{
    if (!g_manual)
    {
        if (g_cycle != C_IDLE) { Log("Ctrl+F11: a cycle is running, try again in a few seconds"); return; }
        g_manual = 1; g_method = 0; g_nheld = 0; g_frozenAt = g_now;
        Log("Ctrl+F11: time stopped: %d shots frozen; new ones freeze once they have flown 2 m", FreezeAll(pool, 0));
        return;
    }
    g_manual = 0;
    Release(pool);
    g_cycle = C_AFTER;
    g_cycleAt = g_now + 0.3;
}

static void Tick(HfMod* mod, float dt, void* user)
{
    static int prev;
    static unsigned lastCount;
    g_now += dt;
    if (!game) return;
    uintptr_t pool = Pool();
    if (!pool)
    {
        if (g_nheld) { Log("the shot pool stopped (mission over): %d frozen shots forgotten", g_nheld); g_nheld = 0; g_manual = 0; g_cycle = C_IDLE; }
        g_cycleAt = g_now + 20;
        return;
    }
    int k = Focused() && (GetAsyncKeyState(VK_F11) & 0x8000) && (GetAsyncKeyState(VK_CONTROL) & 0x8000);
    if (k && !prev) ManualToggle(pool);
    prev = k;
    if (g_manual)
    {
        int n = FreezeAll(pool, 2.0f);
        if (n && lastCount < 40) { lastCount++; Log("  +%d new shots frozen (%d held)", n, g_nheld); }
        HoldFrame(pool);
        return;
    }
    lastCount = 0;
    CycleFrame(pool);
}
static void OnWeapon(HfMod* mod, const HfWeaponEvent* w, void* user)
{
    if (w->avatar && w->avatar != atomic_load(&g_avatar)) atomic_store(&g_avatar, w->avatar);
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gs = 0, xs = 0;
    uintptr_t g = hf->module_base("game.dll", &gs), x = hf->module_base("helldivers2.exe", &xs);
    if (!g || !x || gs != 74727424u || xs != 60719104u) { Log("REFUSED: not build " BUILD); return; }
    /* the step's slow-shot rule and its lifetime countdown, as read */
    if (!hf->code_matches(g + 0x13AB6D5, "f3 0f 5e 47 2c 44 0f 2f c0") || !hf->code_matches(g + 0x13AB82D, "f3 0f 10 47 34 41 0f 2f c4"))
    {
        Log("REFUSED: the shot step is not the reviewed code");
        return;
    }
    game = g;
    exe = x;
    Log("ready: in a mission the cycles start 20 s in (stand somewhere open); Ctrl+F11 freezes every shot in flight by hand");
}
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 16 || api->size < offsetof(HfApi, global) + sizeof api->global || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    Log("Shot Freeze Probe " VERSION ": research towards a time stop (can shots in flight be held and let go?)");
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    if (!api->subscribe(mod, HF_ON_WEAPON, (HfListener)OnWeapon, NULL, 0) || !api->on_game_thread(mod, Tick, NULL)) Log("events or the game thread are unavailable");
    return 0;
}
