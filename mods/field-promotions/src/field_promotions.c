/* Veterancy System (yui.field-promotions): gear that levels up during one mission and resets at its end.
 *
 * v0.1: the A/M-12 Mortar Sentry only.
 *   Level 1  vanilla.
 *   Level 2  (after `level2_kills` mortar kills this mission) each salvo opens with an EMS shell and gets a 4th shell.
 *   Level 3  (after `level3_kills`) when a salvo's last shell lands, a square pattern centred on it follows: its 4
 *            corners, then `pattern_step` seconds later its 4 edge midpoints (`pattern_half` metres from the centre).
 *            The sentry fires `l3_fire_rate` times as fast (its next call-in on), so patterns have room to play out;
 *            a salvo that ends while a pattern still plays gets none.
 *
 * How the extra rounds are placed: nothing in the game data sets a salvo's size or a per-shot projectile, so the mod
 * drops its rounds in itself (spawn_projectile, host). It aims at the sentry's last impact point (HF_ON_EXPLOSION gives
 * every mortar blast's position) and drops the rounds from `drop_height` metres straight down:
 *   - the EMS round: a copy of the EMS Mortar Sentry's shell (add_projectile) with `ems_speed` times its velocity, so
 *     it lands well before the sentry's own shells;
 *   - the 4th round: the A/M-12's own shell, `extra_delay` seconds later.
 * A salvo is noticed from the mission stats (the sentry's shots fired, 10 times a second) when the game records the
 * sentry's shots under it; otherwise from its first impact (the bonus rounds then land a beat after the first shell).
 * Everything the mod sees and does is written to its log, so the first sessions show which way it went.
 *
 * Gas armaments (gas_doctrine.c): at level 3 enemies confused by gas take and deal more damage.
 *
 * Settings: field-promotions.ini in this folder (written with the defaults on first start). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "hellforge_mod.h"
#include "hellforge_content.h"
#include "field_promotions.h"

#define MORTAR_SENTRY HF_ENTITY_AM12_MORTAR_SENTRY
#define MORTAR_SHELL HF_PROJECTILE_AM12_MORTAR_SENTRY
#define MORTAR_BLAST 360u                       /* the A/M-12 shell's ExplosionSettings row (editor catalogue) */
#define EMS_SHELL HF_PROJECTILE_AM23_EMS_MORTAR_SENTRY
#define SALVO_GAP 1.5f                          /* seconds without a shot or impact that end a salvo */
#define AIM_MAX_AGE 30.0f                       /* an aim point older than this is not used */
#define SALVO_END_WAIT 1.4f                     /* seconds after an impact with no other: that was the salvo's last shell */

static HfMod* g_mod;
static const HfApi* g_hf;

/* settings */
static int g_level2Kills = 30, g_level3Kills = 125, g_forceLevel = 0, g_ems = 1, g_extra = 1;
static float g_height = 40.0f, g_extraDelay = 0.6f, g_emsSpeed = 2.0f;
static float g_half = 8.0f, g_step = 1.0f, g_l3FireRate = 1.0f;
static float g_confusion = 8.0f, g_gasTaken = 1.25f, g_gasDealt = 1.5f;
static int g_gasVsPlayers = 0, g_gasElement = -1, g_gasLevel2 = 30, g_gasLevel3 = 150;
static uint64_t g_orbitalGas, g_eagleGas;
static float g_gasCooldown = 0.5f;
static int g_gmShield = 1;
static int g_gmL2 = 30, g_gmL3 = 80;  /* the Gas Mortar Sentry's (and the other gas gear's) own kills for its levels */
static float g_gmAmmoSeconds = 2, g_cloudRadius = 7, g_cloudSeconds = 10;
static int g_gmStackCap = 10, g_gmPopRow = 120;
static float g_gmPopStep = 0.12f, g_gmStackDecay = 3;
static int g_gatL2 = 30, g_gatL3 = 180, g_gatAp = 4, g_gatHeRow = 266, g_gatRoundBase = 148, g_gatHeEvery = 4;
static float g_gatL3Rpm = 0;
static float g_gatRamp = 0.04f, g_gatRampMax = 1.0f, g_gatHe = 0, g_gatHeRate = 8;
static int g_gatSelfImmune = 1;
static float g_gmStackSeconds = 0.7f, g_gmPopDamage = 60, g_gmPopRadius = 2, g_gmPopRadiusStep = 0.6f;

/* state for one mission */
static double g_clock;                          /* seconds of game updates since the mod started */
static int g_inMission, g_kills, g_level = 1;
static int g_hudToasts = 1;  /* level-up banners for the HUD addon */
static int g_hudBadges = 1;  /* rank badges on the stratagem list (the HUD addon) */
static float g_safeRadius = 12;  /* no bonus shell this near one of your sentries */
static int g_mgL2 = 30, g_mgL3 = 180, g_mgFriendly = 1;
static int g_mgHits = 40, g_mgMinHit = 0;
static float g_mgRecharge = 1.5f;
static float g_mgTaken = 0.5f, g_mgShield = 600, g_mgRegen = 150, g_mgDelay = 4, g_mgLife = 1.5f;
typedef struct Guarded { float p[3]; double until; } Guarded;  /* your sentries' beacons */
static Guarded g_guarded[12];
static unsigned g_skipped;
static int g_statsSeen;                         /* the sentry's shots show in the mission stats */
static double g_lastShot = -100, g_lastImpact = -100, g_aimTime = -100;
static float g_aim[3];
static double g_salvoEndAt = -1, g_patternBusyUntil = -100;
static float g_center[3];
static int g_slowed;                            /* the L3 fire rate edit is in place */
static int g_burst;                             /* the L2 four-round salvo edit is in place */
static uint32_t g_emsType = EMS_SHELL;          /* our faster copy, or the vanilla shell if adding it failed */

typedef struct Drop { double at; uint32_t type; float pos[3]; int tries; const char* what; } Drop;
static Drop g_drops[48];
static int g_ndrops;
typedef struct Mark { double until; float pos[3]; } Mark;  /* our own drops' blasts, not the sentry's */
static Mark g_marks[48];
static int g_logged;                            /* stat sources logged (diagnostics) */

/* ---- settings file ----------------------------------------------------------------------------------------- */
static void IniPath(char* out, size_t n) { snprintf(out, n, "%s\\field-promotions.ini", g_hf->mod_folder(g_mod)); }
static FILE* OpenUtf8(const char* path, const wchar_t* mode)
{
    wchar_t wide[MAX_PATH * 2];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, (int)(sizeof wide / sizeof wide[0]))) return NULL;
    return _wfopen(wide, mode);
}
#define IMMUNE_HELP "; Extra enemies gas never confuses (no level 3 boost), as hex resources split by commas. Built in: Bile Titans, Factory and\n" \
    "; War Striders, tanks, Harvesters. The log lists each mission's gassed enemy types with their resources at its end.\n"
