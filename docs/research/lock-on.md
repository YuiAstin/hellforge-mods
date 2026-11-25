> Static analysis of a memory copy of game.dll (build 25480438, dumped by `probes/image-dump`), started from the P-12L Stim Missile mod's guard patterns. The recipe below was **tested in game** with `mods/lock-stim-test` 0.1.x (2026-11-07); the rest is the original static write-up.

## Confirmed in game: a sentry firing game-guided missiles

What works (Lock-on & Stim Test 0.1.1–0.1.3, solo, one Rocket Sentry, mixed crowd: 17 missiles, 32 target hand-offs, none left without a target):

1. **Load the missile's package first.** `load_package(mod, 0x64C37A90C0D906DE)` (packages/generated/loadout/pistol_smart_missile), polled from the game-thread tick until `HF_PACKAGE_RESIDENT`. Without it the missile is the purple "?" placeholder.
2. **Point the sentry at the missile entity.** `set_live(ROCKET, HF_FIELD(ProjectileWeapon, projectile_entity), HF_U64(0xA1470A4DAB237264))` (the P-33's guided missile) → `HF_APPLIES_RESPAWN`, so a sentry called in afterwards fires it. The carried shot stays the sentry's `ProjectileWeapon.projectile` (its rocket, or Veterancy's warhead copy).
3. **Targeting mode is already 2.** The P-33 missile's SeekingMissile record (`component_record("SeekingMissileComponentData", 0xA1470A4DAB237264)`, 272 bytes): mode 2 (follow an entity), guidance on after 0.01 s, no off time/distance, lifetime 30 s. No record edit is needed.
4. **Hand each missile a target every tick** (game thread). Walk the seeking-missile manager `[game+0x3326DF0]`: count `+0x10`, headers `[+0x38] + i*8` (header `+8` = missile entity), state `[+0x50] + i*0x38`, local `[+0x48] + i*0xC0` (local `+0x84` = shooter entity: the sentry itself, its resource 0x37079568DC86E9C6; `+0x74` = launch direction). A sentry's missile launches with state `+0x00 = 0x7FFF` (no target). Write `+0x00 = game.dll+0xFD9AF0(target entity)` (entity → network id, `uint32_t(uint32_t)`), `+0x04 = 0x10` (default aim node; other nodes index unchecked), `+0x34 = 1` (guidance on). The game re-reads `+0x00` every update, so a new target mid-flight works.
5. **The sentry's own target is readable:** Behavior manager `[game+0x3326740]`, its u32 map at `+0x40` (same layout as below), records `[+0x60] + index*0x1F8`, `+0x18` = the entity it is aiming at. 18 of the 32 hand-offs came from there; the rest (re-targets after a kill) from the nearest living enemy within 150 m and 75° of the launch direction.
6. **Re-target only when the target dies,** not when `+0x00` reads 0x7FFF again: the game clears it for the final approach, and forcing guidance back on then would fight it.

Map layout used by the game (game.dll+0x172F670): `{slots ptr, count (power of two), empty key, multiplier}`, probe `(key * mult + i) & (count - 1)`, 8-byte slots `{key, value}`.

Code pins checked before any of this (all matched build 25480438): 0xFD9AF0, 0x641148, 0x642B05, 0x642A0F, 0x643030. Their bytes are in `mods/lock-stim-test/src/lock_stim_test.c`.

Not tested yet: other missiles (W.A.S.P., Spear, Commando, Warrant; the Warrant's package is not in HellForge's catalogue); several sentries at once; a teammate's sentry (the hand-off runs on the host); how the P-33 pistol's own guided mode behaves while the sentry edit is on.

---

# Lock-on / seeking-missile targeting — game.dll build 25480438 (static analysis)

Confidence tags: **[P]** proven from the instructions cited; **[I]** inferred (strong reasoning, not fully traced); **[G]** guess.
All addresses are RVAs (file offset == RVA in game.bin). "invalid entity" = the global at 0x3483C20/0x3483C34/0x3483C4C,
which is 0 in the dump [P]; "no net id" = 0x7FFF [P: 0xFD9AF0 returns [0x2154544]=0x7FFF].

Helpers used everywhere:
- `0xFD9AF0(ecx=entity) -> net id` (0x7FFF if none) [P]
- `0xFD9BA0(rcx=&out, edx=net id) -> &out = entity` (0x7FFF -> invalid) [P]
- `0xFD9D40(ecx=entity) -> instance header` (header +8 entity, +0xC unit, +0x10 net handle, +0x14 bit0 = local authority) [P/I]
- `0xFD97E0(ecx=handle, edx=field hash, r8=&field)` = "network field changed" notify [I]
- `0xFDDF80(..., edx=handle, r8d=hash, r9=&field)` = same for vec3 fields [I]

---------------------------------------------------------------------------------------------------------------------
## 1. SeekingMissile instance: where the target lives, who writes it, the per-missile update

### Manager [game+0x3326DF0] [P]
- entity map: +0x20 slots (u64 {u32 key entity, u32 index}), +0x28 slot count (pow2), +0x2C empty key, +0x30 hash mult;
  probe `slot = (i + key*mult) & (count-1)`, i = 0.. (same open-addressing scheme as every other manager).
- +0x38: header pointer per index (u64[]).  +0x10: number of instances (update loop bound).
- +0x40: per-missile clocks, 12 bytes: bytes +0..+3 one-shot flags, +4 f32 distance accumulator (adds |step|² each
  movement step, 0x645453), +8 f32 age (adds dt, 0x64286C).
- +0x48: local instance, stride 0xC0.  +0x50: replicated state, stride 0x38.

### Replicated state  ([mgr+0x50] + idx*0x38)  — THE TARGET IS HERE [P]
| off | type | meaning | net hash |
|---|---|---|---|
| +0x00 | u32 | **target net id** (0x7FFF = none) | 0xAC08FE53 |
| +0x04 | u32 | **target node** (lock-point index; 0x10 = the target's default aim point) | 0x69A47345 |
| +0x08 | u32 | second id, init 0x7FFF (unused here) | 0x019CD5F2 |
| +0x0C | vec3 | **current target point** (recomputed every update) | 0x7A4BDBBE |
| +0x18 | vec3 | position | 0x8BBEB160 |
| +0x24 | quat | rotation | 0x20605662 |
| +0x34 | u8 | **tracking / guidance on** | 0xD69E362D |
| +0x35 | u8 | movement enabled (record +0x38 time_to_enable_movement) | 0xAA452035 |
| +0x36 | u8 | explosive armed (record +0x3C) | |

### Local instance ([mgr+0x48] + idx*0xC0) [P]
+0x24 age (movement), +0x28/+0x30 current speed (init starting_speed), +0x34 carried projectile type, +0x38 quat,
+0x48 pos, +0x54 previous pos, +0x60 retarget timer (reset to record +0x6C target_update_interval), +0x64 effect id,
+0x68 vec3 javelin apex point, +0x74 vec3 launch direction, +0x80 u8 "use apex/launch dir", **+0x84 shooter weapon
entity** (= spawn info +0x428), +0x88 u8, +0xBC u8 (record +0xFC == 0), +0xBD u8 mirror of state +0x35.

### Written at spawn (from the shooter's lock, through the spawn info) [P]
Chain: fire path 0x6128B0 -> `0x615940` (entity shot) builds a spawn info on its stack (rbp+0xE0) -> `0xFDC140` spawns
-> component creation:
- 0x615940 @0x61606E..0x616124: looks the **weapon entity** (spawn info +0x428) up in the lock-on manager
  [0x3326BD8] (map +0x840). If found and the lock data (0x506FE0) +0x1C != 0, copies the weapon's current lock:
  `spawninfo+0x79C = entity(lock[+0xC])`, `spawninfo+0x7A0 = lock[+0x10]` (lock array [0x3326BD8+0x870], stride 0x14).
  Default (no lock component): +0x79C = 0 (invalid), +0x7A0 = 0 (0x615EA1).
  Note: it copies the lock target whether or not the lock is complete (lock +0x8); the target is assigned when the
  lock starts (progress <= 0.01). [P]
- Other spawn-info members: +0x424 projectile type, +0x428 weapon entity, +0x42C owner/wielder (from [0x3326730]),
  +0x748 owner again, +0x74C owner's Behavior record +0x18 (entity) / +0x750 vec (Behavior record +0x1C) [P for copies,
  I for meaning "owner AI's current target"].
- State init `0x6467F0(rcx, rdx=header, r8=spawn ctx, r9)` (ctx+0x48 = spawn info) [P]:
  state+0x00 = netid(spawninfo+0x79C) or 0x7FFF; +0x04 = spawninfo+0x7A0; +0x0C = 0x6407D0(...);
  **+0x34 = (record+0x0C time_to_enable_guidance == 0)**; +0x35 = (record+0x38 == 0); +0x36 = (record+0x3C == 0).
- Missile spawn `0x641050` [P]: speed, type, pose; @0x6413A8 if the weapon entity has component [0x3326960]
  (a launcher toggle, flipped by a weapon function at 0x755692/0x7556A3 [I]) its record byte +0 overwrites
  state+0x34; if that byte is 0 the missile is turned toward the owner avatar's aim point instead. @0x64190E with the
  lock data +0x1C and record +0xAC (javelin_firing_mode) it sets +0x80 and the apex point +0x68.

### Per-missile update `0x642760` (system update, one loop over all missiles, ~0x642760..0x645933) [P]
Pass 1 (0x642820 loop): timers. age +8 += dt; flags set +0x35/+0x36 when age passes record +0x38/+0x3C;
guidance ON (0x645D90 r8b=1, sets state+0x34) when age > record+0x0C (>0) or distance acc > record+0x14 (>0);
guidance OFF when age > record+0x10 (time_to_disable_guidance) or distance > record+0x18.
Then if state+0x35 (movement on) and the retarget timer +0x60 has run out (reset to record+0x6C), **switch on record
+0x00 targeting_mode** (0x642A17):
- **mode 2 — locked entity (Spear style)** 0x642B05: entity = 0xFD9BA0(state+0x00). If state+0x34 and entity valid:
  target point = `0x916FA0(lockmgr, &out, header(entity), state+0x04)` (node 0x10 -> 0x8D5670 aim point of the target
  in [0x3326CB8], else its unit root) -> state+0x0C. Within record+0x104 (or speed*0.1 when <=0) of the point it
  clears state+0x34 (final approach, flies straight) (0x642C31). **No target (0x7FFF) or tracking off: target point =
  own position + forward * 1000 (0x642C80..0x642DFC) -> flies straight on**, state+0 forced to 0x7FFF.
- **mode 1 — shooter's lock aim point** 0x642E22: `0x9193D0(lockmgr, missile entity, &out)`: finds a lock instance on
  the missile, else on its shooter (+0x84), else via [0x3326698]; returns that lock's aim point ([0x3326BD8+0x868],
  stride 0x2C). Else straight.
- **mode 3 — per-missile designated point** 0x642A3E: `0x810960([0x3326758], shooter +0x84, missile, &out)`:
  shooter record (stride 0x290) lists missiles at +0x18C[i] (count byte +0x28D) with points at +0xC+i*0xC
  (laser-designation style, e.g. Commando [I]). Else straight.
- **mode 0 — missile's own Behavior target** 0x642FB2: Behavior [0x3326740] record of the *missile* (stride 0x1F8,
  records +0x60) +0x18 = target entity; accepted when within record+0x1C angle_lost_guidance; point from
  [0x3326CB8] aim point (0x8D5670) or transform [0x3326508] +0x2E0. Requires the missile to own a Behavior (else
  index -1 is used unchecked).
