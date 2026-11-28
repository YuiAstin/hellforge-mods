/* Andoris (yui.andoris): Andoris's kit from Girls' Frontline 2 as abilities on keys, for your own Helldiver (solo or
 * host). A Bulwark: she takes the hits, mends herself and sets enemies up for Electric damage. Design:
 * docs/andoris-design.md.
 *
 * 0.1.0 (first build; Z played 2026-11-29: the Electric shot, its arc and Electrocharge landed, x2.1 kept): her Z and her
 *   passive.
 * 0.2.0 (untested): Tender Offensive (key_s2, default X): a stick bug beside her (the Escort Probe's trooper copy: bug_health,
 *   armour bug_armour), to_max at most (one more takes the oldest away), walking with her; the enemies on her switch to
 *   it every taunt_every s (the probe's taunt); its hits on a Helldiver or another stick bug are cancelled; hits on it are
 *   eased by her Dullness. Destroyed: Tender Offensive's cooldown cut by to_refund s. Momentum Support: her Electric shot
 *   gains ms_mult for each stick bug within ms_range m of the enemy, an arc, and Negative Charge on it (nc_seconds); her
 *   Electric shot on a Negative-Charged enemy arcs to every other one (nc_arcs). Reinforced Program (C) comes next.
 *   Simplified Decision (key_s1, default Z): her next shot that lands on an enemy is an Electric shot: it deals
 *     sd_mult times more (1.1: +110%), an arc goes off on the hit (arc_explosion: the G-31 Arc grenade's), and the enemy
 *     is Electrocharged for ec_seconds s. Cooldown sd_cooldown s from the shot.
 *   Electrocharge: her hits on it heal her ec_heal of the damage they deal and deal ec_mult more; every ec_spread s it
 *     spreads to the enemies within ec_radius m of it.
 *   Delayed Response (passive):
 *     Self-Repair: once a life, armed sr_arm s after she lands: a hit that would kill her is cancelled and she is mended
 *       to sr_health of her health.
 *     Dullness: each hit on her gives a stack (dull_max at most), each dull_per less damage taken; one is lost every
 *       dull_decay s without a hit.
 *     Taking damage heals: hurt_heal of her health back after a hit, at most once every hurt_every s.
 *   T-Doll body: hp_mult health, stamina_mult sprint, and a Hellbomb on her if her stamina ever runs dry.
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

#define VERSION "0.2.0"
#define BUILD "25480438"

#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"
#include "kitbase/kb_world.h"
#include "kitbase/kb_spawn.h"
#include "kitbase/kb_body.h"
#include "kitbase/kb_hits.h"
#include "kitbase/kb_damage.h"
#include "kitbase/kb_guard.h"
#include "kitbase/kb_settings.h"
#include "kitbase/kb_options.h"
#include "kitbase/kb_roster.h"

KB_ROSTER_EXPORTS(andoris)
KB_OPTIONS_EXPORTS(andoris)

/* ---- settings ------------------------------------------------------------------------------------------------- */
static float s_sdCd = 6, s_sdMult = 1.1f;
static int s_arcRow = 157;
static float s_ecSeconds = 10, s_ecHeal = 0.2f, s_ecMult = 0.15f, s_ecSpread = 5, s_ecRadius = 3;
static float s_srArm = 30, s_srHealth = 0.6f;
static int s_srOn = 1, s_dullMax = 8;
static float s_dullPer = 0.03f, s_dullDecay = 2, s_hurtHeal = 0.05f, s_hurtEvery = 1;
static float s_toCd = 20, s_toRefund = 10, s_bugFollow = 5, s_msRange = 25, s_msMult = 1, s_ncSeconds = 10, s_tauntEvery = 3, s_tauntRadius = 40;
static int s_toMax = 3, s_bugHealth = 600, s_bugArmour = 4, s_ncArcs = 1;
static int s_optionsKey = 0x78;   /* options_key: the key that opens the options screen (120: F9; 0: none) */
static const KbSetting kSettings[] = {
    KB_F("sd_cooldown", s_sdCd, 0, 600), KB_F("sd_mult", s_sdMult, 0, 20), KB_I("arc_explosion", s_arcRow, 0, 100000),
    KB_F("ec_seconds", s_ecSeconds, 0, 120), KB_F("ec_heal", s_ecHeal, 0, 5), KB_F("ec_mult", s_ecMult, 0, 5), KB_F("ec_spread", s_ecSpread, 0.5, 60),
    KB_F("ec_radius", s_ecRadius, 0, 30), KB_I("sr_on", s_srOn, 0, 1), KB_F("sr_arm", s_srArm, 0, 600), KB_F("sr_health", s_srHealth, 0.05, 1),
    KB_I("dull_max", s_dullMax, 0, 50), KB_F("dull_per", s_dullPer, 0, 0.2), KB_F("dull_decay", s_dullDecay, 0.1, 60),
    KB_F("hurt_heal", s_hurtHeal, 0, 1), KB_F("hurt_every", s_hurtEvery, 0, 60),
    KB_F("to_cooldown", s_toCd, 0, 600), KB_I("to_max", s_toMax, 1, 3), KB_F("to_refund", s_toRefund, 0, 600), KB_I("bug_health", s_bugHealth, 1, 100000),
    KB_I("bug_armour", s_bugArmour, 0, 10), KB_F("bug_follow", s_bugFollow, 2, 50), KB_F("taunt_every", s_tauntEvery, 0, 60), KB_F("taunt_radius", s_tauntRadius, 0, 200),
    KB_F("ms_range", s_msRange, 0, 200), KB_F("ms_mult", s_msMult, 0, 20), KB_F("nc_seconds", s_ncSeconds, 0, 120), KB_I("nc_arcs", s_ncArcs, 0, 1),
    KB_F("hp_mult", s_hpMult, 0.1, 10), KB_F("stamina_mult", s_staminaMult, 0.1, 20), KB_I("hellbomb_on_exhaustion", s_hellbomb, 0, 1),
    KB_I("hellbomb_explosion", s_hellbombRow, 0, 100000), KB_F("hellbomb_hold", s_hellbombHold, 0, 10), KB_I("options_key", s_optionsKey, 0, 255),
};