#define GM_HELP "; Gas Mortar Sentry level 2: 1 it takes no damage while it stands in gas; its magazine gains a round per this many\n" \
    "; seconds of its lifetime (0: off).\n" \
    "gm_shield_in_gas = %d\ngm_ammo_seconds = %g\n" \
    "; How big (metres) and how long (seconds) a gas cloud is taken to be after a gas shell, strike, mine or grenade.\n" \
    "gas_cloud_radius = %g\ngas_cloud_seconds = %g\n"
#define GAT_HELP "; Gatling Sentry: its kills this mission for level 2 and level 3.\ngat_level2_kills = %d\ngat_level3_kills = %d\n" \
    "; Level 2: whole-deployment ammo; focus fire: extra damage per hit on the same target (0.04: +4%%), and its most.\n" \
    "gat_ramp_per_hit = %g\ngat_ramp_max = %g\n" \
    "; Level 3: its round's armour penetration (4: heavy); its high-explosive blast, an ExplosionSettings row (266: the Orbital\n" \
    "; Gatling Barrage's shell, 350 damage, AP 3) and that blast's damage (0: the row's own; above 0 changes the row for\n" \
    "; everything using it). Read at game start.\ngat_ap = %d\ngat_he_damage = %g\ngat_he_explosion = %d\n"
#define MIX_HELP "; Gatling level 3: one round in this many is high-explosive (1: all; its belt is 4 rounds, so 4: one in four,\n" \
    "; 2: every other). The rest are AP-only. Its fire rate at level 3, rounds a minute (0: the game's 1600; the A-10's\n" \
    "; GAU-8 fires about 3900).\ngat_he_every = %d\ngat_l3_rpm = %g\n"
#define DECAY_HELP "; Gas Mortar level 3: out of its gas, an enemy loses one stack every this many seconds.\ngm_stack_decay = %g\n"
#define STEP_HELP "; Gas Mortar level 3: a shell's detonation runs through every Gas Mortar cloud touching its own; seconds between\n" \
    "; its pops (each stacked enemy pops where it stands).\ngm_pop_step = %g\n" \
    DECAY_HELP
#define STACK_HELP "; Gas Mortar Sentry level 3: an enemy in its gas gains a stack every gm_stack_seconds (up to gm_stack_cap; one lost\n" \
    "; every gm_stack_decay s out of it). Its next shell landing on them pops the stacks: one explosion of gm_pop_damage per stack, of\n" \
    "; gm_pop_radius + gm_pop_radius_step per stack metres (gm_pop_explosion: the ExplosionSettings row it borrows).\n" \
    "gm_stack_seconds = %g\ngm_stack_cap = %d\ngm_pop_damage = %g\ngm_pop_radius = %g\ngm_pop_radius_step = %g\ngm_pop_explosion = %d\n" \
    STEP_HELP
