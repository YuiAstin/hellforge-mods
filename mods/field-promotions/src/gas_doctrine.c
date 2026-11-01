/* Veterancy System: the gas doctrine, a level track of its own shared by every gas armament.
 *
 * Every kill by any gas armament this mission counts toward it (`gas_level2_kills`, `gas_level3_kills`), apart from the
 * armaments' own levels. At level 3, enemies confused by gas take `gas_taken_mult` times the damage, and deal
 * `gas_dealt_mult` times theirs to other enemies (to Helldivers too when `gas_dealt_vs_players` is 1). At level 2 the gas
 * stratagems' cooldowns are `gas_cooldown_mult` times theirs (a live edit of their StratagemDefinition rows, undone at
 * the mission's end).
 *
 * The Orbital Gas Strike and the Eagle Gas Airstrike have no entity resource in the SDK: the game credits their kills to
 * a source of its own, learned here. When their beacon activates (HF_ON_STRATAGEM), a window opens: the first kill from a
 * source never seen before in this session is that strike's. The log prints it; `orbital_gas_source` and
 * `eagle_gas_source` in the ini skip learning.
 *
 * "Confused": the SDK cannot read an entity's statuses, so an enemy counts as confused for `confusion_seconds` after its
 * last gas hit. A gas hit is a hit whose damage element is the gas element: `gas_element` in the ini, or (-1) learned
 * from the first hit dealt by a known gas armament. Until it is known, the log lists the elements it sees.
 *
 * Damage hooks see what this machine simulates: solo and host. */
#include <ctype.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "hellforge_mod.h"
#include "hellforge_content.h"
#include "field_promotions.h"

#define HELLDIVER 0x4D1C334D294DFA97ull       /* the Helldiver's entity resource (the mission stats' own source) */
#define SLOTS 65536                           /* entity table (open addressing) */

typedef struct Gear { const char* name; uint64_t resource; int kills, level; uint32_t stratagem; double learnUntil; } Gear;
static Gear g_gear[] = {
    {"A/GM-17 Gas Mortar Sentry", HF_ENTITY_AGM17_GAS_MORTAR_SENTRY, 0, 1, 0, 0},
    {"G-4 Gas", HF_ENTITY_G4_GAS, 0, 1, 0, 0},
    {"TX-41 Sterilizer", HF_ENTITY_TX41_STERILIZER, 0, 1, 0, 0},
    {"AX/TX-13 Dog Breath", HF_ENTITY_AXTX13_DOG_BREATH, 0, 1, 0, 0},
    {"MD-8 Gas Mines", HF_ENTITY_MD8_GAS_MINES, 0, 1, 0, 0},
    {"Orbital Gas Strike", 0, 0, 1, HF_STRATAGEM_ORBITAL_GAS_STRIKE, 0},  /* resource learned */
    {"Eagle Gas Airstrike", 0, 0, 1, HF_STRATAGEM_EAGLE_GAS_AIRSTRIKE, 0},
};
#define LEARN_WINDOW 25.0
#define GAS_MORTAR (&g_gear[0])
static void SetGasMortarAmmo(int on);
static int GasMortarLevel(void);
static int g_doctrineKills, g_doctrineLevel = 1;
static uint64_t g_known[256];                 /* kill sources seen this session: never a strike's */
static int g_nknown;
#define NGEAR ((int)(sizeof g_gear / sizeof g_gear[0]))

typedef struct Ent { uint32_t key; uint32_t network; uint64_t resource; double confusedUntil; uint32_t lastDealer, ambiguous; uint64_t lastUpdate; double soaked; int gassed; float pos[3]; int stacks; double lastStack, lastInGas; } Ent;  /* key: entity + 1 */
static Ent g_ents[SLOTS];
static uint32_t g_netToEntity[0x8000];        /* network id -> entity + 1 */

static const HfApi* g_hf;
static HfMod* g_mod;
static int g_element = -1, g_learned, g_active, g_loggedElements;
static uint32_t g_seen[32];
/* diagnostics, logged every minute while active: hits, how many named their target and dealer, the boosts given */
static unsigned g_hits, g_targetKnown, g_dealerKnown, g_dealerNone, g_gasHits, g_boostTaken, g_boostDealt;
static double g_nextReport;
static uint32_t g_rawLogged;

/* Enemies gas does not confuse in the game: never marked, so they neither take nor deal the level 3 boost. Built in: every
 * variant and separately hit part (turrets, cannons, gatlings) of the Bile Titan, Factory Strider, War Strider, the
 * tanks and the Harvester (resources as Smarter Guard Dogs & Sentries' armour table lists them, which match the SDK's).
 * gas_immune in the ini adds more. */
