/* Fire Mission (yui.fire-mission): a stratagem that calls your mortar sentries onto a spot.
 *
 * Throw its beacon (a copy of the Orbital Smoke Strike that drops one harmless spotting round instead of smoke) and every
 * A/M-12 Mortar, A/M-23 EMS Mortar and A/GM-17 Gas Mortar Sentry of yours that is out, alive and has the spot within its
 * targeting range shells it, whatever it can see: `salvos` salvos of `rounds` shells each, `salvo_interval` seconds
 * apart, scattered over `spread` metres, starting `delay` seconds after the beacon. Each sentry's own shell type falls
 * (spawn_projectile, from `drop_height` metres above the spot, as Field Promotions' bonus rounds do); the sentries'
 * magazines are not touched.
 *
 * Where your sentries are: the beacon of each mortar sentry you call in, paired with the sentry entity that spawns
 * after it (HF_ON_STRATAGEM, HF_ON_SPAWN). A sentry is out until it dies (HF_ON_KILL) or its deployed lifetime runs out.
 * Teammates' sentries are not known (their beacons are told on their machines), and shells are host-only: solo first.
 *
 * Settings in fire-mission.ini (read at game start). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "hellforge_mod.h"
#include "hellforge_components.h"
#include "hellforge_content.h"

static const HfApi* g_hf;
static HfMod* g_mod;
static uint32_t g_type;  /* the Fire Mission stratagem */
static double g_clock;
static int g_inMission;

/* settings */
static char g_code[16] = "LRLRUDUD";
static float g_cooldown = 60, g_salvoInterval = 2.5f, g_spread = 7, g_height = 40, g_delay = 2, g_extraRange = 0;
static int g_salvos = 3, g_rounds = 3, g_uses = 0, g_ems = 1, g_gas = 1, g_smoke = 0;

typedef struct Kind { const char* name; uint32_t stratagem; uint64_t entity; uint32_t shell; } Kind;
static const Kind kKinds[] = {
    {"Mortar Sentry", HF_STRATAGEM_AM12_MORTAR_SENTRY, HF_ENTITY_AM12_MORTAR_SENTRY, HF_PROJECTILE_AM12_MORTAR_SENTRY},
    {"EMS Mortar Sentry", HF_STRATAGEM_AM23_EMS_MORTAR_SENTRY, HF_ENTITY_AM23_EMS_MORTAR_SENTRY, HF_PROJECTILE_AM23_EMS_MORTAR_SENTRY},
    {"Gas Mortar Sentry", HF_STRATAGEM_AGM17_GAS_MORTAR_SENTRY, HF_ENTITY_AGM17_GAS_MORTAR_SENTRY, HF_PROJECTILE_AGM17_GAS_MORTAR_SENTRY},
};
#define NKINDS ((int)(sizeof kKinds / sizeof kKinds[0]))

typedef struct Beacon { int kind; float pos[3]; double at; int used; } Beacon;
static Beacon g_beacons[16];
typedef struct Sentry { int kind; uint32_t entity; float pos[3]; double until; } Sentry;
static Sentry g_sentries[16];
static int g_nsentries;
typedef struct Shell { double at; uint32_t type; float pos[3]; int tries; } Shell;
static Shell g_shells[256];
static int g_nshells;
static unsigned g_missions, g_fired, g_unpaired;

