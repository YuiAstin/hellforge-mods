# How enemies perceive a Helldiver (build 25480438): research for an always-hidden kit (Ascalon)

Static analysis of the game.dll dump of build 25480438 (`game.dll.25480438.dump`; file offset = RVA), Lost Contact's
source and logs (docs/research/ai-modding.md on `ai-probe`), HD2Runtime's research notes (SkyeShade) and HellForge's
SDK 15. Nothing was run in the game. All RVAs are game.dll.

Labels:
- **PROVEN**: traced in the disassembly here, or a pin another project traced and shipped.
- **FROM-NOTES**: someone's research or test log says so (cited).
- **SPECULATIVE**: my inference; a probe has to confirm it.

Credit: the perception and behaviour record layouts, the EntityAttribute reader and the passive key names are
HD2Runtime's research (SkyeShade); the sentry pins are HD2Runtime's and HellForge's. Credit them if this is reused.

---

## 0. Answers in short

1. **Break contact does not stop re-detection.** Lost Contact only clears what an enemy *remembers* (her perception
   entries, the behaviour target, the re-pick time). It changes nothing in how she is *sensed*. The next perception
   refresh lets the enemy's eye test her again, and if she is in range with a clear ray, the sense bits come back and
   the re-pick (made due at once by the clear) takes her as a target. The logs show exactly this: 1 to 4 s to
   re-acquire after the hard forget stopped, and 19 → 36 entries sensing her in a breach (FROM-NOTES). Running it every
   frame would not help, since it leaves sensed enemies alone on purpose; 0.2.1's every-pass blanking also lost the race.
