/* Lenna (yui.lenna): Lenna's kit from Girls' Frontline 2 as abilities on keys, for your own Helldiver (solo or host).
 *
 * Her resource is CI (Confectance Index, 0 to ci_max). Enemies she marks with Conductivity build it; an ability that
 * finds 3 CI spends them for its enhanced version.
 *   Wild Extinction (key_wild, default Z): her next we_hits hits on the last enemy she shot, within we_window s, deal
 *     we_mult times their damage; the first stuns it (medium) and marks it. Enhanced: we_mult_enhanced, a large stun.
 *   Leaping Pursuit (key_leap, default X): a teleport lp_distance m along her aim onto the ground there (short of
 *     walls), an arc burst where she left, and a stun on enemies near both ends.
 *     Around it, for lp_window s: no knockdown or ragdoll (her flinch and push stay), lp_damage_taken times the damage
 *     taken, a heal of lp_heal, and lp_speed times her jog and sprint for lp_speed_seconds. On landing (lp_land s), the
 *     last enemy she shot, if within lp_reach m, is stunned (medium) and marked. Enhanced: Wild Extinction is ready again.
 *   Hunting Strategy (key_ult, default C): an arc zap on every enemy within hs_radius m of her, and hs_pulses more
 *     hs_pulse_step s apart; each zapped enemy is stunned for hs_stun s and marked (CI +1 each, at most hs_ci_cap). Then
 *     Electric Arc for hs_arc s: her hits stun (small, at most once a second per enemy) and mark. Enhanced: Stealth
 *     for hs_stealth s: the Reduced Signature passive in the helmet's slot (your armour's own stays) and her weapons
 *     suppressed.
 *   King's Authority (passive): her own EMS pulses do not touch her; marked enemies take mark_mult times the damage for
 *     mark_seconds; killing a marked enemy gives 1 CI.
 *
 * Her aim is read from her Helldiver's scene graph (the head and the head-aim point 2 m ahead of it); her knockdown
 * window clears the knockdown flag of hits on her in the game's hit code (as Primed Sure Footed does).
 *
 * Research credit: the hit-result layout, the hooked functions, the entity and scene-graph reads come from Helldiver
 * Survival Rework 0.3.2 by enochlophobianumerophobia (MIT) and Scope of Authority's Apex knockdown notes, whose research
 * used HD2Runtime by SkyeShade's catalogues (https://github.com/SkyeShade/HD2Runtime). See THIRD_PARTY_NOTICES.md. */
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

#define VERSION "0.2.7"
#define BUILD "25480438"
#define AVATAR UINT64_C(0x4D1C334D294DFA97)
#define NODE_HEAD 90u
#define NODE_HEAD_AIM 91u

static const HfApi* hf;
static HfMod* self;
static uintptr_t game, exe;

/* ---- settings ------------------------------------------------------------------------------------------------- */
/* A key and the modifiers that must be held with it (exactly: Q alone and Shift+Q are different keys) */
#define KM_SHIFT 1
#define KM_CTRL 2
#define KM_ALT 4
typedef struct Key { int vk, mods; char label[24]; } Key;
static Key s_keys[3] = {{'Z', 0, "Z"}, {'X', 0, "X"}, {'C', 0, "C"}};
static int s_ciMax = 6, s_weHits = 0, s_hsPulses = 3, s_hsCiCap = 3;
static float s_weCd = 8, s_weWindow = 5, s_arcStep = 0.35f, s_weMult = 2.4f, s_weMultEnh = 3.0f;
static float s_lpCd = 12, s_lpUp = 8, s_lpForward = 10, s_lpDistance = 10, s_lpWindow = 2.5f, s_lpTaken = 0.5f, s_lpHeal = 20, s_lpSpeed = 1.4f,
             s_lpSpeedSeconds = 4, s_lpLand = 1.0f, s_lpReach = 20;
static float s_hsCd = 40, s_hsPulseStep = 2, s_hsRadius = 20, s_hsArc = 8, s_hsStealth = 10, s_hsStun = 10, s_hsStunEvery = 1.5f, s_lpLift = 1.5f, s_lpClimb = 6, s_lpDrop = 12, s_lpBurstRadius = 6, s_lpStun = 3;
static int s_hsArcsMax = 24;
static float s_markMult = 1.3f, s_markSeconds = 6;
static float s_hpMult = 1.5f, s_staminaMult = 2.0f;
static int s_hellbomb = 1, s_hellbombRow = 125;
static float s_hellbombHold = 0.5f;
static int s_ems = 0, s_arcRow = 157;  /* 157: the G-31 Arc grenade's detonation */  /* the EMS explosion row: 0 = the EMS Mortar Sentry shell's own (read at game start) */

static int s_badKey;
static void Setting(const char* key, double v)
{
#define CL(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#define F(name, var, lo, hi) else if (!strcmp(key, name)) var = (float)CL(v, lo, hi)
#define I(name, var, lo, hi) else if (!strcmp(key, name)) var = (int)CL(v, lo, hi)
    if (0) {}
    I("ci_max", s_ciMax, 3, 20);
    F("we_cooldown", s_weCd, 0, 600); F("we_window", s_weWindow, 0.5, 30); I("we_hits", s_weHits, 0, 1000);
    F("we_mult", s_weMult, 1, 20); F("we_mult_enhanced", s_weMultEnh, 1, 20);
    F("lp_cooldown", s_lpCd, 0, 600); F("lp_up", s_lpUp, 0, 25); F("lp_forward", s_lpForward, 0, 25);
    F("lp_window", s_lpWindow, 0, 30); F("lp_damage_taken", s_lpTaken, 0, 1); F("lp_heal", s_lpHeal, 0, 500);
    F("lp_speed", s_lpSpeed, 1, 3); F("lp_speed_seconds", s_lpSpeedSeconds, 0, 60); F("lp_land", s_lpLand, 0, 5);
    F("lp_reach", s_lpReach, 0, 200); F("lp_distance", s_lpDistance, 2, 30);
    F("hs_cooldown", s_hsCd, 0, 600); I("hs_pulses", s_hsPulses, 0, 10); F("hs_pulse_step", s_hsPulseStep, 0.5, 10);
    F("hs_radius", s_hsRadius, 1, 50); I("hs_ci_cap", s_hsCiCap, 0, 20); F("hs_arc", s_hsArc, 0, 60);
    F("hs_stealth", s_hsStealth, 0, 120); F("hs_stun", s_hsStun, 0, 60); F("hs_stun_every", s_hsStunEvery, 0.3, 10);
    I("hs_arcs_max", s_hsArcsMax, 0, 64); F("lp_lift", s_lpLift, 0, 5); F("lp_climb", s_lpClimb, 0.5, 30); F("lp_drop", s_lpDrop, 0.5, 50);
    F("lp_burst_radius", s_lpBurstRadius, 0, 30); F("lp_stun", s_lpStun, 0, 30);
    F("mark_mult", s_markMult, 1, 10); F("mark_seconds", s_markSeconds, 0.5, 60);
    F("hp_mult", s_hpMult, 0.1, 10); F("stamina_mult", s_staminaMult, 0.1, 20); I("hellbomb_on_exhaustion", s_hellbomb, 0, 1);
    I("hellbomb_explosion", s_hellbombRow, 0, 100000); F("hellbomb_hold", s_hellbombHold, 0, 10);
    I("ems_explosion", s_ems, 0, 100000); I("arc_explosion", s_arcRow, 0, 100000); F("arc_step", s_arcStep, 0.05, 5);
#undef F
#undef I
#undef CL
}

