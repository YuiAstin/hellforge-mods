/* Exusiai (yui.exusiai): Exusiai's kit from Arknights as abilities on keys, for your own Helldiver (solo or host). The
 * first kit built on the kit base (kitbase/, docs/kit-base.md).
 *
 *   Fast Cartridge (passive): her guns fire fire_rate times faster (set when she equips each one).
 *   Angel's Blessings (passive): hp_mult times a Helldiver's health, and her shots deal bless_mult times their damage.
 *   Charging Mode (passive): every ch_every bursts she fires charge it; the next burst sets off ch_free more bursts of
 *     her gun's own rounds along her aim, for free (no ammunition). A burst is a group of her rounds with gaps under
 *     ch_gap s, or her gun's burst size (WeaponData burst_rounds; 3 without one) in full-auto fire. Her rounds are
 *     counted from the magazine the HUD reads (hd2.local_player():ammo()) and passes in (exusiai_ammo).
 *   Shooting Mode (key_guns, default X; "the summoned guns" until 0.3.1): for s2_seconds two guns hover at her shoulders; each time she lands a shot,
 *     each fires at what she hit (at most once every gun_every s).
 *   Overloading Mode (key_overload, default C): for s3_seconds four more guns hover behind her, each choosing its own
 *     target within s3_range m by its own priority (the nearest, the most heavily armoured, whoever last hit her, the
 *     weakest) and firing every s3_every s.
 *
 * Each gun fires rounds of her own for what it hunts (copies of vanilla rounds, add_projectile, spawned from its muzzle
 * and credited to her): the pair the R-63CS Diligence Counter Sniper's (medium penetration, for whatever she hits); the
 * (pair_primary 1: her primary's own round instead, while it fires one that can be spawned); the nearest-target gun a shotgun's pellets (the SG-225 Breaker's); the armour hunter heavy penetration (the APW-1
 * Anti-Materiel Rifle's); her attacker's gun the JAR-5 Dominator's (it staggers); the finisher the AR-23 Liberator's,
 * quick. A round that cannot be spawned falls back to the Guard Dog's (gun_round, 7). While any gun is out her shots do
 * not hurt her allies (no_friendly_fire). The HUD shows each gun as the Guard Dog's own gun model riding a hidden Guard
 * Dog body (Gun Probe 0.1.9's way, docs/research/game-notes.md), turned toward what it aims at (exusiai_guns).
 *
 * Research credit: the entity, scene-graph and hit reads are the kit base's (Helldiver Survival Rework by
 * enochlophobianumerophobia, MIT; HD2Runtime by SkyeShade); the Guard Dog round's type is from HD2Runtime's attack
 * catalogue. See THIRD_PARTY_NOTICES.md. */
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

#define VERSION "0.3.1"

#include <stdatomic.h>
/* rounds the mod itself spawned since the shot counter was last read (her guns', her free bursts'): the game counts them
 * as her shots too (0.3.1's first build: with the Sickle each free burst charged the next, 133 in one mission) */
static atomic_int g_ownRounds;
static double g_ownAt = -100;
#define BUILD "25480438"

#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"
#include "kitbase/kb_body.h"
#include "kitbase/kb_hits.h"
#include "kitbase/kb_damage.h"
#include "kitbase/kb_settings.h"
#include "kitbase/kb_roster.h"
#include "weapon_sounds.h"

/* with other character kits installed: the HUD's roster and picker calls (kitbase/kb_roster.h) */
KB_ROSTER_EXPORTS(exusiai)

/* ---- settings ------------------------------------------------------------------------------------------------- */
static float s_fireRate = 1.15f, s_blessMult = 1.08f;
static int s_chEvery = 4, s_chFree = 3;
static float s_chGap = 0.2f, s_chSpread = 0.6f;
static float s_s2Cd = 40, s_s2Seconds = 15, s_gunEvery = 0.3f;
static float s_s3Cd = 60, s_s3Seconds = 15, s_s3Every = 0.35f, s_s3Range = 60;
static int s_gunRound = 7, s_noFriendlyFire = 1;
/* her guns' own rounds (copies of vanilla ones, add_projectile): the summoned pair, then Overloading Mode's four */
static int s_rBase[5] = {241, 179, 226,   /* R-63CS Diligence Counter Sniper, SG-225 Breaker, APW-1 Anti-Materiel Rifle */
                         177, 276};   /* JAR-5 Dominator, AR-23 Liberator */
static float s_rEvery[4] = {0, 0, 0, 0};   /* Overloading Mode's: the nearest, the armoured, her attacker, the weakest (0.2.3: 0 = its gun's own rate) */
/* 0.2.3: the guns whose rounds Overloading Mode's four fire, for their fire rate and pellets */
static const uint64_t kRoleGun[4] = {HF_ENTITY_SG225_BREAKER, HF_ENTITY_APW1_ANTIMATERIEL_RIFLE, HF_ENTITY_JAR5_DOMINATOR, HF_ENTITY_AR23_LIBERATOR};
static float s_pairHold = 0.6f;
static int s_pairRate = 1;
static int s_pellets = 6, s_pairPrimary = 1;
static float s_pelletSpread = 5;
static float s_maxRounds = 30, s_roundSpeed = 300;

/* Every setting: its .ini / Mod Options Menu name, variable and range (kitbase/kb_settings.h) */
static const KbSetting kSettings[] = {
    KB_F("fire_rate", s_fireRate, 1, 3), KB_F("bless_mult", s_blessMult, 1, 3),
    KB_I("ch_every", s_chEvery, 1, 20), KB_I("ch_free", s_chFree, 0, 10), KB_F("ch_gap", s_chGap, 0.05, 1), KB_F("ch_spread", s_chSpread, 0, 5),
    KB_F("s2_cooldown", s_s2Cd, 0, 600), KB_F("s2_seconds", s_s2Seconds, 1, 120), KB_F("gun_every", s_gunEvery, 0.05, 5), KB_I("pair_rate", s_pairRate, 0, 1), KB_F("pair_hold", s_pairHold, 0.1, 5),
    KB_F("s3_cooldown", s_s3Cd, 0, 600), KB_F("s3_seconds", s_s3Seconds, 1, 120), KB_F("s3_every", s_s3Every, 0.05, 5), KB_F("s3_range", s_s3Range, 5, 200),
    KB_I("gun_round", s_gunRound, 1, 100000), KB_I("no_friendly_fire", s_noFriendlyFire, 0, 1),
    KB_I("round_pair", s_rBase[0], 1, 350), KB_I("round_nearest", s_rBase[1], 1, 350), KB_I("round_armoured", s_rBase[2], 1, 350),
    KB_I("round_attacker", s_rBase[3], 1, 350), KB_I("round_weakest", s_rBase[4], 1, 350),
    KB_F("every_nearest", s_rEvery[0], 0, 10), KB_F("every_armoured", s_rEvery[1], 0, 10), KB_F("every_attacker", s_rEvery[2], 0, 10),
    KB_F("every_weakest", s_rEvery[3], 0, 10), KB_I("pellets", s_pellets, 1, 16), KB_I("pair_primary", s_pairPrimary, 0, 1), KB_F("pellet_spread", s_pelletSpread, 0, 30), KB_F("max_rounds", s_maxRounds, 1, 300), KB_F("round_speed", s_roundSpeed, 0, 3000),
    KB_F("hp_mult", s_hpMult, 0.1, 10), KB_F("stamina_mult", s_staminaMult, 0.1, 20), KB_I("hellbomb_on_exhaustion", s_hellbomb, 0, 1),
    KB_I("hellbomb_explosion", s_hellbombRow, 0, 100000), KB_F("hellbomb_hold", s_hellbombHold, 0, 10),
};

/* The default exusiai.ini: tools/inigen.py writes the file from this text (the mod never writes it). Her body's values
 * are written as numbers here: the kit base's defaults are the T-Dolls', and hers are set in hellforge_mod_init. */
