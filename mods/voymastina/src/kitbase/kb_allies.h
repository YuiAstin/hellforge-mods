/* kitbase allies: shield pools on her allies (a support kit's barrier), for any kit that protects others.
 *
 * Code shared by the character kits. Source of truth: kitbase/ at the repo root. The copy in a kit's src/kitbase/ is
 * written by tools/kitbase_sync.py, so edit kitbase/, never the copy. See docs/kit-base.md.
 * Include after the C headers and the HellForge SDK headers. Everything is static and marked unused, so a kit only
 * pays for what it calls.
 *
 * Allies: every Helldiver (hers and the other players', the Helldiver resource) and every friend on the kit base's
 * friend list (kb_friends.h: sentries, emplacements, guard dogs, exosuits, SEAF, the Pelican, the FRV and mods' copies
 * of them), seen spawning. Not what a Helldiver holds or wears. A killed ally leaves the list.
 *
 * The kit calls:
 *   AllySpawn(entity, resource)       from its spawn event (any resource: the non-allies are ignored)
 *   AllyKill(entity)                  from its kill event
 *   AllyGrant(center, range, amount)  on the game thread: +amount on every ally within range m (no cap); how many
 *   AllyAbsorb(e, d)                  from its damage event, for every hit: a shielded ally's pool takes the hit first
 *                                     (the whole hit: cancelled; part: the rest goes through); 1 when it took any
 *   AllyClear()                       at the mission's end
 * Pools are whole damage points, before the ally's armour (the damage event's amount). */
#ifndef KB_ALLIES_H
#define KB_ALLIES_H

#include "kb_friends.h"

#ifndef KB_ALLIES
#define KB_ALLIES 512
#endif
typedef struct KbAlly { uint32_t e; uint64_t res; double pool, taken; double lastHit; } KbAlly;
KB_VAR KbAlly g_ally[KB_ALLIES];
KB_VAR unsigned g_allyHits, g_allyBroke;
KB_VAR double g_allyAbsorbed;
/* the kit's word when a pool runs dry (optional) */
KB_VAR void (*kb_ally_broke)(uint32_t e);

KB_FN KbAlly* AllyOf(uint32_t e)
{
    if (!e) return NULL;
    for (int i = 0; i < KB_ALLIES; i++) if (g_ally[i].e == e) return &g_ally[i];
    return NULL;
}
KB_FN int AllyRes(uint64_t res) { return res == AVATAR || FriendRes(res); }
KB_FN void AllySpawn(uint32_t e, uint64_t res)
{
    uint32_t holder = 0;
    if (!e || !AllyRes(res)) return;
    if (res != AVATAR && WielderOf(e, &holder) && holder && holder != e) return;   /* a Helldiver's gun or backpack */
    KbAlly* a = AllyOf(e);
    if (a) { a->res = res; return; }
    for (int i = 0; i < KB_ALLIES; i++)
        if (!g_ally[i].e) { memset(&g_ally[i], 0, sizeof g_ally[i]); g_ally[i].res = res; g_ally[i].e = e; return; }
    static unsigned told;
    if (told++ < 3) Log("allies: the list is full (%d): entity %u left out", KB_ALLIES, e);
}
KB_FN void AllyKill(uint32_t e)
{
    KbAlly* a = AllyOf(e);
    if (a) memset(a, 0, sizeof *a);
}
KB_FN void AllyClear(void) { memset(g_ally, 0, sizeof g_ally); g_allyHits = g_allyBroke = 0; g_allyAbsorbed = 0; }
/* an ally's position: its unit's root (z + 1: its chest), and it still is what it was */
KB_FN int AllyPos(const KbAlly* a, float out[3])
{
    uint8_t d[24];
    if (!a->e || !Descriptor(a->e, d) || Quad(d) != a->res || !UnitNode(Word(d + 12), 0, out)) return 0;
    out[2] += 1.0f;
    return 1;
}
KB_FN int AllyGrant(const float center[3], float range, double amount)
{
    int n = 0;
    float p[3];
    for (int i = 0; i < KB_ALLIES; i++)
    {
        KbAlly* a = &g_ally[i];
        if (!a->e) continue;
        if (!AllyPos(a, p))
        {
            uint8_t d[24];
            if (!Descriptor(a->e, d)) memset(a, 0, sizeof *a);   /* gone without a kill (despawned) */
            continue;
        }
        if (Dist2(p, center) > range * range) continue;
        a->pool += amount;
        n++;
    }
    return n;
}
KB_FN int AllyAbsorb(HfDamagePreEvent* e, HfDamageEvent* d)
{
    if (e->cancel || d->amount <= 0 || d->kind == 8 || d->kind == 9) return 0;   /* -1 (a kill), drowning, bleeding out: through */
    KbAlly* a = AllyOf(d->target);
    if (!a || a->pool < 1) return 0;
    g_allyHits++;
    a->lastHit = g_now;
    if (a->pool >= d->amount)
    {
        a->pool -= d->amount;
        a->taken += d->amount;
        g_allyAbsorbed += d->amount;
        e->cancel = 1;
        if (a->pool < 1) { a->pool = 0; g_allyBroke++; if (kb_ally_broke) kb_ally_broke(a->e); }
        return 1;
    }
    int32_t rest = d->amount - (int32_t)a->pool;
    a->taken += a->pool;
    g_allyAbsorbed += a->pool;
    a->pool = 0;
    d->amount = rest;
    g_allyBroke++;
    if (kb_ally_broke) kb_ally_broke(a->e);
    return 1;
}
/* how many allies hold a shield, and the pools' total */
KB_FN int AllyShielded(double* total)
{
    int n = 0;
    double t = 0;
    for (int i = 0; i < KB_ALLIES; i++) if (g_ally[i].e && g_ally[i].pool >= 1) { n++; t += g_ally[i].pool; }
    if (total) *total = t;
    return n;
}

#endif