typedef struct Named { uint64_t r; const char* n; } Named;
static const Named kImmune[] = {
    {0x9E2E17F2CCCCAFDDull, "Bile Titan"}, {0xEF04CB84D097A497ull, "Spore Burst Bile Titan"},
    {0x8093204AEA733DAAull, "Factory Strider"}, {0xD37E8D120D2836E3ull, "Factory Strider"}, {0x30F2DEE2333F227Aull, "Factory Strider"},
    {0x843D18D4B5512B63ull, "Factory Strider cannon"}, {0x390E6B3D2F26BD70ull, "Factory Strider gatling"},
    {0x1C6981CA2FB69508ull, "Factory Strider gatling"}, {0x3AAFBA89B53B0F63ull, "Factory Strider gatling"},
    {0x6A7A54D5FB3A6D17ull, "Factory Strider gatling"}, {0x48C91E42B9F16512ull, "Factory Strider gatling"},
    {0xEF570293245A17C2ull, "War Strider"}, {0x8372619B2702D743ull, "War Strider cannon"}, {0x9AB036439F74C115ull, "War Strider cannon"},
    {0x00BCF8247A544CDCull, "Tank hull"}, {0x16023E554FEE830Eull, "Tank hull MG"}, {0x0DE06D0359CE6B1Cull, "Tank (mortar)"},
    {0xB64245D92DD8FA1Aull, "Tank (MG)"}, {0xC6449FFD9EA3779Cull, "Shredder Tank"}, {0x49674794A165EDA8ull, "Shredder Tank"},
    {0x63DF3D07B7424588ull, "Barrager Tank"}, {0x5D142C3A73EBC634ull, "Barrager Tank turret"},
    {0xF75A20CA493F7407ull, "Annihilator Tank"}, {0x36390775D61159EBull, "Annihilator Tank"}, {0x53D8919D7B8ABD67ull, "Annihilator Tank"},
    {0x31BAE74D2F064D8Dull, "Annihilator Tank"}, {0xCECD6979CF7BB0F2ull, "Annihilator turret"}, {0x8FB47DD28B398CF7ull, "Annihilator turret"},
    {0xB8580CAC32214FC0ull, "Annihilator turret"},
    {0x965EAE5A51ACDD4Aull, "Harvester"}, {0xD63FCBFF0851B7AFull, "Harvester"}};
#define NIMMUNE ((int)(sizeof kImmune / sizeof kImmune[0]))
static uint64_t g_immune[64];  /* the ini's */
static int g_nimmune;
static int Immune(uint64_t r)
{
    for (int i = 0; i < NIMMUNE; i++) if (kImmune[i].r == r) return 1;
    for (int i = 0; i < g_nimmune; i++) if (g_immune[i] == r) return 1;
    return 0;
}
void gas_set_immune(const char* list)
{
    while (list && *list)
    {
        char* end;
        while (*list && !isxdigit((unsigned char)*list)) list++;
        if (!*list) break;
        uint64_t r = strtoull(list, &end, 16);
        if (end == list) break;
        list = end;
        if (r && !Immune(r) && g_nimmune < (int)(sizeof g_immune / sizeof g_immune[0])) g_immune[g_nimmune++] = r;
    }
}

/* The mission's gassed enemy types, logged at its end: how to find an immune enemy's resource for gas_immune. */
typedef struct Kind { uint64_t resource; unsigned gasHits, kills; double soaked; } Kind;
static Kind g_kinds[64];
static int g_nkinds;
static Kind* KindOf(uint64_t r)
{
    for (int i = 0; i < g_nkinds; i++) if (g_kinds[i].resource == r) return &g_kinds[i];
    if (g_nkinds >= (int)(sizeof g_kinds / sizeof g_kinds[0])) return NULL;
    g_kinds[g_nkinds].resource = r;
    return &g_kinds[g_nkinds++];
}
static const char* KindName(uint64_t r)
{
    static const struct { uint64_t r; const char* n; } k[] = {
        {HF_ENTITY_HULK, "Hulk"}, {HF_ENTITY_HULK_FIREBOMBER, "Hulk Firebomber"}, {HF_ENTITY_HULK_OBLITERATOR, "Hulk Obliterator"},
        {HF_ENTITY_CHARGER, "Charger"}, {HF_ENTITY_RUPTURE_CHARGER, "Rupture Charger"}, {HF_ENTITY_SPORE_CHARGER, "Spore Charger"},
        {HF_ENTITY_SCOUT_STRIDER, "Scout Strider"}, {HF_ENTITY_REINFORCED_SCOUT_STRIDER, "Reinforced Scout Strider"},
        {HF_ENTITY_HIVE_GUARD, "Hive Guard"}, {HF_ENTITY_HIVE_LORD, "Hive Lord"}, {HF_ENTITY_DRAGONROACH, "Dragonroach"},
        {HF_ENTITY_FLESHMOB, "Fleshmob"}, {HF_ENTITY_CRESCENT_OVERSEER, "Crescent Overseer"}, {HF_ENTITY_ELEVATED_OVERSEER, "Elevated Overseer"},
        {HF_ENTITY_LEVIATHAN, "Leviathan"}, {HF_ENTITY_STINGRAY, "Stingray"}};
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++) if (k[i].r == r) return k[i].n;
    for (int i = 0; i < NIMMUNE; i++) if (kImmune[i].r == r) return kImmune[i].n;
    return "?";
}
static void ReportKinds(void)
{
    if (!g_nkinds) return;
    g_hf->log(g_mod, "gassed enemy types this mission (soaked: the most raw damage one took, before armour; big = a heavy):");
    for (int i = 0; i < g_nkinds; i++)
        g_hf->log(g_mod, "  %016llX %-24s gas hits %5u, kills %3u, soaked %7.0f%s", (unsigned long long)g_kinds[i].resource, KindName(g_kinds[i].resource),
            g_kinds[i].gasHits, g_kinds[i].kills, g_kinds[i].soaked, Immune(g_kinds[i].resource) ? "  (immune)" : "");
    g_nkinds = 0;
}

