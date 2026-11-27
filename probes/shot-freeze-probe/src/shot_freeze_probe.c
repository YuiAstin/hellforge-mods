/* Shot Freeze Probe (yui.shot-freeze-probe): research towards a true time stop. Can a mod stop what is in flight
 * where it is, hold it, and let it go on as it was?
 *
 * 0.1.0 answered it for the shot pool (bullets, Eruptor rounds): velocity and starting speed x 1e-6, gravity 0, the
 * lifetime held; zeroing the velocity instead ends a shot. 0.2.0 adds what is not in the shot pool, read from game.dll
 * build 25480438 (docs/research/shot-freeze.md):
 *
 *   - Thrown grenades and knives are physics bodies. The Throwable manager [game+0x33264C0]: count +0x20, headers
 *     [+0x48] (8 bytes each: +0 the entity's resource, +8 the entity, +0xC its unit), instances [+0x50] 0x78 bytes each
 *     (+0 the physics actor, -1 none). The engine's actor table [game+0x3326338]: +0x50 position(actor, out),
 *     +0x60 set position(actor, pos), +0xA0 set velocity(actor, linear, angular), +0xA8 velocity(actor, linear,
 *     angular) (ThrowableUpdate 0x6C3AE0 reads +0xA8; its actor swap 0x6C4792..0x6C4819 copies +0x50/+0xA8 into
 *     +0x60/+0xA0; 0x5B6696 stops a body with +0xA0 zeros and +0x60).
 *     The freeze: velocity 0 and the position written back every tick; the release gives back the velocity it had.
 *   - Fuses: the Explosive manager [game+0x3326728] (world update 0x573612): count +0x28, headers [+0x50], instances
 *     [+0x60] 0x40 bytes each. In ExplosiveUpdate 0x8CAFF0: +0 done; +0xC lit, +0x10 the fuse left (at 0 it requests
 *     the explosion, 0x8CB1FA); +4 and +8 two more countdowns (+4 at 0 calls 0x8CA7E0).
 *     The freeze: +4, +8 and +0x10 written back every tick, so no fuse runs out while time is stopped.
 *   - Seeking missiles (Spear, P-33, W.A.S.P., the Rocket Sentry's when its warhead is one): the manager
 *     [game+0x3326DF0] (docs/research/lock-on.md): count +0x10, headers [+0x38], clocks [+0x40] 12 bytes each
 *     (+4 distance, +8 age), state [+0x50] 0x38 bytes each (+0x18 position, +0x35 engine). The update 0x642760 moves a
 *     missile only while +0x35 is set (0x6429B9) and starts the engine once from its age (0x642878).
 *     The freeze: the game's own SetMissileEngine (0x645B80) off, its clocks held; the release turns it back on.
 *   - Shots (unchanged from 0.1.0, method A) and now their position written back too, so a rocket's thrust or a
 *     guided round's steering cannot creep it along.
 *
 * Hands on only: Ctrl+F11 stops time (everything above that is in flight, and anything new once it has left the
 * thrower) until pressed again. Fire a rocket, throw a grenade, launch a Spear, then press it. Solo or host. */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <windows.h>
#include "hellforge_mod.h"
#include "hellforge_content.h"
#include "hellforge_globals.h"

#define BUILD "25480438"
#define VERSION "0.2.0"
#include "kitbase/kb_core.h"

static double g_now;
static int g_frozen;
static double g_frozenAt, g_releasedAt, g_reportAt, g_afterAt;
static uintptr_t g_exeEnd;

static void PutAt(uintptr_t a, const void* v, size_t n) { hf->write(a, v, n); }
static float Len(const float v[3]) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
static float Dist(const float a[3], const float b[3]) { float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; return Len(d); }

/* ==== shots: the pool ======================================================================================== */
#define SLOTS 2048
#define FLAGS 0x203C
#define FLIGHT 0x3040
#define TYPES 0xE5040
#define SCALE 1e-6f
typedef struct Flight { float pos[3], vel[3], carry, gravity, f20, f24, drag, start, flown, life; } Flight;
_Static_assert(sizeof(Flight) == 0x38, "flight head");
static int g_shotsOk;

