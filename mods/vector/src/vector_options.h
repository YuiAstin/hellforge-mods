/* Vector's own options screen (0.2.0), drawn over the game by HellForge's overlay host (SDK 17: add_screen;
 * docs/modding/overlay.md). It takes the Mod Options Menu's place where the host is there: the same numbers, the same
 * names as vector.ini. Included by vector.c only when the SDK has screens (HF_SCREEN_MODAL).
 *
 * Threads: the screen is drawn on the host's render thread. It never calls the game: a changed value waits in g_opWant
 * and Tick (the game thread) sets it (OptionsTick -> OptionSet). What differs from vector.ini is saved to
 * %LOCALAPPDATA%\HellForgeMods\vector.ini when the screen closes (HellForge keeps mod folders read-only while the game
 * runs) and read after vector.ini at the next start. */
#ifndef VECTOR_OPTIONS_H
#define VECTOR_OPTIONS_H

typedef struct OpRow { const char* name; const char* group; const char* label; float lo, hi, step; const char* tip; } OpRow;
static const OpRow kOpRows[] = {
    {"conf_max", "Confectance", "Most", 2, 20, 1,
     "Points at which the Confectance Index is spent for its damage boost (each skill gives 2)."},
    {"conf_mult", "Confectance", "Damage", 1, 3, 0.05,
     "Her hits deal this times their damage while the boost runs."},
    {"conf_seconds", "Confectance", "Duration", 0, 60, 1,
     "Seconds the boost runs."},
    {"s1_cooldown", "Dead End Meltdown", "Cooldown", 0, 120, 1,
     "Seconds before Dead End Meltdown can be used again."},
    {"s1_seconds", "Dead End Meltdown", "Duration", 2, 60, 1,
     "Seconds her shots set what they hit on fire."},
    {"s1_burst_every", "Dead End Meltdown", "Burst Interval", 0.5, 10, 0.5,
     "Seconds between fire bursts on the same burning enemy."},
    {"overburn_seconds", "Overburn", "Duration", 0, 30, 1,
     "Seconds an enemy stays Overburned after a burst."},
    {"overburn_mult", "Overburn", "Fire Damage", 1, 3, 0.05,
     "Fire damage on an Overburned enemy, times."},
    {"s2_cooldown", "Portent of Doom", "Cooldown", 0, 120, 1,
     "Seconds before Portent of Doom can be used again."},
    {"smolder_seconds", "Smolder", "Duration", 0, 60, 1,
     "Seconds her target Smolders."},
    {"smolder_mult", "Smolder", "Fire Damage", 1, 4, 0.05,
     "Fire damage on a Smoldering enemy, times."},
    {"s3_cooldown", "Searing Finale", "Cooldown", 0, 300, 5,
     "Seconds before Searing Finale can be used again."},
    {"s3_shells", "Searing Finale", "Shells", 1, 12, 1,
     "Napalm shells that fall where she aims."},
    {"s3_spread", "Searing Finale", "Spread", 0, 30, 1,
     "Metres around her aim the shells after the first land in."},
    {"accelerant_seconds", "Accelerant", "Duration", 0, 60, 1,
     "Seconds of Accelerant after Searing Finale."},
    {"accelerant_mult", "Accelerant", "Fire Damage", 1, 3, 0.05,
     "Fire damage on enemies during Accelerant, times."},
    {"fire_immune", "Perception Block", "Fireproof", 0, 1, 1,
     "1: fire does nothing to her. 0: she burns like anyone."},
    {"own_burn", "Her Own Burn", "On Armour", 0, 1, 1,
     "1: tanks, walkers and gunships, which the game's fire cannot hurt, burn by her hand. 0: they stay fireproof."},
    {"own_burn_damage", "Her Own Burn", "Damage A Tick", 1, 100, 1,
     "Health burnt off armoured enemies four times a second, past their armour. A soldier's burn is 25."},
    {"own_burn_percent", "Her Own Burn", "Percent Of Health A Tick", 0, 10, 0.25,
     "Share of the enemy's full health burnt off four times a second. 1: burnt through in 25 s of unbroken burning. 0: the flat damage only."},
    {"own_burn_seconds", "Her Own Burn", "Seconds", 1, 30, 1,
     "How long her own burn lasts after her fire."},
    {"hp_mult", "Body", "Health", 0.5, 4, 0.05,
     "Her health, times a Helldiver's."},
    {"stamina_mult", "Body", "Stamina", 0.5, 6, 0.1,
     "How long she sprints before the bar empties, times a Helldiver's."}
};
#define OPROWS (int)(sizeof kOpRows / sizeof kOpRows[0])

static int g_opScreen;                 /* its id; 0: no screen (an older HellForge), the Mod Options Menu stays */
static double g_opIni[OPROWS];         /* each row's value from vector.ini (or its built-in default): what "reset" means */
static _Atomic double g_opWant[OPROWS];
static atomic_uint g_opDirty[OPROWS];
static atomic_uint g_opSave;
static char g_opPath[MAX_PATH * 2];

