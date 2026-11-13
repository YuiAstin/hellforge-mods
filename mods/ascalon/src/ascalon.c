/* Ascalon (Arknights) for HellForge: the always-hidden kit. Design: docs/ascalon-design.md.
 *
 * 0.1.0 is the core only, "hidden until she makes noise":
 *   - Hidden: enemies' visual detection of her is cut (the game's per-entity visibility row, as the CAMO-EX01 Optical
 *     Cloak Pack found it), whoever still targets her forgets her (Lost Contact's break contact, without its wait), her
 *     body and gear are not drawn (the HUD side: Unit.set_mesh_visibility on her units, Cloak Probe 0.1.14).
 *   - What reveals her is the game's own senses (0.1.1, the user's idea): an enemy whose perception entry of her has
 *     sense bits set senses her now (it heard a shot or a blast, or she is right in front of it), and keeps her; only
 *     the ones that hold her without sensing her forget her. No invented noise ranges. While anyone senses her (and
 *     reveal_hold s after) she is drawn and the dodge is off.
 *     0.1.2: the sense bits turned out to be sight only (two test missions: "sense bits 1" at 1 to 27 m, never from a
 *     shot). Hearing shows as the user said it would: an enemy that takes her as its target, or gains an entry of her,
 *     without seeing her. Such an acquisition within noise_window s of her last shot is "it heard her": that enemy
 *     keeps her for heard_hold s. The same without a shot (stale memory, squad chatter, a melee hit) is forgotten.
 *   - The user's rule: with a backpack on, or a support weapon in her hands, there is no functional hiding at all.
 *   - Ambusher: while hidden, half the hits that still land on her miss (dodge).
 * 0.2.0, her trait and her first skill (Yui, 2026-11-14: "hit everything should always be on, in a radius of 15 m";
 * Pursuit without the gas: "the slow, AoE damage, double hits and the multiplier, AoE as the Executor's"):
 *   - Ambusher (always on): every hit of hers (melee, bullet, beam) also strikes every living enemy within area_radius m
 *     of her that she has a clear line to, for area_mult times that hit's damage. As the Executor's area rounds: one
 *     spawned round an enemy, its hit's amount set in the damage event. At most one volley every area_every s, area_max
 *     enemies. The rounds are the mod's, not her gun's: they are not noise.
 *   - Pursuit (S1, by itself, pursuit_charges charges, one back every pursuit_every s): her next hit deals pursuit_mult
 *     times its damage and lands twice (a second volley that takes in the enemy she hit), on everyone in range, and
 *     slows each one it strikes (the flamer slow, pursuit_slow build-up).
 * Endowment, Descent and Mortal Interrogation are not in this build.
 *
 * Built on the kit base (src/kitbase). Solo or host. */
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

#define VERSION "0.2.0"
#define BUILD "25480438"

#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"
#include "kitbase/kb_body.h"
#include "kitbase/kb_hits.h"
#include "kitbase/kb_damage.h"
#include "kitbase/kb_settings.h"
#include "kitbase/kb_roster.h"

KB_ROSTER_EXPORTS(ascalon)

/* ---- settings ------------------------------------------------------------------------------------------------- */
static float s_areaRadius = 15, s_areaMult = 1, s_areaEvery = 0.25f, s_pursuitMult = 2.1f, s_pursuitEvery = 7, s_pursuitSlow = 100;
static int s_areaMax = 12, s_areaRound = 7, s_pursuitCharges = 3;
static float s_visualCut = 95, s_revealHold = 1.5f, s_noiseWindow = 1.0f, s_heardHold = 5;
static float s_dodge = 0.5f, s_forgetEvery = 0.5f, s_forgetRange = 150;
static int s_cloakVisual = 1, s_hideGear = 1, s_gearRule = 1;

static const KbSetting kSettings[] = {
    KB_F("area_radius", s_areaRadius, 0, 60), KB_F("area_mult", s_areaMult, 0, 10), KB_F("area_every", s_areaEvery, 0.05, 5),
    KB_I("area_max", s_areaMax, 0, 24), KB_I("area_round", s_areaRound, 1, 100000), KB_F("pursuit_mult", s_pursuitMult, 1, 20),
    KB_F("pursuit_every", s_pursuitEvery, 0.5, 120), KB_F("pursuit_slow", s_pursuitSlow, 0, 1000), KB_I("pursuit_charges", s_pursuitCharges, 0, 9),
    KB_F("visual_cut", s_visualCut, 0, 99.9), KB_F("reveal_hold", s_revealHold, 0, 60), KB_F("noise_window", s_noiseWindow, 0, 10), KB_F("heard_hold", s_heardHold, 0, 60), KB_F("dodge", s_dodge, 0, 1),
    KB_F("forget_every", s_forgetEvery, 0.1, 5), KB_F("forget_range", s_forgetRange, 10, 500),
    KB_I("cloak_visual", s_cloakVisual, 0, 1), KB_I("hide_gear", s_hideGear, 0, 1), KB_I("gear_rule", s_gearRule, 0, 1),
    KB_F("hp_mult", s_hpMult, 0.1, 10), KB_F("stamina_mult", s_staminaMult, 0.1, 20), KB_I("hellbomb_on_exhaustion", s_hellbomb, 0, 1),
    KB_I("hellbomb_explosion", s_hellbombRow, 0, 100000), KB_F("hellbomb_hold", s_hellbombHold, 0, 10),
};

