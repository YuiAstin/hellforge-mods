/* Extra Boosters (yui.extra-boosters): adds a 22nd booster (Booster value 21) to the game, a copy of a vanilla booster
 * under an item id of its own, owned and in the booster picker. The first step towards an add_booster in HellForge.
 *
 * How (game.dll build 25480438; docs/research/booster-table.md has the research):
 *   - The game's boosters are 56-byte BoosterSettings rows stored inline at game + 0x32FDA90, one per Booster value
 *     (0 None .. 20 FreeEAT). Live data follows row 20, so the rows cannot grow in place: they are copied into a block
 *     within 2 GB above game.dll with the new row 21 after them and a zeroed row 22 (code reads the row of the "none"
 *     value before checking it).
 *   - 117 instructions encode the table's address (109 rip-relative, 8 image base + disp32): each is re-pointed to the
 *     same offset in the copy (booster_sites.h).
 *   - 112 instructions hold the booster count 21 (or 19 for idx-1): guards, loop ends, sentinels (21 is also the game's
 *     "none / not found / rejected" value) and the picker cursor's clamp. Each is raised by one (booster_bounds.h).
 *   - The enum name list (game + 0x21DD630: None .. FreeEAT, "Count") is copied with the new name before "Count", and
 *     its two readers re-pointed.
 *   - The new booster is made owned in game, as HellForge's loader does for its own new items: a copy of the base
 *     booster's catalogue record is appended under the new id, the catalogue sorted again, and one added to the
 *     inventory (the game's own functions, on the game thread, once the catalogue has loaded).
 * Every instruction is checked against its reviewed bytes first, and none may sit under another hook; if one differs
 * (another mod moved the table, or another build) nothing is written and the log says which.
 *
 * Co-op: the booster choice goes over the network as the raw row index, and the game stores a received index without a
 * check, so every player in the session must run this mod with the same settings (HellForge's mod sync offers it).
 *
 * The ownership code is ported from HellForge's mod loader (native/mod-loader/ownership.c, GPL-3.0, by SkyeShade), so
 * this mod is GPL-3.0 (LICENSE). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <windows.h>
#include "hellforge_mod.h"

#define VERSION "0.1.0"
#define BUILD "25480438"
#define GAME_SIZE 74727424u
#define EXE_SIZE 60719104u

#define TABLE 0x32FDA90u
#define ROW 56u
#define VANILLA 21u   /* rows 0..20 */
#define ADDED 1u      /* this version adds one booster: value 21 */
#define ROWS (VANILLA + ADDED + 1u) /* and a zeroed row after the new ones */
#define NAMES 0x21DD630u
#define NAME_LEN 48u

typedef struct BoosterSite { uint32_t rva; uint8_t dispAt, size, rip; uint16_t offset; } BoosterSite;
typedef struct BoosterBound { uint32_t rva; uint8_t at, width; uint32_t value; } BoosterBound;
#include "booster_sites.h"
#include "booster_bounds.h"
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* The block: the rows, then the enum name list (VANILLA + ADDED + 1 pointers), then the new names. */
#define BLOCK_ROWS 0u
#define BLOCK_NAMES (ROWS * ROW)
#define BLOCK_TEXT (BLOCK_NAMES + (VANILLA + ADDED + 1u) * 8u)
#define BLOCK_SIZE (BLOCK_TEXT + ADDED * NAME_LEN)

/* ---- settings */
static int g_enabled = 1;
static uint32_t g_base = 3;                     /* the row copied: 3 MuscleEnhancement */
static uint32_t g_id = 0x1041BB56u;             /* the new booster's item id (+0x00), FNV-1a of "yui.extra-boosters/booster.21" */
static float g_value;                           /* +0x08, the tuning value; NaN: the base's */
static int g_valueSet;
static char g_name[NAME_LEN] = "ExtraBooster1"; /* the enum name (the mission report's) */

