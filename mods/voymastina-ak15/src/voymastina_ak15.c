/* Voymastina: AK-15 on foot (yui.voymastina-ak15), a test build of her GFL1 kit as AK-15, to be merged into Voymastina
 * when her mech mode is ready (in the mech: her GFL2 kit; thrown out of it: this one). Build 25480438, SDK 15.
 *
 *   Rage (passive)                  each of her hits has rage_chance to make her rage for rage_seconds: while raging her
 *                                   hits deal rage_bonus more (GFL1: an extra hit at 40%, 50% at Mod)
 *   Eye of the White Mastiff (key)  every known enemy within eye_range is monitored for eye_seconds: her hits on them
 *                                   deal eye_bonus more, theirs on her eye_evasion less. Overload: +5 every 2 s and +1 per
 *                                   monitored enemy; at overload_max it ends, resets, and her damage drops by
 *                                   overload_penalty for overload_seconds (GFL1: slower and less accurate), during which
 *                                   it can't be used. Cooldown eye_cooldown from use.
 *   Blood-Curdling Howl             when she monitors more than howl_targets enemies (once per Eye), or when she is
 *                                   thrown out of her mech (the test key key_eject stands in for that until the mech
 *                                   exists): immune to all damage for howl_immune seconds, and a blast (howl_blast) on
 *                                   the strongest known enemy within howl_range (twice while raging)
 *
 * Her hits: any damage whose dealer or owner is her Helldiver (her fists, bashes and guns alike). Known enemies: what any
 * player has hit, and whatever hit a player, this mission. Settings: voymastina-ak15.ini beside this DLL.
 * Addresses: the health records (game+0x3326688) and entity resolve (game+0xFD9D40) as Voymastina reads them; the unit
 * position as EXO Beacon Launcher (MIT) found it. */
#include <stdint.h>
#include <stddef.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include <windows.h>
#include "hellforge_mod.h"

#define BUILD "25480438"
#define VERSION "0.1.1"
static const HfApi* hf;
static HfMod* self;
static uintptr_t game, exe;
#define Log(...) hf->log(self, __VA_ARGS__)

/* ---- settings --------------------------------------------------------------------------------------------------- */
static float s_rageChance = 0.15f, s_rageSeconds = 3, s_rageBonus = 0.4f;
static float s_eyeSeconds = 6, s_eyeCooldown = 8, s_eyeRange = 50, s_eyeBonus = 0.4f, s_eyeEvasion = 0.4f;
static float s_overloadMax = 24, s_overloadSeconds = 5, s_overloadPenalty = 0.2f;
static int s_howlTargets = 5, s_howlBlast = 52;
static float s_howlImmune = 2, s_howlRange = 60;
typedef struct { int vk, ctrl, shift, alt; char label[24]; } Key;
static Key s_keyEye, s_keyEject;

static int ParseKey(const char* text, Key* k)
{
    memset(k, 0, sizeof *k);
    snprintf(k->label, sizeof k->label, "%s", text);
    const char* p = text;
    for (;;)
    {
        if (!_strnicmp(p, "Ctrl+", 5)) { k->ctrl = 1; p += 5; }
        else if (!_strnicmp(p, "Shift+", 6)) { k->shift = 1; p += 6; }
        else if (!_strnicmp(p, "Alt+", 4)) { k->alt = 1; p += 4; }
        else break;
    }
    if (strlen(p) == 1 && isalnum((unsigned char)p[0])) { k->vk = toupper((unsigned char)p[0]); return 1; }
    if ((p[0] == 'F' || p[0] == 'f') && atoi(p + 1) >= 1 && atoi(p + 1) <= 24) { k->vk = VK_F1 + atoi(p + 1) - 1; return 1; }
    if (!_stricmp(p, "Mouse4")) { k->vk = VK_XBUTTON1; return 1; }
    if (!_stricmp(p, "Mouse5")) { k->vk = VK_XBUTTON2; return 1; }
    if (!_stricmp(p, "Mouse3")) { k->vk = VK_MBUTTON; return 1; }
    return 0;
}
static void Settings(void)
{
    char path[MAX_PATH] = "", text[64];
    /* the mod's own folder (HellForge builds the DLL in a temp folder): the session's mod list names it */
    for (uint32_t i = 0; hf->size >= offsetof(HfApi, mods) + sizeof(void*) && hf->mods; i++)
    {
        HfModInfo m = {0};
        m.size = sizeof m;
        if (!hf->mods(self, i, &m)) break;
        if (m.id && !strcmp(m.id, "yui.voymastina-ak15") && m.folder && m.folder[0])
        {
            snprintf(path, sizeof path, "%s\\voymastina-ak15.ini", m.folder);
            break;
        }
    }
    if (!path[0])
    {
        HMODULE m = NULL;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(void*)Settings, &m))
            GetModuleFileNameA(m, path, MAX_PATH);
        char* slash = strrchr(path, '\\');
        if (slash) strcpy(slash + 1, "voymastina-ak15.ini");
    }
    DWORD found = GetFileAttributesA(path);
