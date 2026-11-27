/* Veterancy System: the AX/FLAM-75 Hot Dog (hot_dog.c).
 *
 * Its kills this mission level it (hd_level2_kills, hd_level3_kills; force_level too): the mission stats' kills from the
 * Hot Dog (0x3015626AA69F8D4D; the dog's hits are told by the same resource as their dealer, as the gas doctrine tells
 * the Dog Breath's).
 *   Level 2, Veteran: napalm, faked: the dog's fire deals hd_damage_mult times its damage and sets what it hits on fire
 *     every time (the fire status, a full build-up); and while it is out, fire does nothing to you (its damage on your
 *     Helldiver is cancelled: the dog's own, and any of fire's element).
 *   Level 3, Elite: every enemy the dog has set on fire builds up a stack a second while it burns; each of your shots
 *     that hits it adds one more, at most one every hd_player_gap seconds. At hd_stacks stacks the dog fires a plasma
 *     bolt at it (hd_bolt_damage, AP hd_bolt_ap; the SG-8P Punisher Plasma's shot, a copy with its own damage row) and
 *     its count goes back to 0, as it does when it stops burning. A bolt that misses (it ends more than 3 m from its
 *     enemy) is fired again from a metre in front of it, so ten stacks always land.
 * An enemy counts as burning for hd_burn_memory seconds after the dog's fire or any tick of fire on it.
 * Solo or host. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hellforge_mod.h"
#include "hellforge_components.h"
#include "hellforge_content.h"
#include "hellforge_functions.h"
#include "field_promotions.h"

#define HOTDOG HF_ENTITY_AXFLAM75_HOT_DOG
#define HOTDOG_DOG 0x65CD4325BA23F3C6ull   /* the dog itself: the mission stats credit its kills and damage to this (0.21.0, played) */
static int IsDog(uint64_t r) { return r == HOTDOG || r == HOTDOG_DOG; }

static const HfApi* g_hf;
static HfMod* g_mod;
static uintptr_t g_game;
static int g_kills, g_level = 1;
static double g_now;
static uint32_t g_dog, g_avatar;
static uint32_t g_dogs[4];
static uint32_t g_dogBody;   /* the dog itself (resource HOTDOG_DOG) */   /* every Hot Dog entity spawned: the backpack on you and the dog itself (0.21.6 logged two) */
static int g_fireElem = -1;
static uint32_t g_boltType, g_boltDamage;
static int g_boltReady;
static unsigned g_hits, g_doubled, g_lit, g_cancelled, g_bolts, g_refired, g_playerStacks;
static int g_topStacks;
static long long g_dmgBefore, g_dmgAfter;

/* inigen:begin */
static int s_hdL2 = 30, s_hdL3 = 150, s_hdIgnite = 1, s_hdImmune = 1, s_hdStacks = 10, s_hdBoltDamage = 1000, s_hdBoltAp = 6, s_hdBoltRound = 57;
static float s_hdMult = 2, s_hdPlayerGap = 0.5f, s_hdBurnMemory = 1.2f;
/* inigen:end */

int hd_setting(const char* key, double v)
{
#define CL(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
    if (!strcmp(key, "hd_level2_kills")) s_hdL2 = (int)CL(v, 0, 100000);
    else if (!strcmp(key, "hd_level3_kills")) s_hdL3 = (int)CL(v, 0, 100000);
    else if (!strcmp(key, "hd_damage_mult")) s_hdMult = (float)CL(v, 1, 20);
    else if (!strcmp(key, "hd_ignite")) s_hdIgnite = v != 0;
    else if (!strcmp(key, "hd_fire_immune")) s_hdImmune = v != 0;
    else if (!strcmp(key, "hd_stacks")) s_hdStacks = (int)CL(v, 1, 100);
    else if (!strcmp(key, "hd_player_gap")) s_hdPlayerGap = (float)CL(v, 0, 10);
    else if (!strcmp(key, "hd_burn_memory")) s_hdBurnMemory = (float)CL(v, 0.2, 10);
    else if (!strcmp(key, "hd_bolt_damage")) s_hdBoltDamage = (int)CL(v, 1, 100000);
    else if (!strcmp(key, "hd_bolt_ap")) s_hdBoltAp = (int)CL(v, 0, 10);
    else if (!strcmp(key, "hd_bolt_round")) s_hdBoltRound = (int)CL(v, 1, 350);
    else return 0;
    return 1;
#undef CL
}

