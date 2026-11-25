/* W (yui.w): W's kit from Arknights for the Helldiver. First build, not a release: King of Hearts only
 * (docs/w-design.md).
 *
 *   King of Hearts (Z): a GL-21 Grenade Launcher in her hands with koh_rounds rounds. Each round's blast does
 *   koh_damage times its damage and holds the enemies within koh_freeze_radius m of it in their pose for
 *   koh_freeze_seconds. When the rounds are spent, on Z again, or when she puts the launcher away, she gets the weapon
 *   she held back, refilled, and the cooldown starts. A support weapon she carries is dropped by the game as the
 *   launcher is taken, removed, and a new one of its kind is made for her when the skill ends (Yui: only which weapon
 *   it was is kept).
 *
 * The weapon swap is the Wield Probe's (probes/wield-probe, played 2026-11-25): the weapon's entity spawned, taken into
 * the support slot by the inventory's own pickup (game.dll+0x9AE030), selected by the Helldiver's own switch request
 * (0xA93430, on the object at the Avatar manager's +0x53DCD0 + 0x1238 x her index); back by the request for her earlier
 * selection, the item let go (0x9A8840) and removed.
 * The freeze is the Time Stop Probe's method 2 with its hold (branch timestop-probe): a stun every half second, the
 * enemy's animation state machine switched off (the HUD mod's Lua: hud-src/w_hud.lua reads w_state() and logs through
 * w_log()), and the enemy kept where it stood. Here it is per enemy, each with its own end.
 *
 * 0.5.2: a bug hole still was not marked (played; the dynamite does close one). A hole or spawner is now listed whatever
 * else it is (one the kit also counts an enemy was asked to be alive, and has no health), and a press that marks
 * nothing logs the nearest hole or spawner and how far it is from her aim line.
 * 0.5.1: D12 marks bug holes and the enemy's other spawners too (played: a bug hole has no health, so it was never in
 * the list): what has the game's bug hole or AI spawner record, taken twice as far from her aim line as other things
 * (they are wide). A hole that closes is logged with what closed it near, and is not marked again.
 * 0.5.0: D12 (C) and its key (B). A press marks what is nearest her aim line within d12_range m (an enemy before a
 * thing that only has health: a structure, a prop; d12_things 0 leaves those out), up to d12_marks marks (the oldest
 * goes); a mark follows what it is on, goes with it if it dies, and lasts d12_seconds. The fourth key (key_s4, B) blows
 * them one after another: the game's explosion d12_explosion (the TED-63 Dynamite's), its damage d12_damage times,
 * enemies within d12_freeze_radius m held d12_freeze s. The cooldown d12_cooldown runs from the detonation. The HUD
 * marks them (w_mines' fourth value 2).
 * 0.4.2: a mine whose explosion's package is still loading waits for it (up to 5 s) instead of going off silently (the
 * first mine of a mission did). Played: the "dead" event puts a death while held right (a Bile Titan's too).
 * 0.4.1: a mine is shown on the HUD (w_mines: where each lies and whether its fuse is lit; w_view: her head, for the HUD
 * to pick the camera by), since nothing was put where one lay (the contact mine entity is never spawned by a minefield:
 * its mines' blasts come from the deployer itself, so the game's mines are not entities; jib_marker is now 0 by default).
 * An enemy that died while held: the HUD is told (dead, below), so it can start the animation the death missed.
 * 0.4.0: Jack in the Box (X), with mines the kit keeps itself (the game's own mine is not found yet: a minefield's
 * deployer spawns nothing mine-like through the spawn event). jib_charges charges, one back every jib_charge_every s; a
 * press spends one and lays one mine under the living enemy nearest her aim line in a box jib_width m wide and
 * jib_length m long from her, or at a random spot in the box (at her height: the ground there is not looked for yet).
 * An enemy within jib_trigger m sets it off after jib_delay s: the game's explosion jib_explosion (the GL-21's), its
 * damage jib_damage times, and the hold for jib_freeze s within jib_freeze_radius m. At most jib_max out, the oldest
 * goes first; one lasts jib_seconds. Where a mine lies is shown by an entity spawned bare there (jib_marker, default 3: 1 a G/40-K
 * Melta Mine, 2 a TM-1 Lure Mine, 3 the enemy's contact mine, 0 none): a bare-spawned weapon lay inert, and whether
 * these do is part of the probing (a marker's own blast is logged). The mine census now lists every explosion for
 * three minutes after a minefield's deployer spawns, with what it came from.
 * 0.3.1: Artilleryman never went off (her Helldiver's network id was never learned, so no hit read as hers): it is now
 * asked of the game for a hit's owner. Its burst is a G-6 Frag's by default (Yui), at most one every 0.4 s and not
 * within 8 m of her. Ambush is Ascalon's cut of her visibility (ambush_visual_cut percent of the range enemies see her
 * at), with the hits that miss; the forgetting is a switch, off by default (ambush_forget).
 * 0.3.0: her three passives. Artilleryman: a shot of hers that lands on an enemy also sets off a small blast there
 * (splash_explosion; not while the launcher is out, nor within 5 m of her). Insult to Injury: an enemy her skills hold
 * takes insult_mult times her damage. Ambush, in two versions to play (ambush 1: on ambush_after s after she lands;
 * 2: on after that long without firing): ambush_miss of the hits on her miss, and enemies that do not sense her at the
 * moment forget her (Ascalon's forgetting, Lost Contact's records). And, for Jack in the Box: what an MD-6 Anti-Personnel
 * Minefield spawns and what its mines' blasts are is logged (call one in once): the game's own mine is to be found.
 * 0.2.1: an enemy a round kills dies as the game has it; the hold takes only those the blast left alive (Yui).
 * Solo or host. 0.1.2: in the character picker (the roster). 0.2.0: her page on the options screen (F9): the skill's
 * numbers and its key, through the kit base's settings and options. Research: HellForge's mappings and HD2Runtime by SkyeShade; entity, unit
 * and health reads from Helldiver Survival Rework by enochlophobianumerophobia (MIT), through the kit base. */
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <windows.h>
#include "hellforge_mod.h"
#include "hellforge_components.h"
#include "hellforge_content.h"
#include "hellforge_functions.h"
#include "hellforge_globals.h"

#define VERSION "0.5.2"
#define BUILD "25480438"

#define MARKS_MAX 12
#define KB_KEYS 4   /* King of Hearts, Jack in the Box, D12, its detonation */
#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"
#include "kitbase/kb_body.h"
#include "kitbase/kb_settings.h"
#include "kitbase/kb_options.h"
#include "kitbase/kb_roster.h"
KB_OPTIONS_EXPORTS(w)

#define GL21 HF_ENTITY_GL21_GRENADE_LAUNCHER
#define TARGETS 48

/* ---- settings ---------------------------------------------------------------------------------------------------- */
static int s_rounds = 40, s_rareMax = 4;
static float s_damage = 3.5f, s_cooldown = 16, s_freezeSeconds = 3, s_freezeRadius = 5;
static float s_insult = 1.18f, s_splashEvery = 0.4f, s_splashSafe = 8, s_ambushAfter = 10, s_ambushMiss = 0.6f, s_ambushRange = 80, s_visualCut = 60;
static int s_splash = 1, s_splashExplosion = HF_EXPLOSION_G6_FRAG_DETONATION, s_ambush = 1, s_ambushForget = 0;
static int s_jibCharges = 5, s_jibMax = 30, s_jibExplosion = HF_EXPLOSION_MD6_ANTIPERSONNEL_MINEFIELD_MINE, s_jibMarker = 0;
static float s_jibEvery = 8, s_jibWidth = 15, s_jibLength = 40, s_jibTrigger = 3, s_jibDelay = 1.5f, s_jibLife = 120, s_jibDamage = 2.8f, s_jibFreeze = 2.2f,
             s_jibFreezeRadius = 5;
