/* Veterancy System: the M-1000 Maxigun (maxigun.c).
 *
 * Its own kills this mission level it (mx_level2_kills, mx_level3_kills; force_level too):
 *   Level 2, Veteran: a fire-rate choice in its weapon menu (mx_rate_low and mx_rate_mid; the MG-43's rate function,
 *     weapon_function_right = 2), a quicker spin-up and a longer spin-down, mx_recoil times the recoil, firing on the
 *     move (stationary_while_firing off), and a backpack of mx_l2_pack rounds (a supply box refills half).
 *   Level 3, Elite: mx_rate_high joins the menu, the backpack holds mx_l3_pack rounds, and Old Painless: after
 *     mx_ap_seconds of continuous fire its rounds pierce at AP mx_ap (its damage row 127, the Maxigun's alone) until it
 *     stops firing.
 * The gun's fields are read when a Maxigun is made, so a level reaches the next one called in (the backpack's size
 * reaches the one worn as well). Each level earned therefore refunds its call-in (mx_refund): the local player's
 * Maxigun stratagem is made ready at once by writing its cooldown end in the player's stratagem record, a layout from
 * HD2Runtime's research (docs/research/free-call-in.md), checked against three pins of game code first (needs
 * code_matches, SDK 13; without it, or on a mismatch, there is no refund and the log says so). Only the local player's
 * own record is written; the Maxigun's cooldown is individual (cooldown type 0), so teammates' are not touched.
 * A backpack's ammo travels as the network field type deposit_value, 10 bits (at most 1023): packs above that widen it
 * to 14 bits (up to 16383), as HellForge's machine-gun-10k example does (every player needs the same edit; the mod is a
 * gameplay mod, so HellForge's mod sync sees to it). */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hellforge_mod.h"
#include "hellforge_components.h"
#include "hellforge_content.h"
#include "field_promotions.h"

#define GUN HF_ENTITY_M1000_MAXIGUN
#define PACK HF_ENTITY_MAXIGUN_BACKPACK
#define DAMAGE 127u          /* its bullet's damage row (80 / 18, AP 3): the Maxigun's alone */
#define BURST_GAP 0.6        /* seconds without a shot that end a burst */

static const HfApi* g_hf;
static HfMod* g_mod;
static int g_kills, g_level = 1, g_applied = 1;  /* g_applied: the level whose edits are on (1: none) */
static double g_lastShot = -100, g_burstStart = -100;
static int g_apOn;
static unsigned g_apBursts;

/* settings */
/* inigen:begin */
static int s_mxL2 = 60, s_mxL3 = 200, s_refund = 1, s_walk = 1;
static float s_rateLow = 2000, s_rateMid = 4500, s_rateHigh = 6000;
static float s_spinUp = 0.2f, s_spinDown = 1.5f, s_recoil = 0.6f;
static int s_l2Pack = 3000, s_l3Pack = 6000;
static float s_apSeconds = 2;
static int s_ap = 4;
/* inigen:end */

int mx_setting(const char* key, double v)
{
#define CL(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
    if (!strcmp(key, "mx_level2_kills")) s_mxL2 = (int)CL(v, 0, 100000);
    else if (!strcmp(key, "mx_level3_kills")) s_mxL3 = (int)CL(v, 0, 100000);
    else if (!strcmp(key, "mx_refund")) s_refund = v != 0;
    else if (!strcmp(key, "mx_rate_low")) s_rateLow = (float)CL(v, 100, 10000);
    else if (!strcmp(key, "mx_rate_mid")) s_rateMid = (float)CL(v, 100, 10000);
    else if (!strcmp(key, "mx_rate_high")) s_rateHigh = (float)CL(v, 100, 10000);
    else if (!strcmp(key, "mx_spin_up")) s_spinUp = (float)CL(v, 0, 5);
    else if (!strcmp(key, "mx_spin_down")) s_spinDown = (float)CL(v, 0, 10);
    else if (!strcmp(key, "mx_recoil")) s_recoil = (float)CL(v, 0, 2);
    else if (!strcmp(key, "mx_walk_while_firing")) s_walk = v != 0;
    else if (!strcmp(key, "mx_l2_pack")) s_l2Pack = (int)CL(v, 1, 16383);
    else if (!strcmp(key, "mx_l3_pack")) s_l3Pack = (int)CL(v, 1, 16383);
    else if (!strcmp(key, "mx_ap_seconds")) s_apSeconds = (float)CL(v, 0, 30);
    else if (!strcmp(key, "mx_ap")) s_ap = (int)CL(v, 0, 10);
    else return 0;
    return 1;
#undef CL
}