static const HfApi* hf;
static HfMod* self;
static uintptr_t game;
static uint8_t* g_block;
static atomic_int g_installed;

/* ---- the table move ------------------------------------------------------------------------------------------- */

static int32_t Disp(const BoosterSite* s, uintptr_t base, uint32_t offset)
{
    /* rip-relative: from the end of the instruction; image base + disp32: from the image base */
    int64_t target = (int64_t)base + offset;
    return (int32_t)(s->rip ? target - (int64_t)(game + s->rva + s->size) : target - (int64_t)game);
}

static int Fits(const BoosterSite* s, uintptr_t base, uint32_t offset)
{
    int64_t target = (int64_t)base + offset;
    int64_t d = s->rip ? target - (int64_t)(game + s->rva + s->size) : target - (int64_t)game;
    return d >= INT32_MIN && d <= INT32_MAX;
}

/* The same bytes before and after any hook: a write there would not land under another mod's detour. */
static int Unhooked(uintptr_t at, size_t n)
{
    uint8_t now[8], before[8];
    return n <= sizeof now && hf->read(at, now, n) == 0 && hf->read_code(at, before, n) == 0 && memcmp(now, before, n) == 0;
}

/* NULL when every instruction is the reviewed one, else why not. */
static const char* SitesFailed(void)
{
    static char why[160];
    for (unsigned i = 0; i < COUNT(kBoosterSites); i++)
    {
        const BoosterSite* s = &kBoosterSites[i];
        int32_t want = Disp(s, game + TABLE, s->offset), have = 0;
        if (hf->read_code(game + s->rva + s->dispAt, &have, 4) || have != want)
            return snprintf(why, sizeof why, "game.dll+0x%X does not address the booster table as reviewed (another mod moved it?)", s->rva), why;
        if (!Unhooked(game + s->rva + s->dispAt, 4))
            return snprintf(why, sizeof why, "game.dll+0x%X is under another mod's hook", s->rva), why;
    }
    for (unsigned i = 0; i < COUNT(kBoosterNameReaders); i++)
    {
        const BoosterSite* s = &kBoosterNameReaders[i];
        int32_t have = 0;
        if (hf->read_code(game + s->rva + s->dispAt, &have, 4) || have != (int32_t)NAMES || !Unhooked(game + s->rva + s->dispAt, 4))
            return snprintf(why, sizeof why, "game.dll+0x%X does not read the booster names as reviewed", s->rva), why;
    }
    for (unsigned i = 0; i < COUNT(kBoosterBounds); i++)
    {
        const BoosterBound* b = &kBoosterBounds[i];
        uint32_t have = 0;
        if (b->width > 4 || hf->read_code(game + b->rva + b->at, &have, b->width) || have != b->value || !Unhooked(game + b->rva + b->at, b->width))
            return snprintf(why, sizeof why, "game.dll+0x%X does not hold the booster count %u as reviewed", b->rva, b->value), why;
    }
    return NULL;
}

/* A block within 2 GB above game.dll that every site reaches, or NULL. */
static uint8_t* NearBlock(size_t image)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t step = si.dwAllocationGranularity ? si.dwAllocationGranularity : 0x10000;
    uintptr_t from = (game + image + step - 1) & ~(step - 1);
    for (uintptr_t at = from; at + BLOCK_SIZE < game + 0x7FF00000u; at += step)
    {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((void*)at, &mbi, sizeof mbi)) break;
        if (mbi.State != MEM_FREE) { at = ((uintptr_t)mbi.BaseAddress + mbi.RegionSize + step - 1) & ~(step - 1); at -= step; continue; }
        void* p = VirtualAlloc((void*)at, BLOCK_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!p) continue;
        int ok = 1;
        for (unsigned i = 0; i < COUNT(kBoosterSites) && ok; i++) ok = Fits(&kBoosterSites[i], (uintptr_t)p + BLOCK_ROWS, kBoosterSites[i].offset);
        for (unsigned i = 0; i < COUNT(kBoosterNameReaders) && ok; i++) ok = Fits(&kBoosterNameReaders[i], (uintptr_t)p + BLOCK_NAMES, 0);
        if (ok) return (uint8_t*)p;
        VirtualFree(p, 0, MEM_RELEASE);
        break; /* further away only gets worse */
    }
    return NULL;
}