/* "Shift+Q", "Ctrl+Alt+F", "F5", "Mouse4", "5", or a Windows virtual-key code ("#81") */
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
    snprintf(path, sizeof path, "%s\\lenna.ini", hf->mod_folder(self));
    FILE* f = OpenUtf8(path, L"r");
    if (f) hf->log(self, "settings: read %s", path);
    else
    {
        hf->log(self, "settings: no lenna.ini in %s (it ships with the mod), so the built-in defaults; the MODS tab still changes them", path);
        FILE* w = NULL;   /* never written while the game runs (see OpenUtf8); the text stays as the defaults' source */
        if ((f = w) != NULL)
        {
            fprintf(f, "; Lenna %s. Keys: a letter, digit, F1-F24, Mouse3, Mouse4 or Mouse5, with Shift+, Ctrl+ or Alt+ in front for a\n"
                       "; combo (Shift+Q). The game still sees the keys too (Shift sprints, R reloads), so pick ones it leaves alone.\n"
                       "key_wild = %s\nkey_leap = %s\nkey_ult = %s\n; CI: most she holds (enhanced abilities spend 3).\nci_max = %d\n"
                       "; Wild Extinction: cooldown; for we_window s her hits (at most we_hits) deal we_mult times their damage and\n"
                       "; each sets off an arc where it lands (at most one every arc_step s; arc_explosion: 157, the G-31 Arc's).\n"
                       "we_cooldown = %g\nwe_window = %g\nwe_hits = %d\nwe_mult = %g\nwe_mult_enhanced = %g\n"
                       "; Leaping Pursuit: cooldown, a teleport lp_distance m along her aim onto the ground there (at most lp_climb m\n"
                       "; higher or lp_drop m lower; short of walls, shorter when there is no ground); an arc burst where she left and a stun of lp_stun s\n"
                       "; on enemies within lp_burst_radius m of both ends; the protected window (no knockdown or fall damage, damage\n"
                       "; taken times lp_damage_taken), heal, speed boost and its length, landing time and stun reach.\n"
                       "lp_cooldown = %g\nlp_distance = %g\nlp_climb = %g\nlp_drop = %g\nlp_burst_radius = %g\nlp_stun = %g\nlp_window = %g\nlp_damage_taken = %g\nlp_heal = %g\n"
                       "lp_speed = %g\nlp_speed_seconds = %g\nlp_land = %g\nlp_reach = %g\n"
                       "; Hunting Strategy: cooldown; an arc zap on every enemy within hs_radius m of her, then hs_pulses more zaps\n"
                       "; hs_pulse_step s apart; zapped enemies are stunned for hs_stun s (renewed every hs_stun_every s) and marked;\n"
                       "; at most hs_arcs_max arc blasts a zap (the rest are still stunned); CI per cast at most; Electric Arc and\n"
                       "; Stealth lengths.\n"
                       "hs_cooldown = %g\nhs_pulses = %d\nhs_pulse_step = %g\nhs_radius = %g\nhs_stun = %g\nhs_stun_every = %g\n"
                       "hs_arcs_max = %d\nhs_ci_cap = %d\nhs_arc = %g\nhs_stealth = %g\n"
                       "; Conductivity: marked enemies take this times the damage, this long.\nmark_mult = %g\nmark_seconds = %g\n"
                       "; An EMS blast under her zaps too (0: none; 180: the EMS Mortar Sentry shell's field; its stun reaches her).\nems_explosion = %d\n"
                       "arc_explosion = %d\narc_step = %g\n"
                       "; Her body: hp_mult times a Helldiver's health, stamina_mult times her sprint before the bar empties. If her\n"
                       "; stamina ever runs out, hellbomb_on_exhaustion (1) sets off hellbomb_explosion (125: the B-100 Portable\n"
                       "; Hellbomb's) on her.\n"
                       "; hellbomb_hold: seconds the bar must stay empty first (it flickers to 0 for a moment at times).\n"
                       "hp_mult = %g\nstamina_mult = %g\nhellbomb_on_exhaustion = %d\nhellbomb_explosion = %d\nhellbomb_hold = %g\n",
                VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, s_ciMax, (double)s_weCd, (double)s_weWindow, s_weHits, (double)s_weMult,
                (double)s_weMultEnh, (double)s_lpCd, (double)s_lpDistance, (double)s_lpClimb, (double)s_lpDrop, (double)s_lpBurstRadius, (double)s_lpStun, (double)s_lpWindow, (double)s_lpTaken,
                (double)s_lpHeal, (double)s_lpSpeed, (double)s_lpSpeedSeconds, (double)s_lpLand, (double)s_lpReach, (double)s_hsCd,
                s_hsPulses, (double)s_hsPulseStep, (double)s_hsRadius, (double)s_hsStun, (double)s_hsStunEvery, s_hsArcsMax, s_hsCiCap, (double)s_hsArc, (double)s_hsStealth,
                (double)s_markMult, (double)s_markSeconds, s_ems, s_arcRow, (double)s_arcStep,
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
        int k = !strcmp(key, "key_wild") ? 0 : !strcmp(key, "key_leap") ? 1 : !strcmp(key, "key_ult") ? 2 : -1;
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
static int g_ci;
static double g_cdWild, g_cdLeap, g_cdUlt;   /* ready at */
/* the last enemy she shot */
static uint32_t g_lastTarget;
static float g_lastPos[3];
static double g_lastAt;
/* Wild Extinction's charge */
static uint32_t g_chargeTarget;
static int g_chargeHits, g_chargeStun;
static float g_chargeMult;
static double g_chargeUntil;
/* Leaping Pursuit */
static atomic_ullong g_shieldUntilMs;          /* the knockdown window (GetTickCount64 ms; read by the hit hooks) */
static double g_drUntil, g_speedUntil, g_forwardAt, g_landAt;
/* the leap: pushed again each few frames until she is seen rising, then forward until she is seen moving */
static int g_leap;
static double g_leapT0;
static float g_leapDir[2];
static int g_speedOn;
static float g_baseJog = -1, g_baseSprint = -1;
/* Hunting Strategy */
typedef struct Pulse { float p[3]; double at; uint32_t row; } Pulse;
/* her blasts for the hit hooks (other threads): when (GetTickCount64 ms) and where */
static _Atomic unsigned long long g_ownMs[8];
static float g_ownPos[8][3];
static unsigned g_ownNext;
static double g_lastArc;
static Pulse g_pulses[12];
static int g_npulse;

static double g_nextPulse, g_arcUntil, g_stealthUntil;
static int g_pulsesLeft, g_castCi, g_stealthOn;
/* Conductivity marks */
typedef struct Mark { uint32_t entity; double until, stunnedAt; } Mark;
static Mark g_marks[64];
/* counts for the log */
static unsigned g_casts[3], g_marked, g_stuns, g_boosted, g_selfSpared, g_knockdowns;

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

static Mark* MarkOf(uint32_t entity)
{
    for (int i = 0; i < 64; i++) if (g_marks[i].entity == entity && g_marks[i].until > g_now) return &g_marks[i];
    return NULL;
}
static void GainCi(int n)
{
    g_ci += n;
    if (g_ci > s_ciMax) g_ci = s_ciMax;
}
/* Marks an enemy Conductive; 1 when newly marked */
static int MarkEnemy(uint32_t entity)
{
    if (!entity || entity == atomic_load(&g_avatar)) return 0;
    Mark* m = MarkOf(entity);
    if (m) { m->until = g_now + s_markSeconds; return 0; }
    int slot = 0;
    for (int i = 0; i < 64; i++) if (g_marks[i].until < g_marks[slot].until) slot = i;
    g_marks[slot].entity = entity;
    g_marks[slot].until = g_now + s_markSeconds;
    g_marks[slot].stunnedAt = 0;
    g_marked++;
    return 1;
}
static void Stun(uint32_t entity, uint32_t status)
{
    if (Act("stun", hf->apply_status(self, entity, status, 100.0f))) g_stuns++;
}
static int SpendCi(void)
{
    if (g_ci < 3) return 0;
    g_ci -= 3;
    return 1;
}

/* ---- speed, stealth -------------------------------------------------------------------------------------------- */
static void Speed(int on)
{
    HfField jog = HF_FIELD(Avatar, jog), sprint = HF_FIELD(Avatar, sprint);
    if (!on)
    {
        if (g_speedOn) { hf->set_live(self, AVATAR, &jog, NULL, HF_LIVE_SHARED); hf->set_live(self, AVATAR, &sprint, NULL, HF_LIVE_SHARED); }
        g_speedOn = 0;
        return;
    }
    if (g_baseJog < 0)
    {
        HfLiveValue a, b;
        memset(&a, 0, sizeof a);
        memset(&b, 0, sizeof b);
        if (!hf->get_live(self, AVATAR, &jog, &a) || !hf->get_live(self, AVATAR, &sprint, &b)) { Log("speed: the Helldiver's speeds cannot be read"); return; }
        g_baseJog = (float)a.base.number;
        g_baseSprint = (float)b.base.number;
    }
    HfValue j = HF_F32(fminf(10, g_baseJog * s_lpSpeed)), s = HF_F32(fminf(10, g_baseSprint * s_lpSpeed));
    int r1 = hf->set_live(self, AVATAR, &jog, &j, HF_LIVE_SHARED), r2 = hf->set_live(self, AVATAR, &sprint, &s, HF_LIVE_SHARED);
    if (!g_speedOn) Log("speed: jog %.1f -> %.1f, sprint %.1f -> %.1f m/s (applies: %d / %d; 1 = now)", (double)g_baseJog, j.number,
        (double)g_baseSprint, s.number, r1, r2);
    g_speedOn = 1;
}

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
    int n = 0, now = 0;
    for (int i = 1; i <= 3; i++)
    {
        if (!g_weaponRes[i]) continue;
        int r = hf->set_live(self, g_weaponRes[i], &sup, &one, HF_LIVE_SHARED);
        n += r != 0;
        now += r == HF_APPLIES_NOW;
    }
    HfPassives st;
    memset(&st, 0, sizeof st);
    hf->get_passives(self, &st);
    Log("stealth for %.0f s: Reduced Signature %s (%s); %d weapon(s) suppressed (%d at once)", (double)s_hsStealth, p ? "held" : "refused",
        st.status, n, now);
}

/* ---- the abilities -------------------------------------------------------------------------------------------- */
static int TargetFresh(double within) { return g_lastTarget && g_now - g_lastAt <= within; }

static void WildExtinction(void)
{
    if (g_now < g_cdWild) return;
    int enhanced = SpendCi();
    g_chargeTarget = 0;  /* any enemy she hits */
    g_chargeHits = s_weHits > 0 ? s_weHits : 1000000;
    g_chargeStun = 1;
    g_chargeMult = enhanced ? s_weMultEnh : s_weMult;
    g_chargeUntil = g_now + s_weWindow;
    g_cdWild = g_now + s_weCd;
    g_lastArc = 0;
    g_casts[0]++;
    Log("Wild Extinction: for %.0f s her hits (%d at most; 0: all) deal x%.1f and set off arcs%s", (double)s_weWindow, s_weHits, (double)g_chargeMult,
        enhanced ? " (enhanced, 3 CI)" : "");
}

/* ---- the teleport: the game's own position setter, as the LIFT-182 Warp Pack calls it (Warp Probe: game.dll+0x88D795
 * calls 0x5A2BB0 with (NULL, the Helldiver's entity, the destination), then zeroes its velocity with 0x4A7550), with a
 * wall and ground check through the game's raycast (as Helldiver Survival Rework's cover check calls it) -------------- */
static int StunAround(const float at[3], float radius, float seconds);
static void Blast_(uint32_t row, const float at[3], const char* what);
typedef void (*SetVecFn)(void*, uint32_t, const float*);
typedef uint32_t (*WorldFn)(const void*);
typedef uint32_t (*RayFn)(uint32_t, const void*, const void*, float, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t);
static SetVecFn g_setPosition, g_setVelocity;
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
/* The nearest static obstruction along a ray (static actors only, so enemies are not walls): its distance, len + 1
 * when clear, or -1 when the query cannot be made */
static float RayWith(uint32_t unit, const float from[3], const float dir[3], float len, uint32_t filter, uint32_t flags, unsigned* count);
static float g_rayPrefer = -1;
static float Ray(uint32_t unit, const float from[3], const float dir[3], float len) { return RayWith(unit, from, dir, len, 0x393D9518, 0x80000009, NULL); }
static float RayWith(uint32_t unit, const float from[3], const float dir[3], float len, uint32_t filter, uint32_t flags, unsigned* count)
{
    uintptr_t world = Pointer(game + 0x346BFA0);
    if (!world || !g_raycast || !g_worldId) return -1;
    uint32_t id = g_worldId((const void*)world);
    if (id >= 4) return -1;
    uint8_t hits[16 * 44];
    memset(hits, 0, sizeof hits);
    uint32_t n = g_raycast(id, from, dir, len, 2, 1, filter, flags, unit, hits, 16);
    if (count) *count = n;
    if (n > 16) return -1;
    float best = len + 1;
    for (uint32_t i = 0; i < n; i++)
    {
        float d;
        memcpy(&d, hits + 44u * i + 24, 4);
        if (Word(hits + 44u * i + 28) == unit || !isfinite(d) || d < 0.02f) continue;
        if (g_rayPrefer >= 0 ? fabsf(d - g_rayPrefer) < fabsf(best - g_rayPrefer) : d < best) best = d;
    }
    return best;
}
/* The ground under (x, y) near height `ref`: filter 0 with the static-actor flags sees the terrain as well as rocks
 * and floors (the filter sweep found it); of the surfaces under the point the one nearest `ref` (so a roof above a
 * street or a cave ceiling is not taken). 1 found. */
static int GroundAt(uint32_t unit, float x, float y, float ref, float* z)
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
    /* the surfaces under the point, as heights */
    float h[16];
    int k = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        float d;
        memcpy(&d, hits + 44u * i + 24, 4);
        if (Word(hits + 44u * i + 28) == unit || !isfinite(d) || d < 0.02f || d > len) continue;
        h[k++] = top[2] - d;
    }
    if (!k) return 0;
    /* the one nearest her height; but where something is over it within her height (a bug nest, a rock or a structure
     * standing on the terrain: the terrain runs on under it) she would be inside it: then the highest surface */
    int best = 0, top_ = 0;
    for (int i = 1; i < k; i++)
    {
        if (fabsf(h[i] - ref) < fabsf(h[best] - ref)) best = i;
        if (h[i] > h[top_]) top_ = i;
    }
    for (int i = 0; i < k; i++)
        if (h[i] > h[best] + 0.3f && h[i] < h[best] + 2.0f) { best = top_; break; }
    *z = h[best];
    return 1;
}