/* inigen:begin */
void hd_write_defaults(FILE* f)
{
    fprintf(f, "\n; AX/FLAM-75 Hot Dog: its kills this mission for level 2 (Veteran) and level 3 (Elite).\nhd_level2_kills = %d\nhd_level3_kills = %d\n"
               "; Level 2, napalm: the dog's fire deals hd_damage_mult times its damage, and hd_ignite 1 sets what it hits on fire\n"
               "; every time. hd_fire_immune 1: while it is out, fire does nothing to you.\nhd_damage_mult = %g\nhd_ignite = %d\nhd_fire_immune = %d\n"
               "; Level 3: an enemy the dog set on fire builds a stack a second while it burns; each of your shots that hits it\n"
               "; adds one, at most one every hd_player_gap s. At hd_stacks the dog fires a plasma bolt at it (hd_bolt_damage, AP\n"
               "; hd_bolt_ap; projectile row hd_bolt_round, 57: the SG-8P Punisher Plasma's shot, as the bolt's look), and its\n"
               "; count goes back to 0, as it does when it stops burning. An enemy burns for hd_burn_memory s after fire on it.\n"
               "hd_stacks = %d\nhd_player_gap = %g\nhd_bolt_damage = %d\nhd_bolt_ap = %d\nhd_bolt_round = %d\nhd_burn_memory = %g\n",
        s_hdL2, s_hdL3, (double)s_hdMult, s_hdIgnite, s_hdImmune, s_hdStacks, (double)s_hdPlayerGap, s_hdBoltDamage, s_hdBoltAp, s_hdBoltRound,
        (double)s_hdBurnMemory);
}
/* inigen:end */

static int Level(void)
{
    if (fp_forced_level()) return fp_forced_level();
    return g_kills >= s_hdL3 ? 3 : g_kills >= s_hdL2 ? 2 : 1;
}
int hd_level(void) { return Level(); }
uint32_t fp_avatar(void) { return g_avatar; }

static int AddressOk(uintptr_t a, size_t n) { return n && a >= 0x10000 && a < UINT64_C(0x800000000000) && n <= UINT64_C(0x800000000000) - a; }
static int ReadAt(uintptr_t a, void* out, size_t n) { return AddressOk(a, n) && g_hf->read(a, out, n) == 0; }
static uintptr_t Pointer(uintptr_t a) { uintptr_t v = 0; return ReadAt(a, &v, 8) && AddressOk(v, 1) ? v : 0; }
static float Dist2(const float a[3], const float b[3]) { float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; return x * x + y * y + z * z; }

/* ---- the enemies on fire -------------------------------------------------------------------------------------------- */
#define BURNS 128
typedef struct Burn { uint32_t e; double burnUntil, litAt, nextStack, playerAt, igniteAt; int stacks; } Burn;
static Burn g_burn[BURNS];
static Burn* BurnOf(uint32_t e, int make)
{
    int free_ = -1, old = 0;
    for (int i = 0; i < BURNS; i++)
    {
        if (g_burn[i].e == e) return &g_burn[i];
        if (!g_burn[i].e && free_ < 0) free_ = i;
        if (g_burn[i].burnUntil < g_burn[old].burnUntil) old = i;
    }
    if (!make) return NULL;
    Burn* b = &g_burn[free_ >= 0 ? free_ : old];
    memset(b, 0, sizeof *b);
    b->e = e;
    return b;
}
static int IsFire(const HfDamageEvent* d)
{
    if (g_fireElem < 0 || (int)d->element != g_fireElem) return 0;
    return d->kind == 2 || g_fireElem != 0;   /* 0 is also plain bullets' element: then only fire's ticks */
}
static int IsFoe(uint32_t e)
{
    for (int i = 0, n = fp_foe_count(); i < n; i++) if (fp_foe(i) == e) return 1;
    return 0;
}

/* a dealer's resource: the gas doctrine's map of network ids (the entities it saw spawn), else the game's own lookup
 * (the dog's fire comes from network ids the doctrine never saw: 0.21.0 logged them as unmapped) */
