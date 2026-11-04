/* Lemuen (yui.lemuen): Lemuen's kit from Arknights as abilities on keys, for your own Helldiver (solo or host): a
 * long-range sniper, built on the kit base (kitbase/, docs/kit-base.md).
 *
 *   Persecution (passive): a heavily armoured or tough enemy (armour mark_armour+, or mark_health+ health) that she
 *     keeps in her sights (within mark_cone degrees of her view, mark_range m) for mark_seconds is marked for
 *     mark_last s (0, the default: until a newer mark replaces it or it dies). Marked enemies take mark_mult times the damage from her.
 *   Modus Extraditio (passive): modus_after s after she lands, her shots deal modus_mult times their damage.
 *   Salvē Etiam (key_s1, default Z): with a sniper or marksman rifle in hand, her shots deal s1_mult times their damage
 *     until she reloads.
 *   Honesta Missio (key_s2, default X): until she reloads, the enemy nearest the middle of her scope (first person,
 *     within s2_cone degrees, s2_range m) builds up: her next shot on it deals s2_min rising to s2_max times its damage
 *     over s2_hold s of holding it. Changing target starts again; each shot spends the build-up.
 *   Memento Mori (key_s3, default C): an Orbital 120mm HE Barrage shell (projectile s3_shell) falls on every enemy she
 *     has marked, s3_every s apart, at most s3_max, wherever they are.
 *
 * Her reloads are seen in the magazine the HUD reads (hd2.local_player():ammo()) and passes in (lemuen_ammo). Her
 * view is the camera's pose (her unit's world +0x230920, as Apache - Rise Above and Leva's aim_camera read it); the
 * camera at her head means she is in first person.
 *
 * Research credit: the kit base's (Helldiver Survival Rework by enochlophobianumerophobia, MIT; HD2Runtime by
 * SkyeShade); the camera pose as Apache - Rise Above (RSNLRiggs, after Khi) reads it; the Orbital 120mm shell's
 * projectile type from HD2Runtime's attack catalogue. See THIRD_PARTY_NOTICES.md. */
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

#define VERSION "0.1.6"
#define BUILD "25480438"

#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"
#include "kitbase/kb_body.h"
#include "kitbase/kb_hits.h"
#include "kitbase/kb_damage.h"
#include "kitbase/kb_settings.h"
#include "kitbase/kb_roster.h"

/* with other character kits installed: the HUD's roster and picker calls (kitbase/kb_roster.h) */
KB_ROSTER_EXPORTS(lemuen)

/* ---- settings ------------------------------------------------------------------------------------------------- */
/* 0.1.4: a damage number only for a hit the on-hit multipliers (mark, Honesta Missio; 0.1.5: not Modus) raised to numbers_at times
 * or more, as Arknights shows its red number when the final damage is 50% over the damage from her visible ATK and the
 * target's DEF (arknights.wiki.gg/wiki/Damage): Salve Etiam stands for an ATK buff and Memento Mori's shells for her
 * ATK, so neither shows one, as her S1 and S3 show none in Arknights. 0 shows every hit */
static float s_numbersAt = 1.5f;
static int s_markMax = 5;   /* 0.1.2: at most this many marked at once; a new mark replaces the oldest (Yui) */
static float s_markRange = 600, s_markCone = 20, s_markSeconds = 6, s_markLast = 0, s_markMult = 1.18f;
static int s_markArmour = 3, s_markHealth = 750;
static float s_modusAfter = 20, s_modusMult = 1.10f;
static float s_s1Cd = 15, s_s1Mult = 3.1f;
static int s_s1AnyGun = 0;
static int s_sight = 1;   /* 0.1.6: marks and Honesta Missio's lock need a clear view (0: through walls, as 0.1.5) */
static float s_s2Cd = 20, s_s2Min = 1.8f, s_s2Max = 4.25f, s_s2Hold = 3.5f, s_s2Cone = 4, s_s2Range = 600, s_fpDistance = 0.45f;
static float s_s3Cd = 90, s_s3Every = 0.35f, s_s3Height = 120;
static int s_s3Max = 8, s_s3Shell = 194;

static const KbSetting kSettings[] = {
    KB_F("mark_range", s_markRange, 10, 1000), KB_F("mark_cone", s_markCone, 1, 90), KB_F("mark_seconds", s_markSeconds, 0, 30),
    KB_F("mark_last", s_markLast, 0, 600), KB_F("mark_mult", s_markMult, 1, 5), KB_I("mark_armour", s_markArmour, 0, 10), KB_I("mark_max", s_markMax, 1, 32), KB_F("numbers_at", s_numbersAt, 0, 20),
    KB_I("mark_health", s_markHealth, 0, 100000),
    KB_F("modus_after", s_modusAfter, 0, 600), KB_F("modus_mult", s_modusMult, 1, 3),
    KB_F("s1_cooldown", s_s1Cd, 0, 600), KB_F("s1_mult", s_s1Mult, 1, 20), KB_I("s1_any_gun", s_s1AnyGun, 0, 1),
    KB_F("s2_cooldown", s_s2Cd, 0, 600), KB_F("s2_min", s_s2Min, 1, 20), KB_F("s2_max", s_s2Max, 1, 20), KB_F("s2_hold", s_s2Hold, 0.1, 30),
    KB_F("s2_cone", s_s2Cone, 0.5, 30), KB_I("line_of_sight", s_sight, 0, 1), KB_F("s2_range", s_s2Range, 10, 1000), KB_F("first_person", s_fpDistance, 0.1, 2),
    KB_F("s3_cooldown", s_s3Cd, 0, 3600), KB_F("s3_every", s_s3Every, 0.05, 5), KB_F("s3_height", s_s3Height, 20, 500),
    KB_I("s3_max", s_s3Max, 1, 32), KB_I("s3_shell", s_s3Shell, 1, 100000),
    KB_F("hp_mult", s_hpMult, 0.1, 10), KB_F("stamina_mult", s_staminaMult, 0.1, 20), KB_I("hellbomb_on_exhaustion", s_hellbomb, 0, 1),
    KB_I("hellbomb_explosion", s_hellbombRow, 0, 100000), KB_F("hellbomb_hold", s_hellbombHold, 0, 10),
};

