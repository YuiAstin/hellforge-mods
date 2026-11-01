/* Veterancy System: the A/LAS-98 Laser Sentry (laser_sentry.c), the beam that burns through.
 *
 * Its own kills this mission level it (ls_level2_kills, ls_level3_kills; force_level too). The longer it keeps its
 * beam on, the harder the beam hits (its "focus", 0 to full over ls_ramp_seconds of fire; a pause longer than
 * ls_ramp_grace seconds lets it fall at ls_ramp_decay times the rate it builds):
 *   Level 2, "Focusing lens": damage up to ls_l2_damage times, armour penetration up to ls_l2_ap, demolition up to
 *     ls_l2_demolition.
 *   Level 3, "Burn-through": damage up to ls_l3_damage times, armour penetration up to ls_l3_ap, demolition up to
 *     ls_l3_demolition, and the beam gains stagger (ls_l3_stagger) and push (ls_l3_push): a held beam shoves its target.
 * The damage is each sentry's own (its hits' amounts). The armour penetration, demolition, stagger and push are the
 * beam's damage row (ls_damage_id: 507, the Laser Sentry's own), which every Laser Sentry shares: it follows the most
 * focused one, in four steps, and goes back to the game's when no Laser Sentry is firing.
 * From level 2, a heat sink: Laser Sentries called in from then on heat ls_heat_mult times as fast and cool ls_cool_mult
 * times as fast, so the beam (and its focus) stays on longer. Heat is the sentry type's record, copied when a sentry is
 * made (a kill-by-kill vent was tried: the game takes it only at the next call-in). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hellforge_mod.h"
#include "hellforge_components.h"
#include "hellforge_content.h"
#include "field_promotions.h"

#define LS HF_ENTITY_ALAS98_LASER_SENTRY
#define STEPS 4

static const HfApi* g_hf;
static HfMod* g_mod;
static int g_kills, g_level = 1;

/* settings */
static int s_l2 = 30, s_l3 = 180, s_damageId = 507;
static float s_rampSeconds = 5, s_grace = 0.6f, s_decay = 2;
static float s_l2Damage = 2, s_l3Damage = 3;
static float s_heatMult = 0.5f, s_coolMult = 3;
static int s_l2Ap = 5, s_l3Ap = 6, s_l2Demo = 40, s_l3Demo = 60, s_l3Stagger = 30, s_l3Push = 30;

int ls_setting(const char* key, double v)
{
#define CL(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
    if (!strcmp(key, "ls_level2_kills")) s_l2 = (int)CL(v, 0, 100000);
    else if (!strcmp(key, "ls_level3_kills")) s_l3 = (int)CL(v, 0, 100000);
    else if (!strcmp(key, "ls_ramp_seconds")) s_rampSeconds = (float)CL(v, 0.2, 120);
    else if (!strcmp(key, "ls_ramp_grace")) s_grace = (float)CL(v, 0, 10);
    else if (!strcmp(key, "ls_ramp_decay")) s_decay = (float)CL(v, 0, 100);
    else if (!strcmp(key, "ls_l2_damage")) s_l2Damage = (float)CL(v, 1, 50);
    else if (!strcmp(key, "ls_l3_damage")) s_l3Damage = (float)CL(v, 1, 50);
    else if (!strcmp(key, "ls_l2_ap")) s_l2Ap = (int)CL(v, 0, 10);
    else if (!strcmp(key, "ls_l3_ap")) s_l3Ap = (int)CL(v, 0, 10);
    else if (!strcmp(key, "ls_l2_demolition")) s_l2Demo = (int)CL(v, 0, 100);
    else if (!strcmp(key, "ls_l3_demolition")) s_l3Demo = (int)CL(v, 0, 100);
    else if (!strcmp(key, "ls_l3_stagger")) s_l3Stagger = (int)CL(v, 0, 100);
    else if (!strcmp(key, "ls_l3_push")) s_l3Push = (int)CL(v, 0, 100);
    else if (!strcmp(key, "ls_heat_mult")) s_heatMult = (float)CL(v, 0, 10);
    else if (!strcmp(key, "ls_cool_mult")) s_coolMult = (float)CL(v, 0.1, 100);
    else if (!strncmp(key, "ls_vent_", 8)) { /* the kill-by-kill vent's, no longer used */ }
    else if (!strcmp(key, "ls_damage_id")) s_damageId = (int)CL(v, 0, 100000);
    else return 0;
    return 1;
#undef CL
}

