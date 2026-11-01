/* Veterancy System: the A/G-16 Gatling Sentry (gatling.c).
 *
 * Its own kills this mission level it (gat_level2_kills, gat_level3_kills; force_level too):
 *   Level 2, "minigun": ammunition for its whole deployment (the magazine at the turret's most, 2047 rounds, and the
 *     weapon's infinite-ammo flag), and focus fire: each hit on the target it is already shooting adds gat_ramp_per_hit
 *     to its damage, up to +gat_ramp_max; a new target (or a pause) starts over. Its armour penetration is the game's own
 *     (AP 3, medium).
 *   Level 3, "Warthog": it fires a round of its own: a copy of the Orbital Gatling Barrage's shell with its bullet's look,
 *     speed and weight (add_projectile) dealing a copy of its damage
 *     (add_damage) at AP gat_ap (4: heavy, not anti-tank), whose hit sets off a high-explosive blast: an ExplosionSettings
 *     row (gat_he_explosion: the Orbital Gatling Barrage's shell, 266) with its own damage, or gat_he_damage when above 0
 *     (which changes that row for everything using it).
 * The magazine and the round are live edits of the sentry's records: they apply from its next call-in (the log says).
 * Its hits are told by their dealer, which the gas code maps from kills (network id -> entity), so focus fire starts
 * once a sentry has made its first kill. */
#include <math.h>
#include <string.h>
#include "hellforge_mod.h"
#include "hellforge_components.h"
#include "hellforge_content.h"
#include "field_promotions.h"

#define GATLING HF_ENTITY_GATLING_SENTRY
#define GAT_BULLET 148u    /* its projectile */
#define GAT_DAMAGE 125u    /* its bullet's damage (90 / 23, AP 3) */

static const HfApi* g_hf;
static HfMod* g_mod;
static int g_kills, g_level = 1, g_ammo, g_round;
static uint32_t g_bullet, g_apBullet, g_bulletDamage, g_heDamage;  /* the mod's own: 0 if refused (g_bullet: the HE round, g_apBullet: AP only) */
static int g_rate;  /* level 3's fire rate edit */
static int g_rowsReady;

typedef struct Ramp { uint32_t sentry, target; int hits; double last; } Ramp;
static Ramp g_ramps[6];
static unsigned g_hits, g_ramped, g_topHits, g_reported;
/* level 3 checks: the sentry's blasts (row gat_he_explosion from a Gatling) against its bullet hits; with none after a
 * few dozen hits the bullet's own blast is not working, and the mod sets the blast off at its hits itself */
static unsigned g_heBlasts, g_l3Hits, g_spawned, g_verified;
static int g_fallback;
static int g_mixByMod;       /* gat_he_every > 1: every round is the AP one and the mod sets off each Nth hit's blast */
static unsigned g_mixCount;
static double g_lastSpawn;
static unsigned g_kinds[10], g_zero;  /* the Gatling's hits by kind (0 bullet, 1 ricochet), and those dealing nothing */
static double g_nextReport;
/* the guard: its blasts do not hurt Gatling Sentries */
typedef struct Blast { double x, y, z, at; } Blast;
static Blast g_blasts[16];   /* the mod's own (fallback) blasts: they carry no dealer */
static unsigned g_blastNext, g_selfCancelled;
static double g_selfSaved;

static int Level(void)
{
    if (fp_forced_level()) return fp_forced_level();
    return g_kills >= fp_gat_level3_kills() ? 3 : g_kills >= fp_gat_level2_kills() ? 2 : 1;
}

/* Level 2: the magazine and the infinite-ammo flag */
int gat_level(void) { return Level(); }

static void SetAmmo(int on)
{
    HfField cap = HF_FIELD(WeaponMagazine, capacity), inf = HF_FIELD(ProjectileWeapon, infinite_ammo);
    if (!on)
    {
        if (g_ammo) { g_hf->set_live(g_mod, GATLING, &cap, NULL, 0); g_hf->set_live(g_mod, GATLING, &inf, NULL, 0); }
        g_ammo = 0;
        return;
    }
    if (g_ammo) return;
    g_ammo = 1;
    HfValue big = HF_U32(2047), yes = HF_U8(1);
    int a = g_hf->set_live(g_mod, GATLING, &cap, &big, 0);
    if (!a) g_hf->log(g_mod, "Gatling level 2: the magazine change was refused (%s)", g_hf->last_log ? g_hf->last_log(g_mod) : "");
    int b = g_hf->set_live(g_mod, GATLING, &inf, &yes, 0);
    if (!b) g_hf->log(g_mod, "Gatling level 2: the infinite-ammo flag was refused (%s)", g_hf->last_log ? g_hf->last_log(g_mod) : "");
    g_hf->log(g_mod, "Gatling level 2: magazine 500 -> 2047 rounds%s, infinite ammo %s (applies %s)", a ? "" : " (refused)",
        b ? "on" : "refused", (a == HF_APPLIES_NOW || b == HF_APPLIES_NOW) ? "now" : "from its next call-in");
}

