/* Voymastina (yui.voymastina): Voymastina's kit from Girls' Frontline 2 as abilities on keys, for your own Helldiver (solo
 * or host). Her blasts are the game's own physical explosions (no element), set off where she strikes; they never hurt
 * or throw her.
 *   Sirius Fall (key_sirius, default Z): a blast (sf_explosion, the GL-21's) on the enemy under her aim, else the last
 *     she hit. 2 Predation.
 *     Support Action (passive): her own shots on an enemy set off a follow-up blast on it (sa_explosion, the AC-8's), at
 *     most once every sa_every s.
 *   Eye of the White Mastiff (key_eye, default X): overwatch for eye_seconds; an enemy within eye_radius m that moves
 *     (eye_move m between two looks, half a second apart) or hits her takes a Lock On blast (eye_explosion), once per
 *     enemy per eye_every s, at most eye_max per cast. Resets Sirius Fall (Extra Command). 2 Predation.
 *     Drop-in (passive): once she stands after a Hellpod drop, the drop_targets nearest enemies within drop_radius m
 *     take a blast each (drop_explosion, the Eruptor's), and nothing hurts her until they have gone off.
 *   Pile Bunker (key_pile, default C): a pb_lunge m lunge along her aim (the teleport, with its wall and ground checks),
 *     then two blasts 2 and 4 m ahead (pb_explosion, the Breaching Hammer's; pb_fallback, the Breacher's, if the game
 *     refuses it); Stability refilled. Again within pb_window s: a free pb_dash m dash. 2 Predation.
 *     Pile Bunker charge (passive): when her shot or melee hits an enemy within pbp_radius m, a line of blasts runs from
 *     where she stands toward it and a railcannon hits it (she does not move), at most every pbp_every s.
 *   Mastiff's Pledge: never knocked down or ragdolled (steady); dr_base of any damage kept off, dr_stable while her
 *     Stability holds (a poise bar: hits drain it, it refills after stab_delay s unhurt; at 0 she is broken until it is
 *     full again). Her hits pierce ap_bonus more armour.
 *   Predation Protocol: each skill adds pp_per charge; at pp_max the charge is spent and her next skill is empowered:
 *     its blasts pierce pp_ap more armour and deal pp_mult times the damage.
 *   T-Doll body: hp_mult health, stamina_mult sprint, and a Hellbomb on her if her stamina ever runs dry.
 *
 * Research credit: the hit-result layout, the hooked functions, the entity and scene-graph reads, the raycast come from
 * Helldiver Survival Rework 0.3.2 by enochlophobianumerophobia (MIT) and Scope of Authority's Apex knockdown notes,
 * whose research used HD2Runtime by SkyeShade's catalogues (https://github.com/SkyeShade/HD2Runtime); the teleport from
 * HD2Runtime's warp research and a Warp Pack probe. See THIRD_PARTY_NOTICES.md. */
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

#define VERSION "0.4.2"
static int HerRide(uint32_t target);
/* her two kits (p8): the Sinbreaker's GFL2 kit, the AK-15 kit on foot */
static void AkHit(HfDamageEvent* d);
static int AkWatching(void);
static int MpbSpares(const HfDamageEvent* d);
static void MpbKill(const HfKillEvent* k);
static uint32_t MechNow(void);
static int MechPart(uint32_t target);
static double HowlImmuneUntil(void);
#define BUILD "25480438"

#define KB_OWN_SLOTS 16
#define KB_STEADY 1
#define KB_UNSHAKEN 1
#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"
#include "kitbase/kb_body.h"
#include "kitbase/kb_hits.h"
#include "kitbase/kb_damage.h"
#include "kitbase/kb_settings.h"
#include "kitbase/kb_roster.h"

/* with other character kits installed: the HUD's roster and picker calls (kitbase/kb_roster.h) */
KB_ROSTER_EXPORTS(voymastina)

/* ---- settings ------------------------------------------------------------------------------------------------- */
static float s_sfCd = 6, s_sfRange = 40, s_sfWindow = 10;
/* (not in the .ini's text) */
static float s_bareDamage = 5000, s_bareStagger = 80, s_bareAp = 9, s_bareDemolition = 60, s_bareSpeed = 1.2f, s_bareReach = 3.5f, s_bareBlastEvery = 0.5f;
static int s_bareBolt = 1;            /* 1: the blast rides a zero-damage bullet fired along her punch (it lands facing the way she struck) */
static int s_bareBlast = 52;           /* her fist goes off as the Orbital Railcannon's blast, driven along her punch by a bullet of her own */
static int s_dashMove = 1;
static int s_bare = 1;
static int s_sfRow = 294;
static int s_saOn = 1, s_saRow = 404;
static float s_saEvery = 3;
static float s_eyeCd = 20, s_eyeSeconds = 10, s_eyeRadius = 30, s_eyeMove = 1.5f, s_eyeEvery = 2;
static int s_eyeMax = 30, s_eyeRow = 52, s_eyeImmune = 1;
static int s_dropOn = 1, s_dropTargets = 5, s_dropRow = 158;
static float s_dropRadius = 40, s_dropShield = 3;
static float s_pbCd = 12, s_pbLunge = 4, s_pbWindow = 3, s_pbDash = 8;
static int s_pbRow = 343, s_pbFallback = 127, s_pbPoints = 3, s_railRow = 52;
static float s_pbStep = 4, s_pbClimb = 1.5f;
static unsigned g_tooHigh;
static int s_pbpOn = 1, s_pbpPoints = 3, s_pbpRow = 343;
static float s_pbpRadius = 10, s_pbpDistance = 6, s_pbpStep = 4, s_pbpEvery = 6;
static float s_ripple = 0.3f;    /* s between the blasts of a line, rippling out from her (0: all at once) */
static int s_noStaminaHits = 1, s_ppArmor = 4;
static float s_ppArmorEqual = 0.35f, s_ppArmorAfter = 5;
static float s_drBase = 0.3f, s_drStable = 0.8f;
static float s_stabMax = 100, s_stabPer = 0.5f, s_stabMin = 5, s_stabRegen = 20, s_stabDelay = 3;
static int s_apBonus = 1;
static int s_ppPer = 2, s_ppMax = 6, s_ppAp = 2;
static float s_ppMult = 1.5f;
static float s_lpClimb = 6, s_lpDrop = 12;
/* the Sinbreaker (her mech, the GFL2 kit) and its earned Pile Bunker follow-up */
static int s_sinOn = 1, s_sinUses = -1, s_mechAny = 0, s_sinGrant = 1, s_sinLoadout = 1, s_sinFallback = 1, s_sinFreeBase = 2;
static float s_sinCd = 120, s_mechLunge = 8;
static int s_mpbOn = 1, s_mpbUnlock = 2, s_mpbDrag = 10, s_mpbRow = 242, s_mpbFallback = 125;
static float s_mpbLength = 30, s_mpbWidth = 4, s_mpbHold = 0.4f, s_mpbDelay = 0.7f;
/* on foot: her GFL1 kit as AK-15 */
static float s_rageChance = 0.15f, s_rageSeconds = 3, s_rageBonus = 0.4f;
static float s_akEyeSeconds = 6, s_akEyeCd = 8, s_akEyeRange = 50, s_akEyeBonus = 0.4f, s_akEyeEvasion = 0.4f;
static float s_overloadMax = 24, s_overloadSeconds = 5, s_overloadPenalty = 0.2f;
static int s_howlTargets = 5, s_howlRow = 52, s_howlFromControl = 1;
static float s_howlImmune = 2, s_howlRange = 60;
static float s_akErgo = 50, s_akReload = 0.5f, s_akRecoil = 0.7f;
static float s_sinTurn = 1.6f, s_sinSpeed = 1.4f;   /* the Sinbreaker: turning x, walking x */
static int s_sinArmor = 5, s_akZeroTest = 3;   /* her guns on foot: + ergonomics, x reload time */

/* Every setting: its .ini / Mod Options Menu name, variable and range (kitbase/kb_settings.h) */
static const KbSetting kSettings[] = {
    KB_F("sf_cooldown", s_sfCd, 0, 600), KB_F("sf_range", s_sfRange, 5, 200), KB_F("sf_window", s_sfWindow, 0.5, 120),
    KB_I("bare", s_bare, 0, 1), KB_F("bare_damage", s_bareDamage, 0, 1e6), KB_F("bare_stagger", s_bareStagger, 0, 1000), KB_F("bare_ap", s_bareAp, 0, 10), KB_F("bare_demolition", s_bareDemolition, 0, 1000), KB_F("bare_speed", s_bareSpeed, 0.1, 3), KB_F("bare_reach", s_bareReach, 0.5, 10), KB_I("bare_blast", s_bareBlast, 0, 100000), KB_F("bare_blast_every", s_bareBlastEvery, 0, 5), KB_I("bare_bolt", s_bareBolt, 0, 1), KB_I("sf_explosion", s_sfRow, 0, 100000),
    KB_I("sa_on", s_saOn, 0, 1), KB_F("sa_every", s_saEvery, 0.2, 60), KB_I("sa_explosion", s_saRow, 0, 100000),
    KB_F("eye_cooldown", s_eyeCd, 0, 600), KB_F("eye_seconds", s_eyeSeconds, 1, 60), KB_F("eye_radius", s_eyeRadius, 5, 100),
    KB_F("eye_move", s_eyeMove, 0.2, 20), KB_F("eye_every", s_eyeEvery, 0.5, 30), KB_I("eye_max", s_eyeMax, 0, 200), KB_I("eye_immune", s_eyeImmune, 0, 1), KB_I("eye_explosion", s_eyeRow, 0, 100000),
    KB_I("drop_on", s_dropOn, 0, 1), KB_I("drop_targets", s_dropTargets, 0, 12), KB_F("drop_radius", s_dropRadius, 5, 100),
    KB_F("drop_shield", s_dropShield, 0, 10), KB_I("drop_explosion", s_dropRow, 0, 100000),
    KB_F("pb_cooldown", s_pbCd, 0, 600), KB_F("pb_lunge", s_pbLunge, 0, 15), KB_F("pb_window", s_pbWindow, 0, 15), KB_F("pb_dash", s_pbDash, 1, 30),
    KB_I("pb_explosion", s_pbRow, 0, 100000), KB_I("pb_points", s_pbPoints, 0, 8), KB_F("pb_step", s_pbStep, 0.5, 20), KB_F("pb_climb", s_pbClimb, 0, 20), KB_I("rail_explosion", s_railRow, 0, 100000), KB_I("pb_fallback", s_pbFallback, 0, 100000),
    KB_I("pbp_on", s_pbpOn, 0, 1), KB_F("pbp_radius", s_pbpRadius, 2, 60), KB_F("pbp_distance", s_pbpDistance, 1, 20), KB_I("pbp_points", s_pbpPoints, 1, 8),
    KB_F("pbp_step", s_pbpStep, 0.5, 10), KB_F("pbp_every", s_pbpEvery, 0.5, 120), KB_F("pb_ripple", s_ripple, 0, 1), KB_I("pbp_explosion", s_pbpRow, 0, 100000),
    KB_I("steady", s_steady, 0, 1), KB_I("dash_move", s_dashMove, 0, 1), KB_I("unshaken", s_unshaken, 0, 1), KB_I("no_stamina_hits", s_noStaminaHits, 0, 1), KB_I("pp_armor", s_ppArmor, 0, 10), KB_F("pp_armor_equal", s_ppArmorEqual, 0, 1), KB_F("pp_armor_after", s_ppArmorAfter, 0, 60), KB_F("dr_base", s_drBase, 0, 1), KB_F("dr_stable", s_drStable, 0, 1),
    KB_F("stab_max", s_stabMax, 1, 1000), KB_F("stab_per_damage", s_stabPer, 0, 10), KB_F("stab_min_hit", s_stabMin, 0, 100),
    KB_F("stab_regen", s_stabRegen, 0, 500), KB_F("stab_delay", s_stabDelay, 0, 30), KB_I("ap_bonus", s_apBonus, 0, 6),
    KB_I("pp_per", s_ppPer, 0, 10), KB_I("pp_max", s_ppMax, 1, 20), KB_I("pp_ap", s_ppAp, 0, 6), KB_F("pp_mult", s_ppMult, 1, 10),
    KB_F("lp_climb", s_lpClimb, 0.5, 30), KB_F("lp_drop", s_lpDrop, 0.5, 50),
    KB_I("sin_on", s_sinOn, 0, 1), KB_F("sin_cooldown", s_sinCd, 1, 3600), KB_I("sin_uses", s_sinUses, -1, 100), KB_I("mech_any", s_mechAny, 0, 1), KB_I("sin_grant", s_sinGrant, 0, 1), KB_I("sin_loadout", s_sinLoadout, 0, 1), KB_I("sin_fallback", s_sinFallback, 0, 2), KB_I("sin_free_base", s_sinFreeBase, 1, 2), KB_F("mech_lunge", s_mechLunge, 0, 30),
    KB_I("mpb_on", s_mpbOn, 0, 1), KB_I("mpb_unlock", s_mpbUnlock, 0, 2), KB_F("mpb_length", s_mpbLength, 5, 80), KB_F("mpb_width", s_mpbWidth, 1, 20), KB_I("mpb_drag", s_mpbDrag, 0, 32),
    KB_I("mpb_explosion", s_mpbRow, 0, 100000), KB_I("mpb_fallback", s_mpbFallback, 0, 100000), KB_F("mpb_hold", s_mpbHold, 0, 5), KB_F("mpb_delay", s_mpbDelay, 0, 5),
    KB_F("rage_chance", s_rageChance, 0, 1), KB_F("rage_seconds", s_rageSeconds, 0, 60), KB_F("rage_bonus", s_rageBonus, 0, 10),
    KB_F("ak_eye_seconds", s_akEyeSeconds, 0.5, 60), KB_F("ak_eye_cooldown", s_akEyeCd, 0, 300), KB_F("ak_eye_range", s_akEyeRange, 5, 300), KB_F("ak_eye_bonus", s_akEyeBonus, 0, 10), KB_F("ak_eye_evasion", s_akEyeEvasion, 0, 1),
    KB_F("overload_max", s_overloadMax, 1, 1000), KB_F("overload_seconds", s_overloadSeconds, 0, 60), KB_F("overload_penalty", s_overloadPenalty, 0, 1),
    KB_F("sin_turn", s_sinTurn, 0.5, 4), KB_F("sin_speed", s_sinSpeed, 1, 3), KB_I("sin_armor", s_sinArmor, 0, 10), KB_I("ak_zero_test", s_akZeroTest, 0, 3), KB_F("ak_ergo", s_akErgo, -100, 200), KB_F("ak_reload", s_akReload, 0.1, 2), KB_F("ak_recoil", s_akRecoil, 0, 2), KB_I("howl_targets", s_howlTargets, 0, 100), KB_F("howl_immune", s_howlImmune, 0, 30), KB_I("howl_from_control", s_howlFromControl, 0, 1), KB_F("howl_range", s_howlRange, 5, 300), KB_I("howl_explosion", s_howlRow, 0, 100000),
    KB_F("hp_mult", s_hpMult, 0.1, 10), KB_F("stamina_mult", s_staminaMult, 0.1, 20), KB_I("hellbomb_on_exhaustion", s_hellbomb, 0, 1), KB_I("hellbomb_explosion", s_hellbombRow, 0, 100000), KB_F("hellbomb_hold", s_hellbombHold, 0, 10),
};

/* The default voymastina.ini: tools/inigen.py writes the file from this text (the mod never writes it) */
static __attribute__((unused)) void DefaultIni(FILE* f)
{
fprintf(f, "; Voymastina %s. Keys: a letter, digit, F1-F24, Mouse3, Mouse4 or Mouse5, with Shift+, Ctrl+ or Alt+ in front for\n"
               "; a combo (Shift+Q), or Shift, Ctrl or Alt alone. The game still sees the keys too, so pick ones it leaves alone.\n"
               "key_sirius = %s\nkey_eye = %s\nkey_pile = %s\n"
               "; Explosions are ExplosionSettings rows (build 25480438): 361 GL-21 grenade launcher impact, 175 G-12 High\n"
               "; Explosive, 404 AC-8 autocannon impact, 158 R-36 Eruptor impact, 127 P-34 Breacher detonation, 19 CQC-20\n"
               "; Breaching Hammer, 294 GR-8 Recoilless Rifle (its special round), 343 Orbital Precision Strike, 52 Orbital Railcannon. 0: no blast.\n"
               "; Sirius Fall: cooldown; her next shot that hits an enemy within sf_window s sets off sf_explosion where it lands\n"
               "; (one per use). sf_range: how far along her aim Pile Bunker's railcannon finds its target.\n"
               "sf_cooldown = %g\nsf_window = %g\nsf_range = %g\nsf_explosion = %d\n"
               "; Support Action: her own shots on an enemy set off a blast on it, at most once every sa_every s (sa_on 0: off).\n"
               "sa_on = %d\nsa_every = %g\nsa_explosion = %d\n"
               "; Eye of the White Mastiff: cooldown, length, reach; an enemy that moves eye_move m between two looks (half a\n"
               "; second apart) or hits her takes a Lock On blast, once per enemy every eye_every s, at most eye_max per cast.\n"
               "; eye_immune 1: nothing hurts her while it lasts.\n"
               "eye_cooldown = %g\neye_seconds = %g\neye_radius = %g\neye_move = %g\neye_every = %g\neye_max = %d\neye_explosion = %d\neye_immune = %d\n"
               "; Drop-in: once she stands after a Hellpod drop, the drop_targets nearest enemies within drop_radius m take a\n"
               "; blast each; nothing hurts her until they have gone off (drop_shield s at most). drop_on 0: off.\n"
               "drop_on = %d\ndrop_targets = %d\ndrop_radius = %g\ndrop_shield = %g\ndrop_explosion = %d\n"
               "; Pile Bunker: cooldown, the lunge along her aim, then pb_points blasts pb_step m apart ahead (pb_fallback when the\n"
               "; game refuses pb_explosion) and rail_explosion on the enemy she aims at (the charge: on the enemy she charges);\n"
               "; pressed again within pb_window s: a dash of pb_dash m. The lunge and the charge stop in front of the enemy and land\n"
               "; at most pb_climb m above her.\n"
               "pb_cooldown = %g\npb_lunge = %g\npb_window = %g\npb_dash = %g\npb_explosion = %d\npb_fallback = %d\npb_points = %d\npb_step = %g\npb_climb = %g\nrail_explosion = %d\n"
               "; Pile Bunker charge: when her shot or melee hits an enemy within pbp_radius m, she drives a line of\n"
               "; pbp_points blasts pbp_step m apart from where she stands toward it (and a railcannon on it), without moving,\n"
               "; at most every pbp_every s.\n"
               "pbp_on = %d\npbp_radius = %g\npbp_points = %d\npbp_step = %g\npbp_every = %g\npbp_explosion = %d\n"
               "; Mastiff's Pledge: steady 1 never knocked down. Damage kept off her: dr_base always, dr_stable while her\n"
               "; Stability holds (0.8: 80%% less). Stability: stab_max points; a hit drains stab_per_damage per damage point (at\n"
               "; least stab_min_hit); it refills stab_regen a second after stab_delay s unhurt, and in full with Pile Bunker.\n"
               "; ap_bonus: extra armour penetration on her hits. unshaken 1: no stagger, push or flinch from hits either;\n"
               "; no_stamina_hits 1: hits take none of her stamina.\n"
               "steady = %d\nunshaken = %d\nno_stamina_hits = %d\ndr_base = %g\ndr_stable = %g\nstab_max = %g\nstab_per_damage = %g\nstab_min_hit = %g\nstab_regen = %g\nstab_delay = %g\nap_bonus = %d\n"
               "; Predation Protocol: each skill adds pp_per; at pp_max her next skill is empowered: pp_ap more armour\n"
               "; penetration and pp_mult times the damage on its blasts. While it waits (and pp_armor_after s after that skill)\n"
               "; she has heavy armour: armour value pp_armor (4: heavy): a hit with less armour penetration glances off, an\n"
               "; equal one deals pp_armor_equal of its damage (0: off).\n"
               "pp_per = %d\npp_max = %d\npp_ap = %d\npp_mult = %g\npp_armor = %d\npp_armor_equal = %g\npp_armor_after = %g\n"
               "; Where her teleports may land: lp_climb m higher or lp_drop m lower than she stands.\n"
               "lp_climb = %g\nlp_drop = %g\n"
               "; The T-Doll's body: hp_mult times a Helldiver's health, stamina_mult times her sprint before the bar empties.\n"
               "hp_mult = %g\nstamina_mult = %g\n"
               "; If her stamina ever runs out, hellbomb_on_exhaustion (1) sets off hellbomb_explosion (125: the B-100 Portable\n"
               "; Hellbomb's) on her, once the bar has stayed empty hellbomb_hold s (it flickers to 0 at times).\n"
               "hellbomb_on_exhaustion = %d\nhellbomb_explosion = %d\nhellbomb_hold = %g\n",
        VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label,
        (double)s_sfCd, (double)s_sfWindow, (double)s_sfRange, s_sfRow, s_saOn, (double)s_saEvery, s_saRow,
        (double)s_eyeCd, (double)s_eyeSeconds, (double)s_eyeRadius, (double)s_eyeMove, (double)s_eyeEvery, s_eyeMax, s_eyeRow, s_eyeImmune,
        s_dropOn, s_dropTargets, (double)s_dropRadius, (double)s_dropShield, s_dropRow,
        (double)s_pbCd, (double)s_pbLunge, (double)s_pbWindow, (double)s_pbDash, s_pbRow, s_pbFallback, s_pbPoints, (double)s_pbStep, (double)s_pbClimb, s_railRow,
        s_pbpOn, (double)s_pbpRadius, s_pbpPoints, (double)s_pbpStep, (double)s_pbpEvery, s_pbpRow,
        s_steady, s_unshaken, s_noStaminaHits, (double)s_drBase, (double)s_drStable, (double)s_stabMax, (double)s_stabPer, (double)s_stabMin, (double)s_stabRegen, (double)s_stabDelay, s_apBonus,
        s_ppPer, s_ppMax, s_ppAp, (double)s_ppMult, s_ppArmor, (double)s_ppArmorEqual, (double)s_ppArmorAfter, (double)s_lpClimb, (double)s_lpDrop,
        (double)s_hpMult, (double)s_staminaMult, s_hellbomb, s_hellbombRow, (double)s_hellbombHold);
}

/* ---- the clock, the player ------------------------------------------------------------------------------------ */

/* An entity's health record (HD2Runtime's research: the health manager at game+0x3326688, its open-addressed entity
 * hash, 440-byte records with health at +20 and life at +412, 0 alive). 1 with *health and *life filled */
__attribute__((unused)) static int HealthOf(uint32_t entity, int32_t* health, uint32_t* life)
{
    uintptr_t m = Pointer(game + 53634696u);
    uint8_t h[88];
    if (!entity || !m || !ReadAt(m + 4112, h, sizeof h)) return 0;
    uint32_t live = Word(h + 16), cap = Word(h + 40), empty = Word(h + 44), mult = Word(h + 48);
    uintptr_t buckets = (uintptr_t)Quad(h + 32), records = (uintptr_t)Quad(h + 72);
    if (!buckets || !records || !cap || cap > 65536 || (cap & (cap - 1)) || live > 4096) return 0;
    uint32_t start = entity * mult;
    for (uint32_t k = 0; k < cap; k++)
    {
        uint8_t b[8];
        if (!ReadAt(buckets + 8u * ((start + k) & (cap - 1)), b, 8)) return 0;
        uint32_t key = Word(b), index = Word(b + 4);
        if (key == empty) return 0;
        if (key != entity) continue;
        if (index >= live) return 0;
        uint8_t r[416];
        if (!ReadAt(records + 440u * index, r, sizeof r)) return 0;
        *health = (int32_t)Word(r + 20);
        *life = Word(r + 412);
        return 1;
    }
    return 0;
}