static uint64_t DealerResource(uint32_t network, uint32_t* entity)
{
    uint64_t r = gas_dealer_resource(network, entity);
    if (r) return r;
    HfFn_EntityDescriptor descriptor = hf_fn_EntityDescriptor(g_mod);
    if (!network || network >= 0x7FFF || !descriptor || g_hf->size < offsetof(HfApi, entity_of_network) + sizeof g_hf->entity_of_network || !g_hf->entity_of_network) return 0;
    uint32_t e = g_hf->entity_of_network(g_mod, network);
    if (e == 0xFFFFFFFFu || !e) return 0;
    uint8_t* d = descriptor(e);
    uint64_t res = 0;
    if (!d || !ReadAt((uintptr_t)d, &res, 8)) return 0;
    if (entity) *entity = e;
    return res;
}

static void OnDamagePre(HfMod* mod, const HfEventInfo* info, void* user)
{
    HfDamagePreEvent* e = (HfDamagePreEvent*)info;
    HfDamageEvent* d = &e->damage;
    int level = Level();
    if (!fp_in_mission() || level < 2) return;
    uint32_t dealer = 0, owner = 0;
    uint64_t dr = DealerResource(d->dealer, &dealer), orr = DealerResource(d->owner, &owner);
    int dog = IsDog(dr) || IsDog(orr);
    if (dog && g_fireElem < 0 && d->amount > 0)
    {
        g_fireElem = (int)d->element;
        g_hf->log(g_mod, "Hot Dog: its fire's damage element is %d (kind %u)", g_fireElem, d->kind);
    }
    /* fire does nothing to you while your dog is out */
    if (s_hdImmune && g_avatar && d->target == g_avatar && g_dog && (dog || IsFire(d)))
    {
        e->cancel = 1;
        if (g_cancelled++ < 6) g_hf->log(g_mod, "Hot Dog: fire on you cancelled (%d, kind %u, element %u)", d->amount, d->kind, d->element);
        return;
    }
    if (d->target == g_avatar || !IsFoe(d->target)) return;
    Burn* b = NULL;
    if (dog)
    {
        g_hits++;
        if (d->amount > 0 && s_hdMult != 1)
        {
            g_dmgBefore += d->amount;
            double a = floor(d->amount * (double)s_hdMult + 0.5);
            d->amount = a > 2147483647.0 ? 2147483647 : (int32_t)a;
            g_dmgAfter += d->amount;
            g_doubled++;
        }
        b = BurnOf(d->target, 1);
        if (!b->litAt || g_now - b->burnUntil > 0) b->nextStack = g_now + 1.0;
        b->litAt = g_now;
        b->burnUntil = g_now + s_hdBurnMemory;
        if (s_hdIgnite && g_now - b->igniteAt >= 0.5)
        {
            b->igniteAt = g_now;
            if (g_hf->apply_status(g_mod, d->target, HF_STATUS_FIRE, 100.0f)) g_lit++;
        }
        return;
    }
    if (IsFire(d)) { b = BurnOf(d->target, 0); if (b) b->burnUntil = g_now + s_hdBurnMemory; return; }
    /* your shot on an enemy the dog set burning: a stack (Elite) */
    if (level >= 3 && g_avatar && (dealer == g_avatar || owner == g_avatar) && (d->kind == 0 || d->kind == 1 || d->kind == 3 || d->kind == 5))
    {
        b = BurnOf(d->target, 0);
        if (b && b->litAt && b->burnUntil > g_now && g_now - b->playerAt >= s_hdPlayerGap) { b->playerAt = g_now; b->stacks++; g_playerStacks++; }
    }
}

/* ---- the plasma bolt ------------------------------------------------------------------------------------------------ */
typedef struct Bolt { int slot; uint32_t target; double at; float last[3], dir[3]; int retry; } Bolt;
static Bolt g_bolt[16];
static int g_nbolt;
typedef struct BoltWait { uint32_t target; double at; float dir[3]; int retry; } BoltWait;
static BoltWait g_wait[16];
static int g_nwait;
static uintptr_t g_pool;
static uint32_t g_counter;
/* the dog's muzzle, from its skeleton: of its unit's scene-graph nodes (the registry at helldivers2.exe+0x1A100F0, as the
 * Koleda kit reads them: object +0x70 count, +0x88 64-byte world matrices, rows right / forward / up / position), the
 * one farthest along its forward axis within 1.5 m of its root and above it: the nozzle at its front. Its index is
 * logged the first times, so it can be pinned once seen. */
