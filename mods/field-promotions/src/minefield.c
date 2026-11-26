/* Veterancy System: the MD-6 Anti-Personnel Minefield (minefield.c).
 *
 * Its kills this mission level it (md_level2_kills, md_level3_kills; force_level too). The game credits a mine's kills to
 * the minefield (the mission stats' kills from its deployer, 0xF00C349422E8E320: Minefield Probe 0.1.0, 2026-11-26).
 *   Level 2, Veteran: a mine that goes off grows back on its spot md_regrow_seconds later, for md_regrow_window seconds
 *     after the field lands (0: the whole mission); a field never holds more than it landed with.
 *   Level 3, Elite: an enemy within md_launch_range m of a mine sets it off: the mine launches a shot straight up that
 *     then turns onto that enemy and lands the mine's own blast (explosion 366).
 * How: from level 2, a field of yours is taken over as it lands. Its mines are the game's (the minefield manager,
 * game.dll+0x3326958: fields counted at +0x848, records at +0x880, 0x15DC bytes each; a record's mines from +0x14, 0x74
 * bytes each: +0 the mine's unit, +4 its place, +0x10 its state, 1 armed, 2 its fuse running, 0 blown; W's research).
 * Each armed one is switched off (state 0, its unit taken away the game's way: Minefield Probe 0.2.0, played: a
 * switched-off mine never went off) and a mine of the mod's own put on its spot: the MD-6's mine unit as its look
 * (WorldSpawnUnit, as W's Jack in the Box), the MD-6's blast when an enemy comes within md_trigger m (spawn_explosion).
 * Growing a mine back in the game's own record does not work (the probe's F6: armed again, never went off), so the
 * mod keeps every mine of a field it took. Kills of its own blasts are counted to the minefield (the stats give them to
 * you): an enemy that dies within 6 m of one of them within 1.5 s.
 * The launch shot is a copy of the Rocket Sentry's rocket (row 320, a free stratagem's) slowed to md_launch_speed m/s,
 * its hit dealing next to nothing and its impact the MD-6's blast; it is steered in the game's projectile pool as the
 * Destroyer kit steers her grenades ([game+0x347CEA8]: active +0x28, shots fired +0x30, flags u16 +0x203C, flight
 * +0x3040 x 0x70 with velocity at +0x0C, type +0xE5040 x 4; build 25480438): md_launch_free s straight up, then
 * md_launch_turn degrees a second onto its enemy, and within md_launch_catch m of any enemy straight into it.
 * Solo or host, game build 25480438 only. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hellforge_mod.h"
#include "hellforge_components.h"
#include "hellforge_content.h"
#include "hellforge_functions.h"
#include "field_promotions.h"

#define MD6 HF_ENTITY_MD6_ANTIPERSONNEL_MINEFIELD
#define MD6_MINE_UNIT 0xEE89CC10D80B6AFBull
#define UNIT_TYPE 0xE0A48D0BE9A7453Full
#define MD6_BLAST 366u
#define MD6_DAMAGE 406u
#define LAUNCH_BASE 320u   /* the Rocket Sentry's rocket */
#define FIELD_STRIDE 0x15DCull
#define MINE_STRIDE 0x74ull
#define FIELDS 6
#define MINES 48

static const HfApi* g_hf;
static HfMod* g_mod;
static uintptr_t g_game;
static int g_kills, g_level = 1, g_ok = -1, g_removeOk;
static uint32_t g_launchType, g_launchDamage;
static int g_launchReady;
static double g_now;

/* inigen:begin */
static int s_mdL2 = 30, s_mdL3 = 150, s_mdRegrow = 1, s_mdLaunch = 1;
static float s_mdRegrowSeconds = 10, s_mdRegrowWindow = 180, s_mdTrigger = 2.5f, s_mdFuse = 0.4f;
static float s_mdLaunchRange = 10, s_mdLaunchSpeed = 35, s_mdLaunchFree = 0.3f, s_mdLaunchTurn = 540, s_mdLaunchCatch = 2.5f;
/* inigen:end */

