/* Lenna (yui.lenna): Lenna's kit from Girls' Frontline 2 as abilities on keys, for your own Helldiver (solo or host).
 *
 * Her resource is CI (Confectance Index, 0 to ci_max). Enemies she marks with Conductivity build it; an ability that
 * finds 3 CI spends them for its enhanced version.
 *   Wild Extinction (key_wild, default Z): her next we_hits hits on the last enemy she shot, within we_window s, deal
 *     we_mult times their damage; the first stuns it (medium) and marks it. Enhanced: we_mult_enhanced, a large stun.
 *   Leaping Pursuit (key_leap, default X): a teleport lp_distance m along her aim onto the ground there (short of
 *     walls), an arc burst where she left, and a stun on enemies near both ends.
 *     Around it, for lp_window s: no knockdown or ragdoll (her flinch and push stay), lp_damage_taken times the damage
 *     taken, a heal of lp_heal, and lp_speed times her jog and sprint for lp_speed_seconds. On landing (lp_land s), the
 *     last enemy she shot, if within lp_reach m, is stunned (medium) and marked. Enhanced: Wild Extinction is ready again.
 *   Hunting Strategy (key_ult, default C): an arc zap on every enemy within hs_radius m of her, and hs_pulses more
 *     hs_pulse_step s apart; each zapped enemy is stunned for hs_stun s and marked (CI +1 each, at most hs_ci_cap). Then
 *     Electric Arc for hs_arc s: her hits stun (small, at most once a second per enemy) and mark. Enhanced: Stealth
 *     for hs_stealth s: the Reduced Signature passive in the helmet's slot (your armour's own stays) and her weapons
 *     suppressed.
 *   King's Authority (passive): her own EMS pulses do not touch her; marked enemies take mark_mult times the damage for
 *     mark_seconds; killing a marked enemy gives 1 CI.
 *
 * Her aim is read from her Helldiver's scene graph (the head and the head-aim point 2 m ahead of it); her knockdown
 * window clears the knockdown flag of hits on her in the game's hit code (as Primed Sure Footed does).
 *
 * Research credit: the hit-result layout, the hooked functions, the entity and scene-graph reads come from Helldiver
 * Survival Rework 0.3.2 by enochlophobianumerophobia (MIT) and Scope of Authority's Apex knockdown notes, whose research
 * used HD2Runtime by SkyeShade's catalogues (https://github.com/SkyeShade/HD2Runtime). See THIRD_PARTY_NOTICES.md. */
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <windows.h>
#include "hellforge_mod.h"
#include "hellforge_components.h"
#include "hellforge_content.h"

#define VERSION "0.2.10"
#define BUILD "25480438"

#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"
#include "kitbase/kb_body.h"
#include "kitbase/kb_hits.h"
#include "kitbase/kb_damage.h"
#include "kitbase/kb_settings.h"

/* ---- settings ------------------------------------------------------------------------------------------------- */
static int s_ciMax = 6, s_weHits = 0, s_hsPulses = 3, s_hsCiCap = 3;
static float s_weCd = 8, s_weWindow = 5, s_arcStep = 0.35f, s_weMult = 2.4f, s_weMultEnh = 3.0f;
static float s_lpCd = 12, s_lpUp = 8, s_lpForward = 10, s_lpDistance = 10, s_lpWindow = 2.5f, s_lpTaken = 0.5f, s_lpHeal = 20, s_lpSpeed = 1.4f,
             s_lpSpeedSeconds = 4, s_lpLand = 1.0f, s_lpReach = 20;
static float s_hsCd = 40, s_hsPulseStep = 2, s_hsRadius = 20, s_hsArc = 8, s_hsStealth = 10, s_hsStun = 10, s_hsStunEvery = 1.5f, s_lpLift = 1.5f, s_lpClimb = 6, s_lpDrop = 12, s_lpBurstRadius = 6, s_lpStun = 3;
static int s_hsArcsMax = 24;
static float s_arcRate = 10;
static double g_arcBudget = 5, g_arcBudgetAt;
static unsigned g_arcsSkipped;
static float s_markMult = 1.3f, s_markSeconds = 6;
static int s_ems = 0, s_arcRow = 157, s_arcBlast = 41;  /* 157: the G-31 Arc grenade's detonation (her arcs); 41: the GL-52 De-Escalator's burst (their blast) */  /* the EMS explosion row: 0 = the EMS Mortar Sentry shell's own (read at game start) */

/* Every setting: its .ini / Mod Options Menu name, variable and range (kitbase/kb_settings.h) */
static const KbSetting kSettings[] = {
    KB_I("ci_max", s_ciMax, 3, 20),
    KB_F("we_cooldown", s_weCd, 0, 600), KB_F("we_window", s_weWindow, 0.5, 30), KB_I("we_hits", s_weHits, 0, 1000),
    KB_F("we_mult", s_weMult, 1, 20), KB_F("we_mult_enhanced", s_weMultEnh, 1, 20),
    KB_F("lp_cooldown", s_lpCd, 0, 600), KB_F("lp_up", s_lpUp, 0, 25), KB_F("lp_forward", s_lpForward, 0, 25),
    KB_F("lp_window", s_lpWindow, 0, 30), KB_F("lp_damage_taken", s_lpTaken, 0, 1), KB_F("lp_heal", s_lpHeal, 0, 500),
    KB_F("lp_speed", s_lpSpeed, 1, 3), KB_F("lp_speed_seconds", s_lpSpeedSeconds, 0, 60), KB_F("lp_land", s_lpLand, 0, 5),
    KB_F("lp_reach", s_lpReach, 0, 200), KB_F("lp_distance", s_lpDistance, 2, 30),
    KB_F("hs_cooldown", s_hsCd, 0, 600), KB_I("hs_pulses", s_hsPulses, 0, 10), KB_F("hs_pulse_step", s_hsPulseStep, 0.5, 10),
    KB_F("hs_radius", s_hsRadius, 1, 50), KB_I("hs_ci_cap", s_hsCiCap, 0, 20), KB_F("hs_arc", s_hsArc, 0, 60),
    KB_F("hs_stealth", s_hsStealth, 0, 120), KB_F("hs_stun", s_hsStun, 0, 60), KB_F("hs_stun_every", s_hsStunEvery, 0.3, 10),
    KB_I("hs_arcs_max", s_hsArcsMax, 0, 64), KB_F("lp_lift", s_lpLift, 0, 5), KB_F("lp_climb", s_lpClimb, 0.5, 30), KB_F("lp_drop", s_lpDrop, 0.5, 50),
    KB_F("lp_burst_radius", s_lpBurstRadius, 0, 30), KB_F("lp_stun", s_lpStun, 0, 30),
    KB_F("mark_mult", s_markMult, 1, 10), KB_F("mark_seconds", s_markSeconds, 0.5, 60),
    KB_F("hp_mult", s_hpMult, 0.1, 10), KB_F("stamina_mult", s_staminaMult, 0.1, 20), KB_I("hellbomb_on_exhaustion", s_hellbomb, 0, 1),
    KB_I("hellbomb_explosion", s_hellbombRow, 0, 100000), KB_F("hellbomb_hold", s_hellbombHold, 0, 10),
    KB_I("ems_explosion", s_ems, 0, 100000), KB_I("arc_explosion", s_arcRow, 0, 100000), KB_F("arc_step", s_arcStep, 0.05, 5), KB_F("arc_rate", s_arcRate, 1, 100), KB_I("arc_blast", s_arcBlast, 0, 100000),
};

