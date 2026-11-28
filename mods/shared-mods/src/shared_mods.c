/* Shared Mods: mods for the character kits, found on the battlefield and fitted to any kit on the kits' options screen.
 *
 *   Drops. A kill credited to this machine's player can drop a mod (drop_chance; heavies, enemies with at least
 *   heavy_health health, heavy_chance and better odds of the rarer ones). A rare one is kept at once; a common or
 *   uncommon one is carried until the mission ends, and kept only if it ended well: lost if the mission failed or was
 *   left (abandoned, quit, disconnected). The game's own exit from a mission (StateGame on_exit, game.dll+0xACDF50) says
 *   which: its bytes +0x12 (the mission came to a result) and +0x13 (that result a success), as the game's mission-end
 *   telemetry reads them (+0xACE84A).
 *   Ranks. A second copy of a mod ranks it up: rank r needs 1 + r(r+1)/2 copies (rank 5: 16). Each rank adds to it.
 *   Fitting. On the options screen (F9), the SHARED MODS tab: the kit picked in the character column, its four slots and
 *   its capacity (capacity; a mod costs its rarity's base and one more a rank), and every mod found. Each kit has its own
 *   four; the same mod may be fitted to several kits.
 *   Effects. What a kit's fitted mods give is published for the kits (kitbase/kb_extras.h: SharedMods(id)), read at a
 *   mission's start: her skills' cooldowns, damage and durations. Resistance is applied here: the damage taken by this
 *   machine's Helldiver while the acting kit has it fitted.
 *
 * Saved to %LOCALAPPDATA%\HellForgeMods\shared-mods.ini (mods found, each kit's slots); settings in shared-mods.ini in
 * the mod folder. Needs a character kit with this kitbase (its options screen draws the tab). */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <windows.h>
#include "hellforge_mod.h"
#include "hellforge_components.h"
#include "hellforge_functions.h"

#include "kitbase/kb_core.h"
#include "kitbase/kb_roster.h"
#include "kitbase/kb_extras.h"

#define VERSION "0.1.0"
#define BUILD "25480438"
#define SLOTS 4
#define RANK_MAX 5

/* ---- the mods ------------------------------------------------------------------------------------------------------ */
enum { R_COMMON, R_UNCOMMON, R_RARE };
static const char* const kRarity[3] = {"COMMON", "UNCOMMON", "RARE"};
enum { M_COOLDOWN, M_RESIST, M_DURATION, M_DAMAGE, M_N };
typedef struct ModDef
{
    const char* key;    /* in the save file */
    const char* name;
    int rarity;
    float per;          /* what each rank gives (rank 0 gives it once, rank 5 six times) */
    const char* what;   /* printf of the percentage */
} ModDef;
static const ModDef kMods[M_N] = {
    {"cooldown", "Cooldown", R_COMMON, 0.05f, "Skill cooldowns -%.0f%%"},
    {"resistance", "Resistance", R_COMMON, 0.04f, "Damage taken -%.0f%%"},
    {"duration", "Duration", R_UNCOMMON, 0.08f, "Skill durations +%.0f%%"},
    {"skill_damage", "Skill Damage", R_RARE, 0.10f, "Skill damage +%.0f%%"},
};
static int CopiesFor(int rank) { return 1 + rank * (rank + 1) / 2; }
static int RankOf(int copies)
{
    int r = -1;
    while (r < RANK_MAX && copies >= CopiesFor(r + 1)) r++;
    return r;   /* -1: not found */
}
static float Effect(int m, int rank) { return rank < 0 ? 0 : kMods[m].per * (float)(rank + 1); }
static int Cost(int m, int rank) { return 2 + 2 * kMods[m].rarity + (rank < 0 ? 0 : rank); }

/* ---- settings ------------------------------------------------------------------------------------------------------ */
static float s_dropChance = 0.006f, s_heavyChance = 0.06f, s_heavyHealth = 1200;
static float s_odds[2][3] = {{0.70f, 0.22f, 0.08f}, {0.40f, 0.35f, 0.25f}};   /* common, uncommon, rare: a kill, a heavy */
static int s_capacity = 30;

/* ---- the collection (g_lock: the game thread finds, the screen fits) ----------------------------------------------- */
#define KITS 32
typedef struct Loadout { char id[32]; int8_t slot[SLOTS]; } Loadout;   /* -1 empty */
static SRWLOCK g_lock = SRWLOCK_INIT;
static int g_copies[M_N];           /* kept */
static int g_pending[M_N];          /* found this mission, kept if it ends well */
static Loadout g_kit[KITS];
static int g_nkit;
static char g_save[MAX_PATH * 2];
static atomic_int g_dirty;          /* a change to save */
static unsigned g_found[3], g_kept, g_lost;

