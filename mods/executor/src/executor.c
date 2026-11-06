/* Executor (yui.executor): Executor the Ex Foedere's kit from Arknights for your own Helldiver (solo or host): a
 * Laterano Guard with a shotgun, whose hits heal him and whose skills are counted in rounds. Built on the kit base
 * (kitbase/, docs/kit-base.md); the design is docs/executor-design.md (decisions as of 2026-11-06).
 *
 *   Trait: every enemy one of his shots hits heals him heal_pct of his maximum health (50%), up to heal_cap enemies a
 *     shot (3), and each heal mends every injured limb in full. He cannot stim: stim_undo 1 takes back at once any heal
 *     that is not his own (see "No stims" below for why it is an undo and not a refusal).
 *   The Chosen One (passive): each shot has chosen_pct (20%) to fire a free extra round (the gun's own projectile, every
 *     pellet), plus chosen_step (5%) per round spent while a skill runs.
 *   Empathy by Shotgun (passive): +1 skill round per other Laterano kit installed beside him (Exusiai, Lemuen, Exusiai
 *     the New Covenant), read from the kit roster at mission start.
 *   Testament Exacted (key_s1, Z): s1_rounds shots (8) deal s1_mult (x1.5) and pierce s1_ap more armour.
 *   Close Combat (key_s2, X): s2_rounds shots (12) deal s2_mult (x1.8); hits on him deal s2_reduce (80%) less; heal cap
 *     +1; any hit on him has s2_dodge (40%) to be negated and give a round back.
 *   Damnatus Ex Foedere (key_s3, C): s3_rounds shots (16) deal s3_mult (x2.8) plus s3_step (+6%) a round spent (at most
 *     s3_stacks, 30), heals x s3_heal_mult (3); when it ends, an s3_explosion blast on every enemy it hit, its damage
 *     x s3_blast_mult (2.5).
 *   Area rounds (0.2.0, Yui: "spawn additional round matching his gun to every enemies in a 7 meter square in front of
 *     him, expands to 15 meters when using S3"): each of his shots also fires one round of his gun's own projectile (every
 *     pellet) at each living enemy in a square in front of him, area_size m deep by area_size m wide (area_size_s3 while
 *     Damnatus Ex Foedere runs), at most area_max enemies, nearest first. See "Area rounds" below.
 * One skill runs at a time; a skill's cooldown starts when it ends (Arknights' SP charges after the skill).
 *
 * Rounds are his shots, as Lemuen's "until she reloads": no magazine writes, no fire-rate changes. A shot is seen in the
 * magazine the HUD reads (executor_ammo, as Lemuen's and Exusiai's), else in HellForge's shots stat (HF_STAT_SHOTS,
 * pellets fired by the gun in hand, divided by its pellets a shot). Pieces and where they come from:
 *   the heal              HellForge's heal / heal_limb / limb_state (SDK 11), as Koleda's Kinetic Recovery heals
 *   the damage            the kit base's damage event (kb_damage.h: IsShot, ScaleDamage), as Lemuen's skills
 *   armour penetration    the hit pipeline's edit (kb_hits.h kb_hit_before, +0x9C), as Faelynn's Corrosion shred
 *   the free rounds       Exusiai's Charging Mode free bursts (spawn_projectile toward the reticle, pellet by pellet)
 *   S2's damage taken     the damage event on him, as Voymastina's Mastiff's Pledge
 *   S3's blasts           Lemuen's Memento Mori (one on each target of a list), with Koleda's own-blast slots
 *   the roster            kb_roster.h (this kit acts this mission; the other kits installed)
 *   health record writes  Koleda's (the health manager's records, +20 health), for the stim undo
 *
 * No stims: the research (scratchpad stim-research.md and probes/stim-probe on branch stim-probe, Stim Probe 0.2.0) found
 * no pinned stim action, ability id or stim count; the probe that would find them has not been run. So refusing the
 * stim is not proven, and this build has the design's fallback: a stim's heal is undone right after it lands (his
 * health written back down), the stim wasted. stim_undo 0 leaves stims as they are: the default since 0.1.1 (in game the
 * stim's effect still showed, and his own heals, arriving as heal events a few seconds late, were taken back too).
 *
 * Research credit: the kit base's (Helldiver Survival Rework by enochlophobianumerophobia, MIT; HD2Runtime by
 * SkyeShade); the camera pose as Apache - Rise Above (RSNLRiggs, after Khi) reads it. See THIRD_PARTY_NOTICES.md.
 * TEST BUILD: untested in game. */
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <windows.h>
#include "hellforge_mod.h"
#include "hellforge_components.h"
#include "hellforge_content.h"

#define VERSION "0.2.3"
#define BUILD "25480438"

/* S3 sets off a blast on every enemy it hit, 0.1 s apart: room for 12 of his own blasts within the guard's 1.2 s */
#define KB_OWN_SLOTS 16

#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"
#include "kitbase/kb_body.h"
#include "kitbase/kb_hits.h"
#include "kitbase/kb_damage.h"
#include "kitbase/kb_settings.h"
#include "kitbase/kb_roster.h"

/* with other character kits installed: the HUD's roster and picker calls (kitbase/kb_roster.h) */
KB_ROSTER_EXPORTS(executor)

/* an SDK call this HellForge has (the struct is as long as the SDK that built it) */
#define HAS(fn) (hf->size >= offsetof(HfApi, fn) + sizeof hf->fn && hf->fn)

/* ---- settings ------------------------------------------------------------------------------------------------- */
static float s_healPct = 50, s_shotGap = 0.12f;
static int s_healCap = 3, s_healLimbs = 1, s_healShotgunsOnly = 0, s_stimUndo = 0;
static float s_chosenPct = 20, s_chosenStep = 5, s_chosenDelay = 0.08f, s_chosenSpread = 3;
static int s_freeFallback = 7, s_empathy = 1, s_noProjRound = 179;
static float s_maxRounds = 30;
static float s_s1Cd = 20, s_s1Mult = 1.5f, s_s1Ap = 2;
static int s_s1Rounds = 8;
static float s_s2Cd = 30, s_s2Mult = 1.8f, s_s2Reduce = 0.8f, s_s2Dodge = 40;
static int s_s2Rounds = 12, s_s2DodgeDot = 0;
static float s_s3Cd = 60, s_s3Mult = 2.8f, s_s3Step = 0.06f, s_s3HealMult = 3, s_s3BlastMult = 2.5f, s_s3Every = 0.1f;
static int s_s3Rounds = 16, s_s3Stacks = 30, s_s3Row = 404;
static int s_areaRounds = 1, s_areaMax = 12, s_areaPellets = 3;
static float s_areaSize = 15, s_areaSizeS3 = 25;   /* 0.2.2: 15 m, 25 m in S3 (0.2.0: 7 / 15) */

static const KbSetting kSettings[] = {
    KB_F("heal_pct", s_healPct, 0, 100), KB_I("heal_cap", s_healCap, 0, 32), KB_I("heal_limbs", s_healLimbs, 0, 1),
    KB_I("heal_shotguns_only", s_healShotgunsOnly, 0, 1), KB_F("shot_gap", s_shotGap, 0.02, 1), KB_I("stim_undo", s_stimUndo, 0, 1),
    KB_F("chosen_pct", s_chosenPct, 0, 100), KB_F("chosen_step", s_chosenStep, 0, 100), KB_F("chosen_delay", s_chosenDelay, 0, 1),
    KB_F("chosen_spread", s_chosenSpread, 0, 30), KB_I("free_round_fallback", s_freeFallback, 0, 100000), KB_F("max_rounds", s_maxRounds, 1, 200),
    KB_I("no_projectile_round", s_noProjRound, 0, 100000),
    KB_I("empathy", s_empathy, 0, 1),
    KB_I("area_rounds", s_areaRounds, 0, 1), KB_F("area_size", s_areaSize, 1, 100), KB_F("area_size_s3", s_areaSizeS3, 1, 100),
    KB_I("area_max", s_areaMax, 0, 32), KB_I("area_pellets", s_areaPellets, 0, 32),
    KB_F("s1_cooldown", s_s1Cd, 0, 600), KB_I("s1_rounds", s_s1Rounds, 1, 200), KB_F("s1_mult", s_s1Mult, 1, 20), KB_F("s1_ap", s_s1Ap, 0, 10),
    KB_F("s2_cooldown", s_s2Cd, 0, 600), KB_I("s2_rounds", s_s2Rounds, 1, 200), KB_F("s2_mult", s_s2Mult, 1, 20), KB_F("s2_reduce", s_s2Reduce, 0, 1),
    KB_F("s2_dodge", s_s2Dodge, 0, 100), KB_I("s2_dodge_dot", s_s2DodgeDot, 0, 1),
    KB_F("s3_cooldown", s_s3Cd, 0, 3600), KB_I("s3_rounds", s_s3Rounds, 1, 200), KB_F("s3_mult", s_s3Mult, 1, 20), KB_F("s3_step", s_s3Step, 0, 1),
    KB_I("s3_stacks", s_s3Stacks, 0, 200), KB_F("s3_heal_mult", s_s3HealMult, 0, 10), KB_I("s3_explosion", s_s3Row, 0, 100000),
    KB_F("s3_blast_mult", s_s3BlastMult, 0, 20), KB_F("s3_every", s_s3Every, 0.02, 2),
    KB_F("hp_mult", s_hpMult, 0.1, 10), KB_F("stamina_mult", s_staminaMult, 0.1, 20), KB_I("hellbomb_on_exhaustion", s_hellbomb, 0, 1),
    KB_I("hellbomb_explosion", s_hellbombRow, 0, 100000), KB_F("hellbomb_hold", s_hellbombHold, 0, 10),
};

/* The default executor.ini: tools/inigen.py writes the file from this text (the mod never writes it). His body's values
 * are written as numbers: the kit base's defaults are the T-Dolls', and his are set in hellforge_mod_init. */