static uintptr_t Pool(void)
{
    uintptr_t p = Pointer(game + 0x347CEA8);
    uint8_t running = 0;
    return p && ReadAt(p + 0x28, &running, 1) && running == 1 ? p : 0;
}
static int Live(uintptr_t pool, int slot, uint32_t* type)
{
    uint16_t f = 0;
    return ReadAt(pool + FLAGS + 2u * slot, &f, 2) && (f & 2) && !(f & 1) && ReadAt(pool + TYPES + 4u * slot, type, 4);
}
static int ReadFlight(uintptr_t pool, int slot, Flight* f) { return ReadAt(pool + FLIGHT + 0x70u * slot, f, sizeof *f); }
static uint32_t Rides(uintptr_t pool, int slot)
{
    uint32_t u = 1;
    ReadAt(pool + FLIGHT + 0x70u * slot + 0x50, &u, 4);
    return u;
}
static void PutShot(uintptr_t pool, int slot, size_t off, const void* v, size_t n) { PutAt(pool + FLIGHT + 0x70u * slot + off, v, n); }

#define HELD 256
typedef struct Shot { int slot; uint32_t type; Flight saved; float drift; int ended; float releasedAt[3]; } Shot;
static Shot g_shot[HELD];
static int g_nshot;

static int ShotHeld(int slot) { for (int i = 0; i < g_nshot; i++) if (g_shot[i].slot == slot) return 1; return 0; }
static int FreezeShots(uintptr_t pool, float minFlown)
{
    static uint16_t flags[SLOTS];
    int n = 0;
    if (!g_shotsOk || !ReadAt(pool + FLAGS, flags, sizeof flags)) return 0;
    for (int s = 0; s < SLOTS && g_nshot < HELD; s++)
    {
        uint32_t type;
        Flight f;
        if (!(flags[s] & 2) || (flags[s] & 1) || ShotHeld(s) || !Live(pool, s, &type) || Rides(pool, s) || !ReadFlight(pool, s, &f) || f.flown < minFlown) continue;
        Shot* h = &g_shot[g_nshot++];
        memset(h, 0, sizeof *h);
        h->slot = s; h->type = type; h->saved = f;
        float v[3] = {f.vel[0] * SCALE, f.vel[1] * SCALE, f.vel[2] * SCALE}, g = 0, start = f.start * SCALE;
        PutShot(pool, s, 0x0C, v, 12);
        PutShot(pool, s, 0x1C, &g, 4);
        PutShot(pool, s, 0x2C, &start, 4);
        n++;
    }
    return n;
}
static void HoldShots(uintptr_t pool)
{
    for (int i = 0; i < g_nshot; i++)
    {
        Shot* h = &g_shot[i];
        uint32_t type;
        Flight f;
        if (h->ended) continue;
        if (!Live(pool, h->slot, &type) || type != h->type || !ReadFlight(pool, h->slot, &f))
        {
            h->ended = 1;
            Log("  shot %d (type %u) ENDED while frozen, %.2f s in", h->slot, h->type, g_now - g_frozenAt);
            continue;
        }
        float d = Dist(f.pos, h->saved.pos);
        if (d > h->drift) h->drift = d;
        float v[3] = {h->saved.vel[0] * SCALE, h->saved.vel[1] * SCALE, h->saved.vel[2] * SCALE};
        PutShot(pool, h->slot, 0x00, h->saved.pos, 12);
        PutShot(pool, h->slot, 0x0C, v, 12);
        PutShot(pool, h->slot, 0x34, &h->saved.life, 4);
    }
}
static void ReleaseShots(uintptr_t pool)
{
    int alive = 0;
    float worst = 0;
    for (int i = 0; i < g_nshot; i++)
    {
        Shot* h = &g_shot[i];
        uint32_t type;
        Flight f;
        if (h->ended || !Live(pool, h->slot, &type) || type != h->type || !ReadFlight(pool, h->slot, &f)) { h->ended = 1; continue; }
        PutShot(pool, h->slot, 0x0C, h->saved.vel, 12);
        PutShot(pool, h->slot, 0x1C, &h->saved.gravity, 4);
        PutShot(pool, h->slot, 0x2C, &h->saved.start, 4);
        PutShot(pool, h->slot, 0x34, &h->saved.life, 4);
        memcpy(h->releasedAt, f.pos, 12);
        if (h->drift > worst) worst = h->drift;
        alive++;
    }
    Log("  shots: %d of %d let go (the rest ended while frozen); most drift %.4f m", alive, g_nshot, (double)worst);
}
static void AfterShots(uintptr_t pool)
{
    int shown = 0;
    for (int i = 0; i < g_nshot && shown < 16; i++)
    {
        Shot* h = &g_shot[i];
        uint32_t type;
        Flight f;
        if (h->ended) continue;
        shown++;
        if (!Live(pool, h->slot, &type) || type != h->type || !ReadFlight(pool, h->slot, &f))
            Log("  shot %d (type %u): gone 0.3 s after the release (hit something or ran out)", h->slot, h->type);
        else
            Log("  shot %d (type %u): moved %.1f m since the release, speed %.1f m/s (was %.1f), life %.2f s (was %.2f)", h->slot, h->type,
                (double)Dist(f.pos, h->releasedAt), (double)Len(f.vel), (double)Len(h->saved.vel), (double)f.life, (double)h->saved.life);
    }
}

