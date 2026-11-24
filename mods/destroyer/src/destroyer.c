/* Destroyer (yui.destroyer): Destroyer's kit from Girls' Frontline 2 as abilities on keys, for your own Helldiver (solo
 * or host). The plan and what was decided: docs/destroyer-design.md.
 *
 * She pays for her skills with her own health (never below cost_floor of it), and the less she has the harder she hits.
 *   Annihilation Directive (key_s1, default Z): one key, three stages. A leap to where she aims with a blast where she
 *     lands; if that hits, the key again is a line of high-explosive rounds walking away from her; if that hits, the
 *     key again is a cone of them. An enemy hit by all three takes a bonus hit. Each stage
 *     spends a Grenade point.
 *   Doom Barrage (key_s2, default X): a grid of blasts in a box in front of her, spending every Grenade point. +1 Fang.
 *   Party Time (key_s3, default C): a longer leap that blasts the whole path. Grenades back to full; each enemy hit
 *     heals her and gives Frenzy (a heavily armoured one: a full heal). +1 Fang.
 *   Ammo Frenzy (key_frenzy, default B): at 3 Fang, one strike on the enemy in her crosshair whose damage is shared
 *     evenly among every enemy in reach; each gets Hunting Order (her damage on it is higher for a while).
 *   Sangvis Ferri (passive): in a mission against Automatons her damage grows with the Automatons near her, her skill
 *     hits add a share of a non-heavy one's health, and she takes less damage. Denied Exit: a killing hit leaves her at
 *     1 health, once per cooldown.
 *
 * Her blasts are the game's own explosions (the Orbital Precision Strike's, the 500 kg bomb's for Ammo Frenzy), set off
 * with spawn_explosion; what each deals is the kit's (the damage event: a blast's hits on an enemy within the skill's
 * radius are paid once, at the skill's damage; the rest of the row's hits are cancelled). The line's and the cone's
 * rounds are copies of the Orbital Gatling Barrage's HE shell (as Koleda's Sinveiler fires), aimed at points on the
 * ground and paid the same way. Her leaps are Lenna's teleport (the game's position setter,
 * the game's fit test).
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

#define VERSION "0.2.12"
#define BUILD "25480438"

#define KB_KEYS 4   /* Annihilation Directive, Doom Barrage, Party Time, Ammo Frenzy */
#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"
#include "kitbase/kb_body.h"
#include "kitbase/kb_hits.h"
#include "kitbase/kb_damage.h"
#include "kitbase/kb_settings.h"
#include "kitbase/kb_options.h"
#include "kitbase/kb_roster.h"
#include "kitbase/kb_fit.h"

/* with other character kits installed: the HUD's roster and picker calls (kitbase/kb_roster.h) */
KB_ROSTER_EXPORTS(destroyer)
KB_OPTIONS_EXPORTS(destroyer)

/* ---- settings ------------------------------------------------------------------------------------------------- */
static float s_s1Cost = 0.20f, s_s2Cost = 0.30f, s_costFloor = 0.10f;
static int s_grenadesMax = 3, s_frenzyStart = 3, s_frenzyMax = 6;
static float s_grenadeMult = 0.40f, s_frenzyMult = 0.60f;
static int s_mech = 1;                 /* 0: never; 1: a mission where a known Automaton kind has spawned; 2: every mission */
static float s_mechRange = 40, s_mechEach = 0.05f, s_mechMax = 0.50f, s_mechHp = 0.10f, s_mechTaken = 0.30f;
static int s_bossArmour = 4;           /* heavy armour or above: boss-sized (Lemuen's test) */
static float s_deniedCd = 180, s_deniedSeconds = 2, s_rage = 1;
static int s_offenseMax = 3;           /* Offense Only: a stack after each stage of Annihilation Directive; a hit on her spends one and does nothing */
static float s_offenseWindow = 0.5f, s_ammoLoaded = 0.5f;
static float s_s1Cd = 10, s_s1Leap = 15, s_s1Radius = 5, s_s1Window = 3, s_s1Line = 25, s_s1LineWidth = 3, s_s1Cone = 12, s_s1Triple = 2, s_s1Haste = 2, s_hasteMult = 1.3f;
static float s_s1Damage = 600, s_s1Damage1 = 800, s_s1Damage2 = 1000, s_s1Stagger1 = 50, s_s1Stagger2 = 80, s_s1Shield = 1.5f;
static int s_s1Rounds = 12, s_tracerRound = 226, s_tracerDamage = 125, s_rowHe = 175;   /* the stages' rounds: Koleda's bright round and the minigun's damage row; where each lands, the G-12 High Explosive grenade's blast (0.1.6: the Orbital Gatling Barrage's, 266, came without its sound) */
static float s_s1Burst = 1.0f, s_s1ConeBurst = 0.5f, s_s1ConeAngle = 30, s_tracerSpeed = 250;
static float s_s2Cd = 18, s_s2Length = 15, s_s2Width = 9, s_s2Damage = 400, s_s2Grenade = 0.20f, s_s2Crit = 1.5f, s_s2Step = 0.18f;
static float s_s3Cd = 90, s_s3Leap = 18, s_s3Width = 9, s_s3Damage = 1500, s_s3Heal = 0.20f, s_s3Step = 0.12f;
static float s_frenzyRadius = 24, s_frenzyDamage = 12000, s_frenzyRange = 120, s_frenzyCone = 6, s_huntingSeconds = 10, s_huntingTaken = 0.20f, s_frenzyRefund = 5;
static int s_frenzyRing = 3;
static int s_rowSmall = 343, s_rowBig = 277;   /* 343: the Orbital Precision Strike's shell; 277: the Eagle 500kg Bomb going off (193 is it landing) */
static float s_lpClimb = 6, s_lpDrop = 12;
/* her hands (0.1.8): a grenade launcher's rounds arm at once and home; every weapon is held in one hand */
static int s_glArming = 1, s_glHoming = 1, s_glSafe = 1, s_oneHand = 1, s_oneHandGrip = 70;
static float s_flakRadius = 4;
static float s_homingCone = 25, s_homingRange = 80, s_homingTurn = 360, s_homingArm = 1;
static int s_homingMissile = 0;

/* Every setting: its .ini / options name, variable and range (kitbase/kb_settings.h) */
static const KbSetting kSettings[] = {
    KB_F("s1_cost", s_s1Cost, 0, 0.9), KB_F("s2_cost", s_s2Cost, 0, 0.9), KB_F("cost_floor", s_costFloor, 0.01, 0.9),
    KB_I("grenades_max", s_grenadesMax, 0, 6), KB_F("grenade_mult", s_grenadeMult, 0, 5),
    KB_I("frenzy_start", s_frenzyStart, 0, 12), KB_I("frenzy_max", s_frenzyMax, 0, 12), KB_F("frenzy_mult", s_frenzyMult, 0, 5),
    KB_I("mech", s_mech, 0, 2), KB_F("mech_range", s_mechRange, 5, 200), KB_F("mech_each", s_mechEach, 0, 1), KB_F("mech_max", s_mechMax, 0, 5),
    KB_F("mech_hp", s_mechHp, 0, 1), KB_F("mech_taken", s_mechTaken, 0, 0.95), KB_I("boss_armour", s_bossArmour, 1, 10),
    KB_I("offense_max", s_offenseMax, 0, 10), KB_F("offense_window", s_offenseWindow, 0, 5), KB_F("ammo_loaded", s_ammoLoaded, 0, 5),
    KB_F("denied_cooldown", s_deniedCd, 0, 3600), KB_F("denied_seconds", s_deniedSeconds, 0, 10), KB_F("rage", s_rage, 0, 3),
    KB_F("s1_cooldown", s_s1Cd, 0, 600), KB_F("s1_leap", s_s1Leap, 3, 40), KB_F("s1_radius", s_s1Radius, 1, 20), KB_F("s1_window", s_s1Window, 0.5, 20),
    KB_F("s1_line", s_s1Line, 3, 100), KB_F("s1_line_width", s_s1LineWidth, 0, 20), KB_F("s1_cone", s_s1Cone, 3, 100), KB_F("s1_triple", s_s1Triple, 0, 20), KB_F("s1_haste", s_s1Haste, 0, 30),
    KB_F("haste_mult", s_hasteMult, 1, 3), KB_F("s1_damage", s_s1Damage, 0, 100000), KB_F("s1_damage_1", s_s1Damage1, 0, 100000),
    KB_F("s1_damage_2", s_s1Damage2, 0, 100000), KB_F("s1_stagger_1", s_s1Stagger1, 0, 500), KB_F("s1_stagger_2", s_s1Stagger2, 0, 500),
    KB_I("s1_rounds", s_s1Rounds, 1, 40), KB_F("s1_burst", s_s1Burst, 0, 3), KB_F("s1_cone_burst", s_s1ConeBurst, 0, 3), KB_F("s1_cone_angle", s_s1ConeAngle, 5, 80),
    KB_F("s1_round_speed", s_tracerSpeed, 0, 2000), KB_I("s1_round", s_tracerRound, 1, 350), KB_I("s1_round_damage", s_tracerDamage, 1, 649), KB_I("s1_explosion", s_rowHe, 0, 100000),
    KB_F("s1_shield", s_s1Shield, 0, 10),
    KB_F("s2_cooldown", s_s2Cd, 0, 600), KB_F("s2_length", s_s2Length, 5, 40), KB_F("s2_width", s_s2Width, 3, 30), KB_F("s2_damage", s_s2Damage, 0, 100000),
    KB_F("s2_grenade", s_s2Grenade, 0, 5), KB_F("s2_crit", s_s2Crit, 1, 10), KB_F("s2_step", s_s2Step, 0, 2),
    KB_F("s3_cooldown", s_s3Cd, 0, 600), KB_F("s3_leap", s_s3Leap, 3, 40), KB_F("s3_width", s_s3Width, 2, 30), KB_F("s3_damage", s_s3Damage, 0, 100000),
    KB_F("s3_heal", s_s3Heal, 0, 1), KB_F("s3_step", s_s3Step, 0, 2),
    KB_F("frenzy_radius", s_frenzyRadius, 5, 60), KB_F("frenzy_damage", s_frenzyDamage, 0, 1000000), KB_F("frenzy_range", s_frenzyRange, 10, 300),
    KB_F("frenzy_cone", s_frenzyCone, 1, 45), KB_I("frenzy_ring", s_frenzyRing, 0, 6), KB_F("hunting_seconds", s_huntingSeconds, 0, 120),
    KB_F("hunting_taken", s_huntingTaken, 0, 5), KB_F("frenzy_refund", s_frenzyRefund, 0, 120),
    KB_I("explosion_small", s_rowSmall, 1, 100000), KB_I("explosion_big", s_rowBig, 1, 100000),
    KB_I("gl_arming", s_glArming, 0, 1), KB_I("gl_homing", s_glHoming, 0, 1), KB_I("gl_safe", s_glSafe, 0, 1),
    KB_I("homing_missile", s_homingMissile, 0, 2), KB_F("homing_arm", s_homingArm, 0, 100), KB_F("flak_radius", s_flakRadius, 0, 20), KB_F("homing_cone", s_homingCone, 1, 180), KB_F("homing_turn", s_homingTurn, 1, 1440), KB_F("homing_range", s_homingRange, 1, 500), KB_I("one_handed", s_oneHand, 0, 1), KB_I("one_hand_grip", s_oneHandGrip, 0, 200),
    KB_F("leap_climb", s_lpClimb, 0.5, 30), KB_F("leap_drop", s_lpDrop, 0.5, 50),
    KB_F("hp_mult", s_hpMult, 0.1, 10), KB_F("stamina_mult", s_staminaMult, 0.1, 20), KB_I("hellbomb_on_exhaustion", s_hellbomb, 0, 1),
    KB_I("hellbomb_explosion", s_hellbombRow, 0, 100000), KB_F("hellbomb_hold", s_hellbombHold, 0, 10),
};