static uintptr_t g_exe;
static uintptr_t RegistryObject(uint32_t unit)
{
    uintptr_t registry = Pointer(g_exe + 0x1A100F0), generations, objects, object;
    uint32_t count, id, slot = unit & 0x3FFFFF;
    uint8_t generation;
    if (!g_exe || !registry || !ReadAt(registry + 0x98, &count, 4) || count > 0x400000 || slot >= count || !(generations = Pointer(registry + 0xA0)) ||
        !ReadAt(generations + slot, &generation, 1) || generation != (uint8_t)(unit >> 22) || !(objects = Pointer(registry + 0x88)) ||
        !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit)
        return 0;
    return object;
}
static int Muzzle(uint32_t entity, float out[3])
{
    uint32_t unit = fp_entity_unit(entity), count = 0;
    uintptr_t object = unit ? RegistryObject(unit) : 0, nodes;
    static float m[128 * 16];
    if (!object || !ReadAt(object + 0x70, &count, 4) || !count || count > 4096 || !(nodes = Pointer(object + 0x88))) return 0;
    if (count > 128) count = 128;
    if (!ReadAt(nodes, m, (size_t)count * 64)) return 0;
    const float* root = m;   /* node 0: its root */
    float fw[3] = {root[4], root[5], root[6]}, up[3] = {root[8], root[9], root[10]};
    float fl = sqrtf(fw[0] * fw[0] + fw[1] * fw[1] + fw[2] * fw[2]);
    if (!isfinite(fl) || fl < 0.1f) return 0;
    int best = -1;
    float bestAlong = -1e9f;
    for (uint32_t i = 1; i < count; i++)
    {
        const float* t = m + 16 * i + 12;
        float d[3] = {t[0] - root[12], t[1] - root[13], t[2] - root[14]};
        if (!isfinite(d[0]) || !isfinite(d[1]) || !isfinite(d[2]) || d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > 1.5f * 1.5f) continue;
        float along = (d[0] * fw[0] + d[1] * fw[1] + d[2] * fw[2]) / fl, high = d[0] * up[0] + d[1] * up[1] + d[2] * up[2];
        if (high < 0.05f) continue;
        if (along > bestAlong) { bestAlong = along; best = (int)i; }
    }
    if (best < 0) return 0;
    memcpy(out, m + 16 * best + 12, 12);
    static unsigned told;
    if (told++ < 4) g_hf->log(g_mod, "Hot Dog: its muzzle: node %d of %u (%.2f m ahead of its root, %.2f m up)", best, count, (double)bestAlong,
        (double)((out[0] - root[12]) * up[0] + (out[1] - root[13]) * up[1] + (out[2] - root[14]) * up[2]));
    return 1;
}
static int Aim(uint32_t e, float p[3]) { if (!fp_entity_pos(e, p)) return 0; p[2] += 0.8f; return 1; }
static int Fire(uint32_t target, const float from[3], const float dir[3], int retry)
{
    if (!g_boltType || !g_boltReady || g_nwait >= 16) return 0;
    int r = g_hf->spawn_projectile(g_mod, g_boltType, from, dir);
    if (!r || r == HF_ACT_WAITING)
    {
        static unsigned told;
        if (!r && told++ < 3) g_hf->log(g_mod, "Hot Dog: a plasma bolt was refused (%s)", g_hf->last_log ? g_hf->last_log(g_mod) : "");
        return 0;
    }
    BoltWait* w = &g_wait[g_nwait++];
    w->target = target; w->at = g_now; w->retry = retry;
    memcpy(w->dir, dir, 12);
    return 1;
}
static int Bolt_(uint32_t target)
{
    float from[3], to[3];
    if (!Aim(target, to)) return 0;
    /* from the dog itself: the entity of resource 0x65CD4325BA23F3C6. 0.21.7 took the Hot Dog entity farthest from
     * you, which was the backpack's (0x3015626AA69F8D4D): its position stays where it was picked up, 14 m off */
    float me[3] = {0, 0, 0}, best = 0;
    int haveMe = g_avatar && fp_entity_pos(g_avatar, me);
    int muzzle = g_dogBody && Muzzle(g_dogBody, from);
    uint32_t src = muzzle || (g_dogBody && fp_entity_pos(g_dogBody, from)) ? g_dogBody : 0;
    if (src)
    {
        if (!muzzle) from[2] += 0.6f;
        if (haveMe) best = (from[0] - me[0]) * (from[0] - me[0]) + (from[1] - me[1]) * (from[1] - me[1]) + (from[2] - me[2]) * (from[2] - me[2]);
    }
    else if (haveMe) { memcpy(from, me, 12); from[2] += 1.6f; }
    else return 0;
    static unsigned told;
    if (told++ < 6) g_hf->log(g_mod, "Hot Dog: the bolt leaves %s, %.1f m from you", src ? (muzzle ? "the dog's muzzle" : "the dog (its skeleton was not read)") : "you (the dog's position was not read)", haveMe && best > 0 ? (double)sqrtf(best) : 0.0);
    float dir[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]}, n = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (n < 0.1f) return 0;
    for (int k = 0; k < 3; k++) { dir[k] /= n; if (!muzzle) from[k] += dir[k] * 0.8f; }
    if (!Fire(target, from, dir, 0)) return 0;
    g_bolts++;
    if (g_bolts <= 10) g_hf->log(g_mod, "Hot Dog: %d stacks on enemy %u: a plasma bolt at it from %.0f m", s_hdStacks, target, (double)n);
    return 1;
}
/* a bolt that missed: again from a metre in front of its enemy */
static void Refire(uint32_t target, const float dir[3])
{
    float to[3];
    if (!Aim(target, to)) return;
    float from[3] = {to[0] - dir[0] * 1.2f, to[1] - dir[1] * 1.2f, to[2] - dir[2] * 1.2f};
    if (Fire(target, from, dir, 1)) g_refired++;
}
static void BoltsFrame(void)
{
    uintptr_t pool = Pointer(g_game + 0x347CEA8);
    uint8_t active = 0;
    uint32_t counter = 0;
    if (!pool || !ReadAt(pool + 0x28, &active, 1) || active != 1 || !ReadAt(pool + 0x30, &counter, 4)) return;
    if (g_pool != pool) { g_pool = pool; g_counter = counter; g_nbolt = 0; return; }
    uint32_t n = counter - g_counter, from = g_counter;
    g_counter = counter;
    if (n > 64 && n <= 2048) { from += n - 64; n = 64; }
    for (uint32_t i = 0; i < n && n <= 64 && g_nwait; i++)
    {
        int slot = (int)((from + i) & 0x7FF);
        uint32_t type = 0;
        if (!ReadAt(pool + 0xE5040 + (uintptr_t)slot * 4, &type, 4) || type != g_boltType || g_nbolt >= 16) continue;
        Bolt* b = &g_bolt[g_nbolt++];
        memset(b, 0, sizeof *b);
        b->slot = slot; b->target = g_wait[0].target; b->at = g_now; b->retry = g_wait[0].retry;
        memcpy(b->dir, g_wait[0].dir, 12);
        memmove(g_wait, g_wait + 1, sizeof g_wait[0] * (size_t)--g_nwait);
    }
    for (int i = 0; i < g_nwait;) { if (g_now - g_wait[i].at > 2) g_wait[i] = g_wait[--g_nwait]; else i++; }
    for (int i = 0; i < g_nbolt;)
    {
        Bolt* b = &g_bolt[i];
        uint16_t flags = 0;
        uint32_t type = 0;
        float f[3];
        int flying = g_now - b->at <= 6 && ReadAt(pool + 0xE5040 + (uintptr_t)b->slot * 4, &type, 4) && type == g_boltType &&
                     ReadAt(pool + 0x203C + (uintptr_t)b->slot * 2, &flags, 2) && (flags & 2) && !(flags & 1) && ReadAt(pool + 0x3040 + (uintptr_t)b->slot * 0x70, f, 12);
        if (flying) { memcpy(b->last, f, 12); i++; continue; }
        float to[3];
        if (!b->retry && Aim(b->target, to) && IsFoe(b->target) && Dist2(b->last, to) > 9.0f) Refire(b->target, b->dir);
        *b = g_bolt[--g_nbolt];
    }
}

