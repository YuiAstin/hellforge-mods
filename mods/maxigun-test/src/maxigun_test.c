/* Maxigun Test (yui.maxigun-test): research for Veterancy System's M-1000 Maxigun levels. Not for release.
 *
 * At game load it logs the Maxigun's vanilla values (fire rate slots 1-3, spin-up and spin-down, stand-still-while-
 * firing, recoil, backpack capacity / start / refill) and, for comparison, the MG-43's and Stalwart's rate slots (both
 * offer a rate choice in game: slot 2 is the default, slots 1 and 3 the choices). With apply = 1 it then sets the test values from maxigun-test.ini as live edits; they
 * reach a Maxigun called in after that.
 *
 * In a mission it logs:
 *   - each stratagem thrown (its type: the Maxigun's is 110), to tie the call-in to what follows;
 *   - the first stat source of every kind seen (shots, hits, kills, damage) with its resource: whether the Maxigun's
 *     kills are credited to the gun (43A58CB89CFA197C), its backpack (056DE1C5E21E723E) or the Helldiver;
 *   - the Maxigun's real fire rate: its shots counted in 1-second windows while firing, as each burst's peak rpm;
 *   - its shots and kills for the mission at the mission's end.
 *
 * Each set value is logged with what the record then holds; whether the game really uses it (above the editor's limits: rates over
 * 3000, a backpack over 1023) shows in play: the measured rpm and the backpack count on screen.
 *
 * 0.4.0: when the test values go on, the Maxigun's call-in cooldown is refunded (refund_on_apply) by writing the local
 * player's stratagem record (see "the call-in refund" below), and that record is logged every 30 s and after each
 * Maxigun call.
 * 0.3.0: the test values go on apply_delay seconds into each mission (default 60: call in a Maxigun before then and
 * hold it) and come off at its end, and each change logs when HellForge says it applies: does a Maxigun already out
 * take them (rate menu, walking, the bigger pack), or only the next one called in?
 * 0.2.0: widens the backpack's network ammo count past 1023 (pack_bits) and copies the MG-43's fire modes and weapon
 * functions onto the Maxigun (copy_mg43_rate_menu), to see whether its rate choice appears.
 *
 * Fields from HellForge's mod SDK (game build 25480438); the MG-43's and Stalwart's resources from its editor catalogue. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <windows.h>
#include "hellforge_mod.h"
#include "hellforge_components.h"
#include "hellforge_content.h"

#define VERSION "0.4.0"
#define GUN HF_ENTITY_M1000_MAXIGUN           /* the weapon */
#define PACK HF_ENTITY_MAXIGUN_BACKPACK
#define STALWART 0xA6A735ACCB4A327Full     /* M-105 Stalwart (editor catalogue) */
#define MG43 0x11C27D3BABB38956ull         /* MG-43 Machine Gun (editor catalogue) */

static const HfApi* g_hf;
static HfMod* g_mod;

/* settings */
static int s_apply = 1, s_stationary = 0;
static float s_rate[3] = {2000, 4500, 6000};
static float s_windUp = 0.2f, s_windDown = 1.5f, s_recoil = 0.6f;
static int s_packCap = 6000, s_packStart = 6000, s_packRefill = 3000;
static int s_widen = 14, s_copyMenu = 1;
static int s_refund = 1;  /* refund the Maxigun's call-in when the test values go on */
static float s_delay = 60;  /* seconds into a mission before the test values go on (-1: at game load) */

/* state */
static double g_clock, g_missionAt, g_dumpAt = -1;
static int g_inMission;
static long long g_shots, g_kills;
static double g_windowStart = -1, g_lastShot = -100;
static int g_windowShots, g_burstPeak;
static long long g_burstShots;
static int g_applied;

