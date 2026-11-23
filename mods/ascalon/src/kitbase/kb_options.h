/* kitbase options: the character kits' options screen, drawn over the game by HellForge's overlay host (SDK 17:
 * add_screen; docs/modding/overlay.md). One screen for every installed kit: a kit's numbers at the left (the names of its
 * .ini; the Mod Options Menu's rows), the characters at the right with their mod icons and an Off.
 *
 * Code shared by the character kits. Source of truth: kitbase/ at the repo root. The copy in a kit's src/kitbase/ is
 * written by tools/kitbase_sync.py, so edit kitbase/, never the copy. See docs/kit-base.md.
 * Include after kb_roster.h and kb_settings.h. (Its files are written through "o", not "f": tools/inigen.py takes a kit's
 * first fprintf to f of a line starting with a semicolon for its default .ini.)
 *
 * A kit:  #include "kit_options.h" (tools/optgen.py: kOpRows from its HUD's OPTIONS, kOpIcon from its icon.png)
 *         KbOptionsStart(api, mod, "lenna", "Lenna", VERSION, kOpRows, n, kOpIcon, KB_OP_ICON, 0xRRGGBB, key) in
 *           hellforge_mod_init after LoadSettings();
 *         KbOptionsTick() every game-thread tick (before any "not in a mission" return);
 *         KB_OPTIONS_EXPORTS(lenna) once, for its HUD (lenna_options_screen: 1 keeps its rows off the Mod Options Menu).
 *
 * How the kits share one screen: each joins a hub, a second block of named shared memory (Local\yui.kitbase.options.<pid>)
 * holding, for every kit, its icon and the function that draws its rows. The first kit HellForge lets add a screen owns
 * it (the key, the frame, the character column) and calls the shown kit's function for the rest; the pointers are good
 * in every kit DLL, all being in the game's process. A kit without this header is still in the column (from the
 * roster), without an icon or rows.
 *
 * Threads: the screen is drawn on the host's render thread and never calls the game. A changed value waits in g_opWant
 * and a pick in g_opPickWant; KbOptionsTick (the game thread) sets them. What differs from the kit's .ini is saved to
 * %LOCALAPPDATA%\HellForgeMods\<kit>.ini when the screen closes (HellForge keeps mod folders read-only while the game
 * runs) and read after the kit's .ini at the next start; the picked character and the key to HellForgeMods\kits.ini.
 *
 * On a HellForge without screens (or an SDK without them) nothing is added and the Mod Options Menu stays. */
#ifndef KB_OPTIONS_H
#define KB_OPTIONS_H

#include "kb_roster.h"
#include "kb_settings.h"

/* a row's control: a slider (few steps), a switch (0 or 1), a field to type in with - and + (a wide range: its limits
 * are the setting's own in the .ini, not the slider's), or one button a choice (choices: "A|B|C" for 0, 1, 2) */
enum { OP_SLIDER, OP_TOGGLE, OP_FIELD, OP_CHOICE };
/* adv: in the Advanced section (collapsed until opened); when: OP_LIVE, or when a change takes effect (shown beside it) */
enum { OP_LIVE, OP_NEXT_MISSION, OP_RESTART };
typedef struct OpRow { const char* name; const char* group; const char* label; int kind; float lo, hi, step; const char* tip; const char* choices; int adv, when; } OpRow;
#define KB_OP_ICON 96                  /* a kit's icon: KB_OP_ICON x KB_OP_ICON RGBA8 */
#define KB_OP_MAX 192                  /* most rows a kit shows */
#define KB_OP_OFF_ID "~off"

KB_VAR int g_opOn;                     /* this kit's rows are in the hub */
KB_FN int KbOptionsOn(void);           /* and a screen shows them: its HUD then leaves its rows off the Mod Options Menu */
#define KB_OPTIONS_EXPORTS(kit) HF_MOD_EXPORT int kit##_options_screen(unsigned version) { return version == 1 && KbOptionsOn(); }

#ifndef HF_SCREEN_MODAL
KB_FN void KbOptionsStart(const HfApi* api, HfMod* mod, const char* id, const char* name, const char* version, const OpRow* rows, int nrows,
                          const unsigned char* icon, int iconSize, unsigned accent, int key)
{
    api->log(mod, "options screen: built without screens over the game (SDK 17); the numbers stay on the MODS tab");
}
KB_FN void KbOptionsTick(void) {}
KB_FN int KbOptionsOn(void) { return 0; }
#else

typedef struct KbOpBox { float x0, y0, x1, y1, alpha; const char* tip; } KbOpBox;
typedef struct KbOpKit
{
    char id[32];
    const unsigned char* icon;         /* KB_OP_ICON squared RGBA8, in that kit's DLL; NULL none */
    void (*body)(const HfDraw* d, const HfInput* in, float dt, const KbOpBox* box);
} KbOpKit;
#define KB_OPHUB_MAGIC 0x4855504Fu
typedef struct KbOpHub
{
    uint32_t magic, version, count;
    uint32_t screen;                   /* a kit owns the screen */
    volatile LONG opens, closes;       /* counted by the owner; every kit saves at a close */
    KbOpKit kit[KB_ROSTER_MAX];
} KbOpHub;
KB_VAR KbOpHub* kb_ophub;
KB_FN KbOpHub* OpHub(void)
{
    if (kb_ophub) return kb_ophub;
    wchar_t name[64];
    _snwprintf(name, 64, L"Local\\yui.kitbase.options.%lu", (unsigned long)GetCurrentProcessId());
    HANDLE m = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(KbOpHub), name);
    if (!m) return NULL;
    kb_ophub = (KbOpHub*)MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(KbOpHub));
    return kb_ophub;
}

KB_FN int KbOptionsOn(void) { return g_opOn && kb_ophub && kb_ophub->screen; }

