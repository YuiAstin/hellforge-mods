/* kitbase settings: one table per kit drives its .ini, Mod Options Menu's rows and the clamping of every value.
 *
 * Code shared by the character kits. Source of truth: kitbase/ at the repo root. The copy in a kit's src/kitbase/ is
 * written by tools/kitbase_sync.py, so edit kitbase/, never the copy. See docs/kit-base.md.
 * Include after the C headers and the HellForge SDK headers. Everything is static and marked unused, so a kit only
 * pays for what it calls.
 *
 * A kit declares, in hellforge_mod_init before LoadSettings():
 *   kb_ini = "lenna.ini";                          its settings file, in the mod folder
 *   kb_key_names = {"key_wild", ...};              its three skill keys' names there (s_keys[0..2])
 *   KB_SETTINGS(kSettings);                        every setting: KB_F / KB_I (name, variable, lowest, highest)
 *   KB_OPTIONS(kOptions);                          the ones Mod Options Menu shows (its <kit>_option_get / _set)
 *   kb_option_after = OptionAfter;                 optional: after Mod Options Menu sets one (changed: it moved)
 * The default .ini's text stays in the kit (DefaultIni, for tools/inigen.py). */
#ifndef KB_SETTINGS_H
#define KB_SETTINGS_H

#include "kb_body.h"

/* Her three skill keys (Z, X, C unless the .ini says otherwise); s_badKey: bit k set when key k could not be read */
KB_VAR Key s_keys[3] = {{'Z', 0, "Z"}, {'X', 0, "X"}, {'C', 0, "C"}};
KB_VAR int s_badKey;

typedef struct KbSetting { const char* name; float* f; int* i; double lo, hi; } KbSetting;
#define KB_F(name, var, lo, hi) {name, &(var), NULL, lo, hi}
#define KB_I(name, var, lo, hi) {name, NULL, &(var), lo, hi}
typedef struct KbOption { const char* name; float* f; int* i; } KbOption;
#define KB_SETTINGS(t) (kb_settings = (t), kb_nsettings = sizeof (t) / sizeof (t)[0])
#define KB_OPTIONS(t) (kb_options = (const KbOption*)(t), kb_noptions = sizeof (t) / sizeof (t)[0])

KB_VAR const char* kb_ini;
KB_VAR const char* kb_key_names[3];
KB_VAR const KbSetting* kb_settings;
KB_VAR unsigned kb_nsettings;
KB_VAR const KbOption* kb_options;
KB_VAR unsigned kb_noptions;
KB_VAR void (*kb_option_after)(const char* name, int changed);

/* One setting by name: clamped to its range; an unknown name does nothing */
KB_FN void Setting(const char* key, double v)
{
    for (unsigned n = 0; n < kb_nsettings; n++)
    {
        const KbSetting* s = &kb_settings[n];
        if (strcmp(key, s->name)) continue;
        double c = v < s->lo ? s->lo : v > s->hi ? s->hi : v;
        if (s->f) *s->f = (float)c;
        else *s->i = (int)c;
        return;
    }
}

/* The kit's .ini: its keys and settings. Never written (HellForge keeps mod folders read-only while the game runs):
 * without one, the built-in defaults hold. */
KB_FN void LoadSettings(void)
{
    char path[MAX_PATH * 2], line[256];
    snprintf(path, sizeof path, "%s\\%s", hf->mod_folder(self), kb_ini);
    FILE* f = OpenUtf8(path, L"r");
    if (f) hf->log(self, "settings: read %s", path);
    else
    {
        hf->log(self, "settings: no %s in %s (it ships with the mod), so the built-in defaults; the MODS tab still changes them", kb_ini, path);
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
        int k = -1;
        for (int i = 0; i < 3; i++) if (kb_key_names[i] && !strcmp(key, kb_key_names[i])) { k = i; break; }
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

/* Mod Options Menu: read one of the shown settings */
KB_FN int OptionGet(const char* name, double* out)
{
    if (!name || !out) return 0;
    for (unsigned k = 0; k < kb_noptions; k++)
        if (!strcmp(kb_options[k].name, name)) { *out = kb_options[k].f ? (double)*kb_options[k].f : (double)*kb_options[k].i; return 1; }
    return 0;
}

/* Mod Options Menu: set one (clamped); her body changes from here on in a mission */
KB_FN int OptionSet(const char* name, double value)
{
    double before;
    if (!isfinite(value) || !OptionGet(name, &before)) return 0;
    Setting(name, value);
    double now;
    OptionGet(name, &now);
    if (now != before)
    {
        Log("option %s: %g -> %g (Mod Options Menu)", name, before, now);
        if (g_inMission && (!strcmp(name, "hp_mult") || !strcmp(name, "stamina_mult"))) { Body(0); Body(1); }
    }
    if (kb_option_after) kb_option_after(name, now != before);
    return 1;
}

#endif
