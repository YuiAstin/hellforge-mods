/* Koleda (yui.koleda): Koleda's kit from Girls' Frontline 2 (AK-12, DEFY) as abilities on keys, for your own Helldiver
 * (solo or host): a Vanguard whose ultimate calls the Sinveiler, an empty exosuit of her own that follows her, fights for
 * her and takes half the damage aimed at her. Built on the kit base (kitbase/, docs/kit-base.md); the design and what
 * is still untested: docs/koleda-design.md. NOTHING HERE HAS RUN IN GAME YET.
 *
 * 0.3.0: the kit as her official skills read (Dandegate, 2026-11-16; docs/koleda-design.md), turns made seconds:
 *   The Sinner (her passive) and Sinveiler (the summon): it takes share_pct (80) of each hit on her,
 *     follows her and shoots by its gear; each of its actions gives her Upshift (one more in S+). While both stand:
 *     Post-Move Support (her damage and its, x post_move).
 *   Upshift: +upshift_damage (15%) of her damage a stack, at most 5; at 5 she takes upshift_guard (30%) less damage.
 *   Snow Wolf's Eye (key_s1, Z): +1 Upshift first; heavy rounds on the enemy under her crosshair, each stack of Upshift
 *     s1_stack more; Turbo Boost on her and the Sinveiler (damage x turbo_mult for turbo_seconds); the Sinveiler to S+.
 *   Tactical Routebook (key_s2, X): +2 Upshift; Kinetic Recovery on both (damage x kr_mult, no heal); one Kinetic
 *     Release around her; the Sinveiler to S.
 *   Spectre (key_s3, C): one Sinveiler call (the Patriot copy, or the M-103 from her loadout); while it stands: The Winner
 *     (every winner_cooldown s: full health, cleansed; gear N if it was in D); in the FRV, Let's Go For a Spin! first.
 *   Gears (Gear Shift): D by default (its damage x gear_damage_d); N, S, S+ set by her skills for gear_seconds, then D
 *     again. N: it takes no damage and holds Drift and Full Speed. S can only go up to S+, S+ is held. Parked (standing
 *     still): it takes park_taken of the damage. Always: it takes sinner_taken of the damage and heals sinner_heal of
 *     its health when hit (at most every sinner_heal_every s).
 *   Kinetic Release (Gear Shift): while it fights in S or S+, every release_every s (and after Tactical Routebook),
 *     rifle rounds from it at the enemies near it (S: release_targets_s within release_reach, S+: more, farther).
 *   In the FRV Sinveiler (0.3.5, Voymastina's way): her keys are its skills: key_s1 Let's Go Full Speed! (the burst, at the
 *     wheel), key_s2 Let's Go for a Drift!, key_s3 The Winner.
 *   Let's Go for a Drift! (in it, key_s2; drift_cooldown; not in N): waves of EMS bursts out from the Sinveiler,
 *     drift_rings rings out to drift_radius m, each enemy in reach taking drift_damage once (the stun holds them; nobody
 *     is moved).
 *   Let's Go Full Speed! (not in N): while it moves at full_min_speed m/s or more it fires at the enemies in a cone
 *     ahead, more often the faster it goes, and its rams hit harder with speed. Z while she drives: the Full Speed
 *     burst (sprint_seconds without damage, harder rams).
 *
 * The Sinveiler's driving goes through sinner_drive.h (the game's own locomotion, as Exo Companion by M!ni$try0fSc!eиce
 * drives a suit); until the probe fills it in, the Sinveiler slides on Voymastina's warp setter, where its guard allows.
 *
 * Research credit: the kit base's (Helldiver Survival Rework by enochlophobianumerophobia, MIT; HD2Runtime by
 * SkyeShade); the camera pose as Apache - Rise Above (RSNLRiggs, after Khi) reads it; the warp setter as the LIFT-182
 * Warp Pack calls it and the stratagem grant (HD2Runtime's research) from Voymastina; the seat read (Khi). See
 * THIRD_PARTY_NOTICES.md. */
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <windows.h>
#include "hellforge_mod.h"
#include "hellforge_components.h"
#include "hellforge_content.h"

#define VERSION "0.4.4"
#define BUILD "25480438"

#define KB_OWN_SLOTS 48   /* 0.3.0: Kinetic Release's rings and the Drift's rockets */
#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"
#include "kitbase/kb_body.h"
#include "kitbase/kb_hits.h"
#include "kitbase/kb_damage.h"
#include "kitbase/kb_settings.h"
#include "kitbase/kb_options.h"
#include "kitbase/kb_roster.h"
#include "weapon_sounds.h"

/* with other character kits installed: the HUD's roster and picker calls (kitbase/kb_roster.h) */
KB_ROSTER_EXPORTS(koleda)
KB_OPTIONS_EXPORTS(koleda)

/* ---- settings ------------------------------------------------------------------------------------------------- */
/* Upshift */
static int s_upMax = 5;
static float s_upDecay = 10, s_upDamage = 0.15f, s_upEvery = 3, s_upGuard = 0.3f;
/* the Sinveiler: its call, the damage share */
static int s_sinOn = 1, s_callMode = 1;
static float s_sharePct = 80, s_shareRange = 60;
/* the leash */
/* 0.1.13: it keeps follow_distance m from her (beside and behind), never closer than personal_space m (it walked into
 * her and crushed her); leash_start / leash_stop / guard_start: how far from that point it starts and stops moving */
static float s_followDist = 10, s_personal = 6;
static float s_leashStart = 4, s_leashStop = 2, s_guardStart = 4, s_leashWarp = 40, s_stuckSeconds = 1.5f, s_slideSpeed = 6;
static float s_pushStep = 1.5f;
static int s_pushSteps = 10;
static float s_plusDistance = 8, s_plusLeash = 25;
static int s_warpWalks = 1, s_navPath = 1;
/* its fire: rounds by gear (copies of vanilla rounds, add_projectile): N a medium-penetration round at whatever attacks
 * her, S a quick light round, S+ a heavy one, faster */
static int s_sinFire = 1, s_sinSight = 1;
static int s_roundN = 148, s_roundS = 148, s_roundPlus = 148, s_roundRocket = 128, s_roundFallback = 7;   /* 0.1.3: its own arms: the Patriot's minigun (148) and rockets (128) */
static float s_gunEvery = 0, s_plusRate = 0.6f, s_guardRange = 20, s_fireRange = 40;
/* the old pull (Patriot only), off by default */
static int s_pullOn = 0, s_pullExplosion = 0;
static float s_pullEvery = 12, s_pullRadius = 8;
/* Snow Wolf's Eye, Turbo Boost */
static float s_s1Cd = 12, s_s1Every = 0.15f, s_s1Cone = 10, s_s1Range = 200, s_s1Stack = 0.15f, s_turboSeconds = 6, s_turboMult = 1.45f;
static int s_s1Rounds = 2, s_s1Round = 226;
/* Tactical Routebook */
static float s_s2Cd = 20, s_krSeconds = 10, s_krMult = 1.25f, s_krHeal = 0, s_krSinnerHeal = 0;
/* Spectre / The Winner */
static float s_s3Cd = 90, s_winnerCd = 30;
/* 0.3.0: gears, Gear Shift, Post-Move Support, Kinetic Release's waves, Drift and Full Speed */
static float s_gearSeconds = 15, s_gearD = 1.3f, s_gearS = 1.3f, s_gearPlus = 1.6f, s_gearN = 1.0f, s_postMove = 1.3f;
static float s_sinTaken = 0.65f, s_parkTaken = 0.7f, s_sinHeal = 0.25f, s_sinHealEvery = 8;
/* 0.3.2: Kinetic Release: rifle rounds from it at the enemies near it; Drift: EMS waves; Full Speed: its cone of fire
 * ahead while it moves fast, and harder rams */
static int s_relRound = 0, s_relRoundsS = 3, s_relRoundsPlus = 4, s_relPoints = 16, s_fullShots = 3;
/* 0.3.6: their rounds land as the Orbital Gatling Barrage's HE shells (its blast row, 266, where each round lands) */
static int s_heRow = 266, s_fullHe = 1, s_releaseHe = 1;
static int s_rideSafe = 1, s_relRingS = 8, s_relRingPlus = 12;   /* 0.3.7 */
static unsigned g_rideSaved;
static int s_sinSteady = 1;   /* 0.3.8: no stagger or push on the Sinveiler */
static float s_heRate = 10;
/* 0.3.12: Full Speed's tracers: a weak round with a bright trail, sprayed ahead while it drives fast */
static int s_fullTracers = 1, s_tracerDamage = 125;
static float s_tracerEvery = 0.08f;
static int s_heRound = 77;   /* 0.3.11: the Orbital Gatling Barrage's own shell, fired from the Sinveiler (0: a bullet and a blast set off where it lands) */
static float s_relReachPlus = 18, s_relInner = 6, s_relGap = 0.3f;
static int s_driftOn = 1, s_driftRings = 4, s_driftDamage = 800, s_fullOn = 1;
static float s_driftEvery = 12, s_driftRadius = 20;
static float s_fullMinSpeed = 5, s_fullRefSpeed = 15, s_fullEvery = 0.15f, s_fullCone = 35, s_fullRange = 30, s_fullRam = 0.06f, s_fullRamMax = 3;
static int s_cleanse = 1;
/* driving (sinner_drive.h): the probe's numbers; -1 not known */
static int s_drive = 1, s_driveTest = 0;
static int s_turnOn = 0;
/* 0.1.8: its minigun rounds' speed (her copies only): at the base's ~1000 m/s a round's tracer is first drawn a frame
 * along its path, metres out from the arm (it looked spawned far from the Sinveiler); 0 keeps the base's */
static float s_roundSpeed = 250;
static float s_deadAfter = 5;   /* 0.2.10: a target its fire leaves its health alone this long: dead to it for 15 s */
static float s_maxRounds = 20;   /* 0.1.7: off (the HUD side walks and turns it through its animation, Exo Autopilot's way) */
static float g_walkGoal[3];
static int g_walkWant;
static double g_walkAckAt = -100;
static float g_moveDir[2];
static int s_locoManager = -1, s_locoMap = -1, s_locoRecords = -1, s_locoStride = 164;
static int s_stickFwd = -1, s_stickSide = -1, s_stickTurn = -1;
/* aiming */
static int s_sight = 1;
static float s_fpDistance = 0.45f;
/* 0.2.0: the FRV Sinveiler (her M-103 Supply FRV) */
static int s_frvOn = 1, s_releaseRow = 150;   /* 0.2.6: sinner_kind: 0 the Patriot, 1 the FRV (her own FRV Sinveiler call; the default) */
   /* 0.2.5: an EMS burst on each enemy near it (0.2.1-0.2.4: blasts in a ring, which missed) */
static int s_releaseShell = 154, s_releaseTargetsS = 4, s_releaseTargetsPlus = 6, s_emsImmune = 1;
static float s_releaseAp = 6, s_releaseReach = 12;
static float s_releaseEvery = 5, s_releaseRadius = 7,   /* 0.2.4: 7 m (4 m blasts flipped the FRV) */
    s_rideRange = 60;
static int s_grip = 0;   /* (0.3.9: off by default: it never ran before the physics pins were fixed) */
/* 0.3.9: the FRV Sinveiler kept steady: a hit's spin and launch taken off its physics body; set upright when it lies over */
static int s_carSteady = 1, s_carRight = 1;
static float s_carSpin = 1.5f, s_carLift = 2.0f, s_carWindow = 0.6f, s_carRightAfter = 1.5f;
static float s_gripGravity = 12, s_gripCancel = 0.5f, s_gripMargin = 0.35f, s_gripMass = 3000;
static float s_sprintSeconds = 6, s_sprintCd = 20, s_sprintRam = 3, s_sprintRamS = 1.5f, s_sprintRamPlus = 2;

static const KbSetting kSettings[] = {
    KB_I("upshift_max", s_upMax, 1, 20), KB_F("upshift_decay", s_upDecay, 1, 600), KB_F("upshift_damage", s_upDamage, 0, 1), KB_F("upshift_every", s_upEvery, 0, 60), KB_F("upshift_guard", s_upGuard, 0, 1),
    KB_I("sinner", s_sinOn, 0, 1), KB_I("call_mode", s_callMode, 0, 1), KB_F("share_pct", s_sharePct, 0, 100), KB_F("share_range", s_shareRange, 1, 1000),
    KB_F("leash_start", s_leashStart, 2, 100), KB_F("leash_stop", s_leashStop, 1, 100), KB_F("guard_start", s_guardStart, 2, 100),
    KB_F("leash_warp", s_leashWarp, 10, 500), KB_F("stuck_seconds", s_stuckSeconds, 0.5, 60), KB_F("push_step", s_pushStep, 0.5, 5), KB_I("push_steps", s_pushSteps, 0, 50), KB_F("slide_speed", s_slideSpeed, 0.5, 20),
    KB_F("plus_distance", s_plusDistance, 1, 100), KB_F("plus_leash", s_plusLeash, 5, 200), KB_I("warp_needs_walks", s_warpWalks, 0, 1), KB_I("nav_path", s_navPath, 0, 1),
    KB_I("sinner_fire", s_sinFire, 0, 1), KB_F("dead_after", s_deadAfter, 0, 60), KB_I("sinner_sight", s_sinSight, 0, 1),
    KB_I("round_n", s_roundN, 1, 350), KB_I("round_s", s_roundS, 1, 350), KB_I("round_plus", s_roundPlus, 1, 350), KB_I("round_rocket", s_roundRocket, 0, 350), KB_I("round_fallback", s_roundFallback, 1, 100000),
    KB_F("gun_every", s_gunEvery, 0, 10), KB_F("plus_rate", s_plusRate, 0.1, 1), KB_F("guard_range", s_guardRange, 5, 200), KB_F("fire_range", s_fireRange, 5, 300),
    KB_I("pull_on", s_pullOn, 0, 1), KB_I("pull_explosion", s_pullExplosion, 0, 100000), KB_F("pull_every", s_pullEvery, 1, 600), KB_F("pull_radius", s_pullRadius, 1, 30),
    KB_F("s1_cooldown", s_s1Cd, 0, 600), KB_F("s1_every", s_s1Every, 0.05, 5), KB_F("s1_cone", s_s1Cone, 0.5, 45), KB_F("s1_range", s_s1Range, 10, 1000),
    KB_I("s1_rounds", s_s1Rounds, 1, 20), KB_I("s1_round", s_s1Round, 1, 350), KB_F("s1_stack", s_s1Stack, 0, 2), KB_F("turbo_seconds", s_turboSeconds, 0, 60), KB_F("turbo_mult", s_turboMult, 1, 5),
    KB_F("s2_cooldown", s_s2Cd, 0, 600), KB_F("kr_seconds", s_krSeconds, 0, 120), KB_F("kr_mult", s_krMult, 1, 5), KB_F("kr_heal", s_krHeal, 0, 1000),
    KB_F("kr_sinner_heal", s_krSinnerHeal, 0, 1),
    KB_F("s3_cooldown", s_s3Cd, 0, 3600), KB_I("cleanse", s_cleanse, 0, 2), KB_F("winner_cooldown", s_winnerCd, 0, 3600),
    KB_F("gear_seconds", s_gearSeconds, 1, 120), KB_F("gear_damage_d", s_gearD, 0, 10), KB_F("gear_damage_s", s_gearS, 0, 10), KB_F("gear_damage_plus", s_gearPlus, 0, 10), KB_F("gear_damage_n", s_gearN, 0, 10),
    KB_F("post_move", s_postMove, 1, 5), KB_F("sinner_taken", s_sinTaken, 0, 1), KB_F("park_taken", s_parkTaken, 0, 1), KB_F("sinner_heal", s_sinHeal, 0, 1), KB_F("sinner_heal_every", s_sinHealEvery, 0.5, 120),
    KB_I("release_round", s_relRound, 0, 100000), KB_I("release_rounds_s", s_relRoundsS, 1, 10), KB_I("release_rounds_plus", s_relRoundsPlus, 1, 10), KB_F("release_reach_plus", s_relReachPlus, 2, 80),
    KB_I("drift_points", s_relPoints, 3, 32), KB_F("drift_inner", s_relInner, 0, 30), KB_F("drift_gap", s_relGap, 0, 2),
    KB_I("ride_safe", s_rideSafe, 0, 1), KB_I("sinner_steady", s_sinSteady, 0, 1), KB_I("release_ring_s", s_relRingS, 0, 32), KB_I("release_ring_plus", s_relRingPlus, 0, 32),
    KB_I("he_explosion", s_heRow, 0, 100000), KB_I("he_round", s_heRound, 0, 100000), KB_F("he_rate", s_heRate, 1, 60), KB_I("full_he", s_fullHe, 0, 1), KB_I("release_he", s_releaseHe, 0, 1),
    KB_I("full_tracers", s_fullTracers, 0, 1), KB_F("full_tracer_every", s_tracerEvery, 0.03, 2), KB_I("tracer_damage", s_tracerDamage, 0, 100000),
    KB_I("full_on", s_fullOn, 0, 1), KB_I("full_shots", s_fullShots, 1, 8), KB_F("full_min_speed", s_fullMinSpeed, 0, 50), KB_F("full_ref_speed", s_fullRefSpeed, 1, 60), KB_F("full_every", s_fullEvery, 0.05, 5),
    KB_F("full_cone", s_fullCone, 5, 90), KB_F("full_range", s_fullRange, 5, 100), KB_F("full_ram", s_fullRam, 0, 1), KB_F("full_ram_max", s_fullRamMax, 1, 10),
    KB_I("drift_on", s_driftOn, 0, 1), KB_I("drift_rings", s_driftRings, 1, 8), KB_I("drift_damage", s_driftDamage, 0, 100000), KB_F("drift_cooldown", s_driftEvery, 0, 600), KB_F("drift_radius", s_driftRadius, 2, 50),
    KB_I("drive", s_drive, 0, 1), KB_I("turn", s_turnOn, 0, 1), KB_F("follow_distance", s_followDist, 3, 40), KB_F("personal_space", s_personal, 0, 20), KB_F("max_rounds", s_maxRounds, 1, 200), KB_F("round_speed", s_roundSpeed, 0, 3000), KB_I("drive_test", s_driveTest, 0, 1), KB_I("loco_manager", s_locoManager, -1, 100000000),
    KB_I("loco_map", s_locoMap, -1, 65536), KB_I("loco_records", s_locoRecords, -1, 65536), KB_I("loco_stride", s_locoStride, 16, 4096),
    KB_I("stick_offset_fwd", s_stickFwd, -1, 4096), KB_I("stick_offset_side", s_stickSide, -1, 4096), KB_I("stick_offset_turn", s_stickTurn, -1, 4096),
    KB_I("line_of_sight", s_sight, 0, 1), KB_F("first_person", s_fpDistance, 0.1, 2),
    KB_I("sinner_kind", s_frvOn, 0, 1),
    KB_I("release_explosion", s_releaseRow, 0, 100000), KB_I("release_shell", s_releaseShell, 0, 100000), 
    KB_F("release_ap", s_releaseAp, 0, 10), KB_F("release_reach", s_releaseReach, 1, 50), KB_I("release_targets_s", s_releaseTargetsS, 1, 20),
    KB_I("release_targets_plus", s_releaseTargetsPlus, 1, 20), KB_I("ems_immune", s_emsImmune, 0, 1), KB_F("release_every", s_releaseEvery, 0.5, 60), KB_F("release_radius", s_releaseRadius, 1, 20),
    KB_I("grip", s_grip, 0, 1), KB_F("grip_gravity", s_gripGravity, 0, 100), KB_F("grip_cancel", s_gripCancel, 0, 1), KB_F("grip_margin", s_gripMargin, 0.05, 3),
    KB_F("grip_mass", s_gripMass, 100, 100000),
    KB_I("car_steady", s_carSteady, 0, 1), KB_F("car_spin", s_carSpin, 0, 20), KB_F("car_lift", s_carLift, 0, 30), KB_F("car_window", s_carWindow, 0.1, 5),
    KB_I("car_right", s_carRight, 0, 1), KB_F("car_right_after", s_carRightAfter, 0.2, 30),
    KB_F("ride_range", s_rideRange, 1, 1000), KB_F("sprint_seconds", s_sprintSeconds, 0.5, 60), KB_F("sprint_cooldown", s_sprintCd, 0, 600),
    KB_F("sprint_ram", s_sprintRam, 0, 100), KB_F("sprint_ram_s", s_sprintRamS, 0, 100), KB_F("sprint_ram_plus", s_sprintRamPlus, 0, 100),
    KB_F("hp_mult", s_hpMult, 0.1, 10), KB_F("stamina_mult", s_staminaMult, 0.1, 20), KB_I("hellbomb_on_exhaustion", s_hellbomb, 0, 1),
    KB_I("hellbomb_explosion", s_hellbombRow, 0, 100000), KB_F("hellbomb_hold", s_hellbombHold, 0, 10),
};