#define F(name, var, lo, hi) do { GetPrivateProfileStringA("ak15", name, "", text, sizeof text, path); \
        if (text[0]) { float v_ = (float)atof(text); if (v_ >= lo && v_ <= hi) var = v_; } } while (0)
#define I(name, var, lo, hi) do { GetPrivateProfileStringA("ak15", name, "", text, sizeof text, path); \
        if (text[0]) { int v_ = atoi(text); if (v_ >= lo && v_ <= hi) var = v_; } } while (0)
    F("rage_chance", s_rageChance, 0, 1); F("rage_seconds", s_rageSeconds, 0, 60); F("rage_bonus", s_rageBonus, 0, 10);
    F("eye_seconds", s_eyeSeconds, 0.5f, 60); F("eye_cooldown", s_eyeCooldown, 0, 300); F("eye_range", s_eyeRange, 5, 300);
    F("eye_bonus", s_eyeBonus, 0, 10); F("eye_evasion", s_eyeEvasion, 0, 1);
    F("overload_max", s_overloadMax, 1, 1000); F("overload_seconds", s_overloadSeconds, 0, 60); F("overload_penalty", s_overloadPenalty, 0, 1);
    I("howl_targets", s_howlTargets, 0, 100); F("howl_immune", s_howlImmune, 0, 30); F("howl_range", s_howlRange, 5, 300);
    I("howl_blast", s_howlBlast, 0, 100000);
#undef F
#undef I
    GetPrivateProfileStringA("ak15", "key_eye", "N", text, sizeof text, path);
    if (!ParseKey(text, &s_keyEye)) ParseKey("N", &s_keyEye);
    GetPrivateProfileStringA("ak15", "key_eject", "Ctrl+N", text, sizeof text, path);
    if (!ParseKey(text, &s_keyEject)) ParseKey("Ctrl+N", &s_keyEject);
    Log("settings %s (%s): Eye on %s, test ejection on %s; rage %.0f%% for %.0f s (+%.0f%%), Eye %.0f s / %.0f s cooldown, %.0f m, +%.0f%% / -%.0f%%;"
        " overload at %.0f, -%.0f%% for %.0f s; Howl over %d targets, immune %.1f s, blast %d within %.0f m",
        found == INVALID_FILE_ATTRIBUTES ? "built in" : "read", path, s_keyEye.label, s_keyEject.label, (double)(s_rageChance * 100),
        (double)s_rageSeconds, (double)(s_rageBonus * 100), (double)s_eyeSeconds, (double)s_eyeCooldown, (double)s_eyeRange,
        (double)(s_eyeBonus * 100), (double)(s_eyeEvasion * 100), (double)s_overloadMax, (double)(s_overloadPenalty * 100),
        (double)s_overloadSeconds, s_howlTargets, (double)s_howlImmune, s_howlBlast, (double)s_howlRange);
}