/* The default andoris.ini: tools/inigen.py writes the file from this text (the mod never writes it) */
static __attribute__((unused)) void DefaultIni(FILE* f)
{
fprintf(f, "; Andoris %s. Keys: a letter, digit, F1-F24, Mouse3, Mouse4 or Mouse5, with Shift+, Ctrl+ or Alt+ in front for a\n"
               "; combo (Shift+Q), or Shift, Ctrl or Alt alone. The game still sees the keys too, so pick ones it leaves alone.\n"
               "key_s1 = %s\nkey_s2 = %s\nkey_s3 = %s\n"
               "; Simplified Decision: cooldown; her next shot on an enemy deals sd_mult more (1.1: +110%%) and sets off\n"
               "; arc_explosion on it (157: the G-31 Arc grenade's; 0: none).\nsd_cooldown = %g\nsd_mult = %g\narc_explosion = %d\n"
               "; Electrocharge: how long it lasts; her hits on it heal her ec_heal of their damage and deal ec_mult more; it\n"
               "; spreads every ec_spread s to the enemies within ec_radius m.\n"
               "ec_seconds = %g\nec_heal = %g\nec_mult = %g\nec_spread = %g\nec_radius = %g\n"
               "; Self-Repair (sr_on 1): once a life, sr_arm s after she lands, a hit that would kill her is cancelled and she\n"
               "; is mended to sr_health of her health.\nsr_on = %d\nsr_arm = %g\nsr_health = %g\n"
               "; Dullness: a stack for each hit on her (dull_max at most), each dull_per less damage taken; one lost every\n"
               "; dull_decay s without a hit. Taking damage heals: hurt_heal of her health after a hit, once each hurt_every s.\n"
               "dull_max = %d\ndull_per = %g\ndull_decay = %g\nhurt_heal = %g\nhurt_every = %g\n"
               "; Tender Offensive: a stick bug beside her that walks with her (to_max at most: one more takes the oldest away);\n"
               "; cooldown to_cooldown s, cut by to_refund s when one is destroyed. Its body: bug_health health, armour bug_armour\n"
               "; (from the next start of the game). It is sent to her when more than bug_follow m away. Every taunt_every s the\n"
               "; enemies within taunt_radius m of a stick bug that target her switch to it (0: never).\n"
               "to_cooldown = %g\nto_max = %d\nto_refund = %g\nbug_health = %d\nbug_armour = %d\nbug_follow = %g\ntaunt_every = %g\ntaunt_radius = %g\n"
               "; Momentum Support: when her Electric shot lands, each stick bug within ms_range m of the enemy strikes it too (ms_mult\n"
               "; of her shot's damage, an arc) and gives it Negative Charge for nc_seconds s. With nc_arcs 1, her Electric shot on a\n"
               "; Negative-Charged enemy sends an arc to every other Negative-Charged enemy.\n"
               "ms_range = %g\nms_mult = %g\nnc_seconds = %g\nnc_arcs = %d\n"
               "; The T-Doll's body: hp_mult times a Helldiver's health, stamina_mult times her sprint before the bar empties.\n"
               "hp_mult = %g\nstamina_mult = %g\n"
               "; If her stamina ever runs out, hellbomb_on_exhaustion (1) sets off hellbomb_explosion (125: the B-100 Portable\n"
               "; Hellbomb's) on her, once the bar has stayed empty hellbomb_hold s (it flickers to 0 at times).\n"
               "hellbomb_on_exhaustion = %d\nhellbomb_explosion = %d\nhellbomb_hold = %g\n"
               "; The options screen's key (120: F9; 0: none).\noptions_key = %d\n",
        VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, (double)s_sdCd, (double)s_sdMult, s_arcRow,
        (double)s_ecSeconds, (double)s_ecHeal, (double)s_ecMult, (double)s_ecSpread, (double)s_ecRadius,
        s_srOn, (double)s_srArm, (double)s_srHealth, s_dullMax, (double)s_dullPer, (double)s_dullDecay, (double)s_hurtHeal, (double)s_hurtEvery,
        (double)s_toCd, s_toMax, (double)s_toRefund, s_bugHealth, s_bugArmour, (double)s_bugFollow, (double)s_tauntEvery, (double)s_tauntRadius,
        (double)s_msRange, (double)s_msMult, (double)s_ncSeconds, s_ncArcs,
        (double)s_hpMult, (double)s_staminaMult, s_hellbomb, s_hellbombRow, (double)s_hellbombHold, s_optionsKey);
}

/* ---- reads: health, positions ------------------------------------------------------------------------------------ */
/* (HealthOf, Alive: kitbase/kb_world.h) */
static int HitPos(uint32_t e, float out[3])
{
    uint8_t d[24];
    if (!Descriptor(e, d) || !UnitNode(Word(d + 12), 0, out)) return 0;
    out[2] += 1.0f;
    return 1;
}

/* ---- state ---------------------------------------------------------------------------------------------------- */
static double g_cdSd;                    /* Simplified Decision ready at */
static int g_armed;                      /* her next shot is the Electric shot */
static int g_dull;                       /* Dullness stacks */
static double g_dullAt, g_hurtHealAt, g_landedAt;
static int g_repaired;                   /* Self-Repair spent this life */
static int32_t g_hpMax;                  /* her health at its highest this life (her maximum, as near as is read) */
static unsigned g_casts[3], g_ecHits, g_spreads, g_dullHits, g_healsHurt, g_healsEc, g_selfSpared, g_repairs;
static double g_healed;
typedef struct Charge { uint32_t e; double until, spreadAt; } Charge;
#define CHARGES 64
static Charge g_ec[CHARGES];
typedef struct OwnArc { float p[3]; double at; } OwnArc;
static OwnArc g_arcs[8];
static unsigned g_arcNext;
static struct { float p[3]; double until; } g_arcRetry[6];
static int g_arcRetryN;

static Charge* ChargeOf(uint32_t e)
{
    for (int i = 0; i < CHARGES; i++) if (g_ec[i].e == e && g_ec[i].until > g_now) return &g_ec[i];
    return NULL;
}
static int Electrocharge(uint32_t e)
{
    if (!e || e == atomic_load(&g_avatar)) return 0;
    Charge* c = ChargeOf(e);
    if (!c)
        for (int i = 0; i < CHARGES; i++)
            if (g_ec[i].until <= g_now) { c = &g_ec[i]; c->e = e; c->spreadAt = g_now + s_ecSpread; break; }
    if (!c) return 0;
    c->until = g_now + s_ecSeconds;
    return 1;
}
static int Charged(void) { int n = 0; for (int i = 0; i < CHARGES; i++) n += g_ec[i].until > g_now; return n; }