typedef struct Field { const char* label; HfField field; } Field;
static const Field kRate[3] = {
    {"fire rate slot 1", HF_FIELD_AT(ProjectileWeapon, fire_rate, 0)},
    {"fire rate slot 2 (the default)", HF_FIELD_AT(ProjectileWeapon, fire_rate, 1)},
    {"fire rate slot 3", HF_FIELD_AT(ProjectileWeapon, fire_rate, 2)}};
static const Field kWindUp = {"spin-up s", HF_FIELD(WeaponWindUp, wind_up_seconds)};
static const Field kWindDown = {"spin-down s", HF_FIELD(WeaponWindUp, wind_down_seconds)};
static const Field kStationary = {"stationary while firing", HF_FIELD(WeaponData, stationary_while_firing)};
static const Field kRecoil[6] = {
    {"recoil drift horizontal", HF_FIELD(WeaponData, recoil_drift_horizontal)},
    {"recoil drift vertical", HF_FIELD(WeaponData, recoil_drift_vertical)},
    {"recoil climb horizontal", HF_FIELD(WeaponData, recoil_climb_horizontal)},
    {"recoil climb vertical", HF_FIELD(WeaponData, recoil_climb_vertical)},
    {"recoil multiplier horizontal", HF_FIELD(WeaponData, recoil_multiplier_horizontal)},
    {"recoil multiplier vertical", HF_FIELD(WeaponData, recoil_multiplier_vertical)}};
static const Field kModes[5] = {
    {"fire mode 1 (default)", HF_FIELD_AT(WeaponData, fire_modes, 0)},
    {"fire mode 2", HF_FIELD_AT(WeaponData, fire_modes, 1)},
    {"fire mode 3", HF_FIELD_AT(WeaponData, fire_modes, 2)},
    {"weapon function left", HF_FIELD(WeaponData, weapon_function_left)},
    {"weapon function right", HF_FIELD(WeaponData, weapon_function_right)}};
static const Field kPack[3] = {
    {"backpack capacity", HF_FIELD(Deposit, capacity)},
    {"backpack starting rounds", HF_FIELD(Deposit, start_amount)},
    {"backpack rounds from supply", HF_FIELD(Deposit, refill_amount)}};

/* what the record holds now (now = 1) or the game's own value (now = 0); NAN when it cannot be read */
static double Get(uint64_t ref, const Field* f, int now)
{
    HfLiveValue live;
    memset(&live, 0, sizeof live);
    if (!g_hf->get_live(g_mod, ref, &f->field, &live)) return NAN;
    return now ? live.now.number : live.base.number;
}
static void Log(uint64_t ref, const char* who, const Field* f)
{
    double v = Get(ref, f, 0);
    if (isnan(v)) g_hf->log(g_mod, "%s %s: not readable (%s)", who, f->label, g_hf->last_log ? g_hf->last_log(g_mod) : "");
    else g_hf->log(g_mod, "%s %s: %g", who, f->label, v);
}
static void Set(uint64_t ref, const char* who, const Field* f, double x)
{
    HfValue value = {f->field.type, x, 0};
    double before = Get(ref, f, 0);
    int when = g_hf->set_live(g_mod, ref, &f->field, &value, 0);
    if (!when)
    {
        g_hf->log(g_mod, "%s %s: %g refused (%s)", who, f->label, x, g_hf->last_log ? g_hf->last_log(g_mod) : "");
        return;
    }
    g_hf->log(g_mod, "%s %s: %g -> %g asked, the record holds %g (HellForge: applies %s)", who, f->label, before, x, Get(ref, f, 1),
        when == HF_APPLIES_NOW ? "now" : when == HF_APPLIES_RESPAWN ? "on respawn / next call-in" : when == HF_APPLIES_MISSION ? "next mission" : "?");
}

