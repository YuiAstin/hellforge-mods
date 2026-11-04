/* Booster Table Probe (yui.booster-table-probe): research only, no gameplay effect. Maps what an add_booster has to move.
 *
 * The game's boosters are 56-byte BoosterSettings rows stored inline in game.dll's data at +0x32FDA90, one per booster
 * value (0 none, 1..20; HellForge SDK HfBoosterSettings, Diver Kit's catalogue, PSS's booster names). Unlike the
 * stratagem list (8-byte pointers, moved by HellForge's stratagems.c), adding a row means moving the rows themselves, so
 * every instruction that addresses any byte of them has to be found. game.dll is packed on disk, so this reads its code
 * as it is in memory, once the game has loaded, and logs:
 *   1. the rows (+0 value, then the raw 56 bytes) and the bytes just past the last one;
 *   2. every rip-relative instruction whose target lands in the rows (ModRM mod 00, rm 101), with its row and field;
 *   3. every 32-bit displacement equal to an RVA in the rows (the [image base + index*56 + 0x32FDA90] form), noting an
 *      imul by 56 (6B ?? 38 / 69 ?? 38 00 00 00) within 48 bytes before it;
 *   4. every 8-byte pointer in the data sections that points into the rows;
 *   5. the bytes of the booster-active check (+0x856D40, from PSS) and the booster ownership check (+0x136FDB0, from
 *      Diver Kit and PSS), to read by hand;
 *   6. then writes the unpacked game.dll to game.dll.25480438.dump in this mod's folder, for offline analysis.
 * Everything else goes to modloader.log under this mod's name. Turn it off when done. Build 25480438 only. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "hellforge_mod.h"

#define BUILD "25480438"
#define GAME_SIZE 74727424u
#define TABLE 0x32FDA90u
#define ROW 56u
#define VANILLA 21u                  /* rows 0..20 */
#define LO (TABLE - ROW)             /* a default row just before, as the stratagem list has */
#define HI (TABLE + (VANILLA + 1u) * ROW)
#define CHUNK (1u << 20)
#define MAX_SITES 4000u

static const HfApi* hf;
static HfMod* self;
static uintptr_t game;
static unsigned g_sites;

static uint32_t U32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t U64(const uint8_t* p) { uint64_t v; memcpy(&v, p, 8); return v; }

static void Hex(const uint8_t* p, size_t n, char* out, size_t cap)
{
    size_t k = 0;
    out[0] = 0;
    for (size_t i = 0; i < n && k + 4 < cap; i++) k += (size_t)snprintf(out + k, cap - k, "%02x ", p[i]);
}

/* "row 3 +0x10" for an RVA in the rows */
static void Slot(uint32_t rva, char* out, size_t cap)
{
    int64_t off = (int64_t)rva - (int64_t)TABLE;
    if (off < 0) snprintf(out, cap, "default row +0x%02X", (unsigned)(off + ROW));
    else if ((uint64_t)off >= (uint64_t)VANILLA * ROW) snprintf(out, cap, "PAST END +0x%X", (unsigned)(off - VANILLA * ROW));
    else snprintf(out, cap, "row %u +0x%02X", (unsigned)(off / ROW), (unsigned)(off % ROW));
}

static int ImulBefore(const uint8_t* buf, size_t at)
{
    size_t from = at > 48 ? at - 48 : 0;
    for (size_t i = from; i + 3 <= at; i++)
    {
        if (buf[i] == 0x6B && buf[i + 2] == 0x38) return 1;
        if (i + 6 <= at && buf[i] == 0x69 && U32(buf + i + 2) == ROW) return 1;
    }
    return 0;
}

static void Site(const char* form, uint32_t rva, uint32_t target, const uint8_t* buf, size_t at, size_t len, int imul)
{
    if (++g_sites > MAX_SITES) { if (g_sites == MAX_SITES + 1) hf->log(self, "  (more sites; stopped logging at %u)", MAX_SITES); return; }
    char slot[40], ctx[3 * 24 + 4];
    size_t from = at >= 8 ? at - 8 : 0, to = at + 16 <= len ? at + 16 : len;
    Slot(target, slot, sizeof slot);
    Hex(buf + from, to - from, ctx, sizeof ctx);
    hf->log(self, "  %s game.dll+0x%07X -> %s%s | %s", form, rva, slot, imul ? " (imul 56 before)" : "", ctx);
}

/* One executable section: rip-relative targets and absolute displacements in the rows */
static void ScanCode(uint32_t va, uint32_t size, const char* name)
{
    uint8_t* buf = (uint8_t*)VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buf) { hf->log(self, "%s: no memory for %u bytes", name, size); return; }
    uint32_t got = 0;
    for (uint32_t o = 0; o < size; o += CHUNK)
    {
        uint32_t n = size - o < CHUNK ? size - o : CHUNK;
        if (hf->read_code(game + va + o, buf + o, n)) break;
        got = o + n;
    }
    hf->log(self, "%s: 0x%X bytes at +0x%X, 0x%X read", name, size, va, got);
    unsigned rip = 0, abs = 0;
    for (size_t i = 1; i + 4 <= got; i++)
    {
        int32_t disp = (int32_t)U32(buf + i);
        uint32_t raw = U32(buf + i);
        /* rip-relative: ModRM mod 00 rm 101 just before; the target is from the instruction's end, so try the
         * immediate sizes that can follow (0, 1, 2, 4) */
        if ((buf[i - 1] & 0xC7) == 0x05)
        {
            static const uint32_t imm[4] = {0, 1, 2, 4};
            for (int k = 0; k < 4; k++)
            {
                int64_t t = (int64_t)va + (int64_t)i + 4 + imm[k] + disp;
                if (t >= LO && t < HI)
                {
                    char form[16];
                    snprintf(form, sizeof form, "rip+%u", imm[k]);
                    Site(form, va + (uint32_t)i - 1, (uint32_t)t, buf, i, got, 0);
                    rip++;
                    break;
                }
            }
        }
        if (raw >= LO && raw < HI)
        {
            Site("abs", va + (uint32_t)i, raw, buf, i, got, ImulBefore(buf, i));
            abs++;
        }
    }
    hf->log(self, "%s: %u rip-relative, %u absolute", name, rip, abs);
    VirtualFree(buf, 0, MEM_RELEASE);
}