/* inigen:begin */
void mx_write_defaults(FILE* f)
{
    fprintf(f, "\n; M-1000 Maxigun: its kills this mission for level 2 (Veteran) and level 3 (Elite).\nmx_level2_kills = %d\nmx_level3_kills = %d\n"
               "; A level reaches the next Maxigun called in (its backpack's size reaches the one worn too). 1: each level earned\n"
               "; makes its call-in ready at once, so a fresh one can be called; 0: off.\nmx_refund = %d\n"
               "; Its fire rates, rounds a minute, chosen in its weapon menu: level 2 offers low and mid (mid as it arrives),\n"
               "; level 3 all three.\nmx_rate_low = %g\nmx_rate_mid = %g\nmx_rate_high = %g\n"
               "; Level 2: spin-up and spin-down seconds (the game's: 0.5 and 0.5), recoil times the game's, and 1: it fires on\n"
               "; the move (0: the game's stand-still).\nmx_spin_up = %g\nmx_spin_down = %g\nmx_recoil = %g\nmx_walk_while_firing = %d\n"
               "; Its backpack's rounds at level 2 and level 3 (the game's: 1000; at most 16383); a supply box refills half.\n"
               "mx_l2_pack = %d\nmx_l3_pack = %d\n"
               "; Level 3, Old Painless: after this many seconds of continuous fire its rounds pierce at this armour penetration\n"
               "; (the game's: 3; 4: heavy) until it stops firing.\nmx_ap_seconds = %g\nmx_ap = %d\n",
        s_mxL2, s_mxL3, s_refund, (double)s_rateLow, (double)s_rateMid, (double)s_rateHigh, (double)s_spinUp, (double)s_spinDown,
        (double)s_recoil, s_walk, s_l2Pack, s_l3Pack, (double)s_apSeconds, s_ap);
}
/* inigen:end */

static int Level(void)
{
    if (fp_forced_level()) return fp_forced_level();
    return g_kills >= s_mxL3 ? 3 : g_kills >= s_mxL2 ? 2 : 1;
}
int mx_level(void) { return Level(); }

/* ---- the gun and its backpack ------------------------------------------------------------------------------------ */
static const HfField kRate[3] = {HF_FIELD_AT(ProjectileWeapon, fire_rate, 0), HF_FIELD_AT(ProjectileWeapon, fire_rate, 1),
    HF_FIELD_AT(ProjectileWeapon, fire_rate, 2)};
static const HfField kMenu = HF_FIELD(WeaponData, weapon_function_right);
static const HfField kSpinUp = HF_FIELD(WeaponWindUp, wind_up_seconds), kSpinDown = HF_FIELD(WeaponWindUp, wind_down_seconds);
static const HfField kStationary = HF_FIELD(WeaponData, stationary_while_firing);
static const HfField kRecoil[6] = {HF_FIELD(WeaponData, recoil_drift_horizontal), HF_FIELD(WeaponData, recoil_drift_vertical),
    HF_FIELD(WeaponData, recoil_climb_horizontal), HF_FIELD(WeaponData, recoil_climb_vertical),
    HF_FIELD(WeaponData, recoil_multiplier_horizontal), HF_FIELD(WeaponData, recoil_multiplier_vertical)};
static const HfField kPack[3] = {HF_FIELD(Deposit, capacity), HF_FIELD(Deposit, start_amount), HF_FIELD(Deposit, refill_amount)};