KB_VAR const OpRow* kb_op_rows;
KB_VAR int kb_op_n;
KB_VAR const char* kb_op_id;
KB_VAR char kb_op_title[32], kb_op_version[32];
KB_VAR unsigned kb_op_accent = 0xFFE800;
KB_VAR int g_opOwner, g_opScreen;      /* this kit owns the screen; its id */
KB_VAR double g_opIni[KB_OP_MAX];      /* each row's value from the .ini (or its built-in default): what "reset" means */
KB_VAR double g_opLo[KB_OP_MAX], g_opHi[KB_OP_MAX];
KB_VAR _Atomic double g_opWant[KB_OP_MAX];
KB_VAR atomic_uint g_opDirty[KB_OP_MAX];
KB_VAR LONG g_opCloses, g_opOpens;
KB_VAR char g_opPath[MAX_PATH * 2], g_opKitsPath[MAX_PATH * 2];
KB_VAR int g_opKey = 120, g_opKeyFile; /* F9; kits.ini's options_key when it has one */

KB_FN double OpNow(int i) { double v = 0; OptionGet(kb_op_rows[i].name, &v); return v; }
KB_FN double OpSnap(int i, double v)
{
    const OpRow* r = &kb_op_rows[i];
    if (r->kind != OP_FIELD && r->step > 0) v = r->lo + floor((v - r->lo) / r->step + 0.5) * r->step;
    if (r->kind == OP_FIELD) v = floor(v * 100 + 0.5) / 100;
    return v < g_opLo[i] ? g_opLo[i] : v > g_opHi[i] ? g_opHi[i] : v;
}
KB_FN void OpAsk(int i, double v)
{
    atomic_store(&g_opWant[i], OpSnap(i, v));
    atomic_store(&g_opDirty[i], 1);
}
/* what the screen shows: a value asked for and not set yet, else the live one */
KB_FN double OpShown(int i) { return atomic_load(&g_opDirty[i]) ? atomic_load(&g_opWant[i]) : OpNow(i); }

/* ---- the characters --------------------------------------------------------------------------------------------- */
/* "Off" is a kit of its own in the shared roster ("~off": last by id, so it is never the one that acts unpicked). With
 * it picked, no real kit is the picked one, so every kit stands by: kits without this header need no change for it. */
KB_FN void RosterAddOff(void)
{
    KbRoster* r = Roster();
    if (!r || !RosterLock()) return;
    if (r->magic == KB_ROSTER_MAGIC && r->count < KB_ROSTER_MAX)
    {
        int known = 0;
        for (uint32_t i = 0; i < r->count; i++) if (!strncmp(r->id[i], KB_OP_OFF_ID, 31)) known = 1;
        if (!known) { snprintf(r->id[r->count], 32, "%s", KB_OP_OFF_ID); snprintf(r->name[r->count], 32, "Off"); r->count++; }
    }
    RosterUnlock();
}
/* a kit that joins after the Off went in belongs before it (the roster is sorted by id): seen to on the game thread */
KB_FN void RosterOffLast(void)
{
    KbRoster* r = kb_roster;
    if (!r || r->magic != KB_ROSTER_MAGIC || !RosterLock()) return;
    for (uint32_t i = 0; i + 1 < r->count; i++)
        if (!strncmp(r->id[i], KB_OP_OFF_ID, 31))
        {
            char id[32], name[32];
            memcpy(id, r->id[i], 32); memcpy(name, r->name[i], 32);
            memmove(r->id[i], r->id[i + 1], 32 * (r->count - 1 - i)); memmove(r->name[i], r->name[i + 1], 32 * (r->count - 1 - i));
            memcpy(r->id[r->count - 1], id, 32); memcpy(r->name[r->count - 1], name, 32);
            break;
        }
    RosterUnlock();
}
/* the render thread's copy of the roster, each with its hub entry (-1: a kit without the screen) */
KB_VAR struct { int n, active; char id[KB_ROSTER_MAX][32], name[KB_ROSTER_MAX][32]; int hub[KB_ROSTER_MAX]; KbOpKit kit[KB_ROSTER_MAX]; int nkit; } g_opKits;
KB_FN void OpKitsRead(void)
{
    KbRoster* r = kb_roster;
    KbOpHub* h = kb_ophub;
    g_opKits.n = 0;
    if (!r || r->magic != KB_ROSTER_MAGIC || !RosterLock()) return;
    int n = (int)r->count > KB_ROSTER_MAX ? KB_ROSTER_MAX : (int)r->count;
    g_opKits.active = 0;
    g_opKits.nkit = h && h->magic == KB_OPHUB_MAGIC ? (int)(h->count > KB_ROSTER_MAX ? KB_ROSTER_MAX : h->count) : 0;
    for (int k = 0; k < g_opKits.nkit; k++) g_opKits.kit[k] = h->kit[k];
    for (int i = 0; i < n; i++)
    {
        memcpy(g_opKits.id[i], r->id[i], 32); memcpy(g_opKits.name[i], r->name[i], 32);
        g_opKits.id[i][31] = g_opKits.name[i][31] = 0;
        if (r->active[0] && !strncmp(r->id[i], r->active, 31)) g_opKits.active = i;
        g_opKits.hub[i] = -1;
        for (int k = 0; k < g_opKits.nkit; k++) if (!strncmp(g_opKits.kit[k].id, g_opKits.id[i], 31)) g_opKits.hub[i] = k;
    }
    g_opKits.n = n;
    RosterUnlock();
}
KB_VAR char g_opPickSaved[32];         /* kits.ini's pick, set once its kit has joined (kits load one after another) */
KB_VAR double g_opPickUntil = 90, g_opPickAt, g_opClock;
KB_VAR _Atomic int g_opPickWant = -1;  /* the screen's pick: an index into g_opKits */
KB_FN void OpMakeDir(const char* path)
{
    char dir[MAX_PATH * 2];
    snprintf(dir, sizeof dir, "%s", path);
    char* cut = strrchr(dir, '\\');
    if (cut) { *cut = 0; CreateDirectoryA(dir, NULL); }
}
KB_FN void OpKitsSave(const char* id)
{
    if (!g_opKitsPath[0]) return;
    OpMakeDir(g_opKitsPath);
    FILE* o = fopen(g_opKitsPath, "w");
    if (!o) { Log("the picked character could not be saved to %s", g_opKitsPath); return; }
    fprintf(o, "; The character kits' options screen. active: the picked kit (~off: none acts). options_key: the screen's key\n"
               "; (a Windows virtual-key number; 120 is F9; 0 none). Delete this file to forget both.\nactive = %s\n", id);
    if (g_opKeyFile) fprintf(o, "options_key = %d\n", g_opKey);
    fclose(o);
}

