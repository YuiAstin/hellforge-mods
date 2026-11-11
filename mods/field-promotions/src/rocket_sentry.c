/* Veterancy System: the A/MLS-4X Rocket Sentry (rocket_sentry.c).
 *
 * Its own kills this mission level it (rs_level2_kills, rs_level3_kills; force_level too):
 *   Level 2, Veteran: the P-33 Missile Pistol's warhead on its rocket, and a bigger magazine (rs_l2_mag). The rocket
 *     (projectile 320: 100 m/s, straight) is copied (add_projectile) with the P-33's direct-hit damage (row 95: 1000,
 *     AP 5) and impact explosion (row 154: 300 damage, AP 3, radius 2 / 4 / 12 m, its damage row 563), against the
 *     rocket's own (row 232: 525, AP 5; explosion 315: 150, 1 / 4 / 6 m). The rocket's rows are shared with a manned
 *     turret and an enemy rocket soldier, so they stay untouched. (The P-33's missile itself cannot be fired by a sentry,
 *     and HellForge's homing finds its target too late for a sentry rocket: docs in the Rocket Sentry Test mod.)
 *   Level 3, Elite: a bigger magazine still (rs_l3_mag), and Last Stand: when one of your Rocket Sentries is destroyed
 *     (not when its time runs out: then it just goes), the rockets it has left come down on the enemies around it: its
 *     magazine (read when it lands) less the rockets it fired (its own blasts, told by their source; and the shot stats
 *     while it is your only Rocket Sentry out), shared evenly between the living enemies within rs_last_stand_radius m
 *     (the remainder to the nearest; the nearest few one each when there are more enemies than rockets), each a warhead
 *     rocket dropped straight down from rs_last_stand_height m above its target, then an rs_last_stand_blast explosion
 *     goes off where it stood (host). With no enemy near, nothing drops and there is no blast.
 *     Enemies are the entities spawned whose kind runs a behaviour and is no turret or Helldiver, dropped on death (as
 *     Faelynn does), and that are on the other side from the sentry: the game's faction masks ([game+0x3326CB8], its
 *     map at +0x12050, masks at +0x12078; the test the game's own lock-on uses, game.dll+0x8D4F40, pinned) share no bit
 *     with the sentry's, so SEAF soldiers and other allies are never targets, and a unit with no faction is skipped.
 *     Their positions are read from the game's entity descriptors and scene graph, research from Helldiver
 *     Survival Rework by enochlophobianumerophobia (MIT; THIRD_PARTY_NOTICES.md), for game build 25480438 only: another
 *     build gets no Last Stand.
 * The round and magazine are live edits of the sentry's records: they reach a Rocket Sentry called in after the level.
 * Your sentries are known by their beacons (HF_ON_STRATAGEM) paired with the sentry that spawns after each, as Fire
 * Mission does; teammates' are not (their beacons are told on their machines). Tested in game with the Rocket Sentry
 * Test mod (0.2.0 / 0.3.x). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hellforge_mod.h"
#include "hellforge_components.h"
#include "hellforge_content.h"
#include "field_promotions.h"

#define ROCKET HF_ENTITY_AMLS4X_ROCKET_SENTRY
#define ROCKET_ROUND 320u   /* its rocket */
#define P33_DAMAGE 95u      /* the P-33's direct hit: 1000, AP 5 */
#define P33_BLAST 154u      /* the P-33's impact explosion: 300, radius 2 / 4 / 12 m */

static const HfApi* g_hf;
static HfMod* g_mod;
static int g_kills, g_level = 1, g_applied = 1;
static uint32_t g_warhead;   /* the warhead rocket type (0: refused) */
static int g_warheadReady;

/* settings */
/* inigen:begin */
static int s_rsL2 = 30, s_rsL3 = 150, s_rsWarhead = 1, s_rsL2Mag = 32, s_rsL3Mag = 48;
static int s_rsLastStand = 1, s_rsLsBlast = 154;
static float s_rsLsRadius = 50, s_rsLsHeight = 100;
/* inigen:end */

