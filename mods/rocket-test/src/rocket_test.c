/* Rocket Sentry Test (yui.rocket-test): research for Veterancy System's A/MLS-4X Rocket Sentry. Not for release.
 *
 * 0.2.0: the P-33 Missile Pistol's warhead on the Rocket Sentry's own rocket. The P-33's missile itself cannot be fired
 * by a sentry (0.1.0: the P-33 fires a 1 m/s launch shell, projectile 152 or 127, and the game attaches the flying
 * missile to it in a way the data does not show; spawning its missile entity (projectile_entity) gave unloaded,
 * targetless missiles that climbed away). So the rocket (projectile 320: 100 m/s, straight) is copied (add_projectile)
 * with the P-33's direct-hit damage (row 95: 1000, AP 5) and its impact explosion (row 154: 300 damage, AP 3, radius
 * 2 / 4 / 12 m, damage row 563), against the rocket's own (row 232: 525, AP 5; explosion 315: 150, 1 / 4 / 6 m). The
 * rocket's rows are shared with a manned turret and an enemy rocket soldier, so they stay untouched. The Rocket Sentry
 * fires the copy from its next call-in: its projectile, and any magazine pattern slot or first round naming 320.
 * It logs every rocket and P-33 blast (rows 315 and 154) and the Rocket Sentry's kills. Settings in rocket-test.ini.
 *
 * 0.3.2: HellForge's homing only scans for enemies while a homing shot is in the air (ActHomingTick: ActScanEnemies
 * runs when g_actTrackedCount > 0; 512 health-record slots an update, positions older than 0.5 s ignored), so each
 * rocket finds its target 0.1-1 s after launch: 10-100 m at 100 m/s, past most targets. rocket_speed slows the warhead
 * rocket (its own row; 60 m/s by default) to give the scan time and tighten its turn.
 * 0.3.1: HellForge's homing (native/mod-loader/actions.c ActSteer) turns a shot straight at its target's position now,
 * with no lead, by at most turn_rate a second, and picks the target again every 0.1 s (the one most in its path) unless
 * told to keep the first. At 100 m/s, 120 deg/s is a turn ~48 m across, so the rockets curved late and swung past; the
 * defaults are now 540 deg/s (~21 m), keep the first target, armed after 3 m, a 30 deg cone, 0.6 m above the feet.
 * 0.3.0, two level ideas:
 *   Hunter-Killer: a homing rule (HfApi.homing) on the warhead rocket. HellForge's shot rules take "this machine's
 *     player's own shots", fired by the listed weapons; whether a sentry's shots count is what this finds out
 *     (homing_sources 1: the Rocket Sentry's only; 0: any of the player's, the Last Stand's rockets included).
 *   Last Stand: each Rocket Sentry of yours (its beacon paired with the sentry that spawns after it, as Fire Mission
 *     does) is watched; when it is destroyed (HF_ON_KILL), or half a second before its lifetime runs out, it fires
 *     last_stand_rockets warhead rockets in a ring around itself, angled down to land 15-20 m out, then a
 *     last_stand_blast explosion goes off where it stood (spawn_projectile, spawn_explosion: host). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "hellforge_mod.h"
#include "hellforge_components.h"
#include "hellforge_content.h"

#define VERSION "0.3.2"
#define ROCKET HF_ENTITY_AMLS4X_ROCKET_SENTRY
#define ROCKET_ROUND 320u   /* HF_PROJECTILE: the Rocket Sentry's rocket */
#define ROCKET_BLAST 315u
#define P33_BLAST 154u

static const HfApi* g_hf;
static HfMod* g_mod;
static int s_apply = 1, s_damage = 95, s_explosion = 154;
static uint32_t g_type;   /* the warhead rocket (0: refused) */
static int g_inMission, g_rowReady;
static unsigned g_blast[2], g_kills;
static double g_clock;
static int s_homing = 1, s_homingSources = 1, s_lastStand = 1, s_lsRockets = 8, s_lsBlast = 154;
static float s_turn = 540, s_cone = 30, s_range = 120, s_arm = 3, s_aim = 0.6f, s_lsPitch = -0.12f;
static int s_keep = 1;
static float s_speed = 60;  /* the warhead rocket's speed, m/s (the rocket's own: 100) */
static uint32_t g_homing;
typedef struct Beacon { float pos[3]; double at; int used; } Beacon;
static Beacon g_beacons[8];
typedef struct Sentry { uint32_t entity; float pos[3]; double until; int done; } Sentry;
static Sentry g_sentries[8];
typedef struct Shot { double at; uint32_t type; float pos[3], dir[3]; int tries, blast; } Shot;
static Shot g_shots[64];
static int g_nshots;