/* The default koleda.ini: tools/inigen.py writes the file from this text (the mod never writes it) */
static __attribute__((unused)) void DefaultIni(FILE* f)
{
    fprintf(f, "; Koleda %s. Keys: a letter, digit, F1-F24, Mouse3, Mouse4 or Mouse5, with Shift+, Ctrl+ or Alt+ in front for a\n"
               "; combo (Shift+Q). The game still sees the keys too (Shift sprints, R reloads), so pick ones it leaves alone.\n"
               "key_s1 = %s\nkey_s2 = %s\nkey_s3 = %s\n"
               "; In the FRV Sinveiler the same keys are its skills: key_s1 Let's Go Full Speed! (at the wheel), key_s2 Let's Go for\n"
               "; a Drift!, key_s3 The Winner.\n"
               "; Upshift: at most upshift_max stacks, each +upshift_damage (0.15: 15%%) of her damage; one is lost every\n"
               "; upshift_decay s without a gain. A Sinveiler action (a volley, Drift, Full Speed) gives one (two in S+), at most one\n"
               "; gain every upshift_every s. At upshift_max she takes upshift_guard (0.3: 30%%) less damage.\n"
               "upshift_max = %d\nupshift_decay = %g\nupshift_damage = %g\nupshift_every = %g\nupshift_guard = %g\n"
               "; The Sinveiler (sinner 0: none). call_mode 1: Spectre puts one Sinveiler call in her stratagem list (code RRLLD)\n"
               "; each time; 0: one call granted at the drop, unlimited, with the stratagem's own cooldown (s3_cooldown).\n"
               "; share_pct of each hit on her goes to the Sinveiler instead (never below 1 from that), while it stands within\n"
               "; share_range m.\n"
               "sinner = %d\ncall_mode = %d\nshare_pct = %g\nshare_range = %g\n"
               "; The leash: it walks to her beyond leash_start m and stops within leash_stop (in N gear: within guard_start);\n"
               "; beyond leash_warp m it is warped beside her (the last resort). Stuck (walking, but under 0.5 m/s for\n"
               "; stuck_seconds) it is pushed through: stepped push_step m along its way, put on the ground each time, until it\n"
               "; moves again (at most push_steps steps; 0: never). Without the game's locomotion it slides\n"
               "; at slide_speed m/s. In S+ it stands plus_distance m short of her target, at most plus_leash m from her.\n"
               "; warp_needs_walks 1: it is moved only if it has a navigation record (the warp setter crashed on entities\n"
               "; without one); 0 skips that check (at the risk of that crash).\n"
               "leash_start = %g\nleash_stop = %g\nguard_start = %g\nleash_warp = %g\nstuck_seconds = %g\npush_step = %g\npush_steps = %d\nslide_speed = %g\n"
               "; nav_path 1: with a navigation record of its own, it is sent to its follow point on the navmesh (the game's\n"
               "; set_destination, as Lost Contact's patrols) and heads for the path's next waypoint instead of straight at it.\n"
               "plus_distance = %g\nplus_leash = %g\nwarp_needs_walks = %d\nnav_path = %d\n"
               "; Its fire (sinner_fire 0: none): rounds of hers from its shoulders, one every gun_every s (S+: times plus_rate);\n"
               "; gun_every 0: at the fire rate of the gun each gear's round comes from, with its pellets (0.1.2).\n"
               "; N: whoever last hit her, else the nearest enemy within guard_range m of her; S: the nearest within fire_range m\n"
               "; of it; S+: her target. Its own arms' rounds (projectile types, copied), at the Patriot's arms' fire rates: the\n"
               "; right arm's minigun by gear, round_n / round_s / round_plus (148: the Patriot's minigun), and in S+ the left arm's\n"
               "; rockets too, round_rocket (128: the Patriot's rockets; 0: none). One that cannot be spawned falls back to\n"
               "; round_fallback (7: the Guard Dog's). sinner_sight 1: only at what it can see (aiming higher up a target's body\n"
               "; when cover hides its middle). dead_after: a target whose health does not move in this many seconds of its fire\n"
               "; is left as dead for 15 s (0: never).\n"
               "sinner_fire = %d\ndead_after = %g\nsinner_sight = %d\nround_n = %d\nround_s = %d\nround_plus = %d\nround_rocket = %d\nround_fallback = %d\n"
               "gun_every = %g\nplus_rate = %g\nguard_range = %g\nfire_range = %g\n"
               "; The old pull (pull_on 1; untested, so off): in S+, every pull_every s, enemies within pull_radius m of the\n"
               "; Patriot Sinveiler are dragged to it, and pull_explosion (0: none) goes off on it.\n"
               "pull_on = %d\npull_every = %g\npull_radius = %g\npull_explosion = %d\n"
               "; Snow Wolf's Eye: cooldown; +1 Upshift, then the enemy under her crosshair (s1_cone degrees, s1_range m, in sight)\n"
               "; takes s1_rounds heavy rounds (s1_round, 226: the APW-1's), s1_every s apart, each s1_stack more per Upshift stack.\n"
               "; Turbo Boost: her damage and the Sinveiler's x turbo_mult for turbo_seconds. The Sinveiler goes to S+.\n"
               "s1_cooldown = %g\ns1_rounds = %d\ns1_every = %g\ns1_cone = %g\ns1_range = %g\ns1_round = %d\ns1_stack = %g\nturbo_seconds = %g\nturbo_mult = %g\n"
               "; Tactical Routebook: cooldown; +2 Upshift; Kinetic Recovery for kr_seconds: her damage and the Sinveiler's x kr_mult\n"
               "; (kr_heal: she heals this much, kr_sinner_heal: the Sinveiler this part of its health; 0: none); one Kinetic Release\n"
               "; around her. The Sinveiler goes to S.\n"
               "s2_cooldown = %g\nkr_seconds = %g\nkr_mult = %g\nkr_heal = %g\nkr_sinner_heal = %g\n"
               "; Spectre: cooldown. The Winner (Spectre while it stands, every winner_cooldown s): full health; cleanse 1 removes\n"
               "; the Sinveiler's fire, gas, stun and slow once the game's status functions are pinned (off until then); 2 calls them\n"
               "; unpinned; 0 never.\n"
               "s3_cooldown = %g\ncleanse = %d\nwinner_cooldown = %g\n"
               "; Gears: D unless a skill set another, which lasts gear_seconds. The Sinveiler's damage by gear: gear_damage_d / _s /\n"
               "; _plus / _n (times Upshift, Turbo Boost, Kinetic Recovery, Post-Move Support). Post-Move Support: while it stands,\n"
               "; her damage and its x post_move. Gear Shift: it takes sinner_taken of the damage (in N none; standing still:\n"
               "; times park_taken); a hit on it heals sinner_heal of its health, at most every sinner_heal_every s.\n"
               "gear_seconds = %g\ngear_damage_d = %g\ngear_damage_s = %g\ngear_damage_plus = %g\ngear_damage_n = %g\npost_move = %g\n"
               "sinner_taken = %g\npark_taken = %g\nsinner_heal = %g\nsinner_heal_every = %g\n"
               "; Kinetic Release (Gear Shift, S and S+, every release_every s with enemies near; and right after Tactical\n"
               "; Routebook): heavy rifle rounds from it (release_round 0: Snow Wolf's Eye's, with its tracer; else a game\n"
               "; projectile, 241: the Diligence Counter Sniper's) at the nearest\n"
               "; release_targets_s enemies within release_reach m (S+: release_targets_plus within release_reach_plus),\n"
               "; release_rounds_s each (S+: release_rounds_plus), by its gear and her buffs.\n"
               "; Around it too: release_ring_s rounds (S+: release_ring_plus) evenly all around, landing 0.85 of its reach out.\n"
               "; ride_safe 1: in the Sinveiler nothing damages her. sinner_steady 1: hits on the Sinveiler keep their damage but\n"
               "; lose their stagger and push (the bots' cannons flipped the FRV).\n"
               "release_round = %d\nrelease_rounds_s = %d\nrelease_rounds_plus = %d\nrelease_reach_plus = %g\nrelease_ring_s = %d\nrelease_ring_plus = %d\nride_safe = %d\nsinner_steady = %d\n"
               "; Let's Go for a Drift! (key_s2 in the FRV Sinveiler; drift_on 0: off): its active, once a press, every\n"
               "; drift_cooldown s, not in N: waves of EMS bursts out from it: drift_rings rings from drift_inner m out to drift_radius m,\n"
               "; drift_gap s apart, bursts about 5 m apart (at most drift_points a ring); each enemy in reach takes\n"
               "; drift_damage once, release_ap armour penetration (the stun holds them; nobody is moved). release_shell 154:\n"
               "; the EMS mortar's shell from above; else release_explosion 150. ems_immune 1: no EMS stun on her or the Sinveiler.\n"
               "; Her bursts spare her, the Sinveiler and her allies.\n"
               "drift_on = %d\ndrift_cooldown = %g\ndrift_radius = %g\ndrift_rings = %d\ndrift_damage = %d\ndrift_points = %d\ndrift_inner = %g\ndrift_gap = %g\n"
               "; HE rounds (full_he 1: Full Speed's, release_he 1: Kinetic Release's): where each of their rounds lands, the\n"
               "; Orbital Gatling Barrage's high-explosive blast (he_explosion, 266: its shell's; 0: none), at most he_rate a\n"
               "; second; by its gear and her buffs, sparing her, the Sinveiler and her allies.\n"
               "; he_round 77: the barrage's own HE shell is fired from the Sinveiler at them and bursts where it hits (its launch\n"
               "; crashed the game once in another mod: 0 goes back to a bullet with the blast set off where it lands).\n"
               "he_explosion = %d\nhe_rate = %g\nfull_he = %d\nrelease_he = %d\nhe_round = %d\n"
               "; Let's Go Full Speed! (full_on 0: off; not in N): while it moves at full_min_speed m/s or more, it fires at the\n"
               "; enemies ahead (full_cone degrees either side of its way, within full_range m), full_shots shots spread among\n"
               "; them every full_every s at full_ref_speed m/s (faster: more often); and its rams deal 1 + full_ram for each m/s over full_min_speed times\n"
               "; more (at most full_ram_max).\n"
               "; full_tracers 1: at that speed it also sprays tracer rounds ahead across the cone, one every full_tracer_every s,\n"
               "; enemies or not (Snow Wolf's Eye's bright round with the minigun's damage, tracer_damage: a damage id).\n"
               "full_tracers = %d\nfull_tracer_every = %g\ntracer_damage = %d\n"
               "full_on = %d\nfull_shots = %d\nfull_min_speed = %g\nfull_ref_speed = %g\nfull_every = %g\nfull_cone = %g\nfull_range = %g\nfull_ram = %g\nfull_ram_max = %g\n"
               "; Driving with the game's own locomotion (the probe's numbers; -1 unknown: the Sinveiler slides instead).\n"
               "; drive_test 1 writes the stick without the engine and mover calls.\n"
               "; turn 1: it turns toward what it fires at, else the way it slides (its turn record, 0.1.4).\n"
               "; round_speed: m/s of its minigun rounds (her copies; 0: the Patriot's own): slower, its tracers start at its arm.\n"
               "; max_rounds: the most projectiles it spawns a second, in all (spawned rounds by the thousand crashed the renderer).\n"
               "; follow_distance: m it keeps from her (beside and behind); personal_space: closer than this it steps away.\n"
               "follow_distance = %g\npersonal_space = %g\nmax_rounds = %g\nround_speed = %g\nturn = %d\ndrive = %d\ndrive_test = %d\nloco_manager = %d\nloco_map = %d\nloco_records = %d\nloco_stride = %d\n"
               "stick_offset_fwd = %d\nstick_offset_side = %d\nstick_offset_turn = %d\n"
               "; Aiming: line_of_sight 1, Snow Wolf's Eye's target must be in sight. first_person: the camera this close to\n"
               "; her head (m) counts as first person.\nline_of_sight = %d\nfirst_person = %g\n"
               "; Which Sinveiler: sinner_kind 1 the FRV Sinveiler (the default), 0 the Patriot (also a choice on the MODS tab; it applies\n"
               "; to her next Spectre). The FRV Sinveiler is the M-103 Supply FRV she brings in her loadout (without one she gets\n"
               "; the Patriot): Spectre clears its cooldown, and the one she calls becomes the Sinveiler: its own turret fights.\n"
               "; Spectre while it stands: Let's Go For a Spin! (she is put in its driver seat, else a free passenger seat, within\n"
               "; ride_range m); pressed again inside it: The Winner.\n"
               "; Kinetic Release's targets: release_targets_s / _plus, within release_reach (above). release_radius: unused.\n"
               "sinner_kind = %d\n"
               "release_explosion = %d\nrelease_shell = %d\nrelease_ap = %g\nrelease_reach = %g\n"
               "release_targets_s = %d\nrelease_targets_plus = %d\nems_immune = %d\nrelease_every = %g\nrelease_radius = %g\nride_range = %g\n"
               "; Full Speed burst: Snow Wolf's Eye's key while she drives the FRV Sinveiler: for sprint_seconds it and she take\n"
               "; no damage and its rams deal sprint_ram times more (and sprint_ram_s more in S, sprint_ram_plus in S+).\n"
               "sprint_seconds = %g\nsprint_cooldown = %g\nsprint_ram = %g\nsprint_ram_s = %g\nsprint_ram_plus = %g\n"
               "; Grip (grip 0: off): while fewer than 2 of the FRV Sinveiler's 4 corners are within grip_margin m of their resting\n"
               "; height over the ground, grip_cancel of its upward speed is taken out each frame and it is pushed down with\n"
               "; grip_gravity m/s2 more (its mass taken as grip_mass kg). Rocks fling it less; jumps are cut short too.\n"
               "grip = %d\ngrip_gravity = %g\ngrip_cancel = %g\ngrip_margin = %g\ngrip_mass = %g\n"
               "; Steady (car_steady 0: off): for car_window s after a hit on the FRV Sinveiler, its roll and pitch spin is held to\n"
               "; car_spin rad/s and its upward speed to car_lift m/s (the bots' cannons flipped it). car_right 1: lying over\n"
               "; (tilted past 60 degrees) for car_right_after s, it is set upright where it is.\n"
               "car_steady = %d\ncar_spin = %g\ncar_lift = %g\ncar_window = %g\ncar_right = %d\ncar_right_after = %g\n"
               "; Her body: hp_mult times a Helldiver's health, stamina_mult times her sprint. hellbomb_on_exhaustion 1 sets off a\n"
               "; Hellbomb on her if she runs her stamina dry (a T-Doll's price).\n"
               "hp_mult = 1.5\nstamina_mult = 2\nhellbomb_on_exhaustion = 1\n",
            VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, s_upMax, (double)s_upDecay, (double)s_upDamage, (double)s_upEvery, (double)s_upGuard,
            s_sinOn, s_callMode, (double)s_sharePct, (double)s_shareRange,
            (double)s_leashStart, (double)s_leashStop, (double)s_guardStart, (double)s_leashWarp, (double)s_stuckSeconds, (double)s_pushStep, s_pushSteps, (double)s_slideSpeed,
            (double)s_plusDistance, (double)s_plusLeash, s_warpWalks, s_navPath,
            s_sinFire, (double)s_deadAfter, s_sinSight, s_roundN, s_roundS, s_roundPlus, s_roundRocket, s_roundFallback, (double)s_gunEvery, (double)s_plusRate, (double)s_guardRange,
            (double)s_fireRange, s_pullOn, (double)s_pullEvery, (double)s_pullRadius, s_pullExplosion,
            (double)s_s1Cd, s_s1Rounds, (double)s_s1Every, (double)s_s1Cone, (double)s_s1Range, s_s1Round, (double)s_s1Stack, (double)s_turboSeconds, (double)s_turboMult,
            (double)s_s2Cd, (double)s_krSeconds, (double)s_krMult, (double)s_krHeal, (double)s_krSinnerHeal, (double)s_s3Cd, s_cleanse, (double)s_winnerCd,
            (double)s_gearSeconds, (double)s_gearD, (double)s_gearS, (double)s_gearPlus, (double)s_gearN, (double)s_postMove,
            (double)s_sinTaken, (double)s_parkTaken, (double)s_sinHeal, (double)s_sinHealEvery,
            s_relRound, s_relRoundsS, s_relRoundsPlus, (double)s_relReachPlus, s_relRingS, s_relRingPlus, s_rideSafe, s_sinSteady,
            s_driftOn, (double)s_driftEvery, (double)s_driftRadius, s_driftRings, s_driftDamage, s_relPoints, (double)s_relInner, (double)s_relGap,
            s_heRow, (double)s_heRate, s_fullHe, s_releaseHe, s_heRound,
            s_fullTracers, (double)s_tracerEvery, s_tracerDamage,
            s_fullOn, s_fullShots, (double)s_fullMinSpeed, (double)s_fullRefSpeed, (double)s_fullEvery, (double)s_fullCone, (double)s_fullRange, (double)s_fullRam, (double)s_fullRamMax,
            (double)s_followDist, (double)s_personal, (double)s_maxRounds, (double)s_roundSpeed, s_turnOn, s_drive, s_driveTest, s_locoManager, s_locoMap, s_locoRecords, s_locoStride, s_stickFwd, s_stickSide, s_stickTurn,
            s_sight, (double)s_fpDistance, s_frvOn, s_releaseRow, s_releaseShell, (double)s_releaseAp, (double)s_releaseReach, s_releaseTargetsS,
            s_releaseTargetsPlus, s_emsImmune, (double)s_releaseEvery, (double)s_releaseRadius, (double)s_rideRange,
            (double)s_sprintSeconds, (double)s_sprintCd, (double)s_sprintRam, (double)s_sprintRamS, (double)s_sprintRamPlus,
            s_grip, (double)s_gripGravity, (double)s_gripCancel, (double)s_gripMargin, (double)s_gripMass,
            s_carSteady, (double)s_carSpin, (double)s_carLift, (double)s_carWindow, s_carRight, (double)s_carRightAfter);
}

#include "sinner_drive.h"

/* ---- state ---------------------------------------------------------------------------------------------------- */
static double g_cd[3];
static unsigned g_casts[3], g_volleys, g_eyeRounds, g_winners, g_warps, g_pulls, g_upGained;
static int g_up;                      /* Upshift stacks */
static double g_upAt, g_actionAt = -100;
static double g_krUntil;              /* Kinetic Recovery */
enum { GEAR_N = 0, GEAR_S = 1, GEAR_PLUS = 2, GEAR_D = 3 };   /* (0.3.0: D, the default; P = D standing still) */
static int g_gear = GEAR_D;
static double g_gearUntil;            /* N, S, S+ until then, then D */
static const char* const kGear[4] = {"N", "S", "S+", "D"};
static double g_turboUntil, g_eyeHitUntil, g_fightAt = -100;
static int g_parked;                  /* the Sinveiler stands still (P) */
static atomic_uint g_threat;          /* whoever last hit her (the hit hook) */
static _Atomic unsigned long long g_threatMs;
static uint32_t g_plusTarget;         /* Snow Wolf's Eye's target (S+ pushes toward it) */

/* ---- reads: health, positions, her view -------------------------------------------------------------------------- */
/* An entity's health record (HD2Runtime's research: the health manager at game+0x3326688, its open-addressed entity
 * hash, 440-byte records with health at +20 and life at +412, 0 alive): its address, 0 unknown */
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
/* 0.2.10: who is dead to her and the Sinveiler (Exusiai 0.2.13's rule, and more): a life state counts as dead once a kill
 * has shown it; an entity a kill named stays dead for 60 s (whatever its record says); one the Sinveiler fired at for
 * dead_after s without its health moving is left for 15 s (a corpse that kept its health and life state 0) */
static uint32_t g_deadLife[8];
static int g_ndeadLife;
static struct { uint32_t e; double until; } g_dead[64];
static unsigned g_deadNext;

static int DeadLife(uint32_t life) { for (int i = 0; i < g_ndeadLife; i++) if (g_deadLife[i] == life) return 1; return 0; }
static void DeadAdd(uint32_t e, double seconds)
{
    for (int i = 0; i < 64; i++) if (g_dead[i].e == e) { g_dead[i].until = g_now + seconds; return; }
    unsigned k = g_deadNext++ % 64;
    g_dead[k].e = e; g_dead[k].until = g_now + seconds;
}
static int Dead(uint32_t e) { for (int i = 0; i < 64; i++) if (g_dead[i].e == e && g_now < g_dead[i].until) return 1; return 0; }
static void LearnDeadLife(uint32_t e)
{
    int32_t hp = 0;
    uint32_t life = 0;
    if (!HealthOf(e, &hp, &life) || !life || DeadLife(life) || g_ndeadLife >= 8) return;
    g_deadLife[g_ndeadLife++] = life;
    Log("a kill showed life state %u (health %d left): an enemy in it is dead to the Sinveiler", life, hp);
}
static int Alive(uint32_t e)
{
    int32_t hp;
    uint32_t life = 0;
    if (Dead(e) || !HealthOf(e, &hp, &life) || hp <= 0) return 0;
    return !life || !DeadLife(life);
}
/* 0.1.4: the middle of a unit's body: the centre of its scene-graph nodes' box (a Bile Titan's root is on the ground) */
#define BODIES 64
static struct { uint32_t unit; double at; float off[3]; } g_body[BODIES];
static int CmpF(const void* x, const void* y) { float a = *(const float*)x, b = *(const float*)y; return a < b ? -1 : a > b; }
static int BodyOffset(uint32_t unit, const float root[3], float off[3])
{
    unsigned h = (unit * 2654435761u) % BODIES;
    if (g_body[h].unit == unit && g_now - g_body[h].at < 1.0) { memcpy(off, g_body[h].off, 12); return 1; }
    uintptr_t registry = Pointer(exe + 0x1A100F0), objects, object, nodes;
    uint32_t count = 0, id, slot = unit & 0x3FFFFF;
    static float m[256 * 16];
    if (!registry || !(objects = Pointer(registry + 0x88)) || !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit ||
        !ReadAt(object + 0x70, &count, 4) || !count || count > 4096 || !(nodes = Pointer(object + 0x88)))
        return 0;
    if (count > 256) count = 256;
    if (!ReadAt(nodes, m, (size_t)count * 64)) return 0;
    /* the median of its nodes (the box's centre overshot small enemies: a few nodes far up, a weapon, an effect) */
    static float v[3][256];
    int used = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        const float* t = m + 16 * i + 12;
        if (!isfinite(t[0]) || !isfinite(t[1]) || !isfinite(t[2]) || fabsf(t[0] - root[0]) > 20 || fabsf(t[1] - root[1]) > 20 || t[2] < root[2] - 2 || t[2] > root[2] + 20) continue;
        for (int k = 0; k < 3; k++) v[k][used] = t[k] - root[k];
        used++;
    }
    if (used < 2) return 0;
    for (int k = 0; k < 3; k++) { qsort(v[k], used, sizeof(float), CmpF); off[k] = v[k][used / 2]; }
    /* (Yui, 2026-11-11: the guns shot the floor under an Illuminate spawner) a tall thing whose nodes cluster at its
     * foot (a structure: most of them on the ground, its body metres up): half its height instead (its 90th-percentile
     * node, so a stray effect node far up does not count) */
    {
        float top = v[2][(used * 9) / 10];
        if (top > 3.0f && off[2] < 0.35f * top) off[2] = 0.5f * top;
    }
    if (off[2] < 0.3f) off[2] = 0.3f;
    g_body[h].unit = unit; g_body[h].at = g_now; memcpy(g_body[h].off, off, 12);
    return 1;
}
static int TargetPos(uint32_t e, uint64_t res, float out[3])
{
    uint8_t d[24];
    if (!Descriptor(e, d) || (res && Quad(d) != res) || !UnitNode(Word(d + 12), 0, out)) return 0;
    float off[3];
    if (BodyOffset(Word(d + 12), out, off)) { for (int k = 0; k < 3; k++) out[k] += off[k]; }
    else out[2] += 1.0f;
    return 1;
}
static uint32_t UnitOf(uint32_t e)
{
    uint8_t d[24];
    return Descriptor(e, d) ? Word(d + 12) : 0;
}
/* a node's world matrix in a unit's scene graph (rows right, forward, up, position), as UnitNode reads its translation */
static int UnitPose(uint32_t unit, uint32_t node, float m[16])
{
    if (!exe) return 0;
    uintptr_t registry = Pointer(exe + 0x1A100F0), generations, objects, object, nodes;
    uint32_t count, id, slot = unit & 0x3FFFFF;
    uint8_t generation;
    if (!registry || !ReadAt(registry + 0x98, &count, 4) || count > 0x400000 || slot >= count ||
        !(generations = Pointer(registry + 0xA0)) || !ReadAt(generations + slot, &generation, 1) || generation != (uint8_t)(unit >> 22) ||
        !(objects = Pointer(registry + 0x88)) || !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit ||
        !ReadAt(object + 0x70, &count, 4) || node >= count || count > 4096 || !(nodes = Pointer(object + 0x88)))
        return 0;
    if (!ReadAt(nodes + 64u * node, m, 64)) return 0;
    for (int i = 0; i < 16; i++) if (!isfinite(m[i]) || fabsf(m[i]) > 100000) return 0;
    return 1;
}
/* her camera: where it is and where it looks; fp: it sits at her head */
static int View(float pos[3], float fwd[3], int* fp)
{
    uint8_t d[24];
    if (!Descriptor(atomic_load(&g_avatar), d)) return 0;
    uint32_t unit = Word(d + 12), slot = unit & 0x3FFFFF, id = 0;
    uintptr_t registry = Pointer(exe + 0x1A100F0), objects, object, w;
    float m[16], head[3];
    if (!registry || !(objects = Pointer(registry + 0x88)) || !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit ||
        !(w = Pointer(object + 0x18)) || !ReadAt(w + 0x230920, m, 64)) return 0;
    for (int k = 0; k < 16; k++) if (!isfinite(m[k])) return 0;
    if (fabsf(m[15] - 1) > 1e-3f) return 0;
    memcpy(fwd, m + 4, 12);
    memcpy(pos, m + 12, 12);
    float n = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    if (!(n > 0.5f)) return 0;
    for (int k = 0; k < 3; k++) fwd[k] /= n;
    *fp = UnitNode(unit, NODE_HEAD, head) && Dist2(pos, head) < s_fpDistance * s_fpDistance;
    return 1;
}
static float Off(const float cam[3], const float fwd[3], const float p[3], float* dist)
{
    float v[3] = {p[0] - cam[0], p[1] - cam[1], p[2] - cam[2]};
    float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    *dist = n;
    if (!(n > 0.5f)) return 180;
    float c = (v[0] * fwd[0] + v[1] * fwd[1] + v[2] * fwd[2]) / n;
    return acosf(c < -1 ? -1 : c > 1 ? 1 : c) * 57.29578f;
}
static int HerHead(float out[3])
{
    uint8_t d[24];
    return Descriptor(atomic_load(&g_avatar), d) && UnitNode(Word(d + 12), NODE_HEAD, out);
}
static float Flat(const float a[3], const float b[3]) { float x = a[0] - b[0], y = a[1] - b[1]; return sqrtf(x * x + y * y); }
static int FoeIndex(uint32_t e) { for (int k = 0; k < FOES; k++) if (e && g_foe[k] == e) return k; return -1; }
/* a friend of hers: another Helldiver, or anything on the kit base's friend list (the Sinveiler and its arms too) */
static int Friend(uint32_t e)
{
    uint8_t d[24];
    if (!Descriptor(e, d)) return 0;
    uint64_t res = Quad(d);
    return res == AVATAR || FriendRes(res);
}

/* ---- line of sight (Lemuen 0.1.6's and Alva's: the game's raycast, skipping her, leaves, bodies and the target) ---- */
typedef uint32_t (*WorldFn)(const void*);
typedef uint32_t (*RayFn)(uint32_t, const void*, const void*, float, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t);
static WorldFn g_worldId;
static RayFn g_raycast;
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
static uint32_t g_foeUnit[FOES];
static int g_nfoeUnit;
static double g_foeUnitAt = -1;
static int FoeUnit(uint32_t u)
{
    if (g_now - g_foeUnitAt > 0.25 || g_foeUnitAt < 0)
    {
        g_foeUnitAt = g_now;
        g_nfoeUnit = 0;
        for (int k = 0; k < FOES; k++) if (g_foe[k]) { uint32_t fu = UnitOf(g_foe[k]); if (fu) g_foeUnit[g_nfoeUnit++] = fu; }
    }
    for (int i = 0; i < g_nfoeUnit; i++) if (g_foeUnit[i] == u) return 1;
    return 0;
}
static int WorldId(uint32_t* id)
{
    if (!g_raycast || !g_worldId) return 0;
    uintptr_t world = Pointer(game + 0x346BFA0);
    if (!world) return 0;
    *id = g_worldId((const void*)world);
    return *id < 4;
}
static uint32_t g_sinner;   /* (defined below) */
/* the first wall along a ray (len + 1: clear); *hitTarget when the target's unit is met first; skip: a second unit to
 * see through (the Sinveiler) */
static float FirstWall(uint32_t id, const float from[3], const float dir[3], float len, uint32_t me, uint32_t tu, uint32_t skip, int* hitTarget)
{
    uint8_t hits[16 * 44];
    memset(hits, 0, sizeof hits);
    uint32_t n = g_raycast(id, from, dir, len, 2, 1, 0, 0x80000009u, me, hits, 16);
    if (n > 16) n = 16;
    float best = len + 1;
    int target = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        float d;
        memcpy(&d, hits + 44u * i + 24, 4);
        uint32_t hu = Word(hits + 44u * i + 28);
        if (!isfinite(d) || d < 1.2f || d >= best || (hu && (hu == me || hu == skip))) continue;
        if (hu && tu && hu == tu) { best = d; target = 1; continue; }
        uint32_t filt = ActorFilter(Word(hits + 44u * i + 32), hu);
        if (filt == FILTER_FOLIAGE || (hu && (filt == 0x236122F0u || filt == 0xD5B33D56u || FoeUnit(hu)))) continue;
        best = d; target = 0;
    }
    if (hitTarget) *hitTarget = target;
    return best;
}
static int InSight(const float cam[3], const float fwd[3], const float q[3], uint32_t target)
{
    uint32_t id;
    if (!s_sight || !WorldId(&id)) return 1;
    uint32_t me = UnitOf(atomic_load(&g_avatar)), tu = UnitOf(target);
    for (int r = 0; r < 3; r++)
    {
        float to[3] = {q[0], q[1], q[2] + (r == 1 ? 0.7f : 0)}, dir[3], len;
        if (r < 2)
        {
            for (int k = 0; k < 3; k++) dir[k] = to[k] - cam[k];
            len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
            if (!(len > 2.0f)) return 1;
            for (int k = 0; k < 3; k++) dir[k] /= len;
        }
        else
        {
            float d;
            if (Off(cam, fwd, q, &d) > 4) break;
            memcpy(dir, fwd, sizeof dir);
            len = d + 2;
        }
        int hitTarget = 0;
        float w = FirstWall(id, cam, dir, len, me, tu, UnitOf(g_sinner), &hitTarget);   /* 0.1.1: through the Sinveiler */
        float reach = r < 2 ? len : len - 2;
        if (hitTarget || w > reach - 1.5f) return 1;
    }
    return 0;
}
/* the living enemy nearest her crosshair (within cone degrees, range m, in sight): 1 with *target and at */
static unsigned g_aimBlocked;
static int Aim(float cone, float range, float at[3], uint32_t* target)
{
    float cam[3], fwd[3], q[3];
    int fp;
    *target = 0;
    if (!View(cam, fwd, &fp)) return 0;
    float bestOff = cone;
    for (int k = 0; k < FOES; k++)
    {
        uint32_t e = g_foe[k];
        float d, o;
        if (!e || !TargetPos(e, g_foeRes[k], q) || (o = Off(cam, fwd, q, &d)) >= bestOff || d > range) continue;
        if (!Alive(e)) continue;
        if (!InSight(cam, fwd, q, e)) { g_aimBlocked++; continue; }
        bestOff = o; *target = e; memcpy(at, q, 12);
    }
    return *target != 0;
}

/* ---- the warp setter (Voymastina's: the LIFT-182 Warp Pack's own call, game.dll+0x5A2BB0, then the velocity zeroed
 * with 0x4A7550) and what guards it ------------------------------------------------------------------------------- */
typedef void (*SetVecFn)(void*, uint32_t, const float*);
static SetVecFn g_setPosition, g_setVelocity;
/* Voymastina 0.4.1b: an entity with an active navigation record ([game+0x3326418]: map +0x30, records +0x50, 0xA8 each,
 * +0x10 active). The warp setter crashed on entities without one (a strider's rider, a dead enemy) */
static int Walks(uint32_t e)
{
    uintptr_t m = Pointer(game + 0x3326418), recs;
    uint32_t index, active = 0;
    if (!m || !Lookup(m + 0x30, e, 1048576, &index) || index > 65535 || !(recs = Pointer(m + 0x50))) return 0;
    return ReadAt(recs + 0xA8u * index + 0x10, &active, 4) && (active & 0xFF) != 0;
}
/* whether the warp setter may move this entity: this machine's, numbered under 0x400000, alive, and (warp_needs_walks)
 * with a navigation record. why: the reason when not */
static int Movable(uint32_t e, const char** why)
{
    uint8_t d[24];
    *why = "";
    if (!g_setPosition) { *why = "the warp setter is off (not the reviewed code)"; return 0; }
    if (!Descriptor(e, d)) { *why = "it is gone"; return 0; }
    if (!(Word(d + 20) & 1) || e >= 0x400000u) { *why = "it is not simulated here"; return 0; }
    if (!Alive(e)) { *why = "it is down"; return 0; }
    /* 0.1.1: the Sinveiler is exempt (our own spawn, simulated here; the Drive Probe found it has no navigation record,
     * so 0.1.0 never moved it): warp_needs_walks guards the others (a strider's pilot crashed the setter) */
    if (s_warpWalks && e != g_sinner && !Walks(e)) { *why = "it has no navigation record (warp_needs_walks 1)"; return 0; }
    return 1;
}

/* ---- navmesh pathing: the navigation record (Nav Probe's layout: +0x10 active, +0x54 its position, +0x60 destination,
 * +0x6C next waypoint, +0x78 path goal; docs/research/ai-modding.md on branch ai-probe) and set_destination
 * (game+0x5B36C0(NULL, entity, &pos), as Lost Contact's patrols call it, guarded by its Boundary() check) ----------- */
typedef void (*SetDestFn)(void*, uint32_t, const float*);
static SetDestFn g_setDest;
static unsigned g_navSends;
/* a function start: the byte before it is padding or a return, its first is not (Lost Contact's check) */
static int Boundary(uintptr_t at)
{
    uint8_t b[2];
    return hf->size >= offsetof(HfApi, read_code) + sizeof hf->read_code && hf->read_code && !hf->read_code(at - 1, b, 2) &&
           (b[0] == 0xCC || b[0] == 0xC3 || b[0] == 0x90) && b[1] != 0xCC;
}
/* its active navigation record: its address, 0 none */
static uintptr_t NavRec(uint32_t e)
{
    uintptr_t m = Pointer(game + 0x3326418), recs;
    uint32_t index, active = 0;
    if (!m || !e || !Lookup(m + 0x30, e, 1048576, &index) || index > 65535 || !(recs = Pointer(m + 0x50))) return 0;
    uintptr_t rec = recs + 0xA8u * index;
    return ReadAt(rec + 0x10, &active, 4) && (active & 0xFF) ? rec : 0;
}
static int Finite3(const float v[3]) { return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]) && fabsf(v[0]) < 100000 && fabsf(v[1]) < 100000 && fabsf(v[2]) < 100000; }
/* the point to steer at on the way to `to`: the path's next waypoint when the Sinveiler has a navigation record (its
 * destination sent every second), else `to` itself. 1 when the navmesh gave the point */