static __attribute__((unused)) void DefaultIni(FILE* f)
{
    fprintf(f, "; Executor %s (TEST BUILD: untested in game). Keys: a letter, digit, F1-F24, Mouse3, Mouse4 or Mouse5, with\n"
               "; Shift+, Ctrl+ or Alt+ in front for a combo (Shift+Q). The game still sees the keys too, so pick ones it leaves alone.\n"
               "key_s1 = %s\nkey_s2 = %s\nkey_s3 = %s\n"
               "; His trait: every enemy one of his shots hits heals him heal_pct %% of his maximum health, up to heal_cap enemies\n"
               "; a shot (hits within shot_gap s of the shot's first are that shot's). heal_limbs 1: each heal also mends every\n"
               "; injured limb in full. heal_shotguns_only 1: only a gun firing several pellets a shot heals.\n"
               "heal_pct = %g\nheal_cap = %d\nshot_gap = %g\nheal_limbs = %d\nheal_shotguns_only = %d\n"
               "; Stims heal him as anyone (stim_undo 0, the default since 0.1.1). stim_undo 1 (experimental) took back any heal\n"
               "; not his own, but the stim's effect still showed and his own heals could be taken back too.\nstim_undo = %d\n"
               "; The Chosen One: each shot has chosen_pct %% to fire a free extra round chosen_delay s later, plus chosen_step %%\n"
               "; for each round spent while a skill runs; its pellets spread chosen_spread degrees. A gun whose round cannot be\n"
               "; spawned fires projectile free_round_fallback instead. At most max_rounds spawned rounds a second. A gun with no\n"
               "; ProjectileWeapon (the DBS-2 Double Freedom loads shells) fires its shells' own projectile, else\n"
               "; no_projectile_round (179: SG-225 Breaker buckshot; 0: such guns fire no spawned rounds).\n"
               "chosen_pct = %g\nchosen_step = %g\nchosen_delay = %g\nchosen_spread = %g\nfree_round_fallback = %d\nmax_rounds = %g\n"
               "no_projectile_round = %d\n"
               "; Empathy by Shotgun: empathy 1, +1 round to each skill for every other Laterano kit installed (Exusiai, Lemuen,\n"
               "; Exusiai the New Covenant).\nempathy = %d\n"
               "; Area rounds: area_rounds 1, each of his shots also fires one round of his gun (at most area_pellets of its pellets;\n"
               "; 0: all) at each living enemy\n"
               "; in a square in front of him: area_size m along his aim by area_size m across it, from where he stands\n"
               "; (area_size_s3 while Damnatus Ex Foedere runs); at most area_max enemies a shot, the nearest first, each in his\n"
               "; line of fire. They heal as his shot's hits (the same heal_cap), spend no skill round, roll no Chosen One, and\n"
               "; are not held back by max_rounds.\n"
               "area_rounds = %d\narea_size = %g\narea_size_s3 = %g\narea_max = %d\narea_pellets = %d\n"
               "; Skills run for their rounds (his shots), one at a time; the cooldown starts when one ends.\n"
               "; Testament Exacted: s1_rounds shots deal s1_mult times their damage and pierce s1_ap more armour.\n"
               "s1_cooldown = %g\ns1_rounds = %d\ns1_mult = %g\ns1_ap = %g\n"
               "; Close Combat: s2_rounds shots deal s2_mult times their damage; hits on him deal s2_reduce (0..1) less; one more\n"
               "; enemy a shot heals him; a hit on him has s2_dodge %% to be negated and give a round back (s2_dodge_dot 1:\n"
               "; damage over time too).\ns2_cooldown = %g\ns2_rounds = %d\ns2_mult = %g\ns2_reduce = %g\ns2_dodge = %g\ns2_dodge_dot = %d\n"
               "; Damnatus Ex Foedere: s3_rounds shots deal s3_mult times their damage, plus s3_step for each round spent (at most\n"
               "; s3_stacks); his heals x s3_heal_mult. When it ends, an s3_explosion blast (an ExplosionSettings row: 404 AC-8\n"
               "; autocannon impact; 0 none) on every enemy it hit, s3_every s apart, its damage x s3_blast_mult.\n"
               "s3_cooldown = %g\ns3_rounds = %d\ns3_mult = %g\ns3_step = %g\ns3_stacks = %d\ns3_heal_mult = %g\ns3_explosion = %d\n"
               "s3_blast_mult = %g\ns3_every = %g\n"
               "; His body: hp_mult times a Helldiver's health, stamina_mult times his sprint. hellbomb_on_exhaustion 1 sets off a\n"
               "; Hellbomb on him if he runs his stamina dry (a T-Doll's price; off for him).\n"
               "hp_mult = 1\nstamina_mult = 1\nhellbomb_on_exhaustion = 0\n",
            VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, (double)s_healPct, s_healCap, (double)s_shotGap, s_healLimbs,
            s_healShotgunsOnly, s_stimUndo, (double)s_chosenPct, (double)s_chosenStep, (double)s_chosenDelay, (double)s_chosenSpread,
            s_freeFallback, (double)s_maxRounds, s_noProjRound, s_empathy, s_areaRounds, (double)s_areaSize, (double)s_areaSizeS3, s_areaMax, s_areaPellets, (double)s_s1Cd, s_s1Rounds, (double)s_s1Mult, (double)s_s1Ap, (double)s_s2Cd,
            s_s2Rounds, (double)s_s2Mult, (double)s_s2Reduce, (double)s_s2Dodge, s_s2DodgeDot, (double)s_s3Cd, s_s3Rounds, (double)s_s3Mult,
            (double)s_s3Step, s_s3Stacks, (double)s_s3HealMult, s_s3Row, (double)s_s3BlastMult, (double)s_s3Every);
}

/* ---- state ---------------------------------------------------------------------------------------------------- */
static const char* const kSkill[3] = {"Testament Exacted", "Close Combat", "Damnatus Ex Foedere"};
static double g_cd[3];                 /* ready at */
static int g_skill;                    /* the skill running: 1..3, 0 none */
static int g_rounds, g_roundsMax;      /* its rounds left, and its rounds at the start (+ Empathy) */
static int g_spent;                    /* rounds spent in it (The Chosen One's rise, S3's stacks) */
static double g_endAt;                 /* its last round is out: it ends then (its last hits land first) */
static atomic_int g_apOn;              /* S1's armour penetration, for the hit hooks (another thread) */
static int g_bonus;                    /* Empathy by Shotgun: other Laterano kits installed */
static char g_bonusNames[96];
static unsigned g_casts[3], g_shots, g_freeShots, g_heals, g_healHits, g_limbsMended, g_dodges, g_undone, g_blasts;
static unsigned g_areaVolleys, g_areaRoundsFired, g_areaHits;
static atomic_uint g_pierced;
static int g_shotSource;               /* 1: the HUD's magazine; 2: the shots stat (the log says which, once) */

static float Roll(void) { return (float)rand() / (float)RAND_MAX * 100.0f; }

/* ---- reads: health, positions ------------------------------------------------------------------------------------ */
/* An entity's health record (Koleda's HealthRec: the health manager at game+0x3326688, its open-addressed entity hash,
 * 440-byte records with health at +20 and life at +412, 0 alive): its address, 0 unknown */
static uintptr_t HealthRec(uint32_t entity)
{
    uintptr_t m = Pointer(game + 53634696u);
    uint8_t h[88];
    if (!entity || !m || !ReadAt(m + 4112, h, sizeof h)) return 0;
    uint32_t live = Word(h + 16), cap = Word(h + 40), empty = Word(h + 44), mult = Word(h + 48);
    uintptr_t buckets = (uintptr_t)Quad(h + 32), records = (uintptr_t)Quad(h + 72);
    if (!buckets || !records || !cap || cap > 65536 || (cap & (cap - 1)) || live > 4096) return 0;
    uint32_t start = entity * mult;
    for (uint32_t k = 0; k < cap; k++)
    {
        uint8_t b[8];
        if (!ReadAt(buckets + 8u * ((start + k) & (cap - 1)), b, 8)) return 0;
        uint32_t key = Word(b), index = Word(b + 4);
        if (key == empty) return 0;
        if (key != entity) continue;
        if (index >= live) return 0;
        return records + 440u * index;
    }
    return 0;
}
static int HealthOf(uint32_t entity, int32_t* health, uint32_t* life)
{
    uintptr_t r = HealthRec(entity);
    uint8_t b[416];
    if (!r || !ReadAt(r, b, sizeof b)) return 0;
    *health = (int32_t)Word(b + 20);
    *life = Word(b + 412);
    return 1;
}
/* his maximum health: the heal event's (when one has come), else his Helldiver's Health record as it stands now */
static int32_t g_maxHp;
static int32_t MaxHealth(void)
{
    if (g_maxHp > 0) return g_maxHp;
    HfField f = HF_FIELD(Health, health);
    HfLiveValue v;
    memset(&v, 0, sizeof v);
    if (hf->get_live(self, AVATAR, &f, &v) && v.now.number >= 1 && v.now.number < 100000) return (int32_t)v.now.number;
    return 0;
}
static int TargetPos(uint32_t e, float out[3])
{
    uint8_t d[24];
    if (!Descriptor(e, d) || !UnitNode(Word(d + 12), 0, out)) return 0;
    out[2] += 1.0f;
    return 1;
}
static uint32_t UnitOf(uint32_t e) { uint8_t d[24]; return Descriptor(e, d) ? Word(d + 12) : 0; }

/* the enemy's turrets count as enemies (Lemuen 0.1.6f's EnemyTurret: the kit base leaves every turret out, so sentries
 * are never targets; a turret that runs a behaviour and is no friend is the enemy's) */