static FILE* OpenUtf8(const char* path, const wchar_t* mode)
{
    wchar_t w[MAX_PATH * 2];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, MAX_PATH * 2)) return NULL;
    return _wfopen(w, mode);
}
static void LoadSettings(void)
{
    char path[MAX_PATH * 2], line[256];
    snprintf(path, sizeof path, "%s\\rocket-test.ini", g_hf->mod_folder(g_mod));
    FILE* f = OpenUtf8(path, L"r");
    if (!f) { g_hf->log(g_mod, "settings: no rocket-test.ini, so the built-in ones"); return; }
    g_hf->log(g_mod, "settings: read %s", path);
    while (fgets(line, sizeof line, f))
    {
        char* eq = strchr(line, '=');
        if (line[0] == ';' || !eq) continue;
        *eq = 0;
        for (char* e = eq - 1; e >= line && (*e == ' ' || *e == '\t'); e--) *e = 0;
        double v = strtod(eq + 1, NULL);
        if (!strcmp(line, "apply")) s_apply = v != 0;
        else if (!strcmp(line, "warhead_damage")) s_damage = (int)v;
        else if (!strcmp(line, "warhead_explosion")) s_explosion = (int)v;
        else if (!strcmp(line, "homing")) s_homing = v != 0;
        else if (!strcmp(line, "homing_sources")) s_homingSources = (int)v;
        else if (!strcmp(line, "homing_turn_rate")) s_turn = (float)v;
        else if (!strcmp(line, "homing_cone")) s_cone = (float)v;
        else if (!strcmp(line, "homing_range")) s_range = (float)v;
        else if (!strcmp(line, "homing_arm_distance")) s_arm = (float)v;
        else if (!strcmp(line, "homing_aim_height")) s_aim = (float)v;
        else if (!strcmp(line, "homing_keep_target")) s_keep = v != 0;
        else if (!strcmp(line, "rocket_speed")) s_speed = (float)(v < 10 ? 10 : v > 300 ? 300 : v);
        else if (!strcmp(line, "last_stand")) s_lastStand = v != 0;
        else if (!strcmp(line, "last_stand_rockets")) s_lsRockets = v < 0 ? 0 : v > 32 ? 32 : (int)v;
        else if (!strcmp(line, "last_stand_blast")) s_lsBlast = (int)v;
        else if (!strcmp(line, "last_stand_pitch")) s_lsPitch = (float)v;
    }
    fclose(f);
}

/* The copy's impact blast: the P-33's explosion row */
static void ProjectilesReady(HfMod* mod, HfEvent event, void* user)
{
    if (!g_type) return;
    HfProjectileSettings* row = (HfProjectileSettings*)g_hf->projectile_row(g_type);
    if (!row) { g_hf->log(g_mod, "warhead rocket %u: its row is not there", g_type); g_type = 0; return; }
    HfField impact = HF_FIELD(ProjectileSettings, impact_explosion), speed = HF_FIELD(ProjectileSettings, velocity);
    HfValue v = HF_U32(s_explosion), vs = HF_F32(s_speed);
    int ok = g_hf->set_live(g_mod, g_type, &impact, &v, 0);
    if (s_speed != row->velocity)
    {
        int sp = g_hf->set_live(g_mod, g_type, &speed, &vs, 0);
        g_hf->log(g_mod, "warhead rocket speed %.0f -> %.0f m/s %s", (double)row->velocity, (double)s_speed, sp ? "set" : "REFUSED");
    }
    g_rowReady = 1;
    g_hf->log(g_mod, "warhead rocket ready: type %u (copy of %u: %.0f m/s, gravity %.2f), damage row %d, impact blast row %d %s",
        g_type, ROCKET_ROUND, (double)row->velocity, (double)row->gravity, s_damage, s_explosion, ok ? "set" : "REFUSED");
    if (!ok) g_hf->log(g_mod, "  (%s)", g_hf->last_log ? g_hf->last_log(g_mod) : "");
    if (!s_homing) return;
    static const uint64_t kSentry[] = {ROCKET, 0};
    HfHoming h = {sizeof h};
    h.projectile = g_type;
    h.sources = s_homingSources ? kSentry : NULL;
    h.turn_rate = s_turn;
    h.cone = s_cone;
    h.range = s_range;
    h.arm_distance = s_arm;
    h.aim_height = s_aim;
    h.flags = s_keep ? HF_HOMING_NO_RETARGET : 0;
    g_homing = g_hf->homing(g_mod, &h);
    if (g_homing) g_hf->log(g_mod, "homing rule %u on the warhead rocket: %.0f deg/s, cone %.0f deg, %.0f m, armed after %.0f m, %.1f m above the feet, %s; shots of %s",
        g_homing, (double)s_turn, (double)s_cone, (double)s_range, (double)s_arm, (double)s_aim, s_keep ? "keeps its first target" : "re-picks every 0.1 s",
        s_homingSources ? "the Rocket Sentry only" : "any of yours");
    else g_hf->log(g_mod, "the homing rule was refused (%s)", g_hf->last_log ? g_hf->last_log(g_mod) : "");
}

