/* Stim Probe (yui.stim-probe): research for Executor the Ex Foedere (he cannot stim himself, but can stim others).
 * Log only: nothing is refused or changed. Solo first; the teammate stim needs a second player. Host or solo.
 *
 * What it logs (docs/executor-design.md; the research note behind it: HD2Runtime's pinned avatar action pipeline):
 *   at load       the bytes at start_action (game+0xA438E0, not pinned yet: the pin for a later hook) and whether
 *                 try_start_action (0xA431D0) and StartAbility (0x7CAF40) match HellForge's own pins
 *   every action  of the local Helldiver: try_start_action and start_action with the action context (+0 entity, +4
 *                 ability, +8 target, +0xC extra; "no target" = the u32 at game+0x3483C5C) before and after, and the
 *                 request {ability, target, extra}; StartAbility's entity, ability and mode
 *   every update  the local Helldiver's inventory record and counts row ([game+0x3326738]: map +0x28, records +0x50,
 *                 0x30 each, counts +0x58, 8 each; counts +0 is the grenades), each change with the update number
 *   every heal    HF_ON_HEAL's fields
 * 0.2.0, the test runs itself: 10 s after you land (or Ctrl+F12 again any time), it presses the grenade key
 * (a control: counts +0 must drop), hurts your left arm a little (HellForge's injure), presses the stim key (a
 * self-stim, hurt), waits, presses it again (a stim at full health), and logs "test done". Keys: stim-probe.ini,
 * key_grenade and key_stim (default G and V, the game's defaults: set yours if you rebound them). Stand somewhere
 * safe; throw the grenade away from you (it is thrown where you look). Also: take a resupply, and a Supply Pack on
 * yourself if you have one (ability 2629, a known control). Co-op later: stim a hurt teammate, and be stimmed.
 * Credit: the action pipeline, the context layout and the inventory layout are HD2Runtime's research (SkyeShade); the
 * two pins are HellForge's own (actions.c, abilities.c). Nothing of their code is copied. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <windows.h>
#include "hellforge_mod.h"

#define BUILD "25480438"
#define VERSION "0.2.0"
static const HfApi* hf;
static HfMod* self;
static uintptr_t game;
static atomic_uint g_avatar;
static int g_ready;
static unsigned long long g_update;
#define Log(...) hf->log(self, __VA_ARGS__)
static int RD(uintptr_t a, void* out, size_t n) { return a >= 0x10000 && a < 0x7FFFFFFF0000ull && hf->read(a, out, n) == 0; }
static uintptr_t P(uintptr_t a) { uintptr_t v = 0; return RD(a, &v, 8) && v >= 0x10000 && v < 0x7FFFFFFF0000ull ? v : 0; }
static uint32_t U32At(uintptr_t a) { uint32_t v = 0; RD(a, &v, 4); return v; }

static int Lookup(uintptr_t at, uint32_t key, uint32_t* index)
{
    uint8_t h[20], row[8];
    if (!RD(at, h, sizeof h)) return 0;
    uintptr_t entries; memcpy(&entries, h, 8);
    uint32_t n, empty, mult;
    memcpy(&n, h + 8, 4); memcpy(&empty, h + 12, 4); memcpy(&mult, h + 16, 4);
    if (!entries || !n || n > 1048576 || (n & (n - 1)) || key == empty) return 0;
    for (unsigned i = 0; i < n && i < 128; i++)
    {
        if (!RD(entries + 8u * ((key * mult + i) & (n - 1)), row, 8)) return 0;
        uint32_t k, v; memcpy(&k, row, 4); memcpy(&v, row + 4, 4);
        if (k == key) { *index = v; return v != UINT32_MAX; }
        if (k == empty) return 0;
    }
    return 0;
}

/* ---- the hooks: log, always call the original ------------------------------------------------------------------- */
typedef uintptr_t (*ActFn)(uintptr_t ctx, uintptr_t request);
typedef uintptr_t (*AbilityFn)(uintptr_t manager, uintptr_t entity, uintptr_t ability, uintptr_t mode, uintptr_t speed);
static ActFn o_try, o_start;
static AbilityFn o_ability;
static unsigned g_told;
static int Mine(uintptr_t ctx) { uint32_t e = U32At(ctx); return e && e == atomic_load(&g_avatar); }
static void Ctx(const char* what, uintptr_t ctx, uintptr_t request)
{
    uint32_t c[4] = {0}, r[3] = {0};
    RD(ctx, c, sizeof c);
    if (request) RD(request, r, sizeof r);
    uint32_t none = U32At(game + 0x3483C5C);
    Log("[%llu] %s: ctx %llx {entity %u, ability %u, target %u%s, extra %u}; request %s {ability %u, target %u%s, extra %u}", g_update, what,
        (unsigned long long)ctx, c[0], c[1], c[2], c[2] == none ? " (none)" : "", c[3], request ? "yes" : "NULL", r[0], r[1],
        request && r[1] == none ? " (none)" : "", r[2]);
}
static uintptr_t D_try(uintptr_t ctx, uintptr_t request)
{
    int mine = Mine(ctx) && g_told < 400;
    if (mine) { g_told++; Ctx("try_start_action, before", ctx, request); }
    uintptr_t r = o_try(ctx, request);
    if (mine) { uint32_t c[4] = {0}; RD(ctx, c, sizeof c); Log("[%llu]   try_start_action returned %u; ctx now ability %u, target %u", g_update, (unsigned)(r & 0xFF), c[1], c[2]); }
    return r;
}
static uintptr_t D_start(uintptr_t ctx, uintptr_t request)
{
    int mine = Mine(ctx) && g_told < 400;
    if (mine) { g_told++; Ctx("start_action, before", ctx, request); }
    uintptr_t r = o_start(ctx, request);
    if (mine) { uint32_t c[4] = {0}; RD(ctx, c, sizeof c); Log("[%llu]   start_action returned rax %llx; ctx now ability %u, target %u, extra %u", g_update, (unsigned long long)r, c[1], c[2], c[3]); }
    return r;
}
static uintptr_t D_ability(uintptr_t manager, uintptr_t entity, uintptr_t ability, uintptr_t mode, uintptr_t speed)
{
    if ((uint32_t)entity == atomic_load(&g_avatar) && g_told < 400)
    {
        g_told++;
        float sp; uint32_t bits = (uint32_t)speed; memcpy(&sp, &bits, 4);
        Log("[%llu] StartAbility: entity %u, ability %u, mode %u (speed slot %08x)", g_update, (uint32_t)entity, (uint32_t)ability, (uint32_t)mode, bits);
    }
    return o_ability(manager, entity, ability, mode, speed);
}
static int Boundary(uintptr_t at)
{
    uint8_t b[2];
    return !hf->read_code(at - 1, b, 2) && (b[0] == 0xCC || b[0] == 0xC3 || b[0] == 0x90) && b[1] != 0xCC;
}
static void Hooks(void)
{
    uint8_t code[48];
    char hex[48 * 3 + 1];
    if (!hf->read_code(game + 0xA438E0, code, sizeof code))
    {
        for (int k = 0; k < 48; k++) sprintf(hex + k * 3, "%02X ", code[k]);
        Log("start_action (game+0xA438E0) begins: %s(a function start before it: %s)", hex, Boundary(game + 0xA438E0) ? "yes" : "NO");
    }
    int tryOk = hf->code_matches(game + 0xA431D0, "48 89 5C 24 08 57 48 83 EC 20 48 8B FA 48 8B D9");
    int abOk = hf->code_matches(game + 0x7CAF40, "4C 8B DC 49 89 5B 18 55 56 57 41 55 41 56 48 81 EC B0 00 00 00 48 8B 05 B4 10 E7 01 48 33 C4 48");
    Log("pins: try_start_action %s, StartAbility %s", tryOk ? "match" : "DIFFER", abOk ? "match" : "DIFFER");
    if (tryOk && !hf->hook_ex(self, game + 0xA431D0, (void*)D_try, (void**)&o_try, 0)) Log("hooked try_start_action");
    if (abOk && !hf->hook_ex(self, game + 0x7CAF40, (void*)D_ability, (void**)&o_ability, 0)) Log("hooked StartAbility");
    if (Boundary(game + 0xA438E0) && !hf->hook_ex(self, game + 0xA438E0, (void*)D_start, (void**)&o_start, 0)) Log("hooked start_action");
    else Log("start_action not hooked (no function start found before it, or the hook was refused)");
}

