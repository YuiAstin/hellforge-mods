# Commando Plus (design, 2026-11-04)

A configurable mod that makes Commando missions a little more forgiving, from a community wishlist (players who like
Commando's "behind the lines, little support" idea but find it slightly too tight). Every option is an ini setting,
so each player picks how much help they want. Host or solo.

## Options

| # | Option | Setting | How | Status |
|---|---|---|---|---|
| 1 | No mission timer in Commando | `no_timer = 1` | `hf->freeze_mission_timer(mod, 1)` at mission start (SDK: "pauses the mission timer; host; a new mission starts it again") | API exists |
| 2 | A free Resupply at drop-in | `resupply_calls = 1` (0 to 2) | Granted outside the loadout, as Voymastina's Sinbreaker is (her stratagem record, game+0x347CE50, appended as the game grants Reinforce and Resupply; Veterancy System's research) | Path proven |
| 3 | One extra Super Destroyer call-in | `extra_calls = 1` | Depends on how Commando limits it: uses on a stratagem record (then the same grant path as #2, or a uses field raised) or a mission rule | Probe first |
| 4 | More grenades and stims at Commando POIs | `poi_extra = 1` | Depends on whether POI loot comes from editable tables | Not working (0.3.0 played 2026-11-06): the hooked roll fills the POI crates (support weapons, credits), none of its tables holds stims or grenades; off by default in 0.3.1, needs a probe |
| 5 | Patrols keep to their outposts | (separate mod) | Lost Contact's patrol mode | In test |
| 6 | Sabotage sub-objectives, captured SEAF sites | (later) | New objectives: much larger; only if HellForge's mission tools reach that far | Later |
| 7 | **Outpost caches:** a resupply for every enemy base cleared (Yui, 2026-11-06: in Commando bases are skipped; this gives a reason to clear them) | `outpost_caches = 1`, `cache_calls_small = 1`, `cache_calls_large = 2` | Below | Works (played 2026-11-06: the reward ammo rack lands with ammo boxes; thrown only in the Super Destroyer window); 0.3.1 fixes the counting |

## Open questions (one probe)

- ~~Is this a Commando mission?~~ Answered by the Mission Probe (findings below): only Commando grants Call In
  Destroyer (stratagem type 123) on the player's stratagem record, so its slot there marks a Commando mission (the
  hive mind / mode read stays a cross-check if Mission Probe 0.1.1 is ever run).
- ~~How Commando limits stratagems~~: the call-in is that slot, with `uses 1` (below).
- ~~POI loot~~: the loot caches' roll and tables, as HellForge 0.4.0 edits them for Forge Credits (native/mod-loader/forge_credits.c); option 4 raises the stim / grenade tables' weights (0.3.0).

## Not in scope

Options 1-4 and 7 are built (0.3.0, untested); 5 is Lost Contact, 6 later.

## Mission Probe findings (2026-11-04, a normal mission then a Commando mission)

- **The Super Destroyer call-in is a stratagem with uses.** The Commando mission's record had a slot the normal one didn't:
  type 123, Call In Destroyer, `uses 1`, granted. So #3 (`extra_calls`) is the uses field on that slot (raise it to 2), or
  a second grant through the same path as #2. No mission rule is needed.
- The Commando mission also had type 42, Dropoff Hellbomb (unlimited, granted), which the normal mission didn't.
- Both missions: Reinforcement Beacon (124, unlimited) and SOS Beacon (145, 1 use) are on the record too, so the
  Reinforce slot is type 124 and its count is not this record's uses field (unlimited 0 here): reinforcements are
  counted elsewhere.
- The hive mind was not logged in either mission (Mission Probe 0.1.0 cleared its list as the mission started; fixed in 0.1.1).


## Outpost caches (built in 0.2.0, untested; Yui 2026-11-06)

Spawning enemies or placing a supply pod in a base is not possible yet (no generic entity spawn: HellForge has none,
HD2Runtime lists it as blocked). So a cleared base pays out as a free call instead, which the player throws into it:

- **A base:** the spawner structures that stand close together (bug holes `bug_spawner_*`, bot fabricators
  `HF_ENTITY_*FABRICATOR`, the Illuminate's), noted as they spawn (HF_ON_SPAWN) and grouped by distance (within ~40 m).
- **Cleared:** its last spawner closed (HF_ON_BUGHOLE for bug holes) or destroyed (HF_ON_KILL on a noted spawner).
- **The reward:** a free call per base, granted on her stratagem record as option 2's Resupply: the game's own reward
  ammo rack (HF_STRATAGEM_AMMO_RACK_PRESIDENT_REWARD, 109) if it grants and lands, else +1 Resupply use. A small
  outpost gives `cache_calls_small`, a large base (several spawners, a large nest or a large fabricator site)
  `cache_calls_large`. Logged per base.
- The spawner resources are HD2Runtime's enemy catalogue (kind "structure"), the Illuminate's included (warp gateway, staff
  spawner, spawner ship). Open: whether 109 can be granted outside a mission that offers it (its package).

## Played 2026-11-06 (0.3.0), Yui's report

- No timer: works. Extra Destroyer call-in: works (called twice).
- Free Resupply: Resupply is already on Commando's record, unlimited: nothing to grant.
- Outpost caches: the reward ammo rack lands and holds ammo boxes; like every stratagem in Commando it can be thrown
  only while the Super Destroyer is overhead. Most bases did not count. The log: bases "cleared" in bursts as the
  mission loaded and as it ended (spawners streamed out, read as destroyed), which marked them paid or paid them for
  nothing. 0.3.1: a spawner that is only gone leaves its base (gone at load, 3+ at once, or more than 150 m from you);
  a base pays only once one member was destroyed; nothing pays in the first 20 s; every spawner, each one down and why,
  and the bases left at mission end are logged, to see which outposts still do not count.
- POI stims / grenades: not working (above).

## Played 2026-11-07 (0.3.1)

- A base cleared mid-mission paid 1 call, as it should.
- 10 "bases" paid at once 20 s in: 10 bot fabricators read 0 health 2 s into the mission, as they spawned, 1100 m from
  Yui (health not set yet). 0.3.2: 0 health counts only after the spawner was seen above 0; one that never reads above
  0 by 20 s in leaves its base.

## 0.5.0: the cash-in (Yui, 2026-11-09; built from Stratagem Probe 0.5.1)

Calling in the Super Destroyer is cashing in: cleared bases bank ammo boxes, and one stratagem calls them in.

- **Bank:** a small base 4 boxes, a large one 8 (`cache_boxes_small`, `cache_boxes_large`; `cash_in 0` keeps 0.4's
  reward-rack calls).
- **The Outpost Cache** (D U D U D U R): a copy of the B-1 Supply Pack whose settings row becomes the Resupply's (all but
  type, id, names, code, uses and cooldown; the probe's Rack Test D). The Resupply's row is found once at the menu: the
  game's list first, else a memory scan on a thread of its own (~30 s). It is hidden from the loadout and granted on the
  record with uses = ceil(banked / 8). In Commando it is locked outside the Super Destroyer's window like every
  stratagem but Call In Destroyer and Reinforce (the lock is by type: probe 0.2-0.4).
- **Two stratagems** (Yui 2026-11-10, so the player knows what a throw brings): the Outpost Cache always drops 8
  boxes (uses: the full 8s banked); the Small Outpost Cache (D U D U D U L, 1 use while there is a rest) drops the boxes
  past the last 8 (4 with the defaults).
- **Pods:** eight copies of the Resupply pod with 1 to 8 boxes (its HellpodRackComponentData: slots 4-7 take the box and
  slot 0-3's other bytes, spawn count +0x22C). The Small one's payload names the pod of the rest; after a call it is
  switched once its pod spawns. Probe 0.5.1: the 8-box pod brought 8 boxes. Pods under 4 boxes set only the
  spawn count (unverified).
- **`resupply_to_pods`** (off by default): the probe's redirect. ActivateStratagemBeacon (game.dll+0x6ABDB0) is hooked;
  after a Resupply's beacon activates, its thrown ball's scene-graph world position is written every frame for 1.5 s, to
  the nearest Reinforcement Pods (their switch + 6 m) that have had none. Probe 0.3.1: the pod lands there.
- 0.4's tests are gone: `pods_resupply` (the pods have no mount) and `racks_anytime` (the lock is not the granted byte).

## 0.5.1

- 0.5.0 never made the caches (played 2026-11-10): it asked the Resupply's payload for 2 entries; it has 6 (three
  pod / marker pairs), so the row was refused and bases paid reward-rack calls. Now any even count of pairs is taken. The
  row was found at once in the game's list (game.dll+0x37CB600 holds pointers to the rows), no scan needed. It is read
  again at each mission start before both caches are filled.
- The Super Destroyer's window (Yui: how long?): Call In Destroyer's row holds 6.0 (+0x60), 5.0 (+0x64) and 120 (its
  cooldown, +0x68); its record entry's +0x18 lands ~119 s after a call (0.4.1's log). The wiki gives 1 minute. Logged
  now: the row's numbers, each call, and when +0x18 / +0x20 are reached, to set against when stratagems lock.
  `destroyer_window` (test, 0 = the game's) writes its cooldown.
- 0.5.1 played 2026-11-10: both caches landed as they should (4 and 8 boxes). But the HUD showed 1 with 3 on the
  record, and the Small Outpost Cache was thrown at 0 uses. 0.5.2: the row's uses follow the record (the HUD may show
  those), and an empty cache is not granted (+9 0) until it has a use again.
- The window (Yui: 1 minute, neither of Call In Destroyer's timers): found in the availability check
  (game.dll+0x671440). In a mission without the Destroyer, a stratagem other than Call In Destroyer / Reinforce is
  usable only while the player's float at [game+0x3326340] + 0x90 x slot + 0x1FAE4 is above 0: the Destroyer's time
  left (+0x1FAE0 a byte and +0x1FAE8 a float the HUD reads beside it). 0.5.3 logs it per slot and `destroyer_window`
  (61 to 600 s) sets it when it rises.