static int s_d12Marks = 4, s_d12Explosion = HF_EXPLOSION_TED63_DYNAMITE_DETONATION, s_d12Things = 1;
static float s_d12Range = 80, s_d12Aim = 2.5f, s_d12Life = 90, s_d12Cooldown = 20, s_d12Damage = 3.1f, s_d12Freeze = 5, s_d12FreezeRadius = 6;
static int s_optionsKey = 0x78;        /* options_key: the virtual key that opens the options screen (120: F9; 0: none) */
static const KbSetting kSettings[] = {
    KB_I("koh_rounds", s_rounds, 1, 200), KB_F("koh_damage", s_damage, 0.1, 50), KB_F("koh_cooldown", s_cooldown, 0, 600),
    KB_F("koh_freeze_seconds", s_freezeSeconds, 0, 30), KB_F("koh_freeze_radius", s_freezeRadius, 0.5, 30), KB_I("rare_max", s_rareMax, 1, 64),
    KB_F("insult_mult", s_insult, 1, 5), KB_I("splash", s_splash, 0, 1), KB_I("splash_explosion", s_splashExplosion, 0, 2000), KB_F("splash_every", s_splashEvery, 0.02, 5),
    KB_F("splash_safe", s_splashSafe, 0, 30), KB_F("ambush_visual_cut", s_visualCut, 0, 95), KB_I("ambush_forget", s_ambushForget, 0, 1),
    KB_I("ambush", s_ambush, 0, 2), KB_F("ambush_after", s_ambushAfter, 0, 120), KB_F("ambush_miss", s_ambushMiss, 0, 1), KB_F("ambush_range", s_ambushRange, 5, 300),
    KB_I("jib_charges", s_jibCharges, 1, 20), KB_F("jib_charge_every", s_jibEvery, 0.5, 120), KB_I("jib_max", s_jibMax, 1, 64), KB_F("jib_width", s_jibWidth, 1, 60),
    KB_F("jib_length", s_jibLength, 2, 150), KB_F("jib_trigger", s_jibTrigger, 0.5, 15), KB_F("jib_delay", s_jibDelay, 0, 10), KB_F("jib_seconds", s_jibLife, 5, 600),
    KB_F("jib_damage", s_jibDamage, 0.1, 50), KB_F("jib_freeze", s_jibFreeze, 0, 30), KB_F("jib_freeze_radius", s_jibFreezeRadius, 0.5, 30),
    KB_I("jib_explosion", s_jibExplosion, 0, 2000), KB_I("jib_marker", s_jibMarker, 0, 3),
    KB_I("d12_marks", s_d12Marks, 1, MARKS_MAX), KB_I("d12_explosion", s_d12Explosion, 0, 2000), KB_I("d12_things", s_d12Things, 0, 1), KB_F("d12_range", s_d12Range, 5, 300),
    KB_F("d12_aim", s_d12Aim, 0.5, 10), KB_F("d12_seconds", s_d12Life, 5, 600), KB_F("d12_cooldown", s_d12Cooldown, 0, 600), KB_F("d12_damage", s_d12Damage, 0.1, 50),
    KB_F("d12_freeze", s_d12Freeze, 0, 30), KB_F("d12_freeze_radius", s_d12FreezeRadius, 0.5, 30),
    KB_I("options_key", s_optionsKey, 0, 255),
};
static const KbOption kOptions[] = {
    {"koh_rounds", NULL, &s_rounds}, {"koh_damage", &s_damage, NULL}, {"koh_cooldown", &s_cooldown, NULL}, {"koh_freeze_seconds", &s_freezeSeconds, NULL},
    {"koh_freeze_radius", &s_freezeRadius, NULL}, {"rare_max", NULL, &s_rareMax},
    {"insult_mult", &s_insult, NULL}, {"splash", NULL, &s_splash}, {"splash_explosion", NULL, &s_splashExplosion}, {"splash_every", &s_splashEvery, NULL},
    {"splash_safe", &s_splashSafe, NULL}, {"ambush_visual_cut", &s_visualCut, NULL}, {"ambush_forget", NULL, &s_ambushForget},
    {"jib_charges", NULL, &s_jibCharges}, {"jib_charge_every", &s_jibEvery, NULL}, {"jib_max", NULL, &s_jibMax}, {"jib_width", &s_jibWidth, NULL},
    {"jib_length", &s_jibLength, NULL}, {"jib_trigger", &s_jibTrigger, NULL}, {"jib_delay", &s_jibDelay, NULL}, {"jib_seconds", &s_jibLife, NULL},
    {"jib_damage", &s_jibDamage, NULL}, {"jib_freeze", &s_jibFreeze, NULL}, {"jib_freeze_radius", &s_jibFreezeRadius, NULL},
    {"jib_explosion", NULL, &s_jibExplosion}, {"jib_marker", NULL, &s_jibMarker},
    {"d12_marks", NULL, &s_d12Marks}, {"d12_explosion", NULL, &s_d12Explosion}, {"d12_things", NULL, &s_d12Things}, {"d12_range", &s_d12Range, NULL},
    {"d12_aim", &s_d12Aim, NULL}, {"d12_seconds", &s_d12Life, NULL}, {"d12_cooldown", &s_d12Cooldown, NULL}, {"d12_damage", &s_d12Damage, NULL},
    {"d12_freeze", &s_d12Freeze, NULL}, {"d12_freeze_radius", &s_d12FreezeRadius, NULL},
    {"ambush", NULL, &s_ambush}, {"ambush_after", &s_ambushAfter, NULL}, {"ambush_miss", &s_ambushMiss, NULL}, {"ambush_range", &s_ambushRange, NULL},
};
#define s_key s_keys[0]
#include "kit_options.h"   /* kOpRows: the options screen's rows (kitbase/kb_options.h) */
#include "kit_icon.h"      /* kOpIcon, KB_OP_ACCENT */

/* ---- what the HUD mod reads (layout version 1; the same struct in w_hud.lua) --------------------------------------- */
typedef struct WTarget { uint32_t entity, unit; uint64_t object; float pos[3]; uint32_t pad; } WTarget;
typedef struct WState
{
    uint32_t version, seq, active, skill, count, rare_max;   /* skill: its low byte 0 ready, 1 on, 2 cooling down, 3 busy; bit 8: Ambush is on; bits 9-12 the marks, bit 13 D12 cooling down; bits 16-23 the mine charges, 24-31 the mines out */
    uint32_t player_unit, in_mission;
    uint64_t player_object;
    float player_pos[3], value;                               /* value: the rounds left, or the seconds of cooldown */
    WTarget t[TARGETS];
    uint32_t dead_seq, dead[16];                              /* the held enemies that died held: a ring, dead_seq the count so far */
} WState;
static WState g_state = {.version = 1};
static unsigned g_lines;
HF_MOD_EXPORT WState* w_state(void) { return &g_state; }
HF_MOD_EXPORT int w_log(unsigned version, const char* text)
{
    if (version != 1 || !text) return 0;
    if (g_lines++ < 400) Log("[lua] %.400s", text);
    return 1;
}

/* ---- enemies: alive, their unit's object, whether they walk (the Time Stop Probe's reads) ---------------------------- */
static uintptr_t HealthRec(uint32_t entity)
{
    uintptr_t m = Pointer(game + 53634696u);
    uint8_t h[88];
    if (!entity || !m || !ReadAt(m + 4112, h, sizeof h)) return 0;
    uint32_t live = Word(h + 16), cap = Word(h + 40), empty = Word(h + 44), mult = Word(h + 48);
    uintptr_t buckets = (uintptr_t)Quad(h + 32), records = (uintptr_t)Quad(h + 72);
    if (!buckets || !records || !cap || cap > 65536 || (cap & (cap - 1)) || live > 4096) return 0;
    uint32_t start = entity * mult;
    for (uint32_t k = 0; k < cap; k++)
    {
        uint8_t b[8];
        if (!ReadAt(buckets + 8u * ((start + k) & (cap - 1)), b, 8)) return 0;
        uint32_t key = Word(b), index = Word(b + 4);
        if (key == empty) return 0;
        if (key != entity) continue;
        if (index >= live) return 0;
        return records + 440u * index;
    }
    return 0;
}
static int Alive(uint32_t entity)
{
    uintptr_t r = HealthRec(entity);
    uint8_t b[416];
    return r && ReadAt(r, b, sizeof b) && (int32_t)Word(b + 20) > 0 && Word(b + 412) == 0;
}
static uintptr_t UnitObject(uint32_t unit)
{
    if (!exe) return 0;
    uintptr_t registry = Pointer(exe + 0x1A100F0), generations, objects, object;
    uint32_t count, id, slot = unit & 0x3FFFFF;
    uint8_t generation;
    if (!registry || !ReadAt(registry + 0x98, &count, 4) || count > 0x400000 || slot >= count ||
        !(generations = Pointer(registry + 0xA0)) || !ReadAt(generations + slot, &generation, 1) || generation != (uint8_t)(unit >> 22) ||
        !(objects = Pointer(registry + 0x88)) || !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit)
        return 0;
    return object;
}
static int Walks(uint32_t e)   /* 0: no navigation record; 1: a record, inactive (a stunned enemy's); 2: active */
{
    uintptr_t m = Pointer(game + 0x3326418), recs;
    uint32_t index, active = 0;
    if (!m || !Lookup(m + 0x30, e, 1048576, &index) || index > 65535 || !(recs = Pointer(m + 0x50))) return 0;
    if (!ReadAt(recs + 0xA8u * index + 0x10, &active, 4)) return 0;
    return (active & 0xFF) != 0 ? 2 : 1;
}
static int Boundary(uintptr_t at)
{
    uint8_t b[2];
    return hf->read_code && !hf->read_code(at - 1, b, 2) && (b[0] == 0xCC || b[0] == 0xC3 || b[0] == 0x90) && b[1] != 0xCC;
}

/* ---- the freeze: each enemy with its own end ------------------------------------------------------------------- */
typedef void (*SetVecFn)(void*, uint32_t, const float*);
static SetVecFn g_setPosition, g_setVelocity, g_setDest;
typedef struct Frozen { uint32_t entity, unit; float pos[3]; double until; } Frozen;
static Frozen g_frozen[TARGETS];
static unsigned g_frozenTotal, g_pins;
static void Publish(void)
{
    uint8_t d[24];
    float me[3];
    uint32_t avatar = atomic_load(&g_avatar), n = 0;
    for (int k = 0; k < TARGETS; k++)
    {
        const Frozen* f = &g_frozen[k];
        if (!f->entity) continue;
        WTarget* t = &g_state.t[n++];
        t->entity = f->entity; t->unit = f->unit; t->object = UnitObject(f->unit); memcpy(t->pos, f->pos, 12);
    }
    if (Descriptor(avatar, d) && PlayerPose(me, NULL))
    {
        g_state.player_unit = Word(d + 12);
        g_state.player_object = UnitObject(g_state.player_unit);
        memcpy(g_state.player_pos, me, 12);
    }
    g_state.rare_max = (uint32_t)s_rareMax;
    g_state.count = n;
    g_state.active = n != 0;
    g_state.seq++;
}
static int FreezeAdd(uint32_t e, float seconds)
{
    uint8_t d[24];
    float p[3];
    int room = -1;
    for (int k = 0; k < TARGETS; k++)
    {
        if (g_frozen[k].entity == e) { if (g_now + seconds > g_frozen[k].until) g_frozen[k].until = g_now + seconds; return 0; }
        if (!g_frozen[k].entity && room < 0) room = k;
    }
    if (room < 0 || !Descriptor(e, d) || !Alive(e) || !UnitNode(Word(d + 12), 0, p)) return 0;
    g_frozen[room].entity = e; g_frozen[room].unit = Word(d + 12); memcpy(g_frozen[room].pos, p, 12); g_frozen[room].until = g_now + seconds;
    g_frozenTotal++;
    return 1;
}
static void FreezeClear(const char* why)
{
    int n = 0;
    for (int k = 0; k < TARGETS; k++) if (g_frozen[k].entity) { g_frozen[k].entity = 0; n++; }
    if (n) { Publish(); Log("the freeze let go of %d (%s)", n, why); }
}
static void FreezeFrame(void)
{
    static const float zero[3] = {0, 0, 0};
    static double stunAt, destAt;
    int changed = 0, any = 0;
    int stun = g_now >= stunAt, dest = g_setDest && g_now >= destAt, idle = g_setPosition && QueryIdle();
    for (int k = 0; k < TARGETS; k++)
    {
        Frozen* f = &g_frozen[k];
        uint8_t d[24];
        float p[3];
        if (!f->entity) continue;
        if (g_now < f->until && Descriptor(f->entity, d) && Word(d + 12) == f->unit && !Alive(f->entity))
        {
            g_state.dead[g_state.dead_seq % 16] = f->entity;
            g_state.dead_seq++;
        }
        if (g_now >= f->until || !Descriptor(f->entity, d) || Word(d + 12) != f->unit || !Alive(f->entity) || !UnitNode(f->unit, 0, p)) { f->entity = 0; changed = 1; continue; }
        any = 1;
        if (stun && hf->apply_status) hf->apply_status(self, f->entity, HF_STATUS_STUN_LARGE, 100.0f);
        /* held where it stood: only this machine's, and only one with a navigation record (the warp setter crashed on
         * a rider and on a dead enemy) */
        if (!idle || !(Word(d + 20) & 1)) continue;
        int walks = Walks(f->entity);
        if (!walks) continue;
        if (dest && walks == 2) g_setDest(NULL, f->entity, f->pos);
        g_setVelocity(NULL, f->entity, zero);
        if (Dist2(p, f->pos) > 0.03f * 0.03f) { g_setPosition(NULL, f->entity, f->pos); g_pins++; }
    }
    if (any && stun) stunAt = g_now + 0.5;
    if (any && dest) destAt = g_now + 1.0;
    if (changed) Publish();
}

