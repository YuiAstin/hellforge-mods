/* kitbase wield: a weapon or backpack of the kit's own into her hands or onto her back, and her own given back.
 *
 * Code shared by the character kits. Source of truth: kitbase/ at the repo root. The copy in a kit's src/kitbase/ is
 * written by tools/kitbase_sync.py, so edit kitbase/, never the copy. See docs/kit-base.md.
 * Include after the C headers and the HellForge SDK headers (hellforge_functions.h and hellforge_globals.h too).
 * Everything is static and marked unused, so a kit only pays for what it calls.
 *
 * From W's King of Hearts (the Wield Probe, played 2026-11-25) and Hina's gun (2026-11-27): an entity spawned at her
 * feet (RequestEntitySpawn, its LoadoutPackage loaded first), 0.75 s for its spawn to finish, the inventory's own take
 * into a slot (game.dll+0x9AE030: inventory, Helldiver, item, slot, 0; it drops what the slot held), and her own switch
 * request (0xA93430, on the Avatar manager's +0x53DCD0 + 0x1238 x her index: her entity at +0, the wanted selection at
 * +4), which stows what she holds and draws the new one. Back: the request for her earlier selection, the inventory's
 * drop (0x9A8840; it leaves the slot's entry: cleared by hand) and RemoveEntity. The skill's own steps and timing
 * (W's launcher, Hina's gun and backpack) stay in the kit.
 *
 * The kit calls:
 *   WieldPins()              once the game is loaded (game set): pins the three functions; 0 when they are not the
 *                            reviewed code (the swap is off)
 *   InventoryRecord(e)       her inventory record (48 bytes): +0x00 primary, +0x04 secondary, +0x08 support, +0x0C
 *                            backpack, +0x10 held item (entities), +0x1C her selection (1 primary, 2 secondary, 3 support)
 *   WieldTake(e, item, slot) the inventory's take: KB_SLOT_PRIMARY .. KB_SLOT_BACKPACK
 *   WieldDrop(e, item, off)  the inventory's drop, and the record's entry at +off cleared if it still names the item
 *   WieldAsk(sel, why)       her switch request for a selection (1-3)
 *   SpawnNear(res, what)     an entity spawned at her feet (0: none)
 *   LoadoutPackage(res, *p)  an entity's LoadoutPackage (loaded with load_package; 0 none)
 *   MagRounds(w), MagCapacity(w), MagAdd(w, n), FillUp(w)   a weapon's magazine (-1: none); FillUp: magazine and spares
 *   DepositAmount(bp), DepositSet(bp, n)                    a backpack's ammunition now (-1: none) */
#ifndef KB_WIELD_H
#define KB_WIELD_H

#include "kb_player.h"

#define KB_SLOT_PRIMARY 1u
#define KB_SLOT_SECONDARY 2u
#define KB_SLOT_SUPPORT 3u
#define KB_SLOT_HELD 4u       /* 4 and 5: the held item */
#define KB_SLOT_BACKPACK 6u   /* (0x9AE030's jump table: 6 reads the record's +0x0C) */

typedef void (*KbTakeFn)(void* inventory, uint32_t entity, uint32_t item, uint32_t slot, uint8_t flag);
typedef void (*KbDropFn)(void* inventory, uint32_t entity, uint32_t item, uint8_t flag, uint64_t zero, uint8_t one);
typedef void (*KbRequestFn)(void* object);
KB_VAR KbTakeFn g_wieldTake;
KB_VAR KbDropFn g_wieldDrop;
KB_VAR KbRequestFn g_wieldRequest;
KB_VAR uint8_t g_wieldContext[0x8A0];

KB_FN int WieldPin(void** slot, uintptr_t at, const char* pattern, const char* what)
{
    if (hf->code_matches(at, pattern)) { *slot = (void*)at; return 1; }
    Log("%s is not the reviewed code: the weapon swap is off", what);
    return 0;
}
KB_FN int WieldPins(void)
{
    int ok = WieldPin((void**)&g_wieldTake, game + 0x9AE030, "40 53 55 41 55 48 83 ec 40 48 89 7c 24 68 41 8b e8 4c 89 64 24 70 8b da", "the inventory's pickup (0x9AE030)");
    ok &= WieldPin((void**)&g_wieldDrop, game + 0x9A8840, "40 53 56 41 56 48 81 ec a0 00 00 00 48 8b 05 bd 37 c9 01 48 33 c4 48 89", "the inventory's drop (0x9A8840)");
    ok &= WieldPin((void**)&g_wieldRequest, game + 0xA93430, "48 89 5c 24 18 48 89 6c 24 20 56 57 41 54 41 56 41 57 48 83 ec 50 48 8b 05 c3", "the Helldiver's switch request (0xA93430)");
    return ok;
}
KB_FN int WieldReady(void) { return g_wieldTake && g_wieldDrop && g_wieldRequest; }