static void EntitiesReady(HfMod* mod, HfEvent event, void* user)
{
    HfField proj = HF_FIELD(ProjectileWeapon, projectile), first = HF_FIELD(WeaponMagazine, first_projectile);
    HfLiveValue live;
    memset(&live, 0, sizeof live);
    g_hf->get_live(g_mod, ROCKET, &proj, &live);
    g_hf->log(g_mod, "Rocket Sentry projectile: %.0f", live.base.number);
    memset(&live, 0, sizeof live);
    int firstOk = g_hf->get_live(g_mod, ROCKET, &first, &live);
    uint32_t firstWas = firstOk ? (uint32_t)live.base.number : 0;
    g_hf->log(g_mod, "Rocket Sentry magazine first round: %s%u", firstOk ? "" : "unreadable ", firstWas);
    if (!s_apply || !g_type) { g_hf->log(g_mod, s_apply ? "no warhead rocket: nothing changed" : "apply = 0: nothing changed"); return; }
    HfValue v = HF_U32(g_type);
    int when = g_hf->set_live(g_mod, ROCKET, &proj, &v, 0);
    int slots = 0;
    for (int i = 0; i < 32; i++)
    {
        HfField f = HF_FIELD_AT(WeaponMagazine, pattern, i);
        memset(&live, 0, sizeof live);
        if (!g_hf->get_live(g_mod, ROCKET, &f, &live)) continue;
        if ((uint32_t)live.base.number == ROCKET_ROUND && g_hf->set_live(g_mod, ROCKET, &f, &v, 0)) slots++;
    }
    int firstSet = firstWas == ROCKET_ROUND && g_hf->set_live(g_mod, ROCKET, &first, &v, 0);
    if (when) g_hf->log(g_mod, "Rocket Sentry fires the warhead rocket (type %u) from its next call-in; magazine: %d pattern slot%s, first round %s",
        g_type, slots, slots == 1 ? "" : "s", firstSet ? "changed" : "not the rocket");
    else g_hf->log(g_mod, "Rocket Sentry: the round change was refused (%s)", g_hf->last_log ? g_hf->last_log(g_mod) : "");
}