/* ---- settings ------------------------------------------------------------------------------------------------- */
static FILE* OpenUtf8(const char* path, const wchar_t* mode)
{
    wchar_t wide[MAX_PATH * 2];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, (int)(sizeof wide / sizeof wide[0]))) return NULL;
    return _wfopen(wide, mode);
}
static void LoadSettings(void)
{
    char path[MAX_PATH * 2], line[256];
    snprintf(path, sizeof path, "%s\\fire-mission.ini", g_hf->mod_folder(g_mod));
    FILE* f = OpenUtf8(path, L"r");
    if (!f)
    {
        if (!(f = OpenUtf8(path, L"w"))) return;
        fprintf(f, "; Fire Mission settings (read at game start).\n"
                   "; The stratagem's input (U D L R, 1 to 9; it must not start or be the start of another stratagem's code),\n"
                   "; its cooldown in seconds and its uses (0: the flare's own; -1: unlimited).\ncode = %s\ncooldown = %g\nuses = %d\n"
                   "; Each sentry in range: salvos, shells per salvo, seconds between salvos, scatter (metres), seconds after the\n"
                   "; beacon before the first salvo, and how high above the spot the shells start (metres).\n"
                   "salvos = %d\nrounds = %d\nsalvo_interval = %g\nspread = %g\ndelay = %g\ndrop_height = %g\n"
                   "; Which sentries join (1 yes, 0 no; the A/M-12 always does), and metres added to each sentry's targeting range.\n"
                   "ems = %d\ngas = %d\nextra_range = %g\n"
                   "; 1: the call-in still drops the smoke strike's smoke on the spot; 0: one harmless spotting round (read at game start).\nsmoke = %d\n",
            g_code, (double)g_cooldown, g_uses, g_salvos, g_rounds, (double)g_salvoInterval, (double)g_spread, (double)g_delay,
            (double)g_height, g_ems, g_gas, (double)g_extraRange, g_smoke);
        fclose(f);
        return;
    }
    while (fgets(line, sizeof line, f))
    {
        char* eq = strchr(line, '=');
        if (line[0] == ';' || !eq) continue;
        *eq = 0;
        for (char* e = eq - 1; e >= line && (*e == ' ' || *e == '\t'); e--) *e = 0;
        char* val = eq + 1;
        while (*val == ' ' || *val == '\t') val++;
        double v = strtod(val, NULL);
        if (!strcmp(line, "code"))
        {
            int n = 0;
            for (char* c = val; *c && n < 9; c++)
                if (*c == 'U' || *c == 'D' || *c == 'L' || *c == 'R') g_code[n++] = *c;
            if (n) g_code[n] = 0;
            continue;
        }
        if (!isfinite(v)) continue;
        if (!strcmp(line, "cooldown")) g_cooldown = (float)(v < 0 ? 0 : v > 3600 ? 3600 : v);
        else if (!strcmp(line, "uses")) g_uses = v < -1 ? -1 : v > 99 ? 99 : (int)v;
        else if (!strcmp(line, "salvos")) g_salvos = v < 1 ? 1 : v > 20 ? 20 : (int)v;
        else if (!strcmp(line, "rounds")) g_rounds = v < 1 ? 1 : v > 10 ? 10 : (int)v;
        else if (!strcmp(line, "salvo_interval")) g_salvoInterval = (float)(v < 0.3 ? 0.3 : v > 30 ? 30 : v);
        else if (!strcmp(line, "spread")) g_spread = (float)(v < 0 ? 0 : v > 60 ? 60 : v);
        else if (!strcmp(line, "delay")) g_delay = (float)(v < 0 ? 0 : v > 30 ? 30 : v);
        else if (!strcmp(line, "drop_height")) g_height = (float)(v < 5 ? 5 : v > 150 ? 150 : v);
        else if (!strcmp(line, "ems")) g_ems = v != 0;
        else if (!strcmp(line, "smoke")) g_smoke = v != 0;
        else if (!strcmp(line, "gas")) g_gas = v != 0;
        else if (!strcmp(line, "extra_range")) g_extraRange = (float)(v < 0 ? 0 : v > 500 ? 500 : v);
    }
    fclose(f);
}

/* ---- the sentries --------------------------------------------------------------------------------------------- */
static float LiveF32(uint64_t entity, const HfField* field, float fallback)
{
    HfLiveValue v;
    memset(&v, 0, sizeof v);
    return g_hf->get_live(g_mod, entity, field, &v) && v.now.number > 0 ? (float)v.now.number : fallback;
}
static float Dist(const float a[3], const float b[3])
{
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
}
static void Forget(int i) { g_sentries[i] = g_sentries[--g_nsentries]; }

