# Exusiai the New Covenant (design, 2026-11-04)

A character of her own, separate from Exusiai (the drone-gun support kit): a self-draining all-in gunner. Both can be
installed; the roster picks which one acts. Her kit is Arknights' (arknights.wiki.gg/wiki/Exusiai_the_New_Covenant),
fitted to Helldivers 2 with Yui's changes. Built on the kit base.

## Trait and talents

| Arknights | In HD2 | Built from |
|---|---|---|
| **Trait:** continually loses HP | A slow health drain (ini, can be off), paid back by Firepower FM's healing | Body edits, a self-damage tick |
| **Firepower FM:** when an allied operator spends ammo, she heals 6% HP; 25% chance of a 165% ATK splash on enemies in that operator's range | Her shots, your sentries' and your squadmates': each heals her a little; 25% chance of a small splash shell near what they hit | Hit events, explosion spawning (Voymastina) |
| **Covenant of Bullets:** +13% ATK for operators with ammo skills (Laterano: double) | +13% damage while any of her skills runs | The damage pipeline |

## Skills

**S1, Sweeping the Skies** (automatic, with a cooldown; flyers are rare in HD2, so "the sky" is anything airborne)
- Ready: her first shot that hits an airborne enemy starts it: 8 empowered rounds, x2.5.
- While it runs, her shots on an airborne enemy get **armour penetration 10**: nothing in the sky bounces her rounds
  (the hit pipeline's penetration edit, as Voymastina's Predation +2 AP).
- **Markers:** while S1 is ready (off cooldown), a target marker over every airborne enemy near her; none on
  cooldown, so the markers are the cooldown's readout. Lemuen's world-to-screen markers (Chef Shared Loader's
  projection, with permission).
- **Airborne:** some metres above the ground beneath it (a ground raycast, as Voymastina's teleport): leaping
  Hunters and Pouncers, jump-pack Devastators, Stingrays, gunships, Illuminate overseers. The height is an ini value.
- Pressing S1 again while it runs dumps the rounds left into random enemies near her (Arknights' manual end).

**S2, Shootoholic**
- Always: ATK boost (x3.0 for 35 shots) and a Barrier of 250% of her HP, decaying over 30 s (an absorb pool in the
  damage hook).
- The fire-rate steal becomes **free ammo**: for the duration her gun spends no rounds (a fire-rate boost without
  reloads). Arknights already gives her the rest when there is no ally to steal from. A live fire-rate edit is not
  used: from the Maxigun's testing, it only takes effect at the next call-in or respawn.
- How: her magazine watched (the HUD's ammo report) and its rounds written back after each shot. HD2Runtime refills
  turret magazines so; untested on a player's gun. Fallback: S2 refills her magazine when it ends.

**S3, Guaranteed Success!**
- Every trigger pull fires **5 rounds** (Charging Mode's extra rounds), 10 pulls.
- **Delivery Coordinates:** a beacon she throws. Each S3 shot also shells the beacon's spot, and **your longest
  stratagem cooldown is refreshed** (the stratagem record's cooldown end, as the Veterancy System's Maxigun: tested
  and released).
- **+1 reinforcement** (what Arknights' redeploy stands for): where the count lives is open. The Mission Probe logs
  every stratagem's uses after landing: if Reinforce carries the count, it is one write.

## Open items

1. The airborne height threshold (a test).
2. Whether writing her magazine's rounds back holds on a player's gun.
3. Where the reinforcement count lives (Mission Probe, tonight).
4. The beacon's throw: **a stratagem ball** for now (Yui, 2026-11-06).
