/* kitbase layer 1: her skill keys. The .ini's keys (KbKeyDown, exactly the modifiers named), the game's own binding
 * rows (Mod Bindings Menu: the HUD passes each press to the DLL through <kit>_input), and one edge per key (KbPressed:
 * a press of both within 0.12 s counts once).
 *
 * Code shared by the character kits. Source of truth: kitbase/ at the repo root. The copy in a kit's src/kitbase/ is
 * written by tools/kitbase_sync.py, so edit kitbase/, never the copy. See docs/kit-base.md.
 * Lifted 2026-11-26: the <kit>_input and <kit>_key exports every kit had word for word (13 kits), the modifier test
 * each kit's frame repeated, and W 0.6.0's edge (0.3 s ate W's fast presses: 0.12 s).
 *
 * KB_INPUT_EXPORTS(kit, n) once (n skills the binding rows know; s_keys from kb_settings.h); each frame, after
 * `int presses = KbBindPresses();`, KbPressed(i, KbKeyDown(&s_keys[i]), presses) is 1 on the frame skill i was pressed. */
#ifndef KB_INPUT_H
#define KB_INPUT_H

#include "kb_core.h"

KB_VAR _Atomic unsigned long long g_bindMs;
KB_VAR atomic_int g_bindPress;
KB_VAR float kb_press_gap = 0.12f;
#define KB_INPUT_EXPORTS(kit, n)                                                       \
    HF_MOD_EXPORT int kit##_input(unsigned version, int skill)                         \
    {                                                                                  \
        if (version != 1) return 0;                                                    \
        atomic_store(&g_bindMs, GetTickCount64());                                     \
        if (skill >= 1 && skill <= (n)) atomic_fetch_or(&g_bindPress, 1 << (skill - 1)); \
        return 1;                                                                      \
    }                                                                                  \
    HF_MOD_EXPORT int kit##_key(unsigned version, int skill, int* vk, int* mods)       \
    {                                                                                  \
        if (version != 1 || skill < 1 || skill > (n) || !vk || !mods) return 0;       \
        *vk = s_keys[skill - 1].vk;                                                    \
        *mods = s_keys[skill - 1].mods;                                                \
        return 1;                                                                      \
    }
/* The binding rows' presses since the last frame (bit i: skill i + 1) */
KB_FN int KbBindPresses(void) { return atomic_exchange(&g_bindPress, 0); }
/* The modifiers held now (KM_...) */
KB_FN int KbModsNow(void)
{
    return ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? KM_SHIFT : 0) | ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? KM_CTRL : 0) |
           ((GetAsyncKeyState(VK_MENU) & 0x8000) ? KM_ALT : 0);
}
/* A key held now, with exactly its modifiers (a modifier key alone does not count itself as held with), the game focused */
KB_FN int KbKeyDown(const Key* k)
{
    if (!k || !k->vk || !Focused()) return 0;
    int own = k->vk == VK_SHIFT ? KM_SHIFT : k->vk == VK_CONTROL ? KM_CTRL : k->vk == VK_MENU ? KM_ALT : 0;
    return (GetAsyncKeyState(k->vk) & 0x8000) != 0 && (KbModsNow() & ~own) == k->mods;
}
/* Skill i was pressed this frame: its key went down or its binding row fired, and not within kb_press_gap of the last */
#define KB_KEYS_MAX 8
KB_VAR int g_keyPrev[KB_KEYS_MAX];
KB_VAR unsigned long long g_keyFired[KB_KEYS_MAX];
KB_FN int KbPressed(int i, int down, int presses)
{
    if (i < 0 || i >= KB_KEYS_MAX) return 0;
    int edge = down && !g_keyPrev[i];
    g_keyPrev[i] = down;
    if (!edge && !(presses >> i & 1)) return 0;
    unsigned long long now = GetTickCount64();
    if (now - g_keyFired[i] < (unsigned long long)(kb_press_gap * 1000)) return 0;
    g_keyFired[i] = now;
    return 1;
}

#endif