int rs_setting(const char* key, double v)
{
#define CL(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
    if (!strcmp(key, "rs_level2_kills")) s_rsL2 = (int)CL(v, 0, 100000);
    else if (!strcmp(key, "rs_level3_kills")) s_rsL3 = (int)CL(v, 0, 100000);
    else if (!strcmp(key, "rs_warhead")) s_rsWarhead = v != 0;
    else if (!strcmp(key, "rs_l2_mag")) s_rsL2Mag = (int)CL(v, 1, 2047);
    else if (!strcmp(key, "rs_l3_mag")) s_rsL3Mag = (int)CL(v, 1, 2047);
    else if (!strcmp(key, "rs_last_stand")) s_rsLastStand = v != 0;
    else if (!strcmp(key, "rs_last_stand_radius")) s_rsLsRadius = (float)CL(v, 5, 150);
    else if (!strcmp(key, "rs_last_stand_height")) s_rsLsHeight = (float)CL(v, 10, 300);
    else if (!strcmp(key, "rs_last_stand_blast")) s_rsLsBlast = (int)CL(v, 0, 100000);
    else return 0;
    return 1;
#undef CL
}

/* inigen:begin */
void rs_write_defaults(FILE* f)
{
    fprintf(f, "\n; A/MLS-4X Rocket Sentry: its kills this mission for level 2 (Veteran) and level 3 (Elite).\nrs_level2_kills = %d\nrs_level3_kills = %d\n"
               "; Level 2: 1 its rocket carries the P-33 Missile Pistol's warhead (1000 direct, AP 5; a 300 blast, radius 2 / 4 / 12 m;\n"
               "; the rocket's own: 525, a 150 blast, 1 / 4 / 6 m); 0 off. Its magazine at level 2 and level 3 (the game's: 16).\n"
               "; Both reach a Rocket Sentry called in after the level.\nrs_warhead = %d\nrs_l2_mag = %d\nrs_l3_mag = %d\n"
               "; Level 3, Last Stand: 1 when your Rocket Sentry is destroyed (not when its time runs out), the rockets it\n"
               "; has left drop on the enemies within rs_last_stand_radius m, shared evenly (10 left and 5 enemies: 2 each),\n"
               "; from rs_last_stand_height m straight above each; never on SEAF or other allies. Then this explosion row goes\n"
               "; off where it stood (154: the P-33's). With no enemy near, nothing drops and there is no blast. 0: off.\n"
               "rs_last_stand = %d\nrs_last_stand_radius = %g\nrs_last_stand_height = %g\nrs_last_stand_blast = %d\n",
        s_rsL2, s_rsL3, s_rsWarhead, s_rsL2Mag, s_rsL3Mag, s_rsLastStand, (double)s_rsLsRadius, (double)s_rsLsHeight, s_rsLsBlast);
}
/* inigen:end */

static int Level(void)
{
    if (fp_forced_level()) return fp_forced_level();
    return g_kills >= s_rsL3 ? 3 : g_kills >= s_rsL2 ? 2 : 1;
}
int rs_level(void) { return Level(); }

/* ---- the round and the magazine ----------------------------------------------------------------------------------- */
static void Undo(void)
{
    if (g_applied == 1) return;
    HfField proj = HF_FIELD(ProjectileWeapon, projectile), mag = HF_FIELD(WeaponMagazine, capacity);
    g_hf->set_live(g_mod, ROCKET, &proj, NULL, 0);
    g_hf->set_live(g_mod, ROCKET, &mag, NULL, 0);
    g_applied = 1;
    g_hf->log(g_mod, "Rocket Sentry: back to the game's own");
}
static void Apply(int level)
{
    if (level < 2) { Undo(); return; }
    if (level == g_applied) return;
    HfField proj = HF_FIELD(ProjectileWeapon, projectile), mag = HF_FIELD(WeaponMagazine, capacity);
    int round = 0, cap;
    if (s_rsWarhead && g_warhead && g_warheadReady)
    {
        HfValue v = HF_U32(g_warhead);
        round = g_hf->set_live(g_mod, ROCKET, &proj, &v, 0) != 0;
    }
    HfValue m = HF_U32(level >= 3 ? s_rsL3Mag : s_rsL2Mag);
    cap = g_hf->set_live(g_mod, ROCKET, &mag, &m, 0) != 0;
    g_applied = level;
    g_hf->log(g_mod, "Rocket Sentry level %d: %s, magazine %d %s (a Rocket Sentry called in from now on)%s", level,
        !s_rsWarhead ? "its own rocket" : round ? "the P-33 warhead rocket" : "the warhead rocket REFUSED or not ready",
        level >= 3 ? s_rsL3Mag : s_rsL2Mag, cap ? "set" : "REFUSED", level >= 3 && s_rsLastStand ? "; Last Stand on" : "");
}