/* The default lenna.ini: tools/inigen.py writes the file from this text (the mod never writes it) */
static __attribute__((unused)) void DefaultIni(FILE* f)
{
fprintf(f, "; Lenna %s. Keys: a letter, digit, F1-F24, Mouse3, Mouse4 or Mouse5, with Shift+, Ctrl+ or Alt+ in front for a\n"
               "; combo (Shift+Q). The game still sees the keys too (Shift sprints, R reloads), so pick ones it leaves alone.\n"
               "key_wild = %s\nkey_leap = %s\nkey_ult = %s\n; CI: most she holds (enhanced abilities spend 3).\nci_max = %d\n"
               "; Wild Extinction: cooldown; for we_window s her hits (at most we_hits) deal we_mult times their damage and\n"
               "; each sets off an arc where it lands (at most one every arc_step s; arc_explosion: 157, the G-31 Arc's, whose arcs she\n"
               "; uses; arc_blast: 41, the GL-52 De-Escalator burst whose blast and sound they get, 0 none). arc_rate: at most that many\n"
               "; of her arcs a second, half a second's"
               " worth at once (too many at once leave their lightning on the field); the rest are skipped.\n"
               "we_cooldown = %g\nwe_window = %g\nwe_hits = %d\nwe_mult = %g\nwe_mult_enhanced = %g\n"
               "; Leaping Pursuit: cooldown, a teleport lp_distance m along her aim onto the ground there (at most lp_climb m\n"
               "; higher or lp_drop m lower; short of walls, shorter when there is no ground); an arc burst where she left and a stun of lp_stun s\n"
               "; on enemies within lp_burst_radius m of both ends; the protected window (no knockdown or fall damage, damage\n"
               "; taken times lp_damage_taken), heal, speed boost and its length, landing time and stun reach.\n"
               "lp_cooldown = %g\nlp_distance = %g\nlp_climb = %g\nlp_drop = %g\nlp_burst_radius = %g\nlp_stun = %g\nlp_window = %g\nlp_damage_taken = %g\nlp_heal = %g\n"
               "lp_speed = %g\nlp_speed_seconds = %g\nlp_land = %g\nlp_reach = %g\n"
               "; Hunting Strategy: cooldown; an arc zap on every enemy within hs_radius m of her, then hs_pulses more zaps\n"
               "; hs_pulse_step s apart; zapped enemies are stunned for hs_stun s (renewed every hs_stun_every s) and marked;\n"
               "; at most hs_arcs_max arc blasts a zap (the rest are still stunned); CI per cast at most; Electric Arc and\n"
               "; Stealth lengths.\n"
               "hs_cooldown = %g\nhs_pulses = %d\nhs_pulse_step = %g\nhs_radius = %g\nhs_stun = %g\nhs_stun_every = %g\n"
               "hs_arcs_max = %d\nhs_ci_cap = %d\nhs_arc = %g\nhs_stealth = %g\n"
               "; Conductivity: marked enemies take this times the damage, this long.\nmark_mult = %g\nmark_seconds = %g\n"
               "; An EMS blast under her zaps too (0: none; 180: the EMS Mortar Sentry shell's field; its stun reaches her).\nems_explosion = %d\n"
               "arc_explosion = %d\narc_blast = %d\narc_step = %g\narc_rate = %g\n"
               "; Her body: hp_mult times a Helldiver's health, stamina_mult times her sprint before the bar empties. If her\n"
               "; stamina ever runs out, hellbomb_on_exhaustion (1) sets off hellbomb_explosion (125: the B-100 Portable\n"
               "; Hellbomb's) on her.\n"
               "; hellbomb_hold: seconds the bar must stay empty first (it flickers to 0 for a moment at times).\n"
               "hp_mult = %g\nstamina_mult = %g\nhellbomb_on_exhaustion = %d\nhellbomb_explosion = %d\nhellbomb_hold = %g\n",
        VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, s_ciMax, (double)s_weCd, (double)s_weWindow, s_weHits, (double)s_weMult,
        (double)s_weMultEnh, (double)s_lpCd, (double)s_lpDistance, (double)s_lpClimb, (double)s_lpDrop, (double)s_lpBurstRadius, (double)s_lpStun, (double)s_lpWindow, (double)s_lpTaken,
        (double)s_lpHeal, (double)s_lpSpeed, (double)s_lpSpeedSeconds, (double)s_lpLand, (double)s_lpReach, (double)s_hsCd,
        s_hsPulses, (double)s_hsPulseStep, (double)s_hsRadius, (double)s_hsStun, (double)s_hsStunEvery, s_hsArcsMax, s_hsCiCap, (double)s_hsArc, (double)s_hsStealth,
        (double)s_markMult, (double)s_markSeconds, s_ems, s_arcRow, s_arcBlast, (double)s_arcStep, (double)s_arcRate,
        (double)s_hpMult, (double)s_staminaMult, s_hellbomb, s_hellbombRow, (double)s_hellbombHold);
}

/* ---- the clock, the player ------------------------------------------------------------------------------------ */

/* ---- state ---------------------------------------------------------------------------------------------------- */
static int g_ci;
static double g_cdWild, g_cdLeap, g_cdUlt;   /* ready at */
/* the last enemy she shot */
/* Wild Extinction's charge */
static uint32_t g_chargeTarget;
static int g_chargeHits, g_chargeStun;
static float g_chargeMult;
static double g_chargeUntil;
/* Leaping Pursuit (its knockdown window: g_shieldUntilMs, kitbase/kb_hits.h) */
static double g_drUntil, g_speedUntil, g_forwardAt, g_landAt;
/* the leap: pushed again each few frames until she is seen rising, then forward until she is seen moving */
static int g_leap;
static double g_leapT0;
static float g_leapDir[2];
static int g_speedOn;
static float g_baseJog = -1, g_baseSprint = -1;
/* Hunting Strategy */
typedef struct Pulse { float p[3]; double at; uint32_t row; } Pulse;
static double g_lastArc;
static Pulse g_pulses[12];
static int g_npulse;

static double g_nextPulse, g_arcUntil, g_stealthUntil;
static int g_pulsesLeft, g_castCi, g_stealthOn;
/* Conductivity marks */
typedef struct Mark { uint32_t entity; double until, stunnedAt; } Mark;
static Mark g_marks[64];
/* counts for the log */
static unsigned g_casts[3], g_marked, g_stuns, g_boosted, g_selfSpared, g_knockdowns;

static Mark* MarkOf(uint32_t entity)
{
    for (int i = 0; i < 64; i++) if (g_marks[i].entity == entity && g_marks[i].until > g_now) return &g_marks[i];
    return NULL;
}
static void GainCi(int n)
{
    g_ci += n;
    if (g_ci > s_ciMax) g_ci = s_ciMax;
}
/* Marks an enemy Conductive; 1 when newly marked */
static int MarkEnemy(uint32_t entity)
{
    if (!entity || entity == atomic_load(&g_avatar)) return 0;
    Mark* m = MarkOf(entity);
    if (m) { m->until = g_now + s_markSeconds; return 0; }
    int slot = 0;
    for (int i = 0; i < 64; i++) if (g_marks[i].until < g_marks[slot].until) slot = i;
    g_marks[slot].entity = entity;
    g_marks[slot].until = g_now + s_markSeconds;
    g_marks[slot].stunnedAt = 0;
    g_marked++;
    return 1;
}
static void Stun(uint32_t entity, uint32_t status)
{
    if (Act("stun", hf->apply_status(self, entity, status, 100.0f))) g_stuns++;
}
static int SpendCi(void)
{
    if (g_ci < 3) return 0;
    g_ci -= 3;
    return 1;
}

/* ---- speed, stealth -------------------------------------------------------------------------------------------- */
static void Speed(int on)
{
    HfField jog = HF_FIELD(Avatar, jog), sprint = HF_FIELD(Avatar, sprint);
    if (!on)
    {
        if (g_speedOn) { hf->set_live(self, AVATAR, &jog, NULL, HF_LIVE_SHARED); hf->set_live(self, AVATAR, &sprint, NULL, HF_LIVE_SHARED); }
        g_speedOn = 0;
        return;
    }
    if (g_baseJog < 0)
    {
        HfLiveValue a, b;
        memset(&a, 0, sizeof a);
        memset(&b, 0, sizeof b);
        if (!hf->get_live(self, AVATAR, &jog, &a) || !hf->get_live(self, AVATAR, &sprint, &b)) { Log("speed: the Helldiver's speeds cannot be read"); return; }
        g_baseJog = (float)a.base.number;
        g_baseSprint = (float)b.base.number;
    }
    HfValue j = HF_F32(fminf(10, g_baseJog * s_lpSpeed)), s = HF_F32(fminf(10, g_baseSprint * s_lpSpeed));
    int r1 = hf->set_live(self, AVATAR, &jog, &j, HF_LIVE_SHARED), r2 = hf->set_live(self, AVATAR, &sprint, &s, HF_LIVE_SHARED);
    if (!g_speedOn) Log("speed: jog %.1f -> %.1f, sprint %.1f -> %.1f m/s (applies: %d / %d; 1 = now)", (double)g_baseJog, j.number,
        (double)g_baseSprint, s.number, r1, r2);
    g_speedOn = 1;
}

static void Stealth(int on)
{
    HfField sup = HF_FIELD(WeaponData, suppressed);
    if (!on)
    {
        if (g_stealthOn)
        {
            hf->set_passives(self, HF_PASSIVE_KIT, HF_PASSIVE_KIT);
            for (int i = 1; i <= 3; i++) if (g_weaponRes[i]) hf->set_live(self, g_weaponRes[i], &sup, NULL, HF_LIVE_SHARED);
            Log("stealth over");
        }
        g_stealthOn = 0;
        return;
    }
    g_stealthOn = 1;
    int p = hf->set_passives(self, HF_PASSIVE_KIT, 32);  /* REDUCED SIGNATURE in the helmet's slot */
    HfValue one = HF_U8(1);
    int n = 0, now = 0;
    for (int i = 1; i <= 3; i++)
    {
        if (!g_weaponRes[i]) continue;
        int r = hf->set_live(self, g_weaponRes[i], &sup, &one, HF_LIVE_SHARED);
        n += r != 0;
        now += r == HF_APPLIES_NOW;
    }
    HfPassives st;
    memset(&st, 0, sizeof st);
    hf->get_passives(self, &st);
    Log("stealth for %.0f s: Reduced Signature %s (%s); %d weapon(s) suppressed (%d at once)", (double)s_hsStealth, p ? "held" : "refused",
        st.status, n, now);
}

/* ---- the abilities -------------------------------------------------------------------------------------------- */
static int TargetFresh(double within) { return g_lastTarget && g_now - g_lastAt <= within; }