/* ---- state ---------------------------------------------------------------------------------------------------- */
static double g_cd[3];                 /* ready at: Sirius Fall, Eye of the White Mastiff, Pile Bunker */
static int g_pp, g_ppReady;            /* Predation charge; 1: her next skill is empowered */
static float g_stab = 100;             /* Stability */
static int g_broken;
static double g_stabHitAt;
static double g_eyeUntil, g_eyeNextScan;
static int g_eyeShots, g_eyeEmp;
static double g_pbFollowUntil;         /* Pile Bunker's free follow-up dash, until */
static double g_saAt = -100, g_pbpAt = -100;
static double g_immuneUntil;           /* drop-in: nothing hurts her until then */
static _Atomic unsigned long long g_boomMs, g_hitMs;
static atomic_int g_ppArmorOn;
static _Atomic unsigned g_armorHits;
static unsigned g_staminaKept;
static double g_ppArmorUntil;
static unsigned g_sfHits;
static float g_mePos[3];   /* her feet, each update (the hit hook reads it) */
static double g_boomAt = -100;         /* her Hellbomb went off then, there: its blast is meant to reach her */
static float g_boomPos[3];
static unsigned g_casts[3], g_follow, g_support, g_lockOns, g_dropBlasts, g_charges, g_empCasts, g_empHits, g_ppFull;
static unsigned g_breaks, g_reducedHits, g_immuneHits, g_selfSpared, g_knockdowns;
static double g_reduced;

/* ---- Predation Protocol ------------------------------------------------------------------------------------------ */
/* A skill commits: 1 when it is the empowered one (the charge was full) */
static int BeginCast(void)
{
    int emp = g_ppReady;
    g_ppReady = 0;
    if (emp) { g_empCasts++; g_ppArmorUntil = g_now + s_ppArmorAfter; }
    return emp;
}
static void EndCast(int skill)
{
    g_casts[skill]++;
    g_pp += s_ppPer;
    if (g_pp >= s_ppMax)
    {
        g_pp = 0;
        g_ppReady = 1;
        g_ppFull++;
        Log("Predation Protocol: charge full: her next skill is empowered (x%.2f damage, +%d armour penetration)", (double)s_ppMult, s_ppAp);
    }
}
static void StabilityFull(void) { g_stab = s_stabMax; if (g_broken) Log("Stability: restored"); g_broken = 0; }
/* ---- the teleport: the game's own position setter, as the LIFT-182 Warp Pack calls it (Warp Probe: game.dll+0x88D795
 * calls 0x5A2BB0 with (NULL, the Helldiver's entity, the destination), then zeroes its velocity with 0x4A7550), with a
 * wall and ground check through the game's raycast (as Helldiver Survival Rework's cover check calls it) -------------- */
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

/* An actor's collision filter name (as Helldiver Survival Rework's cover check reads it: the actor's generation-tagged
 * pool entry, its physics body, the filter table's names); 0 when it cannot be identified */
static uint32_t ActorFilter(uint32_t id, uint32_t unit)
{
    uint32_t world = id >> 30, layout, stride, key_offset, data_offset, key, index, name = 0;
    uintptr_t pool = exe + 0x2369B00 + 64u * ((id >> 28 & 3) + 10 * world);
    uint8_t header[56], record[40], body[160];
    if (!ReadAt(pool, header, 56)) return 0;
    layout = Word(header + 28); stride = layout & 65535; key_offset = layout >> 16 & 255; data_offset = layout >> 24;
    index = id & Word(header + 40);
    if (index >= Word(header + 36) || !(id & Word(header + 52)) || stride < 40 || stride > 512 || key_offset + 4 > stride || data_offset + 40 > stride) return 0;
    uintptr_t entry = (uintptr_t)Quad(header) + (uintptr_t)index * stride;
    if (!ReadAt(entry + key_offset, &key, 4) || key != id || !ReadAt(entry + data_offset, record, 40) || Word(record + 12) != unit || !(Word(record + 16) & 1)) return 0;
    uintptr_t physics = Pointer(exe + 0x27BA8A8 + 176u * world), bodies = physics ? Pointer(physics + 24) : 0;
    uint32_t body_index = Word(record + 20) & 0xFFFFFF;
    if (!bodies || body_index >= 262144 || !ReadAt(bodies + 160u * body_index, body, 160) || Word(body + 144) != id || Word(body + 148) != unit) return 0;
    uintptr_t filters = Pointer(exe + 0x27C5E48), names;
    uint32_t count, filter = Word(body + 108) & 127;
    if (!filters || !ReadAt(filters + 248, &count, 4) || count > 128 || filter >= count || !(names = Pointer(filters + 256)) || !ReadAt(names + 4u * filter, &name, 4)) return 0;
    return name;
}
#define FILTER_FOLIAGE 0x04A8FBF9u
/* The units of the enemies near a dash (refreshed per dash): their bodies are no floor, and a spot under one is taken */
static int g_underFoe;
static uint32_t g_foeUnits[256];
static int g_nFoeUnits;
static int IsFoeUnit(uint32_t u)
{
    for (int i = 0; i < g_nFoeUnits; i++) if (g_foeUnits[i] == u) return 1;
    return 0;
}

/* Every surface under (x, y) from lp_climb m above `ref` down to lp_drop m below it (terrain, rocks, floors, roofs),
 * highest first; how many */
static int Surfaces(uint32_t unit, float x, float y, float ref, float* z, int max)
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
    int k = 0;
    for (uint32_t i = 0; i < n && k < max; i++)
    {
        float d;
        memcpy(&d, hits + 44u * i + 24, 4);
        uint32_t hu = Word(hits + 44u * i + 28);
        if (hu == unit || !isfinite(d) || d < 0.02f || d > len) continue;
        uint32_t filt = ActorFilter(Word(hits + 44u * i + 32), hu); if (filt == FILTER_FOLIAGE || (hu && (filt == 0x236122F0u || filt == 0xD5B33D56u))) continue;   /* leaves, canopies and bodies (enemies, corpses: filters seen in Voymastina 0.2.1's probe) are no floor */
        if (IsFoeUnit(hu)) { g_underFoe = 1; continue; }                              /* nor is an enemy's back */
        z[k++] = top[2] - d;
    }
    for (int i = 1; i < k; i++)        /* highest first */
        for (int j = i; j > 0 && z[j] > z[j - 1]; j--) { float t = z[j]; z[j] = z[j - 1]; z[j - 1] = t; }
    return k;
}
/* Whether a Helldiver fits standing at (x, y, z): nothing within 1.8 m above her feet, nothing within 0.4 m around
 * her at knee and chest height; 1 fits */
static int FoeAbove(uint32_t unit, const float from[3], float len)
{
    uintptr_t world = Pointer(game + 0x346BFA0);
    if (!world || !g_raycast || !g_worldId) return 0;
    uint32_t id = g_worldId((const void*)world);
    if (id >= 4) return 0;
    uint8_t hits[16 * 44];
    float up[3] = {0, 0, 1};
    memset(hits, 0, sizeof hits);
    uint32_t n = g_raycast(id, from, up, len, 2, 1, 0, 0x80000009u, unit, hits, 16);
    for (uint32_t i = 0; i < n && i < 16; i++) if (IsFoeUnit(Word(hits + 44u * i + 28))) return 1;
    return 0;
}
static int Fits(uint32_t unit, float x, float y, float z)
{
    float up[3] = {0, 0, 1}, foot[3] = {x, y, z + 0.1f};
    float r = RayWith(unit, foot, up, 1.8f, 0, 0x80000009u, NULL);
    if (r < 0 || r <= 1.8f) return 0;
    if (g_nFoeUnits && FoeAbove(unit, foot, 12.0f)) return 0;   /* under a Bile Titan, a Charger, a hulk: taken */
    for (int h = 0; h < 2; h++)
        for (int k = 0; k < 4; k++)
        {
            float a = 1.5708f * (float)k + 0.785f;
            float c[3] = {x, y, z + (h ? 1.3f : 0.6f)}, dir[3] = {cosf(a), sinf(a), 0};
            float w = RayWith(unit, c, dir, 0.4f, 0, 0x80000009u, NULL);
            if (w < 0 || w <= 0.4f) return 0;
        }
    return 1;
}

/* ---- her blasts ----------------------------------------------------------------------------------------------------- */
enum { TAG_NONE, TAG_SIRIUS, TAG_SUPPORT, TAG_LOCKON, TAG_DROP, TAG_PILE, TAG_CHARGE, TAG_FIST, TAGS };
static const char* const kTagName[TAGS] = {"a blast", "Sirius Fall", "Support Action", "Lock On", "Drop-in", "Pile Bunker", "Pile Bunker charge", "her hand"};
#define OWN 32
#define OWN_HOOK KB_OWN_SLOTS
typedef struct Pulse { float p[3]; double at; uint32_t row; int tag, emp; } Pulse;
static Pulse g_pulses[OWN];            /* her blasts lately (OnDamagePre's attribution) */
static unsigned g_npulse;
typedef struct Retry { float p[3]; double until; uint32_t row, fallback; int tag, emp; } Retry;
static Retry g_retry[24];              /* blasts waiting for their packages */
static int g_retryCount;
static uint32_t g_refused[8];          /* rows the game refused to spawn: their fallback from then on */
static int g_nrefused;
static unsigned g_answers[TAGS];

static void Own(const float at[3], uint32_t row, int tag, int emp)
{
    Pulse* p = &g_pulses[g_npulse++ % OWN];
    memcpy(p->p, at, sizeof p->p);
    p->at = g_now;
    p->row = row;
    p->tag = tag;
    p->emp = emp;
    unsigned slot = g_ownNext++ % OWN_HOOK;
    memcpy(g_ownPos[slot], at, sizeof g_ownPos[slot]);
    atomic_store(&g_ownEmp[slot], emp);
    atomic_store(&g_ownMs[slot], GetTickCount64());
}
static int Refused(uint32_t row)
{
    for (int i = 0; i < g_nrefused; i++) if (g_refused[i] == row) return 1;
    return 0;
}
static void Answer(int tag, uint32_t row, int r)
{
    if (g_answers[tag]++ < 3)
        Log("%s: explosion %u %s", kTagName[tag], row, r == HF_ACT_DONE || r == HF_ACT_REQUESTED || r == HF_ACT_QUEUED ? "went off" :
            r == HF_ACT_WAITING ? "still waiting for its packages: dropped" : "refused");
    Act(kTagName[tag], r);
}
/* One of her blasts: it never touches or throws her; its packages load first (it waits for them, up to 3 s); a row the
 * game refuses gives way to `fallback` (and does from then on) */
static int Blast_(uint32_t row, uint32_t fallback, const float at[3], int tag, int emp)
{
    if (fallback && Refused(row)) { row = fallback; fallback = 0; }
    if (!row) return 0;
    Own(at, row, tag, emp);
    int r = hf->spawn_explosion(self, row, at);
    if (!r && fallback)
    {
        if (g_nrefused < 8) g_refused[g_nrefused++] = row;
        Log("%s: the game refused explosion %u (%s): explosion %u instead, from now on", kTagName[tag], row,
            hf->last_log ? hf->last_log(self) : "", fallback);
        return Blast_(fallback, 0, at, tag, emp);
    }
    if (r == HF_ACT_WAITING)
    {
        if (g_retryCount < 24)
        {
            Retry* q = &g_retry[g_retryCount++];
            memcpy(q->p, at, sizeof q->p);
            q->until = g_now + 3;
            q->row = row;
            q->fallback = fallback;
            q->tag = tag;
            q->emp = emp;
        }
        return r;
    }
    Answer(tag, row, r);
    return r;
}
static int Pending(int tag)
{
    int n = 0;
    for (int i = 0; i < g_retryCount; i++) n += g_retry[i].tag == tag;
    return n;
}
static double g_dropLastSpawn;
static void RetryFrame(void)
{
    for (int i = 0; i < g_retryCount; i++)
    {
        Retry q = g_retry[i];
        int r = hf->spawn_explosion(self, q.row, q.p);
        if (r == HF_ACT_WAITING && g_now <= q.until) continue;
        g_retry[i] = g_retry[--g_retryCount];
        i--;
        if ((!r || r == HF_ACT_WAITING) && q.fallback)   /* refused, or its packages never came in */
        {
            if (g_nrefused < 8 && !Refused(q.row)) g_refused[g_nrefused++] = q.row;
            Log("%s: explosion %u %s: explosion %u instead, from now on", kTagName[q.tag], q.row,
                r ? "still waited for its packages after 3 s" : "was refused once its packages were in", q.fallback);
            Blast_(q.fallback, 0, q.p, q.tag, q.emp);
            continue;
        }
        if (r != HF_ACT_WAITING) Own(q.p, q.row, q.tag, q.emp);   /* it goes off now: its hits come from now */
        if (q.tag == TAG_DROP) g_dropLastSpawn = g_now;
        Answer(q.tag, q.row, r);
    }
}
/* One of her blasts within `within` s and `radius` m of a point; *emp: an empowered one among them */
static int NearPulse(const float at[3], double within, float radius, int* emp)
{
    int any = 0;
    for (int i = 0; i < OWN; i++)
        if (g_pulses[i].at > 0 && g_now - g_pulses[i].at <= within && Dist2(at, g_pulses[i].p) <= radius * radius)
        {
            any = 1;
            if (emp && g_pulses[i].emp) *emp = 1;
        }
    return any;
}
/* Whether a blast of hers lately can throw shrapnel (the Eruptor's, the autocannon's): its fragments land as shots */
static int NearShrapnel(const float at[3])
{
    for (int i = 0; i < OWN; i++)
        if (g_pulses[i].at > 0 && g_now - g_pulses[i].at <= 1.2 && (g_pulses[i].row == 158 || g_pulses[i].row == 404) &&
            Dist2(at, g_pulses[i].p) <= 35.0f * 35.0f) return 1;
    return 0;
}
/* ---- enemies around her (her area attacks): spawned entities whose kind runs a behaviour and is no turret or
 * Helldiver, and anything her own hits landed on; dropped on death or when the entity id is reused ------------------ */

/* ---- targets ---------------------------------------------------------------------------------------------------- */
/* An enemy's position (its root), 1 when it is still the tracked entity */
static int FoePos(int i, float p[3])
{
    uint8_t id[24];
    uint32_t e = g_foe[i];
    if (!e || !Descriptor(e, id) || Quad(id) != g_foeRes[i]) { if (e) g_foe[i] = 0; return 0; }
    return UnitNode(Word(id + 12), 0, p);
}
static int EntityPos(uint32_t e, float p[3])
{
    uint8_t id[24];
    return e && Descriptor(e, id) && Quad(id) != AVATAR && UnitNode(Word(id + 12), 0, p);
}
/* The enemy nearest her aim line (within tm_range m and 4 m of the line, nearest the line first), else the last she hit */
static uint32_t AimTarget(float at[3])
{
    float eye[3], aim[3], p[3];
    uint32_t best = 0;
    float bestScore = 1e9f;
    if (PlayerPose(eye, aim))
    {
        eye[2] += 1.5f;
        for (int i = 0; i < FOES; i++)
        {
            if (!g_foe[i] || !FoePos(i, p)) continue;
            p[2] += 1.0f;
            float v[3] = {p[0] - eye[0], p[1] - eye[1], p[2] - eye[2]};
            float t = v[0] * aim[0] + v[1] * aim[1] + v[2] * aim[2];
            if (t < 1 || t > s_sfRange) continue;
            float off2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2] - t * t;
            float off = off2 > 0 ? sqrtf(off2) : 0;
            if (off > 2.0f + t * 0.08f) continue;               /* a cone: 2 m wide near her, wider far out */
            float score = off / (1 + t * 0.08f) + t * 0.02f;
            if (score < bestScore) { bestScore = score; best = g_foe[i]; memcpy(at, p, 12); at[2] -= 1.0f; }
        }
    }
    if (!best && g_lastTarget && g_now - g_lastAt <= 6 && EntityPos(g_lastTarget, at)) best = g_lastTarget;
    return best;
}
/* An enemy standing within 1.5 m of the spot */
static int Occupied(const float at[3])
{
    float p[3];
    for (int i = 0; i < FOES; i++)
    {
        if (!g_foe[i] || !FoePos(i, p)) continue;
        float dx = p[0] - at[0], dy = p[1] - at[1], dz = p[2] - at[2];
        if (dx * dx + dy * dy <= 1.5f * 1.5f && dz > -2 && dz < 2.5f) return 1;
    }
    return 0;
}
/* Whether a surface with another one `gap` m above it is a real floor under a real roof (a bridge, an arch, a nest):
 * looking up from it must meet that roof. Terrain and many rocks are solid only from above, so under them the look up
 * meets nothing: that floor is underground (a buried mesh, a rock's base). 1 a real floor */
static int Roofed(uint32_t unit, float x, float y, float z, float gap)
{
    float up[3] = {0, 0, 1}, foot[3] = {x, y, z + 0.1f};
    float r = RayWith(unit, foot, up, gap + 0.5f, 0, 0x80000009u, NULL);
    return r >= 0 && r <= gap + 0.5f && fabsf(r - (gap - 0.1f)) < 0.6f;
}
static int g_buried;   /* spots skipped as underground (for the log) */
/* The best spot at a distance along her direction: the highest surface there she fits on; *crowded when one fitted
 * but an enemy stood on it. 1 found */
static int SpotAt(uint32_t unit, const float pos[3], const float dir[2], float dist, float out[3], int* crowded, int upFirst)
{
    float z[16];
    float x = pos[0] + dir[0] * dist, y = pos[1] + dir[1] * dist;
    g_underFoe = 0;
    int n = Surfaces(unit, x, y, pos[2], z, 16);
    if (g_underFoe && crowded) *crowded = 1;
    float high[16];
    memcpy(high, z, sizeof(float) * (n > 0 ? n : 0));   /* highest first */
    if (!upFirst)   /* the surface nearest her own height first, then outwards */
        for (int i = 1; i < n; i++)
            for (int j = i; j > 0 && fabsf(z[j] - pos[2]) < fabsf(z[j - 1] - pos[2]); j--) { float t = z[j]; z[j] = z[j - 1]; z[j - 1] = t; }
    for (int i = 0; i < n; i++)
    {
        float above = 1e9f;   /* the lowest surface over this one */
        for (int j = 0; j < n; j++) if (high[j] > z[i] + 0.3f && high[j] < above) above = high[j];
        if (above < 1e8f && !Roofed(unit, x, y, z[i], above - z[i])) { g_buried++; continue; }
        if (!Fits(unit, x, y, z[i])) { if (g_nFoeUnits && crowded && FoeAbove(unit, (float[3]){x, y, z[i] + 0.1f}, 12.0f)) *crowded = 1; continue; }
        float at[3] = {x, y, z[i]};
        if (Occupied(at)) { if (crowded) *crowded = 1; continue; }
        memcpy(out, at, sizeof at);
        return 1;
    }
    return 0;
}

/* ---- the abilities ---------------------------------------------------------------------------------------------- */
static void Up(float b[3], const float at[3]) { b[0] = at[0]; b[1] = at[1]; b[2] = at[2] + 0.3f; }

/* Sirius Fall: her next shot that hits an enemy (within sf_window s) sets off its blast where it lands: one per use */
static int g_sfArmed, g_sfEmp;
static double g_sfUntil;
static void SiriusFall(void)
{
    if (g_now < g_cd[0]) return;
    int emp = BeginCast();
    g_sfArmed = 1;
    g_sfEmp = emp;
    g_sfUntil = g_now + s_sfWindow;
    g_cd[0] = g_now + s_sfCd;
    EndCast(0);
    Log("Sirius Fall: her next hit on an enemy within %.0f s sets it off%s (%d Predation)", (double)s_sfWindow, emp ? ", empowered" : "", g_pp);
}

/* Eye of the White Mastiff: where each enemy stood at the last look, and when it was last locked on */
static float g_eyePos[FOES][3];
static uint8_t g_eyeHave[FOES];
static double g_eyeLock[FOES];
static _Atomic unsigned long long g_atkMs[8];   /* who hit her lately (the hit hook: its dealer and owner entities) */
static atomic_uint g_atkWho[8][2];
static atomic_uint g_atkNext;
static void EyeOfTheWhiteMastiff(void)
{
    if (g_now < g_cd[1]) return;
    int emp = BeginCast();
    g_eyeUntil = g_now + s_eyeSeconds;
    g_eyeShots = 0;
    g_eyeEmp = emp;
    g_eyeNextScan = g_now + 0.5;
    float p[3];
    int n = 0;
    for (int i = 0; i < FOES; i++)
    {
        g_eyeHave[i] = 0;
        g_eyeLock[i] = -100;
        if (g_foe[i] && FoePos(i, p)) { memcpy(g_eyePos[i], p, sizeof p); g_eyeHave[i] = 1; n++; }
    }
    int reset = g_now < g_cd[0];
    g_cd[0] = 0;                                  /* Extra Command */
    g_cd[1] = g_now + s_eyeCd;
    EndCast(1);
    Log("Eye of the White Mastiff for %.0f s over %.0f m (%d enemies watched)%s%s (%d Predation)", (double)s_eyeSeconds, (double)s_eyeRadius, n,
        reset ? "; Sirius Fall ready again" : "", emp ? ", empowered" : "", g_pp);
}
static int Attacker(uint32_t e)
{
    unsigned long long now = GetTickCount64();
    for (int i = 0; i < 8; i++)
    {
        unsigned long long at = atomic_load(&g_atkMs[i]);
        if (at && now - at <= 700 && (atomic_load(&g_atkWho[i][0]) == e || atomic_load(&g_atkWho[i][1]) == e)) return 1;
    }
    return 0;
}
static void EyeFrame(void)
{
    if (g_now >= g_eyeUntil || g_now < g_eyeNextScan) return;
    g_eyeNextScan = g_now + 0.5;
    float me[3], p[3], b[3];
    if (!PlayerPose(me, NULL)) return;
    for (int i = 0; i < FOES; i++)
    {
        if (!g_foe[i] || HerRide(g_foe[i])) continue;   /* never her own exosuit or its arms */
        if (!FoePos(i, p)) { g_eyeHave[i] = 0; continue; }
        int moved = g_eyeHave[i] && Dist2(p, g_eyePos[i]) > s_eyeMove * s_eyeMove;
        memcpy(g_eyePos[i], p, sizeof p);
        if (!g_eyeHave[i]) { g_eyeHave[i] = 1; g_eyeLock[i] = -100; }
        if (g_eyeShots >= s_eyeMax || Dist2(p, me) > s_eyeRadius * s_eyeRadius) continue;
        int attacked = Attacker(g_foe[i]);
        if ((!moved && !attacked) || g_now - g_eyeLock[i] < s_eyeEvery) continue;
        g_eyeLock[i] = g_now;
        Up(b, p);
        Blast_((uint32_t)s_eyeRow, 0, b, TAG_LOCKON, g_eyeEmp);
        g_eyeShots++;
        g_lockOns++;
        if (g_lockOns <= 60) Log("Lock On: entity %u (type %016llx) %.0f m away (%s), %d of %d", g_foe[i], (unsigned long long)g_foeRes[i], (double)sqrtf(Dist2(p, me)), attacked ? "hit her" : "moved", g_eyeShots, s_eyeMax);
    }
}

/* ---- the teleports: Pile Bunker's lunge and follow-up dash, the passive charge (Faelynn's Loyal Dash: the warp's
 * position setter with the wall and ground checks) ------------------------------------------------------------------ */
