/* Vector's own options screen (0.2.0), drawn over the game by HellForge's overlay host (SDK 17: add_screen;
 * docs/modding/overlay.md). It takes the Mod Options Menu's place where the host is there: the same numbers, the same
 * names as vector.ini, and the character kits' picker with an Off. Included by vector.c only when the SDK has screens
 * (HF_SCREEN_MODAL).
 *
 * Threads: the screen is drawn on the host's render thread. It never calls the game: a changed value waits in g_opWant
 * and a pick in g_opPickWant, and Tick (the game thread) sets them (OptionsTick). What differs from vector.ini is saved
 * to %LOCALAPPDATA%\HellForgeMods\vector.ini when the screen closes (HellForge keeps mod folders read-only while the
 * game runs) and read after vector.ini at the next start; the picked character to HellForgeMods\kits.ini. */
#ifndef VECTOR_OPTIONS_H
#define VECTOR_OPTIONS_H

/* a row's control: a slider (few steps), a switch (0 or 1), or a field to type in with - and + (a wide range: its
 * limits are the setting's own in vector.ini, not the slider's) */
enum { OP_SLIDER, OP_TOGGLE, OP_FIELD };
typedef struct OpRow { const char* name; const char* group; const char* label; int kind; float lo, hi, step; const char* tip; } OpRow;
static const OpRow kOpRows[] = {
    {"conf_max", "Confectance", "Most", OP_SLIDER, 2, 20, 1,
     "Points at which the Confectance Index is spent for its damage boost (each skill gives 2)."},
    {"conf_mult", "Confectance", "Damage", OP_SLIDER, 1, 3, 0.05,
     "Her hits deal this times their damage while the boost runs."},
    {"conf_seconds", "Confectance", "Duration", OP_FIELD, 0, 60, 1,
     "Seconds the boost runs."},
    {"s1_cooldown", "Dead End Meltdown", "Cooldown", OP_FIELD, 0, 120, 1,
     "Seconds before Dead End Meltdown can be used again."},
    {"s1_seconds", "Dead End Meltdown", "Duration", OP_FIELD, 2, 60, 1,
     "Seconds her shots set what they hit on fire."},
    {"s1_burst_every", "Dead End Meltdown", "Burst Interval", OP_SLIDER, 0.5, 10, 0.5,
     "Seconds between fire bursts on the same burning enemy."},
    {"overburn_seconds", "Overburn", "Duration", OP_FIELD, 0, 30, 1,
     "Seconds an enemy stays Overburned after a burst."},
    {"overburn_mult", "Overburn", "Fire Damage", OP_SLIDER, 1, 3, 0.05,
     "Fire damage on an Overburned enemy, times."},
    {"s2_cooldown", "Portent of Doom", "Cooldown", OP_FIELD, 0, 120, 1,
     "Seconds before Portent of Doom can be used again."},
    {"smolder_seconds", "Smolder", "Duration", OP_FIELD, 0, 60, 1,
     "Seconds her target Smolders."},
    {"smolder_mult", "Smolder", "Fire Damage", OP_SLIDER, 1, 4, 0.05,
     "Fire damage on a Smoldering enemy, times."},
    {"s3_cooldown", "Searing Finale", "Cooldown", OP_FIELD, 0, 300, 5,
     "Seconds before Searing Finale can be used again."},
    {"s3_shells", "Searing Finale", "Shells", OP_SLIDER, 1, 12, 1,
     "Napalm shells that fall where she aims."},
    {"s3_spread", "Searing Finale", "Spread", OP_SLIDER, 0, 30, 1,
     "Metres around her aim the shells after the first land in."},
    {"accelerant_seconds", "Accelerant", "Duration", OP_FIELD, 0, 60, 1,
     "Seconds of Accelerant after Searing Finale."},
    {"accelerant_mult", "Accelerant", "Fire Damage", OP_SLIDER, 1, 3, 0.05,
     "Fire damage on enemies during Accelerant, times."},
    {"fire_immune", "Perception Block", "Fireproof", OP_TOGGLE, 0, 1, 1,
     "On: fire does nothing to her. Off: she burns like anyone."},
    {"own_burn", "Her Own Burn", "On Armour", OP_TOGGLE, 0, 1, 1,
     "On: tanks, walkers and gunships, which the game's fire cannot hurt, burn by her hand. Off: they stay fireproof."},
    {"own_burn_damage", "Her Own Burn", "Damage A Tick", OP_FIELD, 1, 100, 1,
     "Health burnt off armoured enemies four times a second, past their armour. A soldier's burn is 25."},
    {"own_burn_percent", "Her Own Burn", "Percent Of Health A Tick", OP_SLIDER, 0, 10, 0.25,
     "Share of the enemy's full health burnt off four times a second. 1: burnt through in 25 s of unbroken burning. 0: the flat damage only."},
    {"own_burn_seconds", "Her Own Burn", "Seconds", OP_FIELD, 1, 30, 1,
     "How long her own burn lasts after her fire."},
    {"hp_mult", "Body", "Health", OP_SLIDER, 0.5, 4, 0.05,
     "Her health, times a Helldiver's."},
    {"stamina_mult", "Body", "Stamina", OP_SLIDER, 0.5, 6, 0.1,
     "How long she sprints before the bar empties, times a Helldiver's."},
    {"hellbomb_on_exhaustion", "Body", "Hellbomb When Exhausted", OP_TOGGLE, 0, 1, 1,
     "On: running her stamina dry sets off a Hellbomb on her (a T-Doll's price). Off: it does not."}
};
#define OPROWS (int)(sizeof kOpRows / sizeof kOpRows[0])

