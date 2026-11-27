/* Maxi Mag Probe (yui.maxi-mag-probe): research for Hina's gun. The game's M-1000 Maxigun has no magazine: it draws its
 * rounds from the Maxigun Backpack it is linked to (WeaponLinkedAmmo; HD2Runtime's catalogue calls it backpack-
 * dependent). Can a Maxigun be built with a magazine of its own, so it fires with no backpack? Two tries, each an
 * entity of this probe's own (HellForge's add_entity, SDK 17 parts; nothing of the game's is changed):
 *
 *   A (Page Up): the Maxigun, with the MG-43 Machine Gun's magazine record added (a part from the MG-43). It keeps its
 *     linked-ammo record. HellForge refuses it if no entity of the game has its set of networked components.
 *   B (Page Down): the MG-43, wearing the Maxigun's model (model_from: its unit and the records tied to it), with the
 *     Maxigun's firing record (ProjectileWeapon: its round, rate, spin-up) and its handling (WeaponData). No linked ammo.
 *   End: the probe's gun put away and removed.
 *
 * Each try's gun gets probe_rounds rounds (its magazine's capacity and its count). In a mission, with no support
 * weapon on you (the probe refuses otherwise: it does not give one back), press Page Up or Page Down: the gun is put in your
 * hands the way W's King of Hearts does it, and the log says each second what its magazine reads; fire it and reload
 * it. What it answers: is each try added at all (its log line at the start), does it show the Maxigun, does it fire
 * from its own magazine, does it spin up, does it reload. Solo or host; turn it off when done. */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <windows.h>
#include "hellforge_mod.h"
#include "hellforge_components.h"
#include "hellforge_content.h"
#include "hellforge_functions.h"
#include "hellforge_globals.h"

#define VERSION "0.1.0"
#define BUILD "25480438"
#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"

#define ROUNDS 6000
static uint64_t g_try[2];                 /* A, B: the probe's entities (0: refused) */
static const char* const kTry[2] = {"A (the Maxigun with the MG-43's magazine)", "B (the MG-43 wearing the Maxigun)"};

/* ---- the components each try has --------------------------------------------------------------------------------- */
static const char* const kComps[] = {"WeaponMagazineComponentData", "WeaponLinkedAmmoComponentData", "ProjectileWeaponComponentData",
    "WeaponDataComponentData", "WeaponCustomizationComponentData", "LoadoutPackageComponentData", "UnitComponentData",
    "WeaponReloadComponentData", "WeaponRoundsComponentData", "WeaponHeatComponentData", "WeaponComponentData", "EquipmentComponentData", NULL};
static void Report(const char* who, uint64_t res)
{
    char line[1024];
    size_t n = 0;
    for (int i = 0; kComps[i]; i++)
    {
        uint32_t stride = 0;
        if (hf->component_record(kComps[i], res, &stride)) n += (size_t)snprintf(line + n, n < sizeof line ? sizeof line - n : 0, " %s", kComps[i]);
    }
    if (!n) snprintf(line, sizeof line, " (none it owns)");
    Log("%s %016llx owns:%s", who, (unsigned long long)res, line);
}
static void EntitiesReady(HfMod* mod, HfEvent event, void* user)
{
    Report("the M-1000 Maxigun", HF_ENTITY_M1000_MAXIGUN);
    Report("the MG-43", HF_ENTITY_MG43_MACHINE_GUN);
    for (int t = 0; t < 2; t++)
    {
        if (!g_try[t]) { Log("try %s: not added (the start of this log says why)", kTry[t]); continue; }
        Report(kTry[t], g_try[t]);
        HfField cap = HF_FIELD(WeaponMagazine, capacity), rate = HF_FIELD_AT(ProjectileWeapon, fire_rate, 1), proj = HF_FIELD(ProjectileWeapon, projectile);
        HfField spin = HF_FIELD(ProjectileWeapon, spinup_time);
        HfLiveValue c, r, p, s;
        memset(&c, 0, sizeof c); memset(&r, 0, sizeof r); memset(&p, 0, sizeof p); memset(&s, 0, sizeof s);
        int hc = hf->get_live(self, g_try[t], &cap, &c), hr = hf->get_live(self, g_try[t], &rate, &r), hp = hf->get_live(self, g_try[t], &proj, &p);
        int hs = hf->get_live(self, g_try[t], &spin, &s);
        HfValue v = HF_U32(ROUNDS);
        int set = hf->set_live(self, g_try[t], &cap, &v, 0) != 0;
        Log("  its magazine %s (capacity %.0f -> %d %s); its round %.0f, %.0f rpm, spin-up %.2f s%s", hc ? "is there" : "is NOT there", c.now.number, ROUNDS,
            set ? "set" : "REFUSED", hp ? p.now.number : -1, hr ? r.now.number : -1, hs ? s.now.number : -1, hp && hr ? "" : " (its firing record unread)");
    }
}

