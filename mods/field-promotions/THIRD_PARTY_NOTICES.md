# Notices

The Veterancy System HUD (promotion banners and rank badges) is drawn with **HD2Runtime by SkyeShade**
(https://github.com/SkyeShade/HD2Runtime) and loaded by **Bingus Shared Loader** by CowboyBingus. The banners use
HD2Runtime's public overlay API. The rank badges follow the mission's stratagem list through HD2Runtime's stratagem slot
overlay (its mission HUD list and icon rectangles, behind its stratagem selector check), code and research that are not
part of its public API: an HD2Runtime update may switch the badges off, and the banners stay.

The Maxigun's call-in refund reads and writes the local player's stratagem record (its list at game.dll+0x347CE50, the
entry layout and cooldown end, the game clock) and checks game code at three of the pins found by **HD2Runtime by
SkyeShade**'s research (`domains/slot_cooldown.lua`, `domains/stratagem_slots.lua`,
`scripts/research_stratagem_slot_conversion.py`, `docs/custom-stratagems.md`). Widening the backpack's ammo count
(network field type `deposit_value`) follows HellForge's `machine-gun-10k` example and the HD2Runtime research behind it.

The Rocket Sentry's Last Stand finds the enemies around it by the entity lookup and scene-graph position read taken
from **Helldiver Survival Rework 0.3.2** by enochlophobianumerophobia (https://hf-mod.com/Mods/Details/14), MIT
licensed: its licence is kept in LICENSE-Helldiver-Survival-Rework.txt. Telling enemies by their behaviour component
follows Yui's Faelynn mod, which uses the same research.

No HD2Runtime or Bingus Shared Loader code is included in this mod; both are separate downloads. Apart from the
research above, the game-side mod (the DLL) uses only HellForge's mod SDK.