static const char* Install(size_t image)
{
    static char why[160];
    const char* failed = SitesFailed();
    if (failed) return failed;
    if (g_base == 0 || g_base >= VANILLA) return snprintf(why, sizeof why, "base = %u: must be a booster value 1..20", g_base), why;

    uint8_t* block = NearBlock(image);
    if (!block) return "no free memory within 2 GB above game.dll for the moved table";

    /* rows 0..20 as the game has them, the new row a copy of the base's, then a zeroed row (VirtualAlloc zeroed it) */
    uint8_t* rows = block + BLOCK_ROWS;
    if (hf->read(game + TABLE, rows, VANILLA * ROW)) return "the booster table could not be read";
    uint8_t* added = rows + VANILLA * ROW;
    memcpy(added, rows + g_base * ROW, ROW);
    memcpy(added + 0x00, &g_id, 4);
    if (g_valueSet) memcpy(added + 0x08, &g_value, 4);
    for (unsigned r = 0; r < VANILLA; r++)
    {
        uint32_t id;
        memcpy(&id, rows + r * ROW, 4);
        if (id == g_id) return snprintf(why, sizeof why, "id 0x%08X is already booster %u's", g_id, r), why;
    }

    /* the name list: the vanilla names, the new one, then "Count" */
    uint64_t names[VANILLA + 1];
    if (hf->read(game + NAMES, names, sizeof names)) return "the booster names could not be read";
    char* text = (char*)block + BLOCK_TEXT;
    memcpy(text, g_name, NAME_LEN - 1);
    uint64_t* list = (uint64_t*)(block + BLOCK_NAMES);
    memcpy(list, names, VANILLA * 8);
    list[VANILLA] = (uint64_t)(uintptr_t)text;
    list[VANILLA + ADDED] = names[VANILLA];

    /* the instructions first (the copy's rows 0..20 are the game's, so nothing changes yet), then the names, then the
     * bounds that let code reach the new row */
    for (unsigned i = 0; i < COUNT(kBoosterSites); i++)
    {
        const BoosterSite* s = &kBoosterSites[i];
        int32_t d = Disp(s, (uintptr_t)rows, s->offset);
        if (hf->write(game + s->rva + s->dispAt, &d, 4))
            return snprintf(why, sizeof why, "writing game.dll+0x%X failed after %u of %u sites", s->rva, i, (unsigned)COUNT(kBoosterSites)), why;
    }
    for (unsigned i = 0; i < COUNT(kBoosterNameReaders); i++)
    {
        const BoosterSite* s = &kBoosterNameReaders[i];
        int32_t d = Disp(s, (uintptr_t)list, 0);
        if (hf->write(game + s->rva + s->dispAt, &d, 4)) return snprintf(why, sizeof why, "writing game.dll+0x%X (names) failed", s->rva), why;
    }
    for (unsigned i = 0; i < COUNT(kBoosterBounds); i++)
    {
        const BoosterBound* b = &kBoosterBounds[i];
        uint32_t v = b->value + ADDED;
        if (hf->write(game + b->rva + b->at, &v, b->width))
            return snprintf(why, sizeof why, "writing game.dll+0x%X failed after %u of %u bounds: the game may misbehave, restart it",
                       b->rva, i, (unsigned)COUNT(kBoosterBounds)), why;
    }
    g_block = block;
    return NULL;
}

