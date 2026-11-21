/* Vector (yui.vector): Vector's kit from Girls' Frontline 2 (the SMG Doll, Burn support) as abilities on keys, for your
 * own Helldiver (solo or host), built on the kit base (kitbase/, docs/kit-base.md, docs/vector-design.md).
 *
 *   Perception Block (passive): fire does nothing to her: its statuses never land on her and burning deals her no
 *     damage. An Overburned enemy takes overburn_mult times fire damage.
 *   Confectance Index (passive): each skill she uses gives conf_cast points (at most conf_max). At conf_max they are
 *     spent: her hits deal conf_mult times for conf_seconds.
 *   Dead End Meltdown (key_s1, default Z): for s1_seconds her shots set what they hit on fire. A shot on an enemy that
 *     is already burning sets off a fire burst on it (explosion s1_burst) and Overburns it for overburn_seconds.
 *   Portent of Doom (key_s2, default X): the enemy under her crosshair is set on fire and Smolders for smolder_seconds:
 *     fire damage on it is smolder_mult times.
 *   Searing Finale (key_s3, default C): where she aims, s3_shells napalm shells fall (projectile s3_shell) and leave the
 *     ground burning; Accelerant for accelerant_seconds (fire damage on enemies accelerant_mult times); and an Extra
 *     Command: her other two skills are ready again.
 *
 * Research credit: the kit base's (Helldiver Survival Rework by enochlophobianumerophobia, MIT; HD2Runtime by
 * SkyeShade, and its explosion catalogue for the fire rows); the camera pose as Apache - Rise Above (RSNLRiggs, after
 * Khi) reads it. See THIRD_PARTY_NOTICES.md. */
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

#define VERSION "0.1.16"
#define BUILD "25480438"

#define KB_OWN_SLOTS 32   /* her fire bursts come often */
#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"
#include "kitbase/kb_body.h"
#include "kitbase/kb_hits.h"
#include "kitbase/kb_damage.h"
#include "kitbase/kb_settings.h"
#include "kitbase/kb_roster.h"

KB_ROSTER_EXPORTS(vector)

/* ---- settings ------------------------------------------------------------------------------------------------- */
static int s_confMax = 6, s_confCast = 2;
static float s_confMult = 1.25f, s_confSeconds = 12;
static float s_burnSeconds = 5, s_fireBuildup = 100;
static int s_fireStatus = 5;
static float s_s1Cd = 20, s_s1Seconds = 10, s_s1BurstEvery = 2, s_burstRate = 4, s_overburnSeconds = 8, s_overburnMult = 1.3f;
static int s_s1Burst = 26;
static float s_s2Cd = 15, s_s2Cone = 10, s_s2Range = 150, s_s2Buildup = 300, s_smolderSeconds = 12, s_smolderMult = 1.5f;
static float s_s3Cd = 90, s_s3Cone = 10, s_s3Range = 150, s_s3Spread = 8, s_s3Height = 60, s_s3Every = 0.3f, s_accSeconds = 15, s_accMult = 1.3f;
static int s_s3Shell = 234, s_s3Shells = 3, s_s3Refund = 1;
static int s_fireImmune = 1, s_fireSt[4] = {5, 6, 32, 67}, s_fireElement = -1;
static int s_s2Blast = 359;
static float s_firePour = 0, s_fireHeavyStep = 50, s_ownDamage = 12, s_ownEvery = 0.25f, s_ownSeconds = 8, s_ownPercent = 1;
static int s_ownBurn = 1;
static int s_fireHeavy = 32;
static int s_sight = 1;
static float s_fpDistance = 0.45f;

static const KbSetting kSettings[] = {
    KB_I("conf_max", s_confMax, 1, 50), KB_I("conf_cast", s_confCast, 0, 50), KB_F("conf_mult", s_confMult, 1, 10), KB_F("conf_seconds", s_confSeconds, 0, 120),
    KB_F("burn_seconds", s_burnSeconds, 0.5, 60), KB_F("fire_buildup", s_fireBuildup, 0, 1000), KB_I("fire_status", s_fireStatus, 1, 255),
    KB_F("s1_cooldown", s_s1Cd, 0, 600), KB_F("s1_seconds", s_s1Seconds, 1, 120), KB_F("s1_burst_every", s_s1BurstEvery, 0.1, 60),
    KB_F("burst_rate", s_burstRate, 0.5, 30), KB_I("s1_burst", s_s1Burst, 0, 100000), KB_F("overburn_seconds", s_overburnSeconds, 0, 120),
    KB_F("overburn_mult", s_overburnMult, 1, 10),
    KB_F("s2_cooldown", s_s2Cd, 0, 600), KB_F("s2_cone", s_s2Cone, 0.5, 45), KB_F("s2_range", s_s2Range, 10, 1000), KB_F("s2_buildup", s_s2Buildup, 0, 1000),
    KB_F("smolder_seconds", s_smolderSeconds, 0, 120), KB_F("smolder_mult", s_smolderMult, 1, 10),
    KB_F("s3_cooldown", s_s3Cd, 0, 3600), KB_F("s3_cone", s_s3Cone, 0.5, 45), KB_F("s3_range", s_s3Range, 10, 1000), KB_I("s3_shell", s_s3Shell, 1, 100000),
    KB_I("s3_shells", s_s3Shells, 1, 12), KB_F("s3_spread", s_s3Spread, 0, 40), KB_F("s3_height", s_s3Height, 5, 300), KB_F("s3_every", s_s3Every, 0.05, 5),
    KB_F("accelerant_seconds", s_accSeconds, 0, 120), KB_F("accelerant_mult", s_accMult, 1, 10), KB_I("s3_refund", s_s3Refund, 0, 1),
    KB_I("fire_immune", s_fireImmune, 0, 1), KB_I("fire_status_1", s_fireSt[0], 0, 255), KB_I("fire_status_2", s_fireSt[1], 0, 255),
    KB_I("fire_status_3", s_fireSt[2], 0, 255), KB_I("fire_status_4", s_fireSt[3], 0, 255), KB_I("fire_element", s_fireElement, -1, 255), KB_F("fire_pour", s_firePour, 0, 30), KB_I("s2_blast", s_s2Blast, 0, 100000), KB_I("own_burn", s_ownBurn, 0, 1), KB_F("own_burn_damage", s_ownDamage, 0, 10000), KB_F("own_burn_percent", s_ownPercent, 0, 100),
    KB_F("own_burn_every", s_ownEvery, 0.05, 10), KB_F("own_burn_seconds", s_ownSeconds, 0.5, 120),
    KB_I("line_of_sight", s_sight, 0, 1), KB_F("first_person", s_fpDistance, 0.1, 2),
    KB_F("hp_mult", s_hpMult, 0.1, 10), KB_F("stamina_mult", s_staminaMult, 0.1, 20), KB_I("hellbomb_on_exhaustion", s_hellbomb, 0, 1),
    KB_I("hellbomb_explosion", s_hellbombRow, 0, 100000), KB_F("hellbomb_hold", s_hellbombHold, 0, 10),
};

