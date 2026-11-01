/* Veterancy System: the A/MG-43 Machine Gun Sentry (mg_sentry.c), the one that is always there.
 *
 * Its own kills this mission level it (mg_level2_kills, mg_level3_kills; force_level too). Its gun stays as it is:
 *   Level 2, "Dug in": every hit on it is mg_damage_taken times (0.5), hits from Helldivers (any player's: their
 *     grenades, strikes, stray fire, the mod's own shells) do nothing to it (mg_friendly_immune), and its magazine
 *     lasts its whole deployment (2047 rounds and the weapon's infinite-ammo flag; from its next call-in).
 *   Level 3, "Bastion": a shield on top, per sentry, that counts hits, not damage: it stops the next mg_shield_hits
 *     hits whatever they deal (a Charger's ram, a killing blow), and gets one back every mg_shield_recharge seconds
 *     once mg_shield_delay seconds have passed without a hit (hits under mg_shield_min_hit pass it, halved, and do not
 *     use it; hits within 0.3 s of the one that used a charge are the same blow, and stopped with it); and it stays
 *     mg_lifetime_mult times as long (from its next call-in).
 * Hits on it are told by their target's entity resource (the spawn table the gas code keeps); a hit's amount is before
 * its armour, so the shield counts raw damage. Hits from Helldivers: a hit credited to a player, or dealt (or owned)
 * by a Helldiver. */
#include <math.h>
#include <string.h>
#include "hellforge_mod.h"
#include "hellforge_components.h"
#include "hellforge_content.h"
#include "field_promotions.h"

#define MG HF_ENTITY_AMG43_MACHINE_GUN_SENTRY
#define HELLDIVER 0x4D1C334D294DFA97ull

static const HfApi* g_hf;
static HfMod* g_mod;
static int g_kills, g_level = 1, g_ammo, g_life;
typedef struct Shield { uint32_t entity; double charges, last, blowUntil; } Shield;
#define BLOW 0.3  /* hits on it this close together are one blow (a Charger's ram strikes several of its parts at once) */
static Shield g_shields[8];
static unsigned g_friendly, g_reduced, g_absorbedHits, g_killsStopped, g_breaks, g_blowParts, g_bigNoted, g_deaths;
static double g_absorbed, g_saved;

static int Level(void)
{
    if (fp_forced_level()) return fp_forced_level();
    return g_kills >= fp_mg_level3_kills() ? 3 : g_kills >= fp_mg_level2_kills() ? 2 : 1;
}

int mg_level(void) { return Level(); }

static void SetAmmo(int on)
{
    HfField cap = HF_FIELD(WeaponMagazine, capacity), inf = HF_FIELD(ProjectileWeapon, infinite_ammo);
    if (!on)
    {
        if (g_ammo) { g_hf->set_live(g_mod, MG, &cap, NULL, 0); g_hf->set_live(g_mod, MG, &inf, NULL, 0); }
        g_ammo = 0;
        return;
    }
    if (g_ammo) return;
    g_ammo = 1;
    HfValue big = HF_U32(2047), yes = HF_U8(1);
    int a = g_hf->set_live(g_mod, MG, &cap, &big, 0);
    int b = g_hf->set_live(g_mod, MG, &inf, &yes, 0);
    g_hf->log(g_mod, "Machine Gun Sentry level 2: magazine 175 -> 2047 rounds %s, infinite ammo %s", a ? "set" : "REFUSED", b ? "on" : "REFUSED");
    if (!a || !b) g_hf->log(g_mod, "  (%s)", g_hf->last_log ? g_hf->last_log(g_mod) : "");
}

static void SetLifetime(int on)
{
    HfField life = HF_FIELD(HellpodPayload, lifetime);
    if (!on)
    {
        if (g_life) g_hf->set_live(g_mod, MG, &life, NULL, 0);
        g_life = 0;
        return;
    }
    if (g_life || fp_mg_lifetime_mult() == 1.0f) return;
    g_life = 1;
    HfLiveValue v;
    memset(&v, 0, sizeof v);
    if (!g_hf->get_live(g_mod, MG, &life, &v)) { g_hf->log(g_mod, "Machine Gun Sentry level 3: its lifetime cannot be read"); return; }
    HfValue nv = HF_F32(v.base.number * fp_mg_lifetime_mult());
    int when = g_hf->set_live(g_mod, MG, &life, &nv, 0);
    g_hf->log(g_mod, "Machine Gun Sentry level 3: lifetime %.0f -> %.0f s %s", v.base.number, nv.number,
        when == HF_APPLIES_NOW ? "(now)" : when ? "(from its next call-in)" : "REFUSED");
}

static void Apply(void)
{
    int level = Level();
    if (level != g_level)
    {
        if (level > g_level) fp_toast("A/MG-43 Machine Gun Sentry", level);
        g_level = level;
        g_hf->log(g_mod, "A/MG-43 Machine Gun Sentry: level %d (%d kills this mission)", level, g_kills);
    }
    if (!fp_in_mission()) return;
    if (level >= 2) SetAmmo(1);
    if (level >= 3) SetLifetime(1);
}

void mg_kills(uint64_t resource, int added)
{
    if (resource != MG) return;
    g_kills += added;
    Apply();
}

void mg_reset(void)
{
    if (g_friendly || g_reduced || g_absorbedHits || g_deaths)
        g_hf->log(g_mod, "Machine Gun Sentry this mission: %u friendly hits cancelled, %u hits reduced (%.0f damage saved), shield stopped %u hits "
            "(and %u more parts of the same blows; %.0f damage, %u killing blows), broken %u times; %u destroyed", g_friendly, g_reduced, g_saved,
            g_absorbedHits, g_blowParts, g_absorbed, g_killsStopped, g_breaks, g_deaths);
    g_kills = 0;
    g_level = Level();
    g_friendly = g_reduced = g_absorbedHits = g_killsStopped = g_breaks = g_blowParts = g_bigNoted = g_deaths = 0;
    g_absorbed = g_saved = 0;
    memset(g_shields, 0, sizeof g_shields);
    if (fp_in_mission()) { SetAmmo(0); SetLifetime(0); }  /* at the next start, when no sentry is out (as the others) */
    Apply();
}