/* ---- settings ---------------------------------------------------------------------------------------------- */
static FILE* OpenUtf8(const char* path, const wchar_t* mode)
{
    wchar_t w[MAX_PATH * 2];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, MAX_PATH * 2)) return NULL;
    return _wfopen(w, mode);
}
static void LoadSettings(void)
{
    char path[MAX_PATH * 2], line[512];
    snprintf(path, sizeof path, "%s\\maxigun-test.ini", g_hf->mod_folder(g_mod));
    FILE* f = OpenUtf8(path, L"r");
    if (!f) { g_hf->log(g_mod, "settings: no maxigun-test.ini in %s, so the built-in test values", g_hf->mod_folder(g_mod)); return; }
    g_hf->log(g_mod, "settings: read %s", path);
    while (fgets(line, sizeof line, f))
    {
        char* eq = strchr(line, '=');
        if (line[0] == ';' || !eq) continue;
        *eq = 0;
        for (char* e = eq - 1; e >= line && (*e == ' ' || *e == '\t'); e--) *e = 0;
        double v = strtod(eq + 1, NULL);
        if (!isfinite(v)) continue;
        if (!strcmp(line, "apply")) s_apply = v != 0;
        else if (!strcmp(line, "rate_1")) s_rate[0] = (float)v;
        else if (!strcmp(line, "rate_2")) s_rate[1] = (float)v;
        else if (!strcmp(line, "rate_3")) s_rate[2] = (float)v;
        else if (!strcmp(line, "wind_up")) s_windUp = (float)v;
        else if (!strcmp(line, "wind_down")) s_windDown = (float)v;
        else if (!strcmp(line, "stationary_while_firing")) s_stationary = (int)v;
        else if (!strcmp(line, "recoil_mult")) s_recoil = (float)v;
        else if (!strcmp(line, "pack_capacity")) s_packCap = (int)v;
        else if (!strcmp(line, "pack_start")) s_packStart = (int)v;
        else if (!strcmp(line, "pack_refill")) s_packRefill = (int)v;
        else if (!strcmp(line, "pack_bits")) s_widen = (int)v;
        else if (!strcmp(line, "copy_mg43_rate_menu")) s_copyMenu = v != 0;
        else if (!strcmp(line, "apply_delay")) s_delay = (float)v;
        else if (!strcmp(line, "refund_on_apply")) s_refund = v != 0;
    }
    fclose(f);
}

/* ---- vanilla values and the test edits ---------------------------------------------------------------------- */
static void ApplyAll(const char* when);
static int Pins(void);
static double g_maxigunAt = -1;  /* when this mission's last Maxigun was called (its beacon) */

/* Every test edit off, so the next mission tests a mid-mission change again (the widened ammo count stays). */
static void UndoAll(void)
{
    if (!g_applied) return;
    g_applied = 0;
    const Field* all[] = {&kRate[0], &kRate[1], &kRate[2], &kWindUp, &kWindDown, &kStationary, &kRecoil[0], &kRecoil[1], &kRecoil[2],
        &kRecoil[3], &kRecoil[4], &kRecoil[5], &kModes[0], &kModes[1], &kModes[2], &kModes[3], &kModes[4]};
    for (unsigned i = 0; i < sizeof all / sizeof all[0]; i++) g_hf->set_live(g_mod, GUN, &all[i]->field, NULL, 0);
    for (int i = 0; i < 3; i++) g_hf->set_live(g_mod, PACK, &kPack[i].field, NULL, 0);
    g_hf->log(g_mod, "---- test values off ----");
}