enum { LEAP_NONE, LEAP_LUNGE, LEAP_FOLLOW, LEAP_CHARGE };
typedef struct Leap { int kind, emp; double t0; float dir[2]; uint32_t target; float targetPos[3]; } Leap;
static Leap g_leap;
static int AimFlat(float dir[2])
{
    float pos[3], aim[3];
    if (!PlayerPose(pos, aim)) return 0;
    float h = sqrtf(aim[0] * aim[0] + aim[1] * aim[1]);
    if (h < 0.05f) return 0;
    dir[0] = aim[0] / h;
    dir[1] = aim[1] / h;
    return 1;
}
/* A line of blasts ahead of `from` along dir: `n` at first, first + step, ... m */
/* A line's blasts ripple out from her: the first at once (with the railcannon), each next one pb_ripple later */
typedef struct { double at; float p[3]; uint32_t row, fallback; int tag, emp; } Ripple;
static Ripple g_ripple[32];
static int g_nripple;
static int Line(const float from[3], const float dir[2], float first, float step, int n, uint32_t row, uint32_t fallback, int tag, int emp)
{
    for (int k = 0; k < n; k++)
    {
        float d = first + step * (float)k;
        float b[3] = {from[0] + dir[0] * d, from[1] + dir[1] * d, from[2] + 0.3f};
        if (k == 0 || s_ripple <= 0 || g_nripple >= 32) { Blast_(row, fallback, b, tag, emp); continue; }
        Ripple* r = &g_ripple[g_nripple++];
        r->at = g_now + s_ripple * k; memcpy(r->p, b, sizeof b); r->row = row; r->fallback = fallback; r->tag = tag; r->emp = emp;
    }
    return n;
}
static void RippleFrame(void)
{
    for (int i = 0; i < g_nripple;)
    {
        if (g_now < g_ripple[i].at) { i++; continue; }
        Ripple r = g_ripple[i];
        g_ripple[i] = g_ripple[--g_nripple];
        Blast_(r.row, r.fallback, r.p, r.tag, r.emp);
    }
}
/* The Orbital Railcannon on Pile Bunker's main target: the enemy she charged (target), else the one she aims at */
static void Rail(uint32_t target, int emp)
{
    if (!s_railRow) return;
    float at[3], b[3];
    if (!(target && EntityPos(target, at)) && !AimTarget(at)) return;
    Up(b, at);
    Blast_((uint32_t)s_railRow, 0, b, TAG_PILE, emp);
}
/* The lunge's and the charge's last resort: ground at her own height (within pb_climb) with room around her at knee and
 * chest height and no enemy on it, whatever hangs over it (a shell or debris of her own strikes read as a roof) */
/* For the probe: every hit of the landing check's down-ray, unfiltered, with its unit and collision filter, so the log
 * shows what the thing over the ground is (her own strikes' shells, a prop, debris) */
static int SurfaceUnits(uint32_t unit, float x, float y, float ref, float* z, uint32_t* hu, uint32_t* filt, int max)
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
    int k = 0;
    for (uint32_t i = 0; i < n && k < max; i++)
    {
        float d;
        memcpy(&d, hits + 44u * i + 24, 4);
        if (!isfinite(d) || d > len) continue;
        hu[k] = Word(hits + 44u * i + 28);
        filt[k] = ActorFilter(Word(hits + 44u * i + 32), hu[k]);
        z[k++] = top[2] - d;
    }
    return k;
}
static int SpotLow(uint32_t unit, const float pos[3], const float dir[2], float dist, float out[3])
{
    float z[16], x = pos[0] + dir[0] * dist, y = pos[1] + dir[1] * dist;
    int n = Surfaces(unit, x, y, pos[2], z, 16);
    int best = -1;
    for (int i = 0; i < n; i++)
        if (fabsf(z[i] - pos[2]) <= s_pbClimb && (best < 0 || fabsf(z[i] - pos[2]) < fabsf(z[best] - pos[2]))) best = i;
    if (best < 0) return 0;
    for (int h = 0; h < 2; h++)
        for (int k = 0; k < 4; k++)
        {
            float a = 1.5708f * (float)k + 0.785f;
            float c[3] = {x, y, z[best] + (h ? 1.3f : 0.6f)}, d[3] = {cosf(a), sinf(a), 0};
            float w = RayWith(unit, c, d, 0.4f, 0, 0x80000009u, NULL);
            if (w < 0 || w <= 0.4f) return 0;
        }
    float at[3] = {x, y, z[best]};
    if (Occupied(at)) return 0;
    memcpy(out, at, sizeof at);
    return 1;
}
/* ---- the way she moves (her dashes go that way: kiting means running from what she shoots at). Her root position each
 * update; her movement is the last ~0.2 s of it, when she covers at least 2 m/s and was not hit in the last 0.4 s (a hit's
 * push is not where the player steers). Standing still or just hit: her facing, as before. */
static float g_moveP[16][3];
static double g_moveT[16];
static unsigned g_moveN;
static void MoveTrack(void)
{
    float p[3];
    if (!PlayerPose(p, NULL)) return;
    unsigned k = g_moveN++ % 16;
    memcpy(g_moveP[k], p, sizeof p);
    g_moveT[k] = g_now;
}
static int MoveDir(float dir[2])
{
    if (!s_dashMove || g_moveN < 2) return 0;
    if (GetTickCount64() - atomic_load(&g_hitMs) < 400) return 0;
    unsigned last = (g_moveN - 1) % 16;
    for (unsigned back = 1; back < 16 && back < g_moveN; back++)
    {
        unsigned k = (g_moveN - 1 - back) % 16;
        double dt = g_moveT[last] - g_moveT[k];
        if (dt < 0.15) continue;
        if (dt > 0.5) return 0;
        float dx = g_moveP[last][0] - g_moveP[k][0], dy = g_moveP[last][1] - g_moveP[k][1];
        float d = sqrtf(dx * dx + dy * dy);
        if (d / dt < 2.0) return 0;
        dir[0] = dx / d; dir[1] = dy / d;
        return 1;
    }
    return 0;
}
static void PileBunker(void)
{
    if (g_leap.kind) return;
    float dir[2];
    if (g_now < g_pbFollowUntil)
    {
        if (!g_setPosition || (!MoveDir(dir) && !AimFlat(dir))) return;
        g_pbFollowUntil = 0;
        g_leap = (Leap){LEAP_FOLLOW, 0, g_now, {dir[0], dir[1]}, 0, {0, 0, 0}};
        return;
    }
    if (g_now < g_cd[2]) return;
    if (!AimFlat(dir)) { Log("Pile Bunker: her position or aim cannot be read (or she looks straight up or down)"); return; }
    int emp = BeginCast();
    g_cd[2] = g_now + s_pbCd;
    EndCast(2);
    StabilityFull();
    g_leap = (Leap){LEAP_LUNGE, emp, g_now, {dir[0], dir[1]}, 0, {0, 0, 0}};
}
/* Her shot or melee hit an enemy within pbp_radius m: the passive charge on the game thread's next update. Since 0.2.7
 * she no longer moves (an unasked teleport toward whatever she shot): the line of blasts runs from where she stands */
static struct { int on; uint32_t target; float p[3]; } g_chargeQ;
__attribute__((unused)) static void ChargeAt(uint32_t target, const float at[3])
{
    if (!s_pbpOn || g_chargeQ.on || g_now - g_pbpAt < s_pbpEvery || !g_alive) return;
    float me[3], p[3];
    if (!PlayerPose(me, NULL)) return;
    if (!EntityPos(target, p)) memcpy(p, at, sizeof p);
    if (Dist2(p, me) > s_pbpRadius * s_pbpRadius) return;
    g_pbpAt = g_now;
    g_chargeQ.on = 1; g_chargeQ.target = target; memcpy(g_chargeQ.p, p, sizeof p);
}
static void ChargeFrame(void)
{
    if (!g_chargeQ.on) return;
    g_chargeQ.on = 0;
    float me[3], dir[2], p[3];
    if (!PlayerPose(me, NULL)) return;
    if (!EntityPos(g_chargeQ.target, p)) memcpy(p, g_chargeQ.p, sizeof p);
    float v[2] = {p[0] - me[0], p[1] - me[1]}, h = sqrtf(v[0] * v[0] + v[1] * v[1]);
    if (h < 0.05f) { if (!AimFlat(dir)) return; }
    else { dir[0] = v[0] / h; dir[1] = v[1] / h; }
    Line(me, dir, s_pbpStep, s_pbpStep, s_pbpPoints, (uint32_t)s_pbpRow, 0, TAG_CHARGE, 0);
    Rail(g_chargeQ.target, 0);
    g_charges++;
    if (g_charges <= 6) Log("Pile Bunker charge at entity %u, %.1f m away: %d blasts toward it from where she stands", g_chargeQ.target, (double)h, s_pbpPoints);
}

static const char* const kLeapName[] = {"", "Pile Bunker", "Pile Bunker follow-up", "Pile Bunker charge"};
static void DashFrame(void)
{
    if (!g_leap.kind) return;
    Leap L = g_leap;
    float pos[3];
    if (!g_setPosition || !g_setVelocity || !g_raycast || !QueryIdle())
    {
        if (g_setPosition && g_setVelocity && g_raycast && g_now - L.t0 <= 0.5) return;
        g_leap.kind = LEAP_NONE;
        if (!g_setPosition || !g_setVelocity || !g_raycast) { static int told; if (!told++) Log("%s: the teleport is unavailable on this build (see the start of the log)", kLeapName[L.kind]); }
        else Log("%s: the physics queries stayed busy: no teleport", kLeapName[L.kind]);
        if (L.kind == LEAP_LUNGE && PlayerPose(pos, NULL))   /* the strike still lands where she stands */
            { Line(pos, L.dir, s_pbStep, s_pbStep, s_pbPoints, (uint32_t)s_pbRow, (uint32_t)s_pbFallback, TAG_PILE, L.emp); Rail(0, L.emp); }
        return;
    }
    g_leap.kind = LEAP_NONE;
    uint8_t id[24];
    uint32_t me = atomic_load(&g_avatar);
    if (!Descriptor(me, id) || Quad(id) != AVATAR || !PlayerPose(pos, NULL)) { Log("%s: her Helldiver cannot be read", kLeapName[L.kind]); return; }
    uint32_t unit = Word(id + 12);
    float want = L.kind == LEAP_LUNGE ? s_pbLunge : L.kind == LEAP_FOLLOW ? s_pbDash : s_pbpDistance;
    if (L.kind == LEAP_CHARGE)
    {
        float p[3];
        if (!EntityPos(L.target, p)) memcpy(p, L.targetPos, sizeof p);
        float v[2] = {p[0] - pos[0], p[1] - pos[1]};
        float h = sqrtf(v[0] * v[0] + v[1] * v[1]);
        if (h > s_pbpRadius + 3) return;
        if (h < 0.05f) { if (!AimFlat(L.dir)) return; }
        else { L.dir[0] = v[0] / h; L.dir[1] = v[1] / h; }
        if (h - 1.5f < want) want = h - 1.5f;          /* never past it: she stops in front of it */
    }
    /* the lunge stops in front of the enemy she aims at too (landing on it put her on its back) */
    if (L.kind == LEAP_LUNGE)
    {
        float p[3];
        if (AimTarget(p))
        {
            float v[2] = {p[0] - pos[0], p[1] - pos[1]};
            float h = sqrtf(v[0] * v[0] + v[1] * v[1]);
            if (h > 0.05f && (v[0] * L.dir[0] + v[1] * L.dir[1]) / h > 0.8f && h - 1.5f < want) want = h - 1.5f > 0 ? h - 1.5f : 0;
        }
    }
    /* the enemies near her: their bodies are no floor, and a spot under or on one is taken */
    g_nFoeUnits = 0;
    g_buried = 0;
    {
        uint8_t fid[24];
        float fp[3], reach = want + 20;
        for (int i = 0; i < FOES && g_nFoeUnits < 256; i++)
        {
            uint32_t e = g_foe[i];
            if (!e || !Descriptor(e, fid) || Quad(fid) != g_foeRes[i] || !UnitNode(Word(fid + 12), 0, fp) || Dist2(fp, pos) > reach * reach) continue;
            g_foeUnits[g_nFoeUnits++] = Word(fid + 12);
        }
    }
    /* for the log: what the landing check sees at the full distance (the first few lunges and charges) */
    static unsigned probed;
    if (L.kind != LEAP_FOLLOW && probed < 6 && want >= 1.0f)
    {
        probed++;
        float z[16], x = pos[0] + L.dir[0] * want, y = pos[1] + L.dir[1] * want;
        int n = Surfaces(unit, x, y, pos[2], z, 16);
        char buf[512];
        int k = snprintf(buf, sizeof buf, "%s probe: %.1f m ahead, %d surfaces (height over her feet; R roof reachable from below, F fits, O occupied):",
                         kLeapName[L.kind], (double)want, n);
        for (int i = 0; i < n && k < (int)sizeof buf - 48; i++)
        {
            float above = 1e9f;
            for (int j = 0; j < n; j++) if (z[j] > z[i] + 0.3f && z[j] < above) above = z[j];
            float at[3] = {x, y, z[i]};
            k += snprintf(buf + k, sizeof buf - k, " %+.2f[%s%s%s]", (double)(z[i] - pos[2]),
                          above < 1e8f ? (Roofed(unit, x, y, z[i], above - z[i]) ? "R" : "r") : "-", Fits(unit, x, y, z[i]) ? "F" : "f",
                          Occupied(at) ? "O" : "o");
        }
        Log("%s", buf);
        float zz[16];
        uint32_t uu[16], ff[16];
        n = SurfaceUnits(unit, x, y, pos[2], zz, uu, ff, 16);
        k = snprintf(buf, sizeof buf, "%s probe: down-ray hits (height, unit, filter; her unit %08x):", kLeapName[L.kind], unit);
        for (int i = 0; i < n && k < (int)sizeof buf - 40; i++)
            k += snprintf(buf + k, sizeof buf - k, " %+.2f u%08x f%08x%s", (double)(zz[i] - pos[2]), uu[i], ff[i],
                          uu[i] == unit ? "(her)" : IsFoeUnit(uu[i]) ? "(foe)" : "");
        Log("%s", buf);
    }
    float dest[3] = {pos[0], pos[1], pos[2]}, dist = 0;
    const char* why = "on target";
    int found = 0;
    if (want >= 1.0f)
    {
        int crowded = 0;
        dist = want;
        found = SpotAt(unit, pos, L.dir, dist, dest, &crowded, 0);
        /* the follow-up dash may go further (as Loyal Dash: a few metres for room, more to clear enemies); the lunge and
         * the charge only fall short */
        for (float extra = 1.5f; L.kind == LEAP_FOLLOW && !found && extra <= 15.0f; extra += 1.5f)
        {
            int crowd = 0;
            if (extra > 6.0f && !crowded) break;
            if (SpotAt(unit, pos, L.dir, want + extra, dest, &crowd, 0)) { found = 1; dist = want + extra; why = crowded ? "beyond the enemies" : "beyond: no room there"; }
            crowded |= crowd;
        }
        float stepBack = L.kind == LEAP_FOLLOW ? 1.5f : 1.0f;
        for (float back = stepBack; !found && want - back >= 1.0f; back += stepBack)
            if (SpotAt(unit, pos, L.dir, want - back, dest, NULL, 1)) { found = 1; dist = want - back; why = "closer: no room further"; }
        /* every leap stays on her level: a spot more than pb_climb m above her (an enemy's back, a rock)
         * gives way to a nearer one, else none */
        if (found && dest[2] - pos[2] > s_pbClimb)
        {
            float tmp[3];
            found = 0;
            for (float d = dist - 1.0f; d >= 1.0f; d -= 1.0f)
                if (SpotAt(unit, pos, L.dir, d, tmp, NULL, 1) && tmp[2] - pos[2] <= s_pbClimb)
                { memcpy(dest, tmp, sizeof tmp); found = 1; dist = d; why = "closer: higher ground further"; break; }
            if (!found) g_tooHigh++;
        }
        /* nothing fits: ground at her height, whatever hangs over it */
        for (float d = want; !found && d >= 1.0f; d -= 1.0f)
            if (SpotLow(unit, pos, L.dir, d, dest)) { found = 1; dist = d; why = "under something overhead"; }
    }
    else why = "already there";
    if (want >= 1.0f && !found)
    {
        if (L.kind == LEAP_CHARGE) { g_pbpAt = g_now - s_pbpEvery + 1; static unsigned told; if (told++ < 6) Log("Pile Bunker charge: no spot she fits toward the enemy (%d underground spots skipped): no charge", g_buried); return; }
        Log("%s: no spot she fits within reach (%d underground spots skipped): no teleport", kLeapName[L.kind], g_buried);
        if (L.kind == LEAP_FOLLOW) return;
        memcpy(dest, pos, sizeof dest);                 /* the lunge's strike lands where she stands */
        dist = 0;
    }
    if (found)
    {
        dest[2] += 0.05f;
        float zero[3] = {0, 0, 0};
        g_setPosition(NULL, me, dest);
        g_setVelocity(NULL, me, zero);
    }
    if (L.kind == LEAP_LUNGE)
    {
        Line(dest, L.dir, s_pbStep, s_pbStep, s_pbPoints, (uint32_t)s_pbRow, (uint32_t)s_pbFallback, TAG_PILE, L.emp);
        Rail(0, L.emp);
        g_pbFollowUntil = s_pbWindow > 0 ? g_now + s_pbWindow : 0;
        Log("Pile Bunker: lunge %.1f m (%s), %+.1f m in height; %d blasts %.0f m apart ahead%s (%d Predation)", (double)dist, why,
            (double)(dest[2] - pos[2]), s_pbPoints, (double)s_pbStep, L.emp ? ", empowered" : "", g_pp);
    }
    else if (L.kind == LEAP_FOLLOW)
    {
        g_follow++;
        Log("Pile Bunker follow-up: dash %.1f m (%s), %+.1f m in height", (double)dist, why, (double)(dest[2] - pos[2]));
    }
    else
    {
        Line(dest, L.dir, s_pbpStep, s_pbpStep, s_pbpPoints, (uint32_t)s_pbpRow, 0, TAG_CHARGE, 0);
        Rail(L.target, 0);
        g_charges++;
        if (g_charges <= 6) Log("Pile Bunker charge at entity %u: %.1f m (%s), %d blasts ahead", L.target, (double)dist, why, s_pbpPoints);
    }
}

/* ---- Drop-in: after each Hellpod drop, once she stands still on the ground ---------------------------------------- */
static int g_dropArmed;
static double g_dropSince, g_dropStill, g_dropFiredAt;
static float g_dropZ[3];
static int g_dropShieldOn, g_dropFell;
static unsigned g_dropNotes;
static float g_dropTop;
static void DropArm(void)
{
    if (!s_dropOn) return;
    g_dropArmed = 1;
    g_dropSince = g_now;
    g_dropStill = 0;
    g_dropFell = 0;
    g_dropTop = -1e9f;
    g_dropZ[2] = -1e9f;
}
static void DropFrame(float dt)
{
    if (g_dropShieldOn)   /* the shield ends once its blasts are out (a second for their hits), or at drop_shield s */
    {
        if (g_now >= g_immuneUntil || (!Pending(TAG_DROP) && g_now - g_dropFiredAt > 0.5 && g_now - g_dropLastSpawn > 1.0))
        {
            g_dropShieldOn = 0;
            g_immuneUntil = 0;
            atomic_store(&g_shieldUntilMs, 0);
            Log("Drop-in: shield down after %.1f s (%u hits on her stopped)", g_now - g_dropFiredAt, g_immuneHits);
        }
    }
    if (!g_dropArmed || !g_alive) return;
    if (g_now - g_dropSince > 60) { g_dropArmed = 0; Log("Drop-in: %s within a minute: no blasts", g_dropFell ? "she never stood still after the drop" : "no Hellpod fall seen"); return; }
    float me[3];
    if (!PlayerPose(me, NULL)) { g_dropStill = 0; return; }
    /* the Hellpod's fall first (faster than 15 m/s, or 20 m lower than she was), then standing still for 0.4 s */
    float moved2 = Dist2(me, g_dropZ);
    if (g_dropZ[2] > -1e8f && dt > 0 && !g_dropFell && (moved2 > (15 * dt) * (15 * dt) || g_dropTop - me[2] > 20))
    {
        g_dropFell = 1;
        if (g_dropNotes++ < 4) Log("Drop-in: the Hellpod's fall seen (%.0f m/s)", (double)(sqrtf(moved2) / dt));
    }
    if (me[2] > g_dropTop) g_dropTop = me[2];
    if (moved2 < 0.05f * 0.05f) g_dropStill += dt;
    else g_dropStill = 0;
    memcpy(g_dropZ, me, sizeof me);
    if (!g_dropFell || g_dropStill < 0.4) return;
    g_dropArmed = 0;
    if (s_dropTargets <= 0 || !s_dropRow) return;
    /* the drop_targets nearest enemies within drop_radius m */
    int pick[12], n = 0;
    float d2s[12], p[3];
    for (int i = 0; i < FOES; i++)
    {
        if (!g_foe[i] || !FoePos(i, p)) continue;
        float d2 = Dist2(p, me);
        if (d2 > s_dropRadius * s_dropRadius) continue;
        int k;
        if (n < s_dropTargets) k = n++;
        else if (d2 < d2s[n - 1]) k = n - 1;   /* nearer than the farthest kept: it takes its place */
        else continue;
        d2s[k] = d2; pick[k] = i;
        for (int j = k; j > 0 && d2s[j] < d2s[j - 1]; j--) { float t = d2s[j]; d2s[j] = d2s[j - 1]; d2s[j - 1] = t; int u = pick[j]; pick[j] = pick[j - 1]; pick[j - 1] = u; }
    }
    if (!n) { Log("Drop-in: on the ground, no enemy within %.0f m", (double)s_dropRadius); return; }
    g_dropFiredAt = g_dropLastSpawn = g_now;
    g_immuneUntil = g_now + s_dropShield;
    atomic_store(&g_shieldUntilMs, GetTickCount64() + (ULONGLONG)(s_dropShield * 1000));
    g_dropShieldOn = s_dropShield > 0;
    g_immuneHits = 0;
    for (int k = 0; k < n; k++)
    {
        float b[3];
        if (!FoePos(pick[k], p)) continue;
        Up(b, p);
        Blast_((uint32_t)s_dropRow, 0, b, TAG_DROP, 0);
        g_dropBlasts++;
    }
    Log("Drop-in: on the ground; %d blasts on the nearest enemies (nearest %.0f m), shielded until they are out", n, (double)sqrtf(d2s[0]));
}

/* ---- events ------------------------------------------------------------------------------------------------------ */
typedef struct HitBlast { float p[3]; uint32_t target; } HitBlast;
static HitBlast g_saQueue[4], g_sfShot;
static int g_nsa, g_sfQueued, g_sfQueuedEmp;
static _Atomic unsigned g_bareHits;
static void LearnMelee(void);
/* her fist's blasts lately, for the explosion hook (another thread): they carry her hands' demolition force too */
static _Atomic unsigned long long g_fistBlastMs[4];
static float g_fistBlastPos[4][3];
static atomic_uint g_fistBlastNext;
/* her fist's bullet (add_projectile at init: a Liberator round with nothing of its own, its impact explosion bare_blast):
 * fired from 0.8 m short of where the punch lands, along the punch, so the blast goes off where it strikes and as a
 * projectile's impact (facing the way she struck, where the effect follows the impact) */