typedef struct Boom { volatile uint32_t type, source; volatile float pos[3]; } Boom;
static Boom g_boom[32];
static atomic_uint g_boomHead;
/* ---- her rounds' blasts: seen as they are asked for, on whatever thread ------------------------------------------------ */
static atomic_uint g_gl;                    /* the launcher the skill made (0: none) */
typedef struct Blast { volatile float pos[3]; volatile uint64_t ms; volatile uint32_t kind; } Blast;   /* kind 0: a round's; 1: a mine's; 2: a mark's */
#define BLASTS 16
static Blast g_blast[BLASTS];
static atomic_uint g_blastHead;
static unsigned g_blastTail, g_blastSeen, g_scaled;
static int g_ambushOn;
static uint32_t g_rand = 0x2545F491u;
static unsigned g_missed, g_insulted, g_splashes, g_forgot;
static uint64_t g_dealtMs, g_splashMs;
static float g_splashAt[8][3];
static int g_nsplash;
enum { K_IDLE, K_PACKAGE, K_WAITGL, K_PUTAWAY, K_TAKE, K_ASK, K_HELD, K_LETGO, K_REMOVE, K_WAITS };
static int g_k;
static atomic_ullong g_skillMs;             /* the last moment the skill was on (her rounds still fly a little after) */
static void OnExplosion(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfExplosionEvent* x = (const HfExplosionEvent*)info;
    uint32_t gl = atomic_load(&g_gl);
    if (x->source && x->source != gl)
    {
        unsigned bh = atomic_load(&g_boomHead);
        Boom* bm = &g_boom[bh % 32];
        bm->type = x->type; bm->source = x->source; bm->pos[0] = x->position[0]; bm->pos[1] = x->position[1]; bm->pos[2] = x->position[2];
        atomic_store(&g_boomHead, bh + 1);
    }
    if (!gl || x->source != gl) return;
    unsigned h = atomic_load(&g_blastHead);
    Blast* b = &g_blast[h % BLASTS];
    b->pos[0] = x->position[0]; b->pos[1] = x->position[1]; b->pos[2] = x->position[2];
    b->kind = 0;
    b->ms = GetTickCount64();
    atomic_store(&g_blastHead, h + 1);
}
/* the damage of her rounds' blasts, multiplied: an explosion's or an impact's hit on something other than her, within
 * 12 m of one of her blasts and a quarter second of it */
static void OnDamagePre(HfMod* mod, HfDamagePreEvent* ev, void* user)
{
    HfDamageEvent* d = &ev->damage;
    uint32_t me = atomic_load(&g_avatar);
    if (ev->cancel || d->amount <= 0 || !d->target || !g_inMission || !kb_kit_on) return;
    if (d->target == me)
    {
        /* Ambush: a hit on her misses (not what burns, bleeds or drowns her) */
        if (g_ambushOn && s_ambushMiss > 0 && (d->kind == 0 || d->kind == 1 || d->kind == 3 || d->kind == 4 || d->kind == 5 || d->kind == 6))
        {
            g_rand = g_rand * 1664525u + 1013904223u;
            if ((float)(g_rand >> 8) / 16777216.0f < s_ambushMiss) { ev->cancel = 1; g_missed++; }
        }
        return;
    }
    uint64_t now = GetTickCount64();
    int hers = 0, blast = 0, mine = 0;
    if ((d->kind == 4 || d->kind == 5 || d->kind == 0) && now - atomic_load(&g_skillMs) <= 4000)
        for (int i = 0; i < BLASTS && !blast; i++)
        {
            const Blast* b = &g_blast[i];
            float p[3] = {b->pos[0], b->pos[1], b->pos[2]};
            blast = b->ms && now - b->ms <= 250 && Dist2(p, d->position) <= 12.0f * 12.0f;
            mine = blast ? (int)b->kind : 0;
        }
    double mult = 1;
    if (blast) { mult *= (double)(mine == 2 ? s_d12Damage : mine == 1 ? s_jibDamage : s_damage); g_scaled++; hers = 1; }
    else
    {
        /* her Helldiver's network id, asked of the game for this hit's owner (the spawn list never gave it) */
        if (!NetOk(g_avatarNet) && NetOk(d->owner) && hf->entity_of_network && hf->entity_of_network(self, d->owner) == me)
        {
            g_avatarNet = d->owner;
            Log("her Helldiver: entity %u, network %u (from a hit's owner)", me, g_avatarNet);
        }
        hers = MineHit(d);
    }
    if (!hers) return;
    g_dealtMs = now;
    /* Insult to Injury: what her skills hold takes more from her */
    for (int k = 0; k < TARGETS && s_insult > 1; k++) if (g_frozen[k].entity == d->target) { mult *= (double)s_insult; g_insulted++; break; }
    if (mult != 1) { double a = (double)d->amount * mult; d->amount = a > 2147483647.0 ? 2147483647 : (int32_t)a; }
    /* Artilleryman: a shot that lands also bursts there (asked for from the frame) */
    if (!blast && s_splash && (d->kind == 0 || d->kind == 1 || d->kind == 3) && g_k != K_HELD && g_nsplash < 8 && now - g_splashMs >= (uint64_t)(s_splashEvery * 1000.0f))
    {
        g_splashMs = now;
        memcpy(g_splashAt[g_nsplash++], d->position, 12);
    }
}
static void BlastFrame(void)
{
    unsigned head = atomic_load(&g_blastHead);
    if (head - g_blastTail > BLASTS) g_blastTail = head - BLASTS;
    for (; g_blastTail != head; g_blastTail++)
    {
        const Blast* b = &g_blast[g_blastTail % BLASTS];
        float at[3] = {b->pos[0], b->pos[1], b->pos[2]};
        int caught = 0, fresh = 0;
        /* 0.2.1: a blast's enemies are held 0.15 s after it, once its damage has landed: only the ones it left alive
         * (0.2.0 held the ones it killed for a frame, their state machine off as they died: their corpses went wrong) */
        if (GetTickCount64() - b->ms < 150) break;
        g_blastSeen++;
        float radius = b->kind == 2 ? s_d12FreezeRadius : b->kind == 1 ? s_jibFreezeRadius : s_freezeRadius, seconds = b->kind == 2 ? s_d12Freeze : b->kind == 1 ? s_jibFreeze : s_freezeSeconds;
        for (int i = 0; i < FOES && seconds > 0; i++)
        {
            uint32_t e = g_foe[i];
            uint8_t d[24];
            float p[3];
            if (!e || !Descriptor(e, d) || !UnitNode(Word(d + 12), 0, p)) continue;
            /* a big enemy's root may be far from where the round struck: its height is not counted against it */
            float dx = p[0] - at[0], dy = p[1] - at[1], dz = p[2] - at[2], flat = dx * dx + dy * dy;
            if (flat > radius * radius || fabsf(dz) > 25) continue;
            if (!Alive(e)) continue;
            caught++;
            fresh += FreezeAdd(e, seconds);
        }
        if (fresh) Publish();
        if (g_blastSeen <= 12) Log("a %s blast at %.1f %.1f %.1f: %d enemies within %.1f m held (%d newly)", b->kind == 2 ? "mark's" : b->kind == 1 ? "mine's" : "round's", (double)at[0], (double)at[1],
            (double)at[2], caught, (double)radius, fresh);
    }
}

