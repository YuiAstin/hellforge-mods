/* kitbase guard: what every kit does the same way to a hit on her before it lands (HellForge's damage event, through
 * kb_damage.h's kb_damage_taken): take only a share of it, spare her the landing of a leap, and tell a hit that would
 * kill her.
 *
 * Code shared by the character kits. Source of truth: kitbase/ at the repo root. The copy in a kit's src/kitbase/ is
 * written by tools/kitbase_sync.py, so edit kitbase/, never the copy. See docs/kit-base.md.
 * Include after the C headers and the HellForge SDK headers. Everything is static and marked unused, so a kit only
 * pays for what it calls.
 *
 * From the shipped kits (the same lines in each, 2026-11-27): the share in Faelynn's toughness, Lenna's Leaping Pursuit,
 * Leva's lockdown arcs, Destroyer's Mech and Rage and Voymastina's Stability; the landing in Faelynn's, Lenna's and
 * Destroyer's leap windows; the fatal hit in Faelynn's Last Bark and Destroyer's Denied Exit. Those kits keep their own
 * lines until each is next updated (their releases are not changed by this header); a kit written after it (Andoris
 * first) calls these.
 *
 * The kit calls, inside its kb_damage_taken:
 *   GuardShare(e, d, share)  the hit keeps share of its damage (0: none of it, cancelled; 1: all), rounded; a share that
 *                            rounds under 1 cancels it (an amount of 0 is not taken as "none"). 1 when it cancelled
 *   GuardLanding(d)          1 for a fall or landing hit (impact or flat, dealt by nothing): what a leap's window spares
 *   GuardFatal(d, hp)        1 when the hit would take her hp (or kills outright: -1) */
#ifndef KB_GUARD_H
#define KB_GUARD_H

#include "kb_core.h"

KB_VAR unsigned g_guardShared, g_guardCancelled;
KB_VAR double g_guardKept;   /* damage kept off her by shares, in all */

KB_FN int GuardShare(HfDamagePreEvent* e, HfDamageEvent* d, double share)
{
    if (e->cancel || d->amount <= 0 || share >= 0.9999) return 0;
    if (share <= 0.0001) { g_guardKept += d->amount; g_guardCancelled++; e->cancel = 1; return 1; }
    double a = floor(d->amount * share + 0.5);
    g_guardShared++;
    if (a < 1) { g_guardKept += d->amount; g_guardCancelled++; e->cancel = 1; return 1; }
    g_guardKept += d->amount - a;
    d->amount = (int32_t)a;
    return 0;
}
KB_FN int GuardLanding(const HfDamageEvent* d) { return (d->kind == 5 || d->kind == 7) && !NetOk(d->dealer) && !NetOk(d->owner); }
KB_FN int GuardFatal(const HfDamageEvent* d, int32_t hp) { return d->amount < 0 || (hp > 0 && d->amount >= hp); }

#endif