/* ---- the game: entity resolve, positions, health ------------------------------------------------------------------ */
static int RD(uintptr_t a, void* out, size_t n) { return a >= 0x10000 && a < 0x7FFFFFFF0000ull && hf->read(a, out, n) == 0; }
static uintptr_t P(uintptr_t a) { uintptr_t v = 0; return RD(a, &v, 8) && v >= 0x10000 && v < 0x7FFFFFFF0000ull ? v : 0; }
static uint32_t U32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
typedef void* (*ResolveFn)(uint32_t);
typedef const float* (*LocalPosFn)(uint32_t, uint32_t);
static ResolveFn g_resolve;
static LocalPosFn g_localPos;
static int PosOf(uint32_t entity, float out[3])
{
    if (!g_localPos && game)
    {
        uintptr_t script = P(game + 0x3326308), units = script ? P(script + 0x18) : 0;
        if (units && P(units) == exe + 0x1fc450) { g_localPos = (LocalPosFn)(exe + 0x1fc450); Log("positions: ready"); }
    }
    if (!g_resolve || !g_localPos || !entity || entity == UINT32_MAX) return 0;
    void* o = g_resolve(entity);
    uint8_t h[24];
    if (!o || !RD((uintptr_t)o, h, 24) || U32(h + 8) != entity || !U32(h + 12)) return 0;
    const float* p = g_localPos(U32(h + 12), 0);
    return p && RD((uintptr_t)p, out, 12) && isfinite(out[0]) && isfinite(out[1]) && isfinite(out[2]);
}
static int HealthOf(uint32_t entity, int32_t* health)
{
    uintptr_t m = P(game + 53634696u);
    uint8_t h[88];
    if (!entity || !m || !RD(m + 4112, h, sizeof h)) return 0;
    uint32_t live = U32(h + 16), cap = U32(h + 40), empty = U32(h + 44), mult = U32(h + 48);
    uintptr_t buckets, records;
    memcpy(&buckets, h + 32, 8); memcpy(&records, h + 72, 8);
    if (!buckets || !records || !cap || cap > 65536 || (cap & (cap - 1)) || live > 4096) return 0;
    for (uint32_t k = 0; k < cap; k++)
    {
        uint8_t b[8];
        if (!RD(buckets + 8u * ((entity * mult + k) & (cap - 1)), b, 8)) return 0;
        uint32_t key = U32(b), index = U32(b + 4);
        if (key == empty) return 0;
        if (key != entity) continue;
        if (index >= live) return 0;
        uint8_t r[24];
        if (!RD(records + 440u * index, r, sizeof r)) return 0;
        *health = (int32_t)U32(r + 20);
        return 1;
    }
    return 0;
}
static float Dist(const float a[3], const float b[3])
{
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
}

/* ---- who is who: network ids, players, known enemies ------------------------------------------------------------- */
#define NETS 8192
static uint32_t g_netEntity[NETS];          /* network id -> entity (spawn events) */
static atomic_uint g_me;                    /* her Helldiver */
static uint32_t g_players[8];
#define FOES 512
typedef struct { uint32_t entity; double seen; int monitored; } Foe;
static Foe g_foes[FOES];
static int g_nfoes;
static double g_now;
static int IsPlayer(uint32_t e) { for (int i = 0; i < 8; i++) if (e && g_players[i] == e) return 1; return 0; }
static uint32_t EntityOfNet(uint32_t net) { return net && net < NETS ? g_netEntity[net] : 0; }
static uint32_t MyNet(void)
{
    uint32_t me = atomic_load(&g_me);
    for (uint32_t i = 1; i < NETS && me; i++) if (g_netEntity[i] == me) return i;
    return 0;
}
static Foe* FoeOf(uint32_t e)
{
    for (int i = 0; i < g_nfoes; i++) if (g_foes[i].entity == e) return &g_foes[i];
    return NULL;
}
static void Known(uint32_t e)
{
    if (!e || e == UINT32_MAX || IsPlayer(e)) return;
    Foe* f = FoeOf(e);
    if (!f)
    {
        if (g_nfoes == FOES)
        {
            int old = 0;
            for (int i = 1; i < FOES; i++) if (g_foes[i].seen < g_foes[old].seen) old = i;
            f = &g_foes[old];
        }
        else f = &g_foes[g_nfoes++];
        f->entity = e; f->monitored = 0;
    }
    f->seen = g_now;
}

/* ---- the kit's state ------------------------------------------------------------------------------------------------ */
static double g_rageUntil, g_eyeUntil, g_eyeReady, g_overloadUntil, g_immuneUntil, g_overTick;
static float g_overload;
static int g_howled;   /* Howl from monitoring, once per Eye */
static unsigned g_myNet;
static int Raging(void) { return g_now < g_rageUntil; }
static int Watching(void) { return g_now < g_eyeUntil; }
static int Monitored(uint32_t e) { Foe* f = FoeOf(e); return Watching() && f && f->monitored; }