/* ---- King of Hearts: the launcher in her hands, and her weapon back ------------------------------------------------ */
typedef void (*TakeFn)(void* inventory, uint32_t entity, uint32_t item, uint32_t slot, uint8_t flag);
typedef void (*DropFn)(void* inventory, uint32_t entity, uint32_t item, uint8_t flag, uint64_t zero, uint8_t one);
typedef void (*RequestFn)(void* object);
static TakeFn g_take;
static DropFn g_drop;
static RequestFn g_request;
static uint8_t g_context[0x8A0];
static uint64_t g_package;
static int g_pkg;
static uintptr_t InventoryRecord(uint32_t e)
{
    uintptr_t m = (uintptr_t)hf_global_InventoryManager(self), records;
    uint32_t i;
    if (!m || !Lookup(m + 0x28, e, 1048576, &i) || i >= 65536 || !(records = Pointer(m + 0x50))) return 0;
    return records + 0x30ull * i;
}
static uint64_t ResOf(uint32_t e) { uint8_t d[24]; return Descriptor(e, d) ? Quad(d) : 0; }
static uintptr_t SwitchObject(uint32_t e)
{
    uintptr_t am = (uintptr_t)hf_global_AvatarManager(self), obj;
    uint32_t i, who = 0;
    if (!am || !Lookup(am + 0xF8, e, 1048576, &i) || i >= 1024) return 0;
    obj = am + 0x53DCD0 + (uintptr_t)0x1238 * i;
    return ReadAt(obj, &who, 4) && who == e ? obj : 0;
}
static int Ask(uint32_t selection, const char* why)
{
    uintptr_t obj = SwitchObject(atomic_load(&g_avatar));
    if (!obj || !g_request || hf->write(obj + 4, &selection, 4)) { Log("King of Hearts: the switch request could not be made (%s)", why); return 0; }
    g_request((void*)obj);
    return 1;
}
static uint32_t SpawnAt(uint64_t res, const float at[3], const char* what);
static uint32_t SpawnRes(uint64_t res, const char* what)
{
    float p[3];
    if (!PlayerPose(p, NULL)) { Log("King of Hearts: %s could not be asked for", what); return 0; }
    p[2] += 1.0f;
    return SpawnAt(res, p, what);
}
static uint32_t SpawnAt(uint64_t res, const float at[3], const char* what)
{
    HfFn_RequestEntitySpawn spawn = hf_fn_RequestEntitySpawn(self);
    float p[3] = {at[0], at[1], at[2] - 1.0f};
    uint32_t out = 0;
    if (!spawn) { Log("King of Hearts: %s could not be asked for", what); return 0; }
    struct { uint32_t one, network; float pose[16]; void* context; uint64_t pad; } info;
    _Static_assert(sizeof info == 0x58, "the spawn record is 0x58 bytes");
    memset(&info, 0, sizeof info);
    memset(g_context, 0, sizeof g_context);
    info.one = 1;
    info.network = 0x7FFF;
    info.pose[0] = info.pose[5] = info.pose[10] = info.pose[15] = 1.0f;
    info.pose[12] = p[0]; info.pose[13] = p[1]; info.pose[14] = p[2] + 1.0f;
    info.context = g_context;
    spawn(&out, res, &info);
    if (!out || out == UINT32_MAX) { Log("King of Hearts: %s: no entity came back", what); return 0; }
    return out;
}
static int Rounds(uint32_t weapon)
{
    HfFn_MagazineRounds rounds = hf_fn_MagazineRounds(self);
    void* wm = hf_global_WeaponManager(self);
    return rounds && wm && weapon ? rounds(wm, weapon) : -1;
}
static void FillUp(uint32_t weapon)
{
    HfFn_MagazineCapacity capacity = hf_fn_MagazineCapacity(self);
    HfFn_AddMagazineRounds add = hf_fn_AddMagazineRounds(self);
    HfFn_WeaponRefill refill = hf_fn_WeaponRefill(self);
    uint32_t stride = 0;
    uint64_t res = ResOf(weapon);
    if (!weapon || !res) return;
    if (hf->component_record("WeaponMagazineComponentData", res, &stride) && capacity && add)
    {
        int have = Rounds(weapon), cap = capacity(NULL, weapon);
        if (have >= 0 && cap > have) add(NULL, weapon, cap - have);
    }
    if (refill) for (int i = 0; i < 6; i++) refill(0, weapon, 1.0f);
}
static double g_kAt, g_madeAt, g_readyAt, g_awaySince;
static uint32_t g_kSel, g_kOld, g_kNew;     /* her selection at the start; the support weapon she had; its replacement */
static uint64_t g_kOldRes;
static unsigned g_uses;
static void Start(void)
{
    uint32_t me = atomic_load(&g_avatar), slots[8] = {0}, stride = 0;
    uintptr_t rec;
    if (!g_take || !g_drop || !g_request) { Log("King of Hearts: off (the game's code is not the reviewed one)"); return; }
    if (!(rec = InventoryRecord(me)) || !ReadAt(rec, slots, sizeof slots) || !SwitchObject(me)) { Log("King of Hearts: her inventory or her switch object was not found: nothing done"); return; }
    g_kSel = slots[7] >= 1 && slots[7] <= 3 ? slots[7] : 1;
    g_kOld = slots[2];
    g_kOldRes = ResOf(g_kOld);
    g_kNew = 0;
    if (!g_package)
    {
        uintptr_t r = hf->component_record("LoadoutPackageComponentData", GL21, &stride);
        if (!r || !ReadAt(r + 8, &g_package, 8) || !g_package) g_package = ~0ull;
    }
    g_uses++;
    Log("King of Hearts %u: her selection %u, her support weapon %u (%016llx)", g_uses, g_kSel, g_kOld, (unsigned long long)g_kOldRes);
    g_k = K_PACKAGE;
    g_kAt = g_now + 8;
}
static void End(const char* why)
{
    if (g_k != K_HELD) return;
    Log("King of Hearts %u over (%s): %d rounds left; %u blasts seen, %u hits multiplied, %u enemies held so far", g_uses, why, Rounds(atomic_load(&g_gl)), g_blastSeen,
        g_scaled, g_frozenTotal);
    Ask(g_kSel == 3 ? 1 : g_kSel, "her own weapon back in hand");
    g_k = K_LETGO;
    g_kAt = g_now + 1.0;
    g_readyAt = g_now + s_cooldown;
}
/* she died or the mission ended with the skill running: nothing of hers is left to put back */
static void Abort(const char* why)
{
    HfFn_RemoveEntity remove = hf_fn_RemoveEntity(self);
    void* em = hf_global_EntityManager(self);
    uint32_t gl = atomic_load(&g_gl);
    if (g_k == K_IDLE) return;
    Log("King of Hearts %u cut short (%s)", g_uses, why);
    if (g_inMission && gl && ResOf(gl) == GL21 && remove && em) remove(em, gl);
    atomic_store(&g_gl, 0);
    g_k = K_IDLE;
    g_readyAt = g_now + s_cooldown;
}
static void SkillFrame(void)
{
    void* im = hf_global_InventoryManager(self);
    void* em = hf_global_EntityManager(self);
    HfFn_RemoveEntity remove = hf_fn_RemoveEntity(self);
    uint32_t me = atomic_load(&g_avatar), gl = atomic_load(&g_gl), now = 0;
    uintptr_t rec;
    if (g_k == K_IDLE) return;
    if (!g_alive || !im || !ResOf(me) || !(rec = InventoryRecord(me))) { Abort("she is down or gone"); return; }
    if (g_k >= K_ASK && g_k <= K_HELD) atomic_store(&g_skillMs, GetTickCount64());
    switch (g_k)
    {
    case K_PACKAGE:
        if (g_package != ~0ull && g_pkg != HF_PACKAGE_RESIDENT)
        {
            g_pkg = hf->load_package(self, g_package);
            if (g_pkg == HF_PACKAGE_LOADING) { if (g_now > g_kAt) Abort("the launcher's package did not load"); break; }
            if (!g_pkg) g_package = ~0ull;
        }
        gl = SpawnRes(GL21, "the launcher");
        if (!gl) { Abort("no launcher"); break; }
        atomic_store(&g_gl, gl);
        g_madeAt = g_now;
        g_k = K_WAITGL;
        break;
    case K_WAITGL:
        /* its spawn has to finish before it is taken (taken in its spawn frame it stayed where it was made) */
        if (g_now < g_madeAt + 0.75) break;
        if (ResOf(gl) != GL21) { Abort("the launcher does not read as one"); break; }
        if (g_kSel == 3 && g_kOld) { Ask(1, "her support weapon is in her hands: put away first"); g_k = K_PUTAWAY; g_kAt = g_now + 0.8; }
        else g_k = K_TAKE;
        break;
    case K_PUTAWAY:
        if (g_now >= g_kAt) g_k = K_TAKE;
        break;
    case K_TAKE:
        g_take(im, me, gl, 3, 0);
        g_k = K_ASK; g_kAt = g_now + 0.1;
        break;
    case K_ASK:
    {
        HfFn_AddMagazineRounds add = hf_fn_AddMagazineRounds(self);
        if (g_now < g_kAt) break;
        if (g_kOld && ResOf(g_kOld) == g_kOldRes && remove && em) remove(em, g_kOld);   /* the game dropped it as the launcher was taken */
        Ask(3, "the launcher in hand");
        int have = Rounds(gl);
        if (add && have >= 0 && have < s_rounds) add(NULL, gl, s_rounds - have);
        g_k = K_HELD;
        g_awaySince = 0;
        Log("King of Hearts %u on: launcher %u, %d rounds", g_uses, gl, Rounds(gl));
        break;
    }
    case K_HELD:
    {
        uint32_t sel = 0;
        int left = Rounds(gl);
        if (ResOf(gl) != GL21) { Abort("the launcher is gone"); break; }
        if (left == 0) { End("the rounds are spent"); break; }
        /* she put it away herself: a second of grace for the draw at the start */
        ReadAt(rec + 0x1C, &sel, 4);
        if (sel == 3) g_awaySince = 0;
        else if (!g_awaySince) g_awaySince = g_now;
        else if (g_now - g_awaySince > 1.5) { g_kSel = sel >= 1 && sel <= 2 ? sel : g_kSel; End("she put the launcher away"); }
        break;
    }
    case K_LETGO:
        if (g_now < g_kAt) break;
        g_drop(im, me, gl, 0, 0, 1);
        if (ReadAt(rec + 8, &now, 4) && now == gl) { now = 0; hf->write(rec + 8, &now, 4); }
        g_k = K_REMOVE; g_kAt = g_now + 0.4;
        break;
    case K_REMOVE:
        if (g_now < g_kAt) break;
        if (gl && ResOf(gl) == GL21 && remove && em) remove(em, gl);
        atomic_store(&g_gl, 0);
        if (g_kOldRes) { g_kNew = SpawnRes(g_kOldRes, "a new support weapon of her kind"); g_k = g_kNew ? K_WAITS : K_IDLE; g_kAt = g_now + 0.75; }
        else { FillUp(g_weapon[g_kSel]); g_k = K_IDLE; }
        break;
    case K_WAITS:
        if (g_now < g_kAt) break;
        if (ResOf(g_kNew) == g_kOldRes)
        {
            g_take(im, me, g_kNew, 3, 0);
            FillUp(g_kNew);
            if (g_kSel == 3) Ask(3, "she had it in hand when the skill began");
            else FillUp(g_weapon[g_kSel]);
            Log("King of Hearts %u: her new support weapon %u taken", g_uses, g_kNew);
        }
        else Log("King of Hearts %u: the new support weapon does not read as hers: left", g_uses);
        g_k = K_IDLE;
        break;
    default: g_k = K_IDLE;
    }
}