/* The default lemuen.ini: tools/inigen.py writes the file from this text (the mod never writes it). Her body's values
 * are written as numbers: the kit base's defaults are the T-Dolls', and hers are set in hellforge_mod_init. */
static __attribute__((unused)) void DefaultIni(FILE* f)
{
    fprintf(f, "; Lemuen %s. Keys: a letter, digit, F1-F24, Mouse3, Mouse4 or Mouse5, with Shift+, Ctrl+ or Alt+ in front for a\n"
               "; combo (Shift+Q). The game still sees the keys too (Shift sprints, R reloads), so pick ones it leaves alone.\n"
               "key_s1 = %s\nkey_s2 = %s\nkey_s3 = %s\n"
               "; Persecution: an enemy with armour mark_armour+ or mark_health+ health she keeps within mark_cone degrees of her\n"
               "; view (mark_range m) for mark_seconds is marked for mark_last s (0: until replaced or killed); marked enemies\n"
               "; take mark_mult times the damage from her. At most mark_max are marked at once: a new mark replaces the oldest.\n"
               "mark_range = %g\nmark_cone = %g\nmark_seconds = %g\nmark_last = %g\nmark_mult = %g\nmark_armour = %d\nmark_health = %d\nmark_max = %d\n"
               "; Modus Extraditio: modus_after s after she lands, her shots deal modus_mult times their damage.\n"
               "modus_after = %g\nmodus_mult = %g\n"
               "; Salve Etiam: cooldown; with a sniper or marksman rifle in hand (any gun: s1_any_gun 1) her shots deal s1_mult\n"
               "; times their damage until she reloads.\ns1_cooldown = %g\ns1_mult = %g\ns1_any_gun = %d\n"
               "; Honesta Missio: cooldown; until she reloads, the enemy nearest the middle of her scope (first person; within\n"
               "; s2_cone degrees and s2_range m) builds up: her next shot on it deals s2_min rising to s2_max times its damage over\n"
               "; s2_hold s. first_person: the camera this close to her head (m) counts as scoped.\n"
               "s2_cooldown = %g\ns2_min = %g\ns2_max = %g\ns2_hold = %g\ns2_cone = %g\ns2_range = %g\nfirst_person = %g\n"
               "; Memento Mori: cooldown; an Orbital 120mm HE Barrage shell (s3_shell, 194) falls from s3_height m on every enemy\n"
               "; she has marked, s3_every s apart, at most s3_max.\n"
               "s3_cooldown = %g\ns3_every = %g\ns3_height = %g\ns3_max = %d\ns3_shell = %d\n"
               "; Her body: hp_mult times a Helldiver's health, stamina_mult times her sprint. hellbomb_on_exhaustion 1 sets off a\n"
               "; Hellbomb on her if she runs her stamina dry (a T-Doll's price; off for her).\n"
               "hp_mult = 1.1\nstamina_mult = 1\nhellbomb_on_exhaustion = 0\n"
               "; Damage numbers (the HUD): shown for a hit her on-hit multipliers (mark, Honesta Missio) raised numbers_at times or\n"
               "; more, as Arknights' red numbers (Modus and Salve Etiam are ATK buffs, the shells her ATK: no number). 0: every hit.\n"
               "numbers_at = %g\n"
               "; Line of sight: 1, marks and Honesta Missio's lock only on enemies she can see (no walls or terrain between);\n"
               "; 0 through walls (0.1.5's way).\nline_of_sight = %d\n",
            VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, (double)s_markRange, (double)s_markCone, (double)s_markSeconds,
            (double)s_markLast, (double)s_markMult, s_markArmour, s_markHealth, s_markMax, (double)s_modusAfter, (double)s_modusMult, (double)s_s1Cd,
            (double)s_s1Mult, s_s1AnyGun, (double)s_s2Cd, (double)s_s2Min, (double)s_s2Max, (double)s_s2Hold, (double)s_s2Cone,
            (double)s_s2Range, (double)s_fpDistance, (double)s_s3Cd, (double)s_s3Every, (double)s_s3Height, s_s3Max, s_s3Shell, (double)s_numbersAt, s_sight);
}

/* ---- state ---------------------------------------------------------------------------------------------------- */
static double g_cd[3];
static int g_s1On, g_s2On;
static double g_landedAt = -1;
static unsigned g_casts[3], g_marksMade, g_empowered, g_held, g_shells;

