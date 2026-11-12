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
 *   - The user's rule: with a backpack on, or a support weapon in her hands, there is no functional hiding at all.
 *   - Ambusher: while hidden, half the hits that still land on her miss (dodge).
 * Her skills (Pursuit, Endowment, Descent, Mortal Interrogation) are not in this build.
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

#define VERSION "0.1.1"
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
static float s_visualCut = 95, s_revealHold = 1.5f;
static float s_dodge = 0.5f, s_forgetEvery = 0.5f, s_forgetRange = 150;
static int s_cloakVisual = 1, s_hideGear = 1, s_gearRule = 1;

static const KbSetting kSettings[] = {
    KB_F("visual_cut", s_visualCut, 0, 99.9), KB_F("reveal_hold", s_revealHold, 0, 60), KB_F("dodge", s_dodge, 0, 1),
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
               "; Hidden: while she makes no noise, enemies see her visual_cut percent less far, and whoever within forget_range m\n"
               "; still targets her forgets her every forget_every s.\n"
               "visual_cut = %g\nforget_every = %g\nforget_range = %g\n"
               "; What reveals her is the game's own hearing and sight: an enemy that senses her right now (it heard her gun, or\n"
               "; she is right in front of it) keeps her; she counts as revealed while any does, and reveal_hold s after.\n"
               "reveal_hold = %g\n"
               "; gear_rule 1: with a backpack on or a support weapon in her hands she is not hidden at all (she still looks it).\n"
               "gear_rule = %d\n"
               "; Ambusher: while hidden, this share of the hits that still land on her miss (0.5: half).\ndodge = %g\n"
               "; cloak_visual 1: her body is not drawn while hidden (needs Bingus Shared Loader and HD2Runtime); hide_gear 1: nor\n"
               "; are her weapons and backpack.\ncloak_visual = %d\nhide_gear = %d\n"
               "; Her body: hp_mult times a Helldiver's health, stamina_mult times her sprint.\n"
               "hp_mult = 1\nstamina_mult = 1.2\nhellbomb_on_exhaustion = 0\n",
            VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, (double)s_visualCut, (double)s_forgetEvery, (double)s_forgetRange,
            (double)s_revealHold, s_gearRule, (double)s_dodge, s_cloakVisual,
            s_hideGear);
}

/* ---- state ---------------------------------------------------------------------------------------------------- */
enum { HIDDEN = 0, QUIET = 1 /* unused since 0.1.1 */, REVEALED = 2, GEAR = 3, AWAY = 4 };   /* AWAY: not on the ground */
static atomic_int g_state = AWAY;
static double g_sensedUntil, g_forgetAt;
static int g_sensing;   /* enemies that sense her, at the last pass */
static uint32_t g_heldSel;
static uint64_t g_heldRes;
static unsigned g_dodged, g_forgot, g_reveals;
static const char* kStateName[] = {"hidden", "-", "revealed (an enemy senses her)", "not hidden (backpack or support weapon)", "away"};

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
static void DamageDealt(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me) { (void)e; (void)d; (void)me; }

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
        Log("%s: revealed %u times, %u times an enemy forgot her, %u hits dodged", why, g_reveals, g_forgot, g_dodged);
    int start = !strcmp(why, "mission start");
    if (start) KitDecide();
    Body(start);
    g_boomPending = 0; g_boomArmed = 0;
    g_sensedUntil = g_forgetAt = 0;
    g_sensing = 0;
    g_forgot = g_reveals = g_dodged = 0;
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
} AscalonHud;
static AscalonHud g_hud = {1};
static uint32_t UnitOf(uint32_t e)
{
    uint8_t d[24];
    return e && e != UINT32_MAX && Descriptor(e, d) ? Word(d + 12) : 0;
}
HF_MOD_EXPORT AscalonHud* ascalon_hud_state(void)
{
    int st = atomic_load(&g_state);
    g_hud.in_mission = (uint32_t)(g_inMission && g_alive && kb_kit_on);
    g_hud.state = (uint32_t)st;
    g_hud.reveal_left = st == REVEALED && !g_sensing ? (float)fmax(0, g_sensedUntil - g_now) : 0;
    g_hud.sensing = (uint32_t)g_sensing;
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
    g_hud.units[0] = UnitOf(atomic_load(&g_avatar));
    for (int k = 0; k < 4; k++) g_hud.units[1 + k] = UnitOf(atomic_load(&g_myItems[k]));
    Exhaustion();
    atomic_exchange(&g_ownStopped, 0);
    atomic_exchange(&g_kdStopped, 0);
}

/* ---- Mod Options Menu ------------------------------------------------------------------------------------------- */
static const KbOption kOptions[] = {
    {"visual_cut", &s_visualCut, NULL}, {"reveal_hold", &s_revealHold, NULL}, {"dodge", &s_dodge, NULL},
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
    api->log(mod, "Ascalon %s: hidden until she makes noise (her skills are not in this build)", VERSION);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) &&
             api->subscribe(mod, HF_ON_WEAPON, OnWeaponAsc, NULL, 0) && api->on_game_thread(mod, Tick, NULL);
    if (!ok) api->log(mod, "events or the game thread are unavailable: Ascalon stays off");
    return 0;
}