static int EnemyTurret(uint64_t res)
{
    static uint64_t keys[128];
    static int8_t vals[128];
    unsigned h = (unsigned)(res ^ (res >> 31)) & 127;
    for (unsigned i = 0; i < 128; i++)
    {
        unsigned k = (h + i) & 127;
        if (keys[k] == res) return vals[k];
        if (keys[k]) continue;
        uint32_t stride;
        int t = hf->component_record("TurretComponentData", res, &stride) != 0 && hf->component_record("BehaviorComponentData", res, &stride) != 0 &&
                !hf->component_record("AvatarComponentData", res, &stride) && !FriendRes(res);
        vals[k] = (int8_t)t;
        keys[k] = res;
        return t;
    }
    return 0;
}
/* an enemy: not a Helldiver, nothing on the friend list, something with a behaviour (kb_friends.h FoeKind) */
static int Enemy(uint32_t e)
{
    uint8_t d[24];
    if (!Descriptor(e, d)) return 0;
    uint64_t res = Quad(d);
    return res != AVATAR && (FoeKind(res) || EnemyTurret(res));
}

/* ---- the reticle (Exusiai 0.2.10's: the camera's ray, as Alva's aim; the game's raycast, as Lemuen's line of sight) */
typedef uint32_t (*WorldFn)(const void*);
typedef uint32_t (*RayFn)(uint32_t, const void*, const void*, float, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t);
static WorldFn g_worldId;
static RayFn g_raycast;
static uint32_t ActorFilter(uint32_t id, uint32_t unit)
{
    uint32_t world = id >> 30, layout, stride, key_offset, data_offset, key, index, name = 0;
    uintptr_t pool = exe + 0x2369B00 + 64u * ((id >> 28 & 3) + 10 * world);
    uint8_t header[56], record[40], body[160];
    if (!ReadAt(pool, header, 56)) return 0;
    layout = Word(header + 28); stride = layout & 65535; key_offset = layout >> 16 & 255; data_offset = layout >> 24;
    index = id & Word(header + 40);
    if (index >= Word(header + 36) || !(id & Word(header + 52)) || stride < 40 || stride > 512 || key_offset + 4 > stride || data_offset + 40 > stride) return 0;
    uintptr_t entry = (uintptr_t)Quad(header) + (uintptr_t)index * stride;
    if (!ReadAt(entry + key_offset, &key, 4) || key != id || !ReadAt(entry + data_offset, record, 40) || Word(record + 12) != unit || !(Word(record + 16) & 1)) return 0;
    uintptr_t physics = Pointer(exe + 0x27BA8A8 + 176u * world), bodies = physics ? Pointer(physics + 24) : 0;
    uint32_t body_index = Word(record + 20) & 0xFFFFFF;
    if (!bodies || body_index >= 262144 || !ReadAt(bodies + 160u * body_index, body, 160) || Word(body + 144) != id || Word(body + 148) != unit) return 0;
    uintptr_t filters = Pointer(exe + 0x27C5E48), names;
    uint32_t count, filter = Word(body + 108) & 127;
    if (!filters || !ReadAt(filters + 248, &count, 4) || count > 128 || filter >= count || !(names = Pointer(filters + 256)) || !ReadAt(names + 4u * filter, &name, 4)) return 0;
    return name;
}
#define FILTER_FOLIAGE 0x04A8FBF9u
static uint32_t g_foeUnit[FOES];
static int g_nfoeUnit;
static double g_foeUnitAt = -1;
static int FoeUnit(uint32_t u)
{
    if (g_now - g_foeUnitAt > 0.25 || g_foeUnitAt < 0)
    {
        g_foeUnitAt = g_now;
        g_nfoeUnit = 0;
        for (int k = 0; k < FOES; k++) if (g_foe[k]) { uint32_t fu = UnitOf(g_foe[k]); if (fu) g_foeUnit[g_nfoeUnit++] = fu; }
    }
    for (int i = 0; i < g_nfoeUnit; i++) if (g_foeUnit[i] == u) return 1;
    return 0;
}
static int WorldId(uint32_t* id)
{
    if (!g_raycast || !g_worldId) return 0;
    uintptr_t world = Pointer(game + 0x346BFA0);
    if (!world) return 0;
    *id = g_worldId((const void*)world);
    return *id < 4;
}
/* the first wall along a ray (len + 1: clear), skipping him, leaves and bodies */
static float FirstWall(uint32_t id, const float from[3], const float dir[3], float len, uint32_t me)
{
    uint8_t hits[16 * 44];
    memset(hits, 0, sizeof hits);
    uint32_t n = g_raycast(id, from, dir, len, 2, 1, 0, 0x80000009u, me, hits, 16);
    if (n > 16) n = 16;
    float best = len + 1;
    for (uint32_t i = 0; i < n; i++)
    {
        float d;
        memcpy(&d, hits + 44u * i + 24, 4);
        uint32_t hu = Word(hits + 44u * i + 28);
        if (!isfinite(d) || d < 1.2f || d >= best || (hu && hu == me)) continue;
        uint32_t filt = ActorFilter(Word(hits + 44u * i + 32), hu);
        if (filt == FILTER_FOLIAGE || (hu && (filt == 0x236122F0u || filt == 0xD5B33D56u || FoeUnit(hu)))) continue;
        best = d;
    }
    return best;
}
/* the camera (his unit's world, its camera matrix at +0x230920: Apache - Rise Above's read) */
static int CamView(float pos[3], float fwd[3])
{
    uint8_t d[24];
    if (!Descriptor(atomic_load(&g_avatar), d)) return 0;
    uint32_t unit = Word(d + 12), slot = unit & 0x3FFFFF, id = 0;
    uintptr_t registry = Pointer(exe + 0x1A100F0), objects, object, w;
    float m[16];
    if (!registry || !(objects = Pointer(registry + 0x88)) || !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit ||
        !(w = Pointer(object + 0x18)) || !ReadAt(w + 0x230920, m, 64)) return 0;
    for (int k = 0; k < 16; k++) if (!isfinite(m[k])) return 0;
    if (fabsf(m[15] - 1) > 1e-3f) return 0;
    memcpy(fwd, m + 4, 12);
    memcpy(pos, m + 12, 12);
    float n = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    if (!(n > 0.5f)) return 0;
    for (int k = 0; k < 3; k++) fwd[k] /= n;
    return 1;
}
/* where the camera's ray first meets a wall, within 300 m (else 300 m out) */
static int ReticlePoint(float out[3])
{
    float cam[3], fwd[3];
    if (!CamView(cam, fwd)) return 0;
    float len = 300;
    uint32_t id;
    if (WorldId(&id) && QueryIdle())
    {
        float w = FirstWall(id, cam, fwd, 300, UnitOf(atomic_load(&g_avatar)));
        if (w < len) len = w;
    }
    for (int k = 0; k < 3; k++) out[k] = cam[k] + fwd[k] * len;
    return 1;
}

/* ---- the gun in hand: its round and pellets a shot ------------------------------------------------------------------ */
static uint32_t g_heldEntity;
static uint64_t g_heldRes;
typedef struct GunInfo { uint64_t res; uint32_t projectile, pellets, stat_per, spawn; } GunInfo;   /* spawn: the type spawned for it (0: projectile) */   /* stat_per: the shots stat's count a shot */
static GunInfo g_gunInfo[24];
static int g_ngunInfo;
/* a projectile's pellets a shot (its settings row, Exusiai 0.2.3's read); 1 when unknown */
static uint32_t PelletsOf(uint32_t type)
{
    if (!type) return 1;
    HfField f = HF_FIELD(ProjectileSettings, pellet_count);
    HfLiveValue v;
    memset(&v, 0, sizeof v);
    if (hf->get_live(self, (HfRef)type, &f, &v) && v.base.number >= 1 && v.base.number <= 32) return (uint32_t)v.base.number;
    return 1;
}
/* 0.2.3: the DBS-2 Double Freedom's shell (projectile 317) is not one HellForge can spawn (0.2.2 fell back to one
 * round of projectile 7): his copy of the SG-225 Breaker's buckshot (179, spawnable), the shell's look (visual_from)
 * and, once in a mission, the shell's own damage (set_live), every pellet of the shell */
#define DBS2_SHELL 317u
static uint32_t g_shellCopy;
static int g_shellDamageDone;
static void ShellDamage(void)
{
    if (g_shellDamageDone || !g_shellCopy || !hf->get_live || !hf->set_live) return;
    g_shellDamageDone = 1;
    HfField f = HF_FIELD(ProjectileSettings, damage);
    HfLiveValue v;
    memset(&v, 0, sizeof v);
    if (!hf->get_live(self, (HfRef)DBS2_SHELL, &f, &v) || v.base.number <= 0) { Log("the DBS-2 shell copy: the shell's damage could not be read (the Breaker's stays)"); return; }
    HfValue d = HF_U32(v.base.number);
    int r = hf->set_live(self, (HfRef)g_shellCopy, &f, &d, 0);
    Log("the DBS-2 shell copy (projectile %u): the shell's damage id %.0f %s", g_shellCopy, v.base.number, r ? "set" : "refused (the Breaker's stays)");
}
/* a weapon's round (Exusiai's InfoOf: ProjectileWeapon.projectile; none when it fires an entity a shot) */
static const GunInfo* InfoOf(uint64_t res)
{
    for (int i = 0; i < g_ngunInfo; i++) if (g_gunInfo[i].res == res) return &g_gunInfo[i];
    if (!res || res == AVATAR || g_ngunInfo >= 24) return NULL;
    GunInfo* g = &g_gunInfo[g_ngunInfo++];
    memset(g, 0, sizeof *g);
    g->res = res;
    HfField proj = HF_FIELD(ProjectileWeapon, projectile), entity = HF_FIELD(ProjectileWeapon, projectile_entity);
    HfLiveValue v;
    memset(&v, 0, sizeof v);
    if (hf->get_live(self, res, &proj, &v) && v.base.number > 0) g->projectile = (uint32_t)v.base.number;
    memset(&v, 0, sizeof v);
    if (hf->get_live(self, res, &entity, &v) && v.base.number != 0) g->projectile = 0;
    g->pellets = PelletsOf(g->projectile);
    g->stat_per = g->pellets;   /* a ProjectileWeapon: the stat counts each pellet */
    /* 0.2.2: a gun loaded round by round (the DBS-2 Double Freedom: WeaponRounds, no ProjectileWeapon) fires its rounds'
     * own projectile, and the shots stat counts one a shell (0.2.1 divided it by the stand-in's 11 pellets: 1 shot in 11) */
    const char* how = "";
    if (!g->projectile)
    {
        HfField rp = HF_FIELD(WeaponRounds, primary_projectile);
        memset(&v, 0, sizeof v);
        if (hf->get_live(self, res, &rp, &v) && v.base.number > 0 && v.base.number < 100000)
        { g->projectile = (uint32_t)v.base.number; how = " (its shells' own projectile)"; }
        else if (s_noProjRound > 0) { g->projectile = (uint32_t)s_noProjRound; how = " (none of its own: no_projectile_round stands in)"; }
        g->pellets = PelletsOf(g->projectile);
        g->stat_per = 1;
        if (g->projectile == DBS2_SHELL && g_shellCopy) { g->spawn = g_shellCopy; ShellDamage(); how = " (its shells, spawned as his copy of them)"; }
    }
    Log("weapon %016llx fires projectile %u, %u pellet%s a shot%s", (unsigned long long)res, g->projectile, g->pellets, g->pellets == 1 ? "" : "s",
        g->projectile ? how : " (no projectile: no free rounds from it)");
    return g;
}