/* ==== the engine's physics actors ============================================================================ */
typedef void* (*ActorGetPos)(uint32_t actor, float* out);
typedef void (*ActorSetPos)(uint32_t actor, const float* pos);
typedef void (*ActorSetVel)(uint32_t actor, const float* linear, const float* angular);
typedef void (*ActorGetVel)(uint32_t actor, float* linear, float* angular);
static ActorGetPos a_getPos;
static ActorSetPos a_setPos;
static ActorSetVel a_setVel;
static ActorGetVel a_getVel;

static int ExeCode(uintptr_t f) { return f > exe + 0x1000 && f < g_exeEnd; }
static int ActorApi(void)
{
    uintptr_t t = Pointer(game + 0x3326338), f[4];
    static const int slot[4] = {0x50, 0x60, 0xA0, 0xA8};
    if (!t) return 0;
    for (int i = 0; i < 4; i++) if (!ReadAt(t + slot[i], &f[i], 8) || !ExeCode(f[i])) return 0;
    a_getPos = (ActorGetPos)f[0]; a_setPos = (ActorSetPos)f[1]; a_setVel = (ActorSetVel)f[2]; a_getVel = (ActorGetVel)f[3];
    return 1;
}

/* ==== grenades and knives: Throwables ======================================================================== */
typedef struct Thrown { uint32_t entity, actor; uint64_t resource; float pos[3], lin[3], ang[3], drift, releasedAt[3]; double seen; int held, gone; } Thrown;
#define THROWN 64
static Thrown g_thr[THROWN];
static int g_nthr, g_thrOk;

