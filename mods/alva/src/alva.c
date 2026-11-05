/* Alva (yui.alva): Alva's kit from Girls' Frontline 2 (AN-94, DEFY) as abilities on keys, for your own Helldiver (solo
 * or host): a support who shields every ally around her and deals freeze damage, built on the kit base (kitbase/,
 * docs/kit-base.md, docs/alva-design.md).
 *
 *   Brumal Barrier (passive): every skill she uses, and every shield_every s, gives each ally within shield_range m
 *     (her, the other Helldivers, sentries, emplacements, guard dogs, exosuits, SEAF, the FRV, the Pelican and mods'
 *     copies of them) a shield of shield_amount; shields stack with no cap and soak hits before their health
 *     (kitbase/kb_allies.h).
 *   Freezing Touch (passive): her hits and interceptions build Confectance (at most conf_max); her hits chill enemies
 *     (the flamer slow); a fully chilled enemy freezes (a stun) for frozen_seconds. A frozen enemy takes no damage
 *     except from an explosion (not EMS): that one hit, with its shell's direct hit, deals shatter_mult times and
 *     ends the freeze (0.2.0, 0.2.1). Her snow fields (Frosted Echo's nitrogen field, and a small one where each of its bursts lands) freeze
 *     whoever stands in them after field_freeze_seconds, divided by how many fields overlap there. Each skill she uses adds Battle Prep: at prep_max, the skill with the longest cooldown left
 *     is ready again.
 *   Snow Wolf's Heart (key_s1, default Z): Covering Mode for s1_seconds: an enemy that hits an ally within s1_range m of
 *     her gets an interception shot (projectile s1_round) from her, at most one every s1_every s; each one +1
 *     Confectance and chills it.
 *   Frosted Echo (key_s2, default X): at what she aims at (the enemy under her crosshair, else where her aim lands),
 *     one EMS burst per Confectance point (at least one), and a Liquid Nitrogen field (s2_field_radius m) for
 *     s2_field_seconds that slows and chills every enemy in it. Spends her Confectance.
 *   Nix Requiem (key_s3, default C): on the enemy under her crosshair (in sight), one heavy round (projectile s3_round)
 *     per Confectance point (at least one); with s3_freeze_at points or more it freezes at once. Spends her Confectance.
 *
 * Research credit: the kit base's (Helldiver Survival Rework by enochlophobianumerophobia, MIT; HD2Runtime by
 * SkyeShade); the camera pose as Apache - Rise Above (RSNLRiggs, after Khi) reads it. See THIRD_PARTY_NOTICES.md. */
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

#define VERSION "0.2.2"
#define BUILD "25480438"

#define KB_OWN_SLOTS 32   /* her chill bursts come often */
#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"
#include "kitbase/kb_allies.h"
#include "kitbase/kb_body.h"
#include "kitbase/kb_hits.h"
#include "kitbase/kb_damage.h"
#include "kitbase/kb_settings.h"
#include "kitbase/kb_roster.h"

KB_ROSTER_EXPORTS(alva)

/* ---- settings ------------------------------------------------------------------------------------------------- */
static float s_shieldAmount = 500, s_shieldRange = 30, s_shieldEvery = 20;
static int s_shieldOnCast = 1;
static int s_confMax = 10, s_prepMax = 3;
static float s_confEvery = 0.3f;
static float s_chillHit = 10, s_chillBurst = 25, s_chillS3 = 20, s_chillShot = 15;
static float s_fieldFreeze = 3, s_burstFieldRadius = 10, s_burstFieldSeconds = 6;   /* 0.2.2: 10 m, an EMS field's */
static float s_frozenSeconds = 4, s_shatterMult = 8, s_stunBuildup = 200, s_slowBuildup = 60;
static float s_s1Cd = 25, s_s1Seconds = 12, s_s1Range = 30, s_s1Reach = 150, s_s1Every = 0.4f;
static int s_s1Round = 305;
static float s_s2Cd = 18, s_s2Every = 0.25f, s_s2Spread = 3, s_s2FieldSeconds = 12, s_s2FieldRadius = 8, s_s2Cone = 10, s_s2Range = 150;
static int s_s2Explosion = 150;
static float s_s3Cd = 40, s_s3Every = 0.2f, s_s3Cone = 10, s_s3Range = 200;
static int s_s3Round = 226, s_s3FreezeAt = 5;
static int s_sight = 1;
static int s_chillFx = 1, s_fxExplosion = 150;
static int s_emsImmune = 1, s_allyGuard = 1, s_emsSt[4] = {37, 38, 39, 0}, s_emsBoom[2] = {0, 0};
static float s_fxEvery = 0.4f, s_fxRate = 8;
static int s_emsShell = 154;
static float s_emsShellHeight = 4;
static float s_fpDistance = 0.45f;

static const KbSetting kSettings[] = {
    KB_F("shield_amount", s_shieldAmount, 0, 100000), KB_F("shield_range", s_shieldRange, 1, 500), KB_F("shield_every", s_shieldEvery, 0, 600),
    KB_I("shield_on_cast", s_shieldOnCast, 0, 1),
    KB_I("conf_max", s_confMax, 1, 50), KB_F("conf_every", s_confEvery, 0, 10), KB_I("prep_max", s_prepMax, 0, 20),
    KB_F("chill_hit", s_chillHit, 0, 100), KB_F("chill_shot", s_chillShot, 0, 100), KB_F("chill_burst", s_chillBurst, 0, 100),
    KB_F("chill_s3", s_chillS3, 0, 100), KB_F("field_freeze_seconds", s_fieldFreeze, 0.1, 60), KB_F("burst_field_radius", s_burstFieldRadius, 0, 30),
    KB_F("burst_field_seconds", s_burstFieldSeconds, 0, 60),
    KB_F("frozen_seconds", s_frozenSeconds, 0, 60), KB_F("shatter_mult", s_shatterMult, 1, 50), KB_F("stun_buildup", s_stunBuildup, 0, 1000),
    KB_F("slow_buildup", s_slowBuildup, 0, 1000),
    KB_F("s1_cooldown", s_s1Cd, 0, 600), KB_F("s1_seconds", s_s1Seconds, 1, 120), KB_F("s1_range", s_s1Range, 1, 500),
    KB_F("s1_reach", s_s1Reach, 10, 1000), KB_F("s1_every", s_s1Every, 0.05, 10), KB_I("s1_round", s_s1Round, 1, 100000),
    KB_F("s2_cooldown", s_s2Cd, 0, 600), KB_F("s2_every", s_s2Every, 0.05, 5), KB_F("s2_spread", s_s2Spread, 0, 30),
    KB_F("s2_field_seconds", s_s2FieldSeconds, 0, 120), KB_F("s2_field_radius", s_s2FieldRadius, 1, 50), KB_F("s2_cone", s_s2Cone, 0.5, 45),
    KB_F("s2_range", s_s2Range, 10, 1000), KB_I("s2_explosion", s_s2Explosion, 0, 100000),
    KB_F("s3_cooldown", s_s3Cd, 0, 3600), KB_F("s3_every", s_s3Every, 0.05, 5), KB_F("s3_cone", s_s3Cone, 0.5, 45), KB_F("s3_range", s_s3Range, 10, 1000),
    KB_I("s3_round", s_s3Round, 1, 100000), KB_I("s3_freeze_at", s_s3FreezeAt, 1, 50),
    KB_I("chill_fx", s_chillFx, 0, 1), KB_I("chill_fx_explosion", s_fxExplosion, 1, 100000), KB_F("chill_fx_every", s_fxEvery, 0, 10),
    KB_F("chill_fx_rate", s_fxRate, 0.5, 60), KB_I("ems_shell", s_emsShell, 0, 100000), KB_F("ems_shell_height", s_emsShellHeight, 0.5, 30),
    KB_I("ems_immune", s_emsImmune, 0, 1), KB_I("ally_guard", s_allyGuard, 0, 1),
    KB_I("ems_status_1", s_emsSt[0], 0, 255), KB_I("ems_status_2", s_emsSt[1], 0, 255), KB_I("ems_status_3", s_emsSt[2], 0, 255),
    KB_I("ems_status_4", s_emsSt[3], 0, 255), KB_I("ems_explosion_1", s_emsBoom[0], 0, 100000), KB_I("ems_explosion_2", s_emsBoom[1], 0, 100000),
    KB_I("line_of_sight", s_sight, 0, 1), KB_F("first_person", s_fpDistance, 0.1, 2),
    KB_F("hp_mult", s_hpMult, 0.1, 10), KB_F("stamina_mult", s_staminaMult, 0.1, 20), KB_I("hellbomb_on_exhaustion", s_hellbomb, 0, 1),
    KB_I("hellbomb_explosion", s_hellbombRow, 0, 100000), KB_F("hellbomb_hold", s_hellbombHold, 0, 10),
};