static void WildExtinction(void)
{
    if (g_now < g_cdWild) return;
    int enhanced = SpendCi();
    g_chargeTarget = 0;  /* any enemy she hits */
    g_chargeHits = s_weHits > 0 ? s_weHits : 1000000;
    g_chargeStun = 1;
    g_chargeMult = enhanced ? s_weMultEnh : s_weMult;
    g_chargeUntil = g_now + s_weWindow;
    g_cdWild = g_now + s_weCd;
    g_lastArc = 0;
    g_casts[0]++;
    Log("Wild Extinction: for %.0f s her hits (%d at most; 0: all) deal x%.1f and set off arcs%s", (double)s_weWindow, s_weHits, (double)g_chargeMult,
        enhanced ? " (enhanced, 3 CI)" : "");
}

/* ---- the teleport: the game's own position setter, as the LIFT-182 Warp Pack calls it (Warp Probe: game.dll+0x88D795
 * calls 0x5A2BB0 with (NULL, the Helldiver's entity, the destination), then zeroes its velocity with 0x4A7550), with a
 * wall and ground check through the game's raycast (as Helldiver Survival Rework's cover check calls it) -------------- */
static int StunAround(const float at[3], float radius, float seconds);
static void Blast_(uint32_t row, const float at[3], const char* what);
typedef void (*SetVecFn)(void*, uint32_t, const float*);
typedef uint32_t (*WorldFn)(const void*);
typedef uint32_t (*RayFn)(uint32_t, const void*, const void*, float, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t);
static SetVecFn g_setPosition, g_setVelocity;
static WorldFn g_worldId;
static RayFn g_raycast;
/* The nearest static obstruction along a ray (static actors only, so enemies are not walls): its distance, len + 1
 * when clear, or -1 when the query cannot be made */
static float RayWith(uint32_t unit, const float from[3], const float dir[3], float len, uint32_t filter, uint32_t flags, unsigned* count);
static float g_rayPrefer = -1;
static float Ray(uint32_t unit, const float from[3], const float dir[3], float len) { return RayWith(unit, from, dir, len, 0x393D9518, 0x80000009, NULL); }
static float RayWith(uint32_t unit, const float from[3], const float dir[3], float len, uint32_t filter, uint32_t flags, unsigned* count)
{
    uintptr_t world = Pointer(game + 0x346BFA0);
    if (!world || !g_raycast || !g_worldId) return -1;
    uint32_t id = g_worldId((const void*)world);
    if (id >= 4) return -1;
    uint8_t hits[16 * 44];
    memset(hits, 0, sizeof hits);
    uint32_t n = g_raycast(id, from, dir, len, 2, 1, filter, flags, unit, hits, 16);
    if (count) *count = n;
    if (n > 16) return -1;
    float best = len + 1;
    for (uint32_t i = 0; i < n; i++)
    {
        float d;
        memcpy(&d, hits + 44u * i + 24, 4);
        if (Word(hits + 44u * i + 28) == unit || !isfinite(d) || d < 0.02f) continue;
        if (g_rayPrefer >= 0 ? fabsf(d - g_rayPrefer) < fabsf(best - g_rayPrefer) : d < best) best = d;
    }
    return best;
}
/* The ground under (x, y) near height `ref`: filter 0 with the static-actor flags sees the terrain as well as rocks
 * and floors (the filter sweep found it); of the surfaces under the point the one nearest `ref` (so a roof above a
 * street or a cave ceiling is not taken). 1 found. */
static int GroundAt(uint32_t unit, float x, float y, float ref, float* z)
{
    float top[3] = {x, y, ref + s_lpClimb}, down[3] = {0, 0, -1}, len = s_lpClimb + s_lpDrop;
    uintptr_t world = Pointer(game + 0x346BFA0);
    if (!world || !g_raycast || !g_worldId) return 0;
    uint32_t id = g_worldId((const void*)world);
    if (id >= 4) return 0;
    uint8_t hits[16 * 44];
    memset(hits, 0, sizeof hits);
    uint32_t n = g_raycast(id, top, down, len, 2, 1, 0, 0x80000009u, unit, hits, 16);
    if (n > 16) return 0;
    /* the surfaces under the point, as heights */
    float h[16];
    int k = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        float d;
        memcpy(&d, hits + 44u * i + 24, 4);
        if (Word(hits + 44u * i + 28) == unit || !isfinite(d) || d < 0.02f || d > len) continue;
        h[k++] = top[2] - d;
    }
    if (!k) return 0;
    /* the one nearest her height; but where something is over it within her height (a bug nest, a rock or a structure
     * standing on the terrain: the terrain runs on under it) she would be inside it: then the highest surface */
    int best = 0, top_ = 0;
    for (int i = 1; i < k; i++)
    {
        if (fabsf(h[i] - ref) < fabsf(h[best] - ref)) best = i;
        if (h[i] > h[top_]) top_ = i;
    }
    for (int i = 0; i < k; i++)
        if (h[i] > h[best] + 0.3f && h[i] < h[best] + 2.0f) { best = top_; break; }
    *z = h[best];
    return 1;
}

static void LeapingPursuit(void)
{
    if (g_now < g_cdLeap) return;
    if (!g_setPosition || !g_setVelocity || !g_raycast) { Log("Leaping Pursuit: the teleport is unavailable on this build (see the start of the log)"); return; }
    float pos[3], aim[3];
    if (!PlayerPose(pos, aim)) { Log("Leaping Pursuit: her position or aim cannot be read"); return; }
    float h = sqrtf(aim[0] * aim[0] + aim[1] * aim[1]);
    if (h < 0.05f) { Log("Leaping Pursuit: she is looking straight up or down"); return; }
    g_leapDir[0] = aim[0] / h;
    g_leapDir[1] = aim[1] / h;
    g_leap = 1;          /* the teleport waits for an idle physics query (a frame or two) */
    g_leapT0 = g_now;
}

/* The pending leap: a wall check at chest height (rocks, walls, buildings), the ground at the spot (terrain too),
 * then the warp's two calls: she is there in one frame, standing on the ground. */
static void LeapFrame(void)
{
    if (!g_leap) return;
    if (!QueryIdle())
    {
        if (g_now - g_leapT0 > 0.5) { g_leap = 0; Log("Leaping Pursuit: the physics queries stayed busy: no teleport"); }
        return;
    }
    g_leap = 0;
    uint8_t id[24];
    uint32_t me = atomic_load(&g_avatar);
    float pos[3];
    if (!Descriptor(me, id) || Quad(id) != AVATAR || !PlayerPose(pos, NULL)) { Log("Leaping Pursuit: her Helldiver cannot be read"); return; }
    uint32_t unit = Word(id + 12);
    float dir[3] = {g_leapDir[0], g_leapDir[1], 0};
    float chest[3] = {pos[0], pos[1], pos[2] + 1.0f};
    float wall = Ray(unit, chest, dir, s_lpDistance + 0.6f);
    if (wall < 0) { Log("Leaping Pursuit: the raycast is unavailable now"); return; }
    float dist = fminf(s_lpDistance, wall - 0.6f), dest[3] = {0};
    int found = 0;
    for (int tries = 0; tries < 4 && dist >= 1.5f && !found; tries++)
    {
        dest[0] = pos[0] + dir[0] * dist;
        dest[1] = pos[1] + dir[1] * dist;
        if (GroundAt(unit, dest[0], dest[1], pos[2], &dest[2])) found = 1;
        else dist *= 0.7f;
    }
    /* no ground ahead: to whatever she is looking at (terrain, a wall, a rock), stopped short of it */
    if (!found)
    {
        float look[3];
        float far_ = PlayerPose(pos, look) ? RayWith(unit, chest, look, s_lpDistance + 0.6f, 0, 0x80000009u, NULL) : -1;
        if (far_ >= 1.5f + 0.6f && far_ <= s_lpDistance + 0.6f)
        {
            dist = far_ - 0.6f;
            for (int i = 0; i < 3; i++) dest[i] = chest[i] + look[i] * dist;
            float z;
            if (GroundAt(unit, dest[0], dest[1], dest[2], &z) && z <= dest[2] + 0.5f) dest[2] = z;
            else dest[2] -= 0.9f;   /* against a wall with nothing under: feet a little below the aim line */
            found = 2;
        }
    }
    if (!found) { Log("Leaping Pursuit: no ground within %.0f m up or %.0f m down ahead and nothing in sight within %.0f m: no teleport",
                      (double)s_lpClimb, (double)s_lpDrop, (double)s_lpDistance); return; }
    dest[2] += 0.05f;
    float zero[3] = {0, 0, 0};
    g_setPosition(NULL, me, dest);
    g_setVelocity(NULL, me, zero);
    /* the burst where she left (she is gone) and the stuns at both ends */
    if (s_arcRow && dist >= 4) { float b[3] = {pos[0], pos[1], pos[2] + 0.5f}; Blast_((uint32_t)s_arcRow, b, "Leaping Pursuit's burst"); }
    int left = StunAround(pos, s_lpBurstRadius, s_lpStun), landed = StunAround(dest, s_lpBurstRadius, s_lpStun);
    /* the rest of the ability */
    g_landAt = g_now + s_lpLand;
    g_drUntil = g_now + s_lpWindow;
    atomic_store(&g_shieldUntilMs, GetTickCount64() + (ULONGLONG)(s_lpWindow * 1000));
    g_speedUntil = g_now + s_lpSpeedSeconds;
    Speed(1);
    int enhanced = SpendCi();
    if (enhanced) g_cdWild = 0;
    g_cdLeap = g_now + s_lpCd;
    g_casts[1]++;
    if (s_lpHeal > 0) Act("Leaping Pursuit's heal", hf->heal(self, s_lpHeal));
    Log("Leaping Pursuit: teleported %.1f m, %+.1f m in height%s%s; stunned %d where she left, %d where she landed%s", (double)dist,
        (double)(dest[2] - pos[2]), wall <= s_lpDistance + 0.6f ? " (stopped short of a wall)" : "",
        found == 2 ? " (no ground ahead: to where she aimed)" : "", left, landed,
        enhanced ? "; enhanced: Wild Extinction ready" : "");
}