static void EntitiesReady(HfMod* mod, HfEvent event, void* user)
{
    g_hf->log(g_mod, "---- vanilla values ----");
    for (int i = 0; i < 3; i++) Log(GUN, "Maxigun", &kRate[i]);
    Log(GUN, "Maxigun", &kWindUp);
    Log(GUN, "Maxigun", &kWindDown);
    Log(GUN, "Maxigun", &kStationary);
    for (int i = 0; i < 6; i++) Log(GUN, "Maxigun", &kRecoil[i]);
    for (int i = 0; i < 3; i++) Log(PACK, "Maxigun", &kPack[i]);
    for (int i = 0; i < 3; i++) Log(MG43, "MG-43", &kRate[i]);
    for (int i = 0; i < 3; i++) Log(STALWART, "Stalwart", &kRate[i]);
    Log(MG43, "MG-43", &kStationary);
    {
        static const Field cdType = {"stratagem cooldown type (0 individual)", HF_FIELD(StratagemSettings, cooldown_type)};
        static const Field cd = {"stratagem cooldown s", HF_FIELD(StratagemSettings, cooldown)};
        Log(HF_STRATAGEM_M1000_MAXIGUN, "Maxigun", &cdType);
        Log(HF_STRATAGEM_M1000_MAXIGUN, "Maxigun", &cd);
    }
    Pins();
    for (int i = 0; i < 5; i++) Log(GUN, "Maxigun", &kModes[i]);
    for (int i = 0; i < 5; i++) Log(MG43, "MG-43", &kModes[i]);
    for (int i = 0; i < 5; i++) Log(STALWART, "Stalwart", &kModes[i]);
    if (!s_apply) { g_hf->log(g_mod, "apply = 0: nothing changed"); return; }
    if (s_delay < 0) ApplyAll("at game load");
    else g_hf->log(g_mod, "the test values go on %.0f s into each mission and come off at its end", (double)s_delay);
}

static void ApplyAll(const char* when)
{
    if (g_applied) return;
    g_applied = 1;
    g_hf->log(g_mod, "---- test values on (%s)%s ----", when,
        g_maxigunAt > 0 ? ": you called in a Maxigun before this, so check the one you hold" : "");
    for (int i = 0; i < 3; i++) if (s_rate[i] > 0) Set(GUN, "Maxigun", &kRate[i], s_rate[i]);
    if (s_windUp >= 0) Set(GUN, "Maxigun", &kWindUp, s_windUp);
    if (s_windDown >= 0) Set(GUN, "Maxigun", &kWindDown, s_windDown);
    if (s_stationary >= 0) Set(GUN, "Maxigun", &kStationary, s_stationary ? 1 : 0);
    if (s_recoil != 1.0f)
        for (int i = 0; i < 6; i++)
        {
            double base = Get(GUN, &kRecoil[i], 0);
            if (!isnan(base)) Set(GUN, "Maxigun", &kRecoil[i], base * s_recoil);
        }
    if (s_copyMenu)
        for (int i = 0; i < 5; i++)
        {
            double v = Get(MG43, &kModes[i], 0);
            if (!isnan(v)) Set(GUN, "Maxigun (the MG-43's)", &kModes[i], v);
        }
    if (s_packCap > 0) Set(PACK, "Maxigun", &kPack[0], s_packCap);
    if (s_packStart > 0) Set(PACK, "Maxigun", &kPack[1], s_packStart);
    if (s_packRefill > 0) Set(PACK, "Maxigun", &kPack[2], s_packRefill);
}

/* The backpack's live ammo travels as the network field type deposit_value, 10 bits (0..1023): the engine clamps any
 * count above that. Widened as HellForge's machine-gun-10k example does (all players need the same edit). */
static void NetworkReady(HfMod* mod, HfEvent event, void* user)
{
    HfFieldType t;
    memset(&t, 0, sizeof t);
    if (g_hf->network_field_type("deposit_value", &t)) { g_hf->log(g_mod, "deposit_value: not found"); return; }
    g_hf->log(g_mod, "deposit_value: kind %d, %d bits, min %d (up to %d)", t.kind, t.bits, t.min, t.min + (1 << t.bits) - 1);
    if (!s_apply || s_widen <= 10) return;
    HfFieldType expect = {1, 10, 0};
    if (g_hf->set_network_field_bits(g_mod, "deposit_value", expect, s_widen))
        g_hf->log(g_mod, "deposit_value: widening to %d bits refused (%s)", s_widen, g_hf->last_log ? g_hf->last_log(g_mod) : "");
    else g_hf->log(g_mod, "deposit_value: widened to %d bits (up to %d)", s_widen, (1 << s_widen) - 1);
}

