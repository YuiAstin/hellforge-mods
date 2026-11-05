/* Koleda's Sinner: driving an empty exosuit with the game's own locomotion, as Exo Companion by M!ni$try0fSc!eиce does
 * it (docs/research/exo-companion.md on branch koleda): the stick input written into its VehicleLocomotion record, the
 * engine and the physics mover a pilot leaves behind switched on. Movement only: the Sinner's firing is the kit's own
 * (spawned rounds from its arm positions, credited to her, as Exusiai's summoned guns), not the game's weapon trigger.
 *
 * Everything the kit does to drive the Sinner goes through these four calls, so the probe's findings plug in here:
 *   SinnerDriveReady()                          1 when the record layout is known (the ini's loco_* and stick_* settings)
 *   SinnerDriveStart(entity)                    engine on, mover on; 1 when the Sinner can be driven from now on
 *   SinnerDriveStick(entity, fwd, side, turn)   the stick, each -1 .. 1; 1 when written
 *   SinnerDriveStop(entity)                     stick to 0, engine off; 1 when done
 * Each returns 0 while its part is unknown, and the kit falls back (the warp setter's slide).
 *
 * Known now: nothing is confirmed in game. The stick write is written and takes the record's place from the ini
 * (loco_manager: the game.dll offset of the manager pointer; loco_map / loco_records: where its entity map and records
 * sit in the manager, as the game's other per-entity managers keep them; loco_stride: a record's size, 0xA4 per the
 * Exo Companion signature; stick_offset_fwd / _side / _turn: the three floats in a record). Any of them -1: not known.
 * The engine and the mover have no signature yet: their calls return 0. drive_test 1 lets the stick run
 * without them (to see whether the stick alone moves the suit).
 *
 * The kit defines, before including this: s_drive, s_driveTest, s_locoManager, s_locoMap, s_locoRecords, s_locoStride,
 * s_stickFwd, s_stickSide, s_stickTurn (its settings; tools/inigen.py reads settings from the kit's .c only). */
#ifndef SINNER_DRIVE_H
#define SINNER_DRIVE_H

/* the Sinner's VehicleLocomotion record: its address (0: not known, or the Sinner has none) */
static uintptr_t SinnerLocoRecord(uint32_t e)
{
    if (s_locoManager < 0 || s_locoMap < 0 || s_locoRecords < 0 || s_locoStride < 16 || s_locoStride > 4096 || !e) return 0;
    uintptr_t m = Pointer(game + (uint32_t)s_locoManager), recs;
    uint32_t i;
    if (!m || !Lookup(m + (uint32_t)s_locoMap, e, 1048576, &i) || i > 65535 || !(recs = Pointer(m + (uint32_t)s_locoRecords))) return 0;
    return recs + (uintptr_t)s_locoStride * i;
}
static int StickOffsetOk(int off) { return off >= 0 && off + 4 <= s_locoStride; }

static int SinnerDriveReady(void)
{
    if (!s_drive) return 0;
    return s_locoManager >= 0 && s_locoMap >= 0 && s_locoRecords >= 0 && StickOffsetOk(s_stickFwd) &&
           (s_stickSide < 0 || StickOffsetOk(s_stickSide)) && (s_stickTurn < 0 || StickOffsetOk(s_stickTurn));
}

/* the engine (Exo Companion's eng_set, near game.dll+0xFD9D40): no signature yet */
static int SinnerEngine(uint32_t e, int on) { (void)e; (void)on; return 0; }
/* the physics mover a pilot leaves behind (Exo Companion's mover, near game.dll+0x6FE480): no signature yet */
static int SinnerMover(uint32_t e) { (void)e; return 0; }

static int SinnerDriveStart(uint32_t e)
{
    if (!SinnerDriveReady()) return 0;
    uintptr_t rec = SinnerLocoRecord(e);
    if (!rec) { Log("Sinner drive: entity %u has no locomotion record at the ini's place (loco_manager %d, map +%d, records +%d)", e, s_locoManager, s_locoMap, s_locoRecords); return 0; }
    int engine = SinnerEngine(e, 1), mover = SinnerMover(e);
    if (!(engine && mover) && !s_driveTest)
    {
        Log("Sinner drive: its locomotion record is at %p, but the engine and mover calls are not written yet (drive_test 1 tries the stick alone)", (void*)rec);
        return 0;
    }
    float stick[3] = {0, 0, 0};
    int offs[3] = {s_stickFwd, s_stickSide, s_stickTurn};
    for (int k = 0; k < 3; k++) if (offs[k] >= 0) ReadAt(rec + (uint32_t)offs[k], &stick[k], 4);
    Log("Sinner drive: on for entity %u (record %p; stick now %.2f / %.2f / %.2f; engine %d, mover %d%s)", e, (void*)rec, (double)stick[0],
        (double)stick[1], (double)stick[2], engine, mover, engine && mover ? "" : ", drive_test: the stick alone");
    return 1;
}

static int SinnerDriveStick(uint32_t e, float fwd, float side, float turn)
{
    if (!SinnerDriveReady()) return 0;
    uintptr_t rec = SinnerLocoRecord(e);
    if (!rec) return 0;
    float v[3] = {fwd, side, turn};
    int offs[3] = {s_stickFwd, s_stickSide, s_stickTurn}, wrote = 0;
    for (int k = 0; k < 3; k++)
    {
        if (offs[k] < 0) continue;
        float x = isfinite(v[k]) ? (v[k] < -1 ? -1 : v[k] > 1 ? 1 : v[k]) : 0;
        if (hf->write(rec + (uint32_t)offs[k], &x, 4)) return 0;
        wrote++;
    }
    static unsigned told;
    if (wrote && told < 6)
    {
        float back = 0;
        ReadAt(rec + (uint32_t)s_stickFwd, &back, 4);
        told++;
        Log("Sinner drive: stick %.2f / %.2f / %.2f written (forward reads back %.2f)", (double)fwd, (double)side, (double)turn, (double)back);
    }
    return wrote > 0;
}

static int SinnerDriveStop(uint32_t e)
{
    int done = SinnerDriveStick(e, 0, 0, 0);
    SinnerEngine(e, 0);
    return done;
}

#endif