static uintptr_t Throwables(void) { return Pointer(game + 0x33264C0); }
/* The throwable with this entity: its actor and resource (index found again each time: the manager compacts) */
static int ThrowableOf(uintptr_t m, uint32_t entity, uint32_t* actor, uint64_t* resource)
{
    uint32_t n = 0;
    uintptr_t heads = Pointer(m + 0x48), inst = Pointer(m + 0x50), h;
    if (!heads || !inst || !ReadAt(m + 0x20, &n, 4) || n > 4096) return 0;
    for (uint32_t i = 0; i < n; i++)
    {
        uint32_t e;
        if (!(h = Pointer(heads + 8u * i)) || !ReadAt(h + 8, &e, 4) || e != entity) continue;
        return ReadAt(inst + 0x78u * i, actor, 4) && ReadAt(h, resource, 8);
    }
    return 0;
}
static Thrown* FindThrown(uint32_t e) { for (int i = 0; i < g_nthr; i++) if (g_thr[i].entity == e) return &g_thr[i]; return NULL; }
static void HoldThrown(Thrown* t)
{
    static const float zero[3] = {0, 0, 0};
    a_setVel(t->actor, zero, zero);
    a_setPos(t->actor, t->pos);
}
/* Every throwable in flight, new ones once they have been out `minAge` seconds */
static int FreezeThrown(double minAge)
{
    uintptr_t m = Throwables(), heads, inst, flags, h;
    uint32_t n = 0;
    int got = 0;
    if (!g_thrOk || !a_setVel || !m || !(heads = Pointer(m + 0x48)) || !(inst = Pointer(m + 0x50)) || !(flags = Pointer(m + 0x60)) || !ReadAt(m + 0x20, &n, 4) || n > 4096) return 0;
    for (uint32_t i = 0; i < n; i++)
    {
        uint32_t e, actor;
        uint64_t res;
        uint8_t fl[2];
        /* ThrowableUpdate (0x6C3BD5) moves only one whose two flag bytes are both set: out of the hand and flying */
        if (!ReadAt(flags + 0x2Cu * i, fl, 2) || !fl[0] || !fl[1]) continue;
        if (!(h = Pointer(heads + 8u * i)) || !ReadAt(h + 8, &e, 4) || !ReadAt(h, &res, 8) || !ReadAt(inst + 0x78u * i, &actor, 4) || actor == UINT32_MAX) continue;
        Thrown* t = FindThrown(e);
        if (!t)
        {
            if (g_nthr >= THROWN) continue;
            t = &g_thr[g_nthr++];
            memset(t, 0, sizeof *t);
            t->entity = e; t->resource = res; t->seen = g_now;
        }
        if (t->held || t->gone || g_now - t->seen < minAge) continue;
        t->actor = actor;
        a_getPos(actor, t->pos);
        a_getVel(actor, t->lin, t->ang);
        t->held = 1;
        HoldThrown(t);
        got++;
        Log("  throwable %08X (resource %016llX, actor %08X): at %.1f %.1f %.1f, speed %.1f m/s, spin %.1f rad/s: frozen", e, (unsigned long long)res, actor,
            (double)t->pos[0], (double)t->pos[1], (double)t->pos[2], (double)Len(t->lin), (double)Len(t->ang));
    }
    return got;
}
static void HoldAllThrown(void)
{
    uintptr_t m = Throwables();
    for (int i = 0; i < g_nthr; i++)
    {
        Thrown* t = &g_thr[i];
        uint32_t actor;
        uint64_t res;
        if (!t->held || t->gone) continue;
        if (!m || !ThrowableOf(m, t->entity, &actor, &res) || actor == UINT32_MAX)
        {
            t->gone = 1;
            Log("  throwable %08X GONE while frozen, %.2f s in (went off or was removed)", t->entity, g_now - g_frozenAt);
            continue;
        }
        float p[3];
        a_getPos(actor, p);
        float d = Dist(p, t->pos);
        if (d > t->drift) t->drift = d;
        t->actor = actor;
        HoldThrown(t);
    }
}
static void ReleaseThrown(void)
{
    int n = 0, gone = 0;
    float worst = 0;
    for (int i = 0; i < g_nthr; i++)
    {
        Thrown* t = &g_thr[i];
        if (!t->held) continue;
        if (t->gone) { gone++; continue; }
        a_getPos(t->actor, t->releasedAt);
        a_setVel(t->actor, t->lin, t->ang);
        if (t->drift > worst) worst = t->drift;
        n++;
    }
    if (n || gone) Log("  throwables: %d let go with the velocity they had, %d gone while frozen; most drift %.4f m", n, gone, (double)worst);
}
static void AfterThrown(void)
{
    uintptr_t m = Throwables();
    for (int i = 0; i < g_nthr; i++)
    {
        Thrown* t = &g_thr[i];
        uint32_t actor;
        uint64_t res;
        if (!t->held || t->gone) continue;
        if (!m || !ThrowableOf(m, t->entity, &actor, &res) || actor == UINT32_MAX) { Log("  throwable %08X: gone 0.3 s after the release", t->entity); continue; }
        float p[3], v[3], w[3];
        a_getPos(actor, p);
        a_getVel(actor, v, w);
        Log("  throwable %08X: moved %.2f m since the release, speed %.1f m/s (was %.1f)", t->entity, (double)Dist(p, t->releasedAt), (double)Len(v), (double)Len(t->lin));
    }
    g_nthr = 0;
}