static __attribute__((unused)) void DefaultIni(FILE* f)
{
    fprintf(f, "; Vector %s. Keys: a letter, digit, F1-F24, Mouse3, Mouse4 or Mouse5, with Shift+, Ctrl+ or Alt+ in front for a\n"
               "; combo (Shift+Q). The game still sees the keys too (Shift sprints, R reloads), so pick ones it leaves alone.\n"
               "key_s1 = %s\nkey_s2 = %s\nkey_s3 = %s\n"
               "; Confectance Index: each skill she uses gives conf_cast points (at most conf_max). At conf_max they are spent: her\n"
               "; hits deal conf_mult times for conf_seconds.\n"
               "conf_max = %d\nconf_cast = %d\nconf_mult = %g\nconf_seconds = %g\n"
               "; Fire: she sets an enemy on fire with the status fire_status (5: fire), fire_buildup of build-up a time (100: as a\n"
               "; full hit). An enemy counts as burning for burn_seconds after a fire status or a tick of fire on it.\n"
               "fire_status = %d\nfire_buildup = %g\nburn_seconds = %g\n"
               "; Dead End Meltdown: cooldown; for s1_seconds her shots set what they hit on fire. A shot on an enemy already\n"
               "; burning sets off a fire burst on it (explosion s1_burst, 26: the EAT-700's fire shrapnel; 69: the G-13 Incendiary\n"
               "; Impact's; 409: the G-142 Pyrotech's; 0: none), at most one an enemy every s1_burst_every s and burst_rate a second\n"
               "; in all, and Overburns it for overburn_seconds: fire damage on it is overburn_mult times.\n"
               "s1_cooldown = %g\ns1_seconds = %g\ns1_burst = %d\ns1_burst_every = %g\nburst_rate = %g\noverburn_seconds = %g\noverburn_mult = %g\n"
               "; Portent of Doom: cooldown; the enemy under her crosshair (s2_cone degrees, s2_range m, in sight) is set on fire\n"
               "; (s2_buildup) and Smolders for smolder_seconds: fire damage on it is smolder_mult times.\n"
               "s2_cooldown = %g\ns2_cone = %g\ns2_range = %g\ns2_buildup = %g\nsmolder_seconds = %g\nsmolder_mult = %g\n"
               "; Searing Finale: cooldown; where she aims (the enemy under her crosshair, else where her aim lands, s3_range m)\n"
               "; s3_shells napalm shells (projectile s3_shell, 234: the Orbital Napalm Barrage's; 141: the Eagle Napalm Airstrike's)\n"
               "; fall from s3_height m, s3_every s apart and up to s3_spread m apart. Accelerant for accelerant_seconds: fire\n"
               "; damage on enemies is accelerant_mult times. s3_refund 1: her other two skills are ready again (Extra Command).\n"
               "s3_cooldown = %g\ns3_cone = %g\ns3_range = %g\ns3_shell = %d\ns3_shells = %d\ns3_spread = %g\ns3_height = %g\ns3_every = %g\n"
               "accelerant_seconds = %g\naccelerant_mult = %g\ns3_refund = %d\n"
               "; Perception Block: fire_immune 1, fire does nothing to her: the statuses fire_status_1..4 (5 fire, 6 fire panic,\n"
               "; 32 heavy burning, 67 the flamer's slow) never land on her and fire's damage is cancelled. fire_element: the damage\n"
               "; element of fire (-1: learned from the first burning thing she sees; the log names it).\n"
               "fire_immune = %d\nfire_status_1 = %d\nfire_status_2 = %d\nfire_status_3 = %d\nfire_status_4 = %d\nfire_element = %d\n"
               "; fire_pour: an enemy her fire did not burn within 2 s gets fire build-up every frame for up to this many seconds\n"
               "; (0: never). A kind this lit is poured on from the start for the rest of the session.\n"
               "fire_pour = %g\n"
               "; Her own burn (own_burn 1): what takes no fire damage from the game's fire (tanks, walkers, gunships) burns by her\n"
               "; hand instead, past its armour: own_burn_percent of its full health (never less than own_burn_damage) every\n"
               "; own_burn_every s for own_burn_seconds after her fire. At 1 percent four times a second a full-health enemy burns\n"
               "; through in 25 s of unbroken burning. Smolder, Overburn and Accelerant multiply it.\n"
               "own_burn = %d\nown_burn_damage = %g\nown_burn_percent = %g\nown_burn_every = %g\nown_burn_seconds = %g\n"
               "; s2_blast: the explosion set on Portent of Doom's target as it is cast (359: an Eagle Napalm bomb's; 63: an Orbital\n"
               "; Napalm shell's; 224: an incendiary grenade's; 0: none).\n"
               "s2_blast = %d\n"
               "; Aiming: line_of_sight 1, Portent of Doom's target must be in sight. first_person: the camera this close to her\n"
               "; head (m) counts as first person.\nline_of_sight = %d\nfirst_person = %g\n"
               "; Her body: hp_mult times a Helldiver's health, stamina_mult times her sprint. hellbomb_on_exhaustion 1 sets off a\n"
               "; Hellbomb on her if she runs her stamina dry (a T-Doll's price).\n"
               "hp_mult = 1.5\nstamina_mult = 2\nhellbomb_on_exhaustion = 1\n",
            VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, s_confMax, s_confCast, (double)s_confMult, (double)s_confSeconds, s_fireStatus,
            (double)s_fireBuildup, (double)s_burnSeconds, (double)s_s1Cd, (double)s_s1Seconds, s_s1Burst, (double)s_s1BurstEvery, (double)s_burstRate,
            (double)s_overburnSeconds, (double)s_overburnMult, (double)s_s2Cd, (double)s_s2Cone, (double)s_s2Range, (double)s_s2Buildup,
            (double)s_smolderSeconds, (double)s_smolderMult, (double)s_s3Cd, (double)s_s3Cone, (double)s_s3Range, s_s3Shell, s_s3Shells, (double)s_s3Spread,
            (double)s_s3Height, (double)s_s3Every, (double)s_accSeconds, (double)s_accMult, s_s3Refund, s_fireImmune, s_fireSt[0], s_fireSt[1], s_fireSt[2],
            s_fireSt[3], s_fireElement, (double)s_firePour, s_ownBurn, (double)s_ownDamage, (double)s_ownPercent, (double)s_ownEvery, (double)s_ownSeconds, s_s2Blast, s_sight, (double)s_fpDistance);
}

/* ---- state ---------------------------------------------------------------------------------------------------- */
static double g_cd[3];
static double g_s1Until, g_confUntil, g_accUntil;
static int g_conf;
static int g_fireElem = -1;
static unsigned g_casts[3], g_ignitions, g_bursts, g_overburns, g_shells, g_spends, g_fireOnHer, g_refusedHer, g_scaled;
static double g_fireBonus;

/* ---- reads: health, positions, her view -------------------------------------------------------------------------- */
static int HealthOf(uint32_t entity, int32_t* health)
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
        uint8_t r[24];
        if (!ReadAt(records + 440u * index, r, sizeof r)) return 0;
        *health = (int32_t)Word(r + 20);
        return 1;
    }
    return 0;
}
static int Alive(uint32_t e) { int32_t hp; return HealthOf(e, &hp) && hp > 0; }
static int TargetPos(uint32_t e, uint64_t res, float out[3])
{
    uint8_t d[24];
    if (!Descriptor(e, d) || (res && Quad(d) != res) || !UnitNode(Word(d + 12), 0, out)) return 0;
    out[2] += 1.0f;
    return 1;
}
static uint32_t UnitOf(uint32_t e)
{
    uint8_t d[24];
    return Descriptor(e, d) ? Word(d + 12) : 0;
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
/* her head: where her shots leave from */
static __attribute__((unused)) int HerHead(float out[3])
{
    uint8_t d[24];
    return Descriptor(atomic_load(&g_avatar), d) && UnitNode(Word(d + 12), NODE_HEAD, out);
}

/* ---- line of sight (Lemuen 0.1.6's: the game's raycast, skipping her, leaves, bodies and the target) ------------- */
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
/* the first wall along a ray (len + 1: clear); *hitTarget when the target's unit is met first */
static float FirstWall(uint32_t id, const float from[3], const float dir[3], float len, uint32_t me, uint32_t tu, int* hitTarget)
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
        if (!isfinite(d) || d < 1.2f || d >= best || (hu && hu == me)) continue;
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
        float w = FirstWall(id, cam, dir, len, me, tu, &hitTarget);
        float reach = r < 2 ? len : len - 2;
        if (hitTarget || w > reach - 1.5f) return 1;
    }
    return 0;
}
/* everything spawned that is no friend, her foes or not: the kit base leaves some big enemies out of her foes (the War
 * Strider among them: anything with a turret or seats), and her crosshair finds those by their unit */
#define ANY 4096
static uint32_t g_any[ANY];
static uint64_t g_anyRes[ANY];
static unsigned g_anyNext;
/* the game's own faction tag (0.1.15, from the game's code: its faction component's manager at game+0x3326CB8, 512
 * slots: an entity per index at +0xE030 (8 bytes each), an entity map whose header is at +0x12038 (buckets +0x18,
 * capacity +0x20, empty key +0x24, multiplier +0x28), 112 bytes a slot from +0x10 and 24 more from +0xF030). Where in a
 * slot the faction sits is not known yet: each kind's slot goes to the log once, to compare bots, bugs, squids and
 * Helldivers */
static int FactionIndex(uint32_t entity, uintptr_t* manager)
{
    uintptr_t m = Pointer(game + 0x3326CB8);
    uint8_t h[0x30];
    if (!entity || !m || !ReadAt(m + 0x12038, h, sizeof h)) return -1;
    uintptr_t buckets = (uintptr_t)Quad(h + 0x18);
    uint32_t cap = Word(h + 0x20), empty = Word(h + 0x24), mult = Word(h + 0x28);
    if (!buckets || !cap || cap > 65536 || (cap & (cap - 1))) return -1;
    uint32_t start = entity * mult;
    for (uint32_t k = 0; k < cap; k++)
    {
        uint8_t b[8];
        if (!ReadAt(buckets + 8u * ((start + k) & (cap - 1)), b, 8)) return -1;
        uint32_t key = Word(b), index = Word(b + 4);
        if (key != entity) { if (key == empty) return -1; continue; }
        if (index >= 512) return -1;
        *manager = m;
        return (int)index;
    }
    return -1;
}
static void Hex(char* out, const uint8_t* b, int n) { for (int i = 0; i < n; i++) snprintf(out + 2 * i, 3, "%02x", b[i]); }
static uint64_t g_facSeen[256];
static unsigned g_facN;
static void FactionNote(uint32_t e, uint64_t res)
{
    if (!e || !res) return;
    for (unsigned i = 0; i < g_facN; i++) if (g_facSeen[i] == res) return;
    uintptr_t m = 0;
    int idx = g_facN < 256 ? FactionIndex(e, &m) : -1;
    if (idx < 0) return;
    g_facSeen[g_facN++] = res;
    uint8_t a[8], slot[112], tail[24];
    char ha[20] = "", hs[230] = "", ht[52] = "";
    if (ReadAt(m + 0xE030 + 8u * (unsigned)idx, a, 8)) Hex(ha, a, 8);
    if (ReadAt(m + 0x10 + 112u * (unsigned)idx, slot, 112)) Hex(hs, slot, 112);
    if (ReadAt(m + 0xF030 + 24u * (unsigned)idx, tail, 24)) Hex(ht, tail, 24);
    uint32_t st2;
    Log("faction slot of kind %016llx%s%s%s (entity %u, index %d): entity %s | slot %s | tail %s", (unsigned long long)res,
        res == AVATAR ? " (a Helldiver)" : FriendRes(res) ? " (a friend)" : "", hf->component_record("BehaviorComponentData", res, &st2) ? ", behaviour" : "",
        hf->component_record("TurretComponentData", res, &st2) ? ", turret" : "", e, idx, ha, hs, ht);
}
/* everything spawned, friends too, for the faction log (SeeAny keeps friends out) */
#define FACQ 2048
static uint32_t g_facE[FACQ];
static uint64_t g_facR[FACQ];
static unsigned g_facIn, g_facAt;
/* once a mission, 20 s in: the manager's own numbers (0.1.13's first guess, game+0x3326DC8, was another component:
 * empty, 16 slots) */