/* ---- the skills ------------------------------------------------------------------------------------------------- */
static uint32_t g_s3Hit[64];           /* the enemies S3 hit, for its end */
static int g_ns3Hit;
static uint32_t g_blastQ[64];
static int g_nblast;
static double g_blastNext;
static double SkillCd(int i) { return i == 0 ? s_s1Cd : i == 1 ? s_s2Cd : s_s3Cd; }
static int SkillRounds(int i) { return (i == 0 ? s_s1Rounds : i == 1 ? s_s2Rounds : s_s3Rounds) + (s_empathy ? g_bonus : 0); }
static void Cast(int i)
{
    if (g_skill) { static unsigned told; if (told++ < 5) Log("%s: %s is running (%d rounds left)", kSkill[i], kSkill[g_skill - 1], g_rounds); return; }
    if (g_now < g_cd[i]) return;
    g_skill = i + 1;
    g_rounds = g_roundsMax = SkillRounds(i);
    g_spent = 0;
    g_endAt = 0;
    g_casts[i]++;
    if (i == 0) atomic_store(&g_apOn, 1);
    if (i == 2) g_ns3Hit = 0;
    if (i == 0) Log("%s: %d rounds (%d + %d Empathy), x%.2f and +%.0f armour penetration", kSkill[i], g_rounds, g_rounds - (s_empathy ? g_bonus : 0),
                    s_empathy ? g_bonus : 0, (double)s_s1Mult, (double)s_s1Ap);
    else if (i == 1) Log("%s: %d rounds, x%.2f, %.0f%% less damage taken, heal cap %d, %.0f%% to dodge a hit (a round back)", kSkill[i], g_rounds,
                         (double)s_s2Mult, (double)(s_s2Reduce * 100), s_healCap + 1, (double)s_s2Dodge);
    else Log("%s: %d rounds, x%.2f +%.0f%% a round (at most %d), heals x%.1f, a blast on each enemy hit at the end", kSkill[i], g_rounds,
             (double)s_s3Mult, (double)(s_s3Step * 100), s_s3Stacks, (double)s_s3HealMult);
}
static void End(const char* why)
{
    if (!g_skill) return;
    int i = g_skill - 1;
    g_skill = 0;
    g_endAt = 0;
    atomic_store(&g_apOn, 0);
    g_cd[i] = g_now + SkillCd(i);
    Log("%s ends (%s): %d of %d rounds spent; cooldown %.0f s", kSkill[i], why, g_spent, g_roundsMax, SkillCd(i));
    if (i == 2 && s_s3Row > 0 && g_ns3Hit > 0)
    {
        g_nblast = 0;
        for (int k = 0; k < g_ns3Hit && g_nblast < 64; k++) g_blastQ[g_nblast++] = g_s3Hit[k];
        g_blastNext = g_now;
        Log("%s: a blast (row %d) on each of the %d enemies it hit", kSkill[i], s_s3Row, g_nblast);
    }
    g_ns3Hit = 0;
}
/* S3's end: one blast on each enemy it hit that still stands, s3_every s apart; each is one of his own (it spares him) */
static void BlastFrame(void)
{
    while (g_nblast > 0 && g_now >= g_blastNext)
    {
        uint32_t e = g_blastQ[g_nblast - 1];
        int32_t hp;
        uint32_t life;
        float q[3];
        if (!HealthOf(e, &hp, &life) || hp <= 0 || !TargetPos(e, q)) { g_nblast--; continue; }
        q[2] -= 0.5f;
        unsigned slot = g_ownNext++ % KB_OWN_SLOTS;
        memcpy(g_ownPos[slot], q, 12);
        atomic_store(&g_ownEmp[slot], 0);
        atomic_store(&g_ownMs[slot], GetTickCount64());
        int r = hf->spawn_explosion(self, (uint32_t)s_s3Row, q);
        if (r == HF_ACT_WAITING) { atomic_store(&g_ownMs[slot], 0); g_blastNext = g_now + 0.1; return; }
        if (Act("Damnatus Ex Foedere's blast", r))
        {
            g_blasts++;
            static unsigned told;
            if (told++ < 12) Log("Damnatus Ex Foedere: a blast on entity %u", e);
        }
        g_nblast--;
        g_blastNext = g_now + s_s3Every;
    }
}
/* an explosion hit near one of his S3 blasts (the last 1.2 s, 6 m) */
static int S3BlastHit(const HfDamageEvent* d)
{
    if (d->kind != 4) return 0;
    unsigned long long now = GetTickCount64();
    for (int i = 0; i < KB_OWN_SLOTS; i++)
    {
        unsigned long long at = atomic_load(&g_ownMs[i]);
        if (at && now - at <= 1200 && Dist2(d->position, g_ownPos[i]) <= 6.0f * 6.0f) return 1;
    }
    return 0;
}

/* ---- The Chosen One: free extra rounds (Exusiai's free bursts: the gun's own round, every pellet, toward the reticle) */
static int g_freeOwed;
static double g_freeAt;
static double g_projTokens = 30, g_projAt;
static int ProjBudget(void)
{
    double el = g_now - g_projAt;
    g_projAt = g_now;
    if (el > 0) g_projTokens += el * s_maxRounds;
    if (g_projTokens > s_maxRounds) g_projTokens = s_maxRounds;
    return (int)g_projTokens;
}
static double g_healOwed;              /* fractions of his maximum health owed (his heals, below) */
static int g_healHitsOwed;
static uint32_t g_noSpawn[8];
static int NoSpawn(uint32_t type) { for (int k = 0; k < 8; k++) if (type && g_noSpawn[k] == type) return 1; return 0; }
static void NoSpawnAdd(uint32_t type) { if (NoSpawn(type)) return; for (int k = 0; k < 8; k++) if (!g_noSpawn[k]) { g_noSpawn[k] = type; return; } }
static void FreeFrame(void)
{
    if (g_freeOwed <= 0 || g_now < g_freeAt) return;
    const GunInfo* gi = InfoOf(g_heldRes);
    if (!gi || !gi->projectile) { g_freeOwed = 0; return; }
    if (gi->spawn) ShellDamage();
    uint32_t own = gi->spawn ? gi->spawn : gi->projectile;
    uint32_t round = NoSpawn(own) ? (uint32_t)s_freeFallback : own;
    int pellets = round == own ? (int)gi->pellets : 1;
    float pos[3], aim[3], tgt[3];
    if (!round || !PlayerPose(pos, aim)) { g_freeOwed = 0; return; }
    float from[3] = {pos[0] + aim[0] * 0.9f, pos[1] + aim[1] * 0.9f, pos[2] + 1.45f + aim[2] * 0.9f};
    if (ReticlePoint(tgt))
    {
        float v[3] = {tgt[0] - from[0], tgt[1] - from[1], tgt[2] - from[2]};
        float vn = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (vn > 1.0f) for (int k = 0; k < 3; k++) aim[k] = v[k] / vn;
    }
    int budget = ProjBudget();
    if (budget < 1) { g_freeAt = g_now + 0.05; return; }
    if (pellets > budget) pellets = budget;
    g_projTokens -= pellets;
    int fired = 0;
    for (int p = 0; p < pellets; p++)
    {
        float dir[3], sp = (pellets > 1 ? s_chosenSpread : 0.3f) * 0.0174533f;
        for (int k = 0; k < 3; k++) dir[k] = aim[k] + sp * (Roll() / 100.0f - 0.5f);
        float n = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        for (int k = 0; k < 3; k++) dir[k] /= n;
        int r = hf->spawn_projectile(self, round, from, dir);
        if (r == HF_ACT_WAITING) { g_freeAt = g_now + 0.05; return; }
        if (!r && round != (uint32_t)s_freeFallback)
        {
            NoSpawnAdd(round);
            Log("The Chosen One: projectile %u cannot be spawned here; free rounds use projectile %d (free_round_fallback) instead", round, s_freeFallback);
            return;   /* again next frame, with the fallback */
        }
        if (!Act("The Chosen One's round", r)) break;
        fired++;
    }
    g_freeOwed--;
    g_freeAt = g_now + 0.1;
    if (fired) g_freeShots++;
    static unsigned told;
    if (fired && told++ < 10) Log("The Chosen One: a free round (projectile %u, %d pellet%s)", round, fired, fired == 1 ? "" : "s");
}