int md_setting(const char* key, double v)
{
#define CL(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
    if (!strcmp(key, "md_level2_kills")) s_mdL2 = (int)CL(v, 0, 100000);
    else if (!strcmp(key, "md_level3_kills")) s_mdL3 = (int)CL(v, 0, 100000);
    else if (!strcmp(key, "md_regrow")) s_mdRegrow = v != 0;
    else if (!strcmp(key, "md_regrow_seconds")) s_mdRegrowSeconds = (float)CL(v, 1, 600);
    else if (!strcmp(key, "md_regrow_window")) s_mdRegrowWindow = (float)CL(v, 0, 7200);
    else if (!strcmp(key, "md_trigger")) s_mdTrigger = (float)CL(v, 0.5, 10);
    else if (!strcmp(key, "md_fuse")) s_mdFuse = (float)CL(v, 0, 5);
    else if (!strcmp(key, "md_launch")) s_mdLaunch = v != 0;
    else if (!strcmp(key, "md_launch_range")) s_mdLaunchRange = (float)CL(v, 1, 40);
    else if (!strcmp(key, "md_launch_speed")) s_mdLaunchSpeed = (float)CL(v, 5, 200);
    else if (!strcmp(key, "md_launch_free")) s_mdLaunchFree = (float)CL(v, 0, 3);
    else if (!strcmp(key, "md_launch_turn")) s_mdLaunchTurn = (float)CL(v, 30, 1440);
    else if (!strcmp(key, "md_launch_catch")) s_mdLaunchCatch = (float)CL(v, 0, 10);
    else return 0;
    return 1;
#undef CL
}

/* inigen:begin */
void md_write_defaults(FILE* f)
{
    fprintf(f, "\n; MD-6 Anti-Personnel Minefield: its kills this mission for level 2 (Veteran) and level 3 (Elite).\nmd_level2_kills = %d\nmd_level3_kills = %d\n"
               "; From level 2 a field of yours is run by the mod as it lands (the game's mines swapped for the mod's, same\n"
               "; spots, same look and blast). An enemy within md_trigger m sets a mine off after md_fuse s.\nmd_trigger = %g\nmd_fuse = %g\n"
               "; Level 2: 1 a mine that went off grows back on its spot md_regrow_seconds later, for md_regrow_window s after\n"
               "; the field lands (0: the whole mission); 0 off.\nmd_regrow = %d\nmd_regrow_seconds = %g\nmd_regrow_window = %g\n"
               "; Level 3: 1 an enemy within md_launch_range m makes a mine launch a shot straight up (md_launch_free s), which\n"
               "; then turns md_launch_turn degrees a second onto that enemy at md_launch_speed m/s and lands the mine's blast;\n"
               "; within md_launch_catch m of any enemy it turns straight into it. 0: level 3 mines go off as level 2's.\n"
               "md_launch = %d\nmd_launch_range = %g\nmd_launch_speed = %g\nmd_launch_free = %g\nmd_launch_turn = %g\nmd_launch_catch = %g\n",
        s_mdL2, s_mdL3, (double)s_mdTrigger, (double)s_mdFuse, s_mdRegrow, (double)s_mdRegrowSeconds, (double)s_mdRegrowWindow, s_mdLaunch,
        (double)s_mdLaunchRange, (double)s_mdLaunchSpeed, (double)s_mdLaunchFree, (double)s_mdLaunchTurn, (double)s_mdLaunchCatch);
}
/* inigen:end */

static int Level(void)
{
    if (fp_forced_level()) return fp_forced_level();
    return g_kills >= s_mdL3 ? 3 : g_kills >= s_mdL2 ? 2 : 1;
}
int md_level(void) { return Level(); }