static double g_navSentAt = -100;
static int NavSteer(uint32_t e, const float p[3], const float to[3], float out[3])
{
    memcpy(out, to, 12);
    if (!s_navPath || !g_setDest) return 0;
    uintptr_t rec = NavRec(e);
    if (!rec) return 0;
    if (g_now - g_navSentAt >= 1.0)
    {
        g_navSentAt = g_now;
        g_setDest(NULL, e, to);
        float dest[3] = {0, 0, 0};
        ReadAt(rec + 0x60, dest, 12);
        if (g_navSends++ < 6) Log("Sinveiler pathing: set_destination %.1f %.1f %.1f (its record's destination reads %.1f %.1f %.1f)", (double)to[0], (double)to[1],
                                  (double)to[2], (double)dest[0], (double)dest[1], (double)dest[2]);
    }
    float w[3];
    if (!ReadAt(rec + 0x6C, w, 12) || !Finite3(w)) return 0;
    float dx = w[0] - p[0], dy = w[1] - p[1], d2 = dx * dx + dy * dy;
    if (d2 < 0.5f * 0.5f || d2 > 30.0f * 30.0f) return 0;   /* reached, or not a waypoint of this path */
    memcpy(out, w, 12);
    static unsigned told;
    if (told++ < 6) Log("Sinveiler pathing: heading for the next waypoint %.1f %.1f %.1f (%.1f m)", (double)w[0], (double)w[1], (double)w[2], sqrtf(d2));
    return 1;
}

/* ---- Upshift --------------------------------------------------------------------------------------------------------- */
static void Upshift(int n, const char* why)
{
    if (n <= 0) return;
    int before = g_up;
    g_up += n;
    if (g_up > s_upMax) g_up = s_upMax;
    g_upAt = g_now;
    g_upGained += (unsigned)(g_up - before);
    static unsigned told;
    if (told++ < 16) Log("Upshift +%d (%s): %d of %d", n, why, g_up, s_upMax);
}
static void UpshiftFrame(void)
{
    if (g_up > s_upMax) g_up = s_upMax;
    if (g_up > 0 && g_now - g_upAt >= s_upDecay) { g_up--; g_upAt = g_now; }
}
static void SinnerAction(const char* why)
{
    if (g_now - g_actionAt < s_upEvery) return;
    g_actionAt = g_now;
    Upshift(g_gear == GEAR_PLUS ? 2 : 1, why);
}
/* 0.3.0: Gear Shift's rules: N, S, S+ last gear_seconds, then D; S goes only up to S+; S+ is held to its end; N comes
 * only from D (The Winner). D: always (the Sinveiler gone or new, the time over) */
static int Gear(int gear, const char* why)
{
    const char* no = NULL;
    if (gear != GEAR_D)
    {
        if (g_gear == GEAR_PLUS && gear != GEAR_PLUS) no = "S+ is held to its end";
        else if (g_gear == GEAR_PLUS) no = "already S+";
        else if (gear == GEAR_N && g_gear != GEAR_D) no = "N only from D";
        else if (gear == GEAR_S && g_gear == GEAR_S) no = NULL;
    }
    if (no) { static unsigned told; if (told++ < 12) Log("the Sinveiler's gear stays %s (%s: %s)", kGear[g_gear], why, no); return 0; }
    g_gearUntil = gear == GEAR_D ? 0 : g_now + s_gearSeconds;
    if (gear == g_gear) return 1;
    Log("the Sinveiler's gear: %s -> %s (%s)", kGear[g_gear], kGear[gear], why);
    g_gear = gear;
    return 1;
}
/* its rounds by gear: D shoots S's */
static int RoundGear(void) { return g_gear == GEAR_D ? GEAR_S : g_gear; }
static double GearDamage(void) { return g_gear == GEAR_PLUS ? s_gearPlus : g_gear == GEAR_S ? s_gearS : g_gear == GEAR_N ? s_gearN : s_gearD; }

/* ==== the Sinveiler ===================================================================================================== */
/* her exosuit (a copy of the EXO-45 Patriot) and its stratagem, made in hellforge_mod_init */
static uint64_t g_sinRes;
static uint32_t g_sinType;
/* 0.2.0: the FRV Sinveiler: her own M-103 Supply FRV, its automatic turret its gun */
#define FRV_RES HF_ENTITY_M103_SUPPLY_FRV
#define FRV_TURRET UINT64_C(0x4DF41F84668D07FB)
#define FRV_STRATAGEM HF_STRATAGEM_M103_SUPPLY_FRV
static int g_frv;                      /* the standing Sinveiler is the FRV */
static uint32_t g_frvType;             /* 0.2.6: her FRV Sinveiler call (a copy of the M-103's stratagem) */
static uint32_t g_frvNet, g_turretNet, g_turretCandNet, g_turretCand, g_turretEnt;
static atomic_uint g_turretSeenTarget;
static _Atomic unsigned long long g_turretSeenMs;
static int g_rideWanted;
/* 0.2.2: Let's Go Full Speed! (its hits on enemies: dealer the FRV, its network id learned as the turret's) */
static double g_sprintUntil, g_sprintCdUntil;
static atomic_uint g_frvSeenTarget;
static _Atomic unsigned long long g_frvSeenMs;
static unsigned g_rams, g_sprints, g_sprintSaved;
static int Sprinting(void) { return g_frv && g_sinner && g_now < g_sprintUntil; }
/* 0.2.5: Kinetic Release's targets (its EMS bursts deal release_damage at release_ap to them, once each) */
#define REL_TARGETS 24
static atomic_uint g_relTarget[REL_TARGETS];
static atomic_int g_relPaid[REL_TARGETS];
static _Atomic unsigned long long g_relMs;
static int RelIndex(uint32_t e)
{
    if (!e || GetTickCount64() - atomic_load(&g_relMs) > 3000) return -1;
    for (int i = 0; i < REL_TARGETS; i++) if (atomic_load(&g_relTarget[i]) == e) return i;
    return -1;
}
static double g_turretCandAt = -100;
static int g_frvParked;
static atomic_int g_repairPending;
static unsigned g_turretHits, g_noDeath, g_releases, g_rides;
static uint64_t SinRes(void) { return g_frv ? FRV_RES : g_sinRes; }
/* the one standing: its entity, unit, when it spawned, whether it has landed */
static uint32_t g_sinner, g_sinUnit;
static double g_sinBorn;
static int g_landed, g_winnerUsed, g_piloting;
static double g_winnerNext;
static int32_t g_sinMax, g_sinHp;
static atomic_int g_sinHpNow;         /* its health for the damage event's thread (0: none standing) */
static atomic_int g_sharePending;     /* damage the share moved onto it, written on the game thread */
static atomic_int g_shareOk;          /* the share may run: landed, not piloted, near her */
static int g_callPending;
static unsigned g_shareHits, g_healthWrites;
static double g_shared;
static float g_sinPos[3];
static int g_sinPosOk;

static void SinnerAdd(const HfApi* api, HfMod* mod)
{
    if (!s_sinOn || api->size < offsetof(HfApi, add_stratagem) + sizeof api->add_stratagem || !api->add_entity || !api->add_stratagem) return;
    HfEntity sin = {sizeof sin};
    sin.base = HF_ENTITY_EXO45_PATRIOT_EXOSUIT;
    sin.name = "yui/koleda/sinner";
    sin.title = "Sinveiler";
    sin.designation = "EXO-45K";
    sin.description = "Koleda's Sinveiler: an exosuit that walks beside her, fights by its gear and takes half the damage aimed at her.";
    g_sinRes = api->add_entity(mod, &sin);
    if (!g_sinRes) { api->log(mod, "the Sinveiler could not be added (see the log above): her kit runs without it"); return; }
    static uint64_t payload[2];
    payload[0] = g_sinRes;
    payload[1] = HF_ENTITY_PELICAN_TRANSPORT;
    HfStratagem st = {sizeof st};
    st.base = HF_STRATAGEM_EXO45_PATRIOT;
    st.name = "yui/koleda/sinner_drop";
    st.title = "Sinveiler";
    st.description = "Koleda's Sinveiler, dropped by Pelican. It follows her and takes part of the damage aimed at her.";
    st.code = "RRLLD";
    st.cooldown = s_s3Cd > 1 ? s_s3Cd : 1;
    st.uses = s_callMode ? 1 : -1;
    st.payload = payload;
    st.payload_count = 2;
    st.package = 0x22749A294788AF66ull;   /* packages/generated/loadout/combat_walker (the Patriot's, as Voymastina's free Sinbreaker) */
    g_sinType = api->add_stratagem(mod, &st);
    api->log(mod, "the Sinveiler: entity %016llx, stratagem %u (RRLLD, call_mode %d)", (unsigned long long)g_sinRes, g_sinType, s_callMode);
}
/* 0.2.6: the FRV Sinveiler's own call: the M-103 Supply FRV's stratagem copied (its FRV, its drop, its package) */
__attribute__((unused)) static void FrvAdd(const HfApi* api, HfMod* mod)
{
    if (!s_sinOn || api->size < offsetof(HfApi, add_stratagem) + sizeof api->add_stratagem || !api->add_stratagem) return;
    HfStratagem st = {sizeof st};
    st.base = FRV_STRATAGEM;
    st.name = "yui/koleda/frv_sinner_drop";
    st.title = "FRV Sinveiler";
    st.description = "Koleda's Sinveiler as her Supply FRV: its turret fights by her gear, it takes part of the damage aimed at her, and she can ride it.";
    st.code = "RRLLU";
    st.cooldown = s_s3Cd > 1 ? s_s3Cd : 1;
    st.uses = s_callMode ? 1 : -1;
    g_frvType = api->add_stratagem(mod, &st);
    api->log(mod, "the FRV Sinveiler: stratagem %u (RRLLU, a copy of the M-103 Supply FRV's)", g_frvType);
}
/* 0.2.8: the FRV's package (its call-in package, 149f685717737cae) is not one HellForge's load_package knows, and an FRV
 * called without it crashed the game as it landed (game.dll+0x6f94c1, twice). HD2Runtime's catalogue knows it: Koleda's
 * HUD loads it (hd2.require_assets of the M-103 Supply FRV) and reports here (koleda_frv_package: 1 resident, -1
 * refused). Until it is resident, Spectre brings the Patriot instead (the M-103 in her loadout has it loaded) */
static atomic_int g_frvPkg;
HF_MOD_EXPORT int koleda_frv_package(unsigned version, int state)
{
    if (version != 1) return 0;
    int was = atomic_exchange(&g_frvPkg, state);
    if (was != state) Log("the FRV Sinveiler's package: %s (HD2Runtime, through her HUD)", state == 1 ? "resident" : state < 0 ? "refused" : "loading");
    return 1;
}
static int FrvReady(void) { return atomic_load(&g_frvPkg) == 1; }
/* 0.2.9: a line from her HUD, in this log (its own log lines were not found in the session's logs) */
HF_MOD_EXPORT int koleda_hud_log(unsigned version, const char* text)
{
    if (version != 1 || !text) return 0;
    static unsigned told;
    if (told++ < 40) Log("[HUD] %.300s", text);
    return 1;
}
/* the call Spectre grants: the picked Sinveiler's (the FRV's only with its package resident) */
static uint32_t CallType(void) { return s_frvOn && g_frvType && FrvReady() ? g_frvType : g_sinType; }

/* her rounds: the Sinveiler's by gear and Snow Wolf's Eye's (copies, add_projectile; the vanilla round without one) */
static uint32_t g_round[5], g_tracer;
static int g_roundBad[5];
static const char* const kRoundName[5] = {"N", "S", "S+", "Snow Wolf's Eye", "rocket"};
static void RoundsAdd(void)
{
    static char names[5][48];
    int base[5] = {s_roundN, s_roundS, s_roundPlus, s_s1Round, s_roundRocket};
    static const char* const tag[5] = {"sinner_n", "sinner_s", "sinner_plus", "snow_wolf", "sinner_rocket"};
    for (int r = 0; r < 5; r++)
    {
        if (!base[r]) { g_round[r] = 0; continue; }
        HfProjectile p;
        memset(&p, 0, sizeof p);
        p.size = sizeof p;
        p.base = (uint32_t)base[r];
        if (r == 4 && base[r] == 128)
        {
            /* 0.1.16: the Patriot's rocket row (128) is not one HellForge knows the packages of, so a copy of it cannot be
             * spawned (0.1.15's fell back to the Guard Dog's bullet). Its look and its damage on a recoilless rifle's
             * rocket (153, spawnable), its blast and speed set on the copy below (RocketRow) */
            p.base = 153;
            p.visual_from = 128;
            p.damage = 239;
        }
        snprintf(names[r], sizeof names[r], "yui/koleda/%s", tag[r]);
        p.name = names[r];
        g_round[r] = hf->add_projectile ? hf->add_projectile(self, &p) : 0;
        if (!g_round[r]) { g_round[r] = p.base; Log("the %s round: no copy of projectile %u (the vanilla round fires)", kRoundName[r], p.base); }
    }
    /* 0.3.12: Full Speed's tracer: Snow Wolf's Eye's round (its trail) with a weak damage id */
    if (s_s1Round && hf->add_projectile)
    {
        HfProjectile p;
        memset(&p, 0, sizeof p);
        p.size = sizeof p;
        p.base = (uint32_t)s_s1Round;
        p.damage = (uint32_t)s_tracerDamage;
        p.name = "yui/koleda/sinner_tracer";
        g_tracer = hf->add_projectile(self, &p);
        if (!g_tracer) Log("Full Speed's tracer: no copy of projectile %d (no tracers)", s_s1Round);
    }
}

/* ---- the call: one entry in her own stratagem record (Voymastina's grant; the record's layout and the game's append
 * are HD2Runtime's research, docs/research/bonus-stratagem.md) ------------------------------------------------------ */
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
        if (!hf->code_matches(game + k[i].at, k[i].bytes)) { Log("Sinveiler call: game.dll+0x%llX is not as researched: no call", (unsigned long long)k[i].at); ok = 0; break; }
    return ok;
}
/* the Sinveiler's call in her list with `uses` (-1 unlimited): refilled if there, else appended. 1 done; 0 not (logged),
 * -1 not yet (her record unreadable) */
static int GrantCall(uint32_t type, int uses)
{
    if (!type || !GrantPins()) return 0;
    uintptr_t st = LocalStratagems(), clockObj = Pointer(game + 0x3326348u);
    uint32_t count = 0;
    uint64_t now = 0, zero = 0;
    if (!st || !ReadAt(st + 0x788, &count, 4) || count > 16 || !clockObj || !ReadAt(clockObj + 0x18, &now, 8)) return -1;
    uint32_t u = uses < 0 ? 0xFFFFFFFFu : (uint32_t)uses;
    int tmpl = -1;
    for (uint32_t i = 0; i < count; i++)
    {
        uint8_t e[0x30];
        uintptr_t at = st + 0x188 + i * 0x30u;
        if (!ReadAt(at, e, sizeof e)) return -1;
        if (Word(e) == type)
        {
            /* refilled: uses, and the cooldown fields the append zeroes (untested: whether a used-up entry comes back) */
            uint32_t was = Word(e + 4);
            if (hf->write(at + 4, &u, 4) || hf->write(at + 0x10, &zero, 8) || hf->write(at + 0x18, &zero, 8) || hf->write(at + 0x20, &zero, 8))
            { Log("Sinveiler call: the refill write failed"); return 0; }
            uint32_t back = 0;
            ReadAt(at + 4, &back, 4);
            Log("Sinveiler call: refilled in her stratagems (slot %u: uses %u -> %u, reads back %u)", i, was, u, back);
            return 1;
        }
        if (tmpl < 0 && e[9] == 1 && Quad(e + 0x20) <= now) tmpl = (int)i;
    }
    if (count >= 16) { Log("Sinveiler call: her stratagem list is full (16)"); return 0; }
    uint8_t e[0x30] = {0};
    if (tmpl >= 0) ReadAt(st + 0x188 + (uint32_t)tmpl * 0x30u, e, sizeof e);
    memcpy(e, &type, 4);
    memcpy(e + 4, &u, 4);
    e[9] = 1;
    memcpy(e + 0x10, &zero, 8);
    memcpy(e + 0x18, &zero, 8);
    memcpy(e + 0x20, &zero, 8);
    uint32_t next = count + 1;
    if (hf->write(st + 0x188 + count * 0x30u, e, sizeof e) || hf->write(st + 0x788, &next, 4)) { Log("Sinveiler call: the write failed"); return 0; }
    Log("Sinveiler call: granted outside the loadout (slot %u of %u, template slot %d, uses %s): call it with %s", count, next, tmpl,
        uses < 0 ? "unlimited" : "1", type == g_frvType ? "RRLLU (the FRV Sinveiler)" : "RRLLD");
    return 1;
}
/* 0.2.0: is a stratagem in her own list (the M-103 in her loadout); its entry's cooldown cleared (Spectre with
 * the FRV). 1 found / done, 0 not there, -1 her record unreadable */
static int HasStratagem(uint32_t type)
{
    if (!GrantPins()) return 0;
    uintptr_t st = LocalStratagems();
    uint32_t count = 0;
    if (!st || !ReadAt(st + 0x788, &count, 4) || count > 16) return -1;
    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t t = 0;
        if (ReadAt(st + 0x188 + i * 0x30u, &t, 4) && t == type) return 1;
    }
    return 0;
}
static int ClearCooldown(uint32_t type)
{
    if (!GrantPins()) return 0;
    uintptr_t st = LocalStratagems();
    uint32_t count = 0;
    uint64_t zero = 0;
    if (!st || !ReadAt(st + 0x788, &count, 4) || count > 16) return -1;
    for (uint32_t i = 0; i < count; i++)
    {
        uintptr_t at = st + 0x188 + i * 0x30u;
        uint32_t t = 0;
        if (!ReadAt(at, &t, 4) || t != type) continue;
        if (hf->write(at + 0x10, &zero, 8) || hf->write(at + 0x18, &zero, 8) || hf->write(at + 0x20, &zero, 8)) { Log("the FRV call: the cooldown write failed"); return 0; }
        return 1;
    }
    return 0;
}
/* call_mode 0: once at the drop, unlimited (the Sinbreaker's way) */
static int g_grantDone, g_grantTries;
static double g_grantAt;
static void GrantFrame(void)
{
    if (s_callMode || !CallType() || g_grantDone || !g_alive) return;
    if (g_grantAt <= 0) { g_grantAt = g_now + 3; return; }
    if (g_now < g_grantAt) return;
    g_grantAt = g_now + 1;
    if (++g_grantTries > 20) { g_grantDone = 1; Log("Sinveiler call: her stratagem record never became readable"); return; }
    int r = GrantCall(CallType(), -1);
    if (r >= 0) g_grantDone = 1;
}
/* the call's package, held for the session (a call outside the loadout lands as a purple question mark without it) */
static int g_pkgStates[2];
static void SinPackageOf(int k, uint32_t type)
{
    int* state = &g_pkgStates[k];
    const char* who = k ? "the FRV Sinveiler" : "Sinveiler";
    if (*state == 2 || !type || !hf->stratagem_row || hf->size < offsetof(HfApi, load_package) + sizeof hf->load_package) return;
    uintptr_t row = hf->stratagem_row(type);
    uint64_t pkg = 0;
    if (!row || !ReadAt(row + 0xA8, &pkg, 8) || !pkg || pkg == ~0ull) { if (!*state) { *state = 2; Log("%s: its stratagem names no package", who); } return; }
    int r = hf->load_package(self, pkg);
    if (r == HF_PACKAGE_RESIDENT) { *state = 2; Log("%s: its package %016llx is loaded", who, (unsigned long long)pkg); }
    else if (!r) { *state = 2; Log("%s: its package %016llx could not be held: a call without it in the loadout may fail", who, (unsigned long long)pkg); }
    else *state = 1;
}
static void SinPackage(void)
{
    static double next;
    if (g_now < next) return;
    next = g_now + 2;
    SinPackageOf(0, g_sinType);
}

/* ---- is she piloting it (the seat: Khi's reading of the command bus game+0x3326E68, entry 117, as Voymastina) ----- */
static uint32_t SeatVehicle(void)
{
    uintptr_t bus = Pointer(game + 0x3326E68), manager = 0, seaters, entries;
    uint32_t count = 0, me = atomic_load(&g_avatar);
    if (!bus || !me || !ReadAt(bus + 7948, &count, 4) || count > 512) return 0;
    for (uint32_t i = 0; i < count && !manager; i++)
    {
        uint32_t type = 0;
        if (ReadAt(bus + 7952 + i * 16ull + 8, &type, 4) && type == 117) manager = Pointer(bus + 7952 + i * 16ull);
    }
    if (!manager || !ReadAt(manager + 12, &count, 4) || count > 4096 || !(seaters = Pointer(manager + 56)) || !(entries = Pointer(manager + 72))) return 0;
    for (uint32_t i = 0; i < count; i++)
    {
        uintptr_t object = Pointer(seaters + i * 8ull);
        uint32_t flags = 0, avatar = 0, vehicle = 0;
        if (!object || !ReadAt(object + 20, &flags, 4) || !(flags & 1) || !ReadAt(object + 8, &avatar, 4) || avatar != me) continue;
        return ReadAt(entries + i * 64ull, &vehicle, 4) ? vehicle : 0;
    }
    return 0;
}

/* ---- its health: read, and written (the share, The Winner, Kinetic Recovery) --------------------------------------- */
static int SinnerMaxRead(void)
{
    HfField f = HF_FIELD(Health, health);
    HfLiveValue v;
    memset(&v, 0, sizeof v);
    uint64_t res = SinRes();
    if (res && hf->get_live(self, res, &f, &v) && v.base.number >= 1 && v.base.number < 1e7) return (int)v.base.number;
    uint32_t stride = 0;
    uintptr_t h = res ? hf->component_record("HealthComponentData", res, &stride) : 0;
    int32_t hp = 0;
    if (h && ReadAt(h, &hp, 4) && hp >= 1 && hp < 10000000) return hp;
    return 0;
}
/* its health set to want (clamped to 1 .. its maximum): only while it stands; read back. 1 written */
static int SinnerSetHealth(int32_t want, const char* why)
{
    uintptr_t r = HealthRec(g_sinner);
    uint8_t b[416];
    if (!r || !ReadAt(r, b, sizeof b)) { Log("%s: the Sinveiler's health record cannot be read", why); return 0; }
    int32_t hp = (int32_t)Word(b + 20);
    uint32_t life = Word(b + 412);
    if (hp <= 0 || life != 0) { Log("%s: the Sinveiler is down (health %d, life %u): not written", why, hp, life); return 0; }
    int32_t top = g_sinMax > 0 ? g_sinMax : hp;
    if (want > top) want = top;
    if (want < 1) want = 1;
    if (want == hp) return 1;
    /* 0.4.4: a heal goes through the game's own heal (SDK 16: "AddHealthFraction", to its health and every part).
     * Written into the record alone, a heal lasted until the FRV's next hit: the game then put back what it had
     * before, less the hit (the log of 2026-11-22: 1359 -> 1959 written, 1340 read 7 s later), so the HUD showed
     * health it did not have. The write stays for a HellForge without the function, and for the damage share. */
    if (want > hp && top > 0)
    {
#if defined(HF_MOD_SDK_VERSION) && HF_MOD_SDK_VERSION >= 16
        static void (*heal)(void* manager, uint32_t entity, float fraction);
        static unsigned asked;
        if (!heal && asked < 4 && hf->size >= offsetof(HfApi, function) + sizeof hf->function)
        {
            asked++;
            heal = (void (*)(void*, uint32_t, float))hf->function(self, "AddHealthFraction");
        }
        uintptr_t m = Pointer(game + 53634696u);
        if (heal && m)
        {
            heal((void*)m, g_sinner, (float)(want - hp) / (float)top);
            int32_t now = 0;
            ReadAt(r + 20, &now, 4);
            if (now > hp)
            {
                g_healthWrites++;
                Log("%s: the Sinveiler healed by the game's own heal (its health and every part): %d -> %d of %d (asked for %d)", why, hp, now, top, want);
                return 1;
            }
            Log("%s: the game's own heal left the Sinveiler at %d (asked for %d): written instead", why, now, want);
        }
#endif
    }
    if (hf->write(r + 20, &want, 4)) { Log("%s: the Sinveiler's health write failed", why); return 0; }
    int32_t back = 0;
    ReadAt(r + 20, &back, 4);
    g_healthWrites++;
    static unsigned told;
    if (told++ < 16 || strcmp(why, "the damage share")) Log("%s: the Sinveiler's health %d -> %d of %d (reads back %d)", why, hp, want, top, back);
    g_sinHp = back;
    atomic_store(&g_sinHpNow, back);
    return back == want;
}

/* ---- The Winner's cleanse: the game's status has / remove (static research on branch alva, game-notes.md) ----------- */
typedef uint8_t (*HasStatusFn)(void*, uint32_t, uint32_t, uint32_t);
typedef void (*RemoveStatusFn)(void*, uint32_t, uint32_t);
static HasStatusFn g_hasStatus;
static RemoveStatusFn g_removeStatus;
/* their first bytes, as reviewed: not known yet (the log prints them at load); NULL: not pinned */
static const char* const kHasPin = "48 89 5c 24 10 48 89 6c 24 20 56 57 41 54 41 56 41 57 48 83";   /* game+0x69B1F0, from the build 25480438 dump */
static const char* const kRemovePin = "48 89 5c 24 08 48 89 6c 24 18 48 89 74 24 20 57 41 56 41 57";   /* game+0x69AEC0 */
static const uint32_t kDebuffs[] = {5, 6, 32, 37, 38, 39, 42, 43, 44, 45, 67};
static void StatusPrepare(void)
{
    uint8_t a[32], b[32];
    char ha[100], hb[100];
    int okA = hf->size >= offsetof(HfApi, read_code) + sizeof hf->read_code && hf->read_code && !hf->read_code(game + 0x69B1F0, a, 32);
    int okB = okA && !hf->read_code(game + 0x69AEC0, b, 32);
    for (int i = 0; i < 32; i++) { snprintf(ha + 3 * i, 4, "%02x ", okA ? a[i] : 0); snprintf(hb + 3 * i, 4, "%02x ", okB ? b[i] : 0); }
    Log("status functions, first bytes (for the cleanse's pins): has game.dll+0x69B1F0: %s; remove game.dll+0x69AEC0: %s", okA ? ha : "unreadable", okB ? hb : "unreadable");
    if (!s_cleanse) { Log("The Winner's cleanse off (cleanse 0)"); return; }
    if (!hf->code_matches(game + 0x699E40, "40 55 53 57 41 55 41 57 48 8d 6c 24 d0 48 81 ec 30 01 00 00"))
    { Log("The Winner's cleanse off: the status add (game.dll+0x699E40) is not the reviewed code, so this build's status functions are unknown"); return; }
    int pinned = kHasPin && kRemovePin && hf->code_matches(game + 0x69B1F0, kHasPin) && hf->code_matches(game + 0x69AEC0, kRemovePin);
    if (!pinned && s_cleanse != 2) { Log("The Winner's cleanse off: the status has / remove functions are not pinned yet (cleanse 2 calls them unpinned)"); return; }
    g_hasStatus = (HasStatusFn)(game + 0x69B1F0);
    g_removeStatus = (RemoveStatusFn)(game + 0x69AEC0);
    Log("The Winner's cleanse ready (%s)", pinned ? "pinned" : "UNPINNED: cleanse 2");
}
/* the GAME THREAD only */
static int Cleanse(uint32_t e)
{
    void* manager = (void*)Pointer(game + 0x3326620);
    if (!g_hasStatus || !g_removeStatus || !manager || !e) return -1;
    int removed = 0;
    char list[96] = "";
    size_t k = 0;
    for (unsigned i = 0; i < sizeof kDebuffs / sizeof kDebuffs[0]; i++)
    {
        if (!g_hasStatus(manager, e, kDebuffs[i], 0)) continue;
        g_removeStatus(manager, e, kDebuffs[i]);
        int still = g_hasStatus(manager, e, kDebuffs[i], 0) != 0;
        removed++;
        if (k < sizeof list - 8) k += (size_t)snprintf(list + k, sizeof list - k, "%u%s ", kDebuffs[i], still ? "(still on)" : "");
    }
    Log("The Winner: cleanse on entity %u: %d statuses removed %s", e, removed, list);
    return removed;
}