static void Queue(double at, uint32_t type, const float spot[3])
{
    if (g_nshells >= (int)(sizeof g_shells / sizeof g_shells[0])) return;
    Shell* s = &g_shells[g_nshells++];
    /* a random point in a disc of `spread` metres (uniform over its area) */
    double a = (rand() / (double)RAND_MAX) * 6.283185307, r = g_spread * sqrt(rand() / (double)RAND_MAX);
    s->at = at;
    s->type = type;
    s->pos[0] = spot[0] + (float)(r * cos(a));
    s->pos[1] = spot[1] + (float)(r * sin(a));
    s->pos[2] = spot[2] + g_height;
    s->tries = 0;
}

static void FireMission(const float spot[3])
{
    g_missions++;
    int joined = 0, outOfRange = 0;
    for (int i = 0; i < g_nsentries; )
    {
        Sentry* s = &g_sentries[i];
        if (s->until < g_clock) { Forget(i); continue; }
        const Kind* k = &kKinds[s->kind];
        if ((s->kind == 1 && !g_ems) || (s->kind == 2 && !g_gas)) { i++; continue; }
        HfField range = HF_FIELD(SensorEye, range);
        float reach = LiveF32(k->entity, &range, 125.0f) + g_extraRange, d = Dist(s->pos, spot);
        if (d > reach)
        {
            outOfRange++;
            g_hf->log(g_mod, "fire mission %u: %s at %.0f m is out of its %.0f m range", g_missions, k->name, (double)d, (double)reach);
            i++;
            continue;
        }
        /* each sentry a little apart from the others, its shells a quarter second apart within a salvo */
        for (int v = 0; v < g_salvos; v++)
            for (int r = 0; r < g_rounds; r++)
                Queue(g_clock + g_delay + v * g_salvoInterval + joined * 0.35 + r * 0.25, k->shell, spot);
        g_hf->log(g_mod, "fire mission %u: %s at %.0f m joins (%d salvos of %d)", g_missions, k->name, (double)d, g_salvos, g_rounds);
        joined++;
        i++;
    }
    g_hf->log(g_mod, "fire mission %u on (%.0f, %.0f, %.0f): %d sentr%s firing, %d out of range%s", g_missions, (double)spot[0],
        (double)spot[1], (double)spot[2], joined, joined == 1 ? "y" : "ies", outOfRange, g_nsentries ? "" : " (no mortar sentries of yours are out)");
}

/* ---- events --------------------------------------------------------------------------------------------------- */
static void OnStratagem(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfStratagemEvent* e = (const HfStratagemEvent*)info;
    if (!g_inMission) return;
    if (g_type && e->type == g_type) { FireMission(e->position); return; }
    for (int k = 0; k < NKINDS; k++)
        if (e->type == kKinds[k].stratagem)
        {
            int slot = 0;
            for (int i = 1; i < (int)(sizeof g_beacons / sizeof g_beacons[0]); i++) if (g_beacons[i].at < g_beacons[slot].at) slot = i;
            g_beacons[slot].kind = k;
            memcpy(g_beacons[slot].pos, e->position, sizeof g_beacons[slot].pos);
            g_beacons[slot].at = g_clock;
            g_beacons[slot].used = 0;
        }
}

static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* e = (const HfSpawnEvent*)info;
    for (int k = 0; k < NKINDS; k++)
    {
        if (e->resource != kKinds[k].entity) continue;
        /* the newest unpaired beacon of its kind from the last 40 seconds: where it landed */
        int best = -1;
        for (int i = 0; i < (int)(sizeof g_beacons / sizeof g_beacons[0]); i++)
            if (!g_beacons[i].used && g_beacons[i].kind == k && g_beacons[i].at > 0 && g_clock - g_beacons[i].at < 40 &&
                (best < 0 || g_beacons[i].at > g_beacons[best].at)) best = i;
        if (best < 0) { if (g_unpaired++ < 5) g_hf->log(g_mod, "%s %u spawned with no beacon of yours (a teammate's?): not used", kKinds[k].name, e->entity); return; }
        if (g_nsentries >= (int)(sizeof g_sentries / sizeof g_sentries[0])) Forget(0);
        g_beacons[best].used = 1;
        HfField life = HF_FIELD(HellpodPayload, lifetime);
        Sentry* s = &g_sentries[g_nsentries++];
        s->kind = k;
        s->entity = e->entity;
        memcpy(s->pos, g_beacons[best].pos, sizeof s->pos);
        s->until = g_clock + LiveF32(kKinds[k].entity, &life, 180.0f) + 10.0;  /* its lifetime, from about when it lands */
        g_hf->log(g_mod, "%s %u is out at (%.0f, %.0f, %.0f)", kKinds[k].name, e->entity, (double)s->pos[0], (double)s->pos[1], (double)s->pos[2]);
        return;
    }
}