/* The default destroyer.ini: tools/inigen.py writes the file from this text (the mod never writes it) */
static __attribute__((unused)) void DefaultIni(FILE* f)
{
fprintf(f, "; Destroyer %s. Keys: a letter, digit, F1-F24, Space, Mouse3, Mouse4 or Mouse5, with Shift+, Ctrl+ or Alt+ in front\n"
               "; for a combo (Shift+Q). The game still sees the keys too, so pick ones it leaves alone.\n"
               "key_s1 = %s\nkey_s2 = %s\nkey_s3 = %s\nkey_frenzy = %s\n"
               "; Her skills cost health: a share of her maximum (0.2: 20%%). No skill takes her below cost_floor of it.\n"
               "s1_cost = %g\ns2_cost = %g\ncost_floor = %g\n"
               "; Grenades: most she holds; each one a skill spends adds grenade_mult to that skill's damage (0.4: +40%%).\n"
               "; Frenzy: what she starts a mission with and most she holds; a skill that spends one deals frenzy_mult more\n"
               "; and gives a Grenade back.\n"
               "grenades_max = %d\ngrenade_mult = %g\nfrenzy_start = %d\nfrenzy_max = %d\nfrenzy_mult = %g\n"
               "; Sangvis Ferri. mech: 1 in a mission where a known Automaton kind has spawned, 2 in every mission, 0 never.\n"
               "; Each living enemy within mech_range m adds mech_each to her skill damage, up to mech_max; her skill hits add\n"
               "; mech_hp of the health of one that is not boss-sized (armour boss_armour or above: 4 is heavy); she takes\n"
               "; mech_taken less damage.\n"
               "mech = %d\nmech_range = %g\nmech_each = %g\nmech_max = %g\nmech_hp = %g\nmech_taken = %g\nboss_armour = %d\n"
               "; Denied Exit: a killing hit leaves her at 1 health and nothing hurts her for denied_seconds; then\n"
               "; denied_cooldown s before it can again. rage: each 1%% of health she is missing adds rage %% to her skill\n"
               "; damage and takes half of that off the damage she takes (0: off).\n"
               "denied_cooldown = %g\ndenied_seconds = %g\nrage = %g\n"
               "; Offense Only: each stage of Annihilation Directive gives a stack (at most offense_max); a hit on her spends\n"
               "; one and does nothing, and so do the hits of the next offense_window s (one attack is often many hits).\n"
               "; Ammo Loaded: after the cone, her next skill deals ammo_loaded more (0.5: +50%%).\n"
               "offense_max = %d\noffense_window = %g\nammo_loaded = %g\n"
               "; Annihilation Directive: cooldown (from the end of the chain); the leap's reach; its blast's radius; seconds\n"
               "; the next stage stays open after a hit; how far the line and the cone reach; the bonus on an enemy\n"
               "; all three stages hit (times the cone's damage); the sprint boost after a stage that hits (seconds, times her\n"
               "; speed); each stage's damage; the stagger and push of a round of the line and of the cone that hits an\n"
               "; enemy itself; seconds without knockdown after each stage.\n"
               "s1_cooldown = %g\ns1_leap = %g\ns1_radius = %g\ns1_window = %g\ns1_line = %g\ns1_cone = %g\ns1_triple = %g\ns1_haste = %g\nhaste_mult = %g\n"
               "s1_damage = %g\ns1_damage_1 = %g\ns1_damage_2 = %g\ns1_stagger_1 = %g\ns1_stagger_2 = %g\ns1_shield = %g\n"
               "; The line and the cone are rounds she fires at the ground, s1_rounds of them one after another, each with a\n"
               "; high-explosive blast where it lands: the line's within s1_burst s, walking away from her (as an Eagle's\n"
               "; strafing run); the cone's within s1_cone_burst s, sweeping s1_cone_angle degrees to each side.\n"
               "; s1_round: the projectile they copy (226: the bright round of Koleda's Sinveiler); s1_round_damage: the\n"
               "; damage row a direct hit's copies (the damage is the stage's); s1_round_speed: their m/s (0: the round's own;\n"
               "; slower, their trails start at her); s1_explosion: the blast where each lands (175: the G-12 High Explosive\n"
               "; grenade's; 266, the Orbital Gatling Barrage's shell's, came without its sound). s1_line_width: metres\n"
               "; across the line's rounds land, left and right in turn.\n"
               "s1_rounds = %d\ns1_burst = %g\ns1_cone_burst = %g\ns1_cone_angle = %g\ns1_round = %d\ns1_round_damage = %d\ns1_round_speed = %g\ns1_explosion = %d\ns1_line_width = %g\n"
               "; Doom Barrage: cooldown; the box's length and width; damage; each Grenade spent adds s2_grenade; with 3 or\n"
               "; more spent the damage is times s2_crit; seconds between its blasts.\n"
               "s2_cooldown = %g\ns2_length = %g\ns2_width = %g\ns2_damage = %g\ns2_grenade = %g\ns2_crit = %g\ns2_step = %g\n"
               "; Party Time: cooldown; the leap's reach; the path's width; damage; each enemy hit heals s3_heal of her maximum;\n"
               "; seconds between its blasts, from where she stood to where she landed.\n"
               "s3_cooldown = %g\ns3_leap = %g\ns3_width = %g\ns3_damage = %g\ns3_heal = %g\ns3_step = %g\n"
               "; Ammo Frenzy: its reach around the target; its total damage, shared among the enemies in reach; how far the\n"
               "; target may be and how many degrees off her crosshair; extra blasts on a ring around the first; Hunting\n"
               "; Order's length and how much more of her damage its enemies take; seconds off her cooldowns.\n"
               "frenzy_radius = %g\nfrenzy_damage = %g\nfrenzy_range = %g\nfrenzy_cone = %g\nfrenzy_ring = %d\nhunting_seconds = %g\nhunting_taken = %g\nfrenzy_refund = %g\n"
               "; Her blasts (ExplosionSettings rows): 343 the Orbital Precision Strike's, 277 the Eagle 500kg Bomb going off.\n"
               "explosion_small = %d\nexplosion_big = %d\n"
               "; Her hands. gl_arming 1: the rounds of a grenade launcher she holds arm as they leave (no arming distance:\n"
               "; any weapon whose round has one). gl_safe 1: the blasts of the weapons she holds do not hurt or throw her.\n"
               "; gl_homing 1: the grenades of a launcher she holds home. homing_missile 0: the grenade itself is steered by\n"
               "; the kit (the game's projectile pool): from homing_arm m out it turns homing_turn degrees a second toward the\n"
               "; living enemy nearest her and most in line within homing_cone degrees of where she looked and homing_range m,\n"
               "; and once it is flak_radius m from a living enemy it turns straight into it (0: never), so one that curved\n"
               "; too late still lands. homing_missile 1: the launchers fire the FAF-14 Spear's guided missile instead, 2: the\n"
               "; P-33 Missile Pistol's (a premium weapon's), carrying their own grenade and handed the same enemy; there\n"
               "; flak_radius ends the missile in the air near an enemy.\n"
               "; one_handed 1: she holds every weapon in one hand (the game's one-handed mark on what she carries), in the\n"
               "; pose one_hand_grip (70: the one-handed SMG grip; 1: a pistol's; 0: the weapon's own).\n"
               "gl_arming = %d\ngl_safe = %d\ngl_homing = %d\nhoming_missile = %d\nhoming_cone = %g\nhoming_range = %g\nhoming_turn = %g\nhoming_arm = %g\nflak_radius = %g\none_handed = %d\none_hand_grip = %d\n"
               "; Her leaps land at most leap_climb m higher or leap_drop m lower.\n"
               "leap_climb = %g\nleap_drop = %g\n"
               "; Her body: hp_mult times a Helldiver's health, stamina_mult times her sprint before the bar empties. If her\n"
               "; stamina ever runs out, hellbomb_on_exhaustion (1) sets off hellbomb_explosion (125: the B-100 Portable\n"
               "; Hellbomb's) on her.\n"
               "; hellbomb_hold: seconds the bar must stay empty first (it flickers to 0 for a moment at times).\n"
               "hp_mult = %g\nstamina_mult = %g\nhellbomb_on_exhaustion = %d\nhellbomb_explosion = %d\nhellbomb_hold = %g\n",
        VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, s_keys[3].label[0] ? s_keys[3].label : "B", (double)s_s1Cost, (double)s_s2Cost, (double)s_costFloor,
        s_grenadesMax, (double)s_grenadeMult, s_frenzyStart, s_frenzyMax, (double)s_frenzyMult,
        s_mech, (double)s_mechRange, (double)s_mechEach, (double)s_mechMax, (double)s_mechHp, (double)s_mechTaken, s_bossArmour,
        (double)s_deniedCd, (double)s_deniedSeconds, (double)s_rage, s_offenseMax, (double)s_offenseWindow, (double)s_ammoLoaded,
        (double)s_s1Cd, (double)s_s1Leap, (double)s_s1Radius, (double)s_s1Window, (double)s_s1Line, (double)s_s1Cone, (double)s_s1Triple, (double)s_s1Haste, (double)s_hasteMult,
        (double)s_s1Damage, (double)s_s1Damage1, (double)s_s1Damage2, (double)s_s1Stagger1, (double)s_s1Stagger2, (double)s_s1Shield,
        s_s1Rounds, (double)s_s1Burst, (double)s_s1ConeBurst, (double)s_s1ConeAngle, s_tracerRound, s_tracerDamage, (double)s_tracerSpeed, s_rowHe, (double)s_s1LineWidth,
        (double)s_s2Cd, (double)s_s2Length, (double)s_s2Width, (double)s_s2Damage, (double)s_s2Grenade, (double)s_s2Crit, (double)s_s2Step,
        (double)s_s3Cd, (double)s_s3Leap, (double)s_s3Width, (double)s_s3Damage, (double)s_s3Heal, (double)s_s3Step,
        (double)s_frenzyRadius, (double)s_frenzyDamage, (double)s_frenzyRange, (double)s_frenzyCone, s_frenzyRing, (double)s_huntingSeconds, (double)s_huntingTaken, (double)s_frenzyRefund,
        s_rowSmall, s_rowBig, s_glArming, s_glSafe, s_glHoming, s_homingMissile, (double)s_homingCone, (double)s_homingRange, (double)s_homingTurn, (double)s_homingArm, (double)s_flakRadius, s_oneHand, s_oneHandGrip,
        (double)s_lpClimb, (double)s_lpDrop,
        (double)s_hpMult, (double)s_staminaMult, s_hellbomb, s_hellbombRow, (double)s_hellbombHold);
}

/* ---- state ---------------------------------------------------------------------------------------------------- */
static int g_grenades, g_fang, g_frenzy, g_offense, g_ammoLoaded;
static double g_offenseUntil;
static unsigned g_offenseSpent;
static double g_cd[4];                 /* ready at: Annihilation Directive, Doom Barrage, Party Time, Ammo Frenzy (a short lockout) */
static double g_drUntil, g_speedUntil, g_deniedUntil, g_deniedReady;
static int g_mechMission;              /* this mission is against Automatons */
static unsigned g_casts[4], g_selfSpared, g_knockdowns, g_paidHits, g_cutHits, g_friendSpared, g_denied;
static double g_healOwed;              /* shares of her maximum health owed by Party Time's hits (given in the frame) */
static int g_healFull;

/* ---- reads: health, positions ------------------------------------------------------------------------------------ */
/* An entity's health record (Koleda's HealthRec: the health manager at game+0x3326688, its open-addressed entity hash,
 * 440-byte records with health at +20 and life at +412, 0 alive): its address, 0 unknown */
static uintptr_t HealthRec(uint32_t entity)
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
        return records + 440u * index;
    }
    return 0;
}
static int HealthOf(uint32_t entity, int32_t* health, uint32_t* life)
{
    uintptr_t r = HealthRec(entity);
    uint8_t b[416];
    if (!r || !ReadAt(r, b, sizeof b)) return 0;
    *health = (int32_t)Word(b + 20);
    *life = Word(b + 412);
    return 1;
}

/* ---- the teleport (Lenna's): the game's position setter, the raycast, the ground under a point ------------------- */
typedef void (*SetVecFn)(void*, uint32_t, const float*);
typedef uint32_t (*WorldFn)(const void*);
typedef uint32_t (*RayFn)(uint32_t, const void*, const void*, float, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t);
static SetVecFn g_setPosition, g_setVelocity;
static WorldFn g_worldId;
static RayFn g_raycast;
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
        /* a zone, not a thing (from Ascalon 0.4.14): actors tagged 0xEDC5EBE0 are level faces some 2 m over the ground
         * across a whole area, which a look meets from outside and which stop nothing */
        if (ActorFilter(Word(hits + 44u * i + 32), Word(hits + 44u * i + 28)) == 0xEDC5EBE0u) continue;
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
        uint32_t hu = Word(hits + 44u * i + 28);
        if (hu == unit || !isfinite(d) || d < 0.02f || d > len) continue;
        /* no ground (as Voymastina's and Ascalon's landing checks have it): leaves and canopies, bodies (enemies,
         * corpses), and a zone's face */
        uint32_t filt = ActorFilter(Word(hits + 44u * i + 32), hu);
        if (filt == FILTER_FOLIAGE || filt == 0xEDC5EBE0u || (hu && (filt == 0x236122F0u || filt == 0xD5B33D56u))) continue;
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

/* ---- her health: read, its maximum, the price of a skill ----------------------------------------------------------- */
static int32_t MaxHealth(void)
{
    HfField f = HF_FIELD(Health, health);
    HfLiveValue v;
    memset(&v, 0, sizeof v);
    if (hf->get_live(self, AVATAR, &f, &v) && v.now.number >= 1 && v.now.number < 1000000) return (int32_t)v.now.number;
    return 0;
}
/* her health now and its maximum; 0 when either cannot be read */
static int Hp(int32_t* hp, int32_t* max)
{
    uint32_t life = 1;
    *max = MaxHealth();
    return *max > 0 && HealthOf(atomic_load(&g_avatar), hp, &life) && *hp > 0;
}
/* the share of her health she is missing, 0 .. 1 */
static double Missing(void)
{
    int32_t hp, max;
    if (!Hp(&hp, &max) || hp >= max) return 0;
    return 1.0 - (double)hp / max;
}
/* A skill's price: `share` of her maximum, never taking her below cost_floor of it (under the floor it costs nothing).
 * 0.1.4: written into her health record alone, her health was lower but the game's HUD did not show it (as Koleda's
 * Sinveiler's, before 0.4.4). So the write goes one point under, and the game's own heal gives that point back: the
 * game's health change is what the HUD follows. (Not the game's change with a share off: it takes the same share off
 * every limb, and her limbs would break.) */
static void Pay(const char* what, float share)
{
    int32_t hp, max;
    if (share <= 0) return;
    if (!Hp(&hp, &max)) { static unsigned told; if (told++ < 4) Log("%s: her health cannot be read: no price taken", what); return; }
    int32_t floor_ = (int32_t)ceil(max * (double)s_costFloor), want = hp - (int32_t)floor(max * (double)share + 0.5);
    if (floor_ < 2) floor_ = 2;
    if (want < floor_) want = floor_;
    if (want >= hp) return;
    uintptr_t r = HealthRec(atomic_load(&g_avatar));
    int32_t under = want - 1;
    if (!r || hf->write(r + 20, &under, 4)) { static unsigned told; if (told++ < 4) Log("%s: the health write failed: no price taken", what); return; }
    int nudge = hf->heal(self, 1.0f), now = 0;
    ReadAt(r + 20, &now, 4);
    static unsigned told;
    if (told++ < 16) Log("%s: paid %d health (%d -> %d of %d; the game's heal of the last point answered %d)", what, hp - now, hp, now, max, nudge);
}

/* ---- speed (Lenna's: her jog and sprint, live) --------------------------------------------------------------------- */
static int g_speedOn;
static float g_baseJog = -1, g_baseSprint = -1;
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
    HfValue j = HF_F32(fminf(10, g_baseJog * s_hasteMult)), s = HF_F32(fminf(10, g_baseSprint * s_hasteMult));
    int r1 = hf->set_live(self, AVATAR, &jog, &j, HF_LIVE_SHARED), r2 = hf->set_live(self, AVATAR, &sprint, &s, HF_LIVE_SHARED);
    if (!g_speedOn) { static unsigned told; if (told++ < 3) Log("speed: jog %.1f -> %.1f, sprint %.1f -> %.1f m/s (applies: %d / %d; 1 = now)", (double)g_baseJog, j.number, (double)g_baseSprint, s.number, r1, r2); }
    g_speedOn = 1;
}

/* ---- enemies: where they are, which are boss-sized, which mission is against Automatons ---------------------------- */
static int FoePos(int i, float p[3])
{
    uint8_t id[24];
    uint32_t e = g_foe[i];
    if (!e) return 0;
    if (!Descriptor(e, id) || Quad(id) != g_foeRes[i]) { g_foe[i] = 0; return 0; }
    return UnitNode(Word(id + 12), 0, p);
}
static int Living(uint32_t e)
{
    int32_t hp = 0;
    uint32_t life = 1;
    return HealthOf(e, &hp, &life) && hp > 0 && life == 0;
}
/* a kind's armour and full health (Lemuen's ArmourOf: its Health record's values, kept per kind) */
static void KindBody(uint64_t res, int* armour, int32_t* health)
{
    static uint64_t keys[128];
    static int8_t arm[128];
    static int32_t hpv[128];
    unsigned h = (unsigned)(res ^ (res >> 29)) & 127;
    for (unsigned i = 0; i < 128; i++)
    {
        unsigned k = (h + i) & 127;
        if (keys[k] != res)
        {
            if (keys[k]) continue;
            HfField fa = HF_FIELD(Health, armor_118), fh = HF_FIELD(Health, health);
            HfLiveValue v;
            memset(&v, 0, sizeof v);
            int a = hf->get_live(self, res, &fa, &v) ? (int)v.base.number : 0;
            memset(&v, 0, sizeof v);
            double m = hf->get_live(self, res, &fh, &v) ? v.base.number : 0;
            arm[k] = (int8_t)(a < 0 ? 0 : a > 10 ? 10 : a);
            hpv[k] = m > 0 && m < 10000000 ? (int32_t)m : 0;
            keys[k] = res;
        }
        *armour = arm[k]; *health = hpv[k];
        return;
    }
    *armour = 0; *health = 0;
}
/* The Automaton kinds the SDK's catalogue names. The game's faction tag is not found yet (docs/faction-research.md), so
 * a mission counts as against Automatons once one of these has spawned in it; then every enemy in it is mechanical. */