static Ent* Find(uint32_t entity, int create)
{
    uint32_t h = (entity * 2654435761u) & (SLOTS - 1);
    for (int i = 0; i < SLOTS; i++, h = (h + 1) & (SLOTS - 1))
    {
        if (g_ents[h].key == entity + 1) return &g_ents[h];
        if (!g_ents[h].key) { if (!create) return NULL; g_ents[h].key = entity + 1; return &g_ents[h]; }
    }
    return NULL;
}
/* 0x7FFF is "none": spawns carry no network id (the game gives one later), so the table is learned from kills */
static Ent* ByNetwork(uint32_t network) { return network < 0x7FFF && g_netToEntity[network] ? Find(g_netToEntity[network] - 1, 0) : NULL; }
static unsigned g_mapped;
/* An entity's resource (0: not seen) */
uint64_t gas_entity_resource(uint32_t entity)
{
    Ent* x = entity ? Find(entity, 0) : NULL;
    return x ? x->resource : 0;
}
/* What dealt a hit, for the other armaments: its entity resource (and entity), or 0 when not mapped yet. */
uint64_t gas_dealer_resource(uint32_t network, uint32_t* entity)
{
    if (network >= 0x7FFF || !g_netToEntity[network]) return 0;
    Ent* x = Find(g_netToEntity[network] - 1, 0);
    if (!x) return 0;
    if (entity) *entity = g_netToEntity[network] - 1;
    return x->resource;
}
static int IsGasGear(uint64_t resource)
{
    for (int i = 0; i < NGEAR; i++) if (resource && g_gear[i].resource == resource) return 1;
    return 0;
}

/* Level 2: the gas stratagems' cooldowns. */
static const uint32_t kGasStratagems[] = {HF_STRATAGEM_AGM17_GAS_MORTAR_SENTRY, HF_STRATAGEM_ORBITAL_GAS_STRIKE,
    HF_STRATAGEM_EAGLE_GAS_AIRSTRIKE, HF_STRATAGEM_MD8_GAS_MINES, HF_STRATAGEM_TX41_STERILIZER, HF_STRATAGEM_AXTX13_DOG_BREATH};
#define NSTRAT ((int)(sizeof kGasStratagems / sizeof kGasStratagems[0]))
static int g_cooldownCut;
static void SetCooldowns(int on)
{
    HfField cooldown = HF_FIELD(StratagemSettings, cooldown);
    if (on == g_cooldownCut || (on && fp_gas_cooldown_mult() == 1.0f)) return;
    int done = 0;
    for (int i = 0; i < NSTRAT; i++)
    {
        if (!on) { g_hf->set_live(g_mod, kGasStratagems[i], &cooldown, NULL, HF_LIVE_SHARED); continue; }
        HfLiveValue live;
        memset(&live, 0, sizeof live);
        if (!g_hf->get_live(g_mod, kGasStratagems[i], &cooldown, &live)) continue;
        HfValue value = HF_F32(live.base.number * fp_gas_cooldown_mult());
        int when = g_hf->set_live(g_mod, kGasStratagems[i], &cooldown, &value, HF_LIVE_SHARED);
        if (when)
        {
            done++;
            g_hf->log(g_mod, "gas doctrine level 2: stratagem %u cooldown %.0f -> %.0f s (applies %s)", kGasStratagems[i],
                live.base.number, value.number, when == HF_APPLIES_NOW ? "now" : when == HF_APPLIES_RESPAWN ? "on respawn" : "from the next mission");
        }
        else g_hf->log(g_mod, "gas doctrine level 2: stratagem %u cooldown refused (%s)", kGasStratagems[i],
            g_hf->last_log ? g_hf->last_log(g_mod) : "");
    }
    g_cooldownCut = on;
    if (!on) g_hf->log(g_mod, "gas doctrine: stratagem cooldowns back to the game's");
    (void)done;
}

/* The HUD's badges: the Gas Mortar Sentry at its own level, the other gas stratagems at the doctrine's */
int gas_hud_gear(FpGear* out, int max)
{
    int n = 0;
    for (int i = 0; i < NSTRAT && n < max; i++)
    {
        out[n].stratagem = kGasStratagems[i];
        out[n].level = (uint32_t)(kGasStratagems[i] == HF_STRATAGEM_AGM17_GAS_MORTAR_SENTRY ? GasMortarLevel() : g_doctrineLevel);
        n++;
    }
    return n;
}

static void UpdateActive(void)
{
    int level = fp_forced_level() ? fp_forced_level()
        : g_doctrineKills >= fp_gas_level3_kills() ? 3 : g_doctrineKills >= fp_gas_level2_kills() ? 2 : 1;
    if (level != g_doctrineLevel)
    {
        if (level > g_doctrineLevel && !fp_forced_level()) fp_toast("Gas doctrine", level);
        g_doctrineLevel = level;
        g_hf->log(g_mod, "gas doctrine: level %d (%d gas kills this mission)", level, g_doctrineKills);
    }
    SetCooldowns(fp_in_mission() && level >= 2);
    if (fp_in_mission() && GasMortarLevel() >= 2) SetGasMortarAmmo(1);
    int active = level >= 3;
    if (active != g_active)
    {
        g_active = active;
        g_hf->log(g_mod, "gas doctrine %s", active ? "ACTIVE: confused enemies take and deal more damage" : "off");
    }
}

static int Known(uint64_t resource)
{
    for (int i = 0; i < g_nknown; i++) if (g_known[i] == resource) return 1;
    return 0;
}

