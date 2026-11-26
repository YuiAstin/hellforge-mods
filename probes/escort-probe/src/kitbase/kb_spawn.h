/* kitbase layer 2: making things. Explosions that wait for their packages, entities spawned by resource, the bare
 * engine units the game draws things with (a minefield's mines), and taking them away again.
 *
 * Code shared by the character kits. Source of truth: kitbase/ at the repo root. The copy in a kit's src/kitbase/ is
 * written by tools/kitbase_sync.py, so edit kitbase/, never the copy. See docs/kit-base.md.
 * Lifted 2026-11-26: the strike queue from Destroyer's BlastFrame (every kit that sets off explosions retried
 * HF_ACT_WAITING its own way: Alva, Ascalon, Executor, Exusiai, Faelynn, Koleda, Lemuen, Lenna, W, and the two
 * stratagem mods); the spawn record, the package lookup and the unit calls from W (0.4 to 0.7.8, see docs/w-design.md).
 *
 * A kit calls KbSpawnInstall() in its GAME_LOADED handler and KbStrikeFrame() every frame it acts. Include after
 * kb_core.h and kb_player.h; include kb_hits.h first to have strikes count as her own blasts (its own-blast guard). */
#ifndef KB_SPAWN_H
#define KB_SPAWN_H

#include "kb_core.h"
#include "kb_player.h"

/* ---- packages ------------------------------------------------------------------------------------------------- */
/* An entity resource's loadout package (its LoadoutPackageComponentData +8), 0 when it has none */
KB_FN uint64_t KbPackageOf(uint64_t res)
{
    uint32_t stride = 0;
    uint64_t pkg = 0;
    uintptr_t r = hf->component_record ? hf->component_record("LoadoutPackageComponentData", res, &stride) : 0;
    return r && ReadAt(r + 8, &pkg, 8) && pkg != ~0ull ? pkg : 0;
}
/* Asks for an entity resource's package: HF_PACKAGE_RESIDENT when it is in (or it has none), HF_PACKAGE_LOADING while
 * it comes, 0 refused. Call each frame until resident. */
KB_FN int KbLoadFor(uint64_t res)
{
    uint64_t pkg = KbPackageOf(res);
    return pkg ? hf->load_package(self, pkg) : HF_PACKAGE_RESIDENT;
}

/* ---- explosions: the strike queue ------------------------------------------------------------------------------- */
/* An explosion asked for while its packages load answers HF_ACT_WAITING and has to be asked again. A strike is queued
 * with a delay and kept for kb_strike_window s past it; KbStrikeFrame asks until the game takes it. On success the
 * kit hears kb_strike_done (its tag and where), and with kb_hits.h the place is one of her own blasts for a moment. */
#define KB_STRIKES 64
typedef struct KbStrikeQ { float p[3]; double at, until; uint32_t type; int tag; } KbStrikeQ;
KB_VAR KbStrikeQ g_strike[KB_STRIKES];
KB_VAR int g_nstrike;
KB_VAR float kb_strike_window = 4;
KB_VAR unsigned g_strikesDone, g_strikesLost;
KB_VAR void (*kb_strike_done)(int tag, const float p[3], uint32_t type);
KB_FN int KbStrike(uint32_t type, const float p[3], double delay, int tag)
{
    if (g_nstrike >= KB_STRIKES || !type || !hf->spawn_explosion) return 0;
    KbStrikeQ* q = &g_strike[g_nstrike++];
    memcpy(q->p, p, sizeof q->p);
    q->at = g_now + delay;
    q->until = q->at + kb_strike_window;
    q->type = type;
    q->tag = tag;
    return 1;
}
KB_FN void KbStrikeFrame(void)
{
    for (int i = 0; i < g_nstrike; i++)
    {
        KbStrikeQ* q = &g_strike[i];
        if (g_now < q->at) continue;
        int r = hf->spawn_explosion(self, q->type, q->p);
        if (r == HF_ACT_WAITING && g_now < q->until) continue;   /* its packages are loading: again next frame */
        if (r != HF_ACT_WAITING && Act("a strike", r))
        {
            g_strikesDone++;
#ifdef KB_HITS_H
            unsigned slot = g_ownNext++ % KB_OWN_SLOTS;
            memcpy(g_ownPos[slot], q->p, sizeof g_ownPos[slot]);
            atomic_store(&g_ownMs[slot], GetTickCount64());
#endif
            if (kb_strike_done) kb_strike_done(q->tag, q->p, q->type);
        }
        else if (g_strikesLost++ < 6) Log("a strike (explosion %u) never came: %s", q->type, r == HF_ACT_WAITING ? "its packages did not load in time" : "refused");
        g_strike[i] = g_strike[--g_nstrike];
        i--;
    }
}
KB_FN void KbStrikesClear(void) { g_nstrike = 0; }

/* ---- entities -------------------------------------------------------------------------------------------------- */
/* An entity spawned by resource with its feet at `at` (RequestEntitySpawn: a 0x58-byte record, +0 1, +4 network 0x7FFF
 * (none), +8 a 4x4 pose, +0x48 a zeroed 0x8A0-byte context). Its package must be resident (KbLoadFor). A weapon made
 * this way lies inert until it is taken into an inventory or dropped once; give it 0.75 s before taking it (taken in
 * its spawn frame it stayed where it was made). Returns the entity, 0 when none came back. */