/* ---- the Sinveiler's life: adopted at its spawn, landed, lost -------------------------------------------------------- */
static uint32_t g_parts[8];               /* its arms' units (spawned with it): its rays see through them */
static int g_nparts;
typedef struct { uint32_t entity; double at; } Spawned;
static Spawned g_spawned[64];
static unsigned g_nspawned;
static int g_driving, g_driveTried;
static void SinnerLost(const char* why)
{
    if (!g_sinner) return;
    Log("the Sinveiler (entity %u) is gone: %s", g_sinner, why);
    if (g_driving) SinnerDriveStop(g_sinner);
    g_sinner = g_sinUnit = 0;
    g_landed = g_winnerUsed = g_piloting = 0;
    g_driving = g_driveTried = 0;
    g_frv = 0;
    g_frvNet = g_turretNet = g_turretEnt = 0;
    g_frvParked = 0;
    g_rideWanted = 0;
    g_nparts = 0;
    g_sinPosOk = 0;
    atomic_store(&g_sinHpNow, 0);
    atomic_store(&g_sharePending, 0);
    atomic_store(&g_shareOk, 0);
    Gear(GEAR_D, "the Sinveiler is gone");
}
static int SinPos(float p[3])
{
    return g_sinUnit && UnitNode(g_sinUnit, 0, p);
}
/* a ray that sees through the Sinveiler, her and its arms (and enemies: no ground or wall), as Voymastina's MechRay; only
 * while the scheduler is idle. len + 1 clear, -1 not now */
static float SinRay(const float from[3], const float dir[3], float len)
{
    uintptr_t world = Pointer(game + 0x346BFA0);
    if (!world || !g_raycast || !g_worldId || !QueryIdle()) return -1;
    uint32_t id = g_worldId((const void*)world), pilot = UnitOf(atomic_load(&g_avatar));
    if (id >= 4) return -1;
    uint8_t hits[16 * 44];
    memset(hits, 0, sizeof hits);
    uint32_t n = g_raycast(id, from, dir, len, 2, 1, 0, 0x80000009u, g_sinUnit, hits, 16);
    if (n > 16) return -1;
    float best = len + 1;
    for (uint32_t i = 0; i < n; i++)
    {
        float t;
        memcpy(&t, hits + 44u * i + 24, 4);
        uint32_t u = Word(hits + 44u * i + 28);
        int own = (u && (u == g_sinUnit || u == pilot)) || (u && FoeUnit(u));
        for (int k = 0; k < g_nparts && !own; k++) own = u == g_parts[k];
        if (own || !isfinite(t) || t < 0.3f) continue;
        if (t < best) best = t;
    }
    return best;
}
static void Adopt(uint32_t e)
{
    if (g_sinner) SinnerLost("a new one came");
    uint8_t d[24];
    g_sinner = e;
    g_sinUnit = Descriptor(e, d) ? Word(d + 12) : 0;
    g_sinBorn = g_now;
    g_landed = g_winnerUsed = 0;
    g_winnerNext = 0;
    g_sinMax = 0;
    g_callPending = 0;
    Gear(GEAR_D, "a new Sinveiler");
    Log("the Sinveiler: entity %u (unit %08x) spawned: waiting for it to land", e, g_sinUnit);
}
/* landed: at least 5 s after its spawn, still for 1.5 s, on the ground (within 2.5 m under it) */
static void ArrivalFrame(void)
{
    static double next, stillSince;
    static float last[3];
    if (!g_sinner || g_landed || g_now < next) return;
    next = g_now + 0.25;
    float p[3];
    if (!g_sinUnit) { uint8_t d[24]; if (Descriptor(g_sinner, d)) g_sinUnit = Word(d + 12); }
    if (!SinPos(p)) return;
    if (Dist2(p, last) > 0.3f * 0.3f) { memcpy(last, p, 12); stillSince = g_now; return; }
    if (g_now - g_sinBorn < 5 || g_now - stillSince < 1.5) return;
    float top[3] = {p[0], p[1], p[2] + 1.0f}, down[3] = {0, 0, -1};
    float g = SinRay(top, down, 3.5f);
    if (g < 0 || g > 3.5f) return;
    g_landed = 1;
    /* its arms: what spawned within 3 s of it */
    g_nparts = 0;
    for (unsigned i = 0; i < 64 && g_nparts < 8; i++)
    {
        uint8_t d[24];
        if (!g_spawned[i].entity || g_spawned[i].entity == g_sinner || fabs(g_spawned[i].at - g_sinBorn) > 3.0 || !Descriptor(g_spawned[i].entity, d)) continue;
        uint32_t by = 0;
        if (Friend(g_spawned[i].entity) || (WielderOf(g_spawned[i].entity, &by) && by == g_sinner)) g_parts[g_nparts++] = Word(d + 12);
    }
    int32_t hp = 0;
    uint32_t life = 0;
    HealthOf(g_sinner, &hp, &life);
    g_sinMax = SinnerMaxRead();
    if (g_sinMax < hp) g_sinMax = hp;
    g_sinHp = hp;
    atomic_store(&g_sinHpNow, hp);
    const char* why;
    int movable = Movable(g_sinner, &why);
    Log("the Sinveiler landed (entity %u): health %d of %d (life %u), %d parts, navigation record %s, warp %s%s", g_sinner, hp, g_sinMax, life, g_nparts,
        Walks(g_sinner) ? "yes" : "none", movable ? "allowed" : "refused: ", movable ? "" : why);
}

/* ---- the leash: where it goes, and how ------------------------------------------------------------------------- */
static int g_moving, g_leash;            /* g_leash for the HUD: 0 idle, 1 following, 2 warped lately, 3 cannot move, 4 pushed through */
static double g_warpAt = -100, g_sampleAt, g_slowFor;
static float g_sample[3];
static int g_pushing, g_pushSteps;
static unsigned g_pushes;
static unsigned g_slides, g_moveRefused;
static void MoveRefused(const char* why)
{
    g_leash = 3;
    static unsigned told;
    if (g_moveRefused++ == 0 || told < 3) { told++; Log("the Sinveiler cannot be moved: %s (it stands and fights where it is)", why); }
}
/* a point on the ground near p (from 2 m above, down 6): 1 with out */
static int Ground(const float p[3], float out[3])
{
    float top[3] = {p[0], p[1], p[2] + 2.0f}, down[3] = {0, 0, -1};
    float g = SinRay(top, down, 6.0f);
    if (g < 0 || g > 6.0f) return 0;
    out[0] = p[0]; out[1] = p[1]; out[2] = top[2] - g;
    return 1;
}
static void WarpBeside(const char* why)
{
    float me[3], aim[3], dest[3];
    const char* no;
    if (g_now - g_warpAt < 2) return;
    g_warpAt = g_now;
    if (!Movable(g_sinner, &no)) { MoveRefused(no); return; }
    if (!PlayerPose(me, aim)) return;
    float h = sqrtf(aim[0] * aim[0] + aim[1] * aim[1]);
    float f[2] = {h > 0.05f ? aim[0] / h : 1, h > 0.05f ? aim[1] / h : 0};
    float spot[3] = {me[0] + f[1] * 3.0f - f[0] * 1.0f, me[1] - f[0] * 3.0f - f[1] * 1.0f, me[2]};
    if (!Ground(spot, dest)) return;
    if (fabsf(dest[2] - me[2]) > 3) return;   /* a ledge or a roof: next time */
    static const float zero[3] = {0, 0, 0};
    g_setPosition(NULL, g_sinner, dest);
    if (g_setVelocity) g_setVelocity(NULL, g_sinner, zero);
    g_warps++;
    g_leash = 2;
    memcpy(g_sample, dest, 12);
    g_sampleAt = g_now;
    g_slowFor = 0;
    g_pushing = 0;
    static unsigned told;
    if (told++ < 10) Log("the Sinveiler is warped beside her (%s)", why);
}
/* a step toward the point along the ground (the warp setter: a slide, no leg animation), stopping at walls and steps up */
static int Slide(const float p[3], const float to[3], double dt)
{
    const char* no;
    if (!Movable(g_sinner, &no)) { MoveRefused(no); return 0; }
    if (dt <= 0 || dt > 0.1) return 0;
    float v[2] = {to[0] - p[0], to[1] - p[1]}, h = sqrtf(v[0] * v[0] + v[1] * v[1]);
    if (h < 0.2f) return 0;
    float step = s_slideSpeed * (float)dt;
    if (step > h) step = h;
    float dir[3] = {v[0] / h, v[1] / h, 0};
    for (int k = 0; k < 2; k++)
    {
        float from[3] = {p[0], p[1], p[2] + (k ? 2.4f : 1.0f)};
        float w = SinRay(from, dir, step + 1.5f);
        if (w < 0 || w <= step + 1.5f) return 0;
    }
    float dest[3] = {p[0] + dir[0] * step, p[1] + dir[1] * step, p[2]}, g[3];
    if (!Ground(dest, g) || g[2] - p[2] > 0.5f || p[2] - g[2] > 3.0f) return 0;
    g_setPosition(NULL, g_sinner, g);
    g_moveDir[0] = dir[0]; g_moveDir[1] = dir[1];
    if (g_slides++ == 0) Log("the Sinveiler slides (the warp setter; its own locomotion is not driven yet)");
    return 1;
}
/* stuck: walking, but its own speed (pushes left out) under 0.5 m/s for stuck_seconds: pushed through the obstacle a
 * step of push_step m at a time along its way (toward the next waypoint, or her), each step put on the ground there
 * (skipped with no ground within 6 m, or a drop or climb over 2.5 m), until it moves on its own again (Voymastina's
 * early mech dash went through walls this way). The far warp stays the last resort */
static void StuckFrame(const float p[3], const float to[3])
{
    double el = g_now - g_sampleAt;
    if (el < 0.4) return;
    float sp = Flat(p, g_sample) / (float)el;
    memcpy(g_sample, p, 12);
    g_sampleAt = g_now;
    if (sp >= 0.5f)
    {
        if (g_pushing) { static unsigned told; if (told++ < 10) Log("the Sinveiler moves again (%.1f m/s) after %d push steps", (double)sp, g_pushSteps); g_leash = 1; }
        g_pushing = 0; g_slowFor = 0;
        return;
    }
    g_slowFor += el;
    if (!g_pushing)
    {
        if (g_slowFor < s_stuckSeconds || s_pushSteps <= 0) return;
        g_pushing = 1; g_pushSteps = 0;
        static unsigned told;
        if (told++ < 10) Log("the Sinveiler is stuck (%.1f m/s for %.1f s): pushed through", (double)sp, g_slowFor);
    }
    if (g_pushSteps >= s_pushSteps)
    {
        static unsigned told;
        if (told++ < 6) Log("the Sinveiler: %d push steps did not free it (the far warp at %.0f m stays)", g_pushSteps, (double)s_leashWarp);
        g_pushing = 0; g_slowFor = 0;
        if (g_leash == 4) g_leash = 1;
        return;
    }
    const char* no;
    if (!Movable(g_sinner, &no)) { MoveRefused(no); g_pushing = 0; return; }
    float v[2] = {to[0] - p[0], to[1] - p[1]}, h = sqrtf(v[0] * v[0] + v[1] * v[1]);
    if (h < 0.3f) return;
    float step = s_pushStep < h ? s_pushStep : h;
    float dest[3] = {p[0] + v[0] / h * step, p[1] + v[1] / h * step, p[2]}, g[3];
    g_pushSteps++;   /* (a skipped step counts too: no endless tries at a cliff) */
    if (!Ground(dest, g) || fabsf(g[2] - p[2]) > 2.5f)
    {
        static unsigned told;
        if (told++ < 6) Log("the Sinveiler: a push step skipped (no ground within 6 m there, or a drop or climb)");
        return;
    }
    g_setPosition(NULL, g_sinner, g);
    g_pushes++;
    g_leash = 4;
    memcpy(g_sample, g, 12);   /* its own speed is measured from here */
    g_sampleAt = g_now;
}
static uint32_t PlusTarget(float at[3])
{
    uint32_t t = g_plusTarget;
    int k = FoeIndex(t);
    if (k < 0 || !Alive(t) || !TargetPos(t, g_foeRes[k], at))
    {
        t = g_lastTarget && g_now - g_lastAt < 10 ? g_lastTarget : 0;
        k = FoeIndex(t);
        if (k < 0 || !Alive(t) || !TargetPos(t, g_foeRes[k], at)) return 0;
    }
    return t;
}
static void MoveFrame(double dt)
{
    float me[3], p[3], aim[3];
    if (!g_sinner || !g_landed || g_piloting || g_frv) { if (g_driving && g_sinner) SinnerDriveStick(g_sinner, 0, 0, 0); g_moving = 0; g_walkWant = 0; return; }
    if (!PlayerPose(me, aim) || !SinPos(p)) return;
    if (g_leash == 2 && g_now - g_warpAt > 1.5) g_leash = 0;
    float dHer = Flat(p, me);
    if (dHer > s_leashWarp) { WarpBeside("too far"); return; }
    /* where it goes: beside her (N, S); short of her target (S+) */
    float to[3], start = g_gear == GEAR_N ? s_guardStart : s_leashStart, stop = s_leashStop < start ? s_leashStop : start * 0.6f, tp[3];
    float h = sqrtf(aim[0] * aim[0] + aim[1] * aim[1]);
    float f[2] = {h > 0.05f ? aim[0] / h : 1, h > 0.05f ? aim[1] / h : 0};
    /* 0.1.14: a ring around her, not a point that swung with her aim (it kept shuffling): it stays put while it is within
     * 3 m of follow_distance, else it walks to the nearest point at follow_distance */
    if (dHer > 0.3f) { to[0] = me[0] + (p[0] - me[0]) / dHer * s_followDist; to[1] = me[1] + (p[1] - me[1]) / dHer * s_followDist; }
    else { to[0] = me[0] - f[0] * s_followDist; to[1] = me[1] - f[1] * s_followDist; }
    to[2] = me[2];
    start = 3; stop = 0.75f;
    if (g_gear == GEAR_PLUS && PlusTarget(tp))
    {
        float v[2] = {tp[0] - me[0], tp[1] - me[1]}, len = sqrtf(v[0] * v[0] + v[1] * v[1]);
        float reach = len - s_plusDistance;
        if (reach > s_plusLeash) reach = s_plusLeash;
        if (reach > 0 && len > 0.5f) { to[0] = me[0] + v[0] / len * reach; to[1] = me[1] + v[1] / len * reach; to[2] = me[2]; start = 4; stop = 2; }
    }
    /* too close to her: away from her first, out to follow_distance */
    if (dHer < s_personal)
    {
        float a[2] = {p[0] - me[0], p[1] - me[1]};
        if (dHer < 0.3f) { a[0] = -f[0]; a[1] = -f[1]; dHer = 1; }
        to[0] = me[0] + a[0] / dHer * s_followDist; to[1] = me[1] + a[1] / dHer * s_followDist; to[2] = me[2];
        start = 0.5f; stop = 1.5f;
    }
    float dTo = Flat(p, to);
    if (!g_moving && dTo > start) { g_moving = 1; memcpy(g_sample, p, 12); g_sampleAt = g_now; g_slowFor = 0; g_pushing = 0; }
    else if (g_moving && dTo < stop) g_moving = 0;
    if (!g_moving)
    {
        g_walkWant = 0;
        if (g_driving) SinnerDriveStick(g_sinner, 0, 0, 0);
        if (g_leash == 1 || g_leash == 4) g_leash = 0;
        g_pushing = 0;
        return;
    }
    if (g_leash != 3 && g_leash != 2 && g_leash != 4) g_leash = 1;
    float goal[3];
    memcpy(goal, to, 12);
    NavSteer(g_sinner, p, goal, to);   /* the navmesh's next waypoint, when it has a path; else straight */
    if (g_driving)
    {
        float m[16], v[2] = {to[0] - p[0], to[1] - p[1]}, len = sqrtf(v[0] * v[0] + v[1] * v[1]);
        if (len > 0.1f && UnitPose(g_sinUnit, 0, m))
        {
            float fh = sqrtf(m[4] * m[4] + m[5] * m[5]);
            float fw[2] = {fh > 0.05f ? m[4] / fh : 1, fh > 0.05f ? m[5] / fh : 0}, rt[2] = {fw[1], -fw[0]};
            float ahead = (v[0] * fw[0] + v[1] * fw[1]) / len, side = (v[0] * rt[0] + v[1] * rt[1]) / len;
            SinnerDriveStick(g_sinner, ahead > 0 ? ahead : 0, 0, side * 2);
        }
    }
    else if (g_now - g_walkAckAt > 0.6) Slide(p, to, dt);   /* 0.1.7: the slide only while the HUD side is not walking it */
    memcpy(g_walkGoal, to, 12);
    g_walkWant = 1;
    StuckFrame(p, to);
}

/* ---- its fire: rounds of hers from its shoulders (Exusiai's summoned guns' way), by gear ------------------------- */
#define SHOT_SLOTS 48
static float g_shotAim[SHOT_SLOTS][3];
static double g_shotAimAt[SHOT_SLOTS];
/* 0.1.11: where its rounds start and where they hit, to measure the gap (the first metres where a spawned round neither
 * shows nor hits: the Gatling Sentry's own rounds of this type show from the muzzle) */
static float g_shotFrom[32][3];
static double g_shotFromAt[32];
static unsigned g_shotFromNext, g_gapHits, g_gapBins[5];
static float g_gapMin = 1e9f;
static unsigned g_shotAimNext;
static double g_fireNext;
static int g_arm;
static int SinnerShotNear(const float p[3])
{
    for (int k = 0; k < SHOT_SLOTS; k++) if (g_shotAimAt[k] > 0 && g_now - g_shotAimAt[k] < 2.0 && Dist2(p, g_shotAim[k]) < 3.0f * 3.0f) return 1;
    return 0;
}
static __attribute__((unused)) uint32_t Nearest(const float c[3], float range, float at[3])
{
    uint32_t best = 0;
    float bd = range * range, q[3];
    for (int k = 0; k < FOES; k++)
    {
        float d2;
        if (!g_foe[k] || !TargetPos(g_foe[k], g_foeRes[k], q) || (d2 = Dist2(q, c)) >= bd || !Alive(g_foe[k])) continue;
        bd = d2; best = g_foe[k]; memcpy(at, q, 12);
    }
    return best;
}
/* 0.1.6: a clear shot from its shoulders to a point (no wall between; bodies do not count) */
static int SinClear(const float p[3], const float q[3], uint32_t target)
{
    uint32_t id;
    if (!s_sinSight || !WorldId(&id) || !QueryIdle()) return 1;
    float from[3] = {p[0], p[1], p[2] + 2.4f}, d[3] = {q[0] - from[0], q[1] - from[1], q[2] - from[2]};
    float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(n > 3.0f)) return 1;
    for (int k = 0; k < 3; k++) d[k] /= n;
    int hit = 0;
    float w = FirstWall(id, from, d, n, UnitOf(atomic_load(&g_avatar)), UnitOf(target), g_sinUnit, &hit);
    return hit || w > n - 1.5f;
}
/* the nearest enemies to c within range, nearest first, the first (of 5 tried) it has a clear shot at */
static uint32_t NearestClear(const float c[3], float range, const float p[3], float at[3])
{
    struct { uint32_t e; float d2, q[3]; } b[5];
    int nb = 0;
    float q[3];
    for (int k = 0; k < FOES; k++)
    {
        float d2;
        if (!g_foe[k] || !TargetPos(g_foe[k], g_foeRes[k], q) || (d2 = Dist2(q, c)) >= range * range || !Alive(g_foe[k])) continue;
        int j;
        if (nb == 5) { if (d2 >= b[4].d2) continue; j = 4; }
        else j = nb++;
        while (j > 0 && b[j - 1].d2 > d2) { b[j] = b[j - 1]; j--; }
        b[j].e = g_foe[k]; b[j].d2 = d2; memcpy(b[j].q, q, 12);
    }
    for (int j = 0; j < nb; j++) if (SinClear(p, b[j].q, b[j].e)) { memcpy(at, b[j].q, 12); return b[j].e; }
    return 0;
}
static uint32_t FireTarget(const float p[3], float at[3])
{
    float me[3];
    if (!PlayerPose(me, NULL)) return 0;
    if (g_gear == GEAR_N)
    {
        uint32_t t = atomic_load(&g_threat);
        int k = FoeIndex(t);
        if (t && GetTickCount64() - atomic_load(&g_threatMs) < 5000 && k >= 0 && Alive(t) && TargetPos(t, g_foeRes[k], at) && Dist2(at, p) < s_fireRange * s_fireRange &&
            SinClear(p, at, t)) return t;
        return NearestClear(me, s_guardRange, p, at);
    }
    if (g_gear == GEAR_PLUS)
    {
        uint32_t t = PlusTarget(at);
        if (t && Dist2(at, p) < (s_fireRange * 1.5f) * (s_fireRange * 1.5f) && SinClear(p, at, t)) return t;
    }
    return NearestClear(p, s_fireRange, p, at);   /* (0.1.6: nearest first, with a clear shot: not one behind a wall) */
}
/* 0.1.2: a gear's gun profile: the vanilla gun its round comes from (fire rate, pellets) */
typedef struct GunProfile { uint32_t round; float every; uint32_t pellets; } GunProfile;
static GunProfile g_prof[4];
static const struct { uint32_t round; uint64_t gun; } kRoundGun[] = {
    {148, UINT64_C(0x08F6089289C83D22)}, {128, UINT64_C(0x824B7E0C4C879EB5)},   /* the Patriot's right arm (minigun), left (rockets) */
    {241, HF_ENTITY_R63CS_DILIGENCE_COUNTER_SNIPER}, {276, HF_ENTITY_AR23_LIBERATOR}, {177, HF_ENTITY_JAR5_DOMINATOR},
    {226, HF_ENTITY_APW1_ANTIMATERIEL_RIFLE}, {179, HF_ENTITY_SG225_BREAKER}};
static const GunProfile* Profile(int gear)
{
    int base[4] = {s_roundN, s_roundS, s_roundPlus, s_roundRocket};   /* 3: the rockets */
    GunProfile* g = &g_prof[gear];
    if (g->round == (uint32_t)base[gear] && g->every > 0) return g;
    g->round = (uint32_t)base[gear];
    g->every = 0.5f;
    g->pellets = 1;
    for (unsigned i = 0; i < sizeof kRoundGun / sizeof kRoundGun[0]; i++)
    {
        if (kRoundGun[i].round != g->round) continue;
        HfField rate = {HfProjectileWeapon_INFO, 0x08, 4, HF_TYPE_F32, __LINE__};
        HfLiveValue v;
        memset(&v, 0, sizeof v);
        if (hf->get_live && hf->get_live(self, kRoundGun[i].gun, &rate, &v) && v.base.number > 0) g->every = (float)(60.0 / v.base.number);
    }
    HfField pel = HF_FIELD(ProjectileSettings, pellet_count);
    HfLiveValue v;
    memset(&v, 0, sizeof v);
    if (hf->get_live && hf->get_live(self, (HfRef)g->round, &pel, &v) && v.base.number >= 1 && v.base.number <= 32) g->pellets = (uint32_t)v.base.number;
    if (g->every < 0.05f) g->every = 0.05f;
    if (gear == 3)
    {
        /* 0.1.15: what the rocket arm really fires: a projectile entity per shot (a missile), or the row itself */
        HfField pe = HF_FIELD(ProjectileWeapon, projectile_entity), pr = HF_FIELD(ProjectileWeapon, projectile);
        HfField vel = HF_FIELD(ProjectileSettings, velocity);
        HfLiveValue a, b2, c;
        memset(&a, 0, sizeof a); memset(&b2, 0, sizeof b2); memset(&c, 0, sizeof c);
        int okA = hf->get_live && hf->get_live(self, UINT64_C(0x824B7E0C4C879EB5), &pe, &a);
        int okB = hf->get_live && hf->get_live(self, UINT64_C(0x824B7E0C4C879EB5), &pr, &b2);
        int okC = hf->get_live && hf->get_live(self, (HfRef)g->round, &vel, &c);
        Log("the rocket arm: its projectile %s %.0f, its projectile entity %s %.0f (non-zero: each shot is a missile entity, not the row); "
            "row %u velocity %s %.0f m/s", okB ? "=" : "unread", b2.base.number, okA ? "=" : "unread", a.base.number, g->round, okC ? "=" : "unread", c.base.number);
    }
    Log("the Sinveiler's %s: round %u, one every %.3f s, %u pellet(s) a shot", gear == 3 ? "rockets" : kGear[gear], g->round, (double)g->every, g->pellets);
    return g;
}
static float g_faceTo[2];
static double g_faceAt = -100;
/* 0.1.4: turning it: Exo Companion's rot_set (game.dll+0x755080) writes a direction into a walker's turn record
 * ([game+0x3326CE0]: map +0x30, records +0x58, 0x3F0 each, the direction at +0); written here directly, only when its
 * record is found. Toward its target while it fires, else the way it slides. The log says whether it turns */
static double g_turnLogAt, g_turnSince = -1;
static void TurnFrame(void)
{
    if (!s_turnOn || !g_sinner || !g_landed || g_piloting || g_frv) return;
    float dir[3] = {0, 0, 0};
    if (g_now - g_faceAt < 1.0) { dir[0] = g_faceTo[0]; dir[1] = g_faceTo[1]; }
    else if (g_moveDir[0] != 0 || g_moveDir[1] != 0) { dir[0] = g_moveDir[0]; dir[1] = g_moveDir[1]; }
    else return;
    uintptr_t mgr = Pointer(game + 0x3326CE0), recs;
    uint32_t i;
    if (!mgr || !Lookup(mgr + 0x30, g_sinner, 1048576, &i) || i >= 65536 || !(recs = Pointer(mgr + 0x58)))
    {
        static unsigned told;
        if (!told++) Log("turning: the Sinveiler has no turn record (it keeps facing where it landed)");
        return;
    }
    hf->write(recs + 0x3F0ull * i, dir, 12);
    if (g_turnSince < 0) g_turnSince = g_now;
    float m[16];
    static unsigned logs;
    if (logs < 6 && g_now >= g_turnLogAt && UnitPose(g_sinUnit, 0, m))
    {
        g_turnLogAt = g_now + 2;
        logs++;
        float fh = sqrtf(m[4] * m[4] + m[5] * m[5]);
        double dot = fh > 0.05f ? (m[4] * dir[0] + m[5] * dir[1]) / fh : 0;
        Log("turning: its facing against where it should face: %.0f degrees off (%.1f s of turning)", acos(dot > 1 ? 1 : dot < -1 ? -1 : dot) * 57.2958, g_now - g_turnSince);
    }
}
static uint8_t g_speedSet[5];
static void RoundSpeed(int r)
{
    if (r < 0 || r > 2 || g_speedSet[r] || s_roundSpeed <= 0 || !g_round[r] || !hf->set_live) return;
    g_speedSet[r] = 1;
    HfField f = HF_FIELD(ProjectileSettings, velocity);
    HfValue v = HF_F32(s_roundSpeed);
    HfLiveValue was;
    memset(&was, 0, sizeof was);
    if (hf->get_live) hf->get_live(self, (HfRef)g_round[r], &f, &was);
    int ok = hf->set_live(self, (HfRef)g_round[r], &f, &v, 0);
    Log("the Sinveiler's %s rounds (projectile %u): %.0f m/s -> %.0f (%s)", kGear[r], g_round[r], was.base.number, (double)s_roundSpeed, ok ? "set" : "refused");
}
/* 0.1.10: at most max_rounds projectiles a second in all (two game crashes in rendering came in fights with her guns
 * at the real fire rates: thousands of spawned rounds a mission) */