2. **Detection is a range test, and the range is multiplied by a per-entity visibility value.** The eye test
   (0x64C6F0) computes `effective range = sensor range × detection-range passive × target visibility × sensor
   multiplier × (1.4 when alerted)`. She is seen only when `effective range > distance`. **Target visibility** comes
   from a per-entity record (manager `[game+0x3326540]`, 0x14 bytes) that the game itself sets to **0** to hide an
   entity (state handlers call `SetVisibilityBase(entity, 0.0)`). Writing her base visibility to 0 (or calling the
   game's own setter 0x720350) makes every eye's range 0 for her. This is the cleanest lever (section 2).
3. **Hearing is a noise event with a position and a radius.** `EmitNoise` (0x64A890) takes a noise type, a source
   entity, a position and a loudness. The radius is `loudness × table[type].radius × listener sensitivity × weather`,
   and the heard amount falls off as `(1 − d²/R²) × table[type].intensity`. A listener that hears her gets a
   perception entry for her at the noise position (0x888E50). A gun's noise type is a byte in its WeaponData record
   (+0x84). Noise types 10 to 13 have a **15 m** radius in the game's own table. A suppressed shot that reveals her
   within 15 m can use the game's own hearing (section 3).
4. **Target choice.** The behaviours pick only from perception entries whose sense bits match a sensor (HD2Runtime's
   traced search, FROM-NOTES). With visibility 0 she never gets sense bits, so nothing scores her. Entity flag bit 35
   ("never scored") is a proven filter for sentries but only appears in some behaviours. Faction masks decide
   hostility. Both are riskier than visibility (section 4).
5. **Shooting reveals her by itself.** While a Helldiver fires, the weapon update sets her **visibility floor** to her
   gun's WeaponData +0x88. When she stops, it sets the floor to 0. With base 0, `visibility = max(0, floor)`, so
   she is visible while firing, at `range × floor`. This is the game's own "firing gives you away" rule, and it suits
   the design (section 2.4).

---

## 1. Lost Contact: what break contact actually changes (question 1)

From mods/lost-contact/src/lost_contact.c (main), `BreakPass`, every 0.25 s, for each enemy within `break_range`
(80 m) whose behaviour target (+0x18) is her:

| Step | What it does | Label |
|---|---|---|
| Sensed check | `Senses()`: does any of her entries in the enemy's perception record, or in its squad perceiver's record (behaviour +0x68), have non-zero sense bits (+0x4C)? If so, it notes her position and does nothing else. | PROVEN (source) |
| Timer | Only after `lose_after` (3 s) unsensed does it act. | PROVEN (source) |
| Forget | `ForgetIn`: removes her entries from lists A/B/C (+0x310/+0x818/+0xD20, 0x50 each) and clears in-use (+0x48) on the singles (+0x1228/+0x1278/+0x12C8), in the enemy's own record and its squad's. | PROVEN (source) |
| Target | Behaviour record +0x18 (target) = 0, +0x78 (has-target) = 0, +0x98 (next re-pick) = 0 (re-pick due now). | PROVEN (source) |
| Search | `set_destination(enemy, last seen position)` (0x5B36C0). | PROVEN (source) |

What it does **not** touch: the eye sensors, her visibility, the enemy's alert state (the 0x3326528 state below), the
perception refresh, and the awareness value (entry +0x40, removed with the entry). So:

- **Re-detection is immediate when she is visible** (PROVEN by code path, FROM-NOTES by logs). The gather re-admits
  her (round-robin, at most 8 candidates per refresh, 0x887340), and the eye test (0x64CF30 → 0x64C6F0) plus the ray
  set sense bits. The search stage then re-picks at once, because +0x98 is 0.
  - ai-modding.md 0.2.6: "after it ended they picked her up again within 1 to 4 s".
  - A breach: "her sense bits came back on (19 → 36 entries sensing her)... re-acquired her".
- **Every frame would not hold.** It skips any enemy that senses her, by design. 0.2.1 blanked her sense bits and
  forced re-picks four times a second, and each re-pick took her again (FROM-NOTES).
- **Hearing re-adds her without sight** (PROVEN, 0x888E50, section 3). A shot heard re-creates her list-A entry with
  the noise position and "heard" flags. That is one plausible cause of the bugs' "after her again, unsensed" in
  0.3.4 (SPECULATIVE). The other is ally call-outs: noises in friendly mode write an instigator into the third single
  slot (+0x12C8) of every same-faction listener in range (0x889050; PROVEN as a write, SPECULATIVE as the cause).
- **Verdict for Ascalon:** break contact is the *clean-up* half (it drops targets and memories already held). The
  *never spotted* half has to come from the sensing side (section 2). Run both while she is hidden, with `lose_after`
  short (about 1 s) while hidden.

---

## 2. The sight check and the per-entity visibility (question 2)

### 2.1 The pipeline (PROVEN unless marked)

1. **Perception update** 0x887760 → candidate gather 0x887340 → per candidate, **eye pass 0x886490** (called at
   0x88756F). Per eye sensor of the perceiver, it calls **eye check 0x64CF30**(sensor manager, candidate, sensor, pos,
   alertMul flag, omni flag) at 0x886647.
   - Proximity sensors (0x886A70 → 0x64FD20, the mortar sphere) set sense bits with no range or visibility term
     (0x886BFD). GetVisibility has only three callers (0x64CA08, 0x886E2D, 0x4B5D29), so **an enemy with a proximity
     sensor would still sense her**. Which enemies own one is unknown.
   - The perceiver record (0x13F8) holds sensors at +4+8k: the sensor entity at +4+8k and the sense-bit index at +8+8k.
     This refines the notes' "+8, 8 each".
   - **Alerted boost:** when the perceiver's entity is in manager `[game+0x3326528]` with state (+0x50, u32 per
     record) ≥ 3, and the candidate's entry has awareness +0x40 > 0, the check runs with flag 1. Its range is then ×1.4
     (constant 0x23C6E9C = 1.4) and the facing blend is ignored (front range in every direction). The meaning of
     "state ≥ 3" (alerted, in combat) is SPECULATIVE.
   - **On a failed check** (result 0) the caller clears the entry's line-of-sight byte (+0x30) and that sensor's sense
     bit (`and [entry+0x4C], ~(1<<bit)`), at 0x886655-0x886666.
2. **0x64CF30** looks up the sensor, gets its world position, calls **vision test 0x64C6F0**, and on a hit goes on to
   the ray test. A miss returns 0 at once (0x64D080 → 0x64D1CB).