/* ---- Area rounds (0.2.0) --------------------------------------------------------------------------------------------
 * Yui: "spawn additional round matching his gun to every enemies in a 7 meter square in front of him, expands to 15
 * meters when using S3". Built as: each of his shots (Shot(): the magazine or the shots stat, the same count that
 * spends skill rounds and rolls The Chosen One) queues one volley, fired on the game thread the next frame. The square:
 * from his position (his unit's root), area_size m along his aim's horizontal direction (toward the reticle's point,
 * else his head's facing) and area_size m across it, centred on the aim line (area_size_s3 while Damnatus Ex Foedere
 * runs); an enemy counts when its root is inside it and within area_size m above or below him. The enemies: the kit
 * base's foe list (spawn events, his hits), living (health > 0 and the health record's life state 0: a dead Illuminate
 * keeps health, Exusiai 0.2.12), nearest first, at most area_max, each with a clear line from his muzzle (Exusiai's
 * ClearShot). Each gets one round of his gun's own projectile, every pellet (The Chosen One's spawn path and fallback),
 * from near his muzzle toward the middle of its body (Exusiai 0.2.4's node median). The pellets draw on max_rounds.
 * No chaining: spawned rounds are not the gun in hand's shots (neither the magazine nor the shots stat counts them), so
 * they spend no skill round, roll no Chosen One and queue no volley. Healing: see AreaHit. */
#define AREA_HARD 32
static struct { double at; uint32_t tgt[AREA_HARD]; uint8_t hit[AREA_HARD]; int n, heals; } g_vol;   /* the last volley */
static int g_areaOwed;
static double g_lastShotAt = -100;     /* his last shot (a volley older than it is done with) */
/* the shot under way: its first hit, the enemies it hit, the heals it gave (the damage event's; AreaHit shares them) */
static double g_groupAt = -100;
static uint32_t g_groupHit[32];
static int g_ngroup, g_groupHeals;
static int Living(uint32_t e)
{
    int32_t hp = 0;
    uint32_t life = 1;
    return HealthOf(e, &hp, &life) && hp > 0 && life == 0;
}
/* the middle of a unit's body (Exusiai 0.2.4's: the median of its scene-graph nodes, kept a second per unit) */
#define BODIES 64
static struct { uint32_t unit; double at; float off[3]; } g_body[BODIES];
static int CmpF(const void* x, const void* y) { float a = *(const float*)x, b = *(const float*)y; return a < b ? -1 : a > b; }
static int BodyOffset(uint32_t unit, const float root[3], float off[3])
{
    unsigned h = (unit * 2654435761u) % BODIES;
    if (g_body[h].unit == unit && g_now - g_body[h].at < 1.0) { memcpy(off, g_body[h].off, 12); return 1; }
    uintptr_t registry = Pointer(exe + 0x1A100F0), objects, object, nodes;
    uint32_t count = 0, id, slot = unit & 0x3FFFFF;
    static float m[256 * 16];
    if (!registry || !(objects = Pointer(registry + 0x88)) || !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit ||
        !ReadAt(object + 0x70, &count, 4) || !count || count > 4096 || !(nodes = Pointer(object + 0x88)))
        return 0;
    if (count > 256) count = 256;
    if (!ReadAt(nodes, m, (size_t)count * 64)) return 0;
    static float v[3][256];
    int used = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        const float* t = m + 16 * i + 12;
        if (!isfinite(t[0]) || !isfinite(t[1]) || !isfinite(t[2]) || fabsf(t[0] - root[0]) > 20 || fabsf(t[1] - root[1]) > 20 || t[2] < root[2] - 2 || t[2] > root[2] + 20) continue;
        for (int k = 0; k < 3; k++) v[k][used] = t[k] - root[k];
        used++;
    }
    if (used < 2) return 0;
    for (int k = 0; k < 3; k++) { qsort(v[k], used, sizeof(float), CmpF); off[k] = v[k][used / 2]; }
    if (off[2] < 0.3f) off[2] = 0.3f;
    g_body[h].unit = unit; g_body[h].at = g_now; memcpy(g_body[h].off, off, 12);
    return 1;
}
/* a clear line from his muzzle to a point (no wall between; bodies and leaves do not count: FirstWall skips them) */
static int ClearShot(const float from[3], const float q[3])
{
    uint32_t id;
    if (!WorldId(&id) || !QueryIdle()) return 1;
    float d[3] = {q[0] - from[0], q[1] - from[1], q[2] - from[2]};
    float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(n > 1.5f)) return 1;
    for (int k = 0; k < 3; k++) d[k] /= n;
    return FirstWall(id, from, d, n, UnitOf(atomic_load(&g_avatar))) > n - 1.0f;
}
static void AreaFrame(void)
{
    if (g_areaOwed <= 0) return;
    g_areaOwed--;   /* one volley a frame */
    if (!s_areaRounds || s_areaMax < 1) return;
    const GunInfo* gi = InfoOf(g_heldRes);
    if (!gi || !gi->projectile) return;
    if (gi->spawn) ShellDamage();
    uint32_t own = gi->spawn ? gi->spawn : gi->projectile;
    uint32_t round = NoSpawn(own) ? (uint32_t)s_freeFallback : own;
    int pellets = round == own ? (int)gi->pellets : 1;
    float pos[3], aim[3], tgt[3];
    if (!round || !PlayerPose(pos, aim)) return;
    float fx = aim[0], fy = aim[1];
    if (ReticlePoint(tgt)) { float vx = tgt[0] - pos[0], vy = tgt[1] - pos[1]; if (vx * vx + vy * vy > 1.0f) { fx = vx; fy = vy; } }
    float fn = sqrtf(fx * fx + fy * fy);
    if (!(fn > 1e-3f)) return;
    fx /= fn; fy /= fn;
    float size = g_skill == 3 ? s_areaSizeS3 : s_areaSize, half = size * 0.5f;
    float from[3] = {pos[0] + aim[0] * 0.9f, pos[1] + aim[1] * 0.9f, pos[2] + 1.45f + aim[2] * 0.9f};
    /* the enemies in the square, nearest first */
    struct { uint32_t e; float d2; float q[3]; } c[AREA_HARD];
    int nc = 0;
    for (int k = 0; k < FOES; k++)
    {
        uint32_t e = g_foe[k];
        uint8_t d[24];
        float r[3];
        if (!e || !Descriptor(e, d) || Quad(d) != g_foeRes[k] || !UnitNode(Word(d + 12), 0, r)) continue;
        float dx = r[0] - pos[0], dy = r[1] - pos[1], dz = r[2] - pos[2];
        float along = dx * fx + dy * fy, across = dy * fx - dx * fy;
        if (along < 0 || along > size || across < -half || across > half || dz < -size || dz > size) continue;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (nc == AREA_HARD && d2 >= c[AREA_HARD - 1].d2) continue;
        if (!Living(e)) continue;
        int j = nc < AREA_HARD ? nc++ : AREA_HARD - 1;
        while (j > 0 && c[j - 1].d2 > d2) { c[j] = c[j - 1]; j--; }
        c[j].e = e; c[j].d2 = d2;
        float off[3];
        if (BodyOffset(Word(d + 12), r, off)) for (int t = 0; t < 3; t++) r[t] += off[t];   /* its body's middle */
        else r[2] += 1.0f;
        memcpy(c[j].q, r, 12);
    }
    if (!nc) return;
    memset(&g_vol, 0, sizeof g_vol);
    g_vol.at = g_now;
    int fired = 0;
    for (int j = 0; j < nc && fired < s_areaMax; j++)
    {
        if (!ClearShot(from, c[j].q)) continue;
        /* 0.2.2: every enemy in the square, every shot: area rounds have their own cap (area_pellets a target), not
         * max_rounds (0.2.0-0.2.1 drew on it: an 11-pellet gun reached 3 of 4 enemies) */
        int n = s_areaPellets > 0 && pellets > s_areaPellets ? s_areaPellets : pellets;
        float v[3] = {c[j].q[0] - from[0], c[j].q[1] - from[1], c[j].q[2] - from[2]};
        float vn = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (!(vn > 0.1f)) continue;
        for (int k = 0; k < 3; k++) v[k] /= vn;
        g_vol.tgt[g_vol.n++] = c[j].e;   /* before the spawn: its first pellet may land at once */
        int out = 0, stop = 0;
        for (int p = 0; p < n; p++)
        {
            float dir[3], sp = (n > 1 ? s_chosenSpread : 0.3f) * 0.0174533f;
            for (int k = 0; k < 3; k++) dir[k] = v[k] + sp * (Roll() / 100.0f - 0.5f);
            float dn = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
            for (int k = 0; k < 3; k++) dir[k] /= dn;
            int r = hf->spawn_projectile(self, round, from, dir);
            if (r == HF_ACT_WAITING) { stop = 1; break; }
            if (!r && round != (uint32_t)s_freeFallback)
            {
                NoSpawnAdd(round);
                Log("area rounds: projectile %u cannot be spawned here; spawned rounds use projectile %d (free_round_fallback) instead", round, s_freeFallback);
                stop = 1;
                break;
            }
            if (!Act("an area round", r)) { stop = 1; break; }
            out++;
        }
        if (out) { fired++; g_areaRoundsFired++; }
        else g_vol.n--;
        if (stop) break;
    }
    if (fired)
    {
        g_areaVolleys++;
        static unsigned told;
        if (told++ < 10) Log("area rounds: %d enem%s in the %.0f m square (of %d there), projectile %u, %d pellet%s each", fired, fired == 1 ? "y" : "ies",
                             (double)size, nc, round, pellets, pellets == 1 ? "" : "s");
    }
}
/* the heal cap a shot */
static int HealCap(void) { return s_healCap + (g_skill == 2 ? 1 : 0); }
/* the volley and the shot's own hits are one shot when the shot's first hit is near the volley's time */
static int VolleyLinked(void) { return g_vol.n && fabs(g_groupAt - g_vol.at) <= s_shotGap + 0.25; }
/* An area round's hit on an enemy (one of the last volley's targets, the volley under 0.6 s old and no newer shot since):
 * it heals as one of his shot's hits: once per enemy of the volley, not for an enemy the shot itself already healed for,
 * and the volley's heals and the shot's together stay within the heal cap. 1: it was the volley's (handled). */