static void LoadSettings(void)
{
    char path[MAX_PATH * 2], line[2048];
    int sawImmune = 0, sawGm = 0, sawStack = 0, sawStep = 0, sawDecay = 0, sawGat = 0, sawMix = 0, sawHud = 0, sawSafe = 0, sawMg = 0, sawSelf = 0, sawBadges = 0, sawLs = 0, sawVent = 0, sawHits = 0, sawGmKills = 0;
    IniPath(path, sizeof path);
    FILE* f = OpenUtf8(path, L"r");
    if (!f)
    {
        f = OpenUtf8(path, L"w");
        if (!f) return;
        fprintf(f, "; Veterancy System settings (read at game start).\n"
                   "; Mortar kills this mission for level 2 and level 3.\nlevel2_kills = %d\nlevel3_kills = %d\n"
                   "; 0: earn levels; 1, 2 or 3: always this level (for testing).\nforce_level = %d\n"
                   "; Level 2: the EMS opener (1 on, 0 off) and its speed (times the EMS shell's).\nems_opener = %d\nems_speed = %g\n"
                   "; Level 2: the 4th shell: 2 the sentry fires it (a 4-round salvo, from its next call-in), 1 dropped in by the mod, 0 off; and the dropped one's delay.\nextra_round = %d\nextra_delay = %g\n"
                   "; Metres above the aim point the bonus rounds start from.\ndrop_height = %g\n"
                   "; Level 3: the pattern's half size (metres from the centre to a side) and the seconds between corners and edges.\npattern_half = %g\npattern_step = %g\n"
                   "; Level 3: the sentry's fire rate, times its own (1: unchanged, the default; below 1 slows its salvos, but a sentry called in\n"
                   "; that way fires a late shell into its own barrel and destroys itself).\nl3_fire_rate = %g\n"
                   "; The gas doctrine: kills by every gas armament together, for its level 2 and level 3.\ngas_level2_kills = %d\ngas_level3_kills = %d\n"
                   "; Gas doctrine level 2: the gas stratagems' cooldowns, times their own.\ngas_cooldown_mult = %g\n"
                   "; Gas doctrine level 3: confused enemies take and deal more damage. Confusion lasts this long after the last gas hit.\n"
                   "gas_taken_mult = %g\ngas_dealt_mult = %g\nconfusion_seconds = %g\n"
                   "; 1: confused enemies also deal more to Helldivers; 0: only to other enemies.\ngas_dealt_vs_players = %d\n"
                   "; The gas damage element: -1 learns it from the first gas armament hit.\ngas_element = %d\n"
                   IMMUNE_HELP "gas_immune = \n" GM_HELP STACK_HELP GAT_HELP MIX_HELP,
            g_level2Kills, g_level3Kills, g_forceLevel, g_ems, (double)g_emsSpeed, g_extra, (double)g_extraDelay, (double)g_height, (double)g_half, (double)g_step, (double)g_l3FireRate,
            g_gasLevel2, g_gasLevel3, (double)g_gasCooldown, (double)g_gasTaken, (double)g_gasDealt, (double)g_confusion, g_gasVsPlayers, g_gasElement, g_gmShield, (double)g_gmAmmoSeconds, (double)g_cloudRadius, (double)g_cloudSeconds,
            (double)g_gmStackSeconds, g_gmStackCap, (double)g_gmPopDamage, (double)g_gmPopRadius, (double)g_gmPopRadiusStep, g_gmPopRow, (double)g_gmPopStep, (double)g_gmStackDecay,
            g_gatL2, g_gatL3, (double)g_gatRamp, (double)g_gatRampMax, g_gatAp, (double)g_gatHe, g_gatHeRow,
            g_gatHeEvery, (double)g_gatL3Rpm);
        fclose(f);
        return;
    }
    while (fgets(line, sizeof line, f))
    {
        char* eq = strchr(line, '=');
        if (line[0] == ';' || !eq) continue;
        *eq = 0;
        for (char* e = eq - 1; e >= line && (*e == ' ' || *e == '\t'); e--) *e = 0;
        double v = strtod(eq + 1, NULL);
        if (!isfinite(v)) continue;
        if (!strcmp(line, "level2_kills")) g_level2Kills = v < 0 ? 0 : (int)v;
        else if (!strcmp(line, "level3_kills")) g_level3Kills = v < 0 ? 0 : (int)v;
        else if (!strcmp(line, "force_level")) g_forceLevel = v < 0 ? 0 : v > 3 ? 3 : (int)v;
        else if (!strcmp(line, "pattern_half")) g_half = (float)(v < 2 ? 2 : v > 40 ? 40 : v);
        else if (!strcmp(line, "pattern_step")) g_step = (float)(v < 0.2 ? 0.2 : v > 5 ? 5 : v);
        else if (!strcmp(line, "gas_cooldown_mult")) g_gasCooldown = (float)(v < 0.05 ? 0.05 : v > 5 ? 5 : v);
        else if (!strcmp(line, "gas_level2_kills")) g_gasLevel2 = v < 0 ? 0 : (int)v;
        else if (!strcmp(line, "gas_level3_kills")) g_gasLevel3 = v < 0 ? 0 : (int)v;
        else if (!strcmp(line, "orbital_gas_source")) g_orbitalGas = strtoull(eq + 1, NULL, 16);
        else if (!strcmp(line, "eagle_gas_source")) g_eagleGas = strtoull(eq + 1, NULL, 16);
        else if (!strcmp(line, "gas_taken_mult")) g_gasTaken = (float)(v < 0.1 ? 0.1 : v > 10 ? 10 : v);
        else if (!strcmp(line, "gas_dealt_mult")) g_gasDealt = (float)(v < 0.1 ? 0.1 : v > 10 ? 10 : v);
        else if (!strcmp(line, "confusion_seconds")) g_confusion = (float)(v < 0.5 ? 0.5 : v > 60 ? 60 : v);
        else if (!strcmp(line, "gas_dealt_vs_players")) g_gasVsPlayers = v != 0;
        else if (!strcmp(line, "gm_level2_kills")) { sawGmKills = 1; g_gmL2 = v < 0 ? 0 : (int)v; }
        else if (!strcmp(line, "gm_level3_kills")) g_gmL3 = v < 0 ? 0 : (int)v;
        else if (!strcmp(line, "gm_shield_in_gas")) { sawGm = 1; g_gmShield = v != 0; }
        else if (!strcmp(line, "gm_ammo_seconds")) g_gmAmmoSeconds = (float)(v <= 0 ? 0 : v < 0.2 ? 0.2 : v > 60 ? 60 : v);
        else if (!strcmp(line, "gm_stack_seconds")) { sawStack = 1; g_gmStackSeconds = (float)(v < 0.1 ? 0.1 : v > 30 ? 30 : v); }
        else if (!strcmp(line, "gm_stack_cap")) g_gmStackCap = v < 1 ? 1 : v > 100 ? 100 : (int)v;
        else if (!strcmp(line, "gm_pop_damage")) g_gmPopDamage = (float)(v < 0 ? 0 : v > 5000 ? 5000 : v);
        else if (!strcmp(line, "gm_pop_radius")) g_gmPopRadius = (float)(v < 0.5 ? 0.5 : v > 30 ? 30 : v);
        else if (!strcmp(line, "gm_pop_radius_step")) g_gmPopRadiusStep = (float)(v < 0 ? 0 : v > 10 ? 10 : v);
        else if (!strcmp(line, "gat_level2_kills")) { sawGat = 1; g_gatL2 = v < 0 ? 0 : (int)v; }
        else if (!strcmp(line, "gat_level3_kills")) g_gatL3 = v < 0 ? 0 : (int)v;
        else if (!strcmp(line, "gat_ramp_per_hit")) g_gatRamp = (float)(v < 0 ? 0 : v > 1 ? 1 : v);
        else if (!strcmp(line, "gat_ramp_max")) g_gatRampMax = (float)(v < 0 ? 0 : v > 10 ? 10 : v);
        else if (!strcmp(line, "gat_ap")) g_gatAp = v < 1 ? 1 : v > 10 ? 10 : (int)v;
        else if (!strcmp(line, "gat_he_damage")) g_gatHe = (float)(v < 0 ? 0 : v > 10000 ? 10000 : v);
        else if (!strncmp(line, "ls_", 3) && ls_setting(line, v)) { sawLs = 1; if (!strcmp(line, "ls_heat_mult")) sawVent = 1; }
        else if (!strcmp(line, "mg_level2_kills")) { sawMg = 1; g_mgL2 = v < 0 ? 0 : (int)v; }
        else if (!strcmp(line, "mg_level3_kills")) g_mgL3 = v < 0 ? 0 : (int)v;
        else if (!strcmp(line, "mg_damage_taken")) g_mgTaken = (float)(v < 0 ? 0 : v > 1 ? 1 : v);
        else if (!strcmp(line, "mg_friendly_immune")) g_mgFriendly = v != 0;
        else if (!strcmp(line, "mg_shield")) g_mgShield = (float)(v < 0 ? 0 : v > 100000 ? 100000 : v);
        else if (!strcmp(line, "mg_shield_regen")) g_mgRegen = (float)(v < 0 ? 0 : v > 100000 ? 100000 : v);
        else if (!strcmp(line, "mg_shield_hits")) { sawHits = 1; g_mgHits = v < 0 ? 0 : v > 1000 ? 1000 : (int)v; }
        else if (!strcmp(line, "mg_shield_recharge")) g_mgRecharge = (float)(v < 0.05 ? 0.05 : v > 120 ? 120 : v);
        else if (!strcmp(line, "mg_shield_min_hit")) g_mgMinHit = v < 0 ? 0 : v > 100000 ? 100000 : (int)v;
        else if (!strcmp(line, "mg_shield_delay")) g_mgDelay = (float)(v < 0 ? 0 : v > 60 ? 60 : v);
        else if (!strcmp(line, "mg_lifetime_mult")) g_mgLife = (float)(v < 0.2 ? 0.2 : v > 10 ? 10 : v);
        else if (!strcmp(line, "safe_radius")) { sawSafe = 1; g_safeRadius = (float)(v < 0 ? 0 : v > 50 ? 50 : v); }
        else if (!strcmp(line, "hud_toasts")) { sawHud = 1; g_hudToasts = v != 0; }
        else if (!strcmp(line, "hud_badges")) { sawBadges = 1; g_hudBadges = v != 0; }
        else if (!strcmp(line, "gat_he_every")) { sawMix = 1; g_gatHeEvery = v < 1 ? 1 : v > 32 ? 32 : (int)v; }
        else if (!strcmp(line, "gat_l3_rpm")) g_gatL3Rpm = (float)(v <= 0 ? 0 : v < 100 ? 100 : v > 6000 ? 6000 : v);
        else if (!strcmp(line, "gat_round_base")) g_gatRoundBase = (v == 77) ? 77 : 148;
        else if (!strcmp(line, "gat_self_immune")) { sawSelf = 1; g_gatSelfImmune = v != 0; }
        else if (!strcmp(line, "gat_he_rate")) g_gatHeRate = (float)(v < 0.5 ? 0.5 : v > 30 ? 30 : v);
        else if (!strcmp(line, "gat_he_explosion")) g_gatHeRow = v < 0 ? 0 : (int)v;
        else if (!strcmp(line, "gm_stack_decay")) { sawDecay = 1; g_gmStackDecay = (float)(v < 0.2 ? 0.2 : v > 60 ? 60 : v); }
        else if (!strcmp(line, "gm_pop_step")) { sawStep = 1; g_gmPopStep = (float)(v < 0 ? 0 : v > 2 ? 2 : v); }
        else if (!strcmp(line, "gm_pop_explosion")) g_gmPopRow = v < 0 ? 0 : (int)v;
        else if (!strcmp(line, "gas_cloud_radius")) g_cloudRadius = (float)(v < 1 ? 1 : v > 40 ? 40 : v);
        else if (!strcmp(line, "gas_cloud_seconds")) g_cloudSeconds = (float)(v < 1 ? 1 : v > 60 ? 60 : v);
        else if (!strcmp(line, "gas_immune")) { sawImmune = 1; gas_set_immune(eq + 1); }
        else if (!strcmp(line, "gas_element")) g_gasElement = v < 0 ? -1 : (int)v;
        else if (!strcmp(line, "l3_fire_rate")) g_l3FireRate = (float)(v < 0.1 ? 0.1 : v > 3 ? 3 : v);
        else if (!strcmp(line, "ems_opener")) g_ems = v != 0;
        else if (!strcmp(line, "ems_speed")) g_emsSpeed = (float)(v < 0.5 ? 0.5 : v > 5 ? 5 : v);
        else if (!strcmp(line, "extra_round")) g_extra = v < 0 ? 0 : v > 2 ? 2 : (int)v;
        else if (!strcmp(line, "extra_delay")) g_extraDelay = (float)(v < 0 ? 0 : v > 5 ? 5 : v);
        else if (!strcmp(line, "drop_height")) g_height = (float)(v < 5 ? 5 : v > 150 ? 150 : v);
    }
    fclose(f);
    if (!sawImmune && (f = OpenUtf8(path, L"a")) != NULL)  /* an ini from before gas_immune: add it */
    {
        fprintf(f, "\n" IMMUNE_HELP "gas_immune = \n");
        fclose(f);
    }
    if (!sawGm && (f = OpenUtf8(path, L"a")) != NULL)  /* an ini from before the Gas Mortar levels */
    {
        fprintf(f, "\n" GM_HELP, g_gmShield, (double)g_gmAmmoSeconds, (double)g_cloudRadius, (double)g_cloudSeconds);
        fclose(f);
    }
    if (!sawGmKills && (f = OpenUtf8(path, L"a")) != NULL)  /* the Gas Mortar's own kill counts */
    {
        fprintf(f, "\n; Gas Mortar Sentry (and each other gas armament): its own kills this mission for level 2 and level 3 (gas\n"
                   "; kills are slower than the Mortar Sentry's).\ngm_level2_kills = %d\ngm_level3_kills = %d\n", g_gmL2, g_gmL3);
        fclose(f);
    }
    if (!sawHits && (f = OpenUtf8(path, L"a")) != NULL)  /* the Machine Gun Sentry's hit-counting shield */
    {
        fprintf(f, "\n; Machine Gun Sentry level 3: its shield stops this many hits, whatever they deal (a Charger's ram too), and\n"
                   "; gets one back every mg_shield_recharge seconds once mg_shield_delay seconds pass without a hit. Hits under\n"
                   "; mg_shield_min_hit damage pass the shield (halved as at level 2) and do not use it (0: every hit uses it).\n"
                   "; (mg_shield and mg_shield_regen, from the damage shield before it, are no longer used.)\n"
                   "mg_shield_hits = %d\nmg_shield_recharge = %g\nmg_shield_min_hit = %d\n", g_mgHits, (double)g_mgRecharge, g_mgMinHit);
        fclose(f);
    }
    if (!sawMg && (f = OpenUtf8(path, L"a")) != NULL)  /* the Machine Gun Sentry */
    {
        fprintf(f, "\n; Machine Gun Sentry: its kills this mission for level 2 and level 3.\nmg_level2_kills = %d\nmg_level3_kills = %d\n"
                   "; Level 2: damage it takes, times (0.5: half); 1: Helldivers' hits (grenades, strikes, stray fire) do nothing to it.\n"
                   "mg_damage_taken = %g\nmg_friendly_immune = %d\n"
                   "; Level 3: a shield soaking this much damage, refilling this much a second once this many seconds pass without a hit;\n"
                   "; and its lifetime, times its own (from its next call-in).\nmg_shield = %g\nmg_shield_regen = %g\nmg_shield_delay = %g\nmg_lifetime_mult = %g\n",
            g_mgL2, g_mgL3, (double)g_mgTaken, g_mgFriendly, (double)g_mgShield, (double)g_mgRegen, (double)g_mgDelay, (double)g_mgLife);
        fclose(f);
    }
    if (!sawSafe && (f = OpenUtf8(path, L"a")) != NULL)  /* the sentry safety radius */
    {
        fprintf(f, "\n; No bonus shell (EMS opener, dropped 4th round, pattern) falls within this many metres of one of your\n"
                   "; sentries, and a shell going off there is not taken as the salvo's impact (0: off).\nsafe_radius = %g\n",
            (double)g_safeRadius);
        fclose(f);
    }
    if (!sawHud && (f = OpenUtf8(path, L"a")) != NULL)  /* the HUD toasts (needs the Veterancy System HUD addon) */
    {
        fprintf(f, "\n; 1: a banner on screen for each level earned in a mission (needs the Veterancy System HUD addon,\n"
                   "; HD2Runtime and Bingus Shared Loader); 0: off.\nhud_toasts = %d\n", g_hudToasts);
        fclose(f);
    }
    if (sawLs && !sawVent && (f = OpenUtf8(path, L"a")) != NULL)  /* an ini from before the Laser Sentry's heat sink */
    {
        ls_write_vent(f);
        fclose(f);
    }
    if (!sawLs && (f = OpenUtf8(path, L"a")) != NULL)  /* an ini from before the Laser Sentry */
    {
        ls_write_defaults(f);
        fclose(f);
    }
    if (!sawBadges && (f = OpenUtf8(path, L"a")) != NULL)  /* an ini from before the rank badges */
    {
        fprintf(f, "\n; 1: a rank badge (one chevron: level 2, two: level 3) on each promoted stratagem in the mission's stratagem\n"
                   "; list (needs the same addons as the banners); 0: off.\nhud_badges = %d\n", g_hudBadges);
        fclose(f);
    }
    if (!sawSelf && (f = OpenUtf8(path, L"a")) != NULL)  /* an ini from before the Gatling's blast guard */
    {
        fprintf(f, "\n; Gatling Sentry level 3: 1: its own high-explosive blasts (any Gatling Sentry's) do not hurt Gatling Sentries;\n"
                   "; 0: they do.\ngat_self_immune = %d\n", g_gatSelfImmune);
        fclose(f);
    }
    if (sawGat && !sawMix && (f = OpenUtf8(path, L"a")) != NULL)  /* an ini from before the Gatling's belt */
    {
        fprintf(f, "\n" MIX_HELP, g_gatHeEvery, (double)g_gatL3Rpm);
        fclose(f);
    }
    if (!sawGat && (f = OpenUtf8(path, L"a")) != NULL)  /* an ini from before the Gatling Sentry */
    {
        fprintf(f, "\n" GAT_HELP MIX_HELP, g_gatL2, g_gatL3, (double)g_gatRamp, (double)g_gatRampMax, g_gatAp, (double)g_gatHe, g_gatHeRow,
            g_gatHeEvery, (double)g_gatL3Rpm);
        fclose(f);
    }
    if (sawStep && !sawDecay && (f = OpenUtf8(path, L"a")) != NULL)  /* an ini from before the slow stack loss */
    {
        fprintf(f, "\n" DECAY_HELP, (double)g_gmStackDecay);
        fclose(f);
    }
    if (sawStack && !sawStep && (f = OpenUtf8(path, L"a")) != NULL)  /* an ini from before the chained detonation */
    {
        fprintf(f, "\n" STEP_HELP, (double)g_gmPopStep, (double)g_gmStackDecay);
        fclose(f);
    }
    if (!sawStack && (f = OpenUtf8(path, L"a")) != NULL)  /* an ini from before the Gas Mortar's stacks */
    {
        fprintf(f, "\n" STACK_HELP, (double)g_gmStackSeconds, g_gmStackCap, (double)g_gmPopDamage, (double)g_gmPopRadius, (double)g_gmPopRadiusStep, g_gmPopRow, (double)g_gmPopStep, (double)g_gmStackDecay);
        fclose(f);
    }
}