static __attribute__((unused)) void DefaultIni(FILE* f)
{
    fprintf(f, "; Exusiai %s. Keys: a letter, digit, F1-F24, Mouse3, Mouse4 or Mouse5, with Shift+, Ctrl+ or Alt+ in front for a\n"
               "; combo (Shift+Q). The game still sees the keys too (Shift sprints, R reloads), so pick ones it leaves alone.\n"
               "key_charge = %s\nkey_guns = %s\nkey_overload = %s\n"
               "; (key_charge: Charging Mode is passive now; the key only says so.)\n"
               "; Fast Cartridge: her guns fire this many times faster (from when she next equips each one).\nfire_rate = %g\n"
               "; Angel's Blessings: her shots deal this times their damage (her health: hp_mult, below).\nbless_mult = %g\n"
               "; Charging Mode: every ch_every bursts she fires, the next sets off ch_free more bursts of her gun's rounds for free.\n"
               "; ch_gap: a pause longer than this (s) ends a burst; ch_spread: the free rounds' spread (degrees).\n"
               "ch_every = %d\nch_free = %d\nch_gap = %g\nch_spread = %g\n"
               "; Shooting Mode: cooldown; for s2_seconds two guns beside her fire at what she last hit while she keeps hitting it\n"
               "; (her last hit within pair_hold s). pair_rate 1: each at her primary's fire rate (the two alternate); 0: once\n"
               "; every gun_every s.\ns2_cooldown = %g\ns2_seconds = %g\ngun_every = %g\npair_rate = %d\npair_hold = %g\n"
               "; Overloading Mode: cooldown; for s3_seconds four more guns each pick their own target within s3_range m (the\n"
               "; nearest, the most heavily armoured, whoever last hit her, the weakest), each firing every every_<its target> s\n"
               "; (0: at the fire rate of the gun its round comes from).\n"
               "; (s3_every: no longer used.)\n"
               "s3_cooldown = %g\ns3_seconds = %g\ns3_every = %g\ns3_range = %g\n"
               "; Her guns' own rounds: copies of these vanilla projectiles (types 1-350). round_pair: Shooting Mode's pair (241: the\n"
               "; R-63CS Diligence Counter Sniper's, medium armour penetration, for whatever she hits); round_nearest: a shotgun\n"
               "; (179: the SG-225 Breaker's, pellets of them pellet_spread degrees wide); round_armoured: heavy penetration (226:\n"
               "; the APW-1 Anti-Materiel Rifle's); round_attacker: one that staggers (177: the JAR-5 Dominator's); round_weakest:\n"
               "; quick rounds to finish it (276: the AR-23 Liberator's).\n"
               "round_pair = %d\nround_nearest = %d\nround_armoured = %d\nround_attacker = %d\nround_weakest = %d\n"
               "every_nearest = %g\nevery_armoured = %g\nevery_attacker = %g\nevery_weakest = %g\npellets = %d\npellet_spread = %g\n"
               "; max_rounds: the most projectiles her guns spawn a second, in all; round_speed: m/s of her guns' own rounds (her\n"
               "; copies; 0: the base's): slower, a round is seen from its gun.\nmax_rounds = %g\nround_speed = %g\n"
               "; pair_primary 1: Shooting Mode's pair fire her primary's own round (round_pair when it fires none or it cannot be\n"
               "; spawned).\npair_primary = %d\n"
               "; gun_round: the round used when one of hers cannot be spawned (7: the AX/AR-23 Guard Dog's). no_friendly_fire 1:\n"
               "; while a gun is out her shots spare her allies.\n"
               "gun_round = %d\nno_friendly_fire = %d\n"
               "; Her body: hp_mult times a Helldiver's health, stamina_mult times her sprint. hellbomb_on_exhaustion 1 sets off a\n"
               "; Hellbomb on her if she runs her stamina dry (a T-Doll's price; off for her).\n"
               "hp_mult = 1.13\nstamina_mult = 1\nhellbomb_on_exhaustion = 0\n",
            VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, (double)s_fireRate, (double)s_blessMult, s_chEvery, s_chFree,
            (double)s_chGap, (double)s_chSpread, (double)s_s2Cd, (double)s_s2Seconds, (double)s_gunEvery, s_pairRate, (double)s_pairHold, (double)s_s3Cd,
            (double)s_s3Seconds, (double)s_s3Every, (double)s_s3Range, s_rBase[0], s_rBase[1], s_rBase[2], s_rBase[3], s_rBase[4],
            (double)s_rEvery[0], (double)s_rEvery[1], (double)s_rEvery[2], (double)s_rEvery[3], s_pellets, (double)s_pelletSpread, (double)s_maxRounds, (double)s_roundSpeed, s_pairPrimary, s_gunRound, s_noFriendlyFire);
}

/* ---- state ---------------------------------------------------------------------------------------------------- */
static double g_cd[3];                  /* ready at: (Charging Mode: unused), the guns, Overloading Mode */
static double g_s2Until, g_s3Until;
static unsigned g_casts[3], g_shots[2], g_spared, g_tunedGuns;

/* who hit her last (the hit hook, another thread): Overloading Mode's third gun goes after them */
static atomic_uint g_threat;
static _Atomic unsigned long long g_threatMs;

/* ---- her health reader (as Faelynn's) -------------------------------------------------------------------------- */
static int HealthOf(uint32_t entity, int32_t* health, uint32_t* life)
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
        uint8_t r[416];
        if (!ReadAt(records + 440u * index, r, sizeof r)) return 0;
        *health = (int32_t)Word(r + 20);
        *life = Word(r + 412);
        return 1;
    }
    return 0;
}
/* an enemy's main armour (its Health record +0x118), kept per resource */
static int ArmourOf(uint64_t res)
{
    static uint64_t keys[128];
    static int8_t vals[128];
    unsigned h = (unsigned)(res ^ (res >> 29)) & 127;
    for (unsigned i = 0; i < 128; i++)
    {
        unsigned k = (h + i) & 127;
        if (keys[k] == res) return vals[k];
        if (keys[k]) continue;
        HfField f = HF_FIELD(Health, armor_118);
        HfLiveValue v;
        memset(&v, 0, sizeof v);
        int a = hf->get_live(self, res, &f, &v) ? (int)v.base.number : 0;
        vals[k] = (int8_t)(a < 0 ? 0 : a > 10 ? 10 : a);
        keys[k] = res;
        return vals[k];
    }
    return 0;
}

/* ---- the guns -------------------------------------------------------------------------------------------------- */
/* where each hovers, from her head along her aim: right, up, forward (m). 0-1: the summoned pair at her shoulders;
 * 2-5: Overloading Mode's four, in an arc behind and above her. */
static const float kGunAt[6][3] = {{-1.15f, 0.45f, 0.2f}, {1.15f, 0.45f, 0.2f},   /* 0.2.3: wider (0.55 m sat on her shoulders) */
                                   {-1.8f, 0.8f, -0.4f}, {1.8f, 0.8f, -0.4f}, {-0.95f, 1.5f, -0.6f}, {0.95f, 1.5f, -0.6f}};
static const char* const kPriority[4] = {"the nearest", "the most heavily armoured", "whoever last hit her", "the weakest"};
static double g_gunNext[6];
static uint32_t g_gunTarget[6];
static double g_gunPickAt[6], g_gunSince[6], g_gunFirst[6];
static int32_t g_gunHp[6];
/* her shots' marks (where they landed): S2's guns fire at the next ones on the game thread */
typedef struct Shot { float p[3]; uint32_t target; } Shot;
static Shot g_assist[8];
static int g_nassist;
/* 0.2.3: the pair's target: what she last hit, and when */
static uint32_t g_pairTarget;
static float g_pairPos[3];
static double g_pairAt = -100, g_pairSince = -100;
/* the guns' own shots lately (where they were aimed): a hit there is a gun's, not hers (no assist, no charge) */
static float g_gunAim[16][3];
static double g_gunAimAt[16];
static unsigned g_gunAimNext;

static int GunPos(int i, float out[3])
{
    float pos[3], aim[3];
    if (!PlayerPose(pos, aim)) return 0;
    float h = sqrtf(aim[0] * aim[0] + aim[1] * aim[1]);
    float f[2] = {h > 0.05f ? aim[0] / h : 1, h > 0.05f ? aim[1] / h : 0}, r[2] = {f[1], -f[0]};
    out[0] = pos[0] + r[0] * kGunAt[i][0] + f[0] * kGunAt[i][2];
    out[1] = pos[1] + r[1] * kGunAt[i][0] + f[1] * kGunAt[i][2];
    out[2] = pos[2] + 1.5f + kGunAt[i][1];
    return 1;
}
/* her rounds by role (0: the pair, 1-4: Overloading Mode's guns): her copy, else the vanilla round, else gun_round */
static const char* const kRole[5] = {"pair", "nearest", "armoured", "attacker", "weakest"};
static uint32_t g_round[5], g_roundBad[5];
/* 0.2.8: at most max_rounds projectiles a second in all (two game crashes in rendering came in fights with the guns at
 * the real fire rates and a shotgun's whole shot: thousands of spawned rounds a mission); her rounds' speed (her copies
 * only, round_speed m/s, 0: the base's): at 800-1000 m/s a round is first seen metres out from its gun */
/* 0.2.15 (Yui: a gun sometimes refuses to shoot): one bucket for all let the shotgun gun (11 pellets, 300 rpm) drain it
 * and the others then fired nothing. Now each gun out has its own share of max_rounds. */
static double g_projTok[6], g_projAt[6];
static int ProjBudget(int i)
{
    int out = (g_now < g_s2Until ? 2 : 0) + (g_now < g_s3Until ? 4 : 0);
    double rate = s_maxRounds / (out > 0 ? out : 1), cap = rate < 2 ? 2 : rate;
    double el = g_now - g_projAt[i];
    g_projAt[i] = g_now;
    if (el > 0) g_projTok[i] += el * rate;
    if (g_projTok[i] > cap) g_projTok[i] = cap;
    return (int)g_projTok[i];
}
static uint8_t g_speedSet[5];
static void RoundSpeed(int r)
{
    if (r < 0 || r > 4 || g_speedSet[r] || s_roundSpeed <= 0 || !g_round[r] || g_round[r] == (uint32_t)s_rBase[r] || !hf->set_live) return;
    g_speedSet[r] = 1;
    HfField f = HF_FIELD(ProjectileSettings, velocity);
    HfValue v = HF_F32(s_roundSpeed);
    HfLiveValue was;
    memset(&was, 0, sizeof was);
    if (hf->get_live) hf->get_live(self, (HfRef)g_round[r], &f, &was);
    if (was.base.number > 0 && was.base.number <= s_roundSpeed) return;   /* a round already this slow stays as it is */
    int ok = hf->set_live(self, (HfRef)g_round[r], &f, &v, 0);
    Log("her round %u (projectile %u): %.0f m/s -> %.0f (%s)", r, g_round[r], was.base.number, (double)s_roundSpeed, ok ? "set" : "refused");
}
static void RoundsAdd(void)
{
    static char names[5][48];
    for (int r = 0; r < 5; r++)
    {
        HfProjectile p;
        memset(&p, 0, sizeof p);
        p.size = sizeof p;
        p.base = (uint32_t)s_rBase[r];
        snprintf(names[r], sizeof names[r], "yui/exusiai/round_%s", kRole[r]);
        p.name = names[r];
        g_round[r] = hf->add_projectile ? hf->add_projectile(self, &p) : 0;
        if (!g_round[r]) { g_round[r] = p.base; Log("her %s round: no copy of projectile %u (the vanilla round fires)", kRole[r], p.base); }
    }
}
typedef struct GunInfo { uint64_t res; uint32_t projectile, burst; float rpm; } GunInfo;
static const GunInfo* InfoOf(uint64_t res);
static uint32_t InfoProjectile(const GunInfo* g);
static int NoSpawn(uint32_t type);
static void NoSpawnAdd(uint32_t type);
static uint64_t g_primaryRes;      /* her primary (the weapon event, selection 1) */
static uint32_t PrimaryRound(void)
{
    if (!s_pairPrimary || !g_primaryRes) return 0;
    uint32_t t = InfoProjectile(InfoOf(g_primaryRes));
    return t && !NoSpawn(t) ? t : 0;
}
/* a projectile's pellets per shot (its settings row +0x1C): rows sit in one list by type, so a vanilla row is found from
 * one of hers (projectile_row) and checked by its +0 type; 0: unknown */