/* ---- loading and saving ----------------------------------------------------------------------------------------- */
/* after the kit's .ini: its values kept as each row's reset, then the screen's saved choices */
KB_FN void OpLoad(void)
{
    for (int i = 0; i < kb_op_n; i++)
    {
        g_opIni[i] = OpNow(i);
        g_opLo[i] = kb_op_rows[i].lo; g_opHi[i] = kb_op_rows[i].hi;
        /* a field takes what the .ini itself would take */
        if (kb_op_rows[i].kind == OP_FIELD)
            for (unsigned n = 0; n < kb_nsettings; n++)
                if (!strcmp(kb_settings[n].name, kb_op_rows[i].name)) { g_opLo[i] = kb_settings[n].lo; g_opHi[i] = kb_settings[n].hi; }
    }
    char local[MAX_PATH], line[256];
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", local, sizeof local);
    if (!n || n >= sizeof local) return;
    snprintf(g_opPath, sizeof g_opPath, "%s\\HellForgeMods\\%s", local, kb_ini);
    snprintf(g_opKitsPath, sizeof g_opKitsPath, "%s\\HellForgeMods\\kits.ini", local);
    FILE* f = fopen(g_opPath, "r");
    if (f)
    {
        int got = 0;
        while (fgets(line, sizeof line, f))
        {
            char* c = strchr(line, ';');
            if (c) *c = 0;
            char* eq = strchr(line, '=');
            char key[64], *end;
            if (!eq) continue;
            *eq = 0;
            if (sscanf(line, " %63s", key) != 1) continue;
            double v = strtod(eq + 1, &end);
            if (end == eq + 1) continue;
            for (int i = 0; i < kb_op_n; i++) if (!strcmp(key, kb_op_rows[i].name)) { Setting(key, v); got++; break; }
        }
        fclose(f);
        hf->log(self, "settings: %d of the options screen's choices from %s", got, g_opPath);
    }
    f = fopen(g_opKitsPath, "r");
    if (f)
    {
        while (fgets(line, sizeof line, f))
        {
            char* c = strchr(line, ';');
            if (c) *c = 0;
            char id[32];
            int key;
            if (sscanf(line, " active = %31s", id) == 1) snprintf(g_opPickSaved, sizeof g_opPickSaved, "%s", id);
            if (sscanf(line, " options_key = %d", &key) == 1 && key >= 0 && key < 256) { g_opKey = key; g_opKeyFile = 1; }
        }
        fclose(f);
    }
}
KB_FN void OpSave(void)
{
    if (!g_opPath[0]) return;
    int n = 0;
    for (int i = 0; i < kb_op_n; i++) if (fabs(OpNow(i) - g_opIni[i]) > 1e-6) n++;
    static int had = -1;
    if (had < 0) { FILE* t = fopen(g_opPath, "r"); had = t != NULL; if (t) fclose(t); }
    if (!n && !had) return;            /* nothing chosen, and no file to bring up to date */
    OpMakeDir(g_opPath);
    FILE* o = fopen(g_opPath, "w");
    if (!o) { Log("the options screen's choices could not be saved to %s", g_opPath); return; }
    fprintf(o, "; %s: the options screen's choices (read after the mod's %s). Delete this file to go back to it.\n", kb_op_title, kb_ini);
    for (int i = 0; i < kb_op_n; i++)
        if (fabs(OpNow(i) - g_opIni[i]) > 1e-6) fprintf(o, "%s = %g\n", kb_op_rows[i].name, OpNow(i));
    fclose(o);
    had = 1;
    Log("options screen: %d choice(s) saved to %s", n, g_opPath);
}

/* ---- this kit's rows (called by the screen's owner, on the render thread) --------------------------------------- */
KB_VAR float g_opScroll, g_opT;
KB_VAR int g_opDrag = -1, g_opSel = -1;
KB_VAR int g_opEdit = -1;              /* the field being typed in */
KB_VAR char g_opText[16];