/* The default ascalon.ini: tools/inigen.py writes the file from this text (the mod never writes it). */
static __attribute__((unused)) void DefaultIni(FILE* f)
{
    fprintf(f, "; Ascalon %s. Keys: a letter, digit, F1-F24, Mouse3, Mouse4 or Mouse5, with Shift+, Ctrl+ or Alt+ in front for a\n"
               "; combo (Shift+Q). (Her skills are not in this build: the keys are kept for them.)\n"
               "key_spare = %s\nkey_endowment = %s\nkey_descent = %s\n"
               "; Ambusher (always on): every hit of hers also strikes every enemy within area_radius m of her that she has a\n"
               "; clear line to, for area_mult times that hit's damage: at most area_max enemies, one volley every area_every s.\n"
               "; area_round: the vanilla projectile the strikes fly as (7: a rifle round).\n"
               "area_radius = %g\narea_mult = %g\narea_max = %d\narea_every = %g\narea_round = %d\n"
               "; Pursuit (by itself): pursuit_charges charges, one back every pursuit_every s. A hit with a charge deals\n"
               "; pursuit_mult times its damage, lands twice on everyone in range and slows them (pursuit_slow: 100 a full dose).\n"
               "pursuit_charges = %d\npursuit_every = %g\npursuit_mult = %g\npursuit_slow = %g\n"
               "; Hidden: while she makes no noise, enemies see her visual_cut percent less far, and whoever within forget_range m\n"
               "; still targets her forgets her every forget_every s.\n"
               "visual_cut = %g\nforget_every = %g\nforget_range = %g\n"
               "; What reveals her is the game's own hearing and sight: an enemy that senses her right now (it heard her gun, or\n"
               "; she is right in front of it) keeps her; she counts as revealed while any does, and reveal_hold s after.\n"
               "reveal_hold = %g\n"
               "; Hearing: an enemy that picks her up without seeing her within noise_window s of her last shot heard it, and\n"
               "; keeps her for heard_hold s (the game decides who hears which gun, and how far).\n"
               "noise_window = %g\nheard_hold = %g\n"
               "; gear_rule 1: with a backpack on or a support weapon in her hands she is not hidden at all (she still looks it).\n"
               "gear_rule = %d\n"
               "; Ambusher: while hidden, this share of the hits that still land on her miss (0.5: half).\ndodge = %g\n"
               "; cloak_visual 1: her body is not drawn while hidden (needs Bingus Shared Loader and HD2Runtime); hide_gear 1: nor\n"
               "; are her weapons and backpack.\ncloak_visual = %d\nhide_gear = %d\n"
               "; Her body: hp_mult times a Helldiver's health, stamina_mult times her sprint.\n"
               "hp_mult = 1\nstamina_mult = 1.2\nhellbomb_on_exhaustion = 0\n",
            VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, (double)s_areaRadius, (double)s_areaMult, s_areaMax, (double)s_areaEvery,
            s_areaRound, s_pursuitCharges, (double)s_pursuitEvery, (double)s_pursuitMult, (double)s_pursuitSlow, (double)s_visualCut, (double)s_forgetEvery, (double)s_forgetRange,
            (double)s_revealHold, (double)s_noiseWindow, (double)s_heardHold, s_gearRule, (double)s_dodge, s_cloakVisual,
            s_hideGear);
}

static uint32_t UnitOf(uint32_t e)
{
    uint8_t d[24];
    return e && e != UINT32_MAX && Descriptor(e, d) ? Word(d + 12) : 0;
}
#include "as_world.h"