static int Set(uint64_t ref, const HfField* f, double x)
{
    HfValue value = {f->type, x, 0};
    return g_hf->set_live(g_mod, ref, f, &value, 0) != 0;
}
static double Base(uint64_t ref, const HfField* f)
{
    HfLiveValue live;
    memset(&live, 0, sizeof live);
    return g_hf->get_live(g_mod, ref, f, &live) ? live.base.number : NAN;
}
static void Undo(void)
{
    if (g_applied == 1) return;
    for (int i = 0; i < 3; i++) g_hf->set_live(g_mod, GUN, &kRate[i], NULL, 0);
    g_hf->set_live(g_mod, GUN, &kMenu, NULL, 0);
    g_hf->set_live(g_mod, GUN, &kSpinUp, NULL, 0);
    g_hf->set_live(g_mod, GUN, &kSpinDown, NULL, 0);
    g_hf->set_live(g_mod, GUN, &kStationary, NULL, 0);
    for (int i = 0; i < 6; i++) g_hf->set_live(g_mod, GUN, &kRecoil[i], NULL, 0);
    for (int i = 0; i < 3; i++) g_hf->set_live(g_mod, PACK, &kPack[i], NULL, 0);
    g_applied = 1;
    g_hf->log(g_mod, "Maxigun: back to the game's own");
}
static void Apply(int level)
{
    if (level < 2) { Undo(); return; }
    if (level == g_applied) return;
    int ok = 0, n = 0;
    float rates[3] = {s_rateLow, s_rateMid, level >= 3 ? s_rateHigh : s_rateMid};
    for (int i = 0; i < 3; i++, n++) ok += Set(GUN, &kRate[i], rates[i]);
    ok += Set(GUN, &kMenu, 2), n++;
    ok += Set(GUN, &kSpinUp, s_spinUp), n++;
    ok += Set(GUN, &kSpinDown, s_spinDown), n++;
    if (s_walk) ok += Set(GUN, &kStationary, 0), n++;
    if (s_recoil != 1.0f)
        for (int i = 0; i < 6; i++)
        {
            double base = Base(GUN, &kRecoil[i]);
            if (!isnan(base)) ok += Set(GUN, &kRecoil[i], base * s_recoil), n++;
        }
    int pack = level >= 3 ? s_l3Pack : s_l2Pack;
    ok += Set(PACK, &kPack[0], pack), n++;
    ok += Set(PACK, &kPack[1], pack), n++;
    ok += Set(PACK, &kPack[2], pack / 2), n++;
    g_applied = level;
    g_hf->log(g_mod, "Maxigun level %d: rates %.0f / %.0f / %.0f rpm in its menu, spin %.1f / %.1f s, recoil x%.2f%s, backpack %d "
        "(%d of %d changes taken; a Maxigun called in from now on)", level, (double)rates[0], (double)rates[1], (double)rates[2],
        (double)s_spinUp, (double)s_spinDown, (double)s_recoil, s_walk ? ", fires on the move" : "", pack, ok, n);
}

/* ---- Old Painless: armour penetration after a long burst (level 3) ----------------------------------------------- */
static void SetAp(int on)
{
    if (on == g_apOn) return;
    g_apOn = on;
    for (int i = 0; i < 3; i++)  /* direct, slight and large angle; the extreme angle stays the game's */
    {
        HfField f = HF_FIELD_AT(DamageInfo, armor_penetration, i);
        if (!on) { g_hf->set_live(g_mod, DAMAGE, &f, NULL, HF_LIVE_SHARED); continue; }
        double base = Base(DAMAGE, &f);
        if (isnan(base) || base >= s_ap) continue;
        HfValue v = HF_U32(s_ap);
        g_hf->set_live(g_mod, DAMAGE, &f, &v, HF_LIVE_SHARED);
    }
    if (on && g_apBursts++ < 5) g_hf->log(g_mod, "Maxigun Old Painless: AP %d after %.1f s of fire", s_ap, (double)s_apSeconds);
}
void mx_shots(uint64_t resource, int added)
{
    if (resource != GUN || added <= 0) return;
    double now = fp_clock();
    if (now - g_lastShot > BURST_GAP) g_burstStart = now;
    g_lastShot = now;
}
void mx_tick(double now)
{
    int firing = now - g_lastShot <= BURST_GAP;
    if (g_apOn && (!firing || Level() < 3)) SetAp(0);
    else if (!g_apOn && firing && Level() >= 3 && fp_in_mission() && now - g_burstStart >= s_apSeconds) SetAp(1);
}