/* her arcs: the explosion at a point, after its packages if need be; where and when, so they spare her */
static void ArcAt(const float at[3])
{
    if (s_arcRow <= 0) return;
    int r = hf->spawn_explosion(self, (uint32_t)s_arcRow, at);
    if (r == HF_ACT_WAITING && g_arcRetryN < 6) { memcpy(g_arcRetry[g_arcRetryN].p, at, 12); g_arcRetry[g_arcRetryN++].until = g_now + 2; return; }
    if (!Act("Simplified Decision's arc", r)) return;
    OwnArc* a = &g_arcs[g_arcNext++ % 8];
    memcpy(a->p, at, 12);
    a->at = g_now;
}
static void ArcRetryFrame(void)
{
    for (int i = 0; i < g_arcRetryN; i++)
    {
        int r = hf->spawn_explosion(self, (uint32_t)s_arcRow, g_arcRetry[i].p);
        if (r != HF_ACT_WAITING || g_now > g_arcRetry[i].until)
        {
            if (Act("Simplified Decision's arc (after its packages)", r)) { OwnArc* a = &g_arcs[g_arcNext++ % 8]; memcpy(a->p, g_arcRetry[i].p, 12); a->at = g_now; }
            g_arcRetry[i] = g_arcRetry[--g_arcRetryN];
            i--;
        }
    }
}
static int NearOwnArc(const float at[3])
{
    for (int i = 0; i < 8; i++) if (g_arcs[i].at > 0 && g_now - g_arcs[i].at <= 1.5 && Dist2(at, g_arcs[i].p) <= 100.0f) return 1;
    return 0;
}

/* ---- the abilities ---------------------------------------------------------------------------------------------- */
static void SimplifiedDecision(void)
{
    if (g_armed) { Log("Simplified Decision: already readied (her next shot)"); return; }
    if (g_now < g_cdSd) { Log("Simplified Decision: %.0f s of cooldown left", g_cdSd - g_now); return; }
    g_armed = 1;
    g_casts[0]++;
    Log("Simplified Decision: readied (her next shot on an enemy)");
}
static void NotYet(int i)
{
    static double told[3];
    if (g_now < told[i]) return;
    told[i] = g_now + 5;
    Log("Reinforced Program: not in this build yet (Andoris %s: Simplified Decision, Tender Offensive and her passive)", VERSION);
}

/* ---- Tender Offensive: her stick bugs ------------------------------------------------------------------------------ */
/* A stick bug is the game's SEAF trooper (cha_battlefront_seaf) under a resource of the kit's own (add_entity, its own
 * HealthComponentData: bug_health, armour bug_armour), spawned beside her and led with the destination setter, as the
 * Escort Probe played (0.2 to 0.10.3): it fights, follows, takes the enemies' fire after the taunt, and its hits on her
 * are cancelled. */
#define SEAF UINT64_C(0x75F7E96AF7DCD303)
#define BUGS 3
static uint64_t g_bugRes;                 /* the stick bug's resource; 0: the copy was refused (the plain trooper) */
static uint64_t BugRes(void) { return g_bugRes ? g_bugRes : SEAF; }
typedef struct Bug { uint32_t e, net; double since; int dead; } Bug;
static Bug g_bugs[BUGS];
static int g_bugWant;                     /* a stick bug asked for: its package first */
static double g_bugWantAt, g_cdTo;
static unsigned g_bugsMade, g_bugsLost, g_bugGuarded, g_taunted, g_msStrikes, g_ncArcsSent;
typedef struct NegCharge { uint32_t e; double until; } NegCharge;
static NegCharge g_nc[CHARGES];

static int BugOfEntity(uint32_t e)
{
    for (int i = 0; i < BUGS; i++) if (e && g_bugs[i].e == e) return i;
    return -1;
}
/* A hit's dealer or owner (a network id) as one of her stick bugs: never 0x7FFF, "no one" (environmental damage
 * carries it too; Escort Probe 0.10.3) */