static double g_projTokens = 20, g_projAt;
static int ProjBudget(void)
{
    double el = g_now - g_projAt;
    g_projAt = g_now;
    if (el > 0) g_projTokens += el * s_maxRounds;
    if (g_projTokens > s_maxRounds) g_projTokens = s_maxRounds;
    return (int)g_projTokens;
}
/* 0.1.16: the rocket copy made the Patriot's: its blast (395), its speed (200 m/s), no blast when it expires */
static int g_rocketRowSet;
static void RocketRow(void)
{
    if (g_rocketRowSet || !g_round[4] || s_roundRocket != 128 || !hf->set_live) return;
    g_rocketRowSet = 1;
    HfField fe = HF_FIELD(ProjectileSettings, impact_explosion), fx = HF_FIELD(ProjectileSettings, expiry_explosion), fv = HF_FIELD(ProjectileSettings, velocity);
    HfValue ve = HF_U32(395), vx = HF_U32(0), vv = HF_F32(200);
    int a = hf->set_live(self, (HfRef)g_round[4], &fe, &ve, 0), b = hf->set_live(self, (HfRef)g_round[4], &fx, &vx, 0), c = hf->set_live(self, (HfRef)g_round[4], &fv, &vv, 0);
    Log("the rocket (projectile %u, a recoilless rocket made the Patriot's): its blast 395 %s, no expiry blast %s, 200 m/s %s", g_round[4],
        a ? "set" : "refused", b ? "set" : "refused", c ? "set" : "refused");
}
static int g_shootRocket;   /* 0.1.3: this shot is the left arm's rocket */
static int g_shootRifle;    /* 0.3.2: this shot is Kinetic Release's rifle round (release_round, the vanilla one) */
static int g_shootHe;       /* 0.3.11: this shot is the HE shell itself (he_round) */
static double g_heShotAt = -100;   /* 0.3.13: its last HE shell: no blast or impact reaches the Sinveiler for 2.5 s after */
static int g_heRoundBad;
static double g_rocketNext;
static unsigned g_rocketsFired;
static double g_rocketAt = -100;
static float g_rocketFrom[3];
static void OnBoom(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfExplosionEvent* x = (const HfExplosionEvent*)info;
    static unsigned told;
    if (g_now - g_rocketAt > 3 || told >= 20) return;
    float d = sqrtf(Dist2(x->position, g_rocketFrom));
    if (d > 80) return;
    told++;
    Log("explosion row %u %.1f m from the last rocket's start, %.2f s after it (from entity %u, owner %u)", x->type, d, g_now - g_rocketAt, x->source, x->owner);
}
/* 0.2.10: where to aim from `from` at a target whose body middle is `at`: the middle, else higher up its body (its
 * middle above its root by 1.5 and 2 times, its head) until the line from the arm is clear: an enemy up on a ledge
 * was shot through the ledge's lip below it. Raised for the round's drop (gravity over its flight at round_speed) */
static void AimPoint(uint32_t target, const float from[3], const float at[3], float out[3])
{
    memcpy(out, at, 12);
    uint32_t id;
    uint8_t dd[24];
    float root[3];
    if (s_sinSight && WorldId(&id) && QueryIdle() && Descriptor(target, dd) && UnitNode(Word(dd + 12), 0, root))
    {
        float c[4][3];
        int nc = 0;
        memcpy(c[nc++], at, 12);
        for (int k = 0; k < 2; k++) { float f = k ? 2.0f : 1.5f; c[nc][0] = at[0]; c[nc][1] = at[1]; c[nc][2] = root[2] + (at[2] - root[2]) * f; nc++; }
        if (UnitNode(Word(dd + 12), NODE_HEAD, c[nc]) && fabsf(c[nc][2] - root[2]) < 25) nc++;
        for (int i = 0; i < nc; i++)
        {
            float v[3] = {c[i][0] - from[0], c[i][1] - from[1], c[i][2] - from[2]};
            float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (!(n > 1.0f)) break;
            for (int k = 0; k < 3; k++) v[k] /= n;
            int hit = 0;
            float w = FirstWall(id, from, v, n, UnitOf(atomic_load(&g_avatar)), UnitOf(target), g_sinUnit, &hit);
            if (hit || w > n - 0.3f) { memcpy(out, c[i], 12); if (i) { static unsigned told; if (told++ < 6) Log("the Sinveiler aims higher on entity %u (point %d: the middle was behind cover)", target, i); } break; }
        }
    }
    if (s_roundSpeed > 50)
    {
        float dx = out[0] - from[0], dy = out[1] - from[1], dz = out[2] - from[2];
        float t = sqrtf(dx * dx + dy * dy + dz * dz) / s_roundSpeed;
        out[2] += 0.5f * 9.81f * t * t;
    }
}
static int SinnerShoot(int arm, uint32_t target, const float at0[3])
{
    float p[3], m[16], from[3], d[3], at[3];
    memcpy(at, at0, 12);
    if (!SinPos(p)) return 0;
    /* 0.1.4: the arms as if it faced its target (its body turns slowly or not at all while it slides: shots from its
     * own facing crossed its body and caught on it) */
    float fw[2];
    (void)m;
    {
        float v[2] = {at[0] - p[0], at[1] - p[1]}, vn = sqrtf(v[0] * v[0] + v[1] * v[1]);
        if (vn < 0.1f) return 0;
        fw[0] = v[0] / vn; fw[1] = v[1] / vn;
    }
    g_faceTo[0] = fw[0]; g_faceTo[1] = fw[1]; g_faceAt = g_now;
    float rt[2] = {fw[1], -fw[0]}, side = arm ? 1.3f : -1.3f;
    from[0] = p[0] + rt[0] * side + fw[0] * 1.2f;
    from[1] = p[1] + rt[1] * side + fw[1] * 1.2f;
    from[2] = p[2] + 2.4f;
    if (g_frv)
    {
        /* 0.3.13: the FRV is 5 m long with a turret on top: a shell that started 3 m from its middle burst on it
         * (Kinetic Release's ring destroyed it). Its shots start 3.2 m out toward the target (1.8 m more below) */
        from[0] = p[0] + fw[0] * 3.2f + rt[0] * side * 0.3f;
        from[1] = p[1] + fw[1] * 3.2f + rt[1] * side * 0.3f;
        from[2] = p[2] + 1.6f;
    }
    AimPoint(target, from, at0, at);
    for (int k = 0; k < 3; k++) d[k] = at[k] - from[k];
    float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(n > 2.5f)) return 0;
    for (int k = 0; k < 3; k++) { d[k] /= n; from[k] += d[k] * 1.8f; }   /* clear of its own body and arms */
    if (n < 3.2f) return 0;
    if (g_shootHe) g_heShotAt = g_now;
    uint32_t id;
    if (s_sinSight && WorldId(&id) && QueryIdle())
    {
        int hit = 0;
        float w = FirstWall(id, from, d, n - 1.8f, UnitOf(atomic_load(&g_avatar)), UnitOf(target), g_sinUnit, &hit);
        if (!hit && w <= n - 1.8f - 1.5f) return 0;   /* a wall between */
    }
    int r = g_shootRocket ? 4 : RoundGear();
    RoundSpeed(r);
    if (r == 4) RocketRow();
    uint32_t round = g_roundBad[r] ? (uint32_t)s_roundFallback : g_round[r];
    if (g_shootRifle) round = s_relRound > 0 ? (uint32_t)s_relRound : g_round[3] ? g_round[3] : (uint32_t)s_s1Round;
    if (g_shootHe) round = (uint32_t)s_heRound;
    if (ProjBudget() < 1) return 0;
    g_projTokens -= 1;
    int res = hf->spawn_projectile(self, round, from, d);
    if (res == HF_ACT_WAITING) return 0;
    /* 0.1.2: a shotgun's other pellets, 5 degrees wide */
    if (!res && g_shootHe)
    {
        if (!g_heRoundBad++) Log("the HE shell (projectile %u) cannot be spawned here: a bullet and its blast instead (%s)", round, hf->last_log ? hf->last_log(self) : "");
        return 0;
    }
    if (res && g_shootHe)
    {
        unsigned slot = g_ownNext++ % KB_OWN_SLOTS;   /* its burst spares her and the Sinveiler */
        memcpy(g_ownPos[slot], at0, 12);
        atomic_store(&g_ownEmp[slot], 0);
        atomic_store(&g_ownMs[slot], GetTickCount64());
        static unsigned told;
        if (told++ < 3) Log("an HE shell (projectile %u) fired from the Sinveiler", round);
    }
    for (uint32_t k = 1; res && !g_shootRifle && !g_shootHe && s_gunEvery <= 0 && k < Profile(r == 4 ? 3 : r)->pellets && g_projTokens >= 1; k++)
    {
        g_projTokens -= 1;
        float dir[3];
        for (int j = 0; j < 3; j++) dir[j] = d[j] + 0.0873f * ((float)rand() / RAND_MAX - 0.5f);
        float m = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        for (int j = 0; j < 3; j++) dir[j] /= m;
        hf->spawn_projectile(self, round, from, dir);
    }
    if (!res && g_shootRifle)
    {
        static int told;
        if (!told++) Log("Kinetic Release's round (projectile %u) cannot be spawned here (%s)", round, hf->last_log ? hf->last_log(self) : "");
        return 0;
    }
    if (!res && r == 4)
    {
        /* (0.1.16: no fallback bullet for the rockets: no rockets, said once) */
        static int told;
        if (!told++) Log("the Sinveiler's rocket (projectile %u) cannot be spawned here: no rockets this session (%s)", round, hf->last_log ? hf->last_log(self) : "");
        g_round[4] = 0;
        return 0;
    }
    if (!res && round != (uint32_t)s_roundFallback)
    {
        g_roundBad[r] = 1;
        Log("the Sinveiler's %s round (projectile %u) cannot be spawned here: projectile %d (round_fallback) instead", kRoundName[r], round, s_roundFallback);
        return 0;
    }
    if (!Act("a Sinveiler shot", res)) return 0;
    {
        /* 0.1.9: where its shots start (they looked spawned far out from it): the first 12 */
        static unsigned told;
        float me[3], up[3];
        int ok = PlayerPose(me, NULL), upok = UnitNode(g_sinUnit, 0, up);
        if (told++ < 12)
            Log("shot %u: from %.1f %.1f %.1f = %.1f m from its root (its unit's node 0 %s %.1f m from the root), %.1f m from her, %.1f m to the target",
                told, from[0], from[1], from[2], sqrtf(Dist2(from, p)), upok ? "is" : "unread,", upok ? sqrtf(Dist2(up, p)) : -1.0,
                ok ? sqrtf(Dist2(from, me)) : -1.0, sqrtf(Dist2(from, at)));
    }
    /* 0.1.5: its gun's firing sound (the HUD side plays it: koleda_sounds) */
    {
        uint32_t base = g_shootHe ? (uint32_t)s_roundPlus : g_shootRifle ? (uint32_t)(s_relRound > 0 ? s_relRound : s_s1Round) : r == 4 ? (uint32_t)s_roundRocket : (uint32_t)(r == 0 ? s_roundN : r == 1 ? s_roundS : s_roundPlus);
        for (unsigned i = 0; i < sizeof kRoundGun / sizeof kRoundGun[0]; i++)
            if (kRoundGun[i].round == base) { ShotSound(kRoundGun[i].gun, from); break; }
    }
    unsigned slot = g_shotAimNext++ % SHOT_SLOTS;
    memcpy(g_shotAim[slot], at, 12);
    {
        unsigned fs = g_shotFromNext++ % 32;
        memcpy(g_shotFrom[fs], from, 12);
        g_shotFromAt[fs] = g_now;
    }
    g_shotAimAt[slot] = g_now;
    return 1;
}
static void FireFrame(void)
{
    if (!s_sinFire || !g_sinner || !g_landed || g_piloting || g_frv || g_now < g_fireNext) return;
    float p[3], at[3];
    if (!SinPos(p)) return;
    uint32_t t = FireTarget(p, at);
    if (!t) { g_fireNext = g_now + 0.25; return; }
    /* 0.2.10: a target whose health has not moved in dead_after s of its fire: a corpse, left for 15 s */
    {
        static uint32_t trk;
        static int32_t trkHp;
        static double trkSince;
        int32_t hp = 0;
        uint32_t life = 0;
        if (HealthOf(t, &hp, &life))
        {
            if (t != trk || hp != trkHp) { trk = t; trkHp = hp; trkSince = g_now; }
            else if (s_deadAfter > 0 && g_now - trkSince > s_deadAfter)
            {
                DeadAdd(t, 15);
                static unsigned told;
                if (told++ < 8) Log("entity %u: its health stayed %d through %.0f s of the Sinveiler's fire (life state %u): left as dead for 15 s", t, hp, (double)s_deadAfter, life);
                trk = 0;
                g_fireNext = g_now + 0.1;
                return;
            }
        }
    }
    double every = (s_gunEvery > 0 ? s_gunEvery : Profile(RoundGear())->every) * (g_gear == GEAR_PLUS ? s_plusRate : 1);
    g_fireNext = g_now + every;
    /* 0.1.3: the right arm's minigun; in S+ the left arm's rockets on their own rate */
    if (g_gear == GEAR_PLUS && g_round[4] && g_now >= g_rocketNext)
    {
        g_shootRocket = 1;
        if (SinnerShoot(0, t, at)) { g_rocketNext = g_now + (s_gunEvery > 0 ? s_gunEvery * 2 : Profile(3)->every); g_rocketAt = g_now; SinPos(g_rocketFrom); if (g_rocketsFired++ < 3) Log("the Sinveiler fires a rocket (projectile %u) at entity %u", g_round[4], t); }
        g_shootRocket = 0;
    }
    g_arm = 1;
    if (!SinnerShoot(g_arm, t, at)) return;
    g_fightAt = g_now;
    g_volleys++;
    static unsigned told;
    if (told++ < 10) Log("the Sinveiler fires (%s) at entity %u", kGear[g_gear], t);
    SinnerAction("a Sinveiler volley");
}

/* ---- the pull: the pull (Voymastina's Pile Bunker drag, its guards) ---------------------------------------- */
typedef struct { uint32_t e, unit; float to[3]; } Dragged;
static Dragged g_drag[16];
static int g_ndrag;
static double g_dragUntil, g_dragLast, g_pullAt;
static void PullFrame(void)
{
    float p[3];
    if (s_pullOn && !g_frv && g_sinner && g_landed && !g_piloting && g_gear == GEAR_PLUS && g_now - g_pullAt >= s_pullEvery && SinPos(p))
    {
        float q[3];
        int n = 0;
        for (int k = 0; k < FOES && n < 16; k++)
        {
            uint32_t e = g_foe[k];
            uint8_t d[24];
            const char* no;
            if (!e || !TargetPos(e, g_foeRes[k], q) || Dist2(q, p) > s_pullRadius * s_pullRadius || !Descriptor(e, d) || !Movable(e, &no)) continue;
            Dragged* r = &g_drag[n];
            r->e = e; r->unit = Word(d + 12);
            float ang = (float)n * 2.39996f;
            r->to[0] = p[0] + cosf(ang) * 2.5f; r->to[1] = p[1] + sinf(ang) * 2.5f; r->to[2] = p[2];
            n++;
        }
        if (n)
        {
            g_pullAt = g_now;
            g_ndrag = n;
            g_dragUntil = g_now + 0.5;
            g_dragLast = g_now;
            g_pulls++;
            if (s_pullExplosion > 0)
            {
                unsigned slot = g_ownNext++ % KB_OWN_SLOTS;   /* her own blast: it spares her */
                memcpy(g_ownPos[slot], p, 12);
                atomic_store(&g_ownEmp[slot], 0);
                atomic_store(&g_ownMs[slot], GetTickCount64());
                Act("the pull's blast", hf->spawn_explosion(self, (uint32_t)s_pullExplosion, p));
            }
            Log("the pull: %d enemies dragged to the Sinveiler", n);
            SinnerAction("the pull");
        }
    }
    double dt = g_now - g_dragLast;
    g_dragLast = g_now;
    if (!g_ndrag || g_now >= g_dragUntil) { g_ndrag = 0; return; }
    if (dt <= 0 || dt > 0.1 || !g_setPosition || !QueryIdle()) return;
    for (int i = 0; i < g_ndrag; i++)
    {
        Dragged* r = &g_drag[i];
        float q[3];
        uint8_t d[24];
        int32_t hp; uint32_t life;
        if (!r->e) continue;
        /* only while it lives and is still the same unit (Voymastina 0.4.1b: one that died crashed the warp setter) */
        if (!UnitNode(r->unit, 0, q) || !HealthOf(r->e, &hp, &life) || hp <= 0 || !Descriptor(r->e, d) || Word(d + 12) != r->unit) { r->e = 0; continue; }
        float v[2] = {r->to[0] - q[0], r->to[1] - q[1]}, h = sqrtf(v[0] * v[0] + v[1] * v[1]);
        if (h < 0.3f) continue;
        float step = 25.0f * (float)dt;
        if (step > h) step = h;
        float dest[3] = {q[0] + v[0] / h * step, q[1] + v[1] / h * step, q[2]}, g[3];
        if (!Ground(dest, g)) return;
        if (fabsf(g[2] - q[2]) > 2) continue;
        g_setPosition(NULL, r->e, g);
    }
}

/* ---- the frame's Sinveiler bookkeeping: still there, piloted, its health, the share ----------------------------------- */
static void SinnerFrame(void)
{
    SinPackage();
    GrantFrame();
    if (!g_sinner) return;
    uint8_t d[24];
    int32_t hp = 0;
    uint32_t life = 0;
    if (!Descriptor(g_sinner, d) || Quad(d) != SinRes()) { SinnerLost("it left the world"); return; }
    if (HealthOf(g_sinner, &hp, &life) && (hp <= 0 || life != 0)) { SinnerLost("destroyed"); return; }
    g_sinUnit = Word(d + 12);
    g_sinHp = hp;
    if (hp > g_sinMax && g_landed) g_sinMax = hp;
    atomic_store(&g_sinHpNow, hp);
    g_sinPosOk = SinPos(g_sinPos);
    ArrivalFrame();
    static double nextSeat;
    if (g_now >= nextSeat)
    {
        nextSeat = g_now + 0.25;
        int was = g_piloting;
        g_piloting = SeatVehicle() == g_sinner;
        if (was != g_piloting) Log(g_frv ? (g_piloting ? "she rides the FRV Sinveiler" : "she left the FRV Sinveiler") :
                                   g_piloting ? "she pilots the Sinveiler: it stops driving itself" : "she left the Sinveiler: it drives itself again");
    }
    float me[3];
    atomic_store(&g_shareOk, g_landed && !g_piloting && g_sinPosOk && PlayerPose(me, NULL) && Flat(me, g_sinPos) <= s_shareRange);
    int pending = atomic_exchange(&g_sharePending, 0);
    if (pending > 0) SinnerSetHealth(g_sinHp - pending, "the damage share");
    /* the game's locomotion, tried once a Sinveiler has landed */
    if (g_landed && !g_driveTried && !g_piloting && !g_frv)
    {
        g_driveTried = 1;
        g_driving = SinnerDriveStart(g_sinner);
        if (!g_driving) Log("the Sinveiler: no game locomotion (sinner_drive.h: %s): the slide instead", s_drive ? "its layout is not known yet" : "drive 0");
    }
}

/* ==== 0.2.0: the FRV Sinveiler ========================================================================================== */
/* ---- Let's Go For a Spin!: she is put straight into its seat (the game's own spawn-already-seated sequence, game.dll
 * 0x639830's; Seat Probe 0.1.1 / 0.1.2: it seats her at once, from where she stands, and she gets out as usual). The
 * seater manager [game+0x3326D78] (hash +0x20, 64-byte entries +0x48: +0 the vehicle, +0x10 the seat), the seat
 * collections [game+0x3326D88]. The FRVs' seats: 3 the driver's, 0 1 4 passengers' (2 the M-102's gunner) -------- */
typedef void (*SetEnteringFn)(void* S, uint32_t index, uint32_t coll, uint32_t entrance, uint8_t instant);
typedef void (*RpcEnteringFn)(int peers, uint32_t seater, uint32_t coll, uint32_t entrance);
typedef uint32_t (*SeatNodeFn)(void* C, uint32_t coll, uint32_t seater, uint32_t entrance);
typedef void (*SetSeatNodeFn)(void* S, uint64_t unused, uint32_t coll, uint32_t seater, uint32_t node);
typedef void (*RpcSeatNodeFn)(int peers, uint32_t coll, uint32_t seater, uint32_t node);
typedef void (*SeaterUpdateFn)(void* S, uint32_t index);
static SetEnteringFn g_setEntering;
static RpcEnteringFn g_rpcEntering;
static SeatNodeFn g_seatNode;
static SetSeatNodeFn g_setSeatNode;
static RpcSeatNodeFn g_rpcSeatNode;
static SeaterUpdateFn g_seaterUpdate;
static void SeatPins(void)
{
    struct { void** slot; uintptr_t at; const char* bytes; } k[] = {
        {(void**)&g_setEntering, 0x63A7B0, "40 53 55 56 57 41 55 41 56 41 57 48 81 ec c0 00 00 00 48 8b 05 47 18 00 02"},
        {(void**)&g_seatNode, 0x636A30, "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48 89 7c 24 20 41 55 41 56 41 57 48 83 ec 20 33 f6"},
        {(void**)&g_setSeatNode, 0x63E1A0, "45 85 c0 0f 84 91 03 00 00 4c 8b dc 53 57 41 54 48 83 ec 70 48 8b 05 55 de ff 01"},
        {(void**)&g_seaterUpdate, 0x639B40, "48 89 5c 24 18 55 56 57 41 54 41 55 41 56 41 57 48 81 ec 00 01 00 00 0f 29 b4 24 f0 00 00 00"},
        {(void**)&g_rpcEntering, 0xBEA400, "48 89 5c 24 10 44 89 4c 24 20 57 48 83 ec 60 48 8b 05 fa 1b a5 01"},
        {(void**)&g_rpcSeatNode, 0xBE22B0, "48 89 5c 24 10 44 89 4c 24 20 57 48 83 ec 60 48 8b 05 4a 9d a5 01"}};
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++)
        if (!hf->code_matches(game + k[i].at, k[i].bytes))
        {
            Log("Let's Go For a Spin!: game.dll+0x%llX is not the reviewed code: no ride", (unsigned long long)k[i].at);
            g_setEntering = NULL;
            return;
        }
        else *k[i].slot = (void*)(game + k[i].at);
    Log("Let's Go For a Spin! ready (the game's seat code)");
}
/* who sits where: is that seat of that vehicle taken */
static int SeatTaken(uint32_t vehicle, uint32_t seat)
{
    uintptr_t S = Pointer(game + 0x3326D78), entries;
    uint32_t count = 0;
    if (!S || !ReadAt(S + 12, &count, 4) || count > 4096 || !(entries = Pointer(S + 0x48))) return 1;
    for (uint32_t i = 0; i < count; i++)
    {
        uint8_t e[0x14];
        if (ReadAt(entries + 64ull * i, e, sizeof e) && Word(e) == vehicle && Word(e + 0x10) == seat) return 1;
    }
    return 0;
}
/* is she in its driver seat (her seat entry: +0 the vehicle, +8 the role: 1 the driver's; Seat Probe) */
static int SeatIsDriver(void)
{
    uint32_t me = atomic_load(&g_avatar), idx = 0;
    uintptr_t S = Pointer(game + 0x3326D78), entries;
    uint8_t e[12];
    if (!me || !S || !Lookup(S + 0x20, me, 1048576, &idx) || idx >= 65536 || !(entries = Pointer(S + 0x48)) || !ReadAt(entries + 64ull * idx, e, sizeof e)) return 0;
    return Word(e) == g_sinner && Word(e + 8) == 1;
}
static void Ride(void)
{
    uint32_t me = atomic_load(&g_avatar), idx = 0;
    uintptr_t S = Pointer(game + 0x3326D78), C = Pointer(game + 0x3326D88), entries;
    float here[3];
    if (!g_setEntering) { Log("Let's Go For a Spin!: the seat code is not available on this build"); return; }
    if (!me || !S || !C || !Lookup(S + 0x20, me, 1048576, &idx) || idx >= 65536 || !(entries = Pointer(S + 0x48))) { Log("Let's Go For a Spin!: her seat record cannot be read"); return; }
    uint32_t in = 0;
    ReadAt(entries + 64ull * idx, &in, 4);
    if (in) { Log("Let's Go For a Spin!: she is already in a vehicle (%u)", in); return; }
    if (!PlayerPose(here, NULL) || !g_sinPosOk || Flat(here, g_sinPos) > s_rideRange) { Log("Let's Go For a Spin!: the FRV is beyond ride_range (%.0f m)", (double)s_rideRange); return; }
    static const uint32_t order[4] = {3, 4, 0, 1};
    int seat = -1;
    for (int k = 0; k < 4 && seat < 0; k++) if (!SeatTaken(g_sinner, order[k])) seat = (int)order[k];
    if (seat < 0) { Log("Let's Go For a Spin!: every seat is taken"); return; }
    uint32_t e = (uint32_t)seat;
    g_setEntering((void*)S, idx, g_sinner, e, 1);
    g_rpcEntering(-2, me, g_sinner, e);
    hf->write(entries + 64ull * idx + 0x10, &e, 4);
    uint32_t node = g_seatNode((void*)C, g_sinner, me, e);
    g_setSeatNode((void*)S, 0, g_sinner, me, node);
    g_rpcSeatNode(-2, g_sinner, me, node);
    g_seaterUpdate((void*)S, idx);
    g_rides++;
    Log("Let's Go For a Spin!: into the FRV's %s seat (seat %u)", e == 3 ? "driver" : "passenger", e);
    Upshift(1, "Let's Go For a Spin!");
}
/* every hit calculation: the turret's (dealer entity +0x80), its target (+0x20), for the damage event to learn its
 * network id from */
static void TurretSeen(KbHitCall* c)
{
    /* 0.2.5: Kinetic Release's EMS bursts pierce release_ap armour on its targets */
    if (c->kind == KB_HIT_CALC && s_releaseAp > 0)
    {
        uint8_t h[HIT_SIZE];
        if (ReadAt((uintptr_t)c->hit, h, sizeof h) && Word(h) == 4 && RelIndex(Word(h + 0x20)) >= 0 && OwnBlast(h, NULL))
        {
            memcpy(c->saved, h + 0x9C, 12);
            int ok = 1;
            for (int k = 0; k < 3; k++) if (!(c->saved[k] >= 0 && c->saved[k] <= 12)) ok = 0;
            if (ok)
            {
                float up[3];
                for (int k = 0; k < 3; k++) up[k] = c->saved[k] > s_releaseAp ? c->saved[k] : s_releaseAp;
                memcpy((uint8_t*)c->hit + 0x9C, up, 12);
                c->restore = 1;
            }
        }
    }
    uint32_t t = g_turretEnt, car = g_sinner;
    if (!g_frv || (g_turretNet && g_frvNet) || c->kind != KB_HIT_CALC) return;
    uint32_t w[2];
    if (!ReadAt((uintptr_t)c->hit + 0x80, &w[0], 4) || !w[0] || !ReadAt((uintptr_t)c->hit + 0x20, &w[1], 4)) return;
    if (w[0] == t && !g_turretNet) { atomic_store(&g_turretSeenTarget, w[1]); atomic_store(&g_turretSeenMs, GetTickCount64()); }
    if (w[0] == car && !g_frvNet) { atomic_store(&g_frvSeenTarget, w[1]); atomic_store(&g_frvSeenMs, GetTickCount64()); }
}
/* 0.2.4: her own blasts (Kinetic Release) do nothing to the FRV Sinveiler: their hit result on it is emptied, as the kit
 * base empties it on her */