static const uint64_t kAutomatons[] = {
    0x856E9710E45E760Full /* Assault Raider */, 0x63DF3D07B7424588ull /* Barrager Tank */, 0xD8CBC4A807A6D035ull /* Brawler */,
    0xE0353177F1329573ull /* Conflagration Devastator */, 0x282EB766C1FFA6A1ull /* Gunship */, 0x96BA14C9EBB49CE1ull /* Hulk */,
    0x6DAB2EADF5D8B692ull /* Hulk Firebomber */, 0x1E66EE1F6F7FD00Eull /* Hulk Obliterator */, 0x58B2B86C11369241ull /* Incendiary MG Devastator */,
    0x0002BA767DF856F3ull /* Marauder */, 0x82A87AD8D595B2BAull /* Pyro Trooper */, 0x9076EEED17FCEE35ull /* Reinforced Scout Strider */,
    0x2A12104F2853AE16ull /* Rocket Raider */, 0x30CF04B2EC8C9BD4ull /* Scout Raider */, 0xF8131632AA867107ull /* Scout Strider */,
    0xC6449FFD9EA3779Cull /* Shredder Tank */, 0xEF570293245A17C2ull /* War Strider */,
};
static void MechSeen(uint64_t res)
{
    if (g_mechMission || s_mech != 1) return;
    for (unsigned i = 0; i < sizeof kAutomatons / sizeof kAutomatons[0]; i++)
        if (kAutomatons[i] == res) { g_mechMission = 1; Log("Sangvis Ferri: an Automaton kind (%016llx) is in this mission: its enemies are mechanical", (unsigned long long)res); return; }
}
static int Mech(void) { return s_mech == 2 || (s_mech == 1 && g_mechMission); }
/* her skill damage for the Automatons near her: 1 + mech_each a living one within mech_range, up to mech_max */
static double MechBonus(void)
{
    float me[3], p[3];
    if (!Mech() || !PlayerPose(me, NULL)) return 1;
    int n = 0;
    for (int i = 0; i < FOES; i++)
        if (FoePos(i, p) && Dist2(p, me) <= s_mechRange * s_mechRange && Living(g_foe[i])) n++;
    double b = n * (double)s_mechEach;
    return 1 + (b > s_mechMax ? s_mechMax : b);
}
static int TargetFriend(uint32_t e, uint64_t* res)
{
    uint8_t d[24];
    uint32_t stride;
    *res = 0;
    if (!Descriptor(e, d)) return 0;
    *res = Quad(d);
    return *res == AVATAR || hf->component_record("AvatarComponentData", *res, &stride) || FriendRes(*res);
}

/* ---- Hunting Order: enemies Ammo Frenzy hit take more of her damage ------------------------------------------------ */
typedef struct Hunt { uint32_t entity; double until; } Hunt;
static Hunt g_hunt[96];
static int Hunted(uint32_t e)
{
    for (int i = 0; i < 96; i++) if (g_hunt[i].entity == e && g_hunt[i].until > g_now) return 1;
    return 0;
}
static void HuntMark(uint32_t e)
{
    int slot = 0;
    for (int i = 0; i < 96; i++)
    {
        if (g_hunt[i].entity == e) { slot = i; break; }
        if (g_hunt[i].until < g_hunt[slot].until) slot = i;
    }
    g_hunt[slot].entity = e;
    g_hunt[slot].until = g_now + s_huntingSeconds;
}

/* ---- her blasts ----------------------------------------------------------------------------------------------------
 * A skill's blasts belong to one group: the group holds the skill's damage and radius and who it has paid (an enemy is
 * paid once by a group, however many of its blasts or of one blast's hits reach it). Blasts wait in a queue for their
 * moment (rows of a barrage), for their packages, and for the row's push to be what they need. */
enum { SK_LEAP = 1, SK_LINE, SK_CONE, SK_BARRAGE, SK_PARTY, SK_FRENZY };
#define PAID 128
typedef struct Group { int skill, hits, npaid, spawned; double at, spawnedAt; float damage, radius; uint32_t paid[PAID]; } Group;
static Group g_group[16];
static unsigned g_groupNext;
typedef struct Burst { float p[3]; double at; int group; float reach; } Burst;
static Burst g_blast[64];
static unsigned g_blastNext;
typedef struct Queued { float p[3]; double at, until; int row, group; } Queued;
static Queued g_q[64];
static int g_nq;
static double g_lastSpawn;

static int NewGroup(int skill, float damage, float radius)
{
    int id = (int)(g_groupNext++ % 16);
    memset(&g_group[id], 0, sizeof g_group[id]);
    g_group[id].skill = skill;
    g_group[id].damage = damage;
    g_group[id].radius = radius;
    g_group[id].at = g_now;
    return id;
}
static int PaidBy(const Group* g, uint32_t e) { for (int i = 0; i < g->npaid; i++) if (g->paid[i] == e) return 1; return 0; }
static void Queue(int group, int row, const float p[3], double delay)
{
    if (g_nq >= 64 || !row) return;
    Queued* q = &g_q[g_nq++];
    memcpy(q->p, p, sizeof q->p);
    q->at = g_now + delay;
    q->until = q->at + 4;
    q->row = row;
    q->group = group;
}
static void BlastFrame(void)
{
    for (int i = 0; i < g_nq; i++)
    {
        Queued* q = &g_q[i];
        if (g_now < q->at) continue;
        int r = hf->spawn_explosion(self, (uint32_t)q->row, q->p);
        if (r == HF_ACT_WAITING && g_now < q->until) continue;   /* its packages are loading: again next frame */
        static unsigned noted;
        if (noted++ < 10) Log("blast (row %d, skill %d): answer %d", q->row, g_group[q->group].skill, r);
        if (r != HF_ACT_WAITING && Act("a blast", r))
        {
            Burst* b = &g_blast[g_blastNext++ % 64];
            memcpy(b->p, q->p, sizeof b->p);
            b->at = g_now;
            b->group = q->group;
            b->reach = q->row == s_rowBig ? 60.0f : q->row == s_rowHe ? 10.0f : 30.0f;
            Group* g = &g_group[q->group];
            if (!g->spawned) { g->spawned = 1; g->spawnedAt = g_now; }
            unsigned slot = g_ownNext++ % KB_OWN_SLOTS;
            memcpy(g_ownPos[slot], q->p, sizeof g_ownPos[slot]);
            atomic_store(&g_ownMs[slot], GetTickCount64());
            g_lastSpawn = g_now;
        }
        g_q[i] = g_q[--g_nq];
        i--;
    }
}
/* the blast a kind-4 hit at `at` belongs to: the nearest of her blasts of the last 1.2 s within its row's reach */
static Burst* BlastAt(const float at[3], float* dist)
{
    Burst* best = NULL;
    float bd = 0;
    for (int i = 0; i < 64; i++)
    {
        Burst* b = &g_blast[i];
        if (b->at <= 0 || g_now - b->at > 1.2) continue;
        float d = Dist2(at, b->p);
        if (d > b->reach * b->reach) continue;
        if (!best || d < bd) { best = b; bd = d; }
    }
    if (best && dist) *dist = sqrtf(bd);
    return best;
}

/* ---- the stages' rounds ----------------------------------------------------------------------------------------------
 * The line and the cone are high-explosive rounds: copies of the Orbital Gatling Barrage's shell, whose damage rows
 * carry the stage's stagger and push for a round that hits an enemy itself. What a round or its burst deals is the
 * stage's, paid once an enemy. */
static uint32_t g_tracer[2], g_tracerDamage[2];
static uint32_t MyUnit(void);
static int g_tracerReady;
/* 0.1.2: the camera (Alva's View, Exusiai's: her unit's world, its camera matrix at +0x230920; Apache - Rise Above's
 * read). Her head's facing lags behind the reticle, and the tracers went where the head looked */
static int CamView(float pos[3], float fwd[3])
{
    uint8_t d[24];
    if (!exe || !Descriptor(atomic_load(&g_avatar), d)) return 0;
    uint32_t unit = Word(d + 12), slot = unit & 0x3FFFFF, id = 0;
    uintptr_t registry = Pointer(exe + 0x1A100F0), objects, object, w;
    float m[16];
    if (!registry || !(objects = Pointer(registry + 0x88)) || !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit ||
        !(w = Pointer(object + 0x18)) || !ReadAt(w + 0x230920, m, 64)) return 0;
    for (int k = 0; k < 16; k++) if (!isfinite(m[k])) return 0;
    if (fabsf(m[15] - 1) > 1e-3f) return 0;
    memcpy(fwd, m + 4, 12);
    memcpy(pos, m + 12, 12);
    float n = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    if (!(n > 0.5f)) return 0;
    for (int k = 0; k < 3; k++) fwd[k] /= n;
    return 1;
}
/* where a stage's tracers start (her chest) and go: at what her reticle is on (the camera's ray to the first wall);
 * her head's facing when the camera cannot be read */
static int StageAim(float from[3], float aim[3])
{
    float pos[3], cam[3], fwd[3];
    if (!PlayerPose(pos, aim)) return 0;
    from[0] = pos[0]; from[1] = pos[1]; from[2] = pos[2] + 1.4f;
    static int told = -1;
    int camera = CamView(cam, fwd);
    if (camera != told) { told = camera; Log("the stages' aim: %s", camera ? "the camera's ray (her reticle)" : "her head's facing (the camera cannot be read)"); }
    if (!camera) return 1;
    uint32_t unit = MyUnit();
    float reach = 200, toHer = sqrtf(Dist2(cam, from)) + 3;
    if (unit && QueryIdle()) { float d = Ray(unit, cam, fwd, 200); if (d > 0 && d < reach) reach = d; }
    if (reach < toHer) reach = toHer;      /* never a point between the camera and her */
    float v[3] = {cam[0] + fwd[0] * reach - from[0], cam[1] + fwd[1] * reach - from[1], cam[2] + fwd[2] * reach - from[2]};
    float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (n > 1) for (int k = 0; k < 3; k++) aim[k] = v[k] / n;
    return 1;
}
/* the tracers' speed: slower than the round's own, so their trails start at her (Koleda's round_speed) */
static void TracerSpeed(int k)
{
    static uint8_t set[2];
    if (set[k] || s_tracerSpeed <= 0 || !g_tracer[k]) return;
    set[k] = 1;
    HfField f = HF_FIELD(ProjectileSettings, velocity);
    HfValue v = HF_F32(s_tracerSpeed);
    HfLiveValue was;
    memset(&was, 0, sizeof was);
    hf->get_live(self, (HfRef)g_tracer[k], &f, &was);
    int ok = hf->set_live(self, (HfRef)g_tracer[k], &f, &v, 0);
    Log("the %s's rounds (projectile %u): %.0f m/s -> %.0f (%s)", k ? "cone" : "line", g_tracer[k], was.base.number, (double)s_tracerSpeed, ok ? "set" : "refused");
}
typedef struct Volley { int group, left, cone, n; double next, until; float from[3], aim[3], range; } Volley;
static Volley g_volley = {-1};
static void Queue(int group, int row, const float p[3], double delay);
static void OnGround(uint32_t unit, float x, float y, float ref, float out[3]);
/* A stage's rounds, each at a point on the ground: the line's from near her to its end, one after another (an Eagle's
 * strafing run, from her); the cone's across its angle, near and far in turn. Each point gets a blast of the
 * stage's group when its round lands, so what it hits there is paid as the stage's and spares her and her friends. */
static void VolleyFrame(void)
{
    Volley* v = &g_volley;
    if (v->group < 0 || v->left <= 0 || g_now < v->next) return;
    uint32_t type = g_tracer[v->cone], unit = MyUnit();
    float h = sqrtf(v->aim[0] * v->aim[0] + v->aim[1] * v->aim[1]);
    if (h < 0.05f) { v->left = 0; return; }
    float fx = v->aim[0] / h, fy = v->aim[1] / h, feet = v->from[2] - 1.4f;
    float burst = v->cone ? s_s1ConeBurst : s_s1Burst;
    int per = burst <= 0 ? v->left : 1;   /* one at a time: many at once and the game leaves some of their blasts silent */
    for (int k = 0; k < per && v->left > 0; k++)
    {
        int i = v->n - v->left;
        float t = v->n > 1 ? (float)i / (v->n - 1) : 0, r = (float)rand() / RAND_MAX * 2 - 1;
        float a = v->cone ? (t * 2 - 1) * s_s1ConeAngle * 0.0174533f : 0, c = cosf(a), sn = sinf(a);
        float dx = fx * c - fy * sn, dy = fx * sn + fy * c;
        float far_ = v->cone ? v->range * ((i & 1) ? 1.0f : 0.55f) : 3.0f + (v->range - 3.0f) * t;
        float side = v->cone ? 0 : ((i & 1) ? 0.5f : -0.5f) * s_s1LineWidth + r * 0.3f, p[3];   /* the line: left and right in turn */
        OnGround(unit, v->from[0] + dx * far_ - dy * side, v->from[1] + dy * far_ + dx * side, feet, p);
        p[2] -= 0.4f;   /* (OnGround is half a metre up: just over the ground) */
        float dir[3] = {p[0] - v->from[0], p[1] - v->from[1], p[2] - v->from[2]};
        float n = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        if (n < 0.5f) { v->left--; continue; }
        for (int j = 0; j < 3; j++) dir[j] /= n;
        float from[3] = {v->from[0] + dir[0] * 0.9f, v->from[1] + dir[1] * 0.9f, v->from[2] + dir[2] * 0.9f};
        int r2 = hf->spawn_projectile(self, type, from, dir);
        static unsigned noted;
        if (noted++ < 6) Log("round (projectile %u): answer %d%s", type, r2, r2 == HF_ACT_WAITING ? " (its packages are loading)" : "");
        if (r2 == HF_ACT_WAITING && g_now < v->until) { v->next = g_now + 0.05; return; }   /* again in a moment */
        /* its blast where it lands, when it lands (set off by the kit: a shell's own burst came without its sound) */
        Queue(v->group, s_rowHe, p, n / (s_tracerSpeed > 50 ? s_tracerSpeed : 800));
        v->left--;
    }
    v->next = g_now + (burst > 0 && v->n > 1 ? burst / v->n : 0);
    if (v->left <= 0)
    {
        Group* g = &g_group[v->group];
        g->spawned = 1;
        g->spawnedAt = g_now;
        v->until = g_now + 1.2;
    }
}

/* ---- the chain, the stacks ----------------------------------------------------------------------------------------- */
static int g_chain;                    /* 0: none; 1: the line is next; 2: the cone is next */
static int g_chainGroup[3] = {-1, -1, -1};
static int g_chainWait = -1;           /* the group whose hit opens what is next (-1: none) */
static double g_chainUntil;
static void EndChain(const char* why)
{
    if (!g_chain && g_chainWait < 0) return;
    static unsigned told;
    if (told++ < 20) Log("Annihilation Directive: the chain ends (%s); cooldown %.0f s", why, (double)s_s1Cd);
    g_chain = 0;
    g_chainWait = -1;
    g_cd[0] = g_now + s_s1Cd;
}
static void Gain(int* what, int n, int max) { *what += n; if (*what > max) *what = max; if (*what < 0) *what = 0; }
/* what a cast's damage is times: lost health, the Automatons near her, and (Annihilation Directive, Doom Barrage) a
 * Frenzy stack */