static int BugOfNet(uint32_t net)
{
    if (!net || net == 0x7FFF || net == UINT32_MAX) return -1;
    for (int i = 0; i < BUGS; i++) if (g_bugs[i].e && g_bugs[i].net == net) return i;
    int i = hf->entity_of_network ? BugOfEntity(hf->entity_of_network(self, net)) : -1;
    if (i >= 0) g_bugs[i].net = net;
    return i;
}
static int BugsOut(void) { int n = 0; for (int i = 0; i < BUGS; i++) n += g_bugs[i].e && !g_bugs[i].dead; return n; }
static void BugRemove(Bug* b, const char* why)
{
    if (!b->e) return;
    if (!b->dead && !KbRemoveEntity(b->e, BugRes())) Log("stick bug %u: could not be taken away (%s)", b->e, why);
    memset(b, 0, sizeof *b);
}
static void TenderOffensive(void)
{
    if (g_bugWant) return;
    if (g_now < g_cdTo) { Log("Tender Offensive: %.0f s of cooldown left", g_cdTo - g_now); return; }
    g_bugWant = 1;
    g_bugWantAt = g_now;
    g_casts[1]++;
}
static void BugSpawnFrame(void)
{
    if (!g_bugWant) return;
    int r = KbLoadFor(BugRes());
    if (r != HF_PACKAGE_RESIDENT)
    {
        if (!r || g_now - g_bugWantAt > 8) { Log("Tender Offensive: the stick bug's package %s", r ? "did not load in 8 s" : "was refused"); g_bugWant = 0; }
        return;
    }
    g_bugWant = 0;
    float me[3], aim[3];
    if (!PlayerPose(me, aim)) return;
    float h = sqrtf(aim[0] * aim[0] + aim[1] * aim[1]);
    float fx = h > 0.01f ? aim[0] / h : 1, fy = h > 0.01f ? aim[1] / h : 0;
    int n = BugsOut(), max = s_toMax < 1 ? 1 : s_toMax > BUGS ? BUGS : s_toMax;
    /* a 4th takes the oldest away */
    while (n >= max)
    {
        Bug* old = NULL;
        for (int i = 0; i < BUGS; i++) if (g_bugs[i].e && !g_bugs[i].dead && (!old || g_bugs[i].since < old->since)) old = &g_bugs[i];
        if (!old) break;
        Log("Tender Offensive: the oldest stick bug (%u) goes", old->e);
        BugRemove(old, "the oldest");
        n--;
    }
    Bug* b = NULL;
    for (int i = 0; i < BUGS; i++) if (!g_bugs[i].e || g_bugs[i].dead) { b = &g_bugs[i]; break; }
    if (!b) return;
    int slot = (int)(b - g_bugs);
    /* 2.5 m in front of her, to her left, right or front by its slot (the probe's 3 m beside her spawned well) */
    float side = slot == 0 ? -1.0f : slot == 1 ? 1.0f : 0.0f;
    float at[3] = {me[0] + fx * 2.5f - fy * side * 2.0f, me[1] + fy * 2.5f + fx * side * 2.0f, me[2] + 0.5f};
    uint32_t e = KbSpawnEntity(BugRes(), at, "a stick bug");
    if (!e) return;
    memset(b, 0, sizeof *b);
    b->e = e;
    b->since = g_now;
    g_bugsMade++;
    g_cdTo = g_now + s_toCd;
    int32_t hp = 0; uint32_t life = 0;
    HealthOf(e, &hp, &life);
    Log("Tender Offensive: stick bug %u beside her (health %d; %d out)", e, hp, BugsOut());
}
/* following, its deaths (and the refund), every 0.5 s */
static void BugFollowFrame(void)
{
    static double next;
    static float last[3];
    static int haveLast;
    if (g_now < next) return;
    next = g_now + 0.5;
    float me[3], aim[3], p[3];
    if (!PlayerPose(me, aim)) return;
    float way[2] = {aim[0], aim[1]};
    if (haveLast)
    {
        float dx = me[0] - last[0], dy = me[1] - last[1], m = sqrtf(dx * dx + dy * dy);
        if (m > 0.4f) { way[0] = dx / m; way[1] = dy / m; }
    }
    memcpy(last, me, sizeof last);
    haveLast = 1;
    for (int i = 0; i < BUGS; i++)
    {
        Bug* b = &g_bugs[i];
        if (!b->e || b->dead) continue;
        if (!Alive(b->e))
        {
            b->dead = 1;
            g_bugsLost++;
            g_cdTo -= s_toRefund;
            Log("stick bug %u destroyed after %.0f s: Tender Offensive's cooldown cut by %.0f s", b->e, g_now - b->since, (double)s_toRefund);
            continue;
        }
        if (!g_setDest || !EntityPos(b->e, p) || Dist2(me, p) < s_bugFollow * s_bugFollow || !Walks(b->e)) continue;
        /* to her left, right and back, half a metre along the way she's moving */
        float sx = i == 0 ? -way[1] : i == 1 ? way[1] : -way[0], sy = i == 0 ? way[0] : i == 1 ? -way[0] : -way[1];
        float to[3] = {me[0] + way[0] * 0.5f + sx * 1.5f, me[1] + way[1] * 0.5f + sy * 1.5f, me[2]};
        g_setDest(NULL, b->e, to);
    }
}

/* ---- drawing fire: the enemies on her switch to a stick bug ------------------------------------------------------- */
/* The behaviours ([game+0x3326740]: count +0x20, records +0x60, 0x1F8 each; +0x18 the target's entry, +0x68 the
 * perceiver's key, +0x78 has-target) and the perception records ([game+0x3326548]: map +0x30, records +0x50, 0x13F8 each;
 * list A +0x310 count, +0x318 entries of 0x50). The taunt copies her entry, puts the stick bug's id and place in it, adds
 * it to the perceiver's list when missing and sets it as the target (Escort Probe 0.10.0, played: 45 of 53 held it). */
static uintptr_t PerceptionOf(uint32_t e)
{
    uintptr_t m = Pointer(game + 0x3326548), recs;
    uint32_t i;
    if (!m || !e || !Lookup(m + 0x30, e, 1048576, &i) || i >= 65536 || !(recs = Pointer(m + 0x50))) return 0;
    return recs + 0x13F8ull * i;
}
static void TauntFrame(void)
{
    static double next;
    if (s_tauntEvery <= 0 || g_now < next) return;
    next = g_now + s_tauntEvery;
    uint32_t me = atomic_load(&g_avatar);
    float bp[BUGS][3];
    int have[BUGS] = {0}, any = 0;
    for (int i = 0; i < BUGS; i++) if (g_bugs[i].e && !g_bugs[i].dead && EntityPos(g_bugs[i].e, bp[i])) have[i] = any = 1;
    uintptr_t m = Pointer(game + 0x3326740), recs;
    uint32_t n = 0;
    if (!any || !me || !m || !ReadAt(m + 0x20, &n, 4) || n > 16384 || !(recs = Pointer(m + 0x60))) return;
    int switched = 0;
    for (uint32_t k = 0; k < n; k++)
    {
        uintptr_t r = recs + 0x1F8ull * k;
        uint8_t ent[0x50], has = 0;
        uint32_t perceiver = 0;
        if (!ReadAt(r + 0x18, ent, sizeof ent) || *(uint32_t*)ent != me || !ReadAt(r + 0x78, &has, 1) || !has) continue;
        ReadAt(r + 0x68, &perceiver, 4);
        /* the nearest stick bug to the enemy (where it can be read; else the nearest to her) */
        float q[3];
        int pos = perceiver && EntityPos(perceiver, q);
        if (!pos) memcpy(q, ent + 4, 12);
        int best = -1;
        float bd = 0;
        for (int i = 0; i < BUGS; i++) if (have[i]) { float d2 = Dist2(q, bp[i]); if (best < 0 || d2 < bd) { best = i; bd = d2; } }
        if (best < 0 || (pos && s_tauntRadius > 0 && bd > s_tauntRadius * s_tauntRadius)) continue;
        *(uint32_t*)ent = g_bugs[best].e;
        memcpy(ent + 4, bp[best], 12);
        uintptr_t pr = PerceptionOf(perceiver);
        if (pr)
        {
            uint32_t c = 0, there = 0;
            if (ReadAt(pr + 0x310, &c, 4) && c <= 16)
            {
                for (uint32_t j = 0; j < c && !there; j++) { uint32_t id = 0; there = ReadAt(pr + 0x318 + 0x50u * j, &id, 4) && id == g_bugs[best].e; }
                if (!there && c < 16 && !hf->write(pr + 0x318 + 0x50u * c, ent, sizeof ent)) { uint32_t c1 = c + 1; hf->write(pr + 0x310, &c1, 4); }
            }
        }
        if (!hf->write(r + 0x18, ent, sizeof ent)) switched++;
    }
    if (switched) { g_taunted += (unsigned)switched; static unsigned told; if (told++ < 10) Log("stick bugs: %d enemies on her drawn to them", switched); }
}

