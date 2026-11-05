/* Parry Probe (yui.parry-probe): research for Texas the Omertosa's parry (docs/texas-design.md). Host or solo.
 *
 * With a melee weapon in hand (the CQC stratagem and armory melee weapons; Ctrl+F10: any weapon), a right-click press
 * within the window before a hit lands (0.2 s; F10 cycles 0.15 / 0.2 / 0.3 / 0.5) is a perfect parry:
 *   - the hit is nulled in the game's hit calculation (the kit base's hit hook: no damage, knockdown, stagger or push),
 *     and its damage event cancelled as a backstop;
 *   - she is teleported behind the attacker (its root node's facing; 2.2 m behind, put on the ground by a raycast down;
 *     not beyond 25 m), with the Warp Pack's position setter and a zeroed velocity (as Faelynn's Loyal Dash);
 *   - the attacker takes a counter: an APW-1 round from behind at its chest and a Stun Medium.
 * Every hit on her is logged (the first 80) with how long after her last right-click press it came, its kind, the
 * dealer and owner and whether she held a melee weapon, so the window can be set from real presses.
 * Credit: the hit hook, entity, scene-graph and raycast reads are the kit base's (Helldiver Survival Rework, MIT;
 * HD2Runtime by SkyeShade); the teleport is the LIFT-182 Warp Pack's call as Warp Probe found it. */
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <windows.h>
#include "hellforge_mod.h"
#include "hellforge_components.h"
#include "hellforge_content.h"

#define VERSION "0.1.0"
#define BUILD "25480438"
#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"
#include "kitbase/kb_hits.h"
#include "kitbase/kb_damage.h"

static const float kWindows[4] = {0.15f, 0.2f, 0.3f, 0.5f};
static int g_window = 1, g_anyWeapon;
static atomic_ullong g_pressMs;          /* her last right-click press (GetTickCount64) */
static atomic_int g_melee;               /* a melee weapon in hand */
static unsigned g_hitsLogged, g_parries, g_counters, g_misses;

/* ---- what she holds ------------------------------------------------------------------------------------------ */
static const uint64_t kMelee[] = {HF_ENTITY_CQC20_BREACHING_HAMMER, HF_ENTITY_CQC72_ENTRENCHMENT_TOOL, HF_ENTITY_CQC9_DEFOLIATION_TOOL,
                                  HF_ENTITY_CQC1_ONE_TRUE_FLAG, HF_ENTITY_CQC19_STUN_LANCE, HF_ENTITY_CQC2_SABER, HF_ENTITY_CQC30_STUN_BATON,
                                  HF_ENTITY_CQC42_MACHETE, HF_ENTITY_CQC5_COMBAT_HATCHET};
static void OnWeaponParry(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfWeaponEvent* w = (const HfWeaponEvent*)info;
    if (!w->avatar || w->avatar != atomic_load(&g_avatar)) return;
    int melee = 0;
    for (unsigned i = 0; i < sizeof kMelee / sizeof kMelee[0]; i++) if (w->resource == kMelee[i]) melee = 1;
    atomic_store(&g_melee, melee);
    Log("she holds %016llx (selection %u): %s", (unsigned long long)w->resource, w->selection, melee ? "MELEE: the parry is armed" : "not melee");
}
static int Armed(void) { return g_anyWeapon || atomic_load(&g_melee); }