/* The warhead rocket's impact: the P-33's explosion */
static void ProjectilesReady(HfMod* mod, HfEvent event, void* user)
{
    if (!g_warhead) return;
    HfProjectileSettings* row = (HfProjectileSettings*)g_hf->projectile_row(g_warhead);
    if (!row) { g_hf->log(g_mod, "Rocket Sentry: the warhead rocket's row is not there"); g_warhead = 0; return; }
    HfField impact = HF_FIELD(ProjectileSettings, impact_explosion);
    HfValue v = HF_U32(P33_BLAST);
    g_warheadReady = g_hf->set_live(g_mod, g_warhead, &impact, &v, 0) != 0;
    g_hf->log(g_mod, "Rocket Sentry warhead rocket: type %u (copy of %u, %.0f m/s), damage row %u, impact blast %u %s", g_warhead, ROCKET_ROUND,
        (double)row->velocity, P33_DAMAGE, P33_BLAST, g_warheadReady ? "set" : "REFUSED");
    if (fp_in_mission() && g_applied >= 2) { int l = g_applied; g_applied = 1; Apply(l); }
}

/* ---- Last Stand ----------------------------------------------------------------------------------------------- */
typedef struct Beacon { float pos[3]; double at; int used; } Beacon;
static Beacon g_beacons[8];
typedef struct Sentry { uint32_t entity, side; float pos[3]; double until; int done, mag, fired, shots; } Sentry;
static Sentry g_sentries[8];
typedef struct Shot { double at; uint32_t type; float pos[3], dir[3]; int tries, blast; } Shot;
static Shot g_shots[64];
static int g_nshots;
static double g_lifetime = 150;

/* ---- the enemies (for the Last Stand's drops) ----------------------------------------------------------------------
 * Entities spawned whose kind runs a behaviour and is no turret or Helldiver, dropped on death; positions read from
 * the game's entity descriptors and scene graph (Helldiver Survival Rework's research, MIT; build 25480438 only). */
#define FOES 512
static uint32_t g_foe[FOES];
static uint64_t g_foeRes[FOES];
static unsigned g_foeNext;
static uintptr_t g_gameDll, g_exe;
static int g_readsOk = -1;
typedef struct Kind { uint64_t res; int foe; } Kind;
static Kind g_kinds[256];