/* ---- the call-in refund (0.4.0) --------------------------------------------------------------------------------
 * The local player's stratagem record, from HD2Runtime's research (docs/research/free-call-in.md, all [STATIC]):
 * records at [game.dll+0x347CE50], 0x1690 bytes each, count at +0x2D200, peer id at +0; the stratagem state at +0x38,
 * its entries at +0x188 (0x30 bytes each, count at +0x788, at most 16): type +0, uses +4, granted +9, cooldown end +0x18
 * (u64 microseconds, the game clock [[game.dll+0x3326348]+0x18]). A stratagem is ready once its end is not past the
 * clock, so the refund writes the Maxigun entry's end = now. Only the local player's record is touched, and nothing is
 * read or written unless the game code at three of HD2Runtime's pins matches this build. */
#define REC_LIST 0x347CE50u
#define REC_COUNT 0x2D200u
#define REC_SIZE 0x1690u
#define LOCAL_PEER 0x347CEF0u
#define LOCAL_PEER_AT 0xB398u
#define CLOCK 0x3326348u
static uintptr_t g_game;
static int g_pins = -1;  /* -1 not checked, 0 refused, 1 matched */

static int Pins(void)
{
    if (g_pins >= 0) return g_pins;
    g_pins = 0;
    if (g_hf->size <= offsetof(HfApi, code_matches) || !g_hf->code_matches) { g_hf->log(g_mod, "refund: this HellForge has no code_matches"); return 0; }
    g_game = g_hf->module_base("game.dll", NULL);
    if (!g_game) { g_hf->log(g_mod, "refund: game.dll not found"); return 0; }
    static const struct { uintptr_t at; const char* bytes; } kPins[] = {
        {0x1834AE6, "4C 8B 0D 63 83 C4 01"},  /* the HUD reads the records list (mov r9,[rip -> 0x347CE50]) */
        {0x1834B07, "48 05 90 16 00 00"},     /* ... and steps 0x1690 a record */
        {0x66D24A, "48 8B 8E A0 01 00 00"}};  /* the availability check reads an entry's cooldown end */
    for (unsigned i = 0; i < sizeof kPins / sizeof kPins[0]; i++)
        if (!g_hf->code_matches(g_game + kPins[i].at, kPins[i].bytes))
        {
            g_hf->log(g_mod, "refund: game.dll+0x%llX is not as researched: no stratagem record reads or writes", (unsigned long long)kPins[i].at);
            return 0;
        }
    g_pins = 1;
    g_hf->log(g_mod, "refund: game code matches the research (3 pins)");
    return 1;
}
static int Now(uint64_t* now)
{
    uintptr_t clock = 0;
    return !g_hf->read(g_game + CLOCK, &clock, 8) && clock && !g_hf->read(clock + 0x18, now, 8);
}
/* The local player's stratagem state (record + 0x38), or 0 */
static uintptr_t LocalState(void)
{
    if (Pins() != 1) return 0;
    uintptr_t recs = 0, peerObj = 0;
    uint64_t me = 0;
    uint32_t n = 0;
    if (g_hf->read(g_game + REC_LIST, &recs, 8) || !recs) return 0;
    if (g_hf->read(g_game + LOCAL_PEER, &peerObj, 8) || !peerObj || g_hf->read(peerObj + LOCAL_PEER_AT, &me, 8)) return 0;
    if (g_hf->read(recs + REC_COUNT, &n, 4) || n > 32) return 0;
    for (uint32_t r = 0; r < n; r++)
    {
        uintptr_t rec = recs + (uintptr_t)r * REC_SIZE;
        uint64_t peer = 0;
        if (!g_hf->read(rec, &peer, 8) && peer == me) return rec + 0x38;
    }
    return 0;
}
static void DumpRecord(const char* why)
{
    uintptr_t st = LocalState();
    uint32_t count = 0;
    uint64_t now = 0;
    if (!st || g_hf->read(st + 0x788, &count, 4) || count > 16 || !Now(&now)) { g_hf->log(g_mod, "record (%s): not readable", why); return; }
    g_hf->log(g_mod, "record (%s): %u stratagems", why, count);
    for (uint32_t i = 0; i < count; i++)
    {
        uint8_t e[0x30];
        if (g_hf->read(st + 0x188 + (uintptr_t)i * 0x30, e, sizeof e)) continue;
        uint32_t type, uses;
        uint64_t end;
        memcpy(&type, e, 4);
        memcpy(&uses, e + 4, 4);
        memcpy(&end, e + 0x18, 8);
        double left = ((double)(int64_t)(end - now)) / 1e6;
        g_hf->log(g_mod, "  %2u: type %3u%s uses %s%u granted %u cooldown %s%.1f s", i, type, type == HF_STRATAGEM_M1000_MAXIGUN ? " (Maxigun)" : "",
            uses == 0xFFFFFFFFu ? "unlimited " : "", uses == 0xFFFFFFFFu ? 0 : uses, e[9], left > 0 ? "left " : "ready ", left > 0 ? left : 0.0);
    }
}
/* The Maxigun's cooldown back to ready: its entry's end = now */
static void Refund(void)
{
    uintptr_t st = LocalState();
    uint32_t count = 0;
    uint64_t now = 0;
    if (!st || g_hf->read(st + 0x788, &count, 4) || count > 16 || !Now(&now)) { g_hf->log(g_mod, "refund: the stratagem record is not readable"); return; }
    for (uint32_t i = 0; i < count; i++)
    {
        uintptr_t e = st + 0x188 + (uintptr_t)i * 0x30;
        uint32_t type = 0;
        uint64_t end = 0;
        if (g_hf->read(e, &type, 4) || type != HF_STRATAGEM_M1000_MAXIGUN || g_hf->read(e + 0x18, &end, 8)) continue;
        double left = ((double)(int64_t)(end - now)) / 1e6;
        if (g_hf->write(e + 0x18, &now, 8)) { g_hf->log(g_mod, "refund: the write failed"); return; }
        g_hf->log(g_mod, "refund: Maxigun (slot %u) ready now (it had %.1f s of cooldown left)", i, left > 0 ? left : 0.0);
        return;
    }
    g_hf->log(g_mod, "refund: no Maxigun in your stratagems");
}