/* ---- reads: health, armour, positions, her view ----------------------------------------------------------------- */
static int HealthOf(uint32_t entity, int32_t* health, uint32_t* life)
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
static int ArmourOf(uint64_t res)
{
    static uint64_t keys[128];
    static int8_t vals[128];
    unsigned h = (unsigned)(res ^ (res >> 29)) & 127;
    for (unsigned i = 0; i < 128; i++)
    {
        unsigned k = (h + i) & 127;
        if (keys[k] == res) return vals[k];
        if (keys[k]) continue;
        HfField f = HF_FIELD(Health, armor_118);
        HfLiveValue v;
        memset(&v, 0, sizeof v);
        int a = hf->get_live(self, res, &f, &v) ? (int)v.base.number : 0;
        vals[k] = (int8_t)(a < 0 ? 0 : a > 10 ? 10 : a);
        keys[k] = res;
        return vals[k];
    }
    return 0;
}
static int TargetPos(uint32_t e, uint64_t res, float out[3])
{
    uint8_t d[24];
    if (!Descriptor(e, d) || Quad(d) != res || !UnitNode(Word(d + 12), 0, out)) return 0;
    out[2] += 1.0f;
    return 1;
}
/* her camera: where it is and where it looks (rows: right, forward, up, position); fp: it sits at her head */
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
/* the angle (degrees) between her view and a point */
static float Off(const float cam[3], const float fwd[3], const float p[3], float* dist)
{
    float v[3] = {p[0] - cam[0], p[1] - cam[1], p[2] - cam[2]};
    float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    *dist = n;
    if (!(n > 0.5f)) return 180;
    float c = (v[0] * fwd[0] + v[1] * fwd[1] + v[2] * fwd[2]) / n;
    return acosf(c < -1 ? -1 : c > 1 ? 1 : c) * 57.29578f;
}


/* ---- 0.1.6: line of sight. 0.1.5 marked and locked through walls and terrain (its pick was a cone only). The game's
 * raycast, as Voymastina's teleport calls it (Helldiver Survival Rework's cover check), from her camera to the target's
 * chest: blocked by anything but leaves, bodies (enemies, corpses), her and the target itself, ignoring the last 1.5 m
 * (the target's own cover lip). Without the raycast (another build) everything counts as in sight, as before. */
typedef uint32_t (*WorldFn)(const void*);
typedef uint32_t (*RayFn)(uint32_t, const void*, const void*, float, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t);
static WorldFn g_worldId;
static RayFn g_raycast;
static float s_sightSlack = 1.5f;
static unsigned g_blocked, g_clear;
static uint32_t g_lastWallFilter;
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
static uint32_t UnitOf(uint32_t e)
{
    uint8_t d[24];
    return Descriptor(e, d) ? Word(d + 12) : 0;
}
/* the units of the enemies she knows (refreshed every 0.25 s): an enemy's or a corpse's body is no wall (0.1.6 counted
 * bodies whose actor filter it could not read, filter 0, as walls) */
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
/* the first thing in the way along a ray (its distance; len + 1 when clear), skipping her, leaves and bodies; *hitUnit
 * gets the target's unit when the ray meets it first */
static float FirstWall(uint32_t id, const float from[3], const float dir[3], float len, uint32_t me, uint32_t tu, int* hitTarget)
{
    uint8_t hits[16 * 44];
    memset(hits, 0, sizeof hits);
    uint32_t n = g_raycast(id, from, dir, len, 2, 1, 0, 0x80000009u, me, hits, 16);
    if (n > 16) return -1;
    float best = len + 1;
    int target = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        float d;
        memcpy(&d, hits + 44u * i + 24, 4);
        uint32_t hu = Word(hits + 44u * i + 28);
        if (!isfinite(d) || d < 0.05f || d >= best || (hu && hu == me)) continue;
        if (hu && hu == tu) { best = d; target = 1; continue; }
        uint32_t filt = ActorFilter(Word(hits + 44u * i + 32), hu);
        if (filt == FILTER_FOLIAGE || (hu && (filt == 0x236122F0u || filt == 0xD5B33D56u || FoeUnit(hu)))) continue;   /* leaves; bodies */
        best = d; target = 0;
        g_lastWallFilter = filt;
    }
    if (hitTarget) *hitTarget = target;
    return best;
}
/* 0.1.6b: in sight if any of three rays gets there: to her chest, to her head, or (for an enemy under the crosshair,
 * within 4 degrees) the aim itself reaching the enemy or past it. 0.1.6 tried the chest alone and refused enemies Yui
 * could plainly see */
