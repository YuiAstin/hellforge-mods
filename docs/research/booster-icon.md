# Custom icon for booster 21 (Extra Boosters) — findings

Build 25480438, `game.dll` RVAs, checked in `re/game.bin`. Static analysis only; nothing tested in game.
Sources: `hellforge-mods/docs/research/booster-table.md` (branch sure-footed-booster), HD2Runtime (`hd2rt/`),
HellForge (`hf/`).

## 0. A correction to booster-table.md

`0x3AA8B87E` is not a resource type. It is `murmur64('diffuse_map') >> 32` (checked with `hd2_archive.resource_hash`):
the id of a material's texture slot, which is also the widget's "image property". The real types are:
- `material`: `0xEAC0B497876ADEDF`
- `texture`: `0xCD4238C6A0C69E32`

`0x1798770` doesn't resolve a resource of type `0x3AA8B87E`. It binds the image slot `0x3AA8B87E` of a widget to a
name (see section 1).

## 1. Is booster +0x20 the same kind of id as a stratagem's presentation icon? Yes (high confidence)

Booster row `+0x20` is an **icon name** (u64 murmur64 of a resource name). A stratagem's icon (StratagemInfo `+0xB0`,
which HD2Runtime's `presentation_icon` and HellForge's `HfStratagem.icon` write) is the same kind of value. One name N
stands for a family:
- a GUI material N;
- an atlas sprite N;
- a texture N.

Evidence:
- **Both catalogues treat them alike.** `hd2rt/sdk/HudIconSprites.json` and `hf/mappings/hud-icons.json` take
  booster sprite names from row `+0x20` and stratagem sprite names from `+0xB0`. Both are keys of the same
  `texture_atlas` sprite map: stratagems are on pages 1 and 2, the 20 boosters on page 3. All 20 vanilla `+0x20`
  values in the dump match those sprite names (for example, Vitality is `0xA9C52A2333DFCB68`).
- **The booster readers go through the same consumer kinds as the stratagem readers** (disassembled):

| Reader | What it does with +0x20 | What N must have |
|---|---|---|
| `0x1893DDE` (HUD block), `0x191CA8F`, `0x191DC24` (details panel) | `call 0x144F800`, which sets the widget's material **by name** = N | **GUI material N.** With no fallback, a null material crashes. This is the same handler set HD2Runtime documented after the 0.9.0 crash |
| `0x17982CF` → `0x1798770`, flag `[w+0x155D]` clear | `0x144F800(N)` | **GUI material N** |
| `0x1798770`, flag set | the widget gets the fixed material `0x9448536C2D6AE780`, then GUI API `+0x28 → +0xD8 (N, 0x3AA8B87E)` returns material N's slot texture name (with a fallback), and `0x1449A70` binds it to the image slot | material N's slot texture |
| `0x1799D27`, `0x179A5D7` → `0x1450230` | fixed material `0x9448536C2D6AE780` / `0x57FCF14AD069020B`, then image slot `0x3AA8B87E` = N, bound by name | atlas sprite N, else **texture N**, else missing_texture (no crash) |
| `0x18CBB6D`, `0x18CB3F1` → `0x18CBEC0` | style descriptor `{name = N, kind 0}`. The stratagem tile in the same function (`0x18CB541`) writes kind 2. In the descriptor consumer `0x1943D60`, both kind 0 and kind 2 call `0x144F800(N)`; kind 2 also sets three colour variables | GUI material N (medium: it is not proven that `0x1943D60` consumes this descriptor) |

- `0x9448536C2D6AE780` is a UI image material (shader `0x40600CA5`). HD2Runtime's GameIconProbe drew a booster plate
  through it correctly, in its own colours and alpha.

**The difference from stratagems is the art, not the id.**
- Stratagem textures are masks (BC1: R = category colour, G = white). They are drawn by the icon shader `0x3461FF0D`.
- Booster art is full colour with alpha (BC3 atlas page).
- The material-by-name readers draw with **material N's own shader**. The HD2Runtime and HellForge icon materials
  always use the mask shader `0x3461FF0D`. The booster descriptor (kind 0) sets no colour variables, so a booster
  icon drawn through a mask-shader material will probably come out in default mask colours and without its alpha
  cut-out (guess; HD2Runtime saw a booster drawn through the icon shader as a square). The vanilla booster materials
  must use a colour shader, but their bytes aren't in the dump.

## 2. Simplest working route

**Id:** `+0x20 = murmur64a(name)`. This is the 64-bit MurmurHash64A with seed 0 of the UTF-8 resource name, the same
hash as:
- HellForge's `ResourceTypes.Hash` / `EngineHash.Murmur64A`;
- the native `Murmur64A` and `hf->resource_hash(name)` (`hellforge_mod.h:1175`);
- HD2Runtime's `hd2_archive.resource_hash`.