static void LeapingPursuit(void)
{
    if (g_now < g_cdLeap) return;
    if (!g_setPosition || !g_setVelocity || !g_raycast) { Log("Leaping Pursuit: the teleport is unavailable on this build (see the start of the log)"); return; }
    float pos[3], aim[3];
    if (!PlayerPose(pos, aim)) { Log("Leaping Pursuit: her position or aim cannot be read"); return; }
    float h = sqrtf(aim[0] * aim[0] + aim[1] * aim[1]);
    if (h < 0.05f) { Log("Leaping Pursuit: she is looking straight up or down"); return; }
    g_leapDir[0] = aim[0] / h;
    g_leapDir[1] = aim[1] / h;
    g_leap = 1;          /* the teleport waits for an idle physics query (a frame or two) */
    g_leapT0 = g_now;
}

/* The pending leap: a wall check at chest height (rocks, walls, buildings), the ground at the spot (terrain too),
 * then the warp's two calls: she is there in one frame, standing on the ground. */
static void LeapFrame(void)
{
    if (!g_leap) return;
    if (!QueryIdle())
    {
        if (g_now - g_leapT0 > 0.5) { g_leap = 0; Log("Leaping Pursuit: the physics queries stayed busy: no teleport"); }
        return;
    }
    g_leap = 0;
    uint8_t id[24];
    uint32_t me = atomic_load(&g_avatar);
    float pos[3];
    if (!Descriptor(me, id) || Quad(id) != AVATAR || !PlayerPose(pos, NULL)) { Log("Leaping Pursuit: her Helldiver cannot be read"); return; }
    uint32_t unit = Word(id + 12);
    float dir[3] = {g_leapDir[0], g_leapDir[1], 0};
    float chest[3] = {pos[0], pos[1], pos[2] + 1.0f};
    float wall = Ray(unit, chest, dir, s_lpDistance + 0.6f);
    if (wall < 0) { Log("Leaping Pursuit: the raycast is unavailable now"); return; }
    float dist = fminf(s_lpDistance, wall - 0.6f), dest[3] = {0};
    int found = 0;
    for (int tries = 0; tries < 4 && dist >= 1.5f && !found; tries++)
    {
        dest[0] = pos[0] + dir[0] * dist;
        dest[1] = pos[1] + dir[1] * dist;
        if (GroundAt(unit, dest[0], dest[1], pos[2], &dest[2])) found = 1;
        else dist *= 0.7f;
    }
    /* no ground ahead: to whatever she is looking at (terrain, a wall, a rock), stopped short of it */
    if (!found)
    {
        float look[3];
        float far_ = PlayerPose(pos, look) ? RayWith(unit, chest, look, s_lpDistance + 0.6f, 0, 0x80000009u, NULL) : -1;
        if (far_ >= 1.5f + 0.6f && far_ <= s_lpDistance + 0.6f)
        {
            dist = far_ - 0.6f;
            for (int i = 0; i < 3; i++) dest[i] = chest[i] + look[i] * dist;
            float z;
            if (GroundAt(unit, dest[0], dest[1], dest[2], &z) && z <= dest[2] + 0.5f) dest[2] = z;
            else dest[2] -= 0.9f;   /* against a wall with nothing under: feet a little below the aim line */
            found = 2;
        }
    }
    if (!found) { Log("Leaping Pursuit: no ground within %.0f m up or %.0f m down ahead and nothing in sight within %.0f m: no teleport",
                      (double)s_lpClimb, (double)s_lpDrop, (double)s_lpDistance); return; }
    dest[2] += 0.05f;
    float zero[3] = {0, 0, 0};
    g_setPosition(NULL, me, dest);
    g_setVelocity(NULL, me, zero);
    /* the burst where she left (she is gone) and the stuns at both ends */
    if (s_arcRow && dist >= 4) { float b[3] = {pos[0], pos[1], pos[2] + 0.5f}; Blast_((uint32_t)s_arcRow, b, "Leaping Pursuit's burst"); }
    int left = StunAround(pos, s_lpBurstRadius, s_lpStun), landed = StunAround(dest, s_lpBurstRadius, s_lpStun);
    /* the rest of the ability */
    g_landAt = g_now + s_lpLand;
    g_drUntil = g_now + s_lpWindow;
    atomic_store(&g_shieldUntilMs, GetTickCount64() + (ULONGLONG)(s_lpWindow * 1000));
    g_speedUntil = g_now + s_lpSpeedSeconds;
    Speed(1);
    int enhanced = SpendCi();
    if (enhanced) g_cdWild = 0;
    g_cdLeap = g_now + s_lpCd;
    g_casts[1]++;
    if (s_lpHeal > 0) Act("Leaping Pursuit's heal", hf->heal(self, s_lpHeal));
    Log("Leaping Pursuit: teleported %.1f m, %+.1f m in height%s%s; stunned %d where she left, %d where she landed%s", (double)dist,
        (double)(dest[2] - pos[2]), wall <= s_lpDistance + 0.6f ? " (stopped short of a wall)" : "",
        found == 2 ? " (no ground ahead: to where she aimed)" : "", left, landed,
        enhanced ? "; enhanced: Wild Extinction ready" : "");
}

