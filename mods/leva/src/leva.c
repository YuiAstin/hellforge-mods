/* Leva (yui.leva): Leva's kit from Girls' Frontline 2 as abilities on keys, for your own Helldiver (solo or host).
 *
 * Her electricity is the G-31 Arc grenade's detonation (explosion row 157): her skills set arcs off at points on the
 * ground (a carpet, a ring, a line) and on enemies.
 *   Rational Suppression (key_suppress, default Z): a lockdown field across her view rs_distance m ahead (rs_columns x
 *     rs_rows points over rs_width x rs_depth m), swept in like an Eagle Airstrike: enemies under it get Negative Charge
 *     and are paralysed rs_paralysis s, and Voltage zones stay on its middle row. Then it arcs again every rs_pulse s for
 *     rs_seconds s, holding the enemies in it; its arcs deal rs_damage of their damage (a lockdown, not a nuke).
 *   Orderly Disruption (key_disrupt, default X): Arc Rounds for od_seconds s: her bullets set off an arc where they hit an
 *     enemy (one every od_step s at most), charging it; on charged enemies those arcs deal od_charged times. +1 Code.
 *   Quantum Calculation (key_ult, default C): four rings of arcs expanding out to ult_radius m and an arc on every
 *     enemy in it (all charged), Voltage zones on the inner ring. Then, within blitz_window s, the same key again:
 *   Superconduct Blitz: four arcs blitz_length m along her aim, then a line of arcs chaining back to her, spending all her
 *     Superconduct Code: blitz_power times 1, 1.17, 1.42 or 2 at 1-4 stacks. Then Stealth for blitz_stealth s.
 *   Her arcs are kept to arc_rate a second (too many at once leave their lightning on the field): those near enemies
 *     first, the empty ones (showing her reach) in what is left; her ultimate and Blitz may burst arc_burst above it.
 *   The Fox's Scheme (passive): whenever an enemy within fs_radius m gets Negative Charge, an arc on it (Wise Support,
 *     at most fs_max per fs_every s), each taking fs_cut s off the ultimate's cooldown. A charged enemy's death passes
 *     the charge to the nearest enemy within 10 m and gives a Code stack. A Code stack every code_every s.
 *   Negative Charge (nc_seconds): her arcs hit charged enemies nc_mult times as hard, her shots elite_mult.
 *   Positive Charge (on her): pc_heal health every pc_every s; her arcs on charged enemies pc_mult times again.
 *   Superconduct Code (0-4): code_per more damage per stack on her arcs and shots; at 4, Superconduct Link (link_mult).
 *   Voltage zones (vz_radius m, vz_seconds s): when an enemy in one takes explosive damage, the zone bursts in an arc
 *     (+1 Code) and is gone.
 *   T-Doll body: hp_mult health, stamina_mult sprint, and a Hellbomb on her if her stamina ever runs dry.
 *
 * Research credit: the hit-result layout, the hooked functions, the entity and scene-graph reads, the raycast come from
 * Helldiver Survival Rework 0.3.2 by enochlophobianumerophobia (MIT) and Scope of Authority's Apex knockdown notes,
 * whose research used HD2Runtime by SkyeShade's catalogues (https://github.com/SkyeShade/HD2Runtime). See
 * THIRD_PARTY_NOTICES.md. */
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

#define VERSION "0.5.0"
#define BUILD "25480438"
#define AVATAR UINT64_C(0x4D1C334D294DFA97)
#define NODE_HEAD 90u
#define NODE_HEAD_AIM 91u

static const HfApi* hf;
static HfMod* self;
static uintptr_t game, exe;

/* ---- settings ------------------------------------------------------------------------------------------------- */
#define KM_SHIFT 1
#define KM_CTRL 2
#define KM_ALT 4
typedef struct Key { int vk, mods; char label[24]; } Key;
static Key s_keys[3] = {{'Z', 0, "Z"}, {'X', 0, "X"}, {'C', 0, "C"}};
static int s_nativeRows = 1;
static int s_arcMode = 1, s_arcShell = 222, s_arcRow = 157, s_rsColumns = 7, s_rsRows = 3, s_ultArcsMax = 24, s_fsMax = 2, s_codeMax = 4, s_steady = 0;
static int s_rsArcs = 2;
static float s_rsCd = 14, s_rsDistance = 12, s_rsWidth = 18, s_rsDepth = 6, s_rsParalysis = 4, s_rsSweep = 0.05f, s_rsSeconds = 10, s_rsPulse = 0.5f, s_rsDamage = 0.067f, s_rsHold = 1.0f;
static float s_odCd = 15, s_odSeconds = 8, s_odStep = 0.2f, s_odCharged = 1.3f;
static float s_ultCd = 40, s_ultRadius = 20, s_blitzWindow = 6, s_blitzLength = 20, s_blitzStep = 2.5f, s_blitzStealth = 4, s_blitzPower = 2.0f;
static float s_fsRadius = 20, s_fsEvery = 4, s_fsCut = 2;
static float s_ncSeconds = 6, s_ncMult = 1.3f, s_eliteMult = 1.15f;
static float s_pcHeal = 10, s_pcEvery = 4, s_pcMult = 1.35f;
static float s_codeEvery = 4, s_codePer = 0.05f, s_linkMult = 1.1f;
static float s_vzRadius = 3.5f, s_vzSeconds = 9, s_vzArm = 1.0f;
static float s_arcRate = 10, s_arcReach = 8, s_arcBurst = 0;
static int s_arcChain = -1, s_arcSplit = -1, s_arcStyle = 0;
static float s_arcRange = 0, s_aimSmooth = 0.2f;
static int s_arcGrenade = 0;
static int s_arcEmpty = 1;
static float s_lpClimb = 6, s_lpDrop = 12;
static int s_hellbomb = 1, s_hellbombRow = 125;
static float s_hpMult = 1.5f, s_staminaMult = 2.0f, s_hellbombHold = 0.5f;

static int s_badKey;
static void Setting(const char* key, double v)
{
#define CL(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#define F(name, var, lo, hi) else if (!strcmp(key, name)) var = (float)CL(v, lo, hi)
#define I(name, var, lo, hi) else if (!strcmp(key, name)) var = (int)CL(v, lo, hi)
    if (0) {}
    I("arc_mode", s_arcMode, 0, 3); F("arc_rate", s_arcRate, 1, 100); F("arc_reach", s_arcReach, 1, 30); F("arc_burst", s_arcBurst, 0, 100); I("arc_chain", s_arcChain, -1, 8); I("arc_shape", s_arcStyle, 0, 2); F("arc_range", s_arcRange, 0, 100); F("aim_smooth", s_aimSmooth, 0, 2); I("arc_grenade", s_arcGrenade, 0, 1); I("arc_split", s_arcSplit, -1, 8); I("arc_empty", s_arcEmpty, 0, 1); I("arc_shell", s_arcShell, 0, 100000); I("arc_explosion", s_arcRow, 0, 100000); I("steady", s_steady, 0, 1); I("native_rows", s_nativeRows, 0, 1);
    F("rs_cooldown", s_rsCd, 0, 600); F("rs_distance", s_rsDistance, 2, 60); F("rs_width", s_rsWidth, 2, 60); F("rs_depth", s_rsDepth, 0, 30);
    I("rs_columns", s_rsColumns, 1, 15); I("rs_rows", s_rsRows, 1, 5); F("rs_paralysis", s_rsParalysis, 0, 30); F("rs_sweep", s_rsSweep, 0, 0.5);
    F("rs_seconds", s_rsSeconds, 0, 60); F("rs_pulse", s_rsPulse, 0.1, 5); F("rs_damage", s_rsDamage, 0, 5); F("rs_hold", s_rsHold, 0, 5); I("rs_arcs", s_rsArcs, 0, 20);
    F("od_cooldown", s_odCd, 0, 600); F("od_seconds", s_odSeconds, 0.5, 60); F("od_step", s_odStep, 0.05, 5); F("od_charged", s_odCharged, 1, 5);
    F("ult_cooldown", s_ultCd, 0, 600); F("ult_radius", s_ultRadius, 5, 60); I("ult_arcs_max", s_ultArcsMax, 0, 64);
    F("blitz_window", s_blitzWindow, 1, 30); F("blitz_length", s_blitzLength, 5, 60); F("blitz_step", s_blitzStep, 1, 10); F("blitz_stealth", s_blitzStealth, 0, 30); F("blitz_power", s_blitzPower, 0.1, 20);
    F("fs_radius", s_fsRadius, 0, 60); F("fs_every", s_fsEvery, 0.5, 60); I("fs_max", s_fsMax, 0, 10); F("fs_cut", s_fsCut, 0, 30);
    F("nc_seconds", s_ncSeconds, 0.5, 60); F("nc_mult", s_ncMult, 1, 5); F("elite_mult", s_eliteMult, 1, 5);
    F("pc_heal", s_pcHeal, 0, 200); F("pc_every", s_pcEvery, 0.5, 60); F("pc_mult", s_pcMult, 1, 5);
    I("code_max", s_codeMax, 1, 8); F("code_every", s_codeEvery, 0.5, 60); F("code_per", s_codePer, 0, 1); F("link_mult", s_linkMult, 1, 5);
    F("vz_radius", s_vzRadius, 0.5, 15); F("vz_seconds", s_vzSeconds, 0, 60); F("vz_arm", s_vzArm, 0, 10);
    F("lp_climb", s_lpClimb, 0.5, 30); F("lp_drop", s_lpDrop, 0.5, 50);
    F("hp_mult", s_hpMult, 0.1, 10); F("stamina_mult", s_staminaMult, 0.1, 20); I("hellbomb_on_exhaustion", s_hellbomb, 0, 1);
    I("hellbomb_explosion", s_hellbombRow, 0, 100000); F("hellbomb_hold", s_hellbombHold, 0, 10);
#undef F
#undef I
#undef CL
}

static int ParseKey(const char* text, Key* out)
{
    Key k = {0, 0, ""};
    char buf[64];
    snprintf(buf, sizeof buf, "%s", text);
    for (char* tok = strtok(buf, "+ \t\r\n"); tok; tok = strtok(NULL, "+ \t\r\n"))
    {
        char t[24];
        size_t n = 0;
        for (; tok[n] && n < sizeof t - 1; n++) t[n] = (char)toupper((unsigned char)tok[n]);
        t[n] = 0;
        if (!strcmp(t, "SHIFT")) k.mods |= KM_SHIFT;
        else if (!strcmp(t, "CTRL") || !strcmp(t, "CONTROL")) k.mods |= KM_CTRL;
        else if (!strcmp(t, "ALT")) k.mods |= KM_ALT;
        else if (k.vk) return 0;  /* two main keys */
        else if (n == 1 && ((t[0] >= 'A' && t[0] <= 'Z') || (t[0] >= '0' && t[0] <= '9'))) k.vk = t[0];
        else if (t[0] == 'F' && n <= 3 && atoi(t + 1) >= 1 && atoi(t + 1) <= 24) k.vk = VK_F1 + atoi(t + 1) - 1;
        else if (!strcmp(t, "MOUSE4")) k.vk = VK_XBUTTON1;
        else if (!strcmp(t, "MOUSE5")) k.vk = VK_XBUTTON2;
        else if (!strcmp(t, "MOUSE3") || !strcmp(t, "MIDDLEMOUSE")) k.vk = VK_MBUTTON;
        else if (t[0] == '#' && atoi(t + 1) > 0 && atoi(t + 1) < 255) k.vk = atoi(t + 1);
        else if (n >= 2 && strspn(t, "0123456789") == n && atoi(t) > 0 && atoi(t) < 255) k.vk = atoi(t);  /* 0.1.0's codes */
        else return 0;
    }
    if (!k.vk)  /* a modifier alone is a key of its own: Ctrl, Alt or Shift */
    {
        if (k.mods == KM_SHIFT) k.vk = VK_SHIFT;
        else if (k.mods == KM_CTRL) k.vk = VK_CONTROL;
        else if (k.mods == KM_ALT) k.vk = VK_MENU;
        else return 0;
        k.mods = 0;
    }
    char name[24];
    if ((k.vk >= 'A' && k.vk <= 'Z') || (k.vk >= '0' && k.vk <= '9')) snprintf(name, sizeof name, "%c", k.vk);
    else if (k.vk >= VK_F1 && k.vk <= VK_F24) snprintf(name, sizeof name, "F%d", k.vk - VK_F1 + 1);
    else if (k.vk == VK_XBUTTON1) snprintf(name, sizeof name, "M4");
    else if (k.vk == VK_XBUTTON2) snprintf(name, sizeof name, "M5");
    else if (k.vk == VK_MBUTTON) snprintf(name, sizeof name, "M3");
    else if (k.vk == VK_SHIFT) snprintf(name, sizeof name, "Shift");
    else if (k.vk == VK_CONTROL) snprintf(name, sizeof name, "Ctrl");
    else if (k.vk == VK_MENU) snprintf(name, sizeof name, "Alt");
    else snprintf(name, sizeof name, "#%d", k.vk);
    snprintf(k.label, sizeof k.label, "%s%s%s%s", k.mods & KM_CTRL ? "Ctrl+" : "", k.mods & KM_ALT ? "Alt+" : "",
        k.mods & KM_SHIFT ? "Shift+" : "", name);
    *out = k;
    return 1;
}

/* The settings file, opened by its UTF-8 path as Windows reads it (plain fopen reads the path in the ANSI code page, so a
 * folder with a non-English letter in it fails). HellForge holds mod folders read-only while the game runs, so the mod
 * never writes it: the release ships it with the defaults (made from the text below by inigen/gen.py). */
static FILE* OpenUtf8(const char* path, const wchar_t* mode)
{
    wchar_t w[MAX_PATH * 2];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, MAX_PATH * 2)) return NULL;
    return _wfopen(w, mode);
}