static int InSight(const float cam[3], const float fwd[3], const float q[3], uint32_t target)
{
    if (!s_sight || !g_raycast || !g_worldId) return 1;
    uintptr_t world = Pointer(game + 0x346BFA0);
    if (!world) return 1;
    uint32_t id = g_worldId((const void*)world);
    if (id >= 4) return 1;
    uint32_t me = UnitOf(atomic_load(&g_avatar)), tu = UnitOf(target);
    float wall = 0, dist = 0;
    for (int r = 0; r < 3; r++)
    {
        float to[3] = {q[0], q[1], q[2] + (r == 1 ? 0.7f : 0)}, dir[3], len;
        if (r < 2)
        {
            for (int k = 0; k < 3; k++) dir[k] = to[k] - cam[k];
            len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
            if (!(len > s_sightSlack + 0.5f)) return 1;
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
        if (w < 0) return 1;
        float reach = r < 2 ? len : len - 2;
        if (hitTarget || w > reach - s_sightSlack) { g_clear++; return 1; }
        if (r == 0) { wall = w; dist = len; }
    }
    g_blocked++;
    static unsigned told;
    if (told++ < 6) Log("line of sight: entity %u hidden at %.0f m (chest ray stopped at %.1f m, filter %08x; head and crosshair rays too)", target, dist, wall, g_lastWallFilter);
    return 0;
}

/* ---- Persecution: her marks --------------------------------------------------------------------------------------- */
#define MARKS 64
typedef struct Mark { uint32_t e; uint64_t res; double seen, last, until, made; } Mark;
static Mark g_marks[MARKS];
static int Elite(uint32_t e, uint64_t res)
{
    int32_t hp;
    uint32_t life;
    if (ArmourOf(res) >= s_markArmour) return 1;
    return s_markHealth > 0 && HealthOf(e, &hp, &life) && hp >= s_markHealth;
}
static Mark* MarkOf(uint32_t e) { for (int i = 0; i < MARKS; i++) if (g_marks[i].e == e) return &g_marks[i]; return NULL; }
static int Marked(uint32_t e) { Mark* m = MarkOf(e); return m && m->until > g_now; }
static unsigned MarkedCount(void) { unsigned n = 0; for (int i = 0; i < MARKS; i++) n += g_marks[i].e && g_marks[i].until > g_now; return n; }
static void MarkFrame(void)
{
    static double next;
    if (g_now < next) return;
    double step = 0.2;
    next = g_now + step;
    float cam[3], fwd[3], q[3];
    int fp;
    if (!View(cam, fwd, &fp)) return;
    for (int k = 0; k < FOES; k++)
    {
        uint32_t e = g_foe[k];
        int32_t hp;
        uint32_t life;
        if (!e || !TargetPos(e, g_foeRes[k], q)) continue;
        float d;
        if (Off(cam, fwd, q, &d) > s_markCone || d > s_markRange) continue;
        if (!HealthOf(e, &hp, &life) || hp <= 0 || !Elite(e, g_foeRes[k])) continue;
        if (!InSight(cam, fwd, q, e)) continue;                                             /* 0.1.6: not through walls */
        Mark* m = MarkOf(e);
        if (!m)
        {
            for (int i = 0; i < MARKS && !m; i++) if (!g_marks[i].e || (g_marks[i].until < g_now && g_now - g_marks[i].last > 2)) m = &g_marks[i];
            if (!m) continue;
            memset(m, 0, sizeof *m);
            m->e = e; m->res = g_foeRes[k];
        }
        m->seen = g_now - m->last <= 2 * step + 0.05 ? m->seen + step : step;   /* in sight without a break */
        m->last = g_now;
        if (m->seen >= s_markSeconds)
        {
            if (m->until <= g_now)
            {
                /* a new mark: past mark_max, the oldest standing mark gives way */
                Mark* oldest = NULL;
                int standing = 0;
                for (int i = 0; i < MARKS; i++)
                    if (g_marks[i].e && g_marks[i].until > g_now && &g_marks[i] != m)
                    {
                        standing++;
                        if (!oldest || g_marks[i].made < oldest->made) oldest = &g_marks[i];
                    }
                if (standing >= s_markMax && oldest) oldest->until = 0;
                m->made = g_now;
                g_marksMade++;
                static unsigned told;
                if (told++ < 10) Log("Persecution: entity %u (%016llx) marked", e, (unsigned long long)m->res);
            }
            m->until = s_markLast > 0 ? g_now + s_markLast : 1e18;   /* 0.1.6b: mark_last 0, a mark stands until it is replaced or dies (Yui) */
        }
    }
}

/* ---- the gun in hand, and her reloads (the HUD's magazine report) ------------------------------------------------- */
static uint32_t g_heldEntity;
static uint64_t g_heldRes;
static uint32_t RoundOf(uint64_t res)
{
    HfField proj = HF_FIELD(ProjectileWeapon, projectile);
    HfLiveValue v;
    memset(&v, 0, sizeof v);
    return hf->get_live(self, res, &proj, &v) ? (uint32_t)v.base.number : 0;
}
static int Rifle(uint64_t res)
{
    if (s_s1AnyGun) return 1;
    static const uint64_t kRifles[] = {HF_ENTITY_R63_DILIGENCE, HF_ENTITY_R63CS_DILIGENCE_COUNTER_SNIPER, HF_ENTITY_R6_DEADEYE,
                                       HF_ENTITY_R2_AMENDMENT, HF_ENTITY_R2124_CONSTITUTION, HF_ENTITY_R40K_HOTSHOT_MARKSMAN_RIFLE,
                                       HF_ENTITY_PLAS39_ACCELERATOR_RIFLE, HF_ENTITY_RS422_RAILGUN};
    for (unsigned i = 0; i < sizeof kRifles / sizeof kRifles[0]; i++) if (res == kRifles[i]) return 1;
    /* 0.1.6: other mods' marksman rifles with rounds of their own (no vanilla round to match): giblett's Scope of
     * Authority R-40 Sightliner (a copy of the R-63CS, round 353 in Yui's 2026-11-04 test, refused by 0.1.5) and LAS-44 Nova
     * (a sniper laser, built on the LAS-99; round 352) */
    static const uint64_t kModRifles[] = {0x41F2DEF484634B59ull, 0x245CB793C32F3FBCull};
    for (unsigned i = 0; i < sizeof kModRifles / sizeof kModRifles[0]; i++) if (res == kModRifles[i]) return 1;
    /* 0.1.2: by its round as well, so a mod's copy of a sniper (a new resource, the same round) counts too: 0.1.1's test
     * refused S1 five times with a rifle the list did not name. The APW-1 (a support weapon) is known this way too */
    uint32_t r = RoundOf(res);
    if (!r) return 0;
    if (r == HF_PROJECTILE_APW1_ANTIMATERIEL_RIFLE) return 1;
    static uint32_t rounds[16];
    static int nrounds = -1;
    if (nrounds < 0)
    {
        nrounds = 0;
        for (unsigned i = 0; i < sizeof kRifles / sizeof kRifles[0] && nrounds < 16; i++)
        {
            uint32_t k = RoundOf(kRifles[i]);
            if (k) rounds[nrounds++] = k;
        }
        if (!nrounds) nrounds = -1;   /* the records are not readable yet: ask again next time */
    }
    for (int i = 0; i < nrounds; i++) if (rounds[i] == r) return 1;
    return 0;
}
static atomic_uint g_ammoEntity;
static atomic_int g_ammoRounds = -1;
static _Atomic unsigned long long g_ammoMs;
HF_MOD_EXPORT int lemuen_ammo(unsigned version, uint32_t entity, int rounds)
{
    if (version != 1) return 0;
    static int first = 1;
    if (first) { first = 0; Log("her magazine: the HUD reports it"); }
    atomic_store(&g_ammoEntity, entity);
    atomic_store(&g_ammoRounds, rounds);
    atomic_store(&g_ammoMs, GetTickCount64());
    return 1;
}
static void Reloaded(const char* why)
{
    if (g_s1On || g_s2On) Log("%s: %s%s%s ends", why, g_s1On ? "Salve Etiam" : "", g_s1On && g_s2On ? " and " : "", g_s2On ? "Honesta Missio" : "");
    g_s1On = g_s2On = 0;
}
static void ReloadFrame(void)
{
    static uint32_t lastEntity;
    static int last = -1;
    unsigned long long ms = atomic_load(&g_ammoMs);
    uint32_t ent = atomic_load(&g_ammoEntity);
    int rounds = atomic_load(&g_ammoRounds);
    if (!ms || GetTickCount64() - ms > 1000 || rounds < 0) { last = -1; return; }
    if (ent == lastEntity && last >= 0 && rounds > last) Reloaded("her reload");
    lastEntity = ent;
    last = rounds;
}

/* ---- Honesta Missio: the target in her scope --------------------------------------------------------------------- */
static uint32_t g_s2Target;
static double g_s2Held, g_s2Lost;
/* 0.1.2: the last lock, kept 1.5 s after the scope leaves it: a long shot lands after the recoil has kicked the
 * scope off its target, and 0.1.1 then found no held target when the hit came (Yui: the boost dropped on the last
 * target without a swap or a reload). Also the scope drifting off for a moment no longer starts the hold anew */
static uint32_t g_s2PrevTarget;
static double g_s2PrevHeld, g_s2PrevAt, g_s2Off;
static double S2Mult(void)
{
    double t = g_s2Held / (s_s2Hold > 0.01f ? s_s2Hold : 0.01f);
    if (t > 1) t = 1;
    return s_s2Min + (s_s2Max - s_s2Min) * t;
}
static void ScopeFrame(double dt)
{
    if (!g_s2On) { g_s2Target = 0; g_s2Held = 0; return; }
    float cam[3], fwd[3], q[3];
    int fp = 0;
    if (!View(cam, fwd, &fp) || !fp)
    {
        if (g_now - g_s2Lost > 0.3) { g_s2Target = 0; g_s2Held = 0; }   /* out of the scope: the build-up fades */
        return;
    }
    g_s2Lost = g_now;
    uint32_t best = 0;
    float bestOff = s_s2Cone;
    for (int k = 0; k < FOES; k++)
    {
        uint32_t e = g_foe[k];
        float d;
        if (!e || !TargetPos(e, g_foeRes[k], q)) continue;
        float o = Off(cam, fwd, q, &d);
        if (o < bestOff && d <= s_s2Range && InSight(cam, fwd, q, e)) { bestOff = o; best = e; }   /* 0.1.6: in sight only */
    }
    if (best == g_s2Target) { if (best) { g_s2Held += dt; g_s2Off = 0; } return; }
    if (!best && g_s2Target)
    {
        /* nobody in the cone: keep the lock for a moment (sway, recoil) */
        if (!g_s2Off) g_s2Off = g_now;
        if (g_now - g_s2Off < 0.4) return;
    }
    if (g_s2Target && g_s2Held > 0) { g_s2PrevTarget = g_s2Target; g_s2PrevHeld = g_s2Held; g_s2PrevAt = g_now; }
    g_s2Target = best; g_s2Held = 0; g_s2Off = 0;
}

/* 0.1.2: her marks for the HUD's markers (lemuen_marks): where each standing mark is, refreshed on the game thread
 * every frame (TargetPos reads the game's records, not for the HUD's thread) */
typedef struct { float x, y, z, left; uint32_t heavy; } MarkSpot;
static MarkSpot g_spots[32];
static volatile int g_nspots;
static void SpotsFrame(void)
{
    int n = 0;
    float q[3];
    for (int i = 0; i < MARKS && n < 32; i++)
    {
        Mark* m = &g_marks[i];
        if (!m->e || m->until <= g_now) continue;
        int32_t hp;
        uint32_t life;
        if (!TargetPos(m->e, m->res, q) || !HealthOf(m->e, &hp, &life) || hp <= 0) { m->until = 0; continue; }   /* 0.1.6b: dead or gone, its place is free */
        g_spots[n].x = q[0]; g_spots[n].y = q[1]; g_spots[n].z = q[2];
        g_spots[n].left = (float)(m->until - g_now);
        g_spots[n].heavy = ArmourOf(m->res) >= 4;
        n++;
    }
    g_nspots = n;
}

/* ---- the skills --------------------------------------------------------------------------------------------------- */
static void SalveEtiam(void)
{
    if (g_now < g_cd[0] || g_s1On) return;
    if (!g_heldRes || !Rifle(g_heldRes))
    {
        static unsigned told;
        if (told++ < 5) Log("Salve Etiam: needs a sniper or marksman rifle in hand (this one: %016llx, round %u; s1_any_gun = 1 allows any)",
                            (unsigned long long)g_heldRes, g_heldRes ? RoundOf(g_heldRes) : 0);
        return;
    }
    g_cd[0] = g_now + s_s1Cd;
    g_s1On = 1;
    g_casts[0]++;
    Log("Salve Etiam: her shots deal x%.2f until she reloads", (double)s_s1Mult);
}
static void HonestaMissio(void)
{
    if (g_now < g_cd[1] || g_s2On) return;
    g_cd[1] = g_now + s_s2Cd;
    g_s2On = 1;
    g_s2Target = 0;
    g_s2Held = 0;
    g_casts[1]++;
    Log("Honesta Missio: until she reloads, what she holds in her scope builds up (x%.2f to x%.2f over %.1f s)", (double)s_s2Min,
        (double)s_s2Max, (double)s_s2Hold);
}
static uint32_t g_shellQ[32];
static int g_nshell;
static double g_shellNext;
static void MementoMori(void)
{
    if (g_now < g_cd[2]) return;
    int n = 0;
    for (int i = 0; i < MARKS && n < s_s3Max && n < 32; i++)
    {
        int32_t hp;
        uint32_t life;
        if (g_marks[i].e && g_marks[i].until > g_now && HealthOf(g_marks[i].e, &hp, &life) && hp > 0) g_shellQ[n++] = g_marks[i].e;
    }
    if (!n) { static unsigned told; if (told++ < 5) Log("Memento Mori: no enemy is marked"); return; }
    g_cd[2] = g_now + s_s3Cd;
    g_nshell = n;
    g_shellNext = g_now;
    g_casts[2]++;
    Log("Memento Mori: an Orbital 120mm shell on each of %d marked enemies", n);
}
static void ShellFrame(void)
{
    while (g_nshell > 0 && g_now >= g_shellNext)
    {
        uint32_t e = g_shellQ[g_nshell - 1];
        Mark* m = MarkOf(e);
        float q[3];
        if (!m || !TargetPos(e, m->res, q)) { g_nshell--; continue; }
        float from[3] = {q[0] + ((float)rand() / RAND_MAX - 0.5f) * 2, q[1] + ((float)rand() / RAND_MAX - 0.5f) * 2, q[2] + s_s3Height};
        float down[3] = {0, 0, -1};
        int r = hf->spawn_projectile(self, (uint32_t)s_s3Shell, from, down);
        if (r == HF_ACT_WAITING) { g_shellNext = g_now + 0.1; return; }
        if (Act("a Memento Mori shell", r))
        {
            g_shells++;
            static unsigned toldShell;
            if (toldShell++ < 12) Log("Memento Mori: a shell (projectile %d) from %.0f m above entity %u", s_s3Shell, (double)s_s3Height, e);
        }
        g_nshell--;
        g_shellNext = g_now + s_s3Every;
    }
}

/* ---- the damage event: her shots ------------------------------------------------------------------------------- */
static void DamageTaken(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    (void)e; (void)d; (void)me;
}
/* 0.1.3: her hits for the HUD's damage numbers (lemuen_hits): a ring the game thread writes, the HUD reads by
 * sequence number */
#define HITRING 64
typedef struct { float x, y, z, amount; uint32_t boosted, seq; } HitNote;
static HitNote g_ring[HITRING];
static volatile uint32_t g_ringSeq;
static void NoteHit(const HfDamageEvent* d, int boosted)
{
    uint32_t s = g_ringSeq + 1;
    HitNote* h = &g_ring[s % HITRING];
    h->x = d->position[0]; h->y = d->position[1]; h->z = d->position[2];
    h->amount = (float)d->amount; h->boosted = (uint32_t)boosted;
    h->seq = s;
    g_ringSeq = s;
}
static int g_hitLogReset;
static void DamageDealt(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    (void)e; (void)me;
    if (d->amount <= 0 || !MineHit(d)) return;
    double mult = 1, xMark = 1, xModus = 1, xS1 = 1, xS2 = 1;
    if (Marked(d->target)) mult *= xMark = s_markMult;                                   /* Persecution */
    if (g_landedAt >= 0 && g_now - g_landedAt >= s_modusAfter) mult *= xModus = s_modusMult;   /* Modus Extraditio */
    int32_t base = d->amount;
    if (IsShot(d))
    {
        if (g_s1On) { mult *= xS1 = s_s1Mult; g_empowered++; }                           /* Salve Etiam */
        if (g_s2On && d->target && d->target == g_s2PrevTarget && g_now - g_s2PrevAt < 1.5 && g_s2PrevHeld > 0 &&
            !(d->target == g_s2Target && g_s2Held > 0))
        {
            /* the lock it just left (see g_s2PrevTarget) */
            g_s2Target = g_s2PrevTarget; g_s2Held = g_s2PrevHeld; g_s2PrevTarget = 0;
        }
        if (g_s2On && g_s2Target && d->target == g_s2Target && g_s2Held > 0)             /* Honesta Missio */
        {
            double x = S2Mult();
            mult *= x;
            xS2 = x;
            g_held++;
            static unsigned told;
            if (told++ < 8) Log("Honesta Missio: held %.1f s on entity %u: x%.2f", g_s2Held, d->target, x);
            g_s2Held = 0;
        }
        NoteTarget(d);
    }
    if (mult != 1) ScaleDamage(d, mult);
    /* 0.1.5: Modus Extraditio is +10% ATK in Arknights (a stat buff: no number), Persecution "+18% damage taken" (counts) */
    if (kb_kit_on && g_inMission && xMark * xS2 >= s_numbersAt - 0.0001) NoteHit(d, xS2 != 1);
    /* 0.1.1: her boosted shots in numbers (0.1.0's log said a skill was on, never what a hit dealt): the first 16 hits
     * of a mission that a skill raised, base -> dealt, and each multiplier */
    static unsigned shown;
    if (g_hitLogReset) { shown = 0; g_hitLogReset = 0; }
    if ((xS1 != 1 || xS2 != 1) && shown < 16)
    {
        shown++;
        Log("her hit on entity %u: %d -> %d damage (x%.2f: mark x%.2f, Modus x%.2f, Salve Etiam x%.2f, Honesta Missio x%.2f)", d->target, base,
            d->amount, mult, xMark, xModus, xS1, xS2);
    }
}

/* ---- events ------------------------------------------------------------------------------------------------------ */
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
static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfKillEvent* k = (const HfKillEvent*)info;
    DropFoe(k->victim);
    Mark* m = MarkOf(k->victim);
    if (m) memset(m, 0, sizeof *m);
    if (k->victim == g_s2Target) { g_s2Target = 0; g_s2Held = 0; }
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
        g_landedAt = g_now;   /* Modus Extraditio counts from here */
        LearnNet();
    }
    else { g_alive = 0; Reloaded("she is down"); }
}
static void OnWeaponLem(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfWeaponEvent* w = (const HfWeaponEvent*)info;
    if (!w->avatar || w->avatar != atomic_load(&g_avatar) || !kb_kit_on) return;
    if (g_heldEntity && w->entity != g_heldEntity && g_s1On) Reloaded("she changed guns");
    g_heldEntity = w->entity;
    g_heldRes = w->resource;
}
static void Reset(const char* why)
{
    int start = !strcmp(why, "mission start");
    if (start) KitDecide();   /* with several kits installed: is this one picked? */
    if (g_casts[0] || g_casts[1] || g_casts[2])
        Log("%s: casts %u / %u / %u, %u marks, %u empowered shots, %u held shots, %u shells", why, g_casts[0], g_casts[1], g_casts[2],
            g_marksMade, g_empowered, g_held, g_shells);
    Body(start);
    if (start && s_hellbomb && s_hellbombRow == 125 && !hf->load_package(self, UINT64_C(0x346D785D6CA13FF4))) Log("the Hellbomb's package could not be loaded ahead");
    g_boomPending = 0;
    g_boomArmed = 0;
    memset(g_cd, 0, sizeof g_cd);
    memset(g_casts, 0, sizeof g_casts);
    memset(g_marks, 0, sizeof g_marks);
    g_marksMade = g_empowered = g_held = g_shells = 0;
    g_hitLogReset = 1;
    g_s1On = g_s2On = 0;
    g_s2Target = 0;
    g_s2Held = 0;
    g_nshell = 0;
    g_landedAt = -1;
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
    if (hf->code_matches(exe + 0x79F860, "33 c0 4c 8d 05 c7 b2 01 02 0f 1f 80 00 00 00 00 49 39 0c c0 74 0c ff c0 83 f8 04 75 f3") &&
        hf->code_matches(exe + 0x7F4590, "48 8b c4 f3 0f 11 58 20 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48"))
    {
        g_worldId = (WorldFn)(exe + 0x79F860);
        g_raycast = (RayFn)(exe + 0x7F4590);
        Log("line of sight ready (the game's raycast): marks and Honesta Missio's lock need a clear view");
    }
    else Log("line of sight off: the raycast is not the reviewed code (marks and locks go through walls)");
    g_staminaOk = hf->code_matches(game + 0xA50E2D, "f3 42 0f 11 8c 11 54 78 54 00");
}