static double CastMult(const char* what, int frenzy)
{
    double m = (1 + (double)s_rage * Missing()) * MechBonus();
    if (g_ammoLoaded)
    {
        g_ammoLoaded = 0;
        m *= 1 + (double)s_ammoLoaded;
        static unsigned told;
        if (told++ < 6) Log("%s: Ammo Loaded spent (+%.0f%%)", what, (double)s_ammoLoaded * 100);
    }
    if (frenzy && g_frenzy > 0)
    {
        g_frenzy--;
        m *= 1 + (double)s_frenzyMult;
        Gain(&g_grenades, 1, s_grenadesMax);
        static unsigned told;
        if (told++ < 6) Log("%s: a Frenzy stack spent (+%.0f%%, a Grenade back); %d left", what, (double)s_frenzyMult * 100, g_frenzy);
    }
    return m;
}

/* ---- where she aims ------------------------------------------------------------------------------------------------- */
static uint32_t MyUnit(void)
{
    uint8_t id[24];
    uint32_t me = atomic_load(&g_avatar);
    return Descriptor(me, id) && Quad(id) == AVATAR ? Word(id + 12) : 0;
}
/* her flat aim and how far along it the point she looks at is (at most `max`; `max` when she looks level or up) */
static int AimFlat(float max, float dir[2], float* dist)
{
    float pos[3], aim[3];
    if (!PlayerPose(pos, aim)) return 0;
    float h = sqrtf(aim[0] * aim[0] + aim[1] * aim[1]);
    if (h < 0.05f) return 0;
    dir[0] = aim[0] / h; dir[1] = aim[1] / h;
    *dist = max;
    uint32_t unit = MyUnit();
    if (aim[2] < -0.05f && unit && QueryIdle())
    {
        float head[3] = {pos[0], pos[1], pos[2] + 1.6f};
        float d = RayWith(unit, head, aim, max / h, 0, 0x80000009u, NULL);
        if (d > 0 && d <= max / h) *dist = d * h;
    }
    if (*dist < 2) *dist = 2;
    return 1;
}
/* a point's ground (her own height when it cannot be asked), half a metre up */
static void OnGround(uint32_t unit, float x, float y, float ref, float out[3])
{
    float z = ref;
    if (unit && QueryIdle()) GroundAt(unit, x, y, ref, &z);
    out[0] = x; out[1] = y; out[2] = z + 0.5f;
}

/* ---- her leaps (Lenna's teleport: a wall check at chest height, the ground at the spot, the game's fit test) -------- */
static int g_leap;                     /* 1: Annihilation Directive's, 3: Party Time's */
static double g_leapT0;
static float g_leapDir[2], g_leapDist;
static void Landed(int kind, const float from[3], const float to[3], float dist);
static void LeapFrame(void)
{
    if (!g_leap) return;
    const char* name = g_leap == 1 ? "Annihilation Directive" : "Party Time";
    if (!QueryIdle())
    {
        if (g_now - g_leapT0 > 0.5) { g_leap = 0; Log("%s: the physics queries stayed busy: no leap", name); }
        return;
    }
    int kind = g_leap;
    g_leap = 0;
    uint32_t me = atomic_load(&g_avatar), unit = MyUnit();
    float pos[3];
    if (!unit || !PlayerPose(pos, NULL)) { Log("%s: her Helldiver cannot be read", name); return; }
    float dir[3] = {g_leapDir[0], g_leapDir[1], 0};
    float chest[3] = {pos[0], pos[1], pos[2] + 1.0f};
    float wall = Ray(unit, chest, dir, g_leapDist + 0.6f);
    if (wall < 0) { Log("%s: the raycast is unavailable now", name); return; }
    float dist = fminf(g_leapDist, wall - 0.6f), dest[3] = {0};
    int found = 0;
    for (int tries = 0; tries < 5 && dist >= 1.5f && !found; tries++)
    {
        dest[0] = pos[0] + dir[0] * dist;
        dest[1] = pos[1] + dir[1] * dist;
        if (GroundAt(unit, dest[0], dest[1], pos[2], &dest[2]) && KbFitsOn(me, unit, dest) != 0) found = 1;
        else dist *= 0.75f;
    }
    if (!found) { Log("%s: no ground she fits on within %.0f m ahead (at most %.0f m up, %.0f m down): no leap, nothing spent", name, (double)g_leapDist, (double)s_lpClimb, (double)s_lpDrop); return; }
    dest[2] += 0.05f;
    float zero[3] = {0, 0, 0};
    g_setPosition(NULL, me, dest);
    g_setVelocity(NULL, me, zero);
    Landed(kind, pos, dest, dist);
}

/* ---- Annihilation Directive ----------------------------------------------------------------------------------------- */
static double g_castAt = -100;
static void Shield(float seconds)
{
    g_castAt = g_now;
    g_drUntil = g_now + seconds;
    atomic_store(&g_shieldUntilMs, GetTickCount64() + (ULONGLONG)(seconds * 1000));
}
/* a stage's damage: its own, times a Grenade if she has one, times the cast's */
static float StageDamage(const char* what, float base)
{
    double m = 1;
    if (g_grenades > 0) { g_grenades--; m += s_grenadeMult; }
    return (float)(base * m * CastMult(what, 1));
}
static void Annihilation(void)
{
    static unsigned told;
    if (g_leap) return;
    if (!g_chain)
    {
        if (g_chainWait >= 0 || g_now < g_cd[0]) return;
        if (!g_setPosition || !g_setVelocity || !g_raycast) { Log("Annihilation Directive: the leap is unavailable on this build (see the start of the log)"); return; }
        if (!AimFlat(s_s1Leap, g_leapDir, &g_leapDist)) { Log("Annihilation Directive: she is looking straight up or down, or her aim cannot be read"); return; }
        g_leap = 1;
        g_leapT0 = g_now;
        return;
    }
    if (g_chainWait >= 0) return;      /* the stage before has not landed its hit yet */
    float from[3], aim[3];
    if (!StageAim(from, aim)) return;
    int stage = g_chain;               /* 1: the line, 2: the cone */
    if (!g_tracerReady || !g_tracer[stage - 1]) { Log("Annihilation Directive: its rounds are unavailable (see the start of the log)"); EndChain("no tracers"); return; }
    Pay(stage == 1 ? "Annihilation Directive I" : "Annihilation Directive II", s_s1Cost);
    float damage = StageDamage("Annihilation Directive", stage == 1 ? s_s1Damage1 : s_s1Damage2);
    int g = NewGroup(stage == 1 ? SK_LINE : SK_CONE, damage, stage == 1 ? s_s1Line : s_s1Cone);
    Volley* v = &g_volley;
    memset(v, 0, sizeof *v);
    v->group = g;
    v->cone = stage == 2;
    v->n = v->left = s_s1Rounds;
    v->range = stage == 1 ? s_s1Line : s_s1Cone;
    v->until = g_now + 4;
    memcpy(v->from, from, sizeof v->from);
    TracerSpeed(stage - 1);
    memcpy(v->aim, aim, sizeof v->aim);
    g_chainGroup[stage] = g;
    g_chainWait = g;
    g_chain = stage + 1;               /* 3 after the cone: the chain ends when its hit is known */
    Gain(&g_offense, 1, s_offenseMax);
    if (stage == 2) g_ammoLoaded = 1;
    Shield(s_s1Shield);
    if (told++ < 20) Log("Annihilation Directive %s: %d rounds from %.1f %.1f %.1f along %.2f %.2f %.2f, %.0f damage an enemy within %.0f m, %d Grenade(s) left", stage == 1 ? "I (the line)" : "II (the cone)", s_s1Rounds,
        (double)from[0], (double)from[1], (double)from[2], (double)aim[0], (double)aim[1], (double)aim[2], (double)damage, (double)v->range, g_grenades);
}
/* the chain's waiting stage: a hit opens what is next; none in time ends the chain */
static void ChainFrame(void)
{
    if (g_chainWait >= 0)
    {
        Group* g = &g_group[g_chainWait];
        if (g->hits > 0)
        {
            g_chainWait = -1;
            if (s_s1Haste > 0) { g_speedUntil = g_now + s_s1Haste; Speed(1); }
            if (g_chain >= 3) EndChain("the cone landed");
            else g_chainUntil = g_now + s_s1Window;
        }
        else if ((g->spawned && g_now - g->spawnedAt > (g->skill == SK_LEAP ? 0.7 : 1.2)) || g_now - g->at > 5) EndChain("the stage hit nothing");
    }
    else if (g_chain && g_now > g_chainUntil) EndChain("the next stage was not used in time");
}

/* ---- Doom Barrage --------------------------------------------------------------------------------------------------- */
static void DoomBarrage(void)
{
    if (g_now < g_cd[1]) return;
    float pos[3], dir[2], far_;
    uint32_t unit = MyUnit();
    if (!PlayerPose(pos, NULL) || !AimFlat(10, dir, &far_)) { Log("Doom Barrage: her aim cannot be read"); return; }
    Pay("Doom Barrage", s_s2Cost);
    int n = g_grenades;
    g_grenades = 0;
    double m = (1 + n * (double)s_s2Grenade) * (n >= 3 ? s_s2Crit : 1) * CastMult("Doom Barrage", 1);
    float damage = (float)(s_s2Damage * m), step = s_s2Length / 3.0f, half = s_s2Width / 4.0f;
    float radius = fmaxf(step, s_s2Width / 2.0f) * 0.75f + 1.0f;
    int g = NewGroup(SK_BARRAGE, damage, radius);
    float p[3];
    for (int row = 0; row < 3; row++)   /* rows from near to far, two blasts a row */
        for (int col = -1; col <= 1; col += 2)
        {
            float d = step * (row + 0.5f) + 2.0f, x = pos[0] + dir[0] * d - dir[1] * half * col, y = pos[1] + dir[1] * d + dir[0] * half * col;
            OnGround(unit, x, y, pos[2], p);
            Queue(g, s_rowSmall, p, (double)s_s2Step * (row * 2 + ((col > 0) ^ (row & 1))));   /* one after another, the sides in turn */
        }
    Gain(&g_fang, 1, 3);
    g_cd[1] = g_now + s_s2Cd;
    g_casts[1]++;
    Shield(1.0f);
    Log("Doom Barrage: %d Grenade(s) spent%s, %.0f damage an enemy over %.0f x %.0f m; Fang %d", n, n >= 3 ? " (critical)" : "", (double)damage, (double)s_s2Length, (double)s_s2Width, g_fang);
}

/* ---- Party Time ----------------------------------------------------------------------------------------------------- */
static void PartyTime(void)
{
    if (g_now < g_cd[2] || g_leap) return;
    if (!g_setPosition || !g_setVelocity || !g_raycast) { Log("Party Time: the leap is unavailable on this build (see the start of the log)"); return; }
    if (!AimFlat(s_s3Leap, g_leapDir, &g_leapDist)) { Log("Party Time: she is looking straight up or down, or her aim cannot be read"); return; }
    g_leap = 3;
    g_leapT0 = g_now;
}
static void Landed(int kind, const float from[3], const float to[3], float dist)
{
    uint32_t unit = MyUnit();
    float p[3];
    if (kind == 1)
    {
        Pay("Annihilation Directive", s_s1Cost);
        float damage = StageDamage("Annihilation Directive", s_s1Damage);
        int g = NewGroup(SK_LEAP, damage, s_s1Radius);
        p[0] = to[0]; p[1] = to[1]; p[2] = to[2] + 0.5f;
        Queue(g, s_rowSmall, p, 0);
        g_chainGroup[0] = g; g_chainGroup[1] = g_chainGroup[2] = -1;
        g_chainWait = g;
        g_chain = 1;
        g_casts[0]++;
        Gain(&g_offense, 1, s_offenseMax);
        Shield(s_s1Shield);
        Log("Annihilation Directive: leapt %.1f m (%+.1f m in height), %.0f damage where she landed, %d Grenade(s) left", (double)dist, (double)(to[2] - from[2]), (double)damage, g_grenades);
        return;
    }
    float damage = (float)(s_s3Damage * CastMult("Party Time", 0));
    int g = NewGroup(SK_PARTY, damage, s_s3Width / 2.0f + 0.5f);
    int n = (int)ceilf(dist / (s_s3Width * 0.6f));
    if (n < 1) n = 1;
    if (n > 8) n = 8;
    for (int k = 0; k <= n; k++)       /* blasts along the path, from where she stood to where she landed */
    {
        float t = (float)k / n;
        if (k == n) { p[0] = to[0]; p[1] = to[1]; p[2] = to[2] + 0.5f; }
        else OnGround(unit, from[0] + (to[0] - from[0]) * t, from[1] + (to[1] - from[1]) * t, from[2] + (to[2] - from[2]) * t, p);
        Queue(g, s_rowSmall, p, (double)s_s3Step * k);   /* a chain, from where she stood to where she is */
    }
    g_grenades = s_grenadesMax;
    Gain(&g_fang, 1, 3);
    g_cd[2] = g_now + s_s3Cd;
    g_casts[2]++;
    Shield(2.0f);
    Log("Party Time: leapt %.1f m, %d blasts on the path, %.0f damage an enemy; Grenades full, Fang %d", (double)dist, n + 1, (double)damage, g_fang);
}

/* ---- Ammo Frenzy ---------------------------------------------------------------------------------------------------- */
static void AmmoFrenzy(void)
{
    if (g_now < g_cd[3]) return;
    if (g_fang < 3) { static unsigned told; if (told++ < 5) Log("Ammo Frenzy: needs 3 Fang (she has %d)", g_fang); return; }
    float pos[3], aim[3], p[3], at[3];
    if (!PlayerPose(pos, aim)) return;
    float head[3] = {pos[0], pos[1], pos[2] + 1.6f}, best = cosf(s_frenzyCone * 0.0174533f);
    int found = 0;
    for (int i = 0; i < FOES; i++)
    {
        if (!FoePos(i, p) || !Living(g_foe[i])) continue;
        float v[3] = {p[0] - head[0], p[1] - head[1], p[2] + 1.0f - head[2]}, n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (n < 1 || n > s_frenzyRange) continue;
        float c = (v[0] * aim[0] + v[1] * aim[1] + v[2] * aim[2]) / n;
        if (c >= best) { best = c; memcpy(at, p, sizeof at); found = 1; }
    }
    uint32_t unit = MyUnit();
    if (!found && unit && QueryIdle())   /* no enemy in her crosshair: where she looks */
    {
        float d = RayWith(unit, head, aim, s_frenzyRange, 0, 0x80000009u, NULL);
        if (d > 3 && d <= s_frenzyRange) { for (int k = 0; k < 3; k++) at[k] = head[k] + aim[k] * d; found = 2; }
    }
    if (!found) { static unsigned told; if (told++ < 8) Log("Ammo Frenzy: no enemy within %.0f degrees of her crosshair and nothing in sight within %.0f m: not used", (double)s_frenzyCone, (double)s_frenzyRange); return; }
    int n = 0;
    for (int i = 0; i < FOES; i++) if (FoePos(i, p) && Dist2(p, at) <= s_frenzyRadius * s_frenzyRadius && Living(g_foe[i])) n++;
    double m = CastMult("Ammo Frenzy", 0);
    float share = (float)(s_frenzyDamage * m / (n > 0 ? n : 1));
    int g = NewGroup(SK_FRENZY, share, s_frenzyRadius);
    float c[3] = {at[0], at[1], at[2] + 0.5f};
    Queue(g, s_rowBig, c, 0);
    for (int k = 0; k < s_frenzyRing; k++)
    {
        float a = 6.2831853f * k / s_frenzyRing, r = s_frenzyRadius * 0.55f;
        OnGround(unit, at[0] + cosf(a) * r, at[1] + sinf(a) * r, at[2], p);
        Queue(g, s_rowBig, p, 0.25 + 0.1 * k);
    }
    g_fang = 0;
    g_cd[3] = g_now + 2;
    for (int k = 0; k < 3; k++) if (g_cd[k] > g_now) g_cd[k] = fmax(g_now, g_cd[k] - s_frenzyRefund);
    g_casts[3]++;
    Shield(2.0f);
    Log("Ammo Frenzy: on %s, %d enem%s within %.0f m share %.0f damage (%.0f each); Hunting Order %.0f s; her cooldowns %.0f s shorter",
        found == 1 ? "the enemy in her crosshair" : "where she looks", n, n == 1 ? "y" : "ies", (double)s_frenzyRadius, (double)(s_frenzyDamage * m), (double)share,
        (double)s_huntingSeconds, (double)s_frenzyRefund);
}

