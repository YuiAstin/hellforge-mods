/* FRV Package Probe (yui.frv-package-probe): research with no gameplay effect. Logs the call-in package at stratagem
 * row +0xA8 of the three FRVs (rows 26 Supply, 105 Gunner, 135 Incinerator), for HellForge's catalogue (Avaleine's ask).
 * stratagem_row only answers for stratagems a mod added, so it adds a copy of each (package 0: the base's) and reads
 * the copies' rows once the game has its stratagems, as Koleda's FRV Sinveiler call read the Supply FRV's. The copies
 * are kept out of the loadout picker (row +0x80 bit 1). No hooks, no keys. Build 25480438 only. */
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "hellforge_mod.h"
#include "hellforge_content.h"

#define BUILD "25480438"
#define VERSION "0.1.0"
#include "kitbase/kb_core.h"

static struct { uint32_t base; const char* name; const char* label; uint32_t type; int done; } g_rows[3] = {
    {HF_STRATAGEM_M103_SUPPLY_FRV, "yui/frv_package_probe/supply", "M-103 Supply FRV", 0, 0},
    {HF_STRATAGEM_M102_GUNNER_FRV, "yui/frv_package_probe/gunner", "M-102 Gunner FRV", 0, 0},
    {HF_STRATAGEM_M104_INCINERATOR_FRV, "yui/frv_package_probe/incinerator", "M-104 Incinerator FRV", 0, 0}};
static double g_now;

static void Tick(HfMod* mod, float dt, void* user)
{
    static double next;
    static int tries, over;
    g_now += dt;
    if (over || g_now < next) return;
    next = g_now + 1;
    int left = 0;
    for (unsigned i = 0; i < 3; i++)
    {
        if (g_rows[i].done || !g_rows[i].type) continue;
        uintptr_t row = hf->stratagem_row(g_rows[i].type);
        uint64_t pkg = 0, extra = 0;
        uint8_t sel = 0;
        if (!row || !ReadAt(row + 0xA8, &pkg, 8)) { left = 1; continue; }
        ReadAt(row + 0xF8, &extra, 8);
        g_rows[i].done = 1;
        Log("stratagem row %u (%s): call-in package at +0xA8 = 0x%016llX (row +0xF8: 0x%016llX)", g_rows[i].base, g_rows[i].label, (unsigned long long)pkg,
            (unsigned long long)extra);
        if (ReadAt(row + 0x80, &sel, 1) && (sel & 2)) { sel &= (uint8_t)~2; hf->write(row + 0x80, &sel, 1); }
    }
    if (!left) { over = 1; Log("done: turn this probe off"); }
    else if (++tries == 180) { over = 1; Log("some rows were not there in three minutes: given up"); }
}
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 13 || api->size < offsetof(HfApi, last_log) + sizeof api->last_log || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    Log("FRV Package Probe " VERSION ": the three FRV stratagems' call-in packages (row +0xA8)");
    for (unsigned i = 0; i < 3; i++)
    {
        HfStratagem st = {sizeof st};
        st.base = g_rows[i].base;
        st.name = g_rows[i].name;
        g_rows[i].type = api->add_stratagem(mod, &st);
        if (!g_rows[i].type) Log("the copy of row %u (%s) was refused (%s)", g_rows[i].base, g_rows[i].label, api->last_log ? api->last_log(mod) : "");
    }
    if (!api->on_game_thread(mod, Tick, NULL)) Log("the game thread is unavailable");
    return 0;
}