static uintptr_t g_rowAnchor;
static uint32_t g_rowAnchorType;
static uint32_t PelletsOf(uint32_t type)
{
    /* 0.2.3: the row's pellet count as the game reads it (the list-offset read below found her copies only) */
    if (type)
    {
        HfField f = HF_FIELD(ProjectileSettings, pellet_count);
        HfLiveValue v;
        memset(&v, 0, sizeof v);
        if (hf->get_live && hf->get_live(self, (HfRef)type, &f, &v) && v.base.number >= 1 && v.base.number <= 32) return (uint32_t)v.base.number;
    }
    if (!g_rowAnchor || !type || type > 1000) return 0;
    uintptr_t row = g_rowAnchor + ((intptr_t)type - (intptr_t)g_rowAnchorType) * 272;
    uint32_t t = 0, n = 0;
    if (!ReadAt(row, &t, 4) || t != type || !ReadAt(row + 0x1C, &n, 4)) return 0;
    return n >= 1 && n <= 32 ? n : 0;
}
static void ProjectilesReady(HfMod* mod, HfEvent event, void* user)
{
    for (int r = 0; r < 5 && !g_rowAnchor; r++)
        if (g_round[r] != (uint32_t)s_rBase[r] && hf->projectile_row && (g_rowAnchor = hf->projectile_row(g_round[r]))) g_rowAnchorType = g_round[r];
    if (!g_rowAnchor) { Log("pellets: no row of hers to read the list by (a shotgun primary's pair fire one pellet a shot)"); return; }
    Log("pellets: the SG-225 Breaker's round %u: %u a shot (her copy %u: %u)", (unsigned)s_rBase[1], PelletsOf((uint32_t)s_rBase[1]), g_round[1], PelletsOf(g_round[1]));
}
static uint32_t RoundOf(int i)
{
    int r = i < 2 ? 0 : i - 1;
    uint32_t prim = r == 0 ? PrimaryRound() : 0;
    if (prim) return prim;
    return g_roundBad[r] ? (uint32_t)s_gunRound : g_round[r];
}
static float g_gunLook[6][3];      /* where each gun aims (its model turns there) */
static double g_gunShotAt[6];
static int Fire(int i, const float at[3])
{
    float from[3], d[3];
    if (!GunPos(i, from)) return 0;
    for (int k = 0; k < 3; k++) d[k] = at[k] - from[k];
    float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(n > 0.5f)) return 0;
    for (int k = 0; k < 3; k++) d[k] /= n;
    memcpy(g_gunLook[i], at, 12);
    int role = i < 2 ? 0 : i - 1, pellets = role == 1 ? s_pellets : 1;
    uint32_t round = RoundOf(i);
    if (role == 1) { uint32_t pn = PelletsOf(round); if (pn > 1) pellets = (int)pn; }   /* 0.2.3: the shotgun's own pellet count */
    if (role == 0 && round == PrimaryRound())
    {
        uint32_t n = PelletsOf(round);   /* a shotgun primary: its whole shot, each pellet its own round */
        if (n > 1) pellets = (int)n;
        static uint32_t toldFor;
        if (toldFor != round) { toldFor = round; Log("the pair fire her primary's round %u, %d a shot", round, pellets); }
    }
    for (int k = 0; k < 3; k++) from[k] += d[k] * 0.4f;   /* from its muzzle */
    int r = 0;
    RoundSpeed(role);
    {
        int budget = ProjBudget(i);
        if (budget < 1) return 0;
        if (pellets > budget) pellets = budget;
        g_projTok[i] -= pellets;
    }
    for (int p = 0; p < pellets; p++)
    {
        float dir[3];
        memcpy(dir, d, 12);
        if (pellets > 1)
        {
            float sp = s_pelletSpread * 0.0174533f;
            for (int k = 0; k < 3; k++) dir[k] += sp * ((float)rand() / RAND_MAX - 0.5f);
            float m = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
            for (int k = 0; k < 3; k++) dir[k] /= m;
        }
        r = hf->spawn_projectile(self, round, from, dir);
        atomic_fetch_add(&g_ownRounds, 1);
        if (r == HF_ACT_WAITING) return 0;
        if (!r && role == 0 && round == PrimaryRound())
        {
            NoSpawnAdd(round);
            Log("her primary's round (projectile %u) cannot be spawned here: the pair fire projectile %u (round_pair) instead", round, g_round[0]);
            return 0;
        }
        if (!r && round != (uint32_t)s_gunRound)
        {
            g_roundBad[role] = 1;
            Log("her %s round (projectile %u) cannot be spawned here: that gun fires projectile %d (gun_round) instead", kRole[role], round, s_gunRound);
            return 0;
        }
        if (!Act("a gun's shot", r)) return 0;
    }
    g_gunShotAt[i] = g_now;
    /* 0.2.5: its gun's firing sound, played by the HUD side (exusiai_sounds) */
    ShotSound(role == 0 ? (round == PrimaryRound() && g_primaryRes ? g_primaryRes : HF_ENTITY_R63CS_DILIGENCE_COUNTER_SNIPER) : kRoleGun[role - 1], from);
    unsigned slot = g_gunAimNext++ % 16;
    memcpy(g_gunAim[slot], at, sizeof g_gunAim[slot]);
    g_gunAimAt[slot] = g_now;
    g_shots[i >= 2]++;
    return 1;
}
static int GunsHit(const float p[3])
{
    for (int k = 0; k < 16; k++) if (g_gunAimAt[k] > 0 && g_now - g_gunAimAt[k] < 1.5 && Dist2(p, g_gunAim[k]) < 2.5f * 2.5f) return 1;
    return 0;
}
/* 0.2.13: the life states seen on enemies as they died (the kill event). 0.2.12 took any state but 0 as dead, and her
 * Overloading Mode guns then found nothing to shoot: living enemies with another state were left out. Now a state is
 * dead only once a kill has shown it (a dead Illuminate keeps health on its record, 0.2.12's corpse) */
static uint32_t g_deadLife[8];
static int g_ndeadLife;
static int DeadLife(uint32_t life)
{
    for (int i = 0; i < g_ndeadLife; i++) if (g_deadLife[i] == life) return 1;
    return 0;
}
/* 0.2.14: the kill event comes while the victim still has its living state (1 on every enemy): 0.2.13 learned 1 as
 * dead at the first kill and her guns then shot nothing until the game restarted. Now the state is read again a second
 * after the kill, and only a state the victim changed to is learned as dead. */
#define PENDKILL 16
static struct { uint32_t e, life0; double at; } g_pendKill[PENDKILL];
static unsigned g_pendKillN;
static void LearnDeadLife(uint32_t e)
{
    int32_t hp = 0;
    uint32_t life = 0;
    if (!HealthOf(e, &hp, &life)) return;
    unsigned i = g_pendKillN++ % PENDKILL;
    g_pendKill[i].e = e; g_pendKill[i].life0 = life; g_pendKill[i].at = g_now + 1.0;
}
static void PendingKills(void)
{
    for (unsigned i = 0; i < PENDKILL; i++)
    {
        if (!g_pendKill[i].e || g_now < g_pendKill[i].at) continue;
        uint32_t e = g_pendKill[i].e, life0 = g_pendKill[i].life0, life = 0;
        int32_t hp = 0;
        g_pendKill[i].e = 0;
        if (!HealthOf(e, &hp, &life)) continue;   /* gone already */
        if (!life || life == life0)
        {
            static unsigned told;
            if (told++ < 3) Log("a kill left life state %u as it was a second later (health %d): not taken as dead", life, hp);
            continue;
        }
        if (DeadLife(life) || g_ndeadLife >= 8) continue;
        g_deadLife[g_ndeadLife++] = life;
        Log("a kill changed life state %u -> %u (health %d left): an enemy in state %u is dead to her guns", life0, life, hp, life);
    }
}
static int Living(uint32_t e, int32_t* hp)
{
    uint32_t life = 0;
    if (!HealthOf(e, hp, &life) || *hp <= 0) return 0;
    if (life && !DeadLife(life))
    {
        static uint32_t seen[8];
        static int nseen;
        int k = 0;
        while (k < nseen && seen[k] != life) k++;
        if (k == nseen && nseen < 8) { seen[nseen++] = life; Log("an enemy with health %d has life state %u: counted living (no kill has shown it)", *hp, life); }
    }
    return !life || !DeadLife(life);
}
/* 0.2.4: the middle of a unit's body: the centre of its scene-graph nodes' box (a Bile Titan's root is on the ground
 * between its legs, its body 6 m up), kept a second per enemy */
