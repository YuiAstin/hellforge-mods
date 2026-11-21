/* kitbase upgrades: the UPGRADES tab of the kits' options screen, where the shown kit's upgrades are bought with the
 * player's currency (HellForge's spend_currency, SDK 21), and what a bought upgrade does while its kit is the picked one.
 * A test: one upgrade, One-Handed Grip (every two-handed gun held one-handed, in the one-handed SMGs' grip).
 *
 * Code shared by the character kits. Source of truth: kitbase/ at the repo root (tools/kitbase_sync.py writes the copies).
 * Included by kb_options.h. The screen's owner does all of it: the render thread asks for a purchase, the game thread
 * spends and applies, so one kit alone writes the game's Equipment table. What was bought is kept in
 * %LOCALAPPDATA%\HellForgeMods\upgrades.ini ("<kit> = one_hand"); the Forge Credits, in the player's profile. */
#ifndef KB_UPGRADES_H
#define KB_UPGRADES_H

#define KB_UP_PRICE 100                         /* One-Handed Grip, in Forge Credits */
#define KB_UP_CURRENCY 0x6C3C83C1u              /* HF_CURRENCY_FORGE_CREDITS */
#define KB_UP_CURRENCY_NAME "FORGE CREDITS"
#define KB_UP_GRIP 70u                          /* the one-handed SMGs' grip pose (Knight, Defender, Pummeler) */
#define KB_UP_TWO_HANDED 0x00010000u            /* Equipment +0xC4 of a two-handed weapon; bit 0 makes it one-handed */
#define KB_UP_ROWS 688u
#define KB_UP_RECORDS 1024u

KB_VAR SRWLOCK g_upLock = SRWLOCK_INIT;
KB_VAR char g_upOwned[KB_ROSTER_MAX][32];       /* kits that bought One-Handed Grip (under g_upLock) */
KB_VAR int g_upOwnedN, g_upLoaded;
KB_VAR _Atomic int g_upBuyWant = -1;            /* the screen's purchase: a hub index */
KB_VAR _Atomic int g_upBusy;                    /* a purchase waits for HellForge's answer */
KB_VAR _Atomic long long g_upBalance = -1;      /* Forge Credits as the game holds them; -1 not known */
KB_VAR char g_upStatus[96];                     /* the last purchase's outcome, for the screen */
KB_VAR char g_upBuyingKit[32], g_upBuyingName[32];
KB_VAR int g_upTab;                             /* 0 OPTIONS, 1 UPGRADES */

KB_FN int UpCanSpend(void)
{
#ifdef HF_SPEND_SPENT
    return hf->version >= 21 && hf->size >= offsetof(HfApi, currency_balance) + sizeof hf->currency_balance;
#else
    return 0;
#endif
}