/* ---- the damage event ------------------------------------------------------------------------------------------------ */
static int SelfBlastAt(const float at[3]);
/* A hit on her (HellForge's damage event; kitbase/kb_damage.h routes it here) */
static void DamageTaken(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    /* (for the log: what reaches her in the two seconds after a cast; she was still thrown by her skills in 0.2.3) */
    {
        static unsigned told;
        if (g_now - g_castAt < 2.0 && told < 60)
        {
            told++;
            Log("after a cast (%.2f s): a hit on her of %d, kind %u, element %u, dealer %u, owner %u%s", g_now - g_castAt, d->amount, d->kind, d->element, d->dealer, d->owner,
                d->kind == 4 && (BlastAt(d->position, NULL) || SelfBlastAt(d->position)) ? ": her own, spared" : "");
        }
    }
    /* her own blasts spare her */
    if (d->kind == 4 && (BlastAt(d->position, NULL) || SelfBlastAt(d->position))) { e->cancel = 1; g_selfSpared++; return; }
    if (d->amount <= 0) return;
    if (g_now < g_deniedUntil) { e->cancel = 1; return; }
    /* a leap's window: no fall or landing damage (impact or flat, from nothing) */
    if (g_now < g_drUntil && (d->kind == 5 || d->kind == 7) && !NetOk(d->dealer) && !NetOk(d->owner)) { e->cancel = 1; return; }
    /* Offense Only: a stack spent, this hit and the next moment's do nothing */
    if (g_now < g_offenseUntil) { e->cancel = 1; return; }
    if (g_offense > 0)
    {
        g_offense--;
        g_offenseUntil = g_now + s_offenseWindow;
        g_offenseSpent++;
        e->cancel = 1;
        static unsigned told;
        if (told++ < 12) Log("Offense Only: a hit of %d on her (kind %u) does nothing; %d stack(s) left", d->amount, d->kind, g_offense);
        return;
    }
    double m = 1;
    if (Mech() && (NetOk(d->dealer) || NetOk(d->owner))) m *= 1 - (double)s_mechTaken;
    m *= 1 - 0.5 * (double)s_rage * Missing();
    if (m < 0.05) m = 0.05;
    if (m < 0.999)
    {
        double a = floor(d->amount * m + 0.5);
        if (a < 1) { e->cancel = 1; return; }
        d->amount = (int32_t)a;
    }
    /* Denied Exit */
    int32_t hp, max;
    if (g_now >= g_deniedReady && Hp(&hp, &max) && d->amount >= hp)
    {
        if (hp <= 1) e->cancel = 1;
        else d->amount = hp - 1;
        g_deniedUntil = g_now + s_deniedSeconds;
        g_deniedReady = g_now + s_deniedCd;
        g_denied++;
        Log("Denied Exit: a killing hit on her %d health leaves her at 1; nothing hurts her for %.1f s; again in %.0f s", hp, (double)s_deniedSeconds, (double)s_deniedCd);
    }
}
/* A skill's hit on an enemy: paid once by its group, at the skill's damage */
static void PayHit(HfDamagePreEvent* e, HfDamageEvent* d, Group* g, uint64_t res, float dist, const char* by)
{
    if (PaidBy(g, d->target) || g->npaid >= PAID) { e->cancel = 1; g_cutHits++; return; }
    g->paid[g->npaid++] = d->target;
    g->hits++;
    g_paidHits++;
    double amount = g->damage;
    int armour = 0;
    int32_t full = 0;
    KindBody(res, &armour, &full);
    int boss = armour >= s_bossArmour;
    if (Mech() && !boss && full > 0) amount += (double)s_mechHp * full;
    if (Hunted(d->target)) amount *= 1 + (double)s_huntingTaken;
    if (g->skill == SK_CONE && g_chainGroup[0] >= 0 && g_chainGroup[1] >= 0 && PaidBy(&g_group[g_chainGroup[0]], d->target) && PaidBy(&g_group[g_chainGroup[1]], d->target))
    {
        amount += (double)s_s1Triple * g->damage;
        static unsigned told;
        if (told++ < 10) Log("Annihilation Directive: all three stages hit entity %u: +%.0f", d->target, (double)s_s1Triple * g->damage);
    }
    if (g->skill == SK_PARTY)
    {
        Gain(&g_frenzy, boss ? 3 : 1, s_frenzyMax);
        if (boss) g_healFull = 1;
        else g_healOwed += s_s3Heal;
    }
    if (g->skill == SK_FRENZY) HuntMark(d->target);
    d->amount = amount > 2147483647.0 ? 2147483647 : amount < 1 ? 1 : (int32_t)amount;
    AddFoe(d->target, res);
    static unsigned told;
    if (told++ < 60) Log("her %s (skill %d) on entity %u: %d damage%s%s (%.1f m away)", by, g->skill, d->target, d->amount, boss ? ", boss-sized" : "", Hunted(d->target) ? ", Hunting Order" : "", (double)dist);
}
/* Any other hit: her blasts' and her tracers' on enemies and friends, her shots */
/* A shield she may carry: it takes hits of its own, and a blast of hers on it threw her (0.2.5 and before) */
static int ShieldKind(uint64_t res)
{
    return res == HF_ENTITY_SH20_BALLISTIC_SHIELD_BACKPACK || res == HF_ENTITY_SH51_DIRECTIONAL_SHIELD || res == HF_ENTITY_GSH39_SHIELD;
}
static void DamageDealt(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    if (d->kind == 4)
    {
        float dist = 0;
        Burst* b = BlastAt(d->position, &dist);
        if (b)
        {
            uint64_t res;
            int friend_ = TargetFriend(d->target, &res);
            if (ShieldKind(res))
            {
                static unsigned told;
                KbHers(d->target);
                e->cancel = 1;
                g_selfSpared++;
                if (told++ < 4) Log("her blast on a shield (entity %u, %016llx): spared, and her blasts spare it in the hit code from now on", d->target, (unsigned long long)res);
                return;
            }
            if (friend_) { e->cancel = 1; g_friendSpared++; return; }
            if (!res || !FoeKind(res)) return;   /* not an enemy the kit knows (a wall, a nest, a turret): the row's own hit */
            int stage = g_group[b->group].skill == SK_LINE || g_group[b->group].skill == SK_CONE;   /* (a stage's shells burst small: all they reach) */
            if (!stage && dist > g_group[b->group].radius + 2.0f) { e->cancel = 1; g_cutHits++; return; }   /* the row reaches further than the skill */
            PayHit(e, d, &g_group[b->group], res, dist, stage ? "shell burst" : "blast");
            return;
        }
    }
    /* a stage's rounds that hit an enemy themselves: her projectile hits while its burst is in the air */
    if ((d->kind == 0 || d->kind == 1) && g_volley.group >= 0 && g_now < g_volley.until && MineHit(d))
    {
        uint64_t res;
        if (TargetFriend(d->target, &res)) { e->cancel = 1; g_friendSpared++; return; }
        if (!res || !FoeKind(res)) return;
        float dist = sqrtf(Dist2(d->position, g_volley.from));
        if (dist > g_volley.range + 6.0f) { e->cancel = 1; g_cutHits++; return; }   /* further than the stage reaches */
        PayHit(e, d, &g_group[g_volley.group], res, dist, "round");
        return;
    }
    if (d->amount <= 0) return;
    if (IsShot(d))
    {
        NoteTarget(d);
        if (Hunted(d->target)) ScaleDamage(d, 1 + (double)s_huntingTaken);
    }
}

/* ---- her hands ----------------------------------------------------------------------------------------------------------
 * For each weapon she carries, once: its round's arming distance to 0 (a round with an arming
 * distance is a grenade launcher's: ProjectileSettings +0xA0, the GL-21's 7 m, the GL-52's 4). All of it is put back when the mission ends or the kit stands by. */
typedef struct Hand { uint64_t res; uint32_t projectile; uint8_t armed; } Hand;
static Hand g_hand[12];
static int g_nhand;
static const HfField kArming = {"ProjectileSettings", 25480438u, 0xA0, 4, HF_TYPE_F32, __LINE__};
static void HandSet(uint64_t res)
{
    if (g_nhand >= 12) return;
    Hand* h = &g_hand[g_nhand++];
    memset(h, 0, sizeof *h);
    h->res = res;
    HfLiveValue v;
    HfField proj = HF_FIELD(ProjectileWeapon, projectile), rounds = HF_FIELD(WeaponRounds, primary_projectile);
    memset(&v, 0, sizeof v);
    if (hf->get_live(self, res, &proj, &v) && v.base.number > 0 && v.base.number < 100000) h->projectile = (uint32_t)v.base.number;
    memset(&v, 0, sizeof v);
    if (!h->projectile && hf->get_live(self, res, &rounds, &v) && v.base.number > 0 && v.base.number < 100000) h->projectile = (uint32_t)v.base.number;
    /* a grenade launcher's round: armed as it leaves */
    memset(&v, 0, sizeof v);
    if (h->projectile && s_glArming && hf->get_live(self, (HfRef)h->projectile, &kArming, &v) && v.base.number > 0.01 && v.base.number < 1000)
    {
        double was = v.base.number;
        int set = -1;
        HfValue zero = HF_F32(0);
        set = hf->set_live(self, (HfRef)h->projectile, &kArming, &zero, 0);
        h->armed = set != 0;
        Log("her hands: weapon %016llx fires a grenade round (projectile %u, arming distance %.1f m): arming %s (answer %d)", (unsigned long long)res, h->projectile, was,
            h->armed ? "set to 0" : "REFUSED", set);
    }
}
/* Held in one hand (docs/research/one-handed.md, the kit base's One-Handed Grip on the kits-upgrade-test branch): the
 * game's Equipment table, [[game + 0x346BF98] + 0xF12478 + 8 * 233] -> index rows {u64 resource, u32 record, u32 0} x
 * 688, 232-byte records from +11008; +0xC4 bit 0 is one-handed, +0xC0 the grip. Every record at exactly 0x00010000 (a
 * two-handed weapon) changes at the mission's start, so what she equips after has it, and goes back at its end.
 * (0.1.8 asked HellForge for each weapon's record: it has no such read for Equipment.) Game build 25480438. */
#define ONE_ROWS 688u
#define ONE_RECORDS 1024u
static struct { uint32_t record, grip; } g_oneChanged[ONE_RECORDS];
static int g_oneChangedN;
static uintptr_t OneTable(void)
{
    const char* build = hf->build_id ? hf->build_id() : NULL;
    if (!build || strcmp(build, BUILD) || !game) return 0;
    uintptr_t root = Pointer(game + 0x346BF98);
    return root ? Pointer(root + 0xF12478 + 8u * 233u) : 0;
}
static void OneHanded(int on)
{
    uintptr_t table = OneTable();
    if (!table) { if (on) Log("her hands: the Equipment table is not there: nothing held one-handed"); return; }
    if (on)
    {
        static uint8_t seen[ONE_RECORDS];
        memset(seen, 0, sizeof seen);
        g_oneChangedN = 0;
        int rows = 0, others = 0;
        for (uint32_t i = 0; i < ONE_ROWS; i++)
        {
            struct { uint64_t res; uint32_t record, zero; } row;
            if (!ReadAt(table + 16u * i, &row, sizeof row)) break;
            if (!row.res || row.record >= ONE_RECORDS || seen[row.record]) continue;
            /* weapons only (0.2.4; 0.1.9 marked every two-handed record, 232 of them, the things that are not weapons too) */
            uint32_t stride = 0;
            if (!hf->component_record("ProjectileWeaponComponentData", row.res, &stride) && !hf->component_record("BeamWeaponComponentData", row.res, &stride) &&
                !hf->component_record("ArcWeaponComponentData", row.res, &stride) && !hf->component_record("SprayWeaponComponentData", row.res, &stride)) { others++; continue; }
            seen[row.record] = 1;
            rows++;
            uintptr_t at = table + 11008u + (uintptr_t)row.record * 232u;
            uint32_t flags = 0, grip = 0, one = 0x00010001u, pose = (uint32_t)s_oneHandGrip;
            if (!ReadAt(at + 0xC4, &flags, 4) || flags != 0x00010000u || !ReadAt(at + 0xC0, &grip, 4)) continue;
            if (!pose) pose = grip;
            if (hf->write(at + 0xC4, &one, 4) || hf->write(at + 0xC0, &pose, 4)) continue;
            g_oneChanged[g_oneChangedN].record = row.record;
            g_oneChanged[g_oneChangedN++].grip = grip;
        }
        Log("her hands: %d two-handed weapon record(s) of %d made one-handed (grip %d; 0: their own; %d rows that are not weapons left alone): what she equips from now on", g_oneChangedN, rows, s_oneHandGrip, others);
    }
    else if (g_oneChangedN)
    {
        int back = 0;
        for (int i = 0; i < g_oneChangedN; i++)
        {
            uintptr_t at = table + 11008u + (uintptr_t)g_oneChanged[i].record * 232u;
            uint32_t flags = 0x00010000u, grip = g_oneChanged[i].grip;
            back += !hf->write(at + 0xC4, &flags, 4) && !hf->write(at + 0xC0, &grip, 4);
        }
        Log("her hands: %d weapon record(s) back to two-handed", back);
        g_oneChangedN = 0;
    }
}
/* Homing grenades (docs/research/homing.md and lock-on.md, Lock-on & Stim Test 0.2.0): the grenade launchers fire the
 * P-33 Missile Pistol's guided missile (ProjectileWeapon +0x28; it carries the launcher's own grenade), and each missile
 * that flies with no target (state +0x00 = 0x7FFF in the seeking-missile manager [game + 0x3326DF0]) is handed the
 * nearest living enemy along the way it was fired: state +0x00 = its network id (game + 0xFD9AF0), +0x04 = 0x10 (its
 * default aim point), +0x34 = 1 (guidance on). The game's own guidance flies it; a missile whose target dies gets the
 * next. (HellForge's homing rule on the grenade itself found its target too late and flew past: 0.1.8 to 0.2.0.) */
#define P33_MISSILE UINT64_C(0xA1470A4DAB237264)
#define P33_PACKAGE UINT64_C(0x64C37A90C0D906DE)   /* packages/generated/loadout/pistol_smart_missile */
#define SPEAR_PACKAGE UINT64_C(0xF904B93FBB70584C) /* packages/generated/loadout/faf_missile_launcher (the name's hash; not seen in HellForge's list) */
#define SPEAR_MISSILE UINT64_C(0x1A4323835B84CEA7) /* what the FAF-14 Spear fires (seen in flight, 0.2.6; targeting mode 2) */
#define NO_NET 0x7FFFu
static const uint64_t kLaunchers[] = {HF_ENTITY_GL21_GRENADE_LAUNCHER, HF_ENTITY_GL28_BELTFED_GRENADE_LAUNCHER, HF_ENTITY_GL52_DEESCALATOR,
                                      HF_ENTITY_GL15_EVICTOR, HF_ENTITY_GP31_GRENADE_PISTOL, HF_ENTITY_GP20_ULTIMATUM};