Pass 2 (0x643430 loop): movement/steering: record re-read (0x643471), lifetime +0x40 vs age (missile ends, 0x643656),
turn rate blend by angle/max_angle (0x64428D), speed ramp (0x6442B1..), steering toward state+0x0C.
**Without a target the missile keeps flying straight until max_lifetime (30 s on W.A.S.P./Spear/Commando), then ends;
it does not self-destruct early.** [P for straight flight and lifetime end; what "ends" looks like (detonate vs
despawn) not traced.]

Which mode each missile uses: not read (the records are on the heap, not in the dump). Read SeekingMissile record +0
at runtime (slot 196). [G] Spear = 2, Commando = 3 (laser, time_to_enable_guidance 0), W.A.S.P. = 1 or 2.

---------------------------------------------------------------------------------------------------------------------
## 2. Lock-on component (component index 219) — scan, selection, storage

### Manager [game+0x3326BD8] (one instance per weapon entity that has the component) [P]
- +0x08 candidate count, +0x0C candidate list: 32 x 0x40 bytes {+0 weapon net id, +4 candidate net id, +8 node,
  +0xC raycast id (0x175CA60, line of sight shooter->node), +0x10 ray result, +0x18 ray slot, +0x3C alive}.
- +0x810/+0x818 raycast pools. +0x830/+0x834 instance counts.
- +0x840 entity map (+0x848 count, +0x84C empty, +0x850 mult) -> index into:
  - +0x858 headers (u64), +0x860 state 12 B {+0 fire-mode/programmable state (0x1787500), +4 candidate-category
    bitmask, +8 u8 "allow excluded"}, +0x868 aim record 0x2C B {+0 vec3 aim point, +0xC f32},
  - **+0x870 lock 0x14 B (replicated): +0x00 u8 active, +0x04 f32 lock progress (0x9B738DCA), +0x08 u8 LOCKED
    (progress >= 1.0, 0x1DDC206A), +0x0C u32 target net id (0x7FFF none, 0x83AC92C6), +0x10 u32 node (0x10 default,
    0x47B06683)**.