void gas_kills(uint64_t resource, int added)
{
    double now = fp_clock();
    if (!Known(resource))
    {
        for (int i = 0; i < NGEAR; i++)  /* a new source while a strike's window is open: that strike's */
        {
            Gear* g = &g_gear[i];
            if (g->resource || !g->stratagem || g->learnUntil < now) continue;
            g->resource = resource;
            g->learnUntil = 0;
            g_hf->log(g_mod, "%s's kills come from %016llX (add %s = %016llX to the ini to skip learning)", g->name,
                (unsigned long long)resource, g->stratagem == HF_STRATAGEM_ORBITAL_GAS_STRIKE ? "orbital_gas_source" : "eagle_gas_source",
                (unsigned long long)resource);
            break;
        }
        if (g_nknown < (int)(sizeof g_known / sizeof g_known[0])) g_known[g_nknown++] = resource;
    }
    for (int i = 0; i < NGEAR; i++)
    {
        Gear* g = &g_gear[i];
        if (!g->resource || g->resource != resource) continue;
        g_doctrineKills += added;
        g->kills += added;
        int level = g->kills >= fp_gm_level3_kills() ? 3 : g->kills >= fp_gm_level2_kills() ? 2 : 1;
        if (level != g->level) { if (level > g->level) fp_toast(g->name, level); g->level = level; g_hf->log(g_mod, "%s: level %d (%d kills this mission)", g->name, level, g->kills); }
        if (g == GAS_MORTAR && fp_in_mission() && GasMortarLevel() >= 2) SetGasMortarAmmo(1);
    }
    UpdateActive();
}


/* ---- A/GM-17 Gas Mortar Sentry levels -----------------------------------------------------------------------------
 * Level 2: the sentry takes no damage while it stands in gas (HellForge has no heal for a sentry: a hit on it inside a
 * cloud is cancelled), and its magazine grows by one round per gm_ammo_seconds of its lifetime (a live edit: HellForge
 * says whether the sentry already out gets it or the next call-in does). Level 3: enemies standing in its gas build
 * stacks; its next shell landing on them pops them in one explosion whose damage and radius grow with the stacks (a
 * borrowed ExplosionSettings row, resized by live edits just before it is spawned and put back after). */
#define GM_BLAST 185u                      /* the catalogue's Gas Mortar impact row (its shells are also told by their source) */
static const uint32_t kCloudBlasts[] = {GM_BLAST, 82u, 331u, 270u, 177u};  /* gas mortar, orbital, eagle, mines, G-4 */
typedef struct Cloud { float p[3]; double until; } Cloud;
static Cloud g_clouds[48];
static int g_nextCloud;
static Cloud g_gmClouds[24];          /* the Gas Mortar's own clouds (stacks build only there) */
static int g_nextGmCloud;
static int g_gmAmmo;
static unsigned g_gmShielded, g_pops, g_popsLogged, g_stacksGained;
static uint32_t g_stacked[512];        /* entities holding stacks */
static int g_nstacked;
static uint32_t g_gmBlastType;         /* the shell's real explosion row, learned from its source */
typedef struct Shots { uint32_t entity; unsigned shells; } Shots;
static Shots g_shots[8];
static double g_popUntil;              /* the borrowed row's edits are put back after this */
static int g_popEdited;

static int GasMortarLevel(void) { return fp_forced_level() ? fp_forced_level() : GAS_MORTAR->level; }
static float Dist2(const float a[3], const float b[3]) { float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; return x * x + y * y + z * z; }
static void AddCloud(const float p[3], double seconds)
{
    Cloud* c = &g_clouds[g_nextCloud++ % (int)(sizeof g_clouds / sizeof g_clouds[0])];
    memcpy(c->p, p, sizeof c->p);
    c->until = fp_clock() + seconds;
}
static int InCloud(const float p[3])
{
    double now = fp_clock();
    float r2 = fp_cloud_radius() * fp_cloud_radius();
    for (int i = 0; i < (int)(sizeof g_clouds / sizeof g_clouds[0]); i++)
        if (g_clouds[i].until > now && Dist2(g_clouds[i].p, p) <= r2) return 1;
    return 0;
}
static int InGmCloud(const float p[3])
{
    double now = fp_clock();
    float r2 = fp_cloud_radius() * fp_cloud_radius();
    for (int i = 0; i < (int)(sizeof g_gmClouds / sizeof g_gmClouds[0]); i++)
        if (g_gmClouds[i].until > now && Dist2(g_gmClouds[i].p, p) <= r2) return 1;
    return 0;
}
static void Track(uint32_t entity)
{
    for (int i = 0; i < g_nstacked; i++) if (g_stacked[i] == entity) return;
    if (g_nstacked < (int)(sizeof g_stacked / sizeof g_stacked[0])) g_stacked[g_nstacked++] = entity;
}
/* Stacks still held: out of the gas, one is lost every gm_stack_decay seconds. */
static int Held(const Ent* t, double now)
{
    double out = now - t->lastInGas, step = fp_gm_stack_decay();
    if (t->stacks <= 0) return 0;
    if (out < step) return t->stacks;
    int lost = (int)floor(out / step);
    return lost >= t->stacks ? 0 : t->stacks - lost;
}
/* A gas tick inside the Gas Mortar's cloud: a stack every gm_stack_seconds while it stays in. */
static void Stack(uint32_t entity, Ent* t, double now)
{
    int was = t->stacks;
    t->stacks = Held(t, now);
    if (t->stacks < was) t->lastStack = now;  /* back in after losing some: the clock starts again */
    t->lastInGas = now;
    if (t->stacks >= fp_gm_stack_cap() || now - t->lastStack < fp_gm_stack_seconds()) return;
    if (!t->stacks) t->lastStack = now;  /* the first tick starts the clock */
    else t->lastStack += fp_gm_stack_seconds() * floor((now - t->lastStack) / fp_gm_stack_seconds());
    t->stacks++;
    g_stacksGained++;
    Track(entity);
}
static void PopRow(int on, float damage, float radius)
{
    uint32_t row = (uint32_t)fp_gm_pop_row();
    HfField inner = HF_FIELD(ExplosionSettings, inner_radius), outer = HF_FIELD(ExplosionSettings, outer_radius);
    HfField dmgId = HF_FIELD(ExplosionSettings, damage);
    HfField sd = HF_FIELD(DamageInfo, standard_damage), dd = HF_FIELD(DamageInfo, durable_damage);
    static uint32_t damageId;
    if (!damageId)
    {
        HfLiveValue v;
        memset(&v, 0, sizeof v);
        if (!g_hf->get_live(g_mod, row, &dmgId, &v)) return;
        damageId = (uint32_t)v.base.number;
    }
    if (!on)
    {
        g_hf->set_live(g_mod, row, &inner, NULL, HF_LIVE_SHARED);
        g_hf->set_live(g_mod, row, &outer, NULL, HF_LIVE_SHARED);
        g_hf->set_live(g_mod, damageId, &sd, NULL, HF_LIVE_SHARED);
        g_hf->set_live(g_mod, damageId, &dd, NULL, HF_LIVE_SHARED);
        g_popEdited = 0;
        return;
    }
    HfValue r = HF_F32(radius), ri = HF_F32(radius * 0.5f), d = HF_I32(floor(damage + 0.5));
    g_hf->set_live(g_mod, row, &inner, &ri, HF_LIVE_SHARED);
    g_hf->set_live(g_mod, row, &outer, &r, HF_LIVE_SHARED);
    g_hf->set_live(g_mod, damageId, &sd, &d, HF_LIVE_SHARED);
    g_hf->set_live(g_mod, damageId, &dd, &d, HF_LIVE_SHARED);
    g_popEdited = 1;
}
/* A Gas Mortar shell landed: the detonation runs through its cloud and every Gas Mortar cloud touching it (and the ones
 * touching those). Each stacked enemy inside pops where it stands, sized by its own stacks; enemies within 2.5 m of each
 * other share one pop (the most stacks). The pops go off gm_pop_step apart, outward from the impact, one per game update
 * (the borrowed row holds one size at a time). */
