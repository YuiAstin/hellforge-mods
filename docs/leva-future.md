# Leva — future plans

Ideas parked for later. Nothing here is built yet.

## Duo arc with Lenna (Positive / Negative Charge)

- **Lenna** applies **Positive Charge** (instead of only her Conductivity mark).
- **Leva** keeps applying **Negative Charge** to enemies.
- When a Positive-charged Lenna and a Negative-charging Leva stand within ~5 m of each other, they set off **one arc explosion** between them (with a cooldown).

What stands in the way:

- **Each mod only knows its own player.** Leva's mod can see every Helldiver's position, but not which one is Lenna.
  - Simple version: any other Helldiver within 5 m counts (random squadmates would trigger it too).
  - Proper version: Lenna's mod announces itself over the network (HD2Runtime has peer-messaging research; unchecked whether a code mod can use it).
- **Only the host can spawn explosions.** The duo arc works only when Lenna or Leva is the host.
- **Testing needs two players.** HellForge's session results have a `multiInstance` setting; if it can run two local copies, one PC could test it. Otherwise it needs a friend.

## Wise Support rework

Wise Support (The Fox's Scheme) rarely procs: her skills charge whole groups at once and kill them, so the "newly charged enemy near her" zaps are spent on targets already dying. Options discussed:

1. **Bodyguard** — every ~2 s, zap the nearest enemy within ~10 m (charged or not), charging it.
2. **Chain on kills** — when a charged enemy dies, zap the next one nearby.
3. **Hunt the leftovers** — every ~3 s, zap up to 2 charged enemies still alive within 20 m.
4. **1 + 2 together** (suggested): bodyguard zaps keep the ultimate cooldown cut; chain zaps don't.
