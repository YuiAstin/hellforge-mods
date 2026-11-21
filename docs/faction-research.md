# Faction tag research (parked 2026-11-21)

Goal: tell enemies from friends by the game's own faction tag, instead of the kit base's rule (`FoeKind`: has a behaviour,
no turret, no seats, not on the friend list). That rule leaves out some enemies (the War Strider, `ef570293245a17c2`:
behaviour and turret). Vector works around it: her crosshair adopts any living thing with a behaviour or a turret
(`AdoptUnit`, 0.1.9).

Build 25480438. Offsets are from `game.dll`. The dump is read on the PC only and is never committed or shared.

## Status

- Found: the faction component's manager, and how to look an entity up in it. Verified live (0.1.15).
- Not found: where the faction value sits. That is the whole remaining job.

## What is known

### The manager

`[game + 0x3326CB8]` points to the faction component's manager.

How it was identified:

1. The game has a statistics function (around `game+0x56B000..0x570000`) that prints, per component, a line
   `"<name>" : { "max" : %u, "capacity" : N }`. Each line reads its `max` from `[rdi + offset]`, `rdi` being one large
   struct (call it R). `"faction"` (capacity 512) reads `[R + 0x1C62B0]`.
2. An init function (around `game+0x568000..0x56A000`) fills the manager globals with `lea rax, [rbx + offset]` /
   `mov [rip + global], rax`, `rbx` being the same R. The pair for `0x3326CB8` is `R + 0x1B4278`; the next manager starts
   at `R + 0x1C6348`.
3. `0x1B4278 + 0x12038 = 0x1C62B0`: the faction `max` counter is the first field of this manager's entity map header.

The same method gives the known managers back (health `0x3326688` = `R + 0x7C6230`, its `max` at `R + 0x7C7248`;
behaviour `0x3326740`; navigation `0x3326418`), so the method is sound.

About 224 places in the game's code load this global, among them AI and perception code.

### Looking an entity up (verified)

Entity map header at `manager + 0x12038`:

| Offset in header | Field |
|---|---|
| `+0x00` | max (the statistic) |
| `+0x18` | buckets pointer (8 bytes each: key u32, index u32) |
| `+0x20` | capacity (a power of two; 1024 seen) |
| `+0x24` | empty key |
| `+0x28` | multiplier |

Probe: `slot = (entity * multiplier + k) & (capacity - 1)` for `k = 0, 1, ...`; stop at the empty key; a slot whose key is
the entity gives the index. This is `FactionIndex` in `mods/vector/src/vector.c`.

Live result (0.1.15, one bot mission): 54 kinds were found in it, the Helldiver included, and also bots, turrets,
fabricators, a tank's hull (`2c1a7790c435fd67`) and an HMG emplacement (`a98f8b559c7993d8`). So membership alone does not
mean "enemy".

### What the dumped fields turned out to be

0.1.15 logged, per kind: 8 bytes at `manager + 0xE030 + 8 * index`, 112 bytes at `manager + 0x10 + 112 * index`, 24 bytes
at `manager + 0xF030 + 24 * index`.

- `+0xE030 + 8 * index`: not the entity and not the faction. The first u32 rises with the index and was the same for
  different entities that got the same index in two missions (index 0: `0x2D` both times), so it belongs to the slot. The
  second u32 was 1, 8 or 0, and does not split sides (the Helldiver 1, bot turrets 1, bot soldiers 8, one friend 8,
  another 0). The game passes the first u32 to `game+0xFD9D40` and reads `[result + 0xC]` and `[result + 8]`.
- `+0x10 + 112 * index`: pairs of (u32, `0xFFFFFFFF`), mostly zero. The 112-byte stride is a guess and is probably wrong:
  the header at `manager + 0xE000` reads like a 2048-entry structure (`0x800`, a self pointer, a float 0.0625), and
  `0xE000 / 2048 = 28` bytes.
- `+0xF030 + 24 * index`: all zero.

### A dead end

`[game + 0x3326DC8]` (`R + 0x7590D0`) sits next to faction code (`game+0x8D9400..0x8DA800`) and has a simple layout (count
`+0x18`, map `+0x30/+0x38/+0x3C/+0x40`, data pointers `+0x48`), but live it held 0 entries of 16 with enemies on the map,
and the Helldiver was not in it. Not the tag. (One function there, `game+0x8D9443`, compares `[data + 0xC]` between an
entity and what a spatial query returns, with the constants `0x84881072` and `0x4B8DC02B`: possibly faction name hashes,
worth a look.)

`hf->component_record("FactionComponentData", resource, ...)` returns nothing for every kind (200 tried). The dump names
the component `FactionComponent`. Lemuen's "has a faction record" test has therefore never matched.

## How to pick it up

1. Find a hostility check. Start from the perception code (records of `0x13F8` bytes, manager `[game+0x3326548]`) or the
   AI code at `game+0x7DC200..0x7DC300`, both of which load the faction manager, and follow what they read after the
   lookup until two entities' values are compared.
2. Or log more, live: dump the whole region around an index for a Helldiver, a SEAF soldier or sentry (a friend with a
   behaviour) and a bot in the same mission, at several strides (28, 112), and diff them. A bug or squid mission then
   tells the enemy factions apart.
3. Try the two constants above as name hashes (the engine hashes names with MurmurHash64A; 32-bit ids are usually its low
   or high half) against `automaton`, `terminid`, `illuminate`, `helldiver`, `super_earth` and near spellings.
4. Once the value is known: add `Faction(entity)` to the kit base (`kb_friends.h`), make "enemy faction" the first rule
   in `FoeKind`/`AddFoe` with the present rule as the fallback, and try it in Vector before any released kit.

## Tools used (on the PC, in its Linux shell)

- `strings -n 5 <dump> | grep -i faction` for names.
- Python with `capstone` (installed there). The dump is a flat image: file offset = offset from `game.dll`.
  - References to a string or global: scan for `lea`/`mov` with a rip-relative operand (`48|4C 8D|8B|89 /r disp32`) and
    keep those whose target is the address.
  - Which manager a piece of code uses: count rip-relative loads into `0x3326300..0x3326F00` near it.
- In game: `FactionNote` and `FactionDiag` in `mods/vector/src/vector.c` (they only write to the log; safe to remove
  once this is settled).