/* the n-th of "A|B|C" */
KB_FN void OpChoice(const char* choices, int n, char* out, int size)
{
    out[0] = 0;
    if (!choices) return;
    while (n > 0 && *choices) { if (*choices == '|') n--; choices++; }
    int k = 0;
    while (*choices && *choices != '|' && k < size - 1) out[k++] = *choices++;
    out[k] = 0;
}
KB_FN void OpValueText(int i, double v, char out[32])
{
    const OpRow* r = &kb_op_rows[i];
    if (r->kind == OP_CHOICE) { OpChoice(r->choices, (int)(v + 0.5), out, 32); return; }
    if (r->kind == OP_TOGGLE) snprintf(out, 32, "%s", v != 0 ? "ON" : "OFF");
    else if (r->kind == OP_FIELD) { snprintf(out, 32, "%.2f", v); char* e = out + strlen(out) - 1; while (*e == '0') *e-- = 0; if (*e == '.') *e = 0; }
    else if (r->step >= 1) snprintf(out, 32, "%.0f", v);
    else if (r->step >= 0.1f) snprintf(out, 32, "%.1f", v);
    else snprintf(out, 32, "%.2f", v);
}
KB_FN void OpEditCommit(void)
{
    if (g_opEdit >= 0 && g_opText[0]) { char* end; double v = strtod(g_opText, &end); if (end != g_opText) OpAsk(g_opEdit, v); }
    g_opEdit = -1;
}
KB_FN int OpIn(const HfInput* in, float x0, float y0, float x1, float y1) { return in->x >= x0 && in->x < x1 && in->y >= y0 && in->y < y1; }
KB_FN int OpHead(int i) { return i == 0 || kb_op_rows[i].adv != kb_op_rows[i - 1].adv || strcmp(kb_op_rows[i].group, kb_op_rows[i - 1].group); }
KB_VAR int g_opAdvOpen;                /* the Advanced section is open */
/* the first Advanced row (kb_op_n: none); the rows shown: all of them, or those before it while it is closed */
KB_FN int OpFirstAdv(void) { for (int i = 0; i < kb_op_n; i++) if (kb_op_rows[i].adv) return i; return kb_op_n; }
KB_FN void OpBody(const HfDraw* d, const HfInput* in, float dt, const KbOpBox* box)
{
    const OpRow* rows = kb_op_rows;
    const int firstAdv = OpFirstAdv(), hasAdv = firstAdv < kb_op_n;
    const int N = g_opAdvOpen ? kb_op_n : firstAdv;   /* the rows shown */
    KbOpHub* hub = kb_ophub;
    if (hub && hub->opens != g_opOpens) { g_opOpens = hub->opens; g_opDrag = -1; g_opEdit = -1; g_opT = 0; }
    g_opT += dt;
    float a = box->alpha;
    uint32_t A = (uint32_t)(a * 255);
    const HfColour yellow = HF_RGBA(255, 232, 0, A), white = HF_RGBA(240, 240, 240, A), grey = HF_RGBA(150, 154, 158, A), ink = HF_RGBA(14, 16, 18, A),
                   track = HF_RGBA(70, 74, 78, A), accent = HF_RGBA((kb_op_accent >> 16) & 255, (kb_op_accent >> 8) & 255, kb_op_accent & 255, A);
    int click = (in->pressed & HF_MOUSE_LEFT) != 0;
    const float x0 = box->x0, y0 = box->y0, x1 = box->x1, y1 = box->y1, W = x1 - x0;
    d->rect(x0, y0, x1, y1, HF_RGBA(14, 16, 18, (uint32_t)(238 * a)), yellow, 2);
    d->rect(x0, y0, x1, y0 + 64, yellow, 0, 0);
    d->rect(x0, y0, x0 + 10, y0 + 64, accent, 0, 0);
    d->text(x0 + 28, y0 + 32, kb_op_title, 34, HF_FACE_TITLE, HF_ALIGN_LEFT, ink);
    d->text(x1 - 28, y0 + 32, kb_op_version, 22, HF_FACE_TITLE, HF_ALIGN_RIGHT, ink);
    const char* tip = box->tip;

    /* the list: a group's heading, then its rows */
    const float top = y0 + 74, bottom = y1 - 96, rowH = 46, headH = 44, advH = 56;
    const float lx = x0 + 44, vx = x1 - 44, tx1 = vx - 156, tx0 = tx1 - (W > 1000 ? 420 : 300);
    float content = 0;
    for (int i = 0; i < N; i++) content += rowH + (OpHead(i) ? headH : 0);
    if (hasAdv) content += advH;
    float maxScroll = content - (bottom - top);
    if (maxScroll < 0) maxScroll = 0;
    if (in->x < x1) g_opScroll -= in->wheel * rowH * 2;
    int keyMoved = 0;
    for (uint32_t k = 0; k < in->key_count; k++)
    {
        uint32_t key = in->keys[k];
        if (g_opEdit >= 0)
        {
            /* typing a number: digits, one point, Backspace; Enter or Tab takes it */
            size_t n = strlen(g_opText);
            int digit = key >= '0' && key <= '9' ? (int)key - '0' : key >= VK_NUMPAD0 && key <= VK_NUMPAD9 ? (int)key - VK_NUMPAD0 : -1;
            if (digit >= 0 && n < 9) { g_opText[n] = (char)('0' + digit); g_opText[n + 1] = 0; }
            else if ((key == VK_OEM_PERIOD || key == VK_DECIMAL || key == VK_OEM_COMMA) && n < 9 && !strchr(g_opText, '.')) { g_opText[n] = '.'; g_opText[n + 1] = 0; }
            else if (key == VK_BACK && n) g_opText[n - 1] = 0;
            else if (key == VK_RETURN || key == VK_TAB) OpEditCommit();
            continue;
        }
        if (key == VK_DOWN) { g_opSel = g_opSel < N - 1 ? g_opSel + 1 : N - 1; keyMoved = 1; }
        if (key == VK_UP) { g_opSel = g_opSel > 0 ? g_opSel - 1 : 0; keyMoved = 1; }
        if (g_opSel >= N) g_opSel = N - 1;
        if ((key == VK_LEFT || key == VK_RIGHT) && g_opSel >= 0)
        {
            if (rows[g_opSel].kind == OP_TOGGLE) OpAsk(g_opSel, key == VK_RIGHT);
            else OpAsk(g_opSel, OpShown(g_opSel) + (key == VK_RIGHT ? 1 : -1) * rows[g_opSel].step);
        }
        if ((key == VK_RETURN || key == VK_SPACE) && g_opSel >= 0)
        {
            if (rows[g_opSel].kind == OP_TOGGLE) OpAsk(g_opSel, OpShown(g_opSel) == 0);
            else if (rows[g_opSel].kind == OP_CHOICE) OpAsk(g_opSel, OpShown(g_opSel) >= rows[g_opSel].hi ? 0 : OpShown(g_opSel) + 1);
            else if (rows[g_opSel].kind == OP_FIELD) { g_opEdit = g_opSel; g_opText[0] = 0; }
        }
        if ((key == VK_BACK || key == VK_DELETE) && g_opSel >= 0) OpAsk(g_opSel, g_opIni[g_opSel]);
        if (key == VK_NEXT) g_opScroll += (bottom - top) * 0.8f;
        if (key == VK_PRIOR) g_opScroll -= (bottom - top) * 0.8f;
    }
    if (!(in->down & HF_MOUSE_LEFT)) g_opDrag = -1;
    int inList = OpIn(in, x0, top, x1, bottom), hover = -1, clickedField = 0;
    /* a row chosen by the keys is scrolled into view */
    if (keyMoved && g_opSel >= 0)
    {
        float y = 0;
        for (int i = 0; i <= g_opSel; i++) { if (hasAdv && i == firstAdv) y += advH; if (OpHead(i)) y += headH; if (i < g_opSel) y += rowH; }
        if (y - headH < g_opScroll) g_opScroll = y - headH;
        if (y + rowH > g_opScroll + (bottom - top)) g_opScroll = y + rowH - (bottom - top);
    }
    if (g_opScroll > maxScroll) g_opScroll = maxScroll;
    if (g_opScroll < 0) g_opScroll = 0;

    d->clip_push(x0 + 2, top, x1 - 2, bottom);
    float y = top - g_opScroll;
    int overAdv = 0;
    for (int i = 0; i <= N; i++)
    {
        /* the Advanced section's bar: before its first row, open or closed */
        if (hasAdv && i == firstAdv)
        {
            if (y + advH > top && y < bottom)
            {
                overAdv = inList && g_opDrag < 0 && in->y >= y + 8 && in->y < y + advH - 4;
                d->rect(x0 + 14, y + 8, x1 - 14, y + advH - 4, HF_RGBA(255, 255, 255, (uint32_t)((overAdv ? 30 : 14) * a)), overAdv ? yellow : grey, 2);
                char bar[64];
                snprintf(bar, sizeof bar, "%s  ADVANCED  (%d)", g_opAdvOpen ? "-" : "+", kb_op_n - firstAdv);
                d->text(lx - 16, y + advH / 2 + 2, bar, 22, HF_FACE_TITLE, HF_ALIGN_LEFT, overAdv ? yellow : white);
                d->text(x1 - 32, y + advH / 2 + 2, g_opAdvOpen ? "click to close" : "click to open", 17, HF_FACE_BODY, HF_ALIGN_RIGHT, grey);
                if (overAdv && click) { OpEditCommit(); g_opAdvOpen = !g_opAdvOpen; g_opSel = -1; }
            }
            y += advH;
        }
        if (i == N) break;
        const OpRow* r = &rows[i];
        if (OpHead(i))
        {
            if (y + headH > top && y < bottom)
            {
                d->text(lx - 16, y + headH * 0.58f, r->group, 22, HF_FACE_TITLE, HF_ALIGN_LEFT, yellow);
                d->rect(lx - 16, y + headH - 5, x1 - 28, y + headH - 3, HF_RGBA(255, 232, 0, (uint32_t)(70 * a)), 0, 0);
            }
            y += headH;
        }
        if (y + rowH > top && y < bottom)
        {
            int over = inList && g_opDrag < 0 && in->y >= y && in->y < y + rowH;
            if (over) { hover = i; if (in->pressed || !keyMoved) g_opSel = i; }
            int sel = i == g_opSel || i == g_opDrag || i == g_opEdit;
            if (sel) d->rect(x0 + 14, y + 2, x1 - 14, y + rowH - 2, HF_RGBA(255, 255, 255, (uint32_t)(22 * a)), 0, 0);
            double v = OpShown(i);
            int changed = fabs(v - g_opIni[i]) > 1e-6;
            float cy = y + rowH / 2;
            char text[32];
            OpValueText(i, v, text);
            d->text(lx, cy, r->label, 22, HF_FACE_BODY, HF_ALIGN_LEFT, white);
            if (r->when)
            {
                /* when a change takes effect, beside the label */
                float lw = d->text_width(r->label, 22, HF_FACE_BODY);
                d->text(lx + lw + 12, cy + 1, r->when == OP_RESTART ? "after restart" : "next mission", 16, HF_FACE_BODY, HF_ALIGN_LEFT, grey);
            }
            if (over && (in->pressed & HF_MOUSE_RIGHT)) { if (g_opEdit == i) g_opEdit = -1; OpAsk(i, g_opIni[i]); }
            if (r->kind == OP_TOGGLE)
            {
                /* a switch: a pill with its knob at the right when on */
                const float sw = 76, sh = 32, sx = vx - sw, sy = cy - sh / 2;
                int on = v != 0;
                d->rect(sx, sy, vx, sy + sh, on ? (sel ? yellow : accent) : track, on ? 0 : grey, 2);
                float kx = on ? vx - sh + 4 : sx + 4;
                d->rect(kx, sy + 4, kx + sh - 8, sy + sh - 4, on ? ink : white, 0, 0);
                d->text(sx - 16, cy, text, 22, HF_FACE_TITLE, HF_ALIGN_RIGHT, changed ? yellow : white);
                if (over && click && in->x >= tx0) { OpEditCommit(); OpAsk(i, !on); }
            }
            else if (r->kind == OP_CHOICE)
            {
                /* one button a choice, the chosen one lit */
                float bx1 = vx;
                for (int c = (int)r->hi; c >= 0; c--)
                {
                    char name[48];
                    OpChoice(r->choices, c, name, sizeof name);
                    float w = d->text_width(name, 19, HF_FACE_TITLE) + 28, bx0 = bx1 - w;
                    int on = (int)(v + 0.5) == c, overB = over && OpIn(in, bx0, cy - 17, bx1, cy + 17);
                    d->rect(bx0, cy - 17, bx1, cy + 17, on ? (sel ? yellow : accent) : HF_RGBA(40, 43, 46, A), overB && !on ? yellow : 0, 2);
                    d->text((bx0 + bx1) / 2, cy, name, 19, HF_FACE_TITLE, HF_ALIGN_CENTRE, on ? ink : white);
                    if (overB && click) { OpEditCommit(); OpAsk(i, c); }
                    bx1 = bx0 - 8;
                }
            }
            else if (r->kind == OP_FIELD)
            {
                /* a field to type in, with - and + (one step) either side */
                const float bw = 40, fw = 150, px0 = vx - bw, fx1 = px0 - 8, fx0 = fx1 - fw, mx1 = fx0 - 8, mx0 = mx1 - bw, by0 = cy - 17, by1 = cy + 17;
                int editing = g_opEdit == i, overM = over && OpIn(in, mx0, by0, mx1, by1), overP = over && OpIn(in, px0, by0, vx, by1),
                    overF = over && OpIn(in, fx0, by0, fx1, by1);
                d->rect(mx0, by0, mx1, by1, HF_RGBA(40, 43, 46, A), overM ? yellow : 0, 2);
                d->text((mx0 + mx1) / 2, cy, "-", 24, HF_FACE_TITLE, HF_ALIGN_CENTRE, white);
                d->rect(px0, by0, vx, by1, HF_RGBA(40, 43, 46, A), overP ? yellow : 0, 2);
                d->text((px0 + vx) / 2, cy, "+", 24, HF_FACE_TITLE, HF_ALIGN_CENTRE, white);
                d->rect(fx0, by0, fx1, by1, HF_RGBA(24, 26, 28, A), editing ? yellow : overF ? white : grey, 2);
                if (editing)
                {
                    d->text(fx1 - 14, cy, g_opText, 22, HF_FACE_TITLE, HF_ALIGN_RIGHT, yellow);
                    if (fmodf(g_opT, 1.0f) < 0.5f) d->rect(fx1 - 11, cy - 11, fx1 - 9, cy + 11, yellow, 0, 0);
                }
                else d->text(fx1 - 14, cy, text, 22, HF_FACE_TITLE, HF_ALIGN_RIGHT, changed ? yellow : white);
                if (click && (overM || overP)) { OpEditCommit(); OpAsk(i, OpShown(i) + (overP ? 1 : -1) * r->step); }
                if (click && overF) { if (g_opEdit != i) { OpEditCommit(); g_opEdit = i; g_opText[0] = 0; } clickedField = 1; }
            }
            else
            {
                /* the slider: its track, the part up to the value, a handle; a tick where the .ini's value is */
                float span = r->hi - r->lo, t = span > 0 ? (float)((v - r->lo) / span) : 0;
                float ti = span > 0 ? (float)((g_opIni[i] - r->lo) / span) : 0;
                t = t < 0 ? 0 : t > 1 ? 1 : t;
                d->rect(tx0, cy - 3, tx1, cy + 3, track, 0, 0);
                d->rect(tx0, cy - 3, tx0 + (tx1 - tx0) * t, cy + 3, sel ? yellow : accent, 0, 0);
                if (ti >= 0 && ti <= 1) d->rect(tx0 + (tx1 - tx0) * ti - 1, cy - 9, tx0 + (tx1 - tx0) * ti + 1, cy + 9, grey, 0, 0);
                d->circle(tx0 + (tx1 - tx0) * t, cy, sel ? 10 : 8, sel ? yellow : white, 0, 0);
                d->text(vx, cy, text, 22, HF_FACE_TITLE, HF_ALIGN_RIGHT, changed ? yellow : white);
                if (over && click && in->x >= tx0 - 16 && in->x <= tx1 + 16) { OpEditCommit(); g_opDrag = i; }
            }
        }
        if (g_opDrag == i)
        {
            float t = (in->x - tx0) / (tx1 - tx0);
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            OpAsk(i, r->lo + (r->hi - r->lo) * t);
        }
        y += rowH;
    }
    d->clip_pop();
    if (click && !clickedField) OpEditCommit();                            /* a click anywhere else takes what was typed */
    /* a scroll bar when the list is longer than its box */
    if (maxScroll > 0)
    {
        float h = bottom - top, bar = h * h / content, by = top + (h - bar) * (g_opScroll / maxScroll);
        d->rect(x1 - 12, top, x1 - 8, bottom, HF_RGBA(70, 74, 78, (uint32_t)(120 * a)), 0, 0);
        d->rect(x1 - 12, by, x1 - 8, by + bar, yellow, 0, 0);
    }
    /* the footer: what the thing under the mouse (or the row chosen by the keys) does, and the screen's own keys */
    int tipRow = hover >= 0 ? hover : g_opDrag >= 0 ? g_opDrag : g_opEdit >= 0 ? g_opEdit : g_opSel;
    if (tipRow >= N) tipRow = -1;
    if (!tip && tipRow >= 0) tip = rows[tipRow].tip;
    if (!tip && overAdv)
        tip = g_opAdvOpen ? "The rest of her settings from the .ini. Saved the same way; right click puts one back."
                          : "The rest of her settings from the .ini: finer numbers for her skills and her Sinveiler. Leave them be unless you want to tune her.";
    d->rect(x0 + 2, bottom + 8, x1 - 2, bottom + 10, HF_RGBA(255, 232, 0, (uint32_t)(70 * a)), 0, 0);
    if (tip)
    {
        /* up to two lines, broken at a space */
        char one[200], two[200];
        snprintf(one, sizeof one, "%s", tip);
        two[0] = 0;
        if (d->text_width(one, 19, HF_FACE_BODY) > W - 56)
            for (char* sp = one + strlen(one); sp > one; sp--)
                if (*sp == ' ') { *sp = 0; if (d->text_width(one, 19, HF_FACE_BODY) <= W - 56) { snprintf(two, sizeof two, "%s", tip + (sp - one) + 1); break; } *sp = ' '; }
        d->clip_push(x0 + 28, bottom + 12, x1 - 28, bottom + 66);
        d->text(x0 + 28, bottom + (two[0] ? 28 : 38), one, 19, HF_FACE_BODY, HF_ALIGN_LEFT, white);
        if (two[0]) d->text(x0 + 28, bottom + 52, two, 19, HF_FACE_BODY, HF_ALIGN_LEFT, white);
        d->clip_pop();
    }
    if (!box->tip && tipRow >= 0 && rows[tipRow].kind == OP_FIELD)
    {
        char range[80], lo[32], hi[32];
        OpValueText(tipRow, g_opLo[tipRow], lo); OpValueText(tipRow, g_opHi[tipRow], hi);
        snprintf(range, sizeof range, "%s to %s", lo, hi);
        d->text(x1 - 28, y1 - 18, range, 17, HF_FACE_BODY, HF_ALIGN_RIGHT, grey);
    }
    char keys[200];
    snprintf(keys, sizeof keys, W > 1000 ? "Click a field to type, Enter takes it  |  Right click or Backspace: back to %s's value  |  Esc: close and save"
                                         : "Right click or Backspace: %s's value  |  Esc: close and save", kb_ini);
    d->text(x0 + 28, y1 - 18, keys, 17, HF_FACE_BODY, HF_ALIGN_LEFT, grey);
}