/* ---- the inventory, each update ------------------------------------------------------------------------------- */
static void Inventory(void)
{
    static uint8_t lastRec[0x30], lastCounts[8];
    static int have;
    uint32_t me = atomic_load(&g_avatar), i;
    uintptr_t m = P(game + 0x3326738);
    if (!me || !m || !Lookup(m + 0x28, me, &i) || i > 4096) return;
    uintptr_t recs = P(m + 0x50), counts = P(m + 0x58);
    uint8_t rec[0x30], cnt[8];
    if (!recs || !counts || !RD(recs + 0x30ull * i, rec, sizeof rec) || !RD(counts + 8ull * i, cnt, sizeof cnt)) return;
    if (have && !memcmp(rec, lastRec, sizeof rec) && !memcmp(cnt, lastCounts, sizeof cnt)) return;
    uint32_t c0, c4;
    memcpy(&c0, cnt, 4); memcpy(&c4, cnt + 4, 4);
    char hex[0x30 * 3 + 1];
    for (int k = 0; k < 0x30; k++) sprintf(hex + k * 3, "%02x ", rec[k]);
    Log("[%llu] inventory: counts +0 %u (grenades), +4 %u; record %s", g_update, c0, c4, hex);
    memcpy(lastRec, rec, sizeof rec); memcpy(lastCounts, cnt, sizeof cnt);
    have = 1;
}