/* ---- the HUD state (read by the Lemuen HUD addon through LuaJIT's ffi) ----------------------------------------- */
typedef struct LemuenHud
{
    uint32_t version, in_mission;
    float cd[3], cd_max[3];            /* seconds left, full cooldowns: Salve Etiam, Honesta Missio, Memento Mori */
    uint32_t s1_on, s2_on, marked, scoped;
    float hold;                        /* Honesta Missio's build-up, 0..1 (its target held) */
    float modus;                       /* seconds until Modus Extraditio (0: on) */
    uint32_t keys[3];
    char key_label[3][24];
} LemuenHud;
static LemuenHud g_hud = {1};
/* 0.1.2: her standing marks, 5 floats each (x, y, z of the unit's root, seconds left, 1 if heavily armoured): how many */
HF_MOD_EXPORT int lemuen_marks(unsigned version, float* out, int max)
{
    if (version != 1 || !out || max <= 0) return 0;
    int n = g_nspots;
    if (!g_inMission || !kb_kit_on) return 0;
    if (n > max) n = max;
    for (int i = 0; i < n; i++)
    {
        out[i * 5] = g_spots[i].x; out[i * 5 + 1] = g_spots[i].y; out[i * 5 + 2] = g_spots[i].z;
        out[i * 5 + 3] = g_spots[i].left; out[i * 5 + 4] = (float)g_spots[i].heavy;
    }
    return n;
}
/* 0.1.3: her hits since *seq (the HUD's last), 5 floats each: x, y, z of the impact, damage dealt, 1 if S1 or S2
 * raised it. *seq becomes the newest. A HUD that fell behind gets the latest HITRING */