/* ---- HUD: level-up toasts for the Lua overlay (mods/yui/field_promotions_hud, through HD2Runtime) ----------------
 * The addon finds field_promotions.dll and calls fp_hud_state() through LuaJIT's ffi, every frame, on the game thread
 * (the same thread these events run on). A toast is written whole before `seq` counts it. Layout version 1. */
typedef struct FpToast { uint32_t level; char title[64]; char line[160]; } FpToast;
/* Layout version 2 adds the rank badges (gear: each promotable stratagem's level now) after version 1's fields. */
typedef struct FpHud { uint32_t version, seq, in_mission, toasts_on; FpToast toasts[8]; uint32_t badges_on, gear_count; FpGear gear[16]; } FpHud;
static FpHud g_hud = {2, 0, 0, 1, {{0}}, 1, 0, {{0}}};
static int Level(void);
HF_MOD_EXPORT FpHud* fp_hud_state(void)
{
    g_hud.in_mission = (uint32_t)g_inMission;
    g_hud.toasts_on = (uint32_t)g_hudToasts;
    g_hud.badges_on = (uint32_t)g_hudBadges;
    int n = 0;
    g_hud.gear[n++] = (FpGear){HF_STRATAGEM_AM12_MORTAR_SENTRY, (uint32_t)Level()};
    g_hud.gear[n++] = (FpGear){HF_STRATAGEM_AG16_GATLING_SENTRY, (uint32_t)gat_level()};
    g_hud.gear[n++] = (FpGear){HF_STRATAGEM_AMG43_MACHINE_GUN_SENTRY, (uint32_t)mg_level()};
    g_hud.gear[n++] = (FpGear){HF_STRATAGEM_ALAS98_LASER_SENTRY, (uint32_t)ls_level()};
    n += gas_hud_gear(g_hud.gear + n, 16 - n);
    g_hud.gear_count = (uint32_t)n;
    return &g_hud;
}
/* An earned level-up (not a forced level), in a mission */
void fp_toast(const char* gear, int level)
{
    if (!g_hudToasts || !g_inMission || g_forceLevel || level < 2) return;
    static const char* kLines[2][2] = {
        {"Ammunition requisition approved. Do not waste Super Earth's generosity.",
         "Premium munitions authorised. Liberty thanks you for surviving this long."},
        {"Chemical logistics expedited. Spread democracy evenly.",
         "Chemical doctrine certified. The enemy is now confused about freedom."}};
    int doctrine = strstr(gear, "doctrine") != NULL || strstr(gear, "DOCTRINE") != NULL;
    FpToast* t = &g_hud.toasts[g_hud.seq % 8];
    t->level = (uint32_t)level;
    snprintf(t->title, sizeof t->title, "%s", gear);
    for (char* c = t->title; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
    snprintf(t->line, sizeof t->line, "%s", kLines[doctrine][level >= 3]);
    g_hud.seq++;
    g_hf->log(g_mod, "HUD toast %u: %s level %d", g_hud.seq, t->title, level);
}

double fp_clock(void) { return g_clock; }
int fp_forced_level(void) { return g_forceLevel; }
int fp_level2_kills(void) { return g_level2Kills; }
int fp_level3_kills(void) { return g_level3Kills; }
int fp_gm_level2_kills(void) { return g_gmL2; }
int fp_gm_level3_kills(void) { return g_gmL3; }
float fp_confusion_seconds(void) { return g_confusion; }
float fp_gas_taken_mult(void) { return g_gasTaken; }
float fp_gas_dealt_mult(void) { return g_gasDealt; }
int fp_gas_vs_players(void) { return g_gasVsPlayers; }
int fp_gas_level2_kills(void) { return g_gasLevel2; }
int fp_gas_level3_kills(void) { return g_gasLevel3; }
float fp_gas_cooldown_mult(void) { return g_gasCooldown; }
int fp_gm_shield(void) { return g_gmShield; }
float fp_gm_ammo_seconds(void) { return g_gmAmmoSeconds; }
float fp_gm_stack_seconds(void) { return g_gmStackSeconds; }
int fp_gm_stack_cap(void) { return g_gmStackCap; }
float fp_gm_pop_damage(void) { return g_gmPopDamage; }
float fp_gm_pop_radius(void) { return g_gmPopRadius; }
float fp_gm_pop_radius_step(void) { return g_gmPopRadiusStep; }
int fp_gm_pop_row(void) { return g_gmPopRow; }
float fp_gm_pop_step(void) { return g_gmPopStep; }
float fp_gm_stack_decay(void) { return g_gmStackDecay; }
int fp_mg_level2_kills(void) { return g_mgL2; }
int fp_mg_level3_kills(void) { return g_mgL3; }
float fp_mg_damage_taken(void) { return g_mgTaken; }
int fp_mg_friendly_immune(void) { return g_mgFriendly; }
float fp_mg_shield(void) { return g_mgShield; }
float fp_mg_shield_regen(void) { return g_mgRegen; }
float fp_mg_shield_delay(void) { return g_mgDelay; }
int fp_mg_shield_hits(void) { return g_mgHits; }
float fp_mg_shield_recharge(void) { return g_mgRecharge; }
int fp_mg_shield_min_hit(void) { return g_mgMinHit; }
float fp_mg_lifetime_mult(void) { return g_mgLife; }
int fp_gat_level2_kills(void) { return g_gatL2; }
int fp_gat_level3_kills(void) { return g_gatL3; }
float fp_gat_ramp_per_hit(void) { return g_gatRamp; }
float fp_gat_ramp_max(void) { return g_gatRampMax; }
int fp_gat_ap(void) { return g_gatAp; }
float fp_gat_he_damage(void) { return g_gatHe; }
int fp_gat_he_explosion(void) { return g_gatHeRow; }
float fp_gat_he_rate(void) { return g_gatHeRate; }
int fp_gat_self_immune(void) { return g_gatSelfImmune; }
int fp_gat_round_base(void) { return g_gatRoundBase; }
int fp_gat_he_every(void) { return g_gatHeEvery; }
float fp_gat_l3_rpm(void) { return g_gatL3Rpm; }
float fp_cloud_radius(void) { return g_cloudRadius; }
float fp_cloud_seconds(void) { return g_cloudSeconds; }
int fp_in_mission(void) { return g_inMission; }

/* ---- levels ------------------------------------------------------------------------------------------------- */
static int Level(void)
{
    if (g_forceLevel) return g_forceLevel;
    return g_kills >= g_level3Kills ? 3 : g_kills >= g_level2Kills ? 2 : 1;
}

/* Level 3's slower salvos: a live edit of the sentry's default fire rate (its next call-in takes it), undone at the
 * mission's end. */
static void SetSlow(int on)
{
    HfField rate = HF_FIELD_AT(ProjectileWeapon, fire_rate, 1);
    if (!on)
    {
        if (g_slowed) g_hf->set_live(g_mod, MORTAR_SENTRY, &rate, NULL, HF_LIVE_SHARED);
        g_slowed = 0;
        return;
    }
    if (g_slowed || g_l3FireRate == 1.0f) return;
    HfLiveValue live;
    memset(&live, 0, sizeof live);
    if (!g_hf->get_live(g_mod, MORTAR_SENTRY, &rate, &live)) { g_hf->log(g_mod, "level 3: the sentry's fire rate cannot be read"); return; }
    HfValue value = HF_F32(live.base.number * g_l3FireRate);
    if (g_hf->set_live(g_mod, MORTAR_SENTRY, &rate, &value, HF_LIVE_SHARED))
    {
        g_slowed = 1;
        g_hf->log(g_mod, "level 3: Mortar Sentry fire rate %.0f -> %.0f rpm (from its next call-in)", live.base.number, value.number);
    }
    else g_hf->log(g_mod, "level 3: the fire rate change was refused (%s)", g_hf->last_log ? g_hf->last_log(g_mod) : "");
}

/* Level 2's real 4th shell: the sentry's rounds per burst (3) plus one, from its next call-in. */
static void SetBurst(int on)
{
    HfField burst = HF_FIELD(WeaponData, burst_rounds);
    if (!on)
    {
        if (g_burst) g_hf->set_live(g_mod, MORTAR_SENTRY, &burst, NULL, HF_LIVE_SHARED);
        g_burst = 0;
        return;
    }
    if (g_burst || g_extra != 2) return;
    HfLiveValue live;
    memset(&live, 0, sizeof live);
    if (!g_hf->get_live(g_mod, MORTAR_SENTRY, &burst, &live)) { g_hf->log(g_mod, "level 2: the sentry's burst size cannot be read"); return; }
    HfValue value = HF_U32(live.base.number + 1);
    if (g_hf->set_live(g_mod, MORTAR_SENTRY, &burst, &value, HF_LIVE_SHARED))
    {
        g_burst = 1;
        g_hf->log(g_mod, "level 2: Mortar Sentry salvo %.0f -> %.0f rounds (from its next call-in)", live.base.number, value.number);
    }
    else g_hf->log(g_mod, "level 2: the salvo size change was refused (%s)", g_hf->last_log ? g_hf->last_log(g_mod) : "");
}

static void CheckLevel(void)
{
    int level = Level();
    if (level != g_level)
    {
        if (level > g_level) fp_toast("A/M-12 Mortar Sentry", level);
        g_level = level;
        g_hf->log(g_mod, "Mortar Sentry: level %d (%d kills this mission)", level, g_kills);
    }
    if (g_inMission && level >= 2) SetBurst(1);
    if (g_inMission && level >= 3) SetSlow(1);
}
static void ResetMission(const char* why)
{
    g_kills = 0;
    g_level = Level();
    g_lastShot = g_lastImpact = g_aimTime = -100;
    g_ndrops = 0;
    memset(g_marks, 0, sizeof g_marks);
    g_salvoEndAt = -1;
    g_patternBusyUntil = -100;
    memset(g_guarded, 0, sizeof g_guarded);
    g_skipped = 0;
    /* the sentries' records go back at the next mission start, when none are out: put back at the end, a sentry still
     * on the field can hold more rounds than its magazine (the game crashed after missions that did) */
    if (g_inMission) { SetSlow(0); SetBurst(0); }
    gas_reset();
    gat_reset();
    mg_reset();
    ls_reset();
    g_hf->log(g_mod, "%s: Mortar Sentry back to level %d", why, g_level);
}

/* ---- bonus rounds ------------------------------------------------------------------------------------------- */
/* ---- your sentries: no bonus shell near one ----------------------------------------------------------------------
 * A sentry lands on its beacon (HF_ON_STRATAGEM), so each beacon of yours is a sentry for its lifetime. A blast there is
 * never taken for a salvo's impact, and no bonus shell (the EMS opener, a dropped 4th round, the pattern) falls within
 * safe_radius metres of one: a shell that went off at the sentry (under cover, say) once brought the EMS opener down on
 * the sentry itself. Teammates' sentries are not known (their beacons are told on their machines). */
static const uint32_t kSentryStratagems[] = {HF_STRATAGEM_AM12_MORTAR_SENTRY, HF_STRATAGEM_AM23_EMS_MORTAR_SENTRY,
    HF_STRATAGEM_AGM17_GAS_MORTAR_SENTRY, HF_STRATAGEM_AG16_GATLING_SENTRY, HF_STRATAGEM_AMG43_MACHINE_GUN_SENTRY,
    HF_STRATAGEM_AAC8_AUTOCANNON_SENTRY, HF_STRATAGEM_AMLS4X_ROCKET_SENTRY, HF_STRATAGEM_ALAS98_LASER_SENTRY,
    HF_STRATAGEM_AFLAM40_FLAME_SENTRY};
static int NearSentry(const float p[3])
{
    float r2 = g_safeRadius * g_safeRadius;
    for (int i = 0; i < (int)(sizeof g_guarded / sizeof g_guarded[0]); i++)
    {
        if (g_guarded[i].until < g_clock) continue;
        float x = g_guarded[i].p[0] - p[0], y = g_guarded[i].p[1] - p[1], z = g_guarded[i].p[2] - p[2];
        if (x * x + y * y + z * z <= r2) return 1;
    }
    return 0;
}
static void OnSentryBeacon(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfStratagemEvent* e = (const HfStratagemEvent*)info;
    int sentry = 0;
    for (unsigned i = 0; i < sizeof kSentryStratagems / sizeof kSentryStratagems[0]; i++) sentry |= e->type == kSentryStratagems[i];
    if (!sentry) return;
    int slot = 0;
    for (int i = 1; i < (int)(sizeof g_guarded / sizeof g_guarded[0]); i++) if (g_guarded[i].until < g_guarded[slot].until) slot = i;
    memcpy(g_guarded[slot].p, e->position, sizeof g_guarded[slot].p);
    g_guarded[slot].until = g_clock + 260.0;  /* the call-in, landing and a lifetime of up to about 4 minutes */
}

static void Queue(double at, uint32_t type, const float aim[3], const char* what)
{
    if (NearSentry(aim))
    {
        if (g_skipped++ < 10) g_hf->log(g_mod, "%s skipped: within %.0f m of one of your sentries", what, (double)g_safeRadius);
        return;
    }
    if (g_ndrops >= (int)(sizeof g_drops / sizeof g_drops[0])) return;
    Drop* d = &g_drops[g_ndrops++];
    d->at = at;
    d->type = type;
    d->pos[0] = aim[0];
    d->pos[1] = aim[1];
    d->pos[2] = aim[2] + g_height;
    d->tries = 0;
    d->what = what;
}

static void Salvo(const char* how)
{
    if (!g_inMission || Level() < 2) return;
    if (g_clock - g_aimTime > AIM_MAX_AGE) { g_hf->log(g_mod, "salvo (%s): no recent aim point yet", how); return; }
    if (g_ems) Queue(g_clock, g_emsType, g_aim, "EMS opener");
    if (g_extra == 1) Queue(g_clock + g_extraDelay, MORTAR_SHELL, g_aim, "4th shell");
    g_hf->log(g_mod, "salvo (%s): bonus rounds on (%.0f, %.0f, %.0f)", how, g_aim[0], g_aim[1], g_aim[2]);
}

static void RunDrops(void)
{
    static const float down[3] = {0.0f, 0.0f, -1.0f};
    for (int i = 0; i < g_ndrops; )
    {
        Drop* d = &g_drops[i];
        if (d->at > g_clock) { i++; continue; }
        int r = g_hf->spawn_projectile(g_mod, d->type, d->pos, down);
        if (r == HF_ACT_WAITING && ++d->tries < 120) { d->at = g_clock + 0.1; i++; continue; }  /* its packages load */
        if (r && r != HF_ACT_WAITING && d->type == MORTAR_SHELL)  /* our shells' blasts are mortar blasts too */
            for (int m = 0; m < (int)(sizeof g_marks / sizeof g_marks[0]); m++)
                if (g_marks[m].until < g_clock)
                {
                    g_marks[m].until = g_clock + 3.0;
                    memcpy(g_marks[m].pos, d->pos, sizeof d->pos);
                    break;
                }
        if (!r) g_hf->log(g_mod, "%s refused (%s)", d->what, g_hf->last_log ? g_hf->last_log(g_mod) : "");
        else if (r == HF_ACT_WAITING) g_hf->log(g_mod, "%s: its packages did not load in time", d->what);
        *d = g_drops[--g_ndrops];
    }
}

static int OurBlast(const float p[3])
{
    for (int m = 0; m < (int)(sizeof g_marks / sizeof g_marks[0]); m++)
    {
        if (g_marks[m].until < g_clock) continue;
        float dx = p[0] - g_marks[m].pos[0], dy = p[1] - g_marks[m].pos[1];
        if (dx * dx + dy * dy < 4.0f * 4.0f) return 1;
    }
    return 0;
}

/* Level 3: the square around the salvo's last shell, corners first, then the edge midpoints. */
static void Pattern(void)
{
    if (g_clock < g_patternBusyUntil) { g_hf->log(g_mod, "pattern skipped: the last one is still playing"); return; }
    static const float corners[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}}, edges[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int i = 0; i < 4; i++)
    {
        float p[3] = {g_center[0] + corners[i][0] * g_half, g_center[1] + corners[i][1] * g_half, g_center[2]};
        Queue(g_clock, MORTAR_SHELL, p, "pattern corner");
    }
    for (int i = 0; i < 4; i++)
    {
        float p[3] = {g_center[0] + edges[i][0] * g_half, g_center[1] + edges[i][1] * g_half, g_center[2]};
        Queue(g_clock + g_step, MORTAR_SHELL, p, "pattern edge");
    }
    g_patternBusyUntil = g_clock + g_step + 1.5;
    g_hf->log(g_mod, "pattern on (%.0f, %.0f, %.0f)", g_center[0], g_center[1], g_center[2]);
}