/* One data section: 8-byte pointers into the rows */
static void ScanData(uint32_t va, uint32_t size, const char* name)
{
    static uint8_t buf[CHUNK];
    unsigned found = 0;
    for (uint32_t o = 0; o < size; o += CHUNK - 8)
    {
        uint32_t n = size - o < CHUNK ? size - o : CHUNK;
        if (hf->read(game + va + o, buf, n)) break;
        for (uint32_t i = 0; i + 8 <= n; i += 8)
        {
            uint64_t p = U64(buf + i);
            if (p >= game + LO && p < game + HI)
            {
                char slot[40];
                Slot((uint32_t)(p - game), slot, sizeof slot);
                hf->log(self, "  pointer at game.dll+0x%07X -> %s", va + o + i, slot);
                found++;
            }
        }
        if (n < CHUNK) break;
    }
    if (found) hf->log(self, "%s: %u pointers into the rows", name, found);
}

static void Dump(const char* what, uint32_t rva, uint32_t n)
{
    uint8_t buf[0x200];
    char line[3 * 32 + 4];
    if (n > sizeof buf || hf->read_code(game + rva, buf, n)) { hf->log(self, "%s +0x%X: unreadable", what, rva); return; }
    hf->log(self, "%s game.dll+0x%X, %u bytes:", what, rva, n);
    for (uint32_t o = 0; o < n; o += 32)
    {
        Hex(buf + o, n - o < 32 ? n - o : 32, line, sizeof line);
        hf->log(self, "  +%03X %s", o, line);
    }
}

/* game.dll as it is in memory, laid out by RVA (file offset = RVA), unpacked: code through read_code (the bytes before
 * any hook), the rest through read; a page neither can read is left zero and listed in the log. Written once to
 * <this mod's folder>/game.dll.25480438.dump, skipped if that file is already there. It is the game's code: keep it
 * on your own disk (the repo's .gitignore leaves *.dump out). */
static void WriteImage(size_t size)
{
    const char* folder = hf->mod_folder(self);
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
    (void)mod; (void)event; (void)user;
    size_t size = 0;
    game = hf->module_base("game.dll", &size);
    if (!game || size != GAME_SIZE) { hf->log(self, "REFUSED: not build " BUILD "'s game.dll"); return; }

    /* 1. the rows */
    uint8_t rows[(VANILLA + 2u) * ROW];
    if (hf->read(game + LO, rows, sizeof rows)) hf->log(self, "rows unreadable");
    else
    {
        char line[3 * ROW + 4];
        for (unsigned r = 0; r < VANILLA + 2u; r++)
        {
            Hex(rows + r * ROW, ROW, line, sizeof line);
            const char* tag = r == 0 ? "default?" : r == VANILLA + 1u ? "past end" : "";
            hf->log(self, "row %-2d %-8s +0=%-3u %s", (int)r - 1, tag, U32(rows + r * ROW), line);
        }
    }

    /* the sections */
    uint8_t h[0x400];
    if (hf->read(game, h, sizeof h) || h[0] != 'M' || h[1] != 'Z') { hf->log(self, "headers unreadable"); return; }
    uint32_t pe = U32(h + 0x3C);
    if (pe + 24 > sizeof h) { hf->log(self, "PE header out of range"); return; }
    unsigned count = (unsigned)(h[pe + 6] | h[pe + 7] << 8), opt = (unsigned)(h[pe + 20] | h[pe + 21] << 8);
    uint32_t sec = pe + 24 + opt;
    hf->log(self, "game.dll: %u sections; rows 0x%X..0x%X", count, LO, HI);
    for (unsigned s = 0; s < count && sec + 40 * (s + 1) <= sizeof h; s++)
    {
        const uint8_t* e = h + sec + 40 * s;
        char name[9] = {0};
        memcpy(name, e, 8);
        uint32_t vsize = U32(e + 8), va = U32(e + 12), flags = U32(e + 36);
        if (flags & 0x20000000u) ScanCode(va, vsize, name);   /* executable */
        else if (flags & 0x40000000u) ScanData(va, vsize, name);
    }

    /* 5. the checks to read by hand */
    Dump("booster active (PSS)", 0x856D40, 0x80);
    Dump("booster owned (Diver Kit, PSS)", 0x136FDB0, 0x80);
    hf->log(self, "done: %u code sites. Send modloader.log.", g_sites);

    /* 6. the unpacked image, for offline analysis */
    WriteImage(size);
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 13 || api->size < offsetof(HfApi, read_code) + sizeof api->read_code) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    api->log(mod, "Booster Table Probe 0.2.0 (research only, no gameplay effect)");
    return 0;
}