static double g_facDiagAt;
static void FactionDiag(void)
{
    uintptr_t m = Pointer(game + 0x3326CB8);
    uint8_t h[0x70], pre[0x30];
    char hex[230] = "", hp[100] = "";
    if (!m || !ReadAt(m + 0x12030, h, sizeof h) || !ReadAt(m + 0xE000, pre, sizeof pre)) { Log("faction manager: none (pointer %p)", (void*)m); return; }
    Hex(hex, h, 0x70); Hex(hp, pre, 0x30);
    uintptr_t mm = 0;
    uint32_t me = atomic_load(&g_avatar);
    Log("faction manager at %p: +0xE000 %s | +0x12030 %s | her own entity %u: index %d", (void*)m, hp, hex, me, FactionIndex(me, &mm));
}
/* Portent of Doom's blast is a gas vent's: its flash and sound live in the vent's own package, which the game only
 * loads on planets that have vents (0.1.13: the fire patch, the stagger and the damage showed, no blast and no sound).
 * Asked for at each mission start, and again every second until it is in or refused */
static int g_ventState = -1;           /* -1 not asked, 0 refused, HF_PACKAGE_... */
static double g_ventAt;
static void VentFrame(void)
{
    if (s_s2Blast != 120 || g_ventState == 0 || g_ventState == (int)HF_PACKAGE_RESIDENT || g_now < g_ventAt) return;
    g_ventAt = g_now + 1;
    int was = g_ventState;
    g_ventState = hf->load_package(self, UINT64_C(0x1AB4186A06C84078));
    if (g_ventState != was)
        Log("the gas vent's package: %s", g_ventState == (int)HF_PACKAGE_RESIDENT ? "loaded, Portent of Doom's blast has its flash and sound"
                                          : g_ventState ? "loading" : "refused (HellForge's log line above says why): the blast stays without its flash and sound");
}
static void FactionFrame(void)
{
    VentFrame();
    if (g_facDiagAt > 0 && g_now >= g_facDiagAt) { g_facDiagAt = 0; FactionDiag(); }
    FactionNote(atomic_load(&g_avatar), AVATAR);
    for (int n = 0; n < 48; n++) { unsigned i = g_facAt++ % FACQ; if (g_facE[i]) FactionNote(g_facE[i], g_facR[i]); }
}
static void SeeAny(uint32_t e, uint64_t res)
{
    if (e && res) { unsigned q = g_facIn++ % FACQ; g_facE[q] = e; g_facR[q] = res; }
    if (!e || res == AVATAR || FriendRes(res)) return;
    unsigned i = g_anyNext++ % ANY;
    g_any[i] = e; g_anyRes[i] = res;
}
static int FoeIndex(uint32_t e);
static int HasPart(uint64_t res, const char* component) { uint32_t stride; return hf->component_record(component, res, &stride) != 0; }
/* the living, healthy thing whose unit her aim met, made her foe from now on; 0 none. Only what thinks or shoots (a
 * behaviour or a turret): props and objectives with health stay out */
static uint32_t AdoptUnit(uint32_t hu)
{
    uint8_t d[24];
    for (unsigned i = 0; i < ANY; i++)
    {
        uint32_t e = g_any[i];
        if (!e || !Descriptor(e, d) || Quad(d) != g_anyRes[i] || Word(d + 12) != hu) continue;
        if (!Alive(e)) return 0;
        if (!HasPart(g_anyRes[i], "BehaviorComponentData") && !HasPart(g_anyRes[i], "TurretComponentData"))
        {
            static uint64_t toldRes[8]; static unsigned told;
            int seen = 0;
            for (int t = 0; t < 8; t++) if (toldRes[t] == g_anyRes[i]) seen = 1;
            if (!seen && told < 8) { toldRes[told++] = g_anyRes[i]; Log("her crosshair on entity %u (kind %016llx): no behaviour and no turret, not taken as a foe", e, (unsigned long long)g_anyRes[i]); }
            return 0;
        }
        AddFoe(e, g_anyRes[i]);
        if (FoeIndex(e) < 0) return 0;
        Log("her crosshair on entity %u (kind %016llx), not one of her foes until now: taken as one", e, (unsigned long long)g_anyRes[i]);
        return e;
    }
    return 0;
}
/* what her crosshair is on: the living enemy whose body the aim ray meets first; or, when it meets a part that is no
 * enemy of its own (a big enemy's turret or plate), the living enemy nearest that point */
static int AimDirect(const float cam[3], const float fwd[3], float range, float at[3], uint32_t* target)
{
    uint32_t id;
    if (!WorldId(&id)) return 0;
    uint32_t me = UnitOf(atomic_load(&g_avatar));
    uint8_t hits[16 * 44];
    memset(hits, 0, sizeof hits);
    uint32_t n = g_raycast(id, cam, fwd, range, 2, 1, 0, 0x80000009u, me, hits, 16);
    if (n > 16) n = 16;
    uint8_t used[16] = {0};
    float part = -1;
    for (uint32_t step = 0; step < n; step++)
    {
        int b = -1;
        float bd = range + 1;
        for (uint32_t i = 0; i < n; i++)
        {
            float d;
            memcpy(&d, hits + 44u * i + 24, 4);
            if (!used[i] && isfinite(d) && d < bd) { bd = d; b = (int)i; }
        }
        if (b < 0) break;
        used[b] = 1;
        uint32_t hu = Word(hits + 44u * b + 28);
        if (bd < 1.2f || (hu && hu == me)) continue;
        uint32_t filt = ActorFilter(Word(hits + 44u * b + 32), hu);
        if (filt == FILTER_FOLIAGE || (hu && (filt == 0x236122F0u || filt == 0xD5B33D56u))) continue;
        if (!hu) break;                                        /* the world: nothing behind it counts */
        for (int k = 0; k < FOES; k++)
            if (g_foe[k] && UnitOf(g_foe[k]) == hu && Alive(g_foe[k]))
            {
                *target = g_foe[k];
                for (int j = 0; j < 3; j++) at[j] = cam[j] + fwd[j] * bd;
                return 1;
            }
        {
            uint32_t e = AdoptUnit(hu);
            if (e)
            {
                *target = e;
                for (int j = 0; j < 3; j++) at[j] = cam[j] + fwd[j] * bd;
                return 1;
            }
        }
        if (part < 0) part = bd;
    }
    if (part < 0) return 0;
    float p[3], q[3], best = 12.0f * 12.0f;
    for (int j = 0; j < 3; j++) p[j] = cam[j] + fwd[j] * part;
    for (int k = 0; k < FOES; k++)
    {
        uint32_t e = g_foe[k];
        if (!e || !TargetPos(e, g_foeRes[k], q)) continue;
        float d2 = (q[0] - p[0]) * (q[0] - p[0]) + (q[1] - p[1]) * (q[1] - p[1]) + (q[2] - p[2]) * (q[2] - p[2]);
        if (d2 >= best || !Alive(e)) continue;
        best = d2; *target = e;
    }
    if (!*target) return 0;
    memcpy(at, p, 12);
    return 2;
}
static int g_aimHow;                   /* 1 her crosshair on its body, 2 on one of its parts, 0 nearest her crosshair */
/* where she aims: the living enemy nearest her crosshair (within cone degrees, range m, in sight), else (anyAt) where her
 * aim meets the world. 1 with a target (*target set), 2 with a point only, 0 nothing */
static unsigned g_aimBlocked;
static int FoeCount(void) { int n = 0; for (int k = 0; k < FOES; k++) if (g_foe[k]) n++; return n; }
static int Aim(float cone, float range, int anyAt, float at[3], uint32_t* target)
{
    float cam[3], fwd[3], q[3];
    int fp;
    *target = 0;
    if (!View(cam, fwd, &fp)) return 0;
    if ((g_aimHow = AimDirect(cam, fwd, range, at, target)) != 0) return 1;
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
    if (*target) return 1;
    if (!anyAt) return 0;
    uint32_t id;
    float len = range;
    if (WorldId(&id))
    {
        float w = FirstWall(id, cam, fwd, range, UnitOf(atomic_load(&g_avatar)), 0, NULL);
        if (w <= range) len = w;
    }
    for (int k = 0; k < 3; k++) at[k] = cam[k] + fwd[k] * len;
    return 2;
}