/* ---- 0.2.0: the self-running test ---------------------------------------------------------------------------------- */
static int s_keyGrenade = 'G', s_keyStim = 'V';
static void LoadKeys(void)
{
    char path[MAX_PATH * 2], line[128];
    snprintf(path, sizeof path, "%s\\stim-probe.ini", hf->mod_folder(self));
    FILE* f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f))
    {
        char k[32], v[32];
        if (sscanf(line, " %31[a-z_] = %31s", k, v) != 2) continue;
        int vk = 0;
        if ((v[0] == 'F' || v[0] == 'f') && v[1] >= '1' && v[1] <= '9') vk = VK_F1 + atoi(v + 1) - 1;
        else if (v[0] && !v[1]) vk = (v[0] >= 'a' && v[0] <= 'z') ? v[0] - 32 : v[0];
        if (!vk) continue;
        if (!strcmp(k, "key_grenade")) s_keyGrenade = vk;
        else if (!strcmp(k, "key_stim")) s_keyStim = vk;
    }
    fclose(f);
}
/* a key as the keyboard sends it (scan code: the game reads raw input) */
static void Key(int vk, int down)
{
    INPUT in;
    memset(&in, 0, sizeof in);
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = (WORD)MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC);
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &in, sizeof in);
}
static double g_t, g_testAt, g_stepAt;
static int g_step = -1;
static void Test(void)
{
    if (g_step < 0 || g_t < g_stepAt) return;
    switch (g_step)
    {
    case 0: Log("[%llu] TEST: grenade (key %c)", g_update, s_keyGrenade); Key(s_keyGrenade, 1); g_stepAt = g_t + 0.15; break;
    case 1: Key(s_keyGrenade, 0); g_stepAt = g_t + 4; break;
    case 2:
    {
        int r = hf->injure ? hf->injure(self, HF_LIMB_LEFT_ARM, 25) : 0;
        Log("[%llu] TEST: left arm hurt by 25 (%s)", g_update, r == 0 ? "done" : "refused");
        g_stepAt = g_t + 2;
        break;
    }
    case 3: Log("[%llu] TEST: stim, hurt (key %c)", g_update, s_keyStim); Key(s_keyStim, 1); g_stepAt = g_t + 0.15; break;
    case 4: Key(s_keyStim, 0); g_stepAt = g_t + 6; break;
    case 5: Log("[%llu] TEST: stim at full health (key %c)", g_update, s_keyStim); Key(s_keyStim, 1); g_stepAt = g_t + 0.15; break;
    case 6: Key(s_keyStim, 0); g_stepAt = g_t + 6; break;
    case 7: Log("[%llu] TEST: done (the lines above, from the first TEST, are the run)", g_update); g_step = -2; return;
    }
    g_step++;
}