/* ---- D12: marks, and the key that blows them -------------------------------------------------------------------- */
#define MARKS 12
#define THINGS 256
typedef struct MarkT { int on; uint32_t entity; uint64_t res; int foe; float pos[3]; double born, boomAt; } MarkT;
static MarkT g_mark[MARKS];
static uint32_t g_thing[THINGS];            /* what is not an enemy but has health: a structure, a prop (the spawn event's) */
static uint64_t g_thingRes[THINGS];
static uint8_t g_thingWide[THINGS];         /* 1: a bug hole or a spawner */
static atomic_uint g_thingHead;
static double g_d12Ready;
static unsigned g_marksMade, g_marksBlown;
static int MarksOn(void) { int n = 0; for (int i = 0; i < MARKS; i++) n += g_mark[i].on; return n; }
static void MarksClear(const char* why)
{
    int n = MarksOn();
    memset(g_mark, 0, sizeof g_mark);
    if (n) Log("D12: %d marks gone (%s)", n, why);
}
/* where a marked thing is now (its last place if it cannot be read); 0: it is gone or dead */
static int MarkWhere(MarkT* m)
{
    uint8_t d[24];
    float p[3];
    if (!Descriptor(m->entity, d) || Quad(d) != m->res) return 0;
    if (m->foe && !Alive(m->entity)) return 0;
    if (UnitNode(Word(d + 12), 0, p)) { m->pos[0] = p[0]; m->pos[1] = p[1]; m->pos[2] = p[2]; }
    return 1;
}
static void D12Consider(uint32_t e, uint64_t res, int foe, int wide, const float eye[3], const float aim[3], float* best, MarkT* pick)
{
    uint8_t d[24];
    float p[3];
    if (!e || !Descriptor(e, d) || Quad(d) != res || !UnitNode(Word(d + 12), 0, p)) return;
    if (foe && !Alive(e)) return;
    for (int k = 0; k < MARKS; k++) if (g_mark[k].on && g_mark[k].entity == e) return;
    /* its distance from her aim line, its middle taken a metre over its root */
    float v[3] = {p[0] - eye[0], p[1] - eye[1], p[2] + 1.0f - eye[2]};
    float along = v[0] * aim[0] + v[1] * aim[1] + v[2] * aim[2];
    if (along < 1.5f || along > s_d12Range) return;
    float w[3] = {v[0] - aim[0] * along, v[1] - aim[1] * along, v[2] - aim[2] * along};
    float side = sqrtf(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    if (side > s_d12Aim * (wide ? 2.5f : 1) + along * 0.03f) return;
    /* an enemy before a thing; then the nearest to the line */
    float score = (wide ? side * 0.4f : side) + (foe ? 0 : 2.0f);
    if (score >= *best) return;
    *best = score;
    pick->on = 1; pick->entity = e; pick->res = res; pick->foe = foe; pick->pos[0] = p[0]; pick->pos[1] = p[1]; pick->pos[2] = p[2];
}
static void D12Mark(void)
{
    float me[3], aim[3], best = 1e9f;
    MarkT pick = {0};
    if (g_now < g_d12Ready) return;
    for (int i = 0; i < MARKS; i++) if (g_mark[i].on && g_mark[i].boomAt) return;   /* they are blowing */
    if (!PlayerPose(me, aim)) return;
    float eye[3] = {me[0], me[1], me[2] + 1.6f};
    for (int i = 0; i < FOES; i++) D12Consider(g_foe[i], g_foeRes[i], 1, 0, eye, aim, &best, &pick);
    if (s_d12Things) for (int i = 0; i < THINGS; i++) D12Consider(g_thing[i], g_thingRes[i], 0, g_thingWide[i], eye, aim, &best, &pick);
    if (!pick.on)
    {
        static unsigned told;
        if (told++ < 12)
        {
            int wide = 0, read = 0;
            float nearest = 1e9f, nAlong = 0, nSide = 0;
            uint32_t ne = 0;
            for (int i = 0; i < THINGS; i++)
            {
                uint8_t d[24];
                float p[3];
                if (!g_thing[i] || !g_thingWide[i]) continue;
                wide++;
                if (!Descriptor(g_thing[i], d) || Quad(d) != g_thingRes[i] || !UnitNode(Word(d + 12), 0, p)) continue;
                read++;
                float v[3] = {p[0] - eye[0], p[1] - eye[1], p[2] + 1.0f - eye[2]}, dist = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                if (dist >= nearest) continue;
                float along = v[0] * aim[0] + v[1] * aim[1] + v[2] * aim[2];
                float w[3] = {v[0] - aim[0] * along, v[1] - aim[1] * along, v[2] - aim[2] * along};
                nearest = dist; nAlong = along; nSide = sqrtf(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]); ne = g_thing[i];
            }
            Log("D12: nothing on her aim line within %.0f m to mark; %d holes or spawners listed, %d of them readable; the nearest (entity %u) %.0f m from her, %.1f m along her aim, %.1f m off it",
                (double)s_d12Range, wide, read, ne, (double)(ne ? nearest : 0), (double)nAlong, (double)nSide);
        }
        return;
    }
    int slot = -1, oldest = -1, n = 0;
    for (int i = 0; i < MARKS; i++)
    {
        if (!g_mark[i].on) { if (slot < 0) slot = i; continue; }
        n++;
        if (oldest < 0 || g_mark[i].born < g_mark[oldest].born) oldest = i;
    }
    if (n >= s_d12Marks || slot < 0) slot = oldest;
    if (slot < 0) return;
    pick.born = g_now;
    g_mark[slot] = pick;
    g_marksMade++;
    if (g_marksMade <= 30)
        Log("D12: mark %u on %s %u (%016llx) %.0f m from her; %d marked", g_marksMade, pick.foe ? "enemy" : "thing", pick.entity, (unsigned long long)pick.res,
            (double)sqrtf(Dist2(pick.pos, me)), MarksOn());
}
static void D12Detonate(void)
{
    int n = 0;
    for (int i = 0; i < MARKS; i++)
    {
        if (!g_mark[i].on || g_mark[i].boomAt) continue;
        g_mark[i].boomAt = g_now + 0.15 * n++;
    }
    if (!n) return;
    g_d12Ready = g_now + s_d12Cooldown;
    Log("D12: %d marks set off; the cooldown %.0f s from now", n, (double)s_d12Cooldown);
}
static void MineBlast(const float at[3], uint32_t kind);
static void MarkFrame(void)
{
    for (int i = 0; i < MARKS; i++)
    {
        MarkT* m = &g_mark[i];
        if (!m->on) continue;
        if (!MarkWhere(m) && !m->boomAt) { m->on = 0; continue; }     /* dead or gone before the key: its mark with it */
        if (!m->boomAt) { if (g_now - m->born > s_d12Life) m->on = 0; continue; }
        if (g_now < m->boomAt) continue;
        float at[3] = {m->pos[0], m->pos[1], m->pos[2] + (m->foe ? 0.8f : 0.5f)};
        int r = hf->spawn_explosion ? hf->spawn_explosion(self, (uint32_t)s_d12Explosion, at) : -1;
        if (r == HF_ACT_WAITING && g_now < m->boomAt + 5) continue;
        MineBlast(at, 2);
        g_marksBlown++;
        if (g_marksBlown <= 30) Log("D12: a mark blew at %.1f %.1f %.1f on %s %u (explosion %d: %s)", (double)at[0], (double)at[1], (double)at[2], m->foe ? "enemy" : "thing", m->entity,
            s_d12Explosion, r == HF_ACT_DONE || r == HF_ACT_REQUESTED || r == HF_ACT_QUEUED ? "asked" : r == HF_ACT_WAITING ? "its package never came" : "refused");
        m->on = 0;
    }
}

