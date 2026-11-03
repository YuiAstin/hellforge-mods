/* kitbase T-Doll body: more health and stamina, and a Hellbomb when her stamina runs dry.
 *
 * Code shared by the character kits. Source of truth: kitbase/ at the repo root. The copy in a kit's src/kitbase/ is
 * written by tools/kitbase_sync.py, so edit kitbase/, never the copy. See docs/kit-base.md.
 * Include after the C headers and the HellForge SDK headers. Everything is static and marked unused, so a kit only
 * pays for what it calls. */
#ifndef KB_BODY_H
#define KB_BODY_H

#include "kb_player.h"

/* Settings (the kit's Setting() reads them from its .ini and Mod Options Menu) */
KB_VAR float s_hpMult = 1.5f, s_staminaMult = 2.0f;
KB_VAR int s_hellbomb = 1, s_hellbombRow = 125;
KB_VAR float s_hellbombHold = 0.5f;

/* Kit callbacks, set in hellforge_mod_init (both optional):
 * kb_hellbomb_veto: nonzero holds the Hellbomb back (Faelynn: while Last Bark has her);
 * kb_hellbomb_fired: where it went off (Voymastina: her blast immunity lets this one reach her). */
KB_VAR int (*kb_hellbomb_veto)(void);
KB_VAR void (*kb_hellbomb_fired)(const float pos[3]);

KB_VAR int g_bodyOn;
KB_FN void Body(int on)
{
    HfField hp = HF_FIELD(Health, health), sd = HF_FIELD(Avatar, sprint_duration);
    if (!on)
    {
        if (g_bodyOn) { hf->set_live(self, AVATAR, &hp, NULL, HF_LIVE_SHARED); hf->set_live(self, AVATAR, &sd, NULL, HF_LIVE_SHARED); }
        g_bodyOn = 0;
        return;
    }
    HfLiveValue a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    if (!hf->get_live(self, AVATAR, &hp, &a) || !hf->get_live(self, AVATAR, &sd, &b)) { Log("body: her health or stamina cannot be read"); return; }
    HfValue h = HF_I32((int32_t)floor(a.base.number * s_hpMult + 0.5)), d = HF_F32((float)(b.base.number * s_staminaMult));
    int r1 = fabsf(s_hpMult - 1) > 0.001f ? hf->set_live(self, AVATAR, &hp, &h, HF_LIVE_SHARED) : -1;
    int r2 = fabsf(s_staminaMult - 1) > 0.001f ? hf->set_live(self, AVATAR, &sd, &d, HF_LIVE_SHARED) : -1;
    Log("body: health %.0f -> %d, sprint %.0f -> %.0f s before the bar empties (applies: %d / %d; 1 = now, 2 = next spawn)", a.base.number,
        (int)h.number, b.base.number, d.number, r1, r2);
    g_bodyOn = 1;
}
/* One Hellbomb per run-out: armed while her bar is above a fifth, fired when it reaches 0 (the bar then sits at 0
 * through the recovery delay, which must not fire it again), and spawned once its packages are in. */
KB_VAR double g_boomPending;
KB_VAR int g_boomArmed;
KB_FN void Exhaustion(void)
{
    if (!s_hellbomb || !g_alive || (kb_hellbomb_veto && kb_hellbomb_veto())) { g_boomPending = 0; return; }
    float v = StaminaNow();
    static int noted;
    if (v >= 0 && !noted) { noted = 1; Log("stamina: %.2f (read live)", (double)v); }
    if (v > 0.2f) g_boomArmed = 1;
    /* empty for hellbomb_hold s in a row: the bar flickers to 0 for a moment at times while it is not run dry */
    static double emptySince;
    if (v >= 0 && v <= 0.001f) { if (emptySince <= 0) emptySince = g_now; }
    else emptySince = 0;
    if (g_boomArmed && emptySince > 0 && g_now - emptySince >= s_hellbombHold) { g_boomArmed = 0; g_boomPending = g_now + 3; }
    if (g_boomPending <= 0) return;
    if (g_now > g_boomPending) { g_boomPending = 0; Log("out of stamina: the Hellbomb's packages did not load in time"); return; }
    float pos[3];
    if (!PlayerPose(pos, NULL)) return;
    pos[2] += 0.3f;
    int r = hf->spawn_explosion(self, (uint32_t)s_hellbombRow, pos);   /* not one of her own blasts: it is meant to reach her */
    if (r == HF_ACT_WAITING) return;                                     /* its packages: again next frame */
    g_boomPending = 0;
    if (kb_hellbomb_fired) kb_hellbomb_fired(pos);
    Log("out of stamina: HELLBOMB (row %d, answer %d)", s_hellbombRow, r);
}

#endif