static int FoeIndex(uint32_t e) { for (int k = 0; k < FOES; k++) if (e && g_foe[k] == e) return k; return -1; }

static void Status(uint32_t e, uint32_t status, float buildup, const char* what)
{
    if (buildup <= 0 || !hf->apply_status) return;
    Act(what, hf->apply_status(self, e, status, buildup > 1000 ? 1000 : buildup));
}

/* her own blasts, remembered: they do nothing to her (the kit base's own-blast guard) */
static void Own(const float at[3])
{
    unsigned slot = g_ownNext++ % KB_OWN_SLOTS;
    memcpy(g_ownPos[slot], at, sizeof g_ownPos[slot]);
    atomic_store(&g_ownEmp[slot], 0);
    atomic_store(&g_ownMs[slot], GetTickCount64());
}

/* ---- heat: what burns, Smolders and is Overburned --------------------------------------------------------------- */
#define HEAT 256
typedef struct Heat { uint32_t e; double burnUntil, burnSince, smolderUntil, overburnUntil, burstAt, litAt, tickAt; uint64_t res; int stage; float poured; double ownUntil, ownNext, cookAt, cookUntil, ownTotal; int cooked; int32_t ownMax; double hersUntil; } Heat;
static Heat g_heat[HEAT];
static Heat* HeatOf(uint32_t e, int make)
{
    if (!e) return NULL;
    unsigned h = (e * 2654435761u) >> 24;
    Heat* spare = NULL;
    for (unsigned i = 0; i < 24; i++)
    {
        Heat* c = &g_heat[(h + i) & (HEAT - 1)];
        if (c->e == e) return c;
        if (!c->e && !spare) spare = c;
    }
    if (!make) return NULL;
    if (!spare)
    {
        /* full around here: the one that went cold longest ago */
        spare = &g_heat[h & (HEAT - 1)];
        for (unsigned i = 1; i < 24; i++) { Heat* c = &g_heat[(h + i) & (HEAT - 1)]; if (c->burnUntil < spare->burnUntil) spare = c; }
        memset(spare, 0, sizeof *spare);
    }
    spare->e = e;
    return spare;
}
/* fire seen on an entity (a fire status, a tick of fire): it burns for burn_seconds from now */
static void Burns(uint32_t e)
{
    Heat* c = HeatOf(e, 1);
    if (!c) return;
    if (c->burnUntil <= g_now) c->burnSince = g_now;
    c->burnUntil = g_now + s_burnSeconds;
}
static __attribute__((unused)) int Burning(uint32_t e) { Heat* c = HeatOf(e, 0); return c && c->burnUntil > g_now; }

/* set on fire, from the frame (the damage event is no place to act from): a small queue */
#define IGNQ 64
static struct { uint32_t e; float buildup; } g_ign[IGNQ];
static atomic_uint g_ignIn;
static unsigned g_ignOut;
static void IgniteLater(uint32_t e, float buildup)
{
    unsigned i = atomic_fetch_add(&g_ignIn, 1) % IGNQ;
    g_ign[i].e = e; g_ign[i].buildup = buildup;
}
/* did what she set on fire burn? per kind of enemy: how many took fire damage within 2 s of her fire and how many never did */
static struct { uint64_t res; unsigned caught, heavy, missed; } g_took[48];
static void Took(uint64_t res, int caught)
{
    for (int i = 0; i < 48; i++)
    {
        if (g_took[i].res != res && (g_took[i].res || g_took[i].caught || g_took[i].heavy || g_took[i].missed)) continue;
        g_took[i].res = res;
        if (caught == 1) g_took[i].caught++; else if (caught == 2) g_took[i].heavy++; else g_took[i].missed++;
        return;
    }
}
static void TookReport(void)
{
    for (int i = 0; i < 48; i++)
        if (g_took[i].caught || g_took[i].heavy || g_took[i].missed)
            Log("her fire on kind %016llx: %u burned, %u took her own burn, %u took no fire damage", (unsigned long long)g_took[i].res,
                g_took[i].caught, g_took[i].heavy, g_took[i].missed);
    memset(g_took, 0, sizeof g_took);
}
static double g_tookAt;

/* ---- her own burn: on what the game's fire cannot hurt (tanks, walkers, gunships) ---------------------------------- */
/* kinds that took no fire damage from her fire: her own burn from the start, for the session */
static uint64_t g_proof[32];
static int Proof(uint64_t res, int add)
{
    for (int i = 0; i < 32; i++)
    {
        if (g_proof[i] == res) return 1;
        if (!g_proof[i]) { if (add) g_proof[i] = res; return 0; }
    }
    return 0;
}
/* never hers to burn down: fabricators and dropships (structures and transports die their own way) */
static int NoOwn(uint64_t res)
{
    if (!HasPart(res, "BehaviorComponentData") && !HasPart(res, "TurretComponentData")) return 1;
    return res == UINT64_C(0xd37e8d120d2836e3) || res == UINT64_C(0x4fb8eb356ac8553e) || res == UINT64_C(0xdb90077e76faa025);
}
static uintptr_t HealthAddr(uint32_t entity)
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
        return records + 440u * index + 20;
    }
    return 0;
}
/* an enemy's full health: its kind's, or what it has now if that is more; kept on its heat entry */
static int32_t FullHealth(Heat* c, uint64_t res, int32_t hp)
{
    if (c->ownMax > 0) return c->ownMax;
    uint32_t stride = 0;
    int32_t top = 0;
    uintptr_t rec = res ? hf->component_record("HealthComponentData", res, &stride) : 0;
    if (!rec || !ReadAt(rec, &top, 4) || top < 1 || top > 10000000) top = 0;
    c->ownMax = top > hp ? top : hp;
    return c->ownMax;
}
static unsigned g_ownTicks, g_ownKills;
static double g_ownDealt;
static void OwnStart(Heat* c)
{
    c->ownUntil = g_now + s_ownSeconds;
    if (c->ownNext < g_now) c->ownNext = g_now + s_ownEvery;
}
static void Own(const float p[3]);
static double g_accUntil;
/* her burn's ticks: straight off its health, past its armour (down to 1: the game deals the last blow, OwnKill) */
static void OwnFrame(void)
{
    for (int i = 0; i < HEAT; i++)
    {
        Heat* c = &g_heat[i];
        if (!c->e) continue;
        int32_t hp;
        static unsigned ends;
        if (c->ownTotal > 0 && c->ownUntil <= g_now && !c->cooked)
        {
            int ok = HealthOf(c->e, &hp);
            if (ends++ < 60) Log("her own burn on entity %u (kind %016llx) ended: %.0f health burnt off, %d left", c->e, (unsigned long long)c->res, c->ownTotal, ok ? hp : 0);
            c->ownTotal = 0;
        }
        if (c->ownUntil <= g_now && !c->cooked) continue;
        if (!HealthOf(c->e, &hp) || hp <= 0)
        {
            if (c->ownTotal > 0 && ends++ < 60) Log("entity %u (kind %016llx) died while her own burn was on it (%.0f health burnt off)", c->e, (unsigned long long)c->res, c->ownTotal);
            c->ownUntil = 0; c->cooked = 0; c->ownTotal = 0; c->ownMax = 0;
            continue;
        }
        if (c->cooked)
        {
            /* at 1 health: a fire burst on it, whose hit the damage listener turns into the kill */
            if (g_now > c->cookUntil) { c->cooked = 0; continue; }
            if (g_now - c->cookAt < 0.5) continue;
            c->cookAt = g_now;
            float p[3];
            int k = FoeIndex(c->e);
            if (k >= 0 && TargetPos(c->e, g_foeRes[k], p) && s_s1Burst > 0) { Own(p); hf->spawn_explosion(self, (uint32_t)s_s1Burst, p); }
            continue;
        }
        if (g_now < c->ownNext) continue;
        c->ownNext = g_now + s_ownEvery;
        c->burnUntil = g_now + s_burnSeconds;
        double mult = 1;
        if (c->smolderUntil > g_now) mult *= s_smolderMult;
        if (c->overburnUntil > g_now) mult *= s_overburnMult;
        if (g_accUntil > g_now) mult *= s_accMult;
        /* a share of its full health a tick (its kind's, or what it had when her burn began if that is more), never
         * less than own_burn_damage: a tank and a walker burn through in the same time */
        FullHealth(c, c->res, hp);
        double base = c->ownMax * (double)s_ownPercent / 100.0;
        if (base < s_ownDamage) base = s_ownDamage;
        int32_t dmg = (int32_t)floor(base * mult + 0.5), want = hp - dmg;
        if (want < 1) { want = 1; c->cooked = 1; c->cookAt = 0; c->cookUntil = g_now + 6; }
        uintptr_t a = HealthAddr(c->e);
        if (!a || hf->write(a, &want, 4)) { static unsigned told; if (told++ < 4) Log("her own burn: entity %u's health cannot be written", c->e); continue; }
        g_ownTicks++;
        g_ownDealt += hp - want;
        c->ownTotal += hp - want;
        static unsigned told;
        if (told++ < 16 || c->cooked) Log("her own burn on entity %u (kind %016llx): health %d -> %d of %d (x%.2f)%s", c->e, (unsigned long long)c->res, hp, want, c->ownMax, mult,
                                          c->cooked ? ": burnt through, the next hit on it kills" : "");
    }
}
static void TookFrame(void)
{
    if (g_now - g_tookAt < 0.5) return;
    g_tookAt = g_now;
    for (int i = 0; i < HEAT; i++)
    {
        Heat* c = &g_heat[i];
        if (!c->e || c->litAt <= 0 || g_now - c->litAt < 2.0) continue;
        int caught = c->tickAt >= c->litAt, alive = Alive(c->e);
        if (c->stage == 1) continue;                           /* being poured on: PourFrame's */
        if (!caught && alive && s_ownBurn && !NoOwn(c->res))
        {
            if (!Proof(c->res, 0)) Log("kind %016llx takes no fire damage (entity %u, 2 s after her fire): her own burn on it and its kind from now on", (unsigned long long)c->res, c->e);
            Proof(c->res, 1);
            OwnStart(c);
            Took(c->res, 2);
            c->litAt = 0;
            continue;
        }
        if (!caught && alive && s_firePour > 0)
        {
            c->stage = 1; c->litAt = g_now; c->poured = 0;
            continue;
        }
        if (caught || alive) Took(c->res, caught);
        c->litAt = 0;
    }
}
/* kinds her plain fire did not light, and that more build-up did: poured on from the start next time */
static uint64_t g_hard[32];
static int Hard(uint64_t res, int add)
{
    for (int i = 0; i < 32; i++)
    {
        if (g_hard[i] == res) return 1;
        if (!g_hard[i]) { if (add) g_hard[i] = res; return 0; }
    }
    return 0;
}
/* a Smoldering enemy stays on fire for as long as it Smolders: the game's fire goes out sooner, so it is lit again
 * every second (her own burn, on what the game's fire cannot hurt, is kept up the same way) */