static __attribute__((unused)) void DefaultIni(FILE* f)
{
    fprintf(f, "; Alva %s. Keys: a letter, digit, F1-F24, Mouse3, Mouse4 or Mouse5, with Shift+, Ctrl+ or Alt+ in front for a\n"
               "; combo (Shift+Q). The game still sees the keys too (Shift sprints, R reloads), so pick ones it leaves alone.\n"
               "key_s1 = %s\nkey_s2 = %s\nkey_s3 = %s\n"
               "; Brumal Barrier: each skill she uses (shield_on_cast 1) and every shield_every s (0: skills only) gives every ally\n"
               "; within shield_range m (Helldivers, sentries, emplacements, dogs, exosuits, SEAF, the FRV, the Pelican) a shield\n"
               "; of shield_amount. Shields stack with no cap and take hits before health.\n"
               "shield_amount = %g\nshield_range = %g\nshield_every = %g\nshield_on_cast = %d\n"
               "; Freezing Touch: her hits on enemies give Confectance (at most one per conf_every s, up to conf_max). Each skill\n"
               "; gives Battle Prep: at prep_max the skill with the longest cooldown left is ready again (0: off).\n"
               "conf_max = %d\nconf_every = %g\nprep_max = %d\n"
               "; Chill (out of 100) from her shots, interception shots, EMS bursts, Nix Requiem rounds and the nitrogen field (each\n"
               "; half second); every chill slows (slow_buildup). At 100 an enemy freezes (a stun, stun_buildup) for frozen_seconds.\n"
               "; A frozen enemy takes no damage except from an explosion (not EMS, anyone's): that hit deals shatter_mult times\n"
               "; its damage, once, and the enemy thaws; a shell's direct hit (an orbital's, a recoilless's) joins its blast. Snow fields (Frosted Echo's nitrogen field, and one of burst_field_radius m\n"
               "; for burst_field_seconds where each of its bursts lands) freeze an enemy standing in them in field_freeze_seconds,\n"
               "; divided by how many fields overlap there.\n"
               "chill_hit = %g\nchill_shot = %g\nchill_burst = %g\nchill_s3 = %g\nfield_freeze_seconds = %g\nburst_field_radius = %g\nburst_field_seconds = %g\n"
               "frozen_seconds = %g\nshatter_mult = %g\nstun_buildup = %g\nslow_buildup = %g\n"
               "; chill_fx 1: every chill on an enemy sets off a burst on it (chill_fx_explosion, 150: the EMS mortar's; it stuns too),\n"
               "; at most one per enemy each chill_fx_every s and chill_fx_rate a second in all. Her bursts spare her allies.\n"
               "chill_fx = %d\nchill_fx_explosion = %d\nchill_fx_every = %g\nchill_fx_rate = %g\n"
               "; ems_shell 154 (the EMS mortar's shell): her EMS bursts (chill bursts, Frosted Echo) are that shell fired straight down\n"
               "; from ems_shell_height m above, so they look like the mortar's (an explosion row alone leaves only a crater). 0: the rows.\n"
               "ems_shell = %d\nems_shell_height = %g\n"
               "; ally_guard 1: a status from her own blasts never lands on an ally (gas and fire from anyone still do).\n"
               "; ems_immune 1: no EMS stun on her, from anyone: the statuses ems_status_1..4 (37-39: the stuns), and any other\n"
               "; status that lands on her right after an EMS blast near her (EMS mortar, her bursts, ems_explosion_1..2).\n"
               "ally_guard = %d\nems_immune = %d\nems_status_1 = %d\nems_status_2 = %d\nems_status_3 = %d\nems_status_4 = %d\n"
               "ems_explosion_1 = %d\nems_explosion_2 = %d\n"
               "; Snow Wolf's Heart: cooldown; Covering Mode for s1_seconds: an enemy (within s1_reach m) that hits an ally within\n"
               "; s1_range m of her gets an interception shot (projectile s1_round, 305: the R-63 Diligence's), one per s1_every s.\n"
               "s1_cooldown = %g\ns1_seconds = %g\ns1_range = %g\ns1_reach = %g\ns1_every = %g\ns1_round = %d\n"
               "; Frosted Echo: cooldown; where she aims (the enemy within s2_cone degrees of her crosshair, s2_range m, else where her\n"
               "; aim lands) one EMS burst per Confectance point, s2_every s apart and up to s2_spread m apart (s2_explosion 150: the\n"
               "; EMS mortar's), and a nitrogen field of s2_field_radius m for s2_field_seconds.\n"
               "s2_cooldown = %g\ns2_every = %g\ns2_spread = %g\ns2_field_seconds = %g\ns2_field_radius = %g\ns2_cone = %g\ns2_range = %g\ns2_explosion = %d\n"
               "; Nix Requiem: cooldown; the enemy under her crosshair (s3_cone degrees, s3_range m, in sight) takes one heavy round\n"
               "; (projectile s3_round, 226: the APW-1's) per Confectance point, s3_every s apart; s3_freeze_at points or more\n"
               "; freeze it at once.\n"
               "s3_cooldown = %g\ns3_every = %g\ns3_cone = %g\ns3_range = %g\ns3_round = %d\ns3_freeze_at = %d\n"
               "; Aiming: line_of_sight 1, Frosted Echo's and Nix Requiem's target must be in sight. first_person: the camera this\n"
               "; close to her head (m) counts as first person.\nline_of_sight = %d\nfirst_person = %g\n"
               "; Her body: hp_mult times a Helldiver's health, stamina_mult times her sprint. hellbomb_on_exhaustion 1 sets off a\n"
               "; Hellbomb on her if she runs her stamina dry (a T-Doll's price).\n"
               "hp_mult = 1.5\nstamina_mult = 2\nhellbomb_on_exhaustion = 1\n",
            VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, (double)s_shieldAmount, (double)s_shieldRange, (double)s_shieldEvery,
            s_shieldOnCast, s_confMax, (double)s_confEvery, s_prepMax, (double)s_chillHit, (double)s_chillShot, (double)s_chillBurst,
            (double)s_chillS3, (double)s_fieldFreeze, (double)s_burstFieldRadius, (double)s_burstFieldSeconds, (double)s_frozenSeconds, (double)s_shatterMult, (double)s_stunBuildup, (double)s_slowBuildup, s_chillFx, s_fxExplosion, (double)s_fxEvery, (double)s_fxRate, s_emsShell, (double)s_emsShellHeight,
            s_allyGuard, s_emsImmune, s_emsSt[0], s_emsSt[1], s_emsSt[2], s_emsSt[3], s_emsBoom[0], s_emsBoom[1],
            (double)s_s1Cd, (double)s_s1Seconds, (double)s_s1Range, (double)s_s1Reach, (double)s_s1Every, s_s1Round, (double)s_s2Cd,
            (double)s_s2Every, (double)s_s2Spread, (double)s_s2FieldSeconds, (double)s_s2FieldRadius, (double)s_s2Cone, (double)s_s2Range,
            s_s2Explosion, (double)s_s3Cd, (double)s_s3Every, (double)s_s3Cone, (double)s_s3Range, s_s3Round, s_s3FreezeAt, s_sight,
            (double)s_fpDistance);
}