static void LoadSettings(void)
{
    char path[MAX_PATH * 2], line[256];
    snprintf(path, sizeof path, "%s\\leva.ini", hf->mod_folder(self));
    FILE* f = OpenUtf8(path, L"r");
    if (f) hf->log(self, "settings: read %s", path);
    else
    {
        hf->log(self, "settings: no leva.ini in %s (it ships with the mod), so the built-in defaults; the MODS tab still changes them", path);
        FILE* w = NULL;   /* never written while the game runs (see OpenUtf8); the text stays as the defaults' source */
        if ((f = w) != NULL)
        {
            fprintf(f, "; Leva %s. Keys: a letter, digit, F1-F24, Mouse3, Mouse4 or Mouse5, with Shift+, Ctrl+ or Alt+ in front for a\n"
                       "; combo (Shift+Q), or Shift, Ctrl or Alt alone. The game still sees the keys too, so pick ones it leaves alone.\n"
                       "key_suppress = %s\nkey_disrupt = %s\nkey_ult = %s\n"
                       "; aim_smooth: seconds her aim is averaged over for her lines and fields (her head sways; 0: the raw aim).\n"
                       "aim_smooth = %g\n"
                       "; arc_grenade 1: her arcs are the G-31 Arc grenade's detonation (arc_explosion) set off at each point; 0: arc_mode.\n"
                       "arc_grenade = %d\n"
                       "; arc_mode, how her arcs are made at each point: 1 a bullet fired down from 3 m that bursts like the GL-52\n"
                       "; De-Escalator's shell on whatever it hits (no bounce); 2 the arc_shell projectile itself fired down (222: the GL-52's\n"
                       "; shell, which can bounce away); 3 the GL-52 shell's burst set off directly; 0 arc_explosion set off directly (157: the\n"
                       "; G-31 Arc grenade's, whose electric effect stays on the field). steady: 1 never knocked down.\n"
                       "arc_mode = %d\narc_shell = %d\narc_explosion = %d\nsteady = %d\n"
                       "; native_rows 1: the lockdown bursts as an explosion and arc of her own (copies of the GL-52 burst's, added to the\n"
                       "; game's lists), so rs_damage sets its damage in the game's data; 0: the GL-52 burst, its damage cut as it lands.\n"
                       "native_rows = %d\n"
                       "; Too many arcs at once leave their lightning on the field for good: at most arc_rate arcs a second. Points with an\n"
                       "; enemy within arc_reach m come first (the rest of them wait their turn); with arc_empty = 1 the empty points get\n"
                       "; what the rate leaves over (showing her reach), else none (their charge, paralysis and Voltage still apply).\n"
                       "; arc_burst: extra arcs her ultimate and Blitz may use at once, above the rate.\n"
                       "; arc_shape 2: only arc row 15 takes the Arc Thrower's shape (its bang), row 6's fan keeps hitting; 1: both rows;\n"
                       "; 0: neither. arc_shape 1: her arcs take the ARC-3 Arc Thrower's shape (one beam jumping on from target to target) instead of\n"
                       "; the GL-52 burst's fan; arc_chain / arc_split (0 or more) then set its jumps and split, -1 keeps the shape's own.\n"
                       "; arc_range: how far an arc reaches, in m (0: the burst's own 10 m; the ARC-3's 55 m strands lightning on the field).\n"
                       "; While Leva is on, the GL-52 De-Escalator's own arcs do the same.\n"
                       "arc_rate = %g\narc_reach = %g\narc_empty = %d\narc_burst = %g\narc_shape = %d\narc_range = %g\narc_chain = %d\narc_split = %d\n"
                       "; Rational Suppression (a lockdown): cooldown; a field of rs_columns x rs_rows points over rs_width x rs_depth m,\n"
                       "; rs_distance m ahead, swept in across rs_sweep s per column (enemies under it paralysed rs_paralysis s), then arcs\n"
                       "; again every rs_pulse s for rs_seconds s, each pulse holding the enemies in it rs_hold s more; its arcs deal\n"
                       "; rs_damage of their damage (nothing else adds to it), at most rs_arcs arcs a pulse (enemies first).\n"
                       "rs_cooldown = %g\nrs_distance = %g\nrs_width = %g\nrs_depth = %g\nrs_columns = %d\nrs_rows = %d\nrs_paralysis = %g\nrs_sweep = %g\n"
                       "rs_seconds = %g\nrs_pulse = %g\nrs_damage = %g\nrs_hold = %g\nrs_arcs = %d\n"
                       "; Orderly Disruption (Arc Rounds): cooldown; for od_seconds s her bullets set off an arc where they hit an enemy (one\n"
                       "; every od_step s at most), charging it; on charged enemies those arcs deal od_charged times.\n"
                       "od_cooldown = %g\nod_seconds = %g\nod_step = %g\nod_charged = %g\n"
                       "; Quantum Calculation: cooldown, radius (four rings of arcs out to it), at most ult_arcs_max arcs on enemies.\n"
                       "; Superconduct Blitz: the window after it, the line's length and arc spacing (four arcs at its end, then back along\n"
                       "; it to her), Stealth after it, and its arcs' damage (times the Code it spends: 1 / 1.17 / 1.42 / 2).\n"
                       "ult_cooldown = %g\nult_radius = %g\nult_arcs_max = %d\nblitz_window = %g\nblitz_length = %g\nblitz_step = %g\nblitz_stealth = %g\nblitz_power = %g\n"
                       "; The Fox's Scheme: Wise Support arcs on newly charged enemies within fs_radius m, at most fs_max per fs_every s, each\n"
                       "; fs_cut s off the ultimate's cooldown.\nfs_radius = %g\nfs_every = %g\nfs_max = %d\nfs_cut = %g\n"
                       "; Negative Charge: how long it lasts, her arcs' and her shots' damage on charged enemies.\n"
                       "nc_seconds = %g\nnc_mult = %g\nelite_mult = %g\n"
                       "; Positive Charge (on her): pc_heal health every pc_every s, her arcs on charged enemies pc_mult times again.\n"
                       "pc_heal = %g\npc_every = %g\npc_mult = %g\n"
                       "; Superconduct Code: most stacks, a stack every code_every s, code_per damage per stack, Superconduct Link at full.\n"
                       "code_max = %d\ncode_every = %g\ncode_per = %g\nlink_mult = %g\n"
                       "; Voltage zones: radius, how long they last, how long before they can burst.\nvz_radius = %g\nvz_seconds = %g\nvz_arm = %g\n"
                       "; Her arcs find the ground from lp_climb m above her height down to lp_drop m below it.\nlp_climb = %g\nlp_drop = %g\n"
                       "; The T-Doll's body: hp_mult times a Helldiver's health, stamina_mult times her sprint before the bar empties.\n"
                       "hp_mult = %g\nstamina_mult = %g\n"
                       "; If her stamina ever runs out, hellbomb_on_exhaustion (1) sets off hellbomb_explosion (125: the B-100 Portable\n"
                       "; Hellbomb's) on her, once the bar has stayed empty hellbomb_hold s (it flickers to 0 at times).\n"
                       "hellbomb_on_exhaustion = %d\nhellbomb_explosion = %d\nhellbomb_hold = %g\n",
                VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, (double)s_aimSmooth, s_arcGrenade, s_arcMode, s_arcShell, s_arcRow, s_steady, s_nativeRows, (double)s_arcRate, (double)s_arcReach, s_arcEmpty, (double)s_arcBurst, s_arcStyle, (double)s_arcRange, s_arcChain, s_arcSplit,
                (double)s_rsCd, (double)s_rsDistance, (double)s_rsWidth, (double)s_rsDepth, s_rsColumns, s_rsRows, (double)s_rsParalysis, (double)s_rsSweep,
                (double)s_rsSeconds, (double)s_rsPulse, (double)s_rsDamage, (double)s_rsHold, s_rsArcs,
                (double)s_odCd, (double)s_odSeconds, (double)s_odStep, (double)s_odCharged,
                (double)s_ultCd, (double)s_ultRadius, s_ultArcsMax, (double)s_blitzWindow, (double)s_blitzLength, (double)s_blitzStep, (double)s_blitzStealth, (double)s_blitzPower,
                (double)s_fsRadius, (double)s_fsEvery, s_fsMax, (double)s_fsCut, (double)s_ncSeconds, (double)s_ncMult, (double)s_eliteMult,
                (double)s_pcHeal, (double)s_pcEvery, (double)s_pcMult, s_codeMax, (double)s_codeEvery, (double)s_codePer, (double)s_linkMult,
                (double)s_vzRadius, (double)s_vzSeconds, (double)s_vzArm, (double)s_lpClimb, (double)s_lpDrop,
                (double)s_hpMult, (double)s_staminaMult, s_hellbomb, s_hellbombRow, (double)s_hellbombHold);
            fclose(f);
        }
        return;
    }
    while (fgets(line, sizeof line, f))
    {
        char* c = strchr(line, ';');
        if (c) *c = 0;
        if (line[strspn(line, " \t")] == '#') continue;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char key[64];
        if (sscanf(line, " %63s", key) != 1) continue;
        int k = !strcmp(key, "key_suppress") ? 0 : !strcmp(key, "key_disrupt") ? 1 : !strcmp(key, "key_ult") ? 2 : -1;
        if (k >= 0)
        {
            Key parsed;
            if (ParseKey(eq + 1, &parsed)) s_keys[k] = parsed;
            else s_badKey |= 1 << k;
            continue;
        }
        char* end;
        double v = strtod(eq + 1, &end);
        if (end != eq + 1) Setting(key, v);
    }
    fclose(f);
}

/* ---- the clock, the player ------------------------------------------------------------------------------------ */
static double g_now;
static atomic_uint g_avatar;      /* her Helldiver's entity (0: none) */
static uint32_t g_avatarNet;      /* its network id */
static int g_alive, g_inMission;
static uint32_t g_weapon[4];      /* the held weapons by selection (1 primary, 2 secondary, 3 support): entities */
static uint64_t g_weaponRes[4];