/* Level 3: the sentry fires the mod's round. The Gatling's magazine picks what it fires (its pattern of rounds and the
 * round chambered first: HellForge's reference), so those change too, as well as the weapon's projectile. */
static uint32_t g_magSlots;   /* pattern slots the mod changed (bit i), bit 31.. via g_magFirst */
static int g_magFirst;
static void SetRound(int on)
{
    HfField proj = HF_FIELD(ProjectileWeapon, projectile), first = HF_FIELD(WeaponMagazine, first_projectile);
    if (!on)
    {
        if (g_round)
        {
            g_hf->set_live(g_mod, GATLING, &proj, NULL, 0);
            for (int i = 0; i < 32; i++)
                if (g_magSlots & (1u << i)) { HfField f = HF_FIELD_AT(WeaponMagazine, pattern, i); g_hf->set_live(g_mod, GATLING, &f, NULL, 0); }
            if (g_magFirst) g_hf->set_live(g_mod, GATLING, &first, NULL, 0);
            if (g_rate) { HfField rate = HF_FIELD_AT(ProjectileWeapon, fire_rate, 1); g_hf->set_live(g_mod, GATLING, &rate, NULL, 0); }
        }
        g_round = 0;
        g_mixByMod = 0;
        g_magSlots = 0;
        g_magFirst = 0;
        g_rate = 0;
        return;
    }
    if (g_round || !g_bullet || !g_rowsReady) return;
    g_round = 1;
    int every = fp_gat_he_every();
    /* One HE round in gat_he_every: the belt (148,148,148,242 tracer,148) never fired an HE round put in one of its slots
     * (slot 0 or slot 4: no blast in a whole mission, where an all-HE belt blasted 400 times), so a mixed belt is every
     * round the AP one and the mod sets off the blast at each gat_he_every-th hit (the fallback's path, proven). */
    g_mixByMod = every > 1 && g_apBullet;
    g_mixCount = 0;
    HfValue v = HF_U32(g_mixByMod ? g_apBullet : g_bullet);
    int when = g_hf->set_live(g_mod, GATLING, &proj, &v, 0);
    int slots = 0, refused = 0, he = 0, nth = 0;
    if (g_mixByMod) every = 1 << 30;  /* no HE slot */
    HfValue vap = HF_U32(g_apBullet ? g_apBullet : g_bullet);
    for (int i = 0; i < 32; i++)
    {
        HfField f = HF_FIELD_AT(WeaponMagazine, pattern, i);
        HfLiveValue cur;
        memset(&cur, 0, sizeof cur);
        if (!g_hf->get_live(g_mod, GATLING, &f, &cur) || (uint32_t)cur.base.number != GAT_BULLET) continue;
        int isHe = (nth++ % every) == every - 1;  /* the last of each group (one in four: slot 4): slot 1 alone never fired its blast */
        if (g_hf->set_live(g_mod, GATLING, &f, isHe ? &v : &vap, 0)) { g_magSlots |= 1u << i; slots++; he += isHe; }
        else refused++;
    }
    /* the Warthog's rate, if set (rounds a minute; from its next call-in) */
    if (fp_gat_l3_rpm() > 0)
    {
        HfField rate = HF_FIELD_AT(ProjectileWeapon, fire_rate, 1);
        HfValue r = HF_F32(fp_gat_l3_rpm());
        g_rate = g_hf->set_live(g_mod, GATLING, &rate, &r, 0) != 0;
        g_hf->log(g_mod, "Gatling level 3: fire rate %s %.0f rounds a minute", g_rate ? "set to" : "refused at", (double)fp_gat_l3_rpm());
    }
    HfLiveValue cf;
    memset(&cf, 0, sizeof cf);
    int firstWas = g_hf->get_live(g_mod, GATLING, &first, &cf) ? (int)cf.base.number : -1;
    if (firstWas == (int)GAT_BULLET && g_hf->set_live(g_mod, GATLING, &first, &v, 0)) g_magFirst = 1;
    if (when) g_hf->log(g_mod, "Gatling level 3: it fires the AP %d high-explosive round (type %u; applies %s); magazine: %d pattern slot%s changed (%d HE)%s, first round %s",
        fp_gat_ap(), g_bullet, when == HF_APPLIES_NOW ? "now" : "from its next call-in", slots, slots == 1 ? "" : "s", he,
        refused ? " (some refused)" : "", g_magFirst ? "changed" : firstWas < 0 ? "unreadable" : "not the bullet");
    else g_hf->log(g_mod, "Gatling level 3: the round change was refused (%s)", g_hf->last_log ? g_hf->last_log(g_mod) : "");
}