3. **Vision test 0x64C6F0**(sensors = [game+0x3326520], out16, sensor id, target pos*, target entity [arg5],
   mul [arg6: 1.0 or 1.4], omni [arg7]):
   - The sensor instance is 0x28 bytes (records [mgr+0x58]): front range +0xC, side +0x10, rear +0x14, cone half-angles
     +0x18/+0x1C, shape type +0x24 (matches HD2Runtime). There is also a per-sensor float at [mgr+0x60] + 8·i + 4.
   - **Distance < 1e-6 → result 1** (always seen; irrelevant in practice).
   - **Mission sight limit:** when byte `[world+0x78AF7B0]` is set, the limit is `[world+0x78AF9DC]` × a height term.
     A target beyond it is result 0 (the world pointer is `[game+0x3326340]`). This is weather or fog, mission-wide.
   - **Target visibility `v`:** if the target entity is in the visibility manager `[game+0x3326540]`,
     `v = GetVisibility(target)` (0x720530), else `v = 1.0` (0x64C946-0x64CA0D).
   - **Range per shape:** type 3 (0x64C9C7) front range; type 2 (0x64CA6B) front range plus the cone test; type 1
     (0x64CC1F) the front/rear blend by |cos| plus the side range by (1−|cos|), or the front range alone when omni.
     Each then applies the **detection-range attribute**: `EntityAttribute(target, 0x14ECCE15, flags 3)` (0x11D9DF0),
     at 0x64C9D9, 0x64CAC3, 0x64CC3C and 0x64CDE8. The modifier is applied as Set/Add/Multiply **only when its value is
     > 0** (`comiss value, 0; jbe skip`). **A multiplier of 0 is ignored; use a tiny positive value.**
   - `effective = range × attr × v × sensorMul × arg6`, stored at out+8. Then:
     - `effective > distance` → in range (out = 2);
     - `effective × 0.3 > distance` → near (out = 1, out+8 ×0.3);
     - else out = 0. out+4 = distance, out+0xC = v.
4. **Awareness** 0x886C80 (called from 0x887D6D/0x887DA5/0x887DDD), per entry (0x50):
   - with LOS byte +0x30 set: `+0x40 += clamp(v, 0.5, 5.0) × a × b`;
   - without it: `+0x40 −= decay`;
   - clamped to 0..1.
   - So visibility also sets how fast an enemy that *can* see her becomes sure of her, never slower than 0.5×.

### 2.2 The visibility record (PROVEN)

Manager `[game+0x3326540]`. Its hash map has the same layout as Lost Contact's `Lookup`, with the header at **+0x20**
(entries +0x20, capacity +0x28, empty key +0x2C, multiplier +0x30). Records are at **[mgr+0x40]**, **0x14 bytes**
each, indexed by the map value:

| Offset | Field | Set by |
|---|---|---|
| +0x00 | **base** visibility (from VisibilityComponentData +0) | init 0x720140; `SetBase` 0x720350(_, entity, xmm2); reset-to-data 0x7203F0 |
| +0x04 | A (starts 0) | `SetA` 0x720210, from avatar/character code reading data +0x64 / +0x6C / +0x70 (0xA50F24, 0xA519DB, 0xA72B7A, 0xA85C6E, 0xA9B983) and a thunk 0x11A63D0. What A means (stance, movement) is SPECULATIVE |
| +0x08 | B (starts 1.0) | `SetB` 0x7202B0 (no direct caller found) |
| +0x0C | **floor** | `SetFloor` 0x720490: the weapon update 0x83E200 (0x83EC66 / 0x83EC7C) and 0x616AC0 (0x617427) |
| +0x10 | darkness factor (VisibilityComponentData +8) | init 0x720140 |

**GetVisibility 0x720530**(_, entity):

```
v = base × max(A, B) × statusFactor × weather × lerp(1, darkFactor, darkness)
v = max(v, floor)
```

- `statusFactor` is 0.5 when 0x69B1F0(`[game+0x3326620]`, entity, 0x24) is true; this is probably a status effect.
- `weather` is `[world+0x78AF9E0]` when byte `[world+0x78AF7B0]` is set.
- `darkness` is `[world+0x78AE5BC]`.

**The game's own hiding:** several state handlers set base to 0 on one event and back to 1.0 on another.

- 0x10AE1B0 / 0x10B4810: hash 0x6E7BBE99 → base 0.0; 0x10AE210 / 0x10B5050: hash 0x36E88BE9 → 1.0.
- 0x10B443F: state 0x41 → 0.0.
- 0x7F2370: a boolean → 0.0 or 1.0.

So base 0 is a state the game already uses and handles (PROVEN). What those states are (in a vehicle, a hellpod, a
cloaked unit) is SPECULATIVE.

