/* kitbase layer 2: the pose hold. An enemy held where it stands, in its pose, for its own number of seconds, whatever
 * its size (a Bile Titan too): stopped time, one enemy at a time. The DLL half; the HUD half is kitbase/hud/hold.lua.
 *
 * Code shared by the character kits. Source of truth: kitbase/ at the repo root. The copy in a kit's src/kitbase/ is
 * written by tools/kitbase_sync.py, so edit kitbase/, never the copy. See docs/kit-base.md.
 * Lifted 2026-11-26 from W 0.9.0 (the Time Stop Probe's method, per enemy; played 2026-11-25).
 *
 * The DLL keeps each held enemy still: the game's large stun every half second (HF_STATUS_STUN_LARGE), and for one
 * with a navigation record (Walks) its destination set to where it stands, its velocity zeroed and its place pinned
 * (kb_world.h's setters; only this machine's, and never a rider or a dead one: the warp setter crashed on both). The
 * HUD finds each held enemy's unit in the game's Lua and switches its animation state machine off while it is held and
 * on again after; one that died held gets its "dead" animation event (with its machine off as it died, a big enemy
 * stood dead in its pose and was never cleared away). It also draws the stunned sign over each one, at the top of its
 * skeleton (UnitTop).
 *
 * Hold only enemies a blast left alive, a moment after it (W 0.2.1: held in the frame of their death, their corpses went
 * wrong). The kit calls KbHoldAdd(enemy, seconds), KbHoldFrame() every frame it acts, KbHoldClear(why) at mission end,
 * and KB_HOLD_EXPORTS(kit) once (the HUD's `<kit>_holds` and `<kit>_hold_log`). Include after kb_world.h. */
#ifndef KB_HOLD_H
#define KB_HOLD_H

#include "kb_core.h"
#include "kb_player.h"
#include "kb_world.h"

#define KB_HOLDS 48
/* What the HUD reads (layout version 1; the same struct in kitbase/hud/hold.lua) */
typedef struct KbHoldTarget { uint32_t entity, unit; uint64_t object; float pos[3]; float top; } KbHoldTarget;   /* top: its highest part over pos (0 unread) */
typedef struct KbHoldState
{
    uint32_t version, seq, count, rare_max;   /* seq: bumped on every change; rare_max: the HUD reads places only of unit types this rare */
    uint32_t player_unit, in_mission;
    uint64_t player_object;
    float player_pos[3], pad;
    KbHoldTarget t[KB_HOLDS];
    uint32_t dead_seq, dead[16];              /* the held enemies that died held: a ring, dead_seq the count so far */
} KbHoldState;
KB_VAR KbHoldState g_hold = {.version = 1};
KB_VAR int kb_hold_rare_max = 4;              /* reading the place of a unit of a mass-instanced type crashed the game (Exo Autopilot) */
KB_VAR unsigned g_holdLines;

typedef struct KbHeldT { uint32_t entity, unit; float pos[3]; double until; float top; } KbHeldT;
KB_VAR KbHeldT g_held[KB_HOLDS];
KB_VAR unsigned g_heldTotal, g_holdPins;