#define BODIES 256
static struct { uint32_t unit; double at; float off[3]; } g_body[BODIES];
static int CmpF(const void* x, const void* y) { float a = *(const float*)x, b = *(const float*)y; return a < b ? -1 : a > b; }
static int BodyOffset(uint32_t unit, const float root[3], float off[3])
{
    unsigned h = (unit * 2654435761u) % BODIES;
    if (g_body[h].unit == unit && g_now - g_body[h].at < 1.0) { memcpy(off, g_body[h].off, 12); return 1; }
    uintptr_t registry = Pointer(exe + 0x1A100F0), objects, object, nodes;
    uint32_t count = 0, id, slot = unit & 0x3FFFFF;
    static float m[256 * 16];
    if (!registry || !(objects = Pointer(registry + 0x88)) || !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit ||
        !ReadAt(object + 0x70, &count, 4) || !count || count > 4096 || !(nodes = Pointer(object + 0x88)))
        return 0;
    if (count > 256) count = 256;
    if (!ReadAt(nodes, m, (size_t)count * 64)) return 0;
    /* the median of its nodes (the box's centre overshot small enemies: a few nodes far up, a weapon, an effect) */
    static float v[3][256];
    int used = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        const float* t = m + 16 * i + 12;
        if (!isfinite(t[0]) || !isfinite(t[1]) || !isfinite(t[2]) || fabsf(t[0] - root[0]) > 20 || fabsf(t[1] - root[1]) > 20 || t[2] < root[2] - 2 || t[2] > root[2] + 20) continue;
        for (int k = 0; k < 3; k++) v[k][used] = t[k] - root[k];
        used++;
    }
    if (used < 2) return 0;
    for (int k = 0; k < 3; k++) { qsort(v[k], used, sizeof(float), CmpF); off[k] = v[k][used / 2]; }
    /* (Yui, 2026-11-11: the guns shot the floor under an Illuminate spawner) a tall thing whose nodes cluster at its
     * foot (a structure: most of them on the ground, its body metres up): half its height instead (its 90th-percentile
     * node, so a stray effect node far up does not count) */
    {
        float top = v[2][(used * 9) / 10];
        if (top > 3.0f && off[2] < 0.35f * top) off[2] = 0.5f * top;
    }
    if (off[2] < 0.3f) off[2] = 0.3f;
    g_body[h].unit = unit; g_body[h].at = g_now; memcpy(g_body[h].off, off, 12);
    return 1;
}
static int TargetPos(uint32_t e, uint64_t res, float out[3])
{
    uint8_t d[24];
    if (!Descriptor(e, d) || Quad(d) != res || !UnitNode(Word(d + 12), 0, out)) return 0;
    float off[3];
    if (BodyOffset(Word(d + 12), out, off)) { for (int k = 0; k < 3; k++) out[k] += off[k]; }   /* its body's middle */
    else out[2] += 1.0f;   /* its body, not its feet */
    return 1;
}
/* Overloading Mode's gun i (2-5) picks by its own priority */
static uint32_t UnitOf(uint32_t e) { uint8_t d[24]; return Descriptor(e, d) ? Word(d + 12) : 0; }
/* ---- line of sight (0.2.6; Koleda's, after Lemuen 0.1.6's and Alva's: the game's raycast, skipping her, leaves, bodies and the target) ---- */
typedef uint32_t (*WorldFn)(const void*);
typedef uint32_t (*RayFn)(uint32_t, const void*, const void*, float, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t);
static WorldFn g_worldId;
static RayFn g_raycast;
static uint32_t ActorFilter(uint32_t id, uint32_t unit)
{
    uint32_t world = id >> 30, layout, stride, key_offset, data_offset, key, index, name = 0;
    uintptr_t pool = exe + 0x2369B00 + 64u * ((id >> 28 & 3) + 10 * world);
    uint8_t header[56], record[40], body[160];
    if (!ReadAt(pool, header, 56)) return 0;
    layout = Word(header + 28); stride = layout & 65535; key_offset = layout >> 16 & 255; data_offset = layout >> 24;
    index = id & Word(header + 40);
    if (index >= Word(header + 36) || !(id & Word(header + 52)) || stride < 40 || stride > 512 || key_offset + 4 > stride || data_offset + 40 > stride) return 0;
    uintptr_t entry = (uintptr_t)Quad(header) + (uintptr_t)index * stride;
    if (!ReadAt(entry + key_offset, &key, 4) || key != id || !ReadAt(entry + data_offset, record, 40) || Word(record + 12) != unit || !(Word(record + 16) & 1)) return 0;
    uintptr_t physics = Pointer(exe + 0x27BA8A8 + 176u * world), bodies = physics ? Pointer(physics + 24) : 0;
    uint32_t body_index = Word(record + 20) & 0xFFFFFF;
    if (!bodies || body_index >= 262144 || !ReadAt(bodies + 160u * body_index, body, 160) || Word(body + 144) != id || Word(body + 148) != unit) return 0;
    uintptr_t filters = Pointer(exe + 0x27C5E48), names;
    uint32_t count, filter = Word(body + 108) & 127;
    if (!filters || !ReadAt(filters + 248, &count, 4) || count > 128 || filter >= count || !(names = Pointer(filters + 256)) || !ReadAt(names + 4u * filter, &name, 4)) return 0;
    return name;
}
#define FILTER_FOLIAGE 0x04A8FBF9u
static uint32_t g_foeUnit[FOES];
static int g_nfoeUnit;
static double g_foeUnitAt = -1;
static int FoeUnit(uint32_t u)
{
    if (g_now - g_foeUnitAt > 0.25 || g_foeUnitAt < 0)
    {
        g_foeUnitAt = g_now;
        g_nfoeUnit = 0;
        for (int k = 0; k < FOES; k++) if (g_foe[k]) { uint32_t fu = UnitOf(g_foe[k]); if (fu) g_foeUnit[g_nfoeUnit++] = fu; }
    }
    for (int i = 0; i < g_nfoeUnit; i++) if (g_foeUnit[i] == u) return 1;
    return 0;
}
static int WorldId(uint32_t* id)
{
    if (!g_raycast || !g_worldId) return 0;
    uintptr_t world = Pointer(game + 0x346BFA0);
    if (!world) return 0;
    *id = g_worldId((const void*)world);
    return *id < 4;
}
/* the first wall along a ray (len + 1: clear); *hitTarget when the target's unit is met first; skip: a second unit to
 * see through (the Sinner) */
static float FirstWall(uint32_t id, const float from[3], const float dir[3], float len, uint32_t me, uint32_t tu, uint32_t skip, int* hitTarget)
{
    uint8_t hits[16 * 44];
    memset(hits, 0, sizeof hits);
    uint32_t n = g_raycast(id, from, dir, len, 2, 1, 0, 0x80000009u, me, hits, 16);
    if (n > 16) n = 16;
    float best = len + 1;
    int target = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        float d;
        memcpy(&d, hits + 44u * i + 24, 4);
        uint32_t hu = Word(hits + 44u * i + 28);
        if (!isfinite(d) || d < 1.2f || d >= best || (hu && (hu == me || hu == skip))) continue;
        if (hu && tu && hu == tu) { best = d; target = 1; continue; }
        uint32_t filt = ActorFilter(Word(hits + 44u * i + 32), hu);
        if (filt == FILTER_FOLIAGE || (hu && (filt == 0x236122F0u || filt == 0xD5B33D56u || FoeUnit(hu)))) continue;
        best = d; target = 0;
    }
    if (hitTarget) *hitTarget = target;
    return best;
}
/* a clear shot from a gun to a target (no wall between; its own body or another enemy's does not count) */
static int ClearShot(const float from[3], const float q[3], uint32_t target)
{
    uint32_t id;
    if (!WorldId(&id) || !QueryIdle()) return 1;
    float d[3] = {q[0] - from[0], q[1] - from[1], q[2] - from[2]};
    float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(n > 1.5f)) return 1;
    for (int k = 0; k < 3; k++) d[k] /= n;
    int hit = 0;
    float w = FirstWall(id, from, d, n, UnitOf(atomic_load(&g_avatar)), UnitOf(target), 0, &hit);
    return hit || w > n - 1.0f;
}
/* the camera (Alva's View: her unit's world, its camera matrix at +0x230920; Apache - Rise Above's read) and where its
 * ray first meets a wall, within 300 m (else 300 m out) */
static int CamView(float pos[3], float fwd[3])
{
    uint8_t d[24];
    if (!Descriptor(atomic_load(&g_avatar), d)) return 0;
    uint32_t unit = Word(d + 12), slot = unit & 0x3FFFFF, id = 0;
    uintptr_t registry = Pointer(exe + 0x1A100F0), objects, object, w;
    float m[16];
    if (!registry || !(objects = Pointer(registry + 0x88)) || !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit ||
        !(w = Pointer(object + 0x18)) || !ReadAt(w + 0x230920, m, 64)) return 0;
    for (int k = 0; k < 16; k++) if (!isfinite(m[k])) return 0;
    if (fabsf(m[15] - 1) > 1e-3f) return 0;
    memcpy(fwd, m + 4, 12);
    memcpy(pos, m + 12, 12);
    float n = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    if (!(n > 0.5f)) return 0;
    for (int k = 0; k < 3; k++) fwd[k] /= n;
    return 1;
}
static int ReticlePoint(float out[3])
{
    float cam[3], fwd[3];
    if (!CamView(cam, fwd)) return 0;
    float len = 300;
    uint32_t id;
    if (WorldId(&id) && QueryIdle())
    {
        float w = FirstWall(id, cam, fwd, 300, UnitOf(atomic_load(&g_avatar)), 0, 0, NULL);
        if (w < len) len = w;
    }
    for (int k = 0; k < 3; k++) out[k] = cam[k] + fwd[k] * len;
    return 1;
}
static int RootPos(uint32_t e, uint64_t res, float out[3])
{
    uint8_t d[24];
    if (!Descriptor(e, d) || Quad(d) != res || !UnitNode(Word(d + 12), 0, out)) return 0;
    out[2] += 1.0f;
    return 1;
}
/* 0.2.15 (Yui: her guns sometimes shoot dead bodies): a target a gun has fired at for 2 s without its health changing
 * (a body whose dead state no kill has shown yet, or something her rounds cannot hurt) is left alone for 15 s */