static double OpNow(int i) { double v = 0; OptionGet(kOpRows[i].name, &v); return v; }
static double OpSnap(const OpRow* r, double v)
{
    if (r->step > 0) v = r->lo + floor((v - r->lo) / r->step + 0.5) * r->step;
    return v < r->lo ? r->lo : v > r->hi ? r->hi : v;
}
static void OpAsk(int i, double v)
{
    atomic_store(&g_opWant[i], OpSnap(&kOpRows[i], v));
    atomic_store(&g_opDirty[i], 1);
}
/* what the screen shows: a value asked for and not set yet, else the live one */
static double OpShown(int i) { return atomic_load(&g_opDirty[i]) ? atomic_load(&g_opWant[i]) : OpNow(i); }

/* after vector.ini: its values kept as each row's reset, then the screen's saved choices */
static void OptionsLoad(void)
{
    for (int i = 0; i < OPROWS; i++) g_opIni[i] = OpNow(i);
    char local[MAX_PATH], line[256];
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", local, sizeof local);
    if (!n || n >= sizeof local) return;
    snprintf(g_opPath, sizeof g_opPath, "%s\\HellForgeMods\\vector.ini", local);
    FILE* f = fopen(g_opPath, "r");
    if (!f) return;
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
}

/* ---- drawing ---------------------------------------------------------------------------------------------------- */
#define OP_YELLOW HF_RGBA(255, 232, 0, 255)
#define OP_WHITE HF_RGBA(240, 240, 240, 255)
#define OP_GREY HF_RGBA(150, 154, 158, 255)
#define OP_ORANGE HF_RGBA(255, 138, 36, 255)
static float g_opScroll, g_opOpenT;
static int g_opDrag = -1, g_opSel = -1;