/* ---- ownership (ported from HellForge's native/mod-loader/ownership.c, GPL-3.0) ------------------------------------ */
/* The progression manager [game + 0x347CEF8] holds the catalogue and inventory: a count at +0x1CE0, 0xB8-byte records
 * from +0x1CE4, an index of 24-byte entries from +0xB9CE4 ({record, mixId, itemId, sibling, distance back to parent,
 * children}), the categories' starts at +0xD1CE4, statuses at +0xD5D48 / +0xD5D4C (2: loaded). 0x136EC00 appends a
 * record, 0x136E6A0 sorts the catalogue, 0x136EF80(manager, mixId, amount, 0, 0, 0) adds to an item. */
#define OWN_MANAGER 0x347CEF8u
#define OWN_ADD 0x136EC00u
#define OWN_SORT 0x136E6A0u
#define OWN_APPLY 0x136EF80u
#define OWN_COUNT 0x1CE0u
#define OWN_RECORDS 0x1CE4u
#define OWN_RECORD 0xB8u
#define OWN_INDEX 0xB9CE4u
#define OWN_STARTS 0xD1CE4u
#define OWN_CATALOGUE 0xD5D48u
#define OWN_INVENTORY 0xD5D4Cu
#define OWN_MAX 4096u

typedef uint64_t (*OwnAddFn)(uint64_t unused, uint32_t mixId, uint32_t itemId, uint32_t parent, int32_t field, char flag, const void* buy,
    uint32_t buyCount, const void* sell, uint32_t sellCount, const uint32_t* dwords, uint32_t dwordCount, const uint32_t* required,
    uint32_t requiredCount, uint32_t category, uint32_t maxCap);
typedef void (*OwnSortFn)(void);
typedef void (*OwnApplyFn)(uintptr_t manager, uint32_t mixId, uint32_t amount, uint32_t source, const char* text, int extra);

static const char* const kOwnPins[] = {
    /* OWN_APPLY */ "44 89 44 24 18 48 89 4c 24 08 53 56 57 41 54 41 55 41 56 48 81 ec d8 00 00 00 45 33 e4 45 8b e9 41 8b f8 8b f2 4c 8b f1 81 fa 50 c8 84 35 0f",
    /* OWN_ADD */ "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48 89 7c 24 20 41 55 41 56 41 57 4c 8b 2d d7 e2 10 02",
    /* OWN_SORT + 0x21 */ "48 8b 35 30 e8 10 02 33 db 4d 89 73 18 44 8b f3 8b ae e0 1c 00 00 4c 8d ae e4 1c 00 00 89 ae 44 1d 0d 00",
};

static int OwnEntry(uintptr_t manager, uint32_t mixId)
{
    uint32_t count = *(const uint32_t*)(manager + OWN_COUNT);
    for (uint32_t i = 0; i < count && i < OWN_MAX; i++)
        if (((const uint32_t*)(manager + OWN_INDEX + (uintptr_t)i * 24))[1] == mixId) return (int)i;
    return -1;
}

static const uint8_t* OwnRecordAt(uintptr_t manager, int entry)
{
    const uint32_t* e = (const uint32_t*)(manager + OWN_INDEX + (uintptr_t)entry * 24);
    return (const uint8_t*)(manager + OWN_RECORDS + (uintptr_t)e[0] * OWN_RECORD);
}