/* ---- her arc blast: an explosion of her own --------------------------------------------------------------------- *
 * The G-31 Arc grenade's detonation (157) carries her arcs but no blast; the GL-52 De-Escalator's burst (41) has the
 * blast and sound. Her own explosion is a copy of the burst's row (its blast, sound and effects) whose damage (+4) and
 * arc (+120) are the G-31's: the blast with the G-31's arcs. The SDK cannot add explosions, so the game's explosion list
 * is extended here, the way More Plasma Stratagems 1.1.0 by HadePasianax (MIT) does (its native_plasma_explosions.h):
 *   game.dll + 0x37CC920  ExplosionType -> 152-byte row, 423 slots; reader 0x13C0DC0 and bound 0x13C0DB5 checked first.
 * The list's pointers go to a block within 2 GB of game.dll with her row after them, every instruction that addresses
 * the list is pointed at the block (each read back; a failed write puts back all), and the bounds beside its readers
 * are raised. It cannot be set off directly (HellForge finds explosions in the game's data): a bullet of hers carries it.
 * Leva's lockdown burst extends the same list: only one of the two mods can (the other keeps its fallback). */
#define NT_IMAGE 0x4744000u
#define NT_EXP_TABLE 0x37CC920u
#define NT_EXP_READER 0x13C0DC0u
#define NT_EXP_BOUND 0x13C0DB5u
#define NT_EXP_NATIVE 423u
#define NT_MAX_PATCHES 256u
typedef struct NtPatch { uintptr_t at; uint32_t before, after; } NtPatch;
typedef struct NtList
{
    const char* name;
    uint32_t rva, native, count;  /* the list's RVA, its slots, its slots with ours */
    int boundRequired;
    NtPatch patch[NT_MAX_PATCHES];
    unsigned npatch, refs, bounds;
    uintptr_t* block;
} NtList;
static NtList g_ntExp = {"explosion", NT_EXP_TABLE, NT_EXP_NATIVE, NT_EXP_NATIVE + 1, 1};
static uint8_t g_ntExpRow[152] __attribute__((aligned(16)));
static uint32_t g_ntExpId;
static int g_ntState;            /* 0 not yet, 1 live, -1 given up */
static int g_boltReady;
static double g_ntNext;
static unsigned g_ntTries;
static const char* g_ntWhy = "not tried yet";
static uintptr_t g_ntWhere;
static uintptr_t g_ntBase;
static uint32_t g_blastBolt, g_blastBoltDamage;   /* her bullet that carries it, and its own (zeroed) damage */
static int NtFail(const char* why, uintptr_t where) { g_ntWhy = why; g_ntWhere = where; return 0; }
static int NtRead(uintptr_t at, void* out, size_t n) { return !hf->read(at, out, n); }

static int NtAdd(NtList* L, uintptr_t at, uint32_t before, uint32_t after)
{
    for (unsigned i = 0; i < L->npatch; i++)
        if (L->patch[i].at == at) return L->patch[i].before == before && L->patch[i].after == after;
    if (L->npatch >= NT_MAX_PATCHES) return NtFail("too many sites address the list", at - g_ntBase);
    L->patch[L->npatch++] = (NtPatch){at, before, after};
    return 1;
}

/* One code section's sites for a list: [image base + index*8 + list], rip-relative loads of a slot, and fixed slots
 * through an image-base register; then the bounds (cmp with the slot count) within 128 bytes before a site */
static int NtScan(NtList* L, const uint8_t* code, uint32_t first, uint32_t size, uintptr_t block)
{
    static uintptr_t sites[NT_MAX_PATCHES];
    unsigned count = 0;
    const uint32_t lo = L->rva, hi = L->rva + L->native * 8;
    for (uint32_t i = 0; i + 8 <= size; i++)
    {
        const uint8_t* p = code + i;
        if ((p[0] & 0xf8) != 0x48) continue;
        unsigned imm = 0;
        if (p[1] == 0x83 && (p[2] & 0x38) == 0x38) imm = 1;
        else if ((p[1] == 0x81 && (p[2] & 0x38) == 0x38) || (p[1] == 0xc7 && !(p[2] & 0x38))) imm = 4;
        else if (p[1] != 0x8b && p[1] != 0x8d && p[1] != 0x89) continue;
        unsigned dispAt = 0, length = 0;
        uint32_t disp = 0;
        uintptr_t target = 0;
        int rip = 0;
        if ((p[2] & 0xc7) == 0x84 || ((p[2] & 0xc7) == 0x04 && (p[3] & 7) == 5))
        {
            if ((p[3] >> 6) != 3 && !((p[3] & 0x38) == 0x20 && !(p[0] & 2))) continue;
            memcpy(&disp, p + 4, 4);
            if (disp < lo || disp >= hi || (disp - lo) % 8) continue;
            dispAt = 4; length = 8 + imm;
            target = block + (disp - lo);
        }
        else if ((p[2] & 0xc7) == 0x05)
        {
            memcpy(&disp, p + 3, 4);
            length = 7 + imm;
            uintptr_t old = g_ntBase + first + i + length + (int32_t)disp;
            if (old < g_ntBase + lo || old >= g_ntBase + hi || (old - g_ntBase - lo) % 8) continue;
            dispAt = 3; rip = 1;
            target = block + (old - g_ntBase - lo);
        }
        else if ((p[2] & 0xc0) == 0x80 && (p[2] & 7) != 4)
        {
            memcpy(&disp, p + 3, 4);
            if (disp < lo || disp >= hi || (disp - lo) % 8) continue;
            dispAt = 3; length = 7 + imm;
            target = block + (disp - lo);
        }
        if (!dispAt || i + length > size) continue;
        if (count >= NT_MAX_PATCHES) return NtFail("too many sites address the list", first + i);
        sites[count++] = g_ntBase + first + i;
        int64_t adjusted = rip ? (int64_t)target - (int64_t)(g_ntBase + first + i + length) : (int64_t)target - (int64_t)g_ntBase;
        if (adjusted < INT32_MIN || adjusted > INT32_MAX || !NtAdd(L, g_ntBase + first + i + dispAt, disp, (uint32_t)(int32_t)adjusted)) return 0;
    }
    /* the bounds: cmp eax, imm32 / cmp r32, imm32 / cmp r32, imm8 (an optional REX before), with the slot count */
    for (uint32_t i = 0; i + 7 <= size; i++)
    {
        const uint8_t* p = code + i;
        unsigned rex = (p[0] & 0xf0) == 0x40 && !(p[0] & 8) ? 1 : 0;
        const uint8_t* q = p + rex;
        unsigned off = 0, wide = 4;
        if (q[0] == 0x3d) off = 1;
        else if (q[0] == 0x81 && (q[1] & 0xf8) == 0xf8) off = 2;
        else if (q[0] == 0x83 && (q[1] & 0xf8) == 0xf8) { off = 2; wide = 1; }
        if (!off) continue;
        uint32_t value = 0;
        memcpy(&value, q + off, wide);
        if (value != L->native) continue;
        uintptr_t at = g_ntBase + first + i;
        int close = 0;
        for (unsigned k = 0; k < count; k++)
            if (at <= sites[k] && sites[k] - at <= 128) { close = 1; break; }
        if (!close) continue;
        uint32_t before, after;
        memcpy(&before, q + off, 4);
        after = before;
        if (wide == 4) after = L->count;
        else after = (before & 0xFFFFFF00u) | (L->count & 0xFF);
        if (wide == 1 && L->count > 0x7F) continue;
        if (!NtAdd(L, at + rex + off, before, after)) return 0;
        L->bounds++;
        i += rex + off + wide - 1;
    }
    /* the list's address in a form not understood: change nothing */
    for (uint32_t i = 0; i + 4 <= size; i++)
    {
        uint32_t value;
        memcpy(&value, code + i, 4);
        if (value < lo || value >= hi || (value - lo) % 8) continue;
        int found = 0;
        for (unsigned k = 0; k < L->npatch; k++)
            if (L->patch[k].at == g_ntBase + first + i) { found = 1; break; }
        if (!found) return NtFail("an instruction names the list in a form not understood", first + i);
    }
    L->refs += count;
    return 1;
}

static uint8_t* NtNear(size_t size)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t step = si.dwAllocationGranularity;
    for (uintptr_t at = (g_ntBase + NT_IMAGE + step - 1) & ~(step - 1); at < g_ntBase + 0x70000000u; at += step)
    {
        void* p = VirtualAlloc((void*)at, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (p) return (uint8_t*)p;
    }
    return NULL;
}