/* 0.3.1 (ElectroDasher: Overloading Mode would not shoot War Striders; his log: "took 2 s of a gun's fire with its health
 * unchanged: left alone"): a walker's health sits in its parts, so its own number does not move while the guns take it
 * apart. A gun's hit that deals damage on a target (the damage event) now counts as hurting it. */
static struct { uint32_t e; double at; } g_hurt[32];
static unsigned g_hurtN;
static void Hurt(uint32_t e)
{
    for (int k = 0; k < 32; k++) if (g_hurt[k].e == e) { g_hurt[k].at = g_now; return; }
    unsigned k = g_hurtN++ % 32;
    g_hurt[k].e = e; g_hurt[k].at = g_now;
}
static int HurtLately(uint32_t e)
{
    for (int k = 0; k < 32; k++) if (g_hurt[k].e == e && g_now - g_hurt[k].at < 2.0) return 1;
    return 0;
}
static struct { uint32_t e; double until; } g_shun[32];
static unsigned g_shunN;
static int Shunned(uint32_t e)
{
    for (int k = 0; k < 32; k++) if (g_shun[k].e == e && g_now < g_shun[k].until) return 1;
    return 0;
}
static void Shun(uint32_t e)
{
    unsigned k = g_shunN++ % 32;
    g_shun[k].e = e; g_shun[k].until = g_now + 15;
    static unsigned told;
    if (told++ < 6) Log("entity %u took 2 s of a gun's fire with its health unchanged: left alone for 15 s (a body, or beyond her rounds)", e);
}
static uint32_t Pick(int i, float at[3])
{
    float me[3], q[3], from[3];
    if (!PlayerPose(me, NULL)) return 0;
    if (!GunPos(i, from)) memcpy(from, me, 12);
    int prio = i - 2;
    uint32_t threat = atomic_load(&g_threat);
    int threatFresh = threat && GetTickCount64() - atomic_load(&g_threatMs) < 10000;
    /* 0.2.6: the candidates by score, then the best one with a clear shot from the gun (the first 5 tried) */
    /* 0.2.15 (Yui: laggy in her ult): scored by its root's position (one small read); the body's middle (a read of all
     * its nodes) only for the ones tried. Each gun also keeps its target 0.3 s (GunsFrame) */
    struct { uint32_t e; uint64_t res; double s; float q[3]; } c[48];
    int nc = 0;
    for (int k = 0; k < FOES; k++)
    {
        uint32_t e = g_foe[k];
        int32_t hp;
        if (!e || Shunned(e) || !RootPos(e, g_foeRes[k], q)) continue;
        float d2 = Dist2(me, q);
        if (d2 > s_s3Range * s_s3Range || !Living(e, &hp)) continue;
        double sc = -d2;                                                   /* 0: the nearest */
        if (prio == 1) sc = ArmourOf(g_foeRes[k]) * 1e6 - d2;              /* 1: the most armour, then the nearest */
        else if (prio == 2) sc = (threatFresh && e == threat ? 1e9 : 0) - d2;   /* 2: her attacker, else the nearest */
        else if (prio == 3) sc = -(double)hp * 1e4 - d2;                   /* 3: the least health */
        int j;
        if (nc == 48) { if (sc <= c[47].s) continue; j = 47; }
        else j = nc++;
        while (j > 0 && c[j - 1].s < sc) { c[j] = c[j - 1]; j--; }
        c[j].e = e; c[j].res = g_foeRes[k]; c[j].s = sc; memcpy(c[j].q, q, 12);
    }
    for (int j = 0; j < nc && j < 10; j++)   /* 0.2.15: the first 10 tried (5 left guns idle in cover-heavy fights) */
    {
        if (!TargetPos(c[j].e, c[j].res, c[j].q)) continue;
        if (ClearShot(from, c[j].q, c[j].e)) { memcpy(at, c[j].q, 12); return c[j].e; }
    }
    return 0;
}
static int FoePos(uint32_t e, float out[3])
{
    for (int k = 0; k < FOES; k++) if (g_foe[k] == e) return TargetPos(e, g_foeRes[k], out);
    return 0;
}
static double RoleEvery(int role)
{
    if (s_rEvery[role] > 0) return s_rEvery[role];
    const GunInfo* gi = InfoOf(kRoleGun[role]);
    double every = gi && gi->rpm > 0 ? 60.0 / gi->rpm : 0.5;
    return every < 0.05 ? 0.05 : every;
}
static void GunsFrame(void)
{
    int s2 = g_now < g_s2Until, s3 = g_now < g_s3Until;
    /* the summoned pair: at what she hit */
    g_nassist = 0;
    {
        int32_t php;
        if (g_pairTarget && !Living(g_pairTarget, &php)) { g_pairTarget = 0; g_pairAt = -100; }   /* 0.2.12: it died: they stop */
    }
    if (s2 && g_now - g_pairAt <= s_pairHold)
    {
        const GunInfo* gi = s_pairRate && g_primaryRes ? InfoOf(g_primaryRes) : NULL;
        double every = gi && gi->rpm > 0 ? 60.0 / gi->rpm : s_gunEvery;
        if (every < 0.05) every = 0.05;
        if (g_pairSince < 0) { g_pairSince = g_now; g_gunNext[0] = g_now; g_gunNext[1] = g_now + every / 2; }   /* they alternate */
        float at[3];
        memcpy(at, g_pairPos, 12);
        if (g_pairTarget) FoePos(g_pairTarget, at);   /* follows it */
        for (int i = 0; i < 2; i++)
            if (g_now >= g_gunNext[i] && Fire(i, at)) g_gunNext[i] = (g_gunNext[i] + every > g_now ? g_gunNext[i] : g_now) + every;
        static unsigned told;
        if (!told++) Log("the pair: each fires every %.3f s (%s)", every, gi ? "her primary's rate" : "gun_every");
    }
    else g_pairSince = -1;
    if (!s3) return;
    for (int i = 2; i < 6; i++)
    {
        if (g_now < g_gunNext[i]) continue;
        g_gunNext[i] = g_now + RoleEvery(i - 2);
        float at[3];
        uint32_t t = 0;
        int32_t hp = 0;
        /* 0.2.15: its target kept 0.3 s while it lives (a full pick, with its raycasts, for every shot was the lag) */
        if (g_gunTarget[i] && g_now < g_gunPickAt[i] + 0.3 && Living(g_gunTarget[i], &hp) && FoePos(g_gunTarget[i], at)) t = g_gunTarget[i];
        else
        {
            t = Pick(i, at);
            g_gunPickAt[i] = g_now;
            if (!t) g_gunNext[i] = g_now + 0.15;   /* nothing in sight: look again soon, not a whole shot later */
        }
        if (t != g_gunTarget[i])
        {
            g_gunTarget[i] = t;
            g_gunSince[i] = g_gunFirst[i] = g_now;
            g_gunHp[i] = t && Living(t, &hp) ? hp : 0;
            static unsigned told;
            if (t && told++ < 8) Log("Overloading Mode: gun %d (%s) on entity %u", i - 1, kPriority[i - 2], t);
        }
        else if (t && Living(t, &hp))
        {
            if (hp != g_gunHp[i]) { g_gunHp[i] = hp; g_gunSince[i] = g_gunFirst[i] = g_now; }
            else if (HurtLately(t) && g_now - g_gunFirst[i] < 20) g_gunSince[i] = g_now;   /* damage that never moves its health: 20 s at most (a body) */
            else if (g_now - g_gunSince[i] > 2.0) { Shun(t); g_gunTarget[i] = 0; continue; }
        }
        if (t) Fire(i, at);
    }
}

/* ---- the skills ------------------------------------------------------------------------------------------------- */
/* ---- Charging Mode: her bursts ---------------------------------------------------------------------------------- */
/* her gun in hand (the weapon event) and what its rounds are: its projectile type, burst size and fire rate */
static uint32_t g_heldEntity;
static uint64_t g_heldRes;
static GunInfo g_gunInfo[24];
static int g_ngunInfo;
static const GunInfo* InfoOf(uint64_t res)
{
    for (int i = 0; i < g_ngunInfo; i++) if (g_gunInfo[i].res == res) return &g_gunInfo[i];
    if (!res || g_ngunInfo >= 24) return NULL;
    GunInfo* g = &g_gunInfo[g_ngunInfo++];
    memset(g, 0, sizeof *g);
    g->res = res;
    HfField proj = HF_FIELD(ProjectileWeapon, projectile), rate = {HfProjectileWeapon_INFO, 0x08, 4, HF_TYPE_F32, __LINE__},
            burst = HF_FIELD(WeaponData, burst_rounds), entity = HF_FIELD(ProjectileWeapon, projectile_entity);
    HfLiveValue v;
    memset(&v, 0, sizeof v);
    if (hf->get_live(self, res, &proj, &v) && v.base.number > 0) g->projectile = (uint32_t)v.base.number;
    memset(&v, 0, sizeof v);
    if (hf->get_live(self, res, &entity, &v) && v.base.number != 0) g->projectile = 0;   /* it fires an entity per shot */
    memset(&v, 0, sizeof v);
    g->rpm = hf->get_live(self, res, &rate, &v) && v.base.number > 0 ? (float)v.base.number : 600;
    memset(&v, 0, sizeof v);
    g->burst = hf->get_live(self, res, &burst, &v) && v.base.number >= 2 && v.base.number <= 10 ? (uint32_t)v.base.number : 3;
    Log("Charging Mode: weapon %016llx fires projectile %u, %.0f rpm, bursts of %u%s", (unsigned long long)res, g->projectile, (double)g->rpm,
        g->burst, g->projectile ? "" : " (no projectile: no free bursts from it)");
    return g;
}
/* the HUD's report of the gun in hand: its entity and the rounds in its magazine (-1: unknown) */
static atomic_uint g_ammoEntity;
static atomic_int g_ammoRounds = -1;
static _Atomic unsigned long long g_ammoMs;
HF_MOD_EXPORT int exusiai_ammo(unsigned version, uint32_t entity, int rounds)
{
    if (version != 1) return 0;
    static int first = 1;
    if (first) { first = 0; Log("Charging Mode: the HUD reports her magazine"); }
    atomic_store(&g_ammoEntity, entity);
    atomic_store(&g_ammoRounds, rounds);
    atomic_store(&g_ammoMs, GetTickCount64());
    return 1;
}
/* 0.3.1 (ElectroDasher's report: only the Railgun charged it): a gun that runs on heat (the Sickle, the Talon) has
 * no rounds going down, so her bursts were never counted. The game's own shots counter (HF_ON_STAT) fills in: shots it
 * reports that no magazine drop accounts for within 0.15 s are her rounds. */
