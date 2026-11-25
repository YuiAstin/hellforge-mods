/* kitbase layer 0: memory, lookups, entities, units, log, keys.
 *
 * Code shared by the character kits. Source of truth: kitbase/ at the repo root. The copy in a kit's src/kitbase/ is
 * written by tools/kitbase_sync.py, so edit kitbase/, never the copy. See docs/kit-base.md.
 * Include after the C headers and the HellForge SDK headers. Everything is static and marked unused, so a kit only
 * pays for what it calls. */
#ifndef KB_CORE_H
#define KB_CORE_H

#ifndef KB_FN
#define KB_FN static __attribute__((unused))
#define KB_VAR static __attribute__((unused))
#endif

/* Her Helldiver's resource, and the head nodes PlayerPose aims from */
#define AVATAR UINT64_C(0x4D1C334D294DFA97)
#define NODE_HEAD 90u
#define NODE_HEAD_AIM 91u
/* A key and the modifiers that must be held with it (exactly: Q alone and Shift+Q are different keys) */
#define KM_SHIFT 1
#define KM_CTRL 2
#define KM_ALT 4
typedef struct Key { int vk, mods; char label[24]; } Key;

/* The SDK and the game, set by the kit's hellforge_mod_init */
/* This kit is the one acting this mission (kb_roster.h: with several kits installed, the one the player picked) */
KB_VAR int kb_kit_on = 1;

KB_VAR const HfApi* hf;
KB_VAR HfMod* self;
KB_VAR uintptr_t game, exe;

