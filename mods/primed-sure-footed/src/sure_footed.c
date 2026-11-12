/* Primed Sure Footed (yui.primed-sure-footed): your Helldiver is never knocked down or ragdolled. Hits still flinch it
 * and push it back: only the hit result's knockdown flag is cleared; its stagger, push and hit reaction stay.
 *
 * How: the game works every hit out in its own code, and the result carries the reaction apart from the damage (a
 * damage hook that cancels a hit leaves its push). Three of the game's hit functions are hooked (game.dll, build
 * 25480438): the hit calculation, the explosion dispatch and the final emission (the dispatcher can set the reaction
 * after the calculation, so the flag is cleared again at emission). For a hit whose target is a Helldiver this machine
 * simulates, result +0x1E (knockdown) is set to 0. Result +0x0C stagger, +0x10 push and +0x1F hit reaction are left.
 *
 * Each function's bytes are checked before hooking (code_matches, SDK 13: another mod's hook on it does not fail the
 * check), and the module sizes must be the supported build's; otherwise nothing is hooked and the log says why.
 *
 * As a booster (0.2.0): with Extra Boosters (yui.extra-boosters) adding the Primed Sure Footed booster (its item id:
 * booster_id), knockdowns are stopped only while the game has that booster active in the mission (boosters work for the
 * whole squad; each player's machine does it for its own Helldiver, so everyone needs this mod). booster = 0, or no
 * Extra Boosters / no such booster in it: always on, as 0.1.0 was. Extra Boosters is asked through its exports
 * (extra_boosters_value / extra_boosters_active), on the game thread.
 *
 * Research credit: the hit layout, the hooked functions and their guards come from Helldiver Survival Rework 0.3.2 by
 * enochlophobianumerophobia (MIT; see THIRD_PARTY_NOTICES.md), whose research in turn used HD2Runtime by SkyeShade's
 * catalogues (https://github.com/SkyeShade/HD2Runtime). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <windows.h>
#include "hellforge_mod.h"

#define BUILD "25480438"
#define AVATAR UINT64_C(0x4D1C334D294DFA97)   /* the Helldiver's entity resource */
#define HIT_SIZE 0x118u
#define RESULT_SIZE 0x24u
#define H_KIND 0x00u
#define H_TARGET 0x20u
#define R_KNOCKDOWN 0x1Eu

static const HfApi* hf;
static HfMod* self;
static uintptr_t game;
static atomic_int g_ready, g_on;
static int g_enabled = 1, g_needBooster = 1;
static uint32_t g_boosterId = 0x1041BB56u;   /* Extra Boosters' booster 21 by default */
static _Atomic uint64_t g_cleared, g_seen, g_byKind[8];

static const struct { uint32_t rva; const char* hex; } kPins[] = {
    {0x129DC30u, "48 89 5c 24 08 57 48 83 ec 30 33 c0 0f 57 c0 0f 11 02 49 8b f8 48 8b da 0f 11 42 10 89 42 20 41"},  /* calculation */
    {0x129D180u, "48 89 4c 24 08 53 55 56 57 41 55 48 83 ec 60 41 8b 40 20 33 f6 3b 05 85 6a 1e 02 49 8b f8 44 0f"},  /* explosion dispatch */
    {0x129DD30u, "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 20 57 41 54 41 55 41 56 41 57 48 81 ec a0 00 00 00 44"},  /* emission */
};