/* ---- Last Stand -------------------------------------------------------------------------------------------- */
static void Queue(double at, uint32_t type, const float pos[3], const float dir[3], int blast)
{
    if (g_nshots >= (int)(sizeof g_shots / sizeof g_shots[0])) return;
    Shot* q = &g_shots[g_nshots++];
    q->at = at;
    q->type = type;
    memcpy(q->pos, pos, sizeof q->pos);
    if (dir) memcpy(q->dir, dir, sizeof q->dir);
    q->tries = 0;
    q->blast = blast;
}
static void LastStand(Sentry* s, const char* why)
{
    if (s->done) return;
    s->done = 1;
    if (!s_lastStand || !g_inMission) return;
    uint32_t type = g_type ? g_type : ROCKET_ROUND;
    g_hf->log(g_mod, "Last Stand (%s): Rocket Sentry %u at (%.0f, %.0f, %.0f) fires %d rockets, then a row %d blast", why, s->entity,
        (double)s->pos[0], (double)s->pos[1], (double)s->pos[2], s_lsRockets, s_lsBlast);
    for (int i = 0; i < s_lsRockets; i++)
    {
        double a = 6.283185307 * i / (s_lsRockets ? s_lsRockets : 1);
        float dir[3] = {(float)cos(a), (float)sin(a), s_lsPitch};
        float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        for (int k = 0; k < 3; k++) dir[k] /= len;
        float from[3] = {s->pos[0] + dir[0] * 1.5f, s->pos[1] + dir[1] * 1.5f, s->pos[2] + 2.5f};
        Queue(g_clock + 0.08 * i, type, from, dir, 0);
    }
    if (s_lsBlast > 0) Queue(g_clock + 0.08 * s_lsRockets + 0.3, (uint32_t)s_lsBlast, s->pos, NULL, 1);
}
static void RunShots(void)
{
    for (int i = 0; i < g_nshots; )
    {
        Shot* q = &g_shots[i];
        if (q->at > g_clock) { i++; continue; }
        int r = q->blast ? g_hf->spawn_explosion(g_mod, q->type, q->pos) : g_hf->spawn_projectile(g_mod, q->type, q->pos, q->dir);
        if (r == HF_ACT_WAITING && ++q->tries < 60) { q->at = g_clock + 0.1; i++; continue; }
        if (!r) g_hf->log(g_mod, "Last Stand %s refused (%s)", q->blast ? "blast" : "rocket", g_hf->last_log ? g_hf->last_log(g_mod) : "");
        *q = g_shots[--g_nshots];
    }
}
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* e = (const HfSpawnEvent*)info;
    if (e->resource != ROCKET || !g_inMission) return;
    int best = -1;
    for (int i = 0; i < 8; i++)
        if (!g_beacons[i].used && g_beacons[i].at > 0 && g_clock - g_beacons[i].at < 40 && (best < 0 || g_beacons[i].at > g_beacons[best].at)) best = i;
    if (best < 0) { g_hf->log(g_mod, "Rocket Sentry %u spawned with no beacon of yours: not watched", e->entity); return; }
    g_beacons[best].used = 1;
    int slot = 0;
    for (int i = 1; i < 8; i++) if (g_sentries[i].done || g_sentries[i].until < g_sentries[slot].until) slot = i;
    HfField life = HF_FIELD(HellpodPayload, lifetime);
    HfLiveValue lv;
    memset(&lv, 0, sizeof lv);
    double lifetime = g_hf->get_live(g_mod, ROCKET, &life, &lv) && lv.base.number > 0 ? lv.base.number : 150;
    Sentry* s = &g_sentries[slot];
    s->entity = e->entity;
    memcpy(s->pos, g_beacons[best].pos, sizeof s->pos);
    s->until = g_clock + lifetime;
    s->done = 0;
    g_hf->log(g_mod, "Rocket Sentry %u is out at (%.0f, %.0f, %.0f) for about %.0f s", e->entity, (double)s->pos[0], (double)s->pos[1], (double)s->pos[2], lifetime);
}
static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfKillEvent* k = (const HfKillEvent*)info;
    for (int i = 0; i < 8; i++)
        if (g_sentries[i].entity && g_sentries[i].entity == k->victim)
        {
            g_hf->log(g_mod, "Rocket Sentry %u killed (dealer %u, %.0f s before its time ran out)%s", k->victim, k->dealer,
                g_sentries[i].until - g_clock, g_sentries[i].done ? ": Last Stand already done" : "");
            LastStand(&g_sentries[i], "destroyed");
            g_sentries[i].entity = 0;
            return;
        }
}
static void Tick(HfMod* mod, float dt, void* user)
{
    g_clock += dt;
    for (int i = 0; i < 8; i++)
        if (g_sentries[i].entity && !g_sentries[i].done && g_clock >= g_sentries[i].until - 0.5) LastStand(&g_sentries[i], "time up");
    if (g_nshots) RunShots();
}

