/* Stat Tuner (yui.stat-tuner): per-item stat sliders for HellForge, on the MODS tab of the Universal Mod Menu
 * (Bingus Vanilla Megapack REFORGED), with eight preset slots.
 *
 * How it works
 *   - stat_data.h (generated from HellForge's editor catalogue by tools/gen_stat_data.py) lists every armory item the
 *     F8 editor knows, grouped into pages of at most 16 (the menu's choice limit). Each slider ("slot") drives one or
 *     more catalogue fields: "Spread" both spreads, "Recoil" all four recoil inputs, "Damage" every fire mode.
 *   - A page has an Item row (pick, press APPLY), then the sliders, which then show and edit that item.
 *   - Sliders are relative: 100% is the game's (or a lower mod's) value, read with get_live as the field's base.
 *     Armor penetration and armor are +/- steps instead. Uses at 1000% means unlimited.
 *   - Every change is a HellForge live edit (set_live), kept for the session and renewed when the game reloads its
 *     data. Values are saved in stat-tuner-current.ini in this folder and applied every session, with or without the
 *     menu. Presets are stat-tuner-preset-<n>.ini; both are plain text (Item name|slot=value) you can edit or share.
 *   - The F8 editor's own changes (Local Stats) sit above code mods and win a field they both change.
 *
 * The menu is reached through umm_api.h, the Universal Mod Menu's cross-DLL ABI (as Scope of Authority uses it). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <windows.h>
#include "hellforge_mod.h"
#include "umm_api.h"
#include "stat_data.h"

#define ST_LAYOUT 25480438u
#define PRESETS 8
#define RETRY_SECONDS 5.0f

static HfMod* g_mod;
static const HfApi* g_hf;

static float g_val[ST_ITEMS][ST_MAX_TEMPLATE_SLOTS];      /* the value the player chose */
static float g_applied[ST_ITEMS][ST_MAX_TEMPLATE_SLOTS];  /* the value in the game now (NAN: the game's own) */
static uint8_t g_pending[ST_ITEMS][ST_MAX_TEMPLATE_SLOTS];/* some field could not be read yet: try again */
static uint16_t g_itemPage[ST_ITEMS];
static int g_cur[ST_PAGES];                               /* the item each page shows (0-based, within the page) */
static int g_enabled = 1;
static int g_slot;                                        /* preset slot, 0-based */
static int g_anyPending;
static int g_dirty = 1;
static uint32_t g_refusals;
static uint8_t g_tries[ST_ITEMS][ST_MAX_TEMPLATE_SLOTS];  /* pending retries (gives up after MAX_TRIES) */
static uint16_t g_fieldItem[ST_FIELDS];                   /* the item and slot each field belongs to */
static uint8_t g_fieldSlot[ST_FIELDS];
static int g_show[ST_PAGES];                              /* menu work left for when the menu can be reached */
static int g_resetAction;
#define MAX_TRIES 24
static uint8_t g_fieldDone[ST_FIELDS];                  /* the field holds the slot's current value */

/* ---- data helpers -------------------------------------------------------------------------------------------- */
static const StTemplate* ItemTemplate(int item) { return &kTemplates[kPages[g_itemPage[item]].tpl]; }
static const StSlot* ItemSlot(int item, int s) { return &kSlots[ItemTemplate(item)->first + s]; }
static float Default(int mode) { return mode == ST_DELTA ? 0.0f : 100.0f; }
static float SlotDefault(int item, int s) { return Default(ItemSlot(item, s)->mode); }

static uint16_t TypeSize(uint8_t type)
{
    switch (type)
    {
    case HF_TYPE_U8: return 1;
    case HF_TYPE_U16: return 2;
    default: return 4;
    }
}