static int AreaHit(uint32_t target)
{
    if (!g_vol.n || g_vol.at < g_lastShotAt || g_now - g_vol.at > 0.6) return 0;
    int i = 0;
    while (i < g_vol.n && g_vol.tgt[i] != target) i++;
    if (i == g_vol.n) return 0;
    if (g_vol.hit[i]) return 1;
    g_vol.hit[i] = 1;
    g_areaHits++;
    int linked = VolleyLinked();
    if (linked) for (int k = 0; k < g_ngroup; k++) if (g_groupHit[k] == target) return 1;
    if (g_vol.heals + (linked ? g_groupHeals : 0) >= HealCap()) return 1;
    g_vol.heals++;
    g_healHits++;
    g_healOwed += s_healPct / 100.0 * (g_skill == 3 ? s_s3HealMult : 1);
    g_healHitsOwed++;
    return 1;
}

/* ---- his shots: each one spends a skill round and rolls The Chosen One ------------------------------------------------ */
static void Shot(void)
{
    g_shots++;
    float chance = s_chosenPct + (g_skill ? s_chosenStep * (float)g_spent : 0);
    if (g_skill && g_rounds > 0)
    {
        g_rounds--;
        g_spent++;
        if (g_rounds <= 0) g_endAt = g_now + 0.35;   /* the last round's hits land first */
        static unsigned told;
        if (told++ < 24) Log("%s: a round spent, %d left (The Chosen One %.0f%%)", kSkill[g_skill - 1], g_rounds, (double)(chance > 100 ? 100 : chance));
    }
    g_lastShotAt = g_now;
    if (s_areaRounds && g_areaOwed < 4) g_areaOwed++;   /* (only his gun's own shots come here: no chaining) */
    if (Roll() < chance && g_freeOwed < 4)
    {
        if (!g_freeOwed) g_freeAt = g_now + s_chosenDelay;
        g_freeOwed++;
    }
}
/* the HUD's report of the gun in hand: its entity and the rounds in its magazine (-1: unknown), as Lemuen's and Exusiai's */
static atomic_uint g_ammoEntity;
static atomic_int g_ammoRounds = -1;
static _Atomic unsigned long long g_ammoMs;
HF_MOD_EXPORT int executor_ammo(unsigned version, uint32_t entity, int rounds)
{
    if (version != 1) return 0;
    atomic_store(&g_ammoEntity, entity);
    atomic_store(&g_ammoRounds, rounds);
    atomic_store(&g_ammoMs, GetTickCount64());
    return 1;
}
static int HudAmmo(void) { unsigned long long ms = atomic_load(&g_ammoMs); return ms && GetTickCount64() - ms <= 1000 && atomic_load(&g_ammoRounds) >= 0; }
/* his shots from the magazine: the same gun, fewer rounds (Exusiai's BurstFrame); a rise is a reload */
static void AmmoFrame(void)
{
    static uint32_t lastEntity;
    static int last = -1;
    uint32_t ent = atomic_load(&g_ammoEntity);
    int rounds = atomic_load(&g_ammoRounds);
    if (!HudAmmo()) { last = -1; return; }
    if (g_shotSource != 1) { g_shotSource = 1; Log("his shots: counted from the magazine (the HUD reports it)"); }
    if (ent == lastEntity && last >= 0 && rounds < last && last - rounds <= 20)
        for (int k = 0; k < last - rounds; k++) Shot();
    lastEntity = ent;
    last = rounds;
}
/* his shots from the shots stat when the HUD does not report (pellets fired by the gun in hand, a shot's pellets each) */
static void OnStat(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfStatEvent* s = (const HfStatEvent*)info;
    static int carry;
    static uint64_t carryRes;
    if (s->stat != HF_STAT_SHOTS || !g_inMission || !kb_kit_on || !g_alive || HudAmmo()) return;
    static unsigned told;
    if (told++ < 8)
        Log("shots stat: +%d (unattributed %d), %u source%s: %016llx +%d%s", s->added, s->unattributed, s->count, s->count == 1 ? "" : "s",
            s->count ? (unsigned long long)s->sources[0].resource : 0ull, s->count ? s->sources[0].added : 0, s->count > 1 ? " ..." : "");
    for (uint32_t i = 0; i < s->count && i < HF_STAT_SOURCES; i++)
    {
        uint64_t res = s->sources[i].resource;
        if (!res || res != g_heldRes || s->sources[i].added <= 0) continue;   /* the gun in hand only (not his free rounds, grenades, stratagems) */
        const GunInfo* gi = InfoOf(res);
        int pellets = gi && gi->stat_per ? (int)gi->stat_per : 1;
        if (carryRes != res) { carryRes = res; carry = 0; }
        carry += s->sources[i].added;
        if (g_shotSource != 2) { g_shotSource = 2; Log("his shots: counted from the shots stat (no magazine from the HUD), %d pellet%s a shot", pellets, pellets == 1 ? "" : "s"); }
        int n = carry / pellets;
        carry -= n * pellets;
        for (int k = 0; k < n && k < 20; k++) Shot();
    }
}

/* ---- his heals: queued by the damage event (and AreaHit), given once a frame --------------------------------------- */
/* the heals he gives himself, so the stim undo leaves them: health expected, until when */
static double g_credit, g_creditUntil, g_limbGraceUntil;
static void HealFrame(void)
{
    if (g_healOwed <= 0 || !g_alive) { g_healOwed = 0; g_healHitsOwed = 0; return; }
    double owed = g_healOwed;
    int hits = g_healHitsOwed;
    g_healOwed = 0;
    g_healHitsOwed = 0;
    uint32_t me = atomic_load(&g_avatar);
    int32_t hp = 0, max = MaxHealth();
    uint32_t life = 1;
    if (!HAS(heal) || !me || max <= 0) { static unsigned told; if (told++ < 3) Log("his heal: unavailable (HellForge's heal %s, max health %d)", HAS(heal) ? "present" : "missing", max); return; }
    int known = HealthOf(me, &hp, &life);
    float amount = (float)(owed * max);
    if (!known || hp < max)
    {
        int r = hf->heal(self, amount);
        if (Act("his heal", r))
        {
            g_heals++;
            /* the whole amount is credited (his maximum may read low): a stim in the next second may keep that much */
            g_credit = (g_now < g_creditUntil ? g_credit : 0) + amount + 1;
            g_creditUntil = g_now + 1.0;
            static unsigned told;
            if (told++ < 16) Log("his heal: +%.0f (%d enemy hit%s; health %d of %d before)", (double)amount, hits, hits == 1 ? "" : "s", hp, max);
        }
    }
    if (!s_healLimbs || !HAS(heal_limb) || !HAS(limb_state)) return;
    int mended = 0;
    for (uint32_t l = HF_LIMB_HEAD; l <= HF_LIMB_RIGHT_LEG; l++)
    {
        HfLimbState st;
        memset(&st, 0, sizeof st);
        if (!hf->limb_state(self, me, l, &st) || !st.injured) continue;
        if (Act("his limb mended", hf->heal_limb(self, l))) mended++;
    }
    if (mended)
    {
        g_limbsMended += (unsigned)mended;
        g_limbGraceUntil = g_now + 0.5;   /* a mended limb's health may show as a heal of the pool */
        static unsigned told;
        if (told++ < 10) Log("his heal: %d injured limb%s mended", mended, mended == 1 ? "" : "s");
    }
}

/* ---- No stims (the design's fallback): a heal that is not his own is taken back as it lands ---------------------- */
static void OnHeal(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfHealEvent* h = (const HfHealEvent*)info;
    uint32_t me = atomic_load(&g_avatar);
    if (!h->local || !me || h->entity != me || !g_inMission || !kb_kit_on) return;
    if (h->max_health > 0 && h->max_health != g_maxHp) { g_maxHp = h->max_health; Log("his maximum health: %d (the heal event)", g_maxHp); }
    if (g_now >= g_creditUntil) g_credit = 0;
    int32_t own = (int32_t)fmin((double)h->amount, g_credit);
    g_credit -= own;
    int32_t rest = h->amount - own;
    if (rest > 0 && g_now < g_limbGraceUntil)
    {
        static unsigned told;
        if (told++ < 6) Log("a heal of %d right after his limbs were mended: counted as his own", rest);
        return;
    }
    if (rest <= 1 || !g_alive) return;
    if (!s_stimUndo) { static unsigned told; if (told++ < 6) Log("a heal not his own: +%d (stim_undo 0: kept)", rest); return; }
    uintptr_t r = HealthRec(me);
    int32_t now = 0, want = h->health - rest;
    if (want < 1) want = 1;
    if (!r || !ReadAt(r + 20, &now, 4)) { Log("No stims: a heal of %d not his own, but his health record cannot be read: kept", rest); return; }
    if (now != h->health) { static unsigned told; if (told++ < 6) Log("No stims: his health record reads %d, the heal event %d (taking %d off the record's)", now, h->health, rest); want = now - rest; if (want < 1) want = 1; }
    if (hf->write(r + 20, &want, 4)) { Log("No stims: the health write failed: the heal of %d stays", rest); return; }
    int32_t back = 0;
    ReadAt(r + 20, &back, 4);
    g_undone++;
    static unsigned told;
    if (told++ < 16) Log("No stims: a heal of %d not his own taken back (health %d -> %d, reads back %d)", rest, now, want, back);
}

