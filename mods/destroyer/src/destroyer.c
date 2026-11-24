/* Destroyer (yui.destroyer): Destroyer's kit from Girls' Frontline 2 as abilities on keys, for your own Helldiver (solo
 * or host). The plan and what was decided: docs/destroyer-design.md.
 *
 * She pays for her skills with her own health (never below cost_floor of it), and the less she has the harder she hits.
 *   Annihilation Directive (key_s1, default Z): one key, three stages. A leap to where she aims with a blast where she
 *     lands; if that hits, the key again is a burst of tracer rounds in a line that staggers enemies back; if that hits,
 *     the key again is a burst in a cone with a bigger push. An enemy hit by all three takes a bonus hit. Each stage
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
 * tracers are copies of a bright round (Koleda's Full Speed tracers) with the stage's stagger and push on their damage
 * rows, paid the same way. Her leaps are Lenna's teleport (the game's position setter,
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

#define VERSION "0.1.2"
#define BUILD "25480438"

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
static float s_s1Cd = 10, s_s1Leap = 15, s_s1Radius = 5, s_s1Window = 3, s_s1Line = 20, s_s1Cone = 10, s_s1Triple = 2, s_s1Haste = 2, s_hasteMult = 1.3f;
static float s_s1Damage = 600, s_s1Damage1 = 800, s_s1Damage2 = 1000, s_s1Stagger1 = 50, s_s1Stagger2 = 80, s_s1Shield = 1.5f;
static int s_s1Rounds = 10, s_tracerRound = 226, s_tracerDamage = 125;   /* the stages' tracers: Koleda's (the bright round, the minigun's damage row) */
static float s_s1Burst = 0.2f, s_s1ConeAngle = 30, s_tracerSpeed = 250;
static float s_s2Cd = 18, s_s2Length = 15, s_s2Width = 9, s_s2Damage = 400, s_s2Grenade = 0.20f, s_s2Crit = 1.5f, s_s2Step = 0.18f;
static float s_s3Cd = 90, s_s3Leap = 18, s_s3Width = 9, s_s3Damage = 1500, s_s3Heal = 0.20f, s_s3Step = 0.12f;
static float s_frenzyRadius = 24, s_frenzyDamage = 12000, s_frenzyRange = 120, s_frenzyCone = 6, s_huntingSeconds = 10, s_huntingTaken = 0.20f, s_frenzyRefund = 5;
static int s_frenzyRing = 3;
static int s_rowSmall = 343, s_rowBig = 193;   /* 343: the Orbital Precision Strike's shell; 193: the Eagle 500kg Bomb's payload */
static float s_lpClimb = 6, s_lpDrop = 12;
static Key s_keyFrenzy = {'B', 0, "B"};

