# Dump analysis

Static analysis of `game.dll` as it is in memory (unpacked), for finding every piece of code that uses one of the
game's inline tables before a mod moves or grows it. Used for the booster table
([docs/research/booster-table.md](../../docs/research/booster-table.md)).

## The dump

The Booster Table Probe (`probes/booster-table-probe`) writes `game.dll.25480438.dump` to the session's logs folder
(`HellForge/logs/sessions/<run>/`): the whole module laid out by RVA (file offset = RVA), about 75 MB. It is the game's
code: keep it on your own disk. `*.dump` is in `.gitignore`; never commit, upload or share it, and keep disassembly out of
the docs beyond a short snippet as evidence.

## Scripts

Python 3 with `pip install capstone numpy`.

```
python table_refs.py  --dump game.dll.25480438.dump --table 0x32FDA90 --row 56 --rows 21 --out out
python chain_sweep.py --dump game.dll.25480438.dump --table 0x32FDA90 --row 56 --rows 21 --out out
```

- `table_refs.py`: a byte-level scan of every executable section for rip-relative and absolute displacements that
  land in the table, then a capstone pass over each function holding one, following registers loaded with the table
  address. Writes `functions.json` (per function: its references, `imul` by the row size, compares with the row count,
  direct callers) and `sites.tsv`.
- `chain_sweep.py`: follows the chained unwind info in `.pdata` from each function to its root and disassembles every
  fragment of it, so code split across fragments (table loaded in one, used in the next) is covered. Writes
  `sweep.json`: every instruction that encodes a table address (the list to re-point) and every constant equal to the
  row count or count ±1 (bounds and sentinel candidates, to classify by hand).

Both count on `.pdata` (the exception directory), which the protector leaves intact in memory. Code that handles a
row index without ever addressing the table (a validator, network serialisation) is not found by either: trace it at run
time (Cheat Engine "find out what accesses this address" on a row, then follow the index back).

## Extra Boosters

- `gen_booster_tables.py <dump> docs/research/booster-table.md <sweep.json> <out>`: writes
  `mods/extra-boosters/src/booster_sites.h` and `booster_bounds.h` from the dump and the note's reviewed
  classification, asserting every displacement and count against the dump.
- `simulate_extra_boosters.py`: applies the mod's patches to a copy of the dump, re-disassembles every patched
  instruction, and emulates the game's booster accessors (unicorn) to check booster 21 is found by id and index and
  that "not found" becomes 22. Needs `pip install unicorn`. Paths at the top of the script.