static void OpValueText(const OpRow* r, double v, char out[32])
{
    if (r->lo == 0 && r->hi == 1 && r->step == 1) snprintf(out, 32, "%s", v != 0 ? "ON" : "OFF");
    else if (r->step >= 1) snprintf(out, 32, "%.0f", v);
    else if (r->step >= 0.1f) snprintf(out, 32, "%.1f", v);
    else snprintf(out, 32, "%.2f", v);
}
static int OptionsDraw(void* user, const HfDraw* d, const HfInput* in, float dt)
{
    if (in->closing) { g_opDrag = -1; return 0; }
    g_opOpenT += dt;
    float a = g_opOpenT < 0.12f ? g_opOpenT / 0.12f : 1;                  /* a short fade in */
    uint32_t A = (uint32_t)(a * 255);
    d->backdrop(0.55f * a, 0.35f * a, 10);
    const float W = 1180, x0 = (d->width - W) / 2, y0 = 90, y1 = 990, x1 = x0 + W;
    d->rect(x0, y0, x1, y1, HF_RGBA(14, 16, 18, (uint32_t)(238 * a)), HF_RGBA(255, 232, 0, A), 2);
    d->rect(x0, y0, x1, y0 + 64, HF_RGBA(255, 232, 0, A), 0, 0);
    d->text(x0 + 28, y0 + 32, "VECTOR", 34, HF_FACE_TITLE, HF_ALIGN_LEFT, HF_RGBA(14, 16, 18, A));
    d->text(x1 - 28, y0 + 32, "OPTIONS  " VERSION, 22, HF_FACE_TITLE, HF_ALIGN_RIGHT, HF_RGBA(14, 16, 18, A));

    /* the list: a group's heading, then its rows */
    const float top = y0 + 78, bottom = y1 - 96, rowH = 46, headH = 44;
    const float lx = x0 + 44, tx0 = x0 + 560, tx1 = x0 + 980, vx = x1 - 44;
    float content = 0;
    for (int i = 0; i < OPROWS; i++) content += rowH + ((i == 0 || strcmp(kOpRows[i].group, kOpRows[i - 1].group)) ? headH : 0);
    float maxScroll = content - (bottom - top);
    if (maxScroll < 0) maxScroll = 0;
    g_opScroll -= in->wheel * rowH * 2;
    for (uint32_t k = 0; k < in->key_count; k++)
    {
        uint32_t key = in->keys[k];
        if (key == VK_DOWN) g_opSel = g_opSel < OPROWS - 1 ? g_opSel + 1 : OPROWS - 1;
        if (key == VK_UP) g_opSel = g_opSel > 0 ? g_opSel - 1 : 0;
        if ((key == VK_LEFT || key == VK_RIGHT) && g_opSel >= 0) OpAsk(g_opSel, OpShown(g_opSel) + (key == VK_RIGHT ? 1 : -1) * kOpRows[g_opSel].step);
        if ((key == VK_BACK || key == VK_DELETE) && g_opSel >= 0) OpAsk(g_opSel, g_opIni[g_opSel]);
        if (key == VK_NEXT) g_opScroll += (bottom - top) * 0.8f;
        if (key == VK_PRIOR) g_opScroll -= (bottom - top) * 0.8f;
    }
    if (!(in->down & HF_MOUSE_LEFT)) g_opDrag = -1;
    int inList = in->x >= x0 && in->x <= x1 && in->y >= top && in->y <= bottom;
    int hover = -1, keyMoved = 0;
    for (uint32_t k = 0; k < in->key_count; k++) if (in->keys[k] == VK_DOWN || in->keys[k] == VK_UP) keyMoved = 1;
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
                d->text(lx - 16, y + headH * 0.58f, r->group, 22, HF_FACE_TITLE, HF_ALIGN_LEFT, HF_RGBA(255, 232, 0, A));
                d->rect(lx - 16, y + headH - 5, x1 - 28, y + headH - 3, HF_RGBA(255, 232, 0, (uint32_t)(70 * a)), 0, 0);
            }
            y += headH;
        }
        if (y + rowH > top && y < bottom)
        {
            int over = inList && g_opDrag < 0 && in->y >= y && in->y < y + rowH;
            if (over) { hover = i; if (in->pressed || !keyMoved) g_opSel = i; }
            int sel = i == g_opSel || i == g_opDrag;
            if (sel) d->rect(x0 + 14, y + 2, x1 - 14, y + rowH - 2, HF_RGBA(255, 255, 255, (uint32_t)(22 * a)), 0, 0);
            double v = OpShown(i);
            int changed = fabs(v - g_opIni[i]) > 1e-6;
            d->text(lx, y + rowH / 2, r->label, 22, HF_FACE_BODY, HF_ALIGN_LEFT, HF_RGBA(240, 240, 240, A));
            /* the slider: its track, the part up to the value, a handle; a tick where vector.ini's value is */
            float span = r->hi - r->lo, t = span > 0 ? (float)((v - r->lo) / span) : 0, cy = y + rowH / 2;
            float ti = span > 0 ? (float)((g_opIni[i] - r->lo) / span) : 0;
            d->rect(tx0, cy - 3, tx1, cy + 3, HF_RGBA(70, 74, 78, A), 0, 0);
            d->rect(tx0, cy - 3, tx0 + (tx1 - tx0) * t, cy + 3, sel ? HF_RGBA(255, 232, 0, A) : HF_RGBA(255, 138, 36, A), 0, 0);
            if (ti >= 0 && ti <= 1) d->rect(tx0 + (tx1 - tx0) * ti - 1, cy - 9, tx0 + (tx1 - tx0) * ti + 1, cy + 9, HF_RGBA(150, 154, 158, A), 0, 0);
            d->circle(tx0 + (tx1 - tx0) * t, cy, sel ? 10 : 8, sel ? HF_RGBA(255, 232, 0, A) : HF_RGBA(240, 240, 240, A), 0, 0);
            char text[32];
            OpValueText(r, v, text);
            d->text(vx, cy, text, 22, HF_FACE_TITLE, HF_ALIGN_RIGHT, changed ? HF_RGBA(255, 232, 0, A) : HF_RGBA(240, 240, 240, A));
            if (over && (in->pressed & HF_MOUSE_RIGHT)) OpAsk(i, g_opIni[i]);
            if (over && (in->pressed & HF_MOUSE_LEFT) && in->x >= tx0 - 16 && in->x <= tx1 + 16) g_opDrag = i;
            /* a click on the label or the value: one step down (left half) or up (right half) is not offered: only the track drags */
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
    /* a scroll bar when the list is longer than its box */
    if (maxScroll > 0)
    {
        float h = bottom - top, bar = h * h / content, by = top + (h - bar) * (g_opScroll / maxScroll);
        d->rect(x1 - 12, top, x1 - 8, bottom, HF_RGBA(70, 74, 78, (uint32_t)(120 * a)), 0, 0);
        d->rect(x1 - 12, by, x1 - 8, by + bar, HF_RGBA(255, 232, 0, A), 0, 0);
    }
    /* the footer: what the row under the mouse (or chosen by the keys) does, and the screen's own keys */
    int tipRow = hover >= 0 ? hover : g_opDrag >= 0 ? g_opDrag : g_opSel;
    d->rect(x0 + 2, bottom + 8, x1 - 2, bottom + 10, HF_RGBA(255, 232, 0, (uint32_t)(70 * a)), 0, 0);
    if (tipRow >= 0)
    {
        /* one line, cut to the panel: the tip is short by design */
        d->clip_push(x0 + 28, bottom + 12, x1 - 28, bottom + 56);
        d->text(x0 + 28, bottom + 36, kOpRows[tipRow].tip, 20, HF_FACE_BODY, HF_ALIGN_LEFT, HF_RGBA(240, 240, 240, A));
        d->clip_pop();
    }
    d->text(x0 + 28, y1 - 24, "Drag a slider or use the arrow keys  |  Right click or Backspace: back to vector.ini's value  |  Esc: close and save",
            17, HF_FACE_BODY, HF_ALIGN_LEFT, HF_RGBA(150, 154, 158, A));
    return 1;
}
static void OptionsOpened(void* user, int open)
{
    if (open) { g_opOpenT = 0; g_opDrag = -1; }
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
    if (g_opScreen) api->log(mod, "options screen: key %d opens it (options_key; 120 is F9)", s_optionsKey);
    else api->log(mod, "options screen: refused (modloader.log says why; the manifest needs \"sdk\": 17); her numbers stay on the MODS tab");
}
#endif
