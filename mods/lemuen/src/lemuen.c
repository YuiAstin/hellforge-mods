/* Lemuen (yui.lemuen): Lemuen's kit from Arknights as abilities on keys, for your own Helldiver (solo or host): a
 * long-range sniper, built on the kit base (kitbase/, docs/kit-base.md).
 *
 *   Persecution (passive): a heavily armoured or tough enemy (armour mark_armour+, or mark_health+ health) that she
 *     keeps in her sights (within mark_cone degrees of her view, mark_range m) for mark_seconds is marked for
 *     mark_last s (renewed while she watches it). Marked enemies take mark_mult times the damage from her.
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

#define VERSION "0.1.0"
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
static float s_markRange = 100, s_markCone = 20, s_markSeconds = 6, s_markLast = 30, s_markMult = 1.18f;
static int s_markArmour = 3, s_markHealth = 750;
static float s_modusAfter = 20, s_modusMult = 1.10f;
static float s_s1Cd = 15, s_s1Mult = 3.1f;
static int s_s1AnyGun = 0;
static float s_s2Cd = 20, s_s2Min = 1.8f, s_s2Max = 4.25f, s_s2Hold = 3.5f, s_s2Cone = 4, s_s2Range = 250, s_fpDistance = 0.45f;
static float s_s3Cd = 90, s_s3Every = 0.35f, s_s3Height = 120;
static int s_s3Max = 8, s_s3Shell = 194;

static const KbSetting kSettings[] = {
    KB_F("mark_range", s_markRange, 10, 500), KB_F("mark_cone", s_markCone, 1, 90), KB_F("mark_seconds", s_markSeconds, 0, 30),
    KB_F("mark_last", s_markLast, 1, 600), KB_F("mark_mult", s_markMult, 1, 5), KB_I("mark_armour", s_markArmour, 0, 10),
    KB_I("mark_health", s_markHealth, 0, 100000),
    KB_F("modus_after", s_modusAfter, 0, 600), KB_F("modus_mult", s_modusMult, 1, 3),
    KB_F("s1_cooldown", s_s1Cd, 0, 600), KB_F("s1_mult", s_s1Mult, 1, 20), KB_I("s1_any_gun", s_s1AnyGun, 0, 1),
    KB_F("s2_cooldown", s_s2Cd, 0, 600), KB_F("s2_min", s_s2Min, 1, 20), KB_F("s2_max", s_s2Max, 1, 20), KB_F("s2_hold", s_s2Hold, 0.1, 30),
    KB_F("s2_cone", s_s2Cone, 0.5, 30), KB_F("s2_range", s_s2Range, 10, 1000), KB_F("first_person", s_fpDistance, 0.1, 2),
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
               "; view (mark_range m) for mark_seconds is marked for mark_last s (renewed while she watches it); marked enemies\n"
               "; take mark_mult times the damage from her.\n"
               "mark_range = %g\nmark_cone = %g\nmark_seconds = %g\nmark_last = %g\nmark_mult = %g\nmark_armour = %d\nmark_health = %d\n"
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
               "hp_mult = 1.1\nstamina_mult = 1\nhellbomb_on_exhaustion = 0\n",
            VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, (double)s_markRange, (double)s_markCone, (double)s_markSeconds,
            (double)s_markLast, (double)s_markMult, s_markArmour, s_markHealth, (double)s_modusAfter, (double)s_modusMult, (double)s_s1Cd,
            (double)s_s1Mult, s_s1AnyGun, (double)s_s2Cd, (double)s_s2Min, (double)s_s2Max, (double)s_s2Hold, (double)s_s2Cone,
            (double)s_s2Range, (double)s_fpDistance, (double)s_s3Cd, (double)s_s3Every, (double)s_s3Height, s_s3Max, s_s3Shell);
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

/* ---- Persecution: her marks --------------------------------------------------------------------------------------- */
#define MARKS 64
typedef struct Mark { uint32_t e; uint64_t res; double seen, last, until; } Mark;
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
                g_marksMade++;
                static unsigned told;
                if (told++ < 10) Log("Persecution: entity %u (%016llx) marked", e, (unsigned long long)m->res);
            }
            m->until = g_now + s_markLast;
        }
    }
}

/* ---- the gun in hand, and her reloads (the HUD's magazine report) ------------------------------------------------- */
static uint32_t g_heldEntity;
static uint64_t g_heldRes;
static int Rifle(uint64_t res)
{
    if (s_s1AnyGun) return 1;
    static const uint64_t kRifles[] = {HF_ENTITY_R63_DILIGENCE, HF_ENTITY_R63CS_DILIGENCE_COUNTER_SNIPER, HF_ENTITY_R6_DEADEYE,
                                       HF_ENTITY_R2_AMENDMENT, HF_ENTITY_R2124_CONSTITUTION, HF_ENTITY_R40K_HOTSHOT_MARKSMAN_RIFLE,
                                       HF_ENTITY_PLAS39_ACCELERATOR_RIFLE, HF_ENTITY_RS422_RAILGUN};
    for (unsigned i = 0; i < sizeof kRifles / sizeof kRifles[0]; i++) if (res == kRifles[i]) return 1;
    /* the APW-1 Anti-Materiel Rifle (a support weapon): known by its round */
    HfField proj = HF_FIELD(ProjectileWeapon, projectile);
    HfLiveValue v;
    memset(&v, 0, sizeof v);
    return hf->get_live(self, res, &proj, &v) && (uint32_t)v.base.number == HF_PROJECTILE_APW1_ANTIMATERIEL_RIFLE;
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
        if (o < bestOff && d <= s_s2Range) { bestOff = o; best = e; }
    }
    if (best != g_s2Target) { g_s2Target = best; g_s2Held = 0; }
    else if (best) g_s2Held += dt;
}

/* ---- the skills --------------------------------------------------------------------------------------------------- */
static void SalveEtiam(void)
{
    if (g_now < g_cd[0] || g_s1On) return;
    if (!g_heldRes || !Rifle(g_heldRes)) { static unsigned told; if (told++ < 5) Log("Salve Etiam: needs a sniper or marksman rifle in hand"); return; }
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
        if (Act("a Memento Mori shell", r)) g_shells++;
        g_nshell--;
        g_shellNext = g_now + s_s3Every;
    }
}

/* ---- the damage event: her shots ------------------------------------------------------------------------------- */
static void DamageTaken(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    (void)e; (void)d; (void)me;
}
static void DamageDealt(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    (void)e; (void)me;
    if (d->amount <= 0 || !MineHit(d)) return;
    double mult = 1;
    if (Marked(d->target)) mult *= s_markMult;                                           /* Persecution */
    if (g_landedAt >= 0 && g_now - g_landedAt >= s_modusAfter) mult *= s_modusMult;      /* Modus Extraditio */
    if (IsShot(d))
    {
        if (g_s1On) { mult *= s_s1Mult; g_empowered++; }                                  /* Salve Etiam */
        if (g_s2On && g_s2Target && d->target == g_s2Target && g_s2Held > 0)             /* Honesta Missio */
        {
            double x = S2Mult();
            mult *= x;
            g_held++;
            static unsigned told;
            if (told++ < 8) Log("Honesta Missio: held %.1f s on entity %u: x%.2f", g_s2Held, d->target, x);
            g_s2Held = 0;
        }
        NoteTarget(d);
    }
    if (mult != 1) ScaleDamage(d, mult);
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