/* 0 not yet (catalogue not loaded), 1 done, -1 refused (why set) */
static int Own(const char** why)
{
    uintptr_t manager = 0;
    if (hf->read(game + OWN_MANAGER, &manager, 8) || !manager) return 0;
    if (*(const uint32_t*)(manager + OWN_CATALOGUE) != 2 || *(const uint32_t*)(manager + OWN_INVENTORY) != 2) return 0;
    if (!hf->code_matches(game + OWN_APPLY, kOwnPins[0]) || !hf->code_matches(game + OWN_ADD, kOwnPins[1]) ||
        !hf->code_matches(game + OWN_SORT + 0x21, kOwnPins[2]))
        return *why = "the game's catalogue functions are not the reviewed ones", -1;
    uint32_t baseId;
    memcpy(&baseId, g_block + BLOCK_ROWS + g_base * ROW, 4);
    if (OwnEntry(manager, g_id) < 0)
    {
        int base = OwnEntry(manager, baseId);
        if (base < 0) return *why = "the base booster is not in the game's catalogue", -1;
        if (*(const uint32_t*)(manager + OWN_COUNT) + 16 > OWN_MAX) return *why = "the game's catalogue is full", -1;
        const uint8_t* source = OwnRecordAt(manager, base);
        int32_t field;
        memcpy(&field, source + 0x10, 4);
        ((OwnAddFn)(game + OWN_ADD))(0, g_id, g_id, 0, field, (char)source[0xB4], source + 0x1C, 4, source + 0x4C, 4,
            (const uint32_t*)(source + 0x7C), 4, (const uint32_t*)(source + 0x90),
            *(const uint32_t*)(source + 0x8C) <= 8 ? *(const uint32_t*)(source + 0x8C) : 8, *(const uint32_t*)(source + 0xC),
            *(const uint32_t*)(source + 0x18));
        memset((void*)(manager + OWN_STARTS), 0, 24 * 4);
        ((OwnSortFn)(game + OWN_SORT))();
        if (OwnEntry(manager, g_id) < 0) return *why = "the new booster's catalogue record was not kept", -1;
    }
    int entry = OwnEntry(manager, g_id);
    uint32_t state = *(const uint32_t*)(OwnRecordAt(manager, entry) + 0x14);
    if (state != 2 && state != 4) ((OwnApplyFn)(game + OWN_APPLY))(manager, g_id, 1, 0, NULL, 0);
    state = *(const uint32_t*)(OwnRecordAt(manager, OwnEntry(manager, g_id)) + 0x14);
    hf->log(self, "booster %u owned (item 0x%08X, catalogue state %u)", VANILLA, g_id, state);
    return 1;
}

/* ---- the game's "is this booster active" check, for the test log ------------------------------------------------- */
#define IS_ACTIVE 0x856D40u
static const char kIsActivePin[] = "48 8b 0d 59 f9 ac 02 48 8b 05 ea fb ac 02 83 79 08 00 76 06 83 79 40 04 74 46 44 8b 48 10 33 c9";
typedef char (*IsActiveFn)(uintptr_t unused, uint32_t booster);

static void Tick(HfMod* mod, float dt, void* user)
{
    (void)mod; (void)user;
    if (!atomic_load(&g_installed)) return;
    static int owned; /* 0 waiting, 1 done, -1 refused */
    static float wait;
    if (owned == 0 && (wait += dt) >= 1.0f)
    {
        wait = 0;
        const char* why = NULL;
        owned = Own(&why);
        if (owned < 0) hf->log(self, "the new booster is not owned: %s", why);
    }
    /* in a mission, once: which of the boosters the game says are active */
    static int reported, inMission;
    int mission = hf->game_state(self) == HF_STATE_MISSION;
    if (mission && !inMission) { reported = 0; wait = 0; }
    inMission = mission;
    static float settle;
    if (mission && !reported && (settle += dt) >= 10.0f && hf->code_matches(game + IS_ACTIVE, kIsActivePin))
    {
        reported = 1;
        settle = 0;
        char list[160] = "";
        size_t n = 0;
        for (uint32_t b = 1; b <= VANILLA + ADDED - 1 && n < sizeof list - 8; b++)
            if (((IsActiveFn)(game + IS_ACTIVE))(0, b)) n += (size_t)snprintf(list + n, sizeof list - n, " %u", b);
        hf->log(self, "mission: active boosters:%s%s", n ? list : " none", ((IsActiveFn)(game + IS_ACTIVE))(0, VANILLA) ? " (the new one included)" : "");
    }
}