/* ---- events ----------------------------------------------------------------------------------------------- */
static void OnStat(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfStatEvent* e = (const HfStatEvent*)info;
    for (uint32_t i = 0; i < e->count && i < HF_STAT_SOURCES; i++)
    {
        const HfStatSource* s = &e->sources[i];
        if (g_logged < 40)
        {
            g_logged++;
            g_hf->log(g_mod, "stat %s +%d from %016llX%s", e->stat == HF_STAT_SHOTS ? "shots" : e->stat == HF_STAT_KILLS ? "kills"
                : e->stat == HF_STAT_HITS ? "hits" : "damage", s->added, (unsigned long long)s->resource,
                s->resource == MORTAR_SENTRY ? " (Mortar Sentry)" : "");
        }
        if (e->stat == HF_STAT_KILLS && g_inMission) { gas_kills(s->resource, s->added); gat_kills(s->resource, s->added); mg_kills(s->resource, s->added); ls_kills(s->resource, s->added); }
        if (s->resource != MORTAR_SENTRY) continue;
        if (e->stat == HF_STAT_KILLS && g_inMission)
        {
            g_kills += s->added;
            CheckLevel();
        }
        else if (e->stat == HF_STAT_SHOTS)
        {
            if (!g_statsSeen) { g_statsSeen = 1; g_hf->log(g_mod, "the Mortar Sentry's shots show in the mission stats: salvos are seen as they are fired"); }
            if (g_clock - g_lastShot > SALVO_GAP) Salvo("fired");
            g_lastShot = g_clock;
        }
    }
}