static int g_opScreen;                 /* its id; 0: no screen (an older HellForge), the Mod Options Menu stays */
static double g_opIni[OPROWS];         /* each row's value from vector.ini (or its built-in default): what "reset" means */
static double g_opLo[OPROWS], g_opHi[OPROWS];
static _Atomic double g_opWant[OPROWS];
static atomic_uint g_opDirty[OPROWS];
static atomic_uint g_opSave;
static char g_opPath[MAX_PATH * 2], g_opKitsPath[MAX_PATH * 2];

static double OpNow(int i) { double v = 0; OptionGet(kOpRows[i].name, &v); return v; }
static double OpSnap(int i, double v)
{
    const OpRow* r = &kOpRows[i];
    if (r->kind != OP_FIELD && r->step > 0) v = r->lo + floor((v - r->lo) / r->step + 0.5) * r->step;
    if (r->kind == OP_FIELD) v = floor(v * 100 + 0.5) / 100;
    return v < g_opLo[i] ? g_opLo[i] : v > g_opHi[i] ? g_opHi[i] : v;
}
static void OpAsk(int i, double v)
{
    atomic_store(&g_opWant[i], OpSnap(i, v));
    atomic_store(&g_opDirty[i], 1);
}
/* what the screen shows: a value asked for and not set yet, else the live one */
static double OpShown(int i) { return atomic_load(&g_opDirty[i]) ? atomic_load(&g_opWant[i]) : OpNow(i); }

/* ---- the character kits' picker --------------------------------------------------------------------------------- */
/* "Off" is a kit of its own in the shared roster ("~off": last by id, so it is never the one that acts unpicked). With
 * it picked, no real kit is the picked one, so every kit stands by: the released kits need no change for it. */