/* ---- the call-in refund ------------------------------------------------------------------------------------------
 * The local player's stratagem record (HD2Runtime's research, all [STATIC]; tested in game on build 25480438 with the
 * Maxigun Test mod): records at [game.dll+0x347CE50], 0x1690 bytes each, count at +0x2D200, peer id at +0; the stratagem
 * state at +0x38, its entries at +0x188 (0x30 bytes each, count at +0x788, at most 16): type +0, cooldown end +0x18
 * (u64 microseconds of the game clock [[game.dll+0x3326348]+0x18]). Ready once its end is not past the clock. */
#define REC_LIST 0x347CE50u
#define REC_COUNT 0x2D200u
#define REC_SIZE 0x1690u
#define LOCAL_PEER 0x347CEF0u
#define LOCAL_PEER_AT 0xB398u
#define CLOCK 0x3326348u
static uintptr_t g_game;
static int g_pinsOk;
static unsigned g_pinMisses;

static int Pins(void)
{
    if (g_pinsOk) return 1;
    if (g_hf->size <= offsetof(HfApi, code_matches) || !g_hf->code_matches)
    {
        if (!g_pinMisses++) g_hf->log(g_mod, "Maxigun refund: this HellForge has no code_matches (SDK 13): no refunds");
        return 0;
    }
    if (!g_game) g_game = g_hf->module_base("game.dll", NULL);
    static const struct { uintptr_t at; const char* bytes; } kPins[] = {
        {0x1834AE6, "4C 8B 0D 63 83 C4 01"},  /* the HUD reads the records list (mov r9,[rip -> 0x347CE50]) */
        {0x1834B07, "48 05 90 16 00 00"},     /* ... and steps 0x1690 a record */
        {0x66D24A, "48 8B 8E A0 01 00 00"}};  /* the availability check reads an entry's cooldown end */
    for (unsigned i = 0; g_game && i < sizeof kPins / sizeof kPins[0]; i++)
        if (!g_hf->code_matches(g_game + kPins[i].at, kPins[i].bytes))
        {
            if (g_pinMisses++ < 3)
                g_hf->log(g_mod, "Maxigun refund: game.dll+0x%llX is not as researched: no refund", (unsigned long long)kPins[i].at);
            return 0;
        }
    if (!g_game) return 0;
    g_pinsOk = 1;
    return 1;
}
/* 1: its call-in is ready now (refunded, or ready already) */
static int Refund(void)
{
    uintptr_t recs = 0, peerObj = 0, clock = 0;
    uint64_t me = 0, now = 0;
    uint32_t n = 0;
    if (!Pins()) return 0;
    if (g_hf->read(g_game + REC_LIST, &recs, 8) || !recs || g_hf->read(g_game + LOCAL_PEER, &peerObj, 8) || !peerObj ||
        g_hf->read(peerObj + LOCAL_PEER_AT, &me, 8) || g_hf->read(recs + REC_COUNT, &n, 4) || n > 32 ||
        g_hf->read(g_game + CLOCK, &clock, 8) || !clock || g_hf->read(clock + 0x18, &now, 8))
    {
        g_hf->log(g_mod, "Maxigun refund: the stratagem record is not readable");
        return 0;
    }
    for (uint32_t r = 0; r < n; r++)
    {
        uintptr_t rec = recs + (uintptr_t)r * REC_SIZE, st = rec + 0x38;
        uint64_t peer = 0;
        uint32_t count = 0;
        if (g_hf->read(rec, &peer, 8) || peer != me) continue;
        if (g_hf->read(st + 0x788, &count, 4) || count > 16) break;
        for (uint32_t i = 0; i < count; i++)
        {
            uintptr_t e = st + 0x188 + (uintptr_t)i * 0x30;
            uint32_t type = 0;
            uint64_t end = 0;
            if (g_hf->read(e, &type, 4) || type != HF_STRATAGEM_M1000_MAXIGUN || g_hf->read(e + 0x18, &end, 8)) continue;
            if ((int64_t)(end - now) <= 0) { g_hf->log(g_mod, "Maxigun refund: its call-in is ready already"); return 1; }
            if (g_hf->write(e + 0x18, &now, 8)) { g_hf->log(g_mod, "Maxigun refund: the write failed"); return 0; }
            g_hf->log(g_mod, "Maxigun refund: call-in ready now (%.0f s of cooldown skipped)", (double)(int64_t)(end - now) / 1e6);
            return 1;
        }
        break;
    }
    g_hf->log(g_mod, "Maxigun refund: no Maxigun in your stratagems (a teammate's?)");
    return 0;
}