KB_VAR uint8_t g_spawnContext[0x8A0];
KB_FN uint32_t KbSpawnEntity(uint64_t res, const float at[3], const char* what)
{
    HfFn_RequestEntitySpawn spawn = hf_fn_RequestEntitySpawn(self);
    uint32_t out = 0;
    if (!spawn) { Log("%s could not be asked for (no RequestEntitySpawn)", what); return 0; }
    struct { uint32_t one, network; float pose[16]; void* context; uint64_t pad; } info;
    _Static_assert(sizeof info == 0x58, "the spawn record is 0x58 bytes");
    memset(&info, 0, sizeof info);
    memset(g_spawnContext, 0, sizeof g_spawnContext);
    info.one = 1;
    info.network = 0x7FFF;
    info.pose[0] = info.pose[5] = info.pose[10] = info.pose[15] = 1.0f;
    info.pose[12] = at[0]; info.pose[13] = at[1]; info.pose[14] = at[2];
    info.context = g_spawnContext;
    spawn(&out, res, &info);
    if (!out || out == UINT32_MAX) { Log("%s: no entity came back", what); return 0; }
    return out;
}
/* At her side, a metre up */
KB_FN uint32_t KbSpawnNear(uint64_t res, const char* what)
{
    float p[3];
    if (!PlayerPose(p, NULL)) { Log("%s could not be asked for (she is not readable)", what); return 0; }
    p[2] += 1.0f;
    return KbSpawnEntity(res, p, what);
}
/* Takes an entity away, only while it still reads as `res` (an entity id is reused) and only in a mission */
KB_FN int KbRemoveEntity(uint32_t e, uint64_t res)
{
    HfFn_RemoveEntity remove = hf_fn_RemoveEntity(self);
    void* em = hf_global_EntityManager(self);
    uint8_t d[24];
    if (!e || !g_inMission || !remove || !em || !Descriptor(e, d) || (res && Quad(d) != res)) return 0;
    remove(em, e);
    return 1;
}

/* ---- bare units ------------------------------------------------------------------------------------------------ */
/* What the game draws without an entity: a minefield's mines are units it spawns where the deployer lands, their handles
 * kept in the field's slots (game.dll+0x3326958; W 0.6.2's probe). Their resources: the MD-6's ee89cc10d80b6afb, the
 * MD-17's 032489cf1d439bba, the MD-8's 693028644d27ffef, the MD-I4's e49a5ae078aad407; all in their deployer's loadout
 * package (the MD-6's a4946425b2c70017, packages/generated/loadout/mine_launcher). */
#define KB_UNIT_TYPE UINT64_C(0xE0A48D0BE9A7453F)   /* the engine's resource type of a unit */
KB_VAR int g_unitRemoveOk;
KB_FN int KbUnitLoaded(uint64_t res)
{
    HfFn_CanGetResource can = hf_fn_CanGetResource(self);
    return can && (can(KB_UNIT_TYPE, res) & 1);
}
/* A unit of `res` in the main world at `at` (unturned). WorldSpawnUnit reads its name through the pointer whatever it
 * holds: a pointer to 0 for none, never NULL (W 0.7.2 and 0.7.3 passed NULL: helldivers2.exe+0x1A804B read through it
 * and the game ended). Spawned only when the engine can hand the resource out. Returns the handle, 0 none. */
KB_FN uint32_t KbSpawnUnit(uint64_t res, const float at[3])
{
    HfFn_WorldSpawnUnit spawn = hf_fn_WorldSpawnUnit(self);
    HfFn_MainWorld world = hf_fn_MainWorld(self);
    float pose[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, at[0], at[1], at[2], 1};
    uint64_t r = res, name = 0;
    uint32_t handle = 0;
    void* w = world ? world() : NULL;
    if (!spawn || !w || !KbUnitLoaded(res)) return 0;
    void* unit = spawn(w, &r, &name, pose, 0, 0, 1, 0);
    if (!unit || !ReadAt((uintptr_t)unit + 8, &handle, 4)) return 0;
    return handle;
}
/* Takes a unit away as the game's minefield takes away a mine that did not land well (game.dll+0x9DDA5F: the engine's
 * world table's +0x60 with the game's world and the handle). Hiding it (UnitSetUnitVisibility) when that code is not
 * the reviewed one. */
KB_FN void KbUnitRemove(uint32_t handle)
{
    typedef void (*DestroyFn)(void* world, uint32_t unit);
    HfFn_UnitAlive alive = hf_fn_UnitAlive(self);
    uintptr_t api, table;
    DestroyFn destroy;
    void* world;
    if (!handle || !g_inMission || !alive || !(alive(handle) & 1)) return;
    if (g_unitRemoveOk && (api = Pointer(game + 0x3326308)) && (table = Pointer(api + 8)) && (destroy = (DestroyFn)Pointer(table + 0x60)) &&
        (world = (void*)Pointer(game + 0x346BFA0)))
    {
        destroy(world, handle);
        return;
    }
    HfFn_UnitSetUnitVisibility show = hf_fn_UnitSetUnitVisibility(self);
    if (show) show(handle, 0, 1);
}

KB_FN void KbSpawnInstall(void)
{
    g_unitRemoveOk = hf->code_matches(game + 0x9DDA5F, "48 8b 05 a2 88 94 02 8b 16 48 8b 0d 31 e5 a8 02 4c 8b 40 08 41 ff 50 60");
    if (!g_unitRemoveOk) Log("spawning: the minefield's code is not the reviewed one: units are hidden, not taken away");
}

#endif