/* ---- the screen (its owner only) -------------------------------------------------------------------------------- */
KB_VAR float g_opOpenT, g_opKitsT = 1;
KB_VAR char g_opView[32];              /* the kit whose rows are shown */
KB_VAR HfImage* g_opImage[KB_ROSTER_MAX];
KB_VAR const unsigned char* g_opImageOf[KB_ROSTER_MAX];
KB_FN int OpScreenDraw(void* user, const HfDraw* d, const HfInput* in, float dt)
{
    if (in->closing) return 0;
    g_opOpenT += dt;
    float a = g_opOpenT < 0.12f ? g_opOpenT / 0.12f : 1;                  /* a short fade in */
    uint32_t A = (uint32_t)(a * 255);
    const HfColour yellow = HF_RGBA(255, 232, 0, A), white = HF_RGBA(240, 240, 240, A), grey = HF_RGBA(150, 154, 158, A), ink = HF_RGBA(14, 16, 18, A),
                   red = HF_RGBA(200, 60, 50, A);
    int click = (in->pressed & HF_MOUSE_LEFT) != 0;
    d->backdrop(0.55f * a, 0.35f * a, 10);
    g_opKitsT += dt;
    if (g_opKitsT > 0.5f) { g_opKitsT = 0; OpKitsRead(); }
    int wanted = atomic_load(&g_opPickWant);
    int picked = wanted >= 0 && wanted < g_opKits.n ? wanted : g_opKits.active;
    /* whose rows: the one clicked, else the picked kit, else this one's own */
    int view = -1;
    for (int k = 0; k < g_opKits.nkit; k++) if (g_opView[0] && !strncmp(g_opKits.kit[k].id, g_opView, 31)) view = k;
    if (view < 0 && picked < g_opKits.n && g_opKits.hub[picked] >= 0) view = g_opKits.hub[picked];
    if (view < 0) for (int k = 0; k < g_opKits.nkit; k++) if (!strncmp(g_opKits.kit[k].id, kb_op_id, 31)) view = k;
    if (view >= 0) snprintf(g_opView, sizeof g_opView, "%s", g_opKits.kit[view].id);

    /* the frame: the rows at the left, the characters in a column at the right */
    const float gap = 14, colW = g_opKits.n >= 2 ? 330 : 0, y0 = 70, y1 = 1010;
    float bodyW = d->width - 80 - (colW ? colW + gap : 0);
    if (bodyW > 1180) bodyW = 1180;
    const float total = bodyW + (colW ? colW + gap : 0), x0 = (d->width - total) / 2, bx1 = x0 + bodyW, cx0 = bx1 + gap, cx1 = cx0 + colW;
    const char* tip = NULL;
    int overTile = -1;
    if (colW > 0)
    {
        d->rect(cx0, y0, cx1, y1, HF_RGBA(14, 16, 18, (uint32_t)(238 * a)), yellow, 2);
        d->rect(cx0, y0, cx1, y0 + 64, yellow, 0, 0);
        d->text(cx0 + 22, y0 + 32, "CHARACTER", 28, HF_FACE_TITLE, HF_ALIGN_LEFT, ink);
        const float top = y0 + 74, bottom = y1 - 44;
        float th = (bottom - top) / g_opKits.n;
        if (th > 78) th = 78;
        const int images = d->size >= offsetof(HfDraw, image) + sizeof d->image;
        for (int i = 0; i < g_opKits.n; i++)
        {
            float ty = top + th * i, isz = th - 12, ix = cx0 + 14, iy = ty + 6;
            int k = g_opKits.hub[i], off = !strcmp(g_opKits.id[i], KB_OP_OFF_ID), on = i == picked, shown = k >= 0 && k == view;
            int over = OpIn(in, cx0 + 2, ty, cx1 - 2, ty + th);
            if (over) overTile = i;
            if (on) d->rect(cx0 + 6, ty + 2, cx1 - 6, ty + th - 2, off ? HF_RGBA(200, 60, 50, (uint32_t)(70 * a)) : HF_RGBA(255, 232, 0, (uint32_t)(48 * a)), off ? red : yellow, 2);
            else if (over || shown) d->rect(cx0 + 6, ty + 2, cx1 - 6, ty + th - 2, HF_RGBA(255, 255, 255, (uint32_t)((over ? 26 : 14) * a)), 0, 0);
            /* its mod icon; a kit without the screen (or an older HellForge): its initial */
            HfImage* img = NULL;
            if (images && k >= 0 && g_opKits.kit[k].icon)
            {
                if (g_opImageOf[k] != g_opKits.kit[k].icon) { g_opImageOf[k] = g_opKits.kit[k].icon; g_opImage[k] = d->image_create(KB_OP_ICON, KB_OP_ICON, g_opKits.kit[k].icon); }
                img = g_opImage[k];
            }
            d->rect(ix, iy, ix + isz, iy + isz, HF_RGBA(30, 33, 36, A), on ? (off ? red : yellow) : HF_RGBA(90, 94, 98, A), 2);
            if (img) { HfRect box = {ix + 2, iy + 2, ix + isz - 2, iy + isz - 2}; d->image(img, &box, NULL, HF_RGBA(255, 255, 255, A)); }
            else if (off)
            {
                d->circle(ix + isz / 2, iy + isz / 2, isz * 0.3f, 0, on ? red : grey, 4);
                HfPoint bar[2] = {{ix + isz * 0.29f, iy + isz * 0.71f}, {ix + isz * 0.71f, iy + isz * 0.29f}};
                d->lines(bar, 2, on ? red : grey, 4, 0);
            }
            else { char one[2] = {g_opKits.name[i][0], 0}; d->text(ix + isz / 2, iy + isz / 2, one, isz * 0.5f, HF_FACE_TITLE, HF_ALIGN_CENTRE, grey); }
            float nx = ix + isz + 14, mid = ty + th / 2;
            d->text(nx, on ? mid - 10 : mid, g_opKits.name[i], 23, HF_FACE_TITLE, HF_ALIGN_LEFT, on ? (off ? white : yellow) : white);
            if (on) d->text(nx, mid + 14, off ? "NO KIT ACTS" : i == g_opKits.active && wanted < 0 ? "ACTIVE" : "ACTIVE NEXT MISSION", 15, HF_FACE_BODY, HF_ALIGN_LEFT, grey);
            if (over)
                tip = off ? "Off: no character kit acts, a plain Helldiver. From the next mission start."
                    : k < 0 ? "Only the picked kit acts (keys, body, skills, HUD), from the next mission start. This kit's build has no rows here: its numbers are on the MODS tab."
                            : "Only the picked kit acts (keys, body, skills, HUD), from the next mission start. Its numbers are shown at the left.";
            if (over && click)
            {
                if (i != picked) atomic_store(&g_opPickWant, i);
                if (k >= 0) { view = k; snprintf(g_opView, sizeof g_opView, "%s", g_opKits.kit[k].id); }
            }
        }
        d->text((cx0 + cx1) / 2, y1 - 22, "A pick starts at the next mission", 16, HF_FACE_BODY, HF_ALIGN_CENTRE, grey);
    }
    (void)overTile;
    if (view >= 0 && g_opKits.kit[view].body)
    {
        KbOpBox box = {x0, y0, bx1, y1, a, tip};
        g_opKits.kit[view].body(d, in, dt, &box);
    }
    return 1;
}
KB_FN void OpScreenOpened(void* user, int open)
{
    KbOpHub* h = kb_ophub;
    if (open) { g_opOpenT = 0; g_opKitsT = 1; g_opView[0] = 0; if (h) InterlockedIncrement(&h->opens); }
    else if (h) InterlockedIncrement(&h->closes);
}