static Pulse g_retry[6];  /* blasts waiting for their packages (at: give up then) */
static int g_retryCount;
/* One of her blasts (an EMS pulse, an arc): it never touches or throws her, and the enemies it hits are marked */
static void Blast_(uint32_t row, const float at[3], const char* what)
{
    if (!row) return;
    Pulse* p = &g_pulses[g_npulse++ % 12];
    memcpy(p->p, at, sizeof p->p);
    p->at = g_now;
    p->row = row;
    unsigned slot = g_ownNext++ % 8;
    memcpy(g_ownPos[slot], at, sizeof g_ownPos[slot]);
    atomic_store(&g_ownMs[slot], GetTickCount64());
    int r = hf->spawn_explosion(self, row, at);
    if (r == HF_ACT_WAITING && g_retryCount < 6)  /* its packages are loading: again next frames */
    {
        memcpy(g_retry[g_retryCount].p, at, sizeof g_retry[0].p);
        g_retry[g_retryCount].at = g_now + 3;
        g_retry[g_retryCount].row = row;
        g_retryCount++;
    }
    static unsigned noted;
    if (noted++ < 6) Log("%s (row %u): answer %d", what, row, r);
    if (r != HF_ACT_WAITING) Act(what, r);
}
static void Pulse_(const float at[3]) { Blast_((uint32_t)s_ems, at, "EMS pulse"); }