/* ---- the damage event: his shots, and hits on him --------------------------------------------------------------- */
static double SkillMult(void)
{
    if (!g_skill) return 1;
    if (g_skill == 1) return s_s1Mult;
    if (g_skill == 2) return s_s2Mult;
    int stacks = g_spent < s_s3Stacks ? g_spent : s_s3Stacks;
    return s_s3Mult + s_s3Step * stacks;
}
static int Shotgun(void) { const GunInfo* gi = InfoOf(g_heldRes); return gi && gi->pellets > 1; }
static unsigned g_hitLog;
static void DamageDealt(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    (void)e; (void)me;
    if (d->amount <= 0) return;
    if (S3BlastHit(d))
    {
        if (s_s3BlastMult != 1 && Enemy(d->target)) ScaleDamage(d, s_s3BlastMult);   /* (a teammate in it takes the blast's own) */
        return;
    }
    if (!IsShot(d)) return;
    NoteTarget(d);
    int enemy = Enemy(d->target);
    int32_t base = d->amount;
    double mult = SkillMult();
    if (mult != 1) ScaleDamage(d, mult);
    if (g_skill && g_hitLog < 16) { g_hitLog++; Log("his hit on entity %u: %d -> %d damage (%s x%.2f)", d->target, base, d->amount, kSkill[g_skill - 1], mult); }
    if (!enemy) return;
    if (g_skill == 3)
    {
        int known = 0;
        for (int k = 0; k < g_ns3Hit; k++) if (g_s3Hit[k] == d->target) known = 1;
        if (!known && g_ns3Hit < 64) g_s3Hit[g_ns3Hit++] = d->target;
    }
    /* his trait: each enemy of this shot heals him, up to the cap */
    if (s_healPct <= 0 || (s_healShotgunsOnly && !Shotgun())) return;
    if (AreaHit(d->target)) return;   /* an area round's: healed (or not) as the volley's */
    if (g_now - g_groupAt > s_shotGap) { g_groupAt = g_now; g_ngroup = 0; g_groupHeals = 0; }
    for (int k = 0; k < g_ngroup; k++) if (g_groupHit[k] == d->target) return;
    if (g_ngroup < 32) g_groupHit[g_ngroup++] = d->target;
    if (g_groupHeals + (VolleyLinked() ? g_vol.heals : 0) >= HealCap()) return;
    g_groupHeals++;
    g_healHits++;
    g_healOwed += s_healPct / 100.0 * (g_skill == 3 ? s_s3HealMult : 1);
    g_healHitsOwed++;
}
static void DamageTaken(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    (void)me;
    if (d->amount <= 0) return;
    if (S3BlastHit(d)) { e->cancel = 1; return; }   /* his own blasts spare him */
    if (g_skill != 2) return;
    /* Close Combat: any hit has s2_dodge % to be negated and give a round back; the rest deal s2_reduce less */
    int overTime = d->kind == 2 || d->kind == 8 || d->kind == 9;
    if ((!overTime || s_s2DodgeDot) && Roll() < s_s2Dodge)
    {
        e->cancel = 1;
        g_dodges++;
        if (g_rounds < g_roundsMax && g_rounds > 0) g_rounds++;
        static unsigned told;
        if (told++ < 12) Log("Close Combat: a hit for %d (kind %u) dodged, a round back (%d left)", d->amount, d->kind, g_rounds);
        return;
    }
    double a = floor(d->amount * (1.0 - s_s2Reduce) + 0.5);
    if (a < 1) { e->cancel = 1; return; }
    d->amount = (int32_t)a;
}

/* Testament Exacted's armour penetration: his shots' hits, raised for the game's calculation and then put back (the kit
 * base restores it), as Faelynn's Corrosion shred (hit +0x9C, +0xA0, +0xA4: the penetration by impact angle) */
static void HitBefore(KbHitCall* c)
{
    if (c->kind != KB_HIT_CALC || !atomic_load(&g_apOn) || s_s1Ap <= 0) return;
    uint32_t me = atomic_load(&g_avatar);
    uint8_t h[HIT_SIZE];
    if (!me || !ReadAt((uintptr_t)c->hit, h, sizeof h)) return;
    uint32_t kind = Word(h), target = Word(h + 0x20), dealer = Word(h + 0x80), owner = Word(h + 0x100);
    if (!target || target == me || !(kind == 0 || kind == 1 || kind == 3)) return;
    if (!(dealer == me || (owner == me && (!dealer || Mine(dealer))))) return;   /* his own, not a sentry's */
    memcpy(c->saved, h + 0x9C, 12);
    for (int k = 0; k < 3; k++) if (!(c->saved[k] >= 0 && c->saved[k] <= 12)) return;   /* not the reviewed layout */
    float up[3];
    for (int k = 0; k < 3; k++) up[k] = c->saved[k] + s_s1Ap;
    memcpy((uint8_t*)c->hit + 0x9C, up, 12);
    c->restore = 1;
    atomic_fetch_add(&g_pierced, 1);
}

/* ---- Empathy by Shotgun: the other Laterano kits in the roster ---------------------------------------------------- */
static int Laterano(const char* id)
{
    return !strncmp(id, "exusiai", 7) || !strcmp(id, "lemuen") || strstr(id, "covenant") != NULL;   /* Exusiai, Exusiai the New Covenant, Lemuen */
}
static void Empathy(void)
{
    KbRoster* r = Roster();
    g_bonus = 0;
    g_bonusNames[0] = 0;
    if (!r || r->magic != KB_ROSTER_MAGIC || !RosterLock()) { Log("Empathy by Shotgun: the roster is unavailable (no extra rounds)"); return; }
    int k = 0;
    for (uint32_t i = 0; i < r->count && i < KB_ROSTER_MAX; i++)
    {
        char id[32];
        snprintf(id, sizeof id, "%.31s", r->id[i]);
        if (!Laterano(id)) continue;
        g_bonus++;
        k += snprintf(g_bonusNames + k, sizeof g_bonusNames - (size_t)k, "%s%.31s", k ? ", " : "", r->name[i]);
        if (k >= (int)sizeof g_bonusNames) k = (int)sizeof g_bonusNames - 1;
    }
    RosterUnlock();
    if (g_bonus) Log("Empathy by Shotgun: +%d round%s a skill (%s installed)%s", g_bonus, g_bonus == 1 ? "" : "s", g_bonusNames, s_empathy ? "" : "; empathy 0: off");
    else Log("Empathy by Shotgun: no other Laterano kit installed (no extra rounds)");
}

/* ---- events ------------------------------------------------------------------------------------------------------ */
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    if (s->resource != AVATAR) { if (FoeKind(s->resource) || EnemyTurret(s->resource)) AddFoe(s->entity, s->resource); return; }
    if (!s->authority) return;
    g_spawnEntity[g_spawnNext % 8] = s->entity;
    g_spawnNet[g_spawnNext % 8] = s->network;
    g_spawnNext++;
    LearnNet();
}
static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfKillEvent* k = (const HfKillEvent*)info;
    DropFoe(k->victim);
    if (k->victim == g_lastTarget) g_lastTarget = 0;
}
static void OnPlayer(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    if (!p->local) return;
    if (info->kind == HF_ON_PLAYER_SPAWN)
    {
        if (p->entity && p->entity != atomic_load(&g_avatar)) { atomic_store(&g_avatar, p->entity); g_avatarNet = 0; }
        g_alive = 1;
        LearnNet();
    }
    else
    {
        g_alive = 0;
        End("he is down");
        g_freeOwed = 0;
        g_areaOwed = 0;
        g_healOwed = 0;
    }
}
static void OnWeaponExe(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfWeaponEvent* w = (const HfWeaponEvent*)info;
    if (!w->avatar || w->avatar != atomic_load(&g_avatar) || !kb_kit_on) return;
    g_heldEntity = w->entity;
    g_heldRes = w->resource;
    if (w->resource && w->selection >= 1 && w->selection <= 3) InfoOf(w->resource);
}
static void Reset(const char* why)
{
    g_shellDamageDone = 0;   /* 0.2.3: the shell copy's damage set again in each mission */
    int start = !strcmp(why, "mission start");
    if (g_shots || g_casts[0] || g_casts[1] || g_casts[2])
        Log("%s: %u shots, casts %u / %u / %u, %u free rounds, %u area rounds in %u volleys (%u enemies hit), %u heals (%u enemy hits), "
            "%u limbs mended, %u dodges, %u heals taken back, %u blasts, %u hits pierced", why, g_shots, g_casts[0], g_casts[1], g_casts[2], g_freeShots,
            g_areaRoundsFired, g_areaVolleys, g_areaHits, g_heals, g_healHits, g_limbsMended, g_dodges, g_undone, g_blasts, atomic_load(&g_pierced));
    if (start) { KitDecide(); if (kb_kit_on) Empathy(); }   /* with several kits installed: is this one picked? */
    Body(start);
    if (start && kb_kit_on && s_s3Row == 404 && !hf->load_package(self, UINT64_C(0x6AD7CC21015A5F85)))
        Log("the AC-8 Autocannon's package could not be loaded ahead: S3's first blast may wait a moment");
    if (start && s_hellbomb && s_hellbombRow == 125 && !hf->load_package(self, UINT64_C(0x346D785D6CA13FF4))) Log("the Hellbomb's package could not be loaded ahead");
    g_boomPending = 0;
    g_boomArmed = 0;
    memset(g_cd, 0, sizeof g_cd);
    memset(g_casts, 0, sizeof g_casts);
    g_skill = 0;
    g_rounds = g_roundsMax = g_spent = 0;
    g_endAt = 0;
    atomic_store(&g_apOn, 0);
    g_ns3Hit = g_nblast = 0;
    g_freeOwed = 0;
    g_areaOwed = 0;
    memset(&g_vol, 0, sizeof g_vol);
    g_lastShotAt = -100;
    g_areaVolleys = g_areaRoundsFired = g_areaHits = 0;
    g_healOwed = 0;
    g_healHitsOwed = 0;
    g_credit = 0;
    g_creditUntil = g_limbGraceUntil = 0;
    g_groupAt = -100;
    g_maxHp = 0;
    g_shots = g_freeShots = g_heals = g_healHits = g_limbsMended = g_dodges = g_undone = g_blasts = 0;
    atomic_store(&g_pierced, 0);
    g_hitLog = 0;
    g_lastTarget = 0;
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gameSize = 0, exeSize = 0;
    uintptr_t g = hf->module_base("game.dll", &gameSize), x = hf->module_base("helldivers2.exe", &exeSize);
    if (!g || !x || gameSize != 74727424u || exeSize != 60719104u) { Log("REFUSED: the game's modules are not build " BUILD "'s: his kit stays off"); return; }
    game = g;
    exe = x;
    if (hf->code_matches(exe + 0x79F860, "33 c0 4c 8d 05 c7 b2 01 02 0f 1f 80 00 00 00 00 49 39 0c c0 74 0c ff c0 83 f8 04 75 f3") &&
        hf->code_matches(exe + 0x7F4590, "48 8b c4 f3 0f 11 58 20 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48"))
    {
        g_worldId = (WorldFn)(exe + 0x79F860);
        g_raycast = (RayFn)(exe + 0x7F4590);
        Log("the reticle ready (the game's raycast): free rounds fly where he aims");
    }
    else Log("the reticle off: the raycast is not the reviewed code (free rounds fly along his head's facing)");
    HitsInstall("own-blast guard and Testament Exacted's armour penetration");
    g_staminaOk = hf->code_matches(game + 0xA50E2D, "f3 42 0f 11 8c 11 54 78 54 00");
    Log("HellForge's heal %s, heal_limb %s, limb_state %s", HAS(heal) ? "present" : "MISSING", HAS(heal_limb) ? "present" : "MISSING",
        HAS(limb_state) ? "present" : "MISSING");
}