/* What the records hold now: the sentry's round and that round's impact blast */
static void Verify(const char* when)
{
    HfField proj = HF_FIELD(ProjectileWeapon, projectile), impact = HF_FIELD(ProjectileSettings, impact_explosion);
    HfLiveValue a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    int ra = g_hf->get_live(g_mod, GATLING, &proj, &a);
    int rb = g_bullet ? g_hf->get_live(g_mod, g_bullet, &impact, &b) : 0;
    HfField dmgOf = HF_FIELD(ProjectileSettings, damage);
    HfLiveValue c;
    memset(&c, 0, sizeof c);
    int rc = g_bullet ? g_hf->get_live(g_mod, g_bullet, &dmgOf, &c) : 0;
    double apNow[4] = {-1, -1, -1, -1};
    for (int i = 0; i < 4 && rc; i++)
    {
        HfField f = HF_FIELD_AT(DamageInfo, armor_penetration, i);
        HfLiveValue v;
        memset(&v, 0, sizeof v);
        if (g_hf->get_live(g_mod, (uint32_t)c.now.number, &f, &v)) apNow[i] = v.now.number;
    }
    HfField p0 = HF_FIELD_AT(WeaponMagazine, pattern, 0), fp = HF_FIELD(WeaponMagazine, first_projectile);
    HfLiveValue m0, m1;
    memset(&m0, 0, sizeof m0);
    memset(&m1, 0, sizeof m1);
    int r0 = g_hf->get_live(g_mod, GATLING, &p0, &m0), r1 = g_hf->get_live(g_mod, GATLING, &fp, &m1);
    g_hf->log(g_mod, "Gatling check (%s): the sentry's round %s %.0f (base %.0f); magazine round 1 %.0f%s, first %.0f%s; the mod round's impact blast %s %.0f, its damage id %.0f, AP %.0f/%.0f/%.0f/%.0f",
        when, ra ? "is" : "unreadable,", a.now.number, a.base.number, m0.now.number, r0 ? "" : " (unreadable)", m1.now.number, r1 ? "" : " (unreadable)",
        rb ? "is" : "unreadable,", b.now.number, c.now.number, apNow[0], apNow[1], apNow[2], apNow[3]);
}

static void Apply(void)
{
    int level = Level();
    if (level != g_level)
    {
        if (level > g_level) fp_toast("A/G-16 Gatling Sentry", level);
        g_level = level;
        g_hf->log(g_mod, "A/G-16 Gatling Sentry: level %d (%d kills this mission)", level, g_kills);
    }
    if (!fp_in_mission()) return;
    if (level >= 2) SetAmmo(1);
    if (level >= 3 && !g_round) { SetRound(1); Verify("after the change"); }
}

void gat_kills(uint64_t resource, int added)
{
    if (resource != GATLING) return;
    g_kills += added;
    Apply();
}

void gat_reset(void)
{
    if (g_hits) g_hf->log(g_mod, "Gatling this mission: %u hits told apart, %u with focus fire (longest streak %u hits)", g_hits, g_ramped, g_topHits);
    g_kills = 0;
    g_level = Level();
    if (g_l3Hits || g_heBlasts || g_spawned)
        g_hf->log(g_mod, "Gatling level 3 this mission: %u bullet hits, %u blasts from its round, %u blasts set off by the mod%s", g_l3Hits, g_heBlasts,
            g_spawned, g_mixByMod ? " (the mixed belt's: one in gat_he_every hits)" : g_fallback ? " (fallback on)" : "");
    if (g_kinds[0] || g_kinds[1])
        g_hf->log(g_mod, "Gatling hits this mission: %u bullet, %u ricochet, %u of them dealing nothing", g_kinds[0], g_kinds[1], g_zero);
    if (g_selfCancelled)
        g_hf->log(g_mod, "Gatling Sentry this mission: %u hits from its own blasts cancelled (%.0f damage)", g_selfCancelled, g_selfSaved);
    g_selfCancelled = 0;
    g_selfSaved = 0;
    memset(g_blasts, 0, sizeof g_blasts);
    memset(g_kinds, 0, sizeof g_kinds);
    g_zero = 0;
    g_hits = g_ramped = g_topHits = g_reported = 0;
    g_heBlasts = g_l3Hits = g_spawned = g_verified = 0;
    g_fallback = 0;
    memset(g_ramps, 0, sizeof g_ramps);
    if (fp_in_mission()) { SetAmmo(0); SetRound(0); }  /* at the next start, not the end: see ResetMission */
    Apply();  /* a forced level */
}