/* ---- enemies around her (Hunting Strategy's zap): spawned entities whose kind runs a behaviour and is no turret or
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
/* Stunned and held: the stun is renewed until `until` */
typedef struct Held { uint32_t entity; double until, next; } Held;
static Held g_held[128];
static void HoldFor(uint32_t entity, float seconds)
{
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
        h->next = g_now + s_hsStunEvery;
        if (hf->apply_status(self, h->entity, HF_STATUS_STUN_LARGE, 100.0f) == HF_ACT_DONE) g_stuns++;
    }
}
/* One zap: an arc on every enemy within hs_radius m of her, stunned and marked */
static void Zap(void)
{
    float me[3], p[3];
    if (!PlayerPose(me, NULL)) return;
    if (s_ems) Pulse_(me);
    uint8_t id[24];
    int found = 0, arcs = 0, known = 0;
    for (int i = 0; i < FOES; i++)
    {
        uint32_t e = g_foe[i];
        if (!e) continue;
        if (!Descriptor(e, id) || Quad(id) != g_foeRes[i]) { g_foe[i] = 0; continue; }
        known++;
        if (!UnitNode(Word(id + 12), 0, p) || Dist2(p, me) > s_hsRadius * s_hsRadius) continue;
        found++;
        if (arcs < s_hsArcsMax && s_arcRow) { p[2] += 1.0f; Blast_((uint32_t)s_arcRow, p, "zap"); arcs++; }
        HoldFor(e, s_hsStun);
        if (MarkEnemy(e) && g_castCi < s_hsCiCap) { g_castCi++; GainCi(1); }
    }
    static unsigned noted;
    if (noted++ < 12)
    {
        unsigned kinds = 0, foes = 0;
        for (int i = 0; i < 256; i++) { kinds += g_kinds[i].res != 0; foes += g_kinds[i].res && g_kinds[i].foe; }
        Log("zap: %d enem%s within %.0f m (%d arcs), of %d tracked; %u of %u spawned kinds taken as enemies", found, found == 1 ? "y" : "ies",
            (double)s_hsRadius, arcs, known, foes, kinds);
    }
}

/* Stuns (held for `seconds`) and marks every tracked enemy within `radius` m of a point; how many */
static int StunAround(const float at[3], float radius, float seconds)
{
    uint8_t id[24];
    float p[3];
    int n = 0;
    for (int i = 0; i < FOES; i++)
    {
        uint32_t e = g_foe[i];
        if (!e || !Descriptor(e, id) || Quad(id) != g_foeRes[i] || !UnitNode(Word(id + 12), 0, p) || Dist2(p, at) > radius * radius) continue;
        HoldFor(e, seconds);
        if (MarkEnemy(e)) GainCi(1);
        n++;
    }
    return n;
}

static void HuntingStrategy(void)
{
    if (g_now < g_cdUlt) return;
    g_castCi = 0;
    Zap();
    g_pulsesLeft = s_hsPulses;
    g_nextPulse = g_now + s_hsPulseStep;
    g_arcUntil = g_now + s_hsArc;
    int enhanced = SpendCi();
    if (enhanced) { g_stealthUntil = g_now + s_hsStealth; Stealth(1); }
    g_cdUlt = g_now + s_hsCd;
    g_casts[2]++;
    Log("Hunting Strategy: zap within %.0f m, %d more zaps, stuns %.0f s, Electric Arc %.0f s%s", (double)s_hsRadius, s_hsPulses,
        (double)s_hsStun, (double)s_hsArc, enhanced ? ", Stealth (3 CI)" : "");
}

/* ---- events ------------------------------------------------------------------------------------------------------ */
/* Her own hits. The game's hit code (hooked) names the entity that dealt or owns each hit: one owned or dealt by her
 * Helldiver is hers (the hook records its target). Damage events name network ids instead: her Helldiver's is learned
 * from the first such hit (its spawn carries none), then matched directly. */
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
static int NearPulse(const float at[3], double within)
{
    for (int i = 0; i < 12; i++)
        if (g_pulses[i].at > 0 && g_now - g_pulses[i].at <= within && Dist2(at, g_pulses[i].p) <= 64.0f) return 1;
    return 0;
}