typedef struct PopQ { float p[3]; int stacks; double at; } PopQ;
static PopQ g_popq[32];
static int g_npopq;
static unsigned g_chains, g_chainsLogged;
static void Pop(const float at[3], double now)
{
    enum { NC = (int)(sizeof g_gmClouds / sizeof g_gmClouds[0]) };
    int hop[NC];
    float r = fp_cloud_radius(), r2 = r * r, touch2 = 4 * r2;
    int clouds = 0, changed = 1;
    for (int i = 0; i < NC; i++)
    {
        hop[i] = -1;
        if (g_gmClouds[i].until > now && Dist2(g_gmClouds[i].p, at) <= r2) hop[i] = 0, clouds++;
    }
    for (int h = 0; changed && h < NC; h++)  /* spread through touching clouds */
    {
        changed = 0;
        for (int i = 0; i < NC; i++)
        {
            if (hop[i] != h) continue;
            for (int j = 0; j < NC; j++)
                if (hop[j] < 0 && g_gmClouds[j].until > now && Dist2(g_gmClouds[i].p, g_gmClouds[j].p) <= touch2)
                    hop[j] = h + 1, clouds++, changed = 1;
        }
    }
    typedef struct Cand { float p[3]; int stacks, hop; float d2; } Cand;
    Cand c[64];
    int nc = 0, most = 0, enemies = 0;
    for (int i = 0; i < g_nstacked; )
    {
        Ent* t = Find(g_stacked[i], 0);
        if (t) { int held = Held(t, now); if (held != t->stacks) { t->stacks = held; t->lastInGas = now - fmod(now - t->lastInGas, fp_gm_stack_decay()); } }
        if (!t || !t->stacks) { if (t) t->stacks = 0; g_stacked[i] = g_stacked[--g_nstacked]; continue; }
        int best = -1;
        for (int k = 0; k < NC; k++)
            if (hop[k] >= 0 && Dist2(g_gmClouds[k].p, t->pos) <= r2 && (best < 0 || hop[k] < best)) best = hop[k];
        if (best >= 0)
        {
            enemies++;
            if (t->stacks > most) most = t->stacks;
            int m = -1;
            for (int k = 0; k < nc && m < 0; k++) if (Dist2(c[k].p, t->pos) <= 6.25f) m = k;
            if (m >= 0) { if (t->stacks > c[m].stacks) c[m].stacks = t->stacks; if (best < c[m].hop) c[m].hop = best; }
            else if (nc < 64)
            {
                memcpy(c[nc].p, t->pos, sizeof c[nc].p);
                c[nc].stacks = t->stacks;
                c[nc].hop = best;
                c[nc].d2 = Dist2(t->pos, at);
                nc++;
            }
            t->stacks = 0;
            t->lastStack = now;
        }
        i++;
    }
    if (!nc || !fp_gm_pop_row()) return;
    for (int i = 1; i < nc; i++)  /* nearest hop first, then nearest the impact */
        for (int j = i; j > 0 && (c[j].hop < c[j - 1].hop || (c[j].hop == c[j - 1].hop && c[j].d2 < c[j - 1].d2)); j--)
        { Cand x = c[j]; c[j] = c[j - 1]; c[j - 1] = x; }
    int queued = 0;
    for (int i = 0; i < nc && g_npopq < (int)(sizeof g_popq / sizeof g_popq[0]); i++, queued++)
    {
        PopQ* q = &g_popq[g_npopq++];
        memcpy(q->p, c[i].p, sizeof q->p);
        q->stacks = c[i].stacks;
        q->at = now + fp_gm_pop_step() * i;
    }
    g_chains++;
    if (g_chainsLogged < 15)
    {
        g_chainsLogged++;
        g_hf->log(g_mod, "Gas Mortar level 3: detonation through %d cloud%s: %d stacked enemies, %d pops (most %d stacks: %.0f damage, %.1f m)",
            clouds, clouds == 1 ? "" : "s", enemies, queued, most, fp_gm_pop_damage() * most, fp_gm_pop_radius() + fp_gm_pop_radius_step() * most);
    }
}
/* One queued pop per game update. */
void gas_tick(double now)
{
    if (g_npopq)
    {
        int k = -1;
        for (int i = 0; i < g_npopq; i++) if (g_popq[i].at <= now && (k < 0 || g_popq[i].at < g_popq[k].at)) k = i;
        if (k >= 0)
        {
            PopQ q = g_popq[k];
            g_popq[k] = g_popq[--g_npopq];
            float damage = fp_gm_pop_damage() * q.stacks, radius = fp_gm_pop_radius() + fp_gm_pop_radius_step() * q.stacks;
            PopRow(1, damage, radius);
            int r = g_hf->spawn_explosion(g_mod, (uint32_t)fp_gm_pop_row(), q.p);
            g_popUntil = now + 0.6;
            g_pops++;
            if (r != HF_ACT_REQUESTED && g_popsLogged < 5) { g_popsLogged++; g_hf->log(g_mod, "Gas Mortar level 3: a pop answered %d", r); }
        }
    }
    if (!g_npopq && g_popEdited && now > g_popUntil) PopRow(0, 0, 0);
}
/* the magazine: capacity + lifetime / gm_ammo_seconds (at most 2047, the turret's network count) */
static void SetGasMortarAmmo(int on)
{
    HfField cap = HF_FIELD(WeaponMagazine, capacity);
    if (!on)
    {
        if (g_gmAmmo) g_hf->set_live(g_mod, HF_ENTITY_AGM17_GAS_MORTAR_SENTRY, &cap, NULL, 0);
        g_gmAmmo = 0;
        return;
    }
    if (g_gmAmmo || fp_gm_ammo_seconds() <= 0) return;
    g_gmAmmo = 1;  /* tried once per mission */
    HfField life = HF_FIELD(HellpodPayload, lifetime);
    HfLiveValue c, l;
    memset(&c, 0, sizeof c);
    memset(&l, 0, sizeof l);
    if (!g_hf->get_live(g_mod, HF_ENTITY_AGM17_GAS_MORTAR_SENTRY, &cap, &c) || !g_hf->get_live(g_mod, HF_ENTITY_AGM17_GAS_MORTAR_SENTRY, &life, &l))
    {
        g_hf->log(g_mod, "Gas Mortar level 2: its magazine or lifetime cannot be read");
        g_gmAmmo = 0;
        return;
    }
    double add = floor(l.base.number / fp_gm_ammo_seconds());
    double total = c.base.number + add > 2047 ? 2047 : c.base.number + add;
    HfValue v = HF_U32(total);
    int when = g_hf->set_live(g_mod, HF_ENTITY_AGM17_GAS_MORTAR_SENTRY, &cap, &v, 0);
    if (when) g_hf->log(g_mod, "Gas Mortar level 2: magazine %.0f -> %.0f rounds (1 per %.1f s of its %.0f s; applies %s)", c.base.number, total,
        (double)fp_gm_ammo_seconds(), l.base.number, when == HF_APPLIES_NOW ? "to the sentry already out" : "from its next call-in");
    else g_hf->log(g_mod, "Gas Mortar level 2: the magazine change was refused (%s)", g_hf->last_log ? g_hf->last_log(g_mod) : "");
}
static void OnExplosion(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfExplosionEvent* e = (const HfExplosionEvent*)info;
    double now = fp_clock();
    if (fp_gm_pop_row() && e->type == (uint32_t)fp_gm_pop_row()) return;  /* our own pop */
    for (unsigned i = 0; i < sizeof kCloudBlasts / sizeof kCloudBlasts[0]; i++)
        if (e->type == kCloudBlasts[i]) { AddCloud(e->position, fp_cloud_seconds()); break; }
    Ent* src = e->source ? Find(e->source, 0) : NULL;
    Ent* own = e->owner ? Find(e->owner, 0) : NULL;
    int fromGm = (src && src->resource == HF_ENTITY_AGM17_GAS_MORTAR_SENTRY) || (own && own->resource == HF_ENTITY_AGM17_GAS_MORTAR_SENTRY) ||
                 (g_gmBlastType && e->type == g_gmBlastType) || e->type == GM_BLAST;
    if (!fromGm) return;
    if (!g_gmBlastType || g_gmBlastType != e->type)
    {
        if (!g_gmBlastType) g_hf->log(g_mod, "Gas Mortar shell explosion: row %u", e->type);
        if (!g_gmBlastType) g_gmBlastType = e->type;
    }
    /* shells per sentry: a normal magazine is 16; past that, level 2's bigger magazine is working */
    uint32_t who = (src && src->resource == HF_ENTITY_AGM17_GAS_MORTAR_SENTRY) ? e->source : (own && own->resource == HF_ENTITY_AGM17_GAS_MORTAR_SENTRY) ? e->owner : 0;
    if (who)
    {
        Shots* sh = NULL;
        for (int i = 0; i < 8 && !sh; i++) if (g_shots[i].entity == who) sh = &g_shots[i];
        for (int i = 0; i < 8 && !sh; i++) if (!g_shots[i].entity) { sh = &g_shots[i]; sh->entity = who; sh->shells = 0; }
        if (!sh) { memmove(g_shots, g_shots + 1, sizeof g_shots - sizeof g_shots[0]); sh = &g_shots[7]; sh->entity = who; sh->shells = 0; }
        sh->shells++;
        if (sh->shells == 1 || sh->shells == 16 || sh->shells == 17 || sh->shells % 25 == 0)
            g_hf->log(g_mod, "Gas Mortar (entity %u): shell %u%s", who, sh->shells,
                sh->shells == 17 ? " - past the normal 16-round magazine: the level 2 magazine is working" : "");
    }
    AddCloud(e->position, fp_cloud_seconds());
    Cloud* c = &g_gmClouds[g_nextGmCloud++ % (int)(sizeof g_gmClouds / sizeof g_gmClouds[0])];
    memcpy(c->p, e->position, sizeof c->p);
    c->until = now + fp_cloud_seconds();
    if (GasMortarLevel() >= 3) Pop(e->position, now);
}