static uint32_t g_fistBolt, g_fistBoltDamage;
static int g_fistBoltReady;
static void FistBlast(const float at[3])
{
    unsigned k = atomic_fetch_add(&g_fistBlastNext, 1) % 4;
    memcpy(g_fistBlastPos[k], at, sizeof g_fistBlastPos[k]);
    atomic_store(&g_fistBlastMs[k], GetTickCount64());
    float from[3], dir[3];
    uint8_t d[24];
    uint32_t me = atomic_load(&g_avatar);
    if (s_bareBolt && g_fistBolt && g_fistBoltReady && Descriptor(me, d) && UnitNode(Word(d + 12), NODE_HEAD, from))
    {
        float v[3] = {at[0] - from[0], at[1] - from[1], at[2] - from[2]};
        float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (n > 0.05f)
        {
            for (int i = 0; i < 3; i++) dir[i] = v[i] / n;
            float back = n > 0.8f ? 0.8f : n * 0.5f;
            float start[3] = {at[0] - dir[0] * back, at[1] - dir[1] * back, at[2] - dir[2] * back};
            Own(at, (uint32_t)s_bareBlast, TAG_FIST, 0);
            int r = hf->spawn_projectile(self, g_fistBolt, start, dir);
            static unsigned told;
            int fired = r == HF_ACT_DONE || r == HF_ACT_REQUESTED || r == HF_ACT_QUEUED;
            if (told++ < 3) Log("her hand: her bullet %u fired along the punch (%s)", g_fistBolt, fired ? "ok" : r == HF_ACT_WAITING ? "its packages loading: the blast instead" : "refused: the blast instead");
            if (fired) return;
        }
    }
    Blast_((uint32_t)s_bareBlast, 0, at, TAG_FIST, 0);
}
static float g_fistQ[4][3];
static int g_nfist;
static double g_fistAt = -100;
static atomic_int g_bareOn;
/* What her blasts spare besides her: any exosuit and its parts (hers or not, piloted or empty, still dropping: 0.2.19
 * only spared one within 6 m of her, and an empty or second Breakthrough went down), the arms she holds, the dog drones
 * (all of them), SEAF and the Pelican. None of these is ever a Lock On target. */
static int HerRide(uint32_t target)
{
    uint32_t me = atomic_load(&g_avatar), by = 0;
    if (!target || target == me) return 0;
    if (me && WielderOf(target, &by) && by == me) return 1;
    uint8_t d[24];
    if (!Descriptor(target, d)) return 0;
    uint64_t res;
    memcpy(&res, d, 8);
    return FriendRes(res);
}
/* Mastiff's Pledge on a hit on her, or on the Sinbreaker or its arms while she pilots it: Stability takes the hit, and
 * while it holds most of the damage stays off */
static void Pledge(HfDamagePreEvent* e)
{
    HfDamageEvent* d = &e->damage;
    /* Mastiff's Pledge: Stability takes the hit, and while it holds most of the damage stays off her */
    float reduce = g_broken ? s_drBase : s_drStable;
    float drain = (float)d->amount * s_stabPer;
    if (d->kind != 2 && drain < s_stabMin) drain = s_stabMin;   /* damage over time drains by its amount only */
    g_stabHitAt = g_now;
    if (!g_broken)
    {
        g_stab -= drain;
        if (g_stab <= 0)
        {
            g_stab = 0;
            g_broken = 1;
            g_breaks++;
            if (g_breaks <= 5) Log("Stability broken (a hit for %d, kind %u): %.0f%% kept off until it refills", d->amount, d->kind, (double)(s_drBase * 100));
        }
    }
    if (reduce <= 0.0001f) return;
    if (reduce >= 0.9999f) { e->cancel = 1; g_reducedHits++; g_reduced += d->amount; return; }
    double a = floor(d->amount * (1.0 - reduce) + 0.5);
    g_reducedHits++;
    g_reduced += d->amount - (a < 1 ? 0 : a);
    if (a < 1) { e->cancel = 1; return; }
    d->amount = (int32_t)a;
}
/* A hit on her (HellForge's damage event; kitbase/kb_damage.h routes it here) */
static void DamageTaken(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    /* her own blasts (and their shrapnel) spare her */
    if (d->kind == 4 && NearPulse(d->position, 2.0, 18, NULL)) { e->cancel = 1; g_selfSpared++; return; }
    if ((d->kind == 0 || d->kind == 1) && NearShrapnel(d->position)) { e->cancel = 1; g_selfSpared++; return; }
    /* Drop-in: nothing hurts her until its blasts are out */
    if (g_now < g_immuneUntil || (s_eyeImmune && g_now < g_eyeUntil)) { e->cancel = 1; g_immuneHits++; return; }
    /* her own Hellbomb is meant to reach her */
    if (d->kind == 4 && g_now - g_boomAt < 2.0 && Dist2(d->position, g_boomPos) < 25.0f * 25.0f) return;
    if (g_now < HowlImmuneUntil() || MpbSpares(d)) { e->cancel = 1; g_immuneHits++; return; }   /* Howl; her own Hellbomb */
    if (d->amount <= 0) return;
    if (!MechNow() && AkWatching()) { double ev = floor(d->amount * (1.0 - s_akEyeEvasion) + 0.5); d->amount = ev < 1 ? 1 : (int32_t)ev; }
    Pledge(e);
}
/* Any other hit: hers on enemies, and anyone's on what her skills mark */
static void DamageDealt(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    if (((d->kind == 4 && NearPulse(d->position, 2.0, 18, NULL)) || ((d->kind == 0 || d->kind == 1) && NearShrapnel(d->position)) || MpbSpares(d)) &&
        HerRide(d->target))
    {
        e->cancel = 1;
        static unsigned told;
        uint8_t fd[24]; uint64_t fr = 0;
        if (Descriptor(d->target, fd)) memcpy(&fr, fd, 8);
        if (told++ < 20) Log("her own blast spared a friend (entity %u, type %016llx, kind %u, %d damage)", d->target, (unsigned long long)fr, d->kind, d->amount);
        return;
    }
    if (d->amount <= 0) return;
    if (MechPart(d->target)) { Pledge(e); return; }   /* the Sinbreaker she pilots takes her Pledge */
    if (!MechNow() && MineHit(d) && d->target != me) AkHit(d);   /* on foot: Rage, the AK-15 Eye */
    if (atomic_load(&g_bareOn) && MineHit(d))
    {
        static unsigned told;
        float me[3];
        if (told < 6 && PlayerPose(me, NULL) && Dist2(me, d->position) <= s_bareReach * s_bareReach)
        { told++; Log("her hand: a hit for %d on entity %u (kind %u, element %u)", d->amount, d->target, d->kind, d->element); }
    }
    /* Predation Protocol: an empowered skill's blasts (and what they throw) hit pp_mult times harder */
    int emp = 0;
    if (d->kind == 4 || d->kind == 0 || d->kind == 1 || d->kind == 5) NearPulse(d->position, 1.0, 10, &emp);
    if (emp && s_ppMult > 1.0001f)
    {
        ScaleDamage(d, (double)s_ppMult);
        if (g_empHits++ < 3) Log("Predation Protocol: an empowered blast hit entity %u for %d (kind %u)", d->target, d->amount, d->kind);
    }
    /* her own shots (not her blasts, nor their shrapnel) and her melee (bayonet, stun lance, punches: kind 6 within reach,
     * as 0.2.7's log showed; impact and flat hits within reach too): Sirius Fall, Support Action and the charge take both */
    int shot = IsShot(d) && !((d->kind == 0 || d->kind == 1) && NearShrapnel(d->position));
    float mePos[3];
    int melee = !shot && MineHit(d) && (d->kind == 5 || d->kind == 6 || d->kind == 7) && d->target != atomic_load(&g_avatar) &&
                PlayerPose(mePos, NULL) && Dist2(d->position, mePos) < 6.0f * 6.0f;
    {
        /* every close hit that is not a blast or a burn, hers or not, and what it drew: the first 25 (melee checks) */
        static unsigned close;
        float mc[3];
        if (close < 25 && d->kind != 4 && d->kind != 2 && d->target != atomic_load(&g_avatar) && PlayerPose(mc, NULL) &&
            Dist2(d->position, mc) < 8.0f * 8.0f && !(shot && d->kind == 0))
        {
            close++;
            Log("close hit: kind %u, element %u, %.1f m, dealer %u owner %u (hers %u): %s; Support Action %s, charge %s", d->kind, d->element,
                sqrt(Dist2(d->position, mc)), d->dealer, d->owner, g_avatarNet, shot ? "a shot" : melee ? "melee" : "not hers or not melee",
                g_now - g_saAt >= s_saEvery ? "ready" : "recharging", !s_pbpOn ? "off" : g_now - g_pbpAt >= s_pbpEvery ? "ready" : "recharging");
        }
    }
    if (!shot && !melee)
    {
        /* what else of hers lands near her (the CQC-19 Stun Lance's thrust: which kind, how far): the first few */
        static unsigned other;
        float me2[3];
        if (MineHit(d) && d->kind != 4 && d->kind != 2 && other < 10 && d->target != atomic_load(&g_avatar) && PlayerPose(me2, NULL))
        { other++; Log("her other hit: kind %u, element %u, %.1f m away, %d damage", d->kind, d->element, sqrt(Dist2(d->position, me2)), d->amount); }
        return;
    }
    if (melee && d->kind == 6) LearnMelee();
    /* her bare hands: the fist goes off as a grenade where it lands (bare_blast, at most every bare_blast_every s) */
    if (melee && atomic_load(&g_bareOn) && s_bareBlast > 0 && g_now - g_fistAt >= s_bareBlastEvery && g_nfist < 4)
    {
        g_fistAt = g_now;
        memcpy(g_fistQ[g_nfist++], d->position, sizeof d->position);
    }
    uint8_t tid[24];
    if (!Descriptor(d->target, tid) || Quad(tid) == AVATAR) return;
    if (!FoeKind(Quad(tid))) return;                 /* an enemy, not a prop or a friend */
    AddFoe(d->target, Quad(tid));
    g_lastTarget = d->target;
    memcpy(g_lastPos, d->position, sizeof g_lastPos);
    g_lastAt = g_now;
    /* Sirius Fall: the armed shot sets off its blast here (next update) */
    if (g_sfArmed && g_now <= g_sfUntil && !g_sfQueued)
    {
        g_sfArmed = 0;
        memcpy(g_sfShot.p, d->position, sizeof d->position);
        g_sfShot.target = d->target;
        g_sfQueuedEmp = g_sfEmp;
        g_sfQueued = 1;
    }
    /* Support Action: a follow-up blast on it (next update) */
    if (MechNow() && s_saOn && s_saRow && g_now - g_saAt >= s_saEvery && g_nsa < 4)
    {
        g_saAt = g_now;
        memcpy(g_saQueue[g_nsa].p, d->position, sizeof d->position);
        g_saQueue[g_nsa].target = d->target;
        g_nsa++;
    }
    /* (Pile Bunker charge: since 0.3.0 the Sinbreaker earns a follow-up instead; on foot she has her AK-15 kit) */
}
static void SupportFrame(void)
{
    for (int i = 0; i < g_nfist; i++)
    {
        float b[3];
        Up(b, g_fistQ[i]);
        FistBlast(b);
        static unsigned told;
        if (told++ < 3) Log("her hand: a blast %d where it landed", s_bareBlast);
    }
    g_nfist = 0;
    if (g_sfArmed && g_now > g_sfUntil) { g_sfArmed = 0; Log("Sirius Fall: no hit within %.0f s: its shot is spent", (double)s_sfWindow); }
    if (g_sfQueued)
    {
        float b[3];
        Up(b, g_sfShot.p);
        Blast_((uint32_t)s_sfRow, 0, b, TAG_SIRIUS, g_sfQueuedEmp);
        g_sfQueued = 0;
        g_sfHits++;
        if (g_sfHits <= 5) Log("Sirius Fall: her shot set it off on entity %u%s", g_sfShot.target, g_sfQueuedEmp ? ", empowered" : "");
    }
    for (int i = 0; i < g_nsa; i++)
    {
        float p[3], b[3];
        if (!EntityPos(g_saQueue[i].target, p)) memcpy(p, g_saQueue[i].p, sizeof p);
        Up(b, p);
        Blast_((uint32_t)s_saRow, 0, b, TAG_SUPPORT, 0);
        g_support++;
    }
    g_nsa = 0;
}

static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfKillEvent* k = (const HfKillEvent*)info;
    MpbKill(k);
    DropFoe(k->victim);
    if (k->victim == g_lastTarget) g_lastTarget = 0;
    if (k->victim && k->victim == atomic_load(&g_avatar)) Log("she died (kind %u)", k->kind);
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
        StabilityFull();
        if (g_inMission) DropArm();   /* the mission's first drop is armed at mission start */
    }
    else
    {
        Log("she is down%s", g_inMission ? "" : " (not in a mission)");
        g_alive = 0;
        g_leap.kind = LEAP_NONE;
        g_dropArmed = 0;
    }
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

/* The packages of the explosions she uses, loaded ahead at mission start (HellForge's references.json, build 25480438);
 * the Eruptor's and the Breaching Hammer's list none: spawn_explosion loads what they need the first time */
static const struct { uint32_t row; uint64_t package; const char* name; } kPackages[] = {
    {361, UINT64_C(0x0089BBE3E284FCAA), "GL-21 Grenade Launcher"},
    {175, UINT64_C(0x67B87368042C8B99), "G-12 High Explosive"},
    {404, UINT64_C(0x6AD7CC21015A5F85), "AC-8 Autocannon"},
    {127, UINT64_C(0xF3174EF31B118A55), "P-34 Breacher"},
    {125, UINT64_C(0x346D785D6CA13FF4), "B-100 Portable Hellbomb"},
    {294, UINT64_C(0x6CA87637EAEB5923), "GR-8 Recoilless Rifle"},
    {343, UINT64_C(0xDBAE525060F06D70), "Orbital Precision Strike"},
    {52, UINT64_C(0xF4DC2361985C3026), "Orbital Railcannon Strike"},
};
static void Preload(void)
{
    int rows[10] = {s_bare ? s_bareBlast : 0, s_sfRow, s_saOn ? s_saRow : 0, s_eyeRow, s_dropOn ? s_dropRow : 0, s_pbRow, s_pbFallback, s_pbpOn ? s_pbpRow : 0,
                   s_hellbomb ? s_hellbombRow : 0, s_railRow};
    for (unsigned k = 0; k < sizeof kPackages / sizeof kPackages[0]; k++)
    {
        int used = 0;
        for (int i = 0; i < 10; i++) used |= rows[i] > 0 && (uint32_t)rows[i] == kPackages[k].row;
        if (used && !hf->load_package(self, kPackages[k].package))
            Log("the %s's package could not be loaded ahead: its blast may wait a moment the first time", kPackages[k].name);
    }
}

static unsigned g_pierced;
static void Speed(int on);
static void Reset(const char* why)
{
    if (!strcmp(why, "mission start")) KitDecide();   /* with several kits installed: is this one picked? */
    if (atomic_load(&g_bareOn)) { atomic_store(&g_bareOn, 0); Speed(0); }
    int start = !strcmp(why, "mission start");
    if (g_casts[0] || g_casts[1] || g_casts[2] || g_support || g_dropBlasts || g_charges)
        Log("%s: Sirius Fall %u, Eye of the White Mastiff %u, Pile Bunker %u (%u follow-up dashes); %u Support Actions, %u Lock Ons, "
            "%u drop-in blasts, %u Pile Bunker charges; Predation full %u times, %u empowered casts, %u empowered hits; "
            "%u Stability breaks, %u hits on her reduced (%.0f damage kept off), %u own blasts spared her, %u knockdowns stopped, "
            "%u hits pierced more armour, %u hits on her glanced off her heavy armour (or were cut), %u stamina losses to hits put back",
            why, g_casts[0], g_casts[1], g_casts[2], g_follow, g_support, g_lockOns, g_dropBlasts, g_charges, g_ppFull, g_empCasts, g_empHits,
            g_breaks, g_reducedHits, g_reduced, g_selfSpared, g_knockdowns, g_pierced, atomic_load(&g_armorHits), g_staminaKept);
    atomic_store(&g_armorHits, 0); g_staminaKept = 0; g_ppArmorUntil = 0;
    g_boomPending = 0;
    Body(0);
    if (start) { Body(1); Preload(); }
    memset(g_pulses, 0, sizeof g_pulses);
    memset(g_casts, 0, sizeof g_casts);
    memset(g_answers, 0, sizeof g_answers);
    g_follow = g_support = g_lockOns = g_dropBlasts = g_charges = g_empCasts = g_empHits = g_ppFull = 0;
    g_breaks = g_reducedHits = g_immuneHits = g_selfSpared = g_knockdowns = g_pierced = 0;
    g_reduced = 0;
    g_pp = 0;
    g_ppReady = 0;
    g_stab = s_stabMax;
    g_broken = 0;
    g_cd[0] = g_cd[1] = g_cd[2] = 0;
    g_lastTarget = 0;
    g_eyeUntil = g_pbFollowUntil = 0;
    g_saAt = g_pbpAt = -100;
    g_nsa = 0;
    g_retryCount = 0;
    g_leap.kind = LEAP_NONE;
    g_immuneUntil = 0;
    g_dropShieldOn = 0;
    g_dropArmed = 0;
    atomic_store(&g_shieldUntilMs, 0);
    if (start) DropArm();
}

/* ---- the hit code: hooks (as Primed Sure Footed) ---------------------------------------------------------------------- */
static _Atomic unsigned g_piercedHook;
/* Her stamina written (0..1), where StaminaNow reads it */
static void StaminaSet(float v)
{
    uint32_t me = atomic_load(&g_avatar), ai, named;
    uintptr_t actions = Pointer(game + 0x3326D20), d;
    if (!g_staminaOk || !actions || !HashFind(actions + 0xF8, me, &ai) || ai >= 64 || !(d = Pointer(actions + 0x110 + 8u * ai)) ||
        !ReadAt(d + 8, &named, 4) || named != me) return;
    hf->write(actions + 0x547854 + (uintptr_t)ai * 0x78, &v, 4);
}
/* No stamina lost to hits: a drop of more than 0.02 in a frame within 0.4 s of a hit on her is put back (sprinting
 * drains far less a frame) */
static void StaminaGuard(void)
{
    static float last = -1;
    float v = StaminaNow();
    if (v < 0 || !g_alive) { last = -1; return; }
    unsigned long long hit = atomic_load(&g_hitMs);
    if (s_noStaminaHits && last >= 0 && hit && GetTickCount64() - hit <= 400 && v < last - 0.02f)
    {
        StaminaSet(last);
        g_staminaKept++;
        return;
    }
    last = v;
}
/* ---- her armour penetration: a hit she deals (or one of her blasts) pierces ap_bonus more armour, an empowered
 * blast's pp_ap more on top. The hit (as its calculation gets it) carries its armour penetration at +0x9C, +0xA0, +0xA4
 * (floats, by impact angle; +0xA8 the extreme angle), found with a probe run for Faelynn's Corrosion; it is raised for
 * the calculation only, then put back. ---------------------------------------------------------------------------------- */
static float Shred(const void* hit, float saved[3])
{
    uint32_t me = atomic_load(&g_avatar);
    uint8_t h[HIT_SIZE];
    if (!me || !hit || !ReadAt((uintptr_t)hit, h, sizeof h)) return 0;
    uint32_t target = Word(h + 0x20), dealer = Word(h + 0x80), owner = Word(h + 0x100);
    if (!target || target == me) return 0;
    int emp = 0;
    float bonus = 0;
    if (OwnBlast(h, &emp)) bonus = (float)s_apBonus + (emp ? (float)s_ppAp : 0);
    else if (dealer == me || owner == me) bonus = (float)s_apBonus;
    if (bonus <= 0) return 0;
    memcpy(saved, h + 0x9C, 12);
    for (int k = 0; k < 3; k++) if (!(saved[k] >= 0 && saved[k] <= 12)) return 0;   /* not the reviewed layout */
    float up[3];
    for (int k = 0; k < 3; k++) up[k] = saved[k] + bonus;
    memcpy((uint8_t*)hit + 0x9C, up, 12);
    atomic_fetch_add(&g_piercedHook, 1);
    return bonus;
}
/* Her bare hands: with her primary and secondary empty and her Stability whole, a hit she deals at arm's reach (no blast
 * of hers) pierces bare_ap, and its result gains bare_damage, bare_demolition and bare_stagger (result +0 damage,
 * +0x08 demolition, +0x0C stagger: the layout Faelynn's hit probe read) */
static int BareHit(const void* hit)
{
    uint32_t me = atomic_load(&g_avatar);
    if (!s_bare || !atomic_load(&g_bareOn) || !me || !hit) return 0;
    uint8_t h[HIT_SIZE];
    if (!ReadAt((uintptr_t)hit, h, sizeof h)) return 0;
    uint32_t target = Word(h + 0x20), dealer = Word(h + 0x80), owner = Word(h + 0x100);
    if (!target || target == me || !(dealer == me || (owner == me && (!dealer || Mine(dealer))))) return 0;
    static unsigned told;
    float at[3];
    memcpy(at, h + 0x34, sizeof at);
    float d = isfinite(at[0]) ? sqrtf(Dist2(at, g_mePos)) : -1;
    const char* no = Word(h) == 4 ? "a blast" : OwnBlast(h, NULL) ? "her own blast" : d < 0 || d > s_bareReach ? "out of reach" : NULL;
    if (told < 12) { told++; Log("her hand? a hit of hers %.1f m away (type %u): %s", (double)d, Word(h), no ? no : "yes"); }
    return no == NULL;
}
/* One of her fist's blasts (its origin within 1 m of one, 1.2 s at most): its result gains bare_demolition */
static int FistOrigin(const void* hit)
{
    uint8_t h[0x4C];
    if (!ReadAt((uintptr_t)hit, h, sizeof h) || Word(h) != 4) return 0;
    float o[3];
    memcpy(o, h + 0x40, sizeof o);
    unsigned long long now = GetTickCount64();
    for (int k = 0; k < 4; k++)
    {
        unsigned long long at = atomic_load(&g_fistBlastMs[k]);
        if (at && now - at <= 1500 && Dist2(o, g_fistBlastPos[k]) <= 2.0f * 2.0f) return 1;
    }
    return 0;
}

static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gameSize = 0, exeSize = 0;
    uintptr_t g = hf->module_base("game.dll", &gameSize), x = hf->module_base("helldivers2.exe", &exeSize);
    if (!g || !x || gameSize != 74727424u || exeSize != 60719104u) { Log("REFUSED: the game's modules are not build " BUILD "'s: no aim, no knockdown guard"); return; }
    game = g;
    exe = x;
    if (!HitsInstall("knockdown guard")) return;
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

/* ==== Her two kits (0.3.0): the Sinbreaker carries her GFL2 kit; on foot she fights with her GFL1 kit as AK-15 ===== */

/* ---- the Sinbreaker: her exosuit (a copy of the EXO-55 Breakthrough) and its stratagem (hellforge_mod_init) -------- */
static uint64_t g_sinRes;
static uint32_t g_sinType;
/* The free Sinbreaker, for players without the Exo Experts warbond: a copy of the EXO-49 Emancipator (or EXO-45 Patriot) */
static uint64_t g_sinFreeRes;
static uint32_t g_sinFreeType;
static int IsExosuit(uint64_t r)
{
    return r == 0x79E4B3D2DA5E45E3ull || r == 0xC2D449ECF7FACAB1ull || r == 0x7B2326F6FD9C8069ull || r == 0x35DBF54F016F3624ull;
}
static int HerMechKind(uint64_t r) { return r && ((g_sinRes && r == g_sinRes) || (g_sinFreeRes && r == g_sinFreeRes) || (s_mechAny && IsExosuit(r))); }

/* ---- the seat (Khi's reading, as Apache - Rise Above and Mech Probe do it: the command bus game+0x3326E68, entry 117) */
typedef struct { int found; uint32_t vehicle, role, node; int changing; } Seat;
static Seat ReadSeat(void)
{
    Seat s = {0};
    uintptr_t bus = Pointer(game + 0x3326E68), manager = 0, seaters, entries;
    uint32_t count = 0, me = atomic_load(&g_avatar);
    if (!bus || !me || !ReadAt(bus + 7948, &count, 4) || count > 512) return s;
    for (uint32_t i = 0; i < count && !manager; i++)
    {
        uint32_t type = 0;
        if (ReadAt(bus + 7952 + i * 16ull + 8, &type, 4) && type == 117) manager = Pointer(bus + 7952 + i * 16ull);
    }
    if (!manager || !ReadAt(manager + 12, &count, 4) || count > 4096 || !(seaters = Pointer(manager + 56)) || !(entries = Pointer(manager + 72))) return s;
    for (uint32_t i = 0; i < count; i++)
    {
        uintptr_t object = Pointer(seaters + i * 8ull);
        uint32_t flags = 0, avatar = 0;
        uint8_t raw[64];
        if (!object || !ReadAt(object + 20, &flags, 4) || !(flags & 1) || !ReadAt(object + 8, &avatar, 4) || avatar != me) continue;
        if (!ReadAt(entries + i * 64ull, raw, 64)) return s;
        s.found = 1;
        s.vehicle = Word(raw); s.role = Word(raw + 8); s.node = Word(raw + 20); s.changing = raw[48] != 0;
        return s;
    }
    return s;
}

