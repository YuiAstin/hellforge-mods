/* kitbase extras: what mods other than the kits add to the kits' options screen, and the bonuses Shared Mods publishes
 * for the kits to read.
 *
 * Code shared by the character kits (and Shared Mods, which is not a kit). Source of truth: kitbase/ at the repo root.
 * The copy in a mod's src/kitbase/ is written by tools/kitbase_sync.py, so edit kitbase/, never the copy. See
 * docs/kit-base.md. Include after the C headers, <windows.h> and the HellForge SDK headers. Needs nothing else of kitbase.
 *
 * Tabs (SDK 17 screens). A mod adds a tab to the kits' options screen (kb_options.h) with KbExtraAdd: a second block of
 * named shared memory (Local\yui.kitbase.extras.<pid>), apart from the kits' hub so its layout stays as released kits
 * have it. The kit that owns the screen draws a row of tabs over it ("KIT OPTIONS" and each extra's label) and, while an
 * extra's tab is shown, calls its body in the box the kit's rows use, with the kit shown in the character column. A kit
 * built before this header has no tabs: an extra then waits for a newer kit to own the screen.
 *
 * Bonuses (Shared Mods). Shared Mods writes, for each kit id, what its equipped mods give (Local\yui.sharedmods.<pid>):
 * multipliers on her skills' cooldowns, her skills' damage and their durations, and on the damage she takes (Shared Mods
 * applies that one itself). A kit reads them with SharedMods(id, &b) (at a mission's start: a loadout changed on the
 * screen applies from the next mission). Without Shared Mods every multiplier reads 1. */
#ifndef KB_EXTRAS_H
#define KB_EXTRAS_H

#ifndef KB_FN
#define KB_FN static __attribute__((unused))
#define KB_VAR static __attribute__((unused))
#endif

/* ---- the bonuses -------------------------------------------------------------------------------------------------- */
#define KB_SM_MAGIC 0x444F4D53u        /* "SMOD" */
#define KB_SM_KITS 32
typedef struct KbSmBonus
{
    char id[32];                       /* the kit's id ("hina") */
    float cooldown;                    /* her skills' cooldowns x this (0.85: 15% shorter) */
    float damage;                      /* her skills' damage x this */
    float duration;                    /* her skills' durations x this */
    float taken;                       /* damage she takes x this (applied by Shared Mods) */
    float spare[4];                    /* 1 each: for mods to come */
} KbSmBonus;
typedef struct KbSmPub
{
    uint32_t magic, version;
    volatile LONG seq;                 /* odd while Shared Mods writes */
    uint32_t count;
    KbSmBonus kit[KB_SM_KITS];
} KbSmPub;
KB_VAR KbSmPub* kb_smpub;
KB_FN KbSmPub* SmPub(void)
{
    if (kb_smpub) return kb_smpub;
    wchar_t name[64];
    _snwprintf(name, 64, L"Local\\yui.sharedmods.%lu", (unsigned long)GetCurrentProcessId());
    HANDLE m = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(KbSmPub), name);
    if (!m) return NULL;
    kb_smpub = (KbSmPub*)MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(KbSmPub));
    return kb_smpub;
}
KB_FN void SmNone(KbSmBonus* b, const char* id)
{
    memset(b, 0, sizeof *b);
    snprintf(b->id, sizeof b->id, "%s", id ? id : "");
    b->cooldown = b->damage = b->duration = b->taken = 1;
    for (int i = 0; i < 4; i++) b->spare[i] = 1;
}
/* The kit's bonuses: 1 when Shared Mods publishes something for it, 0 (and every multiplier 1) otherwise. Each
 * multiplier is kept to 0.05..20, whatever was written. */