/* the game thread: what the screen asked for is set, and saved once it has closed */
KB_FN void KbOptionsTick(void)
{
    KbOpHub* h = kb_ophub;
    if (!g_opOn || !h) return;
    LONG closes = h->closes;
    int closed = closes != g_opCloses;
    if (closed) { g_opCloses = closes; OpEditCommit(); }
    for (int i = 0; i < kb_op_n; i++)
        if (atomic_load(&g_opDirty[i])) { OptionSet(kb_op_rows[i].name, atomic_load(&g_opWant[i])); atomic_store(&g_opDirty[i], 0); }
    if (closed) OpSave();
    if (!g_opOwner) return;
    int want = atomic_exchange(&g_opPickWant, -1);
    if (want >= 0 && want < g_opKits.n)
    {
        char id[32];
        snprintf(id, sizeof id, "%s", g_opKits.id[want]);
        if (RosterPick(id)) { OpKitsSave(id); g_opPickSaved[0] = 0; }
    }
    /* once a second: the Off kept last, and kits.ini's pick set as soon as its kit is there */
    g_opClock += 1.0 / 60;             /* a rough clock is enough here */
    if (g_opClock - g_opPickAt < 1) return;
    g_opPickAt = g_opClock;
    RosterOffLast();
    if (g_opPickSaved[0] && g_opClock < g_opPickUntil && RosterPick(g_opPickSaved)) g_opPickSaved[0] = 0;
}