/* ---- the mech: which one she pilots, its unit, its arms (spawned with it) -------------------------------------------- */
static uint32_t g_mech, g_mechUnit, g_mechParts[8];
static int g_nMechParts;
static uint32_t g_lastMech;
static double g_mechLeftAt;
typedef struct { uint32_t entity; uint64_t res; double at; } Spawned8;
static Spawned8 g_spawned[256];
static unsigned g_nspawned;
static void MechSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* e = (const HfSpawnEvent*)info;
    Spawned8* s = &g_spawned[g_nspawned++ % 256];
    s->entity = e->entity; s->res = e->resource; s->at = g_now;
}
static void MechParts(uint32_t mech)
{
    double born = -1e9;
    for (unsigned i = 0; i < 256; i++) if (g_spawned[i].entity == mech) born = g_spawned[i].at;
    g_nMechParts = 0;
    for (unsigned i = 0; i < 256 && g_nMechParts < 8; i++)
    {
        uint8_t d[24];
        if (!g_spawned[i].entity || g_spawned[i].entity == mech || fabs(g_spawned[i].at - born) > 3.0 || !Descriptor(g_spawned[i].entity, d)) continue;
        g_mechParts[g_nMechParts++] = Word(d + 12);
    }
}
static int MechPos(float p[3]) { return g_mechUnit && UnitNode(g_mechUnit, 0, p); }
/* where the camera looks (the unit object's camera, as Mech Probe and Apache - Rise Above read it) */
static int CameraFlat(uint32_t unit, float dir[2])
{
    uintptr_t registry = Pointer(exe + 0x1A100F0), objects, object, w;
    uint32_t slot = unit & 0x3FFFFF, id = 0;
    float m[16];
    if (!registry || !(objects = Pointer(registry + 0x88)) || !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit ||
        !(w = Pointer(object + 0x18)) || !ReadAt(w + 0x230920, m, 64)) return 0;
    for (int k = 0; k < 16; k++) if (!isfinite(m[k])) return 0;
    if (fabsf(m[15] - 1) > 1e-3f) return 0;
    float h = sqrtf(m[4] * m[4] + m[5] * m[5]);
    if (h < 0.05f) return 0;
    dir[0] = m[4] / h; dir[1] = m[5] / h;
    return 1;
}
static int MechAim(float dir[2])
{
    uint8_t d[24];
    if (Descriptor(atomic_load(&g_avatar), d) && CameraFlat(Word(d + 12), dir)) return 1;
    if (g_mechUnit && CameraFlat(g_mechUnit, dir)) return 1;
    return AimFlat(dir);
}
/* A ray that sees through the suit, her and the suit's arms (the shield arm read as a wall 0.6 m ahead: Mech Probe) */
static float MechRay(uint32_t skip, const float from[3], const float dir[3], float len)
{
    uintptr_t world = Pointer(game + 0x346BFA0);
    if (!world || !g_raycast || !g_worldId || !QueryIdle()) return -1;
    uint32_t id = g_worldId((const void*)world), pilot = 0;
    if (id >= 4) return -1;
    uint8_t d[24];
    if (Descriptor(atomic_load(&g_avatar), d)) pilot = Word(d + 12);
    uint8_t hits[16 * 44];
    memset(hits, 0, sizeof hits);
    uint32_t n = g_raycast(id, from, dir, len, 2, 1, 0, 0x80000009u, skip, hits, 16);
    if (n > 16) return -1;
    float best = len + 1;
    for (uint32_t i = 0; i < n; i++)
    {
        float t;
        memcpy(&t, hits + 44u * i + 24, 4);
        uint32_t u = Word(hits + 44u * i + 28);
        int own = u == skip || u == g_mechUnit || (pilot && u == pilot) || IsFoeUnit(u);   /* enemies are no ground or wall: 0.3.9's
                                                                   walk boost stood the suit on a live bug's back, and it flew up */
        for (int k = 0; k < g_nMechParts && !own; k++) own = u == g_mechParts[k];
        if (own || !isfinite(t) || t < 0.3f) continue;
        if (t < best) best = t;
    }
    return best;
}

/* ---- the mech's Pile Bunker: the suit itself lunges (the warp setter a little each update, stopping at walls and edges:
 * Mech Probe's F9), then the strike line and the railcannon from where it stops ---------------------------------- */
static struct { int on, emp; float dir[2], left; double last, until; } g_mlunge;
static struct { double at; float from[3], dir[2], len; } g_pbStrike;   /* the last mech Pile Bunker's strike (its kills) */
static void MechStrike(int emp)
{
    float p[3];
    if (!MechPos(p)) return;
    Line(p, g_mlunge.dir, s_pbStep + 2, s_pbStep, s_pbPoints, (uint32_t)s_pbRow, (uint32_t)s_pbFallback, TAG_PILE, emp);
    Rail(0, emp);
    g_pbStrike.at = g_now;
    memcpy(g_pbStrike.from, p, sizeof p);
    memcpy(g_pbStrike.dir, g_mlunge.dir, sizeof g_mlunge.dir);
    g_pbStrike.len = s_pbStep + 2 + s_pbStep * (float)s_pbPoints;
}
static void MechPileBunker(void)
{
    if (g_mlunge.on || g_now < g_cd[2]) return;
    float dir[2];
    if (!MechAim(dir)) { Log("Pile Bunker (Sinbreaker): its aim cannot be read"); return; }
    int emp = BeginCast();
    g_cd[2] = g_now + s_pbCd;
    EndCast(2);
    StabilityFull();
    g_mlunge.on = 1; g_mlunge.emp = emp; g_mlunge.dir[0] = dir[0]; g_mlunge.dir[1] = dir[1];
    g_mlunge.left = s_mechLunge; g_mlunge.last = g_now; g_mlunge.until = g_now + 1.0;
    Log("Pile Bunker (Sinbreaker): the suit lunges %.0f m%s (%d Predation)", (double)s_mechLunge, emp ? ", empowered" : "", g_pp);
}
static void MechLungeFrame(void)
{
    if (!g_mlunge.on) return;
    float p[3];
    double dt = g_now - g_mlunge.last;
    g_mlunge.last = g_now;
    const char* why = NULL;
    if (!g_mech || !g_setPosition || !MechPos(p)) why = "the suit cannot be moved";
    else if (g_mlunge.left <= 0.05f) why = "done";
    else if (g_now >= g_mlunge.until) why = "out of time";
    if (!why && dt > 0 && dt <= 0.1)
    {
        float step = (float)(25.0 * dt);
        if (step > g_mlunge.left) step = g_mlunge.left;
        float dir[3] = {g_mlunge.dir[0], g_mlunge.dir[1], 0}, down[3] = {0, 0, -1};
        for (int h = 0; h < 2 && !why; h++)
        {
            float from[3] = {p[0], p[1], p[2] + (h ? 2.4f : 1.0f)};
            float w = MechRay(g_mechUnit, from, dir, step + 1.5f);
            if (w < 0) return;
            if (w <= step + 1.5f) why = "a wall ahead";
        }
        if (!why)
        {
            float dest[3] = {p[0] + dir[0] * step, p[1] + dir[1] * step, p[2]}, top[3] = {dest[0], dest[1], p[2] + 2.5f};
            float g = MechRay(g_mechUnit, top, down, 5.0f);
            if (g < 0) return;
            if (g > 5.0f) why = "an edge";
            else if (top[2] - g - p[2] > 1.0f) why = "a climb";
            else
            {
                dest[2] = top[2] - g;
                g_setPosition(NULL, g_mech, dest);
                g_mlunge.left -= step;
                return;
            }
        }
    }
    if (!why) return;
    g_mlunge.on = 0;
    Log("Pile Bunker (Sinbreaker): the lunge stops (%s), %.1f m short; the strike", why, (double)(g_mlunge.left > 0 ? g_mlunge.left : 0));
    MechStrike(g_mlunge.emp);
}

/* ---- the earned follow-up: Pile Bunker kills a heavily armoured enemy (armour 4+) -> Pile Bunker's key again while it
 * recharges (or, in the Breakthrough Sinbreaker, the shield bash) sends a
 * precision-strike line mpb_length m out from the suit, enemies and corpses on it are dragged to its end, and a Hellbomb
 * goes off there ---------------------------------------------------------------------------------------------------- */
static int g_mpbReady;
static double g_mpbFireAt;   /* when it goes: the key's, at once; the bash's, mpb_delay s after it starts (the swing finishes first) */
static double g_mpbBoomAt;
static float g_mpbBoomPos[3];
static unsigned g_mpbFired, g_mpbEarned;
#define DEAD 128
static struct { uint32_t e; uint64_t res; double at; } g_dead[DEAD];
static unsigned g_ndead;
static int Heavy(uint64_t res, char* why, size_t n)
{
    uint32_t stride = 0;
    uintptr_t h = res ? hf->component_record("HealthComponentData", res, &stride) : 0;
    int32_t hp = 0;
    uint32_t main = 0, zone = 0;
    if (!h || !ReadAt(h, &hp, 4) || !ReadAt(h + 0x118, &main, 4) || !ReadAt(h + 0x2E0, &zone, 4)) { snprintf(why, n, "its health record is unreadable"); return 0; }
    int heavy = (main >= 4 && main < 100) || (zone >= 4 && zone < 100) || hp >= 2000;
    snprintf(why, n, "health %d, armour %u / %u", hp, main, zone);
    return heavy;
}
static float SegDist(const float p[3], const float from[3], const float dir[2], float len, float* along)
{
    float v[2] = {p[0] - from[0], p[1] - from[1]};
    float t = v[0] * dir[0] + v[1] * dir[1];
    if (along) *along = t;
    if (t < -2 || t > len + 2) return 1e9f;
    float off = v[0] * dir[1] - v[1] * dir[0];
    return fabsf(off);
}
static void MpbKill(const HfKillEvent* k)
{
    uint8_t d[24];
    if (!k->victim || !Descriptor(k->victim, d)) return;
    uint64_t res = Quad(d);
    if (res == AVATAR) return;
    unsigned slot = g_ndead++ % DEAD;
    g_dead[slot].e = k->victim; g_dead[slot].res = res; g_dead[slot].at = g_now;
    if (!g_mech || !s_mpbOn || g_mpbReady || g_now - g_pbStrike.at > 3.0) return;
    float p[3];
    if (!UnitNode(Word(d + 12), 0, p) || SegDist(p, g_pbStrike.from, g_pbStrike.dir, g_pbStrike.len, NULL) > 6.0f) return;
    char why[96];
    int heavy = Heavy(res, why, sizeof why);
    if (s_mpbUnlock >= 2 && !heavy) { static unsigned told; if (told++ < 6) Log("Pile Bunker killed %016llx (%s): not heavily armoured, no follow-up", (unsigned long long)res, why); return; }
    g_mpbReady = 1;
    g_mpbEarned++;
    Log("Pile Bunker killed %016llx (%s): follow-up ready (Pile Bunker's key again, or the shield bash)", (unsigned long long)res, why);
}
/* the drag: living enemies stepped along the ground (the warp setter, as Mech Probe's Ctrl+F10), corpses pushed */
typedef struct { uint32_t e, unit; float to[3]; } Dragged;
static Dragged g_drag[32];
static int g_ndrag;
static double g_dragUntil, g_dragLast;
static float g_mpbFrom[3], g_mpbDir[2];
/* corpses: physics impulses on every body of the ragdoll (Mech Probe; the engine physics as Apache - Rise Above binds it) */
typedef int (*ComponentsFn)(uint32_t, uint32_t, void*);
typedef int (*ActorBoolFn)(uint32_t);
typedef uint32_t (*ActorOwnerFn)(uint32_t);
typedef void (*ActorPushFn)(uint32_t, const float*, const float*);
typedef void* (*UnitResolveFn)(uint32_t);
static struct { UnitResolveFn unit_resolve; ComponentsFn components; ActorBoolFn valid, physical; ActorOwnerFn owner; ActorPushFn impulse; int tried; } g_phys;
static int PhysicsBound(void)
{
    if (g_phys.tried) return g_phys.impulse != NULL;
    g_phys.tried = 1;
    uintptr_t table = Pointer(game + 0x3326338);
    if (table != exe + 0x27CD910) { Log("the engine physics table is not where Khi found it: corpses are not pushed"); return 0; }
    static const struct { uint32_t rva; const char* pat; uint32_t slot; void** out; } k[] = {
        {0x9d8c0u, "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 48 8b 35 1a 28 97 01", ~0u, (void**)&g_phys.unit_resolve},
        {0x7e1c20u, "48 89 5c 24 20 55 48 83 ec 20 8b c1 4d 8b d0 25 ff ff 3f 00", ~0u, (void**)&g_phys.components},
        {0x7972f0u, "48 83 ec 28 e8 b7 de ff ff 33 c9 48 85 c0 0f 95 c1", 0x18, (void**)&g_phys.valid},
        {0x797310u, "48 83 ec 28 e8 97 de ff ff 48 85 c0 74 0b 8b 40 10", 0x00, (void**)&g_phys.physical},
        {0x799980u, "48 83 ec 28 e8 27 b8 ff ff 48 85 c0 74 08 8b 40 0c", 0x10, (void**)&g_phys.owner},
        {0x797cf0u, "48 89 5c 24 10 48 89 6c 24 18 56 57 41 56 48 83 ec 60 49 8b f0", 0xC0, (void**)&g_phys.impulse},
    };
    void* got[6];
    for (unsigned i = 0; i < 6; i++)
    {
        uintptr_t at = exe + k[i].rva;
        if (!hf->code_matches(at, k[i].pat) || (k[i].slot != ~0u && Pointer(table + k[i].slot) != at)) { Log("engine physics: a function is not the reviewed code: corpses are not pushed"); return 0; }
        got[i] = (void*)at;
    }
    for (unsigned i = 0; i < 6; i++) *k[i].out = got[i];
    return 1;
}
static int Bodies(uint32_t unit, uint32_t* out, int max)
{
    int n = 0;
    _Alignas(8) uint8_t buf[128] = {0};
    if (!PhysicsBound() || !g_phys.unit_resolve(unit) || g_phys.components(unit, 127, buf) <= 0) return 0;
    for (int list = 0; list < 7; list++)
    {
        uint32_t count = Word(buf + 56 + list * 4);
        uintptr_t ids = (uintptr_t)Quad(buf + list * 8);
        for (uint32_t j = 0; j < count && j < 64 && ids && n < max; j++)
        {
            uint32_t id;
            if (!ReadAt(ids + j * 4ull, &id, 4)) break;
            if ((id & 0x20000000u) && g_phys.valid(id) && g_phys.owner(id) == unit && g_phys.physical(id)) out[n++] = id;
        }
    }
    return n;
}
typedef struct { uint32_t actor; float dir[3]; } Pushed;
static Pushed g_push[256];
static int g_npush;
static double g_pushUntil;
/* 0.4.1b: whether an enemy moves on its own: it has an active navigation record ([game+0x3326418]: map +0x30, records
 * +0x50, 0xA8 each, +0x10 active; Lost Contact's layout). A strider's pilot or seat rides its walker and has none, and the
 * warp setter crashed on one (entity 1414, f96257fcae258caa, 2026-11-05) */
static int Walks(uint32_t e)
{
    uintptr_t m = Pointer(game + 0x3326418), recs;
    uint32_t index, active = 0;
    if (!m || !Lookup(m + 0x30, e, 1048576, &index) || index > 65535 || !(recs = Pointer(m + 0x50))) return 0;
    return ReadAt(recs + 0xA8u * index + 0x10, &active, 4) && (active & 0xFF) != 0;
}
static void MpbFire(void)
{
    float p[3], dir[2], down[3] = {0, 0, -1};
    if (!MechPos(p) || !MechAim(dir)) { Log("Pile Bunker follow-up: the suit's position or aim cannot be read"); return; }
    g_mpbReady = s_mpbUnlock == 0;
    g_mpbFired++;
    float len = s_mpbLength;
    /* its end on the ground (from 10 m above down 30 m) */
    float end[3] = {p[0] + dir[0] * len, p[1] + dir[1] * len, p[2]}, top[3] = {end[0], end[1], p[2] + 10};
    float g = MechRay(0, top, down, 30);
    if (g >= 0 && g <= 30) end[2] = top[2] - g;
    int n = (int)(len / s_pbpStep);
    if (n < 1) n = 1;
    if (n > 30) n = 30;
    Line(p, dir, s_pbpStep, s_pbpStep, n, (uint32_t)s_pbpRow, 0, TAG_PILE, 0);
    double lineEnd = g_now + (s_ripple > 0 ? s_ripple * (n - 1) : 0);
    g_mpbBoomAt = lineEnd + s_mpbHold;
    memcpy(g_mpbBoomPos, end, sizeof end);
    memcpy(g_mpbFrom, p, sizeof p);
    g_mpbDir[0] = dir[0]; g_mpbDir[1] = dir[1];
    /* who is on the line: the living first (nearest the line), then corpses at random */
    g_ndrag = 0;
    int live = 0, dead = 0;
    float off[FOES];
    int pick[FOES], npick = 0;
    for (int i = 0; i < FOES; i++)
    {
        float q[3], t;
        int32_t hp; uint32_t life;
        if (!g_foe[i] || !FoePos(i, q) || !HealthOf(g_foe[i], &hp, &life) || hp <= 0) continue;
        float o = SegDist(q, p, dir, len, &t);
        if (o > s_mpbWidth || t < 1) continue;
        off[npick] = o; pick[npick++] = i;
    }
    for (int a = 0; a < npick && g_ndrag < s_mpbDrag && g_ndrag < 32; a++)
    {
        int best = a;
        for (int b = a + 1; b < npick; b++) if (off[b] < off[best]) best = b;
        float to = off[a]; off[a] = off[best]; off[best] = to;
        int tp = pick[a]; pick[a] = pick[best]; pick[best] = tp;
        uint8_t d[24];
        if (!Descriptor(g_foe[pick[a]], d)) continue;
        /* 0.4.1b: only an enemy this machine simulates (its descriptor's authority bit): the warp setter crashed twice on
         * entities numbered 0x400000 and up (4195360, 4195381: not this machine's), 2026-11-05 */
        if (!(Word(d + 20) & 1) || g_foe[pick[a]] >= 0x400000u || !Walks(g_foe[pick[a]]))
        {
            static unsigned toldSkip;
            if (toldSkip++ < 8) Log("Pile Bunker follow-up: entity %u (%016llx) %s: not dragged", g_foe[pick[a]], (unsigned long long)Quad(d),
                                    Walks(g_foe[pick[a]]) ? "is not simulated here" : "has no navigation record of its own (a rider)");
            continue;
        }
        Dragged* r = &g_drag[g_ndrag++];
        r->e = g_foe[pick[a]]; r->unit = Word(d + 12);
        float ang = (float)g_ndrag * 2.39996f, rad = 1.5f + 0.5f * (float)(g_ndrag % 4);   /* a ring around the end */
        r->to[0] = end[0] + cosf(ang) * rad; r->to[1] = end[1] + sinf(ang) * rad; r->to[2] = end[2];
        live++;
        { static unsigned told; if (told++ < 24) Log("Pile Bunker follow-up: drags entity %u (%016llx)", r->e, (unsigned long long)Quad(d)); }
    }
    g_npush = 0;
    if (g_ndrag < s_mpbDrag)
    {
        unsigned cand[DEAD];
        int nc = 0;
        unsigned total = g_ndead < DEAD ? g_ndead : DEAD;
        for (unsigned i = 0; i < total; i++)
        {
            float q[3];
            uint8_t d[24];
            if (g_now - g_dead[i].at > 60 || !Descriptor(g_dead[i].e, d) || Quad(d) != g_dead[i].res || !UnitNode(Word(d + 12), 0, q)) continue;
            if (SegDist(q, p, dir, len, NULL) <= s_mpbWidth) cand[nc++] = i;
        }
        int want = s_mpbDrag - g_ndrag;
        for (int k = 0; k < want && nc > 0; k++)
        {
            int j = rand() % nc;
            unsigned i = cand[j];
            cand[j] = cand[--nc];
            uint8_t d[24];
            float q[3];
            uint32_t actors[64];
            if (!Descriptor(g_dead[i].e, d) || !UnitNode(Word(d + 12), 0, q)) continue;
            int b = Bodies(Word(d + 12), actors, 64);
            if (!b) continue;
            float v[3] = {end[0] - q[0], end[1] - q[1], 0}, h = sqrtf(v[0] * v[0] + v[1] * v[1]);
            if (h < 0.5f) continue;
            for (int a = 0; a < b && g_npush < 256; a++)
            {
                g_push[g_npush].actor = actors[a];
                g_push[g_npush].dir[0] = v[0] / h; g_push[g_npush].dir[1] = v[1] / h; g_push[g_npush].dir[2] = 0.25f;
                g_npush++;
            }
            dead++;
        }
    }
    g_dragLast = g_now;
    g_dragUntil = g_mpbBoomAt - 0.15;
    g_pushUntil = g_now + 0.4;
    Log("Pile Bunker follow-up: %d strikes over %.0f m, %d living enemies and %d corpses dragged to its end, the Hellbomb in %.1f s",
        n, (double)len, live, dead, g_mpbBoomAt - g_now);
}
static void MpbFrame(void)
{
    double dt = g_now - g_dragLast;
    g_dragLast = g_now;
    if (g_ndrag && g_now < g_dragUntil && dt > 0 && dt <= 0.1 && g_setPosition && QueryIdle())
    {
        float down[3] = {0, 0, -1};
        for (int i = 0; i < g_ndrag; i++)
        {
            Dragged* r = &g_drag[i];
            float q[3];
            int32_t hp; uint32_t life;
            uint8_t d[24];
            if (!r->e || !UnitNode(r->unit, 0, q)) { r->e = 0; continue; }
            /* 0.4.1b: only while it lives and is still the same unit: one that died on the line has no movement record
             * left, and the warp setter crashed on it (2026-11-05, game.dll+0x5A2C69, 8 dragged); a corpse stays put */
            if (!HealthOf(r->e, &hp, &life) || hp <= 0 || !Descriptor(r->e, d) || Word(d + 12) != r->unit)
            {
                static unsigned told;
                if (told++ < 6) Log("Pile Bunker follow-up: entity %u left the drag (%s)", r->e, !HealthOf(r->e, &hp, &life) || hp <= 0 ? "dead" : "gone");
                r->e = 0;
                continue;
            }
            float v[2] = {r->to[0] - q[0], r->to[1] - q[1]}, h = sqrtf(v[0] * v[0] + v[1] * v[1]);
            if (h < 0.3f) continue;
            float left = (float)(g_dragUntil - g_now), speed = h / (left > 0.2f ? left : 0.2f);
            if (speed < 12) speed = 12;
            if (speed > 40) speed = 40;
            float step = speed * (float)dt;
            if (step > h) step = h;
            float dest[3] = {q[0] + v[0] / h * step, q[1] + v[1] / h * step, q[2]}, top[3] = {dest[0], dest[1], q[2] + 2.0f};
            float g = MechRay(r->unit, top, down, 4.0f);
            if (g < 0) return;
            if (g <= 4.0f) dest[2] = top[2] - g;
            g_setPosition(NULL, r->e, dest);
        }
    }
    if (g_npush && g_now < g_pushUntil)
        for (int i = 0; i < g_npush; i++)
        {
            if (!g_phys.valid(g_push[i].actor)) continue;
            float j[4] = {g_push[i].dir[0] * 60, g_push[i].dir[1] * 60, g_push[i].dir[2] * 60, 0};
            g_phys.impulse(g_push[i].actor, j, NULL);
        }
    if (g_mpbBoomAt > 0 && g_now >= g_mpbBoomAt)
    {
        g_mpbBoomAt = -g_now;   /* negative: when it went off (her own Hellbomb spares her and the suit) */
        g_ndrag = 0;
        g_npush = 0;
        int r = Blast_((uint32_t)s_mpbRow, (uint32_t)s_mpbFallback, g_mpbBoomPos, TAG_PILE, 0);
        Log("Pile Bunker follow-up: the Hellbomb (%d) at %.1f %.1f %.1f: %d", s_mpbRow, g_mpbBoomPos[0], g_mpbBoomPos[1], g_mpbBoomPos[2], r);
    }
}
/* her own Hellbomb (and its debris) never hurts her or her suit */
static int MpbSpares(const HfDamageEvent* d)
{
    return g_mpbBoomAt != 0 && (d->kind == 4 || d->kind == 0 || d->kind == 1 || d->kind == 5) && g_now + g_mpbBoomAt < 3.0 && g_mpbBoomAt < 0;
}