/* ---- state ---------------------------------------------------------------------------------------------------- */
enum { HIDDEN = 0, QUIET = 1 /* unused since 0.1.1 */, REVEALED = 2, GEAR = 3, AWAY = 4 };   /* AWAY: not on the ground */
static atomic_int g_state = AWAY;
static int g_linksOk;   /* the unit links are where the research has them */
static double g_sensedUntil, g_forgetAt;
static int g_sensing;   /* enemies that see or have heard her, at the last pass */
static double g_noiseAt = -100, g_heardUntil[FOES];
static uint32_t g_heardId[FOES];
static unsigned g_heard, g_shots;
static uint32_t g_heldSel;
static uint64_t g_heldRes;
static unsigned g_dodged, g_forgot, g_reveals;
static const char* kStateName[] = {"hidden", "-", "revealed (an enemy sees or heard her)", "not hidden (backpack or support weapon)", "away"};

/* ---- the visibility row (game.dll+0x3326540: map +0x20, rows +0x40, 20 bytes each: base +0, floor +12) ------------- */
static float g_oldBase, g_wroteBase, g_wroteFloor, g_oldFloor;
static int g_cut;
static uintptr_t VisRow(void)
{
    uint32_t me = atomic_load(&g_avatar), i;
    uintptr_t m = Pointer(game + 0x3326540), rows;
    if (!me || !m || !Lookup(m + 0x20, me, 1048576, &i) || i >= 262144 || !(rows = Pointer(m + 0x40))) return 0;
    return rows + 20ull * i;
}
static void VisCut(int on)
{
    uintptr_t row = VisRow();
    float base, floor;
    if (!row || !ReadAt(row, &base, 4) || !ReadAt(row + 12, &floor, 4)) { g_cut = 0; return; }
    float mult = 1 - s_visualCut / 100;
    if (on)
    {
        if (!(base >= 0 && base <= 10) || !(floor >= 0 && floor <= 10)) return;
        if (!g_cut || fabsf(base - g_wroteBase) > 1e-6f)
        {
            if (!g_cut && !(base >= 0.1f)) return;                       /* not a row the research knows */
            if (!g_cut) Log("hidden: her visibility %.3f -> %.3f (floor %.3f -> %.3f)", base, base * mult, floor, floor * mult);
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

/* ---- forgetting her (Lost Contact 0.4.1's records: behaviour game+0x3326740, perception game+0x3326548) ------------ */
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
/* the sense bits (+0x4C) of her entries in a perception record, or'd: nonzero while that perceiver senses her now */
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
/* does a perception record hold an entry of her at all? */
static int HoldsIn(uintptr_t pr, uint32_t me)
{
    for (int l = 0; l < 3; l++)
    {
        uint32_t c = 0, id = 0;
        if (!ReadAt(pr + kLists[l].count, &c, 4) || c > 16) continue;
        for (uint32_t k = 0; k < c; k++) if (ReadAt(pr + kLists[l].entries + 0x50u * k, &id, 4) && id == me) return 1;
    }
    for (int k = 0; k < 3; k++)
    {
        uint32_t id = 0, used = 0;
        if (ReadAt(pr + kSingles[k], &id, 4) && id == me && ReadAt(pr + kSingles[k] + 0x48, &used, 4) && used) return 1;
    }
    return 0;
}
/* her entries taken out of a perception record (the caller has checked nobody there senses her): entries removed */
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
static int FoeRoot(uint32_t e, uint64_t res, float p[3])
{
    uint8_t d[24];
    return Descriptor(e, d) && Quad(d) == res && UnitNode(Word(d + 12), 0, p);
}
/* every enemy within forget_range m: one that senses her now keeps her (and counts); one that only remembers or
 * targets her forgets her */
static void ForgetPass(void)
{
    uint32_t me = atomic_load(&g_avatar);
    float mp[3], p[3];
    if (!me || !PlayerPose(mp, NULL)) return;
    int n = 0, sensing = 0;
    for (int j = 0; j < FOES; j++)
    {
        uint32_t id = g_foe[j];
        if (!id) continue;
        if (!FoeRoot(id, g_foeRes[j], p)) { g_foe[j] = 0; continue; }
        float d2 = Dist2(p, mp);
        if (d2 > s_forgetRange * s_forgetRange) continue;
        uintptr_t br = Behaviour(id), pr = Perception(id), sq = 0;
        uint8_t b[0xA0];
        uint32_t perceiver = 0, bits = 0;
        int target = 0, had = 0;
        if (br && ReadAt(br, b, sizeof b)) { perceiver = Word(b + 0x68); target = Word(b + 0x18) == me; }
        if (perceiver && perceiver != id) sq = Perception(perceiver);
        if (pr) bits |= SensedIn(pr, me);
        if (sq) bits |= SensedIn(sq, me);
        if (bits)
        {
            sensing++;
            static unsigned told;
            if (told < 40) { told++; Log("sensed by enemy %u (%016llx) at %.0f m: sense bits %x%s", id, (unsigned long long)g_foeRes[j], sqrtf(d2), bits, target ? ", its target" : ""); }
            continue;
        }
        int holds = target || (pr && HoldsIn(pr, me)) || (sq && HoldsIn(sq, me));
        if (holds)
        {
            if (g_heardId[j] != id) { g_heardId[j] = id; g_heardUntil[j] = 0; }
            if (g_now >= g_heardUntil[j] && g_now - g_noiseAt <= s_noiseWindow)
            {
                g_heardUntil[j] = g_now + s_heardHold;
                g_heard++;
                static unsigned told;
                if (told < 40) { told++; Log("heard by enemy %u (%016llx) at %.0f m%s", id, (unsigned long long)g_foeRes[j], sqrtf(d2), target ? ", its target" : ""); }
            }
            if (g_now < g_heardUntil[j]) { sensing++; continue; }
        }
        if (target)
        {
            static const uint32_t z = 0;
            static const uint64_t z8 = 0;
            hf->write(br + 0x18, &z, 4); hf->write(br + 0x78, &z, 4); hf->write(br + 0x98, &z8, 8);
            had = 1;
        }
        if (pr && ForgetIn(pr, me)) had = 1;
        if (sq && ForgetIn(sq, me)) had = 1;
        n += had;
    }
    g_forgot += (unsigned)n;
    g_sensing = sensing;
    if (sensing) g_sensedUntil = g_now + s_revealHold;
}

/* ---- what reveals her ------------------------------------------------------------------------------------------ */
static void OnStat(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfStatEvent* st = (const HfStatEvent*)info;
    if (st->stat != HF_STAT_SHOTS || st->added <= 0 || !kb_kit_on || !g_alive) return;
    /* only shots the game credits to the gun in her hands: her area strikes are spawned rounds (credited to her
     * Helldiver, as Exusiai 0.3.1 found) and are not noise */
    int mine = 0;
    for (uint32_t k = 0; k < st->count && k < HF_STAT_SOURCES; k++)
        if (g_heldRes && st->sources[k].resource == g_heldRes && st->sources[k].added > 0) mine += st->sources[k].added;
    if (mine <= 0) return;
    g_noiseAt = g_now;
    g_shots += (unsigned)mine;
}
static void OnWeaponAsc(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfWeaponEvent* w = (const HfWeaponEvent*)info;
    if (!w->avatar || w->avatar != atomic_load(&g_avatar)) return;
    g_heldSel = w->selection;
    g_heldRes = w->resource;
}
static int Item(int k) { uint32_t e = atomic_load(&g_myItems[k]); return e && e != UINT32_MAX ? (int)e : 0; }
static int StateNow(void)
{
    if (!g_alive || !atomic_load(&g_avatar)) return AWAY;
    if (s_gearRule && (Item(3) || g_heldSel == 3)) return GEAR;
    if (g_now < g_sensedUntil) return REVEALED;
    return HIDDEN;
}

/* ---- hits on her: the Ambusher's dodge --------------------------------------------------------------------------- */
static int HitTaken(const uint8_t* h, void* result, unsigned long long now)
{
    (void)h; (void)now;
    int st = atomic_load(&g_state);
    if ((st != HIDDEN && st != QUIET) || s_dodge <= 0) return 0;
    static _Atomic unsigned seed = 0x9E3779B9u;
    unsigned x = atomic_load(&seed);
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    atomic_store(&seed, x);
    if ((x & 0xFFFF) / 65536.0f >= s_dodge) return 0;
    memset(result, 0, RESULT_SIZE);
    g_dodged++;
    return 1;
}
static void DamageTaken(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me) { (void)e; (void)d; (void)me; }

/* ---- Ambusher: her hits strike everyone in range; Pursuit --------------------------------------------------------- */
static int Friend(uint32_t e)
{
    uint8_t d[24];
    return Descriptor(e, d) && (Quad(d) == AVATAR || FriendRes(Quad(d)));
}
#define VOL 24
static struct { double at; int n, pursuit; int32_t damage; uint32_t tgt[VOL]; uint8_t left[VOL]; } g_vol;
static int g_volOwed, g_volPursuit, g_charges;
static int32_t g_volDamage;
static uint32_t g_volPrimary;
static double g_areaAt = -100, g_chargeAt;
static uint32_t g_slowQ[32];
static int g_nslow;
static unsigned g_volleys, g_strikes, g_pursuits;
static void SlowLater(uint32_t e) { if (s_pursuitSlow > 0 && g_nslow < 32) g_slowQ[g_nslow++] = e; }
static void DamageDealt(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    (void)e; (void)me;
    if (!MineHit(d) || d->amount <= 0 || Friend(d->target) || s_areaMax < 1 || s_areaRadius <= 0) return;
    /* one of the last volley's strikes landing: its amount is the hit's that set it off */
    if (d->kind == 0 && g_vol.n && g_now - g_vol.at <= 1.0)
        for (int i = 0; i < g_vol.n; i++)
            if (g_vol.tgt[i] == d->target && g_vol.left[i])
            {
                g_vol.left[i]--;
                double a = (double)g_vol.damage * s_areaMult * (g_vol.pursuit ? s_pursuitMult : 1);
                d->amount = a < 1 ? 1 : a > 2000000 ? 2000000 : (int32_t)a;
                if (g_vol.pursuit) SlowLater(d->target);
                g_strikes++;
                return;
            }
    /* a hit of her own: not damage over time (2), an explosion (4), drowning or bleeding (8, 9) */
    if (d->kind == 2 || d->kind == 4 || d->kind >= 8) return;
    /* a bullet's hit counts only when her gun has just fired (the shot counter): a strike round that hits a bystander on
     * its way would otherwise set off the next volley, and that one the next */
    if (d->kind == 0 && g_now - g_noiseAt > 1.0) return;
    if (g_now - g_areaAt < s_areaEvery) return;
    g_areaAt = g_now;
    int pursuit = g_charges > 0;
    g_volDamage = d->amount;
    g_volPrimary = d->target;
    g_volPursuit = pursuit;
    g_volOwed = pursuit ? 2 : 1;
    static unsigned told;
    if (told < 12) { told++; Log("a hit of hers (kind %u, %d damage) on entity %u: everyone within %.0f m is struck%s", d->kind, d->amount, d->target, (double)s_areaRadius, pursuit ? " (Pursuit: twice, and slowed)" : ""); }
    if (pursuit)
    {
        if (g_charges == s_pursuitCharges) g_chargeAt = g_now;
        g_charges--;
        g_pursuits++;
        double a = (double)d->amount * s_pursuitMult;
        d->amount = a > 2000000 ? 2000000 : (int32_t)a;
        SlowLater(d->target);
    }
}
/* the GAME THREAD: the volleys owed (one a frame), the slows owed, Pursuit's charges */
static void AreaFrame(void)
{
    for (int i = 0; i < g_nslow; i++) if (hf->apply_status) Act("Pursuit's slow", hf->apply_status(self, g_slowQ[i], HF_STATUS_FLAMER_SLOWED, s_pursuitSlow));
    g_nslow = 0;
    if (g_charges < s_pursuitCharges && g_now - g_chargeAt >= s_pursuitEvery) { g_charges++; g_chargeAt = g_now; }
    if (g_volOwed <= 0) return;
    int second = g_volPursuit && g_volOwed == 1;   /* Pursuit's second landing takes in the enemy she hit */
    g_volOwed--;
    float pos[3], aim[3];
    if (!PlayerPose(pos, aim)) return;
    float from[3] = {pos[0], pos[1], pos[2] + 1.3f};
    struct { uint32_t e; float d2; float q[3]; } c[VOL];
    int nc = 0, cap = s_areaMax < VOL ? s_areaMax : VOL;
    for (int k = 0; k < FOES; k++)
    {
        uint32_t en = g_foe[k];
        uint8_t d[24];
        float r[3];
        if (!en || (en == g_volPrimary && !second) || !Descriptor(en, d) || Quad(d) != g_foeRes[k] || !UnitNode(Word(d + 12), 0, r)) continue;
        float d2 = Dist2(r, pos);
        if (d2 > s_areaRadius * s_areaRadius || (nc == cap && d2 >= c[cap - 1].d2) || !Living(en)) continue;
        int j = nc < cap ? nc++ : cap - 1;
        while (j > 0 && c[j - 1].d2 > d2) { c[j] = c[j - 1]; j--; }
        c[j].e = en; c[j].d2 = d2;
        float off[3];
        if (BodyOffset(Word(d + 12), r, off)) for (int t = 0; t < 3; t++) r[t] += off[t];
        else r[2] += 1.0f;
        memcpy(c[j].q, r, 12);
    }
    if (!nc) return;
    if (!second) { memset(&g_vol, 0, sizeof g_vol); g_vol.damage = g_volDamage; g_vol.pursuit = g_volPursuit; }
    g_vol.at = g_now;
    int fired = 0;
    for (int j = 0; j < nc; j++)
    {
        if (!ClearShot(from, c[j].q)) continue;
        float v[3] = {c[j].q[0] - from[0], c[j].q[1] - from[1], c[j].q[2] - from[2]};
        float vn = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (!(vn > 0.1f)) continue;
        for (int k = 0; k < 3; k++) v[k] /= vn;
        int i = 0;
        while (i < g_vol.n && g_vol.tgt[i] != c[j].e) i++;
        if (i == g_vol.n) { if (g_vol.n == VOL) continue; g_vol.tgt[g_vol.n++] = c[j].e; }
        g_vol.left[i]++;                       /* before the spawn: the round may land at once */
        int r = hf->spawn_projectile(self, (uint32_t)s_areaRound, from, v);
        if (r == HF_ACT_WAITING || !Act("an area strike", r)) { g_vol.left[i]--; if (r == HF_ACT_WAITING) g_volOwed++; break; }
        fired++;
    }
    if (fired)
    {
        g_volleys++;
        static unsigned told;
        if (told++ < 10) Log("area strike: %d enem%s of the %d within %.0f m (projectile %d)%s", fired, fired == 1 ? "y" : "ies", nc, (double)s_areaRadius, s_areaRound, second ? ", Pursuit's second" : "");
    }
}

/* ---- events ------------------------------------------------------------------------------------------------------ */
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
    if (info->kind == HF_ON_PLAYER_SPAWN)
    {
        if (p->entity && p->entity != atomic_load(&g_avatar)) { atomic_store(&g_avatar, p->entity); g_avatarNet = 0; g_cut = 0; }
        g_alive = 1;
        g_sensedUntil = 0; g_sensing = 0;
        LearnNet();
    }
    else { g_alive = 0; g_cut = 0; }
}
static void Reset(const char* why)
{
    if (g_forgot || g_reveals || g_dodged)
        Log("%s: %u shots, heard %u times, revealed %u times, %u times an enemy forgot her, %u hits dodged; %u area volleys (%u strikes landed), Pursuit %u times",
            why, g_shots, g_heard, g_reveals, g_forgot, g_dodged, g_volleys, g_strikes, g_pursuits);
    int start = !strcmp(why, "mission start");
    if (start) KitDecide();
    Body(start);
    g_boomPending = 0; g_boomArmed = 0;
    g_sensedUntil = g_forgetAt = 0;
    g_sensing = 0;
    g_forgot = g_reveals = g_dodged = g_heard = g_shots = 0;
    g_volleys = g_strikes = g_pursuits = 0;
    g_volOwed = 0; g_nslow = 0; g_vol.n = 0;
    g_charges = s_pursuitCharges; g_chargeAt = g_now;
    g_noiseAt = -100;
    memset(g_heardUntil, 0, sizeof g_heardUntil);
    g_cut = 0;
    atomic_store(&g_state, AWAY);
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gameSize = 0, exeSize = 0;
    uintptr_t g = hf->module_base("game.dll", &gameSize), x = hf->module_base("helldivers2.exe", &exeSize);
    if (!g || !x || gameSize != 74727424u || exeSize != 60719104u) { Log("REFUSED: the game's modules are not build " BUILD "'s: her kit stays off"); return; }
    game = g;
    exe = x;
    HitsInstall("the dodge");
    if (hf->code_matches(exe + 0x79F860, "33 c0 4c 8d 05 c7 b2 01 02 0f 1f 80 00 00 00 00 49 39 0c c0 74 0c ff c0 83 f8 04 75 f3") &&
        hf->code_matches(exe + 0x7F4590, "48 8b c4 f3 0f 11 58 20 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48"))
    {
        g_worldId = (WorldFn)(exe + 0x79F860);
        g_raycast = (RayFn)(exe + 0x7F4590);
    }
    else Log("the game's raycast is not the reviewed code: her area strikes do not check for walls");
    g_linksOk = hf->code_matches(exe + 0x2BEB9A, "48 8d b9 d8 01 00 00") && hf->code_matches(exe + 0x2BEBFA, "48 81 c7 e0 01 00 00");
    if (!g_linksOk) Log("the engine's unit links are not where the research has them: her gear is found by position (it may stay drawn)");
    g_staminaOk = hf->code_matches(game + 0xA50E2D, "f3 42 0f 11 8c 11 54 78 54 00");
}

/* ---- the HUD state (read by the Ascalon HUD addon through LuaJIT's ffi) ------------------------------------------ */
typedef struct AscalonHud
{
    uint32_t version, in_mission;
    uint32_t state;                 /* 0 hidden, 2 revealed, 3 not hidden (gear), 4 away */
    float reveal_left;              /* seconds until hidden again once nobody senses her (state 2) */
    uint32_t cloak_visual, hide_gear;
    uint32_t units[5];              /* scene units: her Helldiver, then her primary, secondary, support weapon, backpack (0: none) */
    uint32_t sensing;               /* enemies that sense her now */
    uint32_t charges, charges_max;  /* 0.2.0: Pursuit */
    float charge_left;              /* seconds until the next charge (0: full) */
    uint32_t nwanted;               /* 0.1.3: her scene units, by the engine's own links (0: not known, use units[]) */
    uint32_t wanted[256];
} AscalonHud;
static AscalonHud g_hud = {1};
/* 0.1.3: what she is made of, by the engine's own unit links (the Optical Cloak Pack's way): her Helldiver's unit and
 * every unit linked under it (first child at Unit +0x1D8, next sibling at +0x1E0), and the same from each item of her
 * equipment row (ten slots) that the attach manager (game+0x3326DC0: map +32, rows +64, 48 bytes, parent +4) says hangs
 * on her. 0.1.2 looked for units sharing an item's root and found the weapons' entity units but not what is drawn. */
static uintptr_t UnitObject(uint32_t unit)
{
    if (!exe || !unit) return 0;
    uintptr_t registry = Pointer(exe + 0x1A100F0), generations, objects, object;
    uint32_t count, id, slot = unit & 0x3FFFFF;
    uint8_t generation;
    if (!registry || !ReadAt(registry + 0x98, &count, 4) || count > 0x400000 || slot >= count ||
        !(generations = Pointer(registry + 0xA0)) || !ReadAt(generations + slot, &generation, 1) || generation != (uint8_t)(unit >> 22) ||
        !(objects = Pointer(registry + 0x88)) || !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit)
        return 0;
    return object;
}
static void HerUnits(void)
{
    uint32_t me = atomic_load(&g_avatar), ids[256], skip[10], i;
    int n = 0, nskip = 0, head = 0;
    g_hud.nwanted = 0;
    if (!g_linksOk || !me) return;
    uint32_t mine = UnitOf(me);
    if (!mine || !UnitObject(mine)) return;
    ids[n++] = mine;
    uintptr_t m = Pointer(game + 53634872u), recs, attach = Pointer(game + 0x3326DC0), rows = attach ? Pointer(attach + 64) : 0;
    uint8_t rec[48];
    if (m && rows && Lookup(m + 40, me, 1048576, &i) && i < 0x10000 && (recs = Pointer(m + 80)) && ReadAt(recs + 48u * i, rec, 48))
        for (int k = 0; k < 10; k++)
        {
            uint32_t e = Word(rec + 4u * k), ai, parent = 0, u;
            if (!e || e == UINT32_MAX || !Lookup(attach + 32, e, 8192, &ai) || ai >= 4096 || !ReadAt(rows + 48ull * ai + 4, &parent, 4) || parent != me) continue;
            if (!(u = UnitOf(e)) || !UnitObject(u)) continue;
            if (s_hideGear) { if (n < 256) ids[n++] = u; }
            else skip[nskip++] = u;
        }
    while (head < n)
    {
        uintptr_t child = Pointer(UnitObject(ids[head++]) + 0x1D8);
        for (int guard = 0; child && guard < 256; guard++)
        {
            uint32_t id = 0;
            if (!ReadAt(child + 8, &id, 4) || UnitObject(id) != child) break;
            int seen = 0;
            for (int k = 0; k < n && !seen; k++) seen = ids[k] == id;
            for (int k = 0; k < nskip && !seen; k++) seen = skip[k] == id;
            if (!seen && n < 256) ids[n++] = id;
            child = Pointer(child + 0x1E0);
        }
    }
    memcpy(g_hud.wanted, ids, 4u * (unsigned)n);
    g_hud.nwanted = (uint32_t)n;
    static int told = -1;
    if (n != told) { static unsigned times; if (times++ < 12) Log("her units by the engine's links: %d (her Helldiver's, its parts and %s)", n, s_hideGear ? "her gear" : "no gear"); told = n; }
}
HF_MOD_EXPORT AscalonHud* ascalon_hud_state(void)
{
    int st = atomic_load(&g_state);
    g_hud.in_mission = (uint32_t)(g_inMission && g_alive && kb_kit_on);
    g_hud.state = (uint32_t)st;
    g_hud.reveal_left = st == REVEALED && !g_sensing ? (float)fmax(0, g_sensedUntil - g_now) : 0;
    g_hud.sensing = (uint32_t)g_sensing;
    g_hud.charges = (uint32_t)g_charges;
    g_hud.charges_max = (uint32_t)s_pursuitCharges;
    g_hud.charge_left = g_charges < s_pursuitCharges ? (float)fmax(0, s_pursuitEvery - (g_now - g_chargeAt)) : 0;
    g_hud.cloak_visual = (uint32_t)s_cloakVisual;
    g_hud.hide_gear = (uint32_t)s_hideGear;
    return &g_hud;
}

/* ---- the frame -------------------------------------------------------------------------------------------------- */
static void Tick(HfMod* mod, float dt, void* user)
{
    g_now += dt > 0 && dt < 1 ? dt : 0;
    int inMission = hf->game_state(self) == HF_STATE_MISSION;
    if (inMission != g_inMission) { Reset(inMission ? "mission start" : "mission end"); g_inMission = inMission; }
    if (!g_inMission || !kb_kit_on) { atomic_store(&g_state, AWAY); return; }
    MyItems();
    int st = StateNow(), was = atomic_exchange(&g_state, st);
    if (st != was)
    {
        static unsigned told;
        if (told++ < 60) Log("she is %s", kStateName[st]);
        if (st == REVEALED) g_reveals++;
    }
    VisCut(st == HIDDEN || st == REVEALED);   /* the cut stays while revealed: sight alone must not hold her revealed */
    if ((st == HIDDEN || st == REVEALED) && g_now >= g_forgetAt)
    {
        g_forgetAt = g_now + s_forgetEvery;
        ForgetPass();
    }
    static double unitsAt;
    if (g_now >= unitsAt) { unitsAt = g_now + 0.25; HerUnits(); }
    g_hud.units[0] = UnitOf(atomic_load(&g_avatar));
    for (int k = 0; k < 4; k++) g_hud.units[1 + k] = UnitOf(atomic_load(&g_myItems[k]));
    AreaFrame();
    Exhaustion();
    atomic_exchange(&g_ownStopped, 0);
    atomic_exchange(&g_kdStopped, 0);
}

/* ---- Mod Options Menu ------------------------------------------------------------------------------------------- */
static const KbOption kOptions[] = {
    {"area_radius", &s_areaRadius, NULL}, {"area_mult", &s_areaMult, NULL}, {"area_max", NULL, &s_areaMax}, {"pursuit_mult", &s_pursuitMult, NULL},
    {"pursuit_every", &s_pursuitEvery, NULL}, {"pursuit_slow", &s_pursuitSlow, NULL}, {"pursuit_charges", NULL, &s_pursuitCharges},
    {"visual_cut", &s_visualCut, NULL}, {"reveal_hold", &s_revealHold, NULL}, {"noise_window", &s_noiseWindow, NULL}, {"heard_hold", &s_heardHold, NULL}, {"dodge", &s_dodge, NULL},
    {"gear_rule", NULL, &s_gearRule}, {"cloak_visual", NULL, &s_cloakVisual}, {"hide_gear", NULL, &s_hideGear},
    {"hp_mult", &s_hpMult, NULL}, {"stamina_mult", &s_staminaMult, NULL},
};
HF_MOD_EXPORT int ascalon_option_get(const char* name, double* out) { return OptionGet(name, out); }
HF_MOD_EXPORT int ascalon_option_set(const char* name, double value) { return OptionSet(name, value); }

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    RosterJoin("ascalon", "Ascalon");
    kb_ini = "ascalon.ini";
    kb_key_names[0] = "key_spare";
    kb_key_names[1] = "key_endowment";
    kb_key_names[2] = "key_descent";
    KB_SETTINGS(kSettings);
    KB_OPTIONS(kOptions);
    kb_damage_taken = DamageTaken;
    kb_damage_dealt = DamageDealt;
    kb_hit_taken = HitTaken;
    /* her body: Arknights rules, no T-Doll Hellbomb */
    s_hpMult = 1.0f;
    s_staminaMult = 1.2f;
    s_hellbomb = 0;
    if (!api || api->version < 13 || api->size < offsetof(HfApi, code_matches) + sizeof api->code_matches) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    LoadSettings();
    api->log(mod, "Ascalon %s: hidden until she makes noise; her hits strike everyone within %.0f m; Pursuit", VERSION, (double)s_areaRadius);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) &&
             api->subscribe(mod, HF_ON_WEAPON, OnWeaponAsc, NULL, 0) && api->subscribe(mod, HF_ON_STAT, OnStat, NULL, 0) &&
             api->on_game_thread(mod, Tick, NULL);
    if (!ok) api->log(mod, "events or the game thread are unavailable: Ascalon stays off");
    return 0;
}