static uint32_t Word(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t Quad(const uint8_t* p) { uint64_t v; memcpy(&v, p, 8); return v; }
static int AddressOk(uintptr_t a, size_t n) { return n && a >= 0x10000 && a < UINT64_C(0x800000000000) && n <= UINT64_C(0x800000000000) - a; }
static int ReadAt(uintptr_t a, void* out, size_t n) { return AddressOk(a, n) && hf->read(a, out, n) == 0; }
static uintptr_t Pointer(uintptr_t a) { uintptr_t v = 0; return ReadAt(a, &v, 8) && AddressOk(v, 1) ? v : 0; }

/* The entity table's hash lookup (bounded; a missing key is never slot 0) */
static int Lookup(uintptr_t at, uint32_t key, uint32_t limit, uint32_t* index)
{
    uint8_t h[20], row[8];
    if (!ReadAt(at, h, sizeof h)) return 0;
    uintptr_t entries = (uintptr_t)Quad(h);
    uint32_t n = Word(h + 8), empty = Word(h + 12), mult = Word(h + 16);
    if (!AddressOk(entries, 8) || !n || n > limit || (n & (n - 1)) || key == empty) return 0;
    for (unsigned i = 0; i < n && i < 128; i++)
    {
        if (!ReadAt(entries + 8u * ((key * mult + i) & (n - 1)), row, 8)) return 0;
        if (Word(row) == key) { *index = Word(row + 4); return *index != UINT32_MAX; }
        if (Word(row) == empty) return 0;
    }
    return 0;
}

/* A Helldiver this machine simulates (its resource is the avatar's, its authority bit set) */
static int LocalDiver(uint32_t entity)
{
    if (!entity || entity == UINT32_MAX || entity == 0x7FFF) return 0;
    uintptr_t root = Pointer(game + 0x346BF98);
    uint32_t index;
    uint8_t id[24];
    if (!root || !Lookup(root + 0xF1AEB0, entity, 1048576, &index) || index >= 262144) return 0;
    if (!ReadAt(root + 0xF32F18 + 24u * index, id, sizeof id) || Word(id + 8) != entity) return 0;
    return Quad(id) == AVATAR && (id[20] & 1);
}

/* Clears the knockdown flag of a result whose hit lands on a local Helldiver */
static void Steady(const void* hit, void* result)
{
    if (!atomic_load(&g_ready) || !atomic_load(&g_on) || !hit || !result) return;
    uint8_t h[HIT_SIZE], r[RESULT_SIZE];
    if (!ReadAt((uintptr_t)hit, h, sizeof h) || !LocalDiver(Word(h + H_TARGET))) return;
    atomic_fetch_add(&g_seen, 1);
    if (!ReadAt((uintptr_t)result, r, sizeof r) || !r[R_KNOCKDOWN]) return;
    ((uint8_t*)result)[R_KNOCKDOWN] = 0;
    atomic_fetch_add(&g_cleared, 1);
    uint32_t kind = Word(h + H_KIND);
    atomic_fetch_add(&g_byKind[kind < 7 ? kind : 7], 1);
}

typedef uintptr_t (*CalculateFn)(uintptr_t, void*, const void*);
typedef void (*BlastFn)(uintptr_t, void*, const void*);
typedef uintptr_t (*EmitFn)(uintptr_t, void*, const void*, uint32_t);
static CalculateFn original_calculate;
static BlastFn original_blast;
static EmitFn original_emit;

static uintptr_t Calculate(uintptr_t a, void* out, const void* hit)
{
    uintptr_t result = original_calculate(a, out, hit);
    Steady(hit, out);
    return result;
}
static void Blast(uintptr_t a, void* out, const void* hit)
{
    original_blast(a, out, hit);
    Steady(hit, out);
}
static uintptr_t Emit(uintptr_t a, void* result, const void* hit, uint32_t mode)
{
    Steady(hit, result);  /* the dispatcher may have set it after the calculation */
    return original_emit(a, result, hit, mode);
}

static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gameSize = 0, exeSize = 0;
    game = hf->module_base("game.dll", &gameSize);
    uintptr_t exe = hf->module_base("helldivers2.exe", &exeSize);
    if (!game || !exe || gameSize != 74727424u || exeSize != 60719104u)
    {
        hf->log(self, "REFUSED: the game's modules are not build " BUILD "'s; nothing hooked");
        return;
    }
    for (unsigned i = 0; i < sizeof kPins / sizeof kPins[0]; i++)
        if (!hf->code_matches(game + kPins[i].rva, kPins[i].hex))
        {
            hf->log(self, "REFUSED: game.dll+0x%X is not the reviewed code; nothing hooked", kPins[i].rva);
            return;
        }
    if (hf->hook(self, game + 0x129DC30, (void*)Calculate, (void**)&original_calculate) ||
        hf->hook(self, game + 0x129D180, (void*)Blast, (void**)&original_blast) ||
        hf->hook(self, game + 0x129DD30, (void*)Emit, (void**)&original_emit))
    {
        hf->log(self, "REFUSED: a hook could not be placed; the hooks placed stay pass-through");
        return;
    }
    atomic_store(&g_ready, 1);
    hf->log(self, "Sure Footed ready: hit calculation, explosion dispatch and emission hooked; knockdown cleared on your Helldiver%s",
        g_enabled ? "" : " (OFF in the ini)");
}

