# Adding a booster: the BoosterSettings table

Boosters are stored inline: one 56-byte row per booster in `game.dll`'s data, indexed by the booster's enum value. A 22nd booster (value 21) cannot be added by appending to a pointer list, as the stratagem, projectile and damage lists allow. It needs four things: the table copied somewhere bigger; all 117 code sites that address it re-pointed; every bound and sentinel on a booster index moved from 21 to 22 (the value 21 is today both the row count and the "none / not found / rejected" sentinel); and the enum name table extended. The new booster also needs an item in the backend catalogue so that the player owns it, and every player in a session must run the same table. This note maps the code that does each of these things. Build **25480438**; addresses are `game.dll` RVAs. **Everything here is static analysis of a memory dump of `game.dll` (file offset == RVA, capstone). Nothing has been patched or tested in game yet.**

Tags: **PROVEN** = read directly from the code or data in the dump; **static** = inferred from how the code uses it, not checked at run time; **guess** = a plausible reading only.

## The table

| | |
|---|---|
| RVA | `0x32FDA90` (writable data section, flags `0xC0000040`) |
| Rows | 21 (index 0..20) × 56 (`0x38`) bytes, ending at `0x32FDF28` |
| Before it | 56 zero bytes (`0x32FDA58..0x32FDA8F`): the tail of record 6 of a 7 × `0x110` table at `0x32FD320`, not free space |
| Right after it | `0x32FDF28`: a 24-byte easing-curve global `{f32 0.0, f32 0.5, u32 12, pad, fn ptr → 0x1599E00}`, read by `0x1889930` / `0x1889AB4`; `0x32FDF40`: a different 28 × `0x80` table (see "Neighbouring data") |
| Pointers to it | none: no 8-byte pointer to any row anywhere in the image, at any alignment; `.reloc` is stripped (0x10 bytes) |
| Code references | 117 instructions, all in the main code section: 109 rip-relative, 8 image-base + disp32 (`[r8 + idx*56 + 0x32FDA90]`). A whole-`.text` rip/disp32 scan finds no others |
| Hashes | the rows' `+0x00` ids appear nowhere else in `.text`, `.rdata` or `.data`: no code names a booster by its id |

So the table **cannot grow in place** (live data follows it). It has to be copied and re-pointed. The copy must sit within ±2 GB of each rip-relative site, and the 8 disp32 sites in `0x856DD0` need it within a signed 32-bit offset of the image base. In practice, allocate it near `game.dll`, as HellForge does for the lists it moves.

## Row layout

| Off | Type | Meaning | Evidence | Confidence |
|---|---|---|---|---|
| `+0x00` | u32 | booster id32: the item id in the backend catalogue, the key that every hash → index scan compares, the value stored in loadouts (`+0x130`) and player slots (`+0x168`) | every scan (`0x11F81A0`, …); catalogue match for row 3 (see "Ownership") | PROVEN |
| `+0x04` | u32 | stratagem id granted while the booster is in the team, an index into the stratagem list `0x37CB600` (0 = none). Only row 20 (FreeEAT) has one: `0x0F` | `0x856FE0` → `0x66F190` (grant), `0x8570E0` → `0x66F4C0` (revoke), `0x8568A0` (`0x856A2A`, `0x856A64`), `0x856DD0` (appends `{id, [0x37CB600 + id*8] + 0x50}` to the loadout's `+0x188` stratagem list), HUD `0x1896D40` | index into 0x37CB600: PROVEN; "the EAT stratagem": guess |
| `+0x08` | f32 | the booster's tuning value (multiplier, chance, cap…) | 23 fixed-row reads (section "Effects"), `0x856DB0`, tooltip `0x11F86D0` | PROVEN |
| `+0x0C` | u8 (u32 1 in every row) | selectable / enabled flag: rows with 0 are left out of the picker | `cmp byte [row+0xC],0` at `0x11F8545`, `0x18D865F`, `0x11F8626`, `0x1458987` | use PROVEN, meaning static |
| `+0x10` | u32 | text id (name, guess) | `0x191FC00` (details panel), `0x18CCBD9` (slot tooltip), `0x179D66E` / `0x179D008` (label, when widget `+0x690` bit 1 is set), getter `0x11F8220`, reverse lookup `0x11F81D0` | static |
| `+0x14` | u32 | second text id (short/alt name, guess) | `0x179D675` / `0x179D00E` (label, bit clear), `0x178D518`, getter `0x11F8260`, reverse lookup `0x11F81D0` | static |
| `+0x18` | u32 | description text id | `0x11F89F0` (tooltip, with value tokens `0xAE242FC6` / `0xFCDCBA12`), `0x179E170` | static |
| `+0x1C` | u32 | 0 in every row | no reads found | — |
| `+0x20` | u64 | icon resource id (resolved as type `0x3AA8B87E` by `0x1798770`) | `0x1893DDE`, `0x191CA8F`, `0x191DC24`, `0x18CBB6D`, `0x18CB3F1`, `0x179A5D7`, `0x1799D27`, `0x17982CF`, getter `0x11F82A0` | static (high) |
| `+0x28` | u64 | second resource id | only getter `0x11F82E0` (no caller found) | guess |
| `+0x30` | u32 | how the tooltip formats `+0x08`: 0 = no value, 1/2 = formatters `0x143A300` / `0x143A400`, 3 = scaled token, 4 = `"%i"`. Rows 13 = 3, 16 = 4, 19 = 2 | `0x11F8773` switch in `0x11F86D0` | static |
| `+0x34` | u32 | resource hash loaded while the booster is in the team (`0x12689C0` resolve, `0x12FE350` load / `0x12FE4E0` unload). Rows 11, 12, 17, 18 have one | `0x8569E3`, `0x856AFC` | static |

**Agent disagreements, resolved:**
- **`+0x04`.** The readings were "item/amount parameter" (`0x8570E0`), "HUD indicator id" (`0x1896D40`) and "stratagem id". Every reader uses it as an index into the 8-byte list at `0x37CB600`, which is HellForge's Stratagem list (see `native-tables.md`; also indexed by the entity's stratagem id `[r14+0x398]` in `0x6A5B70`). The default loadout builder `0x17522B0` fills the same `+0x188` list with fixed stratagem ids. It is a granted stratagem, and the HUD code shows that stratagem.
- **`0x8570E0`.** One agent called it the grant. It calls `0x66F4C0` (remove), the same callee as the removal path in `0x8568A0`. Its twin `0x856FE0` calls `0x66F190` (add). So `0x856FE0` grants and `0x8570E0` revokes.
- **The brief's field ranges.** The brief lists "`+0x10..+0x1B` 12 bytes" and "`+0x20..+0x2F` 16 bytes". The code reads them as three u32 text ids and two u64 resource ids. `+0x0C` is a u32 in the data but is only ever read as a byte.

## Booster values

Row index == the game's Booster enum value (HellForge `mappings/boosters.json` "value"). The game's own enum names sit in a 22-pointer `const char*` array at **`0x21DD630`** (absolute pointers, dump image base `0x7FF98F480000`). The 23rd qword is an unrelated string (`nui_weapon_support_faf_missile_launcher_helghast_desc`).

| Value | Enum name (`0x21DD630`) | id32 `+0x00` | `+0x08` | Other non-zero | Display name (HellForge mappings, where noted) |
|---|---|---|---|---|---|
| 0 | None | `0xA3BD6B87` (2747100039) | 1 |  |  |
| 1 | Vitality | `0x4233BB28` (1110686504) | 0.9 |  |  |
| 2 | Stamina | `0x28D025B8` (684729784) | 1.3 |  |  |
| 3 | MuscleEnhancement | `0xA57D4ECB` (2776452811) | 0.35 |  | Muscle Enhancement |
| 4 | UAVRecon | `0xF6948661` (4136928865) | 1.5 |  |  |
| 5 | IncreasedReinforcementBudget | `0xF267C3AC` (4066886572) | 1 |  |  |
| 6 | FlexibleReinforcementBudget | `0xFEB5A878` (4273318008) | 0.75 |  |  |
| 7 | HellpodSpaceOptimization | `0x71BCCD81` (1908198785) | 1.5 |  |  |
| 8 | LocalizationConfusion | `0x3445BCA8` (876985512) | 0.9 |  |  |
| 9 | FastExtraction | `0xD0E0FCF2` (3504405746) | 0.7 |  |  |
| 10 | SlowStunResistance | `0xA17C1852` (2709264466) | 0.5 |  |  |
| 11 | CombatDrugs | `0xE92E637E` (3912131454) | 1 | `+0x34` = 0x6667BDAA |  |
| 12 | FieryDrop | `0xB8F17AF8` (3102833400) | 1 | `+0x34` = 0x904F9DED |  |
| 13 | DoubleSampleChance | `0x140A3C29` (336215081) | 0.15 | `+0x30` = 3 |  |
| 14 | DeathMarch | `0x85763547` (2239116615) | 0.05 |  |  |
| 15 | DefensiveAmmoPod | `0xE21B9534` (3793458484) | 0 |  |  |
| 16 | BigEnemiesDropSamples | `0x2F7DE576` (796779894) | 10 | `+0x30` = 4 |  |
| 17 | ShockPods | `0x449FE15A` (1151328602) | 1 | `+0x34` = 0x8E28D6FC |  |
| 18 | SmokePods | `0x5F895979` (1602836857) | 1 | `+0x34` = 0xD4384EBE |  |
| 19 | FireExtinguish | `0xD35B7855` (3545987157) | 0.5 | `+0x30` = 2 | Integrated Extinguishers |
| 20 | FreeEAT | `0x485AAA8A` (1213901450) | 1 | `+0x04` = 0xF | Surplus EAT Allocation |
| 21 | Count | — | — | the count and the sentinel | |