static atomic_int g_statShots;
static double g_statAt, g_magAt = -100, g_shotAt = -100;
static void OnStat(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfStatEvent* st = (const HfStatEvent*)info;
    if (st->stat != HF_STAT_SHOTS || st->added <= 0 || !kb_kit_on || !g_alive) return;
    /* only the shots the game credits to the gun in her hands: the rounds the mod spawns (her free bursts, her guns')
     * grow the counter too, late and by their own count, and charged her again (Yui, twice) */
    int mine = 0;
    for (uint32_t k = 0; k < st->count && k < HF_STAT_SOURCES; k++)
        if (g_heldRes && st->sources[k].resource == g_heldRes && st->sources[k].added > 0) mine += st->sources[k].added;
    static unsigned told;
    if (told < 6)
    {
        told++;
        Log("shots +%d (%d with no source; %u sources, the first %016llx +%d): %d by the gun in her hands %016llx", st->added, st->unattributed, st->count,
            (unsigned long long)(st->count ? st->sources[0].resource : 0), st->count ? st->sources[0].added : 0, mine, (unsigned long long)g_heldRes);
    }
    if (mine <= 0) return;
    if (!atomic_fetch_add(&g_statShots, mine > 20 ? 20 : mine)) g_statAt = g_now;
    g_shotAt = g_now;
}
static int g_charge;                 /* bursts charged (0 .. ch_every) */
static int g_groupShots;             /* rounds in the burst under way */
static int g_groupFree;              /* the burst under way is the charged one */
static double g_groupLast;           /* her last round */
static uint32_t g_lastEntity;
static int g_lastRounds = -1;
static unsigned g_bursts, g_freeBursts;
/* the free bursts: rounds left, the next one's time, the gap between rounds and the burst size */
static int g_freeLeft, g_freeInBurst;
static double g_freeNext;
static uint32_t g_freeProjectile, g_freeBurstSize;
static float g_freeStep;
static void CloseBurst(void)
{
    if (g_groupShots <= 0) return;
    g_bursts++;
    if (!g_groupFree && g_charge < s_chEvery) g_charge++;
    g_groupShots = 0;
    g_groupFree = 0;
}
/* rounds HellForge cannot spawn (its catalogue has no packages for them: the 1300 rpm SMG's projectile 61, 0.1.0's test):
 * the free rounds use the guns' round instead */
static uint32_t g_noSpawn[8];
static int NoSpawn(uint32_t type)
{
    for (int k = 0; k < 8; k++) if (g_noSpawn[k] == type) return 1;
    return 0;
}
static void NoSpawnAdd(uint32_t type)
{
    if (NoSpawn(type)) return;
    for (int k = 0; k < 8; k++) if (!g_noSpawn[k]) { g_noSpawn[k] = type; return; }
}
static uint32_t InfoProjectile(const GunInfo* g) { return g ? g->projectile : 0; }
static void StartFree(const GunInfo* gi)
{
    if (s_chFree <= 0) return;
    /* 0.3.1: a gun with no round to copy (a beam) fires the guns' round, three to a burst */
    uint32_t round = gi && gi->projectile && !NoSpawn(gi->projectile) ? gi->projectile : (uint32_t)s_gunRound, burst = gi && gi->burst ? gi->burst : 3;
    g_freeProjectile = round;
    g_freeBurstSize = burst;
    g_freeStep = 60.0f / (gi && gi->rpm > 60 ? gi->rpm : 600);
    g_freeLeft = s_chFree * (int)burst;
    g_freeInBurst = 0;
    g_freeNext = g_now + 0.12;   /* right behind her own burst */
    g_freeBursts++;
    g_casts[0]++;
    static unsigned told;
    if (told++ < 6) Log("Charging Mode: %d free bursts of %u (projectile %u)", s_chFree, burst, round);
}
static void BurstFrame(void)
{
    /* her rounds, from the HUD's magazine report: the same gun, fewer rounds */
    unsigned long long ms = atomic_load(&g_ammoMs);
    uint32_t ent = atomic_load(&g_ammoEntity);
    int rounds = atomic_load(&g_ammoRounds), fired = 0;
    const GunInfo* gi = g_heldEntity && ent == g_heldEntity ? InfoOf(g_heldRes) : NULL;
    if (!ms || GetTickCount64() - ms > 1000 || rounds < 0 || ent != g_lastEntity) { g_lastEntity = ent; g_lastRounds = rounds; }
    else if (rounds < g_lastRounds && g_lastRounds - rounds <= 20)
    {
        fired = g_lastRounds - rounds;
        g_lastRounds = rounds;
        g_magAt = g_now;
        atomic_store(&g_statShots, 0);   /* the magazine accounts for them */
    }
    else g_lastRounds = rounds;   /* a reload, a new magazine */
    if (!fired && atomic_load(&g_statShots) > 0 && g_now - g_statAt >= 0.15)
    {
        int n = atomic_exchange(&g_statShots, 0);
        atomic_store(&g_ownRounds, 0); (void)g_ownAt;
        if (n > 0 && g_now - g_magAt > 0.5)
        {
            fired = n;
            static int told;
            if (!told) { told = 1; Log("Charging Mode: this gun's magazine does not move: her shots are counted from the game's shot counter"); }
        }
    }
    if (fired > 0)
    {
        int burst = gi && gi->burst ? (int)gi->burst : 3;
        if (g_groupShots > 0 && g_now - g_groupLast > s_chGap) CloseBurst();
        for (int k = 0; k < fired; k++)
        {
            if (g_groupShots == 0 && g_charge >= s_chEvery) { g_charge = 0; g_groupFree = 1; StartFree(gi); }
            g_groupShots++;
            if (g_groupShots >= burst) CloseBurst();   /* full auto: every burst-size rounds is one */
        }
        g_groupLast = g_now;
    }
    if (g_groupShots > 0 && g_now - g_groupLast > s_chGap) CloseBurst();
    /* the free rounds: from in front of her toward where her reticle points (0.2.10: the camera's ray, as Alva's aim;
     * her head's facing lagged behind the reticle), a little spread */
    while (g_freeLeft > 0 && g_now >= g_freeNext)
    {
        float pos[3], aim[3];
        if (!PlayerPose(pos, aim)) { g_freeLeft = 0; break; }
        float from[3] = {pos[0] + aim[0] * 0.9f, pos[1] + aim[1] * 0.9f, pos[2] + 1.45f + aim[2] * 0.9f}, dir[3], tgt[3];
        if (ReticlePoint(tgt))
        {
            float v[3] = {tgt[0] - from[0], tgt[1] - from[1], tgt[2] - from[2]};
            float vn = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (vn > 1.0f) for (int k = 0; k < 3; k++) aim[k] = v[k] / vn;
        }
        float sp = s_chSpread * 0.0174533f;
        for (int k = 0; k < 3; k++) dir[k] = aim[k] + sp * ((float)rand() / RAND_MAX - 0.5f);
        float n = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        for (int k = 0; k < 3; k++) dir[k] /= n;
        int r = hf->spawn_projectile(self, g_freeProjectile, from, dir);
        atomic_fetch_add(&g_ownRounds, 1); g_ownAt = g_now;
        if (r == HF_ACT_WAITING) { g_freeNext = g_now + 0.05; break; }
        if (!r && g_freeProjectile != (uint32_t)s_gunRound)
        {
            NoSpawnAdd(g_freeProjectile);
            Log("Charging Mode: projectile %u cannot be spawned here; free rounds use projectile %d (gun_round) instead", g_freeProjectile, s_gunRound);
            g_freeProjectile = (uint32_t)s_gunRound;
            continue;
        }
        Act("a free burst's round", r);
        g_freeLeft--;
        if (++g_freeInBurst >= (int)g_freeBurstSize) { g_freeInBurst = 0; g_freeNext = g_now + 0.18; }
        else g_freeNext = g_now + g_freeStep;
    }
}
static void SummonGuns(void)
{
    if (g_now < g_cd[1]) return;
    g_cd[1] = g_now + s_s2Cd;
    g_s2Until = g_now + s_s2Seconds;
    g_gunNext[0] = g_gunNext[1] = 0;
    g_casts[1]++;
    Log("Shooting Mode: two guns for %.0f s at her shoulders", (double)s_s2Seconds);
}
static void OverloadingMode(void)
{
    if (g_now < g_cd[2]) return;
    g_cd[2] = g_now + s_s3Cd;
    g_s3Until = g_now + s_s3Seconds;
    for (int i = 2; i < 6; i++) { g_gunNext[i] = g_now + 0.1 * (i - 2); g_gunTarget[i] = 0; }
    memset(g_roundBad, 0, sizeof g_roundBad);
    g_casts[2]++;
    Log("Overloading Mode: four more guns for %.0f s", (double)s_s3Seconds);
}