static uint32_t Word(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static int AddressOk(uintptr_t a, size_t n) { return n && a >= 0x10000 && a < UINT64_C(0x800000000000) && n <= UINT64_C(0x800000000000) - a; }
static int ReadAt(uintptr_t a, void* out, size_t n) { return AddressOk(a, n) && g_hf->read(a, out, n) == 0; }
static uintptr_t Pointer(uintptr_t a) { uintptr_t v = 0; return ReadAt(a, &v, 8) && AddressOk(v, 1) ? v : 0; }
static float Dist2(const float a[3], const float b[3]) { float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; return x * x + y * y + z * z; }

static int Ok(void)
{
    if (g_ok >= 0) return g_ok;
    size_t size = 0;
    const char* build = g_hf->build_id ? g_hf->build_id() : NULL;
    g_game = g_hf->module_base("game.dll", &size);
    g_ok = build && !strcmp(build, "25480438") && g_game && size == 74727424u && g_hf->size > offsetof(HfApi, code_matches) && g_hf->code_matches &&
           hf_fn_WorldSpawnUnit(g_mod) && hf_fn_MainWorld(g_mod);
    if (g_ok) g_removeOk = g_hf->code_matches(g_game + 0x9DDA5F, "48 8b 05 a2 88 94 02 8b 16 48 8b 0d 31 e5 a8 02 4c 8b 40 08 41 ff 50 60");
    if (!g_ok) g_hf->log(g_mod, "MD-6 minefield: its fields are read for game build 25480438 only, with HellForge's unit functions (SDK 16): its levels 2 and 3 stay off");
    else if (!g_removeOk) { g_ok = 0; g_hf->log(g_mod, "MD-6 minefield: the game's unit removal is not the reviewed code: its levels 2 and 3 stay off"); }
    return g_ok;
}

/* ---- the mines' look: the MD-6's mine unit -------------------------------------------------------------------------- */
static unsigned g_models;
static uint32_t ModelSpawn(const float at[3])
{
    HfFn_WorldSpawnUnit spawn = hf_fn_WorldSpawnUnit(g_mod);
    HfFn_MainWorld mainWorld = hf_fn_MainWorld(g_mod);
    HfFn_CanGetResource can = hf_fn_CanGetResource(g_mod);
    void* world = mainWorld ? mainWorld() : NULL;
    if (!spawn || !world || !can || !(can(UNIT_TYPE, MD6_MINE_UNIT) & 1) || g_models >= 400) return 0;
    float pose[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, at[0], at[1], at[2], 1};
    uint64_t res = MD6_MINE_UNIT, name = 0;   /* the name is read through its pointer: never NULL */
    uint32_t handle = 0;
    void* unit = spawn(world, &res, &name, pose, 0, 0, 1, 0);
    if (!unit || !ReadAt((uintptr_t)unit + 8, &handle, 4)) return 0;
    g_models++;
    return handle;
}
static void ModelHide(uint32_t handle)
{
    typedef void (*DestroyFn)(void* world, uint32_t unit);
    uintptr_t api, table;
    DestroyFn destroy;
    void* world;
    if (!handle || !g_removeOk || !(api = Pointer(g_game + 0x3326308)) || !(table = Pointer(api + 8)) || !(destroy = (DestroyFn)Pointer(table + 0x60)) ||
        !(world = (void*)Pointer(g_game + 0x346BFA0)))
        return;
    destroy(world, handle);
    if (g_models) g_models--;
}

/* ---- the fields the mod runs ------------------------------------------------------------------------------------- */
typedef struct Mine { int on; float pos[3]; uint32_t unit; double boomAt, blownAt; } Mine;
typedef struct Field { int on; float centre[3]; double born; int n; Mine mine[MINES]; } Field;
static Field g_field[FIELDS];
typedef struct Beacon { float pos[3]; double at; } Beacon;
static Beacon g_beacon[4];
static unsigned g_taken, g_blasts, g_launches, g_regrown, g_ownKills;
/* the mod's own blasts, for the kills they make */
typedef struct Boom { float pos[3]; double at; } Boom;
static Boom g_boom[32];
static unsigned g_boomNext;

static int Hostile(uint32_t e, uint32_t mine)
{
    uint32_t me = fp_avatar(), a = me ? fp_side(me) : 0, b = fp_side(e);
    (void)mine;
    return !(a && b && (a & b));
}
/* the enemies about, read once every 0.1 s (a field's 48 mines each looking through the enemy list every update would
 * read the game's records thousands of times a frame): the hostile ones within 80 m of a field of the mod's or a shot */
#define SNAP 256
static struct { uint32_t e; float p[3]; } g_snap[SNAP];
static int g_nsnap;
static double g_snapAt;
static int Near(const float p[3], float r)
{
    for (int i = 0; i < FIELDS; i++) if (g_field[i].on && Dist2(g_field[i].centre, p) < r * r) return 1;
    return 0;
}
static void Snapshot(int shots)
{
    if (g_now - g_snapAt < 0.1) return;
    g_snapAt = g_now;
    g_nsnap = 0;
    for (int i = 0, n = fp_foe_count(); i < n && g_nsnap < SNAP; i++)
    {
        float q[3];
        uint32_t e = fp_foe(i);
        if (!e || !fp_foe_pos(i, q) || (!Near(q, 80) && !shots) || !Hostile(e, 0)) continue;
        g_snap[g_nsnap].e = e;
        memcpy(g_snap[g_nsnap].p, q, 12);
        g_nsnap++;
    }
}
/* the living enemy nearest `at` within `r` m (and 6 m up or down): it, or 0 */
static uint32_t Nearest(const float at[3], float r, float p[3])
{
    uint32_t best = 0;
    float bd = r * r;
    for (int i = 0; i < g_nsnap; i++)
    {
        const float* q = g_snap[i].p;
        if (fabsf(q[2] - at[2]) > 6) continue;
        float dx = q[0] - at[0], dy = q[1] - at[1], d2 = dx * dx + dy * dy;
        if (d2 >= bd) continue;
        bd = d2; best = g_snap[i].e; memcpy(p, q, 12);
    }
    return best;
}
static void Blast(const float at[3])
{
    float p[3] = {at[0], at[1], at[2] + 0.3f};
    int r = g_hf->spawn_explosion(g_mod, MD6_BLAST, p);
    Boom* b = &g_boom[g_boomNext++ % 32];
    memcpy(b->pos, p, 12);
    b->at = g_now;
    g_blasts++;
    if (!r && g_blasts <= 5) g_hf->log(g_mod, "MD-6 minefield: a mine's blast was refused (%s)", g_hf->last_log ? g_hf->last_log(g_mod) : "");
}

/* the takeover: a new field of the MD-6's mines near one of your beacons */
static void TakeOver(uintptr_t rec, const float centre[3], uint32_t count)
{
    Field* f = NULL;
    for (int i = 0; i < FIELDS && !f; i++) if (!g_field[i].on) f = &g_field[i];
    if (!f) { int old = 0; for (int i = 1; i < FIELDS; i++) if (g_field[i].born < g_field[old].born) old = i; f = &g_field[old];
              for (int k = 0; k < f->n; k++) if (f->mine[k].unit) ModelHide(f->mine[k].unit); }
    memset(f, 0, sizeof *f);
    f->on = 1;
    memcpy(f->centre, centre, 12);
    f->born = g_now;
    int took = 0, models = 0;
    for (uint32_t k = 0; k < count && k < MINES; k++)
    {
        uintptr_t at = rec + 0x14 + MINE_STRIDE * k;
        uint8_t m[MINE_STRIDE];
        if (!ReadAt(at, m, sizeof m) || Word(m + 0x10) != 1) continue;
        uint32_t zero = 0;
        if (g_hf->write(at + 0x10, &zero, 4)) continue;
        ModelHide(Word(m));
        Mine* x = &f->mine[f->n++];
        memcpy(x->pos, m + 4, 12);
        x->on = 1;
        x->unit = ModelSpawn(x->pos);
        took++; models += x->unit != 0;
    }
    g_taken++;
    g_hf->log(g_mod, "MD-6 minefield: a field of yours at %.0f %.0f %.0f taken over (level %d): %d mines are the mod's now (%d with the mine's look)", (double)centre[0],
        (double)centre[1], (double)centre[2], Level(), took, models);
}
static int Known(const float c[3])
{
    for (int i = 0; i < FIELDS; i++) if (g_field[i].on && Dist2(g_field[i].centre, c) < 1.0f) return 1;
    return 0;
}
static void WatchFields(void)
{
    uintptr_t m = Pointer(g_game + 0x3326958), recs;
    uint32_t n = 0;
    HfFn_UnitResourceName nameOf = hf_fn_UnitResourceName(g_mod);
    HfFn_UnitAlive alive = hf_fn_UnitAlive(g_mod);
    if (!m || !ReadAt(m + 0x848, &n, 4) || n > 128 || !(recs = Pointer(m + 0x880)) || !nameOf || !alive) return;
    for (uint32_t i = 0; i < n; i++)
    {
        uintptr_t r = recs + FIELD_STRIDE * i;
        float c[4];
        uint32_t count = 0;
        uint8_t first[MINE_STRIDE];
        if (!ReadAt(r, c, 16) || !ReadAt(r + 0x10, &count, 4) || !count || count > MINES || Known(c)) continue;
        /* yours: landed within 40 m of one of your MD-6 beacons in the last 30 s */
        int mine = 0;
        for (int b = 0; b < 4 && !mine; b++) mine = g_beacon[b].at > 0 && g_now - g_beacon[b].at < 30 && Dist2(g_beacon[b].pos, c) < 40.0f * 40.0f;
        if (!mine || !ReadAt(r + 0x14, first, sizeof first)) continue;
        uint32_t unit = Word(first);
        if (!unit || !(alive(unit) & 1) || nameOf(unit) != MD6_MINE_UNIT) continue;   /* the MD-6's, and its mines are out */
        TakeOver(r, c, count);
    }
}

/* ---- the launch shots ------------------------------------------------------------------------------------------- */
typedef struct Launch { int slot; uint32_t target; double at; float flown, last[3]; int snapped; } Launch;
static Launch g_shot[24];
static int g_nshot;
typedef struct Pending { uint32_t target; double at; } Pending;
static Pending g_pending[24];
static int g_npend;
static uintptr_t g_pool;
static uint32_t g_counter;
static void TurnTo(float v[3], const float to[3], float angle)
{
    float speed = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]), length = sqrtf(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
    if (speed < 1e-3f || length < 1e-3f) return;
    float a[3] = {v[0] / speed, v[1] / speed, v[2] / speed}, b[3] = {to[0] / length, to[1] / length, to[2] / length};
    float cosine = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    cosine = cosine > 1 ? 1 : cosine < -1 ? -1 : cosine;
    float between = acosf(cosine);
    if (between < 0.001f) return;
    float t = angle >= between ? 1.0f : angle / between, sn = sinf(between);
    float wa = sn > 1e-4f ? sinf((1 - t) * between) / sn : 1 - t, wb = sn > 1e-4f ? sinf(t * between) / sn : t;
    for (int k = 0; k < 3; k++) v[k] = (wa * a[k] + wb * b[k]) * speed;
}
static int TargetAim(uint32_t e, float aim[3])
{
    for (int i = 0; i < g_nsnap; i++) if (g_snap[i].e == e) { memcpy(aim, g_snap[i].p, 12); aim[2] += 0.8f; return 1; }
    return 0;
}
static void ShotsFrame(float dt)
{
    uintptr_t pool = Pointer(g_game + 0x347CEA8);
    uint8_t active = 0;
    uint32_t counter = 0;
    if (!pool || !ReadAt(pool + 0x28, &active, 1) || active != 1 || !ReadAt(pool + 0x30, &counter, 4)) return;
    if (g_pool != pool) { g_pool = pool; g_counter = counter; g_nshot = 0; return; }
    uint32_t n = counter - g_counter, from = g_counter;
    g_counter = counter;
    if (n > 64 && n <= 2048) { from += n - 64; n = 64; }
    for (uint32_t i = 0; i < n && n <= 64 && g_npend; i++)
    {
        int slot = (int)((from + i) & 0x7FF);
        uint32_t type = 0;
        if (!ReadAt(pool + 0xE5040 + (uintptr_t)slot * 4, &type, 4) || type != g_launchType || g_nshot >= 24) continue;
        Launch* s = &g_shot[g_nshot++];
        memset(s, 0, sizeof *s);
        s->slot = slot;
        s->target = g_pending[0].target;
        s->at = g_now;
        memmove(g_pending, g_pending + 1, sizeof g_pending[0] * (size_t)--g_npend);
    }
    for (int i = 0; i < g_npend;) { if (g_now - g_pending[i].at > 2) g_pending[i] = g_pending[--g_npend]; else i++; }
    for (int i = 0; i < g_nshot;)
    {
        Launch* s = &g_shot[i];
        uintptr_t flight = pool + 0x3040 + (uintptr_t)s->slot * 0x70;
        uint16_t flags = 0;
        uint32_t type = 0;
        float f[14];
        if (g_now - s->at > 10 || !ReadAt(pool + 0xE5040 + (uintptr_t)s->slot * 4, &type, 4) || type != g_launchType || !ReadAt(pool + 0x203C + (uintptr_t)s->slot * 2, &flags, 2) ||
            !(flags & 2) || (flags & 1) || !ReadAt(flight, f, sizeof f) || f[12] < s->flown)
        {
            /* it ended: its blast went off about where it last was (its kills are the minefield's) */
            if (s->flown > 0) { Boom* b = &g_boom[g_boomNext++ % 32]; memcpy(b->pos, s->last, 12); b->at = g_now; }
            *s = g_shot[--g_nshot];
            continue;
        }
        int sane = 1;
        for (int k = 0; k < 6; k++) sane &= isfinite(f[k]) != 0;
        if (!sane) { *s = g_shot[--g_nshot]; continue; }
        s->flown = f[12];
        memcpy(s->last, f, 12);
        i++;
        if (g_now - s->at < s_mdLaunchFree) continue;
        float* v = f + 3, aim[3], p[3];
        if (s_mdLaunchCatch > 0 && Nearest(f, s_mdLaunchCatch, p) && fabsf(p[2] + 0.8f - f[2]) < s_mdLaunchCatch + 1)
        {
            float to[3] = {p[0] - f[0], p[1] - f[1], p[2] + 0.8f - f[2]};
            TurnTo(v, to, 3.2f);
            g_hf->write(flight + 0x0C, v, 12);
            s->snapped = 1;
            continue;
        }
        if (!s->target || !TargetAim(s->target, aim))
        {
            s->target = Nearest(f, 25, p);
            if (!s->target || !TargetAim(s->target, aim)) continue;
        }
        float to[3] = {aim[0] - f[0], aim[1] - f[1], aim[2] - f[2]};
        TurnTo(v, to, s_mdLaunchTurn * 3.14159265f / 180 * (dt > 0 && dt < 0.25f ? dt : 1.0f / 60));
        g_hf->write(flight + 0x0C, v, 12);
    }
}
static struct { uint32_t e; double at; } g_aimed[16];
static int Aimed(uint32_t e)
{
    for (int i = 0; i < 16; i++) if (g_aimed[i].e == e && g_now - g_aimed[i].at < 1.0) return 1;
    return 0;
}
static int Launch_(const float at[3], uint32_t target)
{
    if (!g_launchType || !g_launchReady || g_npend >= 24 || Aimed(target)) return 0;
    float from[3] = {at[0], at[1], at[2] + 0.4f};
    static const float up[3] = {0, 0, 1};
    int r = g_hf->spawn_projectile(g_mod, g_launchType, from, up);
    if (!r) { static unsigned told; if (told++ < 3) g_hf->log(g_mod, "MD-6 minefield: a launch was refused (%s): the mine goes off where it is", g_hf->last_log ? g_hf->last_log(g_mod) : ""); return 0; }
    if (r == HF_ACT_WAITING) return 0;
    g_pending[g_npend++] = (Pending){target, g_now};
    { static unsigned k; g_aimed[k++ % 16].e = target; g_aimed[(k - 1) % 16].at = g_now; }
    g_launches++;
    return 1;
}

