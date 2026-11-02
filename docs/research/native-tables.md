# Adding explosions and arcs: the game's settings lists

The HellForge SDK (15) adds projectiles (`add_projectile`), damage types (`add_damage`), stratagems and entities, but not explosions or arcs. A mod that wants an explosion or arc of its own has to extend the game's lists itself. This note covers what **More Plasma Stratagems 1.1.0** by HadePasianax (https://hf-mod.com/Mods/Details/114, MIT, source in its zip) does, and how Leva 0.5.0 reuses it. Build **25480438**; addresses are `game.dll` RVAs.

## The lists

Each settings type has a list of 8-byte pointers in `game.dll`'s data, indexed by type id, each pointing at a row of the loaded settings data whose `+0` is its own id.

| List | RVA | Slots | Row size | Source |
|---|---|---|---|---|
| StatusEffect | `0x37C5C50` | | 152 | HellForge `actions.c` |
| DamageInfo | `0x37C60C0` | 650 | 76 | HellForge `damage.c` (HellForge moves it for `add_damage`) |
| Projectile | `0x37C7670` | 351 | 272 | HellForge `projectiles.c` (moved for `add_projectile`) |
| Stratagem | `0x37CB600` | 150 | 400 | HellForge `stratagems.c` |
| Explosion | `0x37CC920` | 423 | 152 | HD2Runtime `research_event_actions`; More Plasma Stratagems |
| Arc | `0x37C5F90` | 17 | 104 | Leva 0.5.1 (found at run time: the list holding the ARC-3's arc, row 7 with velocity 300 and range 55, at slot 7; the rows are **not** 104 bytes apart in memory) |

Links between them (HD2Runtime's `attack-outputs.md` and `player_weapon_writes.lua`):

- ProjectileInfo `+144` / `+156` → ExplosionType (impact, expiry)
- ExplosionInfo `+4` → DamageInfo (its blast); `+84` → ProjectileType (shrapnel); **`+120` → ArcType (the arc it releases)**
- ArcInfo `+36` → DamageInfo (the arc's damage)
- BeamInfo `+96` → ExplosionType

`game.dll` on disk is packed (WinLicense: `.winlice` / `.boot` sections; `.text` is 0x850800 bytes on disk for 0x210FA93 in memory), so the code can only be scanned in the running game.

## Extending a list (More Plasma Stratagems' method)

1. **Guard:** check the bytes of the list's main reader (`0x13C0DC0`: `4c 8b b4 c1 20 c9 7c 03`) and bound (`0x13C0DB5`: `cmp eax, 423`) before anything is written. If another mod already moved the list, they differ and nothing is done.
2. **Copy:** allocate a block within 2 GB above `game.dll` (indexed loads add a 32-bit displacement to the image base), copy the list's pointers into it and add the new rows' pointers after them.
3. **Find every site:** read each code section in memory and decode every instruction that addresses the list:
   - `[base + index*8 + list]` indexed loads (REX.W `8b`/`8d`/`89`, SIB scale 8)
   - rip-relative loads of a slot
   - fixed slots through an image-base register, including `cmp` / `mov` immediate forms
   
   Any other 4-byte occurrence of a slot's address in code means an unknown form: refuse the whole change.
4. **Raise the bounds:** `cmp eax, 423` / `cmp r32, 423` within 128 bytes before a site become `cmp …, 423 + new rows`.
5. **Write and check:** write each site's new displacement, read it back, and on any failure put back every site written, including the failed one.
6. **New rows:** a copy of a donor row (`+0` set to the new id), keeping its effect and sound references and its auxiliary list (`+40` pointer, `+48` count), with its own damage type at `+4`, radii, and shrapnel cleared.

They also re-set their projectiles' impact explosion at mission start and on each call of their stratagems, in case the game refreshes its rows, and retry the setup once a second on the game thread until the rows are loaded.

**HellForge's `spawn_explosion` cannot fire a new type:** it finds rows in the loaded settings data by id, not through the list. A new explosion has to come from a projectile whose impact explosion is the new type.

**Two mods cannot both extend the same list:** the second one finds the reader already changed and refuses. More Plasma Stratagems and Leva 0.5.0 (with `native_rows = 1`) both extend the explosion list, so whichever loads second loses its extension.

## Leva 0.5.0: the lockdown's own burst

- **Explosion 423:** a copy of the GL-52 De-Escalator's burst (explosion 41) whose blast deals `yui/leva/lockdown_blast_damage` and which releases her own arc (`+120`).
- **Arc:** a copy of the burst's arc under the arc list's next id, dealing `yui/leva/lockdown_arc_damage` (`+36`).
- **Damage:** both damage types are copies of the vanilla rows (read through the damage list as HellForge has moved it: the displacement at its first site, `0x694E4E + 4`) with standard and durable damage scaled by `rs_damage`, armour penetration and statuses kept. They are rewritten when `rs_damage` changes and at each mission start.
- **Firing:** the lockdown fires its own carrier bullet (`yui/leva/lockdown_bullet`, a Liberator copy with zero damage) whose impact explosion is 423. `HF_ON_EXPLOSION` with type 423 marks exactly where each lockdown burst lands.
- **Arc extension is optional:** if the arc list is not found or cannot be extended, explosion 423 keeps the burst's own arc and Leva cuts those arc hits as they land (0.5.1), and the log says why. If the explosion list cannot be extended, the lockdown falls back to the GL-52 burst with its damage cut as it lands (0.4.x behaviour). `native_rows = 0` in `leva.ini` turns it off.

**Tested in game (Leva 0.5.1/0.5.2, 2026-11-01):**
- explosion list: 47 sites and 36 bounds re-pointed; explosion 423 fires from the lockdown's bullet and its blast deals the scaled damage
- arc list: at `0x37C5F90`, 17 slots, 20 sites, **no bound found**; her arc 17 (a copy of the burst's arc 15) fires and deals its own damage type (0 at `rs_damage` 0). The game evidently does not bound-check arc types on this path.
- the GL-52 burst's blast deals damage 366; its arc 15 deals damage 201.

**But her own arc misbehaves (0.5.2 → off in 0.5.3).** With arc 17 live, the lockdown's lightning ran to and from the player, as the G-31 Arc grenade's arcs did. With `native_arc = 0` (arc 15 kept, its hits cut as they land) it stops. So the arc system keeps more per-type data than the pointer list, and arc 17 reads past it. Not found yet.

What the 20 arc-list sites are (code dumped by Leva 0.5.3, decoded with capstone):

| Sites | RVA | What |
|---|---|---|
| 0 | `0x80DC04` | `lea r9, [list]; mov r9, [r9 + type*8]` (type 0: a default row) |
| 1 | `0x11B3C43` | `mov r9, [slot 0]` |
| 2–9 | `0x11D7B48`–`0x129C37E` | **the loader's clears**: `cmp [slot 1], 0`, then `movups [slot 0..15], xmm0` (16-byte stores) and `mov [slot 16], rax`. Only the `mov`/`cmp`/`lea` forms are re-pointed; the `movups` stores are not REX.W `8b/89/8d`, so the method neither re-points nor refuses them (their displacement is rip-relative, so the raw-address check misses them too). After a data reload the game would clear the old list's slots 0–15 and the new list's slot 16. The explosion list may have the same pattern; More Plasma Stratagems carries the same blind spot. |
| 6 | `0x11D7D6E` | the getter: `test ecx, ecx; jne; lea rax, [default]; ret; lea rcx, [list]; mov rax, [rcx + type*8]; ret` |
| 10, 11 | `0x13B46CF`, `0x13B4BEF` | per arc instance: its type at `[instance + slot*4 + 0x15130]`, then the row's `+0x38` |
| 12–14 | `0x13B4F29`–`0x13B55F5` | the arc update loop: 0x100 instances, 0x118 bytes each; also reads globals at `[manager] + 0x799AB58` and `+0x793A600` (**the likeliest place for per-type data**) |
| 15 | `0x13C10C0` | an explosion's arc (`[explosion + 0x78]`) → its row copied whole |
| 16–19 | `0x176A629`, `0x176D1A0` | explosion → arc (`+0x78`) → its damage (`+0x24`) → the damage row through HellForge's moved damage list (`+0x9192000`) |

The list is 17 slots (0–16): the loader clears exactly that. Next step would be dumping the whole update loop (`0x13B4EE0`–`0x13B5700`) to find what else it indexes by arc type.

## Other patterns worth reusing from More Plasma Stratagems

- **Typed field writes:** `hf_set(mod, id, ProjectileSettings, velocity, HF_F32(…))`, `HF_FIELD(Bombardment, scatter)`, the `HfDamageInfo` struct (armour penetration ×4, demolition, stagger, push, status slots) instead of raw offsets.
- **Private entity copies:** `register_entity` with `own_all = 1` and a field list (`HF_FIELD_AT(WeaponMagazine, pattern, i)`), so a sentry or payload can change without touching the vanilla one.
- **Stratagems:** `add_stratagem` with `package` (a custom model's archive), `loads_item` (an item to own), `flags` (HF_TRAIT_…).
- **Projectile visuals:** `HfProjectile.visual_from` takes another projectile's look.
- **Mod options from Lua:** a Lua addon registers with the shared `ModOptionsMenu` (`register_option(id, {type='choice', …})`, `get`, `on_change`); the DLL exports only versioned `get(version)` / `set(version, choice)`, queues the choice with an atomic exchange and applies it in its game-thread tick.
- **Icons without HD2Runtime:** `hellforge-data.json` resources with `"icon": "images/x.png", "prepare": "raw"` are built by HellForge's own data builder.
- **Settings file:** it saves to `%LOCALAPPDATA%\Hade\MorePlasmaStratagems\settings.ini`. In HellForge builds since late October 2026 the game's LOCALAPPDATA is a per-session folder under `logs/sessions/<id>/game/environment`, so that save does not last past the session.

## Explosives Library 0.1.0 (HadePasianax, MIT): one shared explosion registry (2026-11-02)

hf-mod.com/Mods/Details/118. Source: https://github.com/HadePasianax/hd2-explosives-library. This is the fix for
"two mods cannot both extend the same list".

- **One provider:** the library extends the explosion list once, adding **64 private slots from id 423**. Other mods
  register rows through its API instead of patching the list themselves:
  - `explosives_library_get_api(EL_ABI = 1)` returns `ElApi`, with `register_explosion(const ElExplosion*)`,
    `copy_explosion` and `status`.
  - The provider is found by that exported name in any loaded module (`ElFind` in `include/explosives_library.h`),
    not by the DLL's file name.
- **A registration (`ElExplosion`):**
  - `base`: the donor row, below 423. The new row is a copy of it, so the donor's effects, sound, aux list and arc
    come with it.
  - `damage`: a private type from `add_damage`.
  - `name`: a globally namespaced key. Registering the same name with the same spec returns the same id.
  - `package`: optional; loaded first.
  - `flags`: `EL_RADII` (inner, outer, shockwave) and `EL_SHRAPNEL` (shrapnel count).
  - Call it on the game thread, after the library reports `ready`. Rows are immutable.
- **What a row cannot set:** the arc type (+120) and the shrapnel projectile (+84). A row keeps its donor's arc.
  Lenna's private row (41's look with 157's arc) cannot be made through ABI 1: the arc field would have to be added to
  the API, or Lenna would need another donor.
- **Install:**
  - **Waiting:** it waits for `HF_EVENT_PROJECTILES_READY`, then loads package 0xA7B23B55209766AE, then patches.
  - **The patch:** every executable section below RVA 0x2111000 is scanned.
  - **One new find:** an explicit **kick-stage guard**, a `cmp eax,423` at **0x13c551e** that comes *after* its
    table load at 0x13c54df. A scan for bounds before a reader misses it, so Leva's own extension and More Plasma
    Stratagems both left it at 423.
  - **Safety:** any image-relative table reference it does not recognise refuses the install. Writes are read back,
    with full rollback.
- **Firing a registered row:** as before, a carrier projectile whose `impact_explosion` is the registered id.
  HellForge's `spawn_explosion` still finds only the game's own rows.
- **For the GFL2 kits:** Leva's and Lenna's own list extensions (`native_rows`) make them incompatible with each other
  and with the library.
  - **Plan:** register through the library when it is installed, and fall back to their own extension only when it
    is absent.
  - **Leva:** her burst copies 41 (whose own arc is kept), so it fits ABI 1 as it is.
  - **Lenna:** needs the arc field.
