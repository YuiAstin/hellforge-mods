/* kitbase roster: several character kits installed together, one acting.
 *
 * Code shared by the character kits. Source of truth: kitbase/ at the repo root. The copy in a kit's src/kitbase/ is
 * written by tools/kitbase_sync.py, so edit kitbase/, never the copy. See docs/kit-base.md.
 * Include after the C headers and the HellForge SDK headers. Everything is static and marked unused, so a kit only
 * pays for what it calls.
 *
 * There is no base mod: every kit joins a roster kept in a small block of named shared memory, one per game process
 * (Local\yui.kitbase.roster.<pid>), which every kit DLL in the game opens. With one kit installed it simply acts, as
 * before. With two or more, the HUDs offer a picker (Mod Options Menu: GFL2 Kits HUD > Active Character), and only the
 * picked kit acts; the others stand by (kb_kit_on 0: no keys, no body, no damage changes, no HUD; their hit hooks pass
 * every hit straight through). A pick takes effect at the next mission start.
 *
 * A kit:  RosterJoin("exusiai", "Exusiai") in hellforge_mod_init; KitDecide() at mission start (sets kb_kit_on);
 *         KB_ROSTER_EXPORTS(exusiai) once, for its HUD (exusiai_roster, exusiai_pick). */
#ifndef KB_ROSTER_H
#define KB_ROSTER_H

#include "kb_core.h"

#define KB_ROSTER_MAX 16
#define KB_ROSTER_MAGIC 0x52425953u   /* "SYBR" */
typedef struct KbRoster
{
    uint32_t magic, version, count, picks;
    char id[KB_ROSTER_MAX][32];       /* sorted by id, so every kit sees the same order */
    char name[KB_ROSTER_MAX][32];
    char active[32];                  /* the picked kit's id ("": none picked: the first) */
} KbRoster;
KB_VAR KbRoster* kb_roster;
KB_VAR HANDLE kb_roster_lock;
KB_VAR const char* kb_my_id;

KB_FN KbRoster* Roster(void)
{
    if (kb_roster) return kb_roster;
    wchar_t name[64], lock[64];
    DWORD pid = GetCurrentProcessId();
    _snwprintf(name, 64, L"Local\\yui.kitbase.roster.%lu", (unsigned long)pid);
    _snwprintf(lock, 64, L"Local\\yui.kitbase.lock.%lu", (unsigned long)pid);
    if (!kb_roster_lock) kb_roster_lock = CreateMutexW(NULL, FALSE, lock);
    HANDLE m = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(KbRoster), name);
    if (!m) return NULL;
    /* the handle stays open for the process' life, so the block lives as long as any kit does */
    kb_roster = (KbRoster*)MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(KbRoster));
    return kb_roster;
}
KB_FN int RosterLock(void) { return kb_roster_lock && WaitForSingleObject(kb_roster_lock, 2000) == WAIT_OBJECT_0; }
KB_FN void RosterUnlock(void) { if (kb_roster_lock) ReleaseMutex(kb_roster_lock); }

/* this kit joins the roster (from hellforge_mod_init) */
KB_FN void RosterJoin(const char* id, const char* name)
{
    kb_my_id = id;
    if (!hf) return;
    KbRoster* r = Roster();
    if (!r || !RosterLock()) { Log("roster: the shared roster is unavailable: this kit acts on its own"); return; }
    if (r->magic != KB_ROSTER_MAGIC) { memset(r, 0, sizeof *r); r->magic = KB_ROSTER_MAGIC; r->version = 1; }
    int at = (int)r->count, known = 0;
    for (uint32_t i = 0; i < r->count; i++)
    {
        int c = strncmp(r->id[i], id, 31);
        if (c == 0) { known = 1; break; }
        if (c > 0 && at == (int)r->count) at = (int)i;
    }
    if (!known && r->count < KB_ROSTER_MAX)
    {
        for (int i = (int)r->count; i > at; i--) { memcpy(r->id[i], r->id[i - 1], 32); memcpy(r->name[i], r->name[i - 1], 32); }
        snprintf(r->id[at], 32, "%s", id);
        snprintf(r->name[at], 32, "%s", name);
        r->count++;
    }
    uint32_t n = r->count;
    RosterUnlock();
    Log("roster: %s joined (%u character kit%s installed)", name, n, n == 1 ? "" : "s");
}
/* whether this kit acts: alone, picked, or (nothing picked) first by id */
KB_FN int KitActive(void)
{
    KbRoster* r = Roster();
    if (!r || r->magic != KB_ROSTER_MAGIC || r->count <= 1 || !kb_my_id) return 1;
    int on;
    if (!RosterLock()) return 1;
    int picked = 0;
    for (uint32_t i = 0; i < r->count; i++) if (r->active[0] && !strncmp(r->id[i], r->active, 31)) picked = 1;
    on = picked ? !strncmp(r->active, kb_my_id, 31) : !strncmp(r->id[0], kb_my_id, 31);
    RosterUnlock();
    return on;
}
/* at mission start: this mission's answer */
KB_FN void KitDecide(void)
{
    int was = kb_kit_on;
    kb_kit_on = KitActive();
    KbRoster* r = kb_roster;
    if (r && r->magic == KB_ROSTER_MAGIC && r->count > 1 && (kb_kit_on != was || !kb_kit_on))
        Log(kb_kit_on ? "roster: this kit acts this mission (picked in GFL2 Kits HUD > Active Character)"
                      : "roster: standing by this mission: another character kit is picked (GFL2 Kits HUD > Active Character)");
}
/* for the HUD: "id:Name;id:Name" (sorted), and the active id after a '|' */
KB_FN int RosterText(char* out, int size)
{
    KbRoster* r = Roster();
    if (!out || size < 2) return 0;
    out[0] = 0;
    if (!r || r->magic != KB_ROSTER_MAGIC || !RosterLock()) return 0;
    int k = 0;
    for (uint32_t i = 0; i < r->count && k < size - 1; i++)
        k += snprintf(out + k, (size_t)(size - k), "%s%s:%s", i ? ";" : "", r->id[i], r->name[i]);
    if (k < size - 1) snprintf(out + k, (size_t)(size - k), "|%s", r->active);
    int n = (int)r->count;
    RosterUnlock();
    return n;
}
KB_FN int RosterPick(const char* id)
{
    KbRoster* r = Roster();
    if (!id || !r || r->magic != KB_ROSTER_MAGIC || !RosterLock()) return 0;
    int ok = 0;
    for (uint32_t i = 0; i < r->count; i++) if (!strncmp(r->id[i], id, 31)) ok = 1;
    if (ok && strncmp(r->active, id, 31)) { snprintf(r->active, 32, "%s", id); r->picks++; }
    RosterUnlock();
    if (ok) { static char told[32]; if (strncmp(told, id, 31)) { snprintf(told, 32, "%s", id); Log("roster: %s picked (from the next mission start)", id); } }
    return ok;
}
/* the HUD's two calls, named after the kit (exusiai_roster, exusiai_pick) */
#define KB_ROSTER_EXPORTS(kit) \
    HF_MOD_EXPORT int kit##_roster(char* out, int size) { return RosterText(out, size); } \
    HF_MOD_EXPORT int kit##_pick(const char* id) { return RosterPick(id); }

#endif