/* ---- the parry: in the hit hook --------------------------------------------------------------------------------- */
typedef struct Counter { uint32_t dealer, owner, kind; float origin[3], at[3]; double ms; } Counter;
static Counter g_counter;
static atomic_int g_counterReady;
static atomic_ullong g_parryUntilMs;     /* the damage event's backstop cancels hits until then */
static int HitTaken(const uint8_t* h, void* result, unsigned long long now)
{
    unsigned long long press = atomic_load(&g_pressMs);
    double dt = press ? (double)(now - press) / 1000.0 : 99;
    int armed = Armed(), parry = armed && dt <= kWindows[g_window];
    uint32_t kind = Word(h), dealer = Word(h + 0x80), owner = Word(h + 0x100);
    if (g_hitsLogged++ < 80)
        Log("hit on her: kind %u, dealer %u, owner %u; %.3f s after her right-click press%s%s", kind, dealer, owner, dt, armed ? "" : " (not armed: no melee)",
            parry ? ": PERFECT PARRY" : armed && dt < 1 ? ": too late" : "");
    if (!parry) { if (armed && dt < 1) g_misses++; return 0; }
    memset(result, 0, RESULT_SIZE);
    g_parries++;
    atomic_store(&g_parryUntilMs, now + 150);
    if (!atomic_load(&g_counterReady))
    {
        g_counter.dealer = dealer;
        g_counter.owner = owner;
        g_counter.kind = kind;
        memcpy(g_counter.origin, h + 0x40, 12);
        g_counter.ms = (double)now;
        atomic_store(&g_counterReady, 1);
    }
    return 1;
}
static void DamageTaken(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    if (GetTickCount64() < atomic_load(&g_parryUntilMs) && d->amount > 0) { e->cancel = 1; Log("  (backstop: its damage event cancelled too, %d)", d->amount); }
}