/* 1 applied (or undone), 0 cannot be read yet, -1 refused */
static int ApplyField(const StField* f, int mode, float v, int undo)
{
    HfField field = {kComps[f->comp], ST_LAYOUT, f->off, TypeSize(f->type), f->type, __LINE__};
    uint32_t flags = f->shared ? HF_LIVE_SHARED : 0;
    if (undo)
    {
        g_hf->set_live(g_mod, f->ref, &field, NULL, flags);
        return 1;
    }
    HfLiveValue live;
    memset(&live, 0, sizeof live);
    if (!g_hf->get_live(g_mod, f->ref, &field, &live)) return 0;
    double base = live.base.number, x;
    if (mode == ST_USES && base >= 4294967295.0 && v < 1000.0f) return 1;  /* already unlimited: leave it */
    int unlimited = mode == ST_USES && v >= 1000.0f;
    if (mode == ST_DELTA) x = base + v;
    else if (unlimited) x = 4294967295.0;
    else x = base * v / 100.0;
    if (!unlimited)
    {
        if (!isnan(f->min) && x < f->min) x = f->min;
        if (!isnan(f->max) && x > f->max) x = f->max;
        if (mode == ST_DELTA && x < 0) x = 0;
        if (mode == ST_DELTA && x > 10) x = 10;
    }
    if (f->type != HF_TYPE_F32)
    {
        x = floor(x + 0.5);
        if (mode != ST_DELTA && base >= 1.0 && v > 0 && x < 1.0) x = 1.0;  /* 10% of one pellet is still one */
        double lo = f->type == HF_TYPE_I32 ? -2147483648.0 : 0.0;
        double hi = f->type == HF_TYPE_U8 ? 255.0 : f->type == HF_TYPE_U16 ? 65535.0 : f->type == HF_TYPE_I32 ? 2147483647.0 : 4294967295.0;
        if (x < lo) x = lo;
        if (x > hi) x = hi;
    }
    else if (!isfinite(x)) return -1;
    HfValue value = {f->type, x, 0};
    if (g_hf->set_live(g_mod, f->ref, &field, &value, flags)) return 1;
    if (g_refusals++ < 40)
        g_hf->log(g_mod, "refused: %s +%u on %llX (%s)", kComps[f->comp], (unsigned)f->off, (unsigned long long)f->ref,
            g_hf->last_log ? g_hf->last_log(g_mod) : "");
    return -1;
}

static void ApplySlot(int item, int s)
{
    float def = SlotDefault(item, s);
    float want = g_enabled ? g_val[item][s] : def;
    int isDefault = want == def;
    float now = g_applied[item][s];
    if (isDefault && isnan(now)) { g_pending[item][s] = 0; return; }
    if (!g_pending[item][s] && now == want) return;
    const StRange* r = &kRanges[kItems[item].ranges + s];
    int mode = ItemSlot(item, s)->mode, pending = 0;
    int undo = isDefault && !isnan(now);
    if (now != want || undo)  /* a new value: every field again */
        for (int i = 0; i < r->count; i++) g_fieldDone[r->first + i] = 0;
    for (int i = 0; i < r->count; i++)
    {
        const StField* f = &kFields[r->first + i];
        if (g_fieldDone[r->first + i]) continue;  /* a retry touches only the fields not yet written */
        int res = ApplyField(f, mode, want, undo);
        if (res == 0) pending = 1;
        else g_fieldDone[r->first + i] = 1;
        if (!undo) continue;
        /* the undo put back the game's value: another item or slot that edits the same field applies again */
        for (int j = 0; j < ST_FIELDS; j++)
        {
            const StField* o = &kFields[j];
            if (o == f || o->ref != f->ref || o->comp != f->comp || o->off != f->off) continue;
            int oi = g_fieldItem[j], os = g_fieldSlot[j];
            if (!isnan(g_applied[oi][os])) { g_pending[oi][os] = 1; g_tries[oi][os] = 0; g_fieldDone[j] = 0; g_dirty = 1; }
        }
    }
    if (pending && ++g_tries[item][s] >= MAX_TRIES)
    {
        g_hf->log(g_mod, "%s / %s: some of its fields could not be read; given up for now", kItems[item].name, ItemSlot(item, s)->key);
        pending = 0;
    }
    if (!pending) g_tries[item][s] = 0;
    g_pending[item][s] = (uint8_t)pending;
    g_applied[item][s] = isDefault ? NAN : want;
    if (pending) g_anyPending = 1;
}

static void ApplyAll(void)
{
    g_anyPending = 0;
    for (int i = 0; i < ST_ITEMS; i++)
        for (int s = 0; s < ItemTemplate(i)->count; s++) ApplySlot(i, s);
}

static void ResetItem(int item)
{
    for (int s = 0; s < ItemTemplate(item)->count; s++) g_val[item][s] = SlotDefault(item, s);
}