/* ---- the swap (W's King of Hearts) ---------------------------------------------------------------------------------- */
typedef void (*TakeFn)(void* inventory, uint32_t entity, uint32_t item, uint32_t slot, uint8_t flag);
typedef void (*DropFn)(void* inventory, uint32_t entity, uint32_t item, uint8_t flag, uint64_t zero, uint8_t one);
typedef void (*RequestFn)(void* object);
static TakeFn g_take;
static DropFn g_drop;
static RequestFn g_request;
static uint8_t g_context[0x8A0];
static uintptr_t InventoryRecord(uint32_t e)
{
    uintptr_t m = (uintptr_t)hf_global_InventoryManager(self), records;
    uint32_t i;
    if (!m || !Lookup(m + 0x28, e, 1048576, &i) || i >= 65536 || !(records = Pointer(m + 0x50))) return 0;
    return records + 0x30ull * i;
}
static uint64_t ResOf(uint32_t e) { uint8_t d[24]; return e && Descriptor(e, d) ? Quad(d) : 0; }
static uintptr_t SwitchObject(uint32_t e)
{
    uintptr_t am = (uintptr_t)hf_global_AvatarManager(self), obj;
    uint32_t i, who = 0;
    if (!am || !Lookup(am + 0xF8, e, 1048576, &i) || i >= 1024) return 0;
    obj = am + 0x53DCD0 + (uintptr_t)0x1238 * i;
    return ReadAt(obj, &who, 4) && who == e ? obj : 0;
}
static int Ask(uint32_t selection)
{
    uintptr_t obj = SwitchObject(atomic_load(&g_avatar));
    if (!obj || !g_request || hf->write(obj + 4, &selection, 4)) { Log("the switch request could not be made"); return 0; }
    g_request((void*)obj);
    return 1;
}
static uint32_t SpawnRes(uint64_t res)
{
    HfFn_RequestEntitySpawn spawn = hf_fn_RequestEntitySpawn(self);
    float p[3];
    uint32_t out = 0;
    if (!spawn || !PlayerPose(p, NULL)) return 0;
    struct { uint32_t one, network; float pose[16]; void* context; uint64_t pad; } info;
    memset(&info, 0, sizeof info);
    memset(g_context, 0, sizeof g_context);
    info.one = 1;
    info.network = 0x7FFF;
    info.pose[0] = info.pose[5] = info.pose[10] = info.pose[15] = 1.0f;
    info.pose[12] = p[0]; info.pose[13] = p[1]; info.pose[14] = p[2] + 1.0f;
    info.context = g_context;
    spawn(&out, res, &info);
    return out && out != UINT32_MAX ? out : 0;
}
static int Rounds(uint32_t weapon)
{
    HfFn_MagazineRounds rounds = hf_fn_MagazineRounds(self);
    void* wm = hf_global_WeaponManager(self);
    return rounds && wm && weapon ? rounds(wm, weapon) : -1;
}
static int Capacity(uint32_t weapon)
{
    HfFn_MagazineCapacity capacity = hf_fn_MagazineCapacity(self);
    uint32_t stride = 0;
    uint64_t res = ResOf(weapon);
    return capacity && res && hf->component_record("WeaponMagazineComponentData", res, &stride) ? capacity(NULL, weapon) : -1;
}

