/* kitbase layer 2: statuses as the game puts them on anything (game.dll+0x699E40, AddStatus: manager, entity, status,
 * r9, build-up, from, 0). The kit's kb_status_refuse sees each one first and answers 1 to keep it off.
 *
 * Code shared by the character kits. Source of truth: kitbase/ at the repo root. The copy in a kit's src/kitbase/ is
 * written by tools/kitbase_sync.py, so edit kitbase/, never the copy. See docs/kit-base.md.
 * Lifted 2026-11-26 from Alva (EMS immunity, her blasts' statuses sparing allies), Koleda and Vector (fire statuses
 * never landing on her, burning learned from statuses): the hook and its pin were the same in all three, what each
 * refused was the kit's own.
 *
 * KbStatusInstall(what) in the GAME_LOADED handler, after setting kb_status_refuse (what: the log's name for it). The
 * callback runs on whatever thread the game adds the status on: read, do not wait. */
#ifndef KB_STATUS_H
#define KB_STATUS_H

#include "kb_core.h"
#include "kb_player.h"

typedef void (*AddStatusFn)(void*, uint32_t, uint32_t, uint32_t, float, uint32_t, uint64_t);
KB_VAR AddStatusFn original_add;
KB_VAR atomic_int g_statusHooked;
KB_VAR int (*kb_status_refuse)(uint32_t e, uint32_t st, float buildup, uint32_t from);
KB_FN void KbAddStatus(void* m, uint32_t e, uint32_t st, uint32_t r9, float buildup, uint32_t from, uint64_t z)
{
    if (kb_kit_on && kb_status_refuse && kb_status_refuse(e, st, buildup, from)) return;
    original_add(m, e, st, r9, buildup, from, z);
}
KB_FN int KbStatusInstall(const char* what)
{
    if (!hf->code_matches(game + 0x699E40, "40 55 53 57 41 55 41 57 48 8d 6c 24 d0 48 81 ec 30 01 00 00"))
    { Log("%s off: game.dll+0x699E40 is not the reviewed code", what); return 0; }
    if (hf->hook(self, game + 0x699E40, (void*)KbAddStatus, (void**)&original_add)) { Log("%s off: the hook could not be placed", what); return 0; }
    atomic_store(&g_statusHooked, 1);
    return 1;
}

#endif