/* ---- state ---------------------------------------------------------------------------------------------------- */
static double g_cd[3];
static double g_s1Until;
static int g_conf, g_prep;
static double g_confAt = -100, g_pulseAt;
static unsigned g_shatters;
/* 0.1.5: the last shatter hits for the HUD's damage numbers: x, y, z, damage, sequence */
#define SHATTER_RING 32
static float g_shRing[SHATTER_RING][5];
static volatile unsigned g_shSeq;
static double g_shatterBonus;
static unsigned g_casts[3], g_grants, g_intercepts, g_bursts, g_rounds, g_freezes, g_refunds;

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
static int HerHead(float out[3])
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

/* ---- enemies by network id (who hit an ally: the damage event names it by network id) ----------------------------- */
#define NETS 4096
static uint32_t g_netKey[NETS], g_netEnt[NETS];
static void NetNote(uint32_t net, uint32_t e)
{
    if (!NetOk(net)) return;
    unsigned h = (net * 2654435761u) >> 20;
    for (unsigned i = 0; i < NETS; i++)
    {
        unsigned k = (h + i) & (NETS - 1);
        if (!g_netKey[k] || g_netKey[k] == net) { g_netEnt[k] = e; g_netKey[k] = net; return; }
    }
}
static uint32_t NetEntity(uint32_t net)
{
    if (!NetOk(net)) return 0;
    unsigned h = (net * 2654435761u) >> 20;
    for (unsigned i = 0; i < NETS; i++)
    {
        unsigned k = (h + i) & (NETS - 1);
        if (g_netKey[k] == net) return g_netEnt[k];
        if (!g_netKey[k]) return 0;
    }
    return 0;
}
static int FoeIndex(uint32_t e) { for (int k = 0; k < FOES; k++) if (e && g_foe[k] == e) return k; return -1; }

/* ---- Freezing Touch: chill, frozen, shatter ---------------------------------------------------------------------- */
#define COLD 256
typedef struct Cold { uint32_t e; float chill; double frozenUntil, at, fxAt;
                      /* 0.2.1: a shell's direct hit and its blast are one shatter (either order, same owner, 0.25 s) */
                      int shrug; uint32_t shrugOwner, shatterOwner; double shrugAt, shatterAt; } Cold;
static Cold g_cold[COLD];
static Cold* ColdOf(uint32_t e, int make)
{
    Cold* free_ = NULL, *oldest = NULL;
    for (int i = 0; i < COLD; i++)
    {
        if (g_cold[i].e == e) return &g_cold[i];
        if (!g_cold[i].e && !free_) free_ = &g_cold[i];
        if (g_cold[i].e && (!oldest || g_cold[i].at < oldest->at)) oldest = &g_cold[i];
    }
    if (!make) return NULL;
    Cold* c = free_ ? free_ : oldest;
    memset(c, 0, sizeof *c);
    c->e = e;
    return c;
}
static void Own(const float at[3]);
/* 0.1.3: an EMS burst as the EMS mortar's own shell, fired straight down onto the point (its look and sound: a row
 * spawned alone left only a crater); ems_shell 0, or a row other than 150, spawns the row */
static int EmsBurst(uint32_t row, const float at[3])
{
    if (s_emsShell > 0 && row == 150u)
    {
        float from[3] = {at[0], at[1], at[2] + s_emsShellHeight}, down[3] = {0, 0, -1};
        return hf->spawn_projectile(self, (uint32_t)s_emsShell, from, down);
    }
    return hf->spawn_explosion(self, row, at);
}
static unsigned g_fx, g_fxSkipped;
static double g_fxTokens;
/* the snow: a burst on a chilled enemy (her blasts spare allies) */
static void ChillFx(Cold* c, uint32_t e)
{
    int k = FoeIndex(e);
    float p[3];
    if (!s_chillFx || k < 0 || g_now - c->fxAt < s_fxEvery) return;
    if (g_fxTokens < 1) { g_fxSkipped++; return; }
    if (!TargetPos(e, g_foeRes[k], p)) return;
    p[2] -= 0.5f;
    Own(p);
    int r = EmsBurst((uint32_t)s_fxExplosion, p);
    if (r == HF_ACT_WAITING) return;
    if (!Act("a chill burst", r)) return;
    c->fxAt = g_now;
    g_fxTokens -= 1;
    g_fx++;
}
static __attribute__((unused)) int Frozen(uint32_t e) { Cold* c = ColdOf(e, 0); return c && c->frozenUntil > g_now; }
static void Status(uint32_t e, uint32_t status, float buildup, const char* what)
{
    if (buildup <= 0 || !hf->apply_status) return;
    Act(what, hf->apply_status(self, e, status, buildup > 1000 ? 1000 : buildup));
}
static void Freeze(uint32_t e, const char* why)
{
    Cold* c = ColdOf(e, 1);
    c->at = g_now;
    c->chill = 0;
    c->frozenUntil = g_now + s_frozenSeconds;
    Status(e, HF_STATUS_STUN_LARGE, s_stunBuildup, "frozen (stun)");
    g_freezes++;
    static unsigned told;
    if (told++ < 12) Log("Freezing Touch: entity %u frozen for %.1f s (%s)", e, (double)s_frozenSeconds, why);
}
/* the GAME THREAD only (apply_status) */
static void Chill(uint32_t e, float amount, const char* why)
{
    if (!e || amount <= 0 || FoeIndex(e) < 0) return;
    Cold* c = ColdOf(e, 1);
    c->at = g_now;
    Status(e, HF_STATUS_FLAMER_SLOWED, s_slowBuildup, "chill (slow)");
    if (c->frozenUntil > g_now) return;   /* (0.2.0: frozen is still: no bursts on it) */
    c->chill += amount;
    if (c->chill >= 100) Freeze(e, why);
    ChillFx(c, e);
}
/* chill from her hits: noted on the damage event's thread, applied on the game thread */
#define CHILLQ 64
static uint32_t g_chillQ[CHILLQ];
static float g_chillAmt[CHILLQ];
static atomic_uint g_chillIn;
static unsigned g_chillOut;
static void ChillLater(uint32_t e, float amount)
{
    unsigned i = atomic_load(&g_chillIn);
    if (i - g_chillOut >= CHILLQ) return;
    g_chillQ[i % CHILLQ] = e;
    g_chillAmt[i % CHILLQ] = amount;
    atomic_store(&g_chillIn, i + 1);
}
static void ChillFrame(void)
{
    unsigned in = atomic_load(&g_chillIn);
    while (g_chillOut != in) { Chill(g_chillQ[g_chillOut % CHILLQ], g_chillAmt[g_chillOut % CHILLQ], "her hits"); g_chillOut++; }
}