### 2.3 The detection-range passive (Scout / Reduced Signature) (PROVEN)

- Key **0x14ECCE15** (`enemy_detection_range`): Scout ×0.7, Reduced Signature ×0.6 (HD2Runtime catalogue,
  FROM-NOTES).
- Five readers in all: the 4 vision-test sites above (flags 3: armour slot first, then the helmet/second slot) plus
  0x87A25B.
- It is per player, through her customization record; HellForge's `set_passives(self, HF_PASSIVE_KIT, 32)` is what
  Lenna uses.
- The value itself lives in the **shared** passive object, the customization manager `[game+0x33264F8]` +0x20, as a
  16-byte modifier {key, type, f32 value, text}. A near-0 value means editing Scout's or Reduced Signature's shared
  row, which affects every wearer of that passive. Solo, that is only her. In co-op it is also a teammate wearing it.
- A value of 0 is ignored (see 2.1). Section 5 ranks it below the visibility record.

### 2.4 Firing reveals (PROVEN, values unknown)

The weapon update 0x83E200 (called from the systems pass 0x5737B1) reads the wielder's WeaponData record (0x509A40,
stride 0x4D0):

- while firing: `SetFloor(wielder, WeaponData+0x88)`; otherwise `SetFloor(wielder, 0.0)`;
- each shot: `EmitNoise(type = WeaponData+0x84 (u8), source = wielder, pos, loudness 1.0)` (0x83F1CE, 0x83F2CF).

SDK 15's HfWeaponData has +0x70 `suppressed` and leaves +0x71..+0x8B unknown, so **+0x84 noise type** and **+0x88
reveal floor** are new mappings. Whether a suppressor attachment changes +0x84/+0x88 (a different record, or the
+0x70 flag read elsewhere) is SPECULATIVE. The probe reads them.