/* ---- Momentum Support and Negative Charge -------------------------------------------------------------------------- */
static NegCharge* NegOf(uint32_t e)
{
    for (int i = 0; i < CHARGES; i++) if (g_nc[i].e == e && g_nc[i].until > g_now) return &g_nc[i];
    return NULL;
}
static void NegCharge_(uint32_t e)
{
    NegCharge* c = NegOf(e);
    if (!c) for (int i = 0; i < CHARGES; i++) if (g_nc[i].until <= g_now) { c = &g_nc[i]; c->e = e; break; }
    if (c) c->until = g_now + s_ncSeconds;
}
/* Her Electric shot on `target` at `at`: the stick bugs within ms_range m strike too. Returns the damage share they add. */
static double MomentumSupport(uint32_t target, const float at[3])
{
    double add = 0;
    int was = NegOf(target) != NULL, n = 0;
    for (int i = 0; i < BUGS; i++)
    {
        float p[3];
        if (!g_bugs[i].e || g_bugs[i].dead || !EntityPos(g_bugs[i].e, p) || Dist2(p, at) > s_msRange * s_msRange) continue;
        add += s_msMult;
        n++;
    }
    if (n)
    {
        g_msStrikes += (unsigned)n;
        ArcAt(at);
        NegCharge_(target);
    }
    /* Negative Charge: her Electric hit on a charged enemy arcs to every other charged one */
    int arcs = 0;
    if (was && s_ncArcs)
        for (int i = 0; i < CHARGES && arcs < 8; i++)
        {
            float q[3];
            if (g_nc[i].until <= g_now || g_nc[i].e == target || !Alive(g_nc[i].e) || !HitPos(g_nc[i].e, q)) continue;
            ArcAt(q);
            arcs++;
        }
    g_ncArcsSent += (unsigned)arcs;
    if (n || arcs) Log("Momentum Support: %d stick bug%s struck %u (+%.0f%%)%s; %d Negative Charge arcs", n, n == 1 ? "" : "s", target, add * 100, n ? ", Negative Charge on it" : "", arcs);
    return add;
}

/* ---- the stick bug's numbers: its own health record (bug_health, armour bug_armour; every damage zone armour
 * bug_armour and its health x4, so no part of it falls to one shot: Escort Probe 0.4.0) ------------------------------- */
static void BugNumbers(HfMod* mod, HfEvent event, void* user)
{
    uint32_t stride = 0;
    uintptr_t h = g_bugRes && hf->component_record ? hf->component_record("HealthComponentData", g_bugRes, &stride) : 0;
    uintptr_t base = hf->component_record ? hf->component_record("HealthComponentData", SEAF, &stride) : 0;
    if (!h || h == base) { Log("stick bug: its own health record is not there: the trooper's numbers"); return; }
    int32_t top = s_bugHealth;
    uint32_t armour = (uint32_t)s_bugArmour;
    hf->write(h, &top, 4);
    hf->write(h + 0x118, &armour, 4);
    int zones = 0;
    for (int z = 0; z < 38; z++)
    {
        uintptr_t zr = h + 0x208 + 0x228ull * z;
        uint32_t name = 0;
        int32_t zh = 0;
        if (!ReadAt(zr + 0x60, &name, 4) || !name) continue;
        ReadAt(zr + 0xE8, &zh, 4);
        int32_t zh4 = zh > 0 ? zh * 4 : zh;
        hf->write(zr + 0xD8, &armour, 4);
        hf->write(zr + 0xE8, &zh4, 4);
        zones++;
    }
    Log("stick bug: health %d, armour %u, %d damage zones", top, armour, zones);
}

/* ---- Electrocharge's spread --------------------------------------------------------------------------------------- */
static void SpreadFrame(void)
{
    for (int i = 0; i < CHARGES; i++)
    {
        Charge* c = &g_ec[i];
        if (c->until <= g_now || g_now < c->spreadAt) continue;
        c->spreadAt = g_now + s_ecSpread;
        float p[3], q[3];
        if (!Alive(c->e) || !HitPos(c->e, p)) { c->until = 0; continue; }
        int n = 0;
        for (int k = 0; k < FOES && s_ecRadius > 0; k++)
        {
            uint32_t e = g_foe[k];
            if (!e || e == c->e || ChargeOf(e) || !HitPos(e, q) || Dist2(p, q) > s_ecRadius * s_ecRadius || !Alive(e)) continue;
            if (Electrocharge(e)) n++;
        }
        if (n) { g_spreads += (unsigned)n; static unsigned told; if (told++ < 10) Log("Electrocharge: spread from %u to %d enem%s", c->e, n, n == 1 ? "y" : "ies"); }
    }
}

/* ---- her health ------------------------------------------------------------------------------------------------- */
static int32_t HpNow(void) { int32_t hp = 0; uint32_t life = 0; return HealthOf(atomic_load(&g_avatar), &hp, &life) && life == 0 ? hp : -1; }
static void Mend(double amount, unsigned* count)
{
    if (amount < 1 || !hf->heal) return;
    if (Act("a heal", hf->heal(self, (float)amount))) { g_healed += amount; if (count) (*count)++; }
}