static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfKillEvent* k = (const HfKillEvent*)info;
    for (int i = 0; i < g_nsentries; i++)
        if (g_sentries[i].entity == k->victim)
        {
            g_hf->log(g_mod, "%s %u is gone", kKinds[g_sentries[i].kind].name, k->victim);
            Forget(i);
            return;
        }
}

static void OnMission(HfMod* mod, const HfEventInfo* info, void* user)
{
    g_inMission = info->kind == HF_ON_MISSION_START;
    if (!g_inMission && g_missions) g_hf->log(g_mod, "mission end: %u fire missions, %u shells fired", g_missions, g_fired);
    g_nsentries = g_nshells = 0;
    memset(g_beacons, 0, sizeof g_beacons);
    g_missions = g_fired = g_unpaired = 0;
}

static void Tick(HfMod* mod, float dt, void* user)
{
    static const float down[3] = {0.0f, 0.0f, -1.0f};
    g_clock += dt;
    for (int i = 0; i < g_nshells; )
    {
        Shell* s = &g_shells[i];
        if (s->at > g_clock) { i++; continue; }
        int r = g_hf->spawn_projectile(g_mod, s->type, s->pos, down);
        if (r == HF_ACT_WAITING && ++s->tries < 120) { s->at = g_clock + 0.1; i++; continue; }  /* its packages load */
        if (r && r != HF_ACT_WAITING) g_fired++;
        else if (!r && g_fired < 3) g_hf->log(g_mod, "a shell was refused (%s)", g_hf->last_log ? g_hf->last_log(g_mod) : "");
        *s = g_shells[--g_nshells];
    }
}

/* ---- the spotting round: the call-in without the smoke ---------------------------------------------------------
 * The stratagem's payload is a copy of the Orbital Smoke Strike's barrage (records of its own: the real smoke strike is
 * untouched) firing one shell: a copy of the smoke shell with no blast (so no smoke) and a damage type dealing nothing. */