#define HAS_SPAWN() (hf->size >= offsetof(HfApi, spawn_explosion) + sizeof(void*) && hf->spawn_explosion)
static void Howl(const char* why)
{
    g_immuneUntil = g_now + s_howlImmune;
    float me[3];
    uint32_t best = 0;
    int32_t bestHp = 0;
    if (PosOf(atomic_load(&g_me), me))
        for (int i = 0; i < g_nfoes; i++)
        {
            float p[3];
            int32_t hp;
            if (!HealthOf(g_foes[i].entity, &hp) || hp <= 0 || !PosOf(g_foes[i].entity, p) || Dist(p, me) > s_howlRange) continue;
            if (hp > bestHp) { bestHp = hp; best = g_foes[i].entity; }
        }
    Log("Blood-Curdling Howl (%s): immune for %.1f s; strongest near: %s", why, (double)s_howlImmune, best ? "found" : "none");
    if (!best || !s_howlBlast || !HAS_SPAWN()) return;
    float at[3];
    if (!PosOf(best, at)) return;
    at[2] += 1.0f;
    int n = Raging() ? 2 : 1;
    for (int i = 0; i < n; i++)
    {
        int r = hf->spawn_explosion(self, (uint32_t)s_howlBlast, at);
        Log("  blast %d on entity %u (health %d)%s: %d", s_howlBlast, best, bestHp, Raging() ? ", raging" : "", r);
    }
}
#undef HAS_SPAWN

static void MonitorNow(int log)
{
    float me[3];
    int count = 0;
    if (!PosOf(atomic_load(&g_me), me)) return;
    for (int i = 0; i < g_nfoes; i++)
    {
        float p[3];
        int32_t hp;
        int in = HealthOf(g_foes[i].entity, &hp) && hp > 0 && PosOf(g_foes[i].entity, p) && Dist(p, me) <= s_eyeRange;
        if (in && !g_foes[i].monitored && !log) Log("Eye: now monitoring entity %u too", g_foes[i].entity);
        g_foes[i].monitored = in;
        count += in;
    }
    if (log) Log("Eye of the White Mastiff: %d enemies monitored for %.0f s", count, (double)s_eyeSeconds);
    if (!g_howled && count > s_howlTargets) { g_howled = 1; Howl("more than the howl_targets monitored"); }
}
static void Eye(void)
{
    if (g_now < g_overloadUntil) { Log("Eye: overloaded, %.1f s left", g_overloadUntil - g_now); return; }
    if (g_now < g_eyeReady) { Log("Eye: cooling down, %.1f s left", g_eyeReady - g_now); return; }
    g_eyeUntil = g_now + s_eyeSeconds;
    g_eyeReady = g_now + s_eyeCooldown;
    g_overload = 0;
    g_overTick = g_now + 2;
    g_howled = 0;
    MonitorNow(1);
}
static void EyeFrame(void)
{
    if (!Watching()) return;
    static double nextScan;
    if (g_now >= nextScan) { nextScan = g_now + 0.5; MonitorNow(0); }
    if (g_now >= g_overTick)
    {
        g_overTick += 2;
        int n = 0;
        for (int i = 0; i < g_nfoes; i++) n += g_foes[i].monitored;
        g_overload += 5 + n;
        Log("Eye: overload %.0f / %.0f (%d monitored)", (double)g_overload, (double)s_overloadMax, n);
        if (g_overload >= s_overloadMax)
        {
            g_eyeUntil = 0;
            g_overload = 0;
            g_overloadUntil = g_now + s_overloadSeconds;
            Log("Eye: OVERLOAD: it ends; her damage -%.0f%% for %.0f s", (double)(s_overloadPenalty * 100), (double)s_overloadSeconds);
        }
    }
}