/* ---- events ------------------------------------------------------------------------------------------------------ */
/* A hit on her */
static void DamageTaken(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    if (e->cancel) return;   /* (one of her stick bugs' hits: DamageSeen cancelled it) */
    /* her own arcs spare her */
    if (d->kind != 0 && NearOwnArc(d->position)) { e->cancel = 1; g_selfSpared++; return; }
    if (d->amount == 0) return;
    int32_t hp = HpNow();
    if (hp > g_hpMax) g_hpMax = hp;
    /* Self-Repair: once a life, a hit that would kill her */
    if (s_srOn && !g_repaired && g_alive && g_now - g_landedAt >= s_srArm && hp > 0 && GuardFatal(d, hp))
    {
        e->cancel = 1;
        g_repaired = 1;
        g_repairs++;
        double want = s_srHealth * (g_hpMax > 0 ? g_hpMax : hp) - hp;
        Mend(want, NULL);
        Log("Self-Repair: a hit for %d (kind %u) would have killed her (%d health): cancelled, mended by %.0f", d->amount, d->kind, hp, want);
        return;
    }
    /* Dullness: the stacks she has take their share off; then the hit adds one */
    if (g_dull > 0 && s_dullPer > 0) { GuardShare(e, d, 1.0 - s_dullPer * g_dull); g_dullHits++; }
    if (g_dull < s_dullMax) g_dull++;
    g_dullAt = g_now;
    /* taking damage heals (after the hit, at most once every hurt_every s) */
    if (!e->cancel && s_hurtHeal > 0 && g_now - g_hurtHealAt >= s_hurtEvery && g_hpMax > 0)
    {
        g_hurtHealAt = g_now;
        Mend(s_hurtHeal * g_hpMax, &g_healsHurt);
    }
}
/* Every hit, first: her stick bugs' hits on a Helldiver are cancelled before they land (SEAF can hurt the player;
 * Escort Probe 0.10.3 played: bullets and grenades on her, all cancelled), and hits on a stick bug are eased by her
 * Dullness, as hers are */
static void DamageSeen(HfDamagePreEvent* e, HfDamageEvent* d)
{
    int from = BugOfNet(d->dealer);
    if (from < 0) from = BugOfNet(d->owner);
    if (from >= 0)
    {
        uint8_t t[24];
        if (d->target == atomic_load(&g_avatar) || (Descriptor(d->target, t) && Quad(t) == AVATAR) || BugOfEntity(d->target) >= 0)
        {
            e->cancel = 1;
            if (g_bugGuarded++ < 5) Log("stick bug %u's hit on a friend (%d damage, kind %u): cancelled", g_bugs[from].e, d->amount, d->kind);
        }
        return;
    }
    if (d->amount > 0 && BugOfEntity(d->target) >= 0 && g_dull > 0 && s_dullPer > 0) GuardShare(e, d, 1.0 - s_dullPer * g_dull);
}
/* Any other hit: hers on enemies */
static void DamageDealt(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    if (d->amount <= 0 || d->target == me) return;
    int mine = IsShot(d) || (MineHit(d) && d->kind != 0);
    if (!mine) return;
    NoteTarget(d);
    double mult = 1;
    if (g_armed && IsShot(d))
    {
        g_armed = 0;
        g_cdSd = g_now + s_sdCd;
        mult += s_sdMult;
        mult += MomentumSupport(d->target, d->position);
        ArcAt(d->position);
        Electrocharge(d->target);
        Log("Simplified Decision: the Electric shot on %u (%d x %.2f), an arc on it, Electrocharged %.0f s", d->target, d->amount, 1 + (double)s_sdMult, (double)s_ecSeconds);
    }
    Charge* c = ChargeOf(d->target);
    if (c)
    {
        mult += s_ecMult;
        g_ecHits++;
        if (s_ecHeal > 0) Mend(d->amount * mult * s_ecHeal, &g_healsEc);
    }
    if (mult != 1) ScaleDamage(d, mult);
}

static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfKillEvent* k = (const HfKillEvent*)info;
    DropFoe(k->victim);
    Charge* c = ChargeOf(k->victim);
    if (c) c->until = 0;
    if (k->victim == g_lastTarget) g_lastTarget = 0;
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
static void NewLife(void)
{
    g_landedAt = g_now;
    g_repaired = 0;
    g_hpMax = 0;
    g_dull = 0;
    g_armed = 0;
}
static void OnPlayer(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    if (!p->local) return;
    if (info->kind == HF_ON_PLAYER_SPAWN)
    {
        if (p->entity && p->entity != atomic_load(&g_avatar)) { atomic_store(&g_avatar, p->entity); g_avatarNet = 0; }
        g_alive = 1;
        NewLife();
        LearnNet();
    }
    else { g_alive = 0; g_armed = 0; }
}
/* her Helldiver's network id from the player list (Hina 0.1.4: no spawn event of hers may name it). PlayerManager: count
 * +0x84, slot i's peer +0x2C8 + 0x38 i, its Helldiver's network id +0x3A8 + 0x20 i; LocalUser's peer +0xB398 */
static void NetFromPlayers(void)
{
    static double at;
    if (g_now < at) return;
    at = g_now + 1;
    uintptr_t pm = (uintptr_t)hf_global_PlayerManager(self), lu = (uintptr_t)hf_global_LocalUser(self);
    uint64_t mine = 0, peer = 0;
    uint32_t count = 0, net = 0;
    if (!pm || !lu || !ReadAt(lu + 0xB398, &mine, 8) || !mine || !ReadAt(pm + 0x84, &count, 4) || count > 4) return;
    for (uint32_t i = 0; i < count; i++)
        if (ReadAt(pm + 0x2C8 + 0x38u * i, &peer, 8) && peer == mine && ReadAt(pm + 0x3A8 + 0x20u * i, &net, 4) && NetOk(net))
        {
            g_avatarNet = net;
            Log("her Helldiver's network id %u (the player list, slot %u)", net, i);
            return;
        }
}

