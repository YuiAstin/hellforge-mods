/* kitbase layer 0: her avatar, items and weapons, hook bookkeeping, stamina.
 *
 * Code shared by the character kits. Source of truth: kitbase/ at the repo root. The copy in a kit's src/kitbase/ is
 * written by tools/kitbase_sync.py, so edit kitbase/, never the copy. See docs/kit-base.md.
 * Include after the C headers and the HellForge SDK headers. Everything is static and marked unused, so a kit only
 * pays for what it calls. */
#ifndef KB_PLAYER_H
#define KB_PLAYER_H

#include "kb_core.h"

/* A hit record's size (the damage hooks' third argument) */
#define HIT_SIZE 0x118u
/* Her state, kept up by the kit's frame loop and hooks */
KB_VAR double g_now;
KB_VAR atomic_uint g_avatar;      /* her Helldiver's entity (0: none) */
KB_VAR uint32_t g_avatarNet;      /* its network id */
KB_VAR int g_alive, g_inMission;
KB_VAR uint32_t g_weapon[4];      /* the held weapons by selection (1 primary, 2 secondary, 3 support): entities */
KB_VAR uint64_t g_weaponRes[4];
/* Her own hits. The game's hit code (hooked) names the entity that dealt or owns each hit: one owned or dealt by her
 * Helldiver is hers (the hook records its target). Damage events name network ids instead: her Helldiver's is learned
 * from the first such hit (its spawn carries none), then matched directly. */
KB_VAR atomic_uint g_hookTarget;
KB_VAR _Atomic unsigned long long g_hookMs;
KB_VAR float g_hookPos[3];
KB_VAR unsigned g_hitNotes;
/* Helldivers this machine spawned: entity -> network id (the spawn may come before the player event) */
KB_VAR uint32_t g_spawnEntity[8], g_spawnNet[8];
KB_VAR unsigned g_spawnNext;
/* Her stamina, live (0 empty .. 1 full): the action manager [game.dll+0x3326D20] holds it per Helldiver at +0x547854,
 * 0x78 apart (game.dll+0xA50E2D: movss [rcx+r10+0x547854], xmm1, rcx = index x 0x78; found with Cheat Engine), by the
 * same index as the action records at +0x53E170 (HellForge's resupply: its avatar map at +0xF8). */
KB_VAR int g_staminaOk;
KB_VAR atomic_int g_hooked;
/* Her own items (primary, secondary, support weapon, backpack: the inventory manager game+53634872, HD2Runtime's
 * research: hash +40, descriptors +64, 48-byte records at +80), read twice a second on the game thread: a hit owned by her
 * and dealt by one of them is hers (a laser's hits name the rifle as their dealer), a sentry's or a dog's is not */
KB_VAR atomic_uint g_myItems[4];
KB_VAR double g_myItemsAt = -100;

KB_FN int PlayerPose(float pos[3], float aim[3])
{
    uint8_t id[24];
    uint32_t avatar = atomic_load(&g_avatar);
    if (!Descriptor(avatar, id) || Quad(id) != AVATAR) return 0;
    uint32_t unit = Word(id + 12);
    if (!UnitNode(unit, 0, pos)) return 0;
    if (aim)
    {
        float head[3], ahead[3];
        if (!UnitNode(unit, NODE_HEAD, head) || !UnitNode(unit, NODE_HEAD_AIM, ahead)) return 0;
        float d[3] = {ahead[0] - head[0], ahead[1] - head[1], ahead[2] - head[2]};
        float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (!(n > 0.1f)) return 0;
        for (int i = 0; i < 3; i++) aim[i] = d[i] / n;
    }
    return 1;
}
KB_FN void LearnNet(void)
{
    uint32_t me = atomic_load(&g_avatar);
    for (int i = 0; i < 8; i++)
        if (g_spawnEntity[i] && g_spawnEntity[i] == me && NetOk(g_spawnNet[i]) && g_spawnNet[i] != g_avatarNet)
        {
            g_avatarNet = g_spawnNet[i];
            Log("her Helldiver: entity %u, network %u", me, g_avatarNet);
        }
}
KB_FN void MyItems(void)
{
    if (g_now - g_myItemsAt < 0.5) return;
    g_myItemsAt = g_now;
    uint32_t me = atomic_load(&g_avatar), i;
    uintptr_t m = Pointer(game + 53634872u), arr, d, recs;
    uint8_t desc[24], rec[48];
    if (!me || !m || !Lookup(m + 40, me, 1048576, &i) || i >= 0x10000 || !(arr = Pointer(m + 64)) || !(d = Pointer(arr + 8u * i)) ||
        !ReadAt(d, desc, 24) || Word(desc + 8) != me || !(recs = Pointer(m + 80)) || !ReadAt(recs + 48u * i, rec, 48))
        return;
    for (int k = 0; k < 4; k++) atomic_store(&g_myItems[k], Word(rec + 4u * k));
}
KB_FN int MyItem(uint32_t e)
{
    for (int k = 0; k < 4; k++) if (e && atomic_load(&g_myItems[k]) == e) return 1;
    return 0;
}
/* A hit owned by her and dealt by `dealer`: hers when the game says she holds the dealer (first), else when it is one of her
 * own items (her inventory: a backpack's or support weapon's gear the wielder record does not name) */