/* ---- events, the game thread --------------------------------------------------------------------------------------- */
static void OnWeapon(HfMod* mod, const HfEventInfo* info, void* user)
{
    (void)mod; (void)user;
    const HfWeaponEvent* w = (const HfWeaponEvent*)info;
    if (w->avatar) atomic_store(&g_avatar, w->avatar);
}
static void OnHeal(HfMod* mod, const HfEventInfo* info, void* user)
{
    (void)mod; (void)user;
    const HfHealEvent* h = (const HfHealEvent*)info;
    Log("[%llu] heal: entity %u%s, +%d to %d of %d, peer %llx", g_update, h->entity, h->local ? " (you)" : "", h->amount, h->health, h->max_health,
        (unsigned long long)h->peer);
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    (void)mod; (void)event; (void)user;
    size_t gs = 0, xs = 0;
    game = hf->module_base("game.dll", &gs);
    uintptr_t exe = hf->module_base("helldivers2.exe", &xs);
    if (!game || !exe || gs != 74727424u || xs != 60719104u) { g_ready = -1; Log("REFUSED: not build " BUILD "'s modules"); return; }
    Hooks();
    g_ready = 1;
}
static void Tick(HfMod* mod, float dt, void* user)
{
    (void)mod; (void)user;
    g_update++;
    g_t += dt > 0 && dt < 1 ? dt : 0;
    static int inMission;
    int m = g_ready == 1 && hf->game_state(self) == HF_STATE_MISSION;
    if (m != inMission) { inMission = m; g_step = -1; g_testAt = m ? g_t + 10 : 0; }
    if (!m) return;
    Inventory();
    /* the test: 10 s after landing (once she is known), or Ctrl+F12 */
    if (g_testAt > 0 && g_t >= g_testAt && atomic_load(&g_avatar)) { g_testAt = 0; g_step = 0; g_stepAt = g_t; Log("[%llu] TEST: starting (stand somewhere safe)", g_update); }
    static int prev;
    int d = (GetAsyncKeyState(VK_F12) & 0x8000) && (GetAsyncKeyState(VK_CONTROL) & 0x8000);
    if (d && !prev && g_step < 0) { g_step = 0; g_stepAt = g_t; Log("[%llu] TEST: starting (Ctrl+F12)", g_update); }
    prev = d;
    Test();
}
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 15 || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    LoadKeys();
    Log("Stim Probe " VERSION ": log only; your actions, abilities, inventory and heals (Executor's no-self-stim research); the test runs 10 s after landing (grenade %c, stim %c)",
        s_keyGrenade, s_keyStim);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) && api->subscribe(mod, HF_ON_HEAL, OnHeal, NULL, 0) &&
             api->on_game_thread(mod, Tick, NULL);
    if (!ok) Log("events or the game thread are unavailable");
    return 0;
}