KB_FN uintptr_t InventoryRecord(uint32_t e)
{
    uintptr_t m = (uintptr_t)hf_global_InventoryManager(self), records;
    uint32_t i;
    if (!m || !Lookup(m + 0x28, e, 1048576, &i) || i >= 65536 || !(records = Pointer(m + 0x50))) return 0;
    return records + 0x30ull * i;
}
KB_FN uint64_t ResOf(uint32_t e) { uint8_t d[24]; return e && Descriptor(e, d) ? Quad(d) : 0; }
KB_FN uintptr_t SwitchObject(uint32_t e)
{
    uintptr_t am = (uintptr_t)hf_global_AvatarManager(self), obj;
    uint32_t i, who = 0;
    if (!am || !Lookup(am + 0xF8, e, 1048576, &i) || i >= 1024) return 0;
    obj = am + 0x53DCD0 + (uintptr_t)0x1238 * i;
    return ReadAt(obj, &who, 4) && who == e ? obj : 0;
}
KB_FN int WieldAsk(uint32_t selection, const char* why)
{
    uintptr_t obj = SwitchObject(atomic_load(&g_avatar));
    if (!obj || !g_wieldRequest || hf->write(obj + 4, &selection, 4)) { Log("the switch request could not be made (%s)", why); return 0; }
    g_wieldRequest((void*)obj);
    return 1;
}
KB_FN void WieldTake(uint32_t me, uint32_t item, uint32_t slot)
{
    void* im = hf_global_InventoryManager(self);
    if (im && g_wieldTake && item) g_wieldTake(im, me, item, slot, 0);
}
KB_FN void WieldDrop(uint32_t me, uint32_t item, uint32_t entryOffset)
{
    void* im = hf_global_InventoryManager(self);
    uintptr_t rec = InventoryRecord(me);
    uint32_t now = 0;
    if (!im || !g_wieldDrop || !item) return;
    if (rec && ReadAt(rec + entryOffset, &now, 4) && now == item) g_wieldDrop(im, me, item, 0, 0, 1);
    if (rec && ReadAt(rec + entryOffset, &now, 4) && now == item) { now = 0; hf->write(rec + entryOffset, &now, 4); }
}
KB_FN uint32_t SpawnNear(uint64_t res, const char* what)
{
    HfFn_RequestEntitySpawn spawn = hf_fn_RequestEntitySpawn(self);
    float p[3];
    uint32_t out = 0;
    if (!spawn || !res || !PlayerPose(p, NULL)) { Log("%s could not be asked for", what); return 0; }
    struct { uint32_t one, network; float pose[16]; void* context; uint64_t pad; } info;
    _Static_assert(sizeof info == 0x58, "the spawn record is 0x58 bytes");
    memset(&info, 0, sizeof info);
    memset(g_wieldContext, 0, sizeof g_wieldContext);
    info.one = 1;
    info.network = 0x7FFF;
    info.pose[0] = info.pose[5] = info.pose[10] = info.pose[15] = 1.0f;
    info.pose[12] = p[0]; info.pose[13] = p[1]; info.pose[14] = p[2] + 1.0f;
    info.context = g_wieldContext;
    spawn(&out, res, &info);
    if (!out || out == UINT32_MAX) { Log("%s: no entity came back", what); return 0; }
    return out;
}
/* an entity's LoadoutPackage (its record's +8); 1 with *pkg set */
KB_FN int LoadoutPackage(uint64_t res, uint64_t* pkg)
{
    uint32_t stride = 0;
    uintptr_t r = hf->component_record("LoadoutPackageComponentData", res, &stride);
    *pkg = 0;
    return r && ReadAt(r + 8, pkg, 8) && *pkg && *pkg != ~0ull;
}

/* ---- magazines and backpacks ------------------------------------------------------------------------------------ */
KB_FN int MagRounds(uint32_t weapon)
{
    HfFn_MagazineRounds rounds = hf_fn_MagazineRounds(self);
    void* wm = hf_global_WeaponManager(self);
    return rounds && wm && weapon ? rounds(wm, weapon) : -1;
}
/* the live capacity (attachments included); -1 when it has none. 0x743410 is a lookup in the magazine map, 0 for a
 * weapon not in it (read from the dump): no record check needed (component_record refuses a vanilla weapon's shared
 * record, so the check made every arsenal weapon read -1; Hina, played) */
KB_FN int MagCapacity(uint32_t weapon)
{
    HfFn_MagazineCapacity capacity = hf_fn_MagazineCapacity(self);
    int32_t c = capacity && weapon && ResOf(weapon) ? capacity(NULL, weapon) : 0;
    return c > 0 ? c : -1;
}
/* rounds added past the capacity hold (W: 40 in a 10-round GL-21); the count is 32-bit (0x76E080), only the copy sent to
 * other players is 11-bit (2047) */
KB_FN void MagAdd(uint32_t weapon, int32_t n)
{
    HfFn_AddMagazineRounds add = hf_fn_AddMagazineRounds(self);
    if (add && weapon && n > 0) add(NULL, weapon, n);
}
/* a weapon's magazine to its capacity and its spares in full (a resupply's refill, six times) */
KB_FN void FillUp(uint32_t weapon)
{
    HfFn_WeaponRefill refill = hf_fn_WeaponRefill(self);
    if (!weapon || !ResOf(weapon)) return;
    int have = MagRounds(weapon), cap = MagCapacity(weapon);
    if (have >= 0 && cap > have) MagAdd(weapon, cap - have);
    if (refill) for (int i = 0; i < 6; i++) refill(0, weapon, 1.0f);
}
/* a backpack's ammunition now: the deposit manager [game+0x3326BE8], its map at +0x20, live amounts at +0x50, 8 bytes
 * each (HD2Runtime's player-equipment research) */
KB_FN uintptr_t DepositLive(uint32_t bp)
{
    uintptr_t m = Pointer(game + 53634536u), live;
    uint32_t i;
    if (!bp || !m || !Lookup(m + 32, bp, 1048576, &i) || i >= 65536 || !(live = Pointer(m + 80))) return 0;
    return live + 8ull * i;
}
KB_FN int DepositAmount(uint32_t bp) { uintptr_t a = DepositLive(bp); int32_t v; return a && ReadAt(a, &v, 4) ? v : -1; }
KB_FN int DepositSet(uint32_t bp, int32_t amount) { uintptr_t a = DepositLive(bp); return a && !hf->write(a, &amount, 4); }

#endif