/* ---- Fast Cartridge: her guns' fire rate (each weapon once; from when she next equips it) ---------------------- */
static uint64_t g_tuned[24];
static int g_ntuned;
static void FastCartridge(uint64_t res)
{
    if (!res || res == AVATAR || s_fireRate == 1) return;
    for (int i = 0; i < g_ntuned; i++) if (g_tuned[i] == res) return;
    if (g_ntuned >= 24) return;
    g_tuned[g_ntuned++] = res;
    int n = 0, set = 0;
    double first = 0;
    for (int slot = 0; slot < 3; slot++)
    {
        HfField f = {HfProjectileWeapon_INFO, (uint16_t)(4 + 4 * slot), 4, HF_TYPE_F32, __LINE__};
        HfLiveValue v;
        memset(&v, 0, sizeof v);
        if (!hf->get_live(self, res, &f, &v) || !(v.base.number > 0)) continue;
        if (!n) first = v.base.number;
        n++;
        HfValue x = HF_F32((float)(v.base.number * s_fireRate));
        if (hf->set_live(self, res, &f, &x, 0)) set++;
    }
    if (n) g_tunedGuns++;
    if (g_tunedGuns <= 12) Log("Fast Cartridge: weapon %016llx %s", (unsigned long long)res,
                                n ? "fire rate raised" : "has no projectile fire rate (left alone)");
    if (n && g_tunedGuns <= 12) Log("  %d of %d rate slots set, %.0f -> %.0f rpm", set, n, first, first * s_fireRate);
}
static void OnWeaponExu(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfWeaponEvent* w = (const HfWeaponEvent*)info;
    if (!w->avatar || w->avatar != atomic_load(&g_avatar) || !kb_kit_on) return;
    g_heldEntity = w->entity;
    g_heldRes = w->resource;
    if (w->selection == 1 && w->resource && w->resource != g_primaryRes)
    {
        g_primaryRes = w->resource;
        static unsigned told;
        if (told++ < 6) Log("her primary: weapon %016llx (Shooting Mode's pair fire its round)", (unsigned long long)w->resource);
    }
    FastCartridge(w->resource);
}

/* ---- the damage event: her shots ------------------------------------------------------------------------------- */
static int AnyGun(void) { return g_now < g_s2Until || g_now < g_s3Until; }
/* a friend of hers: another Helldiver, or anything on the kit base's friend list */
static int Friend(uint32_t e)
{
    uint8_t d[24];
    if (!Descriptor(e, d)) return 0;
    uint64_t res = Quad(d);
    return res == AVATAR || FriendRes(res);
}
static void DamageTaken(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    (void)e; (void)d; (void)me;
}
static void DamageDealt(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    if (d->amount <= 0 || !MineHit(d)) return;
    if (s_noFriendlyFire && AnyGun() && Friend(d->target)) { e->cancel = 1; g_spared++; return; }
    if (!IsShot(d))
    {
        /* 0.3.1 (ElectroDasher: the pair did nothing with the Grenade Pistol): a blast of hers (kind 2) within 3 s of a
         * shot of hers is her gun's round going off: the pair fire at whom it hurt. No Blessings, no charge. */
        int32_t thp;
        if (d->kind == 2 && g_now < g_s2Until && g_now - g_shotAt < 3 && !GunsHit(d->position) && !Friend(d->target) && Living(d->target, &thp))
        {
            g_pairTarget = d->target; memcpy(g_pairPos, d->position, 12); g_pairAt = g_now;
            if (g_nassist < 8) { memcpy(g_assist[g_nassist].p, d->position, sizeof d->position); g_assist[g_nassist].target = d->target; g_nassist++; }
        }
        return;
    }
    int gun = GunsHit(d->position);
    double mult = s_blessMult;                                                  /* Angel's Blessings */
    if (mult != 1) ScaleDamage(d, mult);
    if (gun) { Hurt(d->target); return; }
    NoteTarget(d);
    { int32_t thp; if (g_now < g_s2Until && Living(d->target, &thp)) { g_pairTarget = d->target; memcpy(g_pairPos, d->position, 12); g_pairAt = g_now; } }
    if (g_now < g_s2Until && g_nassist < 8)
    {
        memcpy(g_assist[g_nassist].p, d->position, sizeof d->position);
        g_assist[g_nassist].target = d->target;
        g_nassist++;
    }
}
/* a hit on her (the hit hook): who dealt it, for Overloading Mode's third gun */
static int HitTaken(const uint8_t* h, void* result, unsigned long long now)
{
    (void)result;
    uint32_t me = atomic_load(&g_avatar), dealer = Word(h + 0x80), owner = Word(h + 0x100), who = owner && owner != me ? owner : dealer;
    if (who && who != me) { atomic_store(&g_threat, who); atomic_store(&g_threatMs, now); }
    return 0;
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
static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfKillEvent* k = (const HfKillEvent*)info;
    LearnDeadLife(k->victim);
    DropFoe(k->victim);
    if (k->victim == g_lastTarget) g_lastTarget = 0;
}
static void OnPlayer(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    if (!p->local) return;
    if (info->kind == HF_ON_PLAYER_SPAWN)
    {
        if (p->entity && p->entity != atomic_load(&g_avatar)) { atomic_store(&g_avatar, p->entity); g_avatarNet = 0; }
        g_alive = 1;
        LearnNet();
    }
    else
    {
        g_alive = 0;
        g_s2Until = g_s3Until = 0;   /* her guns go with her */
    }
}
static void Reset(const char* why)
{
    if (g_casts[0] || g_casts[1] || g_casts[2])
        Log("%s: %u bursts, %u charged (free bursts), guns %u / Overloading %u, %u assist and %u Overloading shots, %u hits on allies spared",
            why, g_bursts, g_casts[0], g_casts[1], g_casts[2], g_shots[0], g_shots[1], g_spared);
    int start = !strcmp(why, "mission start");
    if (start) KitDecide();   /* with several kits installed: is this one picked? */
    Body(start);
    /* the Guard Dog's package: its rounds fly (and sound) only once that is in */
    if (start && !hf->load_package(self, UINT64_C(0xA14F1A98C80575AF))) Log("the Guard Dog's package could not be loaded ahead: the guns' models and first shots may wait");
    /* 0.2.11: her guns' models (drone_mg and drone_mg_weapon) are not in that one (the Drone Assault Rifle Backpack's):
     * they showed only in matches that also had the AR-23 Liberator's package in. Load the AX/AR-23 Guard Dog's own
     * loadout package and the Liberator's too. */
    if (start && !hf->load_package(self, UINT64_C(0xBE6C260FADCB8719))) Log("the AX/AR-23 Guard Dog's own package could not be loaded: her guns' models may not show");
    if (start && !hf->load_package(self, UINT64_C(0xDBD9381F9EA9788F))) Log("the AR-23 Liberator's package could not be loaded: her guns' models may not show");
    if (start && s_hellbomb && s_hellbombRow == 125 && !hf->load_package(self, UINT64_C(0x346D785D6CA13FF4))) Log("the Hellbomb's package could not be loaded ahead");
    g_boomPending = 0;
    g_boomArmed = 0;
    memset(g_cd, 0, sizeof g_cd);
    memset(g_casts, 0, sizeof g_casts);
    memset(g_shots, 0, sizeof g_shots);
    memset(g_gunNext, 0, sizeof g_gunNext);
    memset(g_gunTarget, 0, sizeof g_gunTarget);
    memset(g_gunAimAt, 0, sizeof g_gunAimAt);
    memset(g_gunShotAt, 0, sizeof g_gunShotAt);
    memset(g_roundBad, 0, sizeof g_roundBad);
    g_spared = 0;
    g_charge = g_groupShots = g_groupFree = 0;
    g_freeLeft = 0;
    g_bursts = g_freeBursts = 0;
    g_lastRounds = -1;
    g_s2Until = g_s3Until = 0;
    g_nassist = 0;
    g_lastTarget = 0;
    atomic_store(&g_threat, 0);
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gameSize = 0, exeSize = 0;
    uintptr_t g = hf->module_base("game.dll", &gameSize), x = hf->module_base("helldivers2.exe", &exeSize);
    if (!g || !x || gameSize != 74727424u || exeSize != 60719104u) { Log("REFUSED: the game's modules are not build " BUILD "'s: her kit stays off"); return; }
    game = g;
    exe = x;
    if (hf->code_matches(exe + 0x79F860, "33 c0 4c 8d 05 c7 b2 01 02 0f 1f 80 00 00 00 00 49 39 0c c0 74 0c ff c0 83 f8 04 75 f3") &&
        hf->code_matches(exe + 0x7F4590, "48 8b c4 f3 0f 11 58 20 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48"))
    {
        g_worldId = (WorldFn)(exe + 0x79F860);
        g_raycast = (RayFn)(exe + 0x7F4590);
        Log("line of sight ready (the game's raycast): her guns pick only what they can hit");
    }
    else Log("line of sight off: the raycast is not the reviewed code");
    HitsInstall("own-blast guard");
    g_staminaOk = hf->code_matches(game + 0xA50E2D, "f3 42 0f 11 8c 11 54 78 54 00");
}