/* ---- levels ------------------------------------------------------------------------------------------------------- */
static void CheckLevel(void)
{
    int level = Level();
    if (level == g_level) return;
    int up = level > g_level;
    g_level = level;
    g_hf->log(g_mod, "M-1000 Maxigun: level %d (%d kills this mission)", level, g_kills);
    if (fp_in_mission()) Apply(level);
    if (up)
    {
        /* the HUD shows its "call it in" box from this line: "is ready" when the call-in was refunded */
        static const char* kReady[2] = {"Veteran kit approved. Your Maxigun call-in is ready: bring in a fresh one.",
            "Elite kit approved. Your Maxigun call-in is ready: Old Painless is cleared for use."};
        static const char* kLater[2] = {"Veteran kit approved. Call in a fresh Maxigun to collect it.",
            "Elite kit approved. Call in a fresh Maxigun: Old Painless is cleared for use."};
        int ready = s_refund && !fp_forced_level() && fp_in_mission() && Refund();
        fp_toast_line("M-1000 Maxigun", level, (ready ? kReady : kLater)[level >= 3]);
    }
}
void mx_kills(uint64_t resource, int added)
{
    if (resource != GUN || added <= 0) return;
    g_kills += added;
    CheckLevel();
}
void mx_reset(void)
{
    g_kills = 0;
    SetAp(0);
    Undo();
    g_lastShot = g_burstStart = -100;
    g_level = Level();
    if (fp_in_mission()) Apply(g_level);  /* a forced level from the mission's start */
}

/* The backpack's ammo count travels as deposit_value (10 bits): widen it for packs above 1023. */
static void NetworkReady(HfMod* mod, HfEvent event, void* user)
{
    int most = s_l2Pack > s_l3Pack ? s_l2Pack : s_l3Pack;
    if (most <= 1023) return;
    HfFieldType expect = {1, 10, 0};
    if (g_hf->set_network_field_bits(g_mod, "deposit_value", expect, 14))
        g_hf->log(g_mod, "Maxigun: the backpack count could not be widened (%s): packs stop at 1023",
            g_hf->last_log ? g_hf->last_log(g_mod) : "");
    else g_hf->log(g_mod, "Maxigun: backpack count widened to 14 bits (up to 16383)");
}

int mx_init(const HfApi* hf, HfMod* mod)
{
    g_hf = hf;
    g_mod = mod;
    hf->on(mod, HF_EVENT_NETWORK_READY, NetworkReady, NULL);
    hf->log(mod, "M-1000 Maxigun: level 2 at %d kills, level 3 at %d; rates %.0f / %.0f (+%.0f at level 3), backpack %d / %d; "
        "Old Painless AP %d after %.1f s; call-in refund %s", s_mxL2, s_mxL3, (double)s_rateLow, (double)s_rateMid, (double)s_rateHigh,
        s_l2Pack, s_l3Pack, s_ap, (double)s_apSeconds, s_refund ? "on" : "off");
    return 1;
}