static uint32_t Word(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t Quad(const uint8_t* p) { uint64_t v; memcpy(&v, p, 8); return v; }
static int AddressOk(uintptr_t a, size_t n) { return n && a >= 0x10000 && a < UINT64_C(0x800000000000) && n <= UINT64_C(0x800000000000) - a; }
static int ReadAt(uintptr_t a, void* out, size_t n) { return AddressOk(a, n) && hf->read(a, out, n) == 0; }
static uintptr_t Pointer(uintptr_t a) { uintptr_t v = 0; return ReadAt(a, &v, 8) && AddressOk(v, 1) ? v : 0; }
static int Lookup(uintptr_t at, uint32_t key, uint32_t limit, uint32_t* index)
{
    uint8_t h[20], row[8];
    if (!ReadAt(at, h, sizeof h)) return 0;
    uintptr_t entries = (uintptr_t)Quad(h);
    uint32_t n = Word(h + 8), empty = Word(h + 12), mult = Word(h + 16);
    if (!AddressOk(entries, 8) || !n || n > limit || (n & (n - 1)) || key == empty) return 0;
    for (unsigned i = 0; i < n && i < 128; i++)
    {
        if (!ReadAt(entries + 8u * ((key * mult + i) & (n - 1)), row, 8)) return 0;
        if (Word(row) == key) { *index = Word(row + 4); return *index != UINT32_MAX; }
        if (Word(row) == empty) return 0;
    }
    return 0;
}
/* An entity's descriptor: its resource (+0), entity (+8), unit (+12), flags (+20, bit 0: this machine's authority) */
static int Descriptor(uint32_t entity, uint8_t out[24])
{
    if (!game || !entity || entity == UINT32_MAX || entity == 0x7FFF) return 0;
    uintptr_t root = Pointer(game + 0x346BF98);
    uint32_t index;
    if (!root || !Lookup(root + 0xF1AEB0, entity, 1048576, &index) || index >= 262144) return 0;
    return ReadAt(root + 0xF32F18 + 24u * index, out, 24) && Word(out + 8) == entity;
}
/* A node's world position in a unit's scene graph (64-byte matrices, translation at +0x30) */
static int UnitNode(uint32_t unit, uint32_t node, float out[3])
{
    if (!exe) return 0;
    uintptr_t registry = Pointer(exe + 0x1A100F0), generations, objects, object, nodes;
    uint32_t count, id, slot = unit & 0x3FFFFF;
    uint8_t generation;
    if (!registry || !ReadAt(registry + 0x98, &count, 4) || count > 0x400000 || slot >= count ||
        !(generations = Pointer(registry + 0xA0)) || !ReadAt(generations + slot, &generation, 1) || generation != (uint8_t)(unit >> 22) ||
        !(objects = Pointer(registry + 0x88)) || !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit ||
        !ReadAt(object + 0x70, &count, 4) || node >= count || count > 4096 || !(nodes = Pointer(object + 0x88)))
        return 0;
    if (!ReadAt(nodes + 64u * node + 48, out, 12)) return 0;
    for (int i = 0; i < 3; i++) if (!isfinite(out[i]) || fabsf(out[i]) > 100000) return 0;
    return 1;
}
static int PlayerPose(float pos[3], float aim[3])
{
    uint8_t id[24];
    uint32_t avatar = atomic_load(&g_avatar);
    if (!Descriptor(avatar, id) || Quad(id) != AVATAR) return 0;
    uint32_t unit = Word(id + 12);
    if (!UnitNode(unit, 0, pos)) return 0;
    if (aim)
    {
        float head[3], ahead[3];
        if (!UnitNode(unit, NODE_HEAD, head) || !UnitNode(unit, NODE_HEAD_AIM, ahead)) return 0;
        float d[3] = {ahead[0] - head[0], ahead[1] - head[1], ahead[2] - head[2]};
        float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (!(n > 0.1f)) return 0;
        for (int i = 0; i < 3; i++) aim[i] = d[i] / n;
    }
    return 1;
}
static float Dist2(const float* a, const float* b) { float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; return x * x + y * y + z * z; }


/* ---- state ---------------------------------------------------------------------------------------------------- */
static double g_cdSuppress, g_cdDisrupt, g_cdUlt;   /* ready at */
static uint32_t g_lastTarget;
static float g_lastPos[3];
static double g_lastAt;
static int g_code;                       /* Superconduct Code */
static double g_codeAt, g_pcAt;          /* the last passive Code stack, the last Positive Charge heal */
static double g_blitzUntil;              /* Superconduct Blitz can be used until */
static double g_blitzHitFrom, g_blitzHitUntil;
static float g_blitzMult = 1;
static double g_stealthUntil;
static int g_stealthOn;
static double g_fsWindowAt;              /* the Fox's Scheme: its current window started at */
static int g_fsUsed;
static atomic_ullong g_shieldUntilMs;
#define OWN 64
typedef struct Pulse { float p[3]; double at; uint32_t row; } Pulse;
static _Atomic unsigned long long g_ownMs[OWN];
static float g_ownPos[OWN][3];
static unsigned g_ownNext;
static Pulse g_pulses[OWN];
static int g_npulse;
typedef struct Mark { uint32_t entity; double until; } Mark;   /* Negative Charge */
static Mark g_marks[128];
typedef struct Zone { float p[3]; double from, until; } Zone;  /* Voltage */
static Zone g_zones[32];
static int g_nzone;
static void LearnNet(void);
static unsigned g_fieldHits;
static double g_probeUntil; static float g_probeAt[3]; static unsigned g_probeCasts, g_probeLines;
static double g_tagDamage[5];   /* her arcs' damage this mission by skill (after her multipliers): arc, lockdown, Blitz, Arc Rounds, shots */
static unsigned g_casts[4], g_charged, g_boosted, g_selfSpared, g_knockdowns, g_wise, g_zonePops, g_stuns, g_heals;

static void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void Log(const char* fmt, ...)
{
    char buf[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(buf, sizeof buf, fmt, a);
    va_end(a);
    hf->log(self, "%s", buf);
}

/* An action's answer: done, requested or queued count as done; the log says why one was not */
static int Act(const char* what, int r)
{
    if (r == HF_ACT_DONE || r == HF_ACT_REQUESTED || r == HF_ACT_QUEUED) return 1;
    static unsigned noted;
    if (noted++ < 20)
        Log("%s: %s (%s)", what, r == HF_ACT_WAITING ? "waiting for its packages" : "refused", hf->last_log ? hf->last_log(self) : "");
    return 0;
}

/* ---- Negative Charge, Superconduct Code, Voltage ---------------------------------------------------------------- */
static Mark* MarkOf(uint32_t entity)
{
    for (int i = 0; i < 128; i++) if (g_marks[i].entity == entity && g_marks[i].until > g_now) return &g_marks[i];
    return NULL;
}
static int ChargedCount(void)
{
    int n = 0;
    for (int i = 0; i < 128; i++) n += g_marks[i].until > g_now;
    return n;
}
static void GainCode(int n)
{
    int before = g_code;
    g_code += n;
    if (g_code > s_codeMax) g_code = s_codeMax;
    if (g_code < 0) g_code = 0;
    if (g_code == s_codeMax && before < s_codeMax) Log("Superconduct Link: %d Code", g_code);
}
static void Zone_(const float at[3])
{
    if (s_vzSeconds <= 0) return;
    Zone* z = &g_zones[g_nzone++ % 32];
    memcpy(z->p, at, sizeof z->p);
    z->from = g_now + s_vzArm;
    z->until = g_now + s_vzSeconds;
}
static int ZoneCount(void)
{
    int n = 0;
    for (int i = 0; i < 32; i++) n += g_zones[i].until > g_now;
    return n;
}

/* ---- stealth (as Lenna's) ---------------------------------------------------------------------------------------- */
static void Stealth(int on)
{
    HfField sup = HF_FIELD(WeaponData, suppressed);
    if (!on)
    {
        if (g_stealthOn)
        {
            hf->set_passives(self, HF_PASSIVE_KIT, HF_PASSIVE_KIT);
            for (int i = 1; i <= 3; i++) if (g_weaponRes[i]) hf->set_live(self, g_weaponRes[i], &sup, NULL, HF_LIVE_SHARED);
            Log("stealth over");
        }
        g_stealthOn = 0;
        return;
    }
    g_stealthOn = 1;
    int p = hf->set_passives(self, HF_PASSIVE_KIT, 32);  /* REDUCED SIGNATURE in the helmet's slot */
    HfValue one = HF_U8(1);
    int n = 0;
    for (int i = 1; i <= 3; i++)
        if (g_weaponRes[i] && hf->set_live(self, g_weaponRes[i], &sup, &one, HF_LIVE_SHARED)) n++;
    Log("stealth for %.0f s: Reduced Signature %s; %d weapon(s) suppressed", (double)s_blitzStealth, p ? "held" : "refused", n);
}

/* ---- the ground under her arcs: the game's raycast (as Helldiver Survival Rework's cover check calls it) --------- */
typedef uint32_t (*WorldFn)(const void*);
typedef uint32_t (*RayFn)(uint32_t, const void*, const void*, float, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t);
static WorldFn g_worldId;
static RayFn g_raycast;
static int QueryIdle(void)
{
    uintptr_t scheduler = Pointer(game + 0x347D7E0);
    uint32_t count, jobs[72];
    if (!scheduler || !ReadAt(scheduler, &count, 4) || count || !ReadAt(scheduler + 0x40008, jobs, sizeof jobs)) return 0;
    for (unsigned i = 0; i < 24; i++) if (jobs[3 * i + 2] != 1) return 0;
    return 1;
}
/* An actor's collision filter name (as Helldiver Survival Rework's cover check reads it: the actor's generation-tagged
 * pool entry, its physics body, the filter table's names); 0 when it cannot be identified */
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
/* Every surface under (x, y) from lp_climb m above `ref` down to lp_drop m below it (terrain, rocks, floors, roofs),
 * highest first; how many */
static int Surfaces(uint32_t unit, float x, float y, float ref, float* z, int max)
{
    float top[3] = {x, y, ref + s_lpClimb}, down[3] = {0, 0, -1}, len = s_lpClimb + s_lpDrop;
    uintptr_t world = Pointer(game + 0x346BFA0);
    if (!world || !g_raycast || !g_worldId) return 0;
    uint32_t id = g_worldId((const void*)world);
    if (id >= 4) return 0;
    uint8_t hits[16 * 44];
    memset(hits, 0, sizeof hits);
    uint32_t n = g_raycast(id, top, down, len, 2, 1, 0, 0x80000009u, unit, hits, 16);
    if (n > 16) return 0;
    int k = 0;
    for (uint32_t i = 0; i < n && k < max; i++)
    {
        float d;
        memcpy(&d, hits + 44u * i + 24, 4);
        uint32_t hu = Word(hits + 44u * i + 28);
        if (hu == unit || !isfinite(d) || d < 0.02f || d > len) continue;
        if (ActorFilter(Word(hits + 44u * i + 32), hu) == FILTER_FOLIAGE) continue;   /* leaves and canopies are no floor */
        z[k++] = top[2] - d;
    }
    for (int i = 1; i < k; i++)        /* highest first */
        for (int j = i; j > 0 && z[j] > z[j - 1]; j--) { float t = z[j]; z[j] = z[j - 1]; z[j - 1] = t; }
    return k;
}

/* ---- her blasts ---------------------------------------------------------------------------------------------------- */
static Pulse g_retry[16];  /* blasts waiting for their packages (at: give up then) */
static int g_retryCount;
#define TAG_ARC 0      /* an ordinary arc of hers */
#define TAG_WISE 5     /* a Wise Support arc (counted with her arcs) */
static uint32_t g_queueTag;   /* the tag the next shots get */
#define TAG_FIELD 1    /* Rational Suppression's lockdown */
#define TAG_BLITZ 2    /* Superconduct Blitz */
#define TAG_ROUNDS 3   /* Orderly Disruption's Arc Rounds */
static void Own(const float at[3], uint32_t tag)
{
    unsigned slot = g_ownNext++ % OWN;
    memcpy(g_ownPos[slot], at, sizeof g_ownPos[slot]);
    atomic_store(&g_ownMs[slot], GetTickCount64());
    Pulse* p = &g_pulses[g_npulse++ % OWN];
    memcpy(p->p, at, sizeof p->p);
    p->at = g_now;
    p->row = tag;
}
/* One of her arcs: it never touches or throws her */
/* The GL-52's burst on a straight bullet: a copy of the Liberator's bullet whose impact explosion is the GL-52 shell's
 * (read from a copy of the shell's row once the projectile rows are ready) */
static uint32_t g_shellCopy, g_bolt, g_glBurst, g_boltDamage, g_fieldBolt;
static int g_ntState;   /* her own lockdown explosion: 1 live (parts_native) */
static uint32_t g_ntExpId;
static int Fire(int mode, const float at[3], uint32_t tag)
{
    float from[3] = {at[0], at[1], at[2] + 3.0f}, down[3] = {0, 0, -1};
    /* the lockdown's own burst rides its own bullet whatever arc_mode says (it cannot be set off directly) */
    if (tag == TAG_FIELD && g_ntState == 1 && g_fieldBolt && !s_arcGrenade) return hf->spawn_projectile(self, g_fieldBolt, from, down);
    if (s_arcGrenade && s_arcRow && mode != 2) return hf->spawn_explosion(self, (uint32_t)s_arcRow, at);   /* the G-31's own blast */
    if (mode == 1 && g_bolt && g_glBurst) return hf->spawn_projectile(self, g_bolt, from, down);
    if (mode == 1 || mode == 3) { if (g_glBurst) return hf->spawn_explosion(self, g_glBurst, at); mode = 2; }
    if (mode == 2 && s_arcShell) return hf->spawn_projectile(self, (uint32_t)s_arcShell, from, down);
    return s_arcRow ? hf->spawn_explosion(self, (uint32_t)s_arcRow, at) : 0;
}
/* One of her arcs: it never touches or throws her */
static int Zap(const float at[3], const char* what, uint32_t tag)
{
    Own(at, tag);
    int r = Fire(s_arcMode, at, tag);
    if (r == HF_ACT_WAITING && g_retryCount < 16)  /* its packages are loading: again next frames */
    {
        memcpy(g_retry[g_retryCount].p, at, sizeof g_retry[0].p);
        g_retry[g_retryCount].at = g_now + 3;
        g_retry[g_retryCount].row = (uint32_t)s_arcMode;
        g_retryCount++;
    }
    if (r != HF_ACT_WAITING) Act(what, r);
    return r;
}
/* ---- private explosion and arc rows ------------------------------------------------------------------------------ *
 * The lockdown's burst as an explosion of her own: a copy of the GL-52 De-Escalator's burst (explosion 41) under a new
 * ExplosionType, whose blast deals a damage type of hers and whose arc is a copy of the burst's arc under a new ArcType,
 * dealing a damage type of hers too. The lockdown's damage is then set in the game's own data (rs_damage x the burst's),
 * not by sorting hits afterwards.
 *
 * The SDK adds projectiles and damage types but not explosions or arcs, so the game's two lists are extended here, as
 * More Plasma Stratagems 1.1.0 by HadePasianax (MIT) extends the explosion list (its native_plasma_explosions.h):
 *   game.dll + 0x37CC920  ExplosionType -> 152-byte row, 423 slots (HD2Runtime's research_event_actions: table, bound
 *              423, stride 152); its reader at 0x13C0DC0 and bound at 0x13C0DB5 are checked before anything is written.
 *   the ArcType list: 104-byte rows (ExplosionInfo +120 names the arc an explosion releases, ArcInfo +36 its damage:
 *              HD2Runtime's player_weapon_writes), found at run time as the run of row pointers 104 bytes apart.
 * Each list's pointers are copied into a block within 2 GB of game.dll with the new row after them, then every
 * instruction that addresses the list (indexed or rip-relative, decoded and range-checked) is pointed at the block and
 * the bounds beside its readers are raised. Every site is read back first; an instruction naming the list in a form not
 * understood refuses the whole change, and a failed write puts back what was written. */
#define NT_IMAGE 0x4744000u
#define NT_EXP_TABLE 0x37CC920u
#define NT_EXP_READER 0x13C0DC0u
#define NT_EXP_BOUND 0x13C0DB5u
#define NT_EXP_NATIVE 423u
#define NT_DMG_SITE 0x694E4Eu      /* mov rcx, [rdi + rax*8 + <the damage list>]: HellForge's damage_sites.h, first site */
#define NT_MAX_PATCHES 256u

typedef struct NtPatch { uintptr_t at; uint32_t before, after; } NtPatch;
typedef struct NtList
{
    const char* name;
    uint32_t rva, native, count;  /* the list's RVA, its slots, its slots with ours */
    int boundRequired;
    NtPatch patch[NT_MAX_PATCHES];
    unsigned npatch, refs, bounds;
    uintptr_t* block;
} NtList;
static NtList g_ntExp = {"explosion", NT_EXP_TABLE, NT_EXP_NATIVE, NT_EXP_NATIVE + 1, 1};
static NtList g_ntArc = {"arc", 0, 0, 0, 0};
static uint8_t g_ntExpRow[152] __attribute__((aligned(16)));
static uint8_t g_ntArcRow[104] __attribute__((aligned(16)));
static uint32_t g_ntArcId, g_ntBaseArc;            /* her explosion, her arc (0: none), the burst's own arc */
static uint32_t g_ntBlastDamage, g_ntArcDamage;               /* her damage types (add_damage) */
static uint8_t g_ntBlastVanilla[76], g_ntArcVanilla[76];      /* the burst's and its arc's damage rows as the game has them */
static double g_ntNext, g_ntApplied = -1;
static unsigned g_ntTries, g_ntBursts;
static const char* g_ntWhy = "not tried yet";
static uintptr_t g_ntWhere;
static uintptr_t g_ntBase;

static void NtBolt(void);
static int NtFail(const char* why, uintptr_t where) { g_ntWhy = why; g_ntWhere = where; return 0; }
static int NtRead(uintptr_t at, void* out, size_t n) { return !hf->read(at, out, n); }

static int NtAdd(NtList* L, uintptr_t at, uint32_t before, uint32_t after)
{
    for (unsigned i = 0; i < L->npatch; i++)
        if (L->patch[i].at == at) return L->patch[i].before == before && L->patch[i].after == after;
    if (L->npatch >= NT_MAX_PATCHES) return NtFail("too many sites address the list", at - g_ntBase);
    L->patch[L->npatch++] = (NtPatch){at, before, after};
    return 1;
}

/* One code section's sites for a list: [image base + index*8 + list], rip-relative loads of a slot, and fixed slots
 * through an image-base register; then the bounds (cmp with the slot count) within 128 bytes before a site */
static int NtScan(NtList* L, const uint8_t* code, uint32_t first, uint32_t size, uintptr_t block)
{
    static uintptr_t sites[NT_MAX_PATCHES];
    unsigned count = 0;
    const uint32_t lo = L->rva, hi = L->rva + L->native * 8;
    for (uint32_t i = 0; i + 8 <= size; i++)
    {
        const uint8_t* p = code + i;
        if ((p[0] & 0xf8) != 0x48) continue;
        unsigned imm = 0;
        if (p[1] == 0x83 && (p[2] & 0x38) == 0x38) imm = 1;
        else if ((p[1] == 0x81 && (p[2] & 0x38) == 0x38) || (p[1] == 0xc7 && !(p[2] & 0x38))) imm = 4;
        else if (p[1] != 0x8b && p[1] != 0x8d && p[1] != 0x89) continue;
        unsigned dispAt = 0, length = 0;
        uint32_t disp = 0;
        uintptr_t target = 0;
        int rip = 0;
        if ((p[2] & 0xc7) == 0x84 || ((p[2] & 0xc7) == 0x04 && (p[3] & 7) == 5))
        {
            if ((p[3] >> 6) != 3 && !((p[3] & 0x38) == 0x20 && !(p[0] & 2))) continue;
            memcpy(&disp, p + 4, 4);
            if (disp < lo || disp >= hi || (disp - lo) % 8) continue;
            dispAt = 4; length = 8 + imm;
            target = block + (disp - lo);
        }
        else if ((p[2] & 0xc7) == 0x05)
        {
            memcpy(&disp, p + 3, 4);
            length = 7 + imm;
            uintptr_t old = g_ntBase + first + i + length + (int32_t)disp;
            if (old < g_ntBase + lo || old >= g_ntBase + hi || (old - g_ntBase - lo) % 8) continue;
            dispAt = 3; rip = 1;
            target = block + (old - g_ntBase - lo);
        }
        else if ((p[2] & 0xc0) == 0x80 && (p[2] & 7) != 4)
        {
            memcpy(&disp, p + 3, 4);
            if (disp < lo || disp >= hi || (disp - lo) % 8) continue;
            dispAt = 3; length = 7 + imm;
            target = block + (disp - lo);
        }
        if (!dispAt || i + length > size) continue;
        if (count >= NT_MAX_PATCHES) return NtFail("too many sites address the list", first + i);
        sites[count++] = g_ntBase + first + i;
        int64_t adjusted = rip ? (int64_t)target - (int64_t)(g_ntBase + first + i + length) : (int64_t)target - (int64_t)g_ntBase;
        if (adjusted < INT32_MIN || adjusted > INT32_MAX || !NtAdd(L, g_ntBase + first + i + dispAt, disp, (uint32_t)(int32_t)adjusted)) return 0;
    }
    /* the bounds: cmp eax, imm32 / cmp r32, imm32 / cmp r32, imm8 (an optional REX before), with the slot count */
    for (uint32_t i = 0; i + 7 <= size; i++)
    {
        const uint8_t* p = code + i;
        unsigned rex = (p[0] & 0xf0) == 0x40 && !(p[0] & 8) ? 1 : 0;
        const uint8_t* q = p + rex;
        unsigned off = 0, wide = 4;
        if (q[0] == 0x3d) off = 1;
        else if (q[0] == 0x81 && (q[1] & 0xf8) == 0xf8) off = 2;
        else if (q[0] == 0x83 && (q[1] & 0xf8) == 0xf8) { off = 2; wide = 1; }
        if (!off) continue;
        uint32_t value = 0;
        memcpy(&value, q + off, wide);
        if (value != L->native) continue;
        uintptr_t at = g_ntBase + first + i;
        int close = 0;
        for (unsigned k = 0; k < count; k++)
            if (at <= sites[k] && sites[k] - at <= 128) { close = 1; break; }
        if (!close) continue;
        uint32_t before, after;
        memcpy(&before, q + off, 4);
        after = before;
        if (wide == 4) after = L->count;
        else after = (before & 0xFFFFFF00u) | (L->count & 0xFF);
        if (wide == 1 && L->count > 0x7F) continue;
        if (!NtAdd(L, at + rex + off, before, after)) return 0;
        L->bounds++;
        i += rex + off + wide - 1;
    }
    /* the list's address in a form not understood: change nothing */
    for (uint32_t i = 0; i + 4 <= size; i++)
    {
        uint32_t value;
        memcpy(&value, code + i, 4);
        if (value < lo || value >= hi || (value - lo) % 8) continue;
        int found = 0;
        for (unsigned k = 0; k < L->npatch; k++)
            if (L->patch[k].at == g_ntBase + first + i) { found = 1; break; }
        if (!found) return NtFail("an instruction names the list in a form not understood", first + i);
    }
    L->refs += count;
    return 1;
}

static uint8_t* NtNear(size_t size)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t step = si.dwAllocationGranularity;
    for (uintptr_t at = (g_ntBase + NT_IMAGE + step - 1) & ~(step - 1); at < g_ntBase + 0x70000000u; at += step)
    {
        void* p = VirtualAlloc((void*)at, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (p) return (uint8_t*)p;
    }
    return NULL;
}

/* The game's code, read in pieces (a read across a protected page can fail where its pages read one by one) */
static int NtCode(uintptr_t at, uint8_t* copy, uint32_t length)
{
    for (uint32_t off = 0; off < length;)
    {
        uint32_t n = length - off < 32768 ? length - off : 32768;
        if (!NtRead(at + off, copy + off, n))
            for (uint32_t page = 0; page < n;)
            {
                uint32_t m = n - page < 4096 ? n - page : 4096;
                if (!NtRead(at + off + page, copy + off + page, m)) return NtFail("a code page cannot be read", at + off + page - g_ntBase);
                page += m;
            }
        off += n;
    }
    return 1;
}

/* Writes a list's sites, reading each back; on a failure puts back every one written (the failed one too) */
static int NtApply(NtList* L)
{
    unsigned done = 0;
    for (; done < L->npatch; done++)
    {
        uint32_t check = 0;
        if (hf->write(L->patch[done].at, &L->patch[done].after, 4) || !NtRead(L->patch[done].at, &check, 4) || check != L->patch[done].after) break;
        FlushInstructionCache(GetCurrentProcess(), (void*)L->patch[done].at, 4);
    }
    if (done == L->npatch) return 1;
    for (unsigned k = done + 1; k-- > 0;)
    {
        hf->write(L->patch[k].at, &L->patch[k].before, 4);
        FlushInstructionCache(GetCurrentProcess(), (void*)L->patch[k].at, 4);
    }
    return NtFail("a site could not be written (all put back)", L->patch[done].at - g_ntBase);
}

/* The ArcType list: a run of row pointers whose rows carry their own index at +0 and lie 104 bytes apart, in the data
 * beside the other settings lists; exactly one such run, or none is used */
static int NtFindArcs(void)
{
    enum { FROM = 0x37C0000u, TO = 0x37D7000u };
    static uintptr_t q[(TO - FROM) / 8];
    if (!NtRead(g_ntBase + FROM, q, sizeof q)) return NtFail("the settings lists cannot be read", FROM);
    uint32_t found = 0, slots = 0;
    for (uint32_t i = 0; i + 4 < (TO - FROM) / 8; i++)
    {
        if (!AddressOk(q[i + 1], 104) || q[i + 2] - q[i + 1] != 104 || q[i + 3] - q[i + 2] != 104) continue;
        uint32_t n = 1;
        for (; i + n < (TO - FROM) / 8; n++)
        {
            uint32_t id;
            if (!AddressOk(q[i + n], 104) || !NtRead(q[i + n], &id, 4) || id != n) break;
            if (n > 1 && q[i + n] - q[i + n - 1] != 104) break;
        }
        if (n < 8) continue;
        if (found) return NtFail("more than one list looks like the arcs'", FROM + i * 8);
        found = FROM + i * 8;
        slots = n;
        i += n;
    }
    if (!found) return NtFail("the arc list was not found", 0);
    g_ntArc.rva = found;
    g_ntArc.native = slots;
    g_ntArc.count = slots + 1;
    return 1;
}

/* A vanilla damage row, read through the damage list as the game uses it now (HellForge moves it: the first of its
 * sites holds the list's current place) */
static int NtDamageRow(uint32_t id, uint8_t out[76])
{
    uint32_t disp;
    uintptr_t row;
    if (!NtRead(g_ntBase + NT_DMG_SITE + 4, &disp, 4) || !NtRead(g_ntBase + disp + 8u * id, &row, 8) || !AddressOk(row, 76) ||
        !NtRead(row, out, 76))
        return 0;
    uint32_t have;
    memcpy(&have, out, 4);
    return have == id;
}

/* Her damage rows: the burst's and its arc's, at rs_damage of their damage (armour penetration, statuses kept) */
static void NtDamage(void)
{
    if (g_ntState != 1) return;
    uint32_t ids[2] = {g_ntBlastDamage, g_ntArcDamage};
    uint8_t* from[2] = {g_ntBlastVanilla, g_ntArcVanilla};
    for (int k = 0; k < 2; k++)
    {
        HfDamageInfo* row = (HfDamageInfo*)hf->damage_row(ids[k]);
        if (!row) continue;
        uint32_t id = row->id;
        memcpy(row, from[k], 76);
        row->id = id;
        const HfDamageInfo* v = (const HfDamageInfo*)from[k];
        row->standard_damage = (int32_t)floor(v->standard_damage * (double)s_rsDamage + 0.5);
        row->durable_damage = (int32_t)floor(v->durable_damage * (double)s_rsDamage + 0.5);
    }
    g_ntApplied = s_rsDamage;
}

static int NtInstall(void)
{
    size_t image = 0;
    g_ntBase = hf->module_base("game.dll", &image);
    if (!g_ntBase || image != NT_IMAGE) return NtFail("not the supported game.dll", 0);
    static const uint8_t reader[8] = {0x4c, 0x8b, 0xb4, 0xc1, 0x20, 0xc9, 0x7c, 0x03}, bound[5] = {0x3d, 0xa7, 0x01, 0, 0};
    uint8_t got[8];
    if (!NtRead(g_ntBase + NT_EXP_READER, got, 8) || memcmp(got, reader, 8))
        return NtFail("the explosion list's reader is not the game's (another mod may have moved the list)", NT_EXP_READER);
    if (!NtRead(g_ntBase + NT_EXP_BOUND, got, 5) || memcmp(got, bound, 5)) return NtFail("the explosion list's bound is not the game's", NT_EXP_BOUND);
    /* the explosion list's rows, the burst's row and its arc */
    static uintptr_t exp[NT_EXP_NATIVE];
    if (!NtRead(g_ntBase + NT_EXP_TABLE, exp, sizeof exp)) return NtFail("the explosion list cannot be read", NT_EXP_TABLE);
    for (uint32_t i = 1; i < NT_EXP_NATIVE; i++)
    {
        uint32_t id;
        if (!AddressOk(exp[i], 152) || !NtRead(exp[i], &id, 4) || id != i) return NtFail("the explosion rows are not loaded yet", i);
    }
    if (!g_glBurst || g_glBurst >= NT_EXP_NATIVE || !NtRead(exp[g_glBurst], g_ntExpRow, 152)) return NtFail("the GL-52 burst's row cannot be read", g_glBurst);
    uint32_t blastDamage, arcType;
    memcpy(&blastDamage, g_ntExpRow + 4, 4);
    memcpy(&arcType, g_ntExpRow + 120, 4);
    if (!NtDamageRow(blastDamage, g_ntBlastVanilla)) return NtFail("the burst's damage row cannot be read", blastDamage);
    /* the arc list (optional: without it her explosion keeps the burst's arc and its damage) */
    int arcs = NtFindArcs();
    uintptr_t arcRow = 0;
    if (arcs && (arcType == 0 || arcType >= g_ntArc.native || !NtRead(g_ntBase + g_ntArc.rva + 8u * arcType, &arcRow, 8) ||
                 !NtRead(arcRow, g_ntArcRow, 104)))
        arcs = NtFail("the burst's arc row cannot be read", arcType);
    uint32_t arcDamage = 0;
    if (arcs)
    {
        memcpy(&arcDamage, g_ntArcRow + 36, 4);
        if (!NtDamageRow(arcDamage, g_ntArcVanilla)) arcs = NtFail("the arc's damage row cannot be read", arcDamage);
    }
    const char* arcWhy = arcs ? NULL : g_ntWhy;
    /* the PE headers: the code sections to scan */
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS64 nt;
    if (!NtRead(g_ntBase, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
        !NtRead(g_ntBase + (uint32_t)dos.e_lfanew, &nt, sizeof nt) || nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.NumberOfSections > 32)
        return NtFail("the game's headers cannot be read", 0);
    uint8_t* block = NtNear(65536);
    if (!block) return NtFail("no memory near the game for the lists", 0);
    g_ntExp.block = (uintptr_t*)block;
    memcpy(g_ntExp.block, exp, sizeof exp);
    g_ntExp.npatch = g_ntExp.refs = g_ntExp.bounds = 0;
    if (arcs)
    {
        g_ntArc.block = (uintptr_t*)(block + 8192);
        if (!NtRead(g_ntBase + g_ntArc.rva, g_ntArc.block, 8u * g_ntArc.native)) arcs = 0, arcWhy = "the arc list cannot be copied";
        g_ntArc.npatch = g_ntArc.refs = g_ntArc.bounds = 0;
    }
    uintptr_t sections = g_ntBase + (uint32_t)dos.e_lfanew + 24 + nt.FileHeader.SizeOfOptionalHeader;
    int ok = 1;
    for (unsigned s = 0; s < nt.FileHeader.NumberOfSections && ok; s++)
    {
        IMAGE_SECTION_HEADER sh;
        if (!NtRead(sections + s * sizeof sh, &sh, sizeof sh)) { ok = NtFail("a section header cannot be read", 0); break; }
        if (!(sh.Characteristics & IMAGE_SCN_MEM_EXECUTE) || sh.VirtualAddress >= 0x2111000u) continue;
        uint32_t length = sh.Misc.VirtualSize;
        if (!length || sh.VirtualAddress >= NT_IMAGE || length > NT_IMAGE - sh.VirtualAddress) { ok = NtFail("a code section is out of range", sh.VirtualAddress); break; }
        uint8_t* copy = (uint8_t*)VirtualAlloc(NULL, length, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!copy) { ok = NtFail("no memory to read the game's code", 0); break; }
        if (!NtCode(g_ntBase + sh.VirtualAddress, copy, length) ||
            !NtScan(&g_ntExp, copy, sh.VirtualAddress, length, (uintptr_t)g_ntExp.block)) ok = 0;
        if (ok && arcs)
        {
            const char* keep = g_ntWhy;
            uintptr_t keepAt = g_ntWhere;
            if (!NtScan(&g_ntArc, copy, sh.VirtualAddress, length, (uintptr_t)g_ntArc.block)) { arcs = 0; arcWhy = g_ntWhy; }
            g_ntWhy = keep; g_ntWhere = keepAt;
        }
        VirtualFree(copy, 0, MEM_RELEASE);
    }
    /* the explosion list's own reader and bound must be among the sites found */
    int reader1 = 0, bound1 = 0;
    for (unsigned i = 0; i < g_ntExp.npatch; i++)
    {
        reader1 |= g_ntExp.patch[i].at == g_ntBase + NT_EXP_READER + 4;
        bound1 |= g_ntExp.patch[i].at == g_ntBase + NT_EXP_BOUND + 1;
    }
    if (ok && (!reader1 || !bound1 || !g_ntExp.refs)) ok = NtFail("the explosion list's reader was not among the sites found", NT_EXP_READER);
    if (arcs && !g_ntArc.refs) { arcs = 0; arcWhy = "no instruction addresses the arc list"; }
    if (!ok) { VirtualFree(block, 0, MEM_RELEASE); g_ntExp.block = g_ntArc.block = NULL; return 0; }
    /* her rows: the arc first (her explosion names it), then the explosion */
    g_ntArcId = 0;
    if (arcs)
    {
        g_ntArcId = g_ntArc.native;
        memcpy(g_ntArcRow, &g_ntArcId, 4);
        memcpy(g_ntArcRow + 36, &g_ntArcDamage, 4);
        g_ntArc.block[g_ntArcId] = (uintptr_t)g_ntArcRow;
        if (!NtApply(&g_ntArc)) { arcWhy = g_ntWhy; g_ntArcId = 0; }
    }
    g_ntBaseArc = arcType;
    g_ntExpId = NT_EXP_NATIVE;
    memcpy(g_ntExpRow, &g_ntExpId, 4);
    memcpy(g_ntExpRow + 4, &g_ntBlastDamage, 4);
    if (g_ntArcId) memcpy(g_ntExpRow + 120, &g_ntArcId, 4);
    g_ntExp.block[g_ntExpId] = (uintptr_t)g_ntExpRow;
    if (!NtApply(&g_ntExp))
    {
        Log("lockdown burst: the explosion list could not be extended (%s at +0x%llx); the lockdown uses the GL-52 burst", g_ntWhy,
            (unsigned long long)g_ntWhere);
        if (g_ntArcId) Log("lockdown burst: the arc list stays extended (arc %u is unused)", g_ntArcId);
        return 0;
    }
    g_ntState = 1;
    Log("lockdown burst: explosion %u (a copy of %u: %u sites, %u bounds) dealing damage %u (a copy of %u)", g_ntExpId, g_glBurst,
        g_ntExp.refs, g_ntExp.bounds, g_ntBlastDamage, blastDamage);
    if (g_ntArcId)
        Log("lockdown burst: its arc is %u (a copy of arc %u: list at +0x%x, %u slots, %u sites, %u bounds) dealing damage %u (a copy of %u)",
            g_ntArcId, arcType, g_ntArc.rva, g_ntArc.native, g_ntArc.refs, g_ntArc.bounds, g_ntArcDamage, arcDamage);
    else
        Log("lockdown burst: its arc stays the burst's own (arc %u, full damage): %s", arcType, arcWhy ? arcWhy : "unknown");
    if (g_ntArcId && !g_ntArc.bounds) Log("lockdown burst: no bound was found beside the arc list's readers (if the game checks one, her arc is skipped)");
    NtDamage();
    NtBolt();
    return 1;
}

/* Tried on the game thread once the rows are ready, once a second, up to 60 times */
static void NtTick(void)
{
    /* her rows stay in the lists (were the game to reload a list into the block, it would fill only its own slots) */
    if (g_ntState == 1)
    {
        static int told;
        if (g_ntExp.block[g_ntExpId] != (uintptr_t)g_ntExpRow || (g_ntArcId && g_ntArc.block[g_ntArcId] != (uintptr_t)g_ntArcRow))
        {
            g_ntExp.block[g_ntExpId] = (uintptr_t)g_ntExpRow;
            if (g_ntArcId) g_ntArc.block[g_ntArcId] = (uintptr_t)g_ntArcRow;
            if (!told++) Log("lockdown burst: the game rewrote a list; her rows are back");
        }
        return;
    }
    if (!s_nativeRows || g_ntState || !g_bolt || !g_glBurst || !g_fieldBolt || g_now < g_ntNext) return;
    g_ntNext = g_now + 1;
    if (NtInstall()) return;
    if (++g_ntTries == 1 || g_ntTries % 10 == 0) Log("lockdown burst: not yet (%s at +0x%llx); try %u", g_ntWhy, (unsigned long long)g_ntWhere, g_ntTries);
    if (g_ntTries >= 60) { g_ntState = -1; Log("lockdown burst: given up; the lockdown uses the GL-52 burst, its damage cut as it lands"); }
}

/* Her lockdown bullet bursts as her explosion once it is live, else as the GL-52's (set again at each mission start, in
 * case the game reloads its projectile rows) */
static void NtBolt(void)
{
    HfProjectileSettings* row = g_fieldBolt ? (HfProjectileSettings*)hf->projectile_row(g_fieldBolt) : NULL;
    if (row) row->impact_explosion = g_ntState == 1 ? g_ntExpId : g_glBurst;
}
/* An explosion near one of her arcs lately: hers; *tag: the nearest such arc's */
/* the burst it most likely came from: an arc strikes within a moment of its burst, so the latest burst in reach wins
 * (a metre further counts as 3 ms older), not merely the nearest one (which is often another skill's, beside its target) */
static int NearOwnTag(const float at[3], double within, float radius, uint32_t* tag)
{
    double best = 1e9;
    for (int i = 0; i < OWN; i++)
    {
        if (g_pulses[i].at <= 0 || g_now - g_pulses[i].at > within) continue;
        float d2 = Dist2(at, g_pulses[i].p);
        if (d2 > radius * radius) continue;
        double score = (g_now - g_pulses[i].at) + sqrt(d2) * 0.003;
        if (score < best) { best = score; if (tag) *tag = g_pulses[i].row; }
    }
    return best < 1e8;
}
static int NearOwn(const float at[3], double within, float radius) { return NearOwnTag(at, within, radius, NULL); }
/* Whether one of her bursts with this tag went off within `within` s and `radius` m */
static int NearOwnTagOnly(const float at[3], double within, float radius, uint32_t tag)
{
    for (int i = 0; i < OWN; i++)
        if (g_pulses[i].at > 0 && g_pulses[i].row == tag && g_now - g_pulses[i].at <= within && Dist2(at, g_pulses[i].p) <= radius * radius) return 1;
    return 0;
}
static unsigned g_wiseToField;
/* For the log: her nearest burst lately, however old or far (age in s, distance in m) */
static int NearestOwn(const float at[3], double* age, float* dist, uint32_t* tag)
{
    double best = 1e9;
    for (int i = 0; i < OWN; i++)
    {
        if (g_pulses[i].at <= 0 || g_now - g_pulses[i].at > 30) continue;
        float d = sqrtf(Dist2(at, g_pulses[i].p));
        double score = (g_now - g_pulses[i].at) + d * 0.003;
        if (score < best) { best = score; *age = g_now - g_pulses[i].at; *dist = d; *tag = g_pulses[i].row; }
    }
    return best < 1e8;
}
static unsigned g_shotKinds[10], g_shotNotes;

/* ---- enemies around her (her area attacks): spawned entities whose kind runs a behaviour and is no turret or
 * Helldiver, and anything her own hits landed on; dropped on death or when the entity id is reused ------------------ */
#define FOES 1024
static uint32_t g_foe[FOES];
static uint64_t g_foeRes[FOES];
static unsigned g_foeNext;
typedef struct Kind { uint64_t res; int foe; } Kind;
static Kind g_kinds[256];
static int FoeKind(uint64_t res)
{
    if (!res || res == AVATAR) return 0;
    unsigned h = (unsigned)(res ^ (res >> 29)) & 255;
    for (unsigned i = 0; i < 256; i++)
    {
        Kind* k = &g_kinds[(h + i) & 255];
        if (k->res == res) return k->foe;
        if (!k->res)
        {
            uint32_t stride;
            k->res = res;
            k->foe = hf->component_record("BehaviorComponentData", res, &stride) != 0 && !hf->component_record("TurretComponentData", res, &stride) &&
                     !hf->component_record("AvatarComponentData", res, &stride);
            return k->foe;
        }
    }
    return 0;
}
static void AddFoe(uint32_t entity, uint64_t res)
{
    if (!entity || entity == atomic_load(&g_avatar) || res == AVATAR) return;
    int free_ = -1;
    for (int i = 0; i < FOES; i++)
    {
        if (g_foe[i] == entity) { g_foeRes[i] = res; return; }
        if (!g_foe[i] && free_ < 0) free_ = i;
    }
    unsigned slot = free_ >= 0 ? (unsigned)free_ : g_foeNext++ % FOES;
    g_foe[slot] = entity;
    g_foeRes[slot] = res;
}
static void DropFoe(uint32_t entity)
{
    for (int i = 0; i < FOES; i++) if (g_foe[i] == entity) g_foe[i] = 0;
}

/* ---- targets ---------------------------------------------------------------------------------------------------- */
/* An enemy's position (its root), 1 when it is still the tracked entity */
static int FoePos(int i, float p[3])
{
    uint8_t id[24];
    uint32_t e = g_foe[i];
    if (!e || !Descriptor(e, id) || Quad(id) != g_foeRes[i]) { if (e) g_foe[i] = 0; return 0; }
    return UnitNode(Word(id + 12), 0, p);
}
static int EntityPos(uint32_t e, float p[3])
{
    uint8_t id[24];
    return e && Descriptor(e, id) && Quad(id) != AVATAR && UnitNode(Word(id + 12), 0, p);
}

/* Stunned and held (paralysis): the stun is renewed until `until` */
typedef struct Held { uint32_t entity; double until, next; } Held;
static Held g_held[128];
static void HoldFor(uint32_t entity, float seconds)
{
    if (seconds <= 0) return;
    int slot = 0;
    for (int i = 0; i < 128; i++)
    {
        if (g_held[i].entity == entity) { slot = i; break; }
        if (g_held[i].until < g_held[slot].until) slot = i;
    }
    g_held[slot].entity = entity;
    g_held[slot].until = g_now + seconds;
    g_held[slot].next = g_now;
}
static void HoldFrame(void)
{
    for (int i = 0; i < 128; i++)
    {
        Held* h = &g_held[i];
        if (!h->entity || g_now >= h->until || g_now < h->next) continue;
        h->next = g_now + 1.5;
        int r = hf->apply_status(self, h->entity, HF_STATUS_STUN_LARGE, 100.0f);
        if (r == HF_ACT_DONE || r == HF_ACT_REQUESTED || r == HF_ACT_QUEUED) g_stuns++;
    }
}

/* Wise Support (the Fox's Scheme): an arc on a newly charged enemy near her, a few per window */
typedef struct Wise { uint32_t entity; double at; } Wise;
static Wise g_wiseQ[16];
static int g_nwise;
/* Negative Charge on an enemy for `seconds`; newly charged ones near her draw Wise Support */
static void Charge(uint32_t entity, float seconds)
{
    if (!entity || entity == atomic_load(&g_avatar)) return;
    Mark* m = MarkOf(entity);
    if (m) { if (m->until < g_now + seconds) m->until = g_now + seconds; }
    else
    {
        int slot = 0;
        for (int i = 0; i < 128; i++) if (g_marks[i].until < g_marks[slot].until) slot = i;
        g_marks[slot].entity = entity;
        g_marks[slot].until = g_now + seconds;
        g_charged++;
    }
    /* a charge given or renewed (the lockdown renews its enemies' every pulse) near her draws Wise Support, within its
     * fs_max per fs_every s; one queued arc per enemy at a time */
    float me[3], p[3];
    int queued = 0;
    for (int i = 0; i < g_nwise; i++) queued |= g_wiseQ[i].entity == entity;
    if (!queued && g_fsUsed < s_fsMax && s_fsMax > 0 && g_nwise < 16 && PlayerPose(me, NULL) && EntityPos(entity, p) && Dist2(me, p) <= s_fsRadius * s_fsRadius)
    {
        g_wiseQ[g_nwise].entity = entity;
        g_wiseQ[g_nwise].at = g_now + 0.25 + 0.15 * g_nwise;
        g_nwise++;
    }
}
static void Queue(const float p[3], double at, int flags, float stun);
static void WiseFrame(void)
{
    if (g_now - g_fsWindowAt >= s_fsEvery) { g_fsWindowAt = g_now; g_fsUsed = 0; }
    for (int i = 0; i < g_nwise; i++)
    {
        if (g_now < g_wiseQ[i].at) continue;
        float p[3];
        if (g_fsUsed < s_fsMax && EntityPos(g_wiseQ[i].entity, p))
        {
            p[2] += 0.6f;
            g_queueTag = TAG_WISE;
            Queue(p, g_now, 8 /* SHOT_TARGET */, 0);   /* through the arc limit like every arc of hers */
            g_queueTag = TAG_ARC;
            g_fsUsed++;
            g_wise++;
            if (g_wise <= 5) Log("Wise Support: an arc on enemy %u (%u this mission)", g_wiseQ[i].entity, g_wise);
            if (g_cdUlt > g_now) g_cdUlt -= s_fsCut;
        }
        g_wiseQ[i] = g_wiseQ[--g_nwise];
        i--;
    }
}
/* Charges (and holds, for `stun` s) every tracked enemy within `radius` m of a point; how many */
/* The tracked enemies' positions, read once a frame (reading them is the costly part) */
static uint32_t g_cacheE[512];
static float g_cacheP[512][3];
static int g_ncache;
static double g_cacheAt = -1;
static void FoeCache(void)
{
    if (g_cacheAt == g_now) return;
    g_cacheAt = g_now;
    g_ncache = 0;
    float p[3];
    for (int i = 0; i < FOES && g_ncache < 512; i++)
        if (g_foe[i] && FoePos(i, p)) { g_cacheE[g_ncache] = g_foe[i]; memcpy(g_cacheP[g_ncache], p, sizeof p); g_ncache++; }
}
static int ChargeAround(const float at[3], float radius, float stun)
{
    int n = 0;
    FoeCache();
    for (int i = 0; i < g_ncache; i++)
        if (Dist2(g_cacheP[i], at) <= radius * radius) { Charge(g_cacheE[i], s_ncSeconds); HoldFor(g_cacheE[i], stun); n++; }
    return n;
}

/* ---- salvos: her arcs at points on the ground, one after another ------------------------------------------------- */
#define SHOT_CHARGE 1
#define SHOT_ZONE 2
#define SHOT_GROUND 4
#define SHOT_TARGET 8   /* aimed at an enemy: always fired */
#define SHOT_READY 16   /* its ground, charge and Voltage done */
#define SHOT_USEFUL 32  /* an enemy near it */
#define SHOT_NOWAIT 64  /* fired now if the rate allows, else dropped */
typedef struct Shot { float p[3]; double at; float stun; int flags; uint32_t tag; } Shot;
static Shot g_shots[160];
static int g_nshots;
static void Queue(const float p[3], double at, int flags, float stun)
{
    if (g_nshots >= 160) return;
    g_shots[g_nshots].tag = g_queueTag;
    Shot* s = &g_shots[g_nshots++];
    memcpy(s->p, p, sizeof s->p);
    s->at = at;
    s->flags = flags;
    s->stun = stun;
}
static uint32_t MyUnit(void)
{
    uint8_t id[24];
    uint32_t avatar = atomic_load(&g_avatar);
    return Descriptor(avatar, id) && Quad(id) == AVATAR ? Word(id + 12) : 0;
}
static float g_arcBudget, g_arcExtra;   /* the rate's allowance; a burst her ultimate and Blitz may use above it */
static double g_arcBudgetAt, g_arcExtraUntil;
static unsigned g_arcsFired, g_arcsSkipped;
static int EnemyNear(const float at[3], float radius)
{
    FoeCache();
    for (int i = 0; i < g_ncache; i++) if (Dist2(g_cacheP[i], at) <= radius * radius) return 1;
    return 0;
}
static void SalvoFrame(void)
{
    g_arcBudget += (float)(g_now - g_arcBudgetAt) * s_arcRate;
    float cap = s_arcRate * 0.5f < 1 ? 1 : s_arcRate * 0.5f;   /* a burst of half a second's worth at most */
    if (g_arcBudget > cap) g_arcBudget = cap;
    g_arcBudgetAt = g_now;
    int idle = -1, waiting = 0;
    uint32_t unit = 0;
    /* two passes over the shots that are due: the ones with an enemy near (or aimed at one) first, then the empty ones,
     * which only take what the rate leaves over (they show her reach) and never hold a useful one back */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < g_nshots; i++)
        {
            Shot* s = &g_shots[i];
            if (g_now < s->at) continue;
            if (!(s->flags & SHOT_READY))
            {
                if (pass) continue;
                if (s->flags & SHOT_GROUND)
                {
                    if (idle < 0) { idle = QueryIdle(); unit = MyUnit(); }
                    if (!idle && g_now - s->at < 0.3) continue;   /* the raycast waits for a quiet moment, briefly */
                    float z[16];
                    int n = idle ? Surfaces(unit, s->p[0], s->p[1], s->p[2], z, 16) : 0;
                    if (n > 0) s->p[2] = z[0];   /* the top surface (no leaves) */
                }
                if (s->flags & SHOT_CHARGE) ChargeAround(s->p, 3.5f, s->stun);
                if (s->flags & SHOT_ZONE) Zone_(s->p);
                s->flags &= ~(SHOT_CHARGE | SHOT_ZONE | SHOT_GROUND);
                s->flags |= SHOT_READY;
                if ((s->flags & SHOT_TARGET) || EnemyNear(s->p, s_arcReach)) s->flags |= SHOT_USEFUL;
            }
            int useful = (s->flags & SHOT_USEFUL) != 0;
            if (useful != !pass) continue;
            int fire = 0;
            float extra = g_now < g_arcExtraUntil ? g_arcExtra : 0;
            if (g_arcBudget + extra >= 1 && (useful || !waiting)) fire = 1;
            else if (useful && !(s->flags & SHOT_NOWAIT) && g_now - s->at < 2.0) { waiting++; continue; }   /* its turn comes */
            else if (!useful && s_arcEmpty && !(s->flags & SHOT_NOWAIT) && g_now - s->at < 0.4) continue;   /* a moment for the rate to allow it */
            if (fire)
            {
                float a[3] = {s->p[0], s->p[1], s->p[2] + 0.4f};
                Zap(a, "arc", s->tag);
                if (extra >= 1) g_arcExtra -= 1;
                else g_arcBudget -= 1;
                g_arcsFired++;
            }
            else g_arcsSkipped++;
            g_shots[i] = g_shots[--g_nshots];
            i--;
        }
}
/* Her aim, flat */
/* Her aim, flat. Her head's aim point sways with her animation (a few degrees: a metre or two off at 20 m), so the aim is
 * the average of the last fraction of a second (AimTrack, every frame) */
static float g_aimAvg[2];
static int g_aimHave;
static double g_aimAt;
static void AimTrack(float dt)
{
    float pos[3], aim[3];
    if (!PlayerPose(pos, aim)) return;
    float n = sqrtf(aim[0] * aim[0] + aim[1] * aim[1]);
    if (!(n > 0.05f)) return;
    float x = aim[0] / n, y = aim[1] / n;
    if (!g_aimHave || g_now - g_aimAt > 0.5) { g_aimAvg[0] = x; g_aimAvg[1] = y; g_aimHave = 1; }
    else
    {
        float k = 1.0f - expf(-(dt > 0 ? dt : 0.016f) / s_aimSmooth);
        if (x * g_aimAvg[0] + y * g_aimAvg[1] < 0.7f) k = 1;   /* a quick turn: follow at once */
        g_aimAvg[0] += (x - g_aimAvg[0]) * k;
        g_aimAvg[1] += (y - g_aimAvg[1]) * k;
        float m = sqrtf(g_aimAvg[0] * g_aimAvg[0] + g_aimAvg[1] * g_aimAvg[1]);
        if (m > 0.01f) { g_aimAvg[0] /= m; g_aimAvg[1] /= m; }
    }
    g_aimAt = g_now;
}
static int AimFlat(float pos[3], float dir[2])
{
    float aim[3];
    if (!PlayerPose(pos, aim)) return 0;
    if (g_aimHave && g_now - g_aimAt < 0.3 && s_aimSmooth > 0) { dir[0] = g_aimAvg[0]; dir[1] = g_aimAvg[1]; return 1; }
    float n = sqrtf(aim[0] * aim[0] + aim[1] * aim[1]);
    if (!(n > 0.05f)) return 0;
    dir[0] = aim[0] / n;
    dir[1] = aim[1] / n;
    return 1;
}

/* ---- the abilities ---------------------------------------------------------------------------------------------- */
/* Rational Suppression's lockdown fields: their points, pulsing until `until` */
typedef struct Field { float p[75][3], center[3], radius; int n, turn; double next, until; } Field;
static Field g_fields[2];
static int g_nfield;
static void FieldFrame(void)
{
    for (int f = 0; f < 2; f++)
    {
        Field* F = &g_fields[f];
        if (F->until <= g_now || g_now < F->next) continue;
        F->next = g_now + s_rsPulse;
        /* every enemy in the field is held (and charged); then at most rs_arcs arcs this pulse: points with an enemy near
         * first, then (with arc_empty) empty ones, from a rotating start so the whole field gets its turn */
        /* the enemies near the field (from the frame's one read of them), then the points */
        static uint32_t nearE[128];
        static float nearP[128][3];
        int nn = 0;
        float reach = F->radius + 4;
        FoeCache();
        for (int i = 0; i < g_ncache && nn < 128; i++)
            if (Dist2(g_cacheP[i], F->center) <= reach * reach) { nearE[nn] = g_cacheE[i]; memcpy(nearP[nn], g_cacheP[i], 12); nn++; }
        int useful[75], empty[75], nu = 0, ne = 0;
        for (int i = 0; i < F->n; i++)
        {
            int any = 0;
            for (int j = 0; j < nn; j++)
            {
                float d2 = Dist2(nearP[j], F->p[i]);
                if (d2 <= 3.5f * 3.5f && nearE[j]) { Charge(nearE[j], s_ncSeconds); HoldFor(nearE[j], s_rsHold); nearE[j] = 0; }
                if (d2 <= s_arcReach * s_arcReach * 0.25f) any = 1;
            }
            if (any) useful[nu++] = i; else empty[ne++] = i;
        }
        int want = s_rsArcs, k = 0;
        g_queueTag = TAG_FIELD;
        for (int j = 0; j < nu && k < want; j++, k++) Queue(F->p[useful[(j + F->turn) % nu]], g_now + 0.03 * k, SHOT_GROUND | SHOT_NOWAIT, 0);
        for (int j = 0; s_arcEmpty && j < ne && k < want; j++, k++) Queue(F->p[empty[(j + F->turn) % ne]], g_now + 0.03 * k, SHOT_GROUND | SHOT_NOWAIT, 0);
        g_queueTag = TAG_ARC;
        F->turn++;
    }
}
static int FieldCount(void)
{
    int n = 0;
    for (int f = 0; f < 2; f++) n += g_fields[f].until > g_now;
    return n;
}
static void RationalSuppression(void)
{
    if (g_now < g_cdSuppress) return;
    float pos[3], dir[2];
    if (!AimFlat(pos, dir)) { Log("Rational Suppression: her aim cannot be read"); return; }
    float side[2] = {-dir[1], dir[0]};
    float cx = pos[0] + dir[0] * s_rsDistance, cy = pos[1] + dir[1] * s_rsDistance;
    Field* F = &g_fields[g_nfield++ % 2];
    F->n = 0;
    g_queueTag = TAG_FIELD;
    for (int c = 0; c < s_rsColumns; c++)
    {
        float u = s_rsColumns > 1 ? -s_rsWidth / 2 + s_rsWidth * (float)c / (float)(s_rsColumns - 1) : 0;
        for (int r = 0; r < s_rsRows; r++)
        {
            float v = s_rsRows > 1 ? -s_rsDepth / 2 + s_rsDepth * (float)r / (float)(s_rsRows - 1) : 0;
            float p[3] = {cx + side[0] * u + dir[0] * v, cy + side[1] * u + dir[1] * v, pos[2]};
            int mid = r == s_rsRows / 2;
            Queue(p, g_now + 0.1 + s_rsSweep * c + 0.02 * r, SHOT_GROUND | SHOT_CHARGE | (mid ? SHOT_ZONE : 0), s_rsParalysis);
            if (F->n < 75) memcpy(F->p[F->n++], p, sizeof p);
        }
    }
    g_queueTag = TAG_ARC;
    F->next = g_now + 0.1 + s_rsSweep * s_rsColumns + s_rsPulse;
    F->until = g_now + 0.1 + s_rsSeconds;
    F->center[0] = cx; F->center[1] = cy; F->center[2] = pos[2];
    F->radius = sqrtf(s_rsWidth * s_rsWidth + s_rsDepth * s_rsDepth) * 0.5f;
    g_cdSuppress = g_now + s_rsCd;
    g_casts[0]++;
    if (g_probeCasts++ < 1) { g_probeUntil = g_now + 2.0; g_probeAt[0] = cx; g_probeAt[1] = cy; g_probeAt[2] = pos[2]; Log("probe: logging every hit and kill within 50 m for 2 s"); }
    Log("Rational Suppression: a lockdown of %d points over %.0f x %.0f m, %.0f m ahead, arcing every %.1f s for %.0f s", F->n,
        (double)s_rsWidth, (double)s_rsDepth, (double)s_rsDistance, (double)s_rsPulse, (double)s_rsSeconds);
}
/* Orderly Disruption: Arc Rounds for od_seconds s (her bullets' hits on enemies arc) */
static double g_roundsUntil, g_roundsAt;
typedef struct HitArc { float p[3]; uint32_t target; } HitArc;
static HitArc g_hitArcs[8];
static int g_nhitArc;
static void OrderlyDisruption(void)
{
    if (g_now < g_cdDisrupt) return;
    g_roundsUntil = g_now + s_odSeconds;
    GainCode(1);
    g_cdDisrupt = g_now + s_odCd;
    g_casts[1]++;
    Log("Orderly Disruption: Arc Rounds for %.0f s; %d Code", (double)s_odSeconds, g_code);
}
static void HitArcFrame(void)
{
    for (int i = 0; i < g_nhitArc; i++)
    {
        Charge(g_hitArcs[i].target, s_ncSeconds);
        g_queueTag = TAG_ROUNDS;
        Queue(g_hitArcs[i].p, g_now, SHOT_TARGET, 0);
        g_queueTag = TAG_ARC;
    }
    g_nhitArc = 0;
}
static void SuperconductBlitz(void)
{
    float pos[3], dir[2];
    if (!AimFlat(pos, dir)) return;
    static const float mult[5] = {1.0f, 1.0f, 1.17f, 1.42f, 2.0f};
    int stacks = g_code > 4 ? 4 : g_code;
    g_blitzMult = s_blitzPower * mult[stacks] * (g_code > 4 ? 1 + 0.15f * (float)(g_code - 4) : 1);
    g_code = 0;
    /* four arcs at the end of the path, then back along it to her */
    int n = 0;
    float end[3] = {pos[0] + dir[0] * s_blitzLength, pos[1] + dir[1] * s_blitzLength, pos[2]};
    g_queueTag = TAG_BLITZ;
    for (int k = 0; k < 4; k++) { Queue(end, g_now + 0.05 + 0.15 * k, SHOT_GROUND | SHOT_CHARGE | SHOT_TARGET, 0); n++; }
    double t = g_now + 0.05 + 0.15 * 4;
    for (float d = s_blitzLength - s_blitzStep; d >= s_blitzStep * 0.5f; d -= s_blitzStep)
    {
        float p[3] = {pos[0] + dir[0] * d, pos[1] + dir[1] * d, pos[2]};
        Queue(p, t, SHOT_GROUND | SHOT_CHARGE | SHOT_TARGET, 0);
        t += 0.06;
        n++;
    }
    g_queueTag = TAG_ARC;
    g_arcExtra = s_arcBurst * 0.5f;
    g_arcExtraUntil = t + 1.0;
    g_blitzHitFrom = g_now;
    g_blitzHitUntil = t + 1.8;
    g_blitzUntil = 0;
    if (s_blitzStealth > 0) { g_stealthUntil = g_now + s_blitzStealth; Stealth(1); }
    g_casts[3]++;
    Log("Superconduct Blitz: 4 arcs %.0f m out, then %d back along the path; %d Code spent: x%.2f", (double)s_blitzLength, n - 4, stacks,
        (double)g_blitzMult);
}
static void LongDistanceCalculation(void)
{
    if (g_now < g_cdUlt)
    {
        if (g_now < g_blitzUntil) SuperconductBlitz();
        return;
    }
    float me[3], p[3];
    if (!PlayerPose(me, NULL)) return;
    /* four rings of arcs on the ground around her, expanding outwards */
    static const int count[4] = {8, 12, 16, 20};
    int n = 0;
    for (int r = 0; r < 4; r++)
    {
        float rad = s_ultRadius * (float)(r + 1) / 4.0f;
        for (int k = 0; k < count[r]; k++)
        {
            float ang = 6.2832f * ((float)k + 0.5f * (float)(r & 1)) / (float)count[r];
            float q[3] = {me[0] + rad * cosf(ang), me[1] + rad * sinf(ang), me[2]};
            Queue(q, g_now + 0.1 + 0.35 * r + 0.005 * k, SHOT_GROUND | (r == 0 ? SHOT_ZONE : 0), 0);
            n++;
        }
    }
    /* and an arc on every enemy in reach, all charged */
    int hit = 0, arcs = 0;
    for (int i = 0; i < FOES; i++)
    {
        if (!g_foe[i] || !FoePos(i, p) || Dist2(p, me) > s_ultRadius * s_ultRadius) continue;
        Charge(g_foe[i], s_ncSeconds);
        hit++;
        float d = sqrtf(Dist2(p, me));
        if (arcs < s_ultArcsMax) { p[2] += 1.0f; Queue(p, g_now + 0.15 + 0.35 * (d / (s_ultRadius / 4.0f)), SHOT_TARGET, 0); arcs++; }
    }
    g_arcExtra = s_arcBurst;
    g_arcExtraUntil = g_now + 2.0;
    g_cdUlt = g_now + s_ultCd;
    g_blitzUntil = g_now + s_blitzWindow;
    g_casts[2]++;
    Log("Quantum Calculation: 4 rings of %d arcs, %d enem%s charged (%d arcs on them); Superconduct Blitz for %.0f s",
        n, hit, hit == 1 ? "y" : "ies", arcs, (double)s_blitzWindow);
}

/* ---- events ------------------------------------------------------------------------------------------------------ */
static atomic_uint g_hookTarget;
static _Atomic unsigned long long g_hookMs;
static float g_hookPos[3];
static unsigned g_hitNotes;
static int NetOk(uint32_t n) { return n && n != 0x7FFF && n != UINT32_MAX; }
static int MineHit(const HfDamageEvent* d)
{
    int match = NetOk(g_avatarNet) && (d->dealer == g_avatarNet || d->owner == g_avatarNet);
    int hooked = d->target && d->target == atomic_load(&g_hookTarget) && GetTickCount64() - atomic_load(&g_hookMs) <= 250;
    if (hooked && !NetOk(g_avatarNet) && NetOk(d->owner))
    {
        g_avatarNet = d->owner;
        Log("her hits: owned by network %u (dealer %u), learned from the game's hit code", d->owner, d->dealer);
    }
    if (d->creditor && g_hitNotes < 6 && (d->kind == 0 || d->kind == 1 || d->kind == 3))
    {
        g_hitNotes++;
        Log("credited hit: kind %u, dealer %u, owner %u (hers: network %u%s): %s", d->kind, d->dealer, d->owner, g_avatarNet,
            hooked ? ", the hook saw it" : "", match || hooked ? "hers" : "not hers");
    }
    return match || hooked;
}
/* A check that her edits stick: the damage the game then applies (HF_ON_DAMAGE) to a hit she changed, for the log */
typedef struct WatchHit { uint32_t target; int before, set, cancel; double at; } WatchHit;
static WatchHit g_watch[16];
static unsigned g_watchN, g_watchLogged;
static void Watch(uint32_t target, int before, int set, int cancel)
{
    if (g_watchLogged >= 12) return;
    WatchHit* w = &g_watch[g_watchN++ % 16];
    w->target = target; w->before = before; w->set = set; w->cancel = cancel; w->at = g_now;
}
static void OnDamage(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfDamageEvent* d = (const HfDamageEvent*)info;
    if (g_watchLogged >= 12) return;
    for (int i = 0; i < 16; i++)
    {
        WatchHit* w = &g_watch[i];
        if (!w->target || w->target != d->target || g_now - w->at > 0.1) continue;
        g_watchLogged++;
        Log("lockdown hit on %u: %d before, set to %d%s; the game applied %d", d->target, w->before, w->set, w->cancel ? " (cancelled)" : "", d->amount);
        w->target = 0;
        return;
    }
}
/* Probe: everything that hits or dies around her first two lockdowns of a mission, for 2 s after each cast */
static int Probing(const float at[3]) { return g_now < g_probeUntil && g_probeLines < 80 && Dist2(at, g_probeAt) < 50.f * 50.f; }
typedef struct Pop { float p[3]; } Pop;
static Pop g_pops[16];
static int g_npop;

static void OnDamagePre(HfMod* mod, const HfEventInfo* info, void* user)
{
    HfDamagePreEvent* e = (HfDamagePreEvent*)(void*)info;
    HfDamageEvent* d = &e->damage;
    if (!g_inMission || e->cancel) return;
    if (Probing(d->position))
    {
        uint32_t t = 9; double age = -1; float dist = -1;
        NearestOwn(d->position, &age, &dist, &t);
        g_probeLines++;
        Log("probe hit: t+%.2f entity %u, kind %u, element %u, %d damage, dealer %u owner %u (hers %u), mine %d; burst tag %u %.2f s / %.1f m",
            2.0 - (g_probeUntil - g_now), d->target, d->kind, d->element, d->amount, d->dealer, d->owner, g_avatarNet, MineHit(d), t, age, dist);
    }
    uint32_t me = atomic_load(&g_avatar);
    if (d->target == me && me)
    {
        /* her own arcs spare her */
        if ((d->kind == 4 || d->kind == 3) && NearOwn(d->position, 1.5, 10)) { e->cancel = 1; g_selfSpared++; }
        return;
    }
    if (d->amount <= 0) return;
    uint32_t tag = TAG_ARC;
    /* her arcs: electric hits (element 3) near one of her bursts lately; other blasts there (a Hellbomb, a grenade) are not
     * hers and get none of her bonuses (the lockdown's own burst blast is cut like its arcs) */
    /* an arc jumps on from its first target (chain and split), so a hit can land 20-30 m from the burst that started it:
     * an electric hit within 35 m of one of her bursts in the last 1.5 s is hers (the nearest burst names its skill); a
     * plain blast counts only right at a lockdown point */
    int arc = 0;
    /* the burst's blast is element 3; its arcs land as projectile hits of element 2 (the probe in 0.3.10 showed it) */
    int electric = d->element == 3 || (d->element == 2 && (d->kind == 0 || d->kind == 1 || d->kind == 3 || d->kind == 4));
    if (electric && d->kind != 2) arc = NearOwnTag(d->position, 1.5, 35, &tag);
    if (!arc && electric && d->kind != 2) arc = NearOwnTag(d->position, 3.0, 45, &tag);
    /* a Wise Support arc inside a lockdown: only its first strike (on the enemy it was sent at) is its own; the chain
     * hits around it there are the lockdown's (both are the same lightning, so a lockdown point close by takes them) */
    if (arc && tag == TAG_WISE && !NearOwnTagOnly(d->position, 0.2, 2.5f, TAG_WISE) && NearOwnTagOnly(d->position, 1.0, 12, TAG_FIELD))
    { tag = TAG_FIELD; g_wiseToField++; }
    else if (d->kind == 4) { uint32_t t2 = TAG_ARC; if (NearOwnTag(d->position, 1.0, 4, &t2) && t2 == TAG_FIELD) { arc = 1; tag = t2; } }
    /* Voltage: an enemy in a zone takes explosive damage, the zone bursts (next frame); the lockdown's own light arcs
     * leave its zones alone */
    if (d->kind == 4 && !(arc && tag == TAG_FIELD))
        for (int i = 0; i < 32 && g_npop < 16; i++)
        {
            Zone* z = &g_zones[i];
            if (z->until <= g_now || g_now < z->from || Dist2(d->position, z->p) > s_vzRadius * s_vzRadius) continue;
            memcpy(g_pops[g_npop++].p, z->p, sizeof z->p);
            z->until = 0;
        }
    if (arc) { static unsigned noted; if (noted++ < 6) Log("her arc hit entity %u: kind %u, element %u, %d damage", d->target, d->kind, d->element, d->amount); }
    int shot = !arc && MineHit(d) && (d->kind == 0 || d->kind == 1 || d->kind == 3);
    /* the bullet that carries an arc of hers (it lands where she set the arc) counts as that arc */
    if (shot && d->kind == 0 && NearOwnTag(d->position, 0.2, 3.0f, &tag)) { shot = 0; arc = 1; }
    /* Arc Rounds: her bullet's hit on an enemy sets off an arc there (next frame) */
    if (shot && d->kind != 3 && g_now < g_roundsUntil && g_now - g_roundsAt >= s_odStep && g_nhitArc < 8 && !NearOwn(d->position, 0.3, 2))
    {
        g_roundsAt = g_now;
        memcpy(g_hitArcs[g_nhitArc].p, d->position, sizeof d->position);
        g_hitArcs[g_nhitArc].target = d->target;
        g_nhitArc++;
    }
    if (shot)
    {
        uint8_t tid[24];
        if (Descriptor(d->target, tid) && Quad(tid) != AVATAR) AddFoe(d->target, Quad(tid));
        g_lastTarget = d->target;
        memcpy(g_lastPos, d->position, sizeof g_lastPos);
        g_lastAt = g_now;
    }
    if (!arc && !shot) return;
    if (arc && tag == TAG_FIELD && g_ntState == 1)   /* her own lockdown burst: its damage is already rs_damage of it */
    {
        g_tagDamage[TAG_FIELD] += d->amount;
        g_fieldHits++;
        return;
    }
    if (arc && tag == TAG_FIELD)   /* the lockdown's arcs: light, and nothing else adds to them */
    {
        double a = floor(d->amount * s_rsDamage + 0.5);
        int before = d->amount;
        if (a < 1) e->cancel = 1;                 /* none of it (an amount of 0 is not taken as "none") */
        else d->amount = (int32_t)a;
        g_tagDamage[TAG_FIELD] += a < 1 ? 0 : a;
        Watch(d->target, before, a < 1 ? 0 : (int32_t)a, e->cancel);
        g_fieldHits++;
        return;
    }
    double mult = 1 + s_codePer * (float)g_code;                                   /* Superconduct Code */
    if (g_code >= s_codeMax) mult *= s_linkMult;                                    /* Superconduct Link */
    int charged = MarkOf(d->target) != NULL;
    if (charged && arc) mult *= s_ncMult * s_pcMult;                                 /* Negative Charge, Positive Charge */
    if (charged && shot) mult *= s_eliteMult;                                       /* Elite Agent */
    if (arc && tag == TAG_BLITZ) mult *= g_blitzMult;                                /* Superconduct Blitz */
    if (arc && tag == TAG_ROUNDS && charged) mult *= s_odCharged;                    /* Arc Rounds on charged */
    if (mult < 0.9999 || mult > 1.0001)
    {
        if (mult > 1) g_boosted++;
        double a = floor(d->amount * mult + 0.5);
        if (a < 1) a = 1;
        d->amount = a > 2147483647.0 ? 2147483647 : (int32_t)a;
    }
    g_tagDamage[arc ? (tag < 4 ? tag : 0) : 4] += d->amount;
    if (!arc)
    {
        g_shotKinds[d->kind < 10 ? d->kind : 9]++;
        if (g_shotNotes < 16)
        {
            double age = -1; float dist = -1; uint32_t t = 9;
            int any = NearestOwn(d->position, &age, &dist, &t);
            g_shotNotes++;
            Log("counted as her shot: entity %u, kind %u, element %u, %d damage (x%.2f); her nearest burst %s %.2f s ago, %.1f m away (tag %u)",
                d->target, d->kind, d->element, d->amount, mult, any ? "was" : "none:", age, dist, t);
        }
    }
}

/* Her lockdown burst going off (her own explosion type): exactly where it lands */
static void OnExplosion(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfExplosionEvent* e = (const HfExplosionEvent*)info;
    if (g_ntState != 1 || e->type != g_ntExpId || !g_inMission) return;
    Own(e->position, TAG_FIELD);
    g_ntBursts++;
}

static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfKillEvent* k = (const HfKillEvent*)info;
    Mark* m = MarkOf(k->victim);
    float at[3];
    int had = EntityPos(k->victim, at);
    if (had && Probing(at))
    {
        g_probeLines++;
        Log("probe kill: t+%.2f entity %u by %u, kind %u, element %u", 2.0 - (g_probeUntil - g_now), k->victim, k->dealer, k->kind, k->element);
    }
    DropFoe(k->victim);
    if (k->victim == g_lastTarget) g_lastTarget = 0;
    if (!m) return;
    m->until = 0;
    GainCode(1);
    /* the charge passes to the nearest enemy within 10 m */
    if (!had) return;
    float p[3], best = 100;
    uint32_t to = 0;
    for (int i = 0; i < FOES; i++)
    {
        if (!g_foe[i] || g_foe[i] == k->victim || MarkOf(g_foe[i]) || !FoePos(i, p)) continue;
        float d2 = Dist2(p, at);
        if (d2 < best) { best = d2; to = g_foe[i]; }
    }
    if (to) Charge(to, s_ncSeconds < 4 ? s_ncSeconds : 4);
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
    else g_alive = 0;
}
/* Helldivers this machine spawned: entity -> network id (the spawn may come before the player event) */
static uint32_t g_spawnEntity[8], g_spawnNet[8];
static unsigned g_spawnNext;
static void LearnNet(void)
{
    uint32_t me = atomic_load(&g_avatar);
    for (int i = 0; i < 8; i++)
        if (g_spawnEntity[i] && g_spawnEntity[i] == me && NetOk(g_spawnNet[i]) && g_spawnNet[i] != g_avatarNet)
        {
            g_avatarNet = g_spawnNet[i];
            Log("her Helldiver: entity %u, network %u", me, g_avatarNet);
        }
}
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
static void OnWeapon(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfWeaponEvent* w = (const HfWeaponEvent*)info;
    if (w->avatar && w->avatar != atomic_load(&g_avatar)) { atomic_store(&g_avatar, w->avatar); g_avatarNet = 0; g_alive = 1; LearnNet(); }
    if (w->selection >= 1 && w->selection <= 3 && w->entity) { g_weapon[w->selection] = w->entity; g_weaponRes[w->selection] = w->resource; }
}
/* Her stamina, live (0 empty .. 1 full): the action manager [game.dll+0x3326D20] holds it per Helldiver at +0x547854,
 * 0x78 apart (game.dll+0xA50E2D: movss [rcx+r10+0x547854], xmm1, rcx = index x 0x78; found with Cheat Engine), by the
 * same index as the action records at +0x53E170 (HellForge's resupply: its avatar map at +0xF8). */
static int g_staminaOk;
static int HashFind(uintptr_t header, uint32_t key, uint32_t* value)
{
    uint8_t h[20];
    if (!key || !ReadAt(header, h, sizeof h)) return 0;
    uintptr_t buckets = (uintptr_t)Quad(h);
    uint32_t cap = Word(h + 8), empty = Word(h + 12), mult = Word(h + 16);
    if (!AddressOk(buckets, 8) || !cap || cap > 1048576) return 0;
    for (uint32_t i = 0; i < cap && i < 4096; i++)
    {
        uint32_t row[2];
        if (!ReadAt(buckets + 8u * ((key * mult + i) % cap), row, 8)) return 0;
        if (row[0] == key) { *value = row[1]; return row[1] != UINT32_MAX; }
        if (row[0] == empty) return 0;
    }
    return 0;
}
/* The T-Doll's body (as Lenna's): more health and a longer sprint, for the mission */
static int g_bodyOn;
static void Body(int on)
{
    HfField hp = HF_FIELD(Health, health), sd = HF_FIELD(Avatar, sprint_duration);
    if (!on)
    {
        if (g_bodyOn) { hf->set_live(self, AVATAR, &hp, NULL, HF_LIVE_SHARED); hf->set_live(self, AVATAR, &sd, NULL, HF_LIVE_SHARED); }
        g_bodyOn = 0;
        return;
    }
    HfLiveValue a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    if (!hf->get_live(self, AVATAR, &hp, &a) || !hf->get_live(self, AVATAR, &sd, &b)) { Log("body: her health or stamina cannot be read"); return; }
    HfValue h = HF_I32((int32_t)floor(a.base.number * s_hpMult + 0.5)), d = HF_F32((float)(b.base.number * s_staminaMult));
    int r1 = fabsf(s_hpMult - 1) > 0.001f ? hf->set_live(self, AVATAR, &hp, &h, HF_LIVE_SHARED) : -1;
    int r2 = fabsf(s_staminaMult - 1) > 0.001f ? hf->set_live(self, AVATAR, &sd, &d, HF_LIVE_SHARED) : -1;
    Log("body: health %.0f -> %d, sprint %.0f -> %.0f s before the bar empties (applies: %d / %d; 1 = now, 2 = next spawn)", a.base.number,
        (int)h.number, b.base.number, d.number, r1, r2);
    g_bodyOn = 1;
}
static float StaminaNow(void)
{
    uint32_t me = atomic_load(&g_avatar), ai, named;
    uintptr_t actions = Pointer(game + 0x3326D20), d;
    if (!g_staminaOk || !actions || !HashFind(actions + 0xF8, me, &ai) || ai >= 64 || !(d = Pointer(actions + 0x110 + 8u * ai)) ||
        !ReadAt(d + 8, &named, 4) || named != me) return -1;
    float v;
    if (!ReadAt(actions + 0x547854 + (uintptr_t)ai * 0x78, &v, 4) || !isfinite(v) || v < -0.01f || v > 1.01f) return -1;
    return v;
}
/* The T-Doll's price (as Lenna's): one Hellbomb per run-out, armed while her bar is above a fifth, fired once it has
 * sat at 0 for hellbomb_hold s, spawned once its packages are in. */
static double g_boomPending;
static int g_boomArmed;
static void Exhaustion(void)
{
    if (!s_hellbomb || !g_alive) { g_boomPending = 0; return; }
    float v = StaminaNow();
    static int noted;
    if (v >= 0 && !noted) { noted = 1; Log("stamina: %.2f (read live)", (double)v); }
    if (v > 0.2f) g_boomArmed = 1;
    static double emptySince;
    if (v >= 0 && v <= 0.001f) { if (emptySince <= 0) emptySince = g_now; }
    else emptySince = 0;
    if (g_boomArmed && emptySince > 0 && g_now - emptySince >= s_hellbombHold) { g_boomArmed = 0; g_boomPending = g_now + 3; }
    if (g_boomPending <= 0) return;
    if (g_now > g_boomPending) { g_boomPending = 0; Log("out of stamina: the Hellbomb's packages did not load in time"); return; }
    float pos[3];
    if (!PlayerPose(pos, NULL)) return;
    pos[2] += 0.3f;
    int r = hf->spawn_explosion(self, (uint32_t)s_hellbombRow, pos);   /* not one of her own blasts: it is meant to reach her */
    if (r == HF_ACT_WAITING) return;
    g_boomPending = 0;
    Log("out of stamina: HELLBOMB (row %d, answer %d)", s_hellbombRow, r);
}

/* Her arcs' chaining: the GL-52 shell's arc (ArcType 15: chain 2, split 2) kept short, for the mission */
/* Her arcs' shape. The GL-52 burst's arc fans out (6 arcs from one hit at the game's settings). With arc_shape 1 the
 * arc rows that look like the GL-52's (velocity 2000, range 10, chain 2, split 2) take the ARC-3 Arc Thrower's arc
 * settings (ArcType 7: one beam that jumps on from target to target), for the mission; arc_chain / arc_split (when 0 or
 * more) then set its jumps and split. Every arc row's settings go to the log once. */
static HfField g_arcFields[8];
static int g_arcFieldsSet;
static uint32_t g_arcRows[8];
static int g_narcRows;
static void ArcFieldsInit(void)
{
    if (g_arcFieldsSet) return;
    HfField f[8] = {HF_FIELD(ArcSettings, velocity), HF_FIELD(ArcSettings, range), HF_FIELD(ArcSettings, distance_at_max_spread),
                    HF_FIELD(ArcSettings, distance_at_max_spread_first_shot), HF_FIELD(ArcSettings, max_angle_spread),
                    HF_FIELD(ArcSettings, max_angle_spread_first_shot), HF_FIELD(ArcSettings, chain_count), HF_FIELD(ArcSettings, max_split)};
    memcpy(g_arcFields, f, sizeof f);
    g_arcFieldsSet = 1;
}
static int ArcRead(uint32_t row, double v[8])
{
    HfLiveValue x;
    for (int k = 0; k < 8; k++)
    {
        memset(&x, 0, sizeof x);
        if (!hf->get_live(self, row, &g_arcFields[k], &x)) return 0;
        v[k] = x.base.number;
    }
    return 1;
}
static void ArcLimits(int on)
{
    ArcFieldsInit();
    if (!on)
    {
        for (int i = 0; i < g_narcRows; i++)
            for (int k = 0; k < 8; k++) hf->set_live(self, g_arcRows[i], &g_arcFields[k], NULL, HF_LIVE_SHARED);
        g_narcRows = 0;
        return;
    }
    static int dumped;
    double model[8], v[8];
    int haveModel = ArcRead(7, model);
    g_narcRows = 0;
    for (uint32_t row = 0; row < 64; row++)
    {
        if (!ArcRead(row, v)) continue;
        if (!dumped)
            Log("arc row %u: velocity %.0f, range %.1f, spread %.1f m (first %.1f), angle %.0f (first %.0f), chain %.0f, split %.0f", row,
                v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
        int gl = row != 7 && fabs(v[0] - 2000) < 1 && fabs(v[1] - 10) < 0.01 && v[6] == 2 && v[7] == 2;
        /* arc_shape 2: only row 15 (the one whose change brought the bang; row 6's fan, which deals the damage, stays) */
        if (s_arcStyle == 2 && row != 15) gl = 0;
        if (gl && g_narcRows < 8) g_arcRows[g_narcRows++] = row;
    }
    dumped = 1;
    if (!s_arcStyle || !g_narcRows) { if (!g_narcRows) Log("arcs: no arc row looks like the GL-52's: left alone"); }
    for (int i = 0; i < g_narcRows; i++)
    {
        uint32_t row = g_arcRows[i];
        int r = 0;
        double own[8];
        int haveOwn = ArcRead(row, own);
        if (s_arcStyle && haveModel)
            for (int k = 0; k < 6; k++)
            {
                double want = model[k];
                if (k == 1) want = s_arcRange > 0 ? s_arcRange : (haveOwn ? own[1] : 10);   /* its reach: short (a long one strands its lightning) */
                HfValue val = HF_F32((float)want);
                r += hf->set_live(self, row, &g_arcFields[k], &val, HF_LIVE_SHARED) != 0;
            }
        double chain = s_arcChain >= 0 ? s_arcChain : (s_arcStyle && haveModel ? model[6] : 2);
        double split = s_arcSplit >= 0 ? s_arcSplit : (s_arcStyle && haveModel ? model[7] : 2);
        HfValue c = HF_U32(chain), sp = HF_U32(split);
        r += hf->set_live(self, row, &g_arcFields[6], &c, HF_LIVE_SHARED) != 0;
        r += hf->set_live(self, row, &g_arcFields[7], &sp, HF_LIVE_SHARED) != 0;
        Log("arcs: row %u %s, range %.0f m, chain %.0f, split %.0f (%d of %d fields set)", row,
            s_arcStyle && haveModel ? "shaped like the ARC-3's" : "kept its shape", s_arcRange > 0 ? (double)s_arcRange : (haveOwn ? own[1] : 10), chain, split, r,
            s_arcStyle && haveModel ? 8 : 2);
    }
}
static void Reset(const char* why)
{
    if (g_casts[0] || g_casts[1] || g_casts[2])
    {
        Log("%s: Rational Suppression %u, Orderly Disruption %u, Quantum Calculation %u, Superconduct Blitz %u; %u charged, %u Wise Support arcs, %u Voltage bursts, %u paralysis stuns, %u boosted hits, %u heals, %u own arcs spared her, %u knockdowns stopped; %u ground arcs fired, %u skipped (no enemy near, or late); %u lockdown arc hits; damage dealt: arcs %.0f, lockdown %.0f, Blitz %.0f, Arc Rounds %.0f, her shots %.0f",
            why, g_casts[0], g_casts[1], g_casts[2], g_casts[3], g_charged, g_wise, g_zonePops, g_stuns, g_boosted, g_heals, g_selfSpared, g_knockdowns, g_arcsFired, g_arcsSkipped, g_fieldHits,
            g_tagDamage[0], g_tagDamage[1], g_tagDamage[2], g_tagDamage[3], g_tagDamage[4]);
        Log("lockdown: %u chain hits next to a Wise Support arc taken as the lockdown's", g_wiseToField);
        if (g_ntState == 1) Log("lockdown: %u of her own bursts went off (explosion %u)", g_ntBursts, g_ntExpId);
        Log("her shots by kind: projectile %u, ricochet %u, beam %u, other %u", g_shotKinds[0], g_shotKinds[1], g_shotKinds[3],
            g_shotKinds[2] + g_shotKinds[4] + g_shotKinds[5] + g_shotKinds[6] + g_shotKinds[7] + g_shotKinds[8] + g_shotKinds[9]);
    }
    memset(g_tagDamage, 0, sizeof g_tagDamage);
    g_watchLogged = 0; g_ntBursts = 0; g_shotNotes = 0; g_wiseToField = 0; g_probeCasts = 0; g_probeLines = 0; g_probeUntil = 0; memset(g_shotKinds, 0, sizeof g_shotKinds);
    g_fieldHits = 0;
    g_arcsFired = g_arcsSkipped = 0;
    Stealth(0);
    /* the GL-52's package (its burst, explosion 41, plays its blast and sound only once that is in; a bullet's impact does
     * not load it, a burst set off directly does) */
    if (!strcmp(why, "mission start") && !hf->load_package(self, UINT64_C(0x4FEE9CC13C0B4CEC)))
        Log("the GL-52 De-Escalator's package could not be loaded ahead: its bursts may stay silent");
    if (!strcmp(why, "mission start") && s_hellbomb && s_hellbombRow == 125 && !hf->load_package(self, UINT64_C(0x346D785D6CA13FF4)))
        Log("the Hellbomb's package could not be loaded ahead");   /* loadout/hellbomb_backpack */
    g_boomPending = 0;
    Body(0);
    if (!strcmp(why, "mission start")) Body(1);
    ArcLimits(0);
    if (!strcmp(why, "mission start")) ArcLimits(1);
    if (!strcmp(why, "mission start") && g_ntState == 1) { NtDamage(); NtBolt(); }
    memset(g_marks, 0, sizeof g_marks);
    memset(g_zones, 0, sizeof g_zones);
    memset(g_held, 0, sizeof g_held);
    memset(g_pulses, 0, sizeof g_pulses);
    memset(g_casts, 0, sizeof g_casts);
    g_charged = g_boosted = g_selfSpared = g_knockdowns = g_wise = g_zonePops = g_stuns = g_heals = 0;
    g_code = 0;
    g_codeAt = g_pcAt = g_now;
    g_cdSuppress = g_cdDisrupt = g_cdUlt = 0;
    g_blitzUntil = g_blitzHitFrom = g_blitzHitUntil = g_stealthUntil = 0;
    g_lastTarget = 0;
    g_nshots = g_nwise = g_npop = g_nhitArc = 0;
    memset(g_fields, 0, sizeof g_fields);
    g_roundsUntil = g_roundsAt = g_arcExtraUntil = 0;
    g_arcExtra = 0;
    g_retryCount = 0;
    atomic_store(&g_shieldUntilMs, 0);
}

/* ---- the knockdown window: hooks on the game's hit code (as Primed Sure Footed) -------------------------------------- */
#define HIT_SIZE 0x118u
#define RESULT_SIZE 0x24u
static atomic_int g_hooked;
static _Atomic unsigned g_kdStopped;
static _Atomic unsigned g_ownStopped;
/* A hit on her: one of her own blasts (an explosion from within 14 m of one she set off in the last 1.2 s) does
 * nothing to her: its result is cleared (damage, stagger, push, knockdown, reaction). With steady (her passive) or in
 * the knockdown window any hit's knockdown is cleared. */
static void Steady(const void* hit, void* result)
{
    if (!atomic_load(&g_hooked) || !hit || !result) return;
    uint8_t h[HIT_SIZE], r[RESULT_SIZE];
    uint32_t me = atomic_load(&g_avatar);
    if (!me || !ReadAt((uintptr_t)hit, h, sizeof h) || Word(h + 0x20) != me) return;
    unsigned long long now = GetTickCount64();
    if (Word(h) == 4)
    {
        float origin[3];
        memcpy(origin, h + 0x40, sizeof origin);
        for (int i = 0; i < OWN; i++)
        {
            unsigned long long at = atomic_load(&g_ownMs[i]);
            if (!at || now - at > 1200) continue;
            float dx = origin[0] - g_ownPos[i][0], dy = origin[1] - g_ownPos[i][1], dz = origin[2] - g_ownPos[i][2];
            if (dx * dx + dy * dy + dz * dz <= 14.0f * 14.0f)
            {
                memset(result, 0, RESULT_SIZE);
                atomic_fetch_add(&g_ownStopped, 1);
                return;
            }
        }
    }
    if (!s_steady && now > atomic_load(&g_shieldUntilMs)) return;   
    if (!ReadAt((uintptr_t)result, r, sizeof r) || !r[0x1E]) return;
    ((uint8_t*)result)[0x1E] = 0;
    atomic_fetch_add(&g_kdStopped, 1);
}
/* Every hit she deals: its target and where it struck (hit +0x20 target, +0x34 point, +0x80 dealer, +0x100 owner) */
static void Observe(const void* hit)
{
    uint32_t me = atomic_load(&g_avatar);
    if (!atomic_load(&g_hooked) || !hit || !me) return;
    uint8_t h[HIT_SIZE];
    if (!ReadAt((uintptr_t)hit, h, sizeof h)) return;
    uint32_t target = Word(h + 0x20), dealer = Word(h + 0x80), owner = Word(h + 0x100);
    if (target == me || !target || (owner != me && dealer != me)) return;
    memcpy(g_hookPos, h + 0x34, sizeof g_hookPos);
    atomic_store(&g_hookTarget, target);
    atomic_store(&g_hookMs, GetTickCount64());
}
typedef uintptr_t (*CalculateFn)(uintptr_t, void*, const void*);
typedef void (*BlastFn)(uintptr_t, void*, const void*);
typedef uintptr_t (*EmitFn)(uintptr_t, void*, const void*, uint32_t);
static CalculateFn original_calculate;
static BlastFn original_blast;
static EmitFn original_emit;
static uintptr_t Calculate(uintptr_t a, void* out, const void* hit) { Observe(hit); uintptr_t r = original_calculate(a, out, hit); Steady(hit, out); return r; }
static void Blast(uintptr_t a, void* out, const void* hit) { Observe(hit); original_blast(a, out, hit); Steady(hit, out); }
static uintptr_t Emit(uintptr_t a, void* result, const void* hit, uint32_t mode) { Steady(hit, result); return original_emit(a, result, hit, mode); }

static const struct { uint32_t rva; const char* hex; } kPins[] = {
    {0x129DC30u, "48 89 5c 24 08 57 48 83 ec 30 33 c0 0f 57 c0 0f 11 02 49 8b f8 48 8b da 0f 11 42 10 89 42 20 41"},
    {0x129D180u, "48 89 4c 24 08 53 55 56 57 41 55 48 83 ec 60 41 8b 40 20 33 f6 3b 05 85 6a 1e 02 49 8b f8 44 0f"},
    {0x129DD30u, "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 20 57 41 54 41 55 41 56 41 57 48 81 ec a0 00 00 00 44"},
};

static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gameSize = 0, exeSize = 0;
    uintptr_t g = hf->module_base("game.dll", &gameSize), x = hf->module_base("helldivers2.exe", &exeSize);
    if (!g || !x || gameSize != 74727424u || exeSize != 60719104u) { Log("REFUSED: the game's modules are not build " BUILD "'s: no aim, no arcs on the ground"); return; }
    game = g;
    exe = x;
    if (hf->code_matches(exe + 0x79F860, "33 c0 4c 8d 05 c7 b2 01 02 0f 1f 80 00 00 00 00 49 39 0c c0 74 0c ff c0 83 f8 04 75 f3") &&
        hf->code_matches(exe + 0x7F4590, "48 8b c4 f3 0f 11 58 20 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48"))
    {
        g_worldId = (WorldFn)(exe + 0x79F860);
        g_raycast = (RayFn)(exe + 0x7F4590);
        Log("ground finder ready (the game's raycast)");
    }
    else Log("ground finder off: the raycast is not the reviewed code (her arcs go off at her own height)");
    g_staminaOk = hf->code_matches(game + 0xA50E2D, "f3 42 0f 11 8c 11 54 78 54 00");
    Log(g_staminaOk ? "stamina readable (the game's stamina write is the reviewed code)" : "stamina off: its code is not the reviewed one");
    for (unsigned i = 0; i < sizeof kPins / sizeof kPins[0]; i++)
        if (!hf->code_matches(game + kPins[i].rva, kPins[i].hex)) { Log("own-arc guard off: game.dll+0x%X is not the reviewed code", kPins[i].rva); return; }
    if (hf->hook(self, game + 0x129DC30, (void*)Calculate, (void**)&original_calculate) ||
        hf->hook(self, game + 0x129D180, (void*)Blast, (void**)&original_blast) ||
        hf->hook(self, game + 0x129DD30, (void*)Emit, (void**)&original_emit))
    { Log("own-arc guard off: a hook could not be placed"); return; }
    atomic_store(&g_hooked, 1);
    Log("own-arc guard ready (hit calculation, explosion dispatch and emission hooked)");
}

static void ProjectilesReady(HfMod* mod, HfEvent event, void* user)
{
    HfProjectileSettings* shell = g_shellCopy ? (HfProjectileSettings*)hf->projectile_row(g_shellCopy) : NULL;
    HfProjectileSettings* bolt = g_bolt ? (HfProjectileSettings*)hf->projectile_row(g_bolt) : NULL;
    if (!shell) { Log("arcs: the GL-52 shell's row cannot be read: arc_mode 1 and 3 fall back to the shell itself"); return; }
    g_glBurst = shell->impact_explosion ? shell->impact_explosion : shell->expiry_explosion;
    if (bolt && g_glBurst) bolt->impact_explosion = g_glBurst;
    HfDamageInfo* dmg = g_boltDamage ? (HfDamageInfo*)hf->damage_row(g_boltDamage) : NULL;
    if (dmg) { dmg->standard_damage = 0; dmg->durable_damage = 0; dmg->demolition = 0; dmg->stagger = 0; dmg->push_force = 0; }
    Log("arcs: the arc bullet's own hit %s", dmg ? "deals nothing (only its burst does)" : "keeps the Liberator's damage (its damage row cannot be read)");
    Log("arcs: the GL-52 shell bursts as explosion %u (expiry %u); the arc bullet %u carries it%s", shell->impact_explosion,
        shell->expiry_explosion, g_bolt, bolt ? "" : " (its row cannot be read: bursts set off directly)");
    if (!bolt) g_bolt = 0;
    NtBolt();
}

/* ---- the HUD state (read by the Leva HUD addon through LuaJIT's ffi) --------------------------------------------- */
typedef struct LevaHud
{
    uint32_t version, in_mission, code, code_max;
    float cd[3], cd_max[3];          /* seconds left, full cooldowns: Rational Suppression, Orderly Disruption, the ultimate */
    float blitz, stealth;            /* seconds left: Superconduct Blitz's window, Stealth */
    uint32_t charged, zones, keys[3];
    char key_label[3][24];
    float stamina;                   /* her stamina 0..1 (-1: unknown) */
    uint32_t hellbomb;               /* 1 when running dry sets off the Hellbomb */
    float rounds;                    /* version 2: seconds left of Arc Rounds */
    uint32_t fields;                 /* version 2: lockdown fields up */
} LevaHud;
static LevaHud g_hud = {2};
HF_MOD_EXPORT LevaHud* leva_hud_state(void)
{
    g_hud.in_mission = (uint32_t)(g_inMission && g_alive);
    g_hud.code = (uint32_t)g_code;
    g_hud.code_max = (uint32_t)s_codeMax;
    double r[3] = {g_cdSuppress, g_cdDisrupt, g_cdUlt};
    float m[3] = {s_rsCd, s_odCd, s_ultCd};
    for (int i = 0; i < 3; i++) { g_hud.cd[i] = (float)fmax(0, r[i] - g_now); g_hud.cd_max[i] = m[i]; }
    g_hud.blitz = (float)fmax(0, g_blitzUntil - g_now);
    g_hud.stealth = (float)fmax(0, g_stealthUntil - g_now);
    g_hud.charged = (uint32_t)ChargedCount();
    g_hud.zones = (uint32_t)ZoneCount();
    for (int i = 0; i < 3; i++)
    {
        g_hud.keys[i] = (uint32_t)s_keys[i].vk;
        memcpy(g_hud.key_label[i], s_keys[i].label, sizeof g_hud.key_label[i]);
    }
    g_hud.stamina = g_inMission && g_alive ? StaminaNow() : -1;
    g_hud.hellbomb = (uint32_t)(s_hellbomb != 0);
    g_hud.rounds = (float)fmax(0, g_roundsUntil - g_now);
    g_hud.fields = (uint32_t)FieldCount();
    return &g_hud;
}

static int Focused(void)
{
    HWND w = GetForegroundWindow();
    DWORD pid = 0;
    if (w) GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

static void Tick(HfMod* mod, float dt, void* user)
{
    g_now += dt > 0 && dt < 1 ? dt : 0;
    int inMission = hf->game_state(self) == HF_STATE_MISSION;
    if (inMission != g_inMission) { Reset(inMission ? "mission start" : "mission end"); g_inMission = inMission; }
    NtTick();
    if (g_ntState == 1 && g_ntApplied != s_rsDamage) NtDamage();
    if (!g_inMission) return;
    static int prev[3];
    int focused = Focused();
    int mods = focused ? ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? KM_SHIFT : 0) | ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? KM_CTRL : 0) |
                         ((GetAsyncKeyState(VK_MENU) & 0x8000) ? KM_ALT : 0) : 0;
    for (int i = 0; i < 3; i++)
    {
        int own = s_keys[i].vk == VK_SHIFT ? KM_SHIFT : s_keys[i].vk == VK_CONTROL ? KM_CTRL : s_keys[i].vk == VK_MENU ? KM_ALT : 0;
        int down = focused && (GetAsyncKeyState(s_keys[i].vk) & 0x8000) != 0 && (mods & ~own) == s_keys[i].mods;
        if (down && !prev[i] && g_alive && atomic_load(&g_avatar))
        {
            if (i == 0) RationalSuppression();
            else if (i == 1) OrderlyDisruption();
            else LongDistanceCalculation();
        }
        prev[i] = down;
    }
    /* the last enemy she hit, as the hit code saw it */
    static unsigned long long seenMs;
    unsigned long long ms = atomic_load(&g_hookMs);
    if (ms != seenMs)
    {
        seenMs = ms;
        uint32_t t = atomic_load(&g_hookTarget);
        if (t) { g_lastTarget = t; memcpy(g_lastPos, g_hookPos, sizeof g_lastPos); g_lastAt = g_now; }
    }
    for (int i = 0; i < g_retryCount; i++)
    {
        int r = Fire((int)g_retry[i].row, g_retry[i].p, TAG_ARC);
        if (r != HF_ACT_WAITING || g_now > g_retry[i].at)
        {
            Act("an arc (after its packages)", r);
            Own(g_retry[i].p, TAG_ARC);
            g_retry[i] = g_retry[--g_retryCount];
            i--;
        }
    }
    AimTrack(dt);
    FieldFrame();
    HitArcFrame();
    SalvoFrame();
    WiseFrame();
    HoldFrame();
    /* Voltage bursts */
    for (int i = 0; i < g_npop; i++)
    {
        Queue(g_pops[i].p, g_now, SHOT_TARGET, 0);   /* through the arc limit like every arc of hers */
        g_zonePops++;
        GainCode(1);
    }
    g_npop = 0;
    /* the Fox's Scheme: a Code stack every code_every s; Positive Charge: a heal every pc_every s */
    if (g_now - g_codeAt >= s_codeEvery) { g_codeAt = g_now; GainCode(1); }
    if (s_pcHeal > 0 && g_alive && g_now - g_pcAt >= s_pcEvery)
    {
        g_pcAt = g_now;
        int r = hf->heal(self, s_pcHeal);
        if (r == HF_ACT_DONE || r == HF_ACT_REQUESTED) g_heals++;
    }
    if (g_stealthOn && g_now >= g_stealthUntil) Stealth(0);
    Exhaustion();
    unsigned own = atomic_exchange(&g_ownStopped, 0);
    if (own) { g_selfSpared += own; static unsigned noted; if (noted++ < 5) Log("%u hit(s) of her own arcs cleared on her", own); }
    unsigned kd = atomic_exchange(&g_kdStopped, 0);
    if (kd) g_knockdowns += kd;
}