/* ---- the mines' frame ----------------------------------------------------------------------------------------------- */
static void MinesFrame(void)
{
    int level = Level();
    for (int fi = 0; fi < FIELDS; fi++)
    {
        Field* f = &g_field[fi];
        if (!f->on) continue;
        int regrow = level >= 2 && s_mdRegrow && (s_mdRegrowWindow <= 0 || g_now - f->born <= s_mdRegrowWindow), live = 0;
        for (int k = 0; k < f->n; k++)
        {
            Mine* m = &f->mine[k];
            if (!m->on)
            {
                if (regrow && m->blownAt > 0 && g_now - m->blownAt >= s_mdRegrowSeconds)
                {
                    float p[3];
                    if (Nearest(m->pos, s_mdTrigger + 1, p)) { m->blownAt = g_now - s_mdRegrowSeconds + 1; continue; }   /* not under an enemy's feet */
                    m->on = 1; m->boomAt = 0; m->blownAt = 0;
                    m->unit = ModelSpawn(m->pos);
                    g_regrown++;
                }
                else if (regrow && m->blownAt > 0) live++;
                if (!m->on) continue;
            }
            live++;
            float p[3];
            if (!m->boomAt)
            {
                uint32_t e = level >= 3 && s_mdLaunch && g_launchReady ? Nearest(m->pos, s_mdLaunchRange, p) : 0;
                if (e && Launch_(m->pos, e))
                {
                    if (m->unit) ModelHide(m->unit);
                    m->unit = 0; m->on = 0; m->blownAt = g_now;
                    continue;
                }
                if (Nearest(m->pos, s_mdTrigger, p)) m->boomAt = g_now + s_mdFuse;
                continue;
            }
            if (g_now < m->boomAt) continue;
            Blast(m->pos);
            if (m->unit) ModelHide(m->unit);
            m->unit = 0; m->on = 0; m->blownAt = g_now; m->boomAt = 0;
        }
        if (!live && !regrow) { f->on = 0; g_hf->log(g_mod, "MD-6 minefield: a field of the mod's is spent"); }
    }
}