/* ---- Jack in the Box: mines the kit keeps itself ---------------------------------------------------------------- */
#define MINES 64
typedef struct MineT { int on; float pos[3]; double born, boomAt; uint32_t marker; } MineT;
static MineT g_mine[MINES];
static int g_charges;
static double g_chargeAt;
static uint64_t g_markerPackage;
static int g_markerPkg, g_markerSeen, g_markerTold;
static unsigned g_minesLaid, g_minesBlown;
static uint64_t MarkerRes(void)
{
    return s_jibMarker == 1 ? HF_ENTITY_G40K_MELTA_MINE : s_jibMarker == 2 ? HF_ENTITY_TM1_LURE_MINE : s_jibMarker == 3 ? HF_ENTITY_CONTACT_MINE : 0;
}
static float Rand01(void) { g_rand = g_rand * 1664525u + 1013904223u; return (float)(g_rand >> 8) / 16777216.0f; }
static int MinesOut(void) { int n = 0; for (int i = 0; i < MINES; i++) n += g_mine[i].on; return n; }
static void MineOff(MineT* m, int touch)
{
    HfFn_RemoveEntity remove = hf_fn_RemoveEntity(self);
    void* em = hf_global_EntityManager(self);
    if (touch && m->marker && remove && em && ResOf(m->marker) == MarkerRes()) remove(em, m->marker);
    memset(m, 0, sizeof *m);
}
static void MinesClear(int touch, const char* why)
{
    int n = MinesOut();
    for (int i = 0; i < MINES; i++) if (g_mine[i].on) MineOff(&g_mine[i], touch);
    if (n) Log("Jack in the Box: %d mines gone (%s)", n, why);
    g_markerSeen = 0; g_markerPkg = 0; g_markerPackage = 0;
}
/* a mine's blast, the kit's own or its marker's: the damage multiplied near it for a moment, and the hold */
static void MineBlast(const float at[3], uint32_t kind)
{
    unsigned h = atomic_load(&g_blastHead);
    Blast* b = &g_blast[h % BLASTS];
    b->pos[0] = at[0]; b->pos[1] = at[1]; b->pos[2] = at[2];
    b->kind = kind;
    b->ms = GetTickCount64();
    atomic_store(&g_blastHead, h + 1);
    atomic_store(&g_skillMs, GetTickCount64());
    if (kind == 1) g_minesBlown++;
}
static void JibPlace(void)
{
    float me[3], aim[3], at[3], best = 1e9f;
    int found = 0;
    if (!PlayerPose(me, aim)) return;
    float n = sqrtf(aim[0] * aim[0] + aim[1] * aim[1]);
    if (n < 0.05f) { Log("Jack in the Box: she aims straight up or down: nothing laid"); return; }
    float fx = aim[0] / n, fy = aim[1] / n;
    for (int i = 0; i < FOES; i++)
    {
        uint32_t e = g_foe[i];
        uint8_t d[24];
        float p[3];
        if (!e || !Descriptor(e, d) || Quad(d) != g_foeRes[i] || !UnitNode(Word(d + 12), 0, p) || !Alive(e)) continue;
        float dx = p[0] - me[0], dy = p[1] - me[1], along = dx * fx + dy * fy, side = fabsf(dx * fy - dy * fx);
        if (along < 0 || along > s_jibLength || side > s_jibWidth / 2 || fabsf(p[2] - me[2]) > 20) continue;
        /* one mine is not laid on another */
        int taken = 0;
        for (int k = 0; k < MINES && !taken; k++) taken = g_mine[k].on && Dist2(g_mine[k].pos, p) < 1.0f;
        if (taken) continue;
        if (side < best) { best = side; at[0] = p[0]; at[1] = p[1]; at[2] = p[2]; found = 1; }
    }
    if (!found)
    {
        float along = 2 + Rand01() * (s_jibLength - 2), side = (Rand01() - 0.5f) * s_jibWidth;
        at[0] = me[0] + fx * along + fy * side; at[1] = me[1] + fy * along - fx * side; at[2] = me[2];
    }
    MineT* m = NULL;
    int out = 0, oldest = -1;
    for (int i = 0; i < MINES; i++)
    {
        if (!g_mine[i].on) { if (!m) m = &g_mine[i]; continue; }
        out++;
        if (oldest < 0 || g_mine[i].born < g_mine[oldest].born) oldest = i;
    }
    if ((out >= s_jibMax || !m) && oldest >= 0) { MineOff(&g_mine[oldest], 1); m = &g_mine[oldest]; out--; }
    if (!m) return;
    m->on = 1; m->born = g_now; m->boomAt = 0; m->marker = 0;
    m->pos[0] = at[0]; m->pos[1] = at[1]; m->pos[2] = at[2];
    if (g_charges == s_jibCharges) g_chargeAt = g_now;
    g_charges--;
    g_minesLaid++;
    /* its marker: only once the marker's resource is known to be loaded (its package resident, or one seen spawned) */
    uint64_t res = MarkerRes();
    if (res && (g_markerPkg == HF_PACKAGE_RESIDENT || g_markerSeen))
    {
        float up[3] = {at[0], at[1], at[2] + 0.15f};
        m->marker = SpawnAt(res, up, "a mine's marker");
    }
    if (g_minesLaid <= 30)
        Log("Jack in the Box: mine %u %s at %.1f %.1f %.1f, %.0f m from her (marker %u); %d charges left, %d out", g_minesLaid, found ? "under an enemy" : "at a random spot", (double)at[0],
            (double)at[1], (double)at[2], (double)sqrtf(Dist2(at, me)), m->marker, g_charges, out + 1);
}
HF_MOD_EXPORT int w_mines(unsigned version, float* out, int max)
{
    int n = 0;
    if (version != 1 || !out || !g_inMission) return 0;
    for (int i = 0; i < MINES && n < max; i++)
    {
        const MineT* m = &g_mine[i];
        if (!m->on) continue;
        out[n * 4] = m->pos[0]; out[n * 4 + 1] = m->pos[1]; out[n * 4 + 2] = m->pos[2]; out[n * 4 + 3] = m->boomAt ? 1.0f : 0.0f;
        n++;
    }
    for (int i = 0; i < MARKS && n < max; i++)
    {
        const MarkT* m = &g_mark[i];
        if (!m->on) continue;
        out[n * 4] = m->pos[0]; out[n * 4 + 1] = m->pos[1]; out[n * 4 + 2] = m->pos[2] + (m->foe ? 1.2f : 0.5f); out[n * 4 + 3] = 2.0f;
        n++;
    }
    return n;
}
HF_MOD_EXPORT int w_view(unsigned version, float* out)
{
    uint8_t id[24];
    uint32_t avatar = atomic_load(&g_avatar);
    if (version != 1 || !out || !g_inMission || !Descriptor(avatar, id) || Quad(id) != AVATAR) return 0;
    return UnitNode(Word(id + 12), NODE_HEAD, out) ? 1 : 0;
}
static void MineFrame(void)
{
    /* the charges */
    if (g_charges >= s_jibCharges) { g_charges = s_jibCharges; g_chargeAt = g_now; }
    else if (g_now - g_chargeAt >= s_jibEvery) { g_charges++; g_chargeAt = g_now; }
    /* the marker's package */
    uint64_t res = MarkerRes();
    if (res && !g_markerPackage)
    {
        uint32_t stride = 0;
        uintptr_t r = hf->component_record("LoadoutPackageComponentData", res, &stride);
        if (!r || !ReadAt(r + 8, &g_markerPackage, 8) || !g_markerPackage) g_markerPackage = ~0ull;
        if (!g_markerTold++) Log("Jack in the Box: the marker %016llx has %s", (unsigned long long)res, g_markerPackage == ~0ull ? "no package of its own: one is spawned only once the mission has spawned one itself" : "a package: asked for");
    }
    if (res && g_markerPackage && g_markerPackage != ~0ull && g_markerPkg != HF_PACKAGE_RESIDENT)
    {
        static double again;
        if (g_now >= again) { g_markerPkg = hf->load_package(self, g_markerPackage); again = g_now + 0.5; if (!g_markerPkg) g_markerPackage = ~0ull; }
    }
    for (int i = 0; i < MINES; i++)
    {
        MineT* m = &g_mine[i];
        if (!m->on) continue;
        /* its marker went by itself (it is a live mine of the game's): its blast is the mine's */
        if (m->marker && g_now - m->born > 1.0 && ResOf(m->marker) != res)
        {
            static unsigned told;
            if (told++ < 10) Log("Jack in the Box: a mine's marker %u is gone by itself %.1f s after it was laid: counted as the mine's blast", m->marker, g_now - m->born);
            MineBlast(m->pos, 1);
            m->marker = 0;
            MineOff(m, 0);
            continue;
        }
        if (g_now - m->born > s_jibLife) { MineOff(m, 1); continue; }
        if (!m->boomAt)
        {
            if (g_now - m->born < 0.6) continue;
            for (int j = 0; j < FOES; j++)
            {
                uint32_t e = g_foe[j];
                uint8_t d[24];
                float p[3];
                if (!e || !Descriptor(e, d) || Quad(d) != g_foeRes[j] || !UnitNode(Word(d + 12), 0, p)) continue;
                float dx = p[0] - m->pos[0], dy = p[1] - m->pos[1];
                if (dx * dx + dy * dy > s_jibTrigger * s_jibTrigger || fabsf(p[2] - m->pos[2]) > 6 || !Alive(e)) continue;
                m->boomAt = g_now + s_jibDelay;
                break;
            }
            if (!m->boomAt) continue;
        }
        if (g_now < m->boomAt) continue;
        float at[3] = {m->pos[0], m->pos[1], m->pos[2] + 0.3f};
        int r = hf->spawn_explosion ? hf->spawn_explosion(self, (uint32_t)s_jibExplosion, at) : -1;
        if (r == HF_ACT_WAITING && g_now < m->boomAt + 5) continue;
        MineBlast(at, 1);
        if (g_minesBlown <= 30) Log("Jack in the Box: a mine went off at %.1f %.1f %.1f (explosion %d: %s)", (double)at[0], (double)at[1], (double)at[2], s_jibExplosion,
            r == HF_ACT_DONE || r == HF_ACT_REQUESTED || r == HF_ACT_QUEUED ? "asked" : r == HF_ACT_WAITING ? "waiting for its package" : "refused");
        MineOff(m, 1);
    }
}

/* ---- the passives' frame: Artilleryman's bursts, and Ambush ------------------------------------------------------- */
static uintptr_t Behaviour(uint32_t e)
{
    uintptr_t m = Pointer(game + 0x3326740), recs;
    uint32_t i, count;
    if (!m || !Lookup(m + 0x40, e, 1048576, &i) || !ReadAt(m + 0x20, &count, 4) || i >= count || i >= 16384 || !(recs = Pointer(m + 0x60))) return 0;
    return recs + 0x1F8ull * i;
}
static uintptr_t Perception(uint32_t e)
{
    uintptr_t m = Pointer(game + 0x3326548), recs;
    uint32_t i;
    if (!m || !Lookup(m + 0x30, e, 1048576, &i) || i >= 16384 || !(recs = Pointer(m + 0x50))) return 0;
    return recs + 0x13F8ull * i;
}
static const struct { uint32_t count, entries; } kLists[3] = {{0x310, 0x318}, {0x818, 0x820}, {0xD20, 0xD28}};
static const uint32_t kSingles[3] = {0x1228, 0x1278, 0x12C8};
static uint32_t SensedIn(uintptr_t pr, uint32_t me)
{
    uint32_t bits = 0;
    for (int l = 0; l < 3; l++)
    {
        uint32_t c = 0;
        uint8_t ent[0x50];
        if (!ReadAt(pr + kLists[l].count, &c, 4) || c > 16) continue;
        for (uint32_t k = 0; k < c; k++)
            if (ReadAt(pr + kLists[l].entries + 0x50u * k, ent, sizeof ent) && Word(ent) == me) bits |= Word(ent + 0x4C);
    }
    return bits;
}
static int ForgetIn(uintptr_t pr, uint32_t me)
{
    int n = 0;
    for (int l = 0; l < 3; l++)
    {
        uint32_t c = 0;
        if (!ReadAt(pr + kLists[l].count, &c, 4) || c > 16 || !c) continue;
        uint8_t list[16 * 0x50];
        if (!ReadAt(pr + kLists[l].entries, list, 0x50u * c)) continue;
        uint32_t keep = 0;
        for (uint32_t k = 0; k < c; k++)
            if (Word(list + 0x50u * k) != me) { if (keep != k) memmove(list + 0x50u * keep, list + 0x50u * k, 0x50); keep++; }
        if (keep == c) continue;
        if (keep && hf->write(pr + kLists[l].entries, list, 0x50u * keep)) continue;
        if (hf->write(pr + kLists[l].count, &keep, 4)) continue;
        n += (int)(c - keep);
    }
    for (int s = 0; s < 3; s++)
    {
        uintptr_t at = pr + kSingles[s];
        uint32_t id = 0, used = 0;
        static const uint32_t zero = 0;
        if (ReadAt(at, &id, 4) && id == me && ReadAt(at + 0x48, &used, 4) && used && !hf->write(at + 0x48, &zero, 4)) n++;
    }
    return n;
}
/* Ambush's other half: an enemy within ambush_range m that does not sense her at this moment forgets her (its target
 * and its entries of her); one that senses her keeps her */
