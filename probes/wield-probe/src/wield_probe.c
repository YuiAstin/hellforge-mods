/* Wield Probe (yui.wield-probe): research for W's King of Hearts. Can a mod put a GL-21 Grenade Launcher in the
 * Helldiver's hands, give it 40 rounds, and hand her own weapon back afterwards? Nothing here has run yet. Solo or
 * host; turn it off when done. Each step has its own key, so the log shows which one a crash follows.
 *
 *   always        the game's own Wield, SwitchWeapon and SetWielder calls on your Helldiver are logged (switch weapons
 *                 and pick a support weapon up by hand first: that shows the slots and Wield's fifth argument), and
 *                 what you hold whenever it changes
 *   Insert        your inventory record, your wield slots, the held weapon's magazine
 *   Shift+Insert  loads the GL-21's package and spawns a GL-21 at your feet (RequestEntitySpawn)
 *   Ctrl+Insert   way A: wields it straight into the slot your last observed Wield used (the inventory is untouched)
 *   Alt+Insert    way B: writes it into your inventory's support slot (record +8) and selects it (SwitchWeapon 3)
 *   Home          its magazine to 40 rounds (AddMagazineRounds)
 *   Delete        gives your weapon back: the support slot as it was (after way B) and your earlier selection
 *   Shift+Delete  removes the spawned GL-21 (RemoveEntity)
 *   End           refills the weapon you hold: its magazine to capacity, and a resupply's spare ammunition
 *
 * Credit: the wielder, inventory and ammunition functions and globals are HellForge's mappings (HD2Runtime by
 * SkyeShade's research); the entity reads are the kit base's. Build 25480438 only. */
#include <ctype.h>
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
#include "hellforge_functions.h"
#include "hellforge_globals.h"

#define BUILD "25480438"
#define VERSION "0.1.0"
#include "kitbase/kb_core.h"

#define GL21 HF_ENTITY_GL21_GRENADE_LAUNCHER
#define ROUNDS 40

static atomic_uint g_avatar, g_held, g_selection;
static atomic_ullong g_heldRes;
static double g_now;

/* ---- the detours: integer stores only before the game's function runs --------------------------------------------- */
typedef uintptr_t (*Fn8)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
typedef struct Call { volatile uint64_t a[6]; volatile uint64_t ret; volatile uint64_t seq; } Call;
#define RING 64
static Call g_wield[RING], g_switch[RING], g_setw[RING];
static volatile uint64_t g_wieldN, g_switchN, g_setwN;
static Fn8 original_wield, original_switch, original_setw;
#define RECORD(ring, n)                                                                       \
    do {                                                                                      \
        uint64_t i_ = n;                                                                      \
        Call* c_ = &ring[i_ % RING];                                                          \
        c_->a[0] = a1; c_->a[1] = a2; c_->a[2] = a3; c_->a[3] = a4; c_->a[4] = a5; c_->a[5] = a6; \
        c_->ret = (uint64_t)(uintptr_t)__builtin_return_address(0);                           \
        c_->seq = i_ + 1;                                                                     \
        n = i_ + 1;                                                                           \
    } while (0)
#define DETOUR(name, ring, n, original)                                                                                   \
    __attribute__((noinline)) static uintptr_t name(uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, \
        uintptr_t a6, uintptr_t a7, uintptr_t a8)                                                                         \
    {                                                                                                                     \
        RECORD(ring, n);                                                                                                  \
        return original(a1, a2, a3, a4, a5, a6, a7, a8);                                                                  \
    }
DETOUR(WieldDetour, g_wield, g_wieldN, original_wield)
DETOUR(SwitchDetour, g_switch, g_switchN, original_switch)
DETOUR(SetWielderDetour, g_setw, g_setwN, original_setw)

/* her last Wield, as the game made it: the slot and the fifth argument way A repeats */
static int g_haveWield;
static uint32_t g_wieldSlot, g_wieldS5;