/* at init, after the settings: this kit's rows join the hub, and the screen is added by the first kit that may */
KB_FN void KbOptionsStart(const HfApi* api, HfMod* mod, const char* id, const char* name, const char* version, const OpRow* rows, int nrows,
                          const unsigned char* icon, int iconSize, unsigned accent, int key)
{
    kb_op_rows = rows;
    kb_op_n = nrows > KB_OP_MAX ? KB_OP_MAX : nrows;
    kb_op_id = id;
    kb_op_accent = accent;
    snprintf(kb_op_title, sizeof kb_op_title, "%s", name);
    for (char* c = kb_op_title; *c; c++) if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
    snprintf(kb_op_version, sizeof kb_op_version, "OPTIONS  %s", version);
    if (key >= 0) g_opKey = key;
    OpLoad();
    if (api->version < 17 || api->size < offsetof(HfApi, screen_show) + sizeof api->screen_show)
    {
        api->log(mod, "options screen: this HellForge has no screens over the game (SDK 17); the numbers stay on the MODS tab (Mod Options Menu)");
        return;
    }
    KbOpHub* h = OpHub();
    if (!h || !Roster() || !RosterLock()) { api->log(mod, "options screen: its shared block is unavailable; the numbers stay on the MODS tab"); return; }
    if (h->magic != KB_OPHUB_MAGIC) { memset(h, 0, sizeof *h); h->magic = KB_OPHUB_MAGIC; h->version = 1; }
    int at = -1, own = 0;
    for (uint32_t i = 0; i < h->count; i++) if (!strncmp(h->kit[i].id, id, 31)) at = (int)i;
    if (at < 0 && h->count < KB_ROSTER_MAX) at = (int)h->count++;
    if (at >= 0)
    {
        snprintf(h->kit[at].id, 32, "%s", id);
        h->kit[at].icon = iconSize == KB_OP_ICON ? icon : NULL;
        h->kit[at].body = OpBody;
    }
    g_opCloses = h->closes;
    int had = h->screen != 0;
    RosterUnlock();
    if (at < 0) { api->log(mod, "options screen: no room left on it; the numbers stay on the MODS tab"); return; }
    if (!had && g_opKey > 0)
    {
        HfScreen screen = {sizeof screen, "Character kits' options", (uint32_t)g_opKey, HF_SCREEN_MODAL, NULL, OpScreenDraw, OpScreenOpened, NULL};
        g_opScreen = api->add_screen(mod, &screen);
        if (g_opScreen && RosterLock()) { own = !h->screen; h->screen = 1; RosterUnlock(); }
        if (own) { g_opOwner = 1; RosterAddOff(); }
        else if (!g_opScreen) api->log(mod, "options screen: HellForge refused this kit's (modloader.log says why; the manifest needs \"sdk\": 17); another kit may add it");
    }
    g_opOn = 1;
    if (g_opOwner)
        api->log(mod, "options screen: key %d opens it (120 is F9; options_key in HellForgeMods\\kits.ini), for every character kit, with the picker and an Off%s%s",
                 g_opKey, g_opPickSaved[0] ? "; picked last time: " : "", g_opPickSaved);
    else api->log(mod, "options screen: %s", had ? "joined (another kit's key opens it)" : g_opKey > 0 ? "its rows wait for a kit that may add the screen" : "no key (options_key 0)");
}
#endif /* HF_SCREEN_MODAL */
#endif