/* ---- in a mission --------------------------------------------------------------------------------------------- */
static void EndBurst(void)
{
    if (g_burstShots >= 20)
        g_hf->log(g_mod, "Maxigun burst: %lld rounds, peak %d rpm (from shots counted over 1 s windows)", g_burstShots, g_burstPeak);
    g_burstShots = 0;
    g_burstPeak = 0;
    g_windowStart = -1;
    g_windowShots = 0;
}
static void OnStat(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfStatEvent* e = (const HfStatEvent*)info;
    static struct { uint64_t res; int stat; } seen[96];
    static int nseen;
    for (uint32_t i = 0; i < e->count && i < HF_STAT_SOURCES; i++)
    {
        const HfStatSource* s = &e->sources[i];
        int known = 0;
        for (int k = 0; k < nseen; k++) known |= seen[k].res == s->resource && seen[k].stat == (int)e->stat;
        if (!known && nseen < 96)
        {
            seen[nseen].res = s->resource;
            seen[nseen++].stat = (int)e->stat;
            g_hf->log(g_mod, "stat source: %s from %016llX%s", e->stat == HF_STAT_SHOTS ? "shots" : e->stat == HF_STAT_KILLS ? "kills"
                : e->stat == HF_STAT_HITS ? "hits" : "damage", (unsigned long long)s->resource,
                s->resource == GUN ? " (the Maxigun)" : s->resource == PACK ? " (the Maxigun backpack)" : "");
        }
        if (s->resource != GUN && s->resource != PACK) continue;
        if (e->stat == HF_STAT_KILLS) g_kills += s->added;
        else if (e->stat == HF_STAT_SHOTS)
        {
            g_shots += s->added;
            if (g_clock - g_lastShot > 0.6) EndBurst();
            g_lastShot = g_clock;
            g_burstShots += s->added;
            if (g_windowStart < 0) { g_windowStart = g_clock; g_windowShots = 0; }
            g_windowShots += s->added;
            if (g_clock - g_windowStart >= 1.0)
            {
                int rpm = (int)(g_windowShots * 60.0 / (g_clock - g_windowStart) + 0.5);
                if (rpm > g_burstPeak) g_burstPeak = rpm;
                g_windowStart = g_clock;
                g_windowShots = 0;
            }
        }
    }
}
static void OnStratagem(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfStratagemEvent* e = (const HfStratagemEvent*)info;
    g_hf->log(g_mod, "stratagem thrown: type %u%s", e->type, e->type == HF_STRATAGEM_M1000_MAXIGUN ? " (the Maxigun)" : "");
    if (e->type == HF_STRATAGEM_M1000_MAXIGUN && g_inMission) { g_maxigunAt = g_clock; g_dumpAt = g_clock + 5; }
}
static void OnMission(HfMod* mod, const HfEventInfo* info, void* user)
{
    if (info->kind == HF_ON_MISSION_END || g_inMission)
    {
        EndBurst();
        g_hf->log(g_mod, "mission end: the Maxigun fired %lld rounds and was credited %lld kills", g_shots, g_kills);
    }
    g_inMission = info->kind == HF_ON_MISSION_START;
    g_shots = g_kills = 0;
    g_missionAt = g_clock;
    g_maxigunAt = -1;
    if (s_delay >= 0) UndoAll();
    if (g_inMission) g_hf->log(g_mod, "mission start");
}
static void Tick(HfMod* mod, float dt, void* user)
{
    g_clock += dt;
    if (s_apply && s_delay >= 0 && g_inMission && !g_applied && g_clock - g_missionAt >= s_delay)
    {
        char why[64];
        snprintf(why, sizeof why, "%.0f s into the mission", (double)s_delay);
        ApplyAll(why);
        DumpRecord("before the refund");
        if (s_refund) Refund();
        DumpRecord("after the refund");
    }
    static double nextDump;
    if (g_inMission && g_clock >= nextDump) { nextDump = g_clock + 30; DumpRecord("every 30 s"); }
    if (g_dumpAt > 0 && g_clock >= g_dumpAt) { g_dumpAt = -1; DumpRecord("5 s after a Maxigun call"); }
    if (g_burstShots && g_clock - g_lastShot > 0.6) EndBurst();
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* hf, HfMod* mod)
{
    if (hf->version < 13) return 1;
    g_hf = hf;
    g_mod = mod;
    LoadSettings();
    hf->on(mod, HF_EVENT_ENTITIES_READY, EntitiesReady, NULL);
    hf->on(mod, HF_EVENT_NETWORK_READY, NetworkReady, NULL);
    int ok = hf->subscribe(mod, HF_ON_STAT, OnStat, NULL, 0) & hf->subscribe(mod, HF_ON_STRATAGEM, OnStratagem, NULL, 0) &
             hf->subscribe(mod, HF_ON_MISSION_START, OnMission, NULL, 0) & hf->subscribe(mod, HF_ON_MISSION_END, OnMission, NULL, 0);
    if (!ok || !hf->on_game_thread(mod, Tick, NULL)) { hf->log(mod, "Maxigun Test: events or the game thread unavailable"); return 1; }
    g_inMission = hf->game_state(mod) == HF_STATE_MISSION;
    hf->log(mod, "Maxigun Test %s: %s", VERSION, s_apply ? "applying the test values from maxigun-test.ini" : "logging only");
    return 0;
}