void ls_write_defaults(FILE* f)
{
    fprintf(f, "\n; Laser Sentry: its kills this mission for level 2 and level 3.\nls_level2_kills = %d\nls_level3_kills = %d\n"
               "; Its focus: seconds of beam to reach full; a pause longer than ls_ramp_grace s lets it fall, ls_ramp_decay\n"
               "; times as fast as it builds.\nls_ramp_seconds = %g\nls_ramp_grace = %g\nls_ramp_decay = %g\n"
               "; Level 2 at full focus: damage times this, armour penetration (4: heavy, 5: tank, 6: anti-tank) and demolition.\n"
               "ls_l2_damage = %g\nls_l2_ap = %d\nls_l2_demolition = %d\n"
               "; Level 3 at full focus: the same, and the beam's stagger and push (the game's: 0).\n"
               "ls_l3_damage = %g\nls_l3_ap = %d\nls_l3_demolition = %d\nls_l3_stagger = %d\nls_l3_push = %d\n"
               "; The beam's damage row (507: the Laser Sentry's own).\nls_damage_id = %d\n"
               "; Level 2 and 3, heat sink (from its next call-in): its beam's heat and its cooling, times the game's.\n"
               "ls_heat_mult = %g\nls_cool_mult = %g\n",
        s_l2, s_l3, (double)s_rampSeconds, (double)s_grace, (double)s_decay, (double)s_l2Damage, s_l2Ap, s_l2Demo,
        (double)s_l3Damage, s_l3Ap, s_l3Demo, s_l3Stagger, s_l3Push, s_damageId, (double)s_heatMult, (double)s_coolMult);
}

void ls_write_vent(FILE* f)
{
    fprintf(f, "\n; Laser Sentry level 2 and 3, heat sink (from its next call-in): its beam's heat and its cooling, times the\n"
               "; game's. (The ls_vent_ lines above are no longer used: the game takes heat changes only at a call-in.)\n"
               "ls_heat_mult = %g\nls_cool_mult = %g\n", (double)s_heatMult, (double)s_coolMult);
}

static int Level(void)
{
    if (fp_forced_level()) return fp_forced_level();
    return g_kills >= s_l3 ? 3 : g_kills >= s_l2 ? 2 : 1;
}
int ls_level(void) { return Level(); }

/* ---- heat sink: a cooler sentry from its next call-in ------------------------------------------------------------ */
static int g_sink;

static void HeatSink(int on)
{
    HfField heat = HF_FIELD(WeaponHeat, heat_per_second), cool = HF_FIELD(WeaponHeat, cool_per_second);
    if (!on)
    {
        if (g_sink) { g_hf->set_live(g_mod, LS, &heat, NULL, 0); g_hf->set_live(g_mod, LS, &cool, NULL, 0); }
        g_sink = 0;
        return;
    }
    if (g_sink || (s_heatMult == 1.0f && s_coolMult == 1.0f)) return;
    g_sink = 1;
    HfLiveValue h, c;
    memset(&h, 0, sizeof h);
    memset(&c, 0, sizeof c);
    if (!g_hf->get_live(g_mod, LS, &heat, &h) || !g_hf->get_live(g_mod, LS, &cool, &c))
    {
        g_hf->log(g_mod, "Laser Sentry heat sink: its heat cannot be read");
        return;
    }
    HfValue hv = HF_F32(h.base.number * s_heatMult), cv = HF_F32(c.base.number * s_coolMult);
    int a = g_hf->set_live(g_mod, LS, &heat, &hv, 0);
    int b = g_hf->set_live(g_mod, LS, &cool, &cv, 0);
    g_hf->log(g_mod, "Laser Sentry level 2: heat %.1f -> %.1f a second %s, cooling %.1f -> %.1f a second %s", h.base.number, hv.number,
        a == HF_APPLIES_NOW ? "(now)" : a ? "(from its next call-in)" : "REFUSED", c.base.number, cv.number,
        b == HF_APPLIES_NOW ? "(now)" : b ? "(from its next call-in)" : "REFUSED");
}

/* ---- the beam's row: armour penetration, demolition, stagger, push ---------------------------------------------- */
typedef struct Base { int read; uint32_t ap[4], demo, stagger, push; } Base;
static Base g_base;
static int g_step = -1;          /* the step the row is at (-1: the game's) */
static unsigned g_stepWrites, g_refused;
static int g_topStep;

static uint32_t ReadU32(const HfField* field)
{
    HfLiveValue v;
    memset(&v, 0, sizeof v);
    if (!g_hf->get_live(g_mod, (uint32_t)s_damageId, field, &v)) return 0xFFFFFFFFu;
    return (uint32_t)v.base.number;
}