KB_FN void KbHoldPublish(void)
{
    uint8_t d[24];
    float me[3];
    uint32_t n = 0;
    for (int k = 0; k < KB_HOLDS; k++)
    {
        const KbHeldT* f = &g_held[k];
        if (!f->entity) continue;
        KbHoldTarget* t = &g_hold.t[n++];
        t->entity = f->entity; t->unit = f->unit; t->object = UnitObject(f->unit); memcpy(t->pos, f->pos, 12); t->top = f->top;
    }
    if (Descriptor(atomic_load(&g_avatar), d) && PlayerPose(me, NULL))
    {
        g_hold.player_unit = Word(d + 12);
        g_hold.player_object = UnitObject(g_hold.player_unit);
        memcpy(g_hold.player_pos, me, 12);
    }
    g_hold.rare_max = (uint32_t)kb_hold_rare_max;
    g_hold.count = n;
    g_hold.seq++;
}
/* Holds an enemy for `seconds` (a longer hold of one already held lengthens it): 1 when newly held */
KB_FN int KbHoldAdd(uint32_t e, float seconds)
{
    uint8_t d[24];
    float p[3];
    int room = -1;
    if (seconds <= 0) return 0;
    for (int k = 0; k < KB_HOLDS; k++)
    {
        if (g_held[k].entity == e) { if (g_now + seconds > g_held[k].until) g_held[k].until = g_now + seconds; return 0; }
        if (!g_held[k].entity && room < 0) room = k;
    }
    if (room < 0 || !Descriptor(e, d) || !Alive(e) || !UnitNode(Word(d + 12), 0, p)) return 0;
    KbHeldT* f = &g_held[room];
    f->entity = e; f->unit = Word(d + 12); memcpy(f->pos, p, 12); f->until = g_now + seconds;
    f->top = UnitTop(f->unit, p);
    g_heldTotal++;
    KbHoldPublish();
    return 1;
}
KB_FN int KbHeld(uint32_t e)
{
    for (int k = 0; k < KB_HOLDS; k++) if (e && g_held[k].entity == e) return 1;
    return 0;
}
KB_FN void KbHoldClear(const char* why)
{
    int n = 0;
    for (int k = 0; k < KB_HOLDS; k++) if (g_held[k].entity) { g_held[k].entity = 0; n++; }
    if (n) { KbHoldPublish(); Log("the hold let go of %d (%s)", n, why); }
}
KB_FN void KbHoldFrame(void)
{
    static const float zero[3] = {0, 0, 0};
    static double stunAt, destAt;
    int changed = 0, any = 0;
    int stun = g_now >= stunAt, dest = g_setDest && g_now >= destAt, idle = g_setPosition && g_setVelocity && QueryIdle();
    g_hold.in_mission = (uint32_t)(g_inMission && kb_kit_on);
    for (int k = 0; k < KB_HOLDS; k++)
    {
        KbHeldT* f = &g_held[k];
        uint8_t d[24];
        float p[3];
        if (!f->entity) continue;
        int same = Descriptor(f->entity, d) && Word(d + 12) == f->unit, alive = same && Alive(f->entity);
        if (g_now < f->until && same && !alive) { g_hold.dead[g_hold.dead_seq % 16] = f->entity; g_hold.dead_seq++; }
        if (g_now >= f->until || !alive || !UnitNode(f->unit, 0, p)) { f->entity = 0; changed = 1; continue; }
        any = 1;
        if (stun && hf->apply_status) hf->apply_status(self, f->entity, HF_STATUS_STUN_LARGE, 100.0f);
        if (!idle || !(Word(d + 20) & 1)) continue;   /* this machine's only */
        int walks = Walks(f->entity);
        if (!walks) continue;
        if (dest && walks == 2) g_setDest(NULL, f->entity, f->pos);
        g_setVelocity(NULL, f->entity, zero);
        if (Dist2(p, f->pos) > 0.03f * 0.03f) { g_setPosition(NULL, f->entity, f->pos); g_holdPins++; }
    }
    if (any && stun) stunAt = g_now + 0.5;
    if (any && dest) destAt = g_now + 1.0;
    if (changed) KbHoldPublish();
}
#define KB_HOLD_EXPORTS(kit)                                                                       \
    HF_MOD_EXPORT KbHoldState* kit##_holds(void) { return &g_hold; }                               \
    HF_MOD_EXPORT int kit##_hold_log(unsigned version, const char* text)                           \
    {                                                                                              \
        if (version != 1 || !text) return 0;                                                       \
        if (g_holdLines++ < 400) Log("[lua] %.400s", text);                                        \
        return 1;                                                                                  \
    }

#endif