- +0x880 map / +0x8C0 data: per-instance copy of the 0x7C-byte component-219 record. `0x506FE0(header)` returns it;
  `0x506B30(hash)` is the static table lookup (records +0x120, stride 0x7C, 18 slots).

### Component-219 record (0x7C) fields used [P offsets, I meanings]
+0x00 u8 aim-record update on, +0x01 u8, +0x10 / +0x14 / +0x18 f32 lock times (progress += dt / time, chosen by
whether the candidate is the same one and in view; 0x9189F2..0x918A12), **+0x1C u8 enabled (gates the scan and the
missile hand-off)**, **+0x24 f32 lock cone (screen-space radius, scaled by a per-zoom table 0x32EC180 entry[0]*0.5)**,
+0x28 u8 lock also when not aiming, +0x3C default + +0x4C 4 x 12 B per-fire-mode entries {key, bytes +5..+8 =
category bits 0..3 allowed, +9 = allow "excluded" entities}, +0x48 u32 fire-mode-dependent flag.
Range is not in this record: it comes from [0x3326520] (keyed by weapon entity, records +0x58 stride 0x28, f32 +0xC).

### Scan `0x917120` (called from 0x56A2B6) [P]
1. Collect up to 764 candidate headers from the global list [0x3326430] (filters 0x927050, 0x13E3A80, entity flag
   bits 0x8000000 / 0x8000000000 via 0x6B5190).