/* ---- the stacks' frame ------------------------------------------------------------------------------------------ */
static void StacksFrame(void)
{
    for (int i = 0; i < BURNS; i++)
    {
        Burn* b = &g_burn[i];
        if (!b->e) continue;
        if (!IsFoe(b->e)) { memset(b, 0, sizeof *b); continue; }
        if (b->burnUntil <= g_now) { b->stacks = 0; b->litAt = 0; continue; }   /* it stopped burning: back to 0 */
        if (!b->litAt) continue;
        while (g_now >= b->nextStack) { b->stacks++; b->nextStack += 1.0; }
        if (b->stacks > g_topStacks) g_topStacks = b->stacks;
        if (b->stacks >= s_hdStacks && Bolt_(b->e)) b->stacks = 0;
    }
}

/* ---- events ------------------------------------------------------------------------------------------------------- */
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* e = (const HfSpawnEvent*)info;
    if (!IsDog(e->resource) || !fp_in_mission()) return;
    g_dog = e->entity;
    int slot = 0;
    for (int i = 0; i < 4; i++) if (!g_dogs[i]) { slot = i; break; } else if (i == 3) slot = 0;
    g_dogs[slot] = e->entity;
    if (e->resource == HOTDOG_DOG) g_dogBody = e->entity;
    static unsigned told;
    if (told++ < 4) g_hf->log(g_mod, "Hot Dog: one spawned (entity %u, resource %016llX, authority %u)", e->entity, (unsigned long long)e->resource, e->authority);
}
static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfKillEvent* k = (const HfKillEvent*)info;
    if (k->victim == g_dog && g_dog) { g_dog = 0; g_hf->log(g_mod, "Hot Dog: it was destroyed"); }
    for (int i = 0; i < 4; i++) if (g_dogs[i] == k->victim) g_dogs[i] = 0;
    if (k->victim == g_dogBody) g_dogBody = 0;
    Burn* b = BurnOf(k->victim, 0);
    if (b) memset(b, 0, sizeof *b);
}
static void OnWeapon(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfWeaponEvent* e = (const HfWeaponEvent*)info;
    g_avatar = e->avatar;
}