static void OnExplosion(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfExplosionEvent* e = (const HfExplosionEvent*)info;
    if (e->type != MORTAR_BLAST || !g_inMission || OurBlast(e->position)) return;
    if (NearSentry(e->position))  /* a shell gone off at one of your sentries: not where the salvo is aimed */
    {
        static unsigned noted;
        if (noted++ < 12)
        {
            float best = 1e9f;
            for (int i = 0; i < (int)(sizeof g_guarded / sizeof g_guarded[0]); i++)
            {
                if (g_guarded[i].until < g_clock) continue;
                float x = g_guarded[i].p[0] - e->position[0], y = g_guarded[i].p[1] - e->position[1], z = g_guarded[i].p[2] - e->position[2];
                float d = sqrtf(x * x + y * y + z * z);
                if (d < best) best = d;
            }
            g_hf->log(g_mod, "mortar blast %.1f m from one of your sentries (from entity %u, owner %u) at (%.1f, %.1f, %.1f): ignored",
                (double)best, e->source, e->owner, (double)e->position[0], (double)e->position[1], (double)e->position[2]);
        }
        return;
    }
    int first = g_clock - g_lastImpact > SALVO_GAP;
    g_lastImpact = g_clock;
    memcpy(g_aim, e->position, sizeof g_aim);
    g_aimTime = g_clock;
    if (first && !g_statsSeen) Salvo("first impact");
    memcpy(g_center, e->position, sizeof g_center);  /* the latest shell: when no other follows, the salvo's last */
    g_salvoEndAt = g_clock + SALVO_END_WAIT;
}