/* ---- events ------------------------------------------------------------------------------------------------------- */
static void OnStratagem(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfStratagemEvent* e = (const HfStratagemEvent*)info;
    if (e->type != HF_STRATAGEM_MD6_ANTIPERSONNEL_MINEFIELD || !fp_in_mission()) return;
    int slot = 0;
    for (int i = 1; i < 4; i++) if (g_beacon[i].at < g_beacon[slot].at) slot = i;
    memcpy(g_beacon[slot].pos, e->position, 12);
    g_beacon[slot].at = g_now;
}
static void CheckLevel(void);
static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfKillEvent* k = (const HfKillEvent*)info;
    float p[3];
    if (!fp_in_mission() || !g_blasts || !fp_entity_pos(k->victim, p)) return;
    for (int i = 0; i < 32; i++)
        if (g_boom[i].at > 0 && g_now - g_boom[i].at <= 1.5 && Dist2(g_boom[i].pos, p) <= 36.0f && Hostile(k->victim, 0))
        {
            g_kills++; g_ownKills++;
            CheckLevel();
            return;
        }
}
/* ---- the launch shot's row ----------------------------------------------------------------------------------------- */
static void ProjectilesReady(HfMod* mod, HfEvent event, void* user)
{
    if (!g_launchType) return;
    HfProjectileSettings* row = (HfProjectileSettings*)g_hf->projectile_row(g_launchType);
    if (!row) { g_hf->log(g_mod, "MD-6 minefield: the launch shot's row is not there: level 3 mines go off as level 2's"); g_launchType = 0; return; }
    row->velocity = s_mdLaunchSpeed;
    HfField impact = HF_FIELD(ProjectileSettings, impact_explosion), expiry = HF_FIELD(ProjectileSettings, expiry_explosion);
    HfValue v = HF_U32(MD6_BLAST);
    g_launchReady = g_hf->set_live(g_mod, g_launchType, &impact, &v, 0) != 0;
    g_hf->set_live(g_mod, g_launchType, &expiry, &v, 0);
    g_hf->log(g_mod, "MD-6 minefield launch shot: type %u (a copy of the Rocket Sentry's rocket, %.0f m/s), its blast the MD-6's (%u) %s", g_launchType, (double)s_mdLaunchSpeed,
        MD6_BLAST, g_launchReady ? "set" : "REFUSED");
}