static void OnDamagePre(HfMod* mod, const HfEventInfo* info, void* user)
{
    HfDamagePreEvent* e = (HfDamagePreEvent*)(void*)info;
    HfDamageEvent* d = &e->damage;
    if (!g_inMission || e->cancel) return;
    uint32_t me = atomic_load(&g_avatar);
    if (d->target == me && me)
    {
        /* King's Authority: her own blasts spare her (the EMS field deals no damage: its stun rides on the hit) */
        if (d->kind == 4 && NearPulse(d->position, 0.6)) { e->cancel = 1; g_selfSpared++; return; }
        if (d->amount == 0) return;
        /* Leaping Pursuit's window: no fall or landing damage (impact or flat, from nothing) */
        if (g_now < g_drUntil && (d->kind == 5 || d->kind == 7) && !NetOk(d->dealer) && !NetOk(d->owner)) { e->cancel = 1; return; }
        if (g_now < g_drUntil && d->amount > 0)
        {
            double a = floor(d->amount * s_lpTaken + 0.5);
            if (a < 1) { e->cancel = 1; return; }
            d->amount = (int32_t)a;
        }
        return;
    }
    /* enemies her pulses hit are marked */
    if (d->kind == 4 && NearPulse(d->position, 0.6) && MarkEnemy(d->target))
        if (g_castCi < s_hsCiCap) { g_castCi++; GainCi(1); }
    if (d->amount == 0) return;
    int mine = MineHit(d) && (d->kind == 0 || d->kind == 1 || d->kind == 3);
    if (mine)
    {
        uint8_t tid[24];
        if (Descriptor(d->target, tid) && Quad(tid) != AVATAR) AddFoe(d->target, Quad(tid));
        g_lastTarget = d->target;
        memcpy(g_lastPos, d->position, sizeof g_lastPos);
        g_lastAt = g_now;
        /* Wild Extinction's charged hits */
        if ((!g_chargeTarget || d->target == g_chargeTarget) && g_chargeHits > 0 && g_now <= g_chargeUntil && d->amount > 0)
        {
            if (g_now - g_lastArc >= s_arcStep) { g_lastArc = g_now; Blast_((uint32_t)s_arcRow, d->position, "arc"); }
            double a = floor(d->amount * g_chargeMult + 0.5);
            d->amount = a > 2147483647.0 ? 2147483647 : (int32_t)a;
            g_chargeHits--;
            g_boosted++;
            if (g_chargeStun)
            {
                g_chargeStun = 0;
                Stun(d->target, g_chargeMult >= s_weMultEnh && s_weMultEnh > s_weMult ? HF_STATUS_STUN_LARGE : HF_STATUS_STUN_MEDIUM);
                if (MarkEnemy(d->target)) GainCi(1);
            }
        }
        /* Electric Arc */
        if (g_now < g_arcUntil)
        {
            if (MarkEnemy(d->target)) GainCi(1);
            Mark* m = MarkOf(d->target);
            if (m && g_now - m->stunnedAt >= 1.0) { m->stunnedAt = g_now; Stun(d->target, HF_STATUS_STUN_SMALL); }
        }
    }
    /* Conductivity: marked enemies take more */
    if (d->amount > 0 && MarkOf(d->target))
    {
        double a = floor(d->amount * s_markMult + 0.5);
        d->amount = a > 2147483647.0 ? 2147483647 : (int32_t)a;
    }
}

static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfKillEvent* k = (const HfKillEvent*)info;
    DropFoe(k->victim);
    Mark* m = MarkOf(k->victim);
    if (m) { m->until = 0; GainCi(1); }
    if (k->victim == g_lastTarget) g_lastTarget = 0;
    if (k->victim == g_chargeTarget) g_chargeHits = 0;
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

static void OnWeapon(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfWeaponEvent* w = (const HfWeaponEvent*)info;
    if (w->avatar && w->avatar != atomic_load(&g_avatar)) { atomic_store(&g_avatar, w->avatar); g_avatarNet = 0; g_alive = 1; LearnNet(); }
    if (w->selection >= 1 && w->selection <= 3 && w->entity) { g_weapon[w->selection] = w->entity; g_weaponRes[w->selection] = w->resource; }
}

/* ---- her body: more health and stamina, and the price of running dry --------------------------------------------- */
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
/* One Hellbomb per run-out: armed while her bar is above a fifth, fired when it reaches 0 (the bar then sits at 0
 * through the recovery delay, which must not fire it again), and spawned once its packages are in. */
static double g_boomPending;
static int g_boomArmed;
static void Exhaustion(void)
{
    if (!s_hellbomb || !g_alive) { g_boomPending = 0; return; }
    float v = StaminaNow();
    static int noted;
    if (v >= 0 && !noted) { noted = 1; Log("stamina: %.2f (read live)", (double)v); }
    if (v > 0.2f) g_boomArmed = 1;
    /* empty for hellbomb_hold s in a row: the bar flickers to 0 for a moment at times while it is not run dry */
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
    if (r == HF_ACT_WAITING) return;                                     /* its packages: again next frame */
    g_boomPending = 0;
    Log("out of stamina: HELLBOMB (row %d, answer %d)", s_hellbombRow, r);
}

static void Reset(const char* why)
{
    if (g_casts[0] || g_casts[1] || g_casts[2])
        Log("%s: casts %u / %u / %u, %u marked, %u stuns, %u charged hits, %u own pulses spared her, %u knockdowns stopped",
            why, g_casts[0], g_casts[1], g_casts[2], g_marked, g_stuns, g_boosted, g_selfSpared, g_knockdowns);
    Speed(0);
    Stealth(0);
    Body(!strcmp(why, "mission start"));
    /* the G-31 Arc grenade's package: its detonation (157) plays its blast and sound only once that is in */
    if (!strcmp(why, "mission start") && s_arcRow == 157 && !hf->load_package(self, UINT64_C(0xDDCEA5FDD4071767)))
        Log("the G-31 Arc grenade's package could not be loaded ahead: its arcs may stay silent");
    if (!strcmp(why, "mission start") && s_hellbomb && s_hellbombRow == 125)
        if (!hf->load_package(self, UINT64_C(0x346D785D6CA13FF4))) Log("the Hellbomb's package could not be loaded ahead");  /* loadout/hellbomb_backpack */
    g_boomPending = 0;
    g_boomArmed = 0;
    memset(g_marks, 0, sizeof g_marks);
    memset(g_held, 0, sizeof g_held);
    memset(g_pulses, 0, sizeof g_pulses);
    memset(g_casts, 0, sizeof g_casts);
    g_marked = g_stuns = g_boosted = g_selfSpared = g_knockdowns = 0;
    g_ci = 0;
    g_cdWild = g_cdLeap = g_cdUlt = 0;
    g_lastTarget = g_chargeTarget = 0;
    g_chargeHits = 0;
    g_pulsesLeft = 0;
    g_retryCount = 0;
    g_arcUntil = g_stealthUntil = g_drUntil = g_speedUntil = g_forwardAt = g_landAt = 0;
    g_leap = 0;
    atomic_store(&g_shieldUntilMs, 0);
}