**21 = `Count`.** It is the row count, and the code also uses it as the value for "no booster", "hash not found" and "request rejected". Hash → index scans return 21 when nothing matches, and the setter sends 21 to say "rejected". Several places store 21 rather than filter it (the active list at `+0xA8C`, `0x856BE0` and `0x11DE430` outputs, `0xBF13D0` arguments). A new booster at value 21 therefore collides with the sentinel. **The sentinel has to move to 22 everywhere, together with the bounds.** Otherwise "no booster" would read as "booster 21": for example, every player with an unknown hash would have the new booster "active".

**Row 0 (`None`, id `0xA3BD6B87`) is the empty booster** (PROVEN from use):
- the default loadout (`0x1752497`) and slot reset (`0x135B60E`) write its id;
- `0x857170` accepts only 1..20, and `0x11F8580` / `0x1458480` loop from 1;
- `0x193944B`, `0x19ADF70`, `0x19AE070`, `0x19AE160` and `0x18CCB50` skip it;
- `0x1893D40` draws it dimmed (alpha 0.4), and `0x875110` always accepts it.

New rows go at the end.

## Accessor functions

These are useful on their own. Signatures are guesses from register use. "idx" = booster value.

| RVA | Signature (guess) | What it does |
|---|---|---|
| `0x856D10` | `bool (?, u32 idx)` | idx is in list `[0x3326938]` (8-byte entries, count `+0x10`) |
| `0x856D40` | `bool is_active(?, u32 idx)` | false if `[[0x33266A0]+8] > 0 && [[0x33266A0]+0x40] == 4` (a mode with boosters off, guess); else idx in `[0x3326938]` or in the active list `[0x33264F8] + 0xA8C + i*0x40` (count `+0x8E8`). Takes an **index**, not a hash; leaves xmm0 alone (callers load the value first). PROVEN |
| `0x856DB0` | `float (?, u32 idx)` | `table[idx].+0x08`, **no bounds check**; its lea is `table + 8` |
| `0x875110` | `bool validate(mgr, player, u32 idx)` | 0 → true; idx ≥ 21 → false; false if another session player's slot already holds idx (no duplicate boosters) |
| `0x875270` | `bool is_free(?, ?, u32 idx)` | 0 → true; false if idx is in `[0x33264F8] + 0x97C + i*0x44` (count `+0x8E8`) or `[0x3326938]`, or if a player record (`[0x347CE50]`, `+0x168`) already has its id |
| `0x8753B0` | `void (?, u32 out[4])` | booster index of up to 4 players: stored index, else hash scan of slot record `+0x168` (21 if unknown) |
| `0x8755D0` | `player (?, u32 idx)` | the player who brought idx (0 → none) |
| `0x875800` | `void request_set(?, ?, u32 idx)` | the local player's booster request (player from `[0x347CEF0] + 0xB398`; edx is not used). See "Selection" |
| `0x8758D0` | `void set(mgr, player, u32 idx)` | authority set / local apply |
| `0x11F8170`, `0x11F8390` | `row* (u32 hash)` | row by id, or null |
| `0x11F81A0` | `u32 (u32 hash)` | index by id, **21 if not found** |
| `0x11F81D0` | `u32 (u32 text_id)` | index whose `+0x10` or `+0x14` matches, 21 if not found (lea is `table + 0x14`) |
| `0x11F8200` | `void (u32* out, u32 idx)` | id of idx, **no bounds check** |
| `0x11F8220` / `0x11F8260` | `u32* (u32* out, u32 hash)` | `+0x10` / `+0x14` by id (0 if none) |
| `0x11F82A0` / `0x11F82E0` | `u64* (u64* out, u32 hash)` | `+0x20` / `+0x28` by id |
| `0x11F83C0` | `row* (u32 idx)` | row by index, null if ≥ 21 |
| `0x11F83E0` | `void (u32* out, u32 idx)` | id of idx, 0 if ≥ 21 |
| `0x11F8410` | `bool (u32 hash)` | id is a booster |
| `0x11F8490` | `bool (u32 idx)` | idx selectable: in `[0x3326938]`, or owned (profile list) + `0x18DD110(hash)` + `+0x0C` |
| `0x11F86D0` | `void (u32 idx, out)` | builds the tooltip text from `+0x08`, `+0x30`, `+0x18` |
| `0x135C100` | `bool (?, u32 idx)` | some player in a given state has this booster's id at `+0x168` |
| `0x136FDB0` | `bool owned(profile, u32 hash)` | profile = `[0x347CEF8]`; requires `[+0xDDCF8] > 7`, finds the id in `+0xB9CE4` (0x18 stride, id at `+4`), state at `+0x1CF8 + i*0xB8` must be 2 or 4. PROVEN |
| `0x1752610` | pack | 0x28-byte loadout record: idx (arg 6) → id at `+0x10` (0 if ≥ 21) |
| `0x1752710` | unpack | record `+0x10` id → idx (21 if not found) |
| `0x11F8140`, `0x11F8320`, `0x11F8360`, `0x11F8450` | | the **alias table** `0x21CAFB0`: 12 × 12 bytes `{item id, text id, booster index 1..4}`. These map 12 other item ids onto rows 1–4 (entries 9–11 carry the row's own `+0x10`). Also read at `0x136E709`, `0x136EAE9`, `0x178D32F`, `0x1799B29`, `0x179A55C` (loop bound `cmp …, 0xC`). Only relevant if a mod adds aliases |

## Effects found in code

There are 23 fixed-row `+0x08` reads. Each one except `0x9D40A0` (a bare, unguarded getter) sits behind `mov edx, N; call 0x856D40; je skip` (static):

| Row | Booster | Where (root) and what the code does with `+0x08` |
|---|---|---|
| 1 | Vitality | `0x923A9E` (`0x9235F0`): an integer count `r12 = int(r12 * 0.9)`. What it counts is not visible (static). |
| 2 | Stamina | `0x82A2B9` (`0x829E90`): the float at player `+0x1050` × 1.3, passed to `0x11DE2A0`. `0x833A0E` (`0x833930`): after passing through `0x11DE2A0`, the divisor of a per-entity meter's depletion (`+0x547854`, stride 0x78) becomes 1.3, so it drains 1.3× slower (static; "stamina" guess from the name). |
| 3 | MuscleEnhancement | `0x692E57` (`0x692240`): `x *= 0.35`, then `[rsi+0x14] = 1 - x` (x from `[rsi+0x18]`) (static). |
| 4 | UAVRecon | `0x18AE078` (`0x18AE050`): radar/scan scale 1.5 instead of 1.0, then ship upgrade `0x1778E262` applied, × 3.5 (static; "radar radius" guess). `0x18AD3A0` (`0x18AD310`): 1.5 × 0.025, a per-player parameter (`r15 = 4` is both the booster index and a 4-player loop counter). |
| 5 | IncreasedReinforcementBudget | `0x8F31B9` (`0x8F3020`), `0x8F3E30` (`0x8F3DA0`), `0x8F3E6C` (`0x8F3E50`), `0xB49A96` (`0xB48C20` case 0x30): `[obj+0x58] = int(1.0)` = +1 (reinforcement budget, static). |
| 6 | FlexibleReinforcementBudget | `0x8F36CB` (`0x8F3530`), `0x8F3D0B` (`0x8F3C20`): a refill time `[rax+0x1C]` × 0.75, then stored as a deadline (event `0x5B6AC15B`) (static). |
| 8 | LocalizationConfusion | `0x9594BC` (`0x959350`): a value from object `+0xEC/+0xF0/+0x118` × 0.9, compared against a threshold. `0x9FEC31` (`0x9FE9F0`), `0x9FEE37` (`0x9FEDE0`): a level-lerped value ÷ 0.9 (static, meaning unknown). |
| 9 | FastExtraction | `0x6A5C8F` (`0x6A5B70`): the timer of stratagem id `0x94` × 0.7 (static; that 0x94 is the extraction is a guess). |
| 10 | SlowStunResistance | `0x69A505` (`0x699E40`): a status-effect build-up amount × 0.5, also gated on `0x8D5100` (static). |
| 13 | DoubleSampleChance | `0x9D1AAC` (`0x9D16E0`): LCG roll (`0x3483C38`) succeeds if 0.15 > roll, a 15 % proc (static). |
| 14 | DeathMarch | `0xA8B568` (`0xA8AE60`): 0.05 used as a threshold against a float from `0x922750`; adds/checks/removes status `0x3B` (`0x699E40` / `0x69B1F0` / `0x69AEC0`) (static, medium). |
| 16 | BigEnemiesDropSamples | `0x9D4072` (`0x9D4060`), `0x9D40A0` (leaf at `0x9D40A0`), `0x9D4202` (`0x9D40B0`): `int(10.0)` as a cap on the counter `[[0x3326B80]+0x84C]` for a drop roll (static). |
| 19 | FireExtinguish | `0x691854` (`0x690FB0`): `xmm14 *= 1 / (1 - 0.5)` = × 2 (static, what it scales unknown). |
| 20 | FreeEAT | no `+0x08` read; its effect is the `+0x04` stratagem grant (`0x0F`) |
| 0, 7, 11, 12, 15, 17, 18 | | no fixed-row read. 11, 12, 17, 18 carry a `+0x34` resource; 7, 11, 12, 17, 18 have `is_active` calls (below) |

**`is_active` calls with no table read** (an E8-call scan of `.text` made for this note; 38 calls to `0x856D40`):
- **Index immediates** for rows 7 (`0x762130`, `0x76DB97`, `0x76DE38`, `0x76EEE3`, `0x76F197`, `0x7799C2`, `0x779C3E`, `0x77BAAA`, `0x9A7762`, `0x9B06E6`), 11 (`0x11C9A16`), 12 (`0x92C0E6`), 17 (`0x92C168`) and 18 (`0x92C1EC`).
- **Data-driven:** two calls take the index from data. `0x695A2A` uses `[r15+0x70]`; `0x8785FA` uses `[r11-0x10]`, from a 0x18-byte-entry list at `[r15+0x118]`. **So some settings data stores booster indices.**
- **Not covered:** rows 0, 15 (DefensiveAmmoPod) and 20 (FreeEAT) have no immediate call. Copies of `0x856D40` that the compiler inlined are not counted.

None of these need patching for a move. A new booster's effect would be new code calling `0x856D40` with 21.

## Selection and network path

1. **Picker list:** `0x18D8540` loops over all rows and adds each selectable index. The confirm handler `0x146E0A0` (menu page `[+0x2818] == 5`) reads the cursor `[+0x1658AC]` and clamps it to ≤ 21. It then calls `0x875800(idx)`, sends message `0x5D509124` with the row's **id**, and refreshes the HUD with `0x1893D40`.
2. **`0x875800`:** the local player comes from `[0x347CEF0] + 0xB398`, and the authority (guess) from `+0xB3A8`.
   - **Local player is the authority:** it validates with `0x875110` and tail-jumps to `0x8758D0` with idx, or 21 on reject.
   - **Otherwise:** it sends **`0x71C061C5` with the raw index** (u32, 4 bytes) to the authority. On a local reject it calls `0x8758D0` with idx 21 and player NULL (`0x8758A0 xor edx, edx; lea r8d, [rdx+0x15]`), which path (b) handles harmlessly.
3. **`0x8758D0(mgr = [0x33264F8], player, idx)`, as read from the code (not traced at run time):**
   - **(a) The authority handles another player's request:** it validates, stores idx at the player's slot `mgr + 0xA8C + slot*0x40`, and calls `0xB64BF0`. It then replies **`0x71C061C5`** with idx, or 21 if rejected.
   - **(b) Local apply:**
     - If idx is 21 (`0x875AF9 cmp ebp, 0x15`), nothing is stored: a reject keeps the old index. In state `[[0x347CE28]+0x4294] == 0xE` it also restores the HUD icon from the old index's row; otherwise it just returns.
     - Any other value is stored at the slot **with no bounds check** (`0x875B8F mov [r8], ebp`) and fires event `0x3B12BD49`.
   - `0x87B954` is a second, replicated write of the raw index into slot `+0xA8C` (event `0x3B12BD49`); `0x1465A3E` writes 0 into the local slot.
   - `0xB727D0` is an inlined copy that runs over every manager instance (component type `0x18` in `[0x3326E68]`).
4. **Storage:**
   - Slots: `[0x33264F8] + 0xA7C + slot*0x40` is the per-player record (`+0x10` = booster index, so `+0xA8C`). Player id → slot goes through a hash map at `+0x930` (capacity `+0x938`, empty key `+0x93C`, multiplier `+0x940`).
   - `0x146FC40` writes the index (21 if the loadout's id is unknown) from each player's loadout (`+0x130` id) into the slot. When the stored index and the loadout's id disagree, it calls `0x875800`.
5. **Active list:**
   - `0x856D40` reads the slot list and `[0x3326938]`.
   - `0x8568A0` keeps a cached 4-index array at `[obj+0x14]`. When the team's set changes, it loads/unloads `+0x34` and grants/revokes `+0x04`.
   - `0x856DD0` appends the team's `+0x04` stratagems to a loadout.
6. **Loadout serialisation:**
   - `0xB9E2D0`, `0x1381220` and `0x1381710` pass the **index** (21 = none) to `0xBF13D0` (packed loadout; guess: replication).
   - `0x135EA10`, `0x1360970` and `0x13620A0` pass it to `0xBEE770` (report/summary, guess).
   - The 0x28-byte record built by `0x1752610` / `0x1752710` stores the **id**.

**Why a mismatched peer breaks:**
- **The wire carries the raw index.** `0x71C061C5` (and probably `0xBF13D0`) send the row index, not the id.
- **An unmodded peer:**
  - reads index 21 as "rejected / none";
  - rejects it in its own `0x875110` (`cmp r8d, 0x15; jae`);
  - cannot resolve the new id (its scans return 21, its "none").
- **An unmodded peer receiving index 21 from a modded one** (or anything ≥ 21) stores it unchecked (`0x8758D0` path (b), the same in `0xB727D0`), then reads past its own 21-row table at `0x875B1D`, `0xB72A90` and `0x17981F0`: row "21" there is the easing-curve global. This is the dangerous direction.
- **A modded peer** reads an unmodded peer's 21 as a real booster (the new one) unless the session's table size is known.
- **The result:** all players need the same table and the same sentinel. A mod must refuse mixed sessions; the unmodded side has no check to fall back on.

## Patch list

### a. Table-address sites (117 in 85 root functions)

The list is from `tools/dump-analysis/chain_sweep.py` (`sweep.json`): every instruction whose rip-relative or disp32 operand lands in `[0x32FDA90, 0x32FDF28)`. Roots were found by following chained unwind info. Two "roots" are pdata-less gaps: `0x856D0B` (holds `0x856DB0`) and `0x11F8058` (holds the 12 accessors), so 96 functions in all. Field reads through a register (`[base + idx*56 + F]`, `[rowptr + F]`) are not listed: they follow the patched `lea`, as long as the row size stays 56 and the fields stay where they are.

- **Re-point to the copy at the same offset.**
  - Base: 80 `lea reg, [table]`.
  - Biased leas (re-point to `new + bias`):
    - `0x856DB6` → `+0x08` (the `+0x08` reader);
    - `0x11F81D2` → `+0x14` (reads `[rax-4]` and `[rax]` per row);
    - `0x11F8599` → `+0x38` (row 1, loop start in `0x11F8580`);
    - `0x14588F7` → `+0x38` (row 1, loop start in `0x1458480`).
  - Disp32 (`0x856DD0`): 4 `lea [r8 + 0x32FDA90]` and 4 `mov [rdx + r8 + 0x32FDA94]` (`+0x04`), with `r8` = image base.
- **Fixed-row reads (25).**
  - These are 23 `[row N + 0x08]` (rows 1–6, 8–10, 13, 14, 16, 19) and 2 `[row 0 + 0x00]` (`0x135B60E`, `0x1752497`).
  - They read one booster's own data, so they keep working only if re-pointed **to the same row in the copy**.
  - If the old table is left intact and the copy keeps the old rows' values, they also work un-patched. They then read the old table, so a mod's edits to those rows in the copy would not take effect.
  - Re-pointing them all is the consistent choice.

| Root | Role | Sites (field / row) |
|---|---|---|
| `0x690FB0` | FireExtinguish (19) effect | `0x691854` **row 19** +0x08 |
| `0x692240` | MuscleEnhancement (3) effect | `0x692E57` **row 3** +0x08 |
| `0x699E40` | SlowStunResistance (10) effect | `0x69A505` **row 10** +0x08 |
| `0x6A5B70` | FastExtraction (9) effect | `0x6A5C8F` **row 9** +0x08 |
| `0x829E90` | Stamina (2) effect, player +0x1050 | `0x82A2B9` **row 2** +0x08 |
| `0x833930` | Stamina (2) effect, meter depletion | `0x833A0E` **row 2** +0x08 |
| `0x8568A0` | team booster set changed: +0x34 load/unload, +0x04 grant/revoke | `0x8568E7` base, `0x8569B0` base, `0x856AB7` base |
| `0x856BE0` | (index, value) pairs from item query 0x12DF110 | `0x856C8A` base |
| `0x856D0B` | gap holding 0x856DB0, the +0x08 reader (lea is base+0x08) | `0x856DB6` base+0x08 |
| `0x856DD0` | append team boosters' +0x04 stratagems to loadout +0x188 (image-base + disp32 forms) | `0x856E11` base (disp32), `0x856E2F` idx +0x04 (disp32), `0x856E81` base (disp32), `0x856E9F` idx +0x04 (disp32), `0x856EF1` base (disp32), `0x856F0F` idx +0x04 (disp32), `0x856F61` base (disp32), `0x856F7F` idx +0x04 (disp32) |
| `0x856FE0` | grant the +0x04 stratagem of index edx | `0x85701B` base |
| `0x8570E0` | revoke the +0x04 stratagem of index edx | `0x857104` base |
| `0x857170` | player's booster hash from manager slot | `0x8572C5` base |
| `0x875270` | is booster index free to pick | `0x8752F7` base |
| `0x8753B0` | up to 4 players' booster indices | `0x875571` base |
| `0x8755D0` | player who brought booster N | `0x8756F7` base |
| `0x8758D0` | set player booster (authority / apply) | `0x875B16` base |
| `0x8F3020` | IncreasedReinforcementBudget (5) effect | `0x8F31B9` **row 5** +0x08 |
| `0x8F3530` | FlexibleReinforcementBudget (6) effect | `0x8F36CB` **row 6** +0x08 |
| `0x8F3C20` | FlexibleReinforcementBudget (6) effect | `0x8F3D0B` **row 6** +0x08 |
| `0x8F3DA0` | IncreasedReinforcementBudget (5) effect | `0x8F3E30` **row 5** +0x08 |
| `0x8F3E50` | IncreasedReinforcementBudget (5) effect | `0x8F3E6C` **row 5** +0x08 |
| `0x9235F0` | Vitality (1) effect | `0x923A9E` **row 1** +0x08 |
| `0x959350` | LocalizationConfusion (8) effect | `0x9594BC` **row 8** +0x08 |
| `0x9D16E0` | DoubleSampleChance (13) effect | `0x9D1AAC` **row 13** +0x08 |
| `0x9D4060` | BigEnemiesDropSamples (16) cap | `0x9D4072` **row 16** +0x08 |
| `0x9D4098` | gap holding 0x9D40A0, BigEnemiesDropSamples (16) getter | `0x9D40A0` **row 16** +0x08 |
| `0x9D40B0` | BigEnemiesDropSamples (16) drop roll | `0x9D4202` **row 16** +0x08 |
| `0x9FE9F0` | LocalizationConfusion (8) effect | `0x9FEC31` **row 8** +0x08 |
| `0x9FEDE0` | LocalizationConfusion (8) effect | `0x9FEE37` **row 8** +0x08 |
| `0xA8AE60` | DeathMarch (14) effect | `0xA8B568` **row 14** +0x08 |
| `0xB48C20` | IncreasedReinforcementBudget (5), component switch case 0x30 | `0xB49A96` **row 5** +0x08 |
| `0xB727D0` | set booster on every manager instance (0x8758D0 inlined) | `0xB72AE3` base |
| `0xB9E2D0` | loadout hash -> index -> 0xBF13D0 | `0xB9E31E` base |
| `0x11DE430` | (index, value) list from item query | `0x11DE4EA` base |
| `0x11F8058` | gap: the accessor set (section 5) | `0x11F8170` base, `0x11F81A2` base, `0x11F81D2` base+0x14, `0x11F8206` base, `0x11F8220` base, `0x11F8260` base, `0x11F82A0` base, `0x11F82E0` base, `0x11F8390` base, `0x11F83CA` base, `0x11F83EB` base, `0x11F8413` base |
| `0x11F8490` | is booster index available | `0x11F84C8` base |
| `0x11F8580` | every booster 1..20 owned/seen check | `0x11F8599` base+0x38, `0x11F85A0` base |
| `0x11F86D0` | tooltip text from +0x08/+0x18/+0x30 | `0x11F870C` base |
| `0x135B2C0` | slot cleanup, writes row 0 hash | `0x135B60E` **row 0** +0x00 |
| `0x135C070` | team booster indices (hash scan) | `0x135C097` base |
| `0x135C100` | is booster N equipped by a player | `0x135C118` base |
| `0x135C1E0` | slot record that carries booster N | `0x135C1F8` base |
| `0x135EA10` | per-player loadout summary -> 0xBEE770 | `0x135F0A6` base |
| `0x1360970` | loadout summary -> 0xBEE770 | `0x1360FED` base |
| `0x13620A0` | loadout -> packed form (0xBEE770, 0x13632C0) | `0x13626D3` base |
| `0x136E6A0` | validate inventory entries, type 5 | `0x136E6FB` base, `0x136E754` base, `0x136E830` base, `0x136EAA9` base |
| `0x1381220` | per-player loadout -> 0xBF13D0 | `0x1381285` base |
| `0x1381710` | loadout report -> 0xBF13D0 | `0x1381798` base |
| `0x1458480` | every booster 1..20 satisfied check | `0x14588F7` base+0x38, `0x145897B` base |
| `0x1468320` | HUD/menu update, booster page | `0x1468891` base |
| `0x146A460` | player's booster index for the HUD | `0x146A513` base |
| `0x146E0A0` | menu confirm: booster picked | `0x146E6FF` base |
| `0x146FC40` | sync loadouts into manager +0xA8C | `0x146FD03` base, `0x14701B4` base, `0x1470272` base, `0x1470577` base |
| `0x14E5D80` | tooltip, name from 0x21DD630 | `0x14E71EF` base |
| `0x14FD800` | item card, name from 0x21DD630 | `0x14FDB3D` base |
| `0x1750870` | validate/repair local loadout | `0x1750C42` base |
| `0x17522B0` | default loadout (row 0 hash) | `0x1752497` **row 0** +0x00 |
| `0x1752610` | pack loadout record (index -> hash) | `0x1752675` base |
| `0x1752710` | unpack loadout record (hash -> index) | `0x1752757` base |
| `0x178D310` | item hash -> +0x14 id | `0x178D48C` base |
| `0x17981F0` | icon strip widget, +0x20 (index unchecked) | `0x17982A4` base, `0x179832D` base, `0x17985A9` base |
| `0x1799750` | item icon, +0x20 (alias path) | `0x1799B6A` base, `0x1799CFD` base |
| `0x179A540` | icon via alias table 0x21CAFB0, +0x20 | `0x179A5A6` base |
| `0x179AA40` | icon widget, +0x20 | `0x179AA53` base |
| `0x179C920` | item label, +0x10/+0x14 | `0x179CC6D` base |
| `0x179D620` | item name label, +0x10/+0x14 | `0x179D62C` base |
| `0x179DF40` | item description id, +0x18 | `0x179E06E` base |
| `0x1893D40` | HUD block icon by hash, +0x20 | `0x1893D62` base |
| `0x1896D40` | HUD indicators for granted stratagems, +0x04 | `0x1896D9D` base, `0x1896E52` base |
| `0x18AD310` | UAVRecon (4) effect | `0x18AD3A0` **row 4** +0x08 |
| `0x18AE050` | UAVRecon (4) effect | `0x18AE078` **row 4** +0x08 |
| `0x18CAFB0` | item tile, booster case, +0x20 | `0x18CB323` base |
| `0x18CBA50` | slot widget: set index, +0x20 | `0x18CBA99` base |
| `0x18CBC40` | icon widget by hash, +0x20 | `0x18CBC4C` base |
| `0x18CC140` | "new" badge | `0x18CC1DF` base |
| `0x18CCB50` | slot tooltip, +0x10 | `0x18CCB8C` base |
| `0x18D8540` | build the selectable-booster list | `0x18D85AF` base |
| `0x191CA50` | widget icon then details | `0x191CA6D` base |
| `0x191D040` | icon + info panel (switch case) | `0x191DBFE` base |
| `0x191FBC0` | details panel, +0x10 | `0x191FBD8` base |
| `0x1939130` | loadout panel refresh | `0x19394BB` base |
| `0x19ADF70` | description refresh | `0x19ADF98` base |
| `0x19AE070` | description refresh | `0x19AE098` base |
| `0x19AE160` | description refresh | `0x19AE16D` base |

### b. Booster bounds and sentinels

Every `0x15` (and `0x14` / `0x16`) instruction in `chain_sweep.py`'s constants (115) was disassembled in context, plus four found outside it: `0x87513B`, `0x87583F`, `0x8758A2` (in `0x875110` / `0x875800`, which never address the table) and `0x8572BC`, a `0x13` bound. Of the 119, **112 are booster bounds or sentinels: 31 guards, 51 loop ends, 29 sentinels, 1 clamp. 7 are NOT booster.**

- **Guard:** `cmp idx, 0x15; jae/ja skip` (or `cmovb`) before a row read. Change 0x15 → 0x16.
- **Loop end:** `cmp i, 0x15; jb loop`, over all rows. Change → 0x16.
- **Sentinel:** the value 21 written as "none / not found / rejected" (`mov reg, 0x15`, one `lea r8d, [rdx+0x15]`), or tested (`cmp reg, 0x15; jne`). Change → 0x16.
- **Clamp:** `mov ebp, 0x15; cmp eax, ebp; cmovb ebp, eax`. Change → 0x16.

**Guards (31)**

| RVA | Instruction | Root | Guards |
|---|---|---|---|
| `0x856A20` | `cmp ebx, 0x15` | `0x8568A0` | `+0x04` grant (add path) |
| `0x856B34` | `cmp ebx, 0x15` | `0x8568A0` | `+0x04` revoke (remove path) |
| `0x856E08` | `cmp eax, 0x15` | `0x856DD0` | append team boosters' +0x04 stratagems to loadout +0x188 (image-base + disp32 forms) |
| `0x856E78` | `cmp eax, 0x15` | `0x856DD0` | append team boosters' +0x04 stratagems to loadout +0x188 (image-base + disp32 forms) |
| `0x856EE8` | `cmp eax, 0x15` | `0x856DD0` | append team boosters' +0x04 stratagems to loadout +0x188 (image-base + disp32 forms) |
| `0x856F58` | `cmp eax, 0x15` | `0x856DD0` | append team boosters' +0x04 stratagems to loadout +0x188 (image-base + disp32 forms) |
| `0x857007` | `cmp edx, 0x15` | `0x856FE0` | grant the +0x04 stratagem of index edx |
| `0x8570F4` | `cmp edx, 0x15` | `0x8570E0` | revoke the +0x04 stratagem of index edx |
| `0x8572BC` | `cmp eax, 0x13` | `0x857170` | `lea eax,[rcx-1]; cmp eax,0x13; ja`: accepts 1..20 only → `0x14` |
| `0x87513B` | `cmp r8d, 0x15` | `0x875110` | validator rejects idx ≥ 21 (idx 0 accepted before it) |
| `0x8752EA` | `cmp r8d, 0x15` | `0x875270` | is booster index free to pick |
| `0x8756EA` | `cmp r15d, 0x15` | `0x8755D0` | player who brought booster N |
| `0x11F83C0` | `cmp ecx, 0x15` | `0x11F8058` | gap: the accessor set (section 5) |
| `0x11F83E0` | `cmp edx, 0x15` | `0x11F8058` | gap: the accessor set (section 5) |
| `0x11F8539` | `cmp esi, 0x15` | `0x11F8490` | `cmovb`: row pointer only if idx < 21 (`+0x00` already read) |
| `0x11F860E` | `cmp edi, 0x15` | `0x11F8580` | inlined row getter in the 1..20 loop |
| `0x11F86D0` | `cmp ecx, 0x15` | `0x11F86D0` | tooltip text from +0x08/+0x18/+0x30 |
| `0x135C10D` | `cmp edx, 0x15` | `0x135C100` | is booster N equipped by a player |
| `0x135C1ED` | `cmp edx, 0x15` | `0x135C1E0` | slot record that carries booster N |
| `0x145896C` | `cmp esi, 0x15` | `0x1458480` | row read in the 1..20 loop |
| `0x146E6F4` | `cmp ebp, 0x15` | `0x146E0A0` | row read after the clamp |
| `0x14701BB` | `cmp eax, 0x15` | `0x146FC40` | slot index → id |
| `0x1470269` | `cmp eax, 0x15` | `0x146FC40` | slot index → id (write-back to loadout) |
| `0x175266C` | `cmp eax, 0x15` | `0x1752610` | index → id in the packed record |
| `0x1896DC2` | `cmp eax, 0x15` | `0x1896D40` | HUD indicators for granted stratagems, +0x04 |
| `0x18CBA8C` | `cmp edx, 0x15` | `0x18CBA50` | slot widget: set index, +0x20 |
| `0x18CC1D2` | `cmp eax, 0x15` | `0x18CC140` | "new" badge |
| `0x18CCB77` | `cmp eax, 0x15` | `0x18CCB50` | slot tooltip, +0x10 |
| `0x191CA64` | `cmp edx, 0x15` | `0x191CA50` | widget icon then details |
| `0x191DBF1` | `cmp ebx, 0x15` | `0x191D040` | icon + info panel (switch case) |
| `0x191FBC0` | `cmp edx, 0x15` | `0x191FBC0` | details panel, +0x10 |

**Loop ends (51)**

| RVA | Instruction | Root | Loop |
|---|---|---|---|
| `0x85691A` | `cmp ecx, 0x15` | `0x8568A0` | id → index scan, team booster set changed: +0x34 load/unload, +0x04 grant/revoke |
| `0x856CBA` | `cmp ecx, 0x15` | `0x856BE0` | id → index scan, (index, value) pairs from item query 0x12DF110 |
| `0x875583` | `cmp edx, 0x15` | `0x8753B0` | id → index scan, up to 4 players' booster indices |
| `0xB9E3E2` | `cmp r8d, 0x15` | `0xB9E2D0` | id → index scan, loadout hash -> index -> 0xBF13D0 |
| `0x11DE51A` | `cmp ecx, 0x15` | `0x11DE430` | id → index scan, (index, value) list from item query |
| `0x11F818A` | `cmp eax, 0x15` | `0x11F8058` | id → index scan, gap: the accessor set (section 5) |
| `0x11F81BA` | `cmp eax, 0x15` | `0x11F8058` | id → index scan, gap: the accessor set (section 5) |
| `0x11F81EF` | `cmp edx, 0x15` | `0x11F8058` | id → index scan, gap: the accessor set (section 5) |
| `0x11F823B` | `cmp eax, 0x15` | `0x11F8058` | id → index scan, gap: the accessor set (section 5) |
| `0x11F827B` | `cmp eax, 0x15` | `0x11F8058` | id → index scan, gap: the accessor set (section 5) |
| `0x11F82BB` | `cmp eax, 0x15` | `0x11F8058` | id → index scan, gap: the accessor set (section 5) |
| `0x11F82FB` | `cmp eax, 0x15` | `0x11F8058` | id → index scan, gap: the accessor set (section 5) |
| `0x11F83AA` | `cmp eax, 0x15` | `0x11F8058` | id → index scan, gap: the accessor set (section 5) |
| `0x11F842A` | `cmp eax, 0x15` | `0x11F8058` | id → index scan, gap: the accessor set (section 5) |
| `0x11F86AB` | `cmp edi, 0x15` | `0x11F8580` | rows 1..20 |
| `0x135C0CA` | `cmp ecx, 0x15` | `0x135C070` | id → index scan, team booster indices (hash scan) |
| `0x135F0DB` | `cmp esi, 0x15` | `0x135EA10` | id → index scan, per-player loadout summary -> 0xBEE770 |
| `0x1361020` | `cmp r14d, 0x15` | `0x1360970` | id → index scan, loadout summary -> 0xBEE770 |
| `0x136270A` | `cmp ebx, 0x15` | `0x13620A0` | id → index scan, loadout -> packed form (0xBEE770, 0x13632C0) |
| `0x136E7D0` | `cmp edx, 0x15` | `0x136E6A0` | id scan (type 5 case) |
| `0x138136D` | `cmp r8d, 0x15` | `0x1381220` | id → index scan, per-player loadout -> 0xBF13D0 |
| `0x13817AD` | `cmp r8d, 0x15` | `0x1381710` | id → index scan, loadout report -> 0xBF13D0 |
| `0x1458A0C` | `cmp esi, 0x15` | `0x1458480` | rows 1..20 |
| `0x14688A2` | `cmp ebx, 0x15` | `0x1468320` | id → index scan, HUD/menu update, booster page |
| `0x146A52A` | `cmp ecx, 0x15` | `0x146A460` | id → index scan, player's booster index for the HUD |
| `0x146FF00` | `cmp edx, 0x15` | `0x146FC40` | id → index scan, sync loadouts into manager +0xA8C |
| `0x14701EF` | `cmp r8d, 0x15` | `0x146FC40` | id → index scan, sync loadouts into manager +0xA8C |
| `0x14E720A` | `cmp edx, 0x15` | `0x14E5D80` | id → index scan, tooltip, name from 0x21DD630 |
| `0x14FDB4E` | `cmp ebx, 0x15` | `0x14FD800` | id → index scan, item card, name from 0x21DD630 |
| `0x1750C5C` | `cmp ecx, 0x15` | `0x1750870` | id scan; not found → reset loadout |
| `0x1750CD0` | `cmp r8d, 0x15` | `0x1750870` | id scan for the reset id |
| `0x1752780` | `cmp ecx, 0x15` | `0x1752710` | id → index scan, unpack loadout record (hash -> index) |
| `0x178D4A2` | `cmp ecx, 0x15` | `0x178D310` | id → index scan, item hash -> +0x14 id |
| `0x178D50D` | `cmp eax, 0x15` | `0x178D310` | re-scan |
| `0x1799B8E` | `cmp ecx, 0x15` | `0x1799750` | id → index scan, item icon, +0x20 (alias path) |
| `0x1799D1A` | `cmp ecx, 0x15` | `0x1799750` | id → index scan, item icon, +0x20 (alias path) |
| `0x179A5CA` | `cmp ecx, 0x15` | `0x179A540` | id → index scan, icon via alias table 0x21CAFB0, +0x20 |
| `0x179AA6E` | `cmp edx, 0x15` | `0x179AA40` | id → index scan, icon widget, +0x20 |
| `0x179CC8E` | `cmp ecx, 0x15` | `0x179C920` | id → index scan, item label, +0x10/+0x14 |
| `0x179CFEA` | `cmp edi, 0x15` | `0x179C920` | re-scan |
| `0x179D650` | `cmp edx, 0x15` | `0x179D620` | id → index scan, item name label, +0x10/+0x14 |
| `0x179E08E` | `cmp ecx, 0x15` | `0x179DF40` | id → index scan, item description id, +0x18 |
| `0x179E14E` | `cmp eax, 0x15` | `0x179DF40` | re-scan |
| `0x1893D7A` | `cmp ebx, 0x15` | `0x1893D40` | id → index scan, HUD block icon by hash, +0x20 |
| `0x18CB33E` | `cmp ecx, 0x15` | `0x18CAFB0` | id → index scan, item tile, booster case, +0x20 |
| `0x18CBC6B` | `cmp edx, 0x15` | `0x18CBC40` | id → index scan, icon widget by hash, +0x20 |
| `0x18D8691` | `cmp edi, 0x15` | `0x18D8540` | all rows (picker list) — without it a new row never appears |
| `0x19394CE` | `cmp ebp, 0x15` | `0x1939130` | id → index scan, loadout panel refresh |
| `0x19ADFAD` | `cmp ecx, 0x15` | `0x19ADF70` | id → index scan, description refresh |
| `0x19AE0AD` | `cmp ecx, 0x15` | `0x19AE070` | id → index scan, description refresh |
| `0x19AE18C` | `cmp ecx, 0x15` | `0x19AE160` | id → index scan, description refresh |

**Sentinels (29):** 23 "not found" values written after a scan, 4 "rejected" values, 2 sentinel tests.

| RVA | Instruction | Root | Use |
|---|---|---|---|
| `0x85691F` | `mov ecx, 0x15` | `0x8568A0` | not found → team array (then `+0x34` read before guard) |
| `0x856CBF` | `mov ecx, 0x15` | `0x856BE0` | not found → output list (unfiltered) |
| `0x875588` | `mov edx, 0x15` | `0x8753B0` | not found → out[4] |
| `0x87583F` | `mov r8d, 0x15` | `0x875800` | rejected (validator failed) → 0x8758D0 |
| `0x8758A2` | `lea r8d, [rdx + 0x15]` | `0x875800` | `xor edx,edx; lea r8d,[rdx+0x15]`: rejected, applied locally |
| `0x8759E4` | `mov eax, 0x15` | `0x8758D0` | rejected value in reply `0x71C061C5` (`cmovne` valid idx) |
| `0x875AF9` | `cmp ebp, 0x15` | `0x8758D0` | test: idx == 21 → HUD restore path |
| `0xB72950` | `mov eax, 0x15` | `0xB727D0` | same, inlined copy |
| `0xB72A72` | `cmp r13d, 0x15` | `0xB727D0` | test: same, inlined copy |
| `0xB9E3E8` | `mov r8d, 0x15` | `0xB9E2D0` | not found → `0xBF13D0` |
| `0x11DE51F` | `mov ecx, 0x15` | `0x11DE430` | not found → output list (unfiltered) |
| `0x11F81BF` | `mov eax, 0x15` | `0x11F8058` | `index_of_hash` return |
| `0x11F81F4` | `mov eax, 0x15` | `0x11F8058` | `index_of_text_id` return |
| `0x135C0CF` | `mov ecx, 0x15` | `0x135C070` | not found → out[4] |
| `0x135F0E0` | `mov esi, 0x15` | `0x135EA10` | not found → `0xBEE770` |
| `0x1361026` | `mov r14d, 0x15` | `0x1360970` | not found → `0xBEE770` |
| `0x136270F` | `mov ebx, 0x15` | `0x13620A0` | not found → `0xBEE770` / `0x13632C0` |
| `0x1381373` | `mov r8d, 0x15` | `0x1381220` | not found → `0xBF13D0` |
| `0x13817B3` | `mov r8d, 0x15` | `0x1381710` | not found → `0xBF13D0` |
| `0x14688A7` | `mov ebx, 0x15` | `0x1468320` | not found → booster page widget `0x189FFE0` |
| `0x146A52F` | `mov ecx, 0x15` | `0x146A460` | not found → `*out` |
| `0x146FF05` | `mov edx, 0x15` | `0x146FC40` | not found → **stored in active slot `+0xA8C`** |
| `0x14701F5` | `mov r8d, 0x15` | `0x146FC40` | not found → `0x875800` |
| `0x14E720F` | `mov edx, 0x15` | `0x14E5D80` | not found → name index ("Count") |
| `0x14FDB53` | `mov ebx, 0x15` | `0x14FD800` | not found → name index ("Count") |
| `0x1750CD6` | `mov r8d, 0x15` | `0x1750870` | not found → `0x875800` |
| `0x1752785` | `mov ecx, 0x15` | `0x1752710` | not found → unpacked index |
| `0x179CFEF` | `mov edi, 0x15` | `0x179C920` | not found (unreachable) → row index |
| `0x179D655` | `mov edx, 0x15` | `0x179D620` | not found → used as row index unchecked |

**Clamp (1)**

| RVA | Instruction | Root | Use |
|---|---|---|---|
| `0x146E6E2` | `mov ebp, 0x15` (then `cmp eax, ebp; cmovb ebp, eax`) | `0x146E0A0` | menu cursor clamped to ≤ 21 before `0x875800` |

**NOT booster (7)**

| RVA | Instruction | Root | Why not |
|---|---|---|---|
| `0x92409B` | `add rdi, 0x14` | `0x9235F0` | pointer stride `+0x14` in a loop |
| `0x924277` | `add r8, 0x14` | `0x9235F0` | struct offset `+0x14` |
| `0x136E72F` | `cmp eax, 0x16` | `0x136E6A0` | `dec eax; cmp eax,0x16; ja`: item-type switch 1..23 (jump table `0x136EB98`) |
| `0x179CDA3` | `mov edx, 0x14` | `0x179C920` | a null-row field offset (`+0x14` / `+0x10`, dereferenced at `0x179CDD2`) after the 90-entry `0x32C9960` scan, not a booster count |
| `0x188A477` | `add edx, 0x15` | `0x1889A70` | unrolled index arithmetic in a curve function |
| `0x18CB078` | `cmp eax, 0x16` | `0x18CAFB0` | item-type switch 1..23 (jump table `0x18CB868`) |
| `0x191D513` | `cmp eax, 0x16` | `0x191D040` | switch 1..23 (jump table `0x191DC4C`) |

Also seen, not a bound: `0x69183E mov edx, 0x13` is the booster index 19 passed to `0x856D40`. Index immediates like it stay as they are.

**Unchecked index uses.** These read a row with no bound, or before the bound. They need no patch, but they show where index 21/22 must stay harmless:
- `0x179A540` and `0x1799B78`: the index comes from the alias table's third field, with no check.
- `0x17981F0` / `0x179851E`: three widget indices at `[+0x1540..+0x1548]`, no check anywhere.
- `0x179D655`: the not-found sentinel is used as a row index straight away (`+0x10` / `+0x14`). Today that reads the curve global (`0x32FDF38`, a function pointer's halves).
- `0x8569E3` and `0x856AFC` (`0x8568A0`): `+0x34` is read **before** the `cmp ebx, 0x15` guard. With 21 that reads `0x32FDF5C` (inside the next table; 0 in this dump).
- `0x11F84DB` (`0x11F8490`): `+0x00` is read before the `0x11F8539` guard.
- `0x875B1D` / `0xB72A90`: the old slot index is read from `+0xA8C` and used to index the table without a check.
- `0x856DB0` and `0x11F8200`: no check at all.
- `0x179CFEF`: an unreachable not-found path (the same hash already matched); moved with the others for consistency.

### c. Other things to extend

- **Enum name table `0x21DD630`:** copy it with 23 entries: names 0..20, the new name at 21, `"Count"` at 22. It has exactly two readers, both image base + disp32: `0x14E7214 mov rdx, [r15 + rdx*8 + 0x21DD630]` and `0x14FDB5D mov rdx, [r14 + rdx*8 + 0x21DD630]`. A new booster left with the old table would be named "Count", and sentinel 22 would read the unrelated string after it.
- **A zeroed row 22 after the new row.**
  - The reads of the sentinel row before or without a check (`0x8569E3`, `0x856AFC`, `0x179D655`, `0x11F84DB`, `0x875B1D`) would hit the copy's row 22. Make it all zero: id 0, `+0x34` 0 (nothing to load), text ids 0.
  - The current layout is safe only because the bytes after row 20 happen to be harmless.
- **The new row:**
  - **Required:**
    - `+0x00`: a unique id32;
    - `+0x0C`: 1, or it never appears in the picker;
    - `+0x10` / `+0x14` / `+0x18`: text ids;
    - `+0x20`: icon;
    - `+0x30`: value format;
    - `+0x08`: the value.
  - **Optional:**
    - `+0x04` (stratagem grant): any stratagem id valid in `0x37CB600`. If HellForge has moved the stratagem list, its readers in `0x856DD0` / `0x85707E` (also `0x857075`, `0x857085`) / `0x1896D40` are HellForge's stratagem sites, not this table's;
    - `+0x34`: a resource hash to keep loaded.
- **Alias table `0x21CAFB0`:** not needed, unless other item ids should map onto the new booster.
- **Profile lists:**
  - `[0x347CDD8] + 0x1DDC` holds 128 seen-booster slots; the count is cached in `0x331387C`, and `0xD903` means "all 128".
  - `0x11F8580` / `0x1458480` require every booster 1..N to be owned and seen. Widening their loops makes them fail until the new booster is owned and seen (static).
  - The picker list (`0x18D43A0`, in `0x18D8540`) caps at `0x100` entries, not 21: no change needed.

## Ownership / backend

From HellForge's LocalBackend (`item-catalog.json`, `profile-templates.json`, `ProfileStore.cs`) and the dump:
- **`+0x00` is the item id32 in the backend catalogue.** Muscle Enhancement (row 3, `0xA57D4ECB` = 2776452811) is `{"itemId": 2776452811, "mixId": 2776452811, "isConsumable": true, "progressionCategory": 5}`. The profile inventory has `{"mixId": 2776452811, "amount": 1, "isDiscovered": true}`.
- **The game's item-type enum also uses 5 for boosters** (static: the `0x136E6A0` validator's case 5 and widget `+0x68C`). Whether that is the same enum as `progressionCategory` is a guess.
- **Ownership:**
  - `0x136FDB0` (owned) and the profile list `[0x347CEF8] + 0xB9CE4` decide it.
  - An unowned booster is left out of the picker (`0x18D8540` / `0x11F8490`).
  - `0x1750870` resets an unowned booster in the loadout to row 0's id and pushes that through `0x875800`.
- **What a new booster needs:** a catalogue item (category 5, consumable) with its new id32 and an inventory entry. HellForge's LocalBackend (`RegisteredItems.cs`) already adds mod weapons and stratagems to the catalogue, so boosters would go the same way.
- **The other route:** the list `[0x3326938]` makes an index selectable without ownership (`0x11F8490`, `0x18D8540`). It starts with a 2-entry `{u32 index, f32 value}` list (count `+0x10`), the shape `0x856BE0` writes, so it is probably filled from there (static, unconfirmed). If so, an unfiltered 21 in it makes `0x856D40(21)` and `0x11F8490(21)` true today; the `0x856CBF` sentinel is already on the patch list. Its readers include `0x8757D0`, `0x18D85D0`, `0x18A01E1` and `0x8573FC` besides those above.
- **Tuning:** HellForge Stats.cs already treats `+0x08` as "the tuning value (BoosterSettings +0x08, the booster's row)".

## Known gaps

- **Code that handles booster values without touching the table.** The sweep only sees functions that address the table. The validator `0x875110` and the setter `0x875800` were found by hand.
  - **Not traced:** the network receive side of `0x71C061C5` (and its handler calling `0x8758D0`), `0xBF13D0` and `0xBEE770`, and the consumers of `0x856BE0` / `0x11DE430` output (callers `0xB4F159`, `0xB4F694`, `0xB9896C`, `0x12DB01C`), which receive 21 unfiltered.
  - **Other booster-index code** (found by review, none needs a `0x15` change): `0x60B5C0` and `0x820840` ("player has booster idx"), `0x8757D0` (value by index), `0x87B954` (replicated write into slot `+0xA8C`), `0x1465A3E` (clears the local slot), `0x14679F1` / `0x11E8B25` (booster indices stored in stratagem data, `+0x118` list) and `0x22655E0` (a static, mode-keyed index list `{8, 5, 6, 0}`).
  - **No static callers:** many of the functions here (`0x856DB0`, `0x856FE0`, `0x8570E0`, `0x11F81A0`, `0x11F8490`, `0x135C100` and others) have no direct callers in the code, so they are reached through tables of function pointers. The static call graph is incomplete; only the runtime test below can confirm coverage.
  - **Data indices:** the data-driven `0x856D40` callers (`0x695A2A`, `0x8785FA`) show that settings data stores booster indices. A data file naming index 21 does not exist yet, but such files would also need to agree.
- **Register-passed table pointers.** A callee can receive a row pointer from `0x11F8170` / `0x11F8390` / `0x11F83C0` (callers `0x14E8574`, `0x1788A9E`, …) and read fields without any table reference. Those reads follow the copy automatically. Any code that compares such a pointer against the old table's bounds would not; none was seen.
- **Inlined `0x856D40` copies and other 21-wide arrays.** An array sized by booster count (for example per-booster state indexed by value) would not show up as a table site or a `0x15` constant if its size is implicit. None was found, but none was searched for beyond the constants.
- **The live-loadout booster slot.** The live-loadout validation in `0x1750870` reads the booster id at `r15 + 0x12C` (`r15 = [0x347CDD8] + 0x30`), while the 0x9F0 loadout has it at `+0x130`. Not reconciled.
- **Runtime tests to run before trusting any of this:**
  - Cheat Engine "find what accesses this address" on a row, for example row 3 `+0x00..+0x37` (`0x32FDB38`) and then on the copy's row 21. Do it through the booster picker (open, scroll, confirm), deploy, the mission (effects, HUD indicator), extraction and the end screen. Any access from an address not in the list above is a missed site.
  - Break on `0x856D40` with edx = 21 to see who asks.
  - Watch `[0x33264F8] + 0xA8C + slot*0x40` across a pick.
  - Co-op: modded host + modded client, then modded + unmodded. Expect the unmodded side to crash or misbehave on index 21 (it stores it unchecked, see "Why a mismatched peer breaks"), so a mod must refuse mixed sessions rather than rely on the game rejecting it.

## Neighbouring data

| RVA | Rows × size | What | Users |
|---|---|---|---|
| `0x32FDF28` | 24 bytes | easing curve `{0.0, 0.5, type 12, pad, fn 0x1599E00 (a powf wrapper)}` | `0x1889930`, `0x1889AB4` |
| `0x32FDF40` | 28 × `0x80` | hash-keyed settings table (key `+0`, prerequisite id `+4`, small ids `+8`/`+0xC`, text id `+0x14`, count ≤ 4 / RNG modulus `+0x24`, u32 array `+0x28`, flag `+0x38`, `+0x60`/`+0x64`, filter `+0x74`). Identity unknown | getter `0x11D8FE0` (returns table, 28), `0x11DB810` (→ ptr), `0x11DB840` (→ index), `0x8B0900`, `0x8B0750`, `0x8B0D40`, `0x8B0CD4`, `0x7CC740`, `0xA77DE0`, `0xA78620`, `0x11BD350`, `0x11DB8B0`, `0x11DBC30`, `0x17C7620`, `0x18D6490`, `0x192AF90` |
| `0x32F9510` | 4 × 56 | pointer at `+0`, id at `+8`; item type 13. The default loadout's `+0x134` id `0x7ED81796` is one of its keys. Its 56-byte stride makes it look like the booster table to an `imul 0x38` scan | getter `0x11D8F80`, random pick `0x11D9000`, scans `0x11D9070` / `0x11D90C0` / `0x11D90F0`, `0x17522B0`, `0x179CE91`, `0x18CB661`, `0x136E92F` |
| `0x32DBF20` | 26 × `0x38` | id-keyed (ids `+0`/`+4`, value `+8`, filter `+0x30`); id `0xC2D9FC37` = "random from the unlocked pool", which is the default loadout's `+0x138` | getter `0x11D8FA0`, item resolver `0x11DBC30` / pool builder `0x11DBDE0` (fallback after 0x32FDF40) |

Also 56-byte-stride arrays that are not the booster table: `0x92385E` (heap records at `[[0x3326598]+0x38]`), `0x146FD9B` (players `[0x3326468]+0x2C8`), `0xB49DD8` (a component's own array).

## Other findings

Collected from the seven agents' `## Other findings`, deduplicated. Each item is something one of them saw in code (static unless marked).

**Booster state and session globals**
- `[0x3326938]`: list of 8-byte entries keyed by booster index (count `+0x10`). Read by `0x856D10`, `0x856D40`, `0x875270`; membership makes an index selectable in `0x11F8490`. Who fills it: unknown.
- `[0x33264F8]`: booster/session manager.
  - Player-id → slot hash map: slots `+0x930`, cap `+0x938`, empty key `+0x93C`, multiplier `+0x940`.
  - Per-slot object `+0x948 + slot*8` (byte `+0x14` bit 0, `+0x10` event target); per-slot record `+0xA7C + slot*0x40`. In the record, `+0x0..+0xC` are compared with loadout `+0x120..+0x12C`, `+0x10` is the booster index, and `+0x28/+0x2C/+0x38/+0x3C` are other loadout fields.
  - Count `+0x8E8`. A second 0x44-stride list at `+0x97C` with the same count is used by `0x875270`.
- `[0x33266A0]`: `[+8] > 0 && [+0x40] == 4` turns boosters off in `0x856D40`. The same mode forces 60.0 in `0x6A5B70`.
- `[[0x3326340] + 0xAC21C] == 4`: a game state, guess "in mission", required by the grant/revoke paths (`0x856FE0`, `0x8570E0`, `0x8568A0`).
- `[[0x347CE28] + 0x4294] == 0xE`: a game state used by `0x8758D0` and `0x1381710`.
- `[0x347CEF0] + 0xB398` is the local player and `+0xB3A8` the authority or peer (guess) (`0x875800`, `0x8758D0`).
- Session player list `[0x3326468]`: entities `+0x2C8` (stride 0x38), count `+0x84`, ids `[+0xE8 + i*8] → +8`, id count `+0x88`, null id `0x3483C34`. Local id: `+0x88 ? [[+0xE8]+8] : [0x3483C34]` (`0x857119`, `0x6A5D28`).
- Slot records `[0x347CE50]`: stride 0x1690, count `+0x2D200`, entity `+0`, flags `+0x28` / `+0x2C`, booster id `+0x168`, `+0x18` (−1 checked in `0x135C1E0`).
- Latent bug (not booster-specific): when a member id is not in the manager's map, slot = 0xFFFFFFFF and `shl rcx,6` gives a huge offset (`0x8756B1`, `0x8751EC`, `0x8759C2`). Apparently never reached.

**Profile, loadout, items**
- Profile `[0x347CEF8]`:
  - Items `+0x1CE4` (stride 0xB8: id `+8`, type `+0xC`), count `+0x1CE0`, state `+0x1CF8 + i*0xB8` (2 or 4 = usable).
  - Id map `+0xB9CE4` (stride 0x18 `{index, id, …, +8 profile id}`), index list `+0xD1D48` over `[+0xD1CF8, +0xD1CFC)`.
  - Also `+0xDDCF8` (> 7 required by `0x136FDB0`) and per-type counts `+0xD1CE4 + type*4`.
- Seen lists `[0x347CDD8]`: `+0x1DDC` (boosters, 128 slots), `+0x13DC` (0x200), `+0xBDC`. The count is cached in `0x331387C` (`0xD903` = full, −1 = not computed). They drive the "new" badge (`0x18CC140`, `0x18CAFB0`).
- Loadout struct, 0x9F0 bytes (`0x17522B0` builds the default, `0x1361F40` copies):
  - `+0x130` booster id (default = row 0);
  - `+0x120..+0x144` other slot ids (`+0x124` `0x261C4A52`, `+0x128` `0x4657CFB3`, `+0x12C` `0x61B31723`, `+0x134` `0x7ED81796`, `+0x138` `0xC2D9FC37`);
  - `+0x188` a list of 0x30-byte stratagem entries (count `+0x788`; defaults ids 0x76, 0x21, 0x42, 0x5C).
- Packed loadout record, 0x28 bytes (`0x1752610` / `0x1752710`): `+0x10` booster id. `+0x1C` comes from the 90 × 0x18 table at `0x32C9960`, and `+0x24` from the 60 × 0x68 table at `0x32AF3E0` (other loadout kinds, guess).
- Item type enum (validator `0x136E6A0`, jump table `0x136EB98`, types 1..23): 5 = booster, 13 = the `0x32F9510` table. The UI category switch (`0x14E5D80` / `0x14FD800`, 0..9) uses 8 for boosters.
- Name arrays like `0x21DD630` for other kinds: `0x21DCB30` (kind 4, "Tier00", …) and `0x21DCE20` (stratagems: "None", "DropoffTank", "DroneStun", …). `0x11F2490` is stratagem id → index.
- `0x18DD110(id)`: calls `0x18DC920` and checks the result against `0x939C741D`. 11 callers; a general item check (guess: entitlement).
- Typed query `0x12DF110` (kind 0x53): entries with `[+0x18] == 4` carry a booster id at `+0x1C` (`0x856BE0`, `0x11DE430`).

**Messages and events (via `0xBDE430` / `0xFD97E0`)**
- `0x71C061C5`: booster index (u32).
- `0x5D509124`: booster id (from `0x146E0A0`).
- `0x3B12BD49`: booster slot changed.
- `0x3293DFA6`: reinforcement refill progress.
- `0x5B6AC15B`: the booster 6 timer.
- `0x723D4A53`: the booster 16 drop.
- `0xB317CB48` and `0x1F51A605`: loadout report.

**UI**
- Per-player HUD blocks at `base + 0x620E8 + n*0x1EE18` (base `[[0x347CE38]+0xB0]` or the UI object); booster id at block `+0x3C4`.
- Menu object: page `[+0x2818]` (5 = boosters), booster cursor `[+0x1658AC]`; pages 1/3/4 call `0x875EC0` / `0x875CD0` / `0x8760B0` (other loadout setters, guess).
- Booster slot widgets are 0x2B68 bytes: index `+0x2B60`, id `+0x2B5C`, flags `+0x2B5A`, category `+0x2B50`.
- Setters seen: `0x144F800` (set image), `0x1450230` (set resource, type `0x9448536C2D6AE780` or `0x57FCF14AD069020B`), `0x1798770` (icon, resource type `0x3AA8B87E`), `0x1448AD0` (alpha/hide), `0x143BF90` / `0x1441720` (text), `0x143A1B0` (text with tokens).

**Other systems**
- Component-manager array `[0x3326E68]`: count `+0x44`, 16-byte entries (`+0x48` manager, `+0x50` type id), walked by the 0x103-case switch in `0xB48C20`. The type `0x18` entries in its `+0x4268` list are booster managers (`0xB727D0`).
- Ship upgrades: a 44 × 0x30 table at `0x32EA690` (id `+0`) and values at `0x330D2E0` (`+0x28` f32, `+0x2C` 0 = mul / else add). Owned bitsets are in an 8 × 0x20 table at `0x346D5A0` (bits `+0x10`) (`0x18AE050`).
- Game RNG: a 64-bit LCG at `0x3483C38` (`× 0x5851F42D4C957F2D + 0x14057B7EF767814F`), in `0x9D1AAC`, `0x8B0880`, `0x11D9000`.
- Stat-modifier lookup `0x11D9DF0(entity, stat id, 3)` → `{+4 mode: 0 set, 1/3 add, 2 mul; +8 f32}` (`0x69A47A`).
- Resources: `0x12689C0` resolves a resource by id, and `0x12FE350` / `0x12FE4E0` load / unload it (with `[0x347CEB0]`).
- Stratagem grant: `0x66F190` / `0x66F4C0` add / remove a stratagem grant, after `0x670E30`, a player check.
- `0x11F8080`: id → body-part suffix string (`_head`, `_torso`, `_l_arm`, `_r_arm`, `_l_leg`, `_r_leg`, at `0x2266010..0x2266078`).
- More settings getters returning (table, count): `0x11D8F80` (`0x32F9510`, 4), `0x11D8FA0` (`0x32DBF20`, 26), `0x11D8FC0` (`0x32C9960`, 90), `0x11D8FE0` (`0x32FDF40`, 28). There is no such getter for the booster table.
- Pointer tables: `0x37C5470` (`0x11F8060`, `+0x38`), `0x37C6040` (`0x11DB7F0`), `0x37CB470` (default stratagem entry). All are zero in the dump (filled at run time).
- Another curve global at `0x32F7EB0` (`0x1889930`). Float lookup tables at `0x21CB160`, `0x21C6F78`, `0x2160678` (`0x11D9130..0x11D91AA`).

