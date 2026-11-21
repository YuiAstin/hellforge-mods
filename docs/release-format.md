# Release format: a character kit's pages, images, posts and options

How a kit goes out, as Vector 0.2.2 did (2026-11-21). The mechanics of packing and uploading are in
[releases.md](releases.md); this is what the pages say and look like, so every kit reads the same.

## Before anything is uploaded

- Yui has played the build and asked for the release. Only tested features ship.
- The branch is merged into `main`; `CHANGELOG.md` gets one entry for the day, one section a mod, saying what changed
  since the last public version (a first release: what the kit is).
- The version is the one played. The zip comes from a clean export of `main`.
- `THIRD_PARTY_NOTICES.md` and the manifest's description match what the kit now does.

## Names

| Where | Value |
|---|---|
| Page title, both sites | `<Series> <gun, if she has one> <Name>'s Playable skillset - HellForge` (`GFL2 AK-12 Koleda's ...`, `GFL2 Vector's ...`, `Arknights Ascalon's ...`). Always ends in ` - HellForge`. |
| File's display name (Nexus) | `<Name> <version>` (`Vector 0.2.2`) |
| Update title (AyakaMods) | `<Name> <version>` |

## Nexus Mods

| Field | Value |
|---|---|
| Category | Gameplay |
| Mod version, file version | the manifest's; "Update mod version to match this file's version" ticked |
| Short description | at most 350 characters: whose kit, from what, and its hook in one or two sentences |
| Full description | the layout below, typed in the editor's BBCode mode (`[size=5][b]Heading[/b][/size]`, `[list][*]...[/list]`) |
| Generative AI usage | AI-Generated ticked for Mod Content and for Media (the tags AI-Generated Content and AI Media follow) |
| Media | the thumbnail first (it becomes the page's thumbnail), then the art |
| File | category Main; primary; mod manager download allowed; description: `<Name>'s kit for HellForge. Add the zip with HellForge's ADD MODS. The HUD needs Bingus Shared Loader and HD2Runtime.` |
| Permissions | the recommended settings; Credits: the description's credits paragraph (the publish dialog asks for it) |
| An update | Files > Add file > "Update existing file", the old one archived; the changelog box takes the `CHANGELOG.md` entry |

## AyakaMods

| Field | Value |
|---|---|
| Game, category | HellForge > Characters (Miscellaneous for mods that are no kit) |
| Title prefix | PC (required) |
| Short description | at most 100 characters (the site refuses more) |
| Description | the same text, typed with the editor switched to BB code (`[HEADING=2]`, `[LIST][*]`, `[B]`) |
| Feature image | the thumbnail (required) |
| Screenshots | the art |
| An update | `<mod page>/post-update`: "Release a new version" with the number and the zip, and "Post an update" with the changelog entry |

## The description's layout

In this order, each a heading with a short list under it. Numbers are the defaults in the kit's .ini.

1. An opening paragraph: who she is, the kit in one sentence, `Solo or host. No character model is included; pair it
   with one if you have it.` A quote only if it is a real line of hers that Yui supplied.
2. Her passive(s).
3. `Skills (default keys Z, X, C)`: one bullet a skill, its name in bold, what it does, its cooldown.
4. Her resource or stacks (Confectance, Upshift), if she has one.
5. Anything of her own that needs saying (Vector: `Her fire`; Koleda: `The Sinveiler`).
6. `T-Doll body` for the GFL2 kits: `1.5x health and 2x stamina.` and the Hellbomb line.
7. `HUD`, `Settings` (the .ini, the MODS tab, Mod Bindings Menu).
8. `Requirements`: HellForge's version, what her loadout needs, `Bingus Shared Loader and HD2Runtime (for the HUD)`,
   `Mod Options Menu (optional, for in-game settings)`, then `Installs beside my other character kits: pick the active
   one on the MODS tab (GFL2 Kits HUD > Active Character).`
9. `Credits`: the research and the code used, in the notices' words. "used with permission" only for Beel, RSNLRiggs
   (after Khi) and Th3chef; "method only, none of their code is included" where only a method was followed
   ([permissions.md](permissions.md)). Ends with `<Name> is a character of <game> (<studio>). This is an unofficial
   fan mod.`

What is not on the pages: features the release HellForge cannot run (Vector's options screen), probes, anything
untested.

## Images

| File (PC: `Releases/nexus/`) | Size | Used as |
|---|---|---|
| `<name>-thumb.png` | 1280x720 | Nexus thumbnail (first gallery image), AyakaMods feature image |
| `<name>-art.jpg` | portrait | second gallery image / screenshot |
| `mods/<name>/icon.png` | 256x256 | the mod's icon in HellForge: a head-and-shoulders crop of her portrait, transparent ground |
| `mods/<name>/hud-src/images/<name>_s1..s3.png`, `_passive.png` | 256x256 | HUD tiles (Yui's 128 px icons, scaled up) |

Official promo art when Yui has it (Koleda, Ascalon, Exusiai). When there is only a portrait and the skill icons
(Vector), `tools/release_art.py` makes both images in one look: a dark ground with a glow in the kit's accent colour, a
bar in that colour down the left edge, her name in capitals, `Playable skillset`, a tagline, her skill icons in framed
tiles, and the portrait at the right.

```
python tools/release_art.py Vector portrait.png s1.png s2.png s3.png passive.png --accent 255,138,36 --out ../Releases/nexus
```

Vector's accent is orange (255, 138, 36), the HUD's.

## The Discord post

Markdown, one message (under 2000 characters; about 1650 for Vector):

```md
# <page title>

<the opening paragraph, without the "pair it with one" clause>

## <Passive's name> (passive)
- ...

## Skills (Z, X, C)
- **<Skill>**: what it does. Cooldown.

## <her resource> / ## <her own section> / ## T-Doll body
- one or two bullets each

## Requirements
- HellForge 0.4 or later
- Bingus Shared Loader and HD2Runtime (for the HUD)
- Mod Options Menu (optional, for in-game settings)

## Download
- Nexus: <url>
- AyakaMods: <url>
```

HUD, Settings and Credits are left out of the post (they are on the pages). Links in `<` `>` stop Discord's previews.

## In-game options: decisions

Where a kit's numbers are changed in game.

| Decision | What was chosen |
|---|---|
| Direction | The kits move from the Mod Options Menu to a screen of their own, drawn by HellForge's screens over the game (SDK 17, `add_screen`: the kind of overlay the F8 editor is). Vector is the first (`mods/vector/src/vector_options.h`). |
| Until the release HellForge has screens | The Mod Options Menu stays, with the same rows. A released zip's manifest asks for the SDK the release HellForge has (13 today), so the screen's code ships but stays off; a copy installed on a HellForge with screens gets `"sdk": 17`. One source for both. |
| Its key | `options_key` in the kit's .ini (F9). Never F5 or F8. |
| On/off settings | A switch, never a 0 to 1 slider: on the screen and as `toggle` rows on the Mod Options Menu. |
| Wide ranges (cooldowns, durations, flat damage) | A field to type in, with - and +. Its limits are the setting's own in the .ini. |
| Few steps (multipliers, counts, percents) | A slider. |
| The T-Doll's Hellbomb | Every GFL2 kit has the row `Body: Hellbomb When Exhausted`, a toggle, with the tip `Running her stamina dry sets off a Hellbomb on her. For fun.` |
| Reset | Right click or Backspace puts a row back to the .ini's value; a tick on a slider shows where that is. |
| Saving | When the screen closes, what differs from the .ini goes to `%LOCALAPPDATA%\HellForgeMods\<kit>.ini` (HellForge redirects that folder to `<installation>\userdata\environment\Local`); mod folders are read-only while the game runs. |
| Character selection | On the screen: a chip for each installed kit and an Off. Off is a kit of its own in the shared roster (`~off`), so the released kits need no change. Saved to `HellForgeMods\kits.ini`. A pick takes effect at the next mission start. |
| Still open | The Off only exists where a kit with the screen is installed; it belongs in the kit base once more kits have the screen. With both the screen and the Mod Options Menu installed, each sets its own saved character at start. |