/* ---- the knockdown window: hooks on the game's hit code (as Primed Sure Footed) -------------------------------------- */
#define HIT_SIZE 0x118u
#define RESULT_SIZE 0x24u
static atomic_int g_hooked;
static _Atomic unsigned g_kdStopped;
static _Atomic unsigned g_ownStopped;
/* A hit on her: one of her own blasts (an explosion from within 14 m of one she set off in the last 1.2 s) does
 * nothing to her: its result is cleared (damage, stagger, push, knockdown, reaction). In Leaping Pursuit's window any
 * hit's knockdown is cleared. */
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
        for (int i = 0; i < 8; i++)
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
    if (now > atomic_load(&g_shieldUntilMs)) return;
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
    if (!g || !x || gameSize != 74727424u || exeSize != 60719104u) { Log("REFUSED: the game's modules are not build " BUILD "'s: no aim, no knockdown window"); return; }
    game = g;
    exe = x;
    for (unsigned i = 0; i < sizeof kPins / sizeof kPins[0]; i++)
        if (!hf->code_matches(game + kPins[i].rva, kPins[i].hex)) { Log("knockdown window off: game.dll+0x%X is not the reviewed code", kPins[i].rva); return; }
    if (hf->hook(self, game + 0x129DC30, (void*)Calculate, (void**)&original_calculate) ||
        hf->hook(self, game + 0x129D180, (void*)Blast, (void**)&original_blast) ||
        hf->hook(self, game + 0x129DD30, (void*)Emit, (void**)&original_emit))
    { Log("knockdown window off: a hook could not be placed"); return; }
    atomic_store(&g_hooked, 1);
    Log("knockdown window ready (hit calculation, explosion dispatch and emission hooked)");
    if (hf->code_matches(game + 0x5A2BB0, "48 89 5c 24 08 48 89 6c 24 10 56 57 41 54 41 56 41 57 48 83 ec 40") &&
        hf->code_matches(game + 0x4A7550, "48 89 5c 24 18 57 48 83 ec 20 3b 15 ?? ?? ?? ?? 48 8b 1d") &&
        hf->code_matches(exe + 0x79F860, "33 c0 4c 8d 05 c7 b2 01 02 0f 1f 80 00 00 00 00 49 39 0c c0 74 0c ff c0 83 f8 04 75 f3") &&
        hf->code_matches(exe + 0x7F4590, "48 8b c4 f3 0f 11 58 20 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48"))
    {
        g_setPosition = (SetVecFn)(game + 0x5A2BB0);
        g_setVelocity = (SetVecFn)(game + 0x4A7550);
        g_worldId = (WorldFn)(exe + 0x79F860);
        g_raycast = (RayFn)(exe + 0x7F4590);
        Log("teleport ready (the warp's position setter, the velocity setter and the raycast)");
    }
    else Log("teleport off: its functions are not the reviewed code");
    g_staminaOk = hf->code_matches(game + 0xA50E2D, "f3 42 0f 11 8c 11 54 78 54 00");
    Log(g_staminaOk ? "stamina readable (the game's stamina write is the reviewed code)" : "stamina off: its code is not the reviewed one");
}

static void EntitiesReady(HfMod* mod, HfEvent event, void* user)
{
    if (s_ems) Log("EMS explosion row %d under her zaps (from the ini)", s_ems);

}

/* ---- the HUD state (read by the Lenna HUD addon through LuaJIT's ffi) --------------------------------------------- */
typedef struct LennaHud
{
    uint32_t version, in_mission, ci, ci_max;
    float cd[3], cd_max[3];          /* seconds left, full cooldowns: Wild Extinction, Leaping Pursuit, Hunting Strategy */
    float charge, arc, stealth, shield;  /* seconds left of each effect (charge: its window) */
    uint32_t charge_hits, marked, keys[3];
    char key_label[3][24];               /* version 2: the keys as written ("Shift+Q") */
    float stamina;                       /* version 3: her stamina 0..1 (-1: unknown) */
    uint32_t hellbomb;                   /* version 3: 1 when running dry sets off the Hellbomb */
} LennaHud;
static LennaHud g_hud = {3};
static float StaminaNow(void);
HF_MOD_EXPORT LennaHud* lenna_hud_state(void)
{
    g_hud.in_mission = (uint32_t)(g_inMission && g_alive);
    g_hud.ci = (uint32_t)g_ci;
    g_hud.ci_max = (uint32_t)s_ciMax;
    double r[3] = {g_cdWild, g_cdLeap, g_cdUlt};
    float m[3] = {s_weCd, s_lpCd, s_hsCd};
    for (int i = 0; i < 3; i++) { g_hud.cd[i] = (float)fmax(0, r[i] - g_now); g_hud.cd_max[i] = m[i]; }
    g_hud.charge = g_chargeHits > 0 ? (float)fmax(0, g_chargeUntil - g_now) : 0;
    g_hud.charge_hits = (uint32_t)(g_chargeHits > 0 && g_chargeHits < 100000 && g_now <= g_chargeUntil ? g_chargeHits : 0);
    g_hud.arc = (float)fmax(0, g_arcUntil - g_now);
    g_hud.stealth = (float)fmax(0, g_stealthUntil - g_now);
    g_hud.shield = (float)fmax(0, g_drUntil - g_now);
    unsigned n = 0;
    for (int i = 0; i < 64; i++) n += g_marks[i].until > g_now;
    g_hud.marked = n;
    for (int i = 0; i < 3; i++)
    {
        g_hud.keys[i] = (uint32_t)s_keys[i].vk;
        memcpy(g_hud.key_label[i], s_keys[i].label, sizeof g_hud.key_label[i]);
    }
    g_hud.stamina = g_inMission && g_alive ? StaminaNow() : -1;
    g_hud.hellbomb = (uint32_t)(s_hellbomb != 0);
    return &g_hud;
}