HD2Runtime's names are `<mod resource>/images/<id>`. For HellForge, use any unique name, for example
`yui/extra_boosters/icons/booster21`. It must not be a vanilla resource or atlas sprite name; HellForge's builder
refuses names the game already has.

**Route A, the minimum that cannot crash (HellForge data spec only):**

1. **Ship the PNG.** Put `icons/booster21.png` in the mod folder: 256 x 256, non-interlaced, full-colour art with
   alpha, ideally the hexagon plate style.
2. **Add `hellforge-data.json`** beside the manifest:
   ```json
   {"format":1,"patches":[{"archives":["9ba626afa44a3aa3"],
     "resources":[{"icon":"icons/booster21.png","name":"yui/extra_boosters/icons/booster21","prepare":"image"}]}]}
   ```
   - `prepare: "image"` gives a BC3 texture with alpha (`IconImage.Family`, `hf/src/HellForge.Core/GameData/IconImage.cs`)
     plus the GUI material N, which has one `diffuse_map` slot naming texture N.
   - HellForge builds the patch from the spec at session start and stages it as `<bundle>.patch_<n>` (+ `.gpu_resources`).
3. **In code,** set `added + 0x20 = hf->resource_hash("yui/extra_boosters/icons/booster21")` in place of the
   `icon_from` copy (`extra_boosters.c:242`).

Result:
- Every reader finds a material and a texture, so there is no crash and no "?" icon (high confidence).
- Colours are right in the widgets that use the fixed UI image material (high-medium).
- Colours are uncertain (probably mask-coloured) in the HUD block, the details panel and the picker tile, which use
  the mask-shader material.

**Route B, correct colours everywhere** (one spec, two patches on the same bundle; medium confidence, untested):
```json
{"format":1,"patches":[
 {"archives":["9ba626afa44a3aa3"],"resources":[
   {"icon":"icons/booster21.png","name":"yui/extra_boosters/icons/booster21","prepare":"image"}]},
 {"archives":["9ba626afa44a3aa3"],"resources":[
   {"type":"material","copy":"0xA9C52A2333DFCB68","name":"yui/extra_boosters/icons/booster21",
    "replace":[{"from":"0xA9C52A2333DFCB68","to":"yui/extra_boosters/icons/booster21"}]}]}]}
```
- **How it works:** the second patch replaces material N with a copy of a vanilla booster material (Vitality's),
  whose slot is re-pointed at texture N. Patches are numbered per bundle in the spec's order (`WriteFiles`,
  `ModDataBuilder.cs:150`), and the higher patch should win.
- **Assumptions:**
  - the vanilla booster material is named by its sprite name. This is proven indirectly: the vanilla readers call
    `0x144F800` with that name;
  - its slot names that same name (a guess that follows the stratagem convention). Add an `expect`, or drop `replace`
    and inspect the material with `HellForge.Cli build-data`, to confirm it.
- **Mod sync:** a `copy` with `replace` counts as raw data for mod sync (verified list, or trust on the prompt).

**Loading requirement:** the material must be resident whenever any widget shows booster 21. A missing material
crashes the picker, the HUD block or the details panel.
- **Use the boot bundle `9ba626afa44a3aa3`.** HD2Runtime ships its icon families as a patch of it
  (`9ba626afa44a3aa3.patch_0`). Its live probe found the family loaded at startup and still resident at every check.
- **Not `007e093ca718ca1a`.** That is HellForge's documented stratagem-icon bundle, and it wasn't yet loaded at
  HD2Runtime's first ship check.
- **When to write +0x20:** any time before the loadout or booster UI first builds its icons, so at table-copy time is
  fine. A change made while a screen is open shows the next time that screen is built.

## 3. Can this be done without HD2Runtime? Yes (high confidence)

HellForge already ports HD2Runtime's icon builder. `IconImage.cs` says it is a port of `hd2_image.py`: the same BC1
texture, header `0xFCE7DA44`, 160-byte material, shader `0x3461FF0D` and slot `0x3AA8B87E`. It adds the BC3 `"image"`
mode and builds the patch from `hellforge-data.json`. You can also ship prebuilt `data/<bundle>.patch_<n>` files.

So the work splits as:
- a HellForge code mod (Extra Boosters) writes `+0x20 = hf->resource_hash(name)`;
- a data spec builds texture N and material N.

You don't need HD2Runtime's Lua build, Bingus or HD2Runtime itself. HD2Runtime would give the same family, named
`murmur64(<resource>/images/<id>)` in `9ba626afa44a3aa3.patch_0`, but its `presentation_icon` API only writes
stratagems.

## Open points to test in game
1. Route A: what the HUD block, details panel and picker tile draw through the mask-shader material.
2. Route B: the vanilla booster material's slot name, and whether patch override works within one mod's spec.
3. Whether `0x1943D60` really consumes the `0x2A98` descriptor that `0x18CBEC0` fills. Either way, `0x144F800(N)` is
   reached directly by the other readers.