2. For every lock instance with data +0x1C: owner = [0x3326730](weapon entity); a player owner must pass the avatar
   test 0xA92A80 ([0x3326D20] +0x53DCD0, stride 0x1238). Then 0x742FE0(weapon) or data +0x28.
3. Per candidate: skip if in [0x33265C8] unless state+8; skip if its category (0x507920(header) +0x28) is not in
   state+4's bits; **if the candidate is in [0x3326CB8] (targetable/faction), `0x8D4F40(mgr, candidate, owner)` at
   0x917979 — true => skip (0x917980 `jne 0x917AE4`)**; then INSERT `0x916C10` for each lock point (record +0x12070
   stride 0x60: count +0x50, nodes +0x10[i]) or node 0x10.
4. INSERT 0x916C10: projects the point to the **local camera** (world+0x20 vfunc +0xF8), rejects behind camera,
   needs screen distance < cone and world distance < range, adds/refreshes the 0x40-byte candidate + LOS ray.
### Select/update `0x917BF0` (called from 0x57311C) [P]
Re-projects candidates whose ray result hits the candidate, picks one, assigns lock +0x0C/+0x10 when progress <= 0.01,
grows progress, sets +0x08 at >= 1.0, drops the lock (0x7FFF / 0 / node 0x10) when progress <= 0.18 and out of view
or the target is gone; 0x918B66 fills the aim record +0x868.