static void OnMission(HfMod* mod, const HfEventInfo* info, void* user)
{
    g_inMission = info->kind == HF_ON_MISSION_START;
    ResetMission(g_inMission ? "mission start" : "mission end");
    if (g_inMission) CheckLevel();  /* a forced level 3 slows the sentry from the start */
}

static void Tick(HfMod* mod, float dt, void* user)
{
    g_clock += dt;
    if (g_salvoEndAt >= 0 && g_clock >= g_salvoEndAt)
    {
        g_salvoEndAt = -1;
        if (g_inMission && Level() >= 3) Pattern();
    }
    if (g_ndrops) RunDrops();
    gas_tick(g_clock);
    ls_tick(g_clock);
}

/* Diagnostics: the sentry's burst size and its three fire rate slots, the candidates for "shots per salvo" and "time
 * between salvos" (the editor catalogue names neither for the Mortar Sentry). */
static void EntitiesReady(HfMod* mod, HfEvent event, void* user)
{
    HfField burst = HF_FIELD(WeaponData, burst_rounds);
    HfLiveValue v;
    memset(&v, 0, sizeof v);
    if (g_hf->get_live(g_mod, MORTAR_SENTRY, &burst, &v)) g_hf->log(g_mod, "Mortar Sentry: burst_rounds %.0f", v.base.number);
    else g_hf->log(g_mod, "Mortar Sentry: burst_rounds not readable (%s)", g_hf->last_log ? g_hf->last_log(g_mod) : "");
    for (int i = 0; i < 3; i++)
    {
        HfField rate = HF_FIELD_AT(ProjectileWeapon, fire_rate, i);
        memset(&v, 0, sizeof v);
        if (g_hf->get_live(g_mod, MORTAR_SENTRY, &rate, &v)) g_hf->log(g_mod, "Mortar Sentry: fire_rate[%d] %.1f rpm", i, v.base.number);
    }
}

