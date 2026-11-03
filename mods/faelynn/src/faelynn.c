/* Faelynn (yui.faelynn): Faelynn's kit from Girls' Frontline 2 as abilities on keys, for your own Helldiver (solo or host).
 *
 * Corrosion becomes the G-4 Gas grenade's detonation (explosion row 177: a lingering cloud, her Toxic Mist), the
 * enemies it catches are gassed and get her Scent Trail mark (they take mark_mult times the damage). HellForge applies
 * no acid status, and the Acid Warrior's burst (row 310) spawned outside its own package crashed the next mission load.
 * Her resources are Combo Pounce (0 to cp_max, spent by Pursuit) and Hunt Tracking (stacks
 * from her hits on marked enemies; each adds ht_per_stack to her damage, twice that while she stands in her own mist).
 *   Triple Maul (key_maul, default Z): a gas blast on the enemy nearest her aim (else the last she hit), Scent Trail
 *     on everything around it, Toxic Mist (the gas cloud) and 1 Combo Pounce.
 *   Knife-Eared Hunt (key_hunt, default X): for kh_seconds, faster jog and sprint, stamina that never drains, kh_mult
 *     times her damage, and Pursuit Instinct that costs no Combo Pounce.
 *   Loyal Dash (key_ult, default C): Lenna's teleport ld_distance m along her aim (short of walls) that leaves a
 *     strip of gas blasts and mist and a cloud where she stops; enemies within ld_width m of it are marked.
 *   Pursuit Instinct I-III (key_ult again, within pursuit_window s each): Loyal Dash repeated up to three times, 1
 *     Combo Pounce each; stage k adds k + 1 Corrosion stacks on the strip, her hits on marked enemies deal
 *     pursuit_bonus x k more and she runs pursuit_speed x k faster, for pursuit_hold s.
 *   Determined Counterpounce (passive): never knocked down or ragdolled (steady = 1); her own gas spares her, and while
 *     she stands in it she takes damage_taken of any hit (0.1: 90% less). Gas immunity (ADVANCED FILTRATION, solo).
 *   Corrosion: every second an enemy spends in her gas adds a stack (her skills add more): gas on it hits
 *     corrosion_per_stack x stacks harder, and every hit on it (anyone's) pierces shred_per_stack x stacks more armour.
 *   Last Bark: a hit that would kill her is blocked; a ring of gas blasts goes off where she stands (bark_damage a hit
 *     on enemies) and her immunity drops, so her own gas takes her.
 *   T-Doll body: hp_mult health, stamina_mult sprint, and a Hellbomb on her if her stamina ever runs dry.
 *
 * Research credit: the hit-result layout, the hooked functions, the entity and scene-graph reads, the raycast come from
 * Helldiver Survival Rework 0.3.2 by enochlophobianumerophobia (MIT) and Scope of Authority's Apex knockdown notes,
 * whose research used HD2Runtime by SkyeShade's catalogues (https://github.com/SkyeShade/HD2Runtime); the teleport from
 * HD2Runtime's warp research and a Warp Pack probe. See THIRD_PARTY_NOTICES.md. */
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

#define VERSION "0.3.5"
#define BUILD "25480438"

#define KB_STEADY 1
#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"
#include "kitbase/kb_body.h"
#include "kitbase/kb_hits.h"

/* ---- settings ------------------------------------------------------------------------------------------------- */
static Key s_keys[3] = {{'Z', 0, "Z"}, {'X', 0, "X"}, {'C', 0, "C"}};
static int s_cpMax = 3, s_htMax = 10, s_acidRow = 177, s_gasImmune = 1;
static float s_tmCd = 10, s_tmRadius = 5, s_tmRange = 40, s_pursuitWindow = 4, s_pursuitBonus = 0.5f, s_pursuitHold = 4, s_pursuitSpeed = 0.15f;
static float s_khCd = 35, s_khSeconds = 8, s_khSpeed = 1.3f, s_khMult = 1.3f;
static float s_ldCd = 45, s_ldDistance = 12, s_ldWidth = 3, s_ldStep = 3, s_ldWindow = 1.5f;
static float s_lpClimb = 6, s_lpDrop = 12;
static float s_markMult = 1.3f, s_markSeconds = 6, s_htPer = 0.05f, s_htSeconds = 10, s_mistSeconds = 8, s_mistRadius = 5;
static float s_shredPer = 0.5f, s_shredMax = 3;
static float s_barkRadius = 6, s_corrPer = 0.5f, s_taken = 0.1f, s_takenOut = 1.0f;
static int s_corrMax = 6;
static int s_barkBlasts = 6, s_barkDamage = 2000;

static int s_badKey;
static void Setting(const char* key, double v)
{
#define CL(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#define F(name, var, lo, hi) else if (!strcmp(key, name)) var = (float)CL(v, lo, hi)
#define I(name, var, lo, hi) else if (!strcmp(key, name)) var = (int)CL(v, lo, hi)
    if (0) {}
    I("cp_max", s_cpMax, 1, 10); I("ht_max", s_htMax, 0, 50); F("ht_per_stack", s_htPer, 0, 1); F("ht_seconds", s_htSeconds, 1, 120);
    F("tm_cooldown", s_tmCd, 0, 600); F("tm_radius", s_tmRadius, 1, 30); F("tm_range", s_tmRange, 5, 200);
    F("pursuit_window", s_pursuitWindow, 0.5, 30); F("pursuit_bonus", s_pursuitBonus, 0, 5); F("pursuit_hold", s_pursuitHold, 0.5, 30); F("pursuit_speed", s_pursuitSpeed, 0, 1);
    F("kh_cooldown", s_khCd, 0, 600); F("kh_seconds", s_khSeconds, 1, 60); F("kh_speed", s_khSpeed, 1, 3); F("kh_mult", s_khMult, 1, 5);
    F("ld_cooldown", s_ldCd, 0, 600); F("ld_distance", s_ldDistance, 2, 30); F("ld_width", s_ldWidth, 0.5, 15); F("ld_step", s_ldStep, 1, 10);
    F("ld_window", s_ldWindow, 0, 10); F("lp_climb", s_lpClimb, 0.5, 30); F("lp_drop", s_lpDrop, 0.5, 50);
    F("mark_mult", s_markMult, 1, 10); F("mark_seconds", s_markSeconds, 0.5, 60); F("mist_seconds", s_mistSeconds, 0, 60);
    F("mist_radius", s_mistRadius, 1, 20); I("steady", s_steady, 0, 1); I("gas_explosion", s_acidRow, 0, 100000); I("gas_immune", s_gasImmune, 0, 1);
    F("bark_radius", s_barkRadius, 0, 30); F("corrosion_per_stack", s_corrPer, 0, 5); F("damage_taken", s_taken, 0, 1); F("damage_taken_outside", s_takenOut, 0, 1); I("corrosion_max", s_corrMax, 1, 20); I("bark_blasts", s_barkBlasts, 0, 24); I("bark_damage", s_barkDamage, 0, 100000); F("shred_per_stack", s_shredPer, 0, 5); F("shred_max", s_shredMax, 0, 10);
    F("hp_mult", s_hpMult, 0.1, 10); F("stamina_mult", s_staminaMult, 0.1, 20); I("hellbomb_on_exhaustion", s_hellbomb, 0, 1); I("hellbomb_explosion", s_hellbombRow, 0, 100000); F("hellbomb_hold", s_hellbombHold, 0, 10);
#undef F
#undef I
#undef CL
}