/* ---- levels ------------------------------------------------------------------------------------------------------- */
static void CheckLevel(void)
{
    int level = Level();
    if (level == g_level) return;
    int up = level > g_level;
    g_level = level;
    g_hf->log(g_mod, "MD-6 Anti-Personnel Minefield: level %d (%d kills this mission, %u by the mod's own mines)", level, g_kills, g_ownKills);
    if (up)
    {
        static const char* kLines[2] = {"Self-repairing mines issued. Your next minefield grows back what it spends.",
            "Launcher mines authorised. Your minefields hunt what comes near."};
        fp_toast_line("MD-6 Anti-Personnel Minefield", level, kLines[level >= 3]);
    }
}
void md_kills(uint64_t resource, int added)
{
    if (resource != MD6 || added <= 0) return;
    g_kills += added;
    CheckLevel();
}
void md_tick(double now, float dt)
{
    g_now = now;
    if (!fp_in_mission() || Level() < 2 || !Ok()) return;
    static double next;
    if (now >= next) { next = now + 0.25; WatchFields(); }
    Snapshot(g_nshot > 0);
    MinesFrame();
    if (g_launchType) ShotsFrame(dt);   /* keeps the pool's count up to date, so a new shot is found the update it leaves */
}
void md_reset(void)
{
    if (g_taken || g_blasts) g_hf->log(g_mod, "MD-6 minefield: %u fields run by the mod, %u blasts, %u launches, %u mines grown back, %u kills by its own mines", g_taken, g_blasts,
        g_launches, g_regrown, g_ownKills);
    for (int i = 0; i < FIELDS; i++) if (g_field[i].on && fp_in_mission()) for (int k = 0; k < g_field[i].n; k++) if (g_field[i].mine[k].unit) ModelHide(g_field[i].mine[k].unit);
    memset(g_field, 0, sizeof g_field);
    memset(g_beacon, 0, sizeof g_beacon);
    memset(g_boom, 0, sizeof g_boom);
    g_nshot = g_npend = 0;
    g_pool = 0;
    g_models = 0;
    g_kills = 0;
    g_taken = g_blasts = g_launches = g_regrown = g_ownKills = 0;
    g_level = Level();
}

