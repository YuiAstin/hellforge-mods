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
| 4 | More grenades and stims at Commando POIs | `poi_extra = 1` | Depends on whether POI loot comes from editable tables | Probe first |
| 5 | Patrols keep to their outposts | (separate mod) | Lost Contact's patrol mode | In test |
| 6 | Sabotage sub-objectives, captured SEAF sites | (later) | New objectives: much larger; only if HellForge's mission tools reach that far | Later |
| 7 | **Outpost caches:** a resupply for every enemy base cleared (Yui, 2026-11-06: in Commando bases are skipped; this gives a reason to clear them) | `outpost_caches = 1`, `cache_calls_small = 1`, `cache_calls_large = 2` | Below | Built in 0.2.0 (untested) |

## Open questions (one probe)

- ~~Is this a Commando mission?~~ Answered by the Mission Probe (findings below): only Commando grants Call In
  Destroyer (stratagem type 123) on the player's stratagem record, so its slot there marks a Commando mission (the
  hive mind / mode read stays a cross-check if Mission Probe 0.1.1 is ever run).
- ~~How Commando limits stratagems~~: the call-in is that slot, with `uses 1` (below).
- **POI loot:** where a POI's contents come from.

## Not in scope

Options 1-3 are buildable (2026-11-06); POI loot (#4) still waits for a probe.

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