static Loadout* KitOf(const char* id, int make)
{
    if (!id || !id[0]) return NULL;
    for (int i = 0; i < g_nkit; i++) if (!strncmp(g_kit[i].id, id, 31)) return &g_kit[i];
    if (!make || g_nkit >= KITS) return NULL;
    Loadout* l = &g_kit[g_nkit++];
    memset(l, 0, sizeof *l);
    snprintf(l->id, sizeof l->id, "%s", id);
    for (int s = 0; s < SLOTS; s++) l->slot[s] = -1;
    return l;
}
static int Used(const Loadout* l)
{
    int c = 0;
    for (int s = 0; l && s < SLOTS; s++) if (l->slot[s] >= 0) c += Cost(l->slot[s], RankOf(g_copies[l->slot[s]]));
    return c;
}

/* what each kit's slots give, for the kits (kb_extras.h); under g_lock */
static void Publish(void)
{
    KbSmPub* p = SmPub();
    if (!p) return;
    InterlockedIncrement(&p->seq);
    MemoryBarrier();
    p->version = 1;
    uint32_t n = 0;
    for (int i = 0; i < g_nkit && n < KB_SM_KITS; i++)
    {
        KbSmBonus b;
        SmNone(&b, g_kit[i].id);
        for (int s = 0; s < SLOTS; s++)
        {
            int m = g_kit[i].slot[s];
            if (m < 0) continue;
            float e = Effect(m, RankOf(g_copies[m]));
            if (m == M_COOLDOWN) b.cooldown *= 1 - e;
            if (m == M_RESIST) b.taken *= 1 - e;
            if (m == M_DURATION) b.duration *= 1 + e;
            if (m == M_DAMAGE) b.damage *= 1 + e;
        }
        p->kit[n++] = b;
    }
    p->count = n;
    p->magic = KB_SM_MAGIC;
    MemoryBarrier();
    InterlockedIncrement(&p->seq);
}

/* ---- the save file ------------------------------------------------------------------------------------------------- */
static int ModByKey(const char* k)
{
    for (int m = 0; m < M_N; m++) if (!strcmp(kMods[m].key, k)) return m;
    return -1;
}
static char* Trim(char* s)
{
    while (*s == ' ' || *s == '\t') s++;
    char* e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}