static unsigned g_carSpared;
static atomic_uint g_carSteadied;
static atomic_ullong g_carHitMs;
static void CarSpared(KbHitCall* c)
{
    uint32_t car = g_sinner;
    uint8_t h[HIT_SIZE];
    if (!car || !c->hit || !c->out || !ReadAt((uintptr_t)c->hit, h, sizeof h) || Word(h + 0x20) != car) return;
    atomic_store(&g_carHitMs, GetTickCount64());   /* 0.3.9: Steady's window */
    if (!OwnBlast(h, NULL) && g_now - g_heShotAt >= 0.4)   /* (0.3.13: and nothing in the 0.4 s after its own HE shell left: one that burst on it) */
    {
        /* 0.3.8: anyone else's hit on it: its damage stays, its stagger, push, knockdown and hit reaction go (the
         * result's fields, as the kit base's knockdown guard on her) */
        if (!s_sinSteady) return;
        uint8_t* r = (uint8_t*)c->out;
        float st, pu;
        memcpy(&st, r + 0x0C, 4); memcpy(&pu, r + 0x10, 4);
        if (st != 0 || pu != 0 || r[0x1E] || r[0x1F])
        {
            static atomic_uint told;
            if (atomic_fetch_add(&told, 1) < 6) Log("a hit on the Sinveiler: stagger %.1f, push %.1f, knockdown %u, reaction %u: removed", (double)st, (double)pu, r[0x1E], r[0x1F]);
            atomic_fetch_add(&g_carSteadied, 1);
        }
        memset(r + 0x0C, 0, 8);
        r[0x1E] = 0; r[0x1F] = 0;
        return;
    }
    memset(c->out, 0, RESULT_SIZE);
    g_carSpared++;
}
/* 0.2.5: no EMS stun on her or the FRV Sinveiler (the status add, game.dll+0x699E40, as Alva's guard: statuses 37-39) */
typedef void (*AddStatusFn)(void* m, uint32_t e, uint32_t st, uint32_t r9, float buildup, uint32_t from, uint64_t z);
static AddStatusFn original_add;
static unsigned g_emsRefused;
static void AddStatus(void* m, uint32_t e, uint32_t st, uint32_t r9, float buildup, uint32_t from, uint64_t z)
{
    if (s_emsImmune && g_inMission && kb_kit_on && (st == 37 || st == 38 || st == 39) && e &&
        (e == atomic_load(&g_avatar) || e == g_sinner))
    {
        if (g_emsRefused++ < 6) Log("EMS immunity: status %u refused on %s", st, e == g_sinner ? "the FRV Sinveiler" : "her");
        return;
    }
    original_add(m, e, st, r9, buildup, from, z);
}
static void StatusInstall(void)
{
    if (!hf->code_matches(game + 0x699E40, "40 55 53 57 41 55 41 57 48 8d 6c 24 d0 48 81 ec 30 01 00 00"))
    { Log("EMS immunity off: game.dll+0x699E40 is not the reviewed code"); return; }
    if (hf->hook(self, game + 0x699E40, (void*)AddStatus, (void**)&original_add)) { Log("EMS immunity off: the hook could not be placed"); return; }
    Log("EMS immunity ready (her and the FRV Sinveiler: statuses 37-39)");
}
/* ---- its turret's fight, Kinetic Release, its repair, whether it is parked ----------------------------------------- */
static double g_frvLookNext;
static void FrvFrame(void)
{
    if (!g_frv || !g_sinner || !g_landed || !g_sinPosOk) return;
    float p[3], at[3];
    memcpy(p, g_sinPos, 12);
    if (g_now >= g_frvLookNext)
    {
        g_frvLookNext = g_now + 0.5;
        if (FireTarget(p, at)) { g_fightAt = g_now; SinnerAction("its turret"); }
    }
}

/* ==== 0.3.0: Gear Shift's time, its heal, standing still; Kinetic Release's waves; Drift and Full Speed =========== */
static double g_releaseNext, g_healNext, g_parkAt;
static float g_parkFrom[3];
static unsigned g_gearSaved, g_heals, g_drifts, g_fulls, g_driftShots;
/* Kinetic Release: rings of EMS bursts out from a point */
static struct { int on, ring, rings; float c[3], reach; double next; } g_wave;   /* (0.3.2: Drift's) */
static atomic_int g_relDmg;
static double g_relUntil;             /* her EMS bursts spare her allies until then */
static void ReleaseStart(const float c[3], int rings, float reach, int damage, const char* why)
{
    if (s_releaseShell <= 0 && s_releaseRow <= 0) return;
    struct { uint32_t e; float d2; } t[REL_TARGETS];
    int n = 0;
    for (int k = 0; k < FOES; k++)
    {
        float q[3], d2;
        uint32_t e = g_foe[k];
        if (!e || !TargetPos(e, g_foeRes[k], q) || (d2 = Dist2(q, c)) > reach * reach || !Alive(e)) continue;
        int j;
        if (n == REL_TARGETS) { if (d2 >= t[n - 1].d2) continue; j = n - 1; }
        else j = n++;
        while (j > 0 && t[j - 1].d2 > d2) { t[j] = t[j - 1]; j--; }
        t[j].e = e; t[j].d2 = d2;
    }
    for (int i = 0; i < REL_TARGETS; i++) { atomic_store(&g_relTarget[i], i < n ? t[i].e : 0); atomic_store(&g_relPaid[i], 0); }
    atomic_store(&g_relMs, GetTickCount64());
    atomic_store(&g_relDmg, damage);
    g_wave.on = 1; g_wave.ring = 0; g_wave.rings = rings;
    memcpy(g_wave.c, c, 12); g_wave.reach = reach; g_wave.next = g_now;
    g_relUntil = g_now + g_wave.rings * (double)s_relGap + 3;
    if (g_drifts <= 10) Log("%s: EMS waves, %d ring(s) out to %.0f m, %d enemies within reach (%d each)", why, g_wave.rings, (double)reach, n, damage);
}
static void WaveFrame(void)
{
    if (!g_wave.on || g_now < g_wave.next) return;
    float inner = s_relInner < g_wave.reach ? s_relInner : g_wave.reach * 0.5f;
    float r = g_wave.rings > 1 ? inner + (g_wave.reach - inner) * (float)g_wave.ring / (float)(g_wave.rings - 1) : (inner + g_wave.reach) * 0.5f;
    int n = (int)(6.2832f * r / 5.0f + 0.5f);
    if (n < 3) n = 3;
    if (n > s_relPoints) n = s_relPoints;
    if (s_releaseShell > 0 && ProjBudget() < n) { g_wave.next = g_now + 0.05; return; }
    atomic_store(&g_relMs, GetTickCount64());
    int done = 0;
    for (int i = 0; i < n; i++)
    {
        float a = 6.2832f * (float)i / (float)n + 0.5f * (float)g_wave.ring, q[3] = {g_wave.c[0] + cosf(a) * r, g_wave.c[1] + sinf(a) * r, g_wave.c[2]}, g[3];
        if (!Ground(q, g)) memcpy(g, q, 12);
        unsigned slot = g_ownNext++ % KB_OWN_SLOTS;   /* her own blast: it spares her and the Sinveiler */
        memcpy(g_ownPos[slot], g, 12);
        atomic_store(&g_ownEmp[slot], 0);
        atomic_store(&g_ownMs[slot], GetTickCount64());
        int res = -1;
        if (s_releaseShell > 0)
        {
            float from[3] = {g[0], g[1], g[2] + 12}, down[3] = {0, 0, -1};
            res = hf->spawn_projectile(self, (uint32_t)s_releaseShell, from, down);
            if (res == HF_ACT_REQUESTED) g_projTokens -= 1;
        }
        if (res != HF_ACT_REQUESTED && s_releaseRow > 0) res = hf->spawn_explosion(self, (uint32_t)s_releaseRow, g);
        if (res == HF_ACT_REQUESTED) done++;
    }
    static unsigned told;
    if (told++ < 12) Log("EMS wave: ring %d of %d, %.1f m out: %d of %d EMS bursts", g_wave.ring + 1, g_wave.rings, (double)r, done, n);
    if (++g_wave.ring >= g_wave.rings) g_wave.on = 0;
    else g_wave.next = g_now + s_relGap;
}
/* 0.3.6: HE rounds: a blast where each lands (its flight time after the shot) */
#define HE_Q 32
/* (0.3.10: the blast goes off where the round hit, just outside what it hit, on the Sinveiler's side: set off at the enemy's
 * middle it burst inside it, downward, digging under it without hurting it. A round that hits nothing: short of its aim) */
static struct { float at[3], from[3], hit[3]; double when; atomic_int struck; } g_he[HE_Q];
static unsigned g_heBlasts, g_heDropped, g_heOnHit;
static double g_heTokens;
static void HeQueue(const float from[3], const float at[3])
{
    if (s_heRow <= 0) return;
    for (int i = 0; i < HE_Q; i++)
        if (g_he[i].when <= 0)
        {
            float d = sqrtf(Dist2(from, at)), t = d / (s_roundSpeed > 50 ? s_roundSpeed : 250);
            memcpy(g_he[i].at, at, 12);
            memcpy(g_he[i].from, from, 12);
            atomic_store(&g_he[i].struck, 0);
            g_he[i].when = g_now + (t > 0.5f ? 0.5f : t) + 0.25;   /* the latest: its round has landed by then */
            return;
        }
    g_heDropped++;
}
/* the damage event: one of the Sinveiler's rounds hit here */
static void HeHit(const float pos[3])
{
    for (int i = 0; i < HE_Q; i++)
        if (g_he[i].when > 0 && !atomic_load(&g_he[i].struck) && Dist2(g_he[i].at, pos) < 3.5f * 3.5f)
        {
            memcpy(g_he[i].hit, pos, 12);
            atomic_store(&g_he[i].struck, 1);
            return;
        }
}
static void HeFrame(double step)
{
    g_heTokens += step * s_heRate;
    if (g_heTokens > s_heRate) g_heTokens = s_heRate;
    for (int i = 0; i < HE_Q; i++)
    {
        int struck = g_he[i].when > 0 && atomic_load(&g_he[i].struck);
        if (g_he[i].when <= 0 || (!struck && g_now < g_he[i].when)) continue;
        g_he[i].when = 0;
        if (g_heTokens < 1) { g_heDropped++; continue; }
        /* back toward the Sinveiler from where it hit (0.5 m) or from its aim (1.2 m), and a little up */
        float q[3], dir[3], back = struck ? 0.5f : 1.2f;
        memcpy(q, struck ? g_he[i].hit : g_he[i].at, 12);
        for (int k = 0; k < 3; k++) dir[k] = g_he[i].from[k] - q[k];
        float n = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        if (n > back + 0.5f) for (int k = 0; k < 3; k++) q[k] += dir[k] / n * back;
        q[2] += 0.3f;
        if (struck) g_heOnHit++;
        unsigned slot = g_ownNext++ % KB_OWN_SLOTS;   /* spares her and the Sinveiler */
        memcpy(g_ownPos[slot], q, 12);
        atomic_store(&g_ownEmp[slot], 0);
        atomic_store(&g_ownMs[slot], GetTickCount64());
        if (hf->spawn_explosion(self, (uint32_t)s_heRow, q) == HF_ACT_REQUESTED)
        {
            g_heTokens -= 1;
            if (g_heBlasts++ < 3) Log("HE round: the Orbital Gatling Barrage's blast (row %d) at %.1f %.1f %.1f (%s)", s_heRow, (double)q[0], (double)q[1], (double)q[2], struck ? "where its round hit" : "short of its aim");
        }
        else { static int told; if (!told++) Log("HE round: blast row %d refused (%s)", s_heRow, hf->last_log ? hf->last_log(self) : ""); }
    }
}
/* shots queued from the Sinveiler (Kinetic Release's rifle rounds), under max_rounds */
#define DRIFT_Q 64
static struct { uint32_t t; int rifle; float at[3]; } g_dq[DRIFT_Q];
static int g_dqn, g_dqi;
static double g_driftNext, g_shotNext;
static void KineticRelease(const char* why)
{
    if (!g_sinner || !g_landed || !g_sinPosOk) return;
    int plus = g_gear == GEAR_PLUS, want = plus ? s_releaseTargetsPlus : s_releaseTargetsS, rounds = plus ? s_relRoundsPlus : s_relRoundsS, n = 0;
    float reach = plus ? s_relReachPlus : s_releaseReach;
    struct { uint32_t e; float d2; } t[20];
    if (want > 20) want = 20;
    for (int k = 0; k < FOES; k++)
    {
        float q[3], d2;
        uint32_t e = g_foe[k];
        if (!e || !TargetPos(e, g_foeRes[k], q) || (d2 = Dist2(q, g_sinPos)) > reach * reach || !Alive(e)) continue;
        int j;
        if (n == want) { if (d2 >= t[n - 1].d2) continue; j = n - 1; }
        else j = n++;
        while (j > 0 && t[j - 1].d2 > d2) { t[j] = t[j - 1]; j--; }
        t[j].e = e; t[j].d2 = d2;
    }
    int ring = plus ? s_relRingPlus : s_relRingS;
    if (!n) return;   /* (only with an enemy in reach) */
    g_releaseNext = g_now + s_releaseEvery;
    if (g_dqi >= g_dqn) g_dqn = g_dqi = 0;
    /* 0.3.7: all around it: a ring of rounds, whichever way it faces */
    static unsigned turn;
    for (int i = 0; i < ring && g_dqn < DRIFT_Q; i++)
    {
        float a = 6.2832f * ((float)i + 0.5f * (float)(turn & 1)) / (float)ring, q[3] = {g_sinPos[0] + cosf(a) * reach * 0.85f, g_sinPos[1] + sinf(a) * reach * 0.85f, g_sinPos[2]}, g[3];
        if (!Ground(q, g)) memcpy(g, q, 12);
        g_dq[g_dqn].t = 0; g_dq[g_dqn].rifle = 1; memcpy(g_dq[g_dqn].at, g, 12); g_dqn++;
    }
    turn++;
    for (int r = 0; r < rounds; r++)
        for (int i = 0; i < n && g_dqn < DRIFT_Q; i++) { g_dq[g_dqn].t = t[i].e; g_dq[g_dqn].rifle = 1; g_dqn++; }
    g_releases++;
    if (g_releases <= 8) Log("%s%s: %d rounds all around it, %d each at %d enemies within %.0f m", why, plus ? "+" : "", ring, rounds, n, (double)reach);
}
static int DriftStart(void)
{
    if (g_wave.on) { Log("Let's Go for a Drift!: its last waves are still going"); return 0; }
    float p[3];
    memcpy(p, g_sinPos, 12);
    int n = 0;
    for (int k = 0; k < FOES; k++)
    {
        float q[3];
        uint32_t e = g_foe[k];
        if (e && TargetPos(e, g_foeRes[k], q) && Dist2(q, p) <= s_driftRadius * s_driftRadius && Alive(e)) n++;
    }
    g_drifts++;
    if (g_drifts <= 10) Log("Let's Go for a Drift! (%s): %d enemies within %.0f m", kGear[g_gear], n, (double)s_driftRadius);
    SinnerAction("Let's Go for a Drift!");
    ReleaseStart(p, s_driftRings, s_driftRadius, s_driftDamage, "Let's Go for a Drift!");
    return 1;
}
/* Full Speed: its speed and way (from its position frame to frame, smoothed) */
static float g_speed, g_way[2], g_wayFrom[3];
static double g_tracerNext, g_tracerAt = -100;
static unsigned g_tracers;
static double g_wayAt = -100, g_fullNext, g_fullSeen = -100;
static float RamBonus(void)
{
    if (!s_fullOn || g_gear == GEAR_N || g_speed < s_fullMinSpeed) return 1;
    float m = 1 + s_fullRam * (g_speed - s_fullMinSpeed);
    return m > s_fullRamMax ? s_fullRamMax : m;
}
static void FullFrame(void)
{
    if (!g_sinner || !g_landed || !g_sinPosOk) { g_speed = 0; g_wayAt = -100; return; }
    double el = g_now - g_wayAt;
    if (el >= 0.1)
    {
        if (el < 1.0)
        {
            float v[2] = {(g_sinPos[0] - g_wayFrom[0]) / (float)el, (g_sinPos[1] - g_wayFrom[1]) / (float)el}, sp = sqrtf(v[0] * v[0] + v[1] * v[1]);
            g_speed = g_speed * 0.5f + sp * 0.5f;
            if (sp > 1) { g_way[0] = v[0] / sp; g_way[1] = v[1] / sp; }
        }
        memcpy(g_wayFrom, g_sinPos, 12);
        g_wayAt = g_now;
    }
    if (!s_fullOn || g_gear == GEAR_N || g_speed < s_fullMinSpeed) return;
    /* 0.3.12: tracers sprayed ahead across the cone, enemies or not: its front is shooting */
    if (s_fullTracers && g_tracer && g_now >= g_tracerNext)
    {
        static unsigned side;
        g_tracerNext = g_now + s_tracerEvery;
        float a = (((float)rand() / RAND_MAX) * 2 - 1) * s_fullCone * 0.0174533f, ca = cosf(a), sa = sinf(a);
        float dir[3] = {g_way[0] * ca - g_way[1] * sa, g_way[0] * sa + g_way[1] * ca, -0.02f - 0.04f * ((float)rand() / RAND_MAX)};
        float rt[2] = {g_way[1], -g_way[0]}, off = (side++ & 1) ? 0.7f : -0.7f;
        float from[3] = {g_sinPos[0] + g_way[0] * 2.8f + rt[0] * off, g_sinPos[1] + g_way[1] * 2.8f + rt[1] * off, g_sinPos[2] + 1.1f};
        float n = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        for (int k = 0; k < 3; k++) dir[k] /= n;
        if (hf->spawn_projectile(self, g_tracer, from, dir) == HF_ACT_REQUESTED) { g_tracers++; g_tracerAt = g_now; }
    }
    if (g_now < g_fullNext || ProjBudget() < 1) return;
    /* the enemies in its cone ahead (the nearest few) */
    float cosMax = cosf(s_fullCone * 0.0174533f), q[3];
    struct { uint32_t e; float d2, q[3]; } t[8];
    int n = 0;
    for (int k = 0; k < FOES; k++)
    {
        uint32_t e = g_foe[k];
        float d2;
        if (!e || !TargetPos(e, g_foeRes[k], q) || (d2 = Dist2(q, g_sinPos)) > s_fullRange * s_fullRange || d2 < 9) continue;
        float dx = q[0] - g_sinPos[0], dy = q[1] - g_sinPos[1], h = sqrtf(dx * dx + dy * dy);
        if (h < 0.1f || (dx * g_way[0] + dy * g_way[1]) / h < cosMax || !Alive(e)) continue;
        int j;
        if (n == 8) { if (d2 >= t[n - 1].d2) continue; j = n - 1; }
        else j = n++;
        while (j > 0 && t[j - 1].d2 > d2) { t[j] = t[j - 1]; j--; }
        t[j].e = e; t[j].d2 = d2; memcpy(t[j].q, q, 12);
    }
    if (!n) { g_fullNext = g_now + 0.1; return; }
    double every = s_fullEvery * s_fullRefSpeed / g_speed;
    g_fullNext = g_now + (every < 0.05 ? 0.05 : every);
    int fired = 0;
    static unsigned rot;
    for (int i = 0; i < s_fullShots && ProjBudget() >= 1; i++)
    {
        int k = (int)(rot++ % (unsigned)n);
        float at[3];
        memcpy(at, t[k].q, 12);
        g_arm = !g_arm;
        int he = s_fullHe && s_heRound > 0 && !g_heRoundBad && g_heTokens >= 1;   /* (at most he_rate shells a second: the rest plain rounds) */
        g_shootHe = he;
        int ok = SinnerShoot(g_arm, t[k].e, at);
        g_shootHe = 0;
        if (ok) { fired++; if (he) { g_heTokens -= 1; g_heBlasts++; } else if (s_fullHe && (s_heRound <= 0 || g_heRoundBad)) HeQueue(g_sinPos, at); }
    }
    if (!fired) return;
    g_fightAt = g_now;
    g_driftShots += (unsigned)fired;
    if (g_now - g_fullSeen > 3) { g_fulls++; if (g_fulls <= 8) Log("Let's Go Full Speed! (%s, %.0f m/s): it fires ahead at %d enemies, rams x%.2f", kGear[g_gear], (double)g_speed, n, (double)RamBonus()); }
    g_fullSeen = g_now;
    SinnerAction("Let's Go Full Speed!");
}
static void GearFrame(void)
{
    if (g_gear != GEAR_D && g_gearUntil > 0 && g_now >= g_gearUntil) Gear(GEAR_D, "its gear_seconds are over");
    if (!g_sinner || !g_landed || !g_sinPosOk) { g_parked = 0; g_dqn = g_dqi = 0; return; }
    if (g_now - g_parkAt >= 0.5)
    {
        int was = g_parked;
        g_parked = Dist2(g_sinPos, g_parkFrom) < 0.3f * 0.3f;
        g_frvParked = g_parked;
        memcpy(g_parkFrom, g_sinPos, 12);
        g_parkAt = g_now;
        if (was != g_parked) { static unsigned told; if (told++ < 6) Log("the Sinveiler %s", g_parked ? "stands still (P: it takes less damage)" : "moves again"); }
    }
    /* Gear Shift: a hit on it heals it, at most every sinner_heal_every s */
    if (atomic_exchange(&g_repairPending, 0) && g_now >= g_healNext && s_sinHeal > 0 && g_sinMax > 0 && g_sinHp < g_sinMax)
    {
        g_healNext = g_now + s_sinHealEvery;
        g_heals++;
        int32_t to = g_sinHp + (int32_t)(g_sinMax * s_sinHeal + 0.5f);
        SinnerSetHealth(to > g_sinMax ? g_sinMax : to, "Gear Shift's heal");
    }
    /* Kinetic Release while it fights in S or S+ */
    if ((g_gear == GEAR_S || g_gear == GEAR_PLUS) && g_now >= g_releaseNext)
    {
        g_releaseNext = g_now + 0.5;   /* (KineticRelease sets release_every once it fires) */
        KineticRelease("Kinetic Release");
    }
}
static int Mounted(void) { return g_frv && g_sinner && g_landed && g_piloting; }
static void Drift(void)
{
    static unsigned told;
    if (!s_driftOn) return;
    if (!g_sinner || !g_landed || !g_sinPosOk) { if (told++ < 8) Log("Let's Go for a Drift!: no Sinveiler standing"); return; }
    if (g_gear == GEAR_N) { if (told++ < 8) Log("Let's Go for a Drift!: not in N"); return; }
    if (g_now < g_driftNext) { if (told++ < 8) Log("Let's Go for a Drift!: %.0f s left on drift_cooldown", g_driftNext - g_now); return; }
    if (DriftStart()) g_driftNext = g_now + s_driftEvery;
}
static void DriftFrame(void)
{
    if (!g_sinner || !g_landed || !g_sinPosOk || (g_piloting && !g_frv)) return;
    /* Kinetic Release's rounds, a few each frame */
    for (int shots = 0; g_dqi < g_dqn && shots < 4 && g_now >= g_shotNext && ProjBudget() >= 1; shots++)
    {
        int i = g_dqi++, k = g_dq[i].t ? FoeIndex(g_dq[i].t) : -1;
        float at[3];
        if (!g_dq[i].t) memcpy(at, g_dq[i].at, 12);
        else if (k < 0 || !Alive(g_dq[i].t) || !TargetPos(g_dq[i].t, g_foeRes[k], at)) continue;
        int he = s_releaseHe && s_heRound > 0 && !g_heRoundBad && g_heTokens >= 1;
        g_shootRifle = g_dq[i].rifle && !he;
        g_shootHe = he;
        g_arm = !g_arm;
        int ok = SinnerShoot(g_arm, g_dq[i].t, at);
        g_shootRifle = g_shootHe = 0;
        if (ok) { g_driftShots++; if (he) { g_heTokens -= 1; g_heBlasts++; } else if (s_releaseHe && (s_heRound <= 0 || g_heRoundBad)) HeQueue(g_sinPos, at); }
    }
}

/* ---- 0.2.3: Grip: the FRV Sinveiler kept on the ground. Its physics body (Khi's and RSNLRiggs' way in Apache - Rise Above:
 * the unit's one physical actor among its components, id bit 29) is read each frame; four rays from its corners find
 * the ground; with fewer than 2 corners near their resting height, its upward speed is cut and it is pushed down (the
 * engine physics table's impulse, slot 0xC0, as Apache pushes its FRV-based body) -------------------------------- */