#define LAUNCHERS (sizeof kLaunchers / sizeof kLaunchers[0])
static int g_missileCode, g_missilePkg, g_missileOn;
static unsigned g_flakN, g_flakOwn;
static uint64_t g_missile, g_missilePackage;
static const char* g_missileName = "";
static uintptr_t g_missileRecord;
static int g_missileSpreadOff, g_missileFlies;
static uint8_t g_launcherSet[LAUNCHERS];
static uint32_t (*g_netId)(uint32_t entity);
typedef struct Missile { uint32_t entity, target; int handed, lost; float from[3], dir[3]; } Missile;
static Missile g_ms[32];
static unsigned g_msNext, g_msSeen, g_msHanded, g_msLost;
static void MissileInit(void)
{
    if (!hf->code_matches(game + 0xFD9AF0, "48 83 EC 08 3B 0D ?? ?? ?? ?? 75 0B 8B 05 ?? ?? ?? ?? 48 83 C4 08 C3") ||
        !hf->code_matches(game + 0x641148, "8B F7 4C 6B C6 38 F3 0F 10 40 44 4C 8D 2C 76 48 89 44 24 58") ||
        !hf->code_matches(game + 0x642B05, "8B 14 1E 48 8D 4C 24 44 E8 ?? ?? ?? ?? 80 7C 1E 34 00") ||
        !hf->code_matches(game + 0x642A0F, "41 8B 45 6C 41 89 47 60 41 8B 4D 00 85 C9 0F 84 ?? ?? ?? ??"))
    { Log("homing grenades off: the game's missile code is not the reviewed one"); return; }
    g_netId = (uint32_t (*)(uint32_t))(game + 0xFD9AF0);
    g_missileCode = 1;
}
static void MissileSwap(int on)
{
    HfField f = HF_FIELD(ProjectileWeapon, projectile_entity);
    HfValue v = HF_U64(g_missile);
    if (on && (g_missileOn || g_missileCode != 2 || g_missilePkg != HF_PACKAGE_RESIDENT || !s_glHoming || !s_homingMissile)) return;
    if (!on && !g_missileOn) return;
    int n = 0;
    for (unsigned i = 0; i < LAUNCHERS; i++)
    {
        if (on) { g_launcherSet[i] = hf->set_live(self, kLaunchers[i], &f, &v, 0) != 0; n += g_launcherSet[i]; }
        else if (g_launcherSet[i]) { hf->set_live(self, kLaunchers[i], &f, NULL, 0); g_launcherSet[i] = 0; n++; }
    }
    g_missileOn = on && n;
    /* (its record in the log, whole: 0.2.10 also made it move at once and start faster, on a misreading; taken out) */
    if (g_missileOn && !g_missileFlies && g_missileRecord)
    {
        float f[68];
        char line[900];
        int n = 0;
        memset(f, 0, sizeof f);
        ReadAt(g_missileRecord, f, sizeof f);
        for (int i = 0; i < 68 && n < (int)sizeof line - 16; i++) n += snprintf(line + n, sizeof line - (size_t)n, "%s%.4g", i ? " " : "", (double)f[i]);
        Log("homing grenades: the missile's record, 4 bytes a number from +0x00: %s", line);
        g_missileFlies = 1;
    }
    /* its spread off (SeekingMissile +0xB4 deviation_enabled: the P-33's missile veers off its launch line, curves and
     * swings back onto its target; fired straight at an enemy that stood close it went around behind it, 0.2.6) */
    uint8_t spread = 0, none = 0, one = 1;
    if (g_missileOn && !g_missileSpreadOff && g_missileRecord && ReadAt(g_missileRecord + 0xB4, &spread, 1))
    {
        if (spread == 1) g_missileSpreadOff = !hf->write(g_missileRecord + 0xB4, &none, 1);
        Log("homing grenades: the missile's spread (it veers off its line and swings back) was %s%s", spread ? "on" : "off already", spread == 1 ? (g_missileSpreadOff ? ": turned off for the mission" : ": could not be turned off") : "");
    }
    else if (!on && g_missileSpreadOff) { hf->write(g_missileRecord + 0xB4, &one, 1); g_missileSpreadOff = 0; }
    if (on) Log("homing grenades: %d of %u grenade launchers fire the %s's missile with their own grenade (one equipped or called in from now on)", n, (unsigned)LAUNCHERS, g_missileName);
    else Log("homing grenades: %d launcher(s) put back; %u missiles seen, %u target hand-offs, %u found no enemy, %u went off as flak (%u by the kit's own blast)", n, g_msSeen, g_msHanded, g_msLost, g_flakN, g_flakOwn);
    if (!on) { memset(g_ms, 0, sizeof g_ms); g_msSeen = g_msHanded = g_msLost = 0; }
}
/* Which guided missile the launchers fire: the FAF-14 Spear's (homing_missile 0, a weapon everyone has), or the P-33
 * Missile Pistol's (1, a premium weapon's; the one the research proved). Looked up once the game's entities are in
 * (every frame, in a mission or not: the package has to be in before a launcher is made). */
static int MissileDonor(uint64_t entity, uint64_t package, const char* name)
{
    uint32_t stride = 0, mode = 0;
    uintptr_t record = entity ? hf->component_record("SeekingMissileComponentData", entity, &stride) : 0;
    if (!record || !ReadAt(record, &mode, 4)) return -1;
    if (mode != 2) { Log("homing grenades: the %s's missile (%016llx) has targeting mode %u, not 2 (follow an entity): not used", name, (unsigned long long)entity, mode); return 0; }
    float f[17];
    memset(f, 0, sizeof f);
    ReadAt(record, f, sizeof f);
    g_missile = entity;
    g_missileRecord = record;
    g_missilePackage = package;
    g_missileName = name;
    g_missileCode = 2;
    g_missilePkg = HF_PACKAGE_LOADING;
    Log("homing grenades ready: the %s's missile (%016llx; guidance on after %.2f s, lifetime %.0f s)", name, (unsigned long long)entity, (double)f[3], (double)f[16]);
    return 1;
}
static void MissilePackage(void)
{
    static double next;
    static int tries;
    if (s_glHoming && s_homingMissile && g_missileCode == 1 && g_now >= next)
    {
        next = g_now + 1;
        int r = s_homingMissile == 2 ? MissileDonor(P33_MISSILE, P33_PACKAGE, "P-33 Missile Pistol") : MissileDonor(SPEAR_MISSILE, SPEAR_PACKAGE, "FAF-14 Spear");
        if (r == 0) g_missileCode = 0;
        else if (r < 0 && ++tries == 120) { g_missileCode = 0; Log("homing grenades off: the guided missile's record was not found in two minutes"); }
    }
    if (!s_glHoming || !s_homingMissile || g_missileCode != 2 || g_missilePkg != HF_PACKAGE_LOADING) return;
    g_missilePkg = hf->load_package(self, g_missilePackage);
    if (g_missilePkg == HF_PACKAGE_RESIDENT) Log("homing grenades: the %s's package is in", g_missileName);
    else if (!g_missilePkg)
    {
        /* (the Spear's package name is a guess; without it the missile may show as the game's placeholder until a Spear is in the mission) */
        Log("homing grenades: the %s's package (%016llx) was refused: going on without it", g_missileName, (unsigned long long)g_missilePackage);
        g_missilePkg = HF_PACKAGE_RESIDENT;
    }
}
/* The enemy a missile gets: within homing_cone degrees of where she was looking when it left (the camera's ray) and
 * homing_range m of her, the one that is nearest and most in line (its distance, up to four times as far when at the
 * cone's edge). 0.2.1 to 0.2.4 took the nearest to the missile within the cone of its own launch direction, which is
 * not where she looks: far enemies were picked over ones in front of her. */
static uint32_t MissileTarget(const float at[3], const float fwd[3])
{
    float cosCone = cosf(s_homingCone * 3.14159265f / 180), best = 1e30f;
    float fl = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    uint32_t pick = 0;
    if (fl < 0.1f) return 0;
    for (int i = 0; i < FOES; i++)
    {
        float p[3];
        if (!g_foe[i] || !FoePos(i, p)) continue;
        p[2] += 1.0f;
        float d[3] = {p[0] - at[0], p[1] - at[1], p[2] - at[2]};
        float d2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        if (d2 >= s_homingRange * s_homingRange || d2 < 1) continue;
        float dist = sqrtf(d2), c = (d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2]) / (dist * fl);
        if (c < cosCone) continue;
        float score = dist * (1 + 3 * (1 - c) / (1 - cosCone + 1e-6f));
        if (score >= best || !Living(g_foe[i])) continue;
        best = score;
        pick = g_foe[i];
    }
    return pick;
}
static int MissileTargetLives(uint32_t e)
{
    for (int i = 0; i < FOES; i++) if (g_foe[i] == e) return Living(e);
    return 0;
}
/* Flak (0.2.11): a homing grenade within flak_radius m of a living enemy goes off there. The missile is ended (its age,
 * [manager + 0x40] + index * 12 + 8, set past its lifetime); if no blast of her weapons follows near that spot within a
 * fifth of a second, the kit sets off the grenade's own (the carried round's ProjectileSettings +0x90, else the stages'). */
typedef struct Flak { float p[3]; double at; uint32_t row; int blasted; } Flak;
static Flak g_flak[8];
static unsigned g_flakNext;
static const HfField kRoundBlast = {"ProjectileSettings", 25480438u, 0x90, 4, HF_TYPE_U32, __LINE__};
static int FlakNear(const float at[3])
{
    for (int i = 0; i < FOES; i++)
    {
        float p[3];
        if (!g_foe[i] || !FoePos(i, p)) continue;
        p[2] += 1.0f;
        if (Dist2(p, at) <= s_flakRadius * s_flakRadius && Living(g_foe[i])) return 1;
    }
    return 0;
}
static void MissilesFrame(void)
{
    if (!g_missileOn) return;
    uintptr_t mgr = Pointer(game + 0x3326DF0), heads, states, locals;
    uint32_t count = 0;
    if (!mgr || !ReadAt(mgr + 0x10, &count, 4) || count == 0 || count > 4096) return;
    if (!(heads = Pointer(mgr + 0x38)) || !(states = Pointer(mgr + 0x50)) || !(locals = Pointer(mgr + 0x48))) return;
    for (uint32_t i = 0; i < count && i < 256; i++)
    {
        uintptr_t h = Pointer(heads + 8u * i);
        uint32_t entity = 0, net = 0, shooter = 0;
        if (!h || !ReadAt(h + 8, &entity, 4) || !entity) continue;
        uintptr_t state = states + 0x38u * i, local = locals + 0xC0u * i;
        if (!ReadAt(state, &net, 4) || !ReadAt(local + 0x84, &shooter, 4)) continue;
        Missile* m = NULL;
        for (int k = 0; k < 32 && !m; k++) if (g_ms[k].entity == entity) m = &g_ms[k];
        if (!m)
        {
            uint8_t sid[24];
            int hers = 0;
            if (Descriptor(shooter, sid)) for (unsigned k = 0; k < LAUNCHERS; k++) hers |= Quad(sid) == kLaunchers[k];
            m = &g_ms[g_msNext++ % 32];
            memset(m, 0, sizeof *m);
            m->entity = entity;
            m->handed = !hers || net != NO_NET ? -1 : 0;
            if (!hers)
            {
                /* (for the log: which missile another weapon fires, the Spear's above all) */
                static unsigned told;
                uint8_t mid[24];
                uint32_t stride = 0, mode = 99;
                uint64_t mres = Descriptor(entity, mid) ? Quad(mid) : 0;
                uintptr_t rec = mres ? hf->component_record("SeekingMissileComponentData", mres, &stride) : 0;
                if (rec) ReadAt(rec, &mode, 4);
                if (told++ < 8) Log("homing grenades: a missile of another weapon: missile %016llx (targeting mode %u), fired by %016llx, target net %u at launch", (unsigned long long)mres, mode,
                    Descriptor(shooter, sid) ? (unsigned long long)Quad(sid) : 0ull, net);
            }   /* another weapon's missile, or one the game gave a lock: left alone */
            if (hers) g_msSeen++;
        }
        if (m->handed < 0) continue;
        if (s_flakRadius > 0 && m->lost < 2)
        {
            float at[3];
            uintptr_t clocks = Pointer(mgr + 0x40);
            if (clocks && ReadAt(state + 0x18, at, 12) && FlakNear(at))
            {
                float old = 1000;
                uint32_t type = 0;
                HfLiveValue v;
                Flak* k = &g_flak[g_flakNext++ % 8];
                memset(k, 0, sizeof *k);
                memset(&v, 0, sizeof v);
                memcpy(k->p, at, sizeof k->p);
                k->at = g_now;
                if (ReadAt(local + 0x34, &type, 4) && type && type < 100000 && hf->get_live(self, (HfRef)type, &kRoundBlast, &v) && v.now.number > 0 && v.now.number < 100000) k->row = (uint32_t)v.now.number;
                hf->write(clocks + 12u * i + 8, &old, 4);
                m->lost = 2;   /* (done with it) */
                g_flakN++;
                static unsigned noted;
                if (noted++ < 8) Log("flak: missile %u is within %.1f m of an enemy: ended there (its round %u, that round's blast row %u)", entity, (double)s_flakRadius, type, k->row);
                continue;
            }
        }
        if (m->lost == 2) continue;
        if (m->target && MissileTargetLives(m->target)) continue;   /* (the game clears +0x00 for the final approach: leave it) */
        if (!m->handed && !m->lost && m->dir[0] == 0 && m->dir[1] == 0 && m->dir[2] == 0)
        {
            /* where she looks as it leaves; else the missile's own start and direction */
            float eye[3];
            if (!CamView(eye, m->dir) || !PlayerPose(m->from, NULL))
                if (!ReadAt(state + 0x18, m->from, 12) || !ReadAt(local + 0x74, m->dir, 12)) continue;
            m->from[2] += 1.0f;
        }
        uint32_t t = MissileTarget(m->from, m->dir), tn = t && t != m->target ? g_netId(t) : NO_NET;
        if (tn != NO_NET)
        {
            uint32_t node = 0x10;
            uint8_t on = 1;
            hf->write(state + 0x00, &tn, 4);
            hf->write(state + 0x04, &node, 4);
            hf->write(state + 0x34, &on, 1);
            m->target = t;
            m->handed++;
            g_msHanded++;
            static unsigned noted;
            if (noted++ < 12) Log("homing grenades: missile %u given enemy %u (net %u)%s", entity, t, tn, m->handed > 1 ? ", a new one" : "");
        }
        else if (!m->lost)
        {
            m->lost = 1;
            g_msLost++;
            static unsigned noted;
            if (noted++ < 6) Log("homing grenades: missile %u found no living enemy within %.0f degrees and %.0f m: it flies straight", entity, (double)s_homingCone, (double)s_homingRange);
        }
    }
}
/* The blasts of the weapons she holds (her grenades go off at once now, often at her feet): kept as her own, so the
 * hit code and the damage event spare her from them */