/* ---- Brumal Barrier ------------------------------------------------------------------------------------------------ */
static void Barrier(const char* why)
{
    float me[3];
    if (s_shieldAmount <= 0 || !PlayerPose(me, NULL)) return;
    me[2] += 1.0f;
    int n = AllyGrant(me, s_shieldRange, s_shieldAmount);
    g_grants++;
    double total;
    int shielded = AllyShielded(&total);
    static unsigned told;
    if (told++ < 20) Log("Brumal Barrier (%s): +%.0f shield on %d allies within %.0f m (%d shielded, %.0f in all)", why, (double)s_shieldAmount, n,
                         (double)s_shieldRange, shielded, total);
}
static void PulseFrame(void)
{
    if (s_shieldEvery <= 0 || g_now - g_pulseAt < s_shieldEvery) return;
    g_pulseAt = g_now;
    Barrier("her pulse");
}
static void AllyBroke(uint32_t e)
{
    static unsigned told;
    if (told++ < 10) Log("Brumal Barrier: the shield on entity %u broke", e);
}

/* ---- Confectance and Battle Prep -------------------------------------------------------------------------------- */
static void Confect(int n) { g_conf += n; if (g_conf > s_confMax) g_conf = s_confMax; if (g_conf < 0) g_conf = 0; }
static void Cast(int skill)
{
    g_casts[skill]++;
    if (s_shieldOnCast) Barrier(skill == 0 ? "Snow Wolf's Heart" : skill == 1 ? "Frosted Echo" : "Nix Requiem");
    if (s_prepMax <= 0) return;
    if (++g_prep < s_prepMax) return;
    g_prep = 0;
    int best = -1;
    double left = 0;
    for (int i = 0; i < 3; i++) if (g_cd[i] - g_now > left) { left = g_cd[i] - g_now; best = i; }
    if (best < 0) return;
    g_cd[best] = g_now;
    g_refunds++;
    static const char* kName[3] = {"Snow Wolf's Heart", "Frosted Echo", "Nix Requiem"};
    Log("Battle Prep: %s is ready again (%.0f s early)", kName[best], left);
}

/* ---- shots: a projectile from her head toward a point ---------------------------------------------------------- */
static int Shoot(uint32_t round, const float to[3], const char* what)
{
    float from[3], dir[3];
    if (!HerHead(from)) return 0;
    for (int k = 0; k < 3; k++) dir[k] = to[k] - from[k];
    float n = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (!(n > 0.5f)) return 0;
    for (int k = 0; k < 3; k++) { dir[k] /= n; from[k] += dir[k] * 1.2f; }   /* clear of her own body */
    int r = hf->spawn_projectile(self, round, from, dir);
    if (r == HF_ACT_WAITING) return -1;
    return Act(what, r);
}

/* ---- Snow Wolf's Heart: Covering Mode ------------------------------------------------------------------------------- */
#define THREATS 16
static uint32_t g_threat[THREATS];
static atomic_uint g_threatIn;
static unsigned g_threatOut;
static double g_shotAt = -100;
static void SnowWolf(void)
{
    if (g_now < g_cd[0]) return;
    g_cd[0] = g_now + s_s1Cd;
    g_s1Until = g_now + s_s1Seconds;
    Log("Snow Wolf's Heart: Covering Mode for %.0f s (allies within %.0f m)", (double)s_s1Seconds, (double)s_s1Range);
    Cast(0);
}
/* the damage event's thread: an enemy hit an ally near her */
static uint32_t NearestFoe(const float at[3], float range)
{
    uint32_t best = 0;
    float bd = range * range, q[3];
    for (int k = 0; k < FOES; k++)
    {
        float d2;
        if (!g_foe[k] || !TargetPos(g_foe[k], g_foeRes[k], q) || (d2 = Dist2(q, at)) >= bd) continue;
        bd = d2; best = g_foe[k];
    }
    return best;
}
static unsigned g_threatSeen, g_threatNamed, g_threatNear;
/* the damage event's thread: an enemy hit an ally near her (who: the hit's owner or dealer; else the enemy nearest the
 * ally, picked on the game thread) */
static uint32_t g_threatAt[THREATS];
static void Threat(const HfDamageEvent* d)
{
    if (g_now >= g_s1Until || d->amount == 0) return;
    KbAlly* a = AllyOf(d->target);
    if (!a || MineHit((HfDamageEvent*)d)) return;
    uint32_t who = NetEntity(d->owner);
    if (!who || FoeIndex(who) < 0) who = NetEntity(d->dealer);
    if (who && FoeIndex(who) < 0) who = 0;
    if (g_threatSeen++ < 12) Log("Covering Mode: ally %u hit (kind %u, %d damage, dealer %u, owner %u): %s %u", d->target, d->kind, d->amount, d->dealer,
                                 d->owner, who ? "attacker" : "attacker unknown, the enemy nearest it", who);
    unsigned in = atomic_load(&g_threatIn);
    if (who) for (unsigned i = g_threatOut; i != in; i++) if (g_threat[i % THREATS] == who) return;
    if (in - g_threatOut >= THREATS) return;
    g_threat[in % THREATS] = who;
    g_threatAt[in % THREATS] = d->target;
    atomic_store(&g_threatIn, in + 1);
}
static void CoverFrame(void)
{
    unsigned in = atomic_load(&g_threatIn);
    if (g_now >= g_s1Until) { g_threatOut = in; return; }
    while (g_threatOut != in && g_now - g_shotAt >= s_s1Every)
    {
        uint32_t e = g_threat[g_threatOut % THREATS], ally = g_threatAt[g_threatOut % THREATS];
        g_threatOut++;
        if (!e)
        {
            KbAlly* a = AllyOf(ally);
            float ap[3];
            if (!a || !AllyPos(a, ap) || !(e = NearestFoe(ap, 40))) continue;
            g_threatNear++;
        }
        else g_threatNamed++;
        int k = FoeIndex(e);
        float q[3], me[3];
        if (k < 0 || !Alive(e) || !TargetPos(e, g_foeRes[k], q) || !PlayerPose(me, NULL) || Dist2(q, me) > s_s1Reach * s_s1Reach) continue;
        int r = Shoot((uint32_t)s_s1Round, q, "an interception shot");
        if (r <= 0) continue;
        g_shotAt = g_now;
        g_intercepts++;
        Confect(1);
        Chill(e, s_chillShot, "an interception shot");
        static unsigned told;
        if (told++ < 12) Log("Snow Wolf's Heart: an interception shot at entity %u (%.0f m)", e, sqrtf(Dist2(q, me)));
    }
}