HF_MOD_EXPORT int lemuen_hits(unsigned version, float* out, int max, uint32_t* seq)
{
    if (version != 1 || !out || !seq || max <= 0) return 0;
    uint32_t now = g_ringSeq, from = *seq;
    if (now - from > HITRING) from = now - HITRING;
    int n = 0;
    for (uint32_t s = from + 1; s - 1 != now && n < max; s++)
    {
        HitNote h = g_ring[s % HITRING];
        if (h.seq != s) continue;
        out[n * 5] = h.x; out[n * 5 + 1] = h.y; out[n * 5 + 2] = h.z; out[n * 5 + 3] = h.amount; out[n * 5 + 4] = (float)h.boosted;
        n++;
    }
    *seq = now;
    return n;
}
HF_MOD_EXPORT LemuenHud* lemuen_hud_state(void)
{
    g_hud.in_mission = (uint32_t)(g_inMission && g_alive && kb_kit_on);
    float m[3] = {s_s1Cd, s_s2Cd, s_s3Cd};
    for (int i = 0; i < 3; i++) { g_hud.cd[i] = (float)fmax(0, g_cd[i] - g_now); g_hud.cd_max[i] = m[i]; }
    g_hud.s1_on = (uint32_t)g_s1On;
    g_hud.s2_on = (uint32_t)g_s2On;
    g_hud.marked = MarkedCount();
    g_hud.scoped = (uint32_t)(g_s2On && g_s2Target != 0);
    g_hud.hold = g_s2Target ? (float)fmin(1, g_s2Held / (s_s2Hold > 0.01f ? s_s2Hold : 0.01f)) : 0;
    g_hud.modus = g_landedAt < 0 ? s_modusAfter : (float)fmax(0, s_modusAfter - (g_now - g_landedAt));
    for (int i = 0; i < 3; i++)
    {
        g_hud.keys[i] = (uint32_t)s_keys[i].vk;
        memcpy(g_hud.key_label[i], s_keys[i].label, sizeof g_hud.key_label[i]);
    }
    return &g_hud;
}
static _Atomic unsigned long long g_bindMs;
static atomic_int g_bindPress;
HF_MOD_EXPORT int lemuen_input(unsigned version, int skill)
{
    if (version != 1) return 0;
    atomic_store(&g_bindMs, GetTickCount64());
    if (skill >= 1 && skill <= 3) atomic_fetch_or(&g_bindPress, 1 << (skill - 1));
    return 1;
}
HF_MOD_EXPORT int lemuen_key(unsigned version, int skill, int* vk, int* mods)
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
    if (!g_inMission || !kb_kit_on) return;   /* (standing by: another kit is picked) */
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
            if (i == 0) SalveEtiam();
            else if (i == 1) HonestaMissio();
            else MementoMori();
        }
        prev[i] = down;
    }
    if (g_alive)
    {
        MarkFrame();
        SpotsFrame();
        ReloadFrame();
        ScopeFrame(step);
        ShellFrame();
    }
    Exhaustion();
    atomic_exchange(&g_ownStopped, 0);
    atomic_exchange(&g_kdStopped, 0);
}