static uint32_t Word(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t Quad(const uint8_t* p) { uint64_t v; memcpy(&v, p, 8); return v; }
static int AddressOk(uintptr_t a, size_t n) { return n && a >= 0x10000 && a < UINT64_C(0x800000000000) && n <= UINT64_C(0x800000000000) - a; }
static int ReadAt(uintptr_t a, void* out, size_t n) { return AddressOk(a, n) && g_hf->read(a, out, n) == 0; }
static uintptr_t Pointer(uintptr_t a) { uintptr_t v = 0; return ReadAt(a, &v, 8) && AddressOk(v, 1) ? v : 0; }
static int ReadsOk(void)
{
    if (g_readsOk >= 0) return g_readsOk;
    const char* build = g_hf->build_id ? g_hf->build_id() : NULL;
    g_gameDll = g_hf->module_base("game.dll", NULL);
    g_exe = g_hf->module_base("helldivers2.exe", NULL);
    g_readsOk = build && !strcmp(build, "25480438") && g_gameDll && g_exe;
    if (!g_readsOk) g_hf->log(g_mod, "Rocket Sentry Last Stand: enemy positions are read for game build 25480438 only (this: %s): no Last Stand",
        build ? build : "?");
    return g_readsOk;
}
static int FoeKind(uint64_t res)
{
    if (!res) return 0;
    unsigned h = (unsigned)(res ^ (res >> 29)) & 255;
    for (unsigned i = 0; i < 256; i++)
    {
        Kind* k = &g_kinds[(h + i) & 255];
        if (k->res == res) return k->foe;
        if (!k->res)
        {
            uint32_t stride;
            k->res = res;
            k->foe = g_hf->component_record("BehaviorComponentData", res, &stride) != 0 && !g_hf->component_record("TurretComponentData", res, &stride) &&
                     !g_hf->component_record("AvatarComponentData", res, &stride);
            return k->foe;
        }
    }
    return 0;
}
static void AddFoe(uint32_t entity, uint64_t res)
{
    int free_ = -1;
    for (int i = 0; i < FOES; i++)
    {
        if (g_foe[i] == entity) { g_foeRes[i] = res; return; }
        if (!g_foe[i] && free_ < 0) free_ = i;
    }
    unsigned slot = free_ >= 0 ? (unsigned)free_ : g_foeNext++ % FOES;
    g_foe[slot] = entity;
    g_foeRes[slot] = res;
}
static int Lookup(uintptr_t at, uint32_t key, uint32_t limit, uint32_t* index)
{
    uint8_t h[20], row[8];
    if (!ReadAt(at, h, sizeof h)) return 0;
    uintptr_t entries = (uintptr_t)Quad(h);
    uint32_t n = Word(h + 8), empty = Word(h + 12), mult = Word(h + 16);
    if (!AddressOk(entries, 8) || !n || n > limit || (n & (n - 1)) || key == empty) return 0;
    for (unsigned i = 0; i < n && i < 128; i++)
    {
        if (!ReadAt(entries + 8u * ((key * mult + i) & (n - 1)), row, 8)) return 0;
        if (Word(row) == key) { *index = Word(row + 4); return *index != UINT32_MAX; }
        if (Word(row) == empty) return 0;
    }
    return 0;
}
/* An entity's descriptor: its resource (+0), entity (+8), unit (+12) */
static int Descriptor(uint32_t entity, uint8_t out[24])
{
    if (!entity || entity == UINT32_MAX || entity == 0x7FFF) return 0;
    uintptr_t root = Pointer(g_gameDll + 0x346BF98);
    uint32_t index;
    if (!root || !Lookup(root + 0xF1AEB0, entity, 1048576, &index) || index >= 262144) return 0;
    return ReadAt(root + 0xF32F18 + 24u * index, out, 24) && Word(out + 8) == entity;
}
/* A unit's root position in its scene graph (64-byte matrices, translation at +0x30) */
static int UnitRoot(uint32_t unit, float out[3])
{
    uintptr_t registry = Pointer(g_exe + 0x1A100F0), generations, objects, object, nodes;
    uint32_t count, id, slot = unit & 0x3FFFFF;
    uint8_t generation;
    if (!registry || !ReadAt(registry + 0x98, &count, 4) || count > 0x400000 || slot >= count ||
        !(generations = Pointer(registry + 0xA0)) || !ReadAt(generations + slot, &generation, 1) || generation != (uint8_t)(unit >> 22) ||
        !(objects = Pointer(registry + 0x88)) || !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit ||
        !ReadAt(object + 0x70, &count, 4) || count == 0 || count > 4096 || !(nodes = Pointer(object + 0x88)))
        return 0;
    if (!ReadAt(nodes + 48, out, 12)) return 0;
    for (int i = 0; i < 3; i++) if (!isfinite(out[i]) || fabsf(out[i]) > 100000) return 0;
    return 1;
}
static int FoePos(int i, float p[3])
{
    uint8_t id[24];
    uint32_t e = g_foe[i];
    if (!e || !Descriptor(e, id) || Quad(id) != g_foeRes[i]) { if (e) g_foe[i] = 0; return 0; }
    return UnitRoot(Word(id + 12), p);
}

/* Sides: the faction mask of an entity (0: none or not readable). The game's lock-on treats two units as the same side
 * when their masks share a bit (game.dll+0x8D4F40); both sites below are checked before the masks are trusted. */
static int g_factionOk = -1;
static uint32_t g_friendly;   /* the last side read off a sentry of yours */
static int FactionOk(void)
{
    if (g_factionOk >= 0) return g_factionOk;
    g_factionOk = ReadsOk() && g_hf->size > offsetof(HfApi, code_matches) && g_hf->code_matches &&
        g_hf->code_matches(g_gameDll + 0x8D4F5A, "8B 0D ?? ?? ?? ?? 3B D1 74 5F 48 8B 35 ?? ?? ?? ?? 45 33 DB 45 8B D3 8B 9E 58 20 01 00 8B AE 60 20 01 00") &&
        g_hf->code_matches(g_gameDll + 0x8D5074, "4C 8B BE 78 20 01 00 44 8B 96 58 20 01 00 8B 9E 60 20 01 00 8B B6 5C 20 01 00");
    if (!g_factionOk) g_hf->log(g_mod, "Rocket Sentry Last Stand: the game's faction code is not the reviewed one: Last Stand off (it cannot tell allies from enemies)");
    return g_factionOk;
}
static uint32_t Side(uint32_t entity)
{
    uintptr_t mgr = Pointer(g_gameDll + 0x3326CB8), masks;
    uint32_t index, mask = 0;
    if (!entity || !mgr || !Lookup(mgr + 0x12050, entity, 1048576, &index) || index >= 1048576 || !(masks = Pointer(mgr + 0x12078))) return 0;
    return ReadAt(masks + 4u * index, &mask, 4) ? mask : 0;
}

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
    if (!s_rsLastStand || !fp_in_mission() || Level() < 3) return;
    if (!FactionOk()) return;
    uint32_t side = s->side ? s->side : g_friendly;
    if (!side) { g_hf->log(g_mod, "Rocket Sentry Last Stand: the sentry's side is not known: nothing drops"); return; }
    double now = fp_clock();
    uint32_t type = g_warhead && g_warheadReady && s_rsWarhead ? g_warhead : ROCKET_ROUND;
    int fired = s->fired > s->shots ? s->fired : s->shots;
    int left = s->mag - fired;
    if (left < 0) left = 0;
    /* the living enemies within the radius, nearest first */
    enum { MAXT = 64 };
    float tp[MAXT][3], td[MAXT];
    int nt = 0, allies = 0, sideless = 0;
    float r2 = s_rsLsRadius * s_rsLsRadius;
    if (left > 0 && ReadsOk())
        for (int i = 0; i < FOES; i++)
        {
            float p[3];
            if (!g_foe[i] || !FoePos(i, p)) continue;
            float dx = p[0] - s->pos[0], dy = p[1] - s->pos[1], dz = p[2] - s->pos[2], d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > r2) continue;
            uint32_t m = Side(g_foe[i]);
            if (!m) { sideless++; continue; }       /* a unit with no side */
            if (m & side) { allies++; continue; }   /* an ally (SEAF...) */
            int at;
            if (nt < MAXT) at = nt++;
            else if (d2 >= td[MAXT - 1]) continue;  /* full: only nearer ones replace the farthest */
            else at = MAXT - 1;
            while (at > 0 && td[at - 1] > d2) { memcpy(tp[at], tp[at - 1], sizeof tp[0]); td[at] = td[at - 1]; at--; }
            memcpy(tp[at], p, sizeof tp[0]);
            td[at] = d2;
        }
    int targets = nt < left ? nt : left;
    g_hf->log(g_mod, "Rocket Sentry Last Stand (%s) at (%.0f, %.0f, %.0f): magazine %d, fired %d (blasts %d, shots %d): %d left, %d enem%s within %.0f m (skipped: %d allies, %d with no side)",
        why, (double)s->pos[0], (double)s->pos[1], (double)s->pos[2], s->mag, fired, s->fired, s->shots, left, nt, nt == 1 ? "y" : "ies", (double)s_rsLsRadius, allies, sideless);
    double t = now;
    if (targets > 0)
    {
        int each = left / targets, extra = left % targets;
        for (int k = 0; k < targets; k++)
        {
            int n = each + (k < extra ? 1 : 0);
            for (int j = 0; j < n; j++)
            {
                float jitter = j == 0 ? 0.0f : 0.8f;
                double a = 2.399963 * j;  /* golden angle: a few rockets on one enemy spread a little */
                float from[3] = {tp[k][0] + jitter * (float)cos(a), tp[k][1] + jitter * (float)sin(a), tp[k][2] + s_rsLsHeight};
                static const float down[3] = {0.0f, 0.0f, -1.0f};
                Queue(t, type, from, down, 0);
                t += 0.06;
            }
        }
        g_hf->log(g_mod, "  %d rockets on %d enem%s, %d each%s, from %.0f m up", left, targets, targets == 1 ? "y" : "ies", each,
            extra ? " (the nearest one more)" : "", (double)s_rsLsHeight);
    }
    else
    {
        g_hf->log(g_mod, "  no enemy near: nothing drops, no blast");
        return;
    }
    if (s_rsLsBlast > 0) Queue(t + 0.3, (uint32_t)s_rsLsBlast, s->pos, NULL, 1);
}
static void RunShots(double now)
{
    for (int i = 0; i < g_nshots; )
    {
        Shot* q = &g_shots[i];
        if (q->at > now) { i++; continue; }
        int r = q->blast ? g_hf->spawn_explosion(g_mod, q->type, q->pos) : g_hf->spawn_projectile(g_mod, q->type, q->pos, q->dir);
        if (r == HF_ACT_WAITING && ++q->tries < 60) { q->at = now + 0.1; i++; continue; }
        if (!r) g_hf->log(g_mod, "Rocket Sentry Last Stand %s refused (%s)", q->blast ? "blast" : "rocket", g_hf->last_log ? g_hf->last_log(g_mod) : "");
        *q = g_shots[--g_nshots];
    }
}
static void OnStratagem(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfStratagemEvent* e = (const HfStratagemEvent*)info;
    if (e->type != HF_STRATAGEM_AMLS4X_ROCKET_SENTRY || !fp_in_mission()) return;
    int slot = 0;
    for (int i = 1; i < 8; i++) if (g_beacons[i].at < g_beacons[slot].at) slot = i;
    memcpy(g_beacons[slot].pos, e->position, sizeof g_beacons[slot].pos);
    g_beacons[slot].at = fp_clock();
    g_beacons[slot].used = 0;
}
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* e = (const HfSpawnEvent*)info;
    if (e->resource != ROCKET)
    {
        if (s_rsLastStand && e->resource && FoeKind(e->resource)) AddFoe(e->entity, e->resource);
        return;
    }
    if (!fp_in_mission()) return;
    double now = fp_clock();
    int best = -1;
    for (int i = 0; i < 8; i++)
        if (!g_beacons[i].used && g_beacons[i].at > 0 && now - g_beacons[i].at < 40 && (best < 0 || g_beacons[i].at > g_beacons[best].at)) best = i;
    if (best < 0) return;  /* a teammate's */
    g_beacons[best].used = 1;
    int slot = 0;
    for (int i = 1; i < 8; i++) if (!g_sentries[i].entity || g_sentries[i].done || g_sentries[i].until < g_sentries[slot].until) slot = i;
    Sentry* s = &g_sentries[slot];
    s->entity = e->entity;
    s->side = 0;   /* read on the next ticks, once the game has given it one */
    memcpy(s->pos, g_beacons[best].pos, sizeof s->pos);
    s->until = now + g_lifetime;
    s->done = 0;
    s->fired = s->shots = 0;
    HfField cap = HF_FIELD(WeaponMagazine, capacity);
    HfLiveValue lv;
    memset(&lv, 0, sizeof lv);
    s->mag = g_hf->get_live(g_mod, ROCKET, &cap, &lv) && lv.now.number > 0 ? (int)lv.now.number : 16;
}
static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfKillEvent* k = (const HfKillEvent*)info;
    for (int i = 0; i < FOES; i++) if (g_foe[i] == k->victim) g_foe[i] = 0;
    for (int i = 0; i < 8; i++)
        if (g_sentries[i].entity && g_sentries[i].entity == k->victim)
        {
            LastStand(&g_sentries[i], "destroyed");
            g_sentries[i].entity = 0;
            return;
        }
}
/* A rocket of one of your sentries going off: one fired (its source is the sentry) */
static void OnExplosion(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfExplosionEvent* e = (const HfExplosionEvent*)info;
    if (!e->source || (e->type != P33_BLAST && e->type != 315u)) return;
    for (int i = 0; i < 8; i++)
        if (g_sentries[i].entity && !g_sentries[i].done && (g_sentries[i].entity == e->source || g_sentries[i].entity == e->owner))
        {
            g_sentries[i].fired++;
            return;
        }
}
/* The Rocket Sentry's shot stats: credited to a sentry of yours only while it is your only one out */
void rs_shots(uint64_t resource, int added)
{
    if (resource != ROCKET || added <= 0) return;
    int alive = -1, n = 0;
    for (int i = 0; i < 8; i++) if (g_sentries[i].entity && !g_sentries[i].done) { alive = i; n++; }
    if (n == 1) g_sentries[alive].shots += added;
}
void rs_tick(double now)
{
    for (int i = 0; i < 8; i++)
    {
        Sentry* s = &g_sentries[i];
        if (!s->entity || s->done) continue;
        if (!s->side && s_rsLastStand && FactionOk() && (s->side = Side(s->entity)) != 0) g_friendly = s->side;
        /* its time up: it goes quietly (the game's own removal is no Last Stand) */
        if (now >= s->until - 0.5) { s->done = 1; g_hf->log(g_mod, "Rocket Sentry: its time is up (no Last Stand)"); }
    }
    if (g_nshots) RunShots(now);
}