typedef struct SelfBlast { float p[3]; double at; } SelfBlast;
static SelfBlast g_selfBlast[16];
static unsigned g_selfBlastNext;
static void OnExplosion(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfExplosionEvent* x = (const HfExplosionEvent*)info;
    if (!s_glSafe || !g_inMission || !kb_kit_on || !x->source) return;
    int hers = x->source == g_weapon[1] || x->source == g_weapon[2] || x->source == g_weapon[3];
    for (int k = 0; k < 32 && !hers; k++) hers = g_ms[k].entity == x->source && g_ms[k].handed >= 0;
    if (!hers)
    {
        /* (for the log: a blast close to her that is not known as hers) */
        static unsigned odd;
        float me[3];
        if (odd < 6 && g_nhand && PlayerPose(me, NULL) && Dist2(me, x->position) < 36) { odd++; Log("her hands: a blast 6 m or less from her that is not a held weapon's (explosion row %u, from entity %u, owner %u; her weapons %u / %u / %u)", x->type, x->source, x->owner, g_weapon[1], g_weapon[2], g_weapon[3]); }
        return;
    }
    for (int k = 0; k < 8; k++) if (g_flak[k].at > 0 && Dist2(g_flak[k].p, x->position) < 64) g_flak[k].blasted = 1;
    SelfBlast* b = &g_selfBlast[g_selfBlastNext++ % 16];
    memcpy(b->p, x->position, sizeof b->p);
    b->at = g_now;
    unsigned slot = g_ownNext++ % KB_OWN_SLOTS;
    memcpy(g_ownPos[slot], x->position, sizeof g_ownPos[slot]);
    atomic_store(&g_ownMs[slot], GetTickCount64());
    static unsigned told;
    if (told++ < 4) Log("her hands: a blast of a weapon she holds (explosion row %u): it spares her", x->type);
}
static int SelfBlastAt(const float at[3])
{
    for (int i = 0; i < 16; i++)
        if (g_selfBlast[i].at > 0 && g_now - g_selfBlast[i].at <= 1.0 && Dist2(at, g_selfBlast[i].p) <= 20.0f * 20.0f) return 1;
    return 0;
}
static void FlakFrame(void)
{
    for (int k = 0; k < 8; k++)
    {
        Flak* f = &g_flak[k];
        if (f->at <= 0 || g_now - f->at < 0.2) continue;
        static unsigned noted;
        if (!f->blasted)
        {
            uint32_t row = f->row ? f->row : (uint32_t)(s_rowHe > 0 ? s_rowHe : 175);
            SelfBlast* b = &g_selfBlast[g_selfBlastNext++ % 16];
            memcpy(b->p, f->p, sizeof b->p);
            b->at = g_now;
            unsigned slot = g_ownNext++ % KB_OWN_SLOTS;
            memcpy(g_ownPos[slot], f->p, sizeof g_ownPos[slot]);
            atomic_store(&g_ownMs[slot], GetTickCount64());
            int r = hf->spawn_explosion(self, row, f->p);
            if (r == HF_ACT_WAITING && g_now - f->at < 2.0) continue;   /* (its packages are coming) */
            g_flakOwn++;
            if (noted++ < 8) Log("flak: the ended missile set off nothing: the kit's blast there (row %u, answer %d)", row, r);
        }
        else if (noted++ < 8) Log("flak: the ended missile went off by itself");
        f->at = 0;
    }
}
/* Homing grenades, steered by the kit (0.2.12; homing_missile 0). HellForge's homing rule (0.1.8 to 0.2.0) looks for
 * enemies only while a homing shot flies, so it turned late; this is the same steering from the kit's own enemy list,
 * with the target known as the grenade leaves. The game's projectile pool, as HellForge's actions.c reads it (build
 * 25480438): [game + 0x347CEA8], active +0x28, shots fired +0x30 (slot = that & 0x7FF), a slot's flags u16 +0x203C (bit
 * 1 in flight, bit 0 ended), flight +0x3040 x 0x70 (position +0, velocity +0x0C, distance flown +0x30), shooter entity
 * +0x3B040 x 0x24 + 0x0C, type +0xE5040 x 4. Within flak_radius m of a living enemy it turns straight into it. */
typedef struct Shot { int slot; uint32_t type, target; float from[3], dir[3], flown; double since; int snapped; } Shot;
static Shot g_shot[24];
static int g_nshot;
static uintptr_t g_shotPool;
static uint32_t g_shotCounter;
static unsigned g_shotSeen, g_shotSteered, g_shotSnapped;
static void TurnTo(float v[3], const float to[3], float angle)
{
    float speed = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]), length = sqrtf(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
    if (speed < 1e-3f || length < 1e-3f) return;
    float a[3] = {v[0] / speed, v[1] / speed, v[2] / speed}, b[3] = {to[0] / length, to[1] / length, to[2] / length};
    float cosine = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    cosine = cosine > 1 ? 1 : cosine < -1 ? -1 : cosine;
    float between = acosf(cosine);
    if (between < 0.001f) return;
    float t = angle >= between ? 1.0f : angle / between, sn = sinf(between);
    float wa = sn > 1e-4f ? sinf((1 - t) * between) / sn : 1 - t, wb = sn > 1e-4f ? sinf(t * between) / sn : t;
    for (int k = 0; k < 3; k++) v[k] = (wa * a[k] + wb * b[k]) * speed;
}
/* the living enemy nearest `at` within flak_radius: where to hit it */
static int ShotNear(const float at[3], float aim[3])
{
    float best = s_flakRadius * s_flakRadius;
    int found = 0;
    for (int i = 0; i < FOES; i++)
    {
        float p[3];
        if (!g_foe[i] || !FoePos(i, p)) continue;
        p[2] += 1.0f;
        float d2 = Dist2(p, at);
        if (d2 > best || !Living(g_foe[i])) continue;
        best = d2;
        memcpy(aim, p, 12);
        found = 1;
    }
    return found;
}
static int ShotAim(uint32_t target, float aim[3])
{
    for (int i = 0; i < FOES; i++) if (g_foe[i] == target) { if (!FoePos(i, aim) || !Living(target)) return 0; aim[2] += 1.0f; return 1; }
    return 0;
}
static void ShotsFrame(float dt)
{
    if (!s_glHoming || s_homingMissile || !game) { g_nshot = 0; g_shotPool = 0; return; }
    uintptr_t pool = Pointer(game + 0x347CEA8);
    uint8_t active = 0;
    uint32_t counter = 0;
    if (!pool || !ReadAt(pool + 0x28, &active, 1) || active != 1 || !ReadAt(pool + 0x30, &counter, 4)) return;
    if (g_shotPool != pool) { g_shotPool = pool; g_shotCounter = counter; g_nshot = 0; return; }
    uint32_t n = counter - g_shotCounter, from = g_shotCounter;
    g_shotCounter = counter;
    if (n > 64 && n <= 2048) { from += n - 64; n = 64; }
    /* her new grenades */
    for (uint32_t i = 0; i < n && n <= 64; i++)
    {
        int slot = (int)((from + i) & 0x7FF), hers = 0;
        uint32_t type = 0, shooter = 0;
        if (!ReadAt(pool + 0xE5040 + (uintptr_t)slot * 4, &type, 4) || !type) continue;
        for (int k = 0; k < g_nhand; k++) hers |= g_hand[k].armed && g_hand[k].projectile == type;
        if (!hers || !ReadAt(pool + 0x3B040 + (uintptr_t)slot * 0x24 + 0x0C, &shooter, 4)) continue;
        if (shooter != g_weapon[1] && shooter != g_weapon[2] && shooter != g_weapon[3]) continue;
        if (g_nshot >= 24) break;
        Shot* t = &g_shot[g_nshot++];
        memset(t, 0, sizeof *t);
        t->slot = slot;
        t->type = type;
        t->since = g_now;
        float eye[3], v[6];
        if (!CamView(eye, t->dir) || !PlayerPose(t->from, NULL))
        {
            if (!ReadAt(pool + 0x3040 + (uintptr_t)slot * 0x70, v, sizeof v)) { g_nshot--; continue; }
            memcpy(t->from, v, 12);
            memcpy(t->dir, v + 3, 12);
        }
        t->from[2] += 1.0f;
        t->target = MissileTarget(t->from, t->dir);
        g_shotSeen++;
        static unsigned noted;
        if (noted++ < 10) Log("homing grenades: a grenade of hers (round %u, slot %d): %s %u", type, slot, t->target ? "it homes on enemy" : "no living enemy in the cone, it flies as fired;", t->target);
    }
    for (int i = 0; i < g_nshot;)
    {
        Shot* t = &g_shot[i];
        uintptr_t flight = pool + 0x3040 + (uintptr_t)t->slot * 0x70;
        uint16_t flags = 0;
        uint32_t type = 0;
        float f[14];
        if (g_now - t->since > 12 || !ReadAt(pool + 0xE5040 + (uintptr_t)t->slot * 4, &type, 4) || type != t->type || !ReadAt(pool + 0x203C + (uintptr_t)t->slot * 2, &flags, 2) ||
            !(flags & 2) || (flags & 1) || !ReadAt(flight, f, sizeof f) || f[12] < t->flown)
        { *t = g_shot[--g_nshot]; continue; }
        t->flown = f[12];
        i++;
        float* v = f + 3;
        float aim[3];
        if (sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) < 1.0f) continue;
        if (s_flakRadius > 0 && f[12] >= s_homingArm && ShotNear(f, aim))
        {
            float to[3] = {aim[0] - f[0], aim[1] - f[1], aim[2] - f[2]};
            TurnTo(v, to, 3.2f);
            hf->write(flight + 0x0C, v, 12);
            if (!t->snapped) { t->snapped = 1; g_shotSnapped++; static unsigned noted; if (noted++ < 8) Log("homing grenades: a grenade %.1f m from an enemy turns straight into it (%.0f m flown)", (double)sqrtf(Dist2(aim, f)), (double)f[12]); }
            continue;
        }
        if (f[12] < s_homingArm) continue;
        if (!t->target || !ShotAim(t->target, aim))
        {
            t->target = MissileTarget(t->from, t->dir);
            if (!t->target || !ShotAim(t->target, aim)) continue;
        }
        float to[3] = {aim[0] - f[0], aim[1] - f[1], aim[2] - f[2]};
        TurnTo(v, to, s_homingTurn * 3.14159265f / 180 * (dt > 0 && dt < 0.25f ? dt : 1.0f / 60));
        hf->write(flight + 0x0C, v, 12);
        g_shotSteered++;
    }
}
static float g_handsDt;
static void HandsFrame(void)
{
    MissilesFrame();
    FlakFrame();
    ShotsFrame(g_handsDt);
    for (int sel = 1; sel <= 3; sel++)
    {
        uint64_t res = g_weaponRes[sel];
        int known = !res || res == AVATAR;
        for (int i = 0; i < g_nhand && !known; i++) known = g_hand[i].res == res;
        if (!known) HandSet(res);
    }
}
static void HandsOff(void)
{
    for (int i = 0; i < g_nhand; i++)
    {
        Hand* h = &g_hand[i];
        if (h->armed) hf->set_live(self, (HfRef)h->projectile, &kArming, NULL, 0);
    }
    MissileSwap(0);
    if (g_shotSeen) Log("homing grenades: %u grenades of hers seen, %u steering steps, %u turned straight into an enemy near them", g_shotSeen, g_shotSteered, g_shotSnapped);
    g_shotSeen = g_shotSteered = g_shotSnapped = 0;
    g_nshot = 0;
    memset(g_selfBlast, 0, sizeof g_selfBlast);
    if (g_nhand) Log("her hands: %d weapon(s) put back as the game has them", g_nhand);
    g_nhand = 0;
    OneHanded(0);
}

static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfKillEvent* k = (const HfKillEvent*)info;
    DropFoe(k->victim);
    if (k->victim == g_lastTarget) g_lastTarget = 0;
}
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    if (s->resource != AVATAR)
    {
        if (ShieldKind(s->resource)) { KbHers(s->entity); Log("a shield spawned (entity %u, %016llx): her blasts spare it", s->entity, (unsigned long long)s->resource); return; }
        MechSeen(s->resource);
        if (FoeKind(s->resource)) AddFoe(s->entity, s->resource);
        return;
    }
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
    else { g_alive = 0; g_leap = 0; if (g_chain || g_chainWait >= 0) EndChain("she went down"); }
}

static void Reset(const char* why)
{
    int start = !strcmp(why, "mission start");
    if (start) KitDecide();   /* with several kits installed: is this one picked? */
    if (g_casts[0] || g_casts[1] || g_casts[2] || g_casts[3])
        Log("%s: casts %u / %u / %u / %u, %u enemies paid by her blasts, %u hits of theirs cut (out of radius or paid already), %u on friends and %u on her spared, %u Denied Exit, %u knockdowns stopped",
            why, g_casts[0], g_casts[1], g_casts[2], g_casts[3], g_paidHits, g_cutHits, g_friendSpared, g_selfSpared, g_denied, g_knockdowns);
    Speed(0);
    HandsOff();
    if (start && kb_kit_on && s_oneHand) OneHanded(1);
    if (start && kb_kit_on) MissileSwap(1);
    Body(start);
    if (start && s_hellbomb && s_hellbombRow == 125)
        if (!hf->load_package(self, UINT64_C(0x346D785D6CA13FF4))) Log("the Hellbomb's package could not be loaded ahead");  /* loadout/hellbomb_backpack */
    g_boomPending = 0;
    g_boomArmed = 0;
    memset(g_hunt, 0, sizeof g_hunt);
    memset(g_blast, 0, sizeof g_blast);
    memset(g_group, 0, sizeof g_group);
    memset(g_casts, 0, sizeof g_casts);
    g_nq = 0;
    g_volley.group = -1;
    g_selfSpared = g_knockdowns = g_paidHits = g_cutHits = g_friendSpared = g_denied = 0;
    g_grenades = s_grenadesMax;
    g_fang = 0;
    g_offense = g_ammoLoaded = 0;
    g_offenseUntil = 0;
    g_offenseSpent = 0;
    g_frenzy = s_frenzyStart > s_frenzyMax ? s_frenzyMax : s_frenzyStart;
    memset(g_cd, 0, sizeof g_cd);
    g_chain = 0;
    g_chainWait = -1;
    g_leap = 0;
    g_healOwed = 0;
    g_healFull = 0;
    g_mechMission = 0;
    g_drUntil = g_speedUntil = g_deniedUntil = g_deniedReady = 0;
    g_lastTarget = 0;
    atomic_store(&g_shieldUntilMs, 0);
    if (start) Log("mission start: Grenades %d, Frenzy %d, Sangvis Ferri %s", g_grenades, g_frenzy, s_mech == 2 ? "on (every mission)" : s_mech ? "when an Automaton kind shows" : "off");
}