/* ---- the counter: on the game thread ---------------------------------------------------------------------------- */
typedef void (*SetVecFn)(void*, uint32_t, const float*);
typedef uint32_t (*WorldFn)(const void*);
typedef uint32_t (*RayFn)(uint32_t, const void*, const void*, float, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t);
static SetVecFn g_setPosition, g_setVelocity;
static WorldFn g_worldId;
static RayFn g_raycast;
/* a node's 64-byte world matrix (rows: right, forward, up, translation) */
static int UnitMatrix(uint32_t unit, uint32_t node, float m[16])
{
    uintptr_t registry = Pointer(exe + 0x1A100F0), objects, object, nodes;
    uint32_t count, id, slot = unit & 0x3FFFFF;
    if (!registry || !(objects = Pointer(registry + 0x88)) || !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit ||
        !ReadAt(object + 0x70, &count, 4) || node >= count || count > 4096 || !(nodes = Pointer(object + 0x88)) || !ReadAt(nodes + 64u * node, m, 64))
        return 0;
    for (int i = 0; i < 16; i++) if (!isfinite(m[i])) return 0;
    return 1;
}
/* the ground under a point: the first hit straight down from 2 m above it, within 6 m below it */
static int Ground(float p[3], uint32_t skip, uint32_t skip2)
{
    uintptr_t world = Pointer(game + 0x346BFA0);
    if (!world || !g_raycast || !g_worldId) return 0;
    uint32_t id = g_worldId((const void*)world);
    if (id >= 4) return 0;
    float from[3] = {p[0], p[1], p[2] + 2}, down[3] = {0, 0, -1};
    uint8_t hits[16 * 44];
    memset(hits, 0, sizeof hits);
    uint32_t n = g_raycast(id, from, down, 8, 2, 1, 0, 0x80000009u, skip, hits, 16);
    if (n > 16) n = 16;
    float best = 99;
    for (uint32_t i = 0; i < n; i++)
    {
        float d;
        memcpy(&d, hits + 44u * i + 24, 4);
        uint32_t hu = Word(hits + 44u * i + 28);
        if (!isfinite(d) || d < 0.05f || (hu && (hu == skip || hu == skip2))) continue;
        if (d < best) best = d;
    }
    if (best > 8) return 0;
    p[2] = from[2] - best;
    return 1;
}
static uint32_t Attacker(const Counter* c)
{
    for (int k = 0; k < FOES; k++) if (g_foe[k] && (g_foe[k] == c->dealer || g_foe[k] == c->owner)) return g_foe[k];
    /* else the enemy nearest where the hit came from (within 6 m of it) */
    float q[3];
    uint32_t best = 0;
    float bd = 36;
    for (int k = 0; k < FOES; k++)
    {
        uint8_t d[24];
        if (!g_foe[k] || !Descriptor(g_foe[k], d) || !UnitNode(Word(d + 12), 0, q)) continue;
        float d2 = Dist2(q, c->origin);
        if (d2 < bd) { bd = d2; best = g_foe[k]; }
    }
    return best;
}
static void CounterFrame(void)
{
    if (!atomic_load(&g_counterReady)) return;
    Counter c = g_counter;
    atomic_store(&g_counterReady, 0);
    uint32_t me = atomic_load(&g_avatar), foe = Attacker(&c);
    float mine[3], m[16];
    uint8_t d[24];
    if (!foe || !Descriptor(foe, d) || !UnitMatrix(Word(d + 12), 0, m) || !PlayerPose(mine, NULL))
    { Log("counter: the attacker is not known (dealer %u, owner %u): the hit is parried, no teleport", c.dealer, c.owner); return; }
    uint32_t unit = Word(d + 12), myUnit = 0;
    uint8_t md[24];
    if (Descriptor(me, md)) myUnit = Word(md + 12);
    float at[3] = {m[12], m[13], m[14]}, fwd[3] = {m[4], m[5], m[6]};
    float h = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1]);
    if (h < 0.1f) { fwd[0] = at[0] - mine[0]; fwd[1] = at[1] - mine[1]; h = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1]); }   /* else: away from her */
    if (h < 0.01f) { Log("counter: no facing for entity %u", foe); return; }
    fwd[0] /= h; fwd[1] /= h;
    float dist = sqrtf(Dist2(at, mine));
    if (dist > 25) { Log("counter: the attacker (entity %u) is %.0f m away: parried, no teleport", foe, dist); return; }
    float dest[3] = {at[0] - fwd[0] * 2.2f, at[1] - fwd[1] * 2.2f, at[2]};
    if (!Ground(dest, myUnit, unit)) { Log("counter: no ground behind entity %u: parried, no teleport", foe); return; }
    if (!g_setPosition) { Log("counter: the teleport is off"); return; }
    float zero[3] = {0, 0, 0};
    dest[2] += 0.05f;
    g_setPosition(NULL, me, dest);
    g_setVelocity(NULL, me, zero);
    g_counters++;
    /* the strike: a heavy round from behind at its chest, and a stun */
    float from[3] = {dest[0], dest[1], dest[2] + 1.3f}, aim[3] = {at[0] - from[0], at[1] - from[1], at[2] + 1.0f - from[2]};
    float n = sqrtf(aim[0] * aim[0] + aim[1] * aim[1] + aim[2] * aim[2]);
    int shot = 0, stun = 0;
    if (n > 0.3f)
    {
        for (int k = 0; k < 3; k++) { aim[k] /= n; from[k] += aim[k] * 0.6f; }
        shot = Act("the counter's round", hf->spawn_projectile(self, HF_PROJECTILE_APW1_ANTIMATERIEL_RIFLE, from, aim));
    }
    if (hf->apply_status) stun = Act("the counter's stun", hf->apply_status(self, foe, HF_STATUS_STUN_MEDIUM, 300));
    Log("COUNTER: behind entity %u (%.1f m from her, facing %.2f %.2f), %.0f ms after the hit; round %s, stun %s", foe, dist, fwd[0], fwd[1],
        (double)(GetTickCount64() - (unsigned long long)c.ms), shot ? "fired" : "NOT fired", stun ? "applied" : "not applied");
}

