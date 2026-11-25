/* Wield Probe (yui.wield-probe): research for W's King of Hearts. Can a mod put a GL-21 Grenade Launcher in the
 * Helldiver's hands, give it 40 rounds, and hand her own weapon back afterwards? Nothing here has run yet. Solo or
 * host; turn it off when done. Each step has its own key, so the log shows which one a crash follows.
 *
 *   always        the game's own Wield, SwitchWeapon and SetWielder calls on your Helldiver are logged (switch weapons
 *                 and pick a support weapon up by hand first: that shows the slots and Wield's fifth argument), and
 *                 what you hold whenever it changes
 *   0.2.0: Insert alone does it all: spawns a GL-21, puts it in your hands (way A) and gives it 40 rounds; fire
 *                 it (each round spent is logged). Delete alone undoes it all: your weapon back, the GL-21 removed,
 *                 your weapon refilled. The keys below are the single steps (0.1.x found: way A holds until a weapon
 *                 switch; way B is switched away by the game in half a second; 40 rounds read back on a 10-round GL)
 *   0.3.0: way A fired (29 of the 40 rounds) but the GL-21 hung in the air and her rifle stayed in her hands: Wield
 *                 alone does not carry it. Insert and Delete now go the game's own way for an item taken in hand
 *                 (way C): the inventory's set-held-item (game.dll+0x9AB950: the old item unwielded, the new one
 *                 attached to her and wielded, record +0x10) and SwitchWeapon 5; Delete clears it the same way
 *   0.3.1: way C crashed the game half a second later (game.dll+0xA99016: the Helldiver's update reads a record a
 *                 hand-carried item has and a GL-21 has not), so the held-item slot is not for weapons and way C is
 *                 gone. Now the probe watches the game do it: Insert drops a GL-21 at your feet; pick it up BY HAND,
 *                 switch weapons, drop it. The carry function (game.dll+0x8BF870) is logged with who called it, with
 *                 the Wield, SwitchWeapon and SetWielder calls: that names the game's support-weapon pickup.
 *                 Delete removes the probe's GL-21
 *   0.4.0: the pickup by hand named the game's own functions: take an item into an inventory slot
 *                 (game.dll+0x9AE030: inventory, Helldiver, item, slot 3 = support, 0; it carries it through 0x9AA290
 *                 and 0x8BF870) and drop it (0x9A8840). Insert now spawns a GL-21, takes it into your support slot
 *                 that way, selects it and gives it 40 rounds (way D); Delete drops it, gives your weapon back,
 *                 removes it and refills. With a support weapon already carried, Insert does nothing (it would be
 *                 dropped)
 *   0.5.0 (Yui: it should replace what is in her hand, not fill the support slot): way E. Insert takes the GL-21
 *                 into the slot of the weapon in her hand (primary, secondary or support) with the same pickup, which
 *                 drops that weapon; Delete takes that weapon back into the slot the same way (which drops the
 *                 GL-21), removes the GL-21 and refills her weapon. 0.4.0's way D never ran: no Insert reached it
 *   0.5.1: way E held: she took the pose and fired, slot and carry calls as a pickup by hand makes them, but the
 *                 GL-21 stayed where it was spawned. It was taken in the very frame it was asked for, before its
 *                 spawn had finished; now it is taken 0.75 s after it is made
 *   0.6.0: with the wait the GL-21 is carried, but on her back (stowed) while her hands hold the pose. Two tries in
 *                 one build. Insert: she puts her weapon away first (selects another slot), the GL-21 is taken into
 *                 the emptied hand's slot, and half a second later that slot is selected, so the game plays its own
 *                 draw. Ctrl+Insert: taken as in 0.5.1, then dropped the game's way and taken again 0.7 s later (a
 *                 GL-21 that had been dropped and picked up by hand was an ordinary weapon). Delete undoes either
 *   0.7.0: both tries left it on her back, firing (Yui: carried and her pose set, but the draw never made). All of
 *                 those were in the PRIMARY slot. Alt+Insert (try 3) takes it into the SUPPORT slot (none carried)
 *                 and selects that, as a pickup by hand does. And the game's AttachToUnit is logged for her items
 *                 and the GL-21 (child, node, who called), so a switch by hand shows what moves a weapon to her
 *                 hands and what the tries lack
 *   0.8.0: the switch by hand showed it. A weapon goes to her back from 0x8C06A3 (node 27 a rifle, 11 a sidearm,
 *                 30 a support weapon) and to her hand (node 84) from 0x8C0B70 a frame or two after the switch; a
 *                 GL-21 taken into the support slot went to her hands when Yui selected it by hand, and the game
 *                 switched her back to the primary after the probe's own SwitchWeapon. The Helldiver keeps its own
 *                 wanted selection: its switch request (game.dll+0xA93430, on an object that starts with her entity
 *                 and the wanted selection at +4) is what a key press runs. Insert (try 4): the GL-21 into the
 *                 support slot (none carried), then that request for selection 3, on the object seen at her last
 *                 switch by hand. Delete: the request for her earlier selection, then the GL-21 let go and removed
 *   0.9.0: try 4 played whole (in her hands, 17 rounds fired, her rifle back). Insert and Delete are now the skill
 *                 as the kit would run it (Yui: a support weapon she carries is deleted, and a new one of its kind
 *                 made for her afterwards: only which weapon it was is kept). No switch by hand first: her
 *                 switch-request object is the Avatar manager's +0x53DCD0 + 0x1238 x her index (as 0x9AC750 and
 *                 0x9A2D9C build it), checked against the one a switch by hand shows. Insert: the GL-21 made, her
 *                 support weapon (if any) dropped by the take and removed, the GL-21 selected, 40 rounds. Delete:
 *                 her earlier weapon selected, the GL-21 removed, a new support weapon of the kind she had made and
 *                 taken, everything refilled. The other keys are the earlier single steps
 *   Ctrl+Home     your inventory record, your wield slots, the held weapon's magazine
 *   Shift+Insert  loads the GL-21's package and spawns a GL-21 at your feet (RequestEntitySpawn)
 *   Ctrl+Insert   way A: wields it straight into the slot your last observed Wield used (the inventory is untouched)
 *   Alt+Insert    way B: writes it into your inventory's support slot (record +8) and selects it (SwitchWeapon 3)
 *   Home          its magazine to 40 rounds (AddMagazineRounds)
 *   Ctrl+Delete   gives your weapon back: the support slot as it was (after way B) and your earlier selection
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
#define VERSION "0.9.0"
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
static Call g_carry[RING];
static volatile uint64_t g_carryN;
static Fn8 original_carry;
DETOUR(CarryDetour, g_carry, g_carryN, original_carry)
static Call g_req[RING];
static volatile uint64_t g_reqN;
static Fn8 original_req;
__attribute__((noinline)) static uintptr_t RequestDetour(uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7, uintptr_t a8)
{
    uint64_t i_ = g_reqN;
    Call* c_ = &g_req[i_ % RING];
    c_->a[0] = a1; c_->a[1] = 0;
    if (a1 >= 0x10000 && a1 < 0x7FFFFFFF0000ull && !(a1 & 3)) { const volatile uint32_t* p_ = (const volatile uint32_t*)a1; c_->a[1] = (uint64_t)p_[0] | ((uint64_t)p_[1] << 32); c_->a[2] = p_[5]; }
    c_->ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    c_->seq = i_ + 1;
    g_reqN = i_ + 1;
    return original_req(a1, a2, a3, a4, a5, a6, a7, a8);
}
static Call g_att[RING * 4];
static volatile uint64_t g_attN;
static Fn8 original_att;
__attribute__((noinline)) static uintptr_t AttachDetour(uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7, uintptr_t a8)
{
    uint64_t i_ = g_attN;
    Call* c_ = &g_att[i_ % (RING * 4)];
    c_->a[0] = a1; c_->a[1] = a2; c_->a[2] = a3; c_->a[3] = a4; c_->a[4] = 0; c_->a[5] = 0;
    if (a5 >= 0x10000 && a5 < 0x7FFFFFFF0000ull && !(a5 & 3)) { const volatile uint32_t* p_ = (const volatile uint32_t*)a5; c_->a[4] = (uint64_t)p_[0] | ((uint64_t)p_[1] << 32); c_->a[5] = p_[2]; }
    c_->ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    c_->seq = i_ + 1;
    g_attN = i_ + 1;
    return original_att(a1, a2, a3, a4, a5, a6, a7, a8);
}

static uintptr_t g_swObj;                   /* her switch-request object, as her last switch by hand passed it */
static uint32_t g_gl, g_oldItem;            /* the GL-21 the probe spawned; the weapon it stands in for */
static uintptr_t InventoryRecord(uint32_t e);
/* her last Wield, as the game made it: the slot and the fifth argument way A repeats */
static int g_haveWield = 1;                 /* 0.2.0: slot 0 and a fifth argument of 0, as every switch seen made it */
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
    static uint64_t seenW, seenS, seenT, seenC, seenA, seenR;
    for (uint64_t now = g_reqN; seenR < now; seenR++)
    {
        if (now - seenR > RING) seenR = now - RING;
        Call* c = &g_req[seenR % RING];
        uint32_t who = (uint32_t)c->a[1], want = (uint32_t)(c->a[1] >> 32);
        if (!who || who != atomic_load(&g_avatar)) continue;
        if (g_swObj != (uintptr_t)c->a[0]) Log("game: your switch-request object is at %p", (void*)(uintptr_t)c->a[0]);
        g_swObj = (uintptr_t)c->a[0];
        static unsigned reqLogged;
        if (reqLogged++ < 120) Log("game: SwitchRequest(you, wanted selection %u; +0x14 was %u) from game+0x%llX", want, (uint32_t)c->a[2], (unsigned long long)(c->ret - game));
    }
    static unsigned attLogged;
    {
        uint32_t mine[6] = {0}, me2 = atomic_load(&g_avatar);
        char nm2[40];
        uintptr_t rec = me2 ? InventoryRecord(me2) : 0;
        if (rec) ReadAt(rec, mine, sizeof mine);
        for (uint64_t now = g_attN; seenA < now; seenA++)
        {
            if (now - seenA > RING * 4) seenA = now - RING * 4;
            Call* c = &g_att[seenA % (RING * 4)];
            uint32_t child = (uint32_t)c->a[1];
            int hers = child && (child == g_gl || child == mine[0] || child == mine[1] || child == mine[2] || child == mine[4] || child == g_oldItem);
            if (!hers || attLogged >= 400) continue;
            attLogged++;
            float off[3];
            uint32_t w[3] = {(uint32_t)c->a[4], (uint32_t)(c->a[4] >> 32), (uint32_t)c->a[5]};
            memcpy(off, w, 12);
            Log("game: AttachToUnit(child %u = %s, link %08x, node %u, offset %.2f %.2f %.2f) from game+0x%llX", child, Name(ResOf(child), nm2, sizeof nm2), (uint32_t)c->a[2],
                (uint32_t)c->a[3], (double)off[0], (double)off[1], (double)off[2], (unsigned long long)(c->ret - game));
        }
    }
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
    for (uint64_t now = g_carryN; seenC < now; seenC++)
    {
        if (now - seenC > RING) seenC = now - RING;
        Call* c = &g_carry[seenC % RING];
        uint32_t item = (uint32_t)c->a[1], carrier = (uint32_t)c->a[2];
        if (carrier != me && ResOf(item) != GL21) continue;
        Log("game: Carry(item %u = %s, carrier %u%s, r9 %u, stack %llx %llx) from game+0x%llX", item, Name(ResOf(item), nm, sizeof nm), carrier, carrier == me ? " = you" : "",
            (uint32_t)c->a[3] & 0xFF, (unsigned long long)(c->a[4] & 0xFF), (unsigned long long)c->a[5], (unsigned long long)(c->ret - game));
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
static double g_madeAt;                     /* when the GL-21 was made: it is taken 0.75 s later */
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
    g_madeAt = g_now;
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
__attribute__((unused)) static void WieldA(void)
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
__attribute__((unused)) static void WieldB(void)
{
    HfFn_SwitchWeapon sw = hf_fn_SwitchWeapon(self);
    HfFn_SetWielder setw = hf_fn_SetWielder(self);
    void* im = hf_global_InventoryManager(self);
    uint32_t me = atomic_load(&g_avatar), was = 0;
    uintptr_t rec;
    if (!Mine("Alt+Insert") || !HaveGl("Alt+Insert")) return;
    if (!sw || !im || !(rec = InventoryRecord(me)) || !ReadAt(rec + 8, &was, 4)) { Log("Alt+Insert: SwitchWeapon or your inventory record is not there: nothing done"); return; }
    if (g_bWritten) { Log("Alt+Insert: the support slot is already the probe's (Ctrl+Delete first)"); return; }
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
__attribute__((unused)) static void GiveBack(void)
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

/* ---- 0.4.0, way D: the game's own pickup into the support slot, and its drop ------------------------------------------ */
typedef void (*TakeFn)(void* inventory, uint32_t entity, uint32_t item, uint32_t slot, uint8_t flag);
typedef void (*DropFn)(void* inventory, uint32_t entity, uint32_t item, uint8_t flag, uint64_t zero, uint8_t one);
static TakeFn g_take;
static DropFn g_drop;
static uint32_t g_slot;                 /* way E: the slot the GL-21 took (g_oldItem: the weapon that was in it) */
static int g_give, g_back;
static double g_giveAt, g_backAt;
typedef void (*RequestFn)(void* object);
static RequestFn g_request;
static int g_seq, g_mode = 1, g_lastRounds = -1;
static int AskSelection(uint32_t selection)
{
    uint32_t me = atomic_load(&g_avatar), who = 0;
    if (!g_request || !g_swObj || !ReadAt(g_swObj, &who, 4) || who != me) { Log("the switch request: her object is not known or not hers any more"); return 0; }
    if (hf->write(g_swObj + 4, &selection, 4)) { Log("the switch request: the write was refused"); return 0; }
    g_request((void*)g_swObj);
    return 1;
}
static double g_seqAt;
static void SeqLog(const char* what) { Log("Insert (try %d): %s", g_mode, what); }
static void WieldD(void)
{
    HfFn_SwitchWeapon sw = hf_fn_SwitchWeapon(self);
    void* im = hf_global_InventoryManager(self);
    uint32_t me = atomic_load(&g_avatar), sel = 0, old = 0;
    uintptr_t rec;
    if (!Mine("Insert") || !HaveGl("Insert")) return;
    if (!g_take || !g_drop || !sw || !im || !(rec = InventoryRecord(me)) || !ReadAt(rec + 0x1C, &sel, 4)) { Log("Insert: the pickup (0x9AE030) is not the reviewed code, or your inventory is not there: nothing done"); return; }
    if (sel < 1 || sel > 3) { Log("Insert: you hold no weapon (selection %u): take your primary, secondary or support weapon in hand first", sel); return; }
    ReadAt(rec + 4u * (sel - 1), &old, 4);
    if (g_mode == 4)
    {
        uint32_t support = 0, who = 0;
        ReadAt(rec + 8, &support, 4);
        if (support) { Log("Insert (try 4): you carry a support weapon (entity %u): drop it first", support); return; }
        if (!g_request || !g_swObj || !ReadAt(g_swObj, &who, 4) || who != me) { Log("Insert (try 4): your switch-request object is not known (switch weapons once by hand first), or 0xA93430 is not the reviewed code"); return; }
        Remember();
        g_slot = 3;
        g_oldItem = 0;
        Log("Insert (try 4): Take(you, %u, slot 3 = support), then the Helldiver's own switch request for selection 3 ...", g_gl);
        g_take(im, me, g_gl, 3, 0);
        g_seq = 20; g_seqAt = g_now + 0.1;
        return;
    }
    if (g_mode == 3)
    {
        uint32_t support = 0;
        ReadAt(rec + 8, &support, 4);
        if (support) { Log("Alt+Insert (try 3): you carry a support weapon (entity %u): drop it first", support); return; }
        Remember();
        g_slot = 3;
        g_oldItem = 0;
        Log("Insert (try 3): Take(you, %u, slot 3 = support), SwitchWeapon(you, 3) ...", g_gl);
        g_take(im, me, g_gl, 3, 0);
        sw(im, me, 3);
        g_seq = 3; g_seqAt = g_now + 0.1;
        return;
    }
    g_slot = sel;
    g_oldItem = old;
    if (g_mode == 1)
    {
        uint32_t other = sel == 2 ? 1 : 2;
        Log("Insert (try 1): the GL-21 %u for your weapon %u in slot %u: SwitchWeapon(you, %u) first (her weapon put away) ...", g_gl, old, sel, other);
        sw(im, me, other);
        g_seq = 1; g_seqAt = g_now + 0.6;
    }
    else
    {
        Log("Insert (try 2): Take(you, %u, slot %u) in place of your weapon %u, SwitchWeapon(you, %u) ...", g_gl, sel, old, sel);
        g_take(im, me, g_gl, sel, 0);
        sw(im, me, sel);
        g_seq = 10; g_seqAt = g_now + 0.6;
    }
}
static void SeqFrame(void)
{
    HfFn_SwitchWeapon sw = hf_fn_SwitchWeapon(self);
    void* im = hf_global_InventoryManager(self);
    uint32_t me = atomic_load(&g_avatar);
    if (!g_seq || g_now < g_seqAt) return;
    if (!sw || !im || !ResOf(me) || !g_gl || ResOf(g_gl) != GL21) { g_seq = 0; Log("Insert: she or the GL-21 is gone: stopped"); return; }
    switch (g_seq)
    {
    case 1: SeqLog("Take(you, GL-21, her slot) while it is put away ..."); g_take(im, me, g_gl, g_slot, 0); g_seq = 2; g_seqAt = g_now + 0.5; break;
    case 2: SeqLog("SwitchWeapon(you, her slot): the game's own draw ..."); sw(im, me, g_slot); g_seq = 3; g_seqAt = g_now + 0.1; break;
    case 10: SeqLog("Drop(you, GL-21) the game's way ..."); g_drop(im, me, g_gl, 0, 0, 1); g_seq = 11; g_seqAt = g_now + 0.7; break;
    case 11: SeqLog("Take(you, GL-21, her slot) again, SwitchWeapon(you, her slot) ..."); g_take(im, me, g_gl, g_slot, 0); sw(im, me, g_slot); g_seq = 3; g_seqAt = g_now + 0.1; break;
    case 20: SeqLog("SwitchRequest(you, 3) ..."); AskSelection(3); g_seq = 3; g_seqAt = g_now + 0.2; break;
    case 30:
    {
        uint32_t now = 0;
        uintptr_t rec = InventoryRecord(me);
        Log("Delete: Drop(you, %u), her support slot emptied ...", g_gl);
        g_drop(im, me, g_gl, 0, 0, 1);
        if (rec && ReadAt(rec + 8, &now, 4) && now == g_gl) { now = 0; hf->write(rec + 8, &now, 4); Log("Delete: the support slot still named it: cleared by the probe"); }
        g_seq = 0; g_slot = 0; g_back = 1; g_backAt = g_now + 0.4;
        break;
    }
    case 3: Fill(); g_lastRounds = -1; g_seq = 0; SeqLog("done. Is the GL-21 in her HANDS now (not on her back)? Fire it; Delete gives your weapon back"); break;
    default: g_seq = 0;
    }
}
static void BackD(void)
{
    HfFn_SwitchWeapon sw = hf_fn_SwitchWeapon(self);
    void* im = hf_global_InventoryManager(self);
    uint32_t me = atomic_load(&g_avatar), now = 0;
    uintptr_t rec;
    if (!Mine("Delete")) return;
    if (g_mode == 4 && g_slot == 3 && g_gl && im && g_drop)
    {
        uint32_t back = g_selWas && g_selWas != 3 ? g_selWas : 1;
        Log("Delete: SwitchRequest(you, %u) (her own weapon, the game's way), then the GL-21 let go ...", back);
        AskSelection(back);
        g_seq = 30; g_seqAt = g_now + 1.0;
        return;
    }
    if (!g_take || !g_drop || !sw || !im || !(rec = InventoryRecord(me)) || g_slot < 1 || g_slot > 3) { Log("Delete: nothing of the probe's to give back (or the pickup is not the reviewed code)"); return; }
    ReadAt(rec + 4u * (g_slot - 1), &now, 4);
    if (!now || now != g_gl) { Log("Delete: slot %u has %u, not the probe's GL-21: left as it is", g_slot, now); return; }
    if (g_oldItem && ResOf(g_oldItem)) { Log("Delete: Take(you, %u, slot %u): your own weapon back in place of the GL-21 ...", g_oldItem, g_slot); g_take(im, me, g_oldItem, g_slot, 0); }
    else { Log("Delete: your weapon %u is gone: Drop(you, %u) ...", g_oldItem, g_gl); g_drop(im, me, g_gl, 0, 0, 1); }
    uint32_t back = g_oldItem ? g_slot : (g_selWas && g_selWas != 3 ? g_selWas : 1);
    Log("Delete: SwitchWeapon(you, %u) ...", back);
    sw(im, me, back);
    Log("Delete: returned. Is your own weapon back in your hands?");
    g_slot = 0;
}

/* ---- 0.2.0: Insert and Delete by themselves do the whole thing ------------------------------------------------------ */
static void Give(int mode)
{
    g_mode = mode;
    if (!Mine("Insert")) return;
    if (g_gl && ResOf(g_gl) == GL21 && atomic_load(&g_held) == g_gl) { Log("Insert: the GL-21 is in your hands already (Delete gives your weapon back)"); return; }
    Log("Insert (try %d): a GL-21 in place of the weapon in your hands, with 40 rounds", mode);
    if (g_seq) { Log("Insert: still busy with the last one"); return; }
    if (!g_gl || ResOf(g_gl) != GL21) Spawn();
    g_give = 1;
    g_giveAt = g_now + 0.3;
}
static void Back(void)
{
    Log("Delete: the GL-21 dropped, your weapon back, the GL-21 removed, your weapon refilled");
    BackD();
    if (g_seq != 30) { g_back = 1; g_backAt = g_now + 0.6; }
}
static void AutoFrame(void)
{
    if (g_give && g_now >= g_giveAt && !g_wantSpawn)
    {
        if (!g_gl || ResOf(g_gl) != GL21) { if (g_now > g_giveAt + 5) { g_give = 0; Log("Insert: no GL-21 came in 5 s: given up"); } }
        else if (g_now >= g_madeAt + 0.75) { g_give = 0; WieldD(); }
    }
    SeqFrame();
    if (g_back && g_now >= g_backAt)
    {
        g_back = 0;
        if (g_gl && ResOf(g_gl) == GL21) Remove();
        Refill();
    }
    /* the rounds she spends from it */
    HfFn_MagazineRounds rounds = hf_fn_MagazineRounds(self);
    void* wm = hf_global_WeaponManager(self);
    if (g_gl && atomic_load(&g_held) == g_gl && rounds && wm && ResOf(g_gl) == GL21)
    {
        int r = rounds(wm, g_gl);
        if (r != g_lastRounds) { if (g_lastRounds >= 0) Log("the GL-21's magazine: %d -> %d", g_lastRounds, r); g_lastRounds = r; }
    }
}

/* ---- 0.9.0: the skill as the kit would run it ------------------------------------------------------------------------ */
static uintptr_t SwitchObject(uint32_t e)
{
    uintptr_t am = (uintptr_t)hf_global_AvatarManager(self), obj;
    uint32_t i, who = 0;
    if (!am || !Lookup(am + 0xF8, e, 1048576, &i) || i >= 1024) return 0;
    obj = am + 0x53DCD0 + (uintptr_t)0x1238 * i;
    return ReadAt(obj, &who, 4) && who == e ? obj : 0;
}
static int Ask(uint32_t selection, const char* why)
{
    uint32_t me = atomic_load(&g_avatar);
    uintptr_t obj = SwitchObject(me);
    if (!obj) { Log("skill: her switch-request object was not found (%s)", why); return 0; }
    if (g_swObj && g_swObj != obj) Log("skill: NOTE the object found (%p) is not the one a switch by hand showed (%p)", (void*)obj, (void*)g_swObj);
    if (!g_request || hf->write(obj + 4, &selection, 4)) { Log("skill: the switch request is off or the write was refused (%s)", why); return 0; }
    Log("skill: SwitchRequest(you, %u) (%s)", selection, why);
    g_request((void*)obj);
    return 1;
}
static uint32_t SpawnRes(uint64_t res, const char* what)
{
    HfFn_RequestEntitySpawn spawn = hf_fn_RequestEntitySpawn(self);
    uint8_t d[24];
    float p[3];
    uint32_t me = atomic_load(&g_avatar), out = 0;
    if (!spawn || !Descriptor(me, d) || !UnitNode(Word(d + 12), 0, p)) { Log("skill: %s could not be asked for", what); return 0; }
    struct { uint32_t one, network; float pose[16]; void* context; uint64_t pad; } info;
    memset(&info, 0, sizeof info);
    memset(g_context, 0, sizeof g_context);
    info.one = 1;
    info.network = 0x7FFF;
    info.pose[0] = info.pose[5] = info.pose[10] = info.pose[15] = 1.0f;
    info.pose[12] = p[0]; info.pose[13] = p[1]; info.pose[14] = p[2] + 1.0f;
    info.context = g_context;
    spawn(&out, res, &info);
    if (!out || out == UINT32_MAX) { Log("skill: %s: no entity came back", what); return 0; }
    Log("skill: %s made: entity %u", what, out);
    return out;
}
static void FillUp(uint32_t weapon, const char* what)
{
    HfFn_MagazineRounds rounds = hf_fn_MagazineRounds(self);
    HfFn_MagazineCapacity capacity = hf_fn_MagazineCapacity(self);
    HfFn_AddMagazineRounds add = hf_fn_AddMagazineRounds(self);
    HfFn_WeaponRefill refill = hf_fn_WeaponRefill(self);
    void* wm = hf_global_WeaponManager(self);
    uint32_t stride = 0;
    uint64_t res = ResOf(weapon);
    if (!weapon || !res) return;
    if (hf->component_record("WeaponMagazineComponentData", res, &stride) && rounds && capacity && add && wm)
    {
        int have = rounds(wm, weapon), cap = capacity(NULL, weapon);
        if (cap > have) add(NULL, weapon, cap - have);
    }
    if (refill) for (int i = 0; i < 6; i++) refill(0, weapon, 1.0f);
    Magazine(what, weapon);
}
enum { K_IDLE, K_WAITGL, K_PUTAWAY, K_TAKE, K_ASK, K_HELD, K_LETGO, K_REMOVE, K_WAITS };
static int g_k;
static double g_kAt;
static uint32_t g_kSel, g_kOld, g_kNew;     /* her selection at the start; the support weapon she had; its replacement */
static uint64_t g_kOldRes;
static void SkillStart(void)
{
    uint32_t me = atomic_load(&g_avatar), slots[8] = {0};
    uintptr_t rec;
    if (!Mine("Insert")) return;
    if (g_k != K_IDLE) { Log("Insert: the skill is %s", g_k == K_HELD ? "on (Delete ends it)" : "busy"); return; }
    if (!g_take || !g_drop || !g_request || !(rec = InventoryRecord(me)) || !ReadAt(rec, slots, sizeof slots) || !SwitchObject(me))
    { Log("Insert: the pickup, the drop or the switch request is not the reviewed code, or her inventory or switch object was not found: nothing done"); return; }
    g_kSel = slots[7] >= 1 && slots[7] <= 3 ? slots[7] : 1;
    g_kOld = slots[2];
    g_kOldRes = ResOf(g_kOld);
    g_kNew = 0;
    char nm[40];
    Log("skill start: selection %u; her support weapon: %u (%s)", g_kSel, g_kOld, g_kOld ? Name(g_kOldRes, nm, sizeof nm) : "none");
    if (!g_gl || ResOf(g_gl) != GL21) Spawn();
    g_k = K_WAITGL;
    g_kAt = g_now + 6;
}
static void SkillEnd(void)
{
    if (g_k != K_HELD) { Log("Delete: the skill is not on"); return; }
    Ask(g_kSel == 3 ? 1 : g_kSel, "her own weapon back in hand");
    g_k = K_LETGO;
    g_kAt = g_now + 1.0;
}
static void SkillFrame(void)
{
    void* im = hf_global_InventoryManager(self);
    void* em = hf_global_EntityManager(self);
    HfFn_RemoveEntity remove = hf_fn_RemoveEntity(self);
    uint32_t me = atomic_load(&g_avatar), now = 0;
    uintptr_t rec;
    if (g_k == K_IDLE || g_k == K_HELD) return;
    if (!im || !ResOf(me) || !(rec = InventoryRecord(me))) { Log("skill: she is gone: stopped"); g_k = K_IDLE; return; }
    switch (g_k)
    {
    case K_WAITGL:
        if (g_wantSpawn) break;
        if (!g_gl || ResOf(g_gl) != GL21) { if (g_now > g_kAt) { g_k = K_IDLE; Log("skill: no GL-21 came: stopped"); } break; }
        if (g_now < g_madeAt + 0.75) break;
        if (g_kSel == 3 && g_kOld) { Ask(1, "her support weapon is in her hands: put away first"); g_k = K_PUTAWAY; g_kAt = g_now + 0.8; }
        else g_k = K_TAKE;
        break;
    case K_PUTAWAY:
        if (g_now >= g_kAt) g_k = K_TAKE;
        break;
    case K_TAKE:
        Log("skill: Take(you, GL-21 %u, slot 3)%s", g_gl, g_kOld ? " (the game drops her support weapon)" : "");
        g_take(im, me, g_gl, 3, 0);
        g_k = K_ASK; g_kAt = g_now + 0.1;
        break;
    case K_ASK:
        if (g_now < g_kAt) break;
        if (g_kOld && ResOf(g_kOld) == g_kOldRes && remove && em) { Log("skill: her dropped support weapon %u removed", g_kOld); remove(em, g_kOld); }
        Ask(3, "the GL-21 in hand");
        Fill();
        g_lastRounds = -1;
        g_k = K_HELD;
        Log("skill on: fire the GL-21. Delete ends it");
        break;
    case K_LETGO:
        if (g_now < g_kAt) break;
        Log("skill: Drop(you, GL-21 %u), her support slot emptied", g_gl);
        g_drop(im, me, g_gl, 0, 0, 1);
        if (ReadAt(rec + 8, &now, 4) && now == g_gl) { now = 0; hf->write(rec + 8, &now, 4); }
        g_k = K_REMOVE; g_kAt = g_now + 0.4;
        break;
    case K_REMOVE:
        if (g_now < g_kAt) break;
        if (g_gl && ResOf(g_gl) == GL21 && remove && em) { remove(em, g_gl); Log("skill: the GL-21 removed"); }
        g_gl = 0;
        if (g_kOldRes) { g_kNew = SpawnRes(g_kOldRes, "a new support weapon of her kind"); g_k = g_kNew ? K_WAITS : K_IDLE; g_kAt = g_now + 0.75; }
        else { FillUp(atomic_load(&g_held), "skill over: her weapon"); g_k = K_IDLE; }
        break;
    case K_WAITS:
        if (g_now < g_kAt) break;
        if (ResOf(g_kNew) != g_kOldRes) { Log("skill: the new support weapon does not read as hers: left"); g_k = K_IDLE; break; }
        Log("skill: Take(you, her new support weapon %u, slot 3)", g_kNew);
        g_take(im, me, g_kNew, 3, 0);
        FillUp(g_kNew, "skill over: her new support weapon");
        if (g_kSel == 3) Ask(3, "she had it in hand when the skill began");
        else FillUp(atomic_load(&g_held), "skill over: her weapon");
        g_k = K_IDLE;
        break;
    default: g_k = K_IDLE;
    }
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
    AutoFrame();
    SkillFrame();
    static int prev[4];
    int focused = Focused(), shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0, ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0,
        alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    int k[4] = {focused && (GetAsyncKeyState(VK_INSERT) & 0x8000), focused && (GetAsyncKeyState(VK_HOME) & 0x8000),
        focused && (GetAsyncKeyState(VK_DELETE) & 0x8000), focused && (GetAsyncKeyState(VK_END) & 0x8000)};
    if (k[0] && !prev[0]) Log("key: Insert pressed%s%s%s", shift ? " +Shift" : "", ctrl ? " +Ctrl" : "", alt ? " +Alt" : "");
    if (k[2] && !prev[2]) Log("key: Delete pressed%s%s", shift ? " +Shift" : "", ctrl ? " +Ctrl" : "");
    if (k[0] && !prev[0]) { if (shift) Spawn(); else if (ctrl) Give(4); else if (alt) Give(3); else SkillStart(); }
    if (k[1] && !prev[1]) { if (ctrl) State("Ctrl+Home"); else Fill(); }
    if (k[2] && !prev[2]) { if (shift) Remove(); else if (ctrl) Back(); else SkillEnd(); }
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
    if (hf->code_matches(game + 0x9AE030, "40 53 55 41 55 48 83 ec 40 48 89 7c 24 68 41 8b e8 4c 89 64 24 70 8b da")) g_take = (TakeFn)(game + 0x9AE030);
    else Log("the pickup (0x9AE030) is not the reviewed code: Insert is off");
    if (hf->code_matches(game + 0x9A8840, "40 53 56 41 56 48 81 ec a0 00 00 00 48 8b 05 bd 37 c9 01 48 33 c4 48 89")) g_drop = (DropFn)(game + 0x9A8840);
    else Log("the drop (0x9A8840) is not the reviewed code: Delete is off");
    if (!hf->code_matches(game + 0x8BF870, "4c 8b dc 55 53 41 56 49 8d 6b a8 48 81 ec 40 01 00 00") ||
        hf->hook(self, game + 0x8BF870, (void*)CarryDetour, (void**)&original_carry)) { original_carry = NULL; Log("the carry function (0x8BF870) is not the reviewed code or its hook was refused: not logged"); }
    if (!hf->code_matches(game + 0xA93430, "48 89 5c 24 18 48 89 6c 24 20 56 57 41 54 41 56 41 57 48 83 ec 50 48 8b 05 c3") ||
        hf->hook(self, game + 0xA93430, (void*)RequestDetour, (void**)&original_req)) { original_req = NULL; Log("the switch request (0xA93430) is not the reviewed code or its hook was refused: Insert is off"); }
    else g_request = (RequestFn)(game + 0xA93430);
    if (hf->hook_function(self, "AttachToUnit", (void*)AttachDetour, (void**)&original_att, 0)) Log("AttachToUnit: hook refused: not logged");
    int w = hf->hook_function(self, "Wield", (void*)WieldDetour, (void**)&original_wield, 0),
        s = hf->hook_function(self, "SwitchWeapon", (void*)SwitchDetour, (void**)&original_switch, 0),
        t = hf->hook_function(self, "SetWielder", (void*)SetWielderDetour, (void**)&original_setw, 0);
    Log("ready: Wield %s, SwitchWeapon %s, SetWielder %s. In a mission, with a weapon in hand: Insert starts the skill (a GL-21 with 40 rounds in her hands; a support weapon she carries is replaced, and a new one of its kind made afterwards); "
        "Delete ends it",
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