static void Load(void)
{
    FILE* f = g_save[0] ? OpenUtf8(g_save, L"r") : NULL;
    if (!f) { Log("nothing saved yet (%s): no mods found", g_save); return; }
    char line[512];
    while (fgets(line, sizeof line, f))
    {
        char* t = Trim(line);
        if (*t == ';' || !*t) continue;
        char* eq = strchr(t, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = Trim(t), *v = Trim(eq + 1);
        if (!strncmp(k, "copies.", 7))
        {
            int m = ModByKey(k + 7);
            if (m >= 0) g_copies[m] = atoi(v) < 0 ? 0 : atoi(v) > 9999 ? 9999 : atoi(v);
        }
        else if (!strncmp(k, "kit.", 4))
        {
            Loadout* l = KitOf(k + 4, 1);
            for (int s = 0; l && s < SLOTS; s++)
            {
                char* comma = strchr(v, ',');
                if (comma) *comma = 0;
                int m = ModByKey(Trim(v));
                l->slot[s] = (int8_t)(m >= 0 && g_copies[m] > 0 ? m : -1);
                for (int o = 0; o < s; o++) if (l->slot[o] == l->slot[s]) l->slot[s] = -1;   /* one of each a kit */
                if (!comma) break;
                v = comma + 1;
            }
        }
    }
    fclose(f);
    Log("loaded %s: %d %s, %d %s, %d %s, %d %s; %d kit%s fitted", g_save, g_copies[0], kMods[0].name, g_copies[1], kMods[1].name, g_copies[2], kMods[2].name,
        g_copies[3], kMods[3].name, g_nkit, g_nkit == 1 ? "" : "s");
}
static void Save(void)
{
    if (!g_save[0]) return;
    char dir[MAX_PATH * 2];
    snprintf(dir, sizeof dir, "%s", g_save);
    char* cut = strrchr(dir, '\\');
    if (cut) { *cut = 0; wchar_t w[MAX_PATH * 2]; if (MultiByteToWideChar(CP_UTF8, 0, dir, -1, w, MAX_PATH * 2)) CreateDirectoryW(w, NULL); }
    AcquireSRWLockShared(&g_lock);
    FILE* o = OpenUtf8(g_save, L"w");
    if (!o) { ReleaseSRWLockShared(&g_lock); Log("could not save to %s", g_save); return; }
    fprintf(o, "; Shared Mods %s: the mods found (copies; rank r needs 1 + r(r+1)/2) and each kit's four slots.\n"
               "; Written by the mod at a mission's end and when the options screen closes. Delete it to start over.\n", VERSION);
    for (int m = 0; m < M_N; m++) fprintf(o, "copies.%s = %d\n", kMods[m].key, g_copies[m]);
    for (int i = 0; i < g_nkit; i++)
    {
        fprintf(o, "kit.%s = ", g_kit[i].id);
        for (int s = 0; s < SLOTS; s++) fprintf(o, "%s%s", s ? "," : "", g_kit[i].slot[s] >= 0 ? kMods[g_kit[i].slot[s]].key : "-");
        fprintf(o, "\n");
    }
    fclose(o);
    ReleaseSRWLockShared(&g_lock);
}
static void LoadSettings(void)
{
    char path[MAX_PATH * 2];
    snprintf(path, sizeof path, "%s\\shared-mods.ini", hf->mod_folder(self));
    FILE* f = OpenUtf8(path, L"r");
    if (!f) { Log("shared-mods.ini not found: the built-in settings"); return; }
    char line[512];
    while (fgets(line, sizeof line, f))
    {
        char* t = Trim(line);
        if (*t == ';' || !*t) continue;
        char* eq = strchr(t, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = Trim(t), *v = Trim(eq + 1);
        double d = atof(v);
        if (!strcmp(k, "drop_chance") && d >= 0 && d <= 1) s_dropChance = (float)d;
        else if (!strcmp(k, "heavy_chance") && d >= 0 && d <= 1) s_heavyChance = (float)d;
        else if (!strcmp(k, "heavy_health") && d >= 1) s_heavyHealth = (float)d;
        else if (!strcmp(k, "capacity") && d >= 1 && d <= 200) s_capacity = (int)d;
        else if (!strcmp(k, "odds") || !strcmp(k, "heavy_odds"))
        {
            float c = 0, u = 0, r = 0;
            if (sscanf(v, "%f , %f , %f", &c, &u, &r) == 3 && c >= 0 && u >= 0 && r >= 0 && c + u + r > 0)
            {
                float* o = s_odds[k[0] == 'h'];
                o[0] = c / (c + u + r); o[1] = u / (c + u + r); o[2] = r / (c + u + r);
            }
        }
    }
    fclose(f);
}

/* ---- the drop notice (a screen of its own, no key: opened for a few seconds when a mod drops) ----------------------- */
static int g_notice;                /* its screen */
static struct { char title[64], line[96]; int rarity; double t; } g_note;
static SRWLOCK g_noteLock = SRWLOCK_INIT;
static const HfColour kRarityRgb[3] = {HF_RGBA(225, 228, 230, 255), HF_RGBA(90, 170, 255, 255), HF_RGBA(255, 150, 40, 255)};
static HfColour Fade(HfColour c, float a) { return (c & 0x00FFFFFFu) | ((uint32_t)(((c >> 24) & 255) * a) << 24); }
static int NoticeDraw(void* user, const HfDraw* d, const HfInput* in, float dt)
{
    if (in->closing) return 0;
    AcquireSRWLockExclusive(&g_noteLock);
    g_note.t += dt;
    double t = g_note.t;
    char title[64], line[96];
    memcpy(title, g_note.title, sizeof title); memcpy(line, g_note.line, sizeof line);
    int r = g_note.rarity;
    ReleaseSRWLockExclusive(&g_noteLock);
    if (t > 5) { hf->screen_show(self, g_notice, 0); return 0; }
    float a = t < 0.2 ? (float)(t / 0.2) : t > 4.4 ? (float)((5 - t) / 0.6) : 1;
    const float w = 460, x1 = d->width - 40, x0 = x1 - w, y0 = 250, y1 = y0 + 84;
    d->rect(x0, y0, x1, y1, HF_RGBA(14, 16, 18, (uint32_t)(225 * a)), Fade(kRarityRgb[r], a), 2);
    d->rect(x0, y0, x0 + 8, y1, Fade(kRarityRgb[r], a), 0, 0);
    d->text(x0 + 24, y0 + 26, title, 24, HF_FACE_TITLE, HF_ALIGN_LEFT, Fade(kRarityRgb[r], a));
    d->text(x0 + 24, y0 + 58, line, 19, HF_FACE_BODY, HF_ALIGN_LEFT, HF_RGBA(240, 240, 240, (uint32_t)(255 * a)));
    return 1;
}
static void Notice(int rarity, const char* title, const char* line)
{
    AcquireSRWLockExclusive(&g_noteLock);
    snprintf(g_note.title, sizeof g_note.title, "%s", title);
    snprintf(g_note.line, sizeof g_note.line, "%s", line);
    g_note.rarity = rarity;
    g_note.t = 0;
    ReleaseSRWLockExclusive(&g_noteLock);
    if (g_notice) hf->screen_show(self, g_notice, 1);
}

/* ---- the mission --------------------------------------------------------------------------------------------------- */
static uint64_t g_peer;             /* this machine's player */
static atomic_uint g_avatar;        /* its Helldiver */
static int g_inMission;
static float g_taken = 1;           /* the acting kit's Resistance, this mission */
static char g_acting[32];
static unsigned g_kills, g_heavies, g_resisted;
static double g_resistedDamage;
static int g_settled;               /* this mission's carried mods were kept or lost */
static int g_endWait;               /* game updates left to wait for the exit hook after a mission */
static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static double Random(void)
{
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17;
    return (double)(g_rng >> 11) / 9007199254740992.0;
}

static float MaxHealth(uint64_t res)
{
    uint32_t stride = 0;
    uintptr_t h = res ? hf->component_record("HealthComponentData", res, &stride) : 0;
    int32_t hp = 0;
    return h && ReadAt(h, &hp, 4) && hp >= 1 && hp < 10000000 ? (float)hp : 0;
}

/* the kit acting this mission: the picked one, else the first by id; none with the Off picked */
static void ActingKit(char out[32])
{
    out[0] = 0;
    KbRoster* r = Roster();
    if (!r || r->magic != KB_ROSTER_MAGIC || !r->count || !RosterLock()) return;
    int picked = -1;
    for (uint32_t i = 0; i < r->count && i < KB_ROSTER_MAX; i++) if (r->active[0] && !strncmp(r->id[i], r->active, 31)) picked = (int)i;
    if (picked < 0) picked = 0;
    snprintf(out, 32, "%s", r->id[picked]);
    RosterUnlock();
    if (!strcmp(out, "~off")) out[0] = 0;
}

static void Settle(int keep, const char* why)
{
    if (g_settled) return;
    g_settled = 1;
    int carried = 0;
    char list[160] = "";
    AcquireSRWLockExclusive(&g_lock);
    for (int m = 0; m < M_N; m++)
    {
        if (!g_pending[m]) continue;
        carried += g_pending[m];
        size_t n = strlen(list);
        snprintf(list + n, sizeof list - n, "%s%d %s", n ? ", " : "", g_pending[m], kMods[m].name);
        if (keep) g_copies[m] += g_pending[m];
        g_pending[m] = 0;
    }
    if (keep && carried) Publish();   /* a rank up changes what a fitted mod gives */
    ReleaseSRWLockExclusive(&g_lock);
    if (keep) g_kept += (unsigned)carried; else g_lost += (unsigned)carried;
    Log("mission over (%s): %s%s%s; %u kills credited here (%u heavies)", why, carried ? (keep ? "kept " : "lost ") : "no mods carried", list, carried ? "" : "",
        g_kills, g_heavies);
    if (carried) atomic_store(&g_dirty, 1);
    if (carried) Notice(keep ? R_UNCOMMON : R_COMMON, keep ? "MODS EXTRACTED" : "MODS LOST", list);
}

/* the game leaving a mission: StateGame on_exit (game.dll+0xACDF50; the state is rcx). Its +0x12 and +0x13 as the game's
 * mission-end telemetry reads them (+0xACE854): +0x12 0 "abandoned"; else +0x13 0 "failed", other "partial". */
static void (*OnExitReal)(void* state, void* b);
static void OnExitHook(void* state, void* b)
{
    uint8_t flags[2] = {0, 0};
    int read = state && ReadAt((uintptr_t)state + 0x12, flags, 2);
    if (g_inMission || !g_settled)
    {
        const char* why = !read ? "the game's result could not be read" : !flags[0] ? "abandoned or left" : !flags[1] ? "failed" : "a result";
        Log("the game left the mission: +0x12 %u, +0x13 %u (%s)", flags[0], flags[1], why);
        Settle(read && flags[0] && flags[1], why);
    }
    OnExitReal(state, b);
}

static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfKillEvent* k = (const HfKillEvent*)info;
    if (!g_inMission || !g_peer || k->creditor != g_peer) return;
    uint8_t vd[24];
    if (!Descriptor(k->victim, vd)) return;
    uint64_t res = Quad(vd);
    if (res == AVATAR) return;                       /* a Helldiver */
    float hp = MaxHealth(res);
    if (hp <= 0) return;                             /* not something with health of its own (a part, a prop) */
    int heavy = hp >= s_heavyHealth;
    g_kills++;
    g_heavies += (unsigned)heavy;
    if (Random() >= (heavy ? s_heavyChance : s_dropChance)) return;
    const float* odds = s_odds[heavy];
    double roll = Random();
    int rarity = roll < odds[0] ? R_COMMON : roll < odds[0] + odds[1] ? R_UNCOMMON : R_RARE;
    int pool[M_N], n = 0;
    for (int m = 0; m < M_N; m++) if (kMods[m].rarity == rarity) pool[n++] = m;
    if (!n) return;
    int m = pool[(int)(Random() * n) % n];
    g_found[rarity]++;
    char title[64], line[96];
    AcquireSRWLockExclusive(&g_lock);
    int before = RankOf(g_copies[m] + g_pending[m]);
    if (rarity == R_RARE) g_copies[m]++; else g_pending[m]++;
    int after = RankOf(g_copies[m] + g_pending[m]);
    if (rarity == R_RARE) Publish();
    ReleaseSRWLockExclusive(&g_lock);
    snprintf(title, sizeof title, "%s MOD: %s", kRarity[rarity], kMods[m].name);
    if (rarity == R_RARE) snprintf(line, sizeof line, after > before && before >= 0 ? "Kept. Rank %d now." : "Kept.", after);
    else snprintf(line, sizeof line, after > before && before >= 0 ? "Extract to keep it (rank %d then)." : "Extract to keep it.", after);
    Notice(rarity, title, line);
    Log("%s dropped %s (%s, from %s %016llx, max health %.0f); %s", heavy ? "a heavy" : "a kill", kMods[m].name, kRarity[rarity], heavy ? "heavy" : "enemy",
        (unsigned long long)res, (double)hp, rarity == R_RARE ? "kept at once" : "carried until the mission ends");
    if (rarity == R_RARE) atomic_store(&g_dirty, 1);
}

static void OnPlayer(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    if (!p->local) return;
    if (p->peer && p->peer != g_peer) { g_peer = p->peer; Log("this machine's player: peer %016llx", (unsigned long long)g_peer); }
    if (p->entity) atomic_store(&g_avatar, p->entity);
}

static void OnDamagePre(HfMod* mod, const HfEventInfo* info, void* user)
{
    HfDamagePreEvent* e = (HfDamagePreEvent*)(void*)info;
    HfDamageEvent* d = &e->damage;
    if (!g_inMission || e->cancel || g_taken >= 1 || d->amount <= 0) return;
    uint32_t me = atomic_load(&g_avatar);
    if (!me || d->target != me) return;
    double a = floor(d->amount * (double)g_taken + 0.5);
    g_resisted++;
    g_resistedDamage += d->amount - a;
    d->amount = a < 1 ? 1 : (int32_t)a;
}

static void MissionStart(void)
{
    g_kills = g_heavies = g_resisted = 0;
    g_resistedDamage = 0;
    g_settled = 0;
    memset(g_found, 0, sizeof g_found);
    ActingKit(g_acting);
    KbSmBonus b;
    SharedMods(g_acting, &b);
    g_taken = b.taken;
    AcquireSRWLockShared(&g_lock);
    Loadout* l = KitOf(g_acting, 0);
    char fit[128] = "";
    for (int s = 0; l && s < SLOTS; s++)
        if (l->slot[s] >= 0)
        {
            size_t n = strlen(fit);
            snprintf(fit + n, sizeof fit - n, "%s%s %d", n ? ", " : "", kMods[l->slot[s]].name, RankOf(g_copies[l->slot[s]]));
        }
    ReleaseSRWLockShared(&g_lock);
    Log("mission start: %s acts, fitted: %s (cooldowns x%.2f, skill damage x%.2f, durations x%.2f, damage taken x%.2f)", g_acting[0] ? g_acting : "no kit",
        fit[0] ? fit : "nothing", (double)b.cooldown, (double)b.damage, (double)b.duration, (double)b.taken);
}

/* ---- the SHARED MODS tab (the kits' options screen, kb_extras.h; on the host's render thread) ---------------------- */
static int g_sel = -1;              /* the slot chosen (-1: the first empty one takes a pick) */
static LONG g_seenOpens = -1, g_seenCloses = -1;
static double g_msgUntil, g_clock;
static char g_msg[120];
static int In(const HfInput* in, float x0, float y0, float x1, float y1) { return in->x >= x0 && in->x < x1 && in->y >= y0 && in->y < y1; }
static void Say(const char* m) { snprintf(g_msg, sizeof g_msg, "%s", m); g_msgUntil = g_clock + 3; }
static void Pips(const HfDraw* d, float x, float y, int rank, HfColour on, HfColour off)
{
    for (int i = 0; i < RANK_MAX + 1; i++) d->rect(x + i * 14, y - 4, x + i * 14 + 9, y + 4, i <= rank ? on : off, 0, 0);
}
static void Body(const HfDraw* d, const HfInput* in, float dt, const KbOpBox* box, const char* kit, const char* kitName)
{
    KbExtras* x = kb_extras;
    if (x && x->opens != g_seenOpens) { g_seenOpens = x->opens; g_sel = -1; g_msgUntil = 0; }
    g_clock += dt;
    const float a = box->alpha;
    const uint32_t A = (uint32_t)(a * 255);
    const HfColour yellow = HF_RGBA(255, 232, 0, A), white = HF_RGBA(240, 240, 240, A), grey = HF_RGBA(150, 154, 158, A), ink = HF_RGBA(14, 16, 18, A),
                   dim = HF_RGBA(60, 64, 68, A), red = HF_RGBA(220, 70, 60, A);
    HfColour rar[3];
    for (int i = 0; i < 3; i++) rar[i] = Fade(kRarityRgb[i], a);
    const int click = (in->pressed & HF_MOUSE_LEFT) != 0, rclick = (in->pressed & HF_MOUSE_RIGHT) != 0;
    const float x0 = box->x0, y0 = box->y0, x1 = box->x1, y1 = box->y1, W = x1 - x0;
    const char* tip = box->tip;
    d->rect(x0, y0, x1, y1, HF_RGBA(14, 16, 18, (uint32_t)(238 * a)), yellow, 2);
    d->rect(x0, y0, x1, y0 + 64, yellow, 0, 0);
    d->text(x0 + 28, y0 + 32, "SHARED MODS", 34, HF_FACE_TITLE, HF_ALIGN_LEFT, ink);

    AcquireSRWLockExclusive(&g_lock);
    Loadout* l = kit[0] ? KitOf(kit, 0) : NULL;
    int used = Used(l), changed = 0;
    char head[96];
    if (kit[0]) snprintf(head, sizeof head, "%s  |  CAPACITY %d / %d", kitName[0] ? kitName : kit, used, s_capacity);
    else snprintf(head, sizeof head, "NO KIT PICKED");
    d->text(x1 - 28, y0 + 32, head, 22, HF_FACE_TITLE, HF_ALIGN_RIGHT, used > s_capacity ? red : ink);

    /* her four slots */
    float sy0 = y0 + 92, sh = 150, gap = 14, sw = (W - 56 - gap * (SLOTS - 1)) / SLOTS;
    d->text(x0 + 28, sy0 + 2, kit[0] ? "FITTED (this kit's four)" : "Pick a character in the column to fit its mods.", 20, HF_FACE_TITLE, HF_ALIGN_LEFT, yellow);
    sy0 += 22;
    for (int s = 0; s < SLOTS && kit[0]; s++)
    {
        float cx0 = x0 + 28 + s * (sw + gap), cx1 = cx0 + sw, cy1 = sy0 + sh;
        int m = l ? l->slot[s] : -1, over = In(in, cx0, sy0, cx1, cy1), sel = g_sel == s;
        d->rect(cx0, sy0, cx1, cy1, HF_RGBA(24, 26, 28, A), sel ? yellow : over ? white : m >= 0 ? rar[kMods[m].rarity] : dim, sel ? 3 : 2);
        if (m >= 0)
        {
            int rank = RankOf(g_copies[m]);
            char e[48], c[24];
            snprintf(e, sizeof e, kMods[m].what, (double)(Effect(m, rank) * 100));
            snprintf(c, sizeof c, "%d", Cost(m, rank));
            d->rect(cx0, sy0, cx1, sy0 + 6, rar[kMods[m].rarity], 0, 0);
            d->text(cx0 + 16, sy0 + 34, kMods[m].name, 24, HF_FACE_TITLE, HF_ALIGN_LEFT, white);
            d->text(cx1 - 16, sy0 + 34, c, 22, HF_FACE_TITLE, HF_ALIGN_RIGHT, grey);
            d->text(cx0 + 16, sy0 + 76, e, 19, HF_FACE_BODY, HF_ALIGN_LEFT, white);
            Pips(d, cx0 + 16, sy0 + 112, rank, rar[kMods[m].rarity], dim);
        }
        else d->text((cx0 + cx1) / 2, (sy0 + cy1) / 2, sel ? "PICK A MOD BELOW" : "EMPTY", 20, HF_FACE_TITLE, HF_ALIGN_CENTRE, sel ? yellow : grey);
        if (over)
        {
            tip = m >= 0 ? "Click: choose this slot (a mod picked below goes here). Right click: take the mod out."
                         : "Click: choose this slot, then pick a mod below.";
            if (click) g_sel = sel ? -1 : s;
            if (rclick && l && m >= 0) { l->slot[s] = -1; changed = 1; }
        }
    }

    /* every mod: found (its copies and rank), carried this mission, or not found yet */
    float gy0 = sy0 + sh + 40;
    d->text(x0 + 28, gy0, "FOUND", 20, HF_FACE_TITLE, HF_ALIGN_LEFT, yellow);
    d->rect(x0 + 28, gy0 + 16, x1 - 28, gy0 + 18, HF_RGBA(255, 232, 0, (uint32_t)(70 * a)), 0, 0);
    gy0 += 34;
    const int cols = W > 1000 ? 4 : 3;
    float gw = (W - 56 - gap * (cols - 1)) / cols, gh = 168;
    for (int m = 0; m < M_N; m++)
    {
        float cx0 = x0 + 28 + (m % cols) * (gw + gap), cy0 = gy0 + (m / cols) * (gh + gap), cx1 = cx0 + gw, cy1 = cy0 + gh;
        if (cy1 > y1 - 70) break;
        int copies = g_copies[m], rank = RankOf(copies), have = copies > 0, over = In(in, cx0, cy0, cx1, cy1);
        int fittedAt = -1;
        for (int s = 0; l && s < SLOTS; s++) if (l->slot[s] == m) fittedAt = s;
        HfColour rc = rar[kMods[m].rarity];
        d->rect(cx0, cy0, cx1, cy1, HF_RGBA(24, 26, 28, A), over && have ? white : fittedAt >= 0 ? yellow : have ? rc : dim, fittedAt >= 0 ? 3 : 2);
        d->rect(cx0, cy0, cx1, cy0 + 6, have ? rc : dim, 0, 0);
        d->text(cx0 + 16, cy0 + 30, kRarity[kMods[m].rarity], 16, HF_FACE_TITLE, HF_ALIGN_LEFT, have ? rc : grey);
        if (fittedAt >= 0) d->text(cx1 - 16, cy0 + 30, "FITTED", 16, HF_FACE_TITLE, HF_ALIGN_RIGHT, yellow);
        d->text(cx0 + 16, cy0 + 58, kMods[m].name, 24, HF_FACE_TITLE, HF_ALIGN_LEFT, have ? white : grey);
        char e[64], p[64];
        snprintf(e, sizeof e, kMods[m].what, (double)(Effect(m, rank < 0 ? 0 : rank) * 100));
        d->text(cx0 + 16, cy0 + 90, e, 19, HF_FACE_BODY, HF_ALIGN_LEFT, have ? white : grey);
        if (have)
        {
            Pips(d, cx0 + 16, cy0 + 118, rank, rc, dim);
            if (rank < RANK_MAX) snprintf(p, sizeof p, "%d / %d copies to rank %d", copies, CopiesFor(rank + 1), rank + 1);
            else snprintf(p, sizeof p, "max rank (%d copies)", copies);
            d->text(cx0 + 110, cy0 + 118, p, 16, HF_FACE_BODY, HF_ALIGN_LEFT, grey);
        }
        else d->text(cx0 + 16, cy0 + 118, "not found yet", 16, HF_FACE_BODY, HF_ALIGN_LEFT, grey);
        if (g_pending[m])
        {
            snprintf(p, sizeof p, "+%d carried: extract to keep", g_pending[m]);
            d->text(cx0 + 16, cy0 + 146, p, 16, HF_FACE_BODY, HF_ALIGN_LEFT, yellow);
        }
        else
        {
            snprintf(p, sizeof p, "costs %d", Cost(m, rank < 0 ? 0 : rank));
            d->text(cx0 + 16, cy0 + 146, p, 16, HF_FACE_BODY, HF_ALIGN_LEFT, grey);
        }
        if (!over) continue;
        tip = !have ? (kMods[m].rarity == R_RARE ? "A rare drop from kills (heavies drop more): kept as soon as it drops."
                                                 : "A drop from kills (heavies drop more): carried until the mission ends, kept if it ends well.")
            : fittedAt >= 0 ? "Fitted to this kit. Click: take it out. A copy found again ranks it up."
                            : "Click: fit it to this kit (the slot chosen above, else the first empty one).";
        if (!(click || rclick) || !have || !kit[0]) continue;
        l = KitOf(kit, 1);
        if (!l) { Say("No room left for another kit's slots."); continue; }
        if (fittedAt >= 0) { l->slot[fittedAt] = -1; changed = 1; continue; }
        if (rclick) continue;
        int s = g_sel >= 0 ? g_sel : -1;
        for (int t = 0; s < 0 && t < SLOTS; t++) if (l->slot[t] < 0) s = t;
        if (s < 0) { Say("All four slots are full: choose one above to swap, or right click one to empty it."); continue; }
        int old = l->slot[s];
        l->slot[s] = (int8_t)m;
        if (Used(l) > s_capacity) { l->slot[s] = (int8_t)old; Say("Not enough capacity for it on this kit."); continue; }
        g_sel = -1;
        changed = 1;
    }
    if (changed) { Publish(); atomic_store(&g_dirty, 1); }
    ReleaseSRWLockExclusive(&g_lock);

    /* the footer */
    if (g_msgUntil > g_clock) tip = g_msg;
    d->rect(x0 + 2, y1 - 66, x1 - 2, y1 - 64, HF_RGBA(255, 232, 0, (uint32_t)(70 * a)), 0, 0);
    if (tip) d->text(x0 + 28, y1 - 42, tip, 19, HF_FACE_BODY, HF_ALIGN_LEFT, g_msgUntil > g_clock ? yellow : white);
    d->text(x0 + 28, y1 - 18, "Applies from the next mission  |  Click a mod to fit or take it out  |  Esc: close and save", 17, HF_FACE_BODY, HF_ALIGN_LEFT, grey);
}

/* ---- the frame ----------------------------------------------------------------------------------------------------- */
static void Tick(HfMod* mod, float dt, void* user)
{
    int inMission = hf->game_state(self) == HF_STATE_MISSION;
    if (inMission != g_inMission)
    {
        g_inMission = inMission;
        if (inMission) MissionStart();
        else
        {
            g_endWait = 600;   /* the exit hook may come a little after the state changed */
            if (g_resisted) Log("Resistance: %u hits on her, %.0f damage kept off", g_resisted, g_resistedDamage);
            Log("this mission: %u common, %u uncommon, %u rare dropped; %u kept, %u lost so far this session", g_found[0], g_found[1], g_found[2], g_kept, g_lost);
        }
    }
    /* the exit hook settles; without it (or a mission it missed) carried mods are kept, and the log says so */
    if (g_endWait > 0 && (g_settled || --g_endWait == 0))
    {
        g_endWait = 0;
        if (!g_settled) Settle(1, "left the mission; the game's result was not seen, so kept");
    }
    KbExtras* x = kb_extras;
    if (x && x->closes != g_seenCloses) { if (g_seenCloses >= 0) atomic_store(&g_dirty, 1); g_seenCloses = x->closes; }
    if (atomic_exchange(&g_dirty, 0)) Save();
}

static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gameSize = 0;
    uintptr_t g = hf->module_base("game.dll", &gameSize);
    if (!g || gameSize != 74727424u) { Log("REFUSED: game.dll is not build " BUILD "'s: no drops this session (fitting still works)"); return; }
    game = g;
    exe = hf->module_base("helldivers2.exe", NULL);
    const uintptr_t at = game + 0xACDF50;
    if (hf->size >= offsetof(HfApi, code_matches) + sizeof hf->code_matches &&
        hf->code_matches(at, "48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 41 54 41 55 41 56 41 57 48 8D A8 18 FF FF FF") &&
        !hf->hook(self, at, (void*)OnExitHook, (void**)&OnExitReal))
        Log("the mission's end is read from the game's own exit (StateGame on_exit)");
    else Log("the mission's end can't be read (StateGame on_exit is not the reviewed code): carried mods are kept at every end");
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (api->version < 17 || api->size < offsetof(HfApi, screen_show) + sizeof api->screen_show)
    {
        api->log(mod, "Shared Mods needs HellForge's screens (SDK 17): off");
        return 1;
    }
    g_rng ^= (uint64_t)GetTickCount64() * 0x2545F4914F6CDD1Dull;
    LoadSettings();
    const char* local = getenv("LOCALAPPDATA");
    if (local) snprintf(g_save, sizeof g_save, "%s\\HellForgeMods\\shared-mods.ini", local);
    Load();
    AcquireSRWLockExclusive(&g_lock);
    Publish();
    ReleaseSRWLockExclusive(&g_lock);
    if (!KbExtraAdd("shared-mods", "SHARED MODS", Body)) api->log(mod, "the options screen's tabs are unavailable: mods can't be fitted this session");
    if (Extras()) g_seenCloses = kb_extras->closes;
    HfScreen notice = {sizeof notice, "Shared Mods: a drop", 0, 0, NULL, NoticeDraw, NULL, NULL};
    g_notice = api->add_screen(mod, &notice);
    if (!g_notice) api->log(mod, "the drop notice can't be shown (modloader.log says why); drops are still logged");
    api->log(mod, "Shared Mods %s: drops %.1f%% a kill, %.1f%% a heavy (%.0f health and up); capacity %d; save %s", VERSION, (double)(s_dropChance * 100),
             (double)(s_heavyChance * 100), (double)s_heavyHealth, s_capacity, g_save);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) api->log(mod, "no game-loaded event: no drops");
    int ok = api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, -10) && api->on_game_thread(mod, Tick, NULL);
    if (!ok) api->log(mod, "events or the game thread are unavailable: no drops");
    return 0;
}