/* A hit on a Gatling Sentry from a Gatling's blast: dealt by a Gatling Sentry (its round's own blast), or near one of
 * the mod's blasts of the last second (those carry no dealer). Explosions only: one sentry's bullets still hit another. */
static int OwnBlast(const HfDamageEvent* d, double now)
{
    if (d->kind != 4 || gas_entity_resource(d->target) != GATLING) return 0;
    if (gas_dealer_resource(d->dealer, NULL) == GATLING || gas_dealer_resource(d->owner, NULL) == GATLING) return 1;
    for (int i = 0; i < 16; i++)
    {
        const Blast* b = &g_blasts[i];
        if (b->at <= 0 || now - b->at > 1.0) continue;
        double dx = d->position[0] - b->x, dy = d->position[1] - b->y, dz = d->position[2] - b->z;
        if (dx * dx + dy * dy + dz * dz <= 10.0 * 10.0 && !gas_dealer_resource(d->dealer, NULL)) return 1;
    }
    return 0;
}

/* Focus fire */
static void OnDamagePre(HfMod* mod, const HfEventInfo* info, void* user)
{
    HfDamagePreEvent* e = (HfDamagePreEvent*)(void*)info;
    HfDamageEvent* d = &e->damage;
    if (fp_gat_self_immune() && d->amount > 0 && !e->cancel && Level() >= 3 && OwnBlast(d, fp_clock()))
    {
        e->cancel = 1;
        g_selfCancelled++;
        g_selfSaved += d->amount;
        return;
    }
    /* bullets (0) and ricochets (1): a deflected round still sets its high-explosive blast off where it struck */
    if ((d->kind != 0 && d->kind != 1) || d->amount < 0 || Level() < 2) return;
    uint32_t sentry = 0;
    if (gas_dealer_resource(d->dealer, &sentry) != GATLING) return;
    double now = fp_clock();
    g_kinds[d->kind]++;
    if (d->amount == 0) g_zero++;
    if (Level() >= 3 && fp_gat_he_explosion())
    {
        g_l3Hits++;
        if (g_verified < 2 && (g_l3Hits == 1 || g_l3Hits == 200)) { g_verified++; Verify(g_l3Hits == 1 ? "first hit" : "200th hit"); }
        if (g_mixByMod && (++g_mixCount % (unsigned)fp_gat_he_every()) == 0 && now - g_lastSpawn >= 1.0 / fp_gat_he_rate())
        {
            g_lastSpawn = now;
            if (g_hf->spawn_explosion(g_mod, (uint32_t)fp_gat_he_explosion(), d->position) == HF_ACT_REQUESTED)
            {
                g_spawned++;
                Blast* b = &g_blasts[g_blastNext++ % 16];
                b->x = d->position[0]; b->y = d->position[1]; b->z = d->position[2]; b->at = now;
            }
        }
        if (!g_mixByMod && !g_fallback && g_l3Hits >= 40 && g_heBlasts == 0)
        {
            g_fallback = 1;
            g_hf->log(g_mod, "Gatling level 3: 40 hits and no blast from its round: the mod sets the blast off at its hits instead");
        }
        /* the fallback: the blast where the bullet struck, at most gat_he_rate a second */
        if (!g_mixByMod && g_fallback && now - g_lastSpawn >= 1.0 / fp_gat_he_rate())
        {
            g_lastSpawn = now;
            if (g_hf->spawn_explosion(g_mod, (uint32_t)fp_gat_he_explosion(), d->position) == HF_ACT_REQUESTED)
            {
                g_spawned++;
                Blast* b = &g_blasts[g_blastNext++ % 16];
                b->x = d->position[0]; b->y = d->position[1]; b->z = d->position[2]; b->at = now;
            }
        }
    }
    if (d->kind != 0 || d->amount <= 0) return;  /* focus fire: damaging bullet hits only */
    g_hits++;
    Ramp* r = NULL;
    for (int i = 0; i < 6 && !r; i++) if (g_ramps[i].sentry == sentry) r = &g_ramps[i];
    for (int i = 0; i < 6 && !r; i++) if (!g_ramps[i].sentry) { r = &g_ramps[i]; r->sentry = sentry; }
    if (!r) { memmove(g_ramps, g_ramps + 1, sizeof g_ramps - sizeof g_ramps[0]); r = &g_ramps[5]; memset(r, 0, sizeof *r); r->sentry = sentry; }
    if (r->target != d->target || now - r->last > 0.75) { r->target = d->target; r->hits = 0; }
    r->last = now;
    double bonus = r->hits * fp_gat_ramp_per_hit();
    if (bonus > fp_gat_ramp_max()) bonus = fp_gat_ramp_max();
    r->hits++;
    if ((unsigned)r->hits > g_topHits) g_topHits = (unsigned)r->hits;
    if (bonus > 0)
    {
        double amount = floor(d->amount * (1.0 + bonus) + 0.5);
        d->amount = amount > 2147483647.0 ? 2147483647 : (int32_t)amount;
        g_ramped++;
    }
    if (now >= g_nextReport && g_reported < 30)
    {
        g_nextReport = now + 60;
        g_reported++;
        g_hf->log(g_mod, "Gatling focus fire: %u hits told apart, %u ramped, longest streak %u", g_hits, g_ramped, g_topHits);
    }
}