/* ==== fuses: Explosives ====================================================================================== */
typedef struct Fuse { uint32_t entity; uint64_t resource; float t4, t8, fuse; uint8_t lit; int gone; } Fuse;
#define FUSES 128
static Fuse g_fuse[FUSES];
static int g_nfuse, g_fuseOk;

static uintptr_t Explosives(void) { return Pointer(game + 0x3326728); }
static Fuse* FindFuse(uint32_t e) { for (int i = 0; i < g_nfuse; i++) if (g_fuse[i].entity == e) return &g_fuse[i]; return NULL; }
/* Holds every explosive's countdowns; one seen for the first time is taken as it is then */
static int HoldFuses(int logNew)
{
    uintptr_t m = Explosives(), heads, inst, h;
    uint32_t n = 0;
    int got = 0;
    if (!g_fuseOk || !m || !(heads = Pointer(m + 0x50)) || !(inst = Pointer(m + 0x60)) || !ReadAt(m + 0x28, &n, 4) || n > 8192) return 0;
    for (int i = 0; i < g_nfuse; i++) g_fuse[i].gone |= 2;   /* cleared when seen below */
    for (uint32_t i = 0; i < n; i++)
    {
        uint32_t e;
        uint64_t res;
        uint8_t rec[0x40];
        if (!(h = Pointer(heads + 8u * i)) || !ReadAt(h + 8, &e, 4) || !ReadAt(h, &res, 8) || !ReadAt(inst + 0x40u * i, rec, sizeof rec)) continue;
        if (rec[0]) continue;   /* done */
        Fuse* f = FindFuse(e);
        if (!f)
        {
            if (g_nfuse >= FUSES) continue;
            f = &g_fuse[g_nfuse++];
            memset(f, 0, sizeof *f);
            f->entity = e; f->resource = res; f->lit = rec[0xC];
            memcpy(&f->t4, rec + 4, 4); memcpy(&f->t8, rec + 8, 4); memcpy(&f->fuse, rec + 0x10, 4);
            got++;
            if (logNew && got <= 12)
                Log("  explosive %08X (resource %016llX): fuse %s %.2f s, timers %.2f / %.2f, mode %u: held", e, (unsigned long long)res, f->lit ? "lit," : "not lit,",
                    (double)f->fuse, (double)f->t4, (double)f->t8, Word(rec + 0x38));
        }
        f->gone &= ~2;
        PutAt(inst + 0x40u * i + 4, &f->t4, 4);
        PutAt(inst + 0x40u * i + 8, &f->t8, 4);
        PutAt(inst + 0x40u * i + 0x10, &f->fuse, 4);
    }
    for (int i = 0; i < g_nfuse; i++)
        if (g_fuse[i].gone == 2) { g_fuse[i].gone = 1; Log("  explosive %08X GONE while frozen, %.2f s in (its fuse was %.2f s)", g_fuse[i].entity, g_now - g_frozenAt, (double)g_fuse[i].fuse); }
        else g_fuse[i].gone &= 1;
    return got;
}

/* ==== seeking missiles ======================================================================================= */
typedef void (*SetEngine)(void* missiles, uint32_t missile, uint8_t on);
static SetEngine m_setEngine;
typedef struct Missile { uint32_t entity; uint64_t resource; float clocks[2], pos[3], drift, releasedAt[3]; uint8_t engine; int held, gone; } Missile;
#define MISSILES 64
static Missile g_mis[MISSILES];
static int g_nmis, g_misOk;