void gas_reset(void)
{
    ReportKinds();
    memset(g_netToEntity, 0, sizeof g_netToEntity);
    g_mapped = 0;
    for (int i = 0; i < NGEAR; i++) g_gear[i].kills = 0, g_gear[i].level = 1, g_gear[i].learnUntil = 0;
    g_doctrineKills = 0;
    if (fp_in_mission()) SetGasMortarAmmo(0);  /* at the next start, not the end: see ResetMission */
    memset(g_clouds, 0, sizeof g_clouds);
    memset(g_gmClouds, 0, sizeof g_gmClouds);
    g_hf->log(g_mod, "Gas Mortar this mission: %u hits on it cancelled in gas, %u stacks gained, %u detonations, %u pops", g_gmShielded, g_stacksGained, g_chains, g_pops);
    g_gmShielded = g_pops = g_popsLogged = g_stacksGained = g_chains = g_chainsLogged = 0;
    g_npopq = 0;
    g_nstacked = 0;
    memset(g_shots, 0, sizeof g_shots);
    if (g_popEdited) PopRow(0, 0, 0);
    for (int i = 0; i < SLOTS; i++) g_ents[i].confusedUntil = 0, g_ents[i].soaked = 0, g_ents[i].gassed = 0, g_ents[i].stacks = 0;
    UpdateActive();
}