static void OnExplosion(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfExplosionEvent* e = (const HfExplosionEvent*)info;
    if (!fp_gat_he_explosion() || e->type != (uint32_t)fp_gat_he_explosion()) return;
    if (gas_entity_resource(e->source) == GATLING || gas_entity_resource(e->owner) == GATLING)
    {
        if (!g_heBlasts) g_hf->log(g_mod, "Gatling level 3: its round's blast works (row %u)", e->type);
        g_heBlasts++;
    }
}

/* The mod's rows, once the game has its projectiles: the round's armour penetration, the blast's damage, the round's
 * impact blast (a live edit, which loads the blast's packages) and the blast row's damage type. */
static void RowsReady(HfMod* mod, HfEvent event, void* user)
{
    HfDamageInfo* bd = g_bulletDamage ? (HfDamageInfo*)g_hf->damage_row(g_bulletDamage) : NULL;
    HfDamageInfo* hd = g_heDamage ? (HfDamageInfo*)g_hf->damage_row(g_heDamage) : NULL;
    if (!bd || !hd || !g_bullet || !g_hf->projectile_row(g_bullet))
    {
        g_hf->log(g_mod, "Gatling level 3: the mod's round or damage rows are not there; level 3 keeps the vanilla round");
        g_bullet = 0;
        return;
    }
    if (g_apBullet && !g_hf->projectile_row(g_apBullet)) { g_hf->log(g_mod, "Gatling level 3: the AP-only round is not there; every round is HE"); g_apBullet = 0; }
    HfProjectileSettings* pr = (HfProjectileSettings*)g_hf->projectile_row(g_bullet);
    if ((uint32_t)fp_gat_round_base() != GAT_BULLET) { pr->velocity = 820.0f; pr->mass = 11.0f; }
    /* the round's armour penetration, as live edits (kept if the game reloads its tables): every angle at gat_ap but the
     * most glancing, two lower (the game's heavy machine gun: 4, 4, 4, 0) */
    uint32_t ap = (uint32_t)fp_gat_ap();
    int apOk = 0;
    for (int i = 0; i < 4; i++)
    {
        HfField f = HF_FIELD_AT(DamageInfo, armor_penetration, i);
        HfValue v = HF_U32(i < 3 ? ap : (ap > 2 ? ap - 2 : ap));
        if (g_hf->set_live(g_mod, g_bulletDamage, &f, &v, HF_LIVE_SHARED)) apOk++;
        else g_hf->log(g_mod, "Gatling level 3: armour penetration slot %d refused (%s)", i, g_hf->last_log ? g_hf->last_log(g_mod) : "");
    }
    if (fp_gat_he_damage() > 0)
    {
        HfField sd = HF_FIELD(DamageInfo, standard_damage), dd = HF_FIELD(DamageInfo, durable_damage);
        HfValue v = HF_I32(fp_gat_he_damage());
        g_hf->set_live(g_mod, g_heDamage, &sd, &v, HF_LIVE_SHARED);
        g_hf->set_live(g_mod, g_heDamage, &dd, &v, HF_LIVE_SHARED);
    }
    uint32_t row = (uint32_t)fp_gat_he_explosion();
    HfField impact = HF_FIELD(ProjectileSettings, impact_explosion), dmg = HF_FIELD(ExplosionSettings, damage);
    HfValue vr = HF_U32(row), vd = HF_U32(g_heDamage);
    int a = row ? g_hf->set_live(g_mod, g_bullet, &impact, &vr, 0) : 0;
    /* gat_he_damage 0: the row keeps its own damage (the Orbital Gatling Barrage's: 350, AP 3) and stays untouched */
    int b = !row ? 0 : fp_gat_he_damage() > 0 ? g_hf->set_live(g_mod, row, &dmg, &vd, HF_LIVE_SHARED) : -1;
    g_rowsReady = 1;
    if (b < 0) g_hf->log(g_mod, "Gatling level 3 round ready: type %u (copy of %u, %.0f m/s, %.0f g), AP %u, %d/%d per bullet; impact blast row %u %s, with the row's own damage",
        g_bullet, (uint32_t)fp_gat_round_base(), (double)pr->velocity, (double)pr->mass, ap, bd->standard_damage, bd->durable_damage, row, a ? "set" : "REFUSED");
    else g_hf->log(g_mod, "Gatling level 3 round ready: type %u, AP %u, %d/%d per bullet; impact blast row %u %s, %d damage %s", g_bullet, ap,
        bd->standard_damage, bd->durable_damage, row, a ? "set" : "REFUSED", (int)fp_gat_he_damage(), b ? "set" : "REFUSED");
    if (!a || !b) g_hf->log(g_mod, "  (%s)", g_hf->last_log ? g_hf->last_log(g_mod) : "");
    g_hf->log(g_mod, "Gatling level 3: armour penetration set in %d of 4 slots", apOk);
    Apply();
}