typedef void* (*UnitResolveFn)(uint32_t);
typedef int (*ComponentsFn)(uint32_t, uint32_t, void*);
typedef int (*ActorBoolFn)(uint32_t);
typedef uint32_t (*ActorOwnerFn)(uint32_t);
typedef void (*ActorVecFn)(uint32_t, float*);
typedef void (*ActorVelFn)(uint32_t, float*, float*);
typedef void (*ActorPushFn)(uint32_t, const float*, const float*);
static struct
{
    UnitResolveFn unit_resolve;
    ComponentsFn components;
    ActorBoolFn valid, physical;
    ActorOwnerFn owner;
    ActorVecFn position, rotation;
    ActorVelFn velocities;
    ActorPushFn impulse;
    /* 0.3.9, from the table itself (the game calls them so at game.dll+0x5C91F2 .. 0x5C9223: both speeds set, then its
     * place and its turn): */
    ActorPushFn set_velocities;          /* slot 0xA0: (actor, linear, angular) */
    ActorVecFn teleport_position;        /* slot 0x60 */
    ActorVecFn teleport_rotation;        /* slot 0x68 */
} px;
static int g_pxOk, g_pxBad;
static void PhysicsPins(void)
{
    struct { void** slot; uintptr_t rva; int table; const char* bytes; } k[] = {
        {(void**)&px.unit_resolve, 0x9d8c0, -1, "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 48 8b 35 1a 28 97 01 8b d9 48 8d 8e d0 00 00 00 ff"},
        {(void**)&px.valid, 0x7972f0, 0x18, "48 83 ec 28 e8 b7 de ff ff 33 c9 48 85 c0 0f 95 c1 8b c1 48 83 c4 28 c3"},
        {(void**)&px.physical, 0x797310, 0x00, "48 83 ec 28 e8 97 de ff ff 48 85 c0 74 0b 8b 40 10 83 e0 01 48 83 c4 28 c3"},
        {(void**)&px.impulse, 0x797cf0, 0xC0, "48 89 5c 24 10 48 89 6c 24 18 56 57 41 56 48 83 ec 60 49 8b f0 8b d9 4c 8b f2 e8 a9 45 b2 00 33"},
        {(void**)&px.velocities, 0x799540, 0xA8, "48 89 5c 24 08 57 48 83 ec 40 49 8b d8 44 8b d9 48 8b fa e8 58 bc ff ff 41 8b cb 0f 57 db 48 c1"},
        {(void**)&px.owner, 0x799980, 0x10, "48 83 ec 28 e8 27 b8 ff ff 48 85 c0 74 08 8b 40 0c 48 83 c4 28 c3"},
        {(void**)&px.position, 0x7999a0, 0x50, "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 20 57 48 83 ec 30 48 8b ea 8b f1 e8 fa 28 b2 00 33 d2"},
        {(void**)&px.rotation, 0x799a90, 0x58, "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 20 57 48 83 ec 40 48 8b ea 8b f1 e8 0a 28 b2 00 33 d2"},
        {(void**)&px.components, 0x7e1c20, -1, "48 89 5c 24 20 55 48 83 ec 20 8b c1 4d 8b d0 25 ff ff 3f 00 8b ea 4c 8d 0c 40 48 8b 05 ff 3e fe"}};
    /* 0.3.9: the table is filled after the game loads (it read as not there at the loaded event, so Grip never ran):
     * asked again when the FRV Sinveiler lands; any table whose slots are the researched code is the table */
    uintptr_t table = Pointer(game + 0x3326338);
    static int tries;
    if (!table) { if (tries++ < 3) Log("the engine physics table is not set yet ([game+0x3326338] = 0): asked again later"); return; }
    if (table != exe + 0x27CD910) Log("the engine physics table is at %p (helldivers2.exe+0x%llX, researched at +0x27CD910): its slots decide", (void*)table, (unsigned long long)(table - exe));
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++)
    {
        uintptr_t at = exe + k[i].rva;
        if (!hf->code_matches(at, k[i].bytes) || (k[i].table >= 0 && Pointer(table + (uintptr_t)k[i].table) != at))
        { if (tries++ < 6) Log("Grip and Steady off: helldivers2.exe+0x%llX is not the researched code (table slot 0x%X = %p)", (unsigned long long)k[i].rva, k[i].table, k[i].table >= 0 ? (void*)Pointer(table + (uintptr_t)k[i].table) : NULL); g_pxBad = 1; return; }
        *k[i].slot = (void*)at;
    }
    uintptr_t sv = Pointer(table + 0xA0), tp = Pointer(table + 0x60), tr = Pointer(table + 0x68);
    size_t exeSize = 0;
    hf->module_base("helldivers2.exe", &exeSize);
    if (sv > exe && sv < exe + exeSize && tp > exe && tp < exe + exeSize && tr > exe && tr < exe + exeSize)
    { px.set_velocities = (ActorPushFn)sv; px.teleport_position = (ActorVecFn)tp; px.teleport_rotation = (ActorVecFn)tr; }
    g_pxOk = 1;
    Log("the engine physics ready (the FRV Sinveiler's body read%s)", px.set_velocities ? ", steadied and set upright" : "; its setters are not there: no Steady");
}
static uint32_t g_gripActor, g_gripUnit;
static float g_gripRest[4];
static int g_gripRestN;
static unsigned g_gripCatches;
static double g_gripLogAt;
static uint32_t GripActor(uint32_t unit)
{
    if (!px.unit_resolve(unit)) return 0;
    uint8_t buf[128] __attribute__((aligned(8)));
    memset(buf, 0, sizeof buf);
    if (px.components(unit, 127, buf) <= 0) return 0;
    for (int list = 0; list < 7; list++)
    {
        uint32_t count = Word(buf + 56 + list * 4);
        uintptr_t ids;
        memcpy(&ids, buf + list * 8, 8);
        if (count > 64) return 0;
        for (uint32_t k = 0; k < count && ids; k++)
        {
            uint32_t id = 0;
            if (!ReadAt(ids + k * 4ull, &id, 4)) return 0;
            if ((id & 0x20000000u) && px.valid(id) && px.owner(id) == unit && px.physical(id)) return id;
        }
    }
    return 0;
}
static void QRotate(const float q[4], const float v[3], float out[3])
{
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float tx = 2 * (y * v[2] - z * v[1]), ty = 2 * (z * v[0] - x * v[2]), tz = 2 * (x * v[1] - y * v[0]);
    out[0] = v[0] + w * tx + (y * tz - z * ty);
    out[1] = v[1] + w * ty + (z * tx - x * tz);
    out[2] = v[2] + w * tz + (x * ty - y * tx);
}
static void GripFrame(double dt)
{
    if (!s_grip || !g_pxOk || !g_frv || !g_sinner || !g_landed || !g_sinUnit || dt <= 0 || dt > 0.1) return;
    uint8_t d[24];
    if (!Descriptor(g_sinner, d) || !(Word(d + 20) & 1)) return;   /* only where it is simulated */
    if (g_gripUnit != g_sinUnit || !g_gripActor || !px.valid(g_gripActor) || px.owner(g_gripActor) != g_sinUnit)
    {
        g_gripUnit = g_sinUnit;
        g_gripActor = GripActor(g_sinUnit);
        g_gripRestN = 0;
        Log("Grip: the FRV Sinveiler's physics body %08x", g_gripActor);
        if (!g_gripActor) return;
    }
    float pos[4] = {0}, q[4] = {0}, v[4] = {0}, w[4] = {0};
    px.position(g_gripActor, pos);
    px.rotation(g_gripActor, q);
    px.velocities(g_gripActor, v, w);
    if (!isfinite(v[0]) || !isfinite(v[1]) || !isfinite(v[2]) || !isfinite(q[3])) return;
    /* its corners, from 1 m above them in its own frame: the ground below each */
    static const float corner[4][3] = {{0.9f, 1.4f, 1.0f}, {-0.9f, 1.4f, 1.0f}, {0.9f, -1.4f, 1.0f}, {-0.9f, -1.4f, 1.0f}};
    float dist[4], down[3] = {0, 0, -1};
    for (int i = 0; i < 4; i++)
    {
        float o[3], from[3];
        QRotate(q, corner[i], o);
        for (int k = 0; k < 3; k++) from[k] = pos[k] + o[k];
        dist[i] = SinRay(from, down, 6.0f);
        if (dist[i] < 0) return;   /* the ray queue is busy: next frame */
    }
    float hs = sqrtf(v[0] * v[0] + v[1] * v[1]);
    /* its resting heights: learned while it stands still and level */
    if (g_gripRestN < 20)
    {
        float tilt = 1 - 2 * (q[0] * q[0] + q[1] * q[1]);   /* its up axis' Z */
        if (hs < 0.3f && fabsf(v[2]) < 0.3f && tilt > 0.97f)
        {
            for (int i = 0; i < 4; i++) g_gripRest[i] = g_gripRestN ? g_gripRest[i] * 0.8f + dist[i] * 0.2f : dist[i];
            if (++g_gripRestN == 20) Log("Grip: its resting heights %.2f %.2f %.2f %.2f m (from 1 m above its corners)", g_gripRest[0], g_gripRest[1], g_gripRest[2], g_gripRest[3]);
        }
        return;
    }
    int touching = 0;
    for (int i = 0; i < 4; i++) if (dist[i] <= g_gripRest[i] + s_gripMargin) touching++;
    if (touching >= 2) return;
    float j[4] = {0, 0, 0, 0};
    if (v[2] > 0) j[2] -= v[2] * s_gripMass * s_gripCancel;
    j[2] -= s_gripMass * s_gripGravity * (float)dt;
    px.impulse(g_gripActor, j, NULL);
    g_gripCatches++;
    if (g_now - g_gripLogAt > 1.0 && g_gripCatches < 400)
    {
        g_gripLogAt = g_now;
        Log("Grip: %d of 4 corners down (gaps %.2f %.2f %.2f %.2f m), up %.1f m/s, %.1f m/s across: pushed down", touching,
            dist[0] - g_gripRest[0], dist[1] - g_gripRest[1], dist[2] - g_gripRest[2], dist[3] - g_gripRest[3], v[2], hs);
    }
}

/* ---- 0.3.9: Steady: the FRV Sinveiler's body after a hit: its roll and pitch spin and its launch held down; set upright
 * when it lies over. (0.3.8 found the bots' cannon hits carry no stagger or push in their hit result: the shove is the
 * blast's own on its physics body.) ---------------------------------------------------------------------------------- */
static unsigned g_carHeld, g_carRighted;
static double g_overSince = -1;
static void SteadyFrame(void)
{
    if (!g_frv || !g_sinner || !g_landed || !g_sinUnit) { g_overSince = -1; return; }
    if (!g_pxOk) { static double next; if (!g_pxBad && g_now >= next) { next = g_now + 5; PhysicsPins(); } return; }
    if ((!s_carSteady && !s_carRight) || !px.set_velocities) return;
    uint8_t d[24];
    if (!Descriptor(g_sinner, d) || !(Word(d + 20) & 1)) return;   /* only where it is simulated */
    if (g_gripUnit != g_sinUnit || !g_gripActor || !px.valid(g_gripActor) || px.owner(g_gripActor) != g_sinUnit)
    {
        g_gripUnit = g_sinUnit;
        g_gripActor = GripActor(g_sinUnit);
        g_gripRestN = 0;
        Log("Steady: the FRV Sinveiler's physics body %08x", g_gripActor);
        if (!g_gripActor) return;
    }
    float pos[4] = {0}, q[4] = {0}, v[4] = {0}, w[4] = {0};
    px.position(g_gripActor, pos);
    px.rotation(g_gripActor, q);
    px.velocities(g_gripActor, v, w);
    for (int i = 0; i < 3; i++) if (!isfinite(v[i]) || !isfinite(w[i]) || !isfinite(q[i]) || !isfinite(pos[i])) return;
    if (!isfinite(q[3])) return;
    if (s_carSteady && GetTickCount64() - atomic_load(&g_carHitMs) < (unsigned long long)(s_carWindow * 1000))
    {
        float wh = sqrtf(w[0] * w[0] + w[1] * w[1]);
        int held = 0;
        if (wh > s_carSpin) { float k = s_carSpin / wh; w[0] *= k; w[1] *= k; held = 1; }
        if (v[2] > s_carLift) { v[2] = s_carLift; held = 1; }
        if (held)
        {
            px.set_velocities(g_gripActor, v, w);
            if (g_carHeld++ < 8) Log("Steady: a hit's shove held (its roll and pitch spin was %.1f rad/s, now at most %.1f; up %.1f m/s)", (double)wh, (double)s_carSpin, (double)v[2]);
        }
    }
    /* lying over: set upright where it is, facing as it faced */
    float upz = 1 - 2 * (q[0] * q[0] + q[1] * q[1]);
    if (!s_carRight || upz > 0.5f) { g_overSince = -1; return; }
    if (g_overSince < 0) { g_overSince = g_now; return; }
    if (g_now - g_overSince < s_carRightAfter) return;
    g_overSince = -1;
    static const float fwdL[3] = {0, 1, 0}, rightL[3] = {1, 0, 0};
    float f[3], a;
    QRotate(q, fwdL, f);
    if (f[0] * f[0] + f[1] * f[1] > 0.04f) a = atan2f(-f[0], f[1]);
    else { QRotate(q, rightL, f); a = atan2f(f[1], f[0]); }
    float nq[4] = {0, 0, sinf(a * 0.5f), cosf(a * 0.5f)}, np[4] = {pos[0], pos[1], pos[2] + 0.8f, 0}, zero[4] = {0, 0, 0, 0};
    px.set_velocities(g_gripActor, zero, zero);
    px.teleport_position(g_gripActor, np);
    px.teleport_rotation(g_gripActor, nq);
    px.set_velocities(g_gripActor, zero, zero);
    g_carRighted++;
    Log("Steady: the FRV Sinveiler lay over (its up axis %.2f): set upright where it is", (double)upz);
}

/* ---- the skills ---------------------------------------------------------------------------------------------------- */
static int Shoot(uint32_t round, const float to[3], const char* what)
{
    float from[3], dir[3];
    if (!HerHead(from)) return 0;
    for (int k = 0; k < 3; k++) dir[k] = to[k] - from[k];
    float n = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (!(n > 0.5f)) return 0;
    for (int k = 0; k < 3; k++) { dir[k] /= n; from[k] += dir[k] * 1.2f; }
    int r = hf->spawn_projectile(self, round, from, dir);
    if (r == HF_ACT_WAITING) return -1;
    return Act(what, r);
}
static uint32_t g_eyeTarget;
static int g_eyeLeft;
static double g_eyeNext;
static void FullSpeedDrive(void)
{
    if (g_now < g_sprintCdUntil) { static unsigned told; if (told++ < 5) Log("Full Speed burst: %.0f s left on its cooldown", g_sprintCdUntil - g_now); return; }
    g_sprintUntil = g_now + s_sprintSeconds;
    g_sprintCdUntil = g_now + s_sprintCd;
    g_sprints++;
    Upshift(1, "Full Speed burst");
    SinnerAction("Full Speed burst");
    Log("Full Speed burst: %.0f s: the FRV and she take no damage, its rams x%.1f (%s)%s", (double)s_sprintSeconds, (double)s_sprintRam, kGear[g_gear],
        g_frvNet ? "" : " (its ram hits are known from its first one)");
}
static void SnowWolfsEye(void)
{
    /* 0.2.2: while she drives the FRV Sinveiler, its key is Let's Go Full Speed! */
    if (g_frv && g_sinner && g_piloting && SeatIsDriver()) { FullSpeedDrive(); return; }
    if (g_now < g_cd[0]) return;
    float at[3];
    uint32_t target;
    g_aimBlocked = 0;
    if (!Aim(s_s1Cone, s_s1Range, at, &target))
    {
        static unsigned told;
        if (told++ < 8) Log("Snow Wolf's Eye: no enemy under her crosshair (%u near it but out of sight)", g_aimBlocked);
        return;
    }
    g_cd[0] = g_now + s_s1Cd;
    g_casts[0]++;
    g_eyeTarget = target;
    g_eyeLeft = s_s1Rounds;
    g_eyeNext = g_now;
    g_plusTarget = target;
    Upshift(1, "Snow Wolf's Eye");
    g_eyeHitUntil = g_now + s_s1Rounds * (double)s_s1Every + 2;
    if (s_turboSeconds > 0) g_turboUntil = g_now + s_turboSeconds;
    Log("Snow Wolf's Eye: %d heavy rounds on entity %u (x%.2f with %d Upshift); Turbo Boost x%.2f for %.0f s", s_s1Rounds, target,
        1 + g_up * (double)s_s1Stack, g_up, (double)s_turboMult, (double)s_turboSeconds);
    if (g_sinner) Gear(GEAR_PLUS, "Snow Wolf's Eye");
}
static void EyeFrame(void)
{
    while (g_eyeLeft > 0 && g_now >= g_eyeNext)
    {
        int k = FoeIndex(g_eyeTarget);
        float q[3];
        if (k < 0 || !Alive(g_eyeTarget) || !TargetPos(g_eyeTarget, g_foeRes[k], q)) { g_eyeLeft = 0; return; }
        int r = Shoot(g_round[3], q, "an Snow Wolf's Eye round");
        if (r < 0) { g_eyeNext = g_now + 0.1; return; }
        g_eyeLeft--;
        g_eyeNext = g_now + s_s1Every;
        if (r > 0) g_eyeRounds++;
    }
}
static void TacticalRoutebook(void)
{
    if (g_now < g_cd[1]) return;
    g_cd[1] = g_now + s_s2Cd;
    g_casts[1]++;
    g_krUntil = g_now + s_krSeconds;
    Upshift(2, "Tactical Routebook");
    int healed = s_krHeal > 0 && hf->size >= offsetof(HfApi, heal) + sizeof hf->heal && hf->heal ? Act("Kinetic Recovery's heal", hf->heal(self, s_krHeal)) : 0;
    Log("Tactical Routebook: Kinetic Recovery for %.0f s (her damage and the Sinveiler's x%.2f%s)", (double)s_krSeconds, (double)s_krMult,
        s_krHeal > 0 ? (healed ? ", healed" : ", the heal refused") : "");
    if (g_sinner && g_landed)
    {
        Gear(GEAR_S, "Tactical Routebook");
        if (s_krSinnerHeal > 0 && g_sinMax > 0) SinnerSetHealth(g_sinHp + (int32_t)(g_sinMax * s_krSinnerHeal), "Kinetic Recovery");
    }
    KineticRelease("Tactical Routebook's Kinetic Release");
}
static void TheWinner(void)
{
    if (!g_landed) { static unsigned told; if (told++ < 5) Log("The Winner: the Sinveiler has not landed yet"); return; }
    if (g_now < g_winnerNext) { static unsigned told; if (told++ < 5) Log("The Winner: %.0f s left on winner_cooldown", g_winnerNext - g_now); return; }
    g_winnerNext = g_now + s_winnerCd;
    g_winners++;
    g_casts[2]++;
    int max = g_sinMax > 0 ? g_sinMax : SinnerMaxRead();
    int ok = max > 0 && SinnerSetHealth(max, "The Winner");
    int cleansed = Cleanse(g_sinner);
    Log("The Winner: the Sinveiler restored (%s), %s", ok ? "full health" : "its health could not be written",
        cleansed < 0 ? "no cleanse (not available)" : "its debuffs cleansed");
    Gear(GEAR_N, "The Winner");   /* (only from D) */
}
static void Spectre(void)
{
    /* 0.2.1: after the key is let go: the game's own seat change is on C in a vehicle, and 0.2.0's ride (on the press)
     * was followed by it 0.6 s later (driver -> passenger) */
    if (g_sinner && g_frv && g_landed && !g_piloting) { g_rideWanted = 1; return; }
    if (g_sinner) { TheWinner(); return; }
    if (g_now < g_cd[2]) return;
    if (s_frvOn && s_sinOn && HasStratagem(FRV_STRATAGEM) > 0 && HasStratagem(g_frvType) <= 0)
    {
        int r = ClearCooldown(FRV_STRATAGEM);
        if (r <= 0) { if (r < 0) Log("Spectre: her stratagem record cannot be read yet"); return; }
        g_cd[2] = g_now + s_s3Cd;
        g_casts[2]++;
        Log("Spectre: her M-103 Supply FRV is ready to call (its cooldown cleared); the one she calls becomes the Sinveiler");
        return;
    }
    if (s_frvOn && g_frvType && !FrvReady())
    { static unsigned told; if (told++ < 5) Log("Spectre: the FRV Sinveiler's package is not loaded (%s): the Patriot comes instead", atomic_load(&g_frvPkg) < 0 ? "HD2Runtime refused it" : "her HUD is loading it, or is not running"); }
    if (!s_sinOn || !CallType()) { static unsigned told; if (told++ < 3) Log("Spectre: there is no Sinveiler in this session (sinner 0, or it could not be added)"); return; }
    if (!s_callMode) { static unsigned told; if (told++ < 5) Log("Spectre: call the Sinveiler with its stratagem (%s; call_mode 0 grants it at the drop)", CallType() == g_frvType ? "RRLLU" : "RRLLD"); return; }
    int r = GrantCall(CallType(), 1);
    if (r <= 0) { if (r < 0) Log("Spectre: her stratagem record cannot be read yet"); return; }
    g_cd[2] = g_now + s_s3Cd;
    g_callPending = 1;
    g_casts[2]++;
    Log("Spectre: one %s call in her stratagems (%s)", CallType() == g_frvType ? "FRV Sinveiler" : "Sinveiler", CallType() == g_frvType ? "RRLLU" : "RRLLD");
}

/* ---- the damage event ----------------------------------------------------------------------------------------- */
static void DamageTaken(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    (void)me;
    if (d->amount > 0 && g_piloting && Sprinting()) { e->cancel = 1; g_sprintSaved++; return; }
    /* 0.3.7: in the Sinveiler nothing reaches her (Let's Go For a Spin!: its rider cannot be targeted) */
    if (d->amount > 0 && s_rideSafe && g_frv && g_sinner && g_piloting)
    {
        e->cancel = 1;
        if (g_rideSaved++ < 3) Log("Let's Go For a Spin!: a hit on her in the Sinveiler stopped (%d damage, kind %u)", d->amount, d->kind);
        return;
    }
    /* 0.3.0: at full Upshift she takes upshift_guard less */
    if (d->amount > 0 && g_up >= s_upMax && s_upGuard > 0) ScaleDamage(d, 1.0 - s_upGuard);
    if (d->amount <= 0 || d->kind == 8 || d->kind == 9 || s_sharePct <= 0 || !atomic_load(&g_shareOk)) return;
    int32_t room = atomic_load(&g_sinHpNow) - 1 - atomic_load(&g_sharePending);
    if (room <= 0) return;
    double want = floor(d->amount * (double)s_sharePct / 100.0 + 0.5);
    int32_t share = want > room ? room : (int32_t)want;
    if (share <= 0) return;
    int32_t before = d->amount;
    d->amount -= share;
    if (d->amount <= 0) { d->amount = 0; e->cancel = 1; }
    atomic_fetch_add(&g_sharePending, share);
    g_shareHits++;
    g_shared += share;
    static atomic_uint told;
    if (atomic_fetch_add(&told, 1) < 12) Log("the Sinveiler takes %d of a %d hit on her (kind %u): %d reaches her", share, before, d->kind, d->amount);
}
static unsigned g_spared;
/* 0.3.0: the buffs on her damage and the Sinveiler's: Upshift, Kinetic Recovery, Turbo Boost, Post-Move Support */
static double Buffs(void)
{
    double m = 1 + g_up * (double)s_upDamage;
    if (g_now < g_krUntil) m *= s_krMult;
    if (g_now < g_turboUntil) m *= s_turboMult;
    if (g_sinner && g_landed) m *= s_postMove;
    return m;
}
static void DamageDealt(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    (void)me;
    uint32_t fsin = g_sinner;
    if (fsin && d->target == fsin && d->amount > 0 && !MineHit(d))
    {
        /* 0.3.0 Gear Shift, either Sinveiler: none in N; sinner_taken of it, times park_taken standing still; never fatal;
         * a hit heals it (GearFrame) */
        if (Sprinting() || g_gear == GEAR_N) { e->cancel = 1; if (g_gear == GEAR_N) g_gearSaved++; else g_sprintSaved++; return; }
        /* 0.3.13: its own HE shells (their blast is not always counted as hers) */
        if ((d->kind == 4 && g_now - g_heShotAt < 2.5) || g_now - g_heShotAt < 0.4)   /* a blast within 2.5 s, anything within 0.4 s */
        {
            e->cancel = 1;
            static atomic_uint told;
            if (atomic_fetch_add(&told, 1) < 6) Log("a hit on the Sinveiler right after its own HE shell (kind %u, %d damage) stopped", d->kind, d->amount);
            return;
        }
        double m = s_sinTaken * (g_parked ? s_parkTaken : 1.0);
        if (m != 1) ScaleDamage(d, m);
        int32_t hp = atomic_load(&g_sinHpNow) - atomic_load(&g_sharePending);
        if (hp > 1 && d->amount >= hp) { d->amount = hp - 1; g_noDeath++; }
        atomic_store(&g_repairPending, 1);
        return;
    }
    if (g_frv && fsin && d->amount > 0 && !MineHit(d))
    {
        /* its turret's hits: by gear, with her buffs */
        if (!g_turretNet && g_turretEnt && NetOk(d->dealer) && d->target == atomic_load(&g_turretSeenTarget) && GetTickCount64() - atomic_load(&g_turretSeenMs) <= 300)
        {
            g_turretNet = d->dealer;
            Log("its turret's hits: dealer network %u (owner %u), learned from the game's hit code", d->dealer, d->owner);
        }
        if (!g_frvNet && NetOk(d->dealer) && d->dealer != g_turretNet && d->target == atomic_load(&g_frvSeenTarget) && GetTickCount64() - atomic_load(&g_frvSeenMs) <= 300)
        {
            g_frvNet = d->dealer;
            Log("the FRV's own hits (ramming): dealer network %u (owner %u, kind %u), learned from the game's hit code", d->dealer, d->owner, d->kind);
        }
        if (g_frvNet && d->dealer == g_frvNet && d->target != atomic_load(&g_avatar))
        {
            /* its ramming: more in a sprint */
            double m = (Sprinting() ? s_sprintRam * (g_gear == GEAR_PLUS ? s_sprintRamPlus : g_gear == GEAR_S ? s_sprintRamS : 1.0) : 1.0) * RamBonus();
            if (m != 1)
            {
                m *= Buffs();
                ScaleDamage(d, m);
                static atomic_uint told;
                if (atomic_fetch_add(&told, 1) < 6) Log("a ram on entity %u (kind %u, %.0f m/s%s) x%.2f -> %d", d->target, d->kind, (double)g_speed, Sprinting() ? ", Full Speed burst" : "", m, d->amount);
            }
            g_rams++;
            return;
        }
        if (g_turretNet && d->dealer == g_turretNet)
        {
            double m = GearDamage() * Buffs();
            if (m != 1) ScaleDamage(d, m);
            static atomic_uint told;
            if (atomic_fetch_add(&told, 1) < 6) Log("its turret hits entity %u (dealer %u, owner %u, %s): x%.2f", d->target, d->dealer, d->owner, kGear[g_gear], m);
            g_turretHits++;
            return;
        }
    }
    {
        /* Kinetic Release's burst on an enemy in its reach: release_damage (S+: release_damage_plus), once */
        int ri = d->kind == 4 ? RelIndex(d->target) : -1;
        if (ri >= 0 && d->target != fsin && !atomic_exchange(&g_relPaid[ri], 1))
        {
            int32_t was = d->amount;
            d->amount = (int32_t)(atomic_load(&g_relDmg) * GearDamage() * Buffs() + 0.5);
            static atomic_uint told;
            if (atomic_fetch_add(&told, 1) < 8) Log("an EMS wave: entity %u takes %d (was %d)", d->target, d->amount, was);
            return;
        }
    }
    if (d->amount <= 0 || !MineHit(d)) return;
    uint32_t sin = g_sinner;
    /* her hits (and the Sinveiler's rounds, which are hers) spare the Sinveiler, and its rounds spare her allies */
    if (d->kind == 4 && g_now < g_relUntil && Friend(d->target))
    {
        e->cancel = 1;
        if (g_spared++ < 8) Log("Kinetic Release's burst on an ally cancelled (entity %u, %d damage)", d->target, d->amount);
        return;
    }
    if (g_now - g_tracerAt < 1.5 && (d->kind == 0 || d->kind == 1) && Friend(d->target)) { e->cancel = 1; g_spared++; return; }   /* 0.3.12: its tracers */
    if (sin && (d->target == sin || (SinnerShotNear(d->position) && Friend(d->target))))
    {
        e->cancel = 1;
        if (g_spared++ < 8) Log("a hit of hers on %s cancelled (entity %u, %d damage)", d->target == sin ? "the Sinveiler" : "an ally", d->target, d->amount);
        return;
    }
    double mult = Buffs();
    int sinShot = SinnerShotNear(d->position);
    if (sinShot) mult *= GearDamage();   /* its rounds (the Patriot's, Drift's, Full Speed's): by gear */
    if (sinShot && IsShot(d)) HeHit(d->position);
    else if (d->target == g_eyeTarget && g_now < g_eyeHitUntil && IsShot(d)) mult *= 1 + g_up * (double)s_s1Stack;   /* Snow Wolf's Eye */
    /* 0.2.2: a ram credited to her as the driver (an impact while she drives in a sprint) */
    if (Sprinting() && g_piloting && d->kind == 5) mult *= s_sprintRam * (g_gear == GEAR_PLUS ? s_sprintRamPlus : g_gear == GEAR_S ? s_sprintRamS : 1.0);
    if (g_piloting && g_frv && d->kind == 5) mult *= RamBonus();
    if (mult != 1) ScaleDamage(d, mult);
    if (IsShot(d) && !SinnerShotNear(d->position)) NoteTarget(d);
    if (SinnerShotNear(d->position) && (d->kind == 0 || d->kind == 1))
    {
        /* the gap: the nearest start among its rounds of the last second */
        float best = 1e9f;
        double age = 0;
        for (int k = 0; k < 32; k++)
        {
            if (g_shotFromAt[k] <= 0 || g_now - g_shotFromAt[k] > 1.0) continue;
            float dd = sqrtf(Dist2(d->position, g_shotFrom[k]));
            if (dd < best) { best = dd; age = g_now - g_shotFromAt[k]; }
        }
        if (best < 1e8f)
        {
            g_gapHits++;
            if (best < g_gapMin) g_gapMin = best;
            g_gapBins[best < 5 ? 0 : best < 10 ? 1 : best < 20 ? 2 : best < 40 ? 3 : 4]++;
            static unsigned told;
            if (told++ < 10) Log("a Sinveiler round hit %.1f m from where it started, %.3f s after (%.0f m/s)", best, age, age > 0 ? best / age : 0.0);
        }
    }
}
/* a hit on her (the hit hook): who dealt it, for the N gear */
static int HitTaken(const uint8_t* h, void* result, unsigned long long now)
{
    (void)result;
    uint32_t me = atomic_load(&g_avatar), dealer = Word(h + 0x80), owner = Word(h + 0x100), who = owner && owner != me ? owner : dealer;
    if (who && who != me && who != g_sinner) { atomic_store(&g_threat, who); atomic_store(&g_threatMs, now); }
    return 0;
}

/* ---- events ------------------------------------------------------------------------------------------------------ */
/* 0.4.1: enemies the kit base's test leaves out (Lemuen's): an enemy's turret (an Automaton cannon tower, a War Strider:
 * a turret that runs a behaviour and is no friend) and anything with the AI's enemy record and health. The Sinveiler
 * would not shoot a War Strider */
