# Bonus stratagems: a one-use Veteran Maxigun mid-mission

Scope: how a HellForge code mod (C, HellForge mod SDK) can give the local player a separate bonus stratagem partway through a mission. The target is a "Veteran Maxigun" that:

- appears in the player's stratagem list when their Maxigun levels up (Veterancy System);
- drops a fresh M-1000 Maxigun and backpack when called;
- is spent after one call.

This note extends option 2 of the earlier note (`S/free-call-in.md` sections 2 and 3.2). It re-checks that note's leads against the sources and corrects two of them (section 0, point 6).

Tags:

- **[PROVEN]**: code that ships and runs on this build (HellForge's loader, the user's own shipped mods), or behaviour HD2Runtime records as live-proven.
- **[STATIC]**: read from disassembly pins and research scripts only, with no live test of this use.
- **[SPEC]**: my inference.

Abbreviations:

- `S` = `/tmp/claude-0/-home-claude/83df7915-a41e-51e7-8756-7e1e82cec6e8/scratchpad`
- `HF` = `S/hf` (HellForge, commit b1aef77)
- `RT` = `S/research-callin/HD2Runtime` (HD2Runtime 0.50.1, commit a8c5339)
- `UM` = `S/research-callin/hellforge-mods` (the user's mods repo, commit 27c403f)

**Offsets.** All RVAs are in game.dll. HD2Runtime's research build F5FEE03DCFDB has the same game.dll SHA-256 as HellForge's build 25480438 (2E2C3B7C...: RT/domains/slot_cooldown.lua `source.gameDllSha256`; HF/native/mod-loader/stratagems.c:5).

**Licence.** The record, HUD, matcher, sync and in-flight offsets come from HD2Runtime's research. Reusing them in a published mod needs visible credit to HD2Runtime (RT/LICENSE:16-24; RT/README.md:140-143). See section 10.

**No game.dll here.** No game.dll image was available, so nothing new was disassembled. Every [STATIC] claim rests on HD2Runtime's pinned instructions.

---

## 0. TL;DR

1. **Define it with `add_stratagem`** [PROVEN mechanism]:
   - `base = HF_STRATAGEM_M1000_MAXIGUN` (110), its own `name` / `title` / `description`, a **new, unrelated code**, `uses = 1`, a short `cooldown`;
   - payload, package and icon left as the base's.
   - The base's payload is already the Maxigun hellpod: rack 0x73B63D637973C7A4 (gun and backpack) plus hellpod 0x73F8498BFFDCF415.
   - It must be a distinct type, because the matcher keeps one candidate per type.
   - Hide it from the loadout picker by clearing its row's selectable bit (row +0x80 bit 1) through `stratagem_row`.
2. **Grant it by appending one entry to the local player's stratagem record**, by hand. This mirrors exactly the five writes of the game's own append, 0x66F190 [STATIC]:
   - `type`, `uses = 1`, `granted = 1`, `cooldown end = 0`, then `count + 1`.
   - Use a 0x30-byte template copied from an idle granted entry, with activation and arrival made safe.
   - The HUD draws slots up to the entry count, and rebuilds a slot's arrows and icon when its type changes [STATIC]. A mid-mission type change in a local record entry is **live-proven** to refresh the HUD, matcher and call-in (HD2Runtime 0.4.0 / 0.5.0). The **append** itself (count + 1) is not yet proven: test it first.
   - Do **not** call 0x66F190 yet. Its arguments are only partly known (section 3.1).
3. **Expire it** by letting the game spend the one use. The entry stays at 0 uses, greyed, until the mission ends, when the game rebuilds the record without it [STATIC].
   - On the next level-up, **refill that same entry** (uses = 1, cooldown end = clock) instead of appending a second one.
   - To make it vanish instead, drop the count by 1, only when it is the last entry and its call-in has landed.
4. **Co-op.** The call runs on the thrower's machine and replicates like any vanilla call [PROVEN path]. But a direct record write is not sent to peers [STATIC: RT determination "a direct write sends nothing"], and a host sync may replace a client's record [I in RT].
   - Ship solo and host first.
   - Treat a client as experimental, with a watchdog that re-appends a wiped entry.
   - Every machine must run the same mod version (type numbering, modlist check).
5. **SDK.** The manifest needs `"sdk": 13`:
   - `add_stratagem` / `stratagem_row` (SDK 4);
   - `load_package`, `on_game_thread`, `session` (SDK 6);
   - `subscribe` / `game_state` (SDK 7);
   - `code_matches` / `read_code` (SDK 13);
   - plus `module_base`, `read` and `write` (SDK 1).
   - The user's Field Promotions manifest is at SDK 11 today (UM/mods/field-promotions/hellforge-mod.json:10).
6. **Corrections to the earlier note:**
   - **0x856FE0 → 0x66F190 is not proven to run mid-mission.** HD2Runtime found "No path adding them mid-mission ... [U]; a host sync replaces the whole record [I]" for the objective-granted types 71, 102 and 123 (RT/docs/custom-stratagems.md:1594-1597). 0x856FE0 is a grant path, but its timing is unknown.
   - **State +0x78C is not a per-entry level array.** It is a per-**type** u32[150] level-package choice (HF/native/mod-loader/stratagems.c:30-32), read only by the mission loader at 0x175340A. A hand append does not need to write it.

---

## 1. Defining the bonus stratagem

### 1.1 The registration

The call shape is the same as the user's shipped Fire Mission (UM/mods/fire-mission/src/fire_mission.c:315-327) [PROVEN].

```c
/* hellforge_mod_init only (HF/native/mod-sdk/hellforge_mod.h:999-1001). */
HfStratagem vet = {sizeof vet};
vet.base        = HF_STRATAGEM_M1000_MAXIGUN;          /* 110 (hellforge_content.h:369) */
vet.name        = "yui/field_promotions/veteran_maxigun";  /* identity: stable id = the name's 32-bit id */
vet.title       = "Veteran Maxigun";
vet.description = "A fresh M-1000 Maxigun and backpack, earned in the field. One use.";
vet.code        = "UUUD";      /* see 1.3; NOT the Maxigun's DLRDUU */
vet.uses        = 1;           /* row +0x50 = 1 */
vet.cooldown    = 5.0f;        /* row +0x68; 0 would keep the base's 480 s */
/* payload, package, loads_item, category, beam, flags, icon: 0/NULL -> the base's */
g_vetType = hf->add_stratagem(mod, &vet);   /* 150..197, or 0 + a log line */
```

| Field | Choice | Evidence | Status |
|---|---|---|---|
| `base` | 110. It must be a selectable base. HellForge refuses a base without row +0x80 bit 1 and +0xC0 bit 0 | HF/native/mod-loader/stratagems.c:371-372; hellforge_mod.h:216 | [PROVEN] (loader code) |
| What a call drops | The base's payload. The Maxigun row's payloads are {0x73B63D637973C7A4, 0x73F8498BFFDCF415}: the "M-1000 Maxigun pod" rack (record 52, not shared, consumers: the Maxigun only, spawnCount 2, slot 1 = pickup m-1000-maxigun) and the support hellpod | RT/domains/stratagem_authoring.lua ("M-1000 Maxigun" root.payloads); HF/mappings/references.json:9029-9037; HF/docs/modding/content.md:378 | [STATIC] data; base payload use [PROVEN] via add_stratagem ("What it does when called is its base's", content.md:371-373) |
| `uses = 1` | Written to row +0x50 (0xFFFFFFFF = unlimited) | stratagems.c:382 | [PROVEN] write. It matters only where the game reads the row's maximum (0x879550): record builds, Eagle rearm. A hand append sets the entry's own uses (section 2) |
| `cooldown` | 0 to 3600 s; > 0 is written to row +0x68. A call sets end = arrival + effective cooldown (0x8796A0) | stratagems.c:383; content.md:417-419; RT/scripts/research_stratagem_rearm_pool.py:93-96 (0xB9A820-0xB9A8A7) | [PROVEN] write / [STATIC] use |
| Icon, title, description | `title` / `description` get their own text ids (row +0x28/+0x2C/+0x30). `icon` NULL = the Maxigun's; a named but unbuilt icon **crashes the game** | stratagems.c:24-27, 379; content.md:421-426 | [PROVEN] |
| Category / ping | Left as the base's (blue, supply) | hellforge_mod.h:233; hellforge_content.h:824-828 | [PROVEN] |
| Limits | 48 new stratagems across all mods. New stratagems never count in mission statistics | hellforge_mod.h:1000; content.md:438-441 | [PROVEN] |

### 1.2 Keeping it out of the loadout picker (optional)

The local backend gives every added stratagem to the player for the session (content.md:428-431). To make the Veteran Maxigun level-up-only, clear the selectable bit after the rows exist:

```c
uintptr_t row = hf->stratagem_row(g_vetType);       /* 400-byte row; +0 and +4 must stay (hellforge_mod.h:1002-1004) */
uint8_t sel;
if (row && !hf->read(row + 0x80, &sel, 1) && (sel & 2)) { sel &= ~2; hf->write(row + 0x80, &sel, 1); }
```

| Claim | Evidence | Status |
|---|---|---|
| The picker grid keeps a type only if row +0xC0 bit 0 and row +0x80 bit 1 are set, and it is owned (0x136FC20) | RT/docs/custom-stratagems.md:502; RT/domains/stratagem_slots.lua pins 0x136FC89, 0x136FC97 | [STATIC] |
| HellForge's selection headings count only rows with +0x80 bit 1, and an empty heading is hidden | HF/native/mod-loader/stratagem_categories.c:126; hellforge_mod.h:201-203 | [STATIC] (loader code) |
| Mission-only (non-selectable) types still work in a mission record: 71, 102 and 123 are granted that way | RT/docs/custom-stratagems.md:1590-1595 | [STATIC] |
| The mission report serializes only selectable entries (row +0x80 bit 1). A hidden type stays out of it | custom-stratagems.md:508 | [STATIC] |
| HellForge keeps added rows in its own memory, so the cleared bit persists for the session | stratagems.c:364-387 (the row is HF's copy); the game's reload rewrites only vanilla slots (stratagems.c:9-10) | [SPEC] |

### 1.3 The code

- **The matcher keeps one candidate per type** (0x66DC10). Two equal codes are ambiguous: "the first candidate left after the swap-removal wins". A code that is the start of another makes that other uncallable (RT/docs/custom-stratagems.md:1580-1586). [STATIC]
- HellForge does not check codes (content.md:412-415).
- **Use a short code with no relation to any native one.** HD2Runtime lists 158 four-arrow codes with no relation to any native row, Up Up Up Down among them (custom-stratagems.md:1597-1599). [STATIC]
- **Also check against the user's own added stratagems.** Fire Mission's code is configurable (fire_mission.c:321).
- Do not reuse DLRDUU. The Maxigun's code is D L R D U U (RT/domains/stratagem_authoring.lua, `stratagem.calldown_code` currentDefault), and a vanilla Maxigun entry in the same record would make the code ambiguous.

### 1.4 Does the HUD show a newly appended entry, with its arrows?

| Claim | Evidence | Status |
|---|---|---|
| The HUD keeps 16 slots (0x3760 bytes each), updated every frame | RT/domains/stratagem_slots.lua pins 0x1834D2F (`mov r12d,16`), 0x1834D59, 0x1834D68 | [STATIC] |
| It finds the local record by peer id in `[game+0x347CE50]` and bounds its slots by the entry count at state +0x788 | pins 0x1834AE6, 0x1834B00, 0x1834B41, **0x1836589** (`cmp r14d,[rdi+0x788]`) | [STATIC] |
| Each frame a slot takes its entry's type (0x183675C) and rebuilds its arrow sprites when the type changed (0x1836D09). Up to 10 sprites (0x1836E0A). The slot widget rebuilds its icon when the type differs from its cached one (0x183A05B) | same file | [STATIC] |
| A mid-mission type change in a local entry redraws the arrows and icon, and the matcher and call-in follow, with no HUD write | HD2Runtime 0.4.0 and 0.5.0 live tests (custom-stratagems.md:1540-1550, 1805-1814) | **[PROVEN]** for a type change in an existing entry |
| So a count + 1 with a valid entry appears as a new slot with the mod type's arrows, title and icon | follows from the rows above | [SPEC]. To test (section 7, step 3) |
| Where it sits (after the loadout, or in the mission group with Reinforce and Resupply) | entry order is defaults first, then the loadout (0x66EFD0, then 0x66F190 appends); the HUD's grouping by the granted flag is not traced | [SPEC] |
| A "new stratagem available" notice like the objective grants' | not traced. It may live in 0x856FE0 or the objective system, not in the append | unknown |

### 1.5 The package

- **The mission loader asks only at mission load**, for every record entry's type (0x1753437 → 0x1753080: row +0xA8, row +0xF8 weapon key, ...) [STATIC] (RT/scripts/research_stratagem_slot_conversion.py docstring 5b). A mid-mission append is never asked for.
- **The Maxigun's package** is 0xB2C627B3BA7E0C0A, shared by the gun and the backpack pickups (HF/mappings/references.json:122, 7779-7786). [STATIC]
- **It is almost certainly already resident.** A Maxigun level-up needs a Maxigun in the world, so its package is loaded on the machine where it levels. [SPEC, high]
- **Hold it anyway, per machine:** `load_package(mod, 0xB2C627B3BA7E0C0A)` from `HF_EVENT_ENTITIES_READY`, as the compendium does (HF/examples/mods/stratagem-compendium/src/compendium.c:25-40). Sources: HF/docs/modding/runtime.md:306-324; hellforge_mod.h:1073-1079.
- **Its limits:** 64 packages are held in all, and loading is per machine. HellForge shares the packages it holds for mods with friends (runtime.md:309-311).

---

## 2. Appending it to the local record mid-mission

### 2.1 The record (re-verified layout)

| Item | Where | Evidence | Status |
|---|---|---|---|
| Records | `recs = *(u64*)(game+0x347CE50)`; 0x1690 bytes each; count u32 at `recs+0x2D200`; `rec+0` = u64 peer | RT/domains/stratagem_slots.lua `record` (global 55037520 = 0x347CE50, stride 5776, count 184832); HUD pins 0x1834AE6 `4C8B0D6383C401`, 0x1834AED `418B8900D20200`, 0x1834B07 `480590160000` | [STATIC] |
| Local peer | `*(u64*)(*(game+0x347CEF0)+0xB398)` | HF/native/mod-loader/event_sources.c:8 | [PROVEN] (HellForge's player events) |
| State | `st = rec+0x38`. Entries at `st+0x188`, 0x30 each, at most 16. Count u32 at `st+0x788`. Call-in key u64 at `st+0x9E8` | stratagem_slots.lua (state 56, entries 392, entryCount 1928, maxEntries 16, key 2536) | [STATIC] |
| State +0x78C | a per-**type** u32[150] level-package choice, read by the mission loader (0x175340A). Not per entry; an append need not touch it | HF/native/mod-loader/stratagems.c:30-32; stratagem_slots.lua pin 0x175340A | [STATIC] |
| Entry +0 | type (u32) | 0x66F2A7 `899688010000` | [STATIC] |
| Entry +4 | uses (u32; 0xFFFFFFFF unlimited) | 0x66F2CD `89868C010000`; reader 0x66D3E5 | [STATIC] |
| Entry +9 | granted (u8): 1 for defaults and grants, 0 for loadout picks | 0x66F2D7 `888691010000`; 0x66F133 | [STATIC] |
| Entry +0x10 | activation (u64 µs) | RT/domains/slot_cooldown.lua `entry.activation`; HUD 0x1836798 | [STATIC] |
| Entry +0x18 | cooldown end (u64 µs, absolute) | 0x66F2E0; availability 0x66D24A-0x66D25C | [STATIC] |
| Entry +0x20 | arrival (u64 µs) | slot_cooldown.lua `entry.arrival`; HUD 0x183678D, 0x18367D4 | [STATIC] |
| Entry +8, +0xA..+0xF, +0x28..+0x2F | **unknown** | none | dump them in the probe (section 7) |
| Clock | `*(u64*)(*(game+0x3326348)+0x18)`, µs | slot_cooldown.lua `clock`; HF/native/mod-loader/actions.c:544 also reads it | [PROVEN] (HellForge reads it) |
| In-flight call-ins | `*(game+0x3326D98)` → +0x1C count, +0x60 entries of 0x28: +1 done (0 = in flight), +0x10 key (= `st+0x9E8`), +0x18 slot index | stratagem_slots.lua `callIns`; pins 0x66D262-0x66D294 | [STATIC] |

### 2.2 What the game's append writes (0x66F190)

| RVA | Bytes | Does |
|---|---|---|
| 0x66F2A7 | `89 96 88 01 00 00` | entry +0 = edx (the type; rsi = state + i*0x30) |
| 0x66F2CD | `89 86 8C 01 00 00` | entry +4 = eax (uses, computed: the row maximum via 0x879550, as 0x66F125 does) |
| 0x66F2D7 | `88 86 91 01 00 00` | entry +9 = al (granted) |
| 0x66F2E0 | `48 C7 86 A0 01 00 00 00 00 00 00` | entry +0x18 = 0 |
| 0x66F2EB | `41 89 87 88 07 00 00` | count + 1 (r15 = state) |

Sources: RT/scripts/research_stratagem_slot_conversion.py:66-72; RT/domains/stratagem_slots.lua pins. [STATIC]

- The gaps between these pins are short: 0x66F2D3-0x66F2D7 is 4 bytes and 0x66F2DD-0x66F2E0 is 3 bytes.
- So the append writes no other entry field. It leaves +8, +0xA-0xF, +0x10, +0x20 and +0x28 as whatever the slot held before. [SPEC] (from the pin addresses)
- The game tolerates that, since its own appends do it. A hand append that sets only these fields is therefore what the game does. Its only risk is a stale arrival or activation, which the template step removes.

### 2.3 The recommended route: a guarded hand append

All of it runs on the game thread: an `on_game_thread` callback or an event listener (hellforge_mod.h:1069-1072, 1092-1095).

```c
#define REC_GLOBAL 0x347CE50u
#define LOCAL_PEER 0x347CEF0u
#define CLOCK      0x3326348u
#define REC_STRIDE 0x1690u
#define ST         0x38u
#define ENTRIES    0x188u
#define ESTRIDE    0x30u
#define ECOUNT     0x788u
#define EMAX       16u

static uintptr_t LocalState(uintptr_t game)            /* state = rec + 0x38, or 0 */
{
    uintptr_t recs = 0, peerObj = 0; uint64_t me = 0; uint32_t n = 0;
    if (hf->read(game + REC_GLOBAL, &recs, 8) || !recs) return 0;
    if (hf->read(game + LOCAL_PEER, &peerObj, 8) || !peerObj || hf->read(peerObj + 0xB398, &me, 8) || !me) return 0;
    if (hf->read(recs + 0x2D200, &n, 4) || n == 0 || n > 32) return 0;
    for (uint32_t r = 0; r < n; r++) {
        uint64_t peer = 0; uintptr_t rec = recs + (uintptr_t)r * REC_STRIDE;
        if (!hf->read(rec, &peer, 8) && peer == me) return rec + ST;
    }
    return 0;
}

/* Grant (or refill) the Veteran Maxigun. 1 granted, 0 refused (logged). */
static int GrantVeteran(uintptr_t game)
{
    uintptr_t st = LocalState(game); uint32_t count = 0; uint64_t now = 0, clockObj = 0;
    if (!st || hf->read(st + ECOUNT, &count, 4) || count > EMAX) return 0;
    if (hf->read(game + CLOCK, &clockObj, 8) || hf->read(clockObj + 0x18, &now, 8)) return 0;
    int tmpl = -1;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t e[ESTRIDE]; if (hf->read(st + ENTRIES + i * ESTRIDE, e, ESTRIDE)) return 0;
        uint32_t type = *(uint32_t*)e;
        if (type == g_vetType) {                         /* refill the one we have: never two */
            uint32_t one = 1; uint64_t end = 0;
            hf->write(st + ENTRIES + i * ESTRIDE + 4, &one, 4);       /* or +1 to stack */
            hf->write(st + ENTRIES + i * ESTRIDE + 0x18, &end, 8);    /* ready now */
            return 1;
        }
        uint64_t arrival = *(uint64_t*)(e + 0x20);
        if (tmpl < 0 && e[9] == 1 && arrival <= now) tmpl = (int)i;   /* an idle granted entry */
    }
    if (count >= EMAX) { hf->log(g_mod, "veteran: record full (16)"); return 0; }
    uint8_t e[ESTRIDE] = {0};
    if (tmpl >= 0) hf->read(st + ENTRIES + tmpl * ESTRIDE, e, ESTRIDE);
    *(uint32_t*)(e + 0)    = g_vetType;
    *(uint32_t*)(e + 4)    = 1;          /* uses */
    e[9]                   = 1;          /* granted */
    *(uint64_t*)(e + 0x10) = 0;          /* activation */
    *(uint64_t*)(e + 0x18) = 0;          /* cooldown end: as 0x66F2E0 */
    *(uint64_t*)(e + 0x20) = 0;          /* arrival: not inbound */
    if (hf->write(st + ENTRIES + count * ESTRIDE, e, ESTRIDE)) return 0;   /* the entry first ... */
    uint32_t next = count + 1;
    return hf->write(st + ECOUNT, &next, 4) == 0;                           /* ... then the count */
}
```

| Choice | Why | Status |
|---|---|---|
| Write the entry, then the count | Readers bound their loops by the count (0x1836589, the matcher at 0x66DADC). A count raised first would expose a half-written entry for a frame | [SPEC] (single thread anyway: run on the game thread) |
| Template from an idle granted entry (Resupply, SOS...) | It keeps the unknown bytes (+8, +0xA-0xF, +0x28) at values the game made for a granted entry | [SPEC]. The probe shows whether those bytes differ between entries at all |
| Activation = arrival = 0 | The HUD's inbound state needs arrival - clock > 0 (0x18367D4-0x18367F2, 0x1836A30). 0 is never inbound | [STATIC] |
| End = 0 | Available while end <= clock (unsigned, 0x66D25C), and it mirrors 0x66F2E0. "End 0" is not special to the game, only to old Runtime guards | [STATIC] (custom-stratagems.md:1958-1966) |
| Refill instead of a second append | Only one entry per mission. No growth toward 16, no matcher ambiguity between two entries of one type | design |
| New slot index | No in-flight call-in can be keyed to an index the record has never had this mission (in-flight entries match (key, slot index)) | [STATIC] (0x66D278-0x66D294) |

### 2.4 Alternative: calling 0x66F190 (not recommended yet)

What the pins show about the call at 0x8570B5 (from 0x856FE0) [STATIC]:

- edx = the type (0x66F2A7). rcx is presumably the state (r15 there; the count is written at r15+0x788).
- `mov word [rsp+0x28], 0x100` at 0x8570A4. On Windows x64, [rsp+0x28] at the call is the **6th** argument's slot (5th = [rsp+0x20]). Its byte 1 is 1, and the callee stores `al` as the granted flag. So granted is probably the high byte of a 2-byte or struct argument, or a 6th-argument bool next to another byte. [SPEC]
- r8, r9 and the 5th argument are unknown. The uses come from 0x879550 inside the callee: the row's uses, a linked bonus and the player's upgrades.

Before calling it:

1. Disassemble 0x66F190's prologue and its four call sites (0xAD917D, 0xADC5CF, 0x1467A52, 0x8570B5), and write down every argument.
2. Check whether 0x66F190 or 0x856FE0 sends `rpc_sync_stratagems`. The loadout exit sends it, but from 0x1467790 (custom-stratagems.md:505), probably not from the append.
3. Check whether 0x856FE0 raises a HUD notice.

**If 0x856FE0 also syncs peers, calling it** (rather than 0x66F190) **would be the better co-op route.** That is the one reason to do this research. Until then, the hand append is equivalent and safer.

### 2.5 When the record is full

- **Up to 16 entries per record** (`maxEntries`; HUD 16 slots). A typical mission has 4 loadout entries plus the mission defaults (6-type default sets, 0x213F550: HF/native/mod-loader/difficulty_missions.c:87-91). [STATIC]
- **At 16, refuse and log.** Show a Veterancy HUD line ("Veteran Maxigun: stratagem list full"), and keep the grant pending: retry when a count drop is seen, which never happens natively mid-mission.
- **Never overwrite another entry** (an objective's, Reinforce).
- With refill-in-place (2.3) this needs at most one slot for the whole mission.

---

## 3. Expiring it

| Claim | Evidence | Status |
|---|---|---|
| A call sets the entry's cooldown end (0xB9A6D0 / twin 0x135C2C0) | RT/scripts/research_stratagem_rearm_pool.py:92-115; RT/docs/stratagem-uses.md:73-74 | [STATIC] |
| Finite uses are spent per entry ("each equipped slot has its own counter"; Exosuit 3 uses → unlimited is gameplay-proven) | stratagem-uses.md:33-39 | [PROVEN] (finite counts exist and work). The decrement instruction is not pinned |
| An entry with 0 uses is unavailable (the readers 0x66D3D0 / 0x66D3E5). A row with a non-zero cooldown type (+0x94) reads a **shared pool** of uses instead (0x66D40B) | rearm_pool.py:76-79 | [STATIC]. Log the mod row's +0x94: it is copied from the Maxigun's and is expected to be 0 |
| The teammate HUD has a state 5 "unavailable" (cards: ready 1, cooling 4, unavailable 5) | RT/scripts/research_teammate_hud.py:17-20, LAYOUT | [STATIC] |
| Vanilla shows a spent limited stratagem greyed in the list, not removed | community and gameplay knowledge; no public source found for this build | [SPEC] |
| At the mission-end transition the record is rebuilt for the ship (loadout only, reverse order), so the bonus entry disappears with no restore write | research_stratagem_slot_conversion.py docstring 4; custom-stratagems.md:1114-1115 | [STATIC] + snapshots |

**The recommended expiry:**

1. Let the game spend the use. The entry stays at 0 uses ("shows 0 uses") until mission end.
2. Guard it. Subscribe to `HF_ON_STRATAGEM` (fires on the thrower's machine with the type: hellforge_mod.h:508, 609-615).
   - When `type == g_vetType` and the thrower is the local player, check one frame later that the entry's uses reached 0.
   - If they did not (an unpinned decrement path that behaves otherwise), write 0 yourself.
3. On `HF_ON_MISSION_END` / `HF_ON_MISSION_START` (hellforge_mod.h:502-503), reset the mod's state ("granted this mission", the index). Never write the record outside `HF_STATE_MISSION` (`game_state`, hellforge_mod.h:1097).

**Optional "disappear":** write count - 1 only if all of these hold:

- the bonus entry is the **last** one (index == count - 1);
- no in-flight call-in has (key = `st+0x9E8`, slot = that index) with done byte 0;
- its arrival <= clock (the pod has landed).

The record has no removal function in the research, and removing an entry from the middle would shift the indexes that in-flight call-ins and HUD slots key on. [STATIC/SPEC] Prefer "greyed at 0 uses" plus refill.

---

## 4. Co-op and host

| Claim | Evidence | Status |
|---|---|---|
| Beacon activation runs once per beacon, on the thrower's machine (0x6ABDB0) | HF/native/mod-loader/event_sources.c:12-13; HF/docs/modding/concepts.md:137-139 | [PROVEN] |
| Added stratagem types travel as numbers and fit 8 bits (150 + 48 <= 256). Mods are set up in mod-id order, so every machine numbers them alike | stratagems.c:44; HF/docs/engine-roadmap.md:199-204, 224-230; content.md:443-445 | [PROVEN] (shipped; "two load orders give identical types" unit-tested) |
| The modlist code refuses a join when gameplay mods (id, version, files) differ. So the type exists on every peer, provided the mod's manifest is `"gameplay": true` | concepts.md:130-136; UM/mods/field-promotions/hellforge-mod.json | [PROVEN] |
| Added stratagems' beacons and spawns work in co-op through HellForge's network apply switch and behaviour fixes. A two-PC confirmation is "not recorded" | engine-roadmap.md:233-251; HF/docs/multiplayer.md:699-706 | [PROVEN] build; co-op [SPEC] |
| Peer sync: `rpc_sync_stratagems` / `rpc_sync_player_history` / `rpc_sync_stratagem_changes` → 0x11E82D0 writes the **sending** peer's record with raw types. "The only check is the sender's record key: no ownership, selectable or enabled check" | custom-stratagems.md:507, 969; RT/domains/slot_cooldown.lua pins 0x670567, 0x670717, 0x11E8365 | [STATIC] |
| A direct record write sends nothing. "Multiplayer: Not synchronised: a direct write sends nothing; peers keep the token. Solo only" | research_stratagem_slot_conversion.py `determinations.multiplayer` | [STATIC] |
| "The host synchronizes mission entries" / "a host sync replaces the whole record" | stratagem-uses.md:41, 101-102; custom-stratagems.md:1596-1597 | [I] in RT, i.e. [SPEC] |
| A peer's teammate panel shows its own copy of your record (4 cards by entry index), so peers will not see the bonus. That is cosmetic | research_teammate_hud.py:13-24 | [STATIC] |
| No host-side check of a client's call against the host's copy of the record was found. HD2Runtime's custom multiplayer is built on peers running a call whose type is not in their copy of the thrower's entry ("its thrower's Runtime runs it; this machine only observes") | RT/runtime/custom_stratagems.lua:5131-5140, 5213-5215 | [SPEC]. HD2Runtime still marks multiplayer "not supported" (custom-stratagems.md:999-1001) |

**Policy:**

1. **Solo and host: supported.** The host's own record is authoritative for itself. [SPEC, high]
2. **Client: experimental, behind a setting.**
   - The call itself should work: it runs on the client and replicates as a vanilla call.
   - But a host sync could rewrite the client's own record and drop the entry.
   - Add a watchdog in `on_game_thread`, every 0.5 s while granted and unspent. If the entry is gone, re-append it (at most 3 times per mission, logged). If it keeps vanishing, stop and say so.
   - Use `hf->session(mod, &s)` for the role (hellforge_mod.h:455-462, 1088).
3. **Write only the local record**, never another peer's. The next sync overwrites it, and it would be cheating.
4. **What HellForge's mod sync requires (SDK 14):** nothing from this mod.
   - Joiners take the host's mods through the MOD SYNC prompt (multiplayer.md:243-250). `sync_state` / `sync_mod` / `sync_answer` are for UIs that present that offer (hellforge_mod.h:1186-1196).
   - The mod only has to be a gameplay mod with one version on every machine.
   - The per-mod message channel (`send`, `is_authority`) is planned, not built (engine-roadmap.md:245-251). So the mod cannot tell peers about the grant itself.
5. **Diagnostics:** every session writes `coop-diagnostics.log`, which records added stratagems' beacons on the thrower's machine with package residency (multiplayer.md:703-706). Compare both players' files after a co-op test.

---

## 5. SDK calls per step (SDK 15 header; the manifest asks for 13)

| Step | Call | Signature (hellforge_mod.h) | SDK |
|---|---|---|---|
| Define | `add_stratagem` | `uint32_t (*)(HfMod*, const HfStratagem*)`, init only (:999-1001) | 4 |
| Hide from the picker | `stratagem_row` + `read` / `write` | `uintptr_t (*)(uint32_t type)` (:1002-1004) | 4 / 1 |
| Hold the package | `on(HF_EVENT_ENTITIES_READY)` + `load_package` | `int (*)(HfMod*, uint64_t)` (:1073-1079) | 1 / 6 |
| Base address | `module_base("game.dll", NULL)` | `uintptr_t (*)(const char*, size_t*)` (:940) | 1 |
| Pins | `code_matches(game+rva, "AA BB ??")` / `read_code` | `int (*)(uintptr_t, const char*)`: 1 match (:1175-1183) | 13 |
| Record I/O | `read` / `write` | `int (*)(uintptr_t, void*, size_t)` / `(uintptr_t, const void*, size_t)`: 0 on success, 1 unreadable (no crash) (:947-950; HF/docs/modding/hooks.md:69-70) | 1 |
| Game thread | `on_game_thread` | `int (*)(HfMod*, HfTickCallback, void*)` (:1069-1072); check `hf->size` | 6 |
| Events | `subscribe(HF_ON_STRATAGEM / HF_ON_MISSION_START / HF_ON_MISSION_END)` | `int (*)(HfMod*, uint32_t, HfListener, void*, int32_t)` (:1092-1095) | 7 |
| State and role | `game_state`, `session` | (:1097, :1088) | 7 / 6 |
| Hooks | none needed. `hook_ex` only if you later hook 0x856FE0 to watch the game's grants | (:1174) | 13 |

**Pinning raw addresses** (HF/docs/modding/hooks.md:80-110):

- HellForge runs exactly build 25480438, so `game + rva` is stable. `api->build_id()` returns "25480438" (hellforge_mod.h:931); refuse any other build.
- Check before touching. `code_matches` sees through HellForge's hooks. `find_pattern` and `read` see bytes as they are now.
- **Code decodes late.** The game's code is decoded by its copy protection up to about 60 s after start. HellForge's own sources wait for their bytes to "read as reviewed" (event_sources.c:3, 22; abilities.c:885). So check the pins at the first `HF_ON_MISSION_START`, or retry for 60 s after `HF_EVENT_GAME_LOADED`, not once at load.

Pins to check, as reader bytes. All are mid-function data readers, so `code_matches` or `read` both work.

| RVA | Pattern | Proves |
|---|---|---|
| 0x1834AE6 | `4C 8B 0D ?? ?? ?? ??` (expect rel32 `63 83 C4 01`) | the records global |
| 0x1834AED | `41 8B 89 00 D2 02 00` | count +0x2D200 |
| 0x1834B07 | `48 05 90 16 00 00` | stride 0x1690 |
| 0x1834B41 | `4D 8D 69 38` | state +0x38 |
| 0x1836589 | `44 3B B7 88 07 00 00` | the HUD bounds slots by count +0x788 |
| 0x183675C | `41 8B 94 CA 88 01 00 00` | the HUD reads entry +0 |
| 0x66F2A7 / 0x66F2CD / 0x66F2D7 / 0x66F2E0 / 0x66F2EB | the bytes in 2.2 | the append's layout |
| 0x66D24A, 0x66D251, 0x66D258 | `48 8B 8E A0 01 00 00`, `48 8B 05 ?? ?? ?? ??`, `48 3B 48 18` | the end vs the clock |
| 0x66D3E5 | `8B 84 CA 8C 01 00 00` | uses +4 |
| 0x66D262, 0x66D278, 0x66D286, 0x66D28D, 0x66D294 | `48 8B 05 ?? ?? ?? ??`, `4C 8B 9F E8 09 00 00`, `4D 39 5C C9 10`, `45 39 54 C9 18`, `41 38 5C C9 01` | the in-flight list |

Bytes are from RT/domains/stratagem_slots.lua and slot_cooldown.lua (`hex` per `rva`). Use `??` for the rel32 parts, then compute the target and compare it with 0x347CE50 / 0x3326348 / 0x3326D98. Any mismatch: disable the feature for the session and log why.

---

## 6. Failure modes and guards

| Failure | Cause | Guard |
|---|---|---|
| Crash on pod landing or content spawn | package not resident on the machine that spawns the rack content | `load_package` at ENTITIES_READY on every machine; log its state at the grant |
| Crash on HUD icon | an `icon` named but not built | leave `icon` NULL, or build it through the data spec (content.md:421-426) |
| Garbage write or a corrupt record | a wrong offset, another build | the pins in section 5; `build_id()`; bounds (records count <= 32, entries <= 16); match the peer id; read before write |
| A half-entry for one frame | count written before the entry | write the entry first, then the count, on the game thread |
| The HUD shows "inbound" or "cooling" on the new slot | a stale activation, arrival or end from the slot's previous use | zero activation and arrival; end = 0 |
| The code calls the wrong stratagem, or none | a code equal to or a prefix of another, or two entries of one type | a unique code (1.3); refill rather than a second entry |
| The bonus never spends, or spends from a pool | the row's cooldown type (+0x94) is not 0 | log it at init; refuse if it is not 0 |
| The record is full | 16 entries | refuse and log; a HUD line |
| The entry is wiped (client) | a host or peer sync | watchdog re-append (bounded) |
| The pick appears in the loadout | the selectable bit left set | clear row +0x80 bit 1 |
| An entry removed while its call is in flight | a count drop during the call | remove only per the rules in section 3, or never |
| Desync on peers | a stale view of the client's record | cosmetic: the teammate panel. The call itself replicates as vanilla. Check coop-diagnostics.log |
| A write outside a mission | the ship record is rebuilt anyway | gate on `game_state == HF_STATE_MISSION` and a pin pass |

---

## 7. Staged in-game test plan

0. **Build.**
   - Manifest `"sdk": 13`, `"gameplay": true`.
   - `add_stratagem` at init: log the type and `stratagem_row`'s +0x50, +0x68, +0x80, +0x94, +0x98/+0xA0 (payload pointer and count), +0xA8 and +0xF8.
   - Expect uses 1, the cooldown you set, the selectable bit set, +0x94 = 0, and payload count 2 with the base's entities.
1. **Probe A, log-only (solo mission).**
   - On `HF_ON_MISSION_START`, then every 2 s, run the pins (section 5) and log the local record:
     - peer, count;
     - per entry: index, type, uses, granted, end - clock (s), arrival - clock (s), and **all 0x30 bytes in hex**.
   - Also log `st+0x9E8` and the in-flight list (count, and each entry's +1/+0x10/+0x18).
   - Expect your 4 loadout types (granted 0) and the mission defaults (granted 1). Note the unknown bytes.
   - Call a Maxigun if you brought one, and watch end and arrival move.
2. **Probe B, the type swap (live-proven mechanism, confidence check).**
   - Debug key: set the **last granted** entry's type to `g_vetType` (for example SOS Beacon), read it back, and restore it on the key again.
   - Expect the HUD slot to redraw with the Veteran arrows and title, and the code to work.
   - This repeats HD2Runtime's proven conversion on this mod's type before trying a count change.
3. **Append (solo).**
   - Debug key → `GrantVeteran`. Expect a new HUD slot with UUUD (or your code), title "Veteran Maxigun" and the Maxigun icon.
   - Enter the code and throw. Expect:
     - a Maxigun hellpod with gun and backpack;
     - `HF_ON_STRATAGEM` with `g_vetType`;
     - the entry's uses at 0 and the slot greyed;
     - the code refused afterwards.
   - Log every frame for 5 s around each step.
4. **Refill.** Press the key again. The same index goes back to 1 use and is callable. The count is unchanged.
5. **Upgrades.** Wire it to the real Maxigun level-up after the `set_live` edits. The new gun has the new fire rate and backpack, and the old one is unchanged (`HF_APPLIES_RESPAWN`: hellforge_mod.h:369).
6. **Edges.**
   - A record with 16 entries, if you can reach it: refused.
   - Grant during a Maxigun call-in.
   - Grant, then die and reinforce: the entry should survive (watch it).
   - Mission end → ship → a new mission: no bonus entry, the mod state reset.
   - Extraction with an unspent bonus.
7. **Co-op (both machines, same mod version).**
   - As host: grant and call. The pod appears for both players and the client can pick up the gun.
   - As client (experimental setting on): grant and call. Watch the watchdog log for wipes. Compare both `coop-diagnostics.log` files and modloader logs.
   - If the host refuses the call or the entry is wiped repeatedly: gate to solo and host.

---

## 8. Open questions

1. **0x66F190's full argument list, and 0x856FE0's role.** When does it run (mid-mission or not)? Does it sync peers or show a HUD notice? This needs a disassembler pass on the decoded game.dll (section 2.4).
2. **Entry +8, +0xA-0xF and +0x28-0x2F.** What are they (blocked or jammer state? a per-entry id?), and does a zero or template value matter? Probe A answers whether they vary.
3. **Where the uses decrement happens on a call**, and whether a refilled entry (uses 0 → 1) needs anything else, such as an "exhausted" flag in the unknown bytes.
4. **Does a client ever receive its own record from the host mid-mission?** Which RPC, and when? This decides whether client grants need the watchdog or simply work.
5. **The HUD's ordering of granted versus loadout entries,** and whether a 0-use entry is drawn greyed or hidden.
6. **The Maxigun row's +0x94 cooldown type** (expected 0).

## 9. Community check

- No public mod or Cheat Engine table was found that **appends** a stratagem mid-mission.
- Public HD2 tables offer "infinite stratagems / instant cooldown" toggles (vgtimes table 97466, game 1.8.45184.0). They act on uses and cooldown in client memory, consistent with sections 2-3, but their scripts are not public.
- The wiki confirms that some mission stratagems are "temporarily provided ... as part of a mission objective" and need the player in an area. Nothing shows how the game adds them. [SPEC: that may be the 0x856FE0 path]

## 10. Credits and sources

**HD2Runtime** 0.50.1 by SkyeShade, https://github.com/SkyeShade/HD2Runtime. Its research is reused here, and a published mod using these offsets must credit HD2Runtime visibly (RT/LICENSE:16-24, RT/README.md:140-143). Files drawn on:

- **Docs:**
  - `docs/custom-stratagems.md`: the loadout picker table, :502-508; the record and writers, :1097-1115; the readers, :1117-1126; codes, :1580-1599; the live tests, :1540-1550 and :1805-1814; the cooldown end, :1958-1966; sync, :2398-2402; multiplayer, :999-1001;
  - `docs/stratagem-uses.md`.
- **Domains:** `domains/stratagem_slots.lua`, `domains/slot_cooldown.lua`, `domains/stratagem_authoring.lua` (the Maxigun row: payloads, package, code, cooldown).
- **Scripts:** `scripts/research_stratagem_slot_conversion.py`, `scripts/research_slot_cooldown.py`, `scripts/research_stratagem_rearm_pool.py`, `scripts/research_teammate_hud.py`.
- **Runtime:** `runtime/custom_stratagems.lua`.

**HellForge** by SkyeShade, https://github.com/SkyeShade/HellForge (the SDK and docs):

- **SDK headers:**
  - `native/mod-sdk/hellforge_mod.h`: HfStratagem :213-251; the API :925-1196; events :501-529, :609-615;
  - `native/mod-sdk/hellforge_content.h`: :369, :824-833.
- **Loader source:** `native/mod-loader/stratagems.c` (row fill :364-400; layout :1-44), `stratagem_categories.c:126`, `event_sources.c:1-30`, `difficulty_missions.c:87-96`, `actions.c:544`, `abilities.c:885`.
- **Examples:** `examples/mods/stratagem-compendium/src/compendium.c`.
- **Docs:** `docs/modding/content.md` ("Stratagems"), `docs/modding/hooks.md` (:60-110), `docs/modding/runtime.md` (:306-333), `docs/modding/concepts.md` (:130-146), `docs/multiplayer.md` (:243-250, :699-706), `docs/engine-roadmap.md` (:188-251).
- **Mappings:** `mappings/references.json` (:122, :7779-7786, :9029-9037).

**The user's mods repo** (UM): `mods/fire-mission/src/fire_mission.c:315-327` (the add_stratagem pattern); the `mods/*/hellforge-mod.json` SDK levels.

**Community:**

- https://vgtimes.com/games/helldivers-2/files/97466-cheat-engine-table-1-8-45184-0.html
- https://helldivers.wiki.gg/wiki/Stratagem
- https://helldivers.wiki.gg/wiki/Category:Mission_Stratagems