/* ---- Frosted Echo: EMS bursts and the nitrogen field ---------------------------------------------------------- */
static uint32_t EmsRow(void) { return (uint32_t)s_s2Explosion; }
static void Own(const float at[3])
{
    unsigned slot = g_ownNext++ % KB_OWN_SLOTS;
    memcpy(g_ownPos[slot], at, sizeof g_ownPos[slot]);
    atomic_store(&g_ownEmp[slot], 0);
    atomic_store(&g_ownMs[slot], GetTickCount64());
}
/* one of her blasts lately near this point (her bursts spare allies) */
static int OwnNear(const float p[3])
{
    unsigned long long now = GetTickCount64();
    for (int i = 0; i < KB_OWN_SLOTS; i++)
    {
        unsigned long long at = atomic_load(&g_ownMs[i]);
        if (at && now - at <= 1500 && Dist2(p, g_ownPos[i]) <= 16.0f * 16.0f) return 1;
    }
    return 0;
}
static float g_echoAt[3];
static uint32_t g_echoTarget;
static int g_echoLeft;
static double g_echoNext, g_fieldUntil, g_fieldTick;
/* 0.2.0: snow fields: Frosted Echo's nitrogen field and one where each of its bursts lands */
#define FIELDS 24
typedef struct Field { float p[3], r; double until; } Field;
static Field g_fields[FIELDS];
static void AddField(const float p[3], float r, float seconds)
{
    if (r <= 0 || seconds <= 0) return;
    Field* f = &g_fields[0];
    for (int i = 0; i < FIELDS; i++) { if (g_fields[i].until < f->until) f = &g_fields[i]; }
    memcpy(f->p, p, 12);
    f->r = r;
    f->until = g_now + seconds;
    if (f->until > g_fieldUntil) g_fieldUntil = f->until;
}
static void FrostedEcho(void)
{
    if (g_now < g_cd[1]) return;
    float at[3];
    uint32_t target;
    if (!Aim(s_s2Cone, s_s2Range, 1, at, &target)) { static unsigned told; if (told++ < 5) Log("Frosted Echo: her aim is unknown"); return; }
        g_cd[1] = g_now + s_s2Cd;
    int n = g_conf > 0 ? g_conf : 1;
    g_conf = 0;
    memcpy(g_echoAt, at, 12);
    g_echoTarget = target;
    g_echoLeft = n;
    g_echoNext = g_now;
    AddField(at, s_s2FieldRadius, s_s2FieldSeconds);
    g_fieldTick = g_now;
    Log("Frosted Echo: %d EMS bursts on %s, and a nitrogen field for %.0f s", n, target ? "her target" : "where she aims", (double)s_s2FieldSeconds);
    Cast(1);
}
static void EchoFrame(void)
{
    while (g_echoLeft > 0 && g_now >= g_echoNext)
    {
        float p[3];
        int k = g_echoTarget ? FoeIndex(g_echoTarget) : -1;
        if (k >= 0 && TargetPos(g_echoTarget, g_foeRes[k], p)) memcpy(g_echoAt, p, 12);   /* follows her target */
        memcpy(p, g_echoAt, 12);
        float a = (float)rand() / RAND_MAX * 6.2831853f, r = (float)rand() / RAND_MAX * s_s2Spread;
        p[0] += cosf(a) * r; p[1] += sinf(a) * r;
        Own(p);
        int res = EmsBurst(EmsRow(), p);
        if (res == HF_ACT_WAITING) { g_echoNext = g_now + 0.1; return; }
        Act("an EMS burst", res);
        AddField(p, s_burstFieldRadius, s_burstFieldSeconds);
        g_bursts++;
        g_echoLeft--;
        g_echoNext = g_now + s_s2Every;
        for (int j = 0; j < FOES; j++)
        {
            float q[3];
            if (g_foe[j] && TargetPos(g_foe[j], g_foeRes[j], q) && Dist2(q, p) <= s_burstFieldRadius * s_burstFieldRadius) Chill(g_foe[j], s_chillBurst, "an EMS burst");
        }
    }
}
static unsigned g_fieldFreezes;
static void FieldFrame(void)
{
    if (g_now >= g_fieldUntil || g_now < g_fieldTick) return;
    const double tick = 0.25;
    g_fieldTick = g_now + tick;
    for (int j = 0; j < FOES; j++)
    {
        float q[3];
        if (!g_foe[j] || !TargetPos(g_foe[j], g_foeRes[j], q)) continue;
        int n = 0;
        for (int i = 0; i < FIELDS; i++)
        {
            const Field* f = &g_fields[i];
            if (f->until <= g_now) continue;
            float dx = q[0] - f->p[0], dy = q[1] - f->p[1];
            if (dx * dx + dy * dy <= f->r * f->r && fabsf(q[2] - f->p[2]) <= 4) n++;
        }
        /* 100 chill in field_freeze_seconds / n */
        if (n) Chill(g_foe[j], (float)(100.0 * tick * n / s_fieldFreeze) + 0.01f, n > 1 ? "overlapping snow fields" : "a snow field");
    }
}

/* ---- Nix Requiem ---------------------------------------------------------------------------------------------------- */
static uint32_t g_nixTarget;
static int g_nixLeft, g_nixFreeze;
static double g_nixNext, g_nixFreezeAt;
static void NixRequiem(void)
{
    if (g_now < g_cd[2]) return;
    float at[3];
    uint32_t target;
    g_aimBlocked = 0;
    if (Aim(s_s3Cone, s_s3Range, 0, at, &target) != 1)
    {
        static unsigned told;
        if (told++ < 8) Log("Nix Requiem: no enemy under her crosshair (%u near it but out of sight; %d enemies known)", g_aimBlocked, FoeCount());
        return;
    }
    g_cd[2] = g_now + s_s3Cd;
    int points = g_conf;
    g_conf = 0;
    g_nixTarget = target;
    g_nixLeft = points > 0 ? points : 1;
    g_nixNext = g_now;
    Log("Nix Requiem: %d heavy rounds on entity %u (%d Confectance)%s", g_nixLeft, target, points, points >= s_s3FreezeAt ? ": frozen after the last round" : "");
    g_nixFreeze = points >= s_s3FreezeAt;   /* 0.2.0: after its rounds (a frozen enemy would shrug them off) */
    Cast(2);
}
static void NixFrame(void)
{
    while (g_nixLeft > 0 && g_now >= g_nixNext)
    {
        int k = FoeIndex(g_nixTarget);
        float q[3];
        if (k < 0 || !Alive(g_nixTarget) || !TargetPos(g_nixTarget, g_foeRes[k], q)) { g_nixLeft = 0; return; }
        int r = Shoot((uint32_t)s_s3Round, q, "a Nix Requiem round");
        if (r < 0) { g_nixNext = g_now + 0.1; return; }
        g_nixLeft--;
        g_nixNext = g_now + s_s3Every;
        if (r > 0) { g_rounds++; if (g_nixLeft > 0 || !g_nixFreeze) Chill(g_nixTarget, s_chillS3, "Nix Requiem"); }
        if (g_nixLeft == 0 && g_nixFreeze) { g_nixFreeze = 0; g_nixFreezeAt = g_now + 0.35; }
    }
    if (g_nixFreezeAt > 0 && g_now >= g_nixFreezeAt)
    {
        g_nixFreezeAt = 0;
        if (FoeIndex(g_nixTarget) >= 0 && Alive(g_nixTarget)) Freeze(g_nixTarget, "Nix Requiem");
    }
}