static uintptr_t Missiles(void) { return Pointer(game + 0x3326DF0); }
static Missile* FindMissile(uint32_t e) { for (int i = 0; i < g_nmis; i++) if (g_mis[i].entity == e) return &g_mis[i]; return NULL; }
static int MissileIndex(uintptr_t m, uint32_t entity, uint32_t* index)
{
    uint32_t n = 0;
    uintptr_t heads = Pointer(m + 0x38), h;
    if (!heads || !ReadAt(m + 0x10, &n, 4) || n > 4096) return 0;
    for (uint32_t i = 0; i < n; i++)
    {
        uint32_t e;
        if ((h = Pointer(heads + 8u * i)) && ReadAt(h + 8, &e, 4) && e == entity) { *index = i; return 1; }
    }
    return 0;
}
static int FreezeMissiles(void)
{
    uintptr_t m = Missiles(), heads, clocks, state, h;
    uint32_t n = 0;
    int got = 0;
    if (!g_misOk || !m || !(heads = Pointer(m + 0x38)) || !(clocks = Pointer(m + 0x40)) || !(state = Pointer(m + 0x50)) || !ReadAt(m + 0x10, &n, 4) || n > 4096) return 0;
    for (uint32_t i = 0; i < n; i++)
    {
        uint32_t e;
        uint64_t res;
        if (!(h = Pointer(heads + 8u * i)) || !ReadAt(h + 8, &e, 4) || !ReadAt(h, &res, 8)) continue;
        Missile* s = FindMissile(e);
        if (s && (s->held || s->gone)) continue;
        if (!s) { if (g_nmis >= MISSILES) continue; s = &g_mis[g_nmis++]; memset(s, 0, sizeof *s); s->entity = e; s->resource = res; }
        if (!ReadAt(clocks + 12u * i + 4, s->clocks, 8) || !ReadAt(state + 0x38u * i + 0x18, s->pos, 12) || !ReadAt(state + 0x38u * i + 0x35, &s->engine, 1)) continue;
        if (s->engine) m_setEngine((void*)m, e, 0);
        s->held = 1;
        got++;
        Log("  missile %08X (resource %016llX): at %.1f %.1f %.1f, age %.2f s, engine %s: frozen", e, (unsigned long long)res, (double)s->pos[0], (double)s->pos[1],
            (double)s->pos[2], (double)s->clocks[1], s->engine ? "on (now off)" : "off");
    }
    return got;
}
static void HoldMissiles(void)
{
    uintptr_t m = Missiles(), clocks, state;
    if (!m || !(clocks = Pointer(m + 0x40)) || !(state = Pointer(m + 0x50))) return;
    for (int i = 0; i < g_nmis; i++)
    {
        Missile* s = &g_mis[i];
        uint32_t k;
        uint8_t on = 0;
        float p[3];
        if (!s->held || s->gone) continue;
        if (!MissileIndex(m, s->entity, &k)) { s->gone = 1; Log("  missile %08X GONE while frozen, %.2f s in", s->entity, g_now - g_frozenAt); continue; }
        PutAt(clocks + 12u * k + 4, s->clocks, 8);
        if (ReadAt(state + 0x38u * k + 0x35, &on, 1) && on) m_setEngine((void*)m, s->entity, 0);   /* the age rule turned it back on */
        if (ReadAt(state + 0x38u * k + 0x18, p, 12)) { float d = Dist(p, s->pos); if (d > s->drift) s->drift = d; }
    }
}
static void ReleaseMissiles(void)
{
    uintptr_t m = Missiles(), state;
    int n = 0, gone = 0;
    float worst = 0;
    if (!m) return;
    state = Pointer(m + 0x50);
    for (int i = 0; i < g_nmis; i++)
    {
        Missile* s = &g_mis[i];
        uint32_t k;
        if (!s->held) continue;
        if (s->gone || !MissileIndex(m, s->entity, &k)) { gone++; continue; }
        if (s->engine) m_setEngine((void*)m, s->entity, 1);
        if (state) ReadAt(state + 0x38u * k + 0x18, s->releasedAt, 12);
        if (s->drift > worst) worst = s->drift;
        n++;
    }
    if (n || gone) Log("  missiles: %d let go (engines back on where they were on), %d gone while frozen; most drift %.4f m", n, gone, (double)worst);
}
static void AfterMissiles(void)
{
    uintptr_t m = Missiles(), state;
    if (!m || !(state = Pointer(m + 0x50))) { g_nmis = 0; return; }
    for (int i = 0; i < g_nmis; i++)
    {
        Missile* s = &g_mis[i];
        uint32_t k;
        float p[3];
        if (!s->held || s->gone) continue;
        if (!MissileIndex(m, s->entity, &k) || !ReadAt(state + 0x38u * k + 0x18, p, 12)) { Log("  missile %08X: gone 0.3 s after the release", s->entity); continue; }
        Log("  missile %08X: moved %.1f m since the release", s->entity, (double)Dist(p, s->releasedAt));
    }
    g_nmis = 0;
}