static const char* Name(uint64_t r, char* out, size_t n)
{
    if (r == GL21) snprintf(out, n, "the GL-21");
    else if (r == AVATAR) snprintf(out, n, "a Helldiver");
    else snprintf(out, n, "%016llx", (unsigned long long)r);
    return out;
}
static uint64_t ResOf(uint32_t e) { uint8_t d[24]; return Descriptor(e, d) ? Quad(d) : 0; }
static void Report(void)
{
    static uint64_t seenW, seenS, seenT;
    uint32_t me = atomic_load(&g_avatar);
    char nm[40];
    for (uint64_t now = g_wieldN; seenW < now; seenW++)
    {
        if (now - seenW > RING) seenW = now - RING;
        Call* c = &g_wield[seenW % RING];
        uint32_t wielder = (uint32_t)c->a[1], slot = (uint32_t)c->a[2], item = (uint32_t)c->a[3], s5 = (uint32_t)c->a[4];
        if (wielder != me) continue;
        Log("game: Wield(you, slot %u, item %u = %s, s5 %u [%llx]) from game+0x%llX", slot, item, Name(ResOf(item), nm, sizeof nm), s5,
            (unsigned long long)c->a[4], (unsigned long long)(c->ret - game));
        g_haveWield = 1; g_wieldSlot = slot; g_wieldS5 = s5;
    }
    for (uint64_t now = g_switchN; seenS < now; seenS++)
    {
        if (now - seenS > RING) seenS = now - RING;
        Call* c = &g_switch[seenS % RING];
        if ((uint32_t)c->a[1] != me) continue;
        Log("game: SwitchWeapon(you, selection %u) from game+0x%llX", (uint32_t)c->a[2], (unsigned long long)(c->ret - game));
    }
    for (uint64_t now = g_setwN; seenT < now; seenT++)
    {
        if (now - seenT > RING) seenT = now - RING;
        Call* c = &g_setw[seenT % RING];
        uint32_t weapon = (uint32_t)c->a[1], wielder = (uint32_t)c->a[2];
        if (wielder != me && ResOf(weapon) != GL21) continue;
        Log("game: SetWielder(weapon %u = %s, wielder %u%s) from game+0x%llX", weapon, Name(ResOf(weapon), nm, sizeof nm), wielder,
            wielder == me ? " = you" : "", (unsigned long long)(c->ret - game));
    }
}