static double g_keepAt;
static unsigned g_relit;
static void KeepFrame(void)
{
    if (g_now - g_keepAt < 1.0) return;
    g_keepAt = g_now;
    for (int i = 0; i < HEAT; i++)
    {
        Heat* c = &g_heat[i];
        if (!c->e || c->smolderUntil <= g_now + 0.5 || !Alive(c->e)) continue;
        if (c->ownUntil > g_now || (s_ownBurn && c->res && Proof(c->res, 0))) { if (c->ownUntil < c->smolderUntil) c->ownUntil = c->smolderUntil; continue; }
        if (hf->apply_status && hf->apply_status(self, c->e, (uint32_t)s_fireStatus, s_s2Buildup > 1000 ? 1000 : s_s2Buildup) != 0) { Burns(c->e); c->hersUntil = g_now + s_burnSeconds + 3; g_relit++; }
    }
}
/* the enemy list holds 1024: the dead are taken off it, a part of the list each frame, so a crowded mission does not
 * push the living off. Only one seen alive before goes (a fresh spawn may have no health yet) */
static uint32_t g_wasAlive[FOES];
static int g_pruneAt;
static unsigned g_pruned, g_foePeak;
static void PruneFrame(void)
{
    for (int n = 0; n < 96; n++)
    {
        int k = g_pruneAt++ % FOES;
        uint32_t e = g_foe[k];
        if (!e) continue;
        if (Alive(e)) g_wasAlive[k] = e;
        else if (g_wasAlive[k] == e) { g_foe[k] = 0; g_wasAlive[k] = 0; g_pruned++; }
    }
    if (g_pruneAt % (FOES * 4) < 96) { unsigned c = (unsigned)FoeCount(); if (c > g_foePeak) g_foePeak = c; }
}
/* an enemy her fire did not light: fire build-up on it every frame until fire damage shows, s_firePour seconds at most */
static void PourFrame(void)
{
    static unsigned told;
    for (int i = 0; i < HEAT; i++)
    {
        Heat* c = &g_heat[i];
        if (!c->e || c->stage != 1) continue;
        if (!Alive(c->e)) { c->stage = 0; c->litAt = 0; continue; }
        if (c->tickAt >= c->litAt)
        {
            if (told++ < 80) Log("entity %u (kind %016llx) caught fire after %.0f more build-up over %.1f s", c->e, (unsigned long long)c->res, (double)c->poured, g_now - c->litAt);
            Took(c->res, 2); Hard(c->res, 1);
            c->stage = 0; c->litAt = 0;
        }
        else if (g_now - c->litAt > s_firePour)
        {
            if (told++ < 80) Log("entity %u (kind %016llx) took no fire damage after %.0f more build-up over %.1f s", c->e, (unsigned long long)c->res, (double)c->poured, g_now - c->litAt);
            Took(c->res, 0);
            c->stage = 0; c->litAt = 0;
        }
        else if (hf->apply_status)
        {
            /* the plain status in bulk, and the heavy one the way a napalm shell lays it on (50 a tick) */
            if (hf->apply_status(self, c->e, (uint32_t)s_fireStatus, 1000) != 0) c->poured += 1000;
            if (s_fireHeavy > 0) hf->apply_status(self, c->e, (uint32_t)s_fireHeavy, s_fireHeavyStep);
        }
    }
}
static void Ignite(uint32_t e, float buildup, const char* what)
{
    int fk = FoeIndex(e);
    if (!e || fk < 0 || !Alive(e)) return;
    Status(e, (uint32_t)s_fireStatus, buildup, what);
    Burns(e);
    { Heat* hc = HeatOf(e, 0); if (hc) { hc->hersUntil = g_now + s_burnSeconds + 3; if (!hc->res) hc->res = g_foeRes[fk]; } }
    {
        Heat* c = HeatOf(e, 0);
        if (c && c->litAt <= 0)
        {
            c->litAt = g_now; c->res = g_foeRes[fk]; c->poured = 0;
            c->stage = s_firePour > 0 && Hard(c->res, 0) ? 1 : 0;
        }
        if (c && s_ownBurn && Proof(g_foeRes[fk], 0)) { c->res = g_foeRes[fk]; OwnStart(c); c->litAt = 0; c->stage = 0; }
    }
    g_ignitions++;
}
/* fire bursts, from the frame too: on an enemy, where it stands */
#define BURSTQ 32
static struct { uint32_t e; float p[3]; } g_burst[BURSTQ];
static atomic_uint g_burstIn;
static unsigned g_burstOut;
static double g_burstTokens;
static void S2BlastFrame(void);
static void HeatFrame(void)
{
    unsigned in = atomic_load(&g_ignIn);
    if (in - g_ignOut > IGNQ) g_ignOut = in - IGNQ;
    for (; g_ignOut != in; g_ignOut++) Ignite(g_ign[g_ignOut % IGNQ].e, g_ign[g_ignOut % IGNQ].buildup, "her fire");
    TookFrame();
    PourFrame();
    OwnFrame();
    KeepFrame();
    S2BlastFrame();
    PruneFrame();
    FactionFrame();
    in = atomic_load(&g_burstIn);
    if (in - g_burstOut > BURSTQ) g_burstOut = in - BURSTQ;
    for (; g_burstOut != in; g_burstOut++)
    {
        uint32_t e = g_burst[g_burstOut % BURSTQ].e;
        float p[3];
        int k = FoeIndex(e);
        if (k < 0 || !TargetPos(e, g_foeRes[k], p)) memcpy(p, g_burst[g_burstOut % BURSTQ].p, 12);
        if (s_s1Burst <= 0) continue;
        Own(p);
        int r = hf->spawn_explosion(self, (uint32_t)s_s1Burst, p);
        if (r == HF_ACT_WAITING) return;                       /* its package is loading: this one again next frame */
        if (Act("a fire burst", r)) g_bursts++;
    }
}

/* ---- Confectance Index ------------------------------------------------------------------------------------------ */
static void Confect(int n)
{
    g_conf += n;
    if (g_conf < 0) g_conf = 0;
    if (g_conf < s_confMax) return;
    g_conf = 0;
    g_confUntil = g_now + s_confSeconds;
    g_spends++;
    Log("Confectance Index full: spent, her hits deal x%.2f for %.0f s", (double)s_confMult, (double)s_confSeconds);
}
static void Cast(int skill)
{
    g_casts[skill]++;
    Confect(s_confCast);
}