/* ---- status guard: the game's status build-up (game.dll+0x699E40, every status on every entity; docs/research/
 * game-notes.md) refuses her own blasts' statuses on her allies, and EMS on her ------------------------------------- */
typedef void (*AddStatusFn)(void*, uint32_t, uint32_t, uint32_t, float, uint32_t, uint64_t);
static AddStatusFn original_add;
static atomic_int g_statusHooked;
static unsigned g_refusedAlly, g_refusedHer, g_learned;
static uint8_t g_emsLearned[256];
/* explosions lately (any): where, when, what row */
#define BOOMS 32
typedef struct Boom { float p[3]; uint32_t type; unsigned long long ms; } Boom;
static Boom g_boom[BOOMS];
static atomic_uint g_boomNext;
static int EmsBlast(uint32_t t)
{
    return t && (t == 150u || t == (uint32_t)s_fxExplosion || t == (uint32_t)s_s2Explosion || t == (uint32_t)s_emsBoom[0] || t == (uint32_t)s_emsBoom[1]);
}
static void OnExplosion(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfExplosionEvent* x = (const HfExplosionEvent*)info;
    unsigned i = atomic_fetch_add(&g_boomNext, 1) % BOOMS;
    g_boom[i].ms = 0;
    memcpy(g_boom[i].p, x->position, 12);
    g_boom[i].type = x->type;
    g_boom[i].ms = GetTickCount64();
}
/* the latest explosion within r m of p in the last ms (ems: EMS ones only): its row, 0 none */
static uint32_t BoomNear(const float p[3], float r, unsigned long long ms, int ems)
{
    unsigned long long now = GetTickCount64(), best = 0;
    uint32_t type = 0;
    for (int i = 0; i < BOOMS; i++)
    {
        unsigned long long at = g_boom[i].ms;
        if (!at || now - at > ms || at < best || Dist2(p, g_boom[i].p) > r * r || (ems && !EmsBlast(g_boom[i].type))) continue;
        best = at; type = g_boom[i].type;
    }
    return type;
}
static int OwnWithin(const float p[3], unsigned long long ms, float r)
{
    unsigned long long now = GetTickCount64();
    for (int i = 0; i < KB_OWN_SLOTS; i++)
    {
        unsigned long long at = atomic_load(&g_ownMs[i]);
        if (at && now - at <= ms && Dist2(p, g_ownPos[i]) <= r * r) return 1;
    }
    return 0;
}
/* gas and fire: friendly fire that still lands on allies */
static int PassStatus(uint32_t st) { return st == 5 || st == 6 || st == 32 || (st >= 42 && st <= 45); }
static int EmsStatus(uint32_t st)
{
    for (int i = 0; i < 4; i++) if (s_emsSt[i] && st == (uint32_t)s_emsSt[i]) return 1;
    return st < 256 && g_emsLearned[st];
}
static int Refuse(uint32_t e, uint32_t st, float buildup, uint32_t from)
{
    uint32_t me = atomic_load(&g_avatar);
    if (!g_inMission || !kb_kit_on || !e) return 0;
    if (e == me)
    {
        float p[3];
        int have = PlayerPose(p, NULL);
        uint32_t nearBoom = have ? BoomNear(p, 20, 700, 0) : 0;
        static unsigned told;
        if (told++ < 40) Log("status %u on her (build-up %.1f, from %u; the latest explosion near her: %u)", st, (double)buildup, from, nearBoom);
        if (!s_emsImmune) return 0;
        if (EmsStatus(st)) { g_refusedHer++; return 1; }
        if (have && !PassStatus(st) && BoomNear(p, 20, 700, 1))
        {
            if (st < 256 && !g_emsLearned[st]) { g_emsLearned[st] = 1; g_learned++; Log("EMS immunity: status %u came with an EMS blast: refused on her from now on", st); }
            g_refusedHer++;
            return 1;
        }
        return 0;
    }
    if (!s_allyGuard || PassStatus(st)) return 0;
    KbAlly* a = AllyOf(e);
    float p[3];
    if (!a || !AllyPos(a, p) || !OwnWithin(p, 1200, 16)) return 0;
    if (g_refusedAlly++ < 15) Log("her blast's status %u refused on an ally (entity %u, build-up %.1f)", st, e, (double)buildup);
    return 1;
}
static void AddStatus(void* m, uint32_t e, uint32_t st, uint32_t r9, float buildup, uint32_t from, uint64_t z)
{
    if (Refuse(e, st, buildup, from)) return;
    original_add(m, e, st, r9, buildup, from, z);
}
static void StatusInstall(void)
{
    if (!hf->code_matches(game + 0x699E40, "40 55 53 57 41 55 41 57 48 8d 6c 24 d0 48 81 ec 30 01 00 00"))
    { Log("status guard off: game.dll+0x699E40 is not the reviewed code (allies feel her EMS; no EMS immunity)"); return; }
    if (hf->hook(self, game + 0x699E40, (void*)AddStatus, (void**)&original_add)) { Log("status guard off: the hook could not be placed"); return; }
    atomic_store(&g_statusHooked, 1);
    Log("status guard ready: her blasts' statuses spare allies; EMS immunity on her (statuses %d %d %d %d)", s_emsSt[0], s_emsSt[1], s_emsSt[2], s_emsSt[3]);
}

