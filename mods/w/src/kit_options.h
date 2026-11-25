/* w: the options screen's rows. hand-written (the first build has no HUD OPTIONS table for tools/optgen.py to read). */
static const OpRow kOpRows[] = {
    {"koh_rounds", "King of Hearts", "Rounds", OP_FIELD, 1, 200, 1,
     "Rounds the launcher is handed with."},
    {"koh_damage", "King of Hearts", "Damage", OP_SLIDER, 1, 10, 0.25f,
     "Its rounds' blasts deal this times their damage."},
    {"koh_cooldown", "King of Hearts", "Cooldown", OP_FIELD, 0, 600, 1,
     "Seconds before King of Hearts can be used again, from when the launcher is put away."},
    {"koh_freeze_seconds", "King of Hearts", "Freeze", OP_SLIDER, 0, 15, 0.5f,
     "Seconds an enemy caught by a round stays frozen in its pose (0: no freeze)."},
    {"koh_freeze_radius", "King of Hearts", "Freeze Radius", OP_SLIDER, 1, 15, 0.5f,
     "Metres from a round's blast within which enemies are frozen."},
    {"rare_max", "Pose Freeze", "Unit Search", OP_SLIDER, 1, 16, 1,
     "When an enemy's unit is not found by its id, it is looked for by its place among unit types with at most this many units.", NULL, 1, OP_LIVE},
};
static const char* const kOpKeyLabels[3] = {"King of Hearts", "", ""};