static int FoeAlso(uint64_t res)
{
    uint32_t stride;
    if (res == AVATAR || hf->component_record("AvatarComponentData", res, &stride) || FriendRes(res)) return 0;
    int turret = hf->component_record("TurretComponentData", res, &stride) != 0 && hf->component_record("BehaviorComponentData", res, &stride) != 0;
    int ai = hf->component_record("AiEnemyComponentData", res, &stride) != 0 && hf->component_record("HealthComponentData", res, &stride) != 0;
    if (turret || ai)
    {
        static uint64_t told[12];
        static unsigned n;
        int seen = 0;
        for (unsigned k = 0; k < n; k++) if (told[k] == res) seen = 1;
        if (!seen && n < 12) { told[n++] = res; Log("an enemy kind the kit base leaves out: %016llx (%s): one of her foes", (unsigned long long)res, turret ? "an enemy turret" : "an AI enemy"); }
    }
    return turret || ai;
}
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    g_spawned[g_nspawned % 64].entity = s->entity;
    g_spawned[g_nspawned % 64].at = g_now;
    g_nspawned++;
    if (g_sinRes && s->resource == g_sinRes)
    {
        if (!g_inMission || !kb_kit_on || !s->authority) return;
        if (g_callPending || !s_callMode || !g_sinner) Adopt(s->entity);
        return;
    }
    if (s->resource == FRV_TURRET)
    {
        g_turretCandNet = s->network; g_turretCand = s->entity; g_turretCandAt = g_now;
        if (g_frv && g_sinner && !g_turretEnt && g_now - g_sinBorn < 2) { g_turretEnt = s->entity; if (NetOk(s->network)) g_turretNet = s->network; }
        return;
    }
    if (s->resource == FRV_RES)
    {
        /* 0.2.0: her M-103, with it in her loadout and no Sinveiler standing: the FRV Sinveiler */
        if (!g_inMission || !kb_kit_on || !s->authority || !s_frvOn || !s_sinOn || g_sinner) return;
        if (!g_callPending && HasStratagem(FRV_STRATAGEM) <= 0 && HasStratagem(g_frvType) <= 0) return;
        Adopt(s->entity);
        g_frv = 1;
        g_frvNet = s->network;
        g_turretEnt = g_now - g_turretCandAt < 2 ? g_turretCand : 0;
        g_turretNet = g_turretEnt && NetOk(g_turretCandNet) ? g_turretCandNet : 0;
        if (!NetOk(g_frvNet)) g_frvNet = 0;
        Log("the FRV Sinveiler: her M-103 (entity %u), its turret entity %u (its network id learned from its first hits)", s->entity, g_turretEnt);
        return;
    }
    if (s->resource != AVATAR) { if (FoeKind(s->resource) || FoeAlso(s->resource)) AddFoe(s->entity, s->resource); return; }
    if (!s->authority) return;
    g_spawnEntity[g_spawnNext % 8] = s->entity;
    g_spawnNet[g_spawnNext % 8] = s->network;
    g_spawnNext++;
    LearnNet();
}
static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfKillEvent* k = (const HfKillEvent*)info;
    LearnDeadLife(k->victim);
    DeadAdd(k->victim, 60);
    DropFoe(k->victim);
    if (k->victim == g_lastTarget) g_lastTarget = 0;
    if (k->victim == g_plusTarget) g_plusTarget = 0;
    /* (the Sinveiler's death is seen on the game thread: SinnerFrame) */
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
    else { g_alive = 0; g_krUntil = 0; }
}
static void Reset(const char* why)
{
    int start = !strcmp(why, "mission start");
    if (start) KitDecide();
    if (g_casts[0] || g_casts[1] || g_casts[2] || g_volleys)
        Log("%s: casts %u / %u / %u, %u Eye rounds, %u Sinveiler volleys, %u The Winner, Upshift gained %u, %u hits shared (%.0f damage), "
            "%u health writes, %u slides, %u warps, %u push steps, %u pulls, %u hits of hers cancelled, %u path sends",
            why, g_casts[0], g_casts[1], g_casts[2], g_eyeRounds, g_volleys, g_winners, g_upGained, g_shareHits, g_shared, g_healthWrites,
            g_slides, g_warps, g_pushes, g_pulls, g_spared, g_navSends);
    if (g_rocketsFired) Log("%s: %u rockets fired", why, g_rocketsFired);
    g_rocketsFired = 0;
    if (g_gapHits)
        Log("%s: its rounds' hits by distance from where they started: %u under 5 m, %u 5-10, %u 10-20, %u 20-40, %u beyond (nearest %.1f m)",
            why, g_gapBins[0], g_gapBins[1], g_gapBins[2], g_gapBins[3], g_gapBins[4], (double)g_gapMin);
    g_gapHits = 0; memset(g_gapBins, 0, sizeof g_gapBins); g_gapMin = 1e9f;
    Body(start);
    if (start && s_hellbomb && s_hellbombRow == 125 && !hf->load_package(self, UINT64_C(0x346D785D6CA13FF4))) Log("the Hellbomb's package could not be loaded ahead");
    g_boomPending = 0;
    g_boomArmed = 0;
    if (g_sinner) Log("the Sinveiler (entity %u) is gone with the mission", g_sinner);
    if (g_turretHits || g_rides || g_releases || g_noDeath)
        Log("%s: the FRV Sinveiler: %u turret hits, %u rides, %u Kinetic Releases, %u fatal hits survived", why, g_turretHits, g_rides, g_releases, g_noDeath);
    if (g_rideSaved) Log("%s: %u hits on her stopped in the Sinveiler", why, g_rideSaved);
    g_rideSaved = 0;
    if (g_tracers) Log("%s: %u Full Speed tracers", why, g_tracers);
    g_tracers = 0;
    if (g_heBlasts || g_heDropped) Log("%s: %u HE blasts (%u where a round hit, %u over he_rate, left out)", why, g_heBlasts, g_heOnHit, g_heDropped);
    g_heBlasts = g_heDropped = g_heOnHit = 0;
    for (int i = 0; i < HE_Q; i++) g_he[i].when = 0;
    if (g_drifts || g_fulls || g_gearSaved || g_heals)
        Log("%s: %u Drifts, %u Full Speeds, %u of their shots, %u hits on the Sinveiler stopped in N, %u Gear Shift heals", why, g_drifts, g_fulls, g_driftShots, g_gearSaved, g_heals);
    g_drifts = g_fulls = g_driftShots = g_gearSaved = g_heals = 0;
    if (g_sprints) Log("%s: %u sprints, %u rams, %u hits stopped in sprints", why, g_sprints, g_rams, g_sprintSaved);
    if (g_carSpared) Log("%s: her own blasts on the FRV Sinveiler emptied %u times", why, g_carSpared);
    g_carSpared = 0;
    if (g_carHeld || g_carRighted) Log("%s: Steady held %u shoves on the FRV Sinveiler, set it upright %u times", why, g_carHeld, g_carRighted);
    g_carHeld = g_carRighted = 0;
    { unsigned n = atomic_exchange(&g_carSteadied, 0); if (n) Log("%s: stagger or push removed from %u hits on the Sinveiler", why, n); }
    if (g_gripCatches) Log("%s: Grip pushed the FRV Sinveiler down in %u frames", why, g_gripCatches);
    g_gripCatches = 0; g_gripActor = g_gripUnit = 0; g_gripRestN = 0;
    g_turretHits = g_rides = g_releases = g_noDeath = 0;
    g_sprints = g_rams = g_sprintSaved = 0;
    g_sprintUntil = g_sprintCdUntil = 0;
    g_frv = 0;
    g_frvNet = g_turretNet = g_turretEnt = 0;
    g_frvParked = 0;
    g_rideWanted = 0;
    g_sinner = g_sinUnit = 0;   /* (no drive stop: its world is gone) */
    g_nparts = 0;
    g_sinPosOk = 0;
    g_landed = g_winnerUsed = g_piloting = g_driving = g_driveTried = 0;
    atomic_store(&g_sinHpNow, 0);
    atomic_store(&g_sharePending, 0);
    atomic_store(&g_shareOk, 0);
    g_callPending = 0;
    g_grantDone = g_grantTries = 0;
    g_grantAt = 0;
    memset(g_cd, 0, sizeof g_cd);
    memset(g_casts, 0, sizeof g_casts);
    g_volleys = g_eyeRounds = g_winners = g_warps = g_pulls = g_upGained = g_shareHits = g_healthWrites = g_slides = g_spared = 0;
    g_moveRefused = 0;
    g_pushes = 0;
    g_pushing = 0;
    g_slowFor = 0;
    g_navSends = 0;
    g_navSentAt = -100;
    g_shared = 0;
    g_up = 0;
    g_krUntil = g_turboUntil = g_eyeHitUntil = 0;
    g_gear = GEAR_D;
    g_gearUntil = 0;
    g_fightAt = -100;
    g_parked = 0;
    g_winnerNext = 0;
    g_wave.on = 0;
    g_relUntil = 0;
    g_dqn = g_dqi = 0;
    g_speed = 0; g_wayAt = -100;
    g_driftNext = g_releaseNext = g_healNext = 0;
    g_eyeLeft = 0;
    g_ndrag = 0;
    g_moving = g_leash = 0;
    g_plusTarget = 0;
    g_lastTarget = 0;
    atomic_store(&g_threat, 0);
    memset(g_shotAimAt, 0, sizeof g_shotAimAt);
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gameSize = 0, exeSize = 0;
    uintptr_t g = hf->module_base("game.dll", &gameSize), x = hf->module_base("helldivers2.exe", &exeSize);
    if (!g || !x || gameSize != 74727424u || exeSize != 60719104u) { Log("REFUSED: the game's modules are not build " BUILD "'s: her kit stays off"); return; }
    game = g;
    exe = x;
    HitsInstall("own-blast guard");
    SeatPins();
    PhysicsPins();
    StatusInstall();
    if (hf->code_matches(exe + 0x79F860, "33 c0 4c 8d 05 c7 b2 01 02 0f 1f 80 00 00 00 00 49 39 0c c0 74 0c ff c0 83 f8 04 75 f3") &&
        hf->code_matches(exe + 0x7F4590, "48 8b c4 f3 0f 11 58 20 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48"))
    {
        g_worldId = (WorldFn)(exe + 0x79F860);
        g_raycast = (RayFn)(exe + 0x7F4590);
        Log("line of sight ready (the game's raycast)");
    }
    else Log("line of sight off: the raycast is not the reviewed code (aims go through walls; the Sinveiler neither slides nor warps)");
    if (g_raycast && hf->code_matches(game + 0x5A2BB0, "48 89 5c 24 08 48 89 6c 24 10 56 57 41 54 41 56 41 57 48 83 ec 40") &&
        hf->code_matches(game + 0x4A7550, "48 89 5c 24 18 57 48 83 ec 20 3b 15 ?? ?? ?? ?? 48 8b 1d"))
    {
        g_setPosition = (SetVecFn)(game + 0x5A2BB0);
        g_setVelocity = (SetVecFn)(game + 0x4A7550);
        Log("the warp setter ready (the Sinveiler's slide and leash; warp_needs_walks %d)", s_warpWalks);
    }
    else Log("the warp setter off: its functions are not the reviewed code (the Sinveiler stands where it lands)");
    StatusPrepare();
    if (Boundary(game + 0x5B36C0)) g_setDest = (SetDestFn)(game + 0x5B36C0);
    Log("Sinveiler pathing: set_destination (game.dll+0x5B36C0) %s; navigation records [game+0x3326418] = %p", g_setDest ? "on" : "off (not a function start)",
        (void*)Pointer(game + 0x3326418));
    Log("Sinveiler drive: %s", SinnerDriveReady() ? "the locomotion record's layout is set in koleda.ini" : "the locomotion record's layout is not known (the slide instead)");
    g_staminaOk = hf->code_matches(game + 0xA50E2D, "f3 42 0f 11 8c 11 54 78 54 00");
}

/* ---- for the HUD: the Sinveiler's marker, her view, her state ------------------------------------------------------- */
static float g_viewOut[6];
static volatile int g_viewOk;
static float g_markOut[5];
static volatile int g_markOk;
static void HudFrame(void)
{
    float c[3], f[3];
    int fp;
    if (View(c, f, &fp)) { memcpy(g_viewOut, c, 12); memcpy(g_viewOut + 3, f, 12); g_viewOk = 1; }
    else g_viewOk = 0;
    if (g_sinner && g_landed && g_sinPosOk)
    {
        g_markOut[0] = g_sinPos[0]; g_markOut[1] = g_sinPos[1]; g_markOut[2] = g_sinPos[2] + 3.2f;
        g_markOut[3] = g_sinMax > 0 ? (float)g_sinHp / (float)g_sinMax : 1;
        g_markOut[4] = (float)(g_gear == GEAR_D && g_parked ? 4 : g_gear);
        g_markOk = 1;
    }
    else g_markOk = 0;
}
typedef struct KoledaHud
{
    uint32_t version, in_mission;
    float cd[3], cd_max[3];            /* seconds left, full cooldowns: Snow Wolf's Eye, Tactical Routebook, Spectre */
    uint32_t upshift, upshift_max;
    uint32_t gear;                     /* 0 N, 1 S, 2 S+ */
    uint32_t sinner;                   /* 0 none, 1 called (a call in her list, or dropping), 2 standing */
    float sinner_hp, sinner_max;
    uint32_t leash;                    /* 0 idle, 1 following, 2 warped beside her, 3 cannot be moved, 4 pushed through */
    uint32_t drive;                    /* 1 the game's locomotion, 0 the slide */
    uint32_t winner;                   /* 1 The Winner ready (C) */
    uint32_t piloted;                  /* 1 she is in it */
    float kr_left;                     /* Kinetic Recovery, seconds left */
    uint32_t keys[3];
    char key_label[3][24];
    /* 0.2.6: her buffs, for the HUD's buff row */
    uint32_t sinner_kind;              /* the picked Sinveiler: 0 the Patriot, 1 the FRV */
    uint32_t frv;                      /* 1 the standing Sinveiler is the FRV */
    float sprint_left, sprint_cd;      /* Let's Go Full Speed!: seconds left, seconds until it can be used again */
    float upshift_pct;                 /* her damage bonus from Upshift, % */
    float kr_mult;                     /* Kinetic Recovery's multiplier */
    float share_pct;                   /* % of each hit on her the Sinveiler takes (while it may) */
    uint32_t share_on;                 /* 1 the share runs now */
    uint32_t ems_immune;               /* 1 no EMS stun on her (and the FRV Sinveiler) */
    uint32_t release_on;               /* 1 Kinetic Release runs (the FRV Sinveiler's turret in S / S+) */
    float sprint_ram;                  /* its rams, times this, while sprinting */
    int32_t frv_pkg;                   /* 0.2.8: the FRV's package: 1 resident, 0 loading, -1 refused */
    /* 0.3.0 */
    float turbo_left, turbo_mult;      /* Turbo Boost: seconds left, its multiplier */
    float post_move;                   /* Post-Move Support's multiplier while the Sinveiler stands (0: not now) */
    float gear_left;                   /* seconds until the gear goes back to D (0: D) */
    float winner_cd;                   /* seconds until The Winner again */
    float upshift_guard;               /* % less damage she takes now (full Upshift) */
    float full_speed, full_ram;        /* 0.3.2: Full Speed firing now: its speed m/s (0: not now), its rams' multiplier */
    float drift_cd;                    /* 0.3.4: seconds left on Drift (-1: no Sinveiler standing / off) */
    char drift_key[24];                /* (0.3.5: unused) */
    uint32_t mounted;                  /* 0.3.5: in the FRV Sinveiler: cd / cd_max are Full Speed's, Drift's, The Winner's */
    float stamina;                     /* 0.3.13: her stamina 0 .. 1 (-1: not now), for the warning */
    uint32_t hellbomb;                 /* hellbomb_on_exhaustion */
} KoledaHud;
static KoledaHud g_hud = {1};
/* the Sinveiler, 5 floats (x, y, z over it, health 0..1, gear): 1 while it stands, else 0 */
/* 0.1.5: the Sinveiler's shots since `after` for their sounds: 5 floats each (x, y, z, the sound, sequence); its name */
HF_MOD_EXPORT int koleda_sounds(unsigned version, unsigned after, float* out, int max)
{
    if (version != 1 || !out || max <= 0) return 0;
    return ShotSounds(after, out, max);
}
HF_MOD_EXPORT const char* koleda_sound_name(int i)
{
    return i >= 0 && i < (int)(sizeof kWeaponSound / sizeof kWeaponSound[0]) ? kWeaponSound[i].name : NULL;
}
/* 0.1.7: walking it (the HUD side, through its animation: Exo Autopilot by marti's method): 12 floats: active (landed,
 * nobody in it, this kit acting), want to move, the goal (3), the Sinveiler's root (3), facing a target (1/0), the target's
 * direction (2), unused. koleda_walk_ack(1, 1) each frame it walks and answers: the slide stays off meanwhile */
HF_MOD_EXPORT int koleda_walk(unsigned version, float* out)
{
    if (version != 1 || !out) return 0;
    memset(out, 0, 12 * sizeof(float));
    int active = g_inMission && kb_kit_on && g_sinner && g_landed && !g_piloting && g_sinPosOk && !g_frv;
    out[0] = (float)active;
    if (!active) return 1;
    out[1] = (float)g_walkWant;
    memcpy(out + 2, g_walkGoal, 12);
    memcpy(out + 5, g_sinPos, 12);
    out[8] = g_now - g_faceAt < 1.0 ? 1.0f : 0.0f;
    out[9] = g_faceTo[0]; out[10] = g_faceTo[1];
    return 1;
}
HF_MOD_EXPORT int koleda_walk_ack(unsigned version, int walking)
{
    if (version != 1) return 0;
    if (walking) g_walkAckAt = g_now;
    static int told;
    if (walking && !told) { told = 1; Log("the HUD side walks the Sinveiler (its own legs): the slide is off while it does"); }
    return 1;
}
HF_MOD_EXPORT int koleda_marks(unsigned version, float* out, int max)
{
    if (version != 1 || !out || max <= 0 || !g_inMission || !kb_kit_on || !g_markOk) return 0;
    memcpy(out, g_markOut, sizeof g_markOut);
    return 1;
}
HF_MOD_EXPORT int koleda_view(unsigned version, float* out)
{
    if (version != 1 || !out || !g_viewOk || !g_inMission) return 0;
    memcpy(out, g_viewOut, 24);
    return 1;
}
HF_MOD_EXPORT KoledaHud* koleda_hud_state(void)
{
    g_hud.in_mission = (uint32_t)(g_inMission && g_alive && kb_kit_on);
    float m[3] = {s_s1Cd, s_s2Cd, s_s3Cd};
    for (int i = 0; i < 3; i++) { g_hud.cd[i] = (float)fmax(0, g_cd[i] - g_now); g_hud.cd_max[i] = m[i]; }
    g_hud.mounted = (uint32_t)Mounted();
    g_hud.stamina = g_inMission && g_alive ? StaminaNow() : -1;
    g_hud.hellbomb = (uint32_t)(s_hellbomb != 0);
    if (g_hud.mounted)
    {
        g_hud.cd[0] = (float)fmax(0, g_sprintCdUntil - g_now); g_hud.cd_max[0] = s_sprintCd;
        g_hud.cd[1] = (float)fmax(0, g_driftNext - g_now); g_hud.cd_max[1] = s_driftEvery;
        g_hud.cd[2] = (float)fmax(0, g_winnerNext - g_now); g_hud.cd_max[2] = s_winnerCd;
    }
    g_hud.upshift = (uint32_t)g_up;
    g_hud.upshift_max = (uint32_t)s_upMax;
    g_hud.gear = (uint32_t)(g_gear == GEAR_D && g_parked && g_sinner ? 4 : g_gear);   /* 3 D, 4 P */
    g_hud.sinner = g_sinner && g_landed ? 2u : (g_sinner || g_callPending) ? 1u : 0u;
    g_hud.sinner_hp = (float)g_sinHp;
    g_hud.sinner_max = (float)g_sinMax;
    g_hud.leash = (uint32_t)g_leash;
    g_hud.drive = (uint32_t)g_driving;
    g_hud.winner = (uint32_t)(g_sinner && g_landed && g_now >= g_winnerNext);
    g_hud.piloted = (uint32_t)g_piloting;
    g_hud.kr_left = (float)fmax(0, g_krUntil - g_now);
    g_hud.sinner_kind = (uint32_t)(s_frvOn != 0);
    g_hud.frv_pkg = !g_inMission || !s_frvOn ? 0 : HasStratagem(FRV_STRATAGEM) > 0 ? 1 : -1;   /* (0.2.12: 1 her loadout has the M-103, -1 not) */
    g_hud.frv = (uint32_t)(g_frv && g_sinner);
    g_hud.sprint_left = Sprinting() ? (float)(g_sprintUntil - g_now) : 0;
    g_hud.sprint_cd = (float)fmax(0, g_sprintCdUntil - g_now);
    g_hud.upshift_pct = (float)(g_up * s_upDamage * 100.0);
    g_hud.kr_mult = s_krMult;
    g_hud.share_pct = s_sharePct;
    g_hud.share_on = (uint32_t)(atomic_load(&g_shareOk) != 0 && s_sharePct > 0);
    g_hud.ems_immune = (uint32_t)(s_emsImmune != 0);
    g_hud.release_on = (uint32_t)(g_sinner && g_landed && (g_gear == GEAR_S || g_gear == GEAR_PLUS) && s_releaseShell + s_releaseRow > 0);
    g_hud.turbo_left = (float)fmax(0, g_turboUntil - g_now);
    g_hud.turbo_mult = s_turboMult;
    g_hud.post_move = g_sinner && g_landed ? s_postMove : 0;
    g_hud.gear_left = g_gear != GEAR_D && g_gearUntil > 0 ? (float)fmax(0, g_gearUntil - g_now) : 0;
    g_hud.winner_cd = (float)fmax(0, g_winnerNext - g_now);
    g_hud.upshift_guard = g_up >= s_upMax ? s_upGuard * 100 : 0;
    g_hud.full_speed = g_sinner && s_fullOn && g_gear != GEAR_N && g_speed >= s_fullMinSpeed ? g_speed : 0;
    g_hud.full_ram = RamBonus();
    g_hud.drift_cd = s_driftOn && g_sinner && g_landed ? (float)fmax(0, g_driftNext - g_now) : -1;
    g_hud.sprint_ram = (float)(s_sprintRam * (g_gear == GEAR_PLUS ? s_sprintRamPlus : g_gear == GEAR_S ? s_sprintRamS : 1.0));
    for (int i = 0; i < 3; i++)
    {
        g_hud.keys[i] = (uint32_t)s_keys[i].vk;
        memcpy(g_hud.key_label[i], s_keys[i].label, sizeof g_hud.key_label[i]);
    }
    return &g_hud;
}
static atomic_int g_bindPress;
HF_MOD_EXPORT int koleda_input(unsigned version, int skill)
{
    if (version != 1) return 0;
    if (skill >= 1 && skill <= 3) atomic_fetch_or(&g_bindPress, 1 << (skill - 1));
    return 1;
}
HF_MOD_EXPORT int koleda_key(unsigned version, int skill, int* vk, int* mods)
{
    if (version != 1 || skill < 1 || skill > 3 || !vk || !mods) return 0;
    *vk = s_keys[skill - 1].vk;
    *mods = s_keys[skill - 1].mods;
    return 1;
}

/* ---- the frame -------------------------------------------------------------------------------------------------- */
static void Tick(HfMod* mod, float dt, void* user)
{
    KbOptionsTick();   /* the options screen's changes, in a mission or not */
    double step = dt > 0 && dt < 1 ? dt : 0;
    g_now += step;
    int inMission = hf->game_state(self) == HF_STATE_MISSION;
    if (inMission != g_inMission) { Reset(inMission ? "mission start" : "mission end"); g_inMission = inMission; }
    if (!g_inMission || !kb_kit_on) return;
    MyItems();
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
            if (Mounted())   /* 0.3.5: in the FRV Sinveiler, its skills */
            {
                if (i == 0) { if (SeatIsDriver()) FullSpeedDrive(); else { static unsigned told; if (told++ < 5) Log("Let's Go Full Speed!: only at the wheel"); } }
                else if (i == 1) Drift();
                else TheWinner();
            }
            else if (i == 0) SnowWolfsEye();
            else if (i == 1) TacticalRoutebook();
            else Spectre();
        }
        prev[i] = down;
    }
    {
        static double upAt;
        if (prev[2]) upAt = g_now;
        if (g_rideWanted && !prev[2] && g_now - upAt >= 0.15) { g_rideWanted = 0; if (g_sinner && g_frv && !g_piloting) Ride(); }
    }
    UpshiftFrame();
    SinnerFrame();
    if (g_alive) EyeFrame();
    MoveFrame(step);
    FireFrame();
    TurnFrame();
    PullFrame();
    FrvFrame();
    SteadyFrame();
    GearFrame();
    WaveFrame();
    DriftFrame();
    FullFrame();
    HeFrame(step);
    GripFrame(step);
    HudFrame();
    Exhaustion();
    atomic_exchange(&g_ownStopped, 0);
    atomic_exchange(&g_kdStopped, 0);
}

/* ---- Mod Options Menu ------------------------------------------------------------------------------------------- */
static const KbOption kOptions[] = {
    {"sinner_kind", NULL, &s_frvOn},
    {"s1_cooldown", &s_s1Cd, NULL}, {"s1_rounds", NULL, &s_s1Rounds}, {"s2_cooldown", &s_s2Cd, NULL}, {"kr_seconds", &s_krSeconds, NULL},
    {"kr_mult", &s_krMult, NULL}, {"s3_cooldown", &s_s3Cd, NULL}, {"share_pct", &s_sharePct, NULL}, {"upshift_max", NULL, &s_upMax},
    {"upshift_damage", &s_upDamage, NULL}, {"upshift_decay", &s_upDecay, NULL}, {"leash_start", &s_leashStart, NULL}, {"leash_stop", &s_leashStop, NULL},
    {"leash_warp", &s_leashWarp, NULL}, {"slide_speed", &s_slideSpeed, NULL}, {"gun_every", &s_gunEvery, NULL}, {"plus_rate", &s_plusRate, NULL},
    {"fire_range", &s_fireRange, NULL}, {"guard_range", &s_guardRange, NULL}, {"sinner_fire", NULL, &s_sinFire}, {"drift_on", NULL, &s_driftOn}, {"full_on", NULL, &s_fullOn}, {"full_min_speed", &s_fullMinSpeed, NULL},
    {"gear_seconds", &s_gearSeconds, NULL}, {"winner_cooldown", &s_winnerCd, NULL}, {"release_every", &s_releaseEvery, NULL}, {"drift_cooldown", &s_driftEvery, NULL},
    {"hp_mult", &s_hpMult, NULL}, {"stamina_mult", &s_staminaMult, NULL},
};
static void OptionAfter(const char* name, int changed) { (void)name; (void)changed; }
HF_MOD_EXPORT int koleda_option_get(const char* name, double* out) { return OptionGet(name, out); }
HF_MOD_EXPORT int koleda_option_set(const char* name, double value) { return OptionSet(name, value); }

#include "kit_options.h"   /* kOpRows: the options screen's rows (kitbase/kb_options.h) */
#include "kit_icon.h"      /* kOpIcon, KB_OP_ACCENT */
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    RosterJoin("koleda", "Koleda");
    kb_ini = "koleda.ini";
    kb_key_names[0] = "key_s1";
    kb_key_names[1] = "key_s2";
    kb_key_names[2] = "key_s3";
    KB_SETTINGS(kSettings);
    KB_OPTIONS(kOptions);
    kb_option_after = OptionAfter;
    kb_damage_taken = DamageTaken;
    kb_damage_dealt = DamageDealt;
    kb_hit_before = TurretSeen;
    kb_hit_after = CarSpared;
    kb_hit_taken = HitTaken;
    /* her body: the T-Doll package (the kit base's defaults: hp_mult 1.5, stamina_mult 2, the stamina Hellbomb) */
    if (!api || api->version < 13 || api->size < offsetof(HfApi, code_matches) + sizeof api->code_matches) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    LoadSettings();
    KbOptionsStart(api, mod, "koleda", "Koleda", VERSION, kOpRows, (int)(sizeof kOpRows / sizeof kOpRows[0]), kOpIcon, KB_OP_ICON, KB_OP_ACCENT, -1);
    RoundsAdd();
    SinnerAdd(api, mod);
    /* (0.2.12: no FRV Sinveiler call of her own: its package could not be loaded reliably. The M-103 she brings in her
     * loadout is the FRV Sinveiler) */
    api->log(mod, "Koleda %s: keys %s / %s / %s (Snow Wolf's Eye, Tactical Routebook, Spectre); the Sinveiler takes %.0f%% of her hits",
             VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, (double)s_sharePct);
    if (s_badKey) api->log(mod, "koleda.ini: a key could not be read (bits %d: s1, s2, s3); its default stays", s_badKey);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) && api->subscribe(mod, HF_ON_EXPLOSION, OnBoom, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) &&
             api->on_game_thread(mod, Tick, NULL);
    if (!ok) api->log(mod, "events or the game thread are unavailable: Koleda stays off");
    return 0;
}
