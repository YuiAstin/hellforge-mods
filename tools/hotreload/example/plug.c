/* Example hot-reload plug: counts kills (in persistent state, so the count survives a reload), logs each one, and
 * exposes a tiny HUD struct the way leva_hud_state does.
 *
 * Plug build:     python3 -m ziglang cc -target x86_64-windows-gnu -shared -O2 -Wall -Werror -Wno-unused-parameter
 *                     -I<sdk> -I<hotreload> example/plug.c -o example_plug.dll
 * Release build:  the same with -DHR_RELEASE -o example.dll (a normal HellForge mod: hellforge_mod_init, no socket). */
#include <stdint.h>
#include "hotreload_plug.h"

#define VERSION "0.1.0"

/* Survives reloads (zeroed on the first load and whenever STATE_VERSION changes). */
typedef struct State
{
    uint32_t kills;
    uint32_t loads;
} State;
#define STATE_VERSION 1

static const HfApi* hf;  /* the api Load gets: the socket's proxy (plug) or HellForge's (release) */
static HfMod* self;
static State* st;

/* The HUD reads this through the exported function (the socket forwards and copies it). */
typedef struct ExampleHud { uint32_t version, kills, loads; } ExampleHud;
static ExampleHud g_hud = {1, 0, 0};
HF_MOD_EXPORT ExampleHud* example_hud_state(void)
{
    g_hud.kills = st ? st->kills : 0;
    g_hud.loads = st ? st->loads : 0;
    return &g_hud;
}
HR_STATE_SIZE(example_hud_state, ExampleHud)

static void OnKill(HfMod* mod, const HfEventInfo* event, void* user)
{
    const HfKillEvent* k = (const HfKillEvent*)event;
    st->kills++;
    hf->log(self, "kill #%u (victim %u) [plug %s]", st->kills, k->victim, VERSION);
}

static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    /* runs once in release; in plug mode also again right after every reload (the event had already fired) */
    hf->log(self, "game loaded (plug %s)", VERSION);
}

static int Load(const HfApi* api, HfMod* mod, State* state, int first)
{
    hf = api;
    self = mod;
    st = state;
    st->loads++;
    api->log(mod, "example %s: load %u (%s), %u kills so far", VERSION, st->loads, first ? "first" : "reload", st->kills);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    if (!api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0)) api->log(mod, "kill events unavailable");
    return 0;
}

static void Unload(const HfApi* api, HfMod* mod, State* state)
{
    /* undo what the socket cannot: live edits, spawned things, threads, open files... */
    api->log(mod, "example %s: unloading with %u kills", VERSION, state->kills);
}

HR_PLUG("example", VERSION, State, STATE_VERSION, Load, Unload)