/* ---- the damage event ------------------------------------------------------------------------------------------- */
static unsigned g_spared, g_shrugged;
/* one of her EMS bursts here just now (3 m, 1 s): its blast neither hurts a frozen enemy nor shatters it */
static int OwnBurstAt(const float p[3])
{
    unsigned long long now = GetTickCount64();
    for (int i = 0; i < KB_OWN_SLOTS; i++)
    {
        unsigned long long at = atomic_load(&g_ownMs[i]);
        if (at && now - at <= 1000 && Dist2(p, g_ownPos[i]) <= 3.0f * 3.0f) return 1;
    }
    return 0;
}
static void ShatterNumber(HfDamageEvent* d, int before, const char* what)
{
    ScaleDamage(d, s_shatterMult);
    g_shatters++;
    g_shatterBonus += d->amount - before;
    unsigned q = g_shSeq + 1;
    float* r = g_shRing[q % SHATTER_RING];
    r[0] = d->position[0]; r[1] = d->position[1]; r[2] = d->position[2]; r[3] = (float)d->amount; r[4] = (float)q;
    g_shSeq = q;
    static unsigned told;
    if (told++ < 20) Log("SHATTER: entity %u, %s (owner %u, kind %u) %d -> %d", d->target, what, d->owner, d->kind, before, d->amount);
}
static void Shatter(HfDamageEvent* d, Cold* c)
{
    /* the same shell's direct hit, shrugged off just before, joins the blast */
    if (c->shrug > 0 && g_now - c->shrugAt <= 0.25 && c->shrugOwner == d->owner) { d->amount += c->shrug; ShatterNumber(d, d->amount, "an explosion with its shell's direct hit"); }
    else ShatterNumber(d, d->amount, "an explosion");
    c->shrug = 0;
    c->frozenUntil = 0;
    c->chill = 0;
    c->shatterAt = g_now;
    c->shatterOwner = d->owner;
}
static void DamageSeen(HfDamagePreEvent* e, HfDamageEvent* d)
{
    /* her EMS bursts spare her allies */
    if (d->kind == 4 && AllyOf(d->target) && d->target != atomic_load(&g_avatar) && OwnNear(d->position))
    {
        e->cancel = 1;
        if (g_spared++ < 10) Log("her burst spared an ally (entity %u, %d damage)", d->target, d->amount);
        return;
    }
    /* 0.2.0: frozen: no damage but an explosion's (not her EMS bursts'), which shatters it once and thaws it */
    if (d->amount > 0 && FoeIndex(d->target) >= 0)
    {
        Cold* c = ColdOf(d->target, 0);
        /* EMS (anyone's: her bursts, an EMS mortar, an EMS strike), its shell's direct hit and its blast alike, neither
         * hurts a frozen enemy nor shatters it: EMS is what freezes */
        int ems = OwnBurstAt(d->position) || BoomNear(d->position, 8, 600, 1);
        if (c && c->frozenUntil > g_now)
        {
            if (d->kind == 4 && !ems) Shatter(d, c);
            else if (ems) { e->cancel = 1; g_shrugged++; return; }
            else
            {
                /* shrugged off, but kept a moment: the blast of the same shell may follow */
                if (g_now - c->shrugAt > 0.25 || c->shrugOwner != d->owner) c->shrug = 0;
                c->shrug += d->amount;
                c->shrugAt = g_now;
                c->shrugOwner = d->owner;
                e->cancel = 1;
                g_shrugged++;
                return;
            }
        }
        else if (c && c->shatterAt > 0 && g_now - c->shatterAt <= 0.25 && d->owner == c->shatterOwner && d->kind != 4 && !ems)
        {
            /* the direct hit of the shell that just shattered it (its blast came first) */
            c->shatterAt = 0;
            ShatterNumber(d, d->amount, "its shell's direct hit, after the blast");
        }
    }
    Threat(d);
    AllyAbsorb(e, d);
}
static void DamageDealt(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    (void)e; (void)me;
    if (d->amount <= 0 || !MineHit(d) || FoeIndex(d->target) < 0) return;
    if (IsShot(d))
    {
        NoteTarget(d);
        ChillLater(d->target, s_chillHit);
    }
    if (g_now - g_confAt >= s_confEvery) { g_confAt = g_now; Confect(1); }
}

/* ---- events ------------------------------------------------------------------------------------------------------ */
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    if (s->resource != AVATAR)
    {
        if (FoeKind(s->resource)) { AddFoe(s->entity, s->resource); NetNote(s->network, s->entity); }
        else AllySpawn(s->entity, s->resource);
        return;
    }
    AllySpawn(s->entity, s->resource);
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
    AllyKill(k->victim);
    Cold* c = ColdOf(k->victim, 0);
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
        g_pulseAt = g_now;
        LearnNet();
    }
    else { g_alive = 0; g_s1Until = 0; }
}
static void Reset(const char* why)
{
    int start = !strcmp(why, "mission start");
    if (start) KitDecide();
    if (g_casts[0] || g_casts[1] || g_casts[2])
        Log("%s: casts %u / %u / %u, %u barriers, %u hits on allies shielded (%.0f damage kept off, %u shields broke), %u interceptions, "
            "%u bursts, %u heavy rounds, %u freezes, %u Battle Prep refunds, %u chill bursts (%u held back), attackers %u named / %u nearest, "
            "statuses refused %u on allies / %u on her (%u learned)",
            why, g_casts[0], g_casts[1], g_casts[2], g_grants, g_allyHits, g_allyAbsorbed, g_allyBroke, g_intercepts, g_bursts, g_rounds, g_freezes, g_refunds, g_fx, g_fxSkipped, g_threatNamed, g_threatNear, g_refusedAlly, g_refusedHer, g_learned);
    if (g_shatters || g_shrugged || g_fieldFreezes)
        Log("%s: %u shatters (+%.0f damage in all, x%.1f), %u hits on frozen enemies shrugged off", why, g_shatters, g_shatterBonus, (double)s_shatterMult, g_shrugged);
    g_shatters = 0;
    g_shrugged = 0;
    memset(g_fields, 0, sizeof g_fields);
    g_nixFreeze = 0;
    g_nixFreezeAt = 0;
    g_shatterBonus = 0;
    Body(start);
    if (start && s_hellbomb && s_hellbombRow == 125 && !hf->load_package(self, UINT64_C(0x346D785D6CA13FF4))) Log("the Hellbomb's package could not be loaded ahead");
    if (!start) AllyClear();   /* (allies spawn before the mission's first frame: kept at its start) */
    g_boomPending = 0;
    g_boomArmed = 0;
    memset(g_cd, 0, sizeof g_cd);
    memset(g_casts, 0, sizeof g_casts);
    memset(g_cold, 0, sizeof g_cold);
    g_grants = g_intercepts = g_bursts = g_rounds = g_freezes = g_refunds = g_spared = 0;
    g_fx = g_fxSkipped = g_threatNamed = g_threatNear = g_threatSeen = 0;
    g_refusedAlly = g_refusedHer = 0;
    g_s1Until = 0;
    g_conf = g_prep = 0;
    g_echoLeft = g_nixLeft = 0;
    g_fieldUntil = 0;
    g_pulseAt = g_now;
    g_threatOut = atomic_load(&g_threatIn);
    g_chillOut = atomic_load(&g_chillIn);
    g_lastTarget = 0;
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

/* ---- for the HUD: shielded allies' markers, her view, her state -------------------------------------------------- */
typedef struct { float x, y, z, pool, who; } AllySpot;
static AllySpot g_spots[32];
static volatile int g_nspots;
static float g_viewOut[6];
static volatile int g_viewOk;
static void SpotsFrame(void)
{
    int n = 0;
    uint32_t me = atomic_load(&g_avatar);
    for (int i = 0; i < KB_ALLIES && n < 32; i++)
    {
        KbAlly* a = &g_ally[i];
        float p[3];
        if (!a->e || a->pool < 1 || !AllyPos(a, p)) continue;
        g_spots[n].x = p[0]; g_spots[n].y = p[1]; g_spots[n].z = p[2];
        g_spots[n].pool = (float)a->pool;
        g_spots[n].who = a->e == me ? 2.0f : a->res == AVATAR ? 1.0f : 0.0f;
        n++;
    }
    g_nspots = n;
    float c[3], f[3];
    int fp;
    if (!View(c, f, &fp)) { g_viewOk = 0; return; }
    memcpy(g_viewOut, c, 12); memcpy(g_viewOut + 3, f, 12);
    g_viewOk = 1;
}
typedef struct AlvaHud
{
    uint32_t version, in_mission;
    float cd[3], cd_max[3];            /* seconds left, full cooldowns: Snow Wolf's Heart, Frosted Echo, Nix Requiem */
    uint32_t s1_on;                    /* Covering Mode */
    float s1_left;
    uint32_t conf, conf_max, prep, prep_max, shielded;
    float shield_total, own_shield, field_left;
    uint32_t keys[3];
    char key_label[3][24];
} AlvaHud;
static AlvaHud g_hud = {1};
static double g_ownPool, g_total;
static int g_shielded;
static void HudFrame(void)
{
    g_shielded = AllyShielded(&g_total);
    KbAlly* a = AllyOf(atomic_load(&g_avatar));
    g_ownPool = a ? a->pool : 0;
}
/* shielded allies, 5 floats each (x, y, z of the chest, shield left, 0 friend / 1 Helldiver / 2 her): how many */
HF_MOD_EXPORT int alva_marks(unsigned version, float* out, int max)
{
    if (version != 1 || !out || max <= 0 || !g_inMission || !kb_kit_on) return 0;
    int n = g_nspots;
    if (n > max) n = max;
    for (int i = 0; i < n; i++)
    {
        out[i * 5] = g_spots[i].x; out[i * 5 + 1] = g_spots[i].y; out[i * 5 + 2] = g_spots[i].z;
        out[i * 5 + 3] = g_spots[i].pool; out[i * 5 + 4] = g_spots[i].who;
    }
    return n;
}
/* 0.1.5: the shatter hits since `after` (a sequence number), 5 floats each (x, y, z, damage, sequence); how many */
HF_MOD_EXPORT int alva_shatters(unsigned version, unsigned after, float* out, int max)
{
    if (version != 1 || !out || max <= 0 || !g_inMission) return 0;
    unsigned last = g_shSeq, first = after + 1;
    if (last - after > SHATTER_RING) first = last - SHATTER_RING + 1;
    int n = 0;
    for (unsigned q = first; q != last + 1 && n < max; q++)
    {
        const float* r = g_shRing[q % SHATTER_RING];
        if ((unsigned)r[4] != q) continue;
        memcpy(out + 5 * n, r, 20);
        n++;
    }
    return n;
}
HF_MOD_EXPORT int alva_view(unsigned version, float* out)
{
    if (version != 1 || !out || !g_viewOk || !g_inMission) return 0;
    memcpy(out, g_viewOut, 24);
    return 1;
}
HF_MOD_EXPORT AlvaHud* alva_hud_state(void)
{
    g_hud.in_mission = (uint32_t)(g_inMission && g_alive && kb_kit_on);
    float m[3] = {s_s1Cd, s_s2Cd, s_s3Cd};
    for (int i = 0; i < 3; i++) { g_hud.cd[i] = (float)fmax(0, g_cd[i] - g_now); g_hud.cd_max[i] = m[i]; }
    g_hud.s1_on = (uint32_t)(g_now < g_s1Until);
    g_hud.s1_left = (float)fmax(0, g_s1Until - g_now);
    g_hud.conf = (uint32_t)g_conf;
    g_hud.conf_max = (uint32_t)s_confMax;
    g_hud.prep = (uint32_t)g_prep;
    g_hud.prep_max = (uint32_t)s_prepMax;
    g_hud.shielded = (uint32_t)g_shielded;
    g_hud.shield_total = (float)g_total;
    g_hud.own_shield = (float)g_ownPool;
    g_hud.field_left = (float)fmax(0, g_fieldUntil - g_now);
    for (int i = 0; i < 3; i++)
    {
        g_hud.keys[i] = (uint32_t)s_keys[i].vk;
        memcpy(g_hud.key_label[i], s_keys[i].label, sizeof g_hud.key_label[i]);
    }
    return &g_hud;
}
static atomic_int g_bindPress;
HF_MOD_EXPORT int alva_input(unsigned version, int skill)
{
    if (version != 1) return 0;
    if (skill >= 1 && skill <= 3) atomic_fetch_or(&g_bindPress, 1 << (skill - 1));
    return 1;
}
HF_MOD_EXPORT int alva_key(unsigned version, int skill, int* vk, int* mods)
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
            if (i == 0) SnowWolf();
            else if (i == 1) FrostedEcho();
            else NixRequiem();
        }
        prev[i] = down;
    }
    g_fxTokens += step * s_fxRate;
    if (g_fxTokens > s_fxRate) g_fxTokens = s_fxRate;
    ChillFrame();
    if (g_alive)
    {
        PulseFrame();
        CoverFrame();
        EchoFrame();
        NixFrame();
    }
    FieldFrame();
    SpotsFrame();
    HudFrame();
    Exhaustion();
    atomic_exchange(&g_ownStopped, 0);
    atomic_exchange(&g_kdStopped, 0);
}