static void LoadSettings(void)
{
    char path[MAX_PATH * 2], line[256];
    snprintf(path, sizeof path, "%s\\faelynn.ini", hf->mod_folder(self));
    FILE* f = OpenUtf8(path, L"r");
    if (f) hf->log(self, "settings: read %s", path);
    else
    {
        hf->log(self, "settings: no faelynn.ini in %s (it ships with the mod), so the built-in defaults; the MODS tab still changes them", path);
        FILE* w = NULL;   /* never written while the game runs (see OpenUtf8); the text stays as the defaults' source */
        if ((f = w) != NULL)
        {
            fprintf(f, "; Faelynn %s. Keys: a letter, digit, F1-F24, Mouse3, Mouse4 or Mouse5, with Shift+, Ctrl+ or Alt+ in front for a\n"
                       "; combo (Shift+Q), or Shift, Ctrl or Alt alone. The game still sees the keys too, so pick ones it leaves alone.\n"
                       "key_maul = %s\nkey_hunt = %s\nkey_ult = %s\n"
                       "; Combo Pounce (Pursuit's fuel) and Hunt Tracking (damage stacks: ht_per_stack each, twice in her mist; lost\n"
                       "; ht_seconds after her last stack).\ncp_max = %d\nht_max = %d\nht_per_stack = %g\nht_seconds = %g\n"
                       "; Triple Maul: cooldown, blast radius, how far along her aim it finds a target. Pursuit Instinct (Loyal Dash\n"
                       "; again, up to 3 times, 1 Combo Pounce each): the window for the next one; per stage, the bonus on marked\n"
                       "; enemies and her extra speed (stacking), and how long both hold.\n"
                       "tm_cooldown = %g\ntm_radius = %g\ntm_range = %g\npursuit_window = %g\npursuit_bonus = %g\npursuit_hold = %g\npursuit_speed = %g\n"
                       "; Knife-Eared Hunt: cooldown, length, speed and damage.\nkh_cooldown = %g\nkh_seconds = %g\nkh_speed = %g\nkh_mult = %g\n"
                       "; Loyal Dash: cooldown, teleport distance, strip width and blast spacing, the protected window after it; the\n"
                       "; ground she lands on may be lp_climb m higher or lp_drop m lower.\n"
                       "ld_cooldown = %g\nld_distance = %g\nld_width = %g\nld_step = %g\nld_window = %g\nlp_climb = %g\nlp_drop = %g\n"
                       "; Scent Trail: marked enemies take mark_mult times the damage for mark_seconds. Toxic Mist: zones of\n"
                       "; mist_radius m that last mist_seconds (her own gas spares her there). steady: 1 never knocked down.\n"
                       "; gas_explosion: her blasts (177: the G-4 Gas grenade's; 0: none). gas_immune: 1 any gas leaves her alone (no damage,\n"
                       "; cough or slow): ADVANCED FILTRATION in the helmet's passive slot, its gas resistance set to full (solo).\n"
                       "mark_mult = %g\nmark_seconds = %g\nmist_seconds = %g\nmist_radius = %g\nsteady = %d\ngas_explosion = %d\ngas_immune = %d\n"
                       "; Last Bark: bark_blasts gas blasts in a ring of bark_radius m where she goes down; each hit of that gas on an enemy deals\n"
                       "; at least bark_damage (0: the gas's own damage).\nbark_radius = %g\nbark_blasts = %d\nbark_damage = %d\n"
                       "; Corrosion: each of her gas hits on an enemy adds a stack (Pursuit Instinct I-III add 2, 3, 4; at most corrosion_max);\n"
                       "; gas damage on it is multiplied by 1 + corrosion_per_stack x stacks while its mark lasts.\n"
                       "corrosion_per_stack = %g\ncorrosion_max = %d\n"
                       "; Armor shred: every hit on a corroded enemy (anyone's) pierces shred_per_stack x stacks more armour (rounded,\n"
                       "; at most shred_max): 1 stack +1, 6 stacks +3 at the defaults.\nshred_per_stack = %g\nshred_max = %g\n"
                       "; Her toughness: the share of any damage she takes while she stands in her own gas (damage_taken: 0.1 is 90%% less;\n"
                       "; 0: invulnerable there) and outside it (damage_taken_outside: 1 is full damage).\n"
                       "damage_taken = %g\ndamage_taken_outside = %g\n"
                       "; The T-Doll's body: hp_mult times a Helldiver's health, stamina_mult times her sprint before the bar empties.\n"
                       "hp_mult = %g\nstamina_mult = %g\n"
                       "; If her stamina ever runs out, hellbomb_on_exhaustion (1) sets off hellbomb_explosion (125: the B-100 Portable\n"
                       "; Hellbomb's) on her, once the bar has stayed empty hellbomb_hold s (it flickers to 0 at times).\n"
                       "hellbomb_on_exhaustion = %d\nhellbomb_explosion = %d\nhellbomb_hold = %g\n",
                VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, s_cpMax, s_htMax, (double)s_htPer, (double)s_htSeconds,
                (double)s_tmCd, (double)s_tmRadius, (double)s_tmRange, (double)s_pursuitWindow, (double)s_pursuitBonus, (double)s_pursuitHold, (double)s_pursuitSpeed,
                (double)s_khCd, (double)s_khSeconds, (double)s_khSpeed, (double)s_khMult,
                (double)s_ldCd, (double)s_ldDistance, (double)s_ldWidth, (double)s_ldStep, (double)s_ldWindow, (double)s_lpClimb, (double)s_lpDrop,
                (double)s_markMult, (double)s_markSeconds, (double)s_mistSeconds, (double)s_mistRadius, s_steady, s_acidRow, s_gasImmune,
                (double)s_barkRadius, s_barkBlasts, s_barkDamage, (double)s_corrPer, s_corrMax, (double)s_shredPer, (double)s_shredMax, (double)s_taken, (double)s_takenOut,
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
        int k = !strcmp(key, "key_maul") ? 0 : !strcmp(key, "key_hunt") ? 1 : !strcmp(key, "key_ult") ? 2 : -1;
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

/* An entity's health record (HD2Runtime's research: the health manager at game+0x3326688, its open-addressed entity
 * hash, 440-byte records with health at +20 and life at +412, 0 alive). 1 with *health and *life filled */
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

/* ---- state ---------------------------------------------------------------------------------------------------- */
static double g_cdMaul, g_cdHunt, g_cdUlt;   /* ready at */
static uint32_t g_lastTarget;
static float g_lastPos[3];
static double g_lastAt;
static int g_cp, g_ht;
static double g_htAt;

static int g_pursuitStage;            /* Pursuit Instinct stages done after the last Loyal Dash (0..3) */
static double g_pursuitUntil, g_bonusUntil;
static float g_bonus;                 /* extra damage on marked enemies from the last pounce */
static double g_huntUntil;
static double g_drUntil;
static int g_leap;
static double g_leapT0;
static float g_leapDir[2];
static int g_speedOn;
static float g_baseJog = -1, g_baseSprint = -1;
typedef struct Pulse { float p[3]; double at; uint32_t row; } Pulse;
static Pulse g_pulses[12];
static int g_npulse;
typedef struct Mark { uint32_t entity; double until; int stacks; double stackAt; } Mark;
static Mark g_marks[64];
static _Atomic unsigned g_pierced;   /* hits that pierced more armour (Corrosion's shred) */
typedef struct Mist { float p[3]; double until; } Mist;
static Mist g_mist[16];
static int g_nmist;
static unsigned g_stackHits, g_shrugged;
static unsigned g_casts[4], g_marked, g_pounces, g_boosted, g_selfSpared, g_knockdowns, g_acidApplied;

static Mark* MarkOf(uint32_t entity)
{
    for (int i = 0; i < 64; i++) if (g_marks[i].entity == entity && g_marks[i].until > g_now) return &g_marks[i];
    return NULL;
}
/* Scent Trail; 1 when newly marked */
static int MarkEnemy(uint32_t entity)
{
    if (!entity || entity == atomic_load(&g_avatar)) return 0;
    Mark* m = MarkOf(entity);
    if (m) { m->until = g_now + s_markSeconds; return 0; }
    int slot = 0;
    for (int i = 0; i < 64; i++) if (g_marks[i].until < g_marks[slot].until) slot = i;
    g_marks[slot].entity = entity;
    g_marks[slot].until = g_now + s_markSeconds;
    g_marks[slot].stacks = 0;
    g_marks[slot].stackAt = 0;
    g_marked++;
    return 1;
}
static void GainCp(int n) { g_cp += n; if (g_cp > s_cpMax) g_cp = s_cpMax; if (g_cp < 0) g_cp = 0; }
static void GainHt(void) { if (g_ht < s_htMax) g_ht++; g_htAt = g_now; }
static void Mist_(const float at[3])
{
    if (s_mistSeconds <= 0) return;
    Mist* m = &g_mist[g_nmist++ % 16];
    memcpy(m->p, at, sizeof m->p);
    m->until = g_now + s_mistSeconds;
}
static int InMist(const float at[3])
{
    for (int i = 0; i < 16; i++) if (g_mist[i].until > g_now && Dist2(at, g_mist[i].p) <= s_mistRadius * s_mistRadius) return 1;
    return 0;
}
/* Corrosion on an enemy: gassed (HellForge's gas status) and Scent Trail */
static void CorrodeN(uint32_t e, int n)
{
    if (Act("gas", hf->apply_status(self, e, HF_STATUS_GAS, 100.0f))) g_acidApplied++;
    MarkEnemy(e);
    Mark* m = MarkOf(e);
    if (m) { m->stacks += n; if (m->stacks > s_corrMax) m->stacks = s_corrMax; }
}
static void Corrode(uint32_t e) { CorrodeN(e, 1); }

/* ---- speed, endless stamina ------------------------------------------------------------------------------------ */
static float SpeedMult(void);
static float g_speedNow = 1;
static void Speed(int on)
{
    HfField jog = HF_FIELD(Avatar, jog), sprint = HF_FIELD(Avatar, sprint);
    if (!on)
    {
        if (g_speedOn) { hf->set_live(self, AVATAR, &jog, NULL, HF_LIVE_SHARED); hf->set_live(self, AVATAR, &sprint, NULL, HF_LIVE_SHARED); }
        g_speedOn = 0;
        g_speedNow = 1;
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
    float m = SpeedMult();
    HfValue j = HF_F32(fminf(12, g_baseJog * m)), s = HF_F32(fminf(12, g_baseSprint * m));
    int r1 = hf->set_live(self, AVATAR, &jog, &j, HF_LIVE_SHARED), r2 = hf->set_live(self, AVATAR, &sprint, &s, HF_LIVE_SHARED);
    Log("speed x%.2f: jog %.1f -> %.1f, sprint %.1f -> %.1f m/s (applies: %d / %d; 1 = now)", (double)m, (double)g_baseJog, j.number,
        (double)g_baseSprint, s.number, r1, r2);
    g_speedNow = m;
    g_speedOn = 1;
}
/* ---- the teleport: the game's own position setter, as the LIFT-182 Warp Pack calls it (Warp Probe: game.dll+0x88D795
 * calls 0x5A2BB0 with (NULL, the Helldiver's entity, the destination), then zeroes its velocity with 0x4A7550), with a
 * wall and ground check through the game's raycast (as Helldiver Survival Rework's cover check calls it) -------------- */
static int Blast_(uint32_t row, const float at[3], const char* what);
typedef void (*SetVecFn)(void*, uint32_t, const float*);
typedef uint32_t (*WorldFn)(const void*);
typedef uint32_t (*RayFn)(uint32_t, const void*, const void*, float, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t);
static SetVecFn g_setPosition, g_setVelocity;
static WorldFn g_worldId;
static RayFn g_raycast;
/* The nearest static obstruction along a ray (static actors only, so enemies are not walls): its distance, len + 1
 * when clear, or -1 when the query cannot be made */
static float RayWith(uint32_t unit, const float from[3], const float dir[3], float len, uint32_t filter, uint32_t flags, unsigned* count);
static float g_rayPrefer = -1;
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
/* The units of the enemies near a dash (refreshed per dash): their bodies are no floor, and a spot under one is taken */
static int g_underFoe;
static uint32_t g_foeUnits[256];
static int g_nFoeUnits;
static int IsFoeUnit(uint32_t u)
{
    for (int i = 0; i < g_nFoeUnits; i++) if (g_foeUnits[i] == u) return 1;
    return 0;
}

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
        if (IsFoeUnit(hu)) { g_underFoe = 1; continue; }                              /* nor is an enemy's back */
        z[k++] = top[2] - d;
    }
    for (int i = 1; i < k; i++)        /* highest first */
        for (int j = i; j > 0 && z[j] > z[j - 1]; j--) { float t = z[j]; z[j] = z[j - 1]; z[j - 1] = t; }
    return k;
}
/* Whether a Helldiver fits standing at (x, y, z): nothing within 1.8 m above her feet, nothing within 0.4 m around
 * her at knee and chest height; 1 fits */
static int FoeAbove(uint32_t unit, const float from[3], float len)
{
    uintptr_t world = Pointer(game + 0x346BFA0);
    if (!world || !g_raycast || !g_worldId) return 0;
    uint32_t id = g_worldId((const void*)world);
    if (id >= 4) return 0;
    uint8_t hits[16 * 44];
    float up[3] = {0, 0, 1};
    memset(hits, 0, sizeof hits);
    uint32_t n = g_raycast(id, from, up, len, 2, 1, 0, 0x80000009u, unit, hits, 16);
    for (uint32_t i = 0; i < n && i < 16; i++) if (IsFoeUnit(Word(hits + 44u * i + 28))) return 1;
    return 0;
}
static int Fits(uint32_t unit, float x, float y, float z)
{
    float up[3] = {0, 0, 1}, foot[3] = {x, y, z + 0.1f};
    float r = RayWith(unit, foot, up, 1.8f, 0, 0x80000009u, NULL);
    if (r < 0 || r <= 1.8f) return 0;
    if (g_nFoeUnits && FoeAbove(unit, foot, 12.0f)) return 0;   /* under a Bile Titan, a Charger, a hulk: taken */
    for (int h = 0; h < 2; h++)
        for (int k = 0; k < 4; k++)
        {
            float a = 1.5708f * (float)k + 0.785f;
            float c[3] = {x, y, z + (h ? 1.3f : 0.6f)}, dir[3] = {cosf(a), sinf(a), 0};
            float w = RayWith(unit, c, dir, 0.4f, 0, 0x80000009u, NULL);
            if (w < 0 || w <= 0.4f) return 0;
        }
    return 1;
}

static Pulse g_retry[6];  /* blasts waiting for their packages (at: give up then) */
static int g_retryCount;
/* One of her blasts (an EMS pulse, an arc): it never touches or throws her, and the enemies it hits are marked */
static int Blast_(uint32_t row, const float at[3], const char* what)
{
    if (!row) return 0;
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
    if (r != HF_ACT_WAITING) Act(what, r);
    return r;
}
/* ---- enemies around her (her area attacks): spawned entities whose kind runs a behaviour and is no turret or
 * Helldiver, and anything her own hits landed on; dropped on death or when the entity id is reused ------------------ */

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
/* The enemy nearest her aim line (within tm_range m and 4 m of the line, nearest the line first), else the last she hit */
static uint32_t AimTarget(float at[3])
{
    float eye[3], aim[3], p[3];
    uint32_t best = 0;
    float bestScore = 1e9f;
    if (PlayerPose(eye, aim))
    {
        eye[2] += 1.5f;
        for (int i = 0; i < FOES; i++)
        {
            if (!g_foe[i] || !FoePos(i, p)) continue;
            p[2] += 1.0f;
            float v[3] = {p[0] - eye[0], p[1] - eye[1], p[2] - eye[2]};
            float t = v[0] * aim[0] + v[1] * aim[1] + v[2] * aim[2];
            if (t < 1 || t > s_tmRange) continue;
            float off2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2] - t * t;
            float off = off2 > 0 ? sqrtf(off2) : 0;
            if (off > 2.0f + t * 0.08f) continue;               /* a cone: 2 m wide near her, wider far out */
            float score = off / (1 + t * 0.08f) + t * 0.02f;
            if (score < bestScore) { bestScore = score; best = g_foe[i]; memcpy(at, p, 12); at[2] -= 1.0f; }
        }
    }
    if (!best && g_lastTarget && g_now - g_lastAt <= 6 && EntityPos(g_lastTarget, at)) best = g_lastTarget;
    return best;
}
/* An enemy standing within 1.5 m of the spot */
static int Occupied(const float at[3])
{
    float p[3];
    for (int i = 0; i < FOES; i++)
    {
        if (!g_foe[i] || !FoePos(i, p)) continue;
        float dx = p[0] - at[0], dy = p[1] - at[1], dz = p[2] - at[2];
        if (dx * dx + dy * dy <= 1.5f * 1.5f && dz > -2 && dz < 2.5f) return 1;
    }
    return 0;
}
/* Whether a surface with another one `gap` m above it is a real floor under a real roof (a bridge, an arch, a nest):
 * looking up from it must meet that roof. Terrain and many rocks are solid only from above, so under them the look up
 * meets nothing: that floor is underground (a buried mesh, a rock's base). 1 a real floor */
static int Roofed(uint32_t unit, float x, float y, float z, float gap)
{
    float up[3] = {0, 0, 1}, foot[3] = {x, y, z + 0.1f};
    float r = RayWith(unit, foot, up, gap + 0.5f, 0, 0x80000009u, NULL);
    return r >= 0 && r <= gap + 0.5f && fabsf(r - (gap - 0.1f)) < 0.6f;
}
static int g_buried;   /* spots skipped as underground (for the log) */
/* The best spot at a distance along her direction: the highest surface there she fits on; *crowded when one fitted
 * but an enemy stood on it. 1 found */
static int SpotAt(uint32_t unit, const float pos[3], const float dir[2], float dist, float out[3], int* crowded, int upFirst)
{
    float z[16];
    float x = pos[0] + dir[0] * dist, y = pos[1] + dir[1] * dist;
    g_underFoe = 0;
    int n = Surfaces(unit, x, y, pos[2], z, 16);
    if (g_underFoe && crowded) *crowded = 1;
    float high[16];
    memcpy(high, z, sizeof(float) * (n > 0 ? n : 0));   /* highest first */
    if (!upFirst)   /* the surface nearest her own height first, then outwards */
        for (int i = 1; i < n; i++)
            for (int j = i; j > 0 && fabsf(z[j] - pos[2]) < fabsf(z[j - 1] - pos[2]); j--) { float t = z[j]; z[j] = z[j - 1]; z[j - 1] = t; }
    for (int i = 0; i < n; i++)
    {
        float above = 1e9f;   /* the lowest surface over this one */
        for (int j = 0; j < n; j++) if (high[j] > z[i] + 0.3f && high[j] < above) above = high[j];
        if (above < 1e8f && !Roofed(unit, x, y, z[i], above - z[i])) { g_buried++; continue; }
        if (!Fits(unit, x, y, z[i])) { if (g_nFoeUnits && crowded && FoeAbove(unit, (float[3]){x, y, z[i] + 0.1f}, 12.0f)) *crowded = 1; continue; }
        float at[3] = {x, y, z[i]};
        if (Occupied(at)) { if (crowded) *crowded = 1; continue; }
        memcpy(out, at, sizeof at);
        return 1;
    }
    return 0;
}
/* Corrodes every tracked enemy within `radius` m of a point; how many */
static int CorrodeAroundN(const float at[3], float radius, int stacks)
{
    float p[3];
    int n = 0;
    for (int i = 0; i < FOES; i++)
        if (g_foe[i] && FoePos(i, p) && Dist2(p, at) <= radius * radius) { CorrodeN(g_foe[i], stacks); n++; }
    return n;
}
static int CorrodeAround(const float at[3], float radius) { return CorrodeAroundN(at, radius, 1); }
static int GasBlast(const float at[3], const char* what)
{
    float b[3] = {at[0], at[1], at[2] + 0.3f};
    return Blast_((uint32_t)s_acidRow, b, what);
}
static const char* Answer(int r)
{
    return r == HF_ACT_DONE || r == HF_ACT_REQUESTED || r == HF_ACT_QUEUED ? "cloud spawned" : r == HF_ACT_WAITING ? "cloud waiting for its package" : r ? "cloud refused" : "no cloud";
}

/* ---- the abilities ---------------------------------------------------------------------------------------------- */
static void TripleMaul(void)
{
    if (g_now < g_cdMaul) return;
    float at[3];
    uint32_t t = AimTarget(at);
    if (!t) { Log("Triple Maul: no enemy along her aim (within %.0f m) and none hit lately", (double)s_tmRange); return; }
    int br = GasBlast(at, "Triple Maul");
    float me[3];
    float far_ = PlayerPose(me, NULL) ? sqrtf(Dist2(me, at)) : -1;
    int n = CorrodeAround(at, s_tmRadius);
    Corrode(t);
    Mist_(at);
    GainCp(1);
    g_cdMaul = g_now + s_tmCd;
    g_casts[0]++;
    Log("Triple Maul on entity %u, %.0f m away at (%.0f, %.0f, %.0f): %s; %d enem%s gassed and marked (%d Combo Pounce)",
        t, (double)far_, (double)at[0], (double)at[1], (double)at[2], Answer(br), n, n == 1 ? "y" : "ies", g_cp);
}

static void KnifeEaredHunt(void)
{
    if (g_now < g_cdHunt) return;
    g_huntUntil = g_now + s_khSeconds;
    Speed(1);
    g_cdHunt = g_now + s_khCd;
    g_casts[1]++;
    Log("Knife-Eared Hunt for %.0f s: speed x%.2f, endless stamina, damage x%.2f, free Pursuit", (double)s_khSeconds, (double)s_khSpeed, (double)s_khMult);
}

static int g_dashStage;   /* the pending dash: 0 Loyal Dash, 1-3 Pursuit Instinct */
static float SpeedMult(void)
{
    float m = g_now < g_huntUntil ? s_khSpeed : 1;
    if (g_now < g_bonusUntil) m *= 1 + s_pursuitSpeed * (float)g_pursuitStage;
    return m;
}
static void LoyalDash(void)
{
    if (g_leap) return;
    if (g_now < g_pursuitUntil && g_pursuitStage < 3)
    {
        if (g_now >= g_huntUntil && g_cp < 1) { Log("Pursuit Instinct: no Combo Pounce left"); return; }
        g_dashStage = g_pursuitStage + 1;
    }
    else
    {
        if (g_now < g_cdUlt)
        {
            if (g_pursuitUntil > 0 && g_pursuitStage < 3) Log("Pursuit Instinct: its window closed %.1f s ago (Loyal Dash ready in %.0f s)", g_now - g_pursuitUntil, g_cdUlt - g_now);
            g_pursuitUntil = 0;
            return;
        }
        g_dashStage = 0;
    }
    if (!g_setPosition || !g_setVelocity || !g_raycast) { Log("Loyal Dash: the teleport is unavailable on this build (see the start of the log)"); return; }
    float pos[3], aim[3];
    if (!PlayerPose(pos, aim)) { Log("Loyal Dash: her position or aim cannot be read"); return; }
    float h = sqrtf(aim[0] * aim[0] + aim[1] * aim[1]);
    if (h < 0.05f) { Log("Loyal Dash: she is looking straight up or down"); return; }
    g_leapDir[0] = aim[0] / h;
    g_leapDir[1] = aim[1] / h;
    g_leap = 1;
    g_leapT0 = g_now;
}

/* The pending dash: as Lenna's leap (wall check, the ground there, the warp's two calls), then the gas strip */
static void DashFrame(void)
{
    if (!g_leap) return;
    if (!QueryIdle())
    {
        if (g_now - g_leapT0 > 0.5) { g_leap = 0; Log("Loyal Dash: the physics queries stayed busy: no dash"); }
        return;
    }
    g_leap = 0;
    uint8_t id[24];
    uint32_t me = atomic_load(&g_avatar);
    float pos[3];
    if (!Descriptor(me, id) || Quad(id) != AVATAR || !PlayerPose(pos, NULL)) { Log("Loyal Dash: her Helldiver cannot be read"); return; }
    uint32_t unit = Word(id + 12);
    /* where she lands: (1) the spot ld_distance m along her aim, the surface there nearest her height she fits on (no
     * leaves or canopies), not on an enemy
     * (no path check: she blinks); (2) else further on, 1.5 m at a time up to 6 m, and on past that while enemies
     * hold the spots (to 15 m more); (3) else closer, 1.5 m at a time, the highest surface she fits on first */
    g_nFoeUnits = 0;
    g_buried = 0;
    {
        uint8_t fid[24];
        float fp[3], reach = s_ldDistance + 20;
        for (int i = 0; i < FOES && g_nFoeUnits < 256; i++)
        {
            uint32_t e = g_foe[i];
            if (!e || !Descriptor(e, fid) || Quad(fid) != g_foeRes[i] || !UnitNode(Word(fid + 12), 0, fp) || Dist2(fp, pos) > reach * reach) continue;
            g_foeUnits[g_nFoeUnits++] = Word(fid + 12);
        }
    }
    float dest[3] = {0}, dist = s_ldDistance;
    const char* why = "on target";
    int found = 0, crowded = 0;
    found = SpotAt(unit, pos, g_leapDir, dist, dest, &crowded, 0);
    for (float extra = 1.5f; !found && extra <= 15.0f; extra += 1.5f)
    {
        int crowd = 0;
        if (extra > 6.0f && !crowded) break;          /* past a few metres only to clear enemies */
        if (SpotAt(unit, pos, g_leapDir, s_ldDistance + extra, dest, &crowd, 0)) { found = 1; dist = s_ldDistance + extra; why = crowded ? "beyond the enemies" : "beyond: no room there"; }
        crowded |= crowd;
    }
    for (float back = 1.5f; !found && s_ldDistance - back >= 1.5f; back += 1.5f)
        if (SpotAt(unit, pos, g_leapDir, s_ldDistance - back, dest, NULL, 1)) { found = 1; dist = s_ldDistance - back; why = "closer: no room further"; }
    if (!found) { Log("Loyal Dash: no spot she fits within reach (%d underground spots skipped): no dash", g_buried); return; }
    dest[2] += 0.05f;
    float zero[3] = {0, 0, 0};
    g_setPosition(NULL, me, dest);
    g_setVelocity(NULL, me, zero);
    /* the strip she crossed: blasts every ld_step m (not on her landing spot), mist, marks within ld_width m */
    int blasts = 0, n = 0;
    float total = sqrtf(Dist2(pos, dest));
    for (float s = 0; s < total + 0.01f; s += s_ldStep)
    {
        float t = total > 0 ? s / total : 0;
        float b[3] = {pos[0] + (dest[0] - pos[0]) * t, pos[1] + (dest[1] - pos[1]) * t, pos[2] + (dest[2] - pos[2]) * t};
        GasBlast(b, "Loyal Dash");
        Mist_(b);
        blasts++;
    }
    if (fmodf(total, s_ldStep) > 0.75f) { GasBlast(dest, "Loyal Dash"); Mist_(dest); blasts++; }   /* and where she stops */
    float p[3];
    for (int i = 0; i < FOES; i++)
    {
        if (!g_foe[i] || !FoePos(i, p)) continue;
        float v[3] = {p[0] - pos[0], p[1] - pos[1], 0}, d[2] = {dest[0] - pos[0], dest[1] - pos[1]};
        float len2 = d[0] * d[0] + d[1] * d[1];
        float t = len2 > 0 ? (v[0] * d[0] + v[1] * d[1]) / len2 : 0;
        if (t < -0.1f || t > 1.1f) continue;
        float cx = v[0] - d[0] * t, cy = v[1] - d[1] * t;
        if (cx * cx + cy * cy <= s_ldWidth * s_ldWidth && fabsf(p[2] - (pos[2] + (dest[2] - pos[2]) * t)) < 4) { CorrodeN(g_foe[i], g_dashStage + 1); n++; }
    }
    g_drUntil = g_now + s_ldWindow;
    atomic_store(&g_shieldUntilMs, GetTickCount64() + (ULONGLONG)(s_ldWindow * 1000));
    int stage = g_dashStage, free_ = g_now < g_huntUntil;
    if (stage == 0)
    {
        g_cdUlt = g_now + s_ldCd;
        g_casts[2]++;
        g_pursuitStage = 0;
        g_pursuitUntil = g_now + s_pursuitWindow;
    }
    else
    {
        if (!free_) GainCp(-1);
        g_pursuitStage = stage;
        g_pursuitUntil = stage < 3 ? g_now + s_pursuitWindow : 0;
        g_bonus = s_pursuitBonus * (float)stage;
        g_bonusUntil = g_now + s_pursuitHold;
        g_pounces++;
        Speed(1);
    }
    Log("%s: %.1f m (%s), %+.1f m in height, %d underground spots skipped; %d gas blasts, %d enem%s gassed%s%s", stage == 0 ? "Loyal Dash" : stage == 1 ? "Pursuit Instinct I" :
        stage == 2 ? "Pursuit Instinct II" : "Pursuit Instinct III", (double)total, why, (double)(dest[2] - pos[2]), g_buried, blasts, n, n == 1 ? "y" : "ies",
        stage ? "; marked enemies take more, she runs faster" : "; Pursuit open", stage && free_ ? " (free: Knife-Eared Hunt)" : "");
    (void)found;
}

static float g_lastMe[3];   /* where she last stood (her body may be gone when she falls) */
static int g_haveLastMe, g_barkPending, g_dying, g_barked;
static float g_dyingAt;
static uint32_t g_dyingAvatar;
static float g_barkAt[3], g_barkUntil;   /* where her Last Bark went off, and until when its gas hits hard */
static unsigned g_barkHits;
static void LastBark(void)
{
    float pos[3];
    if (!s_barkBlasts) return;
    if (!PlayerPose(pos, NULL))
    {
        if (!g_haveLastMe) { Log("Last Bark: where she fell is unknown: no blasts"); return; }
        memcpy(pos, g_lastMe, sizeof pos);
    }
    for (int k = 0; k < s_barkBlasts; k++)
    {
        float a = 6.2832f * (float)k / (float)s_barkBlasts;
        float b[3] = {pos[0] + s_barkRadius * cosf(a), pos[1] + s_barkRadius * sinf(a), pos[2]};
        GasBlast(b, "Last Bark");
    }
    int n = CorrodeAround(pos, s_barkRadius + 3);
    Mist_(pos);
    memcpy(g_barkAt, pos, sizeof g_barkAt);
    g_barkUntil = g_now + (s_mistSeconds > 8 ? s_mistSeconds : 8);
    g_barkHits = 0;
    g_casts[3]++;
    Log("Last Bark: %d gas blasts where she fell, %d enem%s corroded", s_barkBlasts, n, n == 1 ? "y" : "ies");
}

/* ---- events ------------------------------------------------------------------------------------------------------ */
static int NearPulse(const float at[3], double within)
{
    for (int i = 0; i < 12; i++)
        if (g_pulses[i].at > 0 && g_now - g_pulses[i].at <= within && Dist2(at, g_pulses[i].p) <= 100.0f) return 1;
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
        /* Last Bark under way: her own gas finishes her (no toughness, no immunity) */
        if (g_dying)
        {
            if ((d->kind == 2 || d->kind == 4) && g_barked && Dist2(d->position, g_barkAt) <= (s_barkRadius + 5) * (s_barkRadius + 5)) d->amount = -1;
            return;
        }
        /* a hit that would kill her: blocked, and she barks instead (her gas then takes her) */
        if (s_barkBlasts && g_alive)
        {
            int32_t hp;
            uint32_t life;
            if (HealthOf(me, &hp, &life) && life == 0 && hp > 0)
            {
                float mine4[3];
                float taken = PlayerPose(mine4, NULL) && InMist(mine4) ? s_taken : s_takenOut;
                int own = ((d->kind == 4 || d->kind == 2) && NearPulse(d->position, 1.5)) || (d->kind == 2 && InMist(d->position));
                double a = d->amount < 0 ? 1e9 : d->amount * (double)taken;
                if (!own && (d->amount < 0 || a >= hp))
                {
                    e->cancel = 1;
                    g_dying = 1;
                    g_dyingAt = g_now;
                    g_dyingAvatar = me;
                    g_barkPending = 1;
                    Log("a hit for %d (kind %u) would kill her (%d health): Last Bark", d->amount, d->kind, hp);
                    return;
                }
            }
        }
        /* her own gas spares her: its blasts, and its cloud (damage over time) inside her mist */
        if ((d->kind == 4 || d->kind == 2) && NearPulse(d->position, 1.5)) { e->cancel = 1; g_selfSpared++; return; }
        if (d->kind == 2 && InMist(d->position)) { e->cancel = 1; g_selfSpared++; return; }
        if (d->amount == 0) return;
        if (g_now < g_drUntil && (d->kind == 5 || d->kind == 7) && !NetOk(d->dealer) && !NetOk(d->owner)) { e->cancel = 1; return; }
        /* her toughness: a share of everything, much less while she stands in her own gas; none at 0 (instant kills too) */
        float mine3[3];
        float taken = PlayerPose(mine3, NULL) && InMist(mine3) ? s_taken : s_takenOut;
        if (taken <= 0.0001f) { e->cancel = 1; g_shrugged++; return; }
        if (d->amount > 0 && taken < 0.9999f)
        {
            double a = floor(d->amount * taken + 0.5);
            if (a < 1) { e->cancel = 1; g_shrugged++; return; }
            d->amount = (int32_t)a;
            g_shrugged++;
        }
        return;
    }
    if (d->amount <= 0) return;
    /* Last Bark: her dying gas hits like a hellbomb */
    if (s_barkDamage > 0 && g_now < g_barkUntil && (d->kind == 2 || d->kind == 4) && d->target != me)
    {
        float r = s_barkRadius + 5;
        if (Dist2(d->position, g_barkAt) <= r * r && d->amount < s_barkDamage)
        {
            d->amount = s_barkDamage;
            if (g_barkHits++ == 0) Log("Last Bark: its gas hits for %d", s_barkDamage);
            return;
        }
    }
    /* Corrosion from her gas: every second an enemy spends in her gas (her clouds, or anywhere in her mist) adds a stack
     * and keeps its mark */
    if (d->kind == 2 && d->target != me && (InMist(d->position) || MineHit(d)))
    {
        Mark* cm = MarkOf(d->target);
        if (!cm) { MarkEnemy(d->target); cm = MarkOf(d->target); }
        if (cm)
        {
            cm->until = g_now + s_markSeconds;
            if (g_now - cm->stackAt >= 1.0) { cm->stackAt = g_now; if (cm->stacks < s_corrMax) cm->stacks++; }
        }
    }
    int mine = MineHit(d) && (d->kind == 0 || d->kind == 1 || d->kind == 3);
    double mult = 1;
    if (mine)
    {
        uint8_t tid[24];
        if (Descriptor(d->target, tid) && Quad(tid) != AVATAR) AddFoe(d->target, Quad(tid));
        g_lastTarget = d->target;
        memcpy(g_lastPos, d->position, sizeof g_lastPos);
        g_lastAt = g_now;
        Mark* m = MarkOf(d->target);
        if (m)
        {
            GainHt();
            if (g_now < g_bonusUntil) mult += g_bonus;                     /* Pursuit's escalating bonus */
            float me3[3];
            float per = s_htPer * (PlayerPose(me3, NULL) && InMist(me3) ? 2.0f : 1.0f);
            mult += per * (float)g_ht;                                       /* Hunt Tracking */
        }
        if (g_now < g_huntUntil) mult *= s_khMult;                          /* Knife-Eared Hunt */
        if (mult > 1) g_boosted++;
    }
    Mark* tm = MarkOf(d->target);
    if (tm) mult *= s_markMult;                                             /* Scent Trail: anyone's damage */
    if (tm && tm->stacks > 0 && d->kind == 2) { mult *= 1 + s_corrPer * (float)tm->stacks; g_stackHits++; }   /* Corrosion: gas over time */
    if (mult != 1)
    {
        double a = floor(d->amount * mult + 0.5);
        d->amount = a > 2147483647.0 ? 2147483647 : (int32_t)a;
    }
}

static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfKillEvent* k = (const HfKillEvent*)info;
    DropFoe(k->victim);
    Mark* m = MarkOf(k->victim);
    if (m) { m->until = 0; GainCp(1); }                                    /* kills on her marks refill Combo Pounce */
    if (k->victim == g_lastTarget) g_lastTarget = 0;
    if (k->victim && k->victim == atomic_load(&g_avatar))
    {
        Log("she died (kind %u)%s", k->kind, g_barked ? "" : ": Last Bark where she fell");
        if (!g_barked && g_inMission) { g_dying = 1; g_dyingAvatar = k->victim; g_dyingAt = g_now; g_barkPending = 1; }
    }
}