/* ---- her GFL1 kit as AK-15, on foot ------------------------------------------------------------------------------- */
static double g_rageUntil, g_akEyeUntil, g_akEyeReady, g_overloadUntil, g_overTick, g_howlUntil;
static float g_overload;
static int g_howled, g_howlWait;
static uint8_t g_akMon[FOES];
static unsigned g_rages, g_howls;
static int Raging(void) { return g_now < g_rageUntil; }
static int AkWatching(void) { return g_now < g_akEyeUntil; }
static int AkMonitoredEntity(uint32_t e)
{
    if (!AkWatching() || !e) return 0;
    for (int i = 0; i < FOES; i++) if (g_foe[i] == e) return g_akMon[i];
    return 0;
}
static void Howl(const char* why, int fromControl)
{
    g_howls++;
    if (fromControl && s_howlFromControl) { g_howlWait = 1; g_howlUntil = g_now + 15; }   /* counted once she can act again */
    else { g_howlWait = 0; g_howlUntil = g_now + s_howlImmune; }
    float me[3], p[3];
    int best = -1;
    int32_t bestHp = 0;
    if (PlayerPose(me, NULL))
        for (int i = 0; i < FOES; i++)
        {
            int32_t hp; uint32_t life;
            if (!g_foe[i] || !HealthOf(g_foe[i], &hp, &life) || hp <= 0 || !FoePos(i, p) || Dist2(p, me) > s_howlRange * s_howlRange) continue;
            if (hp > bestHp) { bestHp = hp; best = i; }
        }
    Log("Blood-Curdling Howl (%s): immune %s%.1f s; the strongest enemy near: %s", why, g_howlWait ? "from when she can act, for " : "for ",
        (double)s_howlImmune, best >= 0 ? "found" : "none");
    if (best < 0 || !s_howlRow || !FoePos(best, p)) return;
    float b[3];
    Up(b, p);
    int n = Raging() ? 2 : 1;
    for (int i = 0; i < n; i++) Blast_((uint32_t)s_howlRow, 0, b, TAG_LOCKON, 0);
    Log("  Howl: %d blast(s) %d on entity %u (health %d)%s", n, s_howlRow, g_foe[best], bestHp, Raging() ? ", raging" : "");
}
static int AkMonitor(int log)
{
    float me[3], p[3];
    int count = 0;
    if (!PlayerPose(me, NULL)) return 0;
    for (int i = 0; i < FOES; i++)
    {
        int32_t hp; uint32_t life;
        int in = g_foe[i] && HealthOf(g_foe[i], &hp, &life) && hp > 0 && FoePos(i, p) && Dist2(p, me) <= s_akEyeRange * s_akEyeRange;
        g_akMon[i] = (uint8_t)in;
        count += in;
    }
    if (log) Log("Eye of the White Mastiff (AK-15): %d enemies monitored for %.0f s", count, (double)s_akEyeSeconds);
    if (!g_howled && count > s_howlTargets) { g_howled = 1; Howl("more than howl_targets enemies monitored", 0); }
    return count;
}
static void AkEye(void)
{
    if (g_now < g_overloadUntil) { Log("Eye (AK-15): overloaded, %.1f s left", g_overloadUntil - g_now); return; }
    if (g_now < g_akEyeReady) { Log("Eye (AK-15): cooling down, %.1f s left", g_akEyeReady - g_now); return; }
    g_akEyeUntil = g_now + s_akEyeSeconds;
    g_akEyeReady = g_now + s_akEyeCd;
    g_overload = 0;
    g_overTick = g_now + 2;
    g_howled = 0;
    AkMonitor(1);
}
static void AkFrame(void)
{
    if (AkWatching())
    {
        static double nextScan;
        if (g_now >= nextScan) { nextScan = g_now + 0.5; AkMonitor(0); }
        if (g_now >= g_overTick)
        {
            g_overTick += 2;
            int n = 0;
            for (int i = 0; i < FOES; i++) n += g_akMon[i];
            g_overload += 5 + n;
            if (g_overload >= s_overloadMax)
            {
                g_akEyeUntil = 0;
                g_overload = 0;
                g_overloadUntil = g_now + s_overloadSeconds;
                Log("Eye (AK-15): OVERLOAD (%d monitored): it ends; her damage -%.0f%% for %.0f s", n, (double)(s_overloadPenalty * 100), (double)s_overloadSeconds);
            }
        }
    }
    /* Howl's immunity from when she can act: out of the seat, and her height steady for 0.3 s (a fall, a get-up) */
    if (g_howlWait)
    {
        static float lastZ;
        static double steadySince;
        float me[3];
        Seat s = ReadSeat();
        int free_ = !g_mech && !(s.found && s.vehicle) && PlayerPose(me, NULL);
        if (free_ && fabsf(me[2] - lastZ) < 0.03f) { if (steadySince <= 0) steadySince = g_now; }
        else steadySince = 0;
        if (free_) lastZ = me[2];
        if ((steadySince > 0 && g_now - steadySince >= 0.3) || g_now >= g_howlUntil)
        {
            g_howlWait = 0;
            g_howlUntil = g_now + s_howlImmune;
            steadySince = 0;
            Log("Howl: she can act again: immune %.1f s more", (double)s_howlImmune);
        }
    }
}
/* her hits on foot: Rage, the Eye's bonus, the overload's penalty */
static void AkHit(HfDamageEvent* d)
{
    float mult = 1;
    if (!Raging() && s_rageChance > 0 && (float)rand() / (float)RAND_MAX < s_rageChance)
    {
        g_rageUntil = g_now + s_rageSeconds;
        if (g_rages++ < 10) Log("Rage: %.0f s", (double)s_rageSeconds);
    }
    if (Raging()) mult += s_rageBonus;
    if (AkMonitoredEntity(d->target)) mult += s_akEyeBonus;
    if (g_now < g_overloadUntil) mult *= 1 - s_overloadPenalty;
    if (mult != 1) { double a = floor(d->amount * (double)mult + 0.5); d->amount = a > 2147483647.0 ? 2147483647 : (int32_t)a; }
}

/* ---- the game thread: which kit, the shield bash, the frames ------------------------------------------------------ */
static void GrantFrame(void);
static void SinPackage(void);
static void SinHide(void);
static void GrantReset(void);
static void SinTune(void);
static void MechWalkFrame(void);
static void KitFrame(void)
{
    SinHide();
    SinPackage();
    GrantFrame();
    static double next;
    if (g_now >= next)
    {
        next = g_now + 0.1;
        Seat s = ReadSeat();
        uint8_t d[24];
        uint32_t mech = 0;
        if (s.found && !s.changing && s.vehicle && Descriptor(s.vehicle, d) && HerMechKind(Quad(d))) mech = s.vehicle;
        if (mech != g_mech)
        {
            if (mech)
            {
                g_mech = mech; g_mechUnit = Word(d + 12);
                MechParts(mech);
                g_akEyeUntil = 0;
                Log("she pilots the %s (entity %u, %d parts): her GFL2 kit", Quad(d) == g_sinRes ? "Sinbreaker" : Quad(d) == g_sinFreeRes ? "free Sinbreaker" : "exosuit", mech, g_nMechParts);
            }
            else
            {
                g_lastMech = g_mech; g_mechLeftAt = g_now;
                g_mech = 0; g_mechUnit = 0; g_mlunge.on = 0;
                g_eyeUntil = 0;
                Log("she is on foot: her AK-15 kit");
            }
        }
    }
    /* the Sinbreaker falls: she is thrown out with a Howl */
    if (g_mech)
    {
        int32_t hp; uint32_t life;
        if (HealthOf(g_mech, &hp, &life) && hp <= 0 && g_now >= g_howlUntil && !g_howlWait) Howl("her Sinbreaker is destroyed", 1);
    }
    else if (g_lastMech && g_now - g_mechLeftAt < 1.0)
    {
        int32_t hp; uint32_t life;
        uint8_t d[24];
        if ((!Descriptor(g_lastMech, d) || (HealthOf(g_lastMech, &hp, &life) && hp <= 0)) && g_now >= g_howlUntil && !g_howlWait)
        { Howl("her Sinbreaker is destroyed", 1); g_lastMech = 0; }
    }
    /* the shield bash (the suit's ability 570) sets off the follow-up while it is ready */
    if (g_mech && s_mpbOn)
    {
        static int was;
        uintptr_t m = Pointer(game + 0x3326640), recs;
        uint32_t i;
        uint8_t r[17];
        int bash = 0;
        if (m && Lookup(m + 24, g_mech, 1048576, &i) && i < 0x10000 && (recs = Pointer(m + 56)) && ReadAt(recs + 224u * i, r, 17))
            bash = Word(r) == 570 && r[16];
        if (bash && !was)
        {
            if (g_mpbReady || s_mpbUnlock == 0) { if (g_mpbFireAt <= 0) g_mpbFireAt = g_now + s_mpbDelay; }   /* after the swing */
            else { static unsigned told; if (told++ < 5) Log("shield bash: no follow-up earned (Pile Bunker must kill a heavily armoured enemy)"); }
        }
        was = bash;
    }
    SinTune();
    if (g_mech)
    {   /* the enemies' units near the suit, so its rays see through them (the leap's list, refreshed 4 times a second) */
        static double nextFoes;
        float mp[3], fp[3];
        uint8_t fid[24];
        if (g_now >= nextFoes && MechPos(mp))
        {
            nextFoes = g_now + 0.25;
            g_nFoeUnits = 0;
            for (int i = 0; i < FOES && g_nFoeUnits < 256; i++)
            {
                uint32_t e = g_foe[i];
                if (!e || !Descriptor(e, fid) || Quad(fid) != g_foeRes[i] || !UnitNode(Word(fid + 12), 0, fp) || Dist2(fp, mp) > 40.0f * 40.0f) continue;
                g_foeUnits[g_nFoeUnits++] = Word(fid + 12);
            }
        }
    }
    if (g_mpbFireAt > 0 && g_now >= g_mpbFireAt) { g_mpbFireAt = 0; if (g_mech) MpbFire(); }
    MechLungeFrame();
    MechWalkFrame();
    MpbFrame();
    AkFrame();
}
static void KitReset(void)
{
    GrantReset();
    g_mech = g_mechUnit = g_lastMech = 0;
    g_mlunge.on = 0;
    g_mpbReady = s_mpbUnlock == 0;
    g_mpbBoomAt = 0;
    g_mpbFireAt = 0;
    g_ndrag = g_npush = 0;
    g_ndead = 0;
    g_akEyeUntil = g_overloadUntil = g_rageUntil = g_howlUntil = 0;
    g_howlWait = 0;
    g_overload = 0;
    memset(g_akMon, 0, sizeof g_akMon);
}
/* the keys: in the Sinbreaker her GFL2 skills; on foot X is the AK-15 Eye and Z, C rest */
static int KitKey(int i)
{
    if (g_mech)
    {
        if (i == 2)
        {
            /* while Pile Bunker recharges, C again sets off its earned follow-up (the Breakthrough's shield bash does too) */
            if (g_now < g_cd[2] && !g_mlunge.on && s_mpbOn && g_mpbFireAt <= 0)
            {
                if (g_mpbReady || s_mpbUnlock == 0) { g_mpbFireAt = g_now; Log("Pile Bunker follow-up: set off with the key"); }
                else { static unsigned told; if (told++ < 5) Log("Pile Bunker again: no follow-up earned (Pile Bunker must kill a heavily armoured enemy)"); }
                return 1;
            }
            MechPileBunker();
            return 1;
        }
        return 0;   /* Sirius Fall and the GFL2 Eye as before */
    }
    if (i == 1) AkEye();
    else { static unsigned told; if (told++ < 3) Log("%s works in the Sinbreaker (on foot: X is her AK-15 Eye)", i == 0 ? "Sirius Fall" : "Pile Bunker"); }
    return 1;
}
/* One Sinbreaker: an entity copied from base (its turn speed and main armour its own) and its LRRRR stratagem, dropped
 * by Pelican. turn: the base's own turn speed (the Breakthrough's 65, read in 0.3.2). */
static void SinbreakerAdd(const HfApi* api, HfMod* mod, uint64_t base, uint32_t baseStratagem, uint64_t package, float turnBase,
                          const char* name, const char* designation, const char* what, uint64_t* res, uint32_t* type)
{
    char ename[64], sname[64];
    snprintf(ename, sizeof ename, "yui/voymastina/%s", name);
    snprintf(sname, sizeof sname, "yui/voymastina/%s_drop", name);
    HfEntity sin = {sizeof sin};
    sin.base = base;
    /* (no own_all: 0.3.5 crashed the game, helldivers2.exe+0xD3E4AC, the moment a Sinbreaker with records of its own in
     * every component spawned; the edits below give it its own Rotation and Health records only) */
    sin.name = ename;
    sin.title = "Sinbreaker";
    sin.designation = designation;
    sin.description = "Voymastina's exoskeleton, built for her by Nele, with a pile bunker for an arm. While she pilots it she fights with "
                      "her full kit; if it falls she is thrown clear with a Blood-Curdling Howl.";
    *res = api->add_entity(mod, &sin);
    if (!*res) { api->log(mod, "the %s could not be added (see the log above)", what); return; }
    /* its own Rotation and Health records come only from edits made here (0.3.2's live edits were refused: "no record of
     * its own"): its turn speed and its main armour; the zones follow live */
    if (api->set_field)
    {
        HfField turn = HF_FIELD(Rotation, rotation_turn_speed), armour = HF_FIELD(Health, armor_118);
        HfValue tv = HF_F32(turnBase * s_sinTurn), a = HF_U32((uint32_t)(s_sinArmor > 0 ? s_sinArmor : 4));
        int rt = s_sinTurn != 1 ? api->set_field(mod, *res, &turn, &tv) : 0, ra = s_sinArmor > 0 ? api->set_field(mod, *res, &armour, &a) : 0;
        api->log(mod, "the %s: turn speed %.0f%s, main armour %d%s", what, (double)(turnBase * s_sinTurn), rt ? " (refused)" : "", s_sinArmor, ra ? " (refused)" : "");
    }
    static uint64_t payloads[2][2];
    uint64_t* payload = payloads[base == HF_ENTITY_EXO55_BREAKTHROUGH_EXOSUIT ? 0 : 1];
    payload[0] = *res;
    payload[1] = HF_ENTITY_PELICAN_TRANSPORT;
    HfStratagem st = {sizeof st};
    st.base = baseStratagem;
    st.name = sname;
    st.title = "Sinbreaker";
    st.description = "Voymastina's own exosuit, dropped by Pelican. Her GFL2 kit runs while she pilots it.";
    st.code = "LRRRR";
    st.cooldown = s_sinCd;
    st.uses = s_sinUses;
    st.payload = payload;
    st.payload_count = 2;
    /* the base suit's own package (its stratagem row's, HD2Runtime's stratagem catalogue): left 0, HellForge took the
     * Pelican's (131fe264b7380717), and a Sinbreaker called outside the loadout landed as a purple question mark */
    st.package = package;
    *type = api->add_stratagem(mod, &st);
    api->log(mod, "the %s: entity %016llx, stratagem %u (LRRRR, %.0f s, %d uses)", what, (unsigned long long)*res, *type, (double)s_sinCd, s_sinUses);
}
/* The Sinbreaker on the EXO-55 Breakthrough (premium: the Exo Experts warbond; the shield bash also sets off Pile
 * Bunker's follow-up), and the free one for everyone else on the EXO-49 Emancipator (sin_free_base 2) or the EXO-45
 * Patriot (1). Each mission she is granted the one her profile may have (GrantFrame). */
static void SinbreakerInit(const HfApi* api, HfMod* mod)
{
    if (!s_sinOn || api->size < offsetof(HfApi, add_stratagem) + sizeof api->add_stratagem || !api->add_entity || !api->add_stratagem) return;
    SinbreakerAdd(api, mod, HF_ENTITY_EXO55_BREAKTHROUGH_EXOSUIT, HF_STRATAGEM_EXO55_BREAKTHROUGH_EXOSUIT,
                  0x7DE417DB7552D9FCull /* packages/generated/loadout/combat_walker_breacher */, 65.0f, "sinbreaker", "EXO-55V", "Sinbreaker", &g_sinRes, &g_sinType);
    if (!s_sinFallback) return;
    /* the free bases' turn speed is not read yet: taken as the Breakthrough's (SinTune logs the base's own) */
    if (s_sinFreeBase == 1)
        SinbreakerAdd(api, mod, HF_ENTITY_EXO45_PATRIOT_EXOSUIT, HF_STRATAGEM_EXO45_PATRIOT,
                      0x22749A294788AF66ull /* packages/generated/loadout/combat_walker */, 65.0f, "sinbreaker_free", "EXO-45V", "free Sinbreaker", &g_sinFreeRes, &g_sinFreeType);
    else
        SinbreakerAdd(api, mod, HF_ENTITY_EXO49_EMANCIPATOR_EXOSUIT, HF_STRATAGEM_EXO49_EMANCIPATOR_EXOSUIT,
                      0xE72D3E9B05C3DB0Bull /* packages/generated/loadout/combat_walker_obsidian */, 65.0f, "sinbreaker_free", "EXO-49V", "free Sinbreaker", &g_sinFreeRes, &g_sinFreeType);
}

static uint32_t MechNow(void) { return g_mech; }
static int MechPart(uint32_t target)
{
    uint32_t by = 0, me = atomic_load(&g_avatar);
    if (!g_mech || !target) return 0;
    return target == g_mech || (WielderOf(target, &by) && by == me && target != me);
}
static double HowlImmuneUntil(void) { return g_howlWait ? 1e30 : g_howlUntil; }

/* ---- the Sinbreaker outside the loadout: one entry appended to her own stratagem record each mission, as the game
 * grants Reinforce and Resupply (granted flag set). The record's layout and the game's append (0x66F190's five writes)
 * are HD2Runtime's research (SkyeShade; docs/research/bonus-stratagem.md, Veterancy System), the record reads as the
 * Veterancy System's Maxigun refund does them in game. The mission loads packages only for the record's entries at its
 * start, so the Sinbreaker's package is held for the session (load_package) */
static int g_grantDone, g_grantTries, g_pkgState;
static double g_grantAt;
static uintptr_t LocalStratagems(void)
{
    uintptr_t recs = Pointer(game + 0x347CE50u), peerObj = Pointer(game + 0x347CEF0u);
    uint64_t me = 0;
    uint32_t n = 0;
    if (!recs || !peerObj || !ReadAt(peerObj + 0xB398, &me, 8) || !me || !ReadAt(recs + 0x2D200, &n, 4) || !n || n > 32) return 0;
    for (uint32_t r = 0; r < n; r++)
    {
        uint64_t peer = 0;
        uintptr_t rec = recs + (uintptr_t)r * 0x1690u;
        if (ReadAt(rec, &peer, 8) && peer == me) return rec + 0x38;
    }
    return 0;
}
static int GrantPins(void)
{
    static int ok = -1;
    if (ok >= 0) return ok;
    static const struct { uintptr_t at; const char* bytes; } k[] = {
        {0x1834AE6, "4C 8B 0D 63 83 C4 01"},   /* the HUD reads the records list */
        {0x1834B07, "48 05 90 16 00 00"},      /* ... 0x1690 a record */
        {0x66D24A, "48 8B 8E A0 01 00 00"},    /* availability reads an entry's cooldown end */
        {0x66F2A7, "89 96 88 01 00 00"},       /* the game's append: entry +0 type */
        {0x66F2CD, "89 86 8C 01 00 00"},       /* +4 uses */
        {0x66F2D7, "88 86 91 01 00 00"},       /* +9 granted */
        {0x66F2EB, "41 89 87 88 07 00 00"}};   /* count + 1 */
    ok = 1;
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++)
        if (!hf->code_matches(game + k[i].at, k[i].bytes)) { Log("Sinbreaker grant: game.dll+0x%llX is not as researched: no grant", (unsigned long long)k[i].at); ok = 0; break; }
    return ok;
}
static int SinOwned(void);
/* The Sinbreaker her profile may have: the Breakthrough one with the Exo Experts warbond, otherwise the free one (0:
 * none). Settled once a session (SinOwned). */
static uint32_t SinGranted(void)
{
    static int told;
    /* sin_fallback 2: the free one even with the warbond (to try it, or by choice) */
    if (g_sinType && s_sinFallback != 2 && SinOwned()) return g_sinType;
    if (!s_sinFallback || !g_sinFreeType) { if (!told++) Log("Sinbreaker grant: no free Sinbreaker (sin_fallback 0): no grant"); return 0; }
    if (!told++) Log("Sinbreaker grant: the free Sinbreaker (no premium content)");
    return g_sinFreeType;
}
static void SinPackage(void)
{
    if (g_pkgState == 2 || !(g_sinType || g_sinFreeType) || !hf->stratagem_row || hf->size < offsetof(HfApi, load_package) + sizeof hf->load_package) return;
    static double next;
    if (g_now < next) return;
    next = g_now + 2;
    uint32_t type = SinGranted();
    if (!type) { g_pkgState = 2; return; }
    uintptr_t row = hf->stratagem_row(type);
    uint64_t pkg = 0;
    if (!row || !ReadAt(row + 0xA8, &pkg, 8) || !pkg || pkg == ~0ull) { if (!g_pkgState) { g_pkgState = 2; Log("Sinbreaker: its stratagem names no package"); } return; }
    int r = hf->load_package(self, pkg);
    if (r == HF_PACKAGE_RESIDENT) { g_pkgState = 2; Log("Sinbreaker: its package %016llx is loaded", (unsigned long long)pkg); }
    else if (!r) { g_pkgState = 2; Log("Sinbreaker: its package %016llx could not be held: a call without it in the loadout may fail", (unsigned long long)pkg); }
    else g_pkgState = 1;
}
/* The Sinbreaker is a copy of the EXO-55 Breakthrough, premium (the Exo Experts warbond, the manifest's "premium"):
 * HellForge keeps it out of the armory of a player who has not bought it, and the grant must do the same. The session's
 * premium-locked.json (in its logs folder) names the mods locked for this player; no file, or Voymastina in it: no
 * grant */