/* ---- the bolt's rows ------------------------------------------------------------------------------------------- */
static void RowsReady(HfMod* mod, HfEvent event, void* user)
{
    if (!g_boltType || !g_boltDamage || !g_hf->projectile_row(g_boltType) || !g_hf->damage_row(g_boltDamage))
    {
        g_hf->log(g_mod, "Hot Dog: the plasma bolt's rows are not there: level 3 fires no bolts");
        g_boltType = 0;
        return;
    }
    HfField sd = HF_FIELD(DamageInfo, standard_damage), dd = HF_FIELD(DamageInfo, durable_damage);
    HfValue v = HF_I32(s_hdBoltDamage);
    int ok = g_hf->set_live(g_mod, g_boltDamage, &sd, &v, HF_LIVE_SHARED) != 0;
    ok &= g_hf->set_live(g_mod, g_boltDamage, &dd, &v, HF_LIVE_SHARED) != 0;
    for (int i = 0; i < 4; i++)
    {
        HfField f = HF_FIELD_AT(DamageInfo, armor_penetration, i);
        HfValue a = HF_U32((uint32_t)s_hdBoltAp);
        ok &= g_hf->set_live(g_mod, g_boltDamage, &f, &a, HF_LIVE_SHARED) != 0;
    }
    g_boltReady = ok;
    g_hf->log(g_mod, "Hot Dog plasma bolt: type %u (a copy of row %d), %d damage at AP %d %s", g_boltType, s_hdBoltRound, s_hdBoltDamage, s_hdBoltAp, ok ? "set" : "REFUSED in part");
}