/* The shield of a sentry: full when first hit, then a charge back per mg_shield_recharge s past the delay. */
static Shield* ShieldOf(uint32_t entity, double now)
{
    Shield* s = NULL;
    for (int i = 0; i < 8 && !s; i++) if (g_shields[i].entity == entity) s = &g_shields[i];
    if (!s)
    {
        int slot = 0;
        for (int i = 1; i < 8; i++) if (g_shields[i].last < g_shields[slot].last) slot = i;
        s = &g_shields[slot];
        s->entity = entity;
        s->charges = fp_mg_shield_hits();
        s->last = now;
        return s;
    }
    double idle = now - s->last - fp_mg_shield_delay();
    if (idle > 0) s->charges = fmin(fp_mg_shield_hits(), s->charges + idle / fp_mg_shield_recharge());
    return s;
}

static void OnDamagePre(HfMod* mod, const HfEventInfo* info, void* user)
{
    HfDamagePreEvent* e = (HfDamagePreEvent*)(void*)info;
    HfDamageEvent* d = &e->damage;
    if (d->amount == 0 || e->cancel) return;  /* -1: a killing blow (a Charger's ram, demolition) */
    int level = Level();
    if (level < 2 || gas_entity_resource(d->target) != MG) return;
    double now = fp_clock();
    static unsigned noted;  /* the first hits on it, to see what counts as a Helldiver's */
    if (noted++ < 8)
        g_hf->log(g_mod, "Machine Gun Sentry hit: %d (kind %u element %u), creditor %llu, dealer %016llX, owner %016llX", d->amount, d->kind,
            d->element, (unsigned long long)d->creditor, (unsigned long long)gas_dealer_resource(d->dealer, NULL),
            (unsigned long long)gas_dealer_resource(d->owner, NULL));
    if ((d->amount < 0 || d->amount >= 150) && g_bigNoted++ < 20)
    {
        double left = -1;
        for (int i = 0; i < 8; i++) if (g_shields[i].entity == d->target) left = g_shields[i].charges;
        g_hf->log(g_mod, "Machine Gun Sentry big hit: %d (kind %u element %u) from %016llX, level %d, shield %.1f", d->amount, d->kind, d->element,
            (unsigned long long)gas_dealer_resource(d->dealer, NULL), level, left);
    }
    /* Helldivers' hits: credited to a player, or dealt or owned by a Helldiver */
    if (fp_mg_friendly_immune() &&
        (d->creditor || gas_dealer_resource(d->dealer, NULL) == HELLDIVER || gas_dealer_resource(d->owner, NULL) == HELLDIVER))
    {
        e->cancel = 1;
        g_friendly++;
        return;
    }
    /* level 3: the shield stops whole hits, a killing blow too */
    if (level >= 3 && fp_mg_shield_hits() > 0 && (d->amount < 0 || d->amount >= fp_mg_shield_min_hit()))
    {
        Shield* s = ShieldOf(d->target, now);
        s->last = now;
        if (now < s->blowUntil)  /* the rest of a blow the shield took */
        {
            e->cancel = 1;
            g_blowParts++;
            if (d->amount < 0) g_killsStopped++; else g_absorbed += d->amount;
            return;
        }
        if (s->charges >= 1)
        {
            s->charges -= 1;
            s->blowUntil = now + BLOW;
            e->cancel = 1;
            g_absorbedHits++;
            if (d->amount < 0) g_killsStopped++; else g_absorbed += d->amount;
            if (s->charges < 1) g_breaks++;
            return;
        }
    }
    if (d->amount < 0) return;  /* no shield left: a killing blow kills */
    double amount = d->amount * fp_mg_damage_taken();
    g_saved += d->amount - amount;
    g_reduced++;
    if (amount < 0.5) { e->cancel = 1; return; }
    d->amount = (int32_t)floor(amount + 0.5);
}

/* How one died: what killed it, and its shield then */
static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfKillEvent* k = (const HfKillEvent*)info;
    if (gas_entity_resource(k->victim) != MG) return;
    g_deaths++;
    if (g_deaths > 10) return;
    double left = -1;
    for (int i = 0; i < 8; i++) if (g_shields[i].entity == k->victim) left = g_shields[i].charges;
    g_hf->log(g_mod, "Machine Gun Sentry destroyed (kind %u element %u) by %016llX, level %d, shield %.1f", k->kind, k->element,
        (unsigned long long)(k->dealer == 0xFFFFFFFFu ? 0 : gas_entity_resource(k->dealer)), Level(), left);
}

int mg_init(const HfApi* hf, HfMod* mod)
{
    g_hf = hf;
    g_mod = mod;
    hf->log(mod, "Machine Gun Sentry: level 2 at %d kills, level 3 at %d; takes x%.2f, friendly fire %s; shield of %d hits (one back "
        "every %.1f s after %.1f s; hits under %d pass), lifetime x%.2f", fp_mg_level2_kills(), fp_mg_level3_kills(), (double)fp_mg_damage_taken(),
        fp_mg_friendly_immune() ? "ignored" : "taken", fp_mg_shield_hits(), (double)fp_mg_shield_recharge(), (double)fp_mg_shield_delay(),
        fp_mg_shield_min_hit(), (double)fp_mg_lifetime_mult());
    return hf->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) && hf->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0);
}
