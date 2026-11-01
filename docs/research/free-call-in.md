# Free call-ins: giving the player a stratagem mid-mission

> **Correction (see [bonus-stratagem.md](bonus-stratagem.md)):** the 0x856FE0 → 0x66F190 grant path below is not shown to run mid-mission (HD2Runtime found no mid-mission add path for objective stratagems, `custom-stratagems.md:1594-1597`), and state +0x78C is a per-type level-package array, not an entry field. Option 3.2 is worked out there instead, as a hand-written append.

Scope: how a HellForge code mod can give the local player a fresh M-1000 Maxigun hellpod (gun plus backpack) partway through a mission, without spending one of their own call-ins. The goal is the Veterancy System: when the Maxigun levels up, a newly made Maxigun picks up its spawn-time upgrades (fire rate, backpack size). Every claim is tagged:

- **[PROVEN]**: code that ships and runs on this build (HellForge's loader, an HD2Runtime runtime path), or a pinned native read in HD2Runtime research re-verified against the game.dll image.
- **[STATIC]**: read from the disassembly only (HD2Runtime research scripts, pins and generated domains), with no live test.
- **[SPEC]**: my inference.

Abbreviations:

- `S` = `/tmp/claude-0/-home-claude/83df7915-a41e-51e7-8756-7e1e82cec6e8/scratchpad`.
- `HF` = `S/hf` (HellForge, commit b1aef77, 2026-10-26).
- `RT` = `S/research-callin/HD2Runtime` (HD2Runtime 0.50.1, commit a8c5339).
- `REC` = the per-peer stratagem record (section 2).

All RVAs are in game.dll. HD2Runtime's research build "F5FEE03DCFDB" has the same game.dll SHA-256 (2E2C3B7C...) as HellForge's 25480438, so its offsets apply directly.

Licence: the record, cooldown, grant, delivery and beacon offsets below come from HD2Runtime's research (SkyeShade). A mod that uses them needs visible credit to HD2Runtime (see Credits).

---

## 0. TL;DR

1. **The SDK has no "call in a stratagem" or "spawn a hellpod" action, and none is on the SDK roadmap.** The roadmap lists "game functions as SDK calls (spawn, give, ...)" only as a wish (HF/docs/sdk-roadmap.md:86). Anything here goes through `module_base` / `read` / `write` / `hook_ex` against pinned addresses.
2. **A stratagem's availability is plain data in the player's own record, and the game does not police it.**
   - The record has one 0x30-byte entry per stratagem: type +0, uses +4, granted flag +9, cooldown end +0x18 (an absolute game-clock time in µs).
   - An entry is callable while cooldown end <= clock and uses > 0. The HUD and the code matcher read the entries every frame.
   - "No writer checks ownership, or that a type was in the saved loadout" (RT/docs/custom-stratagems.md:1113). [STATIC]
3. **Call-ins run on the thrower's machine.** The beacon activation dispatcher 0x6ABDB0 runs "once per activated beacon, on the machine of its thrower" (HF/native/mod-loader/event_sources.c:12). [PROVEN: HF_ON_STRATAGEM is built on it.] So a call the client makes from its own record makes a real, replicated hellpod, just as every vanilla call does.
4. **Ranking:**
   1. **Refund the cooldown** of the player's own Maxigun entry on level-up: write its cooldown end to "now". Small, local, no new game calls. The player still enters the code and throws, but the call costs them nothing.
   2. **Grant an extra, separate call-in.** Append a granted entry to the record, of a mod stratagem based on 110 with 1 use. The game does exactly this for objective stratagems mid-mission, through 0x856FE0 → 0x66F190. Medium effort: the append function's signature is not yet confirmed.
   3. **A true drop at the player's feet with no throw.** Spawn the Maxigun hellpod through the spawn request 0xFD9710 with a hand-built dispatch context, as `spawn_pelican` does for the Pelican. High effort: it needs new reverse engineering of the hellpod's context.
   - Reject `spawn_pelican`: it has no cargo. Reject raw pickup spawns (unknown initialisation, likely host-only), booster grants (mission start only) and `mission_stratagem` (level 11, mission start only).
5. **Recommendation:** ship option 1 now; make it option 2 if a separate call-in matters for design. Option 1 already meets the actual goal: any new Maxigun call-in makes new entities, so `set_live` edits with `HF_APPLIES_RESPAWN` take effect.

---

## 1. What "fresh Maxigun" needs

| Claim | Evidence | Status |
|---|---|---|
| Spawn-time edits (`set_live` returning `HF_APPLIES_RESPAWN`) apply "when the entity is next made (a spawn, a re-equip, the next call-in)" | HF/native/mod-sdk/hellforge_mod.h:369; HF/docs/sdk-reference.md:163 | [PROVEN] (SDK contract) |
| So any new Maxigun hellpod, from any call path, carries the upgrades. The call does not need to be "free" in the game's eyes, only free to the player | follows from the row above | [SPEC], high confidence |
| The Maxigun hellpod is a rack entity 0x73B63D637973C7A4 (HellpodRackComponentData: 8 slots, spawn_count +556). Its items are the gun 0x43A58CB89CFA197C and the backpack 0x056DE1C5E21E723E | HF/docs/sdk-reference.md:1190-1203; brief | [STATIC] |
| The rack spawns its items about 0.5 s after the pod lands, on the authority, writing each item's network id into its slot | RT/runtime/support_pods.lua:5-10; RT/domains/support_delivery.lua pins 0x6D9321, 0x935A97 | [STATIC] |
| A call-in's packages are loaded at mission load for every record entry's type (0x1753330 → 0x1753080: row +0xA8, +0xF8 weapon key...). A Maxigun the player brought, or a teammate's, is therefore resident | RT/scripts/research_stratagem_slot_conversion.py "5b"; RT/docs/custom-stratagems.md "Assets [O]" | [STATIC] + snapshots |

---

## 2. The player's stratagem record (the state every option touches)

| Item | Where | Evidence | Status |
|---|---|---|---|
| Records array | `recs = *(game+0x347CE50)`; records of 0x1690 bytes, `{u64 peer, ...}`; count u32 at `recs+0x2D200` (32 places) | RT/docs/custom-stratagems.md:1099-1103; RT/domains/stratagem_slots.lua `record`; HUD pins 0x1834AE6 (`mov r9,[rip]` → 0x347CE50), 0x1834AED, 0x1834B00, 0x1834B07 | [STATIC] |
| Local peer id | `*(u64*)(*(game+0x347CEF0) + 0xB398)` | HF/native/mod-loader/event_sources.c:9, 25-26 | [PROVEN] (HellForge's player events) |
| Stratagem state | `st = rec + 0x38`; entries at `st+0x188`, 0x30 each, at most 16; count u32 at `st+0x788`; call-in key at `st+0x9E8` | RT/scripts/research_stratagem_slot_conversion.py:9-11, 60-62 | [STATIC] |
| Entry | +0 type (u32), +4 uses (u32; 0xFFFFFFFF unlimited), +9 granted (u8: 1 defaults/grants, 0 loadout), +0x10 activation (u64), +0x18 **cooldown end** (u64 µs, absolute), +0x20 arrival (u64) | RT/domains/slot_cooldown.lua `entry`; RT/domains/stratagem_slots.lua `entry`; HUD pins 0x183678D, 0x1836798 | [STATIC] |
| Game clock | `*(u64*)(*(game+0x3326348) + 0x18)`, µs (1,000,000 per s, constant at 0x23C7EC0) | RT/domains/slot_cooldown.lua `clock`; also read by HF/native/mod-loader/actions.c:544 (Pelican hover) | [PROVEN] (HellForge reads it) |
| Availability | unavailable while cooldown end > clock (unsigned compare, 0x66D24A-0x66D25C); uses read per entry (0x66D3E5); in-flight call-ins matched by (key, slot index) in `[game+0x3326D98]` (0x66D262-0x66D294) | RT/scripts/research_stratagem_slot_conversion.py:110-119 | [STATIC] |
| A call | 0xB9A6D0 (twin 0x135C2C0) sets cooldown end = arrival + effective cooldown (row +104). Changing the **row's** cooldown later does not move an end that is already set | RT/docs/stratagem-uses.md:73-74 | [STATIC] |
| Mission start | every entry holds one shared non-zero end time at mission start, granted ones included; "end 0" means nothing special | RT/docs/custom-stratagems.md:1958-1966 | [STATIC] + snapshots |
| Uses at launch | uses are copied from the row when the record is built (0x66EFD0 / 0x879550); later row edits do not change the counter | RT/docs/stratagem-uses.md:36-38, 71-72 | [STATIC]; the Exosuit 3 → unlimited change is gameplay-proven |
| HUD | each slot takes its entry's type every frame (0x183675C), rebuilds arrows and icon on a type change, and computes "cooling" from end - clock (0x18368EA-0x1836908) | RT/docs/custom-stratagems.md:1120-1123; RT/domains/slot_cooldown.lua HUD pins | [STATIC]; arrows live-proven in HD2Runtime 0.5.0 |
| Co-op sync | `rpc_sync_stratagems` (handler 0x670567, writes the **sending** peer's record at 0x670717). The sender sends each entry's **remaining** cooldown (0x11E8699-0x11E86AD); the receiver adds its own clock (0x11E837F). A shared cooldown type (row +0x94 = 2) is copied into other records' entries of that type (0x11E84A0) | RT/domains/slot_cooldown.lua pins | [STATIC] |
| Who writes it | 0x66EFD0 clears and appends the mission defaults; 0x66F190 appends (type, uses, granted flag, end 0); the ship sync, loadout handler 0xAD9020 and loadout-screen exit rebuild it; **mission systems append granted stratagems through 0x856FE0** (call 0x66F190 at 0x8570B5, with `word [rsp+0x28] = 0x100` at 0x8570A4: granted 1); the peer sync writes received entries (0x11E82D0) | RT/scripts/research_stratagem_slot_conversion.py:12-16, 63-93 | [STATIC] |

Community corroboration: Cheat Engine tables for HD2 ship an "Infinite Stratagems + Instant Cooldown" toggle (vgtimes table 97466, game 1.8.45184.0). That points to per-player uses and cooldown being plain client memory that the game honours. [SPEC: the table's script is not public, so its mechanism isn't confirmed.]

---

## 3. Options

### 3.1 (b) Refund the Maxigun's own call-in: cooldown end = now (rank 1)

**What:** on level-up, find the local record, find the entry whose type is 110 (`HF_STRATAGEM_M1000_MAXIGUN`), and write its +0x18 to the current clock. If its uses (+4) are finite, add 1. The HUD shows it ready in the same frame. The player calls in a fresh Maxigun pod at no cost.

| Claim | Evidence | Status |
|---|---|---|
| Writing end <= clock makes the entry callable | availability compare at 0x66D24A-0x66D25C | [STATIC] |
| The HUD follows with no HUD write | 0x18368EA-0x1836908, state 4 only while cooling left > 0 (0x1836BEA) | [STATIC] |
| The Maxigun's uses are unlimited (0xFFFFFFFF), so only the cooldown matters | RT/docs/stratagem-uses.md:22-26 ("every other non-Eagle stratagem" unlimited) | [STATIC] |
| The call that follows is a normal vanilla call, so it spawns, replicates and syncs as usual. Activation runs on the thrower's machine (0x6ABDB0) | HF/native/mod-loader/event_sources.c:12 | [PROVEN] (HF_ON_STRATAGEM fires there) |
| In co-op, the host's copy of a client's entry stays stale until the client's next `rpc_sync_stratagems`. Nothing on the host gates the client's throw | sync pins above; "no writer checks ownership" | [SPEC] |

- **Co-op/host:** the record is per peer and owned by its peer. Write only the **local** peer's record (match `rec+0` to the local peer id). It works as host and as client [SPEC]. Never write another peer's record: the next sync overwrites it, and it would be cheating.
- **Shared cooldowns:** if the Maxigun row's `cooldown_type` (+0x94) is not individual (0), a refund may also show on teammates' entries of that type after a sync. Read it once and log it. [SPEC]
- **Effort:** about 60 lines of C. No hook, no game call, one 8-byte write per level-up.
- **Risks:** low.
  - A wrong offset reads garbage. Pin the HUD reader bytes with `code_matches` (0x1834AE6 `4c8b0d6383c401`, 0x1834B07 `480590160000`, 0x66D24A `488b8ea0010000`) and refuse on a mismatch.
  - Writing while a Maxigun call is inbound only lets the player call again early.
  - Run on the game thread (`on_game_thread` or an event callback).
- **Limits:**
  - The player must still enter the code and throw.
  - If they never brought the Maxigun (picked up a teammate's), they have no type-110 entry to refund. Use option 2, or skip.

Sketch (all offsets [STATIC], from HD2Runtime research):

```c
static uintptr_t LocalMaxigunEntry(uintptr_t game)
{
    uintptr_t recs = 0, peerObj = 0; uint64_t me = 0; uint32_t n = 0;
    if (hf->read(game + 0x347CE50, &recs, 8) || !recs) return 0;
    if (hf->read(game + 0x347CEF0, &peerObj, 8) || hf->read(peerObj + 0xB398, &me, 8)) return 0;
    if (hf->read(recs + 0x2D200, &n, 4) || n > 32) return 0;
    for (uint32_t r = 0; r < n; r++) {
        uintptr_t rec = recs + (uintptr_t)r * 0x1690; uint64_t peer = 0; uint32_t count = 0;
        if (hf->read(rec, &peer, 8) || peer != me) continue;
        uintptr_t st = rec + 0x38;
        if (hf->read(st + 0x788, &count, 4) || count > 16) return 0;
        for (uint32_t i = 0; i < count; i++) {
            uintptr_t e = st + 0x188 + (uintptr_t)i * 0x30; uint32_t type = 0;
            if (!hf->read(e, &type, 4) && type == HF_STRATAGEM_M1000_MAXIGUN) return e;
        }
    }
    return 0;
}
/* on level-up, game thread: */
uintptr_t clock = 0; uint64_t now = 0; uintptr_t e = LocalMaxigunEntry(game);
if (e && !hf->read(game + 0x3326348, &clock, 8) && !hf->read(clock + 0x18, &now, 8))
    hf->write(e + 0x18, &now, 8);
```

### 3.2 (a) Grant a separate one-use call-in mid-mission (rank 2)

**What:**

1. At init, use `add_stratagem` with base 110: its own name ("Veteran Maxigun"), a code that clashes with nothing, `uses = 1`, `cooldown` small, and the payload left as the base's (the same rack, the same package).
2. On level-up, append an entry of that type to the local record with the granted flag set. It shows in the mission group like Reinforce, Resupply and objective stratagems.

| Claim | Evidence | Status |
|---|---|---|
| The game grants stratagems mid-mission by appending to the record: 0x856FE0 → 0x66F190, granted flag 1 | RT/scripts/research_stratagem_slot_conversion.py:14-15, 91-92; RT/docs/custom-stratagems.md:1110. Objective-granted examples: 71 DropoffCargoContainer, 102 MobileCommsRelay (RT/docs/custom-stratagems.md:1592) | [STATIC] |
| Granted entries of any type work through the game's own paths (mission-group defaults) | HF `mission_stratagem` patches the default sets read at 0x6AE35D, "any type is accepted (galactic war effects already grant vehicles this way)" (HF/native/mod-loader/difficulty_missions.c:87-96; HF/docs/internals.md:398); shipped in HF/examples/mods/hell-forge (M-102 FRV) | [PROVEN] at mission start; mid-mission [STATIC] |
| Mod stratagem types (150+) work in records, the matcher and activation | HF/native/mod-loader/stratagems.c:1-35 (339 table sites moved, bounds widened) | [PROVEN] (shipped `add_stratagem`) |
| The matcher keeps **one candidate per type** (0x66DC10). A second type-110 entry would be ambiguous, so use a distinct mod type | RT/scripts/research_stratagem_slot_conversion.py:103 | [STATIC]; which duplicate wins is unknown |
| The append function's arguments: edx = type at 0x66F2A7; uses computed (0x66F2CD); the granted flag from a stack word (0x8570A4: 0x100); rcx is presumably the state | research script pins | [STATIC] partial; full signature [SPEC] |
| A direct write (clone a granted entry such as Resupply's 0x30 bytes, set type, uses = 1, end = now, count + 1) reaches the same readers | readers in section 2 | [SPEC] |
| The peer sync carries the new entry to the others (the receiver writes received entries' types at 0x11E8365) | RT/scripts/research_stratagem_slot_conversion.py:93 | [STATIC] |
| The package: a mod type copied from 110 loads 110's package. It is resident if anyone brought a Maxigun. Otherwise `load_package` first | stratagems.c header (+0xA8 / +0xF8 kept); HF/docs/sdk-reference.md:93 | [STATIC] / [PROVEN] (load_package) |

- **Co-op/host:** same as 3.1. The entry is the local player's, and the throw and pod run on their machine. The peers' HUDs learn the entry only by sync. [SPEC]
- **Effort:** medium. Either confirm 0x66F190's signature in a disassembler and call it, or write the entry directly and verify it in game.
- **Loadout picker:** the mod stratagem becomes pickable in the loadout. To hide it, clear its row's selectable bit (row +0x80 bit 2: RT/domains/stratagem_slots.lua `row.selectable`) through `stratagem_row`. [SPEC]
- **Risks:** medium.
  - At most 16 entries. Refuse at 16.
  - The record is rebuilt at mission end, which drops the grant itself. [STATIC]
  - A badly formed entry could upset the HUD slot code (slots 0x3760 bytes each, 16 of them). Clone a live granted entry rather than build from zeros.
  - Unknown: whether the HUD shows the "new stratagem" notice the objective grants show (that may live in 0x856FE0, not the append).

### 3.3 (d) Call or hook the game's call-in machinery

| Variant | Evidence | Verdict |
|---|---|---|
| Call the activation dispatcher 0x6ABDB0 (manager, element, thrower, vec3* pos, record, type, 4 more) | Signature in HF/native/mod-loader/event_sources.c:101-110. It writes the per-beacon record (state+0x40: payload +0x418, type +0x424, waves, spawn request +0x8D8 = 10), and the beacon manager's update processes it by **element** (RT/domains/beacon_redirect.lua pins 0x6ABB89, 0x6AC03D-0x6AC04B, 0x6AD90F) | [STATIC]. Needs a live beacon element. Calling it with a made-up element would corrupt the manager. Reject |
| Hook 0x6ABDB0 (`hook_ex`) and change `type` for one of the player's beacons | Same mechanism as HD2Runtime's beacon redirect (rewrites element +0xC before activation; dev-only; solo host guards). RT/runtime/beacon_redirect.lua:1-30 | [STATIC]/[PROVEN] in HD2Runtime dev. Still needs a throw, so no better than 3.2 |
| Spawn the hellpod directly through the spawn request 0xFD9710 with a dispatch context | `spawn_pelican` calls 0xFD9710(&out, resource, descriptor) with a 0x58-byte descriptor (pose, context pointer) and a 0x8A0-byte context copied from the world's defaults (world+0xF3EF28) (HF/native/mod-loader/actions.c:18, 38, 504-536). HD2Runtime: it is "the descriptor the game's beacon dispatcher builds for a vehicle's Pelican" (RT/docs/event-scripting.md:295-299). The pod's Transport block reads the context's +0x3D8 content resource, +0x3E4 dispatched type and +0x3F8 beacon entity; the rack reads +0x398 type (RT/domains/support_delivery.lua pins 0x6D9835, 0x6D98BE, 0x6DBF43, 0x938B9A) | Pelican [PROVEN]; hellpod [SPEC]. This is the only route to a drop with no throw. It needs: the hellpod resource the dispatcher passes for kind 2 (payload[1], e.g. 0x73F8498BFFDCF415 per HF/docs/modding/content.md:378), every context member the pod and its content spawn read, and the beacon field set to "none". Host or authority only, like `spawn_pelican` |

- **Effort:** high. It needs a disassembler pass over the dispatcher's kind-2 branch (around 0x6ADF35 and the spawn at 0x6AD919), plus live tests.
- **Risks:** a half-filled context can crash on the pod's landing or content spawn. Co-op replication of a pod with no beacon is unknown.

### 3.4 (c) `spawn_pelican`

| Claim | Evidence | Status |
|---|---|---|
| It sends an empty transport Pelican (hover point only; "no cargo, no associated entity ... no other entity can be spawned this way") | HF/native/mod-sdk/hellforge_mod.h:764-773; RT/docs/event-scripting.md:295-299 | [PROVEN] |
| A Pelican **stratagem** (`add_stratagem`, base of delivery kind 3) carries `payload[0]` as cargo, but that is a call-in, and the cargo is a vehicle | HF/docs/modding/content.md:401-402 | [PROVEN] for vehicles; a rack as cargo [SPEC] |

Verdict: not usable for a free Maxigun without new loader work. Reject.

### 3.5 (e) Spawn the gun and backpack as loose pickups

- **No SDK action spawns an entity.** `HF_ON_SPAWN` only reports spawns. SpawnEntity 0xFDC140 (entity manager, entity id, resource, SpawnInfo*) needs an id that has already been allocated (HF/native/mod-loader/event_sources.c:16-17). [STATIC]
- **Calling 0xFD9710 with the weapon resource** would skip the rack's item setup: the slot network ids (0x935A97), and whatever ammo and pickup state the rack gives. [SPEC]
- **The packages are resident**, but the entities would probably be host- or authority-only, like the Pelican.
- **Verdict:** high risk and unknown initialisation. Reject for now. 3.3's pod gives you the rack's own item spawn for free.

### 3.6 (f) Other routes considered

| Route | Evidence | Verdict |
|---|---|---|
| Booster `granted_stratagem` (Surplus EAT grants type 15) | "At mission start the game looks that type up ... and grants it"; "a granted stratagem's uses are copied when it is granted" (RT/docs/booster-authoring.md:88-91, 121-125) | Mission start only, and only with that booster equipped. Reject for mid-mission |
| `HfDifficulty.mission_stratagem` | default sets read once at 0x6AE35D, level 11 only (HF/native/mod-loader/difficulty_missions.c:87-96) | Mission start only. It does prove granted entries of arbitrary types work |
| Put the Maxigun in the **Resupply** pod (stratagem 33): set its rack slots to the gun and backpack (pickup references, as the editor's Hellpod Payload does), then refund the player's Resupply entry | HF/docs/internals.md:821-831 ("Pods called in after the change carry it"; a shared rack "changes for all of them") | [PROVEN] rack writes; the combination [SPEC]. Changes every player's Resupply until reverted, and takes the team's resupply. A fallback only |
| Raise the row's `max_uses` or cut `cooldown` live | uses are copied at launch; a call sets end from the row at call time (RT/docs/stratagem-uses.md:36-38, 73-74) | Does not refund a cooldown already running. Not enough alone |

---

## 4. Ranking

| Rank | Option | Gives | Proven vs spec | Co-op | Effort | Risk |
|---|---|---|---|---|---|---|
| 1 | 3.1 cooldown refund | the player's own Maxigun call is ready again, at no cost | offsets [STATIC] (HD2Runtime pins); availability rule [STATIC]; call path [PROVEN] vanilla | local record only; host and client [SPEC] | small | low |
| 2 | 3.2 granted extra call-in (mod type, 1 use) | a separate "Veteran Maxigun" in the mission group | grant path [STATIC]; mod types [PROVEN]; append signature [SPEC] | local record, peers by sync [SPEC] | medium | medium |
| 3 | 3.3 hellpod via 0xFD9710 + context | an automatic drop at the player, with no throw | Pelican [PROVEN]; hellpod [SPEC] | host or authority [SPEC] | high | high |
| 4 | Resupply rack swap + refund | a Maxigun from the Resupply pod | parts [PROVEN]; whole [SPEC] | shared rack: affects everyone | medium | medium |
| — | spawn_pelican, loose pickups, boosters, mission_stratagem | — | — | — | — | rejected |

## 5. Recommendation

1. **Ship 3.1 in the Veterancy System's Maxigun level-up handler.**
   - Order: apply the `set_live` edits first, then refund the cooldown, then log "Maxigun level N: call in a fresh one, it carries the upgrade". The Veterancy HUD can show the same message.
   - Gate it: in a mission (`game_state`), the code pins match, and the local record has a type-110 entry. Otherwise log and do nothing.
2. **If design wants a call-in that is clearly separate** (or the player picked up someone else's Maxigun), add 3.2: register "Veteran Maxigun" at init, hide it from the picker, and grant 1 use on level-up. First confirm 0x66F190's arguments in a disassembler; the direct-write fallback clones a live granted entry.
3. **Keep 3.3 as research** for a true auto-drop. Start from HD2Runtime's support-delivery research and the dispatcher's kind-2 branch.

## 6. Minimal test plan (3.1)

1. **Pins and read-only pass (ship).** On `HF_EVENT_GAME_LOADED`, check `code_matches` on 0x1834AE6, 0x1834B07, 0x66D24A, 0x66D251 and 0x66D258 (bytes in RT/domains/slot_cooldown.lua and research_stratagem_slot_conversion.py).
2. **Read-only pass (mission, solo).** Every 2 s, log the local record: peer, count, and each entry's type, uses, granted, end - clock (s).
   - Expect your 4 loadout types (granted 0) plus the mission group (granted 1).
   - Expect end - clock < 0 before any call.
   - Call the Maxigun and see its end - clock jump to about its cooldown.
   - Log the Maxigun row's +0x94 (cooldown type).
3. **The write (solo).** Bind a debug key: call the Maxigun, wait for the pod, press the key.
   - Expect the HUD slot to leave "cooling" at once and the code to work.
   - Call again: a second pod with gun and backpack.
   - Check the log for the `HF_ON_STRATAGEM` type 110.
4. **Upgrades apply.** Level the Maxigun (or force a level) so `set_live` reports `HF_APPLIES_RESPAWN` for the fire-rate and backpack fields. Refund and call.
   - The new gun has the new rate and the new backpack capacity.
   - The old gun, still on the ground, keeps the old values.
5. **Edge cases.**
   - Refund while a Maxigun is inbound.
   - Refund when you have no Maxigun entry: refused and logged.
   - Mission end, then a new mission: the record is rebuilt and nothing is carried over.
   - Steam/HellForge restart.
6. **Co-op (two machines, both running the mod).**
   - As a client: refund and call. Expect the pod on both machines, the gun usable, and the host's HUD for you to update after a sync.
   - Repeat as host.
   - Watch for a host-side refusal or a desync. If one appears, gate to solo or host.

## 7. Open questions

- Is the Maxigun's `cooldown_type` individual? If it is shared, a refund reaches teammates' Maxigun entries.
- When does a client send `rpc_sync_stratagems`: on change, on call, periodically? Does anything on the host check a client's call against its copy of the record?
- What are 0x66F190's exact arguments, and does 0x856FE0 also raise a HUD "new stratagem" notice?
- What does the hellpod dispatch context contain (members past +0x398 / +0x3D8 / +0x3E4 / +0x3F8)? Does a pod with no beacon spawn its rack content and replicate?
- How does the matcher choose between two entries of the same type? This decides whether a granted duplicate type 110 could stand in for a mod type.

## 8. Credits and sources

- **HD2Runtime 0.50.1** (SkyeShade), https://github.com/SkyeShade/HD2Runtime. Research used here (credit required for reuse of its internals):
  - `docs/stratagem-uses.md`;
  - `docs/custom-stratagems.md` ("Mission-time slot conversion": the record, writers, readers; the cooldown-end finding);
  - `docs/booster-authoring.md`;
  - `docs/event-scripting.md` (Pelican);
  - `domains/slot_cooldown.lua`, `domains/stratagem_slots.lua`, `domains/support_delivery.lua`, `domains/beacon_redirect.lua`, `domains/stratagem_calldown.lua`;
  - `runtime/call_ins.lua`, `runtime/support_pods.lua`, `runtime/beacon_redirect.lua`;
  - `scripts/research_stratagem_slot_conversion.py`, `scripts/research_slot_cooldown.py`.
- **HellForge** (SkyeShade), https://github.com/SkyeShade/HellForge:
  - `native/mod-sdk/hellforge_mod.h`;
  - `native/mod-loader/actions.c` (spawn_pelican, 0xFD9710), `event_sources.c` (0x6ABDB0, 0xFDC140), `stratagems.c`, `difficulty_missions.c` (mission grants), `stratagem_sites.h`;
  - `docs/sdk-reference.md`, `docs/internals.md` ("Hellpod payloads", mission stratagems), `docs/modding/content.md` ("Stratagems"), `docs/sdk-roadmap.md`, `docs/engine-roadmap.md`.
- **Community:**
  - Cheat Engine table "Infinite Stratagems + Instant Cooldown", game 1.8.45184.0: https://vgtimes.com/games/helldivers-2/files/97466-cheat-engine-table-1-8-45184-0.html
  - The 2024 infinite-stratagem rejoin glitch: https://dotesports.com/helldivers-2/news/helldivers-2-exploit-lets-players-use-strategems-infinitely-with-no-cooldown. Anecdotal: cooldown and use state lives in per-player records reset by the loadout.
  - I found no public mod that drops a stratagem without a throw.
