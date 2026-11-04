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

## Open questions (one probe)

- **Is this a Commando mission?** The SDK names mission kinds (Blitz is kind 6) but has no call for the current one.
  Needs: where the running mission's kind (or its mission row) is read, and Commando's value. Until then, the options
  could apply to every mission, as a fallback the player turns on.
- **How Commando limits stratagems:** what the Super Destroyer call-in is (a stratagem with uses, or a mission rule).
- **POI loot:** where a POI's contents come from.

## Not in scope

The timer and the free Resupply are small. The rest waits for the probe.

## Mission Probe findings (2026-11-04, a normal mission then a Commando mission)

- **The Super Destroyer call-in is a stratagem with uses.** The Commando mission's record had a slot the normal one didn't:
  type 123, Call In Destroyer, `uses 1`, granted. So #3 (`extra_calls`) is the uses field on that slot (raise it to 2), or
  a second grant through the same path as #2. No mission rule is needed.
- The Commando mission also had type 42, Dropoff Hellbomb (unlimited, granted), which the normal mission didn't.
- Both missions: Reinforcement Beacon (124, unlimited) and SOS Beacon (145, 1 use) are on the record too, so the
  Reinforce slot is type 124 and its count is not this record's uses field (unlimited 0 here): reinforcements are
  counted elsewhere.
- The hive mind was not logged in either mission (Mission Probe 0.1.0 cleared its list as the mission started; fixed in 0.1.1).