static void Reset(const char* why)
{
    int start = !strcmp(why, "mission start");
    if (start) KitDecide();
    if (g_casts[0] || g_casts[1] || g_dullHits || g_repairs)
        Log("%s: Simplified Decision %u; %u hits on Electrocharged enemies, %u spreads; %u hits on her eased by Dullness, %u Self-Repairs, %u own arcs spared her; "
            "heals %u (hurt) + %u (Electrocharge), %.0f health in all; stick bugs %u made, %u destroyed, %u enemies drawn to them, %u friendly hits cancelled, "
            "%u Momentum Support strikes, %u Negative Charge arcs",
            why, g_casts[0], g_ecHits, g_spreads, g_dullHits, g_repairs, g_selfSpared, g_healsHurt, g_healsEc, g_healed, g_bugsMade, g_bugsLost, g_taunted,
            g_bugGuarded, g_msStrikes, g_ncArcsSent);
    memset(g_bugs, 0, sizeof g_bugs);   /* (the mission's end takes them with it) */
    memset(g_nc, 0, sizeof g_nc);
    g_bugWant = 0;
    g_cdTo = 0;
    g_bugsMade = g_bugsLost = g_taunted = g_bugGuarded = g_msStrikes = g_ncArcsSent = 0;
    g_boomPending = 0;
    Body(0);
    if (start) Body(1);
    if (start && s_hellbomb && s_hellbombRow == 125 && !hf->load_package(self, UINT64_C(0x346D785D6CA13FF4)))
        Log("the Hellbomb's package could not be loaded ahead");   /* loadout/hellbomb_backpack */
    memset(g_ec, 0, sizeof g_ec);
    memset(g_arcs, 0, sizeof g_arcs);
    memset(g_casts, 0, sizeof g_casts);
    g_arcRetryN = 0;
    g_cdSd = 0;
    g_ecHits = g_spreads = g_dullHits = g_healsHurt = g_healsEc = g_selfSpared = g_repairs = 0;
    g_healed = 0;
    g_lastTarget = 0;
    NewLife();
    if (start) g_avatarNet = 0;
    memset(g_foe, 0, sizeof g_foe);
    memset(g_foeRes, 0, sizeof g_foeRes);
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gameSize = 0, exeSize = 0;
    uintptr_t g = hf->module_base("game.dll", &gameSize), x = hf->module_base("helldivers2.exe", &exeSize);
    if (!g || !x || gameSize != 74727424u || exeSize != 60719104u) { Log("REFUSED: the game's modules are not build " BUILD "'s: her kit stays off"); return; }
    game = g;
    exe = x;
    HitsInstall("her hits");
    KbWorldInstall();
    if (!g_setDest) Log("Tender Offensive: the destination setter is not the reviewed code: her stick bugs won't follow her");
    Log("ready: Tender Offensive on %s (cooldown %.0f s, %d at most, %s)", s_keys[1].label, (double)s_toCd, s_toMax, g_bugRes ? "the stick bug copy" : "the plain SEAF trooper");
    Log("ready: Simplified Decision on %s (cooldown %.0f s, +%.0f%%, arc %d); Delayed Response (Self-Repair %s, Dullness up to %d x %.0f%%, %.0f%% back per hit)",
        s_keys[0].label, (double)s_sdCd, (double)(s_sdMult * 100), s_arcRow, s_srOn ? "on" : "off", s_dullMax, (double)(s_dullPer * 100), (double)(s_hurtHeal * 100));
}

/* ---- for the HUD (read by the Andoris HUD addon through LuaJIT's ffi; layout version 1, the same struct in andoris_hud.lua) */
typedef struct AndorisHud
{
    uint32_t version, in_mission;
    float cd[3], cd_max[3];            /* seconds left, full cooldowns: Simplified Decision, Tender Offensive, Reinforced Program */
    uint32_t armed;                    /* Simplified Decision readied */
    uint32_t charged;                  /* Electrocharged enemies */
    uint32_t dull, dull_max;           /* Dullness stacks */
    uint32_t repair;                   /* Self-Repair: 0 spent, 1 arming, 2 ready */
    float repair_in;                   /* seconds until it is armed */
    uint32_t keys[3];
    char key_label[3][24];
    uint32_t bugs;                     /* 0.2.0: stick bugs out (appended: layout 1 still reads) */
} AndorisHud;
static AndorisHud g_hud = {1};
HF_MOD_EXPORT AndorisHud* andoris_hud_state(void)
{
    g_hud.in_mission = (uint32_t)(g_inMission && g_alive && kb_kit_on);
    g_hud.cd[0] = (float)fmax(0, g_cdSd - g_now);
    g_hud.cd_max[0] = s_sdCd;
    g_hud.cd[1] = (float)fmax(0, g_cdTo - g_now);
    g_hud.cd_max[1] = s_toCd > 0 ? s_toCd : 1;
    g_hud.cd[2] = 0;
    g_hud.cd_max[2] = 1;
    g_hud.bugs = (uint32_t)BugsOut();
    g_hud.armed = (uint32_t)g_armed;
    g_hud.charged = (uint32_t)Charged();
    g_hud.dull = (uint32_t)g_dull;
    g_hud.dull_max = (uint32_t)s_dullMax;
    double in = s_srArm - (g_now - g_landedAt);
    g_hud.repair = !s_srOn || g_repaired ? 0 : in > 0 ? 1 : 2;
    g_hud.repair_in = in > 0 ? (float)in : 0;
    for (int i = 0; i < 3; i++)
    {
        g_hud.keys[i] = (uint32_t)s_keys[i].vk;
        memcpy(g_hud.key_label[i], s_keys[i].label, sizeof g_hud.key_label[i]);
    }
    return &g_hud;
}

/* ---- in-game key bindings: the HUD registers her three skills with Mod Bindings Menu and passes each press here ------ */
static _Atomic unsigned long long g_bindMs;
static atomic_int g_bindPress;
HF_MOD_EXPORT int andoris_input(unsigned version, int skill)
{
    if (version != 1) return 0;
    atomic_store(&g_bindMs, GetTickCount64());
    if (skill >= 1 && skill <= 3) atomic_fetch_or(&g_bindPress, 1 << (skill - 1));
    return 1;
}
HF_MOD_EXPORT int andoris_key(unsigned version, int skill, int* vk, int* mods)
{
    if (version != 1 || skill < 1 || skill > 3 || !vk || !mods) return 0;
    *vk = s_keys[skill - 1].vk;
    *mods = s_keys[skill - 1].mods;
    return 1;
}