int gat_init(const HfApi* hf, HfMod* mod)
{
    g_hf = hf;
    g_mod = mod;
    HfDamage bullet = {sizeof bullet};
    bullet.base = GAT_DAMAGE;
    bullet.name = "yui/field_promotions/gatling_ap_round";
    g_bulletDamage = hf->add_damage(mod, &bullet);
    HfDamage he = {sizeof he};
    he.base = GAT_DAMAGE;
    he.name = "yui/field_promotions/gatling_he_blast";
    g_heDamage = hf->add_damage(mod, &he);
    if (g_bulletDamage && g_heDamage)
    {
        HfProjectile round = {sizeof round};
        /* a copy of the sentry's own bullet (gat_round_base 148: its blast never goes off, so the mod's fallback sets it
         * off at its hits), or of the orbital's explosive shell (77: with the bullet's look, speed and weight; a launch
         * with it crashed once, so it is opt-in) */
        round.base = (uint32_t)fp_gat_round_base();
        round.name = "yui/field_promotions/gatling_he_round";
        round.damage = g_bulletDamage;
        if (round.base != GAT_BULLET) round.visual_from = GAT_BULLET;
        g_bullet = hf->add_projectile(mod, &round);
        /* the belt's other rounds: the same AP damage, no blast (a plain copy of the bullet) */
        HfProjectile plain = {sizeof plain};
        plain.base = GAT_BULLET;
        plain.name = "yui/field_promotions/gatling_ap_shot";
        plain.damage = g_bulletDamage;
        g_apBullet = hf->add_projectile(mod, &plain);
    }
    if (!g_bullet) hf->log(mod, "Gatling level 3: the mod's round could not be added (level 3 keeps the vanilla round)");
    else hf->on(mod, HF_EVENT_PROJECTILES_READY, RowsReady, NULL);
    hf->log(mod, "Gatling Sentry: level 2 at %d kills, level 3 at %d; focus fire +%.0f%% a hit up to +%.0f%%; level 3 AP %d, HE blast row %d (%s)",
        fp_gat_level2_kills(), fp_gat_level3_kills(), fp_gat_ramp_per_hit() * 100, fp_gat_ramp_max() * 100, fp_gat_ap(), fp_gat_he_explosion(),
        fp_gat_he_damage() > 0 ? "the mod's damage" : "its own damage");
    return hf->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) && hf->subscribe(mod, HF_ON_EXPLOSION, OnExplosion, NULL, 0);
}