/* ---- the frame ------------------------------------------------------------------------------------------------- */
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
    if (!g_inMission) return;
    /* keys (edges) */
    static int prev[3];
    int focused = Focused();
    int mods = focused ? ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? KM_SHIFT : 0) | ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? KM_CTRL : 0) |
                         ((GetAsyncKeyState(VK_MENU) & 0x8000) ? KM_ALT : 0) : 0;
    for (int i = 0; i < 3; i++)
    {
        /* a modifier bound alone ignores its own bit */
        int own = s_keys[i].vk == VK_SHIFT ? KM_SHIFT : s_keys[i].vk == VK_CONTROL ? KM_CTRL : s_keys[i].vk == VK_MENU ? KM_ALT : 0;
        int down = focused && (GetAsyncKeyState(s_keys[i].vk) & 0x8000) != 0 && (mods & ~own) == s_keys[i].mods;
        if (down && !prev[i] && g_alive && atomic_load(&g_avatar))
        {
            if (i == 0) WildExtinction();
            else if (i == 1) LeapingPursuit();
            else HuntingStrategy();
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
        if (t)
        {
            static unsigned noted;
            if (noted++ < 3) Log("she hit entity %u (the game's hit code)", t);
            g_lastTarget = t;
            memcpy(g_lastPos, g_hookPos, sizeof g_lastPos);
            g_lastAt = g_now;
        }
    }
    /* pulses that waited for their packages */
    for (int i = 0; i < g_retryCount; i++)
    {
        int r = hf->spawn_explosion(self, g_retry[i].row, g_retry[i].p);
        if (r != HF_ACT_WAITING || g_now > g_retry[i].at)
        {
            Act("a blast (after its packages)", r);
            unsigned slot = g_ownNext++ % 8;
            memcpy(g_ownPos[slot], g_retry[i].p, sizeof g_ownPos[slot]);
            atomic_store(&g_ownMs[slot], GetTickCount64());
            g_retry[i] = g_retry[--g_retryCount];
            i--;
        }
    }
    LeapFrame();
    HoldFrame();
    Exhaustion();
    /* landing: stun the last enemy she shot, if near */
    if (g_landAt > 0 && g_now >= g_landAt)
    {
        g_landAt = 0;
        float pos[3];
        if (TargetFresh(6) && PlayerPose(pos, NULL) && Dist2(pos, g_lastPos) <= s_lpReach * s_lpReach)
        {
            Stun(g_lastTarget, HF_STATUS_STUN_MEDIUM);
            if (MarkEnemy(g_lastTarget)) GainCi(1);
        }
    }
    if (g_speedOn && g_now >= g_speedUntil) Speed(0);
    if (g_stealthOn && g_now >= g_stealthUntil) Stealth(0);
    /* Voltage pulses */
    if (g_pulsesLeft > 0 && g_now >= g_nextPulse)
    {
        g_pulsesLeft--;
        g_nextPulse = g_now + s_hsPulseStep;
        Zap();
    }
    unsigned own = atomic_exchange(&g_ownStopped, 0);
    if (own) { g_selfSpared += own; static unsigned noted; if (noted++ < 5) Log("King's Authority: %u hit(s) of her own blasts cleared on her", own); }
    unsigned kd = atomic_exchange(&g_kdStopped, 0);
    if (kd) { g_knockdowns += kd; Log("Leaping Pursuit: %u knockdown(s) stopped", kd); }
}

/* ---- the Mod Options Menu (through the Lenna HUD addon): read and set a setting by its lenna.ini name --------------- */
static const struct { const char* name; float* f; int* i; } kOptions[] = {
    {"ci_max", NULL, &s_ciMax}, {"we_cooldown", &s_weCd, NULL}, {"we_window", &s_weWindow, NULL}, {"we_mult", &s_weMult, NULL},
    {"we_mult_enhanced", &s_weMultEnh, NULL}, {"lp_cooldown", &s_lpCd, NULL}, {"lp_distance", &s_lpDistance, NULL},
    {"lp_window", &s_lpWindow, NULL}, {"lp_damage_taken", &s_lpTaken, NULL}, {"lp_heal", &s_lpHeal, NULL}, {"lp_speed", &s_lpSpeed, NULL},
    {"lp_speed_seconds", &s_lpSpeedSeconds, NULL}, {"lp_stun", &s_lpStun, NULL}, {"lp_burst_radius", &s_lpBurstRadius, NULL},
    {"hs_cooldown", &s_hsCd, NULL}, {"hs_radius", &s_hsRadius, NULL}, {"hs_pulses", NULL, &s_hsPulses}, {"hs_stun", &s_hsStun, NULL},
    {"hs_arc", &s_hsArc, NULL}, {"hs_stealth", &s_hsStealth, NULL}, {"mark_mult", &s_markMult, NULL}, {"mark_seconds", &s_markSeconds, NULL},
    {"hp_mult", &s_hpMult, NULL}, {"stamina_mult", &s_staminaMult, NULL}, {"hellbomb_on_exhaustion", NULL, &s_hellbomb},
};
HF_MOD_EXPORT int lenna_option_get(const char* name, double* out)
{
    if (!name || !out) return 0;
    for (unsigned k = 0; k < sizeof kOptions / sizeof kOptions[0]; k++)
        if (!strcmp(kOptions[k].name, name)) { *out = kOptions[k].f ? (double)*kOptions[k].f : (double)*kOptions[k].i; return 1; }
    return 0;
}
HF_MOD_EXPORT int lenna_option_set(const char* name, double value)
{
    double before;
    if (!isfinite(value) || !lenna_option_get(name, &before)) return 0;
    Setting(name, value);
    double now;
    lenna_option_get(name, &now);
    if (now != before)
    {
        Log("option %s: %g -> %g (Mod Options Menu)", name, before, now);
        /* her body changes from here on in a mission */
        if (g_inMission && (!strcmp(name, "hp_mult") || !strcmp(name, "stamina_mult"))) { Body(0); Body(1); }
        if (g_ci > s_ciMax) g_ci = s_ciMax;
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
    api->log(mod, "Lenna %s: keys %s / %s / %s (Wild Extinction, Leaping Pursuit, Hunting Strategy); CI up to %d", VERSION, s_keys[0].label,
        s_keys[1].label, s_keys[2].label, s_ciMax);
    if (s_badKey) api->log(mod, "lenna.ini: a key could not be read (bits %d: wild, leap, ult); its default stays", s_badKey);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL) || api->on(mod, HF_EVENT_ENTITIES_READY, EntitiesReady, NULL)) return 1;
    /* registered: from here the DLL stays loaded; a failure below leaves it inert */
    int ok = api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) &&
             api->on_game_thread(mod, Tick, NULL);
    if (!ok) api->log(mod, "events or the game thread are unavailable: Lenna stays off");
    return 0;
}