static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    (void)mod; (void)event; (void)user;
    size_t gameSize = 0, exeSize = 0;
    game = hf->module_base("game.dll", &gameSize);
    uintptr_t exe = hf->module_base("helldivers2.exe", &exeSize);
    if (!game || !exe || gameSize != GAME_SIZE || exeSize != EXE_SIZE)
    {
        hf->log(self, "REFUSED: the game's modules are not build " BUILD "'s; nothing written");
        return;
    }
    if (!g_enabled) { hf->log(self, "off in the ini: nothing written"); return; }
    const char* why = Install(gameSize);
    if (why) { hf->log(self, "REFUSED: %s", why); return; }
    atomic_store(&g_installed, 1);
    uint32_t baseId;
    float value;
    memcpy(&baseId, g_block + BLOCK_ROWS + g_base * ROW, 4);
    memcpy(&value, g_block + BLOCK_ROWS + VANILLA * ROW + 0x08, 4);
    hf->log(self, "booster table moved to 0x%llx (+0x%llx from game.dll): %u sites re-pointed, %u bounds raised to %u, names moved",
        (unsigned long long)(uintptr_t)g_block, (unsigned long long)((uintptr_t)g_block - game), (unsigned)COUNT(kBoosterSites),
        (unsigned)COUNT(kBoosterBounds), VANILLA + ADDED);
    hf->log(self, "booster %u \"%s\": a copy of booster %u (item 0x%08X) as item 0x%08X, value %g", VANILLA, g_name, g_base, baseId, g_id, value);
}

/* ---- settings ---------------------------------------------------------------------------------------------------- */
static void LoadSettings(void)
{
    char path[MAX_PATH * 2], line[256];
    snprintf(path, sizeof path, "%s\\extra-boosters.ini", hf->mod_folder(self));
    FILE* f = fopen(path, "r");
    if (!f)
    {
        if ((f = fopen(path, "w")) != NULL)
        {
            fprintf(f,
                "; Extra Boosters: adds booster 21, a copy of a vanilla booster under its own item id.\n"
                "; Every player in a co-op session needs this mod with the same settings.\n"
                "enabled = 1\n"
                "; the booster copied (its name, icon, description and effect): 1 Vitality, 2 Stamina, 3 Muscle Enhancement ... 20 Surplus EAT\n"
                "base = 3\n"
                "; the new booster's item id, as hex; change it only if it clashes with another mod's\n"
                "id = 0x1041BB56\n"
                "; its tuning value (row +0x08); empty for the base's\n"
                "value =\n"
                "; its enum name, shown where the game prints booster names\n"
                "name = ExtraBooster1\n");
            fclose(f);
        }
        return;
    }
    while (fgets(line, sizeof line, f))
    {
        int v;
        unsigned u;
        float x;
        char s[NAME_LEN];
        if (line[0] == ';') continue;
        if (sscanf(line, " enabled = %d", &v) == 1) g_enabled = v != 0;
        else if (sscanf(line, " base = %u", &u) == 1) g_base = u;
        else if (sscanf(line, " id = %x", &u) == 1) g_id = u;
        else if (sscanf(line, " value = %f", &x) == 1) { g_value = x; g_valueSet = 1; }
        else if (sscanf(line, " name = %47[A-Za-z0-9_]", s) == 1) { memcpy(g_name, s, sizeof s); g_name[NAME_LEN - 1] = 0; }
    }
    fclose(f);
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 13 || api->size < offsetof(HfApi, read_code) + sizeof api->read_code || !api->read_code) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    LoadSettings();
    api->log(mod, "Extra Boosters " VERSION " (SDK %u, game " BUILD "): %s", api->version, g_enabled ? "on" : "off in the ini");
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) { api->log(mod, "REFUSED: the game-loaded event"); return 1; }
    if (!api->on_game_thread(mod, Tick, NULL)) api->log(mod, "the game thread could not be hooked: the new booster will not be owned");
    return 0;
}
