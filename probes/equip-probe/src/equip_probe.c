/* Equipment Probe (yui.equip-probe): research only. Reads two of the game's component tables as they sit in memory
 * once entities are loaded, and writes every owned record to <logs folder>\equip-<component>.tsv:
 *   resource (hex) <tab> record index <tab> the record's bytes (hex).
 * A table: [[game + 0x346BF98] + 0xF12478 + 8 * component index] -> {index rows {u64 resource, u32 record, u32 0}
 * [capacity], records [count] at records_offset} (HD2Runtime's scan/tables.py layout). Sizes from HD2Runtime's
 * weapon_clone research: Equipment 233 (688 rows, 345 records of 232 bytes from +11008), Wieldable 111 (1010 rows, 505
 * records of 32 bytes from +16160). Each row is checked (record < count) and a table that does not fit is skipped. */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "hellforge_mod.h"

static const HfApi* g_hf;
static HfMod* g_mod;
static const struct { const char* name; unsigned index, capacity, count, offset, stride; } kTables[] = {
    {"Equipment", 233, 688, 345, 11008, 232},
    {"Wieldable", 111, 1010, 505, 16160, 32},
};

static void Dump(void)
{
    uintptr_t game = g_hf->module_base("game.dll", NULL), root = 0;
    if (!game || g_hf->read(game + 0x346BF98, &root, 8) || !root) { g_hf->log(g_mod, "no entity manager"); return; }
    const char* folder = g_hf->logs_folder ? g_hf->logs_folder(g_mod) : "";
    for (unsigned t = 0; t < sizeof kTables / sizeof kTables[0]; t++)
    {
        uintptr_t table = 0;
        if (g_hf->read(root + 0xF12478 + 8u * kTables[t].index, &table, 8) || !table) { g_hf->log(g_mod, "%s: no table", kTables[t].name); continue; }
        char path[MAX_PATH * 3];
        snprintf(path, sizeof path, "%s\\equip-%s.tsv", folder, kTables[t].name);
        wchar_t wide[MAX_PATH * 2];
        MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, MAX_PATH * 2);
        FILE* f = _wfopen(wide, L"w");
        if (!f) { g_hf->log(g_mod, "cannot write %s", path); continue; }
        unsigned rows = 0, bad = 0;
        static unsigned char rec[4096];
        for (unsigned i = 0; i < kTables[t].capacity; i++)
        {
            struct { unsigned long long res; unsigned record, zero; } row;
            if (g_hf->read(table + 16u * i, &row, sizeof row) || !row.res || row.res == ~0ull) continue;
            if (row.record >= kTables[t].count) { bad++; continue; }
            if (g_hf->read(table + kTables[t].offset + (uintptr_t)row.record * kTables[t].stride, rec, kTables[t].stride)) { bad++; continue; }
            fprintf(f, "%016llX\t%u\t", row.res, row.record);
            for (unsigned b = 0; b < kTables[t].stride; b++) fprintf(f, "%02X", rec[b]);
            fputc('\n', f);
            rows++;
        }
        fclose(f);
        g_hf->log(g_mod, "%s (component %u, table 0x%llX): %u owned rows written (%u rows out of range) to %s", kTables[t].name, kTables[t].index,
            (unsigned long long)table, rows, bad, path);
    }
}
static void Ready(HfMod* mod, HfEvent event, void* user) { Dump(); }
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* hf, HfMod* mod)
{
    if (hf->version < 13) return 1;
    g_hf = hf;
    g_mod = mod;
    hf->log(mod, "Equipment Probe 0.1.0: the Equipment and Wieldable tables are saved once entities are ready");
    return hf->on(mod, HF_EVENT_ENTITIES_READY, Ready, NULL);
}