KB_FN void UpPath(char* out, size_t size)
{
    char local[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", local, sizeof local);
    out[0] = 0;
    if (n && n < sizeof local) snprintf(out, size, "%s\\HellForgeMods\\upgrades.ini", local);
}
KB_FN int UpOwns(const char* kit)
{
    int yes = 0;
    AcquireSRWLockShared(&g_upLock);
    for (int i = 0; i < g_upOwnedN; i++) if (!strncmp(g_upOwned[i], kit, 31)) yes = 1;
    ReleaseSRWLockShared(&g_upLock);
    return yes;
}
KB_FN void UpLoad(void)
{
    char path[MAX_PATH * 2], line[128], kit[32], what[32];
    UpPath(path, sizeof path);
    FILE* f = path[0] ? fopen(path, "r") : NULL;
    AcquireSRWLockExclusive(&g_upLock);
    g_upOwnedN = 0;
    while (f && fgets(line, sizeof line, f) && g_upOwnedN < KB_ROSTER_MAX)
        if (line[0] != ';' && sscanf(line, " %31[^= ] = %31s", kit, what) == 2 && !strcmp(what, "one_hand"))
            snprintf(g_upOwned[g_upOwnedN++], 32, "%s", kit);
    ReleaseSRWLockExclusive(&g_upLock);
    if (f) fclose(f);
    g_upLoaded = 1;
}
KB_FN void UpSave(void)
{
    char path[MAX_PATH * 2];
    UpPath(path, sizeof path);
    if (!path[0]) return;
    OpMakeDir(path);
    FILE* o = fopen(path, "w");
    if (!o) { Log("upgrades: could not save %s", path); return; }
    fprintf(o, "; The character kits' upgrades bought on the options screen (<kit> = <upgrade>). The currency is in the profile.\n");
    AcquireSRWLockShared(&g_upLock);
    for (int i = 0; i < g_upOwnedN; i++) fprintf(o, "%s = one_hand\n", g_upOwned[i]);
    ReleaseSRWLockShared(&g_upLock);
    fclose(o);
}

/* ---- buying (game thread) --------------------------------------------------------------------------------------- */
#ifdef HF_SPEND_SPENT
KB_FN void UpPaid(HfMod* mod, uint32_t request, uint32_t result, int64_t balance, void* user)
{
    (void)mod; (void)request; (void)user;
    if (result == HF_SPEND_SPENT)
    {
        AcquireSRWLockExclusive(&g_upLock);
        if (g_upOwnedN < KB_ROSTER_MAX) snprintf(g_upOwned[g_upOwnedN++], 32, "%s", g_upBuyingKit);
        ReleaseSRWLockExclusive(&g_upLock);
        UpSave();
        snprintf(g_upStatus, sizeof g_upStatus, "%s: One-Handed Grip bought", g_upBuyingName);
    }
    else if (result == HF_SPEND_TOO_LITTLE) snprintf(g_upStatus, sizeof g_upStatus, "Not enough Forge Credits (%lld)", (long long)balance);
    else snprintf(g_upStatus, sizeof g_upStatus, "The purchase failed: see the mod's log");
    if (balance >= 0) atomic_store(&g_upBalance, balance);
    Log("upgrades: %s's One-Handed Grip: %s", g_upBuyingKit, g_upStatus);
    atomic_store(&g_upBusy, 0);
}
#endif
KB_FN void UpBuy(int hubIndex)
{
#ifdef HF_SPEND_SPENT
    KbOpHub* h = kb_ophub;
    if (!h || hubIndex < 0 || hubIndex >= (int)h->count || atomic_load(&g_upBusy) || !UpCanSpend()) return;
    snprintf(g_upBuyingKit, sizeof g_upBuyingKit, "%s", h->kit[hubIndex].id);
    snprintf(g_upBuyingName, sizeof g_upBuyingName, "%s", g_upBuyingKit);
    KbRoster* r = kb_roster;
    if (r && r->magic == KB_ROSTER_MAGIC && RosterLock())
    {
        for (uint32_t i = 0; i < r->count; i++) if (!strncmp(r->id[i], g_upBuyingKit, 31)) snprintf(g_upBuyingName, sizeof g_upBuyingName, "%s", r->name[i]);
        RosterUnlock();
    }
    if (UpOwns(g_upBuyingKit)) return;
    char reason[96];
    snprintf(reason, sizeof reason, "%s upgrade: One-Handed Grip", g_upBuyingName);
    atomic_store(&g_upBusy, 1);
    if (!hf->spend_currency(self, KB_UP_CURRENCY, KB_UP_PRICE, reason, UpPaid, NULL))
    {
        snprintf(g_upStatus, sizeof g_upStatus, "HellForge refused the purchase (the mod's log says why)");
        atomic_store(&g_upBusy, 0);
    }
#else
    (void)hubIndex;
#endif
}

/* ---- One-Handed Grip (game thread): the game's Equipment table, as yui.one-hand-test found it ------------------------
 * [[game + 0x346BF98] + 0xF12478 + 8 * 233] -> index rows {u64 resource, u32 record, u32 0} x 688, 232-byte records from
 * +11008; +0xC4 bit 0 is one-handed, +0xC0 the grip. Only records at exactly KB_UP_TWO_HANDED change, and each goes back
 * as it was when the picked kit has no grip. A weapon equipped after the change has it. Game build 25480438. */
KB_VAR struct { uint32_t record, grip; } g_upChanged[KB_UP_RECORDS];
KB_VAR int g_upChangedN, g_upApplied;
KB_FN uintptr_t UpTable(void)
{
    const char* build = hf->build_id ? hf->build_id() : NULL;
    if (!build || strcmp(build, "25480438") || !game) return 0;
    uintptr_t root = Pointer(game + 0x346BF98);
    return root ? Pointer(root + 0xF12478 + 8u * 233u) : 0;
}
KB_FN void UpGrip(int on)
{
    uintptr_t table = UpTable();
    if (!table) return;
    if (on)
    {
        static uint8_t seen[KB_UP_RECORDS];
        memset(seen, 0, sizeof seen);
        g_upChangedN = 0;
        for (uint32_t i = 0; i < KB_UP_ROWS; i++)
        {
            struct { uint64_t res; uint32_t record, zero; } row;
            if (!ReadAt(table + 16u * i, &row, sizeof row)) break;
            if (!row.res || row.record >= KB_UP_RECORDS || seen[row.record]) continue;
            seen[row.record] = 1;
            uintptr_t at = table + 11008u + (uintptr_t)row.record * 232u;
            uint32_t flags = 0, grip = 0, one = KB_UP_TWO_HANDED | 1u, pose = KB_UP_GRIP;
            if (!ReadAt(at + 0xC4, &flags, 4) || flags != KB_UP_TWO_HANDED || !ReadAt(at + 0xC0, &grip, 4)) continue;
            if (hf->write(at + 0xC4, &one, 4) || hf->write(at + 0xC0, &pose, 4)) continue;
            g_upChanged[g_upChangedN].record = row.record;
            g_upChanged[g_upChangedN++].grip = grip;
        }
        Log("upgrades: One-Handed Grip on: %d two-handed weapon record(s) made one-handed", g_upChangedN);
    }
    else
    {
        int back = 0;
        for (int i = 0; i < g_upChangedN; i++)
        {
            uintptr_t at = table + 11008u + (uintptr_t)g_upChanged[i].record * 232u;
            uint32_t flags = KB_UP_TWO_HANDED, grip = g_upChanged[i].grip;
            back += !hf->write(at + 0xC4, &flags, 4) && !hf->write(at + 0xC0, &grip, 4);
        }
        Log("upgrades: One-Handed Grip off: %d weapon record(s) back to two-handed", back);
        g_upChangedN = 0;
    }
    g_upApplied = on;
}

/* the screen's owner, every game update: purchases, the balance, and the picked kit's grip */
KB_VAR double g_upClock, g_upPolled;
KB_FN void UpTick(void)
{
    if (!g_upLoaded) UpLoad();
    int want = atomic_exchange(&g_upBuyWant, -1);
    if (want >= 0) UpBuy(want);
    g_upClock += 1.0 / 60;
    if (g_upClock - g_upPolled < 1) return;
    g_upPolled = g_upClock;
#ifdef HF_SPEND_SPENT
    int64_t held;
    if (UpCanSpend() && hf->currency_balance(self, KB_UP_CURRENCY, &held) == 0) atomic_store(&g_upBalance, held);
#endif
    KbRoster* r = kb_roster;
    char active[32] = "";
    if (r && r->magic == KB_ROSTER_MAGIC && RosterLock())
    {
        snprintf(active, sizeof active, "%s", r->active[0] ? r->active : r->count ? r->id[0] : "");
        RosterUnlock();
    }
    int on = active[0] && UpOwns(active);
    if (on != g_upApplied) UpGrip(on);
}

/* ---- the tab (render thread) ------------------------------------------------------------------------------------ */
KB_FN void UpDraw(const HfDraw* d, const HfInput* in, float a, float x0, float y0, float x1, float y1, int view, const char* tip)
{
    uint32_t A = (uint32_t)(a * 255);
    const HfColour yellow = HF_RGBA(255, 232, 0, A), white = HF_RGBA(240, 240, 240, A), grey = HF_RGBA(150, 154, 158, A), ink = HF_RGBA(14, 16, 18, A);
    int click = (in->pressed & HF_MOUSE_LEFT) != 0;
    const char* id = view >= 0 ? g_opKits.kit[view].id : "";
    char title[48] = "UPGRADES", balance[48];
    for (int i = 0; i < g_opKits.n; i++)
        if (!strncmp(g_opKits.id[i], id, 31)) { snprintf(title, sizeof title, "%s", g_opKits.name[i]); for (char* c = title; *c; c++) if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32); }
    d->rect(x0, y0, x1, y1, HF_RGBA(14, 16, 18, (uint32_t)(238 * a)), yellow, 2);
    d->rect(x0, y0, x1, y0 + 64, yellow, 0, 0);
    d->text(x0 + 28, y0 + 32, title, 34, HF_FACE_TITLE, HF_ALIGN_LEFT, ink);
    long long held = atomic_load(&g_upBalance);
    if (held >= 0) snprintf(balance, sizeof balance, KB_UP_CURRENCY_NAME "  %lld", held);
    else snprintf(balance, sizeof balance, KB_UP_CURRENCY_NAME "  -");
    d->text(x1 - 28, y0 + 32, balance, 22, HF_FACE_TITLE, HF_ALIGN_RIGHT, ink);
    if (!UpCanSpend())
    {
        d->text(x0 + 28, y0 + 120, "Upgrades need HellForge's spend_currency (mod SDK 21).", 22, HF_FACE_BODY, HF_ALIGN_LEFT, white);
        return;
    }
    /* the one upgrade, as a card */
    const float cx0 = x0 + 28, cy0 = y0 + 92, cx1 = x1 - 28, cy1 = cy0 + 150, bx1 = cx1 - 22, bx0 = bx1 - 220, by0 = cy1 - 66, by1 = cy1 - 22;
    int owned = id[0] && UpOwns(id), busy = atomic_load(&g_upBusy), afford = held < 0 || held >= KB_UP_PRICE;
    d->rect(cx0, cy0, cx1, cy1, HF_RGBA(24, 26, 28, A), owned ? yellow : HF_RGBA(90, 94, 98, A), 2);
    d->text(cx0 + 22, cy0 + 32, "ONE-HANDED GRIP", 26, HF_FACE_TITLE, HF_ALIGN_LEFT, owned ? yellow : white);
    d->text(cx0 + 22, cy0 + 68, "Every gun is held one-handed, in the one-handed SMGs' grip,", 20, HF_FACE_BODY, HF_ALIGN_LEFT, white);
    d->text(cx0 + 22, cy0 + 94, "while this character is the picked one.", 20, HF_FACE_BODY, HF_ALIGN_LEFT, white);
    char price[48];
    snprintf(price, sizeof price, "%d " KB_UP_CURRENCY_NAME, KB_UP_PRICE);
    int over = OpIn(in, bx0, by0, bx1, by1);
    const char* label = owned ? "OWNED" : busy ? "BUYING..." : price;
    d->rect(bx0, by0, bx1, by1, owned ? HF_RGBA(40, 43, 46, A) : afford && !busy ? (over ? yellow : HF_RGBA(255, 232, 0, (uint32_t)(200 * a))) : HF_RGBA(60, 63, 66, A), 0, 0);
    d->text((bx0 + bx1) / 2, (by0 + by1) / 2, label, 20, HF_FACE_TITLE, HF_ALIGN_CENTRE, owned || !afford ? grey : ink);
    if (over && click && !owned && !busy && afford && view >= 0) atomic_store(&g_upBuyWant, view);
    if (g_upStatus[0]) d->text(cx0, cy1 + 34, g_upStatus, 20, HF_FACE_BODY, HF_ALIGN_LEFT, yellow);
    const char* help = tip ? tip : over && !owned ? "Takes the Forge Credits from your profile at once; the grip applies while this character is picked." : NULL;
    if (help) d->text(x0 + 28, y1 - 46, help, 19, HF_FACE_BODY, HF_ALIGN_LEFT, white);
    d->text(x0 + 28, y1 - 18, "Bought upgrades: HellForgeMods\\upgrades.ini  |  Esc: close", 17, HF_FACE_BODY, HF_ALIGN_LEFT, grey);
}

/* the tabs at the left of the frame; the one picked in g_upTab */
KB_FN void UpTabs(const HfDraw* d, const HfInput* in, float a, float x0, float y0, float x1)
{
    uint32_t A = (uint32_t)(a * 255);
    const HfColour yellow = HF_RGBA(255, 232, 0, A), white = HF_RGBA(240, 240, 240, A), ink = HF_RGBA(14, 16, 18, A);
    static const char* const names[] = {"OPTIONS", "UPGRADES"};
    for (int t = 0; t < 2; t++)
    {
        float ty0 = y0 + t * 62, ty1 = ty0 + 54;
        int on = g_upTab == t, over = OpIn(in, x0, ty0, x1, ty1);
        d->rect(x0, ty0, x1, ty1, on ? yellow : HF_RGBA(14, 16, 18, (uint32_t)(238 * a)), on ? 0 : over ? yellow : HF_RGBA(90, 94, 98, A), 2);
        d->text((x0 + x1) / 2, (ty0 + ty1) / 2, names[t], 21, HF_FACE_TITLE, HF_ALIGN_CENTRE, on ? ink : white);
        if (over && (in->pressed & HF_MOUSE_LEFT)) g_upTab = t;
    }
}
#endif