/* ---- Dead End Meltdown ------------------------------------------------------------------------------------------ */
static void Meltdown(void)
{
    if (g_now < g_cd[0]) return;
    g_cd[0] = g_now + s_s1Cd;
    g_s1Until = g_now + s_s1Seconds;
    Log("Dead End Meltdown: her shots set what they hit on fire for %.0f s; a shot on a burning enemy sets off a fire burst", (double)s_s1Seconds);
    Cast(0);
}
/* one of her shots landed on an enemy while it is on */
static void MeltdownHit(uint32_t target, const float at[3])
{
    Heat* c = HeatOf(target, 1);
    if (!c) return;
    if (c->burnUntil > g_now && g_now - c->burnSince >= 0.3)
    {
        if (c->overburnUntil <= g_now) g_overburns++;
        c->overburnUntil = g_now + s_overburnSeconds;
        if (g_now - c->burstAt >= s_s1BurstEvery && g_burstTokens >= 1)
        {
            g_burstTokens -= 1;
            c->burstAt = g_now;
            unsigned i = atomic_fetch_add(&g_burstIn, 1) % BURSTQ;
            g_burst[i].e = target; memcpy(g_burst[i].p, at, 12);
        }
    }
    IgniteLater(target, s_fireBuildup);
}

/* Portent of Doom's blast: set off from the frame until the game takes it (its package may still be loading on the
 * first cast of a session: up to 6 s) */
static float g_s2BlastAt[3];
static double g_s2BlastUntil;
static void S2BlastFrame(void)
{
    if (g_s2BlastUntil <= 0) return;
    if (g_now > g_s2BlastUntil || s_s2Blast <= 0) { g_s2BlastUntil = 0; return; }
    Own(g_s2BlastAt);
    int r = hf->spawn_explosion(self, (uint32_t)s_s2Blast, g_s2BlastAt);
    if (r == HF_ACT_WAITING) return;
    g_s2BlastUntil = 0;
    Act("Portent of Doom's blast", r);
}
/* ---- Portent of Doom -------------------------------------------------------------------------------------------- */
static void Portent(void)
{
    if (g_now < g_cd[1]) return;
    float at[3];
    uint32_t target;
    g_aimBlocked = 0;
    if (Aim(s_s2Cone, s_s2Range, 0, at, &target) != 1)
    {
        static unsigned told;
        if (told++ < 8) Log("Portent of Doom: no enemy under her crosshair (%u near it but out of sight; %d enemies known)", g_aimBlocked, FoeCount());
        return;
    }
    g_cd[1] = g_now + s_s2Cd;
    Ignite(target, s_s2Buildup, "Portent of Doom");
    if (s_s2Blast > 0)
    {
        /* seen from afar: a gas vent's blast on it (her own blast: it does nothing to her) */
        float bp[3];
        int bk = FoeIndex(target);
        if (bk < 0 || !TargetPos(target, g_foeRes[bk], bp)) memcpy(bp, at, 12);
        memcpy(g_s2BlastAt, bp, 12);
        g_s2BlastUntil = g_now + 6;
        S2BlastFrame();
    }
    Heat* c = HeatOf(target, 1);
    if (c) c->smolderUntil = g_now + s_smolderSeconds;
    Log("Portent of Doom: entity %u (%s) is on fire and Smolders for %.0f s (fire damage on it x%.2f)", target,
        g_aimHow == 1 ? "her crosshair on its body" : g_aimHow == 2 ? "her crosshair on one of its parts" : "nearest her crosshair", (double)s_smolderSeconds, (double)s_smolderMult);
    Cast(1);
}

/* ---- Searing Finale --------------------------------------------------------------------------------------------- */
static float g_finAt[3];
static int g_finLeft;
static double g_finNext;
/* where her shells are to land: kept as her own blasts until they have (the own-blast guard forgets one after 1.2 s, and
 * a shell takes longer to fall) */
static struct { float p[3]; double until; } g_finOwn[12];
static double g_finOwnAt;
static void Finale(void)
{
    if (g_now < g_cd[2]) return;
    float at[3];
    uint32_t target;
    if (!Aim(s_s3Cone, s_s3Range, 1, at, &target)) { static unsigned told; if (told++ < 5) Log("Searing Finale: her aim is unknown"); return; }
    g_cd[2] = g_now + s_s3Cd;
    memcpy(g_finAt, at, 12);
    g_finLeft = s_s3Shells;
    g_finNext = g_now;
    g_accUntil = g_now + s_accSeconds;
    if (s_s3Refund) { g_cd[0] = g_now; g_cd[1] = g_now; }
    Log("Searing Finale: %d napalm shells on %s; Accelerant for %.0f s (fire damage x%.2f)%s", s_s3Shells, target ? "her target" : "where she aims",
        (double)s_accSeconds, (double)s_accMult, s_s3Refund ? "; her other skills are ready again" : "");
    Cast(2);
}
static void FinaleFrame(void)
{
    if (g_now - g_finOwnAt >= 0.5)
    {
        g_finOwnAt = g_now;
        for (int i = 0; i < 12; i++) if (g_finOwn[i].until > g_now) Own(g_finOwn[i].p);
    }
    while (g_finLeft > 0 && g_now >= g_finNext)
    {
        float p[3], from[3], down[3] = {0, 0, -1};
        memcpy(p, g_finAt, 12);
        if (g_finLeft != s_s3Shells)                           /* the first one on the mark, the rest around it */
        {
            float a = (float)rand() / RAND_MAX * 6.2831853f, r = (0.4f + 0.6f * (float)rand() / RAND_MAX) * s_s3Spread;
            p[0] += cosf(a) * r; p[1] += sinf(a) * r;
        }
        from[0] = p[0]; from[1] = p[1]; from[2] = p[2] + s_s3Height;
        Own(p);
        for (int i = 0; i < 12; i++) if (g_finOwn[i].until <= g_now) { memcpy(g_finOwn[i].p, p, 12); g_finOwn[i].until = g_now + 6; break; }
        int r = hf->spawn_projectile(self, (uint32_t)s_s3Shell, from, down);
        if (r == HF_ACT_WAITING) { g_finNext = g_now + 0.1; return; }
        if (Act("a napalm shell", r)) g_shells++;
        g_finLeft--;
        g_finNext = g_now + s_s3Every;
    }
}

/* ---- Perception Block: fire does nothing to her. The game's status build-up (game.dll+0x699E40, every status on every
 * entity; docs/research/game-notes.md, as Alva's EMS immunity) is where a fire status is refused on her, and where she
 * learns what burns ------------------------------------------------------------------------------------------------ */
typedef void (*AddStatusFn)(void*, uint32_t, uint32_t, uint32_t, float, uint32_t, uint64_t);
static AddStatusFn original_add;
static atomic_int g_statusHooked;
/* the last fire statuses seen, on anything: who and when (the element of the fire damage that follows is learned) */
#define LIT 32
static struct { uint32_t e; unsigned long long ms; } g_lit[LIT];
static atomic_uint g_litNext;
static int FireStatus(uint32_t st)
{
    for (int i = 0; i < 4; i++) if (s_fireSt[i] && st == (uint32_t)s_fireSt[i]) return 1;
    return 0;
}
static int LitLately(uint32_t e, unsigned long long within)
{
    unsigned long long now = GetTickCount64();
    for (int i = 0; i < LIT; i++) if (g_lit[i].e == e && g_lit[i].ms && now - g_lit[i].ms <= within) return 1;
    return 0;
}
static int Refuse(uint32_t e, uint32_t st, float buildup, uint32_t from)
{
    if (!g_inMission || !kb_kit_on || !e || !FireStatus(st)) return 0;
    unsigned i = atomic_fetch_add(&g_litNext, 1) % LIT;
    g_lit[i].ms = 0; g_lit[i].e = e; g_lit[i].ms = GetTickCount64();
    if (e != atomic_load(&g_avatar)) { if (st != 67u && FoeIndex(e) >= 0) Burns(e); return 0; }
    if (!s_fireImmune) return 0;
    if (g_refusedHer++ < 12) Log("fire status %u refused on her (build-up %.1f, from %u)", st, (double)buildup, from);
    return 1;
}
static void AddStatus(void* m, uint32_t e, uint32_t st, uint32_t r9, float buildup, uint32_t from, uint64_t z)
{
    if (Refuse(e, st, buildup, from)) return;
    /* every status the game puts on an enemy, once each: names what an environment's fire uses */
    {
        static atomic_uint seen[8];
        if (st < 256 && e != atomic_load(&g_avatar) && !(atomic_fetch_or(&seen[st >> 5], 1u << (st & 31)) & (1u << (st & 31))))
        {
            int k = FoeIndex(e);
            if (k >= 0) Log("status %u first seen on an enemy: entity %u (kind %016llx), build-up %.1f, from %u", st, e, (unsigned long long)g_foeRes[k], (double)buildup, from);
            else atomic_fetch_and(&seen[st >> 5], ~(1u << (st & 31)));
        }
    }
    original_add(m, e, st, r9, buildup, from, z);
}
static void StatusInstall(void)
{
    if (!hf->code_matches(game + 0x699E40, "40 55 53 57 41 55 41 57 48 8d 6c 24 d0 48 81 ec 30 01 00 00"))
    { Log("status guard off: game.dll+0x699E40 is not the reviewed code (fire statuses land on her; burning is known only from her own fire)"); return; }
    if (hf->hook(self, game + 0x699E40, (void*)AddStatus, (void**)&original_add)) { Log("status guard off: the hook could not be placed"); return; }
    atomic_store(&g_statusHooked, 1);
    Log("status guard ready: fire statuses %d %d %d %d never land on her", s_fireSt[0], s_fireSt[1], s_fireSt[2], s_fireSt[3]);
}