/* ---- files: "Item name|slot=value" ---------------------------------------------------------------------------- */
static void FilePath(char* out, size_t n, int preset)
{
    if (preset < 0) snprintf(out, n, "%s\\stat-tuner-current.ini", g_hf->mod_folder(g_mod));
    else snprintf(out, n, "%s\\stat-tuner-preset-%d.ini", g_hf->mod_folder(g_mod), preset + 1);
}

static FILE* OpenUtf8(const char* path, const wchar_t* mode)
{
    wchar_t wide[MAX_PATH * 2];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, (int)(sizeof wide / sizeof wide[0]))) return NULL;
    return _wfopen(wide, mode);
}

static int SaveFile(int preset)
{
    char path[MAX_PATH * 2];
    FilePath(path, sizeof path, preset);
    FILE* f = OpenUtf8(path, L"w");
    if (!f) return 0;
    fprintf(f, "; Stat Tuner values: Item name|slot = value. 100 is the game's value (+/- slots: 0).\n");
    fprintf(f, "enabled = %d\n", g_enabled);
    int n = 0;
    for (int i = 0; i < ST_ITEMS; i++)
        for (int s = 0; s < ItemTemplate(i)->count; s++)
            if (g_val[i][s] != SlotDefault(i, s))
            {
                fprintf(f, "%s|%s = %g\n", kItems[i].name, ItemSlot(i, s)->key, (double)g_val[i][s]);
                n++;
            }
    fclose(f);
    return n + 1;
}