KB_FN uint32_t Word(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
KB_FN uint64_t Quad(const uint8_t* p) { uint64_t v; memcpy(&v, p, 8); return v; }
KB_FN int AddressOk(uintptr_t a, size_t n) { return n && a >= 0x10000 && a < UINT64_C(0x800000000000) && n <= UINT64_C(0x800000000000) - a; }
KB_FN int ReadAt(uintptr_t a, void* out, size_t n) { return AddressOk(a, n) && hf->read(a, out, n) == 0; }
KB_FN uintptr_t Pointer(uintptr_t a) { uintptr_t v = 0; return ReadAt(a, &v, 8) && AddressOk(v, 1) ? v : 0; }
KB_FN int Lookup(uintptr_t at, uint32_t key, uint32_t limit, uint32_t* index)
{
    uint8_t h[20], row[8];
    if (!ReadAt(at, h, sizeof h)) return 0;
    uintptr_t entries = (uintptr_t)Quad(h);
    uint32_t n = Word(h + 8), empty = Word(h + 12), mult = Word(h + 16);
    if (!AddressOk(entries, 8) || !n || n > limit || (n & (n - 1)) || key == empty) return 0;
    for (unsigned i = 0; i < n && i < 128; i++)
    {
        if (!ReadAt(entries + 8u * ((key * mult + i) & (n - 1)), row, 8)) return 0;
        if (Word(row) == key) { *index = Word(row + 4); return *index != UINT32_MAX; }
        if (Word(row) == empty) return 0;
    }
    return 0;
}
KB_FN int HashFind(uintptr_t header, uint32_t key, uint32_t* value)
{
    uint8_t h[20];
    if (!key || !ReadAt(header, h, sizeof h)) return 0;
    uintptr_t buckets = (uintptr_t)Quad(h);
    uint32_t cap = Word(h + 8), empty = Word(h + 12), mult = Word(h + 16);
    if (!AddressOk(buckets, 8) || !cap || cap > 1048576) return 0;
    for (uint32_t i = 0; i < cap && i < 4096; i++)
    {
        uint32_t row[2];
        if (!ReadAt(buckets + 8u * ((key * mult + i) % cap), row, 8)) return 0;
        if (row[0] == key) { *value = row[1]; return row[1] != UINT32_MAX; }
        if (row[0] == empty) return 0;
    }
    return 0;
}
/* An entity's descriptor: its resource (+0), entity (+8), unit (+12), flags (+20, bit 0: this machine's authority) */
KB_FN int Descriptor(uint32_t entity, uint8_t out[24])
{
    if (!game || !entity || entity == UINT32_MAX || entity == 0x7FFF) return 0;
    uintptr_t root = Pointer(game + 0x346BF98);
    uint32_t index;
    if (!root || !Lookup(root + 0xF1AEB0, entity, 1048576, &index) || index >= 262144) return 0;
    return ReadAt(root + 0xF32F18 + 24u * index, out, 24) && Word(out + 8) == entity;
}
/* A node's world position in a unit's scene graph (64-byte matrices, translation at +0x30) */
KB_FN int UnitNode(uint32_t unit, uint32_t node, float out[3])
{
    if (!exe) return 0;
    uintptr_t registry = Pointer(exe + 0x1A100F0), generations, objects, object, nodes;
    uint32_t count, id, slot = unit & 0x3FFFFF;
    uint8_t generation;
    if (!registry || !ReadAt(registry + 0x98, &count, 4) || count > 0x400000 || slot >= count ||
        !(generations = Pointer(registry + 0xA0)) || !ReadAt(generations + slot, &generation, 1) || generation != (uint8_t)(unit >> 22) ||
        !(objects = Pointer(registry + 0x88)) || !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit ||
        !ReadAt(object + 0x70, &count, 4) || node >= count || count > 4096 || !(nodes = Pointer(object + 0x88)))
        return 0;
    if (!ReadAt(nodes + 64u * node + 48, out, 12)) return 0;
    for (int i = 0; i < 3; i++) if (!isfinite(out[i]) || fabsf(out[i]) > 100000) return 0;
    return 1;
}
KB_FN float Dist2(const float* a, const float* b) { float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; return x * x + y * y + z * z; }
KB_FN int NetOk(uint32_t n) { return n && n != 0x7FFF && n != UINT32_MAX; }
KB_FN void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
KB_FN void Log(const char* fmt, ...)
{
    char buf[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(buf, sizeof buf, fmt, a);
    va_end(a);
    hf->log(self, "%s", buf);
}
/* An action's answer: done, requested or queued count as done; the log says why one was not */
KB_FN int Act(const char* what, int r)
{
    if (r == HF_ACT_DONE || r == HF_ACT_REQUESTED || r == HF_ACT_QUEUED) return 1;
    static unsigned noted;
    if (noted++ < 20)
        Log("%s: %s (%s)", what, r == HF_ACT_WAITING ? "waiting for its packages" : "refused", hf->last_log ? hf->last_log(self) : "");
    return 0;
}
KB_FN int QueryIdle(void)
{
    uintptr_t scheduler = Pointer(game + 0x347D7E0);
    uint32_t count, jobs[72];
    if (!scheduler || !ReadAt(scheduler, &count, 4) || count || !ReadAt(scheduler + 0x40008, jobs, sizeof jobs)) return 0;
    for (unsigned i = 0; i < 24; i++) if (jobs[3 * i + 2] != 1) return 0;
    return 1;
}
KB_FN int Focused(void)
{
    HWND w = GetForegroundWindow();
    DWORD pid = 0;
    if (w) GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}
/* Who holds an item: the Wieldable component (game+53634864, HD2Runtime's attribution research: hash +24, u32 wielders at
 * +56). Her rifle is held by her; a sentry or a turret holds itself, so its hits are its own. 1 with *by, 0 unreadable */
KB_FN int WielderOf(uint32_t e, uint32_t* by)
{
    uintptr_t m = Pointer(game + 53634864u), arr;
    uint32_t i;
    if (!e || !m || !Lookup(m + 24, e, 1048576, &i) || i >= 0x10000 || !(arr = Pointer(m + 56))) return 0;
    return ReadAt(arr + 4u * i, by, 4) && *by;
}
/* "Shift+Q", "Ctrl+Alt+F", "F5", "Mouse4", "5", or a Windows virtual-key code ("#81") */
KB_FN int ParseKey(const char* text, Key* out)
{
    Key k = {0, 0, ""};
    char buf[64];
    snprintf(buf, sizeof buf, "%s", text);
    for (char* tok = strtok(buf, "+ \t\r\n"); tok; tok = strtok(NULL, "+ \t\r\n"))
    {
        char t[24];
        size_t n = 0;
        for (; tok[n] && n < sizeof t - 1; n++) t[n] = (char)toupper((unsigned char)tok[n]);
        t[n] = 0;
        if (!strcmp(t, "SHIFT")) k.mods |= KM_SHIFT;
        else if (!strcmp(t, "CTRL") || !strcmp(t, "CONTROL")) k.mods |= KM_CTRL;
        else if (!strcmp(t, "ALT")) k.mods |= KM_ALT;
        else if (k.vk) return 0;  /* two main keys */
        else if (n == 1 && ((t[0] >= 'A' && t[0] <= 'Z') || (t[0] >= '0' && t[0] <= '9'))) k.vk = t[0];
        else if (t[0] == 'F' && n <= 3 && atoi(t + 1) >= 1 && atoi(t + 1) <= 24) k.vk = VK_F1 + atoi(t + 1) - 1;
        else if (!strcmp(t, "MOUSE4")) k.vk = VK_XBUTTON1;
        else if (!strcmp(t, "MOUSE5")) k.vk = VK_XBUTTON2;
        else if (!strcmp(t, "MOUSE3") || !strcmp(t, "MIDDLEMOUSE")) k.vk = VK_MBUTTON;
        else if (t[0] == '#' && atoi(t + 1) > 0 && atoi(t + 1) < 255) k.vk = atoi(t + 1);
        else if (n >= 2 && strspn(t, "0123456789") == n && atoi(t) > 0 && atoi(t) < 255) k.vk = atoi(t);  /* 0.1.0's codes */
        else return 0;
    }
    if (!k.vk)  /* a modifier alone is a key of its own: Ctrl, Alt or Shift */
    {
        if (k.mods == KM_SHIFT) k.vk = VK_SHIFT;
        else if (k.mods == KM_CTRL) k.vk = VK_CONTROL;
        else if (k.mods == KM_ALT) k.vk = VK_MENU;
        else return 0;
        k.mods = 0;
    }
    char name[24];
    if ((k.vk >= 'A' && k.vk <= 'Z') || (k.vk >= '0' && k.vk <= '9')) snprintf(name, sizeof name, "%c", k.vk);
    else if (k.vk >= VK_F1 && k.vk <= VK_F24) snprintf(name, sizeof name, "F%d", k.vk - VK_F1 + 1);
    else if (k.vk == VK_XBUTTON1) snprintf(name, sizeof name, "M4");
    else if (k.vk == VK_XBUTTON2) snprintf(name, sizeof name, "M5");
    else if (k.vk == VK_MBUTTON) snprintf(name, sizeof name, "M3");
    else if (k.vk == VK_SHIFT) snprintf(name, sizeof name, "Shift");
    else if (k.vk == VK_CONTROL) snprintf(name, sizeof name, "Ctrl");
    else if (k.vk == VK_MENU) snprintf(name, sizeof name, "Alt");
    else snprintf(name, sizeof name, "#%d", k.vk);
    snprintf(k.label, sizeof k.label, "%s%s%s%s", k.mods & KM_CTRL ? "Ctrl+" : "", k.mods & KM_ALT ? "Alt+" : "",
        k.mods & KM_SHIFT ? "Shift+" : "", name);
    *out = k;
    return 1;
}
/* The settings file, opened by its UTF-8 path as Windows reads it (plain fopen reads the path in the ANSI code page, so a
 * folder with a non-English letter in it fails). HellForge holds mod folders read-only while the game runs, so the mod
 * never writes it: the release ships it with the defaults (made from the text below by inigen/gen.py). */
KB_FN FILE* OpenUtf8(const char* path, const wchar_t* mode)
{
    wchar_t w[MAX_PATH * 2];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, MAX_PATH * 2)) return NULL;
    return _wfopen(w, mode);
}

#endif