/* The game's code, read in pieces (a read across a protected page can fail where its pages read one by one) */
static int NtCode(uintptr_t at, uint8_t* copy, uint32_t length)
{
    for (uint32_t off = 0; off < length;)
    {
        uint32_t n = length - off < 32768 ? length - off : 32768;
        if (!NtRead(at + off, copy + off, n))
            for (uint32_t page = 0; page < n;)
            {
                uint32_t m = n - page < 4096 ? n - page : 4096;
                if (!NtRead(at + off + page, copy + off + page, m)) return NtFail("a code page cannot be read", at + off + page - g_ntBase);
                page += m;
            }
        off += n;
    }
    return 1;
}

/* Writes a list's sites, reading each back; on a failure puts back every one written (the failed one too) */
static int NtApply(NtList* L)
{
    unsigned done = 0;
    for (; done < L->npatch; done++)
    {
        uint32_t check = 0;
        if (hf->write(L->patch[done].at, &L->patch[done].after, 4) || !NtRead(L->patch[done].at, &check, 4) || check != L->patch[done].after) break;
        FlushInstructionCache(GetCurrentProcess(), (void*)L->patch[done].at, 4);
    }
    if (done == L->npatch) return 1;
    for (unsigned k = done + 1; k-- > 0;)
    {
        hf->write(L->patch[k].at, &L->patch[k].before, 4);
        FlushInstructionCache(GetCurrentProcess(), (void*)L->patch[k].at, 4);
    }
    return NtFail("a site could not be written (all put back)", L->patch[done].at - g_ntBase);
}

