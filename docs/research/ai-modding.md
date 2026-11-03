# AI modding in HellForge 0.3.0-beta (SDK 15, game build 25480438)

Scope: friendly AI first (sentries, guard dogs), then enemy AI, then the tools each relies on. Every claim is tagged:

- **[PROVEN]**: code that ships and runs on this build (HellForge's loader, SGD 5.2.3, VPM), or a pinned native read in HD2Runtime research that was re-verified against the game.dll image.
- **[STATIC]**: read from the disassembly only, with no live test.
- **[SPEC]**: my inference.

Abbreviations:

- `S` = `<scratch>`.
- `HF` = `S/hf/HellForge`.
- `RT` = `S/ext/hd2rt` (HD2Runtime 0.50.1).
- `SGD` = `S/sgdsrc/sgd/smarter-guard-dogs/src`, a read-only copy unzipped from the user's mods folder.

All RVAs are in game.dll unless marked exe. HD2Runtime's research build "F5FEE03DCFDB" has the same game.dll SHA-256 (2E2C3B7C...) as HellForge's 25480438, so its offsets apply directly.

Licence: anything taken from HD2Runtime internals below (its research offsets and native calls) needs visible credit to HD2Runtime (SkyeShade). HellForge's behaviours.c is HellForge's own code. SGD is chef's mod, ported by giblett.

---

## 0. TL;DR

1. **Sentries and dogs don't "scan the world". They pick only from their perceiver's lists.** A perception record (0x13F8 bytes) holds three lists of up to 16 entries (A, B, C) plus 3 single slots. The perception update fills them with a round-robin gather of at most 8 candidates per refresh. Each candidate's "sense bits" are then set by the sensors: vision rays from SensorEye, and for the 3 mortars a no-ray proximity sphere (SensorProximity). The AI's search stage scores only entries that are perceived, hostile, alive and targetable.
   - So a pinged enemy that isn't in those lists does not exist for the sentry. This explains both observations: ping only re-prioritises known enemies, and an idle mortar ignores an enemy at 60 m.
2. **Nothing in the SDK can add a target.** The game's own primitive is the target setter `0x4AF4E0(ctx, entry*)`. It copies a 0x50-byte perception entry into the behaviour record. Both HellForge (Pelican turret, behaviour 645) and HD2Runtime (the Pelican Gatling proof, behaviour 213) use it in shipping code, but only with entries that are already in the turret's own lists.
   - Writing a fabricated entry into a list, then calling the setter, is the realistic injection route. It is unproven: the next perception refresh may drop it.
3. **What exists now, without new reverse engineering:**
   - **Sensing:** SensorEye range/side/rear (spawn-time) and turret turn speed and limits, via `set_live`.
   - **Hiding targets (SGD's method):** blank a perception entry's sense bits (+0x4C) so the AI ignores it.
   - **Forcing a re-pick (SGD's "kick"):** write the behaviour record's re-pick time, P+0x90 (record +152), to "now".
   - **Ending fire on a lost target ("loss cut"):** write record +408 to `now - 1 s`.
   - **Re-aiming a direct-fire gun:** the targeting aim override (HellForge's AimLow).
   - **Friendly-fire control:** cancel damage in `HF_ON_DAMAGE_PRE`.
   - **"Fire at point" without the AI:** `spawn_projectile` / `spawn_explosion`. Fire Mission already does this.
   - **Reading pings:** game+0x3326450 (SGD).
4. **Needs reverse engineering:**
   - **Engagement constants:** the score curves, the 100 m / 50 m / 125 m caps, the fire cones and the re-pick timings are immediates in each behaviour's code, not data.
   - **Armour priority:** has to go into the steering layer, as SGD does.
   - **Gather, scan rate and list admission:** the perception gather and refresh (type settings world+0xF12D48), and which entities are admitted to the lists.
   - **Mortar range:** SensorProximity radius; HellForge has no mapping for it.
   - **Enemy aggro and target choice.**
5. **Enemy AI is almost entirely unmapped on HellForge.**
   - HD2Runtime's enemy fields answer `HELLFORGE_UNAVAILABLE` there (RT/docs/hellforge.md "Writes" and "Unavailable areas").
   - Usable now: director and difficulty budgets (`HfDifficultySettings`, `add_difficulty`), statuses via `apply_status` (stun 37/38/39, gas confusion 44/45, fire panic 6), `HF_ON_SPAWN`, and damage pre-hooks.
   - Enemies share the same perception and faction system, so SGD-style hiding could in principle make enemies ignore someone [SPEC].

---

## 1. Friendly AI: how a sentry picks and fires

### 1.1 Data path, end to end

| Stage | What | Where | Status |
|---|---|---|---|
| Sensor creation | `SensorEyeComponentData` (component 191, 44 B) is copied into the sensor instance at spawn, × an instance scale. Front range +0, side +4, rear +8 (a negative value means "use front"). Cone half-angles +12/+16 apply only to shape type 2. Node name +20, shape type +32 (1 for all sentries; 3 for Tesla, which uses the front range only). Sensor manager game+0x3326520. | RT/scripts/research_sentry_components.py:184-211 (pins 0x64C4C0, 0x64C3C0, 0x64C46C...) | [STATIC], plus the live test of MG-43 `targeting.range` (LIVE set, line ~297) |
| Vision test | 0x64C6F0. Type 1 blends front/rear range by \|cos\| and the side range by (1-\|cos\|), with no cone. In range when range > distance. A mission-wide sight limit is read at 0x64C8B2 (-1 = none). Sight itself is ray-tested and "flickers", so HellForge allows a 1 s grace. | research_custom_payloads.py:~440-446; HF behaviours.c:345 | [STATIC] |
| Proximity sensor | `SensorProximityComponentData` +0 radius. Every candidate within it gets its sense bit set (`or [entry+0x4C]`) **without a ray**. Only the 3 mortars own one, with radius = 125 = targeting.range. HD2Runtime deferred exposing it; HellForge has no mapping. | research_sentry_components.py:213-224, 845 | [STATIC] |
| Perception record | Manager game+0x3326548: map +0x30, records +0x50, stride **0x13F8**. +0 = sensor count; +8 = sensors, 8 B each, low 5 bits = sense-bit index. **Lists:** A count +0x310 / entries +0x318; B +0x818/+0x820; C +0xD20/+0xD28; max 16 each, 0x50 per entry. Singles at +0x1228, +0x1278, +0x12C8 (valid when entry+0x48 ≠ 0). +0x13AC = gather cursor, +0x13D8 = next refresh time, +0x13E8 = the perceiver's own faction mask. | HF behaviours.c:58, 439-450, 478-487; RT domains/pelican.lua `targetSet.perception`; SGD sgd_read.c:736-737 | [PROVEN] (read by HellForge's turret and by SGD) |
| Perception entry (0x50) | +0x00 entity id; +0x04 last-known position (3 floats); +0x10 last-sensed time (u64, µs); +0x30 (=48) "line of sight now" byte (SGD); +0x40 (=64) memory float, 1→0 over about 1 s (SGD); +0x44 score (f32); +0x48 flags (bit 0 = alive/valid); **+0x4C sense bits**. "Perceived" means `(sense & sensor-mask) != 0`. | SGD sgd_read_perc.c:112-262; RT pelican.lua `entry*` | [PROVEN] |
| Refresh and gather | A perceiver refreshes only when +0x13D8 ≤ now; next = now + uniform(S+0, S+4) s, where S comes from the perception type settings (world+0xF12D48, 0x27E slots, 0x18-byte records). The gather is a **round-robin cursor over all faction entities, at most 8 candidates per refresh** (0x887340, 0x8875B4). The perception update is 0x887760; the proximity handler is 0x886A70 → 0x64FD20. | research_custom_payloads.py:429-433 | [STATIC] |
| Hostility | The target's faction mask (game+0x3326CB8: map +0x12050, masks +0x12078, u32 each) must be non-zero and share no bit with the perceiver's +0x13E8. | HF behaviours.c:59, 463; research_pelican.py:747-800 | [PROVEN] |
| Entity flags | game+0x3326430: map +0x18, u64 values +0x38. This looks like the live tag mask (HD2Runtime's no-credit tag 0x10000000 lives here). Bit 25 = invalid (the refresh clears such a target); **bit 35 = never scored/targeted**. | HF behaviours.c:60, 461; research_pelican.py:~775 | [PROVEN] (as a filter) |
| Behaviour record | Manager game+0x3326740: map +0x40, records +0x60, stride 0x1F8; P = record + 8. P+0 stage; P+0x10..0x5F = a **copy** of the target entry (P+0x10 is the target id); P+0x60 = the perceiver key; P+0x68 = previous target; P+0x70 = has-target; **P+0x90 = next re-pick time (µs)**; P+0xA8 = next fire check; P+0x178 = stage start; P+0x180 = stage-3 start; P+0x188 = alert start. Every update with a target, 0x889320 re-copies P+0x10.. from the live perception entry, then refresh 0x4AF390 runs. | research_custom_payloads.py:337-428; research_pelican.py:685-800 | [PROVEN]/[STATIC] |
| Search and score (213 Gatling, the direct-fire template) | Stage 2 re-picks when P+0x90 ≤ now (every 0.25 s, or 1 s once P+0x58 = 0x20). It scores **lists A+B+C (+3 singles)**. The score is the product of: perceived 0/1, hostile 0/1, not dead, has health, targetable (component game+0x3326D20), an angle curve (never below 0.8), and a distance curve (5 m→1, 40 m→0.8, **100 m→0**). The previous target gets ×1.0, others ×0.9 (AC-8 uses ×1.1). Flag 35 → 0. "Class 3" → 0, "class 2" → ×0.5. The best result goes through setter 0x4AF4E0. | research_custom_payloads.py:337-418 | [STATIC] |
| Constants per sentry (code, not data) | MG-43 / G-16 / AC-8 / LAS-98: distance curve ends at **100 m**; FLAM-40 at **50 m**. Fire cone: 3° (MG, Gatling), 2° (AC), 10° (Flame, Laser). Mortars: score 0 below 25 m (M-12) or 14 m (EMS/Gas), falling to 0 at **125 m**; a third segment (M-12 `(50,1)→(51,0)`, Gas/EMS `(40,1)→(70,0)`) has no established role. Behaviour ids: MG-43 312, G-16 213, AC-8 5, M-12 319, GM-17 320, M-23 323, Rocket 611, Laser 308, Flame 207, Tesla 661. SGD also sees ids 319-324 on mortars and 212 on the FRV/pod gun. | research_sentry_components.py:54-66, 274-310, 577-622, 1085-1100 | [STATIC]; the "notData" conclusion is explicit |
| Aim | The targeting system (game+0x3326D30: map +0x150, records +0x178 stride 0xD0, network bits +0x180 stride 0x18, update 0x6BBDC0) aims at the target's aim node. The turret update (0x6DEDB0) slews yaw/pitch at the instance speeds. `TurretComponentData` limits are read live every frame; turn speeds are copied at spawn. Mortars set Targeting +25, which skips the aim-node/lead section. **Aim override:** record +0x20/+0x2C = point, +0x60 = 2 (linear), +0x6C = 0, +0x50 = t (1 = on, −1 = off). The game's own AI action code uses this, and HellForge's turret uses it live. | HF behaviours.c:624-666; research_sentry_components.py:225-273 | [PROVEN] for Pelican turrets |
| Fire | Stage 5 checks the barrel is within the cone (0x4B54D0) every 0.5 s (P+0xA8). Stage 12 pulls the trigger (0x4BC940; release 0x4BC9E0). The game keeps firing where the target was until 1 s after the loss time at record +408 (SGD "loss cut"). | research_custom_payloads.py:~400-418; SGD sgd_read_perc.c:335-356 | [PROVEN] (SGD writes it) |
| Ping state | game+0x3326450: hash at +0x20 (capacity at +8 of the header), rows at +0x40, 24 B each. A pinged entity has row byte +17 == 1 and u32 +8 ≠ 0 (the ping "word"). Read every 0.25 s. | SGD sgd_sentry2_step.c:264-345, 919 | [PROVEN] (shipped, build-gated on behaviours RVA 0x3326740) |

### 1.2 Why an idle mortar ignores a pinged enemy 60 m away in the open

SGD's Ping Targeting (`PingTarget`, SGD sgd_sentry2_step.c:348-376) only looks inside `st.candidates`, which is that sentry's perceiver lists. It then "blocks", by blanking sense bits, every other candidate and "kicks" (P+0x90 = now). An enemy that isn't in the lists can't be chosen. That matches the observation.

Why would a 60 m enemy in the open not be in a mortar's lists, or score 0? These are candidate causes, to separate by testing:

1. **The gather never brought it in** [SPEC, most likely].
   - The gather takes at most 8 entities per refresh, round-robin across *all* faction entities, and the lists cap at 16 each.
   - On a busy map the lists can be full of nearer or other entries, and admission and eviction rules are unknown.
   - The mortar's proximity sensor (125 m, no ray) only sets sense bits on entries **already in the list**.
2. **The unexplained third score segment** [SPEC].
   - M-12's search function loads `(50,1)→(51,0)`, and Gas/EMS load `(40,1)→(70,0)`.
   - If one score factor reads a distance-like input in some state (idle, or no current target), a 60 m candidate would score 0 while a 45 m one would not.
   - Worth a direct test: ping enemies at 45, 55 and 80 m.
3. **SGD's own rules hide it.** SGD hides a candidate when an enemy is at the sentry's feet or next to a helldiver, or when it falls under the mortar min-range or safety rules (`PingTarget` conditions, line 365-370; the threat check, 391-404).

**Diagnostic to write first.** On a ping, read the mortar's perceiver and log the following [feasible now; all addresses above]:

- whether the pinged id is in A/B/C/singles;
- its entry +0x4C against the sensor mask, its +0x44 score and +0x48 flags;
- P+0x90 and the stage P+0.

How to get there:

1. Find the sentry's behaviour record through behaviour manager game+0x3326740 (map +0x40), using the sentry's entity id from `HF_ON_SPAWN`.
2. P+0x60 is the perceiver key.
3. Look the key up in perception map game+0x3326548 (+0x30), as HF behaviours.c:440-450 does.

### 1.3 Mod controls for friendly AI

| Want | How | Status |
|---|---|---|
| Sensor range / side / rear | `hf_live(mod, HF_ENTITY_AM12_MORTAR_SENTRY, SensorEye, range, HF_F32(x), 0)` (also `side_range`, `rear_range`); HF hellforge_components.h:1636-1651. Copied **at spawn**, so it only affects newly placed sentries. Values above the behaviour's score cap (100/50/125 m) do nothing. For mortars, the proximity radius still decides perception. | [PROVEN]: live-tested on MG-43 (RT LIVE set) |
| Turret slew and limits | `HfTurret` pitch/yaw speed (+8/+12, spawn), coupling +16 and limits +20..+32 (live every frame); hellforge_components.h:1653-1681 | [STATIC]; AC-8 speeds live-tested in RT |
| Cone half-angle (shape-2 sensors only) | SensorEye +12/+16 sit in `_unknown_0C`. Sentries are shape 1 (no cone) and Tesla is shape 3, so this does nothing for sentries | [STATIC] |
| Mortar perception radius | SensorProximity +0. **Not in HellForge's SDK mapping** (`sdk_fields.h` has no entry), so `set_live` and `hellforge_lua_set` refuse it ("HellForge has no mappings for", lua_bridge.c:88-96). A C mod can try `api->component_record("SensorProximityComponentData", mortar, &stride)` (hellforge_modloader.c:557-611; the session's component list may include it) and `api->write`. That bypasses live-edit conflict handling, and vanilla records are read-only memory. | [SPEC] |
| Scan rate | No component. The perception refresh interval comes from the perception type settings (world+0xF12D48), and the re-pick interval is a code constant (0.25 s / 1 s). Two levers: (a) **force re-pick**, writing record+152 (P+0x90) = now, as SGD does (sgd_decide_steer.c:330-337; sgd_sentry2_step.c:148); (b) force a perception refresh by writing perceiver +0x13D8 = now [SPEC]. | (a) [PROVEN]; (b) [SPEC] |
| Hide or exclude a target ("never shoot X") | Blank the entry's sense bits (+0x4C, 4 bytes = 0) while the entry still holds the same id, and restore them afterwards. This is SGD's hider (sgd_decide_steer.c:1-160). Per-type and permanent: set tag bit 35 on that enemy type via `HfTag` / `set_tag` [SPEC: the bit is proven as a filter, not as a settable tag]. | [PROVEN] (SGD) |
| Prioritise (by armour, by type) | Steer: hide everything except the wanted target, then kick. This is how SGD does Armor Intelligence and Target Prioritization (sgd_sentry2.c, sgd_armor.c). The native score can't be re-weighted without patching the behaviour code (the score curves live in .rdata/immediates at the RVAs in research_sentry_components.py:274-310). | [PROVEN] (SGD) |
| Inject a target it doesn't know | Option A: write a fabricated entry into the perceiver's list (count < 16) with entity, position, seen = now, flags bit 0, sense = mask, then kick. Option B: also call setter `0x4AF4E0(ctx={handle, P}, entry*)`, as HellForge `Release()`/`TurretUpdate()` do (behaviours.c:584, 719) and HD2Runtime's `native_set_target` (RT runtime/windows_write.lua:364-376, credit HD2Runtime). The setter copies the entry and tail-calls refresh 0x4AF390, which only clears flag-25 targets. Risks: the next gather may overwrite or evict the slot; replication (the setter makes no network call); running on a non-authority machine. Call it on the game thread (`on_game_thread`), only for sentries this machine has authority over (handle +0x14 bit 0). | [SPEC]; the setter itself is [PROVEN] with real entries |
| Force fire at a point or entity (direct fire) | Aim override on the targeting record (HF behaviours.c:624-666), then trigger 0x4BC940 / release 0x4BC9E0 [STATIC]. Simpler and already proven: spawn the round yourself, `spawn_projectile(type, muzzle, dir)` (host, credited to the local Helldiver). The game's AI fire wrapper `FireProjectile 0x13A8F50(_, type, pos, dir, entity, target, 0)` can name the **sentry** as the shooter (the AI helper calls it at 0x119E612; RT docs/events.md:472-485), which would credit its owner [SPEC for a sentry as firer]. | spawn_projectile [PROVEN]; the rest [SPEC] |
| Force fire, mortars | Fire Mission's approach (spawn the mortar projectile, `HF_PROJECTILE_AM12_MORTAR_SENTRY` 346, EMS 154, Gas 342, along a ballistic arc). Making the mortar *visibly* fire needs the behaviour stage machine (M-12 switch 0x339EA0). | spawn [PROVEN]; native [needs RE] |
| Replace the AI entirely | `add_entity` (own_all) on a sentry base with `behaviour = add_behaviour({base = HF_BEHAVIOUR_PELICAN_TURRET, ...})`. HellForge's own picker then runs: sight-gated, nearest, 120 m (`T_RANGE`, a HellForge constant), with burst, reserve and aim height. Only bases 667 and 645 exist, so this is untested on a ground sentry body [SPEC]. Live swap: game `SetBehaviour(manager=[game+0x3326740], entity, id)` at 0x843EA0 (RT pelican.lua `setBehaviour`; HD2Runtime used it for 213↔645 only). | [SPEC] for sentries |
| Stop shooting a lost target sooner | SGD loss cut: record+408 = now − 1000001 µs (sgd_read_perc.c:335-356) | [PROVEN] |
| Friendly-fire avoidance | (a) **Damage-side, SDK only:** in `HF_ON_DAMAGE_PRE`, if the target is a Helldiver and the dealer or owner is a sentry/dog network id (learn network ids from `HF_ON_SPAWN.network`), set `cancel = 1` (hellforge_mod.h `HfDamagePreEvent`). It runs on the machine applying the damage. Status and push may still apply. (b) **Aim-side:** SGD's safety (hide plus a hold-fire ray test). VPM SentryAimRetention uses the engine terrain raycast exe+0x7F4590 (world id via exe+0x79F860) and the native setters retention 0x6BF390 (flag 2), yaw/pitch speed 0x11CBA20/0x11CB930 and fire mode 0x755F90 (S/ext/vpm/components/SentryAimRetention/src/windows_api.lua:20-26, 190-225). | (a) API [PROVEN]; (b) [PROVEN] in those mods |
| Credit kills | `credit_to_player(entity)` (solo host) | [PROVEN] |
| Gatling pen, mortar reach | SGD hard-codes these (pen = 3, reach = 125; SGD sgd_core_data.c:60-75 sentry table). They are SGD's own estimates for decisions, not game data. The real penetration is the projectile's `DamageInfo` (editable via `set_live` on DamageInfo/ProjectileSettings rows). | known |

### 1.4 Guard dogs (AX/AR-23, Rover, K-9, Hot Dog, Dog Breath)

- **Same machinery:** perceiver lists, a behaviour record with re-pick at +152 and stage ("node") 7 = firing, and the same hide/kick steering. SGD treats the dog with the same code (sgd_decide_steer.c header; sgd_read_perc.c:378-505). [PROVEN]
- **Reach (SGD's own numbers):** Guard Dog 32 m `hard_range`, Rover 35 m (sgd_core_data.c:47-57). The stats schema exposes no SensorEye for the dog drones, only the backpack's `DepositComponentData` and damage zones. The dog's own behaviour ids aren't recorded in these sources [needs RE: read P+0 behaviour id of a deployed drone].
- **Dog movement and leash:** unmapped.

### 1.5 The HellForge turret picker as a reference implementation

HF native/mod-loader/behaviours.c:343-757 is a complete, readable re-implementation of turret targeting:

- `Perceiver` reads the record and builds the sensor mask;
- `Acceptable` checks perceived, valid, flag 35, faction, health and range;
- `Pick` takes the nearest, or nearest to the last target, with angle as a tie-break;
- `Track` keeps the target with a 1 s sight grace and lets go after 3 s without damage;
- the aim override, refill and stage driving follow.

It is the best template for a "smart sentry" behaviour in C, with constants at lines 345-358.

---

## 2. Enemy AI

| Topic | What's available | Status |
|---|---|---|
| HD2Runtime enemy controls (`hd2.enemy`, scale, spawner, shield, jammer, hive multipliers, compositions, spawn weights) | **Unavailable on HellForge**: "Enemy fields answer HELLFORGE_UNAVAILABLE until HellForge maps enemies"; spawn_weight/list/status likewise (RT/docs/hellforge.md:43-45, 93-106). They work only outside HellForge. | [PROVEN] (doc) |
| Director and difficulty | `HfDifficultySettings` row (by level): `encounter_points` +0xE4, `guardforce_points` +0xE8, `tech_level` +0xEC, `spawner_cooldown` +0x104, `patrol_interval[3]` +0x2A0 (live), `patrol_cap[3]` +0x2AC (live) (hellforge_components.h:405-460). `add_difficulty` (level 11; `patrol_frequency`, `patrol_cap`, `outpost_scale`, `mission_minutes`...; hellforge_mod.h:300-330). `HfHiveMind.mission_duration` +0x74. | [PROVEN] API; some fields "[now?]" |
| Spawn hooks | `HF_ON_SPAWN` (entity, network, resource, authority) for every entity, `HF_ON_BUGHOLE`, `HF_ON_KILL`. Use them to tag or track enemies and apply effects on spawn. | [PROVEN] |
| Statuses (stun, confusion, panic) | `apply_status(entity, status, buildup 0..1000)`, host. `HF_STATUS_STUN_SMALL/MEDIUM/LARGE` 37/38/39, `GAS_CONFUSION` 44 / `_2` 45, `FIRE_PANIC` 6, `FLAMER_SLOWED` 67. The game's queue is 0x129F170. Buildup is additive against the target's threshold; duration comes from StatusEffectSettings +40 (editable via `set_live`). RT status docs: Gas confusion 5 s, 9 s for `_2`; only `fire` is live-proven in HD2Runtime; Maxigun `stun_medium` was observed at about 1-2 s against a 3 s definition (RT/docs/status-effects.md:212-231). Adding a status to any damage row: `DamageInfo` status slots +44+8n. | API [PROVEN]; effect per status partly unproven |
| What confusion does to AI | Not traced. Presumably it changes the enemy's hostility or target choice for its duration [SPEC]. | needs RE |
| Aggro and target choice | Enemies run behaviours through the same dispatcher (693-entry tables; update 0x472F60, stage 0x48EE50, event 0x4966D0; HF behaviours.c:5-14). They have perceivers in the same manager (RT validation reads "every real AI's perception record", RT/scripts/validate_packaged_runtime.py:3911-4005). Hostility uses the faction masks. So in principle: (a) blank a player's entry sense bits in enemy perceivers, giving stealth/decoy [SPEC]; (b) change an enemy's faction mask (game+0x3326CB8 +0x12078), giving "charm" or infighting [SPEC, risky]; (c) "aggro" a target by writing it into an enemy perceiver and kicking its re-pick [SPEC]. Enemy behaviour code (stage tables, alert, threat) is unmapped. | needs RE |
| Enemy steering | "AI Steering Fix Native 0.1.0" (in the user's mods; README/TECHNICAL_NOTES.md) raises character "hysteresis" (+0xC4) and "sidedness" (+0xC8) in the main exe's character systems (slot base exe 0x027C52E0, stride 424, +32). It is a port of GOHDKHI's AIBehaviorFix and has not been tested in game. | [SPEC] |
| Damage to and from enemies | `HF_ON_DAMAGE_PRE` (set amount or cancel); `HF_ON_DAMAGE`; `HfHealth` component fields per entity resource via `set_live`, where the enemy resource is known (e.g. `HF_ENTITY_HIVE_GUARD`) | [PROVEN] API |

---

## 3. Mechanism inventory (what to build with)

- **HellForge component fields relevant to AI:**
  - `HfSensorEye` (191): range/side_range/rear_range;
  - `HfTurret` (248): turn speeds and limits;
  - `HfBehavior` (284): behaviour_id (read at creation, 0x510BB0), task_cooldown, task_user;
  - `HfTag` (3): tags; `set_tag`;
  - `HfMount` (283);
  - `HfWeaponWindUp` (70);
  - `HfEagle.target_radius`, `HfOrbitalAbility.retarget_interval` (+0x220), `HfSeekingMissile` turn rates;
  - `HfArcSettings.chain_count/max_split`.
  - Not mapped: SensorProximity, Targeting, Detector, Perception.
- **Live edits:** `set_live` / `get_live` (`HF_LIVE_SHARED` for shared records; spawn-time fields need a fresh spawn) and `component_record`, plus the Lua bridge `hellforge_lua_set/get`, which accepts SDK-mapped components only (lua_bridge.c:88-96).
- **Events:** `HF_ON_SPAWN`, `HF_ON_DAMAGE_PRE` (writable), `HF_ON_DAMAGE`, `HF_ON_KILL`, `HF_ON_STRATAGEM` (beacon position), `HF_ON_EXPLOSION`, `HF_ON_BUGHOLE`, `HF_ON_MISSION_*`. There is no ping event; read game+0x3326450 as SGD does.
- **Actions (SDK 11):**
  - `spawn_projectile` (as the local Helldiver);
  - `spawn_explosion`;
  - `apply_status`;
  - `modify_shots` / `homing` (player shots only);
  - `credit_to_player`;
  - `spawn_pelican`;
  - `add_behaviour` (bases 645 and 667; 8 slots total);
  - `add_entity` / `register_entity`.
- **Raw hooks:** `hook_ex` with `code_matches`, `read`/`write`, `on_game_thread`. Candidate hook points for AI work:
  - setter 0x4AF4E0;
  - refresh 0x4AF390;
  - perception update 0x887760 and gather 0x887340;
  - proximity handler 0x886A70;
  - vision test 0x64C6F0;
  - Gatling search 0x2809A0 (stage 2), stage 5 0x2833E0, stage 12 0x2846F0;
  - M-12 stage switch 0x339EA0;
  - behaviour update dispatcher 0x472F60 (HellForge hooks its `ja` sites; chain via `hook_ex`).
- **Native helpers others call:**
  - SetBehaviour 0x843EA0;
  - FireProjectile 0x13A8F50;
  - QueueStatus 0x129F170;
  - explosion request 0x13C0A80;
  - degrees-to-point 0x7582D0;
  - fire gate 0x4B54D0;
  - valid target 0x8858A0;
  - find perception entry 0x8891C0;
  - descriptor 0xFD9D40;
  - terrain ray exe 0x7F4590.

---

## 4. Feasible now vs needs reverse engineering

**Feasible now (SDK plus SGD-proven memory):**

1. **Ping diagnostic and true "ping to engage" for enemies already in the lists.** Read the ping table and perceiver, hide the others, kick. This is SGD's method; use it without SGD to avoid fighting it.
2. **Fire Mission-style direct attacks for anything not in the lists.** Mortar or rocket rounds via `spawn_projectile` from the sentry's barrel position (pose from the scene graph, as HellForge `Root()` does at behaviours.c:524-535).
3. **SensorEye range and turret speed tuning** via `set_live` (spawn-time).
4. **Friendly-fire cancel** in `HF_ON_DAMAGE_PRE`.
5. **Armour or type priority** via hide plus kick (SGD's approach).
6. **Enemy stun and confusion** via `apply_status`; director budgets via difficulty rows.

**Needs work or reverse engineering:**

1. **Injecting unknown targets:** list-entry fabrication and the setter. Prototype on a Gatling (213) first; check that the entry survives one refresh (+0x13D8) and that P+0x10 stays.
2. **The perception gather rules**, plus the refresh interval S (type settings world+0xF12D48), to raise the scan rate or widen admission.
3. **Mortar search internals:** the third curve segment's input, and the SensorProximity write path.
4. **Engagement constants** (100/50/125 m caps, fire cones, the 2 s initial wait, 7 s alert, re-pick 0.25/1 s): patch the immediates or .rdata at the pinned RVAs, or hook the search stage.
5. **Enemy behaviour, aggro and confusion semantics;** HellForge enemy mappings.
6. **Guard dog behaviour ids and movement.**

## 5. Conflicts and cautions

- **SGD is installed** (`sentries=2`, `ping=1`). It writes sense bits, P+0x90 and record+408 every frame for your sentries and dog. A new mod touching the same records will fight it: disable SGD for those sentries or build on top of it.
- **Most AI state lives on the machine with authority.** Behaviour records replicate only some fields (e.g. P+0x178); the setter makes no network call. Gate writes on solo or host and on authority (handle +0x14 bit 0; HF `Refill` uses `ctx->entity + 0x14 & 1`).
- **Pin everything with `code_matches`/byte checks.** All addresses are for game.dll 2E2C3B7C… (build 25480438).

## Patrols and hiding, AI Probe 0.2.1 in game (2026-11-03, build 25480438)

**Navigation records** ([game+0x3326418]: entity hash at +0x30, records at +0x50, 0xA8 bytes each). One
fight, 28 enemies within 60 m, 26 after her; every 2 s each record's float triples measured against her position
while she moved:

| Offset | What it follows | Reading |
|---|---|---|
| +0x1C, +0x54 | the enemy itself (the same distance as its unit) | its own position |
| +0x60 | her, often 0.0 m, and it moves when she moves | **the destination**, her position when set |
| +0x28, +0x78 | her, 0.1 to 4 m off | the path's goal on the navmesh (snapped) |
| +0x6C | her, 0 to 2 m, sometimes 10 m | the next waypoint, or the goal of a sub-path |

After she respawned 120 m away, these fields stayed on her old spot (17 to 27 m from her) until each enemy
picked her again. So **steering a patrol is a destination write or set_destination**, and the test of
`set_destination` (game 0x5B36C0, Shift+Alt+F7) is next: +0x60 should move to the point it was sent to.

**Hiding** (Ctrl+F7: her perception entries' sense bits blanked, re-pick time set to 0, four times a second for
10 s): it did not work. The 25 enemies after her stayed after her the whole time. Each forced re-pick picked her
again, and "see her" climbed back (0 → 4 → 7) between passes. Two notes for 0.2.2:
- Squad members' behaviour records name another entity as their perceiver (+0x68: 624 for six enemies of one
  squad), and their own perception lists are empty: they perceive through the squad's record, which 0.2.1 never
  blanked. 0.2.2 blanks hers there too.
- Her sense bits read 0 in almost every entry, even in the enemies after her, so those bits may not be what
  keeps a target. If 0.2.2 also fails, hiding by perception is a dead end, and a lower detection signature
  (Lenna's Stealth method) is the route.

**0.2.3, forget when unseen** (Yui's idea): the game refreshes her entry while she is sensed, so blanking it loses
the race. Ctrl+F7 now removes her entries outright, but only from a record where none of them has sense bits (the
enemy, or its squad, cannot sense her right now), then forces the re-pick. If they then pick someone else, a hide
that works while out of sight (a sniper nest after breaking contact) is possible. 0.2.2's blanking stays on
Ctrl+Shift+F7 for comparison.

**0.2.3 in game (the same evening):**
- **Forget:** one enemy after her (a squad, perceiver 884). Her entries were removed from the squad's record on
  most passes (it kept re-adding one), the re-pick forced; at t+6 s it stopped being after her, and nobody was
  after her through the end of the watch (t+14 s, 8 s after the forget had stopped). One case, but the first time
  anything lost her. The stall guard stopped it at t+7 s (a 52 ms pass, most likely a wave of new spawns being
  classified at once); 0.2.4 asks about at most 24 new spawns per pass.
- **set_destination:** a squad of three walking toward her (75 m) was sent 80 m to her side, once. Two traces
  later its members were 20 to 29 m from her, after her, their +0x60 on her again. Either the call did not hold, or
  the squad's own target re-set it at once. 0.2.4 sends again every 2 s and logs each one's +0x60 against the point.
- **Gun Probe 0.1.3:** guns spawned beside the view and followed it (532 and 546 frames moved in 10 s); the
  drone_mg unit is the Guard Dog's body without its gun. 0.1.4 tries the support machine guns first.

**0.2.4 in game: set_destination steers a patrol that has not seen her.** Three enemies within 150 m, all "after
nobody". Two stood still with +0x60 on their own position, and set_destination did not change it (guards or idle
enemies; the call only moves ones already walking). The third, walking, had its +0x60 set exactly to the point
(0.0 m) and held there through every resend for 20 s; it walked toward the point (96 m → 87 m from it, a patrol's
pace). When it spotted her it turned "after HER" and +0x60 went back to her (63 m from the point). So:
- **Lemuen's nest works this way:** while she is unseen, patrols heading her way get a destination away from her,
  sent again every few seconds; once one sees her, the fight is normal.
- Standing enemies are untouched (they were not coming anyway).
- The forget (Ctrl+F7) could not be judged: nobody was after her when it ran.
- Passes still stalled now and then (22 to 72 ms): 0.2.5 keeps the enemy check per enemy type, not per spawn.

**Gun Probe 0.1.4:** the support machine gun spawned as a dropped weapon: it fell to the ground first and only then
followed the view (its physics moved it until it rested). 0.1.5 makes its actors kinematic, with collision off.

**0.2.5 in game:** breach spawns lost sight of her at once (smoke) and still came through it, after a plain
forget removed their perception entries. A breach most likely hands its spawns her position (a destination) or a
target directly, so perception is not what drives them. 0.2.6 adds Ctrl+Alt+F7, a hard forget: unsensing enemies
after her also lose the behaviour target (+0x18, +0x78) and are sent to where they stand; and the tally counts
enemies after her with no perception entry for her at all. Gun Probe 0.1.5: Unit.num_actors is not reachable;
Unit.disable_physics went through. The Guard Dog's firing sound (a loop entry) played on the floating models.

**0.2.6 in game: the hard forget works while she is unsensed.** No enemy after her ever lacked a perception entry
for her, so breach spawns are not clairvoyant in the data: what kept them on her was the behaviour target, which a
plain forget left in place. With the target cleared (+0x18, +0x78), her entries removed and the enemy stopped:
- 2 to 3 enemies after her → 0 for the 9 s it ran; after it ended they picked her up again within 1 to 4 s (more
  arriving, some sensing her).
- A breach of 42 within 60 m: 24 targets cleared at once, nobody after her for 6 s; then her sense bits came back
  on (19 → 36 entries sensing her), they were left alone as designed and re-acquired her.
So it holds exactly as long as nothing senses her. For Lemuen: clear unsensing enemies' targets while she is hidden
(scoped, still, out of sight), and let anything that sees her fight as normal. Gun Probe 0.1.5: guns still fell
with disable_physics; 0.1.6 links them to the view camera's unit (World.link_unit) instead.

**0.3.0, auto break-contact** (Ctrl+Alt+F7 toggles): every enemy within 80 m that is after her, four times a
second. While it or its squad senses her, her position is kept as its last sight of her. After 3 s without
sensing her it loses her: entries removed, target cleared, re-pick due, and set_destination to where it last saw
her, so it goes and searches there instead of freezing. The probe now keeps a list of live enemies as they spawn
(enemy types classified a few per pass) instead of walking all 2048 spawns, the cause of the 20 to 70 ms passes.

**0.3.0 in game against bots (Yui's account; the probe's log said nothing, see 0.3.1):** a patrol spotted her and
called a bot drop. With auto break-contact on, once she broke contact the drop's bots despawned after about a
minute. Normally a bot drop or bug breach chases her across the map until she dies at least once. Despawning is
the game's own clean-up of enemies with nothing to do, so the cleared targets and the search at her last seen spot
let the game's own logic finish the job. 0.3.1 logs the counts every 5 s to confirm it.

**Baseline caveat (looked up):** players report that enemies called in by an alert despawn "if they can't find a
Helldiver for a while", mostly solo after breaking line of sight, often with smoke (gideonsgaming.com, "the biggest
mistake you're making in Helldivers 2: running away"); the wiki gives no timer for it. So a despawning bot drop is
not proof by itself. 0.3.2's toggle cycles off → on → observe only: observe logs the same counts and touches
nobody, for a side-by-side of how long they stay after her with and without it.

**Correction (0.3.4):** 0.3.0 to 0.3.3's auto pass never reset the per-pass budget for classifying new enemy types,
so its live list stopped growing after the first four types and break-contact acted on nobody. The HUD showed 0
within 80 m with enemies on the minimap. **So the bot drop that despawned in 0.3.0's run did so on its own** (the
game's give-up, see the baseline caveat above), and break-contact has not been tested yet.

**0.3.4 in game, on vs observe (one mission, same area; averages per pass every 5 s):**
- **On (14:48 to 14:51):** three engagements, the first a bot drop (Yui: it gave up easily). Each time, once the enemies after her stopped sensing her, they
  lost her (7, 7 and 5 of them) and "after her" was 0 within the next 5 to 10 s. Example: 7.0 after her, all
  sensing → 4.5 after her, none sensing (7 lost her) → 0.
- **Observe (14:51 to 14:54):** one longer engagement. "After her" rose to 9.7 and then stayed up for about two
  minutes; from 14:52:57 nobody was sensing her at all (sensing 0.0) and still 3.7 → 2.2 → 1.0 → 1.4 → 0.6 were
  after her for 40 s more before it reached 0 (dying, despawning or giving up on their own).
So unsensed chasers drop off in about 5 s with break-contact on, against 40 s or more without it. One mission, against
bots (Yui: in observe they chased unreasonably after contact broke, even where they should not have been able
to follow), different fights: a direction, not a measurement. Next: the same fight shape both ways.

**0.3.4 again, bots, a bot drop in each phase (2026-11-04 00:18 session):**
- **Observe (17:23:46 to 17:26:33):** from 17:24:41 for 55 s, 4 to 7 bots were after her with **nobody sensing her
  (0.0)**, and they kept coming: at 17:25:37 they had found her again (6.0 sensing). Yui: without it they "give
  chase and pretty much know where you are across the map". A second wave reached 13.6 after her.
- **On (17:26:34 on):** 6.6 after her, 5.1 unsensed → 9 lost her → **0 within 5 s**. 9.1 after her → 10 lost her →
  0.1 within 5 s. Every later group of 1 to 3 unsensed chasers dropped to 0 in 5 to 10 s.
**Conclusion: break-contact works against bots.** Without it, bots off sight keep a target and track her; with it,
they drop her once unsensed for 3 s. Still open: bugs and Illuminate, co-op (other Helldivers), defence objectives.

**0.3.4, Illuminate then bugs (2026-11-04 00:54 session; observe, then on, each mission):**
- **Illuminate, observe:** groups of 5 to 12 stayed after her with nobody sensing her for 15 to 50 s at a time
  (17:59:30, 18:01:02 to 18:01:53, 18:04:21). **On:** a wave of 27 after her (22.7 unsensed) → 23 + 33 lost her →
  0.5 after her within 10 s; every later group cleared the same way. **Works, as for bots.**
- **Bugs, observe:** from 18:14:29, about 10 after her with **nobody sensing her for almost two minutes**.
  **On (18:16 to 18:25):** they lost her (5 to 18 every 5 s), yet 4 to 12 were after her again, still unsensed,
  through most of 18:18 to 18:24, and only from 18:24:35 did it stay at 0. Bugs **pick her up again without sensing
  her** (or breaches keep handing her to new spawns). So for bugs break-contact only thins the chase.
Next for bugs: count, per enemy, how often one that lost her is after her again within a few seconds without
sensing her, and log where it came from (new spawn, its squad's perceiver, a breach), to find what re-targets them.