#define OP_OFF_ID "~off"
static void RosterAddOff(void)
{
    KbRoster* r = Roster();
    if (!r || !RosterLock()) return;
    if (r->magic == KB_ROSTER_MAGIC && r->count < KB_ROSTER_MAX)
    {
        int known = 0;
        for (uint32_t i = 0; i < r->count; i++) if (!strncmp(r->id[i], OP_OFF_ID, 31)) known = 1;
        if (!known) { snprintf(r->id[r->count], 32, "%s", OP_OFF_ID); snprintf(r->name[r->count], 32, "Off"); r->count++; }
    }
    RosterUnlock();
}
/* a kit that joins after the Off went in belongs before it (the roster is sorted by id): seen to on the game thread */
static void RosterOffLast(void)
{
    KbRoster* r = kb_roster;
    if (!r || r->magic != KB_ROSTER_MAGIC || !RosterLock()) return;
    for (uint32_t i = 0; i + 1 < r->count; i++)
        if (!strncmp(r->id[i], OP_OFF_ID, 31))
        {
            char id[32], name[32];
            memcpy(id, r->id[i], 32); memcpy(name, r->name[i], 32);
            memmove(r->id[i], r->id[i + 1], 32 * (r->count - 1 - i)); memmove(r->name[i], r->name[i + 1], 32 * (r->count - 1 - i));
            memcpy(r->id[r->count - 1], id, 32); memcpy(r->name[r->count - 1], name, 32);
            break;
        }
    RosterUnlock();
}
static struct { int n, active; char id[KB_ROSTER_MAX][32], name[KB_ROSTER_MAX][32]; } g_opKits;   /* the render thread's copy */
static void OpKitsRead(void)
{
    KbRoster* r = kb_roster;
    g_opKits.n = 0;
    if (!r || r->magic != KB_ROSTER_MAGIC || !RosterLock()) return;
    int n = (int)r->count > KB_ROSTER_MAX ? KB_ROSTER_MAX : (int)r->count;
    g_opKits.active = 0;
    for (int i = 0; i < n; i++)
    {
        memcpy(g_opKits.id[i], r->id[i], 32); memcpy(g_opKits.name[i], r->name[i], 32);
        g_opKits.id[i][31] = g_opKits.name[i][31] = 0;
        if (r->active[0] && !strncmp(r->id[i], r->active, 31)) g_opKits.active = i;
    }
    g_opKits.n = n;
    RosterUnlock();
}
static char g_opPickSaved[32];         /* kits.ini's pick, set once its kit has joined (kits load one after another) */
static double g_opPickUntil = 90, g_opPickAt;
static _Atomic int g_opPickWant = -1;  /* the screen's pick: an index into g_opKits */
static void OpKitsSave(const char* id)
{
    if (!g_opKitsPath[0]) return;
    char dir[MAX_PATH * 2];
    snprintf(dir, sizeof dir, "%s", g_opKitsPath);
    char* cut = strrchr(dir, '\\');
    if (cut) { *cut = 0; CreateDirectoryA(dir, NULL); }
    FILE* f = fopen(g_opKitsPath, "w");
    if (!f) { Log("the picked character could not be saved to %s", g_opKitsPath); return; }
    fprintf(f, "; The character kits' picked one (an options screen's choice; ~off: none acts). Delete this file to forget it.\nactive = %s\n", id);
    fclose(f);
}

/* ---- loading and saving ----------------------------------------------------------------------------------------- */
/* after vector.ini: its values kept as each row's reset, then the screen's saved choices */
static void OptionsLoad(void)
{
    for (int i = 0; i < OPROWS; i++)
    {
        g_opIni[i] = OpNow(i);
        g_opLo[i] = kOpRows[i].lo; g_opHi[i] = kOpRows[i].hi;
        /* a field takes what vector.ini itself would take */
        if (kOpRows[i].kind == OP_FIELD)
            for (unsigned n = 0; n < kb_nsettings; n++)
                if (!strcmp(kb_settings[n].name, kOpRows[i].name)) { g_opLo[i] = kb_settings[n].lo; g_opHi[i] = kb_settings[n].hi; }
    }
    char local[MAX_PATH], line[256];
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", local, sizeof local);
    if (!n || n >= sizeof local) return;
    snprintf(g_opPath, sizeof g_opPath, "%s\\HellForgeMods\\vector.ini", local);
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
            for (int i = 0; i < OPROWS; i++) if (!strcmp(key, kOpRows[i].name)) { Setting(key, v); got++; break; }
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
            if (sscanf(line, " active = %31s", id) == 1) snprintf(g_opPickSaved, sizeof g_opPickSaved, "%s", id);
        }
        fclose(f);
    }
}
static void OptionsSave(void)
{
    if (!g_opPath[0]) return;
    char dir[MAX_PATH * 2];
    snprintf(dir, sizeof dir, "%s", g_opPath);
    char* cut = strrchr(dir, '\\');
    if (cut) { *cut = 0; CreateDirectoryA(dir, NULL); }
    FILE* f = fopen(g_opPath, "w");
    if (!f) { Log("the options screen's choices could not be saved to %s", g_opPath); return; }
    int n = 0;
    fprintf(f, "; Vector: the options screen's choices (read after the mod's vector.ini). Delete this file to go back to it.\n");
    for (int i = 0; i < OPROWS; i++)
        if (fabs(OpNow(i) - g_opIni[i]) > 1e-6) { fprintf(f, "%s = %g\n", kOpRows[i].name, OpNow(i)); n++; }
    fclose(f);
    Log("options screen: %d choice(s) saved to %s", n, g_opPath);
}
/* the game thread: what the screen asked for is set, and saved once it has closed */
static void OptionsTick(void)
{
    if (!g_opScreen) return;
    for (int i = 0; i < OPROWS; i++)
        if (atomic_load(&g_opDirty[i])) { OptionSet(kOpRows[i].name, atomic_load(&g_opWant[i])); atomic_store(&g_opDirty[i], 0); }
    if (atomic_exchange(&g_opSave, 0)) OptionsSave();
    int want = atomic_exchange(&g_opPickWant, -1);
    if (want >= 0 && want < g_opKits.n)
    {
        char id[32];
        snprintf(id, sizeof id, "%s", g_opKits.id[want]);
        if (RosterPick(id)) { OpKitsSave(id); g_opPickSaved[0] = 0; }
    }
    /* once a second: the Off kept last, and kits.ini's pick set as soon as its kit is there */
    static double clock;
    clock += 1.0 / 60;                 /* a rough clock is enough here */
    if (clock - g_opPickAt < 1) return;
    g_opPickAt = clock;
    RosterOffLast();
    if (g_opPickSaved[0] && clock < g_opPickUntil && RosterPick(g_opPickSaved)) g_opPickSaved[0] = 0;
}