/* ---- Mod Options Menu ------------------------------------------------------------------------------------------- */
static const KbOption kOptions[] = {
    {"mark_range", &s_markRange, NULL}, {"mark_seconds", &s_markSeconds, NULL}, {"mark_mult", &s_markMult, NULL}, {"mark_armour", NULL, &s_markArmour},
    {"modus_mult", &s_modusMult, NULL}, {"s1_cooldown", &s_s1Cd, NULL}, {"s1_mult", &s_s1Mult, NULL}, {"s1_any_gun", NULL, &s_s1AnyGun},
    {"s2_cooldown", &s_s2Cd, NULL}, {"s2_max", &s_s2Max, NULL}, {"s2_hold", &s_s2Hold, NULL}, {"s3_cooldown", &s_s3Cd, NULL},
    {"s3_max", NULL, &s_s3Max}, {"hp_mult", &s_hpMult, NULL}, {"stamina_mult", &s_staminaMult, NULL},
};
static void OptionAfter(const char* name, int changed) { (void)name; (void)changed; }
HF_MOD_EXPORT int lemuen_option_get(const char* name, double* out)
{
    return OptionGet(name, out);
}
HF_MOD_EXPORT int lemuen_option_set(const char* name, double value)
{
    return OptionSet(name, value);
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    RosterJoin("lemuen", "Lemuen");
    kb_ini = "lemuen.ini";
    kb_key_names[0] = "key_s1";
    kb_key_names[1] = "key_s2";
    kb_key_names[2] = "key_s3";
    KB_SETTINGS(kSettings);
    KB_OPTIONS(kOptions);
    kb_option_after = OptionAfter;
    kb_damage_taken = DamageTaken;
    kb_damage_dealt = DamageDealt;
    /* her body: a Sankta's, not a T-Doll's (the kit base's defaults are the T-Dolls') */
    s_hpMult = 1.1f;
    s_staminaMult = 1.0f;
    s_hellbomb = 0;
    if (!api || api->version < 13 || api->size < offsetof(HfApi, code_matches) + sizeof api->code_matches) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    LoadSettings();
    api->log(mod, "Lemuen %s: keys %s / %s / %s (Salve Etiam, Honesta Missio, Memento Mori)", VERSION, s_keys[0].label, s_keys[1].label,
             s_keys[2].label);
    if (s_badKey) api->log(mod, "lemuen.ini: a key could not be read (bits %d: s1, s2, s3); its default stays", s_badKey);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) &&
             api->subscribe(mod, HF_ON_WEAPON, OnWeaponLem, NULL, 0) && api->on_game_thread(mod, Tick, NULL);
    if (!ok) api->log(mod, "events or the game thread are unavailable: Lemuen stays off");
    return 0;
}