/* ---- events and the frame ----------------------------------------------------------------------------------------- */
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    if (s->resource != AVATAR) { if (FoeKind(s->resource)) AddFoe(s->entity, s->resource); return; }
    if (!s->authority) return;
    g_spawnEntity[g_spawnNext % 8] = s->entity;
    g_spawnNet[g_spawnNext % 8] = s->network;
    g_spawnNext++;
    LearnNet();
}
static void OnKill(HfMod* mod, const HfEventInfo* info, void* user) { DropFoe(((const HfKillEvent*)info)->victim); }
static void OnPlayer(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    if (!p->local) return;
    if (info->kind == HF_ON_PLAYER_SPAWN) { if (p->entity) atomic_store(&g_avatar, p->entity); g_alive = 1; LearnNet(); }
    else g_alive = 0;
}
static void Tick(HfMod* mod, float dt, void* user)
{
    g_now += dt > 0 && dt < 1 ? dt : 0;
    int in = hf->game_state(self) == HF_STATE_MISSION;
    if (in != g_inMission)
    {
        if (!in && (g_parries || g_hitsLogged)) Log("mission end: %u hits logged, %u perfect parries, %u counters, %u presses too late", g_hitsLogged, g_parries, g_counters, g_misses);
        g_inMission = in;
        g_hitsLogged = g_parries = g_counters = g_misses = 0;
    }
    if (!in) return;
    static int prevR, prevF10;
    int focused = Focused();
    int r = focused && (GetAsyncKeyState(VK_RBUTTON) & 0x8000);
    if (r && !prevR) atomic_store(&g_pressMs, GetTickCount64());
    prevR = r;
    int f10 = focused && (GetAsyncKeyState(VK_F10) & 0x8000);
    if (f10 && !prevF10)
    {
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) { g_anyWeapon = !g_anyWeapon; Log("Ctrl+F10: %s", g_anyWeapon ? "any weapon arms the parry" : "melee only"); }
        else { g_window = (g_window + 1) % 4; Log("F10: the parry window is %.2f s", (double)kWindows[g_window]); }
    }
    prevF10 = f10;
    CounterFrame();
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gs = 0, xs = 0;
    uintptr_t g = hf->module_base("game.dll", &gs), x = hf->module_base("helldivers2.exe", &xs);
    if (!g || !x || gs != 74727424u || xs != 60719104u) { Log("REFUSED: not build " BUILD); return; }
    game = g;
    exe = x;
    if (!HitsInstall("parry hook")) return;
    if (hf->code_matches(game + 0x5A2BB0, "48 89 5c 24 08 48 89 6c 24 10 56 57 41 54 41 56 41 57 48 83 ec 40") &&
        hf->code_matches(game + 0x4A7550, "48 89 5c 24 18 57 48 83 ec 20 3b 15 ?? ?? ?? ?? 48 8b 1d") &&
        hf->code_matches(exe + 0x79F860, "33 c0 4c 8d 05 c7 b2 01 02 0f 1f 80 00 00 00 00 49 39 0c c0 74 0c ff c0 83 f8 04 75 f3") &&
        hf->code_matches(exe + 0x7F4590, "48 8b c4 f3 0f 11 58 20 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48"))
    {
        g_setPosition = (SetVecFn)(game + 0x5A2BB0);
        g_setVelocity = (SetVecFn)(game + 0x4A7550);
        g_worldId = (WorldFn)(exe + 0x79F860);
        g_raycast = (RayFn)(exe + 0x7F4590);
        Log("teleport ready");
    }
    else Log("teleport off: its functions are not the reviewed code (parries only)");
    Log("ready: melee in hand, right-click just before a hit (window %.2f s; F10 cycles it, Ctrl+F10 any weapon)", (double)kWindows[g_window]);
}
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    kb_hit_taken = HitTaken;
    kb_damage_taken = DamageTaken;
    if (!api || api->version < 13 || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    Log("Parry Probe " VERSION ": research for Texas's parry");
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) && api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) &&
             api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) &&
             api->subscribe(mod, HF_ON_WEAPON, OnWeaponParry, NULL, 0) && api->on_game_thread(mod, Tick, NULL);
    if (!ok) Log("events or the game thread are unavailable");
    return 0;
}