static void NtBolt(void)
{
    HfProjectileSettings* row = g_blastBolt ? (HfProjectileSettings*)hf->projectile_row(g_blastBolt) : NULL;
    if (row) row->impact_explosion = g_ntState == 1 ? g_ntExpId : (uint32_t)s_arcRow;
}
static int NtInstall(void)
{
    size_t image = 0;
    g_ntBase = hf->module_base("game.dll", &image);
    if (!g_ntBase || image != NT_IMAGE) return NtFail("not the supported game.dll", 0);
    static const uint8_t reader[8] = {0x4c, 0x8b, 0xb4, 0xc1, 0x20, 0xc9, 0x7c, 0x03}, bound[5] = {0x3d, 0xa7, 0x01, 0, 0};
    uint8_t got[8];
    if (!NtRead(g_ntBase + NT_EXP_READER, got, 8) || memcmp(got, reader, 8))
        return NtFail("the explosion list's reader is not the game's (another mod, such as Leva or More Plasma Stratagems, moved the list)", NT_EXP_READER);
    if (!NtRead(g_ntBase + NT_EXP_BOUND, got, 5) || memcmp(got, bound, 5)) return NtFail("the explosion list's bound is not the game's", NT_EXP_BOUND);
    static uintptr_t exp[NT_EXP_NATIVE];
    if (!NtRead(g_ntBase + NT_EXP_TABLE, exp, sizeof exp)) return NtFail("the explosion list cannot be read", NT_EXP_TABLE);
    for (uint32_t i = 1; i < NT_EXP_NATIVE; i++)
    {
        uint32_t id;
        if (!AddressOk(exp[i], 152) || !NtRead(exp[i], &id, 4) || id != i) return NtFail("the explosion rows are not loaded yet", i);
    }
    uint32_t look = (uint32_t)s_arcBlast, arcs = (uint32_t)s_arcRow;
    uint8_t logic[152];
    if (!look || look >= NT_EXP_NATIVE || !NtRead(exp[look], g_ntExpRow, 152)) return NtFail("the blast's row cannot be read", look);
    if (!arcs || arcs >= NT_EXP_NATIVE || !NtRead(exp[arcs], logic, 152)) return NtFail("the arcs' row cannot be read", arcs);
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS64 nt;
    if (!NtRead(g_ntBase, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
        !NtRead(g_ntBase + (uint32_t)dos.e_lfanew, &nt, sizeof nt) || nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.NumberOfSections > 32)
        return NtFail("the game's headers cannot be read", 0);
    uint8_t* block = NtNear(65536);
    if (!block) return NtFail("no memory near the game for the list", 0);
    g_ntExp.block = (uintptr_t*)block;
    memcpy(g_ntExp.block, exp, sizeof exp);
    g_ntExp.npatch = g_ntExp.refs = g_ntExp.bounds = 0;
    uintptr_t sections = g_ntBase + (uint32_t)dos.e_lfanew + 24 + nt.FileHeader.SizeOfOptionalHeader;
    int ok = 1;
    for (unsigned s = 0; s < nt.FileHeader.NumberOfSections && ok; s++)
    {
        IMAGE_SECTION_HEADER sh;
        if (!NtRead(sections + s * sizeof sh, &sh, sizeof sh)) { ok = NtFail("a section header cannot be read", 0); break; }
        if (!(sh.Characteristics & IMAGE_SCN_MEM_EXECUTE) || sh.VirtualAddress >= 0x2111000u) continue;
        uint32_t length = sh.Misc.VirtualSize;
        if (!length || sh.VirtualAddress >= NT_IMAGE || length > NT_IMAGE - sh.VirtualAddress) { ok = NtFail("a code section is out of range", sh.VirtualAddress); break; }
        uint8_t* copy = (uint8_t*)VirtualAlloc(NULL, length, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!copy) { ok = NtFail("no memory to read the game's code", 0); break; }
        if (!NtCode(g_ntBase + sh.VirtualAddress, copy, length) || !NtScan(&g_ntExp, copy, sh.VirtualAddress, length, (uintptr_t)g_ntExp.block)) ok = 0;
        VirtualFree(copy, 0, MEM_RELEASE);
    }
    int reader1 = 0, bound1 = 0;
    for (unsigned i = 0; i < g_ntExp.npatch; i++)
    {
        reader1 |= g_ntExp.patch[i].at == g_ntBase + NT_EXP_READER + 4;
        bound1 |= g_ntExp.patch[i].at == g_ntBase + NT_EXP_BOUND + 1;
    }
    if (ok && (!reader1 || !bound1 || !g_ntExp.refs)) ok = NtFail("the explosion list's reader was not among the sites found", NT_EXP_READER);
    if (!ok) { VirtualFree(block, 0, MEM_RELEASE); g_ntExp.block = NULL; return 0; }
    /* her row: the blast's look, the arcs' damage and arc */
    g_ntExpId = NT_EXP_NATIVE;
    memcpy(g_ntExpRow, &g_ntExpId, 4);
    memcpy(g_ntExpRow + 4, logic + 4, 4);
    memcpy(g_ntExpRow + 120, logic + 120, 4);
    g_ntExp.block[g_ntExpId] = (uintptr_t)g_ntExpRow;
    if (!NtApply(&g_ntExp)) return 0;
    g_ntState = 1;
    uint32_t dmg, arc;
    memcpy(&dmg, logic + 4, 4); memcpy(&arc, logic + 120, 4);
    Log("arc blast: explosion %u (the look of %u, the damage %u and arc %u of %u: %u sites, %u bounds)", g_ntExpId, look, dmg, arc, arcs,
        g_ntExp.refs, g_ntExp.bounds);
    NtBolt();
    return 1;
}
static void NtTick(void)
{
    if (g_ntState == 1)
    {
        static int told;
        if (g_ntExp.block[g_ntExpId] != (uintptr_t)g_ntExpRow)
        {
            g_ntExp.block[g_ntExpId] = (uintptr_t)g_ntExpRow;
            if (!told++) Log("arc blast: the game rewrote the explosion list; her row is back");
        }
        return;
    }
    if (!s_arcBlast || g_ntState || !g_blastBolt || !g_boltReady || g_now < g_ntNext) return;
    g_ntNext = g_now + 1;
    if (NtInstall()) return;
    if (++g_ntTries == 1 || g_ntTries % 10 == 0) Log("arc blast: not yet (%s at +0x%llx); try %u", g_ntWhy, (unsigned long long)g_ntWhere, g_ntTries);
    if (g_ntTries >= 60) { g_ntState = -1; Log("arc blast: given up; her arcs stay the G-31's without the blast"); }
}

static Pulse g_retry[6];  /* blasts waiting for their packages (at: give up then) */
static int g_retryCount;
/* One of her blasts (an EMS pulse, an arc): it never touches or throws her, and the enemies it hits are marked */
static void Blast_(uint32_t row, const float at[3], const char* what)
{
    if (!row) return;
    /* her arcs: at most arc_rate a second, half a second's worth at once */
    if (row == (uint32_t)s_arcRow)
    {
        double cap = s_arcRate * 0.5 > 1 ? s_arcRate * 0.5 : 1;
        g_arcBudget += (g_now - g_arcBudgetAt) * s_arcRate;
        g_arcBudgetAt = g_now;
        if (g_arcBudget > cap) g_arcBudget = cap;
        if (g_arcBudget < 1) { g_arcsSkipped++; return; }
        g_arcBudget -= 1;
    }
    Pulse* p = &g_pulses[g_npulse++ % 12];
    memcpy(p->p, at, sizeof p->p);
    p->at = g_now;
    p->row = row;
    unsigned slot = g_ownNext++ % 8;
    memcpy(g_ownPos[slot], at, sizeof g_ownPos[slot]);
    atomic_store(&g_ownMs[slot], GetTickCount64());
    /* her arcs with the blast: her bullet, fired down from 3 m, bursts as her own explosion where it lands */
    if (row == (uint32_t)s_arcRow && g_ntState == 1 && g_blastBolt)
    {
        float from[3] = {at[0], at[1], at[2] + 3.0f}, down[3] = {0, 0, -1};
        int b = hf->spawn_projectile(self, g_blastBolt, from, down);
        static unsigned told;
        if (told++ < 3) Log("%s with the blast: answer %d", what, b);
        Act(what, b);
        return;
    }
    int r = hf->spawn_explosion(self, row, at);
    if (r == HF_ACT_WAITING && g_retryCount < 6)  /* its packages are loading: again next frames */
    {
        memcpy(g_retry[g_retryCount].p, at, sizeof g_retry[0].p);
        g_retry[g_retryCount].at = g_now + 3;
        g_retry[g_retryCount].row = row;
        g_retryCount++;
    }
    static unsigned noted;
    if (noted++ < 6) Log("%s (row %u): answer %d", what, row, r);
    if (r != HF_ACT_WAITING) Act(what, r);
}
static void Pulse_(const float at[3]) { Blast_((uint32_t)s_ems, at, "EMS pulse"); }

/* ---- enemies around her (Hunting Strategy's zap): spawned entities whose kind runs a behaviour and is no turret or
 * Helldiver, and anything her own hits landed on; dropped on death or when the entity id is reused ------------------ */
/* Stunned and held: the stun is renewed until `until` */
typedef struct Held { uint32_t entity; double until, next; } Held;
static Held g_held[128];
static void HoldFor(uint32_t entity, float seconds)
{
    int slot = 0;
    for (int i = 0; i < 128; i++)
    {
        if (g_held[i].entity == entity) { slot = i; break; }
        if (g_held[i].until < g_held[slot].until) slot = i;
    }
    g_held[slot].entity = entity;
    g_held[slot].until = g_now + seconds;
    g_held[slot].next = g_now;
}
static void HoldFrame(void)
{
    for (int i = 0; i < 128; i++)
    {
        Held* h = &g_held[i];
        if (!h->entity || g_now >= h->until || g_now < h->next) continue;
        h->next = g_now + s_hsStunEvery;
        if (hf->apply_status(self, h->entity, HF_STATUS_STUN_LARGE, 100.0f) == HF_ACT_DONE) g_stuns++;
    }
}
/* One zap: an arc on every enemy within hs_radius m of her, stunned and marked */
static void Zap(void)
{
    float me[3], p[3];
    if (!PlayerPose(me, NULL)) return;
    if (s_ems) Pulse_(me);
    uint8_t id[24];
    int found = 0, arcs = 0, known = 0;
    for (int i = 0; i < FOES; i++)
    {
        uint32_t e = g_foe[i];
        if (!e) continue;
        if (!Descriptor(e, id) || Quad(id) != g_foeRes[i]) { g_foe[i] = 0; continue; }
        known++;
        if (!UnitNode(Word(id + 12), 0, p) || Dist2(p, me) > s_hsRadius * s_hsRadius) continue;
        found++;
        if (arcs < s_hsArcsMax && s_arcRow) { p[2] += 1.0f; Blast_((uint32_t)s_arcRow, p, "zap"); arcs++; }
        HoldFor(e, s_hsStun);
        if (MarkEnemy(e) && g_castCi < s_hsCiCap) { g_castCi++; GainCi(1); }
    }
    static unsigned noted;
    if (noted++ < 12)
    {
        unsigned kinds = 0, foes = 0;
        for (int i = 0; i < 256; i++) { kinds += g_kinds[i].res != 0; foes += g_kinds[i].res && g_kinds[i].foe; }
        Log("zap: %d enem%s within %.0f m (%d arcs), of %d tracked; %u of %u spawned kinds taken as enemies", found, found == 1 ? "y" : "ies",
            (double)s_hsRadius, arcs, known, foes, kinds);
    }
}

/* Stuns (held for `seconds`) and marks every tracked enemy within `radius` m of a point; how many */
static int StunAround(const float at[3], float radius, float seconds)
{
    uint8_t id[24];
    float p[3];
    int n = 0;
    for (int i = 0; i < FOES; i++)
    {
        uint32_t e = g_foe[i];
        if (!e || !Descriptor(e, id) || Quad(id) != g_foeRes[i] || !UnitNode(Word(id + 12), 0, p) || Dist2(p, at) > radius * radius) continue;
        HoldFor(e, seconds);
        if (MarkEnemy(e)) GainCi(1);
        n++;
    }
    return n;
}

static void HuntingStrategy(void)
{
    if (g_now < g_cdUlt) return;
    g_castCi = 0;
    Zap();
    g_pulsesLeft = s_hsPulses;
    g_nextPulse = g_now + s_hsPulseStep;
    g_arcUntil = g_now + s_hsArc;
    int enhanced = SpendCi();
    if (enhanced) { g_stealthUntil = g_now + s_hsStealth; Stealth(1); }
    g_cdUlt = g_now + s_hsCd;
    g_casts[2]++;
    Log("Hunting Strategy: zap within %.0f m, %d more zaps, stuns %.0f s, Electric Arc %.0f s%s", (double)s_hsRadius, s_hsPulses,
        (double)s_hsStun, (double)s_hsArc, enhanced ? ", Stealth (3 CI)" : "");
}

/* ---- events ------------------------------------------------------------------------------------------------------ */
static int NearPulseR(const float at[3], double within, float radius)
{
    for (int i = 0; i < 12; i++)
        if (g_pulses[i].at > 0 && g_now - g_pulses[i].at <= within && Dist2(at, g_pulses[i].p) <= radius * radius) return 1;
    return 0;
}
static int NearPulse(const float at[3], double within)
{
    for (int i = 0; i < 12; i++)
        if (g_pulses[i].at > 0 && g_now - g_pulses[i].at <= within && Dist2(at, g_pulses[i].p) <= 64.0f) return 1;
    return 0;
}

/* A hit on her (HellForge's damage event; kitbase/kb_damage.h routes it here) */
static void DamageTaken(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    /* King's Authority: her own blasts spare her (the EMS field deals no damage: its stun rides on the hit) */
    if (d->kind == 4 && NearPulse(d->position, 0.6)) { e->cancel = 1; g_selfSpared++; return; }
    /* her arcs land as element-2 (or 3) hits of kind 0, 1 or 3, a little later and further out than the burst */
    if ((d->element == 2 || d->element == 3) && (d->kind == 0 || d->kind == 1 || d->kind == 3) && NearPulseR(d->position, 1.5, 35))
    { e->cancel = 1; g_selfSpared++; return; }
    if (d->amount == 0) return;
    /* Leaping Pursuit's window: no fall or landing damage (impact or flat, from nothing) */
    if (g_now < g_drUntil && (d->kind == 5 || d->kind == 7) && !NetOk(d->dealer) && !NetOk(d->owner)) { e->cancel = 1; return; }
    if (g_now < g_drUntil && d->amount > 0)
    {
        double a = floor(d->amount * s_lpTaken + 0.5);
        if (a < 1) { e->cancel = 1; return; }
        d->amount = (int32_t)a;
    }
}
/* Any other hit: hers on enemies, and anyone's on what her skills mark */
static void DamageDealt(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    /* enemies her pulses hit are marked */
    if (d->kind == 4 && NearPulse(d->position, 0.6) && MarkEnemy(d->target))
        if (g_castCi < s_hsCiCap) { g_castCi++; GainCi(1); }
    if (d->amount == 0) return;
    int mine = IsShot(d);
    if (mine)
    {
        NoteTarget(d);
        /* Wild Extinction's charged hits */
        if ((!g_chargeTarget || d->target == g_chargeTarget) && g_chargeHits > 0 && g_now <= g_chargeUntil && d->amount > 0)
        {
            if (g_now - g_lastArc >= s_arcStep) { g_lastArc = g_now; Blast_((uint32_t)s_arcRow, d->position, "arc"); }
            ScaleDamage(d, g_chargeMult);
            g_chargeHits--;
            g_boosted++;
            if (g_chargeStun)
            {
                g_chargeStun = 0;
                Stun(d->target, g_chargeMult >= s_weMultEnh && s_weMultEnh > s_weMult ? HF_STATUS_STUN_LARGE : HF_STATUS_STUN_MEDIUM);
                if (MarkEnemy(d->target)) GainCi(1);
            }
        }
        /* Electric Arc */
        if (g_now < g_arcUntil)
        {
            if (MarkEnemy(d->target)) GainCi(1);
            Mark* m = MarkOf(d->target);
            if (m && g_now - m->stunnedAt >= 1.0) { m->stunnedAt = g_now; Stun(d->target, HF_STATUS_STUN_SMALL); }
        }
    }
    /* Conductivity: marked enemies take more */
    if (d->amount > 0 && MarkOf(d->target))
    {
        ScaleDamage(d, s_markMult);
    }
}

static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfKillEvent* k = (const HfKillEvent*)info;
    DropFoe(k->victim);
    Mark* m = MarkOf(k->victim);
    if (m) { m->until = 0; GainCi(1); }
    if (k->victim == g_lastTarget) g_lastTarget = 0;
    if (k->victim == g_chargeTarget) g_chargeHits = 0;
}

static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    if (s->resource != AVATAR) { if (FoeKind(s->resource)) AddFoe(s->entity, s->resource); return; }
    if (!s->authority) return;
    g_spawnEntity[g_spawnNext % 8] = s->entity;
    g_spawnNet[g_spawnNext % 8] = s->network;
    g_spawnNext++;
    LearnNet();
}

