/* kitbase layer 3: a weapon of the kit's into her hands for a while, and hers back afterwards (W's King of Hearts).
 *
 * Code shared by the character kits. Source of truth: kitbase/ at the repo root. The copy in a kit's src/kitbase/ is
 * written by tools/kitbase_sync.py, so edit kitbase/, never the copy. See docs/kit-base.md.
 * Lifted 2026-11-26 from W 0.9.0 (the Wield Probe's findings, probes/README.md). One kit so far.
 *
 * How it goes (KbSwapFrame steps it each frame):
 *   1. the weapon's package is asked for, then the weapon spawned at her side (kb_spawn.h);
 *   2. after 0.75 s (taken in its spawn frame it stayed where it was made), with her support weapon in hand she is first
 *      asked to switch to her primary;
 *   3. the inventory takes it into the support slot (game.dll+0x9AE030; the game drops the support weapon she had,
 *      which is taken away); the Helldiver's switch request (+0xA93430 on her switch object, AvatarManager
 *      +0x53DCD0 + 0x1238 x her index, the wanted selection at +4) draws it; its magazine is filled to kb_swap_rounds
 *      (AddMagazineRounds holds past the capacity);
 *   4. held until its rounds are spent, KbSwapEnd, or she puts it away for 1.5 s;
 *   5. her earlier selection is asked for, the weapon dropped (+0x9A8840) and taken away, and a new support weapon of
 *      the kind she had is spawned and taken (the kit only needs which weapon it was, not that one weapon); her weapon
 *      in hand is refilled.
 * Doesn't work (kept from the probe): Wield or SwitchWeapon directly (no draw; she switched back), the held-item slot
 * (+0x9AB950 crashed at 0xA99016), +0x9ABC30 (a backpack's equip).
 *
 * A kit calls KbSwapInstall() in its GAME_LOADED handler, KbSwapStart(res) on its key, KbSwapFrame() every frame and
 * KbSwapAbort(why) when she dies or the mission ends. Include after kb_spawn.h. */
#ifndef KB_SWAP_H
#define KB_SWAP_H

#include "kb_core.h"
#include "kb_player.h"
#include "kb_spawn.h"

typedef void (*KbTakeFn)(void* inventory, uint32_t entity, uint32_t item, uint32_t slot, uint8_t flag);
typedef void (*KbDropFn)(void* inventory, uint32_t entity, uint32_t item, uint8_t flag, uint64_t zero, uint8_t one);
typedef void (*KbRequestFn)(void* object);
KB_VAR KbTakeFn g_take;
KB_VAR KbDropFn g_drop;
KB_VAR KbRequestFn g_request;

/* ---- her inventory --------------------------------------------------------------------------------------------- */
/* Her inventory record (InventoryManager: map +0x28, records +0x50, 0x30 each): slots +0 primary, +4 secondary,
 * +8 support, +0x10 held item, +0x14 backpack, +0x1C her selection (1 primary, 2 secondary, 3 support) */
KB_FN uintptr_t InventoryRecord(uint32_t e)
{
    uintptr_t m = (uintptr_t)hf_global_InventoryManager(self), records;
    uint32_t i;
    if (!m || !Lookup(m + 0x28, e, 1048576, &i) || i >= 65536 || !(records = Pointer(m + 0x50))) return 0;
    return records + 0x30ull * i;
}
KB_FN uintptr_t SwitchObject(uint32_t e)
{
    uintptr_t am = (uintptr_t)hf_global_AvatarManager(self), obj;
    uint32_t i, who = 0;
    if (!am || !Lookup(am + 0xF8, e, 1048576, &i) || i >= 1024) return 0;
    obj = am + 0x53DCD0 + (uintptr_t)0x1238 * i;
    return ReadAt(obj, &who, 4) && who == e ? obj : 0;
}
/* Asks her Helldiver to switch to a selection, as the player's own switch does */
KB_FN int KbAskSelection(uint32_t selection, const char* why)
{
    uintptr_t obj = SwitchObject(atomic_load(&g_avatar));
    if (!obj || !g_request || hf->write(obj + 4, &selection, 4)) { Log("the switch request could not be made (%s)", why); return 0; }
    g_request((void*)obj);
    return 1;
}
/* A weapon's rounds in its magazine (-1 unknown), and filling it up with its spare magazines too */
KB_FN int Rounds(uint32_t weapon)
{
    HfFn_MagazineRounds rounds = hf_fn_MagazineRounds(self);
    void* wm = hf_global_WeaponManager(self);
    return rounds && wm && weapon ? rounds(wm, weapon) : -1;
}
KB_FN void FillUp(uint32_t weapon)
{
    HfFn_MagazineCapacity capacity = hf_fn_MagazineCapacity(self);
    HfFn_AddMagazineRounds add = hf_fn_AddMagazineRounds(self);
    HfFn_WeaponRefill refill = hf_fn_WeaponRefill(self);
    uint32_t stride = 0;
    uint8_t d[24];
    if (!weapon || !Descriptor(weapon, d)) return;
    if (hf->component_record("WeaponMagazineComponentData", Quad(d), &stride) && capacity && add)
    {
        int have = Rounds(weapon), cap = capacity(NULL, weapon);
        if (have >= 0 && cap > have) add(NULL, weapon, cap - have);
    }
    if (refill) for (int i = 0; i < 6; i++) refill(0, weapon, 1.0f);
}