static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* e = (const HfSpawnEvent*)info;
    static unsigned logged, total, noNetwork;
    total++;
    if (e->network >= 0x7FFF) noNetwork++;
    if (logged < 5 || (total % 2000) == 0)
    {
        logged++;
        g_hf->log(g_mod, "spawn: entity %u network %u resource %016llX authority %u (%u spawns, %u without a network id)",
            e->entity, e->network, (unsigned long long)e->resource, e->authority, total, noNetwork);
    }
    Ent* x = Find(e->entity, 1);
    if (!x) return;
    if (x->network < 0x8000 && g_netToEntity[x->network] == e->entity + 1) g_netToEntity[x->network] = 0;
    x->resource = e->resource;
    x->network = e->network;
    x->confusedUntil = 0;
    x->lastDealer = 0;
    x->ambiguous = 0;
    x->lastUpdate = 0;
    x->soaked = 0;
    x->gassed = 0;
    x->stacks = 0;
    if (e->network < 0x7FFF) g_netToEntity[e->network] = e->entity + 1;
}

static void LogElement(const HfDamageEvent* d, const Ent* dealer)
{
    uint32_t key = d->element * 16 + d->kind;
    for (int i = 0; i < g_loggedElements; i++) if (g_seen[i] == key) return;
    if (g_loggedElements >= (int)(sizeof g_seen / sizeof g_seen[0])) return;
    g_seen[g_loggedElements++] = key;
    g_hf->log(g_mod, "damage element %u (kind %u) from %016llX", d->element, d->kind, dealer ? (unsigned long long)dealer->resource : 0ull);
}