/* ---- the damage event ------------------------------------------------------------------------------------------- */
/* fire's damage: damage over time of fire's element; a hit of any kind of that element when the element is not 0 (0 is
 * also what plain bullets come with) */
static int IsFire(const HfDamageEvent* d)
{
    if (g_fireElem < 0 || (int)d->element != g_fireElem) return 0;
    return d->kind == 2 || g_fireElem != 0;
}
static void DamageSeen(HfDamagePreEvent* e, HfDamageEvent* d)
{
    uint32_t me = atomic_load(&g_avatar);
    /* what element fire is: damage over time on something a fire status just landed on (or was refused on) */
    if (g_fireElem < 0 && d->kind == 2 && d->amount > 0 && LitLately(d->target, 1500))
    {
        g_fireElem = (int)d->element;
        Log("fire's damage element learned: %d (kind 2 on entity %u; fire_element = %d in vector.ini skips the learning)", g_fireElem, d->target, g_fireElem);
    }
    /* what lands on an enemy she is pouring fire on: names the damage a hard-to-light enemy's fire deals */
    {
        Heat* pc = HeatOf(d->target, 0);
        static unsigned told;
        if (pc && pc->stage == 1 && told++ < 40) Log("while pouring: entity %u takes %d (kind %u, element %u)", d->target, d->amount, d->kind, d->element);
    }
    /* burnt through by her own burn (1 health left): this hit kills it, the game's way */
    {
        Heat* oc = HeatOf(d->target, 0);
        int32_t hp;
        if (oc && oc->cooked && HealthOf(d->target, &hp) && hp <= 1)
        {
            d->amount = -1;
            oc->cooked = 0; oc->ownUntil = 0;
            g_ownKills++;
            Log("her own burn: entity %u (kind %016llx) burnt through and killed (a kind %u hit)", d->target, (unsigned long long)oc->res, d->kind);
            return;
        }
    }
    if (!IsFire(d)) return;
    if (d->amount <= 0)
    {
        static unsigned told;
        int k = FoeIndex(d->target);
        if (k >= 0 && told++ < 20) Log("fire on entity %u (kind %016llx) for no damage (kind %u)", d->target, (unsigned long long)g_foeRes[k], d->kind);
        return;
    }
    if (d->target == me && me)
    {
        if (!s_fireImmune) return;
        e->cancel = 1;
        if (g_fireOnHer++ < 6) Log("fire on her: %d damage cancelled (kind %u, element %u)", d->amount, d->kind, d->element);
        return;
    }
    if (FoeIndex(d->target) < 0) return;
    Burns(d->target);
    Heat* c = HeatOf(d->target, 0);
    if (c) c->tickAt = g_now;
    double mult = 1;
    if (c && c->smolderUntil > g_now) mult *= s_smolderMult;
    if (c && c->overburnUntil > g_now) mult *= s_overburnMult;
    if (g_accUntil > g_now) mult *= s_accMult;
    /* her fire scales with what it burns: a tick of fire she lit (or any fire during Accelerant) is never less than
     * own_burn_percent of the enemy's full health, so a walker's burn is not a soldier's 25 a tick */
    if (c && d->kind == 2 && s_ownPercent > 0 && (c->hersUntil > g_now || c->smolderUntil > g_now || c->overburnUntil > g_now || g_accUntil > g_now))
    {
        int32_t hp = 0;
        int k = FoeIndex(d->target);
        if (k >= 0 && HealthOf(d->target, &hp) && hp > 0)
        {
            double floor_ = FullHealth(c, g_foeRes[k], hp) * (double)s_ownPercent / 100.0;
            if (floor_ > d->amount)
            {
                static unsigned told;
                if (told++ < 12) Log("fire on entity %u (kind %016llx): a tick of %d raised to %.0f (%.2f%% of its %d health)", d->target, (unsigned long long)g_foeRes[k], d->amount,
                                     floor_, (double)s_ownPercent, c->ownMax);
                g_fireBonus += floor(floor_ + 0.5) - d->amount;
                d->amount = (int32_t)floor(floor_ + 0.5);
            }
        }
    }
    if (mult != 1)
    {
        int before = d->amount;
        ScaleDamage(d, mult);
        g_fireBonus += d->amount - before;
        if (g_scaled++ < 8) Log("fire on entity %u: %d -> %d (x%.2f:%s%s%s)", d->target, before, d->amount, mult, c && c->smolderUntil > g_now ? " Smolder" : "",
                                c && c->overburnUntil > g_now ? " Overburn" : "", g_accUntil > g_now ? " Accelerant" : "");
    }
}
static void DamageDealt(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    (void)e; (void)me;
    if (d->amount <= 0 || !MineHit(d) || FoeIndex(d->target) < 0) return;
    if (g_confUntil > g_now) ScaleDamage(d, s_confMult);
    if (!IsShot(d)) return;
    NoteTarget(d);
    if (g_s1Until > g_now) MeltdownHit(d->target, d->position);
}

/* ---- events ------------------------------------------------------------------------------------------------------ */
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    if (s->resource != AVATAR)
    {
        if (FoeKind(s->resource)) AddFoe(s->entity, s->resource);
        SeeAny(s->entity, s->resource);
        return;
    }
    if (!s->authority) return;
    g_spawnEntity[g_spawnNext % 8] = s->entity;
    g_spawnNet[g_spawnNext % 8] = s->network;
    g_spawnNext++;
    LearnNet();
}
static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfKillEvent* k = (const HfKillEvent*)info;
    DropFoe(k->victim);
    Heat* c = HeatOf(k->victim, 0);
    if (c) memset(c, 0, sizeof *c);
    if (k->victim == g_lastTarget) g_lastTarget = 0;
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
    else { g_alive = 0; g_s1Until = 0; }
}
static void Reset(const char* why)
{
    int start = !strcmp(why, "mission start");
    if (start) KitDecide();
    if (g_casts[0] || g_casts[1] || g_casts[2] || g_refusedHer || g_fireOnHer)
        Log("%s: casts %u / %u / %u, %u ignitions, %u fire bursts, %u Overburns, %u napalm shells, Confectance spent %u times, +%.0f fire damage from "
            "Smolder, Overburn and Accelerant; on her: %u fire statuses refused, %u fire hits cancelled; fire's element %d",
            why, g_casts[0], g_casts[1], g_casts[2], g_ignitions, g_bursts, g_overburns, g_shells, g_spends, g_fireBonus, g_refusedHer, g_fireOnHer, g_fireElem);
    TookReport();
    g_facDiagAt = start ? g_now + 20 : 0;
    if (g_relit) Log("Smolder kept %u fires alight", g_relit);
    g_relit = 0; g_keepAt = 0;
    if (g_ownTicks) Log("her own burn: %u ticks, %.0f health burnt off past armour, %u kills", g_ownTicks, g_ownDealt, g_ownKills);
    g_ownTicks = g_ownKills = 0; g_ownDealt = 0;
    if (g_pruned || g_foePeak) Log("enemy list: %u at its fullest (it holds %d), %u dead taken off it", g_foePeak, FOES, g_pruned);
    g_pruned = g_foePeak = 0;
    g_tookAt = 0;
    Body(start);
    if (start && s_hellbomb && s_hellbombRow == 125 && !hf->load_package(self, UINT64_C(0x346D785D6CA13FF4))) Log("the Hellbomb's package could not be loaded ahead");
    g_boomPending = 0;
    g_boomArmed = 0;
    memset(g_cd, 0, sizeof g_cd);
    memset(g_casts, 0, sizeof g_casts);
    memset(g_heat, 0, sizeof g_heat);
    g_ignitions = g_bursts = g_overburns = g_shells = g_spends = g_fireOnHer = g_refusedHer = g_scaled = 0;
    g_fireBonus = 0;
    g_s1Until = g_confUntil = g_accUntil = 0;
    g_conf = 0;
    g_finLeft = 0;
    memset(g_finOwn, 0, sizeof g_finOwn);
    g_ignOut = atomic_load(&g_ignIn);
    g_burstOut = atomic_load(&g_burstIn);
    g_burstTokens = s_burstRate;
    g_lastTarget = 0;
    if (start && s_fireElement >= 0) g_fireElem = s_fireElement;
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gameSize = 0, exeSize = 0;
    uintptr_t g = hf->module_base("game.dll", &gameSize), x = hf->module_base("helldivers2.exe", &exeSize);
    if (!g || !x || gameSize != 74727424u || exeSize != 60719104u) { Log("REFUSED: the game's modules are not build " BUILD "'s: her kit stays off"); return; }
    game = g;
    exe = x;
    HitsInstall("own-blast guard");
    StatusInstall();
    if (hf->code_matches(exe + 0x79F860, "33 c0 4c 8d 05 c7 b2 01 02 0f 1f 80 00 00 00 00 49 39 0c c0 74 0c ff c0 83 f8 04 75 f3") &&
        hf->code_matches(exe + 0x7F4590, "48 8b c4 f3 0f 11 58 20 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48"))
    {
        g_worldId = (WorldFn)(exe + 0x79F860);
        g_raycast = (RayFn)(exe + 0x7F4590);
        Log("line of sight ready (the game's raycast)");
    }
    else Log("line of sight off: the raycast is not the reviewed code (aims go through walls)");
    g_staminaOk = hf->code_matches(game + 0xA50E2D, "f3 42 0f 11 8c 11 54 78 54 00");
}