static int SinOwned(void)
{
    static int known = -1;
    if (known >= 0) return known;
    known = 0;
    const char* folder = hf->size >= offsetof(HfApi, logs_folder) + sizeof hf->logs_folder && hf->logs_folder ? hf->logs_folder(self) : "";
    char path[1024];
    if (!folder || !folder[0]) { Log("Sinbreaker grant: the session's logs folder is unknown: taken as not owned (premium content)"); return 0; }
    snprintf(path, sizeof path, "%s\\premium-locked.json", folder);
    FILE* f = OpenUtf8(path, L"rb");
    if (!f) { Log("Sinbreaker grant: %s is unreadable: taken as not owned (premium content)", path); return 0; }
    char buf[8192];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    if (strstr(buf, "yui.voymastina")) { Log("Sinbreaker grant: this profile does not own the Exo Experts warbond (the EXO-55 Breakthrough)"); return 0; }
    known = 1;
    return 1;
}
static void GrantFrame(void)
{
    if (!(g_sinType || g_sinFreeType) || !s_sinGrant || g_grantDone || !g_alive) return;
    if (g_grantAt <= 0) { g_grantAt = g_now + 3; return; }   /* once she is down on the ground a moment */
    if (g_now < g_grantAt) return;
    g_grantAt = g_now + 1;
    if (++g_grantTries > 20) { g_grantDone = 1; Log("Sinbreaker grant: her stratagem record never became readable"); return; }
    if (!GrantPins()) { g_grantDone = 1; return; }
    uint32_t want = SinGranted();
    if (!want) { g_grantDone = 1; return; }
    uintptr_t st = LocalStratagems(), clockObj = Pointer(game + 0x3326348u);
    uint32_t count = 0;
    uint64_t now = 0;
    if (!st || !ReadAt(st + 0x788, &count, 4) || count > 16 || !clockObj || !ReadAt(clockObj + 0x18, &now, 8)) return;
    int tmpl = -1;
    for (uint32_t i = 0; i < count; i++)
    {
        uint8_t e[0x30];
        if (!ReadAt(st + 0x188 + i * 0x30u, e, sizeof e)) return;
        if (Word(e) == want) { g_grantDone = 1; Log("Sinbreaker: already in her stratagems (slot %u)", i); return; }
        if (tmpl < 0 && e[9] == 1 && Quad(e + 0x20) <= now) tmpl = (int)i;
    }
    g_grantDone = 1;
    if (count >= 16) { Log("Sinbreaker grant: her stratagem list is full (16)"); return; }
    uint8_t e[0x30] = {0};
    if (tmpl >= 0) ReadAt(st + 0x188 + (uint32_t)tmpl * 0x30u, e, sizeof e);
    uint32_t type = want, uses = s_sinUses < 0 ? 0xFFFFFFFFu : (uint32_t)s_sinUses;
    uint64_t zero = 0;
    memcpy(e, &type, 4);
    memcpy(e + 4, &uses, 4);
    e[9] = 1;
    memcpy(e + 0x10, &zero, 8);
    memcpy(e + 0x18, &zero, 8);
    memcpy(e + 0x20, &zero, 8);
    uint32_t next = count + 1;
    if (hf->write(st + 0x188 + count * 0x30u, e, sizeof e) || hf->write(st + 0x788, &next, 4)) { Log("Sinbreaker grant: the write failed"); return; }
    Log("%s: granted outside the loadout (slot %u of %u, template slot %d, uses %s): call it with LRRRR", want == g_sinType ? "Sinbreaker" : "the free Sinbreaker", count, next, tmpl,
        s_sinUses < 0 ? "unlimited" : "limited");
}
static void GrantReset(void) { g_grantDone = 0; g_grantTries = 0; g_grantAt = 0; }
/* hide it from the loadout picker (row +0x80 bit 1), sin_loadout 0 */
static void SinHide(void)
{
    static int done;
    if (done || s_sinLoadout || !(g_sinType || g_sinFreeType) || !hf->stratagem_row) return;
    const uint32_t types[2] = {g_sinType, g_sinFreeType};
    for (int k = 0; k < 2; k++)
    {
        if (!types[k]) continue;
        uintptr_t row = hf->stratagem_row(types[k]);
        uint8_t sel;
        if (!row || !ReadAt(row + 0x80, &sel, 1)) return;
        if (sel & 2) { sel &= (uint8_t)~2; hf->write(row + 0x80, &sel, 1); Log("%s: kept out of the loadout picker (sin_loadout 0)", k ? "the free Sinbreaker" : "Sinbreaker"); }
    }
    done = 1;
}

/* ---- her guns on foot (AK-15): +ak_ergo ergonomics and x ak_reload reload time on every weapon she holds, as live edits
 * of the weapon's own records (WeaponData +0x164, WeaponReload +0x38); the game takes them when the weapon is next made
 * (a re-equip, a resupply, her next drop) */
static uint64_t g_tuned[24];
static int g_ntuned;
static unsigned g_tuneNotes;
static void TuneWeapon(uint64_t res)
{
    if (!res || res == AVATAR || (s_akErgo == 0 && s_akReload == 1)) return;
    for (int i = 0; i < g_ntuned; i++) if (g_tuned[i] == res) return;
    if (g_ntuned >= 24) return;
    g_tuned[g_ntuned++] = res;
    HfField ergo = HF_FIELD(WeaponData, ergonomics), reload = HF_FIELD(WeaponReload, duration);
    HfLiveValue v;
    char got[224];
    int k = 0;
    memset(&v, 0, sizeof v);
    /* a weapon with no ergonomics (0: the MG-206 Heavy Machine Gun) is left alone: 0.3.1 crashed the game (game.dll+0x59D679)
     * 1.4 s after editing that gun */
    int haveErgo = hf->get_live(self, res, &ergo, &v), zero = !haveErgo || v.base.number <= 0;
    /* ak_zero_test (finding which edit crashed): 0 leaves such a gun alone, 1 cuts only its reload, 2 only adds ergonomics */
    if (zero && s_akZeroTest == 0)
    {
        if (g_tuneNotes++ < 12) Log("her weapon %016llx (AK-15): stored ergonomics 0 (its attachments carry it): left alone", (unsigned long long)res);
        return;
    }
    if (zero) Log("her weapon %016llx (AK-15): stored ergonomics 0: crash test %d (%s)", (unsigned long long)res, s_akZeroTest,
                  s_akZeroTest == 1 ? "the reload cut only" : s_akZeroTest == 2 ? "the ergonomics only" : "both");
    if (s_akErgo != 0 && haveErgo && !(zero && s_akZeroTest == 1))
    {
        HfValue x = HF_F32(v.base.number + s_akErgo);
        int r = hf->set_live(self, res, &ergo, &x, 0);
        k += snprintf(got + k, sizeof got - k, "ergonomics %.0f -> %.0f%s", v.base.number, v.base.number + s_akErgo, r ? "" : " (refused)");
    }
    memset(&v, 0, sizeof v);
    if (s_akReload != 1 && !(zero && s_akZeroTest == 2) && hf->get_live(self, res, &reload, &v) && v.base.number > 0.01)
    {
        HfValue x = HF_F32(v.base.number * s_akReload);
        int r = hf->set_live(self, res, &reload, &x, 0);
        k += snprintf(got + k, sizeof got - k, "%sreload %.2f -> %.2f s%s", k ? ", " : "", v.base.number, v.base.number * s_akReload, r ? "" : " (refused)");
    }
    /* recoil x ak_recoil: both blocks, sideways and up (WeaponData +0x00 +0x04 +0x1C +0x20, as Stat Tuner's recoil slot);
     * not on a gun under the crash test, so the test stays one edit */
    if (s_akRecoil != 1 && (!zero || s_akZeroTest == 3))
    {
        static const HfField rec[4] = {HF_FIELD(WeaponData, recoil_drift_horizontal), HF_FIELD(WeaponData, recoil_drift_vertical),
                                       HF_FIELD(WeaponData, recoil_climb_horizontal), HF_FIELD(WeaponData, recoil_climb_vertical)};
        int n = 0, set = 0;
        for (int i = 0; i < 4; i++)
        {
            memset(&v, 0, sizeof v);
            if (!hf->get_live(self, res, &rec[i], &v) || v.base.number == 0) continue;
            n++;
            HfValue x = HF_F32(v.base.number * s_akRecoil);
            if (hf->set_live(self, res, &rec[i], &x, 0)) set++;
        }
        if (n) k += snprintf(got + k, sizeof got - k, "%srecoil x%.2f on %d of %d", k ? ", " : "", (double)s_akRecoil, set, n);
    }
    if (g_tuneNotes++ < 12) Log("her weapon %016llx (AK-15): %s", (unsigned long long)res, k ? got : "no ergonomics or reload record");
}
static void OnWeaponAk(HfMod* mod, const HfEventInfo* info, void* user)
{
    if (!kb_kit_on) return;
    const HfWeaponEvent* w = (const HfWeaponEvent*)info;
    if (!w->avatar || w->avatar != atomic_load(&g_avatar)) return;
    TuneWeapon(w->resource);
}

/* ---- the Sinbreaker's handling and armour: live edits of its own records (own_all) and its arms' --------------------
 * turning (Rotation +0/+4/+8, VehicleMotion steering response) x sin_turn; armour: the main value and every armoured
 * zone (HealthComponentData zone i at +0x2E0 + 0x228 i) raised to sin_armor, on the suit and on what its Mount carries
 * (the arms are vanilla entities: a Breakthrough's arms get it too) */
static int g_sinTuned;
static int RaiseArmour(uint64_t res, const char* what)
{
    int raised = 0, zones = 0;
    for (int i = -1; i < 38; i++)
    {
        uint32_t off = i < 0 ? 0x118u : 0x2E0u + 0x228u * (uint32_t)i;
        HfField f = {HfHealth_INFO, (uint16_t)off, 4, HF_TYPE_U32, __LINE__};
        HfLiveValue v;
        memset(&v, 0, sizeof v);
        if (!hf->get_live(self, res, &f, &v)) continue;
        double b = v.base.number;
        if (b < 1 || b >= s_sinArmor || b > 10) continue;
        zones++;
        HfValue x = HF_U32((uint32_t)s_sinArmor);
        if (hf->set_live(self, res, &f, &x, 0) || hf->set_live(self, res, &f, &x, HF_LIVE_SHARED)) raised++;
    }
    Log("Sinbreaker: %s %016llx: armour %d on %d of %d armoured values", what, (unsigned long long)res, s_sinArmor, raised, zones);
    return raised;
}
static void SinTuneOne(uint64_t res, const char* what)
{
    static const struct { HfField f; const char* name; } turn[] = {
        {HF_FIELD(Rotation, rotation_turn_speed), "turn speed"}, {HF_FIELD(Rotation, rotation_acceleration), "turn acceleration"},
        {HF_FIELD(Rotation, rotation_deceleration), "turn deceleration"}, {HF_FIELD(VehicleMotion, steering_response_speed), "steering response"}};
    for (unsigned i = 1; i < sizeof turn / sizeof turn[0] && s_sinTurn != 1; i++)   /* (the turn speed: set at init) */
    {
        HfLiveValue v;
        memset(&v, 0, sizeof v);
        if (!hf->get_live(self, res, &turn[i].f, &v) || v.base.number <= 0) { Log("%s: no %s to change", what, turn[i].name); continue; }
        HfValue x = HF_F32(v.base.number * s_sinTurn);
        int r = hf->set_live(self, res, &turn[i].f, &x, 0);
        Log("%s: %s %.2f -> %.2f%s", what, turn[i].name, v.base.number, v.base.number * s_sinTurn, r ? "" : " (refused)");
    }
    if (s_sinArmor <= 0) return;
    RaiseArmour(res, what);
    uint32_t stride = 0;
    uintptr_t m = hf->component_record("MountComponentData", res, &stride);
    for (int k = 0; m && k < 5; k++)
    {
        uint64_t arm = 0;
        if (ReadAt(m + 24u * (uint32_t)k, &arm, 8) && arm) RaiseArmour(arm, "its mount");
    }
}
/* both Sinbreakers, once, in the first mission frame; and the free base's own turn speed, for the record */
static void SinTune(void)
{
    if (g_sinTuned || !(g_sinRes || g_sinFreeRes)) return;
    g_sinTuned = 1;
    if (g_sinRes) SinTuneOne(g_sinRes, "Sinbreaker");
    if (g_sinFreeRes)
    {
        SinTuneOne(g_sinFreeRes, "free Sinbreaker");
        HfField turn = HF_FIELD(Rotation, rotation_turn_speed);
        HfLiveValue v;
        memset(&v, 0, sizeof v);
        uint64_t base = s_sinFreeBase == 1 ? HF_ENTITY_EXO45_PATRIOT_EXOSUIT : HF_ENTITY_EXO49_EMANCIPATOR_EXOSUIT;
        if (hf->get_live(self, base, &turn, &v)) Log("free Sinbreaker: its base's own turn speed %.1f (set from 65 x sin_turn)", v.base.number);
    }
}

/* ---- her walk in the Sinbreaker: x sin_speed. The suit's own movement each update (measured from where it stood after
 * the last push) gets (sin_speed - 1) of it more, through the warp setter, stopping at walls and following the ground */
static float g_walkPrev[3];
static double g_walkPrevT;
static int g_walkHave;
static void MechWalkFrame(void)
{
    float p[3];
    if (!g_mech || s_sinSpeed <= 1.001f || g_mlunge.on || !g_setPosition || !MechPos(p)) { g_walkHave = 0; return; }
    double dt = g_now - g_walkPrevT;
    if (!g_walkHave || dt <= 0 || dt > 0.1) { memcpy(g_walkPrev, p, sizeof p); g_walkPrevT = g_now; g_walkHave = 1; return; }
    float v[2] = {(p[0] - g_walkPrev[0]) / (float)dt, (p[1] - g_walkPrev[1]) / (float)dt}, sp = sqrtf(v[0] * v[0] + v[1] * v[1]);
    memcpy(g_walkPrev, p, sizeof p);
    g_walkPrevT = g_now;
    if (sp < 0.8f || sp > 12.0f) return;                    /* standing, or thrown (a blast, a fall) */
    float step = (s_sinSpeed - 1.0f) * sp * (float)dt;
    float dir[3] = {v[0] / sp, v[1] / sp, 0}, down[3] = {0, 0, -1};
    for (int h = 0; h < 2; h++)
    {
        float from[3] = {p[0], p[1], p[2] + (h ? 2.4f : 1.0f)};
        float w = MechRay(g_mechUnit, from, dir, step + 1.5f);
        if (w < 0 || w <= step + 1.5f) return;
    }
    float dest[3] = {p[0] + dir[0] * step, p[1] + dir[1] * step, p[2]}, top[3] = {dest[0], dest[1], p[2] + 1.5f};
    float g = MechRay(g_mechUnit, top, down, 3.0f);
    if (g < 0 || g > 3.0f) return;
    dest[2] = top[2] - g;
    if (dest[2] - p[2] > 0.5f) return;                       /* never a step up: the game's own walk climbs, the push does not */
    g_setPosition(NULL, g_mech, dest);
    memcpy(g_walkPrev, dest, sizeof dest);                   /* its own movement is measured from here next update */
}

static void EntitiesReady(HfMod* mod, HfEvent event, void* user) {}

/* ---- the HUD state (read by the Voymastina HUD addon through LuaJIT's ffi; keep in step with its cdef) ------------- */
typedef struct VoymastinaHud
{
    uint32_t version, in_mission;
    float cd[3], cd_max[3];          /* seconds left, full cooldowns: Sirius Fall, Eye of the White Mastiff, Pile Bunker */
    uint32_t pp, pp_max, empowered, broken;
    float stab, stab_max;            /* Stability now and its most */
    float eye, follow, shield;       /* seconds left: the Eye's overwatch, Pile Bunker's follow-up window, the drop-in shield */
    uint32_t eye_shots, eye_max, keys[3];
    char key_label[3][24];
    float stamina;                   /* her stamina 0..1 (-1: unknown) */
    uint32_t hellbomb;               /* 1 when running dry sets off the Hellbomb */
    /* version 2: the passives' internal cooldowns (seconds left, full; on: switched on): Support Action, the charge */
    float pas[2], pas_max[2];
    uint32_t pas_on[2];
    /* version 3: her two kits */
    uint32_t mode;                   /* 0 on foot (her AK-15 kit), 1 in the Sinbreaker (her GFL2 kit) */
    uint32_t mpb_ready, monitored, mpb_on;
    float ak_cd, ak_cd_max, ak_eye, overload, overload_max, overload_lock, rage, howl;
} VoymastinaHud;
static VoymastinaHud g_hud = {3};
HF_MOD_EXPORT VoymastinaHud* voymastina_hud_state(void)
{
    g_hud.in_mission = (uint32_t)(g_inMission && g_alive && kb_kit_on);
    double r[3] = {g_cd[0], g_cd[1], g_cd[2]};
    float m[3] = {s_sfCd, s_eyeCd, s_pbCd};
    for (int i = 0; i < 3; i++) { g_hud.cd[i] = (float)fmax(0, r[i] - g_now); g_hud.cd_max[i] = m[i]; }
    g_hud.pp = (uint32_t)g_pp;
    g_hud.pp_max = (uint32_t)s_ppMax;
    g_hud.empowered = (uint32_t)g_ppReady;
    g_hud.broken = (uint32_t)g_broken;
    g_hud.stab = g_stab;
    g_hud.stab_max = s_stabMax;
    g_hud.eye = (float)fmax(0, g_eyeUntil - g_now);
    g_hud.follow = (float)fmax(0, g_pbFollowUntil - g_now);
    g_hud.shield = g_dropShieldOn ? (float)fmax(0, g_immuneUntil - g_now) : 0;
    g_hud.eye_shots = (uint32_t)g_eyeShots;
    g_hud.eye_max = (uint32_t)s_eyeMax;
    for (int i = 0; i < 3; i++)
    {
        g_hud.keys[i] = (uint32_t)s_keys[i].vk;
        memcpy(g_hud.key_label[i], s_keys[i].label, sizeof g_hud.key_label[i]);
    }
    g_hud.stamina = g_inMission && g_alive ? StaminaNow() : -1;
    g_hud.hellbomb = (uint32_t)(s_hellbomb != 0);
    g_hud.pas[0] = (float)fmax(0, g_saAt + s_saEvery - g_now); g_hud.pas_max[0] = s_saEvery; g_hud.pas_on[0] = (uint32_t)(s_saOn && s_saRow);
    g_hud.pas[1] = (float)fmax(0, g_pbpAt + s_pbpEvery - g_now); g_hud.pas_max[1] = s_pbpEvery; g_hud.pas_on[1] = 0;   /* the charge: gone since 0.3.0 */
    g_hud.mode = g_mech ? 1 : 0;
    g_hud.mpb_on = (uint32_t)(s_mpbOn != 0);
    g_hud.mpb_ready = (uint32_t)(g_mpbReady || s_mpbUnlock == 0);
    unsigned mon = 0;
    if (AkWatching()) for (int i = 0; i < FOES; i++) mon += g_akMon[i];
    g_hud.monitored = mon;
    g_hud.ak_cd = (float)fmax(0, fmax(g_akEyeReady, g_overloadUntil) - g_now);
    g_hud.ak_cd_max = g_now < g_overloadUntil ? s_overloadSeconds : s_akEyeCd;
    g_hud.ak_eye = (float)fmax(0, g_akEyeUntil - g_now);
    g_hud.overload = g_overload;
    g_hud.overload_max = s_overloadMax;
    g_hud.overload_lock = (float)fmax(0, g_overloadUntil - g_now);
    g_hud.rage = (float)fmax(0, g_rageUntil - g_now);
    g_hud.howl = g_howlWait ? 99.0f : (float)fmax(0, g_howlUntil - g_now);
    return &g_hud;
}

/* Her bare hands' speed: jog and sprint x bare_speed while they are on (the Helldiver's own speeds put back after) */
static float g_baseJog = -1, g_baseSprint = -1;
static int g_speedOn;
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
        if (!hf->get_live(self, AVATAR, &jog, &a) || !hf->get_live(self, AVATAR, &sprint, &b)) return;
        g_baseJog = (float)a.base.number;
        g_baseSprint = (float)b.base.number;
    }
    HfValue j = HF_F32(fminf(12, g_baseJog * s_bareSpeed)), s = HF_F32(fminf(12, g_baseSprint * s_bareSpeed));
    hf->set_live(self, AVATAR, &jog, &j, HF_LIVE_SHARED);
    hf->set_live(self, AVATAR, &sprint, &s, HF_LIVE_SHARED);
    g_speedOn = 1;
}
/* The HUD tells her ammo (HD2Runtime reads magazine guns): version 2, per slot 2 bits, primary in bits 0-1, secondary in
 * bits 2-3: 0 loaded, 1 empty, 2 not readable there (she reads shell-fed guns herself) */
static atomic_int g_ammoHud = 0;
static _Atomic unsigned long long g_ammoMs;
HF_MOD_EXPORT int voymastina_ammo(unsigned version, int state)
{
    if (version != 2) return 0;
    static int first = 1;
    if (first) { first = 0; Log("her ammo: the HUD reports it (state %d)", state); }
    atomic_store(&g_ammoHud, state);
    atomic_store(&g_ammoMs, GetTickCount64());
    return 1;
}
static int SlotEmpty(int slot, int hud);
static void BareFrame(void)
{
    float me[3];
    if (PlayerPose(me, NULL)) memcpy(g_mePos, me, sizeof me);
    unsigned long long seen = atomic_load(&g_ammoMs);
    int hud = atomic_load(&g_ammoHud);
    int on = s_bare && g_alive && !g_broken && seen && GetTickCount64() - seen < 2000 && SlotEmpty(0, hud & 3) && SlotEmpty(1, hud >> 2 & 3);
    if (on != atomic_load(&g_bareOn))
    {
        atomic_store(&g_bareOn, on);
        Speed(on);
        static unsigned told;
        if (told++ < 6) Log("%s", on ? "her hands: on" : "her hands: off");
    }
}
/* ---- shell-loaded guns (the SG-97 Sweeper and the other rounds-fed weapons): HD2Runtime reads magazines only, so she
 * reads these herself. Found with Voymastina 0.2.3/0.2.4's probes (2026-11-01, SG-97 Sweeper):
 *   - her weapons: the inventory manager game+53634872 (HD2Runtime's research): hash +40, descriptors +64, 48-byte
 *     records at +80, primary +0, secondary +4
 *   - the weapon manager game+0x3326660: hash +40, 40-byte records at +80, flags +0 (256: fed by WeaponRounds)
 *   - the WeaponRounds manager game+0x3326CF0: hash +40, descriptors +64; 24-byte records at +80 with the loaded rounds
 *     at +4 (the array is 64 x 24 bytes plus 16 before the next one); a second array at +88 holds the spare rounds at +0
 *     and the loaded rounds again at +8 (its stride is taken as the one whose +8 agrees with the record's rounds) */
typedef struct { uintptr_t global; uint32_t hashOff, descOff, index; uint64_t type; } ProbeComp;
static int InventoryWeapon(uint32_t avatar, int slot, uint32_t* out)
{
    uintptr_t m = Pointer(game + 53634872u), arr, d, recs;
    uint32_t i;
    uint8_t desc[24], rec[48];
    if (!m || !Lookup(m + 40, avatar, 1048576, &i) || i >= 0x10000 || !(arr = Pointer(m + 64)) || !(d = Pointer(arr + 8u * i)) ||
        !ReadAt(d, desc, 24) || Word(desc + 8) != avatar || !(recs = Pointer(m + 80)) || !ReadAt(recs + 48u * i, rec, 48))
        return 0;
    *out = Word(rec + 4u * slot);
    return *out != 0;
}
static int CompOf(uintptr_t m, uint32_t hashOff, uint32_t entity, ProbeComp* c)
{
    uint32_t i;
    if (!Lookup(m + hashOff, entity, 1048576, &i) || i >= 0x10000) return 0;
    static const uint32_t gaps[] = {24, 16, 32, 8};
    for (int g = 0; g < 4; g++)
    {
        uintptr_t arr = Pointer(m + hashOff + gaps[g]), d;
        uint8_t desc[24];
        if (!arr || !(d = Pointer(arr + 8u * i)) || !ReadAt(d, desc, 24) || Word(desc + 8) != entity) continue;
        c->hashOff = hashOff; c->descOff = hashOff + gaps[g]; c->index = i; c->type = Quad(desc);
        return 1;
    }
    return 0;
}
/* 1 empty (no rounds loaded, none spare), 2 a weapon with no ammunition at all (a melee weapon; *loaded = its flags),
 * 0 loaded, -1 not a rounds-fed weapon or unreadable */