/* ---- the swap -------------------------------------------------------------------------------------------------- */
enum { KS_IDLE, KS_PACKAGE, KS_WAIT, KS_PUTAWAY, KS_TAKE, KS_ASK, KS_HELD, KS_LETGO, KS_REMOVE, KS_WAITNEW };
KB_VAR int kb_swap;                          /* where it is (KS_...): KS_HELD while the weapon is in her hands */
KB_VAR atomic_uint kb_swap_weapon;           /* the weapon it made (0: none) */
KB_VAR int kb_swap_rounds = 40;
KB_VAR const char* kb_swap_name = "the swap";
KB_VAR void (*kb_swap_over)(const char* why); /* it ended (spent, put away, KbSwapEnd): her weapon is coming back */
KB_VAR uint64_t g_swapRes, g_swapOldRes;
KB_VAR uint32_t g_swapSel, g_swapOld, g_swapNew;
KB_VAR double g_swapAt, g_swapMade, g_swapAway;

KB_FN void KbSwapInstall(void)
{
    if (hf->code_matches(game + 0x9AE030, "40 53 55 41 55 48 83 ec 40 48 89 7c 24 68 41 8b e8 4c 89 64 24 70 8b da")) g_take = (KbTakeFn)(game + 0x9AE030);
    if (hf->code_matches(game + 0x9A8840, "40 53 56 41 56 48 81 ec a0 00 00 00 48 8b 05 bd 37 c9 01 48 33 c4 48 89")) g_drop = (KbDropFn)(game + 0x9A8840);
    if (hf->code_matches(game + 0xA93430, "48 89 5c 24 18 48 89 6c 24 20 56 57 41 54 41 56 41 57 48 83 ec 50 48 8b 05 c3")) g_request = (KbRequestFn)(game + 0xA93430);
    if (!g_take || !g_drop || !g_request)
        Log("%s is off: the inventory's pickup %s, its drop %s, the switch request %s", kb_swap_name, g_take ? "ok" : "not reviewed", g_drop ? "ok" : "not reviewed",
            g_request ? "ok" : "not reviewed");
}
KB_FN int KbSwapStart(uint64_t res)
{
    uint32_t me = atomic_load(&g_avatar), slots[8] = {0};
    uintptr_t rec;
    uint8_t d[24];
    if (kb_swap != KS_IDLE) return 0;
    if (!g_take || !g_drop || !g_request) { Log("%s: off (the game's code is not the reviewed one)", kb_swap_name); return 0; }
    if (!(rec = InventoryRecord(me)) || !ReadAt(rec, slots, sizeof slots) || !SwitchObject(me)) { Log("%s: her inventory or her switch object was not found", kb_swap_name); return 0; }
    g_swapRes = res;
    g_swapSel = slots[7] >= 1 && slots[7] <= 3 ? slots[7] : 1;
    g_swapOld = slots[2];
    g_swapOldRes = g_swapOld && Descriptor(g_swapOld, d) ? Quad(d) : 0;
    g_swapNew = 0;
    Log("%s: her selection %u, her support weapon %u (%016llx)", kb_swap_name, g_swapSel, g_swapOld, (unsigned long long)g_swapOldRes);
    kb_swap = KS_PACKAGE;
    g_swapAt = g_now + 8;
    return 1;
}
/* Ends it while held: her earlier weapon is asked for, the rest follows in KbSwapFrame */
KB_FN void KbSwapEnd(const char* why)
{
    if (kb_swap != KS_HELD) return;
    Log("%s over (%s): %d rounds left", kb_swap_name, why, Rounds(atomic_load(&kb_swap_weapon)));
    KbAskSelection(g_swapSel == 3 ? 1 : g_swapSel, "her own weapon back in hand");
    kb_swap = KS_LETGO;
    g_swapAt = g_now + 1.0;
    if (kb_swap_over) kb_swap_over(why);
}
/* She died or the mission ended with it running: nothing of hers is left to put back */
KB_FN void KbSwapAbort(const char* why)
{
    if (kb_swap == KS_IDLE) return;
    Log("%s cut short (%s)", kb_swap_name, why);
    KbRemoveEntity(atomic_load(&kb_swap_weapon), g_swapRes);
    atomic_store(&kb_swap_weapon, 0);
    kb_swap = KS_IDLE;
    if (kb_swap_over) kb_swap_over(why);
}
KB_FN void KbSwapFrame(void)
{
    void* im = hf_global_InventoryManager(self);
    uint32_t me = atomic_load(&g_avatar), w = atomic_load(&kb_swap_weapon), now = 0;
    uintptr_t rec;
    uint8_t d[24];
    if (kb_swap == KS_IDLE) return;
    if (!g_alive || !im || !Descriptor(me, d) || !(rec = InventoryRecord(me))) { KbSwapAbort("she is down or gone"); return; }
    int isIt = w && Descriptor(w, d) && Quad(d) == g_swapRes;
    switch (kb_swap)
    {
    case KS_PACKAGE:
    {
        int s = KbLoadFor(g_swapRes);
        if (s == HF_PACKAGE_LOADING) { if (g_now > g_swapAt) KbSwapAbort("the weapon's package did not load"); break; }
        w = KbSpawnNear(g_swapRes, kb_swap_name);
        if (!w) { KbSwapAbort("no weapon came"); break; }
        atomic_store(&kb_swap_weapon, w);
        g_swapMade = g_now;
        kb_swap = KS_WAIT;
        break;
    }
    case KS_WAIT:
        if (g_now < g_swapMade + 0.75) break;
        if (!isIt) { KbSwapAbort("the weapon does not read as itself"); break; }
        if (g_swapSel == 3 && g_swapOld) { KbAskSelection(1, "her support weapon is in her hands: put away first"); kb_swap = KS_PUTAWAY; g_swapAt = g_now + 0.8; }
        else kb_swap = KS_TAKE;
        break;
    case KS_PUTAWAY:
        if (g_now >= g_swapAt) kb_swap = KS_TAKE;
        break;
    case KS_TAKE:
        g_take(im, me, w, 3, 0);
        kb_swap = KS_ASK; g_swapAt = g_now + 0.1;
        break;
    case KS_ASK:
    {
        HfFn_AddMagazineRounds add = hf_fn_AddMagazineRounds(self);
        if (g_now < g_swapAt) break;
        KbRemoveEntity(g_swapOld, g_swapOldRes);   /* the game dropped it as the weapon was taken */
        KbAskSelection(3, "the weapon in hand");
        int have = Rounds(w);
        if (add && have >= 0 && have < kb_swap_rounds) add(NULL, w, kb_swap_rounds - have);
        kb_swap = KS_HELD;
        g_swapAway = 0;
        Log("%s on: weapon %u, %d rounds", kb_swap_name, w, Rounds(w));
        break;
    }
    case KS_HELD:
    {
        uint32_t sel = 0;
        if (!isIt) { KbSwapAbort("the weapon is gone"); break; }
        if (Rounds(w) == 0) { KbSwapEnd("the rounds are spent"); break; }
        /* she put it away herself: a second and a half of grace for the draw at the start */
        ReadAt(rec + 0x1C, &sel, 4);
        if (sel == 3) g_swapAway = 0;
        else if (!g_swapAway) g_swapAway = g_now;
        else if (g_now - g_swapAway > 1.5) { g_swapSel = sel >= 1 && sel <= 2 ? sel : g_swapSel; KbSwapEnd("she put it away"); }
        break;
    }
    case KS_LETGO:
        if (g_now < g_swapAt) break;
        g_drop(im, me, w, 0, 0, 1);
        if (ReadAt(rec + 8, &now, 4) && now == w) { now = 0; hf->write(rec + 8, &now, 4); }
        kb_swap = KS_REMOVE; g_swapAt = g_now + 0.4;
        break;
    case KS_REMOVE:
        if (g_now < g_swapAt) break;
        KbRemoveEntity(w, g_swapRes);
        atomic_store(&kb_swap_weapon, 0);
        if (g_swapOldRes) { g_swapNew = KbSpawnNear(g_swapOldRes, "a new support weapon of her kind"); kb_swap = g_swapNew ? KS_WAITNEW : KS_IDLE; g_swapAt = g_now + 0.75; }
        else { FillUp(g_weapon[g_swapSel]); kb_swap = KS_IDLE; }
        break;
    case KS_WAITNEW:
        if (g_now < g_swapAt) break;
        if (Descriptor(g_swapNew, d) && Quad(d) == g_swapOldRes)
        {
            g_take(im, me, g_swapNew, 3, 0);
            FillUp(g_swapNew);
            if (g_swapSel == 3) KbAskSelection(3, "she had it in hand when it began");
            else FillUp(g_weapon[g_swapSel]);
        }
        else Log("%s: the new support weapon does not read as hers: left", kb_swap_name);
        kb_swap = KS_IDLE;
        break;
    default: kb_swap = KS_IDLE;
    }
}

#endif
