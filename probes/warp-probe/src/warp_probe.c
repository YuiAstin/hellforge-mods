/* Warp Probe (yui.warp-probe): a research mod with no gameplay effect. It learns how the game moves a Helldiver when
 * the LIFT-182 Warp Pack warps, so Lenna's Leaping Pursuit can teleport the same way.
 *
 * At game start it logs the code of the functions involved (as they were before any hook) and the engine mover table.
 * It hooks the warp's position setter (game.dll+0x5A2BB0) and the launch-flag setter (+0x5A3D80) with pass-through
 * detours that only copy their argument registers and the first floats behind the third one, then call the game's
 * function unchanged. The log shows each call: its arguments, who called it, and whether the entity is your Helldiver.
 *
 * Addresses: HD2Runtime by SkyeShade's research (warp call site 0x88D790 -> 0x5A2BB0, SetVelocity 0x4A7550, the launch
 * flag 0x5A3D80 called at 0xA51226; https://github.com/SkyeShade/HD2Runtime), and ConsistentVaulting (0BSD) for the
 * mover table at [game+0x3326388]. Build 25480438 only. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <windows.h>
#include "hellforge_mod.h"

#define BUILD "25480438"
static const HfApi* hf;
static HfMod* self;
static uintptr_t game, exe;
static size_t gameSize, exeSize;
static atomic_uint g_avatar;

/* ---- the detours: integer stores only (no calls, no SSE) before the game's function runs ------------------------- */
typedef uintptr_t (*Fn8)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
typedef struct Call { volatile uint64_t a[8]; volatile uint64_t ret; volatile uint32_t f[8]; volatile uint32_t floats; volatile uint64_t seq; } Call;
#define RING 64
static Call g_set[RING], g_flag[RING];
static volatile uint64_t g_setN, g_flagN;
static Fn8 original_set, original_flag;

static int Plausible(uint64_t p) { return p >= 0x10000 && p < 0x7FFFFFFF0000ull && !(p & 3); }

#define RECORD(ring, n, a1, a2, a3, a4, a5, a6, a7, a8)                                      \
    do {                                                                                     \
        uint64_t i_ = n;                                                                     \
        Call* c_ = &ring[i_ % RING];                                                         \
        c_->a[0] = a1; c_->a[1] = a2; c_->a[2] = a3; c_->a[3] = a4;                          \
        c_->a[4] = a5; c_->a[5] = a6; c_->a[6] = a7; c_->a[7] = a8;                          \
        c_->ret = (uint64_t)(uintptr_t)__builtin_return_address(0);                          \
        c_->floats = 0;                                                                      \
        if (Plausible(a3))                                                                   \
        {                                                                                    \
            const volatile uint32_t* p_ = (const volatile uint32_t*)(uintptr_t)a3;           \
            c_->f[0] = p_[0]; c_->f[1] = p_[1]; c_->f[2] = p_[2]; c_->f[3] = p_[3];          \
            c_->floats = 4;                                                                  \
        }                                                                                    \
        c_->seq = i_ + 1;                                                                    \
        n = i_ + 1;                                                                          \
    } while (0)

__attribute__((noinline)) static uintptr_t SetDetour(uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6,
    uintptr_t a7, uintptr_t a8)
{
    RECORD(g_set, g_setN, a1, a2, a3, a4, a5, a6, a7, a8);
    return original_set(a1, a2, a3, a4, a5, a6, a7, a8);
}
__attribute__((noinline)) static uintptr_t FlagDetour(uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6,
    uintptr_t a7, uintptr_t a8)
{
    RECORD(g_flag, g_flagN, a1, a2, a3, a4, a5, a6, a7, a8);
    return original_flag(a1, a2, a3, a4, a5, a6, a7, a8);
}