/* The booster: 1 when this mission's knockdowns are to be stopped (always, without the booster set up) */
typedef int (*BoosterFn)(uint32_t itemId);
static int BoosterAllows(int mission)
{
    static int wasMission, mode;          /* mode: 0 not looked yet, 1 always on, 2 needs the booster */
    static int active = -1;
    static BoosterFn isActive;
    if (!mission) { wasMission = 0; return 0; }
    if (!wasMission)
    {
        wasMission = 1;
        active = -1;
        mode = 1;
        HMODULE eb = g_needBooster ? GetModuleHandleA("extra_boosters.dll") : NULL;
        BoosterFn value = eb ? (BoosterFn)(void*)GetProcAddress(eb, "extra_boosters_value") : NULL;
        isActive = eb ? (BoosterFn)(void*)GetProcAddress(eb, "extra_boosters_active") : NULL;
        if (!g_needBooster) hf->log(self, "mission: on (booster = 0 in the ini: no booster needed)");
        else if (!value || !isActive) hf->log(self, "mission: on (Extra Boosters is not loaded, so no booster needed)");
        else if (value(g_boosterId) < 0) hf->log(self, "mission: on (Extra Boosters has no booster 0x%08X, so no booster needed)", g_boosterId);
        else mode = 2;
    }
    if (mode != 2) return 1;
    int now = isActive(g_boosterId) != 0;
    if (now != active)
    {
        active = now;
        hf->log(self, "mission: the Primed Sure Footed booster is %s: knockdowns %s", now ? "active" : "not active", now ? "stopped" : "as in the game");
    }
    return now;
}

static void Tick(HfMod* mod, float dt, void* user)
{
    int mission = hf->game_state(self) == HF_STATE_MISSION;
    int allowed = g_enabled ? BoosterAllows(mission) : 0;   /* called every tick, so each mission starts with a fresh look */
    int on = g_enabled && mission && allowed;
    atomic_store(&g_on, on);
    static uint64_t reported;
    static float since;
    since += dt;
    uint64_t cleared = atomic_load(&g_cleared);
    if (cleared != reported && since >= 5)
    {
        since = 0;
        reported = cleared;
        hf->log(self, "knockdowns stopped: %llu (bullet %llu, explosion %llu, impact %llu, ability %llu, other %llu) of %llu hits on you",
            (unsigned long long)cleared, (unsigned long long)atomic_load(&g_byKind[0]), (unsigned long long)atomic_load(&g_byKind[4]),
            (unsigned long long)atomic_load(&g_byKind[5]), (unsigned long long)atomic_load(&g_byKind[6]),
            (unsigned long long)(atomic_load(&g_byKind[1]) + atomic_load(&g_byKind[2]) + atomic_load(&g_byKind[3]) + atomic_load(&g_byKind[7])),
            (unsigned long long)atomic_load(&g_seen));
    }
}

/* The settings ship in primed-sure-footed.ini: a mod never writes its folder (HellForge holds it read-only in a session). */
static void LoadSettings(void)
{
    char path[MAX_PATH * 2], line[256];
    snprintf(path, sizeof path, "%s\\primed-sure-footed.ini", hf->mod_folder(self));
    FILE* f = fopen(path, "r");
    if (!f) { hf->log(self, "no primed-sure-footed.ini: the built-in settings"); return; }
    while (fgets(line, sizeof line, f))
    {
        int v;
        unsigned u;
        if (line[0] == ';') continue;
        if (sscanf(line, " enabled = %d", &v) == 1) g_enabled = v != 0;
        else if (sscanf(line, " booster = %d", &v) == 1) g_needBooster = v != 0;
        else if (sscanf(line, " booster_id = %x", &u) == 1) g_boosterId = u;
    }
    fclose(f);
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 13 || api->size < offsetof(HfApi, code_matches) + sizeof api->code_matches || !api->code_matches) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    LoadSettings();
    api->log(mod, "Primed Sure Footed 0.2.0 (SDK %u, game " BUILD "): %s%s", api->version, g_enabled ? "on" : "off in the ini",
        g_enabled && g_needBooster ? " (with Extra Boosters: while its Primed Sure Footed booster is active)" : "");
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) { api->log(mod, "REFUSED: the game-loaded event"); return 1; }
    /* registered: from here the DLL stays loaded, so a failure only leaves the mod inert */
    if (!api->on_game_thread(mod, Tick, NULL)) api->log(mod, "the game thread could not be hooked: the mod stays off");
    return 0;
}