#define SMOKE_BARRAGE 0xDA76A06325E692C9ull  /* the Orbital Smoke Strike's payload (BombardmentComponentData) */
#define SMOKE_SHELL 247u                     /* its shell */
#define SMOKE_DAMAGE 261u                    /* the shell's damage */
static uint32_t g_spotShell, g_spotDamage;
static HfRef g_spotter;
static HfSet g_spotSet[12];
static HfEntityDef g_spotDef = {
    .size = sizeof(HfEntityDef),
    .name = "yui/fire_mission/spotting_round",
    .base = SMOKE_BARRAGE,
    .set = g_spotSet,
    .own_all = 1,
};
static void SpotRowsReady(HfMod* mod, HfEvent event, void* user)
{
    HfProjectileSettings* row = g_spotShell ? (HfProjectileSettings*)g_hf->projectile_row(g_spotShell) : NULL;
    HfDamageInfo* dmg = g_spotDamage ? (HfDamageInfo*)g_hf->damage_row(g_spotDamage) : NULL;
    if (!row || !dmg) { g_hf->log(g_mod, "spotting round: its rows are not there (the smoke strike's shell may still fall)"); return; }
    row->impact_explosion = 0;
    row->expiry_explosion = 0;
    dmg->standard_damage = dmg->durable_damage = 0;
    g_hf->log(g_mod, "spotting round ready: shell type %u, no blast, no damage", g_spotShell);
}
static uint64_t g_payload[1];
/* The payload for the stratagem, or NULL (then the smoke strike's own). */
static const uint64_t* SpottingPayload(const HfApi* hf, HfMod* mod)
{
    HfDamage d = {sizeof d};
    d.base = SMOKE_DAMAGE;
    d.name = "yui/fire_mission/spotting_round_damage";
    g_spotDamage = hf->add_damage(mod, &d);
    HfProjectile p = {sizeof p};
    p.base = SMOKE_SHELL;
    p.name = "yui/fire_mission/spotting_round_shell";
    p.damage = g_spotDamage;
    g_spotShell = g_spotDamage ? hf->add_projectile(mod, &p) : 0;
    if (!g_spotShell) { hf->log(mod, "spotting round: the shell could not be added (the stratagem keeps the smoke)"); return NULL; }
    int n = 0;
    g_spotSet[n++] = (HfSet){HF_FIELD(Bombardment, salvos), HF_U32(1)};
    g_spotSet[n++] = (HfSet){HF_FIELD(Bombardment, shells_per_salvo), HF_U32(1)};
    g_spotSet[n++] = (HfSet){HF_FIELD_AT(Bombardment, shells, 0), HF_U32(g_spotShell)};
    for (int i = 1; i < 8; i++) g_spotSet[n++] = (HfSet){HF_FIELD_AT(Bombardment, shells, i), HF_U32(0)};
    g_spotSet[n] = (HfSet){0};
    g_spotter = hf->register_entity(mod, &g_spotDef);
    if (!g_spotter) { hf->log(mod, "spotting round: the barrage copy was refused (the stratagem keeps the smoke)"); return NULL; }
    hf->on(mod, HF_EVENT_PROJECTILES_READY, SpotRowsReady, NULL);
    g_payload[0] = g_spotter;
    return g_payload;
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* hf, HfMod* mod)
{
    if (hf->version < 11 || hf->size <= offsetof(HfApi, spawn_projectile) || !hf->spawn_projectile) return 1;
    g_hf = hf;
    g_mod = mod;
    LoadSettings();
    srand(GetTickCount());
    HfStratagem fm = {sizeof fm};
    fm.base = HF_STRATAGEM_ORBITAL_SMOKE_STRIKE;  /* a pickable base (the flare is not); its payload is the spotting round */
    fm.name = "yui/fire_mission/fire_mission";
    fm.title = "Fire Mission";
    fm.description = "Calls every mortar sentry you have out onto the marked spot, whether or not it can see it: each sentry in "
                     "range shells the area with its own rounds.";
    fm.code = g_code;
    fm.icon = "yui/fire_mission/icons/fire_mission";  /* hellforge-data.json builds it from images/fire_mission.png */
    fm.cooldown = g_cooldown;
    fm.uses = g_uses;
    const uint64_t* payload = g_smoke ? NULL : SpottingPayload(hf, mod);
    if (payload) { fm.payload = payload; fm.payload_count = 1; }
    g_type = hf->add_stratagem(mod, &fm);
    if (!g_type) hf->log(mod, "Fire Mission: the stratagem was refused (%s)", hf->last_log ? hf->last_log(mod) : "");
    int ok = hf->subscribe(mod, HF_ON_STRATAGEM, OnStratagem, NULL, 0) & hf->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) &
             hf->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) & hf->subscribe(mod, HF_ON_MISSION_START, OnMission, NULL, 0) &
             hf->subscribe(mod, HF_ON_MISSION_END, OnMission, NULL, 0);
    if (!ok || !hf->on_game_thread(mod, Tick, NULL)) { hf->log(mod, "Fire Mission: events or the game thread unavailable"); return 1; }
    g_inMission = hf->game_state(mod) == HF_STATE_MISSION;
    hf->log(mod, "Fire Mission 0.1.3: stratagem %u, code %s, cooldown %.0f s; %d salvos of %d shells, %.1f s apart, %.0f m scatter",
        g_type, g_code, (double)g_cooldown, g_salvos, g_rounds, (double)g_salvoInterval, (double)g_spread);
    return 0;
}
