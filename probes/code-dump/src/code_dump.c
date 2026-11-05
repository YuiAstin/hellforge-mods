/* Code Dump (yui.code-dump): writes game.dll as it is in memory, unpacked, for offline analysis (disassembly of the
 * functions the mods call or hook). Rebuilt from the Booster Table Probe 0.2.1's dumper, alone.
 *
 * The image is laid out by RVA (file offset = RVA): code through read_code (the bytes before any hook), the rest through
 * read; a page neither can read is left zero and counted in the log. It goes to <the session's logs folder>/
 * game.dll.<build>.dump (HellForge keeps mod folders read-only while a session runs and switches off a mod whose folder
 * gained files). About 75 MB. It is the game's code: keep it on your own disk; the repo's .gitignore leaves *.dump out.
 * HellForge clears old session folders: copy the dump somewhere of your own. Turn the mod off when done. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "hellforge_mod.h"

#define BUILD "25480438"
#define GAME_SIZE 74727424u
static const HfApi* hf;
static HfMod* self;
static uintptr_t game;

static void WriteImage(size_t size)
{
    const char* folder = hf->size >= offsetof(HfApi, logs_folder) + sizeof hf->logs_folder ? hf->logs_folder(self) : NULL;
    char path[MAX_PATH * 3];
    wchar_t wide[MAX_PATH * 2];
    if (!folder || !*folder || snprintf(path, sizeof path, "%s\\game.dll." BUILD ".dump", folder) >= (int)sizeof path ||
        !MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, MAX_PATH * 2))
    {
        hf->log(self, "dump: no path");
        return;
    }
    if (GetFileAttributesW(wide) != INVALID_FILE_ATTRIBUTES) { hf->log(self, "dump: already there (%s)", path); return; }
    HANDLE f = CreateFileW(wide, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) { hf->log(self, "dump: cannot create %s", path); return; }
    static uint8_t page[0x1000];
    unsigned missing = 0;
    int ok = 1;
    for (size_t o = 0; o < size && ok; o += sizeof page)
    {
        if (hf->read_code(game + o, page, sizeof page) && hf->read(game + o, page, sizeof page))
        {
            memset(page, 0, sizeof page);
            if (++missing <= 64) hf->log(self, "  dump: page +0x%07X unreadable, zeroed", (unsigned)o);
        }
        DWORD put = 0;
        ok = WriteFile(f, page, sizeof page, &put, NULL) && put == sizeof page;
    }
    CloseHandle(f);
    if (!ok) { DeleteFileW(wide); hf->log(self, "dump: write failed, removed"); return; }
    hf->log(self, "dump: %s, 0x%zX bytes, %u unreadable pages", path, size, missing);
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t size = 0;
    game = hf->module_base("game.dll", &size);
    if (!game || size != GAME_SIZE) { hf->log(self, "REFUSED: not build " BUILD "'s game.dll"); return; }
    WriteImage(size);
}
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 13 || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    hf->log(self, "Code Dump 0.1.0: writes the unpacked game.dll to this session's logs folder once the game has loaded");
    return api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL) ? 1 : 0;
}