/* ---- the Mod Options Menu (through the Leva HUD addon): read and set a setting by its leva.ini name -------------- */
static const struct { const char* name; float* f; int* i; } kOptions[] = {
    {"rs_cooldown", &s_rsCd, NULL}, {"rs_distance", &s_rsDistance, NULL}, {"rs_width", &s_rsWidth, NULL}, {"rs_paralysis", &s_rsParalysis, NULL},
    {"od_cooldown", &s_odCd, NULL}, {"od_charged", &s_odCharged, NULL}, {"od_seconds", &s_odSeconds, NULL},
    {"rs_seconds", &s_rsSeconds, NULL}, {"rs_damage", &s_rsDamage, NULL}, {"rs_pulse", &s_rsPulse, NULL}, {"rs_arcs", NULL, &s_rsArcs}, {"blitz_power", &s_blitzPower, NULL}, {"arc_rate", &s_arcRate, NULL}, {"arc_chain", NULL, &s_arcChain}, {"arc_shape", NULL, &s_arcStyle}, {"arc_mode", NULL, &s_arcMode}, {"arc_grenade", NULL, &s_arcGrenade}, {"arc_range", &s_arcRange, NULL}, {"arc_split", NULL, &s_arcSplit}, {"arc_empty", NULL, &s_arcEmpty},
    {"ult_cooldown", &s_ultCd, NULL}, {"ult_radius", &s_ultRadius, NULL}, {"blitz_window", &s_blitzWindow, NULL}, {"blitz_length", &s_blitzLength, NULL},
    {"blitz_stealth", &s_blitzStealth, NULL}, {"fs_max", NULL, &s_fsMax}, {"fs_cut", &s_fsCut, NULL},
    {"nc_seconds", &s_ncSeconds, NULL}, {"nc_mult", &s_ncMult, NULL}, {"elite_mult", &s_eliteMult, NULL},
    {"pc_heal", &s_pcHeal, NULL}, {"pc_mult", &s_pcMult, NULL}, {"code_every", &s_codeEvery, NULL}, {"code_per", &s_codePer, NULL},
    {"link_mult", &s_linkMult, NULL}, {"vz_seconds", &s_vzSeconds, NULL}, {"vz_radius", &s_vzRadius, NULL}, {"steady", NULL, &s_steady},
    {"hp_mult", &s_hpMult, NULL}, {"stamina_mult", &s_staminaMult, NULL}, {"hellbomb_on_exhaustion", NULL, &s_hellbomb},
};
HF_MOD_EXPORT int leva_option_get(const char* name, double* out)
{
    if (!name || !out) return 0;
    for (unsigned k = 0; k < sizeof kOptions / sizeof kOptions[0]; k++)
        if (!strcmp(kOptions[k].name, name)) { *out = kOptions[k].f ? (double)*kOptions[k].f : (double)*kOptions[k].i; return 1; }
    return 0;
}
HF_MOD_EXPORT int leva_option_set(const char* name, double value)
{
    double before;
    if (!isfinite(value) || !leva_option_get(name, &before)) return 0;
    Setting(name, value);
    double now;
    leva_option_get(name, &now);
    if (now != before)
    {
        Log("option %s: %g -> %g (Mod Options Menu)", name, before, now);
        if (g_inMission && (!strcmp(name, "hp_mult") || !strcmp(name, "stamina_mult"))) { Body(0); Body(1); }
        if (g_inMission && (!strcmp(name, "arc_chain") || !strcmp(name, "arc_split") || !strcmp(name, "arc_shape") || !strcmp(name, "arc_range"))) { ArcLimits(0); ArcLimits(1); }
    }
    return 1;
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 13 || api->size < offsetof(HfApi, code_matches) + sizeof api->code_matches) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    LoadSettings();
    api->log(mod, "Leva %s: keys %s / %s / %s (Rational Suppression, Orderly Disruption, Quantum Calculation and Superconduct Blitz)",
        VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label);
    if (s_badKey) api->log(mod, "leva.ini: a key could not be read (bits %d: suppress, disrupt, ult); its default stays", s_badKey);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL) || api->on(mod, HF_EVENT_PROJECTILES_READY, ProjectilesReady, NULL)) return 1;
    if (api->size >= offsetof(HfApi, projectile_row) + sizeof api->projectile_row)
    {
        HfProjectile shell = {sizeof shell}, bolt = {sizeof bolt};
        shell.base = 222;   /* the GL-52 De-Escalator's shell: only read, for its burst */
        shell.name = "yui/leva/gl52_shell_copy";
        bolt.base = HF_PROJECTILE_AR23_LIBERATOR;
        bolt.name = "yui/leva/arc_bullet";
        /* the bullet only carries the burst: its own hit deals nothing (a copy of its damage type, zeroed when the rows
         * are ready) */
        HfDamage nothing = {sizeof nothing};
        nothing.base = 108;
        nothing.name = "yui/leva/arc_bullet_damage";
        nothing.scale = 1;
        g_boltDamage = api->add_damage(mod, &nothing);
        bolt.damage = g_boltDamage;
        g_shellCopy = api->add_projectile(mod, &shell);
        g_bolt = api->add_projectile(mod, &bolt);
        /* the lockdown's own bullet, and the damage types of its own burst and arc (filled from the GL-52 burst's once
         * the game's rows are in: parts_native) */
        HfProjectile field = bolt;
        field.name = "yui/leva/lockdown_bullet";
        g_fieldBolt = api->add_projectile(mod, &field);
        HfDamage blast = nothing, arc = nothing;
        blast.name = "yui/leva/lockdown_blast_damage";
        arc.name = "yui/leva/lockdown_arc_damage";
        g_ntBlastDamage = api->add_damage(mod, &blast);
        g_ntArcDamage = api->add_damage(mod, &arc);
        if (!g_fieldBolt || !g_ntBlastDamage || !g_ntArcDamage) { g_fieldBolt = 0; api->log(mod, "lockdown burst: its bullet or damage types could not be added"); }
        if (!g_shellCopy || !g_bolt) api->log(mod, "arcs: a projectile could not be added (shell %u, bullet %u)", g_shellCopy, g_bolt);
    }
    int ok = api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) && api->subscribe(mod, HF_ON_DAMAGE, OnDamage, NULL, 0) && api->subscribe(mod, HF_ON_EXPLOSION, OnExplosion, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) &&
             api->on_game_thread(mod, Tick, NULL);
    if (!ok) api->log(mod, "events or the game thread are unavailable: Leva stays off");
    return 0;
}