enum { S_IDLE, S_PACKAGE, S_WAIT, S_TAKE, S_HELD, S_AWAY, S_REMOVE };
static int g_s, g_t;
static uint32_t g_gun;
static double g_at, g_reportAt;
static int g_lastRounds = -2, g_fired;
static uint64_t g_pkg;
static void Start(int t)
{
    uint32_t me = atomic_load(&g_avatar), slots[8] = {0};
    uintptr_t rec = InventoryRecord(me);
    if (g_s != S_IDLE) { Log("busy: End first"); return; }
    if (!g_take || !g_drop || !g_request) { Log("off: the game's code is not the reviewed one"); return; }
    if (!g_try[t]) { Log("try %s was not added: nothing to give", kTry[t]); return; }
    if (!rec || !ReadAt(rec, slots, sizeof slots)) { Log("her inventory was not found"); return; }
    if (slots[2]) { Log("you carry a support weapon (%u): drop it first (the probe does not give one back)", slots[2]); return; }
    g_t = t;
    g_s = S_PACKAGE;
    g_at = g_now + 8;
    Log("---- try %s: into your hands ----", kTry[t]);
}
static void Frame(void)
{
    void* im = hf_global_InventoryManager(self);
    void* em = hf_global_EntityManager(self);
    HfFn_RemoveEntity remove = hf_fn_RemoveEntity(self);
    HfFn_AddMagazineRounds add = hf_fn_AddMagazineRounds(self);
    uint32_t me = atomic_load(&g_avatar);
    uintptr_t rec = InventoryRecord(me);
    if (g_s == S_IDLE) return;
    if (!im || !rec) { Log("she is gone: stopped"); g_s = S_IDLE; return; }
    switch (g_s)
    {
    case S_PACKAGE:
    {
        static int state;
        if (!g_pkg)
        {
            uint32_t stride = 0;
            uintptr_t r = hf->component_record("LoadoutPackageComponentData", HF_ENTITY_M1000_MAXIGUN, &stride);
            if (!r || !ReadAt(r + 8, &g_pkg, 8) || !g_pkg) g_pkg = ~0ull;
        }
        if (g_pkg != ~0ull && state != HF_PACKAGE_RESIDENT)
        {
            state = hf->load_package(self, g_pkg);
            if (state == HF_PACKAGE_LOADING) { if (g_now > g_at) { Log("the Maxigun's package did not load"); g_s = S_IDLE; } break; }
        }
        g_gun = SpawnRes(g_try[g_t]);
        Log("the probe's gun: entity %u (%016llx)", g_gun, (unsigned long long)ResOf(g_gun));
        if (!g_gun) { g_s = S_IDLE; break; }
        g_s = S_WAIT; g_at = g_now + 0.75;
        break;
    }
    case S_WAIT:
        if (g_now < g_at) break;
        if (ResOf(g_gun) != g_try[g_t]) { Log("the gun does not read as the try's: stopped"); g_s = S_IDLE; break; }
        g_take(im, me, g_gun, 3, 0);
        g_s = S_TAKE; g_at = g_now + 0.1;
        break;
    case S_TAKE:
    {
        if (g_now < g_at) break;
        Ask(3);
        int have = Rounds(g_gun);
        if (add && have >= 0 && have < ROUNDS) add(NULL, g_gun, ROUNDS - have);
        Log("in your hands: its magazine reads %d of %d (before the fill: %d)", Rounds(g_gun), Capacity(g_gun), have);
        g_s = S_HELD; g_reportAt = g_now + 1; g_lastRounds = -2; g_fired = 0;
        break;
    }
    case S_HELD:
    {
        uint32_t sel = 0, support = 0;
        ReadAt(rec + 0x1C, &sel, 4);
        ReadAt(rec + 8, &support, 4);
        int r = Rounds(g_gun);
        if (g_lastRounds >= 0 && r >= 0 && r < g_lastRounds) g_fired += g_lastRounds - r;
        if (g_now >= g_reportAt)
        {
            static unsigned told;
            g_reportAt = g_now + 1;
            if (told++ < 300 && (r != g_lastRounds || told < 5))
                Log("  rounds %d (fired %d so far), capacity %d; selection %u, support slot %u%s   [LMB %s]", r, g_fired, Capacity(g_gun), sel, support,
                    support == g_gun ? "" : " (not the gun: dropped?)", (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? "held" : "-");
            g_lastRounds = r;
        }
        break;
    }
    case S_AWAY:
        if (g_now < g_at) break;
        {
            uint32_t now = 0;
            g_drop(im, me, g_gun, 0, 0, 1);
            if (ReadAt(rec + 8, &now, 4) && now == g_gun) { now = 0; hf->write(rec + 8, &now, 4); }
        }
        g_s = S_REMOVE; g_at = g_now + 0.4;
        break;
    case S_REMOVE:
        if (g_now < g_at) break;
        if (ResOf(g_gun) == g_try[g_t] && remove && em) remove(em, g_gun);
        Log("the probe's gun removed (%d rounds fired)", g_fired);
        g_gun = 0;
        g_s = S_IDLE;
        break;
    default: g_s = S_IDLE;
    }
}
static void Away(void)
{
    if (g_s != S_HELD) { Log("nothing to take away"); return; }
    Ask(1);
    g_s = S_AWAY; g_at = g_now + 1.0;
}

static void OnPlayer(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    if (!p->local) return;
    if (info->kind == HF_ON_PLAYER_SPAWN && p->entity) { atomic_store(&g_avatar, p->entity); g_alive = 1; }
    else { g_alive = 0; if (g_s != S_IDLE) { Log("she is down: the probe's gun is left"); g_s = S_IDLE; } }
}
static void Tick(HfMod* mod, float dt, void* user)
{
    g_now += dt > 0 && dt < 1 ? dt : 0;
    int in = hf->game_state(self) == HF_STATE_MISSION;
    if (in != g_inMission) { g_inMission = in; g_s = S_IDLE; g_gun = 0; }
    if (!g_inMission || !game || !g_alive) return;
    static int prev[3];
    const int vk[3] = {VK_PRIOR, VK_NEXT, VK_END};
    int focused = Focused();
    for (int i = 0; i < 3; i++)
    {
        int down = focused && (GetAsyncKeyState(vk[i]) & 0x8000) != 0;
        if (down && !prev[i]) { if (i < 2) Start(i); else Away(); }
        prev[i] = down;
    }
    Frame();
}
static void Pin(void** slot, uintptr_t at, const char* pattern, const char* what)
{
    if (hf->code_matches(at, pattern)) *slot = (void*)at;
    else Log("%s is not the reviewed code: the swap is off", what);
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gameSize = 0, exeSize = 0;
    uintptr_t g = hf->module_base("game.dll", &gameSize), x = hf->module_base("helldivers2.exe", &exeSize);
    if (!g || !x || gameSize != 74727424u || exeSize != 60719104u) { Log("REFUSED: the game's modules are not build " BUILD "'s"); return; }
    game = g;
    exe = x;
    Pin((void**)&g_take, game + 0x9AE030, "40 53 55 41 55 48 83 ec 40 48 89 7c 24 68 41 8b e8 4c 89 64 24 70 8b da", "the inventory's pickup (0x9AE030)");
    Pin((void**)&g_drop, game + 0x9A8840, "40 53 56 41 56 48 81 ec a0 00 00 00 48 8b 05 bd 37 c9 01 48 33 c4 48 89", "the inventory's drop (0x9A8840)");
    Pin((void**)&g_request, game + 0xA93430, "48 89 5c 24 18 48 89 6c 24 20 56 57 41 54 41 56 41 57 48 83 ec 50 48 8b 05 c3", "the Helldiver's switch request (0xA93430)");
    Log("ready: in a mission with no support weapon on you, Page Up (try A) or Page Down (try B); End takes it away");
}
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 17 || api->size < offsetof(HfApi, global) + sizeof api->global || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    Log("Maxi Mag Probe " VERSION ": research for Hina's gun (a Maxigun with a magazine of its own)");
    static const HfEntityPart partsA[] = {{"WeaponMagazineComponentData", HF_ENTITY_MG43_MACHINE_GUN}, {NULL, 0}};
    HfEntity a = {sizeof a};
    a.base = HF_ENTITY_M1000_MAXIGUN;
    a.name = "yui/maxi_mag_probe/maxigun_with_magazine";
    a.parts = partsA;
    a.own_all = 1;
    g_try[0] = api->add_entity(mod, &a);
    static const HfEntityPart partsB[] = {{"ProjectileWeaponComponentData", HF_ENTITY_M1000_MAXIGUN}, {"WeaponDataComponentData", HF_ENTITY_M1000_MAXIGUN}, {NULL, 0}};
    HfEntity b = {sizeof b};
    b.base = HF_ENTITY_MG43_MACHINE_GUN;
    b.name = "yui/maxi_mag_probe/mg43_as_maxigun";
    b.model_from = HF_ENTITY_M1000_MAXIGUN;
    b.parts = partsB;
    b.own_all = 1;
    g_try[1] = api->add_entity(mod, &b);
    Log("try %s: %016llx; try %s: %016llx (0: refused, the line before says why)", kTry[0], (unsigned long long)g_try[0], kTry[1], (unsigned long long)g_try[1]);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    if (api->on(mod, HF_EVENT_ENTITIES_READY, EntitiesReady, NULL)) Log("the entities' event is unavailable: no report");
    int ok = api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) && api->on_game_thread(mod, Tick, NULL);
    if (!ok) Log("events or the game thread are unavailable");
    return 0;
}