/* Every setting: its .ini / options name, variable and range (kitbase/kb_settings.h) */
static const KbSetting kSettings[] = {
    KB_F("s1_cost", s_s1Cost, 0, 0.9), KB_F("s2_cost", s_s2Cost, 0, 0.9), KB_F("cost_floor", s_costFloor, 0.01, 0.9),
    KB_I("grenades_max", s_grenadesMax, 0, 6), KB_F("grenade_mult", s_grenadeMult, 0, 5),
    KB_I("frenzy_start", s_frenzyStart, 0, 12), KB_I("frenzy_max", s_frenzyMax, 0, 12), KB_F("frenzy_mult", s_frenzyMult, 0, 5),
    KB_I("mech", s_mech, 0, 2), KB_F("mech_range", s_mechRange, 5, 200), KB_F("mech_each", s_mechEach, 0, 1), KB_F("mech_max", s_mechMax, 0, 5),
    KB_F("mech_hp", s_mechHp, 0, 1), KB_F("mech_taken", s_mechTaken, 0, 0.95), KB_I("boss_armour", s_bossArmour, 1, 10),
    KB_F("denied_cooldown", s_deniedCd, 0, 3600), KB_F("denied_seconds", s_deniedSeconds, 0, 10), KB_F("rage", s_rage, 0, 3),
    KB_F("s1_cooldown", s_s1Cd, 0, 600), KB_F("s1_leap", s_s1Leap, 3, 40), KB_F("s1_radius", s_s1Radius, 1, 20), KB_F("s1_window", s_s1Window, 0.5, 20),
    KB_F("s1_line", s_s1Line, 3, 100), KB_F("s1_cone", s_s1Cone, 3, 100), KB_F("s1_triple", s_s1Triple, 0, 20), KB_F("s1_haste", s_s1Haste, 0, 30),
    KB_F("haste_mult", s_hasteMult, 1, 3), KB_F("s1_damage", s_s1Damage, 0, 100000), KB_F("s1_damage_1", s_s1Damage1, 0, 100000),
    KB_F("s1_damage_2", s_s1Damage2, 0, 100000), KB_F("s1_stagger_1", s_s1Stagger1, 0, 500), KB_F("s1_stagger_2", s_s1Stagger2, 0, 500),
    KB_I("s1_rounds", s_s1Rounds, 1, 40), KB_F("s1_burst", s_s1Burst, 0, 2), KB_F("s1_cone_angle", s_s1ConeAngle, 5, 80),
    KB_F("tracer_speed", s_tracerSpeed, 0, 2000), KB_I("tracer_round", s_tracerRound, 1, 350), KB_I("tracer_damage", s_tracerDamage, 1, 649),
    KB_F("s1_shield", s_s1Shield, 0, 10),
    KB_F("s2_cooldown", s_s2Cd, 0, 600), KB_F("s2_length", s_s2Length, 5, 40), KB_F("s2_width", s_s2Width, 3, 30), KB_F("s2_damage", s_s2Damage, 0, 100000),
    KB_F("s2_grenade", s_s2Grenade, 0, 5), KB_F("s2_crit", s_s2Crit, 1, 10), KB_F("s2_step", s_s2Step, 0, 2),
    KB_F("s3_cooldown", s_s3Cd, 0, 600), KB_F("s3_leap", s_s3Leap, 3, 40), KB_F("s3_width", s_s3Width, 2, 30), KB_F("s3_damage", s_s3Damage, 0, 100000),
    KB_F("s3_heal", s_s3Heal, 0, 1), KB_F("s3_step", s_s3Step, 0, 2),
    KB_F("frenzy_radius", s_frenzyRadius, 5, 60), KB_F("frenzy_damage", s_frenzyDamage, 0, 1000000), KB_F("frenzy_range", s_frenzyRange, 10, 300),
    KB_F("frenzy_cone", s_frenzyCone, 1, 45), KB_I("frenzy_ring", s_frenzyRing, 0, 6), KB_F("hunting_seconds", s_huntingSeconds, 0, 120),
    KB_F("hunting_taken", s_huntingTaken, 0, 5), KB_F("frenzy_refund", s_frenzyRefund, 0, 120),
    KB_I("explosion_small", s_rowSmall, 1, 100000), KB_I("explosion_big", s_rowBig, 1, 100000),
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
               "; Annihilation Directive: cooldown (from the end of the chain); the leap's reach; its blast's radius; seconds\n"
               "; the next stage stays open after a hit; how far the line's and the cone's tracers pay; the bonus on an enemy\n"
               "; all three stages hit (times the cone's damage); the sprint boost after a stage that hits (seconds, times her\n"
               "; speed); each stage's damage; the line's and the cone's stagger and push on what they hit; seconds without\n"
               "; knockdown after each stage.\n"
               "s1_cooldown = %g\ns1_leap = %g\ns1_radius = %g\ns1_window = %g\ns1_line = %g\ns1_cone = %g\ns1_triple = %g\ns1_haste = %g\nhaste_mult = %g\n"
               "s1_damage = %g\ns1_damage_1 = %g\ns1_damage_2 = %g\ns1_stagger_1 = %g\ns1_stagger_2 = %g\ns1_shield = %g\n"
               "; The line and the cone are bursts of tracer rounds: s1_rounds of them within s1_burst s, the cone's spread\n"
               "; s1_cone_angle degrees to each side. tracer_round: the projectile they copy (226: the bright round of\n"
               "; Koleda's Sinveiler); tracer_damage: the damage row theirs copies (their damage is the stage's);\n"
               "; tracer_speed: their m/s (0: the round's own; slower, their trails start at her and not far ahead).\n"
               "s1_rounds = %d\ns1_burst = %g\ns1_cone_angle = %g\ntracer_round = %d\ntracer_damage = %d\ntracer_speed = %g\n"
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
               "; Her blasts (ExplosionSettings rows): 343 the Orbital Precision Strike's, 193 the Eagle 500kg Bomb's.\n"
               "explosion_small = %d\nexplosion_big = %d\n"
               "; Her leaps land at most leap_climb m higher or leap_drop m lower.\n"
               "leap_climb = %g\nleap_drop = %g\n"
               "; Her body: hp_mult times a Helldiver's health, stamina_mult times her sprint before the bar empties. If her\n"
               "; stamina ever runs out, hellbomb_on_exhaustion (1) sets off hellbomb_explosion (125: the B-100 Portable\n"
               "; Hellbomb's) on her.\n"
               "; hellbomb_hold: seconds the bar must stay empty first (it flickers to 0 for a moment at times).\n"
               "hp_mult = %g\nstamina_mult = %g\nhellbomb_on_exhaustion = %d\nhellbomb_explosion = %d\nhellbomb_hold = %g\n",
        VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, s_keyFrenzy.label, (double)s_s1Cost, (double)s_s2Cost, (double)s_costFloor,
        s_grenadesMax, (double)s_grenadeMult, s_frenzyStart, s_frenzyMax, (double)s_frenzyMult,
        s_mech, (double)s_mechRange, (double)s_mechEach, (double)s_mechMax, (double)s_mechHp, (double)s_mechTaken, s_bossArmour,
        (double)s_deniedCd, (double)s_deniedSeconds, (double)s_rage,
        (double)s_s1Cd, (double)s_s1Leap, (double)s_s1Radius, (double)s_s1Window, (double)s_s1Line, (double)s_s1Cone, (double)s_s1Triple, (double)s_s1Haste, (double)s_hasteMult,
        (double)s_s1Damage, (double)s_s1Damage1, (double)s_s1Damage2, (double)s_s1Stagger1, (double)s_s1Stagger2, (double)s_s1Shield,
        s_s1Rounds, (double)s_s1Burst, (double)s_s1ConeAngle, s_tracerRound, s_tracerDamage, (double)s_tracerSpeed,
        (double)s_s2Cd, (double)s_s2Length, (double)s_s2Width, (double)s_s2Damage, (double)s_s2Grenade, (double)s_s2Crit, (double)s_s2Step,
        (double)s_s3Cd, (double)s_s3Leap, (double)s_s3Width, (double)s_s3Damage, (double)s_s3Heal, (double)s_s3Step,
        (double)s_frenzyRadius, (double)s_frenzyDamage, (double)s_frenzyRange, (double)s_frenzyCone, s_frenzyRing, (double)s_huntingSeconds, (double)s_huntingTaken, (double)s_frenzyRefund,
        s_rowSmall, s_rowBig, (double)s_lpClimb, (double)s_lpDrop,
        (double)s_hpMult, (double)s_staminaMult, s_hellbomb, s_hellbombRow, (double)s_hellbombHold);
}