/* ---- her records --------------------------------------------------------------------------------------------- */
static void Hex(const uint8_t* b, int n, char* out) { for (int i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", b[i]); }
static uintptr_t InventoryRecord(uint32_t e)
{
    uintptr_t m = (uintptr_t)hf_global_InventoryManager(self), records;
    uint32_t i;
    if (!m || !Lookup(m + 0x28, e, 1048576, &i) || i >= 65536 || !(records = Pointer(m + 0x50))) return 0;
    return records + 0x30ull * i;
}
static uintptr_t WieldSlots(uint32_t e)
{
    uintptr_t m = (uintptr_t)hf_global_WielderManager(self), slots;
    uint32_t i;
    if (!m || !Lookup(m + 0x30, e, 1048576, &i) || i >= 65536 || !(slots = Pointer(m + 0x60))) return 0;
    return slots + 0x1D0ull * i;
}
static void Magazine(const char* what, uint32_t weapon)
{
    HfFn_MagazineRounds rounds = hf_fn_MagazineRounds(self);
    HfFn_MagazineCapacity capacity = hf_fn_MagazineCapacity(self);
    HfFn_OwnSpareAmmo spare = hf_fn_OwnSpareAmmo(self);
    void* wm = hf_global_WeaponManager(self);
    uint32_t stride = 0;
    uint64_t res = ResOf(weapon);
    if (!weapon || !res) { Log("%s: no weapon entity", what); return; }
    if (!hf->component_record("WeaponMagazineComponentData", res, &stride)) { Log("%s: entity %u has no magazine component (not asked)", what, weapon); return; }
    Log("%s: entity %u: %d rounds of %d in the magazine, spare %u", what, weapon, rounds && wm ? rounds(wm, weapon) : -1, capacity ? capacity(NULL, weapon) : -1,
        spare && wm ? spare(wm, weapon) : 0u);
}
static void State(const char* why)
{
    uint32_t me = atomic_load(&g_avatar), held = atomic_load(&g_held);
    uint8_t rec[0x30], slots[0x190];
    char hex[2 * 0x50 + 1], nm[40];
    if (!me) { Log("%s: your Helldiver is not known yet (switch weapons once)", why); return; }
    uintptr_t r = InventoryRecord(me), s = WieldSlots(me);
    Log("%s: you are entity %u, holding %u (%s), selection %u", why, me, held, Name(atomic_load(&g_heldRes), nm, sizeof nm), atomic_load(&g_selection));
    if (r && ReadAt(r, rec, sizeof rec))
    {
        Hex(rec, 0x30, hex);
        Log("  inventory record: %s", hex);
        Log("  as slots: +0 %u, +4 %u, +8 %u, +0xC %u, +0x10 %u, +0x14 %u; selection (+0x1C) %u", Word(rec), Word(rec + 4), Word(rec + 8), Word(rec + 12), Word(rec + 16),
            Word(rec + 20), Word(rec + 0x1C));
    }
    else Log("  inventory record: not found (manager %p)", (void*)hf_global_InventoryManager(self));
    if (s && ReadAt(s, slots, sizeof slots))
        for (int k = 0; k < 5; k++)
        {
            Hex(slots + 0x50 * k, 0x50, hex);
            Log("  wield slot %d (item %u = %s): %s", k, Word(slots + 0x50 * k), Name(ResOf(Word(slots + 0x50 * k)), nm, sizeof nm), hex);
        }
    else Log("  wield slots: not found (manager %p)", (void*)hf_global_WielderManager(self));
    Magazine("  held", held);
}

/* ---- the steps ----------------------------------------------------------------------------------------------- */
static uint32_t g_gl;                       /* the GL-21 the probe spawned */
static int g_pkg;                           /* its package: 0 not asked, else HF_PACKAGE_... */
static uint64_t g_package;
static int g_bWritten;                      /* way B wrote the support slot */
static uint32_t g_bWas, g_selWas;           /* what it held before, and her selection before a wield */
static int g_wantSpawn;
static uint8_t g_context[0x8A0];

static int Mine(const char* key)
{
    if (!atomic_load(&g_avatar)) { Log("%s: your Helldiver is not known yet (switch weapons once)", key); return 0; }
    if (!ResOf(atomic_load(&g_avatar))) { Log("%s: your Helldiver is gone", key); return 0; }
    return 1;
}
static int HaveGl(const char* key)
{
    if (!g_gl || ResOf(g_gl) != GL21) { Log("%s: there is no spawned GL-21 (Shift+Insert first)", key); g_gl = 0; return 0; }
    return 1;
}
static void SpawnNow(void)
{
    HfFn_RequestEntitySpawn spawn = hf_fn_RequestEntitySpawn(self);
    uint8_t d[24];
    float p[3];
    uint32_t me = atomic_load(&g_avatar), out = 0;
    if (!spawn) { Log("Shift+Insert: RequestEntitySpawn is not mapped: not called"); return; }
    if (!Descriptor(me, d) || !UnitNode(Word(d + 12), 0, p)) { Log("Shift+Insert: your position is unreadable"); return; }
    struct { uint32_t one, network; float pose[16]; void* context; uint64_t pad; } info;
    _Static_assert(sizeof info == 0x58, "the spawn record is 0x58 bytes");
    memset(&info, 0, sizeof info);
    memset(g_context, 0, sizeof g_context);
    info.one = 1;
    info.network = 0x7FFF;
    info.pose[0] = info.pose[5] = info.pose[10] = info.pose[15] = 1.0f;
    info.pose[12] = p[0] + 1.0f; info.pose[13] = p[1]; info.pose[14] = p[2] + 0.5f;
    info.context = g_context;
    Log("Shift+Insert: asking for a GL-21 at %.1f %.1f %.1f ...", (double)info.pose[12], (double)info.pose[13], (double)info.pose[14]);
    spawn(&out, GL21, &info);
    g_gl = out && out != UINT32_MAX ? out : 0;
    if (g_gl) Log("Shift+Insert: made entity %u (%s). Is it on the ground by you? Can you pick it up by hand?", g_gl, ResOf(g_gl) == GL21 ? "reads as a GL-21" : "does not read as a GL-21 yet");
    else Log("Shift+Insert: no entity came back (out %u)", out);
}
static void Spawn(void)
{
    uint32_t stride = 0;
    if (!Mine("Shift+Insert")) return;
    if (g_gl && ResOf(g_gl) == GL21) { Log("Shift+Insert: GL-21 %u is still there (Shift+Delete removes it)", g_gl); return; }
    if (!g_package)
    {
        uintptr_t rec = hf->component_record("LoadoutPackageComponentData", GL21, &stride);
        if (!rec || !ReadAt(rec + 8, &g_package, 8) || !g_package) { Log("Shift+Insert: the GL-21 has no loadout package record: spawning without one"); g_package = ~0ull; }
        else Log("Shift+Insert: the GL-21's package is %016llx", (unsigned long long)g_package);
    }
    g_wantSpawn = 1;
}
static void SpawnFrame(void)
{
    static double next;
    if (!g_wantSpawn || g_now < next) return;
    next = g_now + 0.5;
    if (g_package != ~0ull && g_pkg != HF_PACKAGE_RESIDENT)
    {
        int was = g_pkg;
        g_pkg = hf->load_package(self, g_package);
        if (g_pkg == HF_PACKAGE_LOADING) { if (!was) Log("Shift+Insert: its package is loading ..."); return; }
        if (!g_pkg) { Log("Shift+Insert: its package was refused (%s): spawning anyway", hf->last_log ? hf->last_log(self) : ""); g_package = ~0ull; }
        else Log("Shift+Insert: its package is in");
    }
    g_wantSpawn = 0;
    SpawnNow();
}
static void Remember(void)
{
    uint32_t sel = atomic_load(&g_selection);
    if (sel) g_selWas = sel;
}
static void WieldA(void)
{
    HfFn_Wield wield = hf_fn_Wield(self);
    HfFn_SetWielder setw = hf_fn_SetWielder(self);
    void* wm = hf_global_WielderManager(self);
    uint32_t me = atomic_load(&g_avatar);
    if (!Mine("Ctrl+Insert") || !HaveGl("Ctrl+Insert")) return;
    if (!g_haveWield) { Log("Ctrl+Insert: no Wield of yours was seen yet (switch weapons once, so its slot and fifth argument are known)"); return; }
    if (!wield || !wm) { Log("Ctrl+Insert: Wield or the wielder manager is not mapped: not called"); return; }
    Remember();
    Log("Ctrl+Insert (way A): SetWielder(%u, you), then Wield(you, slot %u, %u, s5 %u) ...", g_gl, g_wieldSlot, g_gl, g_wieldS5);
    if (setw) setw(0, g_gl, me);
    wield(wm, me, g_wieldSlot, g_gl, g_wieldS5);
    Log("Ctrl+Insert: returned. Is the GL-21 in your hands? Does it aim and fire? (Insert logs the state; Delete gives your weapon back)");
}
static void WieldB(void)
{
    HfFn_SwitchWeapon sw = hf_fn_SwitchWeapon(self);
    HfFn_SetWielder setw = hf_fn_SetWielder(self);
    void* im = hf_global_InventoryManager(self);
    uint32_t me = atomic_load(&g_avatar), was = 0;
    uintptr_t rec;
    if (!Mine("Alt+Insert") || !HaveGl("Alt+Insert")) return;
    if (!sw || !im || !(rec = InventoryRecord(me)) || !ReadAt(rec + 8, &was, 4)) { Log("Alt+Insert: SwitchWeapon or your inventory record is not there: nothing done"); return; }
    if (g_bWritten) { Log("Alt+Insert: the support slot is already the probe's (Delete first)"); return; }
    Remember();
    Log("Alt+Insert (way B): your support slot (record +8) %u -> %u, SetWielder, SwitchWeapon(you, 3) ...", was, g_gl);
    if (hf->write(rec + 8, &g_gl, 4)) { Log("Alt+Insert: the write was refused: nothing done"); return; }
    g_bWas = was; g_bWritten = 1;
    if (setw) setw(0, g_gl, me);
    sw(im, me, 3);
    Log("Alt+Insert: returned. Is the GL-21 in your hands? Does it aim and fire? (Delete gives your weapon back)");
}
static void Fill(void)
{
    HfFn_MagazineRounds rounds = hf_fn_MagazineRounds(self);
    HfFn_AddMagazineRounds add = hf_fn_AddMagazineRounds(self);
    void* wm = hf_global_WeaponManager(self);
    if (!HaveGl("Home")) return;
    if (!rounds || !add || !wm) { Log("Home: the magazine functions are not mapped: not called"); return; }
    Magazine("Home: before", g_gl);
    int have = rounds(wm, g_gl);
    if (have < ROUNDS) add(NULL, g_gl, ROUNDS - have);
    Magazine("Home: after (asked for 40)", g_gl);
}
static void GiveBack(void)
{
    HfFn_SwitchWeapon sw = hf_fn_SwitchWeapon(self);
    void* im = hf_global_InventoryManager(self);
    uint32_t me = atomic_load(&g_avatar), sel = g_selWas ? g_selWas : 1;
    uintptr_t rec;
    if (!Mine("Delete")) return;
    if (g_bWritten && (rec = InventoryRecord(me)))
    {
        uint32_t now = 0;
        ReadAt(rec + 8, &now, 4);
        if (now == g_gl && !hf->write(rec + 8, &g_bWas, 4)) Log("Delete: your support slot is %u again", g_bWas);
        else Log("Delete: your support slot holds %u, not the probe's GL-21: left as it is", now);
        g_bWritten = 0;
    }
    if (!sw || !im) { Log("Delete: SwitchWeapon is not mapped: not called"); return; }
    Log("Delete: SwitchWeapon(you, %u) ...", sel);
    sw(im, me, sel);
    Log("Delete: returned. Is your own weapon back in your hands? (Shift+Delete removes the GL-21, End refills what you hold)");
}
static void Remove(void)
{
    HfFn_RemoveEntity remove = hf_fn_RemoveEntity(self);
    void* em = hf_global_EntityManager(self);
    if (!HaveGl("Shift+Delete")) return;
    if (atomic_load(&g_held) == g_gl) { Log("Shift+Delete: you still hold it (Delete first)"); return; }
    if (!remove || !em) { Log("Shift+Delete: RemoveEntity is not mapped: not called"); return; }
    Log("Shift+Delete: RemoveEntity(%u) ...", g_gl);
    remove(em, g_gl);
    Log("Shift+Delete: asked. Did it vanish?");
    g_gl = 0;
}
static void Refill(void)
{
    HfFn_MagazineRounds rounds = hf_fn_MagazineRounds(self);
    HfFn_MagazineCapacity capacity = hf_fn_MagazineCapacity(self);
    HfFn_AddMagazineRounds add = hf_fn_AddMagazineRounds(self);
    HfFn_WeaponRefill refill = hf_fn_WeaponRefill(self);
    void* wm = hf_global_WeaponManager(self);
    uint32_t held = atomic_load(&g_held), stride = 0;
    if (!held || !ResOf(held)) { Log("End: you hold nothing"); return; }
    Magazine("End: before", held);
    if (hf->component_record("WeaponMagazineComponentData", ResOf(held), &stride) && rounds && capacity && add && wm)
    {
        int have = rounds(wm, held), cap = capacity(NULL, held);
        if (cap > have) add(NULL, held, cap - have);
    }
    if (refill) refill(0, held, 1.0f);
    Magazine("End: after (magazine to capacity, one resupply of spares)", held);
}

/* ---- events and the frame ------------------------------------------------------------------------------------ */
static atomic_int g_heldChanged;
static void OnWeapon(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfWeaponEvent* w = (const HfWeaponEvent*)info;
    if (w->avatar) atomic_store(&g_avatar, w->avatar);
    atomic_store(&g_held, w->entity);
    atomic_store(&g_heldRes, w->resource);
    atomic_store(&g_selection, w->selection);
    atomic_store(&g_heldChanged, 1);
}
static void OnPlayer(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    if (p->local && info->kind == HF_ON_PLAYER_SPAWN && p->entity) atomic_store(&g_avatar, p->entity);
}
static atomic_uint g_glSpawned;
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    if (s->resource == GL21) atomic_store(&g_glSpawned, s->entity);
}
static void Tick(HfMod* mod, float dt, void* user)
{
    g_now += dt;
    if (!game) return;
    Report();
    uint32_t e = atomic_exchange(&g_glSpawned, 0);
    if (e) Log("spawn event: a GL-21, entity %u%s", e, e == g_gl ? " (the probe's)" : "");
    if (atomic_exchange(&g_heldChanged, 0)) State("held changed");
    SpawnFrame();
    static int prev[4];
    int focused = Focused(), shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0, ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0,
        alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    int k[4] = {focused && (GetAsyncKeyState(VK_INSERT) & 0x8000), focused && (GetAsyncKeyState(VK_HOME) & 0x8000),
        focused && (GetAsyncKeyState(VK_DELETE) & 0x8000), focused && (GetAsyncKeyState(VK_END) & 0x8000)};
    if (k[0] && !prev[0]) { if (shift) Spawn(); else if (ctrl) WieldA(); else if (alt) WieldB(); else State("Insert"); }
    if (k[1] && !prev[1]) Fill();
    if (k[2] && !prev[2]) { if (shift) Remove(); else GiveBack(); }
    if (k[3] && !prev[3]) Refill();
    memcpy(prev, k, sizeof prev);
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gs = 0, xs = 0;
    uintptr_t g = hf->module_base("game.dll", &gs), x = hf->module_base("helldivers2.exe", &xs);
    if (!g || !x || gs != 74727424u || xs != 60719104u) { Log("REFUSED: not build " BUILD); return; }
    game = g;
    exe = x;
    int w = hf->hook_function(self, "Wield", (void*)WieldDetour, (void**)&original_wield, 0),
        s = hf->hook_function(self, "SwitchWeapon", (void*)SwitchDetour, (void**)&original_switch, 0),
        t = hf->hook_function(self, "SetWielder", (void*)SetWielderDetour, (void**)&original_setw, 0);
    Log("ready: Wield %s, SwitchWeapon %s, SetWielder %s. First switch weapons and pick a support weapon up by hand; then Shift+Insert (spawn a GL-21), "
        "Ctrl+Insert or Alt+Insert (wield it: way A or B), Home (40 rounds), Delete (your weapon back), Shift+Delete (remove it), End (refill); Insert logs the state",
        w ? "OFF" : "hooked", s ? "OFF" : "hooked", t ? "OFF" : "hooked");
}
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 16 || api->size < offsetof(HfApi, global) + sizeof api->global || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    Log("Wield Probe " VERSION ": research for W's King of Hearts (a GL-21 into your hands and your weapon back)");
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) && api->on_game_thread(mod, Tick, NULL);
    if (!ok) Log("events or the game thread are unavailable");
    return 0;
}