So with her base at 0, a loud gun with floor 1.0 makes her fully visible while she fires. A floor of 0.3 makes her
visible to an eye with 50 m range only within 15 m. The game's own floor is reset to 0 when she stops, so "hidden
again N s after the last shot" needs the mod to hold the floor up for N s (write +0x0C each tick, or call 0x720490)
(SPECULATIVE that the game's per-update write does not override it within the same frame; probe it).

---

## 3. Hearing (question 3) (PROVEN unless marked)

**EmitNoise 0x64A890**(_, dl = type, r8d = source entity, r9 = position*, arg5 = mode (0 hostile listeners, 1 same
faction), arg6 = loudness f32, arg7 = instigator entity). It has 152 call sites.

- **Radius** = `arg6 × table[type].radius`, from the table at **0x2140320** (30 entries, 8 bytes each: f32 radius,
  f32 intensity):

  | Type | Radius / intensity | Type | Radius / intensity |
  |---|---|---|---|
  | 1 | 60 / 20 | 10-13 | **15 / 5** |
  | 2 | 60 / 2 | 14 | 50 / 15 |
  | 3 | 35 / 80 | 15 | 50 / 20 |
  | 4 | 70 / 80 | 16 | 60 / 20 |
  | 5 | 10 / 50 (movement: ×`movement_noise`) | 17 | 60 / 30 |
  | 6 | 35 / 15 | 21-25 | 80 / 15-30 |
  | 7 | 50 / 15 | 26 | 60 / 100 |
  | 8 | 60 / 15 | 27, 29 | 150 / 150, 150 / 5000 |
  | 9 | 70 / 15 | 28 | 5000 / 5000 |

  Types 0x15-0x1D come from enemy behaviour code (alerts and call-outs). Type 5 is the only one that applies the
  `movement_noise` attribute (0xAF8B7112, Multiply only; Reduced Signature and Feet First ×0.5) at 0x64ACAD.
- **Listeners** are `[game+0x3326DA8]`: count +0x10, objects +0x38, perceiver key per listener +0x40, records +0x48
  (0x18 each: time, level, max level, sensitivity +0x10), multiplier per listener +0x50.
  - A listener can whitelist up to 4 noise types.
  - Faction filter: in mode 0 the listener must be hostile to the source.
  - `R = radius × sensitivity × multiplier × weather([world+0x78AF9E4])`, and `heard = (1 − d²/R²) × intensity`;
    nothing is heard beyond R.
- **Effect:** the listener's level rises, then:
  - mode 0: 0x888E50(perceiver, source, pos, heard) finds or creates the **source's list-A entry** in the listener's
    perception record (a source with no faction goes to single slot +0x1228). It sets entry +0x38 (heard level, max'd,
    clamped), +0x20 (time) and flag bit 2 (heard). When the entry is "heard only" (flags = 4), it moves the last-known
    position +0x04 to the noise position. Sense bits (+0x4C) are not set, so hearing gives a position to search, not a
    sight target (SPECULATIVE on behaviour, PROVEN on data);
  - mode 1: 0x889050 writes the **instigator** into single slot 3 (+0x12C8), with the position and level at +0x1300,
    flags at +0x1310. This is an ally call-out;
  - then 0xB58010(listener, source, pos, heard) runs, probably the AI's "heard something" event (SPECULATIVE).

**For "suppressed shots reveal her only within 15 m"**, two ways, both cheap:

- **Data:** give her own weapon copies (HellForge `add_weapon` records are per weapon) WeaponData +0x84 = 10 (15 m,
  intensity 5) and a low +0x88 floor.
- **Hook:** hook EmitNoise. For calls whose source (r8d) or instigator (arg7) is her: scale arg6 so the radius is
  15 m (`arg6 = 15 / table[type].radius`), or 0 for silent actions. MinHook (HellForge's `hook`) relocates the leading
  `test dl, dl; je rel32` (the first 8 bytes).

---

## 4. Target choice (question 4)

- **Only perceived entries are scored** (FROM-NOTES: HD2Runtime's traced Gatling search: "perceived (entry sense bits
  × sensors) 0/1" at 0x280BF3 → 0x8859C0). Enemies run behaviours through the same dispatcher and perception
  manager (FROM-NOTES). Their own search code is not traced; that they also require sense bits is SPECULATIVE, but
  consistent with every Lost Contact log.
- **Entity flags** `[game+0x3326430]` (map +0x18, u64 values +0x38; reader 0x4A9710):
  - bit 25 (invalid) is tested at 162 sites across behaviour code and in hearing (0x888F0E);
  - bit 35 ("never scored") at 140 sites, concentrated in 0x25C-0x285 (sentries), 0x31E-0x355, 0x3E0-0x467 and 0xBB-0xC0;
  - which of those are enemy behaviours is not known. Setting bit 35 on her is SPECULATIVE. Bit 25 would likely also
    hide her from friendly systems and HUD: do not use it.
- **Faction masks** `[game+0x3326CB8]` +0x12078: hostility (and the noise filter) needs no shared bit. Giving her an
  enemy faction bit would make enemies treat her as an ally. That also changes friendly fire, sentries and hearing:
  high risk (SPECULATIVE).
- **Visibility also reaches behaviours directly:** 0x4B5CA0 returns the AI context target's visibility (1.0 when it has
  no record) to behaviour code at 0x21BB07, 0x2A7317, 0x3136E1, 0x313DEF, 0x314317 and 0x315B0B. That may lower aim or
  engagement against a low-visibility target (SPECULATIVE: a free "rarely targeted" effect, or a reason to keep v
  small but not 0 while she is revealed).

---

## 5. Levers, ranked (likely to work × low risk first)

| # | Lever | How | Works? | Risk |
|---|---|---|---|---|
| 1 | **Visibility base = 0** | Call `SetBase` 0x720350(NULL, her entity, xmm2 = 0.0f) on the game thread every 0.25 s while hidden. Or write [mgr+0x40] + 0x14·idx + 0 directly (mgr = [game+0x3326540], map at +0x20). Restore the value read at start (or call 0x7203F0, which reloads it from data) when revealed. | High: every eye's effective range becomes 0 (PROVEN math). The floor still applies while firing (2.4). | Low: a state the game uses itself; one entity; host/solo (AI runs on the host). Watch for vehicle/hellpod exits writing 1.0 back (the re-write covers it). |
| 2 | **Break contact while hidden** | Lost Contact's pass with `lose_after` ≈ 1 s for her (exported rules, or Ascalon runs her own copy). | High for targets already held (FROM-NOTES: bots and Illuminate 0 within 5-10 s; bugs only thinned). | Low (shipped code). |
| 3 | **Her own noise** | Data: her weapons' WeaponData +0x84 (type 10 = 15 m) and +0x88 (floor). Hook: EmitNoise 0x64A890 scaling arg6 for her as source or instigator. | High (PROVEN radius math). | Data: low, her own records only. Hook: medium (152 call sites; keep the detour trivial). |
| 4 | **Reveal timing** | Hold the floor (+0x0C) at a chosen value for 2 s / 5 s after a shot (0x720490 each tick) or at 0 for a "silent" kill. | Medium: the game writes the floor every weapon update (SPECULATIVE on order). | Low. |
| 5 | **Detection passive** | `set_passives(self, HF_PASSIVE_KIT, 2 /*SCOUT*/)` plus Scout's shared modifier 0x14ECCE15 set to 0.001 (customization manager +0x20). | High solo; >0 only. | Medium: shared row (co-op teammates with Scout), and the armour's own row wins when her armour has the key. |
| 6 | **Hook the vision test** | 0x64C6F0: when arg5 (`[rsp+0x28]` at entry) is her, set out = {0} and return. The prologue (`mov rax, rsp; mov [rax+0x18], rbx; push ...`) is position independent. | High. | Medium: hot path. It also serves 0x75FD50 (systems pass 0x572EE3) and 0x7EB000 (24 callers, probably script/API), which may lose sight of her too (objectives, cameras: unknown). |
| 7 | Entity flag bit 35 on her | `[game+0x3326430]` u64 value \|= 1<<35 | Unknown for enemies. | Medium-high (sentries' filter; unknown elsewhere). |
| 8 | Faction mask | Share a bit with enemies | Likely, crudely. | High. |

**Recommendation:** levers 1 + 2 + 3 (data) make the core. 4 tunes the reveal windows. 5 is the fallback if 1 turns
out to be overwritten every frame. The 50% dodge is separate (kit base hit hook).

---

## 6. Probe plan (one mission, solo host)

A "Perception Probe" (C, SDK 15, build-gated like Lost Contact), keys to cycle the modes, a log line every 2 s:

1. **Read her record.** Look her avatar up in `[game+0x3326540]` (map +0x20, records +0x40, stride 0x14). Log
   base/A/B/floor/dark and `GetVisibility` (call 0x720530(NULL, id): a pure read) while she stands, crouches, goes
   prone, sprints, fires a loud gun, fires a suppressed gun, rides a vehicle and enters a hellpod. Expected: base 1.0,
   B 1.0, floor jumps while firing. This confirms the layout and what A and the floor do.
2. **Hidden mode (key):** SetBase(her, 0) every 0.25 s.
   - Walk into a patrol's view at 30, 15, 5 and 2 m.
   - Per enemy within 50 m, log: her entry present, sense bits, +0x30 LOS, +0x40 awareness, behaviour target, and the
     enemy's state in `[game+0x3326528]` +0x50.
   - Expected: no sense bits, no target, awareness falling; at 2 m still unseen (only d < 1e-6 is automatic).
   - Then bump into one enemy and stand in front of it for 10 s.
3. **Firing while hidden:** fire a loud gun, then a suppressed one, from 40 m and 10 m. Log the floor, who senses her,
   and when. Read her guns' WeaponData +0x70/+0x84/+0x88 (component_record("WeaponDataComponentData", resource)) with
   and without a suppressor. Confirms that the floor reveals her and gives the per-gun values.
4. **Noise observe:** hook 0x64A890 in observe mode. Log every call with source or instigator = her (type, arg6,
   radius, position) and, for 0.5 s after it, which enemies gain a heard-only entry (flags +0x48 = 4). Then mute mode
   (arg6 = 0 for her) and 15 m mode, side by side.
5. **With break contact** (Lost Contact on, lose_after 1 s) in hidden mode: count enemies after her that are not
   sensing her, per 5 s, against hidden mode alone.
6. **Leftovers to log:** does anything write her base back to 1.0 (count re-writes needed per minute)? Do the
   behaviours that read 0x4B5CA0 change accuracy against v = 0 (hits taken while hidden but in a fight)?

Stop condition: if step 2 shows sense bits on her with base 0, the eye is not the only sensor. Then log the perceiver's
sensor list (record +4+8k) per enemy type and look for SensorEar or SensorProximity entries (the mortar-style no-ray
sphere sets bits without any visibility check: 0x886BFD).