/* 0 no file, else 1 + the values read */
static int LoadFile(int preset)
{
    char path[MAX_PATH * 2], raw[256];
    FilePath(path, sizeof path, preset);
    FILE* f = OpenUtf8(path, L"r");
    if (!f) return 0;
    for (int i = 0; i < ST_ITEMS; i++) ResetItem(i);
    int n = 0;
    while (fgets(raw, sizeof raw, f))
    {
        char* line = raw;
        if ((unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) line += 3;
        while (*line == ' ' || *line == '\t') line++;
        if (line[0] == ';' || line[0] == '#') continue;
        char* eq = strrchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char* end = eq - 1;
        while (end >= line && (*end == ' ' || *end == '\t')) *end-- = 0;
        double v = strtod(eq + 1, NULL);
        if (!isfinite(v)) continue;
        if (!strcmp(line, "enabled")) { if (preset < 0) g_enabled = v != 0; continue; }
        char* bar = strrchr(line, '|');
        if (!bar) continue;
        *bar = 0;
        for (int i = 0; i < ST_ITEMS; i++)
            if (!strcmp(kItems[i].name, line))
            {
                for (int s = 0; s < ItemTemplate(i)->count; s++)
                    if (!strcmp(ItemSlot(i, s)->key, bar + 1))
                    {
                        int delta = ItemSlot(i, s)->mode == ST_DELTA;
                        double lo = delta ? -6.0 : 0.0, hi = delta ? 6.0 : 1000.0;
                        v = delta ? floor(v + 0.5) : floor(v / 5.0 + 0.5) * 5.0;  /* the sliders' steps */
                        g_val[i][s] = (float)(v < lo ? lo : v > hi ? hi : v);
                        n++;
                    }
                break;
            }
    }
    fclose(f);
    return n + 1;
}

/* ---- the menu (umm_api.h) -------------------------------------------------------------------------------------- */
typedef struct Arena { umm_arena a; void* blocks[2048]; int n; } Arena;
static void* ArenaAlloc(umm_arena* self, size_t bytes)
{
    Arena* A = (Arena*)self->impl;
    if (A->n >= (int)(sizeof A->blocks / sizeof A->blocks[0])) return NULL;
    void* p = _aligned_malloc(bytes ? bytes : 1, 16);
    if (!p) return NULL;
    memset(p, 0, bytes ? bytes : 1);
    A->blocks[A->n++] = p;
    return p;
}
static void ArenaInit(Arena* A) { A->a.alloc = ArenaAlloc; A->a.impl = A; A->n = 0; }
static void ArenaFree(Arena* A) { for (int i = 0; i < A->n; i++) _aligned_free(A->blocks[i]); A->n = 0; }

static umm_value Str(const char* s) { umm_value v = {UMM_STR, (uint32_t)strlen(s), {0}}; v.u.s = s; return v; }
static umm_value Num(double n) { umm_value v = {UMM_NUM, 0, {0}}; v.u.n = n; return v; }
static umm_value Tab(umm_table* t) { umm_value v = {UMM_TAB, 0, {0}}; v.u.t = t; return v; }
static const umm_value* Field(const umm_value* tab, const char* key)
{
    if (!tab || tab->type != UMM_TAB || !tab->u.t) return NULL;
    for (uint32_t i = 0; i < tab->u.t->count; i++)
    {
        const umm_pair* p = &tab->u.t->pairs[i];
        if (p->key.type == UMM_STR && !strcmp(p->key.u.s, key)) return &p->value;
    }
    return NULL;
}

typedef struct Menu { Arena snap; const umm_value *reg, *get, *set, *onChange; int sections; } Menu;
static Menu g_menu;
static Arena g_out;
static const umm_api* g_api;

static int MenuOpen(void)
{
    umm_value mom;
    uint64_t instance = 0;
    ArenaInit(&g_menu.snap);
    if (!g_api || !g_api->snapshot("ModOptionsMenu", &g_menu.snap.a, &mom, &instance)) { ArenaFree(&g_menu.snap); return 0; }
    static umm_value root;
    root = mom;
    const umm_value* menu = Field(&root, "api");
    if (!menu || menu->type != UMM_TAB) menu = &root;
    g_menu.reg = Field(menu, "register_option");
    g_menu.get = Field(menu, "get");
    g_menu.set = Field(menu, "set");
    g_menu.onChange = Field(menu, "on_change");
    const umm_value* sec = Field(&root, "sections");
    g_menu.sections = (g_api->extensions & UMM_EXT_SECTIONS) && sec && sec->type == UMM_NUM && sec->u.n >= 1;
    if (!g_menu.reg || !g_menu.get || !g_menu.set || !g_menu.onChange) { ArenaFree(&g_menu.snap); return 0; }
    return 1;
}
static void MenuClose(void) { ArenaFree(&g_menu.snap); }

static int Call(const umm_value* fn, int argc, umm_value* argv, umm_value* rets)
{
    if (!fn || fn->type != UMM_FN || !fn->u.fn) return -2;
    ArenaInit(&g_out);
    return fn->u.fn->call(fn->u.fn, argc, argv, &g_out.a, rets, UMM_MAXRETS, 0);
}

static void MenuSet(const char* id, double value)
{
    umm_value args[2] = {Str(id), Num(value)}, rets[UMM_MAXRETS];
    Call(g_menu.set, 2, args, rets);
    ArenaFree(&g_out);
}
static int MenuGet(const char* id, double* value)
{
    umm_value arg = Str(id), rets[UMM_MAXRETS];
    int n = Call(g_menu.get, 1, &arg, rets), ok = 0;
    if (n >= 1 && rets[0].type == UMM_NUM) { *value = rets[0].u.n; ok = 1; }
    ArenaFree(&g_out);
    return ok;
}

/* rows */
enum { R_ITEM, R_SLOT, R_RESET, R_ENABLE, R_PSLOT, R_ACTION };
typedef struct Row { char id[96]; int kind, page, slot; umm_callback cb; } Row;
#define MAX_ROWS (ST_PAGES * (ST_MAX_TEMPLATE_SLOTS + 2) + 8)
static Row g_rows[MAX_ROWS];
static int g_nrows;
static char g_slug[ST_PAGES][48];

typedef struct Event { int row; double value; } Event;
static Event g_queue[512];
static int g_queued;

static const char g_owner = 0;
static void CbRetain(umm_callback* self) { self->refs++; }
static void CbRelease(umm_callback* self) { if (self->refs) self->refs--; }
static int CbCall(umm_callback* self, int argc, umm_value* argv, umm_arena* out, umm_value* rets, int maxrets, uint32_t flags)
{
    int row = (int)self->user;
    if (row < 0 || row >= g_nrows || argc < 1 || g_queued >= (int)(sizeof g_queue / sizeof g_queue[0])) return 0;
    double v = argv[0].type == UMM_NUM ? argv[0].u.n : argv[0].type == UMM_BOOL ? (double)argv[0].u.b : NAN;
    if (isnan(v)) return 0;
    g_queue[g_queued++] = (Event){row, v};
    return 0;
}

static void Slug(char* out, size_t n, const char* title)
{
    size_t j = 0;
    for (const char* p = title; *p && j + 1 < n; p++)
    {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[j++] = c;
        else if (j && out[j - 1] != '-') out[j++] = '-';
    }
    while (j && out[j - 1] == '-') j--;
    out[j] = 0;
}

typedef struct Spec { umm_pair pairs[16]; int n; umm_pair choices[16]; umm_table choiceTab, tab; } Spec;
static void Put(Spec* s, const char* key, umm_value v) { s->pairs[s->n].key = Str(key); s->pairs[s->n].value = v; s->n++; }

static int AddRow(const char* id, int kind, int page, int slot, Spec* spec, const char* pageTitle, const char* section)
{
    static char modName[64], modId[96], sectionName[64];
    if (g_nrows >= MAX_ROWS) return 0;
    if (g_menu.sections)
    {
        snprintf(modName, sizeof modName, "%s", pageTitle);
        snprintf(sectionName, sizeof sectionName, "Stat Tuner: %s", section);
        Put(spec, "section", Str(sectionName));
        Put(spec, "mod_description", Str("Stat Tuner: pick an item, press APPLY, then move its sliders and APPLY again. "
                                        "100% is the game's value. Presets are on the Presets page."));
    }
    else snprintf(modName, sizeof modName, "Stat Tuner: %s", pageTitle);
    snprintf(modId, sizeof modId, "yui.stat-tuner.%s", page >= 0 ? g_slug[page] : "presets");
    Put(spec, "mod", Str(modName));
    Put(spec, "mod_id", Str(modId));
    spec->tab = (umm_table){(uint32_t)spec->n, 0, spec->pairs};
    umm_value args[2] = {Str(id), Tab(&spec->tab)}, rets[UMM_MAXRETS];
    int n = Call(g_menu.reg, 2, args, rets);
    int added = n >= 1 && rets[0].type == UMM_BOOL && rets[0].u.b;
    if (!added)
        g_hf->log(g_mod, "menu: %s refused (%s)", id, n == -1 && rets[0].type == UMM_STR ? rets[0].u.s
            : n >= 2 && rets[1].type == UMM_STR ? rets[1].u.s : "no reason given");
    ArenaFree(&g_out);
    if (!added) return 0;
    Row* r = &g_rows[g_nrows];
    snprintf(r->id, sizeof r->id, "%s", id);
    r->kind = kind;
    r->page = page;
    r->slot = slot;
    r->cb = (umm_callback){sizeof(umm_callback), 0, CbCall, CbRetain, CbRelease, &g_owner, (uintptr_t)g_nrows};
    umm_value cbArgs[2] = {Str(r->id), {UMM_FN, 0, {0}}};
    cbArgs[1].u.fn = &r->cb;
    Call(g_menu.onChange, 2, cbArgs, rets);
    ArenaFree(&g_out);
    return ++g_nrows;
}

static void Choices(Spec* s, const char* const* names, int count)
{
    for (int c = 0; c < count; c++) { s->choices[c].key = Num(c + 1); s->choices[c].value = Str(names[c]); }
    s->choiceTab = (umm_table){(uint32_t)count, UMM_TF_ARRAY, s->choices};
    Put(s, "type", Str("choice"));
    Put(s, "choices", Tab(&s->choiceTab));
}

static void ShowPage(int p);

static int Register(void)
{
    static const char* const kOnOff[] = {"Off", "On"};
    static const char* const kSlotNames[] = {"1", "2", "3", "4", "5", "6", "7", "8"};
    static const char* const kAction[] = {"Choose...", "Save to slot", "Load slot", "Reset all to vanilla"};
    static const char* const kReset[] = {"No", "Reset to vanilla"};
    static char ids[96], labels[ST_MAX_TEMPLATE_SLOTS][80], descs[ST_MAX_TEMPLATE_SLOTS][420];
    if (!MenuOpen()) return 0;
    g_nrows = 0;
    Spec s;

    /* Presets */
    memset(&s, 0, sizeof s);
    Choices(&s, kOnOff, 2);
    Put(&s, "label", Str("Stat Tuner"));
    Put(&s, "default", Num(2));
    Put(&s, "description", Str("Off puts every stat back to the game's values; your settings are kept for when you turn it on."));
    AddRow("yst.presets.enabled", R_ENABLE, -1, 0, &s, "Presets", "Diver and presets");
    memset(&s, 0, sizeof s);
    Choices(&s, kSlotNames, PRESETS);
    Put(&s, "label", Str("Preset slot"));
    Put(&s, "gap", (umm_value){UMM_BOOL, 0, {1}});
    Put(&s, "description", Str("The slot the action below saves to or loads from (stat-tuner-preset-<n>.ini in the mod's folder)."));
    AddRow("yst.presets.slot", R_PSLOT, -1, 0, &s, "Presets", "Diver and presets");
    memset(&s, 0, sizeof s);
    Choices(&s, kAction, 4);
    Put(&s, "label", Str("Preset action"));
    Put(&s, "description", Str("Pick an action and press APPLY. Save stores every item's sliders in the slot; Load replaces them "
                               "with the slot's; Reset puts every item back to the game's values."));
    AddRow("yst.presets.action", R_ACTION, -1, 0, &s, "Presets", "Diver and presets");

    for (int p = 0; p < ST_PAGES; p++)
    {
        const StPage* page = &kPages[p];
        const StTemplate* t = &kTemplates[page->tpl];
        if (!page->direct)
        {
            const char* names[16];
            int n = page->item_count > 16 ? 16 : page->item_count;
            for (int i = 0; i < n; i++) names[i] = kItems[page->item_first + i].name;
            memset(&s, 0, sizeof s);
            Choices(&s, names, n);
            Put(&s, "label", Str("Item"));
            Put(&s, "description", Str("Pick an item and press APPLY: the sliders below switch to it. Then move them and press "
                                       "APPLY again. Sliders an item does not have change nothing."));
            snprintf(ids, sizeof ids, "yst.%s.item", g_slug[p]);
            AddRow(ids, R_ITEM, p, 0, &s, page->title, page->section);
        }
        for (int k = 0; k < t->count; k++)
        {
            const StSlot* sl = &kSlots[t->first + k];
            memset(&s, 0, sizeof s);
            snprintf(labels[k], sizeof labels[k], "%s%s", sl->label, sl->mode == ST_DELTA ? "" : " %");
            snprintf(descs[k], sizeof descs[k], "%s %s", sl->desc,
                sl->mode == ST_DELTA ? "0 keeps the game's value; +2 on armor 3 gives armor 5." : "100% keeps the game's value.");
            Put(&s, "type", Str("slider"));
            Put(&s, "label", Str(labels[k]));
            Put(&s, "description", Str(descs[k]));
            if (sl->mode == ST_DELTA)
            {
                Put(&s, "min", Num(-6)); Put(&s, "max", Num(6)); Put(&s, "step", Num(1)); Put(&s, "default", Num(0));
            }
            else
            {
                Put(&s, "min", Num(0)); Put(&s, "max", Num(1000)); Put(&s, "step", Num(5)); Put(&s, "default", Num(100));
            }
            if (k == 0) Put(&s, "gap", (umm_value){UMM_BOOL, 0, {1}});
            snprintf(ids, sizeof ids, "yst.%s.%s", g_slug[p], sl->key);
            AddRow(ids, R_SLOT, p, k, &s, page->title, page->section);
        }
        {
            memset(&s, 0, sizeof s);
            Choices(&s, kReset, 2);
            Put(&s, "label", Str(page->direct ? "Reset this page" : "Reset this item"));
            Put(&s, "gap", (umm_value){UMM_BOOL, 0, {1}});
            Put(&s, "description", Str("Pick Reset to vanilla and press APPLY: every slider here goes back to the game's value."));
            snprintf(ids, sizeof ids, "yst.%s.reset", g_slug[p]);
            AddRow(ids, R_RESET, p, 0, &s, page->title, page->section);
        }
    }

    /* sync with the menu's saved rows */
    double v;
    if (MenuGet("yst.presets.slot", &v) && v >= 1 && v <= PRESETS) g_slot = (int)v - 1;
    MenuSet("yst.presets.enabled", g_enabled ? 2 : 1);
    MenuSet("yst.presets.action", 1);
    for (int p = 0; p < ST_PAGES; p++)
    {
        if (!kPages[p].direct)
        {
            snprintf(ids, sizeof ids, "yst.%s.item", g_slug[p]);
            if (MenuGet(ids, &v) && v >= 1 && v <= kPages[p].item_count) g_cur[p] = (int)v - 1;
        }
        ShowPage(p);
    }
    MenuClose();
    g_hf->log(g_mod, "menu: %d rows on the MODS page (%s; Universal Mod Menu %s)", g_nrows,
        g_menu.sections ? "grouped in sections" : "one category per page", g_api->build ? g_api->build : "");
    return 1;
}

/* the page's sliders show its current item's values (the menu must be open) */
static void ShowPage(int p)
{
    char id[96];
    const StPage* page = &kPages[p];
    int item = page->item_first + (page->direct ? 0 : g_cur[p]);
    const StTemplate* t = &kTemplates[page->tpl];
    for (int k = 0; k < t->count; k++)
    {
        snprintf(id, sizeof id, "yst.%s.%s", g_slug[p], kSlots[t->first + k].key);
        double now;
        if (MenuGet(id, &now) && now == (double)g_val[item][k]) continue;  /* only rows that must change */
        MenuSet(id, g_val[item][k]);
    }
    snprintf(id, sizeof id, "yst.%s.reset", g_slug[p]);
    double reset;
    if (!MenuGet(id, &reset) || reset != 1) MenuSet(id, 1);
}

/* ---- the game thread --------------------------------------------------------------------------------------- */
static void ProcessQueue(void)
{
    int itemChanged[ST_PAGES] = {0}, action = 0, changed = 0, n = g_queued;
    int* show = g_show;
    g_queued = 0;
    (void)itemChanged;
    /* sliders and resets first: they were edited while the page showed its previous item, so they belong to it */
    for (int i = 0; i < n; i++)
    {
        const Row* r = &g_rows[g_queue[i].row];
        double v = g_queue[i].value;
        switch (r->kind)
        {
        case R_SLOT:
            {
                const StPage* page = &kPages[r->page];
                int item = page->item_first + (page->direct ? 0 : g_cur[r->page]);
                g_val[item][r->slot] = (float)v;
                changed = 1;
                g_hf->log(g_mod, "%s: %s = %g", kItems[item].name, ItemSlot(item, r->slot)->key, v);
            }
            break;
        case R_RESET:
            if (v >= 2)
            {
                const StPage* page = &kPages[r->page];
                ResetItem(page->item_first + (page->direct ? 0 : g_cur[r->page]));
                show[r->page] = changed = 1;
                g_hf->log(g_mod, "%s: reset to vanilla", page->direct ? page->title : kItems[page->item_first + g_cur[r->page]].name);
            }
            break;
        case R_ENABLE: g_enabled = v >= 2; changed = 1; break;
        case R_PSLOT: if (v >= 1 && v <= PRESETS) g_slot = (int)v - 1; break;
        case R_ACTION: action = (int)v; break;
        default: break;
        }
    }
    for (int i = 0; i < n; i++)  /* then item picks: the page shows the new item's values */
    {
        const Row* r = &g_rows[g_queue[i].row];
        if (r->kind != R_ITEM) continue;
        int v = (int)g_queue[i].value - 1;
        if (v >= 0 && v < kPages[r->page].item_count && v != g_cur[r->page])
        {
            g_cur[r->page] = v;
            show[r->page] = 1;
            g_hf->log(g_mod, "%s: showing %s", kPages[r->page].title, kItems[kPages[r->page].item_first + v].name);
        }
    }
    if (action == 2)
    {
        int saved = SaveFile(g_slot);
        g_hf->log(g_mod, saved ? "preset %d saved (%d changed sliders)" : "preset %d: could not write its file", g_slot + 1, saved - 1);
    }
    else if (action == 3)
    {
        int loaded = LoadFile(g_slot);
        if (loaded) { changed = 1; for (int p = 0; p < ST_PAGES; p++) show[p] = 1; }
        g_hf->log(g_mod, loaded ? "preset %d loaded (%d sliders)" : "preset %d is empty", g_slot + 1, loaded - 1);
    }
    else if (action == 4)
    {
        for (int i = 0; i < ST_ITEMS; i++) ResetItem(i);
        changed = 1;
        for (int p = 0; p < ST_PAGES; p++) show[p] = 1;
        g_hf->log(g_mod, "every item reset to vanilla");
    }
    if (changed)
    {
        if (!SaveFile(-1)) g_hf->log(g_mod, "could not write stat-tuner-current.ini in %s", g_hf->mod_folder(g_mod));
        g_dirty = 1;
    }
    if (action > 1) g_resetAction = 1;
}

/* menu rows that must follow the values (kept until the menu can be reached) */
static void FlushMenu(void)
{
    int any = g_resetAction;
    for (int p = 0; p < ST_PAGES; p++) any |= g_show[p];
    if (!any || !MenuOpen()) return;
    for (int p = 0; p < ST_PAGES; p++) if (g_show[p]) { ShowPage(p); g_show[p] = 0; }
    if (g_resetAction) { MenuSet("yst.presets.action", 1); g_resetAction = 0; }
    MenuClose();
}

static void Tick(HfMod* mod, float dt, void* user)
{
    static float menuWait, retry;
    if (!g_api)
    {
        menuWait -= dt;
        if (menuWait <= 0)
        {
            menuWait = 1.0f;
            const umm_api* api = umm_api_find();
            if (api)
            {
                g_api = api;
                if (!Register()) g_api = NULL;  /* published before its api was set up: try again */
            }
        }
    }
    if (g_queued) ProcessQueue();
    if (g_api) FlushMenu();
    retry -= dt;
    if (g_dirty || (g_anyPending && retry <= 0))
    {
        g_dirty = 0;
        retry = RETRY_SECONDS;
        ApplyAll();
    }
}

static void DataLoaded(HfMod* mod, HfEvent event, void* user)
{
    /* live edits are renewed by HellForge when the game reloads its data; this only catches rows not readable before */
    for (int i = 0; i < ST_ITEMS; i++)
        for (int s = 0; s < ST_MAX_TEMPLATE_SLOTS; s++)
            if (!isnan(g_applied[i][s]) && !g_pending[i][s])
            {
                const StRange* r = &kRanges[kItems[i].ranges + s];
                for (int k = 0; k < r->count; k++)
                    if (!g_fieldDone[r->first + k]) { g_pending[i][s] = 1; g_tries[i][s] = 0; break; }
            }
    g_dirty = 1;
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* hf, HfMod* mod)
{
    if (hf->version < 6 || hf->size <= offsetof(HfApi, on_game_thread) || !hf->on_game_thread) return 1;
    g_hf = hf;
    g_mod = mod;
    for (int p = 0; p < ST_PAGES; p++)
    {
        Slug(g_slug[p], sizeof g_slug[p], kPages[p].title);
        for (int i = 0; i < kPages[p].item_count; i++) g_itemPage[kPages[p].item_first + i] = (uint16_t)p;
    }
    for (int i = 0; i < ST_ITEMS; i++)
        for (int s = 0; s < ST_MAX_TEMPLATE_SLOTS; s++) g_applied[i][s] = NAN;
    for (int i = 0; i < ST_ITEMS; i++)
        for (int s = 0; s < ItemTemplate(i)->count; s++)
        {
            const StRange* r = &kRanges[kItems[i].ranges + s];
            for (int k = 0; k < r->count; k++) { g_fieldItem[r->first + k] = (uint16_t)i; g_fieldSlot[r->first + k] = (uint8_t)s; }
        }
    for (int i = 0; i < ST_ITEMS; i++) ResetItem(i);
    int loaded = LoadFile(-1);
    hf->on(mod, HF_EVENT_ENTITIES_READY, DataLoaded, NULL);
    hf->on(mod, HF_EVENT_PROJECTILES_READY, DataLoaded, NULL);
    if (!hf->on_game_thread(mod, Tick, NULL)) { hf->log(mod, "Stat Tuner: the game thread could not be hooked"); return 1; }
    hf->log(mod, "Stat Tuner: %d items, %d fields; %s; waiting for the Universal Mod Menu", ST_ITEMS, ST_FIELDS,
        loaded ? "saved values loaded" : "no saved values yet");
    return 0;
}
