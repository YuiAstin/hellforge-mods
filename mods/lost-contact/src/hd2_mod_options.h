/* HD2 mod options (version 1): other HellForge code mods add rows to Vanilla Plus Native's in-game MODS tab
 * (ESC > OPTIONS > MODS), the way Bingus mods registered with CowboyBingus's Mod Options Menu.
 *
 * The menu publishes itself in the process environment variable HD2_MOD_OPTIONS_v1 = "<process id>:<address of a
 * HfModOptionsApi>" when it starts (hellforge_mod_init). A mod looks for it each frame until found (the menu may be
 * set up after it) and checks that the process id is its own. Everything runs on the game thread: register from your
 * game-thread callback; on_change is called from the menu's game-thread callback.
 *
 * Values: a toggle is 0 (off) or 1 (on); a choice is the 0-based index into `choices`. The menu keeps nothing: the
 * registering mod passes the current value and saves changes itself (its own .ini), so its settings file stays the
 * source of truth and keeps working without the menu. */
#ifndef HD2_MOD_OPTIONS_H
#define HD2_MOD_OPTIONS_H
#include <stdint.h>

#define HD2_MOD_OPTIONS_ENV "HD2_MOD_OPTIONS_v1"
#define HD2_MOD_OPTIONS_VERSION 1
#define HD2_MOD_OPTION_TOGGLE 1
#define HD2_MOD_OPTION_CHOICE 2
#define HD2_MOD_OPTION_MAX_CHOICES 24

typedef void (*Hd2ModOptionChanged)(void* user, const char* id, int32_t value);

typedef struct Hd2ModOption
{
    uint32_t size;            /* sizeof(Hd2ModOption) */
    const char* mod;          /* the mod's name: its section of the MODS tab ("Smarter Guard Dogs & Sentries") */
    const char* id;           /* unique among the mod's rows */
    const char* label;        /* the row's text */
    const char* description;  /* shown under the rows while the mod's section is open (the first row's is used); may be NULL */
    uint32_t type;            /* HD2_MOD_OPTION_TOGGLE or HD2_MOD_OPTION_CHOICE */
    uint32_t count;           /* choices of a choice row (2 .. HD2_MOD_OPTION_MAX_CHOICES) */
    const char* const* choices;
    int32_t value;            /* the current value */
    Hd2ModOptionChanged on_change;
    void* user;
} Hd2ModOption;

typedef struct Hd2ModOptionsApi
{
    uint32_t version;         /* HD2_MOD_OPTIONS_VERSION */
    uint32_t size;
    /* Adds a row (the strings are copied). 1 added, 2 already there (its value and callback are updated), 0 refused
     * (bad arguments or no room: 8 mods, 31 rows each). */
    int (*register_option)(const Hd2ModOption* option);
    /* The row's value as the menu shows it: 1 with *value set, 0 when there is no such row. */
    int (*get)(const char* mod, const char* id, int32_t* value);
    /* Sets the row's value from the mod's side (the menu shows it; on_change is not called). */
    int (*set)(const char* mod, const char* id, int32_t value);
} Hd2ModOptionsApi;

#ifdef _WIN32
#include <stdio.h>
#include <stdlib.h>
/* The menu's API if it is running in this process, else NULL (cheap enough to call once a frame). */
static inline const Hd2ModOptionsApi* hd2_mod_options(void)
{
    char text[64];
    unsigned long n = GetEnvironmentVariableA(HD2_MOD_OPTIONS_ENV, text, sizeof text);
    if (!n || n >= sizeof text) return NULL;
    unsigned long pid = 0;
    unsigned long long at = 0;
    if (sscanf(text, "%lu:%llx", &pid, &at) != 2 || pid != GetCurrentProcessId() || !at) return NULL;
    const Hd2ModOptionsApi* api = (const Hd2ModOptionsApi*)(uintptr_t)at;
    if (api->version < 1 || api->size < sizeof(Hd2ModOptionsApi)) return NULL;
    return api;
}
#endif
#endif