/* ---- levels ------------------------------------------------------------------------------------------------------- */
static void CheckLevel(void)
{
    int level = Level();
    if (level == g_level) return;
    int up = level > g_level;
    g_level = level;
    g_hf->log(g_mod, "A/MLS-4X Rocket Sentry: level %d (%d kills this mission)", level, g_kills);
    if (fp_in_mission()) Apply(level);
    if (up)
    {
        static const char* kLines[2] = {"Missile warheads issued. Your next Rocket Sentry carries them.",
            "Last Stand authorised. Your Rocket Sentries go out fighting."};
        fp_toast_line("A/MLS-4X Rocket Sentry", level, kLines[level >= 3]);
    }
}
void rs_kills(uint64_t resource, int added)
{
    if (resource != ROCKET || added <= 0) return;
    g_kills += added;
    CheckLevel();
}
void rs_reset(void)
{
    g_kills = 0;
    Undo();
    memset(g_beacons, 0, sizeof g_beacons);
    memset(g_sentries, 0, sizeof g_sentries);
    memset(g_foe, 0, sizeof g_foe);
    g_nshots = 0;
    g_level = Level();
    if (fp_in_mission())
    {
        HfField life = HF_FIELD(HellpodPayload, lifetime);
        HfLiveValue lv;
        memset(&lv, 0, sizeof lv);
        g_lifetime = g_hf->get_live(g_mod, ROCKET, &life, &lv) && lv.base.number > 0 ? lv.base.number : 150;
        Apply(g_level);  /* a forced level from the mission's start */
    }
}