static void OnPlayer(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    if (!p->local) return;
    if (info->kind == HF_ON_PLAYER_SPAWN)
    {
        if (p->entity && p->entity != atomic_load(&g_avatar)) { atomic_store(&g_avatar, p->entity); g_avatarNet = 0; }
        g_alive = 1;
        LearnNet();
    }
    else g_alive = 0;
}

/* ---- her body: more health and stamina, and the price of running dry --------------------------------------------- */

static void Reset(const char* why)
{
    if (g_casts[0] || g_casts[1] || g_casts[2])
        Log("%s: casts %u / %u / %u, %u marked, %u stuns, %u charged hits, %u own pulses spared her, %u knockdowns stopped",
            why, g_casts[0], g_casts[1], g_casts[2], g_marked, g_stuns, g_boosted, g_selfSpared, g_knockdowns);
    Speed(0);
    Stealth(0);
    Body(!strcmp(why, "mission start"));
    /* the G-31 Arc grenade's package: its detonation (157) plays its blast and sound only once that is in */
    if (!strcmp(why, "mission start") && s_arcRow == 157 && !hf->load_package(self, UINT64_C(0xDDCEA5FDD4071767)))
        Log("the G-31 Arc grenade's package could not be loaded ahead: its arcs may stay silent");
    /* the GL-52 De-Escalator's package: its burst (41) plays its blast and sound only once that is in */
    if (!strcmp(why, "mission start") && (s_arcRow == 41 || s_arcBlast == 41) && !hf->load_package(self, UINT64_C(0x4FEE9CC13C0B4CEC)))
        Log("the GL-52 De-Escalator's package could not be loaded ahead: its bursts may stay silent");
    if (g_arcsSkipped) Log("%s: %u arcs skipped over arc_rate", why, g_arcsSkipped);
    if (!strcmp(why, "mission start")) NtBolt();
    g_arcsSkipped = 0;
    if (!strcmp(why, "mission start") && s_hellbomb && s_hellbombRow == 125)
        if (!hf->load_package(self, UINT64_C(0x346D785D6CA13FF4))) Log("the Hellbomb's package could not be loaded ahead");  /* loadout/hellbomb_backpack */
    g_boomPending = 0;
    g_boomArmed = 0;
    memset(g_marks, 0, sizeof g_marks);
    memset(g_held, 0, sizeof g_held);
    memset(g_pulses, 0, sizeof g_pulses);
    memset(g_casts, 0, sizeof g_casts);
    g_marked = g_stuns = g_boosted = g_selfSpared = g_knockdowns = 0;
    g_ci = 0;
    g_cdWild = g_cdLeap = g_cdUlt = 0;
    g_lastTarget = g_chargeTarget = 0;
    g_chargeHits = 0;
    g_pulsesLeft = 0;
    g_retryCount = 0;
    g_arcUntil = g_stealthUntil = g_drUntil = g_speedUntil = g_forwardAt = g_landAt = 0;
    g_leap = 0;
    atomic_store(&g_shieldUntilMs, 0);
}

/* ---- the knockdown window: hooks on the game's hit code (as Primed Sure Footed) -------------------------------------- */

static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gameSize = 0, exeSize = 0;
    uintptr_t g = hf->module_base("game.dll", &gameSize), x = hf->module_base("helldivers2.exe", &exeSize);
    if (!g || !x || gameSize != 74727424u || exeSize != 60719104u) { Log("REFUSED: the game's modules are not build " BUILD "'s: no aim, no knockdown window"); return; }
    game = g;
    exe = x;
    if (!HitsInstall("knockdown window")) return;
    if (hf->code_matches(game + 0x5A2BB0, "48 89 5c 24 08 48 89 6c 24 10 56 57 41 54 41 56 41 57 48 83 ec 40") &&
        hf->code_matches(game + 0x4A7550, "48 89 5c 24 18 57 48 83 ec 20 3b 15 ?? ?? ?? ?? 48 8b 1d") &&
        hf->code_matches(exe + 0x79F860, "33 c0 4c 8d 05 c7 b2 01 02 0f 1f 80 00 00 00 00 49 39 0c c0 74 0c ff c0 83 f8 04 75 f3") &&
        hf->code_matches(exe + 0x7F4590, "48 8b c4 f3 0f 11 58 20 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48"))
    {
        g_setPosition = (SetVecFn)(game + 0x5A2BB0);
        g_setVelocity = (SetVecFn)(game + 0x4A7550);
        g_worldId = (WorldFn)(exe + 0x79F860);
        g_raycast = (RayFn)(exe + 0x7F4590);
        Log("teleport ready (the warp's position setter, the velocity setter and the raycast)");
    }
    else Log("teleport off: its functions are not the reviewed code");
    g_staminaOk = hf->code_matches(game + 0xA50E2D, "f3 42 0f 11 8c 11 54 78 54 00");
    Log(g_staminaOk ? "stamina readable (the game's stamina write is the reviewed code)" : "stamina off: its code is not the reviewed one");
}

/* her blast bullet: its own hit deals nothing; it bursts as the G-31's detonation until her explosion is in */
static void ProjectilesReady(HfMod* mod, HfEvent event, void* user)
{
    HfDamageInfo* dmg = g_blastBoltDamage ? (HfDamageInfo*)hf->damage_row(g_blastBoltDamage) : NULL;
    if (dmg) { dmg->standard_damage = 0; dmg->durable_damage = 0; dmg->demolition = 0; dmg->stagger = 0; dmg->push_force = 0; }
    else g_blastBolt = 0;
    g_boltReady = 1;
    NtBolt();
    Log("arc blast: her bullet %u %s", g_blastBolt, g_blastBolt ? "is ready (its own hit deals nothing)" : "is unavailable: her arcs stay without the blast");
}
static void EntitiesReady(HfMod* mod, HfEvent event, void* user)
{
    if (s_ems) Log("EMS explosion row %d under her zaps (from the ini)", s_ems);

}

/* ---- the HUD state (read by the Lenna HUD addon through LuaJIT's ffi) --------------------------------------------- */
typedef struct LennaHud
{
    uint32_t version, in_mission, ci, ci_max;
    float cd[3], cd_max[3];          /* seconds left, full cooldowns: Wild Extinction, Leaping Pursuit, Hunting Strategy */
    float charge, arc, stealth, shield;  /* seconds left of each effect (charge: its window) */
    uint32_t charge_hits, marked, keys[3];
    char key_label[3][24];               /* version 2: the keys as written ("Shift+Q") */
    float stamina;                       /* version 3: her stamina 0..1 (-1: unknown) */
    uint32_t hellbomb;                   /* version 3: 1 when running dry sets off the Hellbomb */
} LennaHud;
static LennaHud g_hud = {3};
HF_MOD_EXPORT LennaHud* lenna_hud_state(void)
{
    g_hud.in_mission = (uint32_t)(g_inMission && g_alive);
    g_hud.ci = (uint32_t)g_ci;
    g_hud.ci_max = (uint32_t)s_ciMax;
    double r[3] = {g_cdWild, g_cdLeap, g_cdUlt};
    float m[3] = {s_weCd, s_lpCd, s_hsCd};
    for (int i = 0; i < 3; i++) { g_hud.cd[i] = (float)fmax(0, r[i] - g_now); g_hud.cd_max[i] = m[i]; }
    g_hud.charge = g_chargeHits > 0 ? (float)fmax(0, g_chargeUntil - g_now) : 0;
    g_hud.charge_hits = (uint32_t)(g_chargeHits > 0 && g_chargeHits < 100000 && g_now <= g_chargeUntil ? g_chargeHits : 0);
    g_hud.arc = (float)fmax(0, g_arcUntil - g_now);
    g_hud.stealth = (float)fmax(0, g_stealthUntil - g_now);
    g_hud.shield = (float)fmax(0, g_drUntil - g_now);
    unsigned n = 0;
    for (int i = 0; i < 64; i++) n += g_marks[i].until > g_now;
    g_hud.marked = n;
    for (int i = 0; i < 3; i++)
    {
        g_hud.keys[i] = (uint32_t)s_keys[i].vk;
        memcpy(g_hud.key_label[i], s_keys[i].label, sizeof g_hud.key_label[i]);
    }
    g_hud.stamina = g_inMission && g_alive ? StaminaNow() : -1;
    g_hud.hellbomb = (uint32_t)(s_hellbomb != 0);
    return &g_hud;
}

/* ---- the frame ------------------------------------------------------------------------------------------------- */

/* ---- in-game key bindings: the HUD registers her three skills with Mod Bindings Menu (Vanilla Plus Megapack: the MODS
 * tab of the binding pages) and passes each press here (and a heartbeat twice a second); the .ini's keys keep working */