KB_FN int SharedMods(const char* id, KbSmBonus* out)
{
    SmNone(out, id);
    KbSmPub* p = SmPub();
    if (!p || p->magic != KB_SM_MAGIC || !id) return 0;
    for (int tries = 0; tries < 50; tries++)
    {
        LONG s = p->seq;
        if (s & 1) { Sleep(0); continue; }
        int found = 0;
        uint32_t n = p->count > KB_SM_KITS ? KB_SM_KITS : p->count;
        for (uint32_t i = 0; i < n; i++)
            if (!strncmp(p->kit[i].id, id, 31)) { *out = p->kit[i]; found = 1; break; }
        if (p->seq != s) continue;
        if (!found) { SmNone(out, id); return 0; }
        float* f = &out->cooldown;
        for (int i = 0; i < 8; i++) f[i] = !(f[i] >= 0.05f) ? 1 : f[i] > 20 ? 20 : f[i];
        out->id[31] = 0;
        return 1;
    }
    SmNone(out, id);
    return 0;
}

/* ---- the tabs ------------------------------------------------------------------------------------------------------ */
#ifdef HF_SCREEN_MODAL
/* the box a kit's rows (or an extra's body) are drawn in; tip: the character column's, when the mouse is over it */
#ifndef KB_OPBOX_DEFINED
#define KB_OPBOX_DEFINED
typedef struct KbOpBox { float x0, y0, x1, y1, alpha; const char* tip; } KbOpBox;
#endif
#define KB_EXTRAS_MAGIC 0x52545845u    /* "EXTR" */
#define KB_EXTRAS_MAX 8
typedef struct KbOpExtra
{
    char id[32];                       /* the mod's ("shared-mods") */
    char label[24];                    /* its tab ("SHARED MODS") */
    /* on the host's render thread while its tab is shown: kit, the kit shown in the character column ("" none) */
    void (*body)(const HfDraw* d, const HfInput* in, float dt, const KbOpBox* box, const char* kit, const char* kitName);
} KbOpExtra;
typedef struct KbExtras
{
    uint32_t magic, version, count;
    volatile LONG lock;
    volatile LONG opens, closes;       /* the screen's, counted by its owner: an extra saves at a close */
    uint32_t tabs;                     /* 1: the screen's owner draws tabs (a kit built with this header) */
    uint32_t reserved;
    KbOpExtra tab[KB_EXTRAS_MAX];
} KbExtras;
KB_VAR KbExtras* kb_extras;
KB_FN KbExtras* Extras(void)
{
    if (kb_extras) return kb_extras;
    wchar_t name[64];
    _snwprintf(name, 64, L"Local\\yui.kitbase.extras.%lu", (unsigned long)GetCurrentProcessId());
    HANDLE m = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(KbExtras), name);
    if (!m) return NULL;
    KbExtras* x = (KbExtras*)MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(KbExtras));
    if (!x) return NULL;
    for (int i = 0; i < 2000 && InterlockedCompareExchange(&x->lock, 1, 0); i++) Sleep(1);
    if (x->magic != KB_EXTRAS_MAGIC) { x->version = 1; x->magic = KB_EXTRAS_MAGIC; }   /* a new block is all zeros */
    InterlockedExchange(&x->lock, 0);
    kb_extras = x;
    return x;
}
/* adds (or replaces) the mod's tab: 1, or 0 (no shared block, or no room) */
KB_FN int KbExtraAdd(const char* id, const char* label, void (*body)(const HfDraw*, const HfInput*, float, const KbOpBox*, const char*, const char*))
{
    KbExtras* x = Extras();
    if (!x) return 0;
    for (int i = 0; i < 2000 && InterlockedCompareExchange(&x->lock, 1, 0); i++) Sleep(1);
    int at = -1;
    for (uint32_t i = 0; i < x->count && i < KB_EXTRAS_MAX; i++) if (!strncmp(x->tab[i].id, id, 31)) at = (int)i;
    if (at < 0 && x->count < KB_EXTRAS_MAX) at = (int)x->count;
    if (at >= 0)
    {
        KbOpExtra e;
        memset(&e, 0, sizeof e);
        snprintf(e.id, sizeof e.id, "%s", id);
        snprintf(e.label, sizeof e.label, "%s", label);
        e.body = body;
        x->tab[at] = e;
        MemoryBarrier();
        if ((uint32_t)at == x->count) x->count++;
    }
    InterlockedExchange(&x->lock, 0);
    return at >= 0;
}
#endif /* HF_SCREEN_MODAL */

#endif