static void Tick(HfMod* mod, float dt, void* user)
{
    KbOptionsTick();   /* the options screen's changes, in a mission or not */
    g_now += dt > 0 && dt < 1 ? dt : 0;
    int inMission = hf->game_state(self) == HF_STATE_MISSION;
    if (inMission != g_inMission) { Reset(inMission ? "mission start" : "mission end"); g_inMission = inMission; }
    if (!g_inMission || !game || !kb_kit_on) return;   /* (standing by: another kit is picked) */
    MyItems();
    {
        /* a new Helldiver (her landing, a reinforcement): a new life for Self-Repair and her stacks (the player spawn
         * event comes only on the ship: Hina 0.1.4) */
        static uint32_t lastAv;
        uint32_t av = atomic_load(&g_avatar);
        if (av && av != lastAv) { lastAv = av; NewLife(); Log("her Helldiver %u: a new life (Self-Repair arms in %.0f s)", av, (double)s_srArm); }
    }
    if (!NetOk(g_avatarNet)) NetFromPlayers();
    static int prev[3];
    int focused = Focused();
    int mods = focused ? ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? KM_SHIFT : 0) | ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? KM_CTRL : 0) |
                         ((GetAsyncKeyState(VK_MENU) & 0x8000) ? KM_ALT : 0) : 0;
    int presses = atomic_exchange(&g_bindPress, 0);
    for (int i = 0; i < 3; i++)
    {
        int own = s_keys[i].vk == VK_SHIFT ? KM_SHIFT : s_keys[i].vk == VK_CONTROL ? KM_CTRL : s_keys[i].vk == VK_MENU ? KM_ALT : 0;
        int down = focused && (GetAsyncKeyState(s_keys[i].vk) & 0x8000) != 0 && (mods & ~own) == s_keys[i].mods;
        static unsigned long long fired[3];
        int press = (down && !prev[i]) || (presses >> i & 1);
        if (press && GetTickCount64() - fired[i] > 300 && g_alive && atomic_load(&g_avatar) && (fired[i] = GetTickCount64(), 1))
        {
            if (i == 0) SimplifiedDecision();
            else if (i == 1) TenderOffensive();
            else NotYet(i);
        }
        prev[i] = down;
    }
    ArcRetryFrame();
    SpreadFrame();
    BugSpawnFrame();
    BugFollowFrame();
    TauntFrame();
    if (g_dull > 0 && g_now - g_dullAt >= s_dullDecay) { g_dull--; g_dullAt = g_now; }
    int32_t hp = HpNow();
    if (hp > g_hpMax) g_hpMax = hp;
    Exhaustion();
}

/* ---- settings read and set by name (the options screen, the Mod Options Menu) ------------------------------------- */
static const KbOption kOptions[] = {
    {"sd_cooldown", &s_sdCd, NULL}, {"sd_mult", &s_sdMult, NULL}, {"ec_seconds", &s_ecSeconds, NULL}, {"ec_heal", &s_ecHeal, NULL}, {"ec_mult", &s_ecMult, NULL},
    {"ec_spread", &s_ecSpread, NULL}, {"ec_radius", &s_ecRadius, NULL}, {"sr_on", NULL, &s_srOn}, {"sr_arm", &s_srArm, NULL}, {"sr_health", &s_srHealth, NULL},
    {"dull_max", NULL, &s_dullMax}, {"dull_per", &s_dullPer, NULL}, {"dull_decay", &s_dullDecay, NULL}, {"hurt_heal", &s_hurtHeal, NULL}, {"hurt_every", &s_hurtEvery, NULL},
    {"hp_mult", &s_hpMult, NULL}, {"stamina_mult", &s_staminaMult, NULL}, {"hellbomb_on_exhaustion", NULL, &s_hellbomb},
    {"to_cooldown", &s_toCd, NULL}, {"to_max", NULL, &s_toMax}, {"to_refund", &s_toRefund, NULL}, {"bug_health", NULL, &s_bugHealth}, {"bug_armour", NULL, &s_bugArmour},
    {"bug_follow", &s_bugFollow, NULL}, {"taunt_every", &s_tauntEvery, NULL}, {"taunt_radius", &s_tauntRadius, NULL}, {"ms_range", &s_msRange, NULL},
    {"ms_mult", &s_msMult, NULL}, {"nc_seconds", &s_ncSeconds, NULL}, {"nc_arcs", NULL, &s_ncArcs},
};
static void OptionAfter(const char* name, int changed)
{
    if (g_dull > s_dullMax) g_dull = s_dullMax;
}
HF_MOD_EXPORT int andoris_option_get(const char* name, double* out) { return OptionGet(name, out); }
HF_MOD_EXPORT int andoris_option_set(const char* name, double value) { return OptionSet(name, value); }

#include "kit_options.h"   /* kOpRows: the options screen's rows (kitbase/kb_options.h) */
#include "kit_icon.h"      /* kOpIcon, KB_OP_ACCENT */
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 17 || api->size < offsetof(HfApi, global) + sizeof api->global) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    RosterJoin("andoris", "Andoris");
    kb_ini = "andoris.ini";
    kb_key_names[0] = "key_s1";
    kb_key_names[1] = "key_s2";
    kb_key_names[2] = "key_s3";
    KB_SETTINGS(kSettings);
    KB_OPTIONS(kOptions);
    kb_option_after = OptionAfter;
    kb_damage_seen = DamageSeen;
    kb_damage_taken = DamageTaken;
    kb_damage_dealt = DamageDealt;
    LoadSettings();
    KbOptionsStart(api, mod, "andoris", "Andoris", VERSION, kOpRows, (int)(sizeof kOpRows / sizeof kOpRows[0]), kOpIcon, KB_OP_ICON, KB_OP_ACCENT, s_optionsKey);
    api->log(mod, "Andoris %s: keys %s / %s / %s (Simplified Decision, Tender Offensive; Reinforced Program not in this build)", VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label);
    if (s_badKey) api->log(mod, "andoris.ini: a key could not be read (bits %d: s1, s2, s3); its default stays", s_badKey);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    if (api->add_entity)
    {
        static const char* const own[] = {"HealthComponentData", NULL};
        HfEntity bug = {sizeof bug};
        bug.base = SEAF;
        bug.name = "yui/andoris/stick_bug";
        bug.own_components = own;
        g_bugRes = api->add_entity(mod, &bug);
        if (!g_bugRes) api->log(mod, "stick bug: the trooper copy was refused: her stick bugs are plain SEAF troopers");
        else if (api->on(mod, HF_EVENT_ENTITIES_READY, BugNumbers, NULL)) api->log(mod, "stick bug: its numbers can't be set (no ENTITIES_READY)");
    }
    int ok = api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) &&
             api->on_game_thread(mod, Tick, NULL);
    if (!ok) api->log(mod, "events or the game thread are unavailable: Andoris stays off");
    return 0;
}