/* ==== stop and start ========================================================================================= */
static void Stop(uintptr_t pool)
{
    g_frozen = 1; g_frozenAt = g_now; g_reportAt = g_now + 1; g_afterAt = 0;
    g_nshot = g_nthr = g_nfuse = g_nmis = 0;
    if (g_thrOk && !a_setVel && !ActorApi()) { g_thrOk = 0; Log("the engine's actor table is not readable: grenades stay off"); }
    Log("Ctrl+F11: time stopped");
    int s = FreezeShots(pool, 0), t = FreezeThrown(0), mi = FreezeMissiles(), f = HoldFuses(1);
    Log("  frozen: %d shots, %d throwables, %d missiles; %d explosives' countdowns held", s, t, mi, f);
    for (int i = 0; i < g_nshot && i < 8; i++)
    {
        const Flight* x = &g_shot[i].saved;
        Log("  shot %d (type %u): at %.1f %.1f %.1f, speed %.1f m/s, gravity %.2f, flown %.1f m, life %.2f s", g_shot[i].slot, g_shot[i].type, (double)x->pos[0],
            (double)x->pos[1], (double)x->pos[2], (double)Len(x->vel), (double)x->gravity, (double)x->flown, (double)x->life);
    }
}
static void Start(uintptr_t pool)
{
    g_frozen = 0;
    Log("Ctrl+F11: time runs again after %.2f s", g_now - g_frozenAt);
    if (pool) ReleaseShots(pool);
    ReleaseThrown();
    ReleaseMissiles();
    Log("  explosives: %d held; their countdowns run from where they were", g_nfuse);
    g_afterAt = g_now + 0.3;
}
static void Report(void)
{
    int shots = 0, thr = 0, mis = 0, fuses = 0;
    float ds = 0, dt = 0, dm = 0;
    for (int i = 0; i < g_nshot; i++) if (!g_shot[i].ended) { shots++; if (g_shot[i].drift > ds) ds = g_shot[i].drift; }
    for (int i = 0; i < g_nthr; i++) if (g_thr[i].held && !g_thr[i].gone) { thr++; if (g_thr[i].drift > dt) dt = g_thr[i].drift; }
    for (int i = 0; i < g_nmis; i++) if (g_mis[i].held && !g_mis[i].gone) { mis++; if (g_mis[i].drift > dm) dm = g_mis[i].drift; }
    for (int i = 0; i < g_nfuse; i++) fuses += !g_fuse[i].gone;
    Log("  %.0f s frozen: %d shots (drift %.4f m), %d throwables (drift %.4f m), %d missiles (drift %.4f m), %d explosives still held", g_now - g_frozenAt, shots,
        (double)ds, thr, (double)dt, mis, (double)dm, fuses);
}