static void ForgetPass(void)
{
    uint32_t me = atomic_load(&g_avatar);
    float mp[3], p[3];
    if (!me || !PlayerPose(mp, NULL)) return;
    for (int j = 0; j < FOES; j++)
    {
        uint32_t id = g_foe[j];
        uint8_t d[24], b[0xA0];
        if (!id || !Descriptor(id, d) || Quad(d) != g_foeRes[j] || !UnitNode(Word(d + 12), 0, p) || Dist2(p, mp) > s_ambushRange * s_ambushRange) continue;
        uintptr_t br = Behaviour(id), pr = Perception(id), sq = 0;
        uint32_t perceiver = 0, bits = 0;
        int target = 0, had = 0;
        if (br && ReadAt(br, b, sizeof b)) { perceiver = Word(b + 0x68); target = Word(b + 0x18) == me; }
        if (perceiver && perceiver != id) sq = Perception(perceiver);
        if (pr) bits |= SensedIn(pr, me);
        if (sq) bits |= SensedIn(sq, me);
        if (bits) continue;
        if (target)
        {
            static const uint32_t z = 0;
            static const uint64_t z8 = 0;
            hf->write(br + 0x18, &z, 4); hf->write(br + 0x78, &z, 4); hf->write(br + 0x98, &z8, 8);
            had = 1;
        }
        if (pr && ForgetIn(pr, me)) had = 1;
        if (sq && ForgetIn(sq, me)) had = 1;
        g_forgot += (unsigned)had;
    }
}
/* Ambush as Ascalon is hidden: her row in the visibility table (game.dll+0x3326540: map +0x20, rows +0x40, 20 bytes each:
 * base +0, floor +12) cut by ambush_visual_cut percent while it is on, and put back */
static float g_oldBase, g_wroteBase, g_wroteFloor, g_oldFloor;
static int g_cut;
static void VisCut(int on)
{
    uint32_t me = atomic_load(&g_avatar), i;
    uintptr_t m = Pointer(game + 0x3326540), rows, row;
    float base, floor;
    if (!me || !m || !Lookup(m + 0x20, me, 1048576, &i) || i >= 262144 || !(rows = Pointer(m + 0x40))) { g_cut = 0; return; }
    row = rows + 20ull * i;
    if (!ReadAt(row, &base, 4) || !ReadAt(row + 12, &floor, 4)) { g_cut = 0; return; }
    float mult = 1 - s_visualCut / 100;
    if (on)
    {
        if (!(base >= 0 && base <= 10) || !(floor >= 0 && floor <= 10)) return;
        if (!g_cut || fabsf(base - g_wroteBase) > 1e-6f)
        {
            if (!g_cut && !(base >= 0.1f)) return;                       /* not a row the research knows */
            if (!g_cut) Log("Ambush: her visibility %.3f -> %.3f (floor %.3f -> %.3f)", (double)base, (double)(base * mult), (double)floor, (double)(floor * mult));
            g_oldBase = base; g_wroteBase = base * mult;
            hf->write(row, &g_wroteBase, 4);
        }
        if (!g_cut || fabsf(floor - g_wroteFloor) > 1e-6f)
        {
            g_oldFloor = floor; g_wroteFloor = floor * mult;
            hf->write(row + 12, &g_wroteFloor, 4);
        }
        g_cut = 1;
    }
    else if (g_cut)
    {
        if (fabsf(base - g_wroteBase) <= 1e-6f) hf->write(row, &g_oldBase, 4);
        if (fabsf(floor - g_wroteFloor) <= 1e-6f) hf->write(row + 12, &g_oldFloor, 4);
        g_cut = 0;
    }
}
static double g_landAt, g_firedAt, g_forgetAt;
static atomic_int g_landed;
static void PassiveFrame(void)
{
    uint32_t me = atomic_load(&g_avatar);
    float mp[3];
    /* Artilleryman's bursts */
    int n = g_nsplash;
    g_nsplash = 0;
    for (int i = 0; i < n && hf->spawn_explosion && PlayerPose(mp, NULL); i++)
    {
        if (Dist2(g_splashAt[i], mp) < s_splashSafe * s_splashSafe) continue;
        int r = hf->spawn_explosion(self, (uint32_t)s_splashExplosion, g_splashAt[i]);
        g_splashes++;
        if (g_splashes <= 6) Log("Artilleryman: a burst (explosion %d) where her shot landed, %.0f m from her: %s", s_splashExplosion, (double)sqrtf(Dist2(g_splashAt[i], mp)),
            r == HF_ACT_DONE || r == HF_ACT_REQUESTED || r == HF_ACT_QUEUED ? "asked" : r == HF_ACT_WAITING ? "waiting for its package" : "refused");
    }
    /* Ambush */
    if (atomic_exchange(&g_landed, 0)) g_landAt = g_firedAt = g_now;
    static uint32_t lastWeapon;
    static int lastRounds = -1;
    uintptr_t rec = InventoryRecord(me);
    uint32_t slots[8] = {0};
    if (rec && ReadAt(rec, slots, sizeof slots) && slots[7] >= 1 && slots[7] <= 3)
    {
        uint32_t w = slots[slots[7] - 1];
        int r = Rounds(w);
        if (w == lastWeapon && r >= 0 && lastRounds >= 0 && r < lastRounds) g_firedAt = g_now;
        lastWeapon = w; lastRounds = r;
    }
    if (g_dealtMs && GetTickCount64() - g_dealtMs < 200) g_firedAt = g_now;
    int was = g_ambushOn;
    g_ambushOn = g_alive && (s_ambush == 1 ? g_now - g_landAt >= s_ambushAfter : s_ambush == 2 ? g_now - g_firedAt >= s_ambushAfter : 0);
    static unsigned told;
    if (g_ambushOn != was && told++ < 30)
        Log(g_ambushOn ? "Ambush on (%s)" : "Ambush off (%s): %u hits on her missed, %u enemies made to forget her so far", s_ambush == 1 ? "after landing" : "not firing", g_missed, g_forgot);
    VisCut(g_ambushOn && s_visualCut > 0);
    if (g_ambushOn && s_ambushForget && g_now >= g_forgetAt) { g_forgetAt = g_now + 0.5; ForgetPass(); }
}

/* ---- for Jack in the Box: what a minefield spawns (logged; call an MD-6 Anti-Personnel Minefield in once) -------------- */
#define CENSUS 64
static uint32_t g_cEnt[CENSUS];
static uint64_t g_cRes[CENSUS];
static atomic_uint g_cHead;
static unsigned g_cTail;
static uint64_t g_mineKinds[24];
static int g_nMineKinds;
static double g_censusUntil, g_boomsUntil;
static unsigned g_boomTail;
static void CensusFrame(void)
{
    static const char* names[] = {"Health", "Explosive", "Projectile", "Behavior", "AiEnemy", "Interactable", "Throwable", "Hazard", "Trigger", "Sensor", "Mount"};
    for (unsigned head = atomic_load(&g_cHead); g_cTail != head; g_cTail++)
    {
        uint32_t e = g_cEnt[g_cTail % CENSUS];
        uint64_t r = g_cRes[g_cTail % CENSUS];
        if (r == HF_ENTITY_MD6_ANTIPERSONNEL_MINEFIELD)
        {
            g_censusUntil = g_now + 25;
            g_boomsUntil = g_now + 180;
            Log("mine census: an MD-6 Anti-Personnel Minefield deployer spawned (entity %u): what spawns in the next 25 s, and every explosion in the next 3 minutes, is listed", e);
            continue;
        }
        if (g_now > g_censusUntil || FoeKind(r) || r == AVATAR) continue;
        int known = 0;
        for (int i = 0; i < g_nMineKinds; i++) known |= g_mineKinds[i] == r;
        if (known || g_nMineKinds >= 24) continue;
        g_mineKinds[g_nMineKinds++] = r;
        char kinds[200] = "", name[64];
        uint32_t st;
        for (unsigned i = 0; i < sizeof names / sizeof *names; i++)
        {
            snprintf(name, sizeof name, "%sComponentData", names[i]);
            if (hf->component_record(name, r, &st)) { strcat(kinds, " "); strcat(kinds, names[i]); }
        }
        Log("mine census: spawned after the deployer: entity %u, resource %016llx:%s", e, (unsigned long long)r, kinds[0] ? kinds : " (none of the listed components)");
    }
    for (unsigned head = atomic_load(&g_boomHead); g_boomTail != head; g_boomTail++)
    {
        const Boom* b = &g_boom[g_boomTail % 32];
        uint64_t r = ResOf(b->source);
        static unsigned told, mine;
        if (r && r == MarkerRes() && mine++ < 10)
            Log("Jack in the Box: a MARKER blew by itself: explosion type %u from entity %u (%016llx) at %.1f %.1f %.1f", b->type, b->source, (unsigned long long)r, (double)b->pos[0],
                (double)b->pos[1], (double)b->pos[2]);
        if (g_now <= g_boomsUntil && told < 40 && r != GL21 && !FoeKind(r))
        {
            told++;
            Log("mine census: explosion type %u from entity %u (resource %016llx) at %.1f %.1f %.1f", b->type, b->source, (unsigned long long)r, (double)b->pos[0], (double)b->pos[1],
                (double)b->pos[2]);
        }
    }
}