static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gameSize = 0, exeSize = 0;
    uintptr_t g = hf->module_base("game.dll", &gameSize), x = hf->module_base("helldivers2.exe", &exeSize);
    if (!g || !x || gameSize != 74727424u || exeSize != 60719104u) { Log("REFUSED: the game's modules are not build " BUILD "'s: no leaps, no knockdown window"); return; }
    game = g;
    exe = x;
    if (!HitsInstall("knockdown window")) return;
    if (hf->code_matches(game + 0x5A2BB0, "48 89 5c 24 08 48 89 6c 24 10 56 57 41 54 41 56 41 57 48 83 ec 40") &&
        hf->code_matches(game + 0x4A7550, "48 89 5c 24 18 57 48 83 ec 20 3b 15 ?? ?? ?? ?? 48 8b 1d") &&
        hf->code_matches(exe + 0x79F860, "33 c0 4c 8d 05 c7 b2 01 02 0f 1f 80 00 00 00 00 49 39 0c c0 74 0c ff c0 83 f8 04 75 f3") &&
        hf->code_matches(exe + 0x7F4590, "48 8b c4 f3 0f 11 58 20 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48"))
    {
        g_setPosition = (SetVecFn)(game + 0x5A2BB0);
        KbFitInit();
        g_setVelocity = (SetVecFn)(game + 0x4A7550);
        g_worldId = (WorldFn)(exe + 0x79F860);
        g_raycast = (RayFn)(exe + 0x7F4590);
        Log("leaps ready (the warp's position setter, the velocity setter and the raycast)");
    }
    else Log("leaps off: their functions are not the reviewed code");
    g_staminaOk = hf->code_matches(game + 0xA50E2D, "f3 42 0f 11 8c 11 54 78 54 00");
    MissileInit();
    Log(g_staminaOk ? "stamina readable (the game's stamina write is the reviewed code)" : "stamina off: its code is not the reviewed one");
}

/* the tracers' damage rows: the stage's stagger and push, nothing of their own to deal but a token */
static void ProjectilesReady(HfMod* mod, HfEvent event, void* user)
{
    for (int k = 0; k < 2; k++)
    {
        HfDamageInfo* dmg = g_tracerDamage[k] ? (HfDamageInfo*)hf->damage_row(g_tracerDamage[k]) : NULL;
        if (!dmg || !g_tracer[k]) { g_tracer[k] = 0; continue; }
        float want = k ? s_s1Stagger2 : s_s1Stagger1;
        Log("the %s's round: projectile %u, damage row %u: stagger %u -> %.0f, push %u -> %.0f", k ? "cone" : "line", g_tracer[k], g_tracerDamage[k],
            (unsigned)dmg->stagger, (double)want, (unsigned)dmg->push_force, (double)want);
        dmg->stagger = (__typeof__(dmg->stagger))want;
        dmg->push_force = (__typeof__(dmg->push_force))want;
    }
    g_tracerReady = 1;
}

/* ---- the HUD state (read by the Destroyer HUD addon through LuaJIT's ffi) ------------------------------------------- */
typedef struct DestroyerHud
{
    uint32_t version, in_mission;
    float cd[4], cd_max[4];              /* seconds left, full cooldowns: Annihilation Directive, Doom Barrage, Party Time, Ammo Frenzy */
    uint32_t grenades, grenades_max, fang, frenzy;
    uint32_t stage;                      /* Annihilation Directive: 0 the leap, 1 the line is open, 2 the cone is open */
    float stage_left;                    /* seconds the open stage stays */
    float denied_cd, denied;             /* Denied Exit: seconds until it can again; seconds of its protection left */
    float rage;                          /* her skill damage for lost health, as a factor (1: none) */
    uint32_t mech, hunted;
    uint32_t keys[4];
    char key_label[4][24];
    float stamina;                       /* 0..1 (-1: unknown) */
    uint32_t hellbomb;
    float haste;
    uint32_t offense, ammo_loaded;       /* the stacks of Offense Only; Ammo Loaded is waiting for her next skill */
} DestroyerHud;
static DestroyerHud g_hud = {1};
HF_MOD_EXPORT DestroyerHud* destroyer_hud_state(void)
{
    g_hud.in_mission = (uint32_t)(g_inMission && g_alive && kb_kit_on);
    float m[4] = {s_s1Cd, s_s2Cd, s_s3Cd, 2};
    for (int i = 0; i < 4; i++) { g_hud.cd[i] = (float)fmax(0, g_cd[i] - g_now); g_hud.cd_max[i] = m[i]; }
    g_hud.grenades = (uint32_t)g_grenades;
    g_hud.grenades_max = (uint32_t)s_grenadesMax;
    g_hud.fang = (uint32_t)g_fang;
    g_hud.frenzy = (uint32_t)g_frenzy;
    g_hud.stage = (uint32_t)(g_chainWait >= 0 ? 0 : g_chain > 2 ? 0 : g_chain);
    g_hud.stage_left = g_chain && g_chainWait < 0 ? (float)fmax(0, g_chainUntil - g_now) : 0;
    g_hud.denied_cd = (float)fmax(0, g_deniedReady - g_now);
    g_hud.denied = (float)fmax(0, g_deniedUntil - g_now);
    g_hud.rage = g_inMission && g_alive ? (float)(1 + (double)s_rage * Missing()) : 1;
    g_hud.mech = (uint32_t)Mech();
    unsigned n = 0;
    for (int i = 0; i < 96; i++) n += g_hunt[i].until > g_now;
    g_hud.hunted = n;
    for (int i = 0; i < 4; i++)
    {
        const Key* k = &s_keys[i];
        g_hud.keys[i] = (uint32_t)k->vk;
        memcpy(g_hud.key_label[i], k->label, sizeof g_hud.key_label[i]);
    }
    g_hud.stamina = g_inMission && g_alive ? StaminaNow() : -1;
    g_hud.hellbomb = (uint32_t)(s_hellbomb != 0);
    g_hud.haste = (float)fmax(0, g_speedUntil - g_now);
    g_hud.offense = (uint32_t)g_offense;
    g_hud.ammo_loaded = (uint32_t)g_ammoLoaded;
    return &g_hud;
}

/* ---- in-game key bindings: the HUD registers her three skills with Mod Bindings Menu and passes each press here (and
 * a heartbeat twice a second); the .ini's keys keep working. Ammo Frenzy's key is the .ini's and the options screen's. */
static _Atomic unsigned long long g_bindMs;
static atomic_int g_bindPress;
HF_MOD_EXPORT int destroyer_input(unsigned version, int skill)
{
    if (version != 1) return 0;
    atomic_store(&g_bindMs, GetTickCount64());
    if (skill >= 1 && skill <= 4) atomic_fetch_or(&g_bindPress, 1 << (skill - 1));
    return 1;
}
HF_MOD_EXPORT int destroyer_key(unsigned version, int skill, int* vk, int* mods)
{
    if (version != 1 || skill < 1 || skill > 4 || !vk || !mods) return 0;
    const Key* k = &s_keys[skill - 1];
    *vk = k->vk;
    *mods = k->mods;
    return 1;
}
/* ---- the frame ------------------------------------------------------------------------------------------------------ */
static void Tick(HfMod* mod, float dt, void* user)
{
    KbOptionsTick();   /* the options screen's changes, in a mission or not */
    g_now += dt > 0 && dt < 1 ? dt : 0;
    int inMission = hf->game_state(self) == HF_STATE_MISSION;
    MissilePackage();
    if (inMission != g_inMission) { Reset(inMission ? "mission start" : "mission end"); g_inMission = inMission; }
    if (!g_inMission || !kb_kit_on) return;   /* (standing by: another kit is picked) */
    MyItems();
    g_handsDt = dt;
    MissileSwap(1);   /* (the package came in after the mission began) */
    HandsFrame();
    static int prev[4];
    int focused = Focused();
    int mods = focused ? ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? KM_SHIFT : 0) | ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? KM_CTRL : 0) |
                         ((GetAsyncKeyState(VK_MENU) & 0x8000) ? KM_ALT : 0) : 0;
    /* the keys from Mod Bindings Menu while the HUD passes them, else the .ini's */
    int bound = GetTickCount64() - atomic_load(&g_bindMs) < 2000, presses = atomic_exchange(&g_bindPress, 0);
    static int wasBound = -1;
    if (bound != wasBound) { wasBound = bound; Log("keys: %s", bound ? "the .ini's and Mod Bindings Menu's (the binding pages' MODS tab)" : "the .ini's"); }
    for (int i = 0; i < 4; i++)
    {
        const Key* key = &s_keys[i];
        /* a modifier bound alone ignores its own bit */
        int own = key->vk == VK_SHIFT ? KM_SHIFT : key->vk == VK_CONTROL ? KM_CTRL : key->vk == VK_MENU ? KM_ALT : 0;
        int down = focused && (GetAsyncKeyState(key->vk) & 0x8000) != 0 && (mods & ~own) == key->mods;
        static unsigned long long fired[4];
        int press = (down && !prev[i]) || (presses >> i & 1);
        if (press && GetTickCount64() - fired[i] > 250 && g_alive && atomic_load(&g_avatar) && (fired[i] = GetTickCount64(), 1))
        {
            if (i == 0) Annihilation();
            else if (i == 1) DoomBarrage();
            else if (i == 2) PartyTime();
            else AmmoFrenzy();
        }
        prev[i] = down;
    }
    LeapFrame();
    BlastFrame();
    VolleyFrame();
    ChainFrame();
    Exhaustion();
    /* Party Time's heals: what its hits owe, never past her maximum */
    if ((g_healOwed > 0 || g_healFull) && g_alive)
    {
        int32_t hp, max;
        if (Hp(&hp, &max) && hp < max)
        {
            double want = g_healFull ? max : g_healOwed * max;
            if (want > max - hp) want = max - hp;
            if (want >= 1 && Act("Party Time's heal", hf->heal(self, (float)want)))
            { static unsigned told; if (told++ < 12) Log("Party Time: healed %.0f (health %d of %d before)", want, hp, max); }
        }
        g_healOwed = 0;
        g_healFull = 0;
    }
    if (g_speedOn && g_now >= g_speedUntil) Speed(0);
    unsigned own = atomic_exchange(&g_ownStopped, 0);
    if (own) { g_selfSpared += own; static unsigned noted; if (noted++ < 5) Log("%u hit(s) of her own blasts cleared on her (the hit code)", own); }
    unsigned kd = atomic_exchange(&g_kdStopped, 0);
    if (kd) { g_knockdowns += kd; static unsigned noted; if (noted++ < 10) Log("%u knockdown(s) stopped in a skill's window", kd); }
}

/* ---- the Mod Options Menu (through the Destroyer HUD addon): read and set a setting by its destroyer.ini name -------- */
static const KbOption kOptions[] = {
    {"s1_cost", &s_s1Cost, NULL}, {"s2_cost", &s_s2Cost, NULL}, {"cost_floor", &s_costFloor, NULL}, {"rage", &s_rage, NULL},
    {"s1_cooldown", &s_s1Cd, NULL}, {"s1_leap", &s_s1Leap, NULL}, {"s1_damage", &s_s1Damage, NULL}, {"s1_damage_1", &s_s1Damage1, NULL}, {"s1_damage_2", &s_s1Damage2, NULL},
    {"s2_cooldown", &s_s2Cd, NULL}, {"s2_damage", &s_s2Damage, NULL}, {"s3_cooldown", &s_s3Cd, NULL}, {"s3_leap", &s_s3Leap, NULL}, {"s3_damage", &s_s3Damage, NULL},
    {"s3_heal", &s_s3Heal, NULL}, {"frenzy_damage", &s_frenzyDamage, NULL}, {"frenzy_radius", &s_frenzyRadius, NULL}, {"hunting_seconds", &s_huntingSeconds, NULL},
    {"denied_cooldown", &s_deniedCd, NULL}, {"offense_max", NULL, &s_offenseMax}, {"mech", NULL, &s_mech},
    {"gl_arming", NULL, &s_glArming}, {"gl_homing", NULL, &s_glHoming}, {"one_handed", NULL, &s_oneHand},
    {"hp_mult", &s_hpMult, NULL}, {"stamina_mult", &s_staminaMult, NULL}, {"hellbomb_on_exhaustion", NULL, &s_hellbomb},
};
static void OptionAfter(const char* name, int changed)
{
    if (!changed) return;
    if (g_grenades > s_grenadesMax) g_grenades = s_grenadesMax;
    if (g_frenzy > s_frenzyMax) g_frenzy = s_frenzyMax;
}
HF_MOD_EXPORT int destroyer_option_get(const char* name, double* out) { return OptionGet(name, out); }
HF_MOD_EXPORT int destroyer_option_set(const char* name, double value) { return OptionSet(name, value); }

#include "kit_options.h"   /* kOpRows: the options screen's rows (kitbase/kb_options.h) */
#include "kit_icon.h"      /* kOpIcon, KB_OP_ACCENT */
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    RosterJoin("destroyer", "Destroyer");
    kb_ini = "destroyer.ini";
    kb_key_names[0] = "key_s1";
    kb_key_names[1] = "key_s2";
    kb_key_names[2] = "key_s3";
    kb_key_names[3] = "key_frenzy";
    s_keys[3] = (Key){'B', 0, "B"};
    KB_SETTINGS(kSettings);
    KB_OPTIONS(kOptions);
    kb_option_after = OptionAfter;
    kb_damage_taken = DamageTaken;
    kb_damage_dealt = DamageDealt;
    if (!api || api->version < 13 || api->size < offsetof(HfApi, code_matches) + sizeof api->code_matches) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    LoadSettings();
    KbOptionsStart(api, mod, "destroyer", "Destroyer", VERSION, kOpRows, (int)(sizeof kOpRows / sizeof kOpRows[0]), kOpIcon, KB_OP_ICON, KB_OP_ACCENT, -1);
    api->log(mod, "Destroyer %s: keys %s / %s / %s / %s (Annihilation Directive, Doom Barrage, Party Time, Ammo Frenzy); skills cost %.0f%% and %.0f%% of her health, never below %.0f%%",
        VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, s_keys[3].label, (double)s_s1Cost * 100, (double)s_s2Cost * 100, (double)s_costFloor * 100);
    if (s_badKey) api->log(mod, "destroyer.ini: a key could not be read (bits %d: s1, s2, s3, frenzy); its default stays", s_badKey);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    if (api->size >= offsetof(HfApi, projectile_row) + sizeof api->projectile_row)
    {
        static const char* const dn[2] = {"yui/destroyer/line_damage", "yui/destroyer/cone_damage"};
        static const char* const pn[2] = {"yui/destroyer/line_round", "yui/destroyer/cone_round"};
        for (int k = 0; k < 2; k++)
        {
            HfDamage dm = {sizeof dm};
            dm.base = (uint32_t)s_tracerDamage;
            dm.name = dn[k];
            dm.scale = 1;
            g_tracerDamage[k] = api->add_damage(mod, &dm);
            HfProjectile pr = {sizeof pr};
            pr.base = (uint32_t)s_tracerRound;
            pr.name = pn[k];
            pr.damage = g_tracerDamage[k];
            g_tracer[k] = g_tracerDamage[k] ? api->add_projectile(mod, &pr) : 0;
        }
        if (!g_tracer[0] || !g_tracer[1] || api->on(mod, HF_EVENT_PROJECTILES_READY, ProjectilesReady, NULL))
        { g_tracer[0] = g_tracer[1] = 0; api->log(mod, "the stages' rounds could not be added: Annihilation Directive is the leap alone"); }
    }
    else api->log(mod, "this HellForge has no projectile rows: Annihilation Directive is the leap alone");
    /* registered: from here the DLL stays loaded; a failure below leaves it inert */
    int ok = api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_EXPLOSION, OnExplosion, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) &&
             api->on_game_thread(mod, Tick, NULL);
    if (!ok) api->log(mod, "events or the game thread are unavailable: Destroyer stays off");
    return 0;
}