KB_FN int Mine(uint32_t dealer)
{
    uint32_t me = atomic_load(&g_avatar), by = 0;
    int held = WielderOf(dealer, &by), mine = held && by == me ? 1 : held && by != dealer && MyItem(by) ? 1 : !held && MyItem(dealer);
    static atomic_uint told;
    if (atomic_fetch_add(&told, 1) < 8)
        Log("a hit dealt by entity %u: %s%s", dealer, held ? (by == dealer ? "it holds itself" : by == me ? "held by her" : "held by another") : "no holder known",
            mine ? ": hers" : ": not hers");
    return mine;
}
KB_FN int MineHit(const HfDamageEvent* d)
{
    int match = NetOk(g_avatarNet) && (d->dealer == g_avatarNet || (d->owner == g_avatarNet && !NetOk(d->dealer)));   /* not a sentry's or a dog's: theirs name their own dealer */
    int hooked = d->target && d->target == atomic_load(&g_hookTarget) && GetTickCount64() - atomic_load(&g_hookMs) <= 250;
    if (hooked && !NetOk(g_avatarNet) && NetOk(d->owner))
    {
        g_avatarNet = d->owner;
        Log("her hits: owned by network %u (dealer %u), learned from the game's hit code", d->owner, d->dealer);
    }
    if (d->creditor && g_hitNotes < 6 && (d->kind == 0 || d->kind == 1 || d->kind == 3))
    {
        g_hitNotes++;
        Log("credited hit: kind %u, dealer %u, owner %u (hers: network %u%s): %s", d->kind, d->dealer, d->owner, g_avatarNet,
            hooked ? ", the hook saw it" : "", match || hooked ? "hers" : "not hers");
    }
    return match || hooked;
}
/* Every hit she deals: its target and where it struck (hit +0x20 target, +0x34 point, +0x80 dealer, +0x100 owner) */
KB_FN void Observe(const void* hit)
{
    uint32_t me = atomic_load(&g_avatar);
    if (!atomic_load(&g_hooked) || !hit || !me) return;
    uint8_t h[HIT_SIZE];
    if (!ReadAt((uintptr_t)hit, h, sizeof h)) return;
    uint32_t target = Word(h + 0x20), dealer = Word(h + 0x80), owner = Word(h + 0x100);
    if (target == me || !target || !(dealer == me || (owner == me && (!dealer || Mine(dealer))))) return;   /* dealt by her or her weapon: a sentry's or a dog's hits are dealt by it */
    memcpy(g_hookPos, h + 0x34, sizeof g_hookPos);
    atomic_store(&g_hookTarget, target);
    atomic_store(&g_hookMs, GetTickCount64());
}
KB_FN float StaminaNow(void)
{
    uint32_t me = atomic_load(&g_avatar), ai, named;
    uintptr_t actions = Pointer(game + 0x3326D20), d;
    if (!g_staminaOk || !actions || !HashFind(actions + 0xF8, me, &ai) || ai >= 64 || !(d = Pointer(actions + 0x110 + 8u * ai)) ||
        !ReadAt(d + 8, &named, 4) || named != me) return -1;
    float v;
    if (!ReadAt(actions + 0x547854 + (uintptr_t)ai * 0x78, &v, 4) || !isfinite(v) || v < -0.01f || v > 1.01f) return -1;
    return v;
}
KB_FN void OnWeapon(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfWeaponEvent* w = (const HfWeaponEvent*)info;
    if (w->avatar && w->avatar != atomic_load(&g_avatar)) { atomic_store(&g_avatar, w->avatar); g_avatarNet = 0; g_alive = 1; LearnNet(); }
    if (w->selection >= 1 && w->selection <= 3 && w->entity) { g_weapon[w->selection] = w->entity; g_weaponRes[w->selection] = w->resource; }
}

#endif