### Faction test `0x8D4F40(rcx=[0x3326CB8], edx=A, r8d=B)` [P]
Returns 1 when A or B has no record in [0x3326CB8] (map +0x12050), else `(mask[A] & mask[B]) != 0` with the u32 faction
masks at +0x12078. I.e. "same side (or unknown) => not lockable". P-12L's CALL_SITE 0x917974 is exactly this call;
"allowing friendlies" = making 0x917980's `jne` not taken (e.g. NOP the 6 bytes, or force al=0). [P that the site is
the ally gate; I what P-12L wrote.]
0x4AFF20 (P-12L's FACTION_TEST) uses manager [0x33266E0] and is not on the lock path [P: not called from 0x9169B0..0x9199xx].

---------------------------------------------------------------------------------------------------------------------
## 3. Sentry feasibility

- A ProjectileWeapon with +0x28 = missile entity spawns that missile through 0x615940 whatever owns the weapon [P for
  the path; I that the sentry's weapon fires through 0x6128B0, which has 5 callers: 0x6174CE, 0x7420BF, 0x762D35,
  0xA398F9, 0xB80535].
- **With no lock component the missile gets no target**: spawn info +0x79C = 0 -> state+0 = 0x7FFF -> a mode-2 missile
  flies straight along the muzzle direction (a slow-accelerating dumb rocket) until max_lifetime [P]. Mode 1/3 also fly
  straight (no lock instance / no designation record on the sentry) [P for the fallbacks]. The game's lock-on cannot be
  reused for a sentry: it is screen-space against the local camera [P].
- **Clean write points** (pick one):
  a) **Hook `0x6467F0` (state init) entry**: r8 = spawn ctx, [r8+0x48] = spawn info. If spawninfo+0x428 is the
     sentry's weapon entity, write spawninfo+0x79C = target entity, +0x7A0 = 0x10. The game then fills state+0x00/+0x04
     and the initial target point itself and includes them in the creation message. Tracking still follows
     time_to_enable_guidance (==0 -> on at spawn; >0 -> on after that delay). For a missile whose value is <= 0 and
     has no distance rule (Spear: -1), also set state+0x34 = 1 after creation (or set record +0x0C to e.g. 0.05). [P]
  b) **After creation** (e.g. once per frame for missiles whose local +0x84 == sentry weapon entity): find idx in
     [0x3326DF0] map (+0x20..), then write state ([+0x50]+idx*0x38) +0x00 = 0xFD9AF0(target), +0x04 = 0x10, +0x34 = 1.
     Mode 2 re-reads +0x00 every retarget interval, so retargeting mid-flight works too. Optionally call 0xFD97E0 /
     0xFDDF80 with the hashes above to replicate (not needed offline/solo host). [P]
  The missile must be a **mode 2** missile (read record +0 first). Don't use node values other than 0x10 unless the
  target has a lock-point record (0x916FA0 indexes [0x3326CB8]+0x12070 unchecked) [P].
- **Turret's current target**: not traced to a TurretComponent field (not cheap). Lead [I]: the sentry's Behavior
  instance ([0x3326740], map +0x40, records +0x60 stride 0x1F8) +0x18 = current target entity, +0x1C vec — the engine
  itself copies the shooter-owner's Behavior +0x18/+0x1C into the spawn info (0x615F40..0x61601B) and mode 0 uses
  Behavior +0x18 as "the target". Verify live by reading it on a sentry while it fires. Note the owner key there is
  [0x3326730](weapon entity): if the sentry's weapon is the sentry entity itself, that copy is skipped.

**Verdict: feasible.** Fire a mode-2 missile from the sentry, and supply the target yourself (hook 0x6467F0 or write the
state after spawn); the missile's own guidance (turn rates, speed ramp, proximity release, lifetime) then does the rest.
Without that write it is only a straight-flying rocket.