/* ---- the HUD state (read by the Executor HUD addon through LuaJIT's ffi) ----------------------------------------- */
typedef struct ExecutorHud
{
    uint32_t version, in_mission;
    float cd[3], cd_max[3];            /* seconds left, full cooldowns: Testament Exacted, Close Combat, Damnatus Ex Foedere */
    uint32_t skill;                    /* the skill running: 1..3, 0 none */
    int32_t rounds, rounds_max;        /* its rounds left, and at its start */
    float chosen;                      /* The Chosen One's chance now (0..100) */
    uint32_t stacks;                   /* Damnatus Ex Foedere's stacks */
    uint32_t bonus;                    /* Empathy by Shotgun's extra rounds */
    uint32_t keys[3];
    char key_label[3][24];
} ExecutorHud;
static ExecutorHud g_hud = {1};
HF_MOD_EXPORT ExecutorHud* executor_hud_state(void)
{
    g_hud.in_mission = (uint32_t)(g_inMission && g_alive && kb_kit_on);
    for (int i = 0; i < 3; i++) { g_hud.cd[i] = (float)fmax(0, g_cd[i] - g_now); g_hud.cd_max[i] = (float)SkillCd(i); }
    g_hud.skill = (uint32_t)g_skill;
    g_hud.rounds = g_rounds;
    g_hud.rounds_max = g_roundsMax;
    float c = s_chosenPct + (g_skill ? s_chosenStep * (float)g_spent : 0);
    g_hud.chosen = c > 100 ? 100 : c;
    g_hud.stacks = (uint32_t)(g_skill == 3 ? (g_spent < s_s3Stacks ? g_spent : s_s3Stacks) : 0);
    g_hud.bonus = (uint32_t)(s_empathy ? g_bonus : 0);
    for (int i = 0; i < 3; i++)
    {
        g_hud.keys[i] = (uint32_t)s_keys[i].vk;
        memcpy(g_hud.key_label[i], s_keys[i].label, sizeof g_hud.key_label[i]);
    }
    return &g_hud;
}
/* in-game key bindings: the HUD registers his three skills with Mod Bindings Menu and passes each press here */
static _Atomic unsigned long long g_bindMs;
static atomic_int g_bindPress;
HF_MOD_EXPORT int executor_input(unsigned version, int skill)
{
    if (version != 1) return 0;
    atomic_store(&g_bindMs, GetTickCount64());
    if (skill >= 1 && skill <= 3) atomic_fetch_or(&g_bindPress, 1 << (skill - 1));
    return 1;
}
HF_MOD_EXPORT int executor_key(unsigned version, int skill, int* vk, int* mods)
{
    if (version != 1 || skill < 1 || skill > 3 || !vk || !mods) return 0;
    *vk = s_keys[skill - 1].vk;
    *mods = s_keys[skill - 1].mods;
    return 1;
}

/* ---- the frame -------------------------------------------------------------------------------------------------- */
static void Tick(HfMod* mod, float dt, void* user)
{
    g_now += dt > 0 && dt < 1 ? dt : 0;
    int inMission = hf->game_state(self) == HF_STATE_MISSION;
    if (inMission != g_inMission) { Reset(inMission ? "mission start" : "mission end"); g_inMission = inMission; }
    if (!g_inMission || !kb_kit_on) return;   /* (standing by: another kit is picked) */
    MyItems();
    static int prev[3];
    int focused = Focused();
    int mods = focused ? ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? KM_SHIFT : 0) | ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? KM_CTRL : 0) |
                         ((GetAsyncKeyState(VK_MENU) & 0x8000) ? KM_ALT : 0) : 0;
    int presses = atomic_exchange(&g_bindPress, 0);
    for (int i = 0; i < 3; i++)
    {
        int own = s_keys[i].vk == VK_SHIFT ? KM_SHIFT : s_keys[i].vk == VK_CONTROL ? KM_CTRL : s_keys[i].vk == VK_MENU ? KM_ALT : 0;
        int down = focused && (GetAsyncKeyState(s_keys[i].vk) & 0x8000) != 0 && (mods & ~own) == s_keys[i].mods;
        static unsigned long long fired[3];
        int press = (down && !prev[i]) || (presses >> i & 1);
        if (press && GetTickCount64() - fired[i] > 300 && g_alive && atomic_load(&g_avatar) && (fired[i] = GetTickCount64(), 1)) Cast(i);
        prev[i] = down;
    }
    if (g_alive)
    {
        AmmoFrame();
        HealFrame();
        FreeFrame();
        AreaFrame();
    }
    if (g_skill && g_rounds <= 0 && g_endAt > 0 && g_now >= g_endAt) End("its rounds are spent");
    BlastFrame();
    Exhaustion();
    atomic_exchange(&g_ownStopped, 0);
    atomic_exchange(&g_kdStopped, 0);
}

/* ---- Mod Options Menu ------------------------------------------------------------------------------------------- */
static const KbOption kOptions[] = {
    {"heal_pct", &s_healPct, NULL}, {"heal_cap", NULL, &s_healCap}, {"heal_shotguns_only", NULL, &s_healShotgunsOnly}, {"stim_undo", NULL, &s_stimUndo},
    {"chosen_pct", &s_chosenPct, NULL}, {"chosen_step", &s_chosenStep, NULL}, {"empathy", NULL, &s_empathy},
    {"area_rounds", NULL, &s_areaRounds}, {"area_size", &s_areaSize, NULL}, {"area_size_s3", &s_areaSizeS3, NULL}, {"area_max", NULL, &s_areaMax},
    {"s1_cooldown", &s_s1Cd, NULL}, {"s1_rounds", NULL, &s_s1Rounds}, {"s1_mult", &s_s1Mult, NULL}, {"s1_ap", &s_s1Ap, NULL},
    {"s2_cooldown", &s_s2Cd, NULL}, {"s2_rounds", NULL, &s_s2Rounds}, {"s2_mult", &s_s2Mult, NULL}, {"s2_reduce", &s_s2Reduce, NULL}, {"s2_dodge", &s_s2Dodge, NULL},
    {"s3_cooldown", &s_s3Cd, NULL}, {"s3_rounds", NULL, &s_s3Rounds}, {"s3_mult", &s_s3Mult, NULL}, {"s3_heal_mult", &s_s3HealMult, NULL},
    {"s3_blast_mult", &s_s3BlastMult, NULL}, {"hp_mult", &s_hpMult, NULL}, {"stamina_mult", &s_staminaMult, NULL},
};
static void OptionAfter(const char* name, int changed)
{
    if (changed && !strcmp(name, "s1_ap") && !g_skill) atomic_store(&g_apOn, 0);
}
HF_MOD_EXPORT int executor_option_get(const char* name, double* out)
{
    return OptionGet(name, out);
}
HF_MOD_EXPORT int executor_option_set(const char* name, double value)
{
    return OptionSet(name, value);
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    RosterJoin("executor", "Executor");
    kb_ini = "executor.ini";
    kb_key_names[0] = "key_s1";
    kb_key_names[1] = "key_s2";
    kb_key_names[2] = "key_s3";
    KB_SETTINGS(kSettings);
    KB_OPTIONS(kOptions);
    kb_option_after = OptionAfter;
    kb_damage_taken = DamageTaken;
    kb_damage_dealt = DamageDealt;
    kb_hit_before = HitBefore;
    /* his body: a Helldiver's (the kit base's defaults are the T-Dolls') */
    s_hpMult = 1.0f;
    s_staminaMult = 1.0f;
    s_hellbomb = 0;
    srand((unsigned)GetTickCount64());
    if (!api || api->version < 13 || api->size < offsetof(HfApi, code_matches) + sizeof api->code_matches) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    LoadSettings();
    if (api->add_projectile)
    {
        HfProjectile p;
        memset(&p, 0, sizeof p);
        p.size = sizeof p;
        p.base = HF_PROJECTILE_SG225_BREAKER;
        p.visual_from = DBS2_SHELL;
        p.name = "yui/executor/dbs2_shell";
        g_shellCopy = api->add_projectile(mod, &p);
        if (!g_shellCopy) api->log(mod, "the DBS-2 shell copy could not be added: its spawned rounds fall back");
    }
    api->log(mod, "Executor %s (TEST BUILD: untested in game): keys %s / %s / %s (Testament Exacted, Close Combat, Damnatus Ex Foedere); "
             "heal %.0f%% a hit, up to %d a shot; The Chosen One %.0f%%; area rounds %s (%g m, %g m in S3, up to %d); stim_undo %d", VERSION, s_keys[0].label,
             s_keys[1].label, s_keys[2].label, (double)s_healPct, s_healCap, (double)s_chosenPct, s_areaRounds ? "on" : "off", (double)s_areaSize,
             (double)s_areaSizeS3, s_areaMax, s_stimUndo);
    if (s_badKey) api->log(mod, "executor.ini: a key could not be read (bits %d: s1, s2, s3); its default stays", s_badKey);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) &&
             api->subscribe(mod, HF_ON_WEAPON, OnWeaponExe, NULL, 0) && api->subscribe(mod, HF_ON_STAT, OnStat, NULL, 0) &&
             api->subscribe(mod, HF_ON_HEAL, OnHeal, NULL, 0) && api->on_game_thread(mod, Tick, NULL);
    if (!ok) api->log(mod, "events or the game thread are unavailable: Executor stays off");
    return 0;
}