/* ---- levels ------------------------------------------------------------------------------------------------------- */
static void CheckLevel(void)
{
    int level = Level();
    if (level == g_level) return;
    int up = level > g_level;
    g_level = level;
    g_hf->log(g_mod, "AX/FLAM-75 Hot Dog: level %d (%d kills this mission)", level, g_kills);
    if (up)
    {
        static const char* kLines[2] = {"Napalm issued. Your Hot Dog burns hotter, and its fire spares you.",
            "Plasma authorised. What your Hot Dog sets burning, it finishes."};
        fp_toast_line("AX/FLAM-75 Hot Dog", level, kLines[level >= 3]);
    }
}
void hd_kills(uint64_t resource, int added)
{
    if (!IsDog(resource) || added <= 0) return;
    g_kills += added;
    CheckLevel();
}
static void Report(void)
{
    g_hf->log(g_mod, "Hot Dog (level %d, %d kills): %u hits of its fire seen, %u raised (%lld damage -> %lld), %u set alight, %u fire hits on you cancelled; "
        "most stacks on one enemy %d, %u from your shots, %u plasma bolts (%u fired again after a miss)", Level(), g_kills, g_hits, g_doubled, g_dmgBefore, g_dmgAfter, g_lit,
        g_cancelled, g_topStacks, g_playerStacks, g_bolts, g_refired);
}
void hd_tick(double now, float dt)
{
    (void)dt;
    g_now = now;
    static double next;
    static unsigned lastHits;
    if (fp_in_mission() && now >= next) { next = now + 30; if (g_hits != lastHits) { lastHits = g_hits; Report(); } }
    if (!fp_in_mission() || Level() < 3) return;
    StacksFrame();
    if (g_boltType && g_game) BoltsFrame();
}
void hd_reset(void)
{
    if (g_hits || g_bolts) Report();
    memset(g_burn, 0, sizeof g_burn);
    g_nbolt = g_nwait = 0;
    g_pool = 0;
    g_dog = 0;
    memset(g_dogs, 0, sizeof g_dogs);
    g_dogBody = 0;
    g_kills = 0;
    g_hits = g_doubled = g_lit = g_cancelled = g_bolts = g_refired = g_playerStacks = 0;
    g_topStacks = 0;
    g_dmgBefore = g_dmgAfter = 0;
    g_level = Level();
}

int hd_init(const HfApi* hf, HfMod* mod)
{
    g_hf = hf;
    g_mod = mod;
    const char* build = hf->build_id ? hf->build_id() : NULL;
    size_t size = 0;
    uintptr_t game = hf->module_base("game.dll", &size);
    if (build && !strcmp(build, "25480438") && game && size == 74727424u) { g_game = game; g_exe = hf->module_base("helldivers2.exe", NULL); }
    else hf->log(mod, "Hot Dog: the game's projectile pool is read for build 25480438 only: a plasma bolt that misses is not fired again");
    HfDamage dmg = {sizeof dmg};
    dmg.base = HF_DAMAGE_PLAS1_SCORCHER_AND_1_OTHER;
    dmg.name = "yui/field_promotions/hot_dog_plasma";
    g_boltDamage = hf->add_damage(mod, &dmg);
    HfProjectile bolt = {sizeof bolt};
    bolt.base = (uint32_t)s_hdBoltRound;
    bolt.name = "yui/field_promotions/hot_dog_plasma_bolt";
    bolt.damage = g_boltDamage;
    g_boltType = g_boltDamage ? hf->add_projectile(mod, &bolt) : 0;
    if (!g_boltType) hf->log(mod, "Hot Dog: the plasma bolt was refused (%s): level 3 fires no bolts", hf->last_log ? hf->last_log(mod) : "");
    else hf->on(mod, HF_EVENT_PROJECTILES_READY, RowsReady, NULL);
    int ok = hf->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) & hf->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) &
             hf->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) & hf->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0);
    hf->log(mod, "AX/FLAM-75 Hot Dog: level 2 at %d kills, level 3 at %d; napalm x%.1f, ignite %s, fire immunity %s; plasma at %d stacks (%d damage, AP %d)", s_hdL2, s_hdL3,
        (double)s_hdMult, s_hdIgnite ? "on" : "off", s_hdImmune ? "on" : "off", s_hdStacks, s_hdBoltDamage, s_hdBoltAp);
    return ok;
}