/* ---- events -------------------------------------------------------------------------------------------------------- */
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    if (s->network && s->network < NETS) g_netEntity[s->network] = s->entity;
}
static void OnPlayer(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    if (!p->entity) return;
    int k = 0;
    while (k < 8 && g_players[k] && g_players[k] != p->entity) k++;
    if (k < 8) g_players[k] = p->entity;
    if (p->local) { atomic_store(&g_me, p->entity); g_myNet = 0; }
}
static int Mine(uint32_t dealer, uint32_t owner)
{
    if (!g_myNet) g_myNet = MyNet();
    return g_myNet && (dealer == g_myNet || owner == g_myNet);
}
static void OnDamage(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfDamageEvent* d = (const HfDamageEvent*)info;
    uint32_t from = EntityOfNet(d->dealer), by = EntityOfNet(d->owner);
    if (IsPlayer(d->target)) { if (from && !IsPlayer(from)) Known(from); if (by && !IsPlayer(by)) Known(by); }
    else if (IsPlayer(from) || IsPlayer(by) || d->creditor) Known(d->target);
}
static void OnDamagePre(HfMod* mod, const HfEventInfo* info, void* user)
{
    HfDamagePreEvent* e = (HfDamagePreEvent*)(uintptr_t)info;
    HfDamageEvent* d = &e->damage;
    uint32_t me = atomic_load(&g_me);
    if (!me) return;
    if (d->target == me)
    {
        if (g_now < g_immuneUntil) { e->cancel = 1; return; }
        uint32_t from = EntityOfNet(d->dealer), by = EntityOfNet(d->owner);
        if (d->amount > 0 && (Monitored(from) || Monitored(by)))
            d->amount = (int32_t)lroundf((float)d->amount * (1 - s_eyeEvasion));
        return;
    }
    if (d->amount <= 0 || IsPlayer(d->target) || !Mine(d->dealer, d->owner)) return;
    float mult = 1;
    if (!Raging() && s_rageChance > 0 && (float)rand() / (float)RAND_MAX < s_rageChance)
    {
        g_rageUntil = g_now + s_rageSeconds;
        Log("Rage: %.0f s", (double)s_rageSeconds);
    }
    if (Raging()) mult += s_rageBonus;
    if (Monitored(d->target)) mult += s_eyeBonus;
    if (g_now < g_overloadUntil) mult *= 1 - s_overloadPenalty;
    if (mult != 1) d->amount = (int32_t)lroundf((float)d->amount * mult);
}

/* ---- the game thread ------------------------------------------------------------------------------------------------ */
static int Focused(void)
{
    DWORD pid = 0;
    HWND w = GetForegroundWindow();
    if (w) GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}
static int Held(const Key* k)
{
    if (!k->vk || !(GetAsyncKeyState(k->vk) & 0x8000)) return 0;
    int c = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0, s = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0,
        a = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    return c == k->ctrl && s == k->shift && a == k->alt;
}
static void Tick(HfMod* mod, float dt, void* user)
{
    g_now += dt > 0 && dt < 1 ? dt : 0;
    if (hf->game_state(self) != HF_STATE_MISSION) { g_nfoes = 0; g_eyeUntil = g_overloadUntil = g_immuneUntil = g_rageUntil = 0; return; }
    EyeFrame();
    static int prevEye, prevEject;
    int focus = Focused(), eye = focus && Held(&s_keyEye), eject = focus && Held(&s_keyEject);
    static ULONGLONG lastPress;
    ULONGLONG t = GetTickCount64();
    if (eye && !prevEye && t - lastPress > 300) { lastPress = t; Eye(); }
    if (eject && !prevEject && t - lastPress > 300) { lastPress = t; Howl("test ejection key"); }
    prevEye = eye; prevEject = eject;
    static int wasImmune;
    int immune = g_now < g_immuneUntil;
    if (wasImmune && !immune) Log("Howl: immunity over");
    wasImmune = immune;
}

static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gs = 0, xs = 0;
    game = hf->module_base("game.dll", &gs);
    exe = hf->module_base("helldivers2.exe", &xs);
    if (!game || !exe || gs != 74727424u || xs != 60719104u) { Log("REFUSED: not build " BUILD "'s modules: positions off"); return; }
    if (hf->code_matches(game + 0xFD9D40, "48 83 ec 08 3b 0d 02 9f 4a 02 0f 84 9b 00 00 00")) g_resolve = (ResolveFn)(game + 0xFD9D40);
    Settings();
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 15 || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    Log("Voymastina: AK-15 on foot " VERSION " (test build of her GFL1 kit; run it with Voymastina for her fists)");
    srand(GetTickCount());
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 10) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_DAMAGE, OnDamage, NULL, 0) && api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) &&
             api->on_game_thread(mod, Tick, NULL);
    if (!ok) Log("events or the game thread are unavailable");
    return 0;
}