static void OnDamagePre(HfMod* mod, const HfEventInfo* info, void* user)
{
    HfDamagePreEvent* e = (HfDamagePreEvent*)(void*)info;
    HfDamageEvent* d = &e->damage;
    double now = fp_clock();
    Ent* dealer = ByNetwork(d->dealer);
    Ent* owner = ByNetwork(d->owner);
    g_hits++;
    if (d->dealer == 0x7FFF) g_dealerNone++;
    else if (dealer) g_dealerKnown++;
    else if (g_rawLogged < 12)
    {
        g_rawLogged++;
        g_hf->log(g_mod, "unmapped dealer: network %u (owner %u), target entity %u, element %u kind %u", d->dealer, d->owner, d->target, d->element, d->kind);
    }
    if (!g_learned) LogElement(d, dealer);
    /* learn the gas element from a hit a known gas armament dealt */
    if (g_element < 0 && ((dealer && IsGasGear(dealer->resource)) || (owner && IsGasGear(owner->resource))))
    {
        g_element = (int)d->element;
        g_learned = 1;
        g_hf->log(g_mod, "gas element learned: %d (add gas_element = %d to the ini to skip learning)", g_element, g_element);
    }
    Ent* target = Find(d->target, 0);
    if (target)
    {
        g_targetKnown++;
        if (d->dealer < 0x7FFF)
        {
            /* the hits of one game update: two different dealers make the update ambiguous (nothing is learned) */
            if (target->lastUpdate == info->update && target->lastDealer && target->lastDealer != d->dealer) target->ambiguous = 1;
            else if (target->lastUpdate != info->update) target->ambiguous = 0;
            target->lastDealer = d->dealer;
            target->lastUpdate = info->update;
        }
    }
    if (target && d->amount > 0) target->soaked += d->amount;
    if (target) memcpy(target->pos, d->position, sizeof target->pos);
    /* gas hits show where clouds are now (the Sterilizer's and Dog Breath's too) */
    if (g_element >= 0 && (int)d->element == g_element && d->kind == 2) AddCloud(d->position, 1.5);
    /* Gas Mortar level 2: no damage to it while it stands in gas */
    if (target && target->resource == HF_ENTITY_AGM17_GAS_MORTAR_SENTRY && GasMortarLevel() >= 2 && fp_gm_shield() && InCloud(d->position))
    {
        e->cancel = 1;
        if (g_gmShielded < 5 || g_gmShielded % 50 == 0)
            g_hf->log(g_mod, "Gas Mortar level 2: a hit of %d on it cancelled (it stands in gas; %u so far)", d->amount, g_gmShielded + 1);
        g_gmShielded++;
        return;
    }
    /* Gas Mortar level 3: enemies ticking in its gas build stacks */
    if (target && GasMortarLevel() >= 3 && g_element >= 0 && (int)d->element == g_element && target->resource != HELLDIVER &&
        target->resource != HF_ENTITY_AGM17_GAS_MORTAR_SENTRY && InGmCloud(d->position))
        Stack(d->target, target, now);
    if (g_element >= 0 && (int)d->element == g_element && target && target->resource != HELLDIVER)
    {
        Kind* k = KindOf(target->resource);
        if (k) k->gasHits++;
        target->gassed = 1;
        if (!Immune(target->resource)) target->confusedUntil = now + fp_confusion_seconds(), g_gasHits++;
    }
    if (!g_active || d->amount <= 0) return;
    double mult = 1.0;
    if (target && target->confusedUntil > now && target->resource != HELLDIVER) mult *= fp_gas_taken_mult(), g_boostTaken++;
    if (dealer && dealer->confusedUntil > now && (fp_gas_vs_players() || !target || target->resource != HELLDIVER))
        mult *= fp_gas_dealt_mult(), g_boostDealt++;
    if (now >= g_nextReport)
    {
        g_nextReport = now + 60;
        g_hf->log(g_mod, "gas doctrine: %u hits (target known %u, dealer known %u, no dealer %u, dealers learned %u), %u gas hits, boosted taken %u / dealt %u",
            g_hits, g_targetKnown, g_dealerKnown, g_dealerNone, g_mapped, g_gasHits, g_boostTaken, g_boostDealt);
    }
    if (mult != 1.0)
    {
        double amount = floor(d->amount * mult + 0.5);
        d->amount = amount > 2147483647.0 ? 2147483647 : amount < 1 ? 1 : (int32_t)amount;
    }
}

/* A kill names its dealer as an entity, where a hit names it by network id: the victim's last hit just before its death
 * pairs the two, and the table grows as enemies kill things. */
static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfKillEvent* k = (const HfKillEvent*)info;
    Ent* victim = Find(k->victim, 0);
    if (!victim) return;
    if (victim->gassed)
    {
        Kind* kind = KindOf(victim->resource);
        if (kind) { kind->kills++; if (victim->soaked > kind->soaked) kind->soaked = victim->soaked; }
        victim->gassed = 0;
    }
    victim->confusedUntil = 0;
    victim->stacks = 0;
    /* the victim's own network id goes free when it dies: the game hands it to something else later */
    if (victim->network < 0x7FFF && g_netToEntity[victim->network] == k->victim + 1) g_netToEntity[victim->network] = 0;
    victim->network = 0x7FFF;
    if (k->dealer == 0xFFFFFFFFu || !victim->lastDealer || victim->ambiguous) return;
    /* only the hit of the same game update as the kill (or the one before: a kill can be told an update later) */
    if (k->info.update - victim->lastUpdate > 1) return;
    uint32_t net = victim->lastDealer;
    if (net >= 0x7FFF || g_netToEntity[net] == k->dealer + 1) return;
    if (!Find(k->dealer, 1)) return;
    if (!g_netToEntity[net]) g_mapped++;
    g_netToEntity[net] = k->dealer + 1;
    Ent* dealer = Find(k->dealer, 0);
    if (dealer) dealer->network = net;
}

static void OnStratagem(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfStratagemEvent* e = (const HfStratagemEvent*)info;
    for (int i = 0; i < NGEAR; i++)
        if (g_gear[i].stratagem == e->type && !g_gear[i].resource)
        {
            g_gear[i].learnUntil = fp_clock() + LEARN_WINDOW;
            g_hf->log(g_mod, "%s called in: learning its kill source", g_gear[i].name);
        }
}

int gas_init(const HfApi* hf, HfMod* mod, int element, uint64_t orbital, uint64_t eagle)
{
    g_hf = hf;
    g_mod = mod;
    hf->log(mod, "gas doctrine: %d enemy types immune to confusion (%d built in, %d from gas_immune)", NIMMUNE + g_nimmune, NIMMUNE, g_nimmune);
    g_element = element;
    g_learned = element >= 0;
    for (int i = 0; i < NGEAR; i++)
    {
        if (g_gear[i].stratagem == HF_STRATAGEM_ORBITAL_GAS_STRIKE) g_gear[i].resource = orbital;
        if (g_gear[i].stratagem == HF_STRATAGEM_EAGLE_GAS_AIRSTRIKE) g_gear[i].resource = eagle;
    }
    /* the sources every mission credits that are never a strike's */
    g_known[g_nknown++] = HELLDIVER;
    g_known[g_nknown++] = HF_ENTITY_AM12_MORTAR_SENTRY;
    for (int i = 0; i < NGEAR; i++) if (g_gear[i].resource) g_known[g_nknown++] = g_gear[i].resource;
    return hf->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && hf->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) &&
           hf->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) && hf->subscribe(mod, HF_ON_EXPLOSION, OnExplosion, NULL, 0) &&
           hf->subscribe(mod, HF_ON_STRATAGEM, OnStratagem, NULL, 0);
}