static void Tick(HfMod* mod, float dt, void* user)
{
    static int prev;
    static unsigned newShots;
    g_now += dt;
    if (!game) return;
    uintptr_t pool = Pool();
    if (!pool)
    {
        if (g_frozen) { Log("the mission ended while frozen: everything held is forgotten"); g_frozen = 0; }
        g_nshot = g_nthr = g_nfuse = g_nmis = 0; g_afterAt = 0;
        return;
    }
    int k = Focused() && (GetAsyncKeyState(VK_F11) & 0x8000) && (GetAsyncKeyState(VK_CONTROL) & 0x8000);
    if (k && !prev)
    {
        if (!g_frozen) { if (!g_afterAt) Stop(pool); else Log("Ctrl+F11: still reporting the last release, try again in a moment"); }
        else Start(pool);
    }
    prev = k;
    if (g_frozen)
    {
        int s = FreezeShots(pool, 2.0f), t = FreezeThrown(0.15), mi = FreezeMissiles();
        if ((s || t || mi) && newShots < 40) { newShots++; Log("  +%d shots, +%d throwables, +%d missiles frozen as they came", s, t, mi); }
        HoldShots(pool);
        HoldAllThrown();
        HoldMissiles();
        HoldFuses(1);
        if (g_now >= g_reportAt) { Report(); g_reportAt = g_now + (g_now - g_frozenAt < 5 ? 1 : 10); }
        return;
    }
    newShots = 0;
    if (g_afterAt && g_now >= g_afterAt)
    {
        g_afterAt = 0;
        Log("0.3 s after the release:");
        AfterShots(pool);
        AfterThrown();
        AfterMissiles();
        g_nshot = g_nfuse = 0;
    }
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gs = 0, xs = 0;
    uintptr_t g = hf->module_base("game.dll", &gs), x = hf->module_base("helldivers2.exe", &xs);
    if (!g || !x || gs != 74727424u || xs != 60719104u) { Log("REFUSED: not build " BUILD); return; }
    game = g;
    exe = x;
    g_exeEnd = x + xs;
    /* each part checks the code it was read from, and stays off when that differs */
    g_shotsOk = hf->code_matches(g + 0x13AB6D5, "f3 0f 5e 47 2c 44 0f 2f c0") && hf->code_matches(g + 0x13AB82D, "f3 0f 10 47 34 41 0f 2f c4");
    g_fuseOk = hf->code_matches(g + 0x573612, "48 8d 9f 90 59 eb 00") && hf->code_matches(g + 0x8CB03F, "48 8b 47 50") &&
               hf->code_matches(g + 0x8CB047, "4c 8b 6f 60") && hf->code_matches(g + 0x8CB0B0, "43 80 7c 2c 0c 00") &&
               hf->code_matches(g + 0x8CB0C0, "f3 43 0f 10 4c 2c 10") && hf->code_matches(g + 0x8CB2BB, "f3 43 0f 10 44 2c 04");
    g_thrOk = hf->code_matches(g + 0x6C3B90, "4c 8b 5b 60") && hf->code_matches(g + 0x6C3B97, "48 8b 43 48") &&
              hf->code_matches(g + 0x6C3C77, "ff 90 a8 00 00 00") && hf->code_matches(g + 0x6C4803, "ff 50 60") &&
              hf->code_matches(g + 0x6C4819, "ff 90 a0 00 00 00") && hf->code_matches(g + 0x5A0C7E, "ff 50 50") &&
              hf->code_matches(g + 0x6C3BD5, "47 38 24 1e");
    g_misOk = hf->code_matches(g + 0x64286C, "f3 0f 58 44 9f 08") && hf->code_matches(g + 0x642878, "80 7c 06 35 00") &&
              hf->code_matches(g + 0x6429B9, "80 7c 1e 35 00") && hf->code_matches(g + 0x645B80, "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18");
    if (g_misOk) m_setEngine = (SetEngine)(g + 0x645B80);
    Log("ready: Ctrl+F11 stops time and starts it again. Parts on: shots %s, grenades %s (actor table read at the first stop), fuses %s, missiles %s",
        g_shotsOk ? "yes" : "NO", g_thrOk ? "yes" : "NO", g_fuseOk ? "yes" : "NO", g_misOk ? "yes" : "NO");
}
/* The actor table is filled by the engine at start; read it once a mission runs */
static void MissionStart(HfMod* mod, const HfEventInfo* info, void* user)
{
    if (!game || !g_thrOk || a_setVel) return;
    if (!ActorApi()) { g_thrOk = 0; Log("the engine's actor table is not readable: grenades stay off"); }
    else Log("actor table: position %p, set position %p, set velocity %p, velocity %p", (void*)a_getPos, (void*)a_setPos, (void*)a_setVel, (void*)a_getVel);
}
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 16 || api->size < offsetof(HfApi, global) + sizeof api->global || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    Log("Shot Freeze Probe " VERSION ": research towards a time stop (shots, rockets, grenades, missiles and fuses held and let go)");
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    if (!api->subscribe(mod, HF_ON_MISSION_START, (HfListener)MissionStart, NULL, 0) || !api->on_game_thread(mod, Tick, NULL)) Log("events or the game thread are unavailable");
    return 0;
}
