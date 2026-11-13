/* One-Handed Test (yui.one-hand-test): research for one-handed weapons. Not for release.
 *
 * Every weapon the game calls one-handed (the 34 with the ONE HANDED trait: pistols, melee, the Knight, Defender,
 * Pummeler...) has bit 0 of EquipmentComponentData +0xC4 set (0x00010001), and every two-handed one has it clear
 * (0x00010000); +0xC0 is its grip pose (70 for the one-handed primaries, 1 for most pistols, 42 for blades, 10 / 15 /
 * 80 / 90 for two-handed rifles and shotguns). Found by the Equipment Probe (probes/equip-probe) over all 81 weapons.
 * This sets the bit (and, if asked, the grip) on the weapons listed in one-hand-test.ini, in the game's Equipment
 * table once entities are ready: [[game + 0x346BF98] + 0xF12478 + 8 * 233] -> index rows {u64 resource, u32 record,
 * u32 0} x 688, records of 232 bytes from +11008. A record another entity shares is changed for both (logged). It
 * reaches a weapon equipped from then on. Game build 25480438. */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "hellforge_mod.h"

#define EQUIPMENT 233u
#define ROWS 688u
#define RECORDS_AT 11008u
#define STRIDE 232u
static const HfApi* g_hf;
static HfMod* g_mod;
static struct { unsigned long long res; unsigned grip; } g_weapons[8];
static int g_count;

static void LoadSettings(void)
{
    char path[MAX_PATH * 3], line[256];
    snprintf(path, sizeof path, "%s\\one-hand-test.ini", g_hf->mod_folder(g_mod));
    FILE* f = fopen(path, "r");
    if (!f) { g_hf->log(g_mod, "no one-hand-test.ini: nothing to change"); return; }
    while (fgets(line, sizeof line, f) && g_count < 8)
    {
        unsigned long long res;
        unsigned grip = 0;
        if (line[0] == ';') continue;
        if (sscanf(line, " weapon = %llx %u", &res, &grip) >= 1 && res) { g_weapons[g_count].res = res; g_weapons[g_count].grip = grip; g_count++; }
    }
    fclose(f);
}

static void Apply(HfMod* mod, HfEvent event, void* user)
{
    uintptr_t game = g_hf->module_base("game.dll", NULL), root = 0, table = 0;
    const char* build = g_hf->build_id ? g_hf->build_id() : "";
    if (!build || strcmp(build, "25480438")) { g_hf->log(g_mod, "not game build 25480438: nothing changed"); return; }
    if (!game || g_hf->read(game + 0x346BF98, &root, 8) || !root || g_hf->read(root + 0xF12478 + 8u * EQUIPMENT, &table, 8) || !table)
    { g_hf->log(g_mod, "the Equipment table is not there: nothing changed"); return; }
    for (int w = 0; w < g_count; w++)
    {
        unsigned record = ~0u, sharers = 0;
        for (unsigned i = 0; i < ROWS; i++)
        {
            struct { unsigned long long res; unsigned record, zero; } row;
            if (g_hf->read(table + 16u * i, &row, sizeof row)) break;
            if (row.res == g_weapons[w].res) record = row.record;
        }
        if (record == ~0u || record > 1024) { g_hf->log(g_mod, "%016llX: no Equipment record", g_weapons[w].res); continue; }
        for (unsigned i = 0; i < ROWS; i++)
        {
            struct { unsigned long long res; unsigned record, zero; } row;
            if (!g_hf->read(table + 16u * i, &row, sizeof row) && row.record == record && row.res && row.res != g_weapons[w].res)
            { sharers++; g_hf->log(g_mod, "  record %u is also %016llX's (changed for it too)", record, row.res); }
        }
        uintptr_t at = table + RECORDS_AT + (uintptr_t)record * STRIDE;
        unsigned grip = 0, flags = 0;
        g_hf->read(at + 0xC0, &grip, 4);
        g_hf->read(at + 0xC4, &flags, 4);
        unsigned newFlags = flags | 1u, newGrip = g_weapons[w].grip ? g_weapons[w].grip : grip;
        int ok = !g_hf->write(at + 0xC4, &newFlags, 4) && !g_hf->write(at + 0xC0, &newGrip, 4);
        g_hf->log(g_mod, "%016llX (record %u): one-handed %s; grip %u -> %u, flags %08X -> %08X%s", g_weapons[w].res, record,
            ok ? "set" : "REFUSED", grip, newGrip, flags, newFlags, sharers ? " (shared record)" : "");
    }
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* hf, HfMod* mod)
{
    if (hf->version < 13) return 1;
    g_hf = hf;
    g_mod = mod;
    LoadSettings();
    hf->log(mod, "One-Handed Test 0.1.0: %d weapon%s to make one-handed once entities are ready", g_count, g_count == 1 ? "" : "s");
    return hf->on(mod, HF_EVENT_ENTITIES_READY, Apply, NULL);
}