static void ProjectilesReady(HfMod* mod, HfEvent event, void* user)
{
    if (g_emsType == EMS_SHELL) return;
    HfProjectileSettings* row = (HfProjectileSettings*)g_hf->projectile_row(g_emsType);
    if (!row || row->type != g_emsType) { g_hf->log(g_mod, "the fast EMS shell's row is not there: the vanilla EMS shell is used"); g_emsType = EMS_SHELL; return; }
    float v;
    memcpy(&v, (uint8_t*)row + 0x20, 4);  /* +0x20 velocity (ProjectileSettings, editor catalogue offset 32) */
    float fast = v * g_emsSpeed;
    memcpy((uint8_t*)row + 0x20, &fast, 4);
    g_hf->log(g_mod, "fast EMS shell (type %u): velocity %.0f -> %.0f m/s", g_emsType, v, fast);
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* hf, HfMod* mod)
{
    if (hf->version < 11 || hf->size <= offsetof(HfApi, spawn_projectile) || !hf->spawn_projectile) return 1;
    g_hf = hf;
    g_mod = mod;
    LoadSettings();
    g_level = Level();
    if (g_ems && g_emsSpeed != 1.0f)
    {
        HfProjectile fast = {sizeof fast};
        fast.base = EMS_SHELL;
        fast.name = "yui/field_promotions/fast_ems_shell";
        uint32_t type = hf->add_projectile(mod, &fast);
        if (type) g_emsType = type;
        hf->on(mod, HF_EVENT_PROJECTILES_READY, ProjectilesReady, NULL);
    }
    hf->on(mod, HF_EVENT_ENTITIES_READY, EntitiesReady, NULL);
    int ok = hf->subscribe(mod, HF_ON_STAT, OnStat, NULL, 0) & hf->subscribe(mod, HF_ON_EXPLOSION, OnExplosion, NULL, 0) &
             hf->subscribe(mod, HF_ON_STRATAGEM, OnSentryBeacon, NULL, 0) &
             hf->subscribe(mod, HF_ON_MISSION_START, OnMission, NULL, 0) & hf->subscribe(mod, HF_ON_MISSION_END, OnMission, NULL, 0);
    ok = ok && gas_init(hf, mod, g_gasElement, g_orbitalGas, g_eagleGas) && gat_init(hf, mod) && mg_init(hf, mod) && ls_init(hf, mod);
    if (!ok || !hf->on_game_thread(mod, Tick, NULL)) { hf->log(mod, "Veterancy System: events or the game thread unavailable"); return 1; }
    g_inMission = hf->game_state(mod) == HF_STATE_MISSION;
    hf->log(mod, "Veterancy System 0.17.0: Mortar Sentry level 2 at %d kills, level 3 at %d%s; EMS opener %s (x%.1f speed), 4th shell %s",
        g_level2Kills, g_level3Kills, g_forceLevel ? " (forced level in the ini)" : "", g_ems ? "on" : "off", (double)g_emsSpeed, g_extra == 2 ? "fired by the sentry" : g_extra ? "dropped in" : "off");
    return 0;
}