static void OnMission(HfMod* mod, const HfEventInfo* info, void* user)
{
    memset(g_sentries, 0, sizeof g_sentries);
    memset(g_beacons, 0, sizeof g_beacons);
    g_nshots = 0;
    if (!(g_inMission = info->kind == HF_ON_MISSION_START))
    {
        g_hf->log(g_mod, "mission end: %u P-33 blasts, %u rocket blasts, %u Rocket Sentry kills", g_blast[1], g_blast[0], g_kills);
        return;
    }
    g_blast[0] = g_blast[1] = g_kills = 0;
    g_hf->log(g_mod, "mission start");
}
static void OnStratagem(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfStratagemEvent* e = (const HfStratagemEvent*)info;
    if (e->type != HF_STRATAGEM_AMLS4X_ROCKET_SENTRY || !g_inMission) return;
    g_hf->log(g_mod, "Rocket Sentry called in at (%.0f, %.0f, %.0f)", (double)e->position[0], (double)e->position[1], (double)e->position[2]);
    int slot = 0;
    for (int i = 1; i < 8; i++) if (g_beacons[i].at < g_beacons[slot].at) slot = i;
    memcpy(g_beacons[slot].pos, e->position, sizeof g_beacons[slot].pos);
    g_beacons[slot].at = g_clock;
    g_beacons[slot].used = 0;
}
static void OnExplosion(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfExplosionEvent* e = (const HfExplosionEvent*)info;
    if (!g_inMission || (e->type != (uint32_t)s_explosion && e->type != ROCKET_BLAST)) return;
    int p33 = e->type == (uint32_t)s_explosion;
    unsigned n = ++g_blast[p33];
    if (n <= 8 || n % 25 == 0)
        g_hf->log(g_mod, "%s blast #%u (row %u) at (%.0f, %.0f, %.0f)", p33 ? "P-33 warhead" : "rocket", n, e->type,
            (double)e->position[0], (double)e->position[1], (double)e->position[2]);
}
static void OnStat(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfStatEvent* e = (const HfStatEvent*)info;
    for (uint32_t i = 0; i < e->count && i < HF_STAT_SOURCES; i++)
        if (e->stat == HF_STAT_KILLS && e->sources[i].resource == ROCKET)
        {
            unsigned before = g_kills;
            g_kills += (unsigned)e->sources[i].added;
            if (before / 10 != g_kills / 10 || before == 0) g_hf->log(g_mod, "Rocket Sentry kills this mission: %u", g_kills);
        }
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* hf, HfMod* mod)
{
    if (hf->version < 11) return 1;
    g_hf = hf;
    g_mod = mod;
    LoadSettings();
    if (s_apply)
    {
        HfProjectile round = {sizeof round};
        round.base = ROCKET_ROUND;
        round.name = "yui/rocket_test/p33_warhead_rocket";
        round.damage = (uint32_t)s_damage;
        g_type = hf->add_projectile(mod, &round);
        if (!g_type) hf->log(mod, "the warhead rocket was refused (%s)", hf->last_log ? hf->last_log(mod) : "");
        hf->on(mod, HF_EVENT_PROJECTILES_READY, ProjectilesReady, NULL);
    }
    hf->on(mod, HF_EVENT_ENTITIES_READY, EntitiesReady, NULL);
    int ok = hf->subscribe(mod, HF_ON_MISSION_START, OnMission, NULL, 0) & hf->subscribe(mod, HF_ON_MISSION_END, OnMission, NULL, 0) &
             hf->subscribe(mod, HF_ON_STRATAGEM, OnStratagem, NULL, 0) & hf->subscribe(mod, HF_ON_EXPLOSION, OnExplosion, NULL, 0) &
             hf->subscribe(mod, HF_ON_STAT, OnStat, NULL, 0) & hf->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) &
             hf->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0);
    if (!ok || !hf->on_game_thread(mod, Tick, NULL)) { hf->log(mod, "Rocket Sentry Test: events unavailable"); return 1; }
    g_inMission = hf->game_state(mod) == HF_STATE_MISSION;
    hf->log(mod, "Rocket Sentry Test %s: %s; homing %s; Last Stand %s", VERSION, s_apply ? "the Rocket Sentry's rocket carries the P-33's warhead" : "logging only",
        s_homing ? "on" : "off", s_lastStand ? "on" : "off");
    return 0;
}