/* ---- drawing ---------------------------------------------------------------------------------------------------- */
static float g_opScroll, g_opOpenT, g_opKitsT = 1;
static int g_opDrag = -1, g_opSel = -1;
static int g_opEdit = -1;              /* the field being typed in */
static char g_opText[16];

static void OpValueText(int i, double v, char out[32])
{
    const OpRow* r = &kOpRows[i];
    if (r->kind == OP_TOGGLE) snprintf(out, 32, "%s", v != 0 ? "ON" : "OFF");
    else if (r->kind == OP_FIELD) { snprintf(out, 32, "%.2f", v); char* e = out + strlen(out) - 1; while (*e == '0') *e-- = 0; if (*e == '.') *e = 0; }
    else if (r->step >= 1) snprintf(out, 32, "%.0f", v);
    else if (r->step >= 0.1f) snprintf(out, 32, "%.1f", v);
    else snprintf(out, 32, "%.2f", v);
}
static void OpEditCommit(void)
{
    if (g_opEdit >= 0 && g_opText[0]) { char* end; double v = strtod(g_opText, &end); if (end != g_opText) OpAsk(g_opEdit, v); }
    g_opEdit = -1;
}
static int OpIn(const HfInput* in, float x0, float y0, float x1, float y1) { return in->x >= x0 && in->x < x1 && in->y >= y0 && in->y < y1; }
static int OptionsDraw(void* user, const HfDraw* d, const HfInput* in, float dt)
{
    if (in->closing) { g_opDrag = -1; OpEditCommit(); return 0; }
    g_opOpenT += dt;
    float a = g_opOpenT < 0.12f ? g_opOpenT / 0.12f : 1;                  /* a short fade in */
    uint32_t A = (uint32_t)(a * 255);
    const HfColour yellow = HF_RGBA(255, 232, 0, A), white = HF_RGBA(240, 240, 240, A), grey = HF_RGBA(150, 154, 158, A), ink = HF_RGBA(14, 16, 18, A),
                   track = HF_RGBA(70, 74, 78, A), orange = HF_RGBA(255, 138, 36, A);
    int click = (in->pressed & HF_MOUSE_LEFT) != 0;
    d->backdrop(0.55f * a, 0.35f * a, 10);
    const float W = 1180, x0 = (d->width - W) / 2, y0 = 70, y1 = 1010, x1 = x0 + W;
    d->rect(x0, y0, x1, y1, HF_RGBA(14, 16, 18, (uint32_t)(238 * a)), yellow, 2);
    d->rect(x0, y0, x1, y0 + 64, yellow, 0, 0);
    d->text(x0 + 28, y0 + 32, "VECTOR", 34, HF_FACE_TITLE, HF_ALIGN_LEFT, ink);
    d->text(x1 - 28, y0 + 32, "OPTIONS  " VERSION, 22, HF_FACE_TITLE, HF_ALIGN_RIGHT, ink);
    const char* tip = NULL;

    /* the character kits: one chip each and Off, above the list; the picked one lit */
    float top = y0 + 78;
    g_opKitsT += dt;
    if (g_opKitsT > 0.5f) { g_opKitsT = 0; OpKitsRead(); }
    if (g_opKits.n >= 2)
    {
        d->text(x0 + 28, top + 16, "Active Character", 22, HF_FACE_TITLE, HF_ALIGN_LEFT, yellow);
        d->text(x1 - 28, top + 16, "from the next mission start", 17, HF_FACE_BODY, HF_ALIGN_RIGHT, grey);
        float cx = x0 + 28, cy = top + 40;
        int wanted = atomic_load(&g_opPickWant);
        for (int i = 0; i < g_opKits.n; i++)
        {
            float w = d->text_width(g_opKits.name[i], 21, HF_FACE_TITLE) + 36;
            if (cx + w > x1 - 28) { cx = x0 + 28; cy += 48; }
            int on = wanted >= 0 ? i == wanted : i == g_opKits.active, over = OpIn(in, cx, cy, cx + w, cy + 38);
            int off = !strcmp(g_opKits.id[i], OP_OFF_ID);
            d->rect(cx, cy, cx + w, cy + 38, on ? (off ? HF_RGBA(200, 60, 50, A) : yellow) : HF_RGBA(40, 43, 46, A), over && !on ? yellow : 0, 2);
            d->text(cx + w / 2, cy + 19, g_opKits.name[i], 21, HF_FACE_TITLE, HF_ALIGN_CENTRE, on ? (off ? white : ink) : white);
            if (over) tip = off ? "No character kit acts: a plain Helldiver. From the next mission start." : "Only the picked kit acts (keys, body, skills, HUD). From the next mission start.";
            if (over && click) { OpEditCommit(); atomic_store(&g_opPickWant, i); }
            cx += w + 10;
        }
        top = cy + 38 + 14;
        d->rect(x0 + 28, top - 6, x1 - 28, top - 4, HF_RGBA(255, 232, 0, (uint32_t)(70 * a)), 0, 0);
    }

    /* the list: a group's heading, then its rows */
    const float bottom = y1 - 96, rowH = 46, headH = 44;
    const float lx = x0 + 44, tx0 = x0 + 560, tx1 = x0 + 980, vx = x1 - 44;
    float content = 0;
    for (int i = 0; i < OPROWS; i++) content += rowH + ((i == 0 || strcmp(kOpRows[i].group, kOpRows[i - 1].group)) ? headH : 0);
    float maxScroll = content - (bottom - top);
    if (maxScroll < 0) maxScroll = 0;
    g_opScroll -= in->wheel * rowH * 2;
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
        if (key == VK_DOWN) { g_opSel = g_opSel < OPROWS - 1 ? g_opSel + 1 : OPROWS - 1; keyMoved = 1; }
        if (key == VK_UP) { g_opSel = g_opSel > 0 ? g_opSel - 1 : 0; keyMoved = 1; }
        if ((key == VK_LEFT || key == VK_RIGHT) && g_opSel >= 0)
        {
            if (kOpRows[g_opSel].kind == OP_TOGGLE) OpAsk(g_opSel, key == VK_RIGHT);
            else OpAsk(g_opSel, OpShown(g_opSel) + (key == VK_RIGHT ? 1 : -1) * kOpRows[g_opSel].step);
        }
        if ((key == VK_RETURN || key == VK_SPACE) && g_opSel >= 0)
        {
            if (kOpRows[g_opSel].kind == OP_TOGGLE) OpAsk(g_opSel, OpShown(g_opSel) == 0);
            else if (kOpRows[g_opSel].kind == OP_FIELD) { g_opEdit = g_opSel; g_opText[0] = 0; }
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
        for (int i = 0; i <= g_opSel; i++) { if (i == 0 || strcmp(kOpRows[i].group, kOpRows[i - 1].group)) y += headH; if (i < g_opSel) y += rowH; }
        if (y - headH < g_opScroll) g_opScroll = y - headH;
        if (y + rowH > g_opScroll + (bottom - top)) g_opScroll = y + rowH - (bottom - top);
    }
    if (g_opScroll > maxScroll) g_opScroll = maxScroll;
    if (g_opScroll < 0) g_opScroll = 0;

    d->clip_push(x0 + 2, top, x1 - 2, bottom);
    float y = top - g_opScroll;
    for (int i = 0; i < OPROWS; i++)
    {
        const OpRow* r = &kOpRows[i];
        if (i == 0 || strcmp(r->group, kOpRows[i - 1].group))
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
            if (over && (in->pressed & HF_MOUSE_RIGHT)) { if (g_opEdit == i) g_opEdit = -1; OpAsk(i, g_opIni[i]); }
            if (r->kind == OP_TOGGLE)
            {
                /* a switch: a pill with its knob at the right when on */
                const float sw = 76, sh = 32, sx = vx - sw, sy = cy - sh / 2;
                int on = v != 0;
                d->rect(sx, sy, vx, sy + sh, on ? (sel ? yellow : orange) : track, on ? 0 : grey, 2);
                float kx = on ? vx - sh + 4 : sx + 4;
                d->rect(kx, sy + 4, kx + sh - 8, sy + sh - 4, on ? ink : white, 0, 0);
                d->text(sx - 16, cy, text, 22, HF_FACE_TITLE, HF_ALIGN_RIGHT, changed ? yellow : white);
                if (over && click && in->x >= tx0) { OpEditCommit(); OpAsk(i, !on); }
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
                    float w = d->text(fx1 - 14, cy, g_opText, 22, HF_FACE_TITLE, HF_ALIGN_RIGHT, yellow);
                    (void)w;
                    if (fmodf(g_opOpenT, 1.0f) < 0.5f) d->rect(fx1 - 11, cy - 11, fx1 - 9, cy + 11, yellow, 0, 0);
                }
                else d->text(fx1 - 14, cy, text, 22, HF_FACE_TITLE, HF_ALIGN_RIGHT, changed ? yellow : white);
                if (click && (overM || overP)) { OpEditCommit(); OpAsk(i, OpShown(i) + (overP ? 1 : -1) * r->step); }
                if (click && overF) { if (g_opEdit != i) { OpEditCommit(); g_opEdit = i; g_opText[0] = 0; } clickedField = 1; }
            }
            else
            {
                /* the slider: its track, the part up to the value, a handle; a tick where vector.ini's value is */
                float span = r->hi - r->lo, t = span > 0 ? (float)((v - r->lo) / span) : 0;
                float ti = span > 0 ? (float)((g_opIni[i] - r->lo) / span) : 0;
                t = t < 0 ? 0 : t > 1 ? 1 : t;
                d->rect(tx0, cy - 3, tx1, cy + 3, track, 0, 0);
                d->rect(tx0, cy - 3, tx0 + (tx1 - tx0) * t, cy + 3, sel ? yellow : orange, 0, 0);
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
    if (!tip && tipRow >= 0) tip = kOpRows[tipRow].tip;
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
    if (tipRow >= 0 && kOpRows[tipRow].kind == OP_FIELD)
    {
        char range[80], lo[32], hi[32];
        OpValueText(tipRow, g_opLo[tipRow], lo); OpValueText(tipRow, g_opHi[tipRow], hi);
        snprintf(range, sizeof range, "%s to %s", lo, hi);
        d->text(x1 - 28, y1 - 18, range, 17, HF_FACE_BODY, HF_ALIGN_RIGHT, grey);
    }
    d->text(x0 + 28, y1 - 18, "Click a field to type, Enter takes it  |  Right click or Backspace: back to vector.ini's value  |  Esc: close and save",
            17, HF_FACE_BODY, HF_ALIGN_LEFT, grey);
    return 1;
}
static void OptionsOpened(void* user, int open)
{
    if (open) { g_opOpenT = 0; g_opDrag = -1; g_opEdit = -1; g_opKitsT = 1; }
    else atomic_store(&g_opSave, 1);
}
/* at init, after the settings: the screen, where this HellForge has screens */
static void OptionsInit(const HfApi* api, HfMod* mod)
{
    if (api->version < 17 || api->size < offsetof(HfApi, screen_show) + sizeof api->screen_show || s_optionsKey <= 0)
    {
        api->log(mod, "options screen: %s; her numbers stay on the MODS tab (Mod Options Menu)",
                 s_optionsKey <= 0 ? "off (options_key 0)" : "this HellForge has no screens over the game (SDK 17)");
        return;
    }
    HfScreen screen = {sizeof screen, "Vector options", (uint32_t)s_optionsKey, HF_SCREEN_MODAL, NULL, OptionsDraw, OptionsOpened, NULL};
    g_opScreen = api->add_screen(mod, &screen);
    if (g_opScreen)
    {
        RosterAddOff();
        api->log(mod, "options screen: key %d opens it (options_key; 120 is F9); the character kits' picker is on it, with an Off%s%s", s_optionsKey,
                 g_opPickSaved[0] ? "; picked last time: " : "", g_opPickSaved);
    }
    else api->log(mod, "options screen: refused (modloader.log says why; the manifest needs \"sdk\": 17); her numbers stay on the MODS tab");
}
#endif