/* ---- for the HUD ------------------------------------------------------------------------------------------------ */
typedef struct VectorHud
{
    uint32_t version, in_mission;
    float cd[3], cd_max[3];            /* seconds left, full cooldowns: Dead End Meltdown, Portent of Doom, Searing Finale */
    uint32_t s1_on;
    float s1_left;
    uint32_t conf, conf_max, conf_on;
    float conf_left, conf_mult, acc_left, acc_mult;
    uint32_t burning, smoldering, overburned, fire_immune;
    uint32_t keys[3];
    char key_label[3][24];
    float stamina;                     /* her stamina bar 0-1 (-1: unread), for the low-stamina warning */
    uint32_t hellbomb;                 /* hellbomb_on_exhaustion */
} VectorHud;
static VectorHud g_hud = {1};
static int g_nBurning, g_nSmolder, g_nOverburn;
/* the Smoldering and Overburned enemies, for the HUD's marker over each: x, y, z, kind (1 Smolder, 2 Overburn, 3 both),
 * seconds left */
static float g_heatOut[32][5];
static volatile int g_nheat;
static float g_viewOut[6];
static volatile int g_viewOk;
static void HudFrame(void)
{
    int n = 0, b = 0, s = 0, o = 0;
    for (int i = 0; i < HEAT; i++)
    {
        Heat* c = &g_heat[i];
        if (!c->e) continue;
        int sm = c->smolderUntil > g_now, ob = c->overburnUntil > g_now;
        if (c->burnUntil > g_now) b++;
        if (sm) s++;
        if (ob) o++;
        float p[3];
        if ((sm || ob) && n < 32 && TargetPos(c->e, 0, p))
        {
            g_heatOut[n][0] = p[0]; g_heatOut[n][1] = p[1]; g_heatOut[n][2] = p[2];
            g_heatOut[n][3] = (float)(sm + 2 * ob);
            g_heatOut[n][4] = (float)(fmax(c->smolderUntil, c->overburnUntil) - g_now);
            n++;
        }
    }
    g_nheat = n;
    g_nBurning = b; g_nSmolder = s; g_nOverburn = o;
    float c[3], f[3];
    int fp;
    if (!View(c, f, &fp)) { g_viewOk = 0; return; }
    memcpy(g_viewOut, c, 12); memcpy(g_viewOut + 3, f, 12);
    g_viewOk = 1;
}
HF_MOD_EXPORT int vector_heat(unsigned version, float* out, int max)
{
    if (version != 1 || !out || max <= 0 || !g_inMission || !kb_kit_on) return 0;
    int n = g_nheat;
    if (n > max) n = max;
    memcpy(out, g_heatOut, (size_t)n * 20);
    return n;
}
HF_MOD_EXPORT int vector_view(unsigned version, float* out)
{
    if (version != 1 || !out || !g_viewOk || !g_inMission) return 0;
    memcpy(out, g_viewOut, 24);
    return 1;
}
HF_MOD_EXPORT VectorHud* vector_hud_state(void)
{
    g_hud.in_mission = (uint32_t)(g_inMission && g_alive && kb_kit_on);
    float m[3] = {s_s1Cd, s_s2Cd, s_s3Cd};
    for (int i = 0; i < 3; i++) { g_hud.cd[i] = (float)fmax(0, g_cd[i] - g_now); g_hud.cd_max[i] = m[i]; }
    g_hud.s1_on = (uint32_t)(g_now < g_s1Until);
    g_hud.s1_left = (float)fmax(0, g_s1Until - g_now);
    g_hud.conf = (uint32_t)g_conf;
    g_hud.conf_max = (uint32_t)s_confMax;
    g_hud.conf_on = (uint32_t)(g_now < g_confUntil);
    g_hud.conf_left = (float)fmax(0, g_confUntil - g_now);
    g_hud.conf_mult = s_confMult;
    g_hud.acc_left = (float)fmax(0, g_accUntil - g_now);
    g_hud.acc_mult = s_accMult;
    g_hud.burning = (uint32_t)g_nBurning;
    g_hud.smoldering = (uint32_t)g_nSmolder;
    g_hud.overburned = (uint32_t)g_nOverburn;
    g_hud.fire_immune = (uint32_t)(s_fireImmune != 0);
    g_hud.stamina = g_inMission && g_alive ? StaminaNow() : -1;
    g_hud.hellbomb = (uint32_t)(s_hellbomb != 0);
    for (int i = 0; i < 3; i++)
    {
        g_hud.keys[i] = (uint32_t)s_keys[i].vk;
        memcpy(g_hud.key_label[i], s_keys[i].label, sizeof g_hud.key_label[i]);
    }
    return &g_hud;
}
static atomic_int g_bindPress;
HF_MOD_EXPORT int vector_input(unsigned version, int skill)
{
    if (version != 1) return 0;
    if (skill >= 1 && skill <= 3) atomic_fetch_or(&g_bindPress, 1 << (skill - 1));
    return 1;
}
HF_MOD_EXPORT int vector_key(unsigned version, int skill, int* vk, int* mods)
{
    if (version != 1 || skill < 1 || skill > 3 || !vk || !mods) return 0;
    *vk = s_keys[skill - 1].vk;
    *mods = s_keys[skill - 1].mods;
    return 1;
}

/* ---- the frame -------------------------------------------------------------------------------------------------- */
static void Tick(HfMod* mod, float dt, void* user)
{
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
            if (i == 0) Meltdown();
            else if (i == 1) Portent();
            else Finale();
        }
        prev[i] = down;
    }
    g_burstTokens += step * s_burstRate;
    if (g_burstTokens > s_burstRate) g_burstTokens = s_burstRate;
    HeatFrame();
    if (g_alive) FinaleFrame();
    HudFrame();
    Exhaustion();
    atomic_exchange(&g_ownStopped, 0);
    atomic_exchange(&g_kdStopped, 0);
}

/* ---- Mod Options Menu ------------------------------------------------------------------------------------------- */
static const KbOption kOptions[] = {
    {"conf_max", NULL, &s_confMax}, {"conf_mult", &s_confMult, NULL}, {"conf_seconds", &s_confSeconds, NULL},
    {"s1_cooldown", &s_s1Cd, NULL}, {"s1_seconds", &s_s1Seconds, NULL}, {"s1_burst_every", &s_s1BurstEvery, NULL},
    {"overburn_seconds", &s_overburnSeconds, NULL}, {"overburn_mult", &s_overburnMult, NULL},
    {"s2_cooldown", &s_s2Cd, NULL}, {"smolder_seconds", &s_smolderSeconds, NULL}, {"smolder_mult", &s_smolderMult, NULL},
    {"s3_cooldown", &s_s3Cd, NULL}, {"s3_shells", NULL, &s_s3Shells}, {"s3_spread", &s_s3Spread, NULL}, {"accelerant_seconds", &s_accSeconds, NULL},
    {"accelerant_mult", &s_accMult, NULL}, {"fire_immune", NULL, &s_fireImmune}, {"own_burn", NULL, &s_ownBurn}, {"own_burn_damage", &s_ownDamage, NULL}, {"own_burn_percent", &s_ownPercent, NULL}, {"own_burn_seconds", &s_ownSeconds, NULL}, {"hp_mult", &s_hpMult, NULL}, {"stamina_mult", &s_staminaMult, NULL},
};
static void OptionAfter(const char* name, int changed) { (void)name; (void)changed; }
HF_MOD_EXPORT int vector_option_get(const char* name, double* out) { return OptionGet(name, out); }
HF_MOD_EXPORT int vector_option_set(const char* name, double value) { return OptionSet(name, value); }

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    RosterJoin("vector", "Vector");
    kb_ini = "vector.ini";
    kb_key_names[0] = "key_s1";
    kb_key_names[1] = "key_s2";
    kb_key_names[2] = "key_s3";
    KB_SETTINGS(kSettings);
    KB_OPTIONS(kOptions);
    kb_option_after = OptionAfter;
    kb_damage_seen = DamageSeen;
    kb_damage_dealt = DamageDealt;
    if (!api || api->version < 13 || api->size < offsetof(HfApi, code_matches) + sizeof api->code_matches) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    LoadSettings();
    g_fireElem = s_fireElement;
    api->log(mod, "Vector %s: keys %s / %s / %s (Dead End Meltdown, Portent of Doom, Searing Finale); fire does %s to her", VERSION, s_keys[0].label,
             s_keys[1].label, s_keys[2].label, s_fireImmune ? "nothing" : "what it does to anyone");
    if (s_badKey) api->log(mod, "vector.ini: a key could not be read (bits %d: s1, s2, s3); its default stays", s_badKey);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) &&
             api->on_game_thread(mod, Tick, NULL);
    if (!ok) api->log(mod, "events or the game thread are unavailable: Vector stays off");
    return 0;
}