---------------------------------------------------------------------------------------------------------------------
## 4. Pins (all verified unique in game.bin with find(); ?? = rel32 / rip disp)
| site | RVA | pattern |
|---|---|---|
| missile update (prologue) | 0x642760 | 48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 41 54 41 55 41 56 41 57 48 8D A8 08 FD FF FF |
| update: retarget timer + mode switch | 0x642A0F | 41 8B 45 6C 41 89 47 60 41 8B 4D 00 85 C9 0F 84 ?? ?? ?? ?? |
| mode-2 target read | 0x642B05 | 8B 14 1E 48 8D 4C 24 44 E8 ?? ?? ?? ?? 80 7C 1E 34 00 |
| proximity release (rec +0x104) | 0x642C31 | F3 41 0F 10 8D 04 01 00 00 41 0F 2F CD 77 0E F3 41 0F 10 4F 28 |
| mode-0 Behavior target | 0x64303E | 8B 5F 18 3B 1D ?? ?? ?? ?? 0F 84 ?? ?? ?? ?? 8B CB |
| missile spawn | 0x641050 | 4C 8B DC 55 53 41 55 49 8D AB 88 FE FF FF 48 81 EC 60 02 00 00 |
| spawn: instance index/stride | 0x641148 | 8B F7 4C 6B C6 38 F3 0F 10 40 44 4C 8D 2C 76 48 89 44 24 58 |
| spawn: state+0x34 from [0x3326960] | 0x6414A2 | 88 54 01 34 BA 2D 36 9E D6 4D 8B 44 24 50 49 8B 44 24 38 |
| state init (hook point) | 0x6467F0 | 48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 55 41 56 41 57 48 83 EC 50 |
| state init: reads spawninfo+0x79C | 0x6468A6 | 8B 88 9C 07 00 00 E8 ?? ?? ?? ?? 8B C8 EB 10 3B C8 |
| guidance on/off setter | 0x645D90 | 41 56 48 83 EC 20 3B 15 ?? ?? ?? ?? 45 0F B6 F0 4C 8B D1 0F 84 ?? ?? ?? ?? 44 8B 59 28 |
| entity-shot spawn | 0x615940 | 4C 8B DC 55 53 56 57 41 55 41 56 41 57 49 8D AB A8 F5 FF FF |
| 0x615940: lock -> spawninfo copy | 0x6160FF | 48 8B BF 70 08 00 00 48 8D 34 B6 48 8D 4C 24 40 |
| 0x615940: owner Behavior copy | 0x615F40 | 4C 8B 1D ?? ?? ?? ?? 45 8B CE 41 8B 73 50 41 8B 7B 48 |
| 0x615940: spawn call 0xFDC140 | 0x6164C8 | 4C 8D 4D 20 4C 8B C3 8B D0 48 8B CF E8 ?? ?? ?? ?? |
| lock scan | 0x917120 | 41 55 B8 00 19 00 00 E8 ?? ?? ?? ?? 48 2B E0 48 8B 05 ?? ?? ?? ?? |
| ally gate call (P-12L CALL_SITE) | 0x917974 | 44 8B C3 8B D0 E8 ?? ?? ?? ?? 84 C0 0F 85 ?? ?? ?? ?? |
| candidate insert | 0x916C10 | 40 53 55 56 57 41 56 48 81 EC D0 00 00 00 48 8B 05 ?? ?? ?? ?? |
| lock select/update (interior, fn 0x917BF0) | 0x917CD2 | 49 8B 85 58 08 00 00 48 89 4C 24 48 4C 8B 34 C8 |
| lock target assign | 0x9185C7 | 41 8B CF E8 ?? ?? ?? ?? 49 8B 8D 70 08 00 00 4D 8D 46 03 |
| target aim point | 0x916FA0 | 40 57 41 54 41 57 48 83 EC 70 48 8B 05 ?? ?? ?? ?? |
| faction test | 0x8D4F40 | 48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 54 41 56 41 57 8B 0D ?? ?? ?? ?? |
| lock data lookup (interior, fn 0x506FE0) | 0x50701B | 45 8B 8B 88 08 00 00 41 8B 9B 90 08 00 00 0F AF D8 |
| mode-1 getter | 0x9193D0 | 48 89 5C 24 08 48 89 74 24 10 57 41 54 41 55 41 56 |
| mode-3 getter | 0x810960 | 48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 41 57 3B 15 ?? ?? ?? ?? 4D 8B F9 |

Prologue-only pins (0x642760, 0x641050, 0x6467F0, 0x615940, 0x8D4F40, 0x9193D0, 0x810960) are unique now but fragile
across builds; prefer anchoring on an interior pin and walking back to the function start (CC padding, 16-aligned).
Function starts that may be hooked by other mods in a live process: check for a leading `jmp` out of the image.

Open items: per-missile targeting_mode values; what [0x3326960] is exactly; where the TurretComponent instance keeps
its target; whether the sentry's fire goes through 0x6128B0 (very likely, unverified).