/* ---- the HUD state (read by the Exusiai HUD addon through LuaJIT's ffi) ----------------------------------------- */
typedef struct ExusiaiHud
{
    uint32_t version, in_mission;
    float cd[3], cd_max[3];           /* seconds left, full cooldowns: (Charging Mode: 0), the guns, Overloading Mode */
    uint32_t charge, charge_max;      /* Charging Mode: bursts charged, of ch_every */
    float free_bursts, guns, overload;  /* free bursts' rounds left; seconds left of the guns and of Overloading Mode */
    uint32_t keys[3];
    char key_label[3][24];
} ExusiaiHud;
static ExusiaiHud g_hud = {1};
HF_MOD_EXPORT ExusiaiHud* exusiai_hud_state(void)
{
    g_hud.in_mission = (uint32_t)(g_inMission && g_alive && kb_kit_on);
    float m[3] = {0, s_s2Cd, s_s3Cd};
    for (int i = 0; i < 3; i++) { g_hud.cd[i] = (float)fmax(0, g_cd[i] - g_now); g_hud.cd_max[i] = m[i]; }
    g_hud.charge = (uint32_t)g_charge;
    g_hud.charge_max = (uint32_t)s_chEvery;
    g_hud.free_bursts = (float)g_freeLeft;
    g_hud.guns = (float)fmax(0, g_s2Until - g_now);
    g_hud.overload = (float)fmax(0, g_s3Until - g_now);
    for (int i = 0; i < 3; i++)
    {
        g_hud.keys[i] = (uint32_t)s_keys[i].vk;
        memcpy(g_hud.key_label[i], s_keys[i].label, sizeof g_hud.key_label[i]);
    }
    return &g_hud;
}
/* her guns for the HUD's models: 8 floats each (out, x, y, z where it hovers, x, y, z where it aims, seconds since its
 * last shot), the pair then Overloading Mode's four; written on the game thread (GunsOut) */
static float g_gunsOut[6][8];
static atomic_int g_gunsSeq;
static void GunsOut(void)
{
    float pos[3], aim[3];
    int have = g_inMission && g_alive && kb_kit_on && PlayerPose(pos, aim);
    for (int i = 0; i < 6; i++)
    {
        float* o = g_gunsOut[i];
        float p[3];
        int on = have && (i < 2 ? g_now < g_s2Until : g_now < g_s3Until) && GunPos(i, p);
        o[0] = (float)on;
        if (!on) continue;
        memcpy(o + 1, p, 12);
        int fresh = g_gunShotAt[i] > 0 && g_now - g_gunShotAt[i] < 1.5;
        if (fresh) memcpy(o + 4, g_gunLook[i], 12);
        else for (int k = 0; k < 3; k++) o[4 + k] = p[k] + aim[k] * 20;
        o[7] = (float)(g_gunShotAt[i] > 0 ? g_now - g_gunShotAt[i] : 99);
    }
    atomic_fetch_add(&g_gunsSeq, 1);
}
/* 0.2.5: her guns' shots since `after` for their sounds: 5 floats each (x, y, z, the sound, sequence); the sound's name */
HF_MOD_EXPORT int exusiai_sounds(unsigned version, unsigned after, float* out, int max)
{
    if (version != 1 || !out || max <= 0) return 0;
    return ShotSounds(after, out, max);
}
HF_MOD_EXPORT const char* exusiai_sound_name(int i)
{
    return i >= 0 && i < (int)(sizeof kWeaponSound / sizeof kWeaponSound[0]) ? kWeaponSound[i].name : NULL;
}
HF_MOD_EXPORT int exusiai_guns(unsigned version, float* out, int max)
{
    if (version != 1 || !out || max < 6) return 0;
    memcpy(out, g_gunsOut, sizeof g_gunsOut);
    return 6;
}
/* in-game key bindings: the HUD registers her three skills with Mod Bindings Menu and passes each press here */
static _Atomic unsigned long long g_bindMs;
static atomic_int g_bindPress;
HF_MOD_EXPORT int exusiai_input(unsigned version, int skill)
{
    if (version != 1) return 0;
    atomic_store(&g_bindMs, GetTickCount64());
    if (skill >= 1 && skill <= 3) atomic_fetch_or(&g_bindPress, 1 << (skill - 1));
    return 1;
}
HF_MOD_EXPORT int exusiai_key(unsigned version, int skill, int* vk, int* mods)
{
    if (version != 1 || skill < 1 || skill > 3 || !vk || !mods) return 0;
    *vk = s_keys[skill - 1].vk;
    *mods = s_keys[skill - 1].mods;
    return 1;
}

/* ---- the frame -------------------------------------------------------------------------------------------------- */
static void Tick(HfMod* mod, float dt, void* user)
{
    g_now += dt > 0 && dt < 1 ? dt : 0;
    int inMission = hf->game_state(self) == HF_STATE_MISSION;
    if (inMission != g_inMission) { Reset(inMission ? "mission start" : "mission end"); g_inMission = inMission; }
    if (!g_inMission || !kb_kit_on) return;   /* (standing by: another kit is picked) */
    PendingKills();
    MyItems();
    static int prev[3];
    int focused = Focused();
    int mods = focused ? ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? KM_SHIFT : 0) | ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? KM_CTRL : 0) |
                         ((GetAsyncKeyState(VK_MENU) & 0x8000) ? KM_ALT : 0) : 0;
    int presses = atomic_exchange(&g_bindPress, 0);
    for (int i = 0; i < 3; i++)
    {
        int own = s_keys[i].vk == VK_SHIFT ? KM_SHIFT : s_keys[i].vk == VK_CONTROL ? KM_CTRL : s_keys[i].vk == VK_MENU ? KM_ALT : 0;
        int down = focused && (GetAsyncKeyState(s_keys[i].vk) & 0x8000) != 0 && (mods & ~own) == s_keys[i].mods;
        static unsigned long long fired[3];
        int press = (down && !prev[i]) || (presses >> i & 1);
        if (press && GetTickCount64() - fired[i] > 300 && g_alive && atomic_load(&g_avatar) && (fired[i] = GetTickCount64(), 1))
        {
            if (i == 0) { static unsigned told; if (told++ < 3) Log("Charging Mode is passive: every %d bursts the next sets off %d more", s_chEvery, s_chFree); }
            else if (i == 1) SummonGuns();
            else OverloadingMode();
        }
        prev[i] = down;
    }
    if (g_alive) { GunsFrame(); BurstFrame(); }
    GunsOut();
    Exhaustion();
    atomic_exchange(&g_ownStopped, 0);
    atomic_exchange(&g_kdStopped, 0);
}

/* ---- Mod Options Menu ------------------------------------------------------------------------------------------- */
static const KbOption kOptions[] = {
    {"fire_rate", &s_fireRate, NULL}, {"bless_mult", &s_blessMult, NULL}, {"ch_every", NULL, &s_chEvery}, {"ch_free", NULL, &s_chFree}, {"s2_cooldown", &s_s2Cd, NULL}, {"s2_seconds", &s_s2Seconds, NULL}, {"gun_every", &s_gunEvery, NULL},
    {"s3_cooldown", &s_s3Cd, NULL}, {"s3_seconds", &s_s3Seconds, NULL}, {"s3_range", &s_s3Range, NULL},
    {"every_nearest", &s_rEvery[0], NULL}, {"every_armoured", &s_rEvery[1], NULL}, {"every_attacker", &s_rEvery[2], NULL}, {"every_weakest", &s_rEvery[3], NULL},
    {"no_friendly_fire", NULL, &s_noFriendlyFire}, {"hp_mult", &s_hpMult, NULL}, {"stamina_mult", &s_staminaMult, NULL},
    {"hellbomb_on_exhaustion", NULL, &s_hellbomb},
};
static void OptionAfter(const char* name, int changed)
{
    if (changed && g_charge > s_chEvery) g_charge = s_chEvery;
}
HF_MOD_EXPORT int exusiai_option_get(const char* name, double* out)
{
    return OptionGet(name, out);
}
HF_MOD_EXPORT int exusiai_option_set(const char* name, double value)
{
    return OptionSet(name, value);
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    RosterJoin("exusiai", "Exusiai");
    kb_ini = "exusiai.ini";
    kb_key_names[0] = "key_charge";
    kb_key_names[1] = "key_guns";
    kb_key_names[2] = "key_overload";
    KB_SETTINGS(kSettings);
    KB_OPTIONS(kOptions);
    kb_option_after = OptionAfter;
    kb_damage_taken = DamageTaken;
    kb_damage_dealt = DamageDealt;
    kb_hit_taken = HitTaken;
    /* her body: a Sankta's, not a T-Doll's (the kit base's defaults are the T-Dolls') */
    s_hpMult = 1.13f;
    s_staminaMult = 1.0f;
    s_hellbomb = 0;
    if (!api || api->version < 13 || api->size < offsetof(HfApi, code_matches) + sizeof api->code_matches) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    LoadSettings();
    RoundsAdd();
    api->log(mod, "Exusiai %s: keys %s / %s / %s (Charging Mode, Shooting Mode, Overloading Mode)", VERSION, s_keys[0].label,
             s_keys[1].label, s_keys[2].label);
    if (s_badKey) api->log(mod, "exusiai.ini: a key could not be read (bits %d: charge, guns, overload); its default stays", s_badKey);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL) || api->on(mod, HF_EVENT_PROJECTILES_READY, ProjectilesReady, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) &&
             api->subscribe(mod, HF_ON_WEAPON, OnWeaponExu, NULL, 0) && api->subscribe(mod, HF_ON_STAT, OnStat, NULL, 0) && api->on_game_thread(mod, Tick, NULL);
    if (!ok) api->log(mod, "events or the game thread are unavailable: Exusiai stays off");
    return 0;
}