static int ReadBase(void)
{
    if (g_base.read) return 1;
    for (int i = 0; i < 4; i++)
    {
        HfField f = HF_FIELD_AT(DamageInfo, armor_penetration, i);
        g_base.ap[i] = ReadU32(&f);
        if (g_base.ap[i] == 0xFFFFFFFFu) return 0;
    }
    HfField d = HF_FIELD(DamageInfo, demolition), s = HF_FIELD(DamageInfo, stagger), p = HF_FIELD(DamageInfo, push_force);
    g_base.demo = ReadU32(&d);
    g_base.stagger = ReadU32(&s);
    g_base.push = ReadU32(&p);
    if (g_base.demo == 0xFFFFFFFFu || g_base.stagger == 0xFFFFFFFFu || g_base.push == 0xFFFFFFFFu) return 0;
    g_base.read = 1;
    g_hf->log(g_mod, "Laser Sentry beam (damage %d): AP %u/%u/%u/%u, demolition %u, stagger %u, push %u", s_damageId, g_base.ap[0],
        g_base.ap[1], g_base.ap[2], g_base.ap[3], g_base.demo, g_base.stagger, g_base.push);
    return 1;
}

static uint32_t Lerp(uint32_t from, int to, double t)
{
    if (to < (int)from) return from;  /* never below the game's */
    return (uint32_t)floor(from + (to - (double)from) * t + 0.5);
}

static int Set(const HfField* f, uint32_t value)
{
    HfValue v = HF_U32(value);
    int r = g_hf->set_live(g_mod, (uint32_t)s_damageId, f, &v, HF_LIVE_SHARED);
    if (!r && g_refused++ < 3)
        g_hf->log(g_mod, "Laser Sentry: a beam row change was refused (%s)", g_hf->last_log ? g_hf->last_log(g_mod) : "");
    return r;
}

static void SetStep(int step)
{
    if (step == g_step) return;
    HfField d = HF_FIELD(DamageInfo, demolition), s = HF_FIELD(DamageInfo, stagger), p = HF_FIELD(DamageInfo, push_force);
    if (step <= 0)
    {
        if (g_step > 0)
        {
            for (int i = 0; i < 4; i++) { HfField f = HF_FIELD_AT(DamageInfo, armor_penetration, i); g_hf->set_live(g_mod, (uint32_t)s_damageId, &f, NULL, HF_LIVE_SHARED); }
            g_hf->set_live(g_mod, (uint32_t)s_damageId, &d, NULL, HF_LIVE_SHARED);
            g_hf->set_live(g_mod, (uint32_t)s_damageId, &s, NULL, HF_LIVE_SHARED);
            g_hf->set_live(g_mod, (uint32_t)s_damageId, &p, NULL, HF_LIVE_SHARED);
        }
        g_step = step < 0 ? -1 : 0;
        return;
    }
    if (!ReadBase()) return;
    int level = Level();
    double t = (double)step / STEPS;
    int ap = level >= 3 ? s_l3Ap : s_l2Ap, demo = level >= 3 ? s_l3Demo : s_l2Demo;
    for (int i = 0; i < 4; i++)
    {
        HfField f = HF_FIELD_AT(DamageInfo, armor_penetration, i);
        /* the angle slots keep their gap to the direct one (4/4/4/4 rises together; 4/4/4/2 keeps its 2 lower) */
        int gap = (int)g_base.ap[0] - (int)g_base.ap[i];
        uint32_t direct = Lerp(g_base.ap[0], ap, t);
        Set(&f, (uint32_t)((int)direct - gap < 0 ? 0 : (int)direct - gap));
    }
    Set(&d, Lerp(g_base.demo, demo, t));
    Set(&s, level >= 3 ? Lerp(g_base.stagger, s_l3Stagger, t) : g_base.stagger);
    Set(&p, level >= 3 ? Lerp(g_base.push, s_l3Push, t) : g_base.push);
    g_step = step;
    g_stepWrites++;
    if (step > g_topStep)
    {
        g_topStep = step;
        g_hf->log(g_mod, "Laser Sentry focus step %d/%d (level %d): AP %u, demolition %u%s", step, STEPS, level, Lerp(g_base.ap[0], ap, t),
            Lerp(g_base.demo, demo, t), level >= 3 ? ", with stagger and push" : "");
    }
}

/* ---- each sentry's focus ------------------------------------------------------------------------------------------ */
typedef struct Focus { uint32_t sentry; double focus, last, updated; } Focus;
static Focus g_focus[6];
static unsigned g_hits, g_boosted, g_noted;
static double g_topFocus;

static void Decay(Focus* f, double now)
{
    double idle = now - f->last - s_grace;
    double since = now - (f->updated > f->last + s_grace ? f->updated : f->last + s_grace);
    if (idle > 0 && since > 0) f->focus = fmax(0, f->focus - since * s_decay);
    f->updated = now;
}