/* ---- logging ------------------------------------------------------------------------------------------------- */
static void Dump(const char* what, uintptr_t rva, size_t n)
{
    uint8_t buf[0x200];
    if (n > sizeof buf) n = sizeof buf;
    if (hf->read_code(game + rva, buf, n)) { hf->log(self, "%s game.dll+0x%llX: unreadable", what, (unsigned long long)rva); return; }
    hf->log(self, "%s game.dll+0x%llX, %u bytes:", what, (unsigned long long)rva, (unsigned)n);
    for (size_t o = 0; o < n; o += 32)
    {
        char line[128];
        size_t k = 0;
        for (size_t i = o; i < o + 32 && i < n; i++) k += (size_t)snprintf(line + k, sizeof line - k, "%02x", buf[i]);
        hf->log(self, "  +%03X %s", (unsigned)o, line);
    }
}
static const char* Where(uint64_t p, char* out, size_t n)
{
    if (p >= game && p < game + gameSize) snprintf(out, n, "game+0x%llX", (unsigned long long)(p - game));
    else if (p >= exe && p < exe + exeSize) snprintf(out, n, "exe+0x%llX", (unsigned long long)(p - exe));
    else snprintf(out, n, "0x%llX", (unsigned long long)p);
    return out;
}
static void Report(const char* name, Call* ring, volatile uint64_t* n, uint64_t* seen, unsigned* logged)
{
    uint64_t now = *n;
    if (now - *seen > RING) *seen = now - RING;
    uint32_t me = atomic_load(&g_avatar);
    while (*seen < now)
    {
        Call* c = &ring[*seen % RING];
        (*seen)++;
        uint32_t entity = (uint32_t)c->a[1];
        int mine = me && entity == me;
        /* every call on your Helldiver; the first 10 others */
        if (!mine && *logged >= 10) continue;
        if (!mine) (*logged)++;
        char r[48];
        float f[4] = {0};
        for (int i = 0; i < 4 && i < (int)c->floats; i++) { uint32_t v = c->f[i]; memcpy(&f[i], &v, 4); }
        hf->log(self, "%s #%llu%s from %s: rcx %llX, rdx %llX (entity %u), r8 %llX -> [%.3f %.3f %.3f %.3f], r9 %llX, stack %llX %llX %llX %llX",
            name, (unsigned long long)(*seen), mine ? " (YOUR HELLDIVER)" : "", Where(c->ret, r, sizeof r), (unsigned long long)c->a[0],
            (unsigned long long)c->a[1], entity, (unsigned long long)c->a[2], (double)f[0], (double)f[1], (double)f[2], (double)f[3],
            (unsigned long long)c->a[3], (unsigned long long)c->a[4], (unsigned long long)c->a[5], (unsigned long long)c->a[6],
            (unsigned long long)c->a[7]);
    }
}

static void Tick(HfMod* mod, float dt, void* user)
{
    static uint64_t seenSet, seenFlag;
    static unsigned loggedSet, loggedFlag;
    if (original_set) Report("SetPosition", g_set, &g_setN, &seenSet, &loggedSet);
    if (original_flag) Report("LaunchFlag", g_flag, &g_flagN, &seenFlag, &loggedFlag);
}

static void OnWeapon(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfWeaponEvent* w = (const HfWeaponEvent*)info;
    if (w->avatar && w->avatar != atomic_load(&g_avatar))
    {
        atomic_store(&g_avatar, w->avatar);
        hf->log(self, "your Helldiver: entity %u (backpack calls are logged when you warp)", w->avatar);
    }
}

static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    game = hf->module_base("game.dll", &gameSize);
    exe = hf->module_base("helldivers2.exe", &exeSize);
    if (!game || !exe || gameSize != 74727424u || exeSize != 60719104u) { hf->log(self, "REFUSED: not build " BUILD "'s modules"); return; }
    Dump("SetPosition (warp)", 0x5A2BB0, 0x200);
    Dump("the warp's call site", 0x88D700, 0xD0);
    Dump("SetVelocity", 0x4A7550, 0x80);
    Dump("LaunchFlag", 0x5A3D80, 0x100);
    Dump("the launch's call site", 0xA51200, 0x40);
    Dump("ConsistentVaulting's stand check", 0xA9D560, 0x80);
    uintptr_t table = 0;
    if (hf->read(game + 0x3326388, &table, 8) == 0 && table)
    {
        uint64_t q[16];
        if (hf->read(table, q, sizeof q) == 0)
        {
            char w[48];
            hf->log(self, "mover table at %s:", Where(table, w, sizeof w));
            for (int i = 0; i < 16; i++) hf->log(self, "  +0x%02X %s", i * 8, Where(q[i], w, sizeof w));
        }
    }
    else hf->log(self, "mover table pointer unreadable");
    if (hf->hook(self, game + 0x5A2BB0, (void*)SetDetour, (void**)&original_set)) { original_set = NULL; hf->log(self, "SetPosition: hook refused"); }
    if (hf->hook(self, game + 0x5A3D80, (void*)FlagDetour, (void**)&original_flag)) { original_flag = NULL; hf->log(self, "LaunchFlag: hook refused"); }
    hf->log(self, "probing: SetPosition %s, LaunchFlag %s. Equip a LIFT-182 Warp Pack and warp a few times.", original_set ? "hooked" : "OFF",
        original_flag ? "hooked" : "OFF");
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 13 || api->size < offsetof(HfApi, read_code) + sizeof api->read_code) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    if (!api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) || !api->on_game_thread(mod, Tick, NULL))
        api->log(mod, "events or the game thread are unavailable");
    api->log(mod, "Warp Probe 0.1.0 (research only, no gameplay effect)");
    return 0;
}