static _Atomic unsigned long long g_bindMs;
static atomic_int g_bindPress;
HF_MOD_EXPORT int lenna_input(unsigned version, int skill)
{
    if (version != 1) return 0;
    atomic_store(&g_bindMs, GetTickCount64());
    if (skill >= 1 && skill <= 3) atomic_fetch_or(&g_bindPress, 1 << (skill - 1));
    return 1;
}
/* A skill's .ini key for the binding's default: its virtual-key code and modifiers (1 Shift, 2 Ctrl, 4 Alt) */
HF_MOD_EXPORT int lenna_key(unsigned version, int skill, int* vk, int* mods)
{
    if (version != 1 || skill < 1 || skill > 3 || !vk || !mods) return 0;
    *vk = s_keys[skill - 1].vk;
    *mods = s_keys[skill - 1].mods;
    return 1;
}
static void Tick(HfMod* mod, float dt, void* user)
{
    g_now += dt > 0 && dt < 1 ? dt : 0;
    int inMission = hf->game_state(self) == HF_STATE_MISSION;
    if (inMission != g_inMission) { Reset(inMission ? "mission start" : "mission end"); g_inMission = inMission; }
    NtTick();
    if (!g_inMission) return;
    /* keys (edges) */
    MyItems();
    static int prev[3];
    int focused = Focused();
    int mods = focused ? ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? KM_SHIFT : 0) | ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? KM_CTRL : 0) |
                         ((GetAsyncKeyState(VK_MENU) & 0x8000) ? KM_ALT : 0) : 0;
    /* the keys from Mod Bindings Menu while the HUD passes them, else the .ini's */
    int bound = GetTickCount64() - atomic_load(&g_bindMs) < 2000, presses = atomic_exchange(&g_bindPress, 0);
    static int wasBound = -1;
    if (bound != wasBound) { wasBound = bound; Log("keys: %s", bound ? "the .ini's and Mod Bindings Menu's (the binding pages' MODS tab)" : "the .ini's"); }
    for (int i = 0; i < 3; i++)
    {
        /* a modifier bound alone ignores its own bit */
        int own = s_keys[i].vk == VK_SHIFT ? KM_SHIFT : s_keys[i].vk == VK_CONTROL ? KM_CTRL : s_keys[i].vk == VK_MENU ? KM_ALT : 0;
        /* both the .ini's key and the binding fire it; one press within 0.3 s of the other is the same press */
        int down = focused && (GetAsyncKeyState(s_keys[i].vk) & 0x8000) != 0 && (mods & ~own) == s_keys[i].mods;
        static unsigned long long fired[3];
        int press = (down && !prev[i]) || (presses >> i & 1);
        if (press && GetTickCount64() - fired[i] > 300 && g_alive && atomic_load(&g_avatar) && (fired[i] = GetTickCount64(), 1))
        {
            if (i == 0) WildExtinction();
            else if (i == 1) LeapingPursuit();
            else HuntingStrategy();
        }
        prev[i] = down;
    }
    /* the last enemy she hit, as the hit code saw it */
    static unsigned long long seenMs;
    unsigned long long ms = atomic_load(&g_hookMs);
    if (ms != seenMs)
    {
        seenMs = ms;
        uint32_t t = atomic_load(&g_hookTarget);
        if (t)
        {
            static unsigned noted;
            if (noted++ < 3) Log("she hit entity %u (the game's hit code)", t);
            g_lastTarget = t;
            memcpy(g_lastPos, g_hookPos, sizeof g_lastPos);
            g_lastAt = g_now;
        }
    }
    /* pulses that waited for their packages */
    for (int i = 0; i < g_retryCount; i++)
    {
        int r = hf->spawn_explosion(self, g_retry[i].row, g_retry[i].p);
        if (r != HF_ACT_WAITING || g_now > g_retry[i].at)
        {
            Act("a blast (after its packages)", r);
            unsigned slot = g_ownNext++ % 8;
            memcpy(g_ownPos[slot], g_retry[i].p, sizeof g_ownPos[slot]);
            atomic_store(&g_ownMs[slot], GetTickCount64());
            g_retry[i] = g_retry[--g_retryCount];
            i--;
        }
    }
    LeapFrame();
    HoldFrame();
    Exhaustion();
    /* landing: stun the last enemy she shot, if near */
    if (g_landAt > 0 && g_now >= g_landAt)
    {
        g_landAt = 0;
        float pos[3];
        if (TargetFresh(6) && PlayerPose(pos, NULL) && Dist2(pos, g_lastPos) <= s_lpReach * s_lpReach)
        {
            Stun(g_lastTarget, HF_STATUS_STUN_MEDIUM);
            if (MarkEnemy(g_lastTarget)) GainCi(1);
        }
    }
    if (g_speedOn && g_now >= g_speedUntil) Speed(0);
    if (g_stealthOn && g_now >= g_stealthUntil) Stealth(0);
    /* Voltage pulses */
    if (g_pulsesLeft > 0 && g_now >= g_nextPulse)
    {
        g_pulsesLeft--;
        g_nextPulse = g_now + s_hsPulseStep;
        Zap();
    }
    unsigned own = atomic_exchange(&g_ownStopped, 0);
    if (own) { g_selfSpared += own; static unsigned noted; if (noted++ < 5) Log("King's Authority: %u hit(s) of her own blasts cleared on her", own); }
    unsigned kd = atomic_exchange(&g_kdStopped, 0);
    if (kd) { g_knockdowns += kd; Log("Leaping Pursuit: %u knockdown(s) stopped", kd); }
}

/* ---- the Mod Options Menu (through the Lenna HUD addon): read and set a setting by its lenna.ini name --------------- */
static const KbOption kOptions[] = {
    {"ci_max", NULL, &s_ciMax}, {"we_cooldown", &s_weCd, NULL}, {"we_window", &s_weWindow, NULL}, {"we_mult", &s_weMult, NULL},
    {"we_mult_enhanced", &s_weMultEnh, NULL}, {"lp_cooldown", &s_lpCd, NULL}, {"lp_distance", &s_lpDistance, NULL},
    {"lp_window", &s_lpWindow, NULL}, {"lp_damage_taken", &s_lpTaken, NULL}, {"lp_heal", &s_lpHeal, NULL}, {"lp_speed", &s_lpSpeed, NULL},
    {"lp_speed_seconds", &s_lpSpeedSeconds, NULL}, {"lp_stun", &s_lpStun, NULL}, {"lp_burst_radius", &s_lpBurstRadius, NULL},
    {"hs_cooldown", &s_hsCd, NULL}, {"hs_radius", &s_hsRadius, NULL}, {"hs_pulses", NULL, &s_hsPulses}, {"hs_stun", &s_hsStun, NULL},
    {"hs_arc", &s_hsArc, NULL}, {"hs_stealth", &s_hsStealth, NULL}, {"mark_mult", &s_markMult, NULL}, {"mark_seconds", &s_markSeconds, NULL},
    {"hp_mult", &s_hpMult, NULL}, {"stamina_mult", &s_staminaMult, NULL}, {"hellbomb_on_exhaustion", NULL, &s_hellbomb},
};
/* After Mod Options Menu sets one (changed: its value moved) */
static void OptionAfter(const char* name, int changed)
{
    if (changed && g_ci > s_ciMax) g_ci = s_ciMax;
}
HF_MOD_EXPORT int lenna_option_get(const char* name, double* out)
{
    return OptionGet(name, out);
}
HF_MOD_EXPORT int lenna_option_set(const char* name, double value)
{
    return OptionSet(name, value);
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    kb_ini = "lenna.ini";
    kb_key_names[0] = "key_wild";
    kb_key_names[1] = "key_leap";
    kb_key_names[2] = "key_ult";
    KB_SETTINGS(kSettings);
    KB_OPTIONS(kOptions);
    kb_option_after = OptionAfter;
    kb_damage_taken = DamageTaken;
    kb_damage_dealt = DamageDealt;
    if (!api || api->version < 13 || api->size < offsetof(HfApi, code_matches) + sizeof api->code_matches) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    LoadSettings();
    api->log(mod, "Lenna %s: keys %s / %s / %s (Wild Extinction, Leaping Pursuit, Hunting Strategy); CI up to %d", VERSION, s_keys[0].label,
        s_keys[1].label, s_keys[2].label, s_ciMax);
    if (s_badKey) api->log(mod, "lenna.ini: a key could not be read (bits %d: wild, leap, ult); its default stays", s_badKey);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL) || api->on(mod, HF_EVENT_ENTITIES_READY, EntitiesReady, NULL)) return 1;
    if (s_arcBlast && api->size >= offsetof(HfApi, projectile_row) + sizeof api->projectile_row)
    {
        HfDamage nothing = {sizeof nothing};
        nothing.base = 108;
        nothing.name = "yui/lenna/blast_bullet_damage";
        nothing.scale = 1;
        g_blastBoltDamage = api->add_damage(mod, &nothing);
        HfProjectile bolt = {sizeof bolt};
        bolt.base = HF_PROJECTILE_AR23_LIBERATOR;
        bolt.name = "yui/lenna/blast_bullet";
        bolt.damage = g_blastBoltDamage;
        g_blastBolt = g_blastBoltDamage ? api->add_projectile(mod, &bolt) : 0;
        if (!g_blastBolt || api->on(mod, HF_EVENT_PROJECTILES_READY, ProjectilesReady, NULL))
        { g_blastBolt = 0; api->log(mod, "arc blast: her bullet could not be added: her arcs stay without the blast"); }
    }
    /* registered: from here the DLL stays loaded; a failure below leaves it inert */
    int ok = api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) &&
             api->on_game_thread(mod, Tick, NULL);
    if (!ok) api->log(mod, "events or the game thread are unavailable: Lenna stays off");
    return 0;
}