static Focus* FocusOf(uint32_t sentry, double now)
{
    for (int i = 0; i < 6; i++) if (g_focus[i].sentry == sentry) return &g_focus[i];
    int slot = 0;
    for (int i = 1; i < 6; i++) if (g_focus[i].last < g_focus[slot].last) slot = i;
    Focus* f = &g_focus[slot];
    memset(f, 0, sizeof *f);
    f->sentry = sentry;
    f->last = f->updated = now;
    return f;
}

static void OnDamagePre(HfMod* mod, const HfEventInfo* info, void* user)
{
    HfDamagePreEvent* e = (HfDamagePreEvent*)(void*)info;
    HfDamageEvent* d = &e->damage;
    if (d->amount < 0 || e->cancel || Level() < 2) return;
    uint32_t sentry = 0;
    if (gas_dealer_resource(d->dealer, &sentry) != LS) return;
    double now = fp_clock();
    if (g_noted++ < 6) g_hf->log(g_mod, "Laser Sentry hit: %d (kind %u element %u)", d->amount, d->kind, d->element);
    Focus* f = FocusOf(sentry, now);
    double gap = now - f->last;
    if (gap <= s_grace) f->focus = fmin(s_rampSeconds, f->focus + gap);
    else { Decay(f, now); }
    f->last = f->updated = now;
    g_hits++;
    double t = f->focus / s_rampSeconds;
    if (t > g_topFocus) g_topFocus = t;
    double mult = 1.0 + t * ((Level() >= 3 ? s_l3Damage : s_l2Damage) - 1.0);
    if (mult > 1.0 && d->amount > 0)
    {
        double amount = floor(d->amount * mult + 0.5);
        d->amount = amount > 2147483647.0 ? 2147483647 : (int32_t)amount;
        g_boosted++;
    }
}

/* every frame: the focus of sentries that stopped falls; the row follows the most focused one */
void ls_tick(double now)
{
    if (Level() < 2 || !fp_in_mission()) { if (g_step > 0) SetStep(0); return; }
    double top = 0;
    for (int i = 0; i < 6; i++)
    {
        Focus* f = &g_focus[i];
        if (!f->sentry) continue;
        if (now - f->last > s_grace) Decay(f, now);
        if (f->focus <= 0 && now - f->last > 30) { memset(f, 0, sizeof *f); continue; }
        double t = f->focus / s_rampSeconds;
        if (t > top) top = t;
    }
    int step = (int)floor(top * STEPS + 1e-6);
    if (step > STEPS) step = STEPS;
    SetStep(step);
}

static void Apply(void)
{
    int level = Level();
    if (level != g_level)
    {
        if (level > g_level) fp_toast("A/LAS-98 Laser Sentry", level);
        g_level = level;
        g_hf->log(g_mod, "A/LAS-98 Laser Sentry: level %d (%d kills this mission)", level, g_kills);
        if (g_step > 0) { int s = g_step; g_step = 0; SetStep(s); }  /* the new level's values at the same step */
    }
    if (level >= 2 && fp_in_mission()) HeatSink(1);
}

void ls_kills(uint64_t resource, int added)
{
    if (resource != LS) return;
    g_kills += added;
    Apply();
}

void ls_reset(void)
{
    if (g_hits)
        g_hf->log(g_mod, "Laser Sentry this mission: %u beam hits, %u boosted; most focus %.0f%%; highest step %d/%d (%u row changes)", g_hits,
            g_boosted, g_topFocus * 100, g_topStep, STEPS, g_stepWrites);
    SetStep(0);
    if (fp_in_mission()) HeatSink(0);  /* at the next start, when no sentry is out (as the others) */
    g_kills = 0;
    g_level = Level();
    g_hits = g_boosted = g_stepWrites = 0;
    g_topFocus = 0;
    g_topStep = 0;
    memset(g_focus, 0, sizeof g_focus);
    Apply();
}

int ls_init(const HfApi* hf, HfMod* mod)
{
    g_hf = hf;
    g_mod = mod;
    hf->log(mod, "Laser Sentry: level 2 at %d kills, level 3 at %d; full focus in %.1f s of beam; level 2 x%.1f damage, AP %d, demolition %d; "
        "level 3 x%.1f damage, AP %d, demolition %d, stagger %d, push %d (beam damage row %d)", s_l2, s_l3, (double)s_rampSeconds,
        (double)s_l2Damage, s_l2Ap, s_l2Demo, (double)s_l3Damage, s_l3Ap, s_l3Demo, s_l3Stagger, s_l3Push, s_damageId);
    return hf->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0);
}