/* ---- state ---------------------------------------------------------------------------------------------------- */
static int g_grenades, g_fang, g_frenzy;
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
/* A skill's price: `share` of her maximum, never taking her below cost_floor of it (under the floor it costs nothing) */
static void Pay(const char* what, float share)
{
    int32_t hp, max;
    if (share <= 0) return;
    if (!Hp(&hp, &max)) { static unsigned told; if (told++ < 4) Log("%s: her health cannot be read: no price taken", what); return; }
    int32_t floor_ = (int32_t)ceil(max * (double)s_costFloor), want = hp - (int32_t)floor(max * (double)share + 0.5);
    if (floor_ < 1) floor_ = 1;
    if (want < floor_) want = floor_;
    if (want >= hp) return;
    uintptr_t r = HealthRec(atomic_load(&g_avatar));
    if (!r || hf->write(r + 20, &want, 4)) { static unsigned told; if (told++ < 4) Log("%s: the health write failed: no price taken", what); return; }
    static unsigned told;
    if (told++ < 12) Log("%s: paid %d health (%d -> %d of %d)", what, hp - want, hp, want, max);
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
            b->reach = q->row == s_rowBig ? 60.0f : 30.0f;
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

/* ---- the stages' tracers --------------------------------------------------------------------------------------------
 * The line and the cone are bursts of tracer rounds (as Full Speed's on Koleda's Sinveiler, all at once): copies of a
 * bright round whose damage rows carry the stage's stagger and push. What a round deals is the stage's, paid once an
 * enemy: her projectile hits while a burst is in the air belong to it. */
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
    Log("the %s's tracers (projectile %u): %.0f m/s -> %.0f (%s)", k ? "cone" : "line", g_tracer[k], was.base.number, (double)s_tracerSpeed, ok ? "set" : "refused");
}
typedef struct Volley { int group, left, cone, n; double next, until; float from[3], aim[3], range; } Volley;
static Volley g_volley = {-1};
static void VolleyFrame(void)
{
    Volley* v = &g_volley;
    if (v->group < 0 || v->left <= 0 || g_now < v->next) return;
    uint32_t type = g_tracer[v->cone];
    int per = s_s1Burst <= 0 ? v->left : 2;
    for (int k = 0; k < per && v->left > 0; k++)
    {
        int i = v->n - v->left;
        float h = sqrtf(v->aim[0] * v->aim[0] + v->aim[1] * v->aim[1]);
        if (h < 0.05f) h = 0.05f;
        /* the line: a hair of spread; the cone: evenly across its angle, a little up and down */
        float t = v->n > 1 ? (float)i / (v->n - 1) * 2 - 1 : 0, r = (float)rand() / RAND_MAX * 2 - 1;
        float a = (v->cone ? t * s_s1ConeAngle : r * 1.2f) * 0.0174533f, c = cosf(a), sn = sinf(a);
        float dir[3] = {v->aim[0] * c - v->aim[1] * sn, v->aim[0] * sn + v->aim[1] * c, v->aim[2] + (v->cone ? r * 0.04f : 0)};
        float n = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        for (int j = 0; j < 3; j++) dir[j] /= n;
        float from[3] = {v->from[0] + dir[0] * 0.9f, v->from[1] + dir[1] * 0.9f, v->from[2]};
        int r2 = hf->spawn_projectile(self, type, from, dir);
        static unsigned noted;
        if (noted++ < 6) Log("tracer (projectile %u): answer %d", type, r2);
        v->left--;
    }
    v->next = g_now + (s_s1Burst > 0 && v->n > 2 ? s_s1Burst / (v->n / 2.0) : 0);
    if (v->left <= 0)
    {
        Group* g = &g_group[v->group];
        g->spawned = 1;
        g->spawnedAt = g_now;
        v->until = g_now + 0.9;
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
static void Shield(float seconds)
{
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
    if (!g_tracerReady || !g_tracer[stage - 1]) { Log("Annihilation Directive: its tracer rounds are unavailable (see the start of the log)"); EndChain("no tracers"); return; }
    Pay(stage == 1 ? "Annihilation Directive I" : "Annihilation Directive II", s_s1Cost);
    float damage = StageDamage("Annihilation Directive", stage == 1 ? s_s1Damage1 : s_s1Damage2);
    int g = NewGroup(stage == 1 ? SK_LINE : SK_CONE, damage, stage == 1 ? s_s1Line : s_s1Cone);
    Volley* v = &g_volley;
    memset(v, 0, sizeof *v);
    v->group = g;
    v->cone = stage == 2;
    v->n = v->left = s_s1Rounds;
    v->range = stage == 1 ? s_s1Line : s_s1Cone;
    memcpy(v->from, from, sizeof v->from);
    TracerSpeed(stage - 1);
    memcpy(v->aim, aim, sizeof v->aim);
    v->until = g_now + 3;
    g_chainGroup[stage] = g;
    g_chainWait = g;
    g_chain = stage + 1;               /* 3 after the cone: the chain ends when its hit is known */
    Shield(s_s1Shield);
    if (told++ < 20) Log("Annihilation Directive %s: %d tracer rounds from %.1f %.1f %.1f along %.2f %.2f %.2f, %.0f damage an enemy within %.0f m, %d Grenade(s) left", stage == 1 ? "I (the line)" : "II (the cone)", s_s1Rounds,
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
        else if ((g->spawned && g_now - g->spawnedAt > (g->skill == SK_LEAP ? 0.7 : 0.9)) || g_now - g->at > 5) EndChain("the stage hit nothing");
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
/* A hit on her (HellForge's damage event; kitbase/kb_damage.h routes it here) */
static void DamageTaken(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    /* her own blasts spare her */
    if (d->kind == 4 && BlastAt(d->position, NULL)) { e->cancel = 1; g_selfSpared++; return; }
    if (d->amount <= 0) return;
    if (g_now < g_deniedUntil) { e->cancel = 1; return; }
    /* a leap's window: no fall or landing damage (impact or flat, from nothing) */
    if (g_now < g_drUntil && (d->kind == 5 || d->kind == 7) && !NetOk(d->dealer) && !NetOk(d->owner)) { e->cancel = 1; return; }
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
static void DamageDealt(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    if (d->kind == 4)
    {
        float dist = 0;
        Burst* b = BlastAt(d->position, &dist);
        if (b)
        {
            uint64_t res;
            if (TargetFriend(d->target, &res)) { e->cancel = 1; g_friendSpared++; return; }
            if (!res || !FoeKind(res)) return;   /* not an enemy the kit knows (a wall, a nest, a turret): the row's own hit */
            if (dist > g_group[b->group].radius + 2.0f) { e->cancel = 1; g_cutHits++; return; }   /* the row reaches further than the skill */
            PayHit(e, d, &g_group[b->group], res, dist, "blast");
            return;
        }
    }
    /* a stage's tracers: her projectile hits while its burst is in the air */
    if ((d->kind == 0 || d->kind == 1) && g_volley.group >= 0 && g_now < g_volley.until && MineHit(d))
    {
        uint64_t res;
        if (TargetFriend(d->target, &res)) { e->cancel = 1; g_friendSpared++; return; }
        if (!res || !FoeKind(res)) return;
        float dist = sqrtf(Dist2(d->position, g_volley.from));
        if (dist > g_volley.range + 2.0f) { e->cancel = 1; g_cutHits++; return; }   /* a tracer flies further than the stage pays */
        PayHit(e, d, &g_group[g_volley.group], res, dist, "tracers");
        return;
    }
    if (d->amount <= 0) return;
    if (IsShot(d))
    {
        NoteTarget(d);
        if (Hunted(d->target)) ScaleDamage(d, 1 + (double)s_huntingTaken);
    }
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
        Log("the %s's tracer: projectile %u, damage row %u: stagger %u -> %.0f, push %u -> %.0f", k ? "cone" : "line", g_tracer[k], g_tracerDamage[k],
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
        const Key* k = i < 3 ? &s_keys[i] : &s_keyFrenzy;
        g_hud.keys[i] = (uint32_t)k->vk;
        memcpy(g_hud.key_label[i], k->label, sizeof g_hud.key_label[i]);
    }
    g_hud.stamina = g_inMission && g_alive ? StaminaNow() : -1;
    g_hud.hellbomb = (uint32_t)(s_hellbomb != 0);
    g_hud.haste = (float)fmax(0, g_speedUntil - g_now);
    return &g_hud;
}

/* ---- in-game key bindings: the HUD registers her three skills with Mod Bindings Menu and passes each press here (and
 * a heartbeat twice a second); the .ini's keys keep working. Ammo Frenzy's key is the .ini's only. */
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
    const Key* k = skill <= 3 ? &s_keys[skill - 1] : &s_keyFrenzy;
    *vk = k->vk;
    *mods = k->mods;
    return 1;
}
/* key_frenzy: read here (the kit base knows three keys), from the mod's .ini and then the options screen's file */
static void FrenzyKeyLoad(void)
{
    char path[2][MAX_PATH * 2], line[256], local[MAX_PATH];
    snprintf(path[0], sizeof path[0], "%s\\%s", hf->mod_folder(self), kb_ini);
    path[1][0] = 0;
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", local, sizeof local);
    if (n && n < sizeof local) snprintf(path[1], sizeof path[1], "%s\\HellForgeMods\\%s", local, kb_ini);
    for (int k = 0; k < 2; k++)
    {
        FILE* f = path[k][0] ? OpenUtf8(path[k], L"r") : NULL;
        if (!f) continue;
        while (fgets(line, sizeof line, f))
        {
            char* c = strchr(line, ';');
            if (c) *c = 0;
            char* eq = strchr(line, '=');
            char key[64];
            if (!eq) continue;
            *eq = 0;
            if (sscanf(line, " %63s", key) != 1 || strcmp(key, "key_frenzy")) continue;
            Key parsed;
            if (ParseKey(eq + 1, &parsed)) s_keyFrenzy = parsed;
            else hf->log(self, "destroyer.ini: key_frenzy could not be read; its default stays");
        }
        fclose(f);
    }
}

/* ---- the frame ------------------------------------------------------------------------------------------------------ */
static void Tick(HfMod* mod, float dt, void* user)
{
    KbOptionsTick();   /* the options screen's changes, in a mission or not */
    g_now += dt > 0 && dt < 1 ? dt : 0;
    int inMission = hf->game_state(self) == HF_STATE_MISSION;
    if (inMission != g_inMission) { Reset(inMission ? "mission start" : "mission end"); g_inMission = inMission; }
    if (!g_inMission || !kb_kit_on) return;   /* (standing by: another kit is picked) */
    MyItems();
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
        const Key* key = i < 3 ? &s_keys[i] : &s_keyFrenzy;
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
    {"denied_cooldown", &s_deniedCd, NULL}, {"mech", NULL, &s_mech},
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
    KB_SETTINGS(kSettings);
    KB_OPTIONS(kOptions);
    kb_option_after = OptionAfter;
    kb_damage_taken = DamageTaken;
    kb_damage_dealt = DamageDealt;
    if (!api || api->version < 13 || api->size < offsetof(HfApi, code_matches) + sizeof api->code_matches) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    LoadSettings();
    FrenzyKeyLoad();
    KbOptionsStart(api, mod, "destroyer", "Destroyer", VERSION, kOpRows, (int)(sizeof kOpRows / sizeof kOpRows[0]), kOpIcon, KB_OP_ICON, KB_OP_ACCENT, -1);
    api->log(mod, "Destroyer %s: keys %s / %s / %s / %s (Annihilation Directive, Doom Barrage, Party Time, Ammo Frenzy); skills cost %.0f%% and %.0f%% of her health, never below %.0f%%",
        VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, s_keyFrenzy.label, (double)s_s1Cost * 100, (double)s_s2Cost * 100, (double)s_costFloor * 100);
    if (s_badKey) api->log(mod, "destroyer.ini: a key could not be read (bits %d: s1, s2, s3); its default stays", s_badKey);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    if (api->size >= offsetof(HfApi, projectile_row) + sizeof api->projectile_row)
    {
        static const char* const dn[2] = {"yui/destroyer/line_damage", "yui/destroyer/cone_damage"};
        static const char* const pn[2] = {"yui/destroyer/line_tracer", "yui/destroyer/cone_tracer"};
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
        { g_tracer[0] = g_tracer[1] = 0; api->log(mod, "the stages' tracers could not be added: Annihilation Directive is the leap alone"); }
    }
    else api->log(mod, "this HellForge has no projectile rows: Annihilation Directive is the leap alone");
    /* registered: from here the DLL stays loaded; a failure below leaves it inert */
    int ok = api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) &&
             api->on_game_thread(mod, Tick, NULL);
    if (!ok) api->log(mod, "events or the game thread are unavailable: Destroyer stays off");
    return 0;
}