int md_init(const HfApi* hf, HfMod* mod)
{
    g_hf = hf;
    g_mod = mod;
    if (s_mdLaunch)
    {
        HfDamage dmg = {sizeof dmg};
        dmg.base = MD6_DAMAGE;
        dmg.name = "yui/field_promotions/md6_launch_hit";
        dmg.scale = 0.01f;   /* its blast is the payload, not the rocket's hit */
        g_launchDamage = hf->add_damage(mod, &dmg);
        HfProjectile shot = {sizeof shot};
        shot.base = LAUNCH_BASE;
        shot.name = "yui/field_promotions/md6_launch_shot";
        shot.damage = g_launchDamage;
        g_launchType = hf->add_projectile(mod, &shot);
        if (!g_launchType) hf->log(mod, "MD-6 minefield: the launch shot was refused (%s): level 3 mines go off as level 2's", hf->last_log ? hf->last_log(mod) : "");
        else hf->on(mod, HF_EVENT_PROJECTILES_READY, ProjectilesReady, NULL);
    }
    int ok = hf->subscribe(mod, HF_ON_STRATAGEM, OnStratagem, NULL, 0) & hf->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0);
    hf->log(mod, "MD-6 Anti-Personnel Minefield: level 2 at %d kills, level 3 at %d; regrowth %s (%.0f s, for %.0f s), launcher mines %s (%.0f m, %.0f m/s)", s_mdL2, s_mdL3,
        s_mdRegrow ? "on" : "off", (double)s_mdRegrowSeconds, (double)s_mdRegrowWindow, s_mdLaunch ? "on" : "off", (double)s_mdLaunchRange, (double)s_mdLaunchSpeed);
    return ok;
}