/* ---- events and the frame ---------------------------------------------------------------------------------------- */
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    {
        unsigned ch = atomic_load(&g_cHead);
        g_cEnt[ch % CENSUS] = s->entity; g_cRes[ch % CENSUS] = s->resource;
        atomic_store(&g_cHead, ch + 1);
        if (s->resource == MarkerRes()) g_markerSeen = 1;
    }
    if (s->resource != AVATAR)
    {
        uint32_t stride;
        int foe = FoeKind(s->resource);
        /* also what the kit base's test leaves out: an enemy's turret, anything with the AI's enemy record and health */
        if (!foe && !FriendRes(s->resource) && !hf->component_record("AvatarComponentData", s->resource, &stride))
            foe = (hf->component_record("TurretComponentData", s->resource, &stride) && hf->component_record("BehaviorComponentData", s->resource, &stride)) ||
                  (hf->component_record("AiEnemyComponentData", s->resource, &stride) && hf->component_record("HealthComponentData", s->resource, &stride));
        if (hf->component_record("BugholeComponentData", s->resource, &stride) || hf->component_record("AiSpawnerComponentData", s->resource, &stride))
        {
            static unsigned told;
            if (told++ < 12) Log("D12: a bug hole or spawner listed: entity %u (%016llx)%s", s->entity, (unsigned long long)s->resource, foe ? ", which the kit also counts an enemy" : "");
            if (foe) AddFoe(s->entity, s->resource);
            unsigned th = atomic_load(&g_thingHead);
            g_thing[th % THINGS] = s->entity; g_thingRes[th % THINGS] = s->resource; g_thingWide[th % THINGS] = 1;
            atomic_store(&g_thingHead, th + 1);
        }
        else if (foe) AddFoe(s->entity, s->resource);
        else if (!FriendRes(s->resource) && hf->component_record("HealthComponentData", s->resource, &stride) && !hf->component_record("AvatarComponentData", s->resource, &stride) &&
                 !hf->component_record("SeatCollectionComponentData", s->resource, &stride) && !hf->component_record("WeaponComponentData", s->resource, &stride) &&
                 !hf->component_record("ProjectileComponentData", s->resource, &stride))
        {
            unsigned th = atomic_load(&g_thingHead);
            g_thing[th % THINGS] = s->entity; g_thingRes[th % THINGS] = s->resource; g_thingWide[th % THINGS] = 0;
            atomic_store(&g_thingHead, th + 1);
        }
        return;
    }
    if (!s->authority) return;
    g_spawnEntity[g_spawnNext % 8] = s->entity;
    g_spawnNet[g_spawnNext % 8] = s->network;
    g_spawnNext++;
}
static void OnBughole(HfMod* mod, const HfBugholeEvent* b, void* user)
{
    static unsigned told;
    for (int i = 0; i < THINGS; i++) if (g_thing[i] == b->entity) g_thing[i] = 0;
    for (int i = 0; i < MARKS; i++) if (g_mark[i].on && g_mark[i].entity == b->entity && !g_mark[i].boomAt) g_mark[i].on = 0;
    if (told++ < 20) Log("a bug hole closed: entity %u (%016llx), stage %u; %llu ms after her last skill blast", b->entity, (unsigned long long)b->resource, b->stage,
        (unsigned long long)(GetTickCount64() - atomic_load(&g_skillMs)));
}
static void OnKill(HfMod* mod, const HfEventInfo* info, void* user) { DropFoe(((const HfKillEvent*)info)->victim); }
static void OnPlayer(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    if (!p->local) return;
    if (info->kind == HF_ON_PLAYER_SPAWN)
    {
        if (p->entity && p->entity != atomic_load(&g_avatar)) { atomic_store(&g_avatar, p->entity); g_avatarNet = 0; }
        g_alive = 1;
        atomic_store(&g_landed, 1);
    }
    else g_alive = 0;
}
static void Tick(HfMod* mod, float dt, void* user)
{
    double step = dt > 0 && dt < 1 ? dt : 0;
    g_now += step;
    KbOptionsTick();   /* the options screen's changes, in a mission or not */
    int inMission = hf->game_state(self) == HF_STATE_MISSION;
    if (inMission != g_inMission)
    {
        g_inMission = inMission;
        if (inMission) { KitDecide(); g_charges = s_jibCharges; g_chargeAt = g_now; }
        if (!inMission)
        {
            Abort("the mission ended");
            MinesClear(0, "the mission ended");
            MarksClear("the mission ended");
            memset(g_thing, 0, sizeof g_thing); g_d12Ready = 0;
            g_cut = 0; g_ambushOn = 0;
            memset(g_frozen, 0, sizeof g_frozen);
            g_state.active = 0; g_state.count = 0; g_state.seq++;
            memset(g_foe, 0, sizeof g_foe); memset(g_foeRes, 0, sizeof g_foeRes);
            g_readyAt = 0;
        }
    }
    g_state.in_mission = (uint32_t)(g_inMission && kb_kit_on);
    if (!g_inMission || !game || !kb_kit_on) { g_state.skill = 0; return; }
    static int prev;
    int focused = Focused();
    int mods = focused ? ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? KM_SHIFT : 0) | ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? KM_CTRL : 0) |
                         ((GetAsyncKeyState(VK_MENU) & 0x8000) ? KM_ALT : 0) : 0;
    int own = s_key.vk == VK_SHIFT ? KM_SHIFT : s_key.vk == VK_CONTROL ? KM_CTRL : s_key.vk == VK_MENU ? KM_ALT : 0;
    int down = focused && (GetAsyncKeyState(s_key.vk) & 0x8000) != 0 && (mods & ~own) == s_key.mods;
    if (down && !prev && g_alive && atomic_load(&g_avatar))
    {
        if (g_k == K_HELD) End("the key again");
        else if (g_k == K_IDLE && g_now >= g_readyAt) Start();
    }
    prev = down;
    {
        static int prev2;
        const Key* k2 = &s_keys[1];
        int own2 = k2->vk == VK_SHIFT ? KM_SHIFT : k2->vk == VK_CONTROL ? KM_CTRL : k2->vk == VK_MENU ? KM_ALT : 0;
        int down2 = focused && (GetAsyncKeyState(k2->vk) & 0x8000) != 0 && (mods & ~own2) == k2->mods;
        if (down2 && !prev2 && g_alive && atomic_load(&g_avatar) && g_charges > 0) JibPlace();
        prev2 = down2;
        static int prev3[2];
        for (int q = 0; q < 2; q++)
        {
            const Key* k = &s_keys[2 + q];
            int o = k->vk == VK_SHIFT ? KM_SHIFT : k->vk == VK_CONTROL ? KM_CTRL : k->vk == VK_MENU ? KM_ALT : 0;
            int dn = focused && (GetAsyncKeyState(k->vk) & 0x8000) != 0 && (mods & ~o) == k->mods;
            if (dn && !prev3[q] && g_alive && atomic_load(&g_avatar)) { if (q) D12Detonate(); else D12Mark(); }
            prev3[q] = dn;
        }
    }
    if (!NetOk(g_avatarNet)) LearnNet();
    SkillFrame();
    MineFrame();
    MarkFrame();
    BlastFrame();
    FreezeFrame();
    PassiveFrame();
    CensusFrame();
    if (!g_alive) FreezeClear("she is down");
    g_state.skill = (g_k == K_HELD ? 1u : g_k != K_IDLE ? 3u : g_now < g_readyAt ? 2u : 0u) | (g_ambushOn ? 0x100u : 0u) | ((uint32_t)MarksOn() << 9) | (g_now < g_d12Ready ? 0x2000u : 0u) |
                    ((uint32_t)(g_charges < 0 ? 0 : g_charges > 255 ? 255 : g_charges) << 16) | ((uint32_t)MinesOut() << 24);
    g_state.value = g_k == K_HELD ? (float)Rounds(atomic_load(&g_gl)) : g_now < g_readyAt ? (float)(g_readyAt - g_now) : 0;
}
static void Pin(void** slot, uintptr_t at, const char* pattern, const char* what)
{
    if (hf->code_matches(at, pattern)) *slot = (void*)at;
    else Log("%s is not the reviewed code: King of Hearts is off", what);
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gameSize = 0, exeSize = 0;
    uintptr_t g = hf->module_base("game.dll", &gameSize), x = hf->module_base("helldivers2.exe", &exeSize);
    if (!g || !x || gameSize != 74727424u || exeSize != 60719104u) { Log("REFUSED: the game's modules are not build " BUILD "'s: the kit stays off"); return; }
    game = g;
    exe = x;
    Pin((void**)&g_take, game + 0x9AE030, "40 53 55 41 55 48 83 ec 40 48 89 7c 24 68 41 8b e8 4c 89 64 24 70 8b da", "the inventory's pickup (0x9AE030)");
    Pin((void**)&g_drop, game + 0x9A8840, "40 53 56 41 56 48 81 ec a0 00 00 00 48 8b 05 bd 37 c9 01 48 33 c4 48 89", "the inventory's drop (0x9A8840)");
    Pin((void**)&g_request, game + 0xA93430, "48 89 5c 24 18 48 89 6c 24 20 56 57 41 54 41 56 41 57 48 83 ec 50 48 8b 05 c3", "the Helldiver's switch request (0xA93430)");
    if (hf->code_matches(game + 0x5A2BB0, "48 89 5c 24 08 48 89 6c 24 10 56 57 41 54 41 56 41 57 48 83 ec 40") &&
        hf->code_matches(game + 0x4A7550, "48 89 5c 24 18 57 48 83 ec 20 3b 15 ?? ?? ?? ?? 48 8b 1d"))
    {
        g_setPosition = (SetVecFn)(game + 0x5A2BB0);
        g_setVelocity = (SetVecFn)(game + 0x4A7550);
    }
    if (Boundary(game + 0x5B36C0)) g_setDest = (SetVecFn)(game + 0x5B36C0);
    Log("ready: King of Hearts on %s (%d rounds, %.2f times the damage, %.0f s cooldown; the freeze %.1f s within %.1f m); the hold %s", s_key.label, s_rounds,
        (double)s_damage, (double)s_cooldown, (double)s_freezeSeconds, (double)s_freezeRadius, g_setPosition ? "ready" : "off (frozen enemies may slide)");
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 16 || api->size < offsetof(HfApi, global) + sizeof api->global) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    kb_ini = "w.ini";
    kb_key_names[0] = "key_s1";
    kb_key_names[1] = "key_s2";
    kb_key_names[2] = "key_s3";
    kb_key_names[3] = "key_s4";
    s_keys[3] = (Key){'B', 0, "B"};
    KB_SETTINGS(kSettings);
    KB_OPTIONS(kOptions);
    RosterJoin("w", "W");
    LoadSettings();
    KbOptionsStart(api, mod, "w", "W", VERSION, kOpRows, (int)(sizeof kOpRows / sizeof kOpRows[0]), kOpIcon, KB_OP_ICON, KB_OP_ACCENT, s_optionsKey);
    api->log(mod, "W %s (first build: King of Hearts on %s)", VERSION, s_key.label);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) && api->subscribe(mod, HF_ON_EXPLOSION, OnExplosion, NULL, 0) &&
             !hf_on_damage_pre(mod, OnDamagePre, NULL, 0) && api->on_game_thread(mod, Tick, NULL);   /* the typed helper answers 0 when it is in */
    if (!ok) api->log(mod, "events or the game thread are unavailable: the kit stays off");
    else if (hf_on_bughole(mod, OnBughole, NULL, 0)) api->log(mod, "the bug hole event is unavailable: a closed hole can be marked again");
    return 0;
}