static int RoundsEmpty(uint32_t weapon, uint32_t* loaded, uint32_t* spare)
{
    ProbeComp c;
    uintptr_t wm = Pointer(game + 0x3326660u), wrecs, m, recs, live;
    uint32_t flags = 0;
    if (!wm || !CompOf(wm, 40, weapon, &c) || !(wrecs = Pointer(wm + 80)) || !ReadAt(wrecs + 40u * c.index, &flags, 4)) return -1;
    *loaded = flags;
    /* no magazine (128), rounds (256), heat (512) or linked ammunition (1024: a backpack's): nothing to run out of, as
     * the CQC-19 Stun Lance (2026-11-02): counts as empty */
    if (!(flags & (128 | 256 | 512 | 1024))) return 2;
    /* heat only (the LAS-12 Sai, the other lasers): the WeaponHeat manager game+0x3326D48 (hash +40, descriptors +64) and
     * its 12-byte live records at +88: spare heatsinks +0, heat +4 (0-100), +8 set while overheated until a heatsink goes
     * in (0.2.9's heat probe). Empty: no heatsink left and overheated. Unreadable: counts as empty, as 0.2.9 did */
    if ((flags & 512) && !(flags & (128 | 256 | 1024)))
    {
        uintptr_t hm = Pointer(game + 0x3326D48u), live;
        uint8_t rec[12];
        if (!hm || !CompOf(hm, 40, weapon, &c) || c.descOff != 64 || c.index >= 256 || !(live = Pointer(hm + 88)) ||
            !ReadAt(live + 12u * c.index, rec, 12) || Word(rec) > 100)
            return 3;
        *loaded = rec[8];
        *spare = Word(rec);
        return Word(rec) == 0 && rec[8] ? 5 : 4;
    }
    if (!(flags & 256)) return -1;
    if (!(m = Pointer(game + 0x3326CF0u)) || !CompOf(m, 40, weapon, &c) || c.descOff != 64 || c.index >= 64 ||
        !(recs = Pointer(m + 80)) || !(live = Pointer(m + 88)))
        return -1;
    uint32_t r = 0;
    if (!ReadAt(recs + 24u * c.index + 4, &r, 4) || r > 1000) return -1;
    static const uint32_t strides[] = {24, 16, 32, 20, 28, 40, 48, 64};
    for (int k = 0; k < 8; k++)
    {
        uint32_t v[3];
        if (!ReadAt(live + (uintptr_t)strides[k] * c.index, v, 12) || v[2] != r || v[0] > 10000) continue;
        *loaded = r; *spare = v[0];
        return r == 0 && v[0] == 0;
    }
    return -1;
}
/* Whether a slot is empty: the HUD's magazine reading (1 empty, 0 loaded, 2 unreadable there) or her own rounds reading */
static int SlotEmpty(int slot, int hud)
{
    if (hud == 1) return 1;
    if (hud == 0) return 0;
    uint32_t me = atomic_load(&g_avatar), w, loaded = 0, spare = 0;
    if (!me || !InventoryWeapon(me, slot, &w)) return 0;
    int e = RoundsEmpty(w, &loaded, &spare);
    static int last[2] = {-2, -2};
    static unsigned told;
    if (e != last[slot] && told < 20)
    {
        told++;
        last[slot] = e;
        if (e < 0) Log("her ammo: the %s can't be read (not magazine- or shell-fed)", slot ? "secondary" : "primary");
        else if (e == 2) Log("her ammo: the %s uses no ammunition (weapon flags 0x%X): counts as empty", slot ? "secondary" : "primary", loaded);
        else if (e == 3) Log("her ammo: the %s is cooled by heat and its heatsinks can't be read (weapon flags 0x%X): counts as empty", slot ? "secondary" : "primary", loaded);
        else if (e >= 4) Log("her ammo: the %s is cooled by heat: %u heatsinks spare%s", slot ? "secondary" : "primary", spare, e == 5 ? ", overheated (empty)" : loaded ? ", overheated" : "");
        else Log("her ammo: the %s is shell-fed: %u loaded, %u spare%s", slot ? "secondary" : "primary", loaded, spare, e ? " (empty)" : "");
    }
    return e == 1 || e == 2 || e == 3 || e == 5;
}
/* ---- in-game key bindings: the HUD registers her three skills with Mod Bindings Menu (Vanilla Plus Megapack: the MODS
 * tab of the binding pages) and passes each press here (and a heartbeat twice a second); the .ini's keys keep working */
static _Atomic unsigned long long g_bindMs;
static atomic_int g_bindPress;
HF_MOD_EXPORT int voymastina_input(unsigned version, int skill)
{
    if (version != 1) return 0;
    atomic_store(&g_bindMs, GetTickCount64());
    if (skill >= 1 && skill <= 3) atomic_fetch_or(&g_bindPress, 1 << (skill - 1));
    return 1;
}
/* A skill's .ini key for the binding's default: its virtual-key code and modifiers (1 Shift, 2 Ctrl, 4 Alt) */
HF_MOD_EXPORT int voymastina_key(unsigned version, int skill, int* vk, int* mods)
{
    if (version != 1 || skill < 1 || skill > 3 || !vk || !mods) return 0;
    *vk = s_keys[skill - 1].vk;
    *mods = s_keys[skill - 1].mods;
    return 1;
}
/* ---- her fist on walls, rocks and the ground ahead: no damage event comes from those, so the swing itself is watched.
 * The ability she runs (HD2Runtime's research: the ability manager game+0x3326640, hash +24, descriptors +48, 224-byte
 * records at +56, id +0, active +16) is learned as a melee one when a melee hit of hers lands while it runs; in bare
 * hands, each such swing that has not hit an enemy casts a 2.5 m ray from her head along her aim from 0.12 s on, and the
 * first thing it meets (terrain, a wall, a prop) takes the fist's grenade blast */
static uint32_t g_meleeIds[8];
static int g_nMeleeIds;
static int RunningAbility(uint32_t avatar, uint32_t* id, int* active)
{
    ProbeComp c;
    uintptr_t m = Pointer(game + 0x3326640u), recs;
    uint8_t r[17];
    if (!avatar || !m || !CompOf(m, 24, avatar, &c) || c.descOff != 48 || !(recs = Pointer(m + 56)) || !ReadAt(recs + 224u * c.index, r, 17)) return 0;
    *id = Word(r);
    *active = r[16] != 0;
    return 1;
}
static int MeleeId(uint32_t id)
{
    for (int i = 0; i < g_nMeleeIds; i++) if (g_meleeIds[i] == id) return 1;
    return 0;
}
static void LearnMelee(void)
{
    uint32_t id;
    int act;
    if (!RunningAbility(atomic_load(&g_avatar), &id, &act) || !act || !id || MeleeId(id) || g_nMeleeIds >= 8) return;
    g_meleeIds[g_nMeleeIds++] = id;
    Log("her melee: ability %u learned (a melee hit of hers landed while it ran)", id);
}
static void FistFrame(void)
{
    static int was, done;
    static double since;
    uint32_t me = atomic_load(&g_avatar), id;
    int act;
    if (!s_bare || !atomic_load(&g_bareOn) || s_bareBlast <= 0 || !g_nMeleeIds || !RunningAbility(me, &id, &act)) { was = 0; return; }
    int melee = act && MeleeId(id);
    if (melee && !was) { since = g_now; done = 0; }
    was = melee;
    if (!melee || done || g_now - since < 0.12 || g_now - since > 0.6) return;
    if (g_fistAt >= since - 0.05) { done = 1; return; }          /* it hit an enemy: that blast is out already */
    uint8_t d[24];
    float head[3], ahead[3];
    if (!Descriptor(me, d) || Quad(d) != AVATAR || !UnitNode(Word(d + 12), NODE_HEAD, head) || !UnitNode(Word(d + 12), NODE_HEAD_AIM, ahead)) return;
    float dir[3] = {ahead[0] - head[0], ahead[1] - head[1], ahead[2] - head[2]};
    float n = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (!(n > 0.1f)) return;
    for (int i = 0; i < 3; i++) dir[i] /= n;
    float r = RayWith(Word(d + 12), head, dir, 2.5f, 0, 0x80000009u, NULL);
    /* nothing within reach: punching down into an opening (a bug hole) still goes off 1.5 m ahead, inside it; a swing
     * at the air does not */
    if (r < 0 || r > 2.5f)
    {
        if (r < 0 || dir[2] > -0.25f) return;
        r = 1.65f;
    }
    float at[3] = {head[0] + dir[0] * (r - 0.15f), head[1] + dir[1] * (r - 0.15f), head[2] + dir[2] * (r - 0.15f)};
    done = 1;
    g_fistAt = g_now;
    FistBlast(at);
    static unsigned told;
    if (told++ < 5) Log("her hand: a blast %d on what she struck, %.1f m ahead", s_bareBlast, (double)r);
}
static void FistBoltReady(HfMod* mod, HfEvent event, void* user)
{
    HfDamageInfo* dmg = g_fistBoltDamage ? (HfDamageInfo*)hf->damage_row(g_fistBoltDamage) : NULL;
    HfProjectileSettings* row = g_fistBolt ? (HfProjectileSettings*)hf->projectile_row(g_fistBolt) : NULL;
    if (dmg && row)
    {
        dmg->standard_damage = 0; dmg->durable_damage = 0; dmg->demolition = 0; dmg->stagger = 0; dmg->push_force = 0;
        row->impact_explosion = (uint32_t)s_bareBlast;
        g_fistBoltReady = 1;
    }
    else g_fistBolt = 0;
}
static void Tick(HfMod* mod, float dt, void* user)
{
    float step = dt > 0 && dt < 1 ? dt : 0;
    g_now += step;
    int inMission = hf->game_state(self) == HF_STATE_MISSION;
    if (inMission != g_inMission) { Reset(inMission ? "mission start" : "mission end"); KitReset(); g_inMission = inMission; }
    if (!g_inMission || !kb_kit_on) return;   /* (standing by: another kit is picked) */
    atomic_store(&g_ppArmorOn, g_ppReady || g_now < g_ppArmorUntil);
    StaminaGuard();
    BareFrame();
    FistFrame();
    MyItems();
    MoveTrack();
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
        int own = s_keys[i].vk == VK_SHIFT ? KM_SHIFT : s_keys[i].vk == VK_CONTROL ? KM_CTRL : s_keys[i].vk == VK_MENU ? KM_ALT : 0;
        /* both the .ini's key and the binding fire it; one press within 0.3 s of the other is the same press */
        int down = focused && (GetAsyncKeyState(s_keys[i].vk) & 0x8000) != 0 && (mods & ~own) == s_keys[i].mods;
        static unsigned long long fired[3];
        int press = (down && !prev[i]) || (presses >> i & 1);
        {   /* which source a press came from, the first presses (a binding seemed not to fire on foot) */
            static unsigned told;
            if ((press || (presses >> i & 1)) && told < 30)
            {
                told++;
                Log("key %d (%s): %s%s%s; %s, %s", i + 1, i == 0 ? "Sirius Fall" : i == 1 ? "Eye" : "Pile Bunker", (down && !prev[i]) ? "the .ini key" : "",
                    (down && !prev[i]) && (presses >> i & 1) ? " and " : "", (presses >> i & 1) ? "the binding" : "", MechNow() ? "in the Sinbreaker" : "on foot",
                    !g_alive ? "not alive: ignored" : GetTickCount64() - fired[i] <= 300 ? "within 0.3 s of the last: one press" : "fired");
            }
        }
        if (press && GetTickCount64() - fired[i] > 300 && g_alive && atomic_load(&g_avatar) && (fired[i] = GetTickCount64(), 1))
        {
            if (KitKey(i)) {}
            else if (i == 0) SiriusFall();
            else if (i == 1) EyeOfTheWhiteMastiff();
            else PileBunker();
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
        if (t) { g_lastTarget = t; memcpy(g_lastPos, g_hookPos, sizeof g_lastPos); g_lastAt = g_now; }
    }
    KitFrame();
    RetryFrame();
    SupportFrame();
    DashFrame();
    ChargeFrame();
    RippleFrame();
    EyeFrame();
    DropFrame(step);
    Exhaustion();
    /* Stability refills after stab_delay s unhurt; broken until it is full again */
    if (g_now - g_stabHitAt >= s_stabDelay && g_stab < s_stabMax)
    {
        g_stab += s_stabRegen * step;
        if (g_stab >= s_stabMax) { g_stab = s_stabMax; if (g_broken) { g_broken = 0; static unsigned noted; if (noted++ < 5) Log("Stability: restored"); } }
    }
    if (g_stab > s_stabMax) g_stab = s_stabMax;
    g_pierced += atomic_exchange(&g_piercedHook, 0);
    unsigned own = atomic_exchange(&g_ownStopped, 0);
    if (own) { g_selfSpared += own; static unsigned noted; if (noted++ < 5) Log("Mastiff's Pledge: %u hit(s) of her own blasts cleared on her", own); }
    unsigned kd = atomic_exchange(&g_kdStopped, 0);
    if (kd) { g_knockdowns += kd; static unsigned noted2; if (noted2++ < 20) Log("Mastiff's Pledge: %u knockdown(s) stopped", kd); }
}

/* ---- the Mod Options Menu (through the Voymastina HUD addon): read and set a setting by its voymastina.ini name ---- */
static const KbOption kOptions[] = {
    {"sf_cooldown", &s_sfCd, NULL}, {"sa_on", NULL, &s_saOn}, {"sa_every", &s_saEvery, NULL},
    {"eye_cooldown", &s_eyeCd, NULL}, {"eye_seconds", &s_eyeSeconds, NULL}, {"eye_radius", &s_eyeRadius, NULL}, {"eye_max", NULL, &s_eyeMax}, {"eye_move", &s_eyeMove, NULL},
    {"drop_on", NULL, &s_dropOn}, {"drop_targets", NULL, &s_dropTargets},
    {"pb_cooldown", &s_pbCd, NULL}, {"pb_lunge", &s_pbLunge, NULL}, {"pb_window", &s_pbWindow, NULL}, {"pb_dash", &s_pbDash, NULL},
    {"pbp_on", NULL, &s_pbpOn}, {"pbp_radius", &s_pbpRadius, NULL}, {"pbp_distance", &s_pbpDistance, NULL}, {"pbp_points", NULL, &s_pbpPoints}, {"pbp_every", &s_pbpEvery, NULL},
    {"steady", NULL, &s_steady}, {"dr_base", &s_drBase, NULL}, {"dr_stable", &s_drStable, NULL},
    {"stab_max", &s_stabMax, NULL}, {"stab_per_damage", &s_stabPer, NULL}, {"stab_regen", &s_stabRegen, NULL}, {"stab_delay", &s_stabDelay, NULL},
    {"ap_bonus", NULL, &s_apBonus}, {"pp_max", NULL, &s_ppMax}, {"pp_mult", &s_ppMult, NULL},
    {"hp_mult", &s_hpMult, NULL}, {"stamina_mult", &s_staminaMult, NULL}, {"hellbomb_on_exhaustion", NULL, &s_hellbomb},
    /* the rest is read (not shown on the MODS tab: it takes 32 rows a mod) */
    {"sf_range", &s_sfRange, NULL}, {"eye_every", &s_eyeEvery, NULL}, {"drop_radius", &s_dropRadius, NULL}, {"drop_shield", &s_dropShield, NULL},
    {"pbp_step", &s_pbpStep, NULL}, {"stab_min_hit", &s_stabMin, NULL}, {"pp_per", NULL, &s_ppPer}, {"pp_ap", NULL, &s_ppAp},
};
/* After Mod Options Menu sets one (changed: its value moved) */
static void OptionAfter(const char* name, int changed)
{
    if (changed && !strcmp(name, "stab_max") && (g_stab > s_stabMax || !g_inMission)) g_stab = s_stabMax;
    if (g_pp >= s_ppMax) { g_pp = 0; g_ppReady = 1; }   /* a lower most: the charge is full */
}
HF_MOD_EXPORT int voymastina_option_get(const char* name, double* out)
{
    return OptionGet(name, out);
}
HF_MOD_EXPORT int voymastina_option_set(const char* name, double value)
{
    return OptionSet(name, value);
}

/* Her Hellbomb went off: its blast is meant to reach her (OnDamagePre lets it through) */
static void HellbombFired(const float pos[3])
{
    g_boomAt = g_now;
    atomic_store(&g_boomMs, GetTickCount64());
    memcpy(g_boomPos, pos, sizeof g_boomPos);
}

/* Her hits, before the game reads them: her hand's carry bare_ap armour penetration, any other the Shred's (both put
 * back after) */
static void HitBefore(KbHitCall* c)
{
    if (c->kind == KB_HIT_CALC && BareHit(c->hit))
    {
        memcpy(c->saved, (const uint8_t*)c->hit + 0x9C, 12);
        float ap[3] = {s_bareAp, s_bareAp, s_bareAp};
        memcpy((uint8_t*)c->hit + 0x9C, ap, 12);
        c->restore = 1;
        c->flags = 1;
    }
    else c->restore = Shred(c->hit, c->saved) > 0;
}
/* Her hits, after: her hand's extra damage, demolition and stagger; its blast's extra demolition */
static void HitAfter(KbHitCall* c)
{
    void* out = c->out;
    if (c->kind == KB_HIT_CALC && c->flags && out)
    {
        float dmg;
        int32_t demo, stag;
        memcpy(&dmg, out, 4);
        memcpy(&demo, (uint8_t*)out + 0x08, 4);
        memcpy(&stag, (uint8_t*)out + 0x0C, 4);
        float before = dmg;
        if (isfinite(dmg)) dmg += s_bareDamage;
        demo += (int32_t)s_bareDemolition;
        stag += (int32_t)s_bareStagger;
        memcpy(out, &dmg, 4);
        memcpy((uint8_t*)out + 0x08, &demo, 4);
        memcpy((uint8_t*)out + 0x0C, &stag, 4);
        unsigned n = atomic_fetch_add(&g_bareHits, 1);
        if (n < 5) Log("her hand: %.0f -> %.0f damage, demolition %d, stagger %d", (double)before, (double)dmg, demo, stag);
    }
    if (c->kind == KB_HIT_BLAST && out && s_bareDemolition > 0 && FistOrigin(c->hit) && Word((const uint8_t*)c->hit + 0x20) != atomic_load(&g_avatar))
    {
        int32_t demo;
        memcpy(&demo, (uint8_t*)out + 0x08, 4);
        int32_t before = demo;
        demo += (int32_t)s_bareDemolition;
        memcpy((uint8_t*)out + 0x08, &demo, 4);
        static atomic_uint told;
        if (atomic_fetch_add(&told, 1) < 4) Log("her hand: its blast's demolition %d -> %d", before, demo);
    }
}
/* A hit on her (not one of her own blasts): who dealt it, her Hellbomb let through, the stamina guard's note, and
 * Predation Protocol's armour. Nonzero: nothing more is done to it. */
static int HitTaken(const uint8_t* h, void* result, unsigned long long now)
{
    uint32_t me = atomic_load(&g_avatar);
    uint8_t r[RESULT_SIZE];
    uint32_t dealer = Word(h + 0x80), owner = Word(h + 0x100);
    if ((dealer && dealer != me) || (owner && owner != me))
    {
        unsigned slot = atomic_fetch_add(&g_atkNext, 1) % 8;
        atomic_store(&g_atkWho[slot][0], dealer);
        atomic_store(&g_atkWho[slot][1], owner);
        atomic_store(&g_atkMs[slot], now);
    }
    /* her own Hellbomb is meant to reach her as it is */
    if (now - atomic_load(&g_boomMs) < 2000) return 1;
    atomic_store(&g_hitMs, now);   /* the stamina guard (Tick): a hit's stamina loss is put back */
    if (!ReadAt((uintptr_t)result, r, sizeof r)) return 1;
    /* Predation Protocol: heavy armour (armour value pp_armor) while the buff holds: a hit whose armour penetration is
     * below it glances off (no damage), one equal to it deals pp_armor_equal of its damage */
    if (s_ppArmor > 0 && atomic_load(&g_ppArmorOn))
    {
        float ap, dmg;
        memcpy(&ap, h + 0x9C, 4);
        memcpy(&dmg, r, 4);
        if (isfinite(ap) && isfinite(dmg) && dmg > 0)
        {
            float keep = ap < (float)s_ppArmor - 0.5f ? 0.0f : ap < (float)s_ppArmor + 0.5f ? s_ppArmorEqual : 1.0f;
            if (keep < 1.0f) { dmg *= keep; memcpy(result, &dmg, 4); atomic_fetch_add(&g_armorHits, 1); }
        }
    }
    return 0;
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    RosterJoin("voymastina", "Voymastina");
    kb_ini = "voymastina.ini";
    kb_key_names[0] = "key_sirius";
    kb_key_names[1] = "key_eye";
    kb_key_names[2] = "key_pile";
    KB_SETTINGS(kSettings);
    KB_OPTIONS(kOptions);
    kb_option_after = OptionAfter;
    kb_damage_taken = DamageTaken;
    kb_damage_dealt = DamageDealt;
    kb_hit_before = HitBefore;
    kb_hit_after = HitAfter;
    kb_hit_taken = HitTaken;
    kb_hellbomb_fired = HellbombFired;
    if (!api || api->version < 13 || api->size < offsetof(HfApi, code_matches) + sizeof api->code_matches) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    LoadSettings();
    g_stab = s_stabMax;
    SinbreakerInit(api, mod);
    api->log(mod, "Voymastina %s: keys %s / %s / %s (Sirius Fall, Eye of the White Mastiff, Pile Bunker); explosions %d / %d / %d (fallback %d); "
        "Stability %.0f, damage kept off %.0f%% / %.0f%% broken; Predation %d per skill, empowered at %d", VERSION,
        s_keys[0].label, s_keys[1].label, s_keys[2].label, s_sfRow, s_eyeRow, s_pbRow, s_pbFallback, (double)s_stabMax,
        (double)(s_drStable * 100), (double)(s_drBase * 100), s_ppPer, s_ppMax);
    if (s_badKey) api->log(mod, "voymastina.ini: a key could not be read (bits %d: sirius, eye, pile); its default stays", s_badKey);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL) || api->on(mod, HF_EVENT_ENTITIES_READY, EntitiesReady, NULL)) return 1;
    if (s_bare && s_bareBolt && api->size >= offsetof(HfApi, projectile_row) + sizeof api->projectile_row)
    {
        HfDamage nothing = {sizeof nothing};
        nothing.base = 108;
        nothing.name = "yui/voymastina/fist_bullet_damage";
        nothing.scale = 1;
        g_fistBoltDamage = api->add_damage(mod, &nothing);
        HfProjectile bolt = {sizeof bolt};
        bolt.base = HF_PROJECTILE_AR23_LIBERATOR;
        bolt.name = "yui/voymastina/fist_bullet";
        bolt.damage = g_fistBoltDamage;
        g_fistBolt = g_fistBoltDamage ? api->add_projectile(mod, &bolt) : 0;
        if (!g_fistBolt || api->on(mod, HF_EVENT_PROJECTILES_READY, FistBoltReady, NULL)) g_fistBolt = 0;
    }
    int ok = api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_SPAWN, MechSpawn, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeaponAk, NULL, 0) &&
             api->on_game_thread(mod, Tick, NULL);
    if (!ok) api->log(mod, "events or the game thread are unavailable: Voymastina stays off");
    return 0;
}