static void OnPlayer(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    if (info->kind != HF_ON_PLAYER_SPAWN) Log("a player is down (entity %u, %s)", p->entity, p->local ? "hers" : "not hers");
    if (!p->local) return;
    if (info->kind == HF_ON_PLAYER_SPAWN)
    {
        if (p->entity && p->entity != atomic_load(&g_avatar)) { atomic_store(&g_avatar, p->entity); g_avatarNet = 0; }
        g_alive = 1;
        LearnNet();
    }
    else
    {
        if (g_alive && g_inMission) g_barkPending = 1;   /* the blasts go off on the game thread, next frame */
        Log("she is down%s", g_inMission ? "" : " (not in a mission)");
        g_alive = 0;
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
/* Her stamina kept full (Knife-Eared Hunt): the same place, written */
static void StaminaFull(void)
{
    uint32_t me = atomic_load(&g_avatar), ai, named;
    uintptr_t actions = Pointer(game + 0x3326D20), d;
    if (!g_staminaOk || !actions || !HashFind(actions + 0xF8, me, &ai) || ai >= 64 || !(d = Pointer(actions + 0x110 + 8u * ai)) ||
        !ReadAt(d + 8, &named, 4) || named != me) return;
    float one = 1.0f;
    hf->write(actions + 0x547854 + (uintptr_t)ai * 0x78, &one, 4);
}

/* ---- gas immunity: ADVANCED FILTRATION (armor passive 13) in the helmet's passive slot, its gas resistance (passive
 * modifier row 13 x 16 + 0, key 0x6E99CCE5, x0.2) set to 0 for the mission; both undone at its end ----------------- */
static int g_gasOn;
static void GasImmunity(int on)
{
    HfField val = HF_FIELD(PassiveModifier, value), key = HF_FIELD(PassiveModifier, key);
    HfRef row = 13u * 16u + 0u;
    if (!on)
    {
        if (g_gasOn) { hf->set_live(self, row, &val, NULL, HF_LIVE_SHARED); hf->set_passives(self, HF_PASSIVE_KIT, HF_PASSIVE_KIT); Log("gas immunity off"); }
        g_gasOn = 0;
        return;
    }
    if (!s_gasImmune || g_gasOn) return;
    HfLiveValue k;
    memset(&k, 0, sizeof k);
    if (!hf->get_live(self, row, &key, &k) || (uint32_t)k.base.number != 0x6E99CCE5u)
    { Log("gas immunity: ADVANCED FILTRATION's gas resistance is not where it was reviewed (key %.0f): off", k.base.number); return; }
    HfValue zero = HF_F32(0.0f);
    int r = hf->set_live(self, row, &val, &zero, HF_LIVE_SHARED);
    int p = hf->set_passives(self, HF_PASSIVE_KIT, 13);
    HfPassives st;
    memset(&st, 0, sizeof st);
    hf->get_passives(self, &st);
    g_gasOn = 1;
    Log("gas immunity: ADVANCED FILTRATION in the helmet's slot %s (%s), its gas resistance x0 (applies: %d)", p ? "held" : "refused", st.status, r);
}

static void Reset(const char* why)
{
    g_haveLastMe = 0;
    g_barkPending = 0;
    g_dying = g_barked = 0;
    g_barkUntil = 0;
    if (g_casts[0] || g_casts[1] || g_casts[2])
        Log("%s: Triple Maul %u, Knife-Eared Hunt %u, Loyal Dash %u, Last Bark %u; %u Pursuit dashes, %u marked, %u enemies gassed, %u boosted hits, %u stacked gas ticks, %u hits on her reduced, %u own blasts spared her, %u knockdowns stopped, %u hits pierced more armour",
            why, g_casts[0], g_casts[1], g_casts[2], g_casts[3], g_pounces, g_marked, g_acidApplied, g_boosted, g_stackHits, g_shrugged, g_selfSpared, g_knockdowns, atomic_exchange(&g_pierced, 0));
    Speed(0);
    GasImmunity(0);
    if (!strcmp(why, "mission start")) GasImmunity(1);
    if (!strcmp(why, "mission start") && s_hellbomb && s_hellbombRow == 125 && !hf->load_package(self, UINT64_C(0x346D785D6CA13FF4)))
        Log("the Hellbomb's package could not be loaded ahead");   /* loadout/hellbomb_backpack */
    g_boomPending = 0;
    Body(0);
    if (!strcmp(why, "mission start")) Body(1);
    if (!strcmp(why, "mission start") && s_acidRow == 177 && !hf->load_package(self, UINT64_C(0x25A04F6D9707D2CC)))
        Log("the G-4 Gas package could not be loaded ahead");   /* its clouds then wait a moment the first time */
    memset(g_marks, 0, sizeof g_marks);
    memset(g_mist, 0, sizeof g_mist);
    memset(g_pulses, 0, sizeof g_pulses);
    memset(g_casts, 0, sizeof g_casts);
    g_marked = g_pounces = g_boosted = g_stackHits = g_shrugged = g_selfSpared = g_knockdowns = g_acidApplied = 0;
    g_cp = s_cpMax;
    g_ht = 0;
    g_cdMaul = g_cdHunt = g_cdUlt = 0;
    g_lastTarget = 0;
    g_pursuitStage = 0;
    g_pursuitUntil = g_bonusUntil = g_huntUntil = g_drUntil = 0;
    g_retryCount = 0;
    g_leap = 0;
    atomic_store(&g_shieldUntilMs, 0);
}

/* ---- the knockdown window: hooks on the game's hit code (as Primed Sure Footed) -------------------------------------- */
/* ---- Corrosion's armor shred: a hit on a corroded enemy pierces more armour. The hit (as its calculation gets it)
 * carries its armour penetration at +0x9C, +0xA0, +0xA4 (floats, by impact angle; +0xA8 the extreme angle), found with
 * a probe run (Liberator AP 2, a medium gun AP 3, gas AP 6); the result's +4 is the armour's share (1, 0.65 at equal
 * armour, 0 deflected). It is raised for the calculation only, then put back. ----------------------------------------- */
static float Shred(const void* hit, float saved[3])
{
    uint32_t me = atomic_load(&g_avatar);
    uint8_t h[0xA8];
    if (!hit || !ReadAt((uintptr_t)hit, h, sizeof h)) return 0;
    uint32_t target = Word(h + 0x20);
    if (!target || target == me) return 0;
    Mark* m = MarkOf(target);
    if (!m || m->stacks <= 0) return 0;
    float bonus = floorf((float)m->stacks * s_shredPer + 0.5f);
    if (bonus > s_shredMax) bonus = s_shredMax;
    if (bonus <= 0) return 0;
    memcpy(saved, h + 0x9C, 12);
    for (int k = 0; k < 3; k++) if (!(saved[k] >= 0 && saved[k] <= 12)) return 0;   /* not the reviewed layout */
    float up[3];
    for (int k = 0; k < 3; k++) up[k] = saved[k] + bonus;
    memcpy((uint8_t*)hit + 0x9C, up, 12);
    atomic_fetch_add(&g_pierced, 1);
    return bonus;
}

static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gameSize = 0, exeSize = 0;
    uintptr_t g = hf->module_base("game.dll", &gameSize), x = hf->module_base("helldivers2.exe", &exeSize);
    if (!g || !x || gameSize != 74727424u || exeSize != 60719104u) { Log("REFUSED: the game's modules are not build " BUILD "'s: no aim, no knockdown guard"); return; }
    game = g;
    exe = x;
    if (!HitsInstall("knockdown guard")) return;
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

static void EntitiesReady(HfMod* mod, HfEvent event, void* user) {}

/* ---- the HUD state (read by the Faelynn HUD addon through LuaJIT's ffi) ------------------------------------------ */
typedef struct FaelynnHud
{
    uint32_t version, in_mission, cp, cp_max, ht, ht_max;
    float cd[3], cd_max[3];          /* seconds left, full cooldowns: Triple Maul, Knife-Eared Hunt, Loyal Dash */
    float pursuit, hunt, bonus_left, bonus;   /* seconds left in Pursuit's window, Knife-Eared Hunt, the pounce bonus; the bonus */
    uint32_t pursuit_stage, marked, in_mist, keys[3];
    char key_label[3][24];
    float stamina;                   /* version 2: her stamina 0..1 (-1: unknown) */
    uint32_t hellbomb;               /* version 2: 1 when running dry sets off the Hellbomb */
} FaelynnHud;
static FaelynnHud g_hud = {2};
HF_MOD_EXPORT FaelynnHud* faelynn_hud_state(void)
{
    g_hud.in_mission = (uint32_t)(g_inMission && g_alive);
    g_hud.cp = (uint32_t)g_cp;
    g_hud.cp_max = (uint32_t)s_cpMax;
    g_hud.ht = (uint32_t)g_ht;
    g_hud.ht_max = (uint32_t)s_htMax;
    double r[3] = {g_cdMaul, g_cdHunt, g_cdUlt};
    float m[3] = {s_tmCd, s_khCd, s_ldCd};
    for (int i = 0; i < 3; i++) { g_hud.cd[i] = (float)fmax(0, r[i] - g_now); g_hud.cd_max[i] = m[i]; }
    g_hud.pursuit = g_pursuitStage < 3 ? (float)fmax(0, g_pursuitUntil - g_now) : 0;
    g_hud.pursuit_stage = (uint32_t)g_pursuitStage;
    g_hud.hunt = (float)fmax(0, g_huntUntil - g_now);
    g_hud.bonus_left = (float)fmax(0, g_bonusUntil - g_now);
    g_hud.bonus = g_bonus;
    unsigned n = 0;
    for (int i = 0; i < 64; i++) n += g_marks[i].until > g_now;
    g_hud.marked = n;
    float p[3];
    g_hud.in_mist = (uint32_t)(g_inMission && PlayerPose(p, NULL) && InMist(p));
    for (int i = 0; i < 3; i++)
    {
        g_hud.keys[i] = (uint32_t)s_keys[i].vk;
        memcpy(g_hud.key_label[i], s_keys[i].label, sizeof g_hud.key_label[i]);
    }
    g_hud.stamina = g_inMission && g_alive ? StaminaNow() : -1;
    g_hud.hellbomb = (uint32_t)(s_hellbomb != 0);
    return &g_hud;
}

/* ---- in-game key bindings: the HUD registers her three skills with Mod Bindings Menu (Vanilla Plus Megapack: the MODS
 * tab of the binding pages) and passes each press here (and a heartbeat twice a second); the .ini's keys keep working */
static _Atomic unsigned long long g_bindMs;
static atomic_int g_bindPress;
HF_MOD_EXPORT int faelynn_input(unsigned version, int skill)
{
    if (version != 1) return 0;
    atomic_store(&g_bindMs, GetTickCount64());
    if (skill >= 1 && skill <= 3) atomic_fetch_or(&g_bindPress, 1 << (skill - 1));
    return 1;
}
/* A skill's .ini key for the binding's default: its virtual-key code and modifiers (1 Shift, 2 Ctrl, 4 Alt) */
HF_MOD_EXPORT int faelynn_key(unsigned version, int skill, int* vk, int* mods)
{
    if (version != 1 || skill < 1 || skill > 3 || !vk || !mods) return 0;
    *vk = s_keys[skill - 1].vk;
    *mods = s_keys[skill - 1].mods;
    return 1;
}
static void Tick(HfMod* mod, float dt, void* user)
{
    g_now += dt > 0 && dt < 1 ? dt : 0;
    int inMission = hf->game_state(self) == HF_STATE_MISSION;
    if (inMission != g_inMission) { Reset(inMission ? "mission start" : "mission end"); g_inMission = inMission; }
    if (!g_inMission) return;
    MyItems();
    static int prev[3];
    int focused = Focused();
    int mods = focused ? ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? KM_SHIFT : 0) | ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? KM_CTRL : 0) |
                         ((GetAsyncKeyState(VK_MENU) & 0x8000) ? KM_ALT : 0) : 0;
    /* the keys from Mod Bindings Menu while the HUD passes them, else the .ini's */
    int bound = GetTickCount64() - atomic_load(&g_bindMs) < 2000, presses = atomic_exchange(&g_bindPress, 0);
    static int wasBound = -1;
    if (bound != wasBound) { wasBound = bound; Log("keys: %s", bound ? "the .ini's and Mod Bindings Menu's (the binding pages' MODS tab)" : "the .ini's"); }
    for (int i = 0; i < 3; i++)
    {
        int own = s_keys[i].vk == VK_SHIFT ? KM_SHIFT : s_keys[i].vk == VK_CONTROL ? KM_CTRL : s_keys[i].vk == VK_MENU ? KM_ALT : 0;
        /* both the .ini's key and the binding fire it; one press within 0.3 s of the other is the same press */
        int down = focused && (GetAsyncKeyState(s_keys[i].vk) & 0x8000) != 0 && (mods & ~own) == s_keys[i].mods;
        static unsigned long long fired[3];
        int press = (down && !prev[i]) || (presses >> i & 1);
        if (press && GetTickCount64() - fired[i] > 300 && g_alive && atomic_load(&g_avatar) && (fired[i] = GetTickCount64(), 1))
        {
            if (i == 0) TripleMaul();
            else if (i == 1) KnifeEaredHunt();
            else LoyalDash();
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
    if (g_barkPending)
    {
        g_barkPending = 0;
        if (g_dying) GasImmunity(0);   /* she breathes her own last gas */
        if (!g_barked) { g_barked = 1; LastBark(); }
    }
    if (g_dying)
    {
        uint32_t av = atomic_load(&g_avatar);
        int32_t hp;
        uint32_t life;
        if (av != g_dyingAvatar || !HealthOf(g_dyingAvatar, &hp, &life) || hp <= 0 || life != 0)
        {
            if (av != g_dyingAvatar && av) { g_dying = 0; g_barked = 0; GasImmunity(1); Log("she is back: gas immunity again"); }
        }
        else if (g_now - g_dyingAt > 3.0f && g_now - g_dyingAt < 3.5f)   /* the gas did not take her: the game's own wound does */
        {
            hf->injure(self, HF_LIMB_CHEST, 60);
            hf->injure(self, HF_LIMB_HEAD, 85);
            g_dyingAt -= 1.0f;
            Log("Last Bark: her gas did not finish her; a fatal wound instead");
        }
    }
    else if (g_alive) { float me[3]; if (PlayerPose(me, NULL)) { memcpy(g_lastMe, me, sizeof me); g_haveLastMe = 1; } }
    Exhaustion();
    DashFrame();
    if (g_now < g_huntUntil) StaminaFull();
    {
        float m = SpeedMult();
        if (m <= 1.001f) { if (g_speedOn) { Speed(0); Log("speed back to normal"); } }
        else if (fabsf(m - g_speedNow) > 0.001f) Speed(1);
    }
    if (g_ht && g_now - g_htAt > s_htSeconds) g_ht = 0;
    unsigned own = atomic_exchange(&g_ownStopped, 0);
    if (own) { g_selfSpared += own; static unsigned noted; if (noted++ < 5) Log("Determined Counterpounce: %u hit(s) of her own gas cleared on her", own); }
    unsigned kd = atomic_exchange(&g_kdStopped, 0);
    if (kd) { g_knockdowns += kd; static unsigned noted2; if (noted2++ < 20) Log("Determined Counterpounce: %u knockdown(s) stopped", kd); }
}

/* ---- the Mod Options Menu (through the Faelynn HUD addon): read and set a setting by its faelynn.ini name --------- */
static const struct { const char* name; float* f; int* i; } kOptions[] = {
    {"cp_max", NULL, &s_cpMax}, {"ht_max", NULL, &s_htMax}, {"ht_per_stack", &s_htPer, NULL},
    {"tm_cooldown", &s_tmCd, NULL}, {"tm_radius", &s_tmRadius, NULL}, {"tm_range", &s_tmRange, NULL},
    {"pursuit_window", &s_pursuitWindow, NULL}, {"pursuit_bonus", &s_pursuitBonus, NULL}, {"pursuit_hold", &s_pursuitHold, NULL}, {"pursuit_speed", &s_pursuitSpeed, NULL},
    {"kh_cooldown", &s_khCd, NULL}, {"kh_seconds", &s_khSeconds, NULL}, {"kh_speed", &s_khSpeed, NULL}, {"kh_mult", &s_khMult, NULL},
    {"ld_cooldown", &s_ldCd, NULL}, {"ld_distance", &s_ldDistance, NULL}, {"ld_width", &s_ldWidth, NULL},
    {"mark_mult", &s_markMult, NULL}, {"mark_seconds", &s_markSeconds, NULL}, {"mist_seconds", &s_mistSeconds, NULL},
    {"steady", NULL, &s_steady}, {"bark_blasts", NULL, &s_barkBlasts}, {"bark_damage", NULL, &s_barkDamage}, {"gas_immune", NULL, &s_gasImmune},
    {"corrosion_per_stack", &s_corrPer, NULL}, {"shred_per_stack", &s_shredPer, NULL}, {"shred_max", &s_shredMax, NULL}, {"hp_mult", &s_hpMult, NULL}, {"stamina_mult", &s_staminaMult, NULL}, {"hellbomb_on_exhaustion", NULL, &s_hellbomb}, {"corrosion_max", NULL, &s_corrMax}, {"damage_taken", &s_taken, NULL}, {"damage_taken_outside", &s_takenOut, NULL},
};
HF_MOD_EXPORT int faelynn_option_get(const char* name, double* out)
{
    if (!name || !out) return 0;
    for (unsigned k = 0; k < sizeof kOptions / sizeof kOptions[0]; k++)
        if (!strcmp(kOptions[k].name, name)) { *out = kOptions[k].f ? (double)*kOptions[k].f : (double)*kOptions[k].i; return 1; }
    return 0;
}
HF_MOD_EXPORT int faelynn_option_set(const char* name, double value)
{
    double before;
    if (!isfinite(value) || !faelynn_option_get(name, &before)) return 0;
    Setting(name, value);
    double now;
    faelynn_option_get(name, &now);
    if (now != before)
    {
        Log("option %s: %g -> %g (Mod Options Menu)", name, before, now);
        if (!strcmp(name, "gas_immune") && g_inMission) GasImmunity(s_gasImmune);
        if (g_inMission && (!strcmp(name, "hp_mult") || !strcmp(name, "stamina_mult"))) { Body(0); Body(1); }
    }
    if (g_cp > s_cpMax) g_cp = s_cpMax;
    if (g_ht > s_htMax) g_ht = s_htMax;
    return 1;
}

/* Last Bark: her Hellbomb waits while it has her */
static int HellbombVeto(void) { return g_dying; }

/* Her hits, before the game reads them: the Shred's armour penetration (put back after) */
static void HitBefore(KbHitCall* c) { c->restore = Shred(c->hit, c->saved) > 0; }

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    kb_hit_before = HitBefore;
    kb_hellbomb_veto = HellbombVeto;
    if (!api || api->version < 13 || api->size < offsetof(HfApi, code_matches) + sizeof api->code_matches) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    LoadSettings();
    api->log(mod, "Faelynn %s: keys %s / %s / %s (Triple Maul and Pursuit, Knife-Eared Hunt, Loyal Dash); Combo Pounce up to %d", VERSION,
        s_keys[0].label, s_keys[1].label, s_keys[2].label, s_cpMax);
    if (s_badKey) api->log(mod, "faelynn.ini: a key could not be read (bits %d: maul, hunt, ult); its default stays", s_badKey);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL) || api->on(mod, HF_EVENT_ENTITIES_READY, EntitiesReady, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) &&
             api->on_game_thread(mod, Tick, NULL);
    if (!ok) api->log(mod, "events or the game thread are unavailable: Faelynn stays off");
    return 0;
}