/* ---- Mod Options Menu ------------------------------------------------------------------------------------------- */
static const KbOption kOptions[] = {
    {"shield_amount", &s_shieldAmount, NULL}, {"shield_range", &s_shieldRange, NULL}, {"shield_every", &s_shieldEvery, NULL},
    {"conf_max", NULL, &s_confMax}, {"shatter_mult", &s_shatterMult, NULL}, {"field_freeze_seconds", &s_fieldFreeze, NULL},
    {"burst_field_radius", &s_burstFieldRadius, NULL}, {"burst_field_seconds", &s_burstFieldSeconds, NULL}, {"frozen_seconds", &s_frozenSeconds, NULL},
    {"s1_cooldown", &s_s1Cd, NULL}, {"s1_seconds", &s_s1Seconds, NULL}, {"s2_cooldown", &s_s2Cd, NULL}, {"s2_field_seconds", &s_s2FieldSeconds, NULL},
    {"s3_cooldown", &s_s3Cd, NULL}, {"s3_freeze_at", NULL, &s_s3FreezeAt}, {"hp_mult", &s_hpMult, NULL}, {"stamina_mult", &s_staminaMult, NULL},
};
static void OptionAfter(const char* name, int changed) { (void)name; (void)changed; }
HF_MOD_EXPORT int alva_option_get(const char* name, double* out) { return OptionGet(name, out); }
HF_MOD_EXPORT int alva_option_set(const char* name, double value) { return OptionSet(name, value); }

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    RosterJoin("alva", "Alva");
    kb_ini = "alva.ini";
    kb_key_names[0] = "key_s1";
    kb_key_names[1] = "key_s2";
    kb_key_names[2] = "key_s3";
    KB_SETTINGS(kSettings);
    KB_OPTIONS(kOptions);
    kb_option_after = OptionAfter;
    kb_damage_seen = DamageSeen;
    kb_damage_dealt = DamageDealt;
    kb_ally_broke = AllyBroke;
    if (!api || api->version < 13 || api->size < offsetof(HfApi, code_matches) + sizeof api->code_matches) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    LoadSettings();
    api->log(mod, "Alva %s: keys %s / %s / %s (Snow Wolf's Heart, Frosted Echo, Nix Requiem); shields %.0f on allies within %.0f m", VERSION,
             s_keys[0].label, s_keys[1].label, s_keys[2].label, (double)s_shieldAmount, (double)s_shieldRange);
    if (s_badKey) api->log(mod, "alva.ini: a key could not be read (bits %d: s1, s2, s3); its default stays", s_badKey);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_EXPLOSION, OnExplosion, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) &&
             api->on_game_thread(mod, Tick, NULL);
    if (!ok) api->log(mod, "events or the game thread are unavailable: Alva stays off");
    return 0;
}
