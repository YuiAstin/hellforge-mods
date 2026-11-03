/* kitbase damage event: HellForge's damage event (HF_ON_DAMAGE_PRE) routed to the kit, and helpers for its handlers.
 *
 * Code shared by the character kits. Source of truth: kitbase/ at the repo root. The copy in a kit's src/kitbase/ is
 * written by tools/kitbase_sync.py, so edit kitbase/, never the copy. See docs/kit-base.md.
 * Include after the C headers and the HellForge SDK headers. Everything is static and marked unused, so a kit only
 * pays for what it calls.
 *
 * The kit subscribes OnDamagePre to HF_ON_DAMAGE_PRE. In a mission, for a hit nothing has cancelled yet:
 *   kb_damage_seen     every hit (Leva's probe)
 *   kb_damage_taken    a hit on her
 *   kb_damage_dealt    any other hit (hers or anyone's: the kit tells with IsShot / MineHit)
 * The skills' own rules stay in the kit; what every kit does the same way is a helper here. */
#ifndef KB_DAMAGE_H
#define KB_DAMAGE_H

#include "kb_friends.h"

/* Her last target (her own shots): who, where, when */
KB_VAR uint32_t g_lastTarget;
KB_VAR float g_lastPos[3];
KB_VAR double g_lastAt;

/* Kit callbacks, set in hellforge_mod_init (all optional). me: her Helldiver's entity. */
KB_VAR void (*kb_damage_seen)(HfDamagePreEvent* e, HfDamageEvent* d);
KB_VAR void (*kb_damage_taken)(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me);
KB_VAR void (*kb_damage_dealt)(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me);

/* One of her own shots: a hit of hers (MineHit) of kind 0, 1 or 3 (projectile, melee-projectile, beam) */
KB_FN int IsShot(const HfDamageEvent* d) { return MineHit(d) && (d->kind == 0 || d->kind == 1 || d->kind == 3); }

/* The hit's damage times mult, rounded, at most INT32_MAX */
KB_FN void ScaleDamage(HfDamageEvent* d, double mult)
{
    double a = floor(d->amount * mult + 0.5);
    d->amount = a > 2147483647.0 ? 2147483647 : (int32_t)a;
}

/* Her shot's target: kept as a foe (unless it is a Helldiver) and as her last target */
KB_FN void NoteTarget(const HfDamageEvent* d)
{
    uint8_t tid[24];
    if (Descriptor(d->target, tid) && Quad(tid) != AVATAR) AddFoe(d->target, Quad(tid));
    g_lastTarget = d->target;
    memcpy(g_lastPos, d->position, sizeof g_lastPos);
    g_lastAt = g_now;
}

KB_FN void OnDamagePre(HfMod* mod, const HfEventInfo* info, void* user)
{
    HfDamagePreEvent* e = (HfDamagePreEvent*)(void*)info;
    HfDamageEvent* d = &e->damage;
    if (!g_inMission || e->cancel) return;
    if (kb_damage_seen) kb_damage_seen(e, d);
    uint32_t me = atomic_load(&g_avatar);
    if (d->target == me && me) { if (kb_damage_taken) kb_damage_taken(e, d, me); }
    else if (kb_damage_dealt) kb_damage_dealt(e, d, me);
}

#endif