int rs_init(const HfApi* hf, HfMod* mod)
{
    g_hf = hf;
    g_mod = mod;
    if (s_rsWarhead)
    {
        HfProjectile round = {sizeof round};
        round.base = ROCKET_ROUND;
        round.name = "yui/field_promotions/p33_warhead_rocket";
        round.damage = P33_DAMAGE;
        g_warhead = hf->add_projectile(mod, &round);
        if (!g_warhead) hf->log(mod, "Rocket Sentry: the warhead rocket was refused (%s)", hf->last_log ? hf->last_log(mod) : "");
        hf->on(mod, HF_EVENT_PROJECTILES_READY, ProjectilesReady, NULL);
    }
    int ok = hf->subscribe(mod, HF_ON_STRATAGEM, OnStratagem, NULL, 0) & hf->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) &
             hf->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) & hf->subscribe(mod, HF_ON_EXPLOSION, OnExplosion, NULL, 0);
    hf->log(mod, "A/MLS-4X Rocket Sentry: level 2 at %d kills, level 3 at %d; P-33 warhead %s, magazine %d / %d; Last Stand %s (its rockets left on the enemies within %.0f m, from %.0f m up; blast %d)",
        s_rsL2, s_rsL3, s_rsWarhead ? "on" : "off", s_rsL2Mag, s_rsL3Mag, s_rsLastStand ? "on" : "off", (double)s_rsLsRadius, (double)s_rsLsHeight, s_rsLsBlast);
    return ok;
}
