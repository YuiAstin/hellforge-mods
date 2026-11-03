/* Exusiai (yui.exusiai): Exusiai's kit from Arknights as abilities on keys, for your own Helldiver (solo or host). The
 * first kit built on the kit base (kitbase/, docs/kit-base.md).
 *
 *   Fast Cartridge (passive): her guns fire fire_rate times faster (set when she equips each one).
 *   Angel's Blessings (passive): hp_mult times a Helldiver's health, and her shots deal bless_mult times their damage.
 *   Charging Mode (key_charge, default Z): her next ch_hits hits within ch_window s deal ch_mult times their damage.
 *   Summoned guns (key_guns, default X): for s2_seconds two guns hover at her shoulders; each time she lands a shot,
 *     each fires at what she hit (at most once every gun_every s).
 *   Overloading Mode (key_overload, default C): for s3_seconds four more guns hover behind her, each choosing its own
 *     target within s3_range m by its own priority (the nearest, the most heavily armoured, whoever last hit her, the
 *     weakest) and firing every s3_every s.
 *
 * The guns fire the AX/AR-23 Guard Dog's rounds (projectile gun_round, 7), spawned from where each gun hovers and
 * credited to her. While any gun is out her shots do not hurt her allies (no_friendly_fire). The guns are invisible in
 * this version: the floating models wait on the Gun Probe (probes/gun-probe).
 *
 * Research credit: the entity, scene-graph and hit reads are the kit base's (Helldiver Survival Rework by
 * enochlophobianumerophobia, MIT; HD2Runtime by SkyeShade); the Guard Dog round's type is from HD2Runtime's attack
 * catalogue. See THIRD_PARTY_NOTICES.md. */
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

#define VERSION "0.1.0"
#define BUILD "25480438"

#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"
#include "kitbase/kb_body.h"
#include "kitbase/kb_hits.h"
#include "kitbase/kb_damage.h"
#include "kitbase/kb_settings.h"

/* ---- settings ------------------------------------------------------------------------------------------------- */
static float s_fireRate = 1.15f, s_blessMult = 1.08f;
static float s_chCd = 6, s_chWindow = 8, s_chMult = 1.45f;
static int s_chHits = 3;
static float s_s2Cd = 40, s_s2Seconds = 15, s_gunEvery = 0.15f;
static float s_s3Cd = 60, s_s3Seconds = 15, s_s3Every = 0.35f, s_s3Range = 60;
static int s_gunRound = 7, s_noFriendlyFire = 1;

/* Every setting: its .ini / Mod Options Menu name, variable and range (kitbase/kb_settings.h) */
static const KbSetting kSettings[] = {
    KB_F("fire_rate", s_fireRate, 1, 3), KB_F("bless_mult", s_blessMult, 1, 3),
    KB_F("ch_cooldown", s_chCd, 0, 120), KB_F("ch_window", s_chWindow, 1, 30), KB_F("ch_mult", s_chMult, 1, 10), KB_I("ch_hits", s_chHits, 1, 50),
    KB_F("s2_cooldown", s_s2Cd, 0, 600), KB_F("s2_seconds", s_s2Seconds, 1, 120), KB_F("gun_every", s_gunEvery, 0.05, 5),
    KB_F("s3_cooldown", s_s3Cd, 0, 600), KB_F("s3_seconds", s_s3Seconds, 1, 120), KB_F("s3_every", s_s3Every, 0.05, 5), KB_F("s3_range", s_s3Range, 5, 200),
    KB_I("gun_round", s_gunRound, 1, 100000), KB_I("no_friendly_fire", s_noFriendlyFire, 0, 1),
    KB_F("hp_mult", s_hpMult, 0.1, 10), KB_F("stamina_mult", s_staminaMult, 0.1, 20), KB_I("hellbomb_on_exhaustion", s_hellbomb, 0, 1),
    KB_I("hellbomb_explosion", s_hellbombRow, 0, 100000), KB_F("hellbomb_hold", s_hellbombHold, 0, 10),
};

/* The default exusiai.ini: tools/inigen.py writes the file from this text (the mod never writes it). Her body's values
 * are written as numbers here: the kit base's defaults are the T-Dolls', and hers are set in hellforge_mod_init. */
static __attribute__((unused)) void DefaultIni(FILE* f)
{
    fprintf(f, "; Exusiai %s. Keys: a letter, digit, F1-F24, Mouse3, Mouse4 or Mouse5, with Shift+, Ctrl+ or Alt+ in front for a\n"
               "; combo (Shift+Q). The game still sees the keys too (Shift sprints, R reloads), so pick ones it leaves alone.\n"
               "key_charge = %s\nkey_guns = %s\nkey_overload = %s\n"
               "; Fast Cartridge: her guns fire this many times faster (from when she next equips each one).\nfire_rate = %g\n"
               "; Angel's Blessings: her shots deal this times their damage (her health: hp_mult, below).\nbless_mult = %g\n"
               "; Charging Mode: cooldown; her next ch_hits hits within ch_window s deal ch_mult times their damage.\n"
               "ch_cooldown = %g\nch_hits = %d\nch_window = %g\nch_mult = %g\n"
               "; Summoned guns: cooldown; for s2_seconds two guns at her shoulders fire at what she hits (each at most once every\n"
               "; gun_every s).\ns2_cooldown = %g\ns2_seconds = %g\ngun_every = %g\n"
               "; Overloading Mode: cooldown; for s3_seconds four more guns each pick their own target within s3_range m (the\n"
               "; nearest, the most heavily armoured, whoever last hit her, the weakest) and fire every s3_every s.\n"
               "s3_cooldown = %g\ns3_seconds = %g\ns3_every = %g\ns3_range = %g\n"
               "; The guns' round (7: the AX/AR-23 Guard Dog's). no_friendly_fire 1: while a gun is out her shots spare her allies.\n"
               "gun_round = %d\nno_friendly_fire = %d\n"
               "; Her body: hp_mult times a Helldiver's health, stamina_mult times her sprint. hellbomb_on_exhaustion 1 sets off a\n"
               "; Hellbomb on her if she runs her stamina dry (a T-Doll's price; off for her).\n"
               "hp_mult = 1.13\nstamina_mult = 1\nhellbomb_on_exhaustion = 0\n",
            VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, (double)s_fireRate, (double)s_blessMult, (double)s_chCd, s_chHits,
            (double)s_chWindow, (double)s_chMult, (double)s_s2Cd, (double)s_s2Seconds, (double)s_gunEvery, (double)s_s3Cd,
            (double)s_s3Seconds, (double)s_s3Every, (double)s_s3Range, s_gunRound, s_noFriendlyFire);
}

/* ---- state ---------------------------------------------------------------------------------------------------- */
static double g_cd[3];                  /* ready at: Charging Mode, the guns, Overloading Mode */
static int g_chHits;                    /* charged hits left */
static double g_chUntil;
static double g_s2Until, g_s3Until;
static unsigned g_casts[3], g_charged, g_shots[2], g_spared, g_tunedGuns;

/* who hit her last (the hit hook, another thread): Overloading Mode's third gun goes after them */
static atomic_uint g_threat;
static _Atomic unsigned long long g_threatMs;

/* ---- her health reader (as Faelynn's) -------------------------------------------------------------------------- */
static int HealthOf(uint32_t entity, int32_t* health, uint32_t* life)
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
        uint8_t r[416];
        if (!ReadAt(records + 440u * index, r, sizeof r)) return 0;
        *health = (int32_t)Word(r + 20);
        *life = Word(r + 412);
        return 1;
    }
    return 0;
}
/* an enemy's main armour (its Health record +0x118), kept per resource */
static int ArmourOf(uint64_t res)
{
    static uint64_t keys[128];
    static int8_t vals[128];
    unsigned h = (unsigned)(res ^ (res >> 29)) & 127;
    for (unsigned i = 0; i < 128; i++)
    {
        unsigned k = (h + i) & 127;
        if (keys[k] == res) return vals[k];
        if (keys[k]) continue;
        HfField f = HF_FIELD(Health, armor_118);
        HfLiveValue v;
        memset(&v, 0, sizeof v);
        int a = hf->get_live(self, res, &f, &v) ? (int)v.base.number : 0;
        vals[k] = (int8_t)(a < 0 ? 0 : a > 10 ? 10 : a);
        keys[k] = res;
        return vals[k];
    }
    return 0;
}

/* ---- the guns -------------------------------------------------------------------------------------------------- */
/* where each hovers, from her head along her aim: right, up, forward (m). 0-1: the summoned pair at her shoulders;
 * 2-5: Overloading Mode's four, in an arc behind and above her. */
static const float kGunAt[6][3] = {{-0.55f, 0.25f, 0.3f}, {0.55f, 0.25f, 0.3f},
                                   {-1.1f, 0.6f, -0.3f}, {1.1f, 0.6f, -0.3f}, {-0.6f, 1.1f, -0.5f}, {0.6f, 1.1f, -0.5f}};
static const char* const kPriority[4] = {"the nearest", "the most heavily armoured", "whoever last hit her", "the weakest"};
static double g_gunNext[6];
static uint32_t g_gunTarget[6];
/* her shots' marks (where they landed): S2's guns fire at the next ones on the game thread */
typedef struct Shot { float p[3]; uint32_t target; } Shot;
static Shot g_assist[8];
static int g_nassist;
/* the guns' own shots lately (where they were aimed): a hit there is a gun's, not hers (no assist, no charge) */
static float g_gunAim[16][3];
static double g_gunAimAt[16];
static unsigned g_gunAimNext;

static int GunPos(int i, float out[3])
{
    float pos[3], aim[3];
    if (!PlayerPose(pos, aim)) return 0;
    float h = sqrtf(aim[0] * aim[0] + aim[1] * aim[1]);
    float f[2] = {h > 0.05f ? aim[0] / h : 1, h > 0.05f ? aim[1] / h : 0}, r[2] = {f[1], -f[0]};
    out[0] = pos[0] + r[0] * kGunAt[i][0] + f[0] * kGunAt[i][2];
    out[1] = pos[1] + r[1] * kGunAt[i][0] + f[1] * kGunAt[i][2];
    out[2] = pos[2] + 1.5f + kGunAt[i][1];
    return 1;
}
static int Fire(int i, const float at[3])
{
    float from[3], d[3];
    if (!GunPos(i, from)) return 0;
    for (int k = 0; k < 3; k++) d[k] = at[k] - from[k];
    float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(n > 0.5f)) return 0;
    for (int k = 0; k < 3; k++) d[k] /= n;
    int r = hf->spawn_projectile(self, (uint32_t)s_gunRound, from, d);
    if (r == HF_ACT_WAITING) return 0;
    if (!Act("a summoned gun's shot", r)) return 0;
    unsigned slot = g_gunAimNext++ % 16;
    memcpy(g_gunAim[slot], at, sizeof g_gunAim[slot]);
    g_gunAimAt[slot] = g_now;
    g_shots[i >= 2]++;
    return 1;
}
static int GunsHit(const float p[3])
{
    for (int k = 0; k < 16; k++) if (g_gunAimAt[k] > 0 && g_now - g_gunAimAt[k] < 1.5 && Dist2(p, g_gunAim[k]) < 2.5f * 2.5f) return 1;
    return 0;
}
static int Living(uint32_t e, int32_t* hp)
{
    uint32_t life;
    return HealthOf(e, hp, &life) && *hp > 0;
}
static int TargetPos(uint32_t e, uint64_t res, float out[3])
{
    uint8_t d[24];
    if (!Descriptor(e, d) || Quad(d) != res || !UnitNode(Word(d + 12), 0, out)) return 0;
    out[2] += 1.0f;   /* its body, not its feet */
    return 1;
}
/* Overloading Mode's gun i (2-5) picks by its own priority */
static uint32_t Pick(int i, float at[3])
{
    float me[3], q[3];
    if (!PlayerPose(me, NULL)) return 0;
    int prio = i - 2;
    uint32_t threat = atomic_load(&g_threat);
    int threatFresh = threat && GetTickCount64() - atomic_load(&g_threatMs) < 10000;
    uint32_t best = 0;
    double score = -1e30;
    for (int k = 0; k < FOES; k++)
    {
        uint32_t e = g_foe[k];
        int32_t hp;
        if (!e || !TargetPos(e, g_foeRes[k], q) || !Living(e, &hp)) continue;
        float d2 = Dist2(me, q);
        if (d2 > s_s3Range * s_s3Range) continue;
        double s = -d2;                                                   /* 0: the nearest */
        if (prio == 1) s = ArmourOf(g_foeRes[k]) * 1e6 - d2;              /* 1: the most armour, then the nearest */
        else if (prio == 2) s = (threatFresh && e == threat ? 1e9 : 0) - d2;   /* 2: her attacker, else the nearest */
        else if (prio == 3) s = -(double)hp * 1e4 - d2;                   /* 3: the least health */
        if (s > score) { score = s; best = e; memcpy(at, q, 12); }
    }
    return best;
}
static void GunsFrame(void)
{
    int s2 = g_now < g_s2Until, s3 = g_now < g_s3Until;
    /* the summoned pair: at what she hit */
    while (g_nassist > 0)
    {
        Shot s = g_assist[--g_nassist];
        if (!s2) continue;
        for (int i = 0; i < 2; i++) if (g_now >= g_gunNext[i] && Fire(i, s.p)) g_gunNext[i] = g_now + s_gunEvery;
    }
    if (!s3) return;
    for (int i = 2; i < 6; i++)
    {
        if (g_now < g_gunNext[i]) continue;
        g_gunNext[i] = g_now + s_s3Every;
        float at[3];
        uint32_t t = Pick(i, at);
        if (t != g_gunTarget[i])
        {
            g_gunTarget[i] = t;
            static unsigned told;
            if (t && told++ < 8) Log("Overloading Mode: gun %d (%s) on entity %u", i - 1, kPriority[i - 2], t);
        }
        if (t) Fire(i, at);
    }
}

/* ---- the skills ------------------------------------------------------------------------------------------------- */
static void ChargingMode(void)
{
    if (g_now < g_cd[0]) return;
    g_cd[0] = g_now + s_chCd;
    g_chHits = s_chHits;
    g_chUntil = g_now + s_chWindow;
    g_casts[0]++;
    Log("Charging Mode: her next %d hits deal x%.2f (%.0f s)", s_chHits, (double)s_chMult, (double)s_chWindow);
}
static void SummonGuns(void)
{
    if (g_now < g_cd[1]) return;
    g_cd[1] = g_now + s_s2Cd;
    g_s2Until = g_now + s_s2Seconds;
    g_gunNext[0] = g_gunNext[1] = 0;
    g_casts[1]++;
    Log("the summoned guns: %.0f s at her shoulders", (double)s_s2Seconds);
}
static void OverloadingMode(void)
{
    if (g_now < g_cd[2]) return;
    g_cd[2] = g_now + s_s3Cd;
    g_s3Until = g_now + s_s3Seconds;
    for (int i = 2; i < 6; i++) { g_gunNext[i] = g_now + 0.1 * (i - 2); g_gunTarget[i] = 0; }
    g_casts[2]++;
    Log("Overloading Mode: four more guns for %.0f s", (double)s_s3Seconds);
}

/* ---- Fast Cartridge: her guns' fire rate (each weapon once; from when she next equips it) ---------------------- */
static uint64_t g_tuned[24];
static int g_ntuned;
static void FastCartridge(uint64_t res)
{
    if (!res || res == AVATAR || s_fireRate == 1) return;
    for (int i = 0; i < g_ntuned; i++) if (g_tuned[i] == res) return;
    if (g_ntuned >= 24) return;
    g_tuned[g_ntuned++] = res;
    int n = 0, set = 0;
    double first = 0;
    for (int slot = 0; slot < 3; slot++)
    {
        HfField f = {HfProjectileWeapon_INFO, (uint16_t)(4 + 4 * slot), 4, HF_TYPE_F32, __LINE__};
        HfLiveValue v;
        memset(&v, 0, sizeof v);
        if (!hf->get_live(self, res, &f, &v) || !(v.base.number > 0)) continue;
        if (!n) first = v.base.number;
        n++;
        HfValue x = HF_F32((float)(v.base.number * s_fireRate));
        if (hf->set_live(self, res, &f, &x, 0)) set++;
    }
    if (n) g_tunedGuns++;
    if (g_tunedGuns <= 12) Log("Fast Cartridge: weapon %016llx %s", (unsigned long long)res,
                                n ? "fire rate raised" : "has no projectile fire rate (left alone)");
    if (n && g_tunedGuns <= 12) Log("  %d of %d rate slots set, %.0f -> %.0f rpm", set, n, first, first * s_fireRate);
}
static void OnWeaponExu(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfWeaponEvent* w = (const HfWeaponEvent*)info;
    if (!w->avatar || w->avatar != atomic_load(&g_avatar)) return;
    FastCartridge(w->resource);
}

/* ---- the damage event: her shots ------------------------------------------------------------------------------- */
static int AnyGun(void) { return g_now < g_s2Until || g_now < g_s3Until; }
/* a friend of hers: another Helldiver, or anything on the kit base's friend list */
static int Friend(uint32_t e)
{
    uint8_t d[24];
    if (!Descriptor(e, d)) return 0;
    uint64_t res = Quad(d);
    return res == AVATAR || FriendRes(res);
}
static void DamageTaken(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    (void)e; (void)d; (void)me;
}
static void DamageDealt(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    if (d->amount <= 0 || !MineHit(d)) return;
    if (s_noFriendlyFire && AnyGun() && Friend(d->target)) { e->cancel = 1; g_spared++; return; }
    if (!IsShot(d)) return;
    int gun = GunsHit(d->position);
    double mult = s_blessMult;                                                  /* Angel's Blessings */
    if (!gun && g_chHits > 0 && g_now <= g_chUntil) { mult *= s_chMult; g_chHits--; g_charged++; }   /* Charging Mode */
    if (mult != 1) ScaleDamage(d, mult);
    if (gun) return;
    NoteTarget(d);
    if (g_now < g_s2Until && g_nassist < 8)
    {
        memcpy(g_assist[g_nassist].p, d->position, sizeof d->position);
        g_assist[g_nassist].target = d->target;
        g_nassist++;
    }
}
/* a hit on her (the hit hook): who dealt it, for Overloading Mode's third gun */
static int HitTaken(const uint8_t* h, void* result, unsigned long long now)
{
    (void)result;
    uint32_t me = atomic_load(&g_avatar), dealer = Word(h + 0x80), owner = Word(h + 0x100), who = owner && owner != me ? owner : dealer;
    if (who && who != me) { atomic_store(&g_threat, who); atomic_store(&g_threatMs, now); }
    return 0;
}

/* ---- events ------------------------------------------------------------------------------------------------------ */
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    if (s->resource != AVATAR) { if (FoeKind(s->resource)) AddFoe(s->entity, s->resource); return; }
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
        g_s2Until = g_s3Until = 0;   /* her guns go with her */
    }
}
static void Reset(const char* why)
{
    if (g_casts[0] || g_casts[1] || g_casts[2])
        Log("%s: casts %u / %u / %u, %u charged hits, %u assist and %u Overloading shots, %u hits on allies spared", why, g_casts[0],
            g_casts[1], g_casts[2], g_charged, g_shots[0], g_shots[1], g_spared);
    int start = !strcmp(why, "mission start");
    Body(start);
    /* the Guard Dog's package: its rounds fly (and sound) only once that is in */
    if (start && s_gunRound == 7 && !hf->load_package(self, UINT64_C(0xA14F1A98C80575AF))) Log("the Guard Dog's package could not be loaded ahead: the guns' first shots may wait");
    if (start && s_hellbomb && s_hellbombRow == 125 && !hf->load_package(self, UINT64_C(0x346D785D6CA13FF4))) Log("the Hellbomb's package could not be loaded ahead");
    g_boomPending = 0;
    g_boomArmed = 0;
    memset(g_cd, 0, sizeof g_cd);
    memset(g_casts, 0, sizeof g_casts);
    memset(g_shots, 0, sizeof g_shots);
    memset(g_gunNext, 0, sizeof g_gunNext);
    memset(g_gunTarget, 0, sizeof g_gunTarget);
    memset(g_gunAimAt, 0, sizeof g_gunAimAt);
    g_charged = g_spared = 0;
    g_chHits = 0;
    g_chUntil = g_s2Until = g_s3Until = 0;
    g_nassist = 0;
    g_lastTarget = 0;
    atomic_store(&g_threat, 0);
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gameSize = 0, exeSize = 0;
    uintptr_t g = hf->module_base("game.dll", &gameSize), x = hf->module_base("helldivers2.exe", &exeSize);
    if (!g || !x || gameSize != 74727424u || exeSize != 60719104u) { Log("REFUSED: the game's modules are not build " BUILD "'s: her kit stays off"); return; }
    game = g;
    exe = x;
    HitsInstall("own-blast guard");
    g_staminaOk = hf->code_matches(game + 0xA50E2D, "f3 42 0f 11 8c 11 54 78 54 00");
}

/* ---- the HUD state (read by the Exusiai HUD addon through LuaJIT's ffi) ----------------------------------------- */
typedef struct ExusiaiHud
{
    uint32_t version, in_mission;
    float cd[3], cd_max[3];           /* seconds left, full cooldowns: Charging Mode, the guns, Overloading Mode */
    uint32_t charge_hits;             /* charged hits left */
    float charge, guns, overload;     /* seconds left of each */
    uint32_t keys[3];
    char key_label[3][24];
} ExusiaiHud;
static ExusiaiHud g_hud = {1};
HF_MOD_EXPORT ExusiaiHud* exusiai_hud_state(void)
{
    g_hud.in_mission = (uint32_t)(g_inMission && g_alive);
    float m[3] = {s_chCd, s_s2Cd, s_s3Cd};
    for (int i = 0; i < 3; i++) { g_hud.cd[i] = (float)fmax(0, g_cd[i] - g_now); g_hud.cd_max[i] = m[i]; }
    int charged = g_chHits > 0 && g_now <= g_chUntil;
    g_hud.charge_hits = (uint32_t)(charged ? g_chHits : 0);
    g_hud.charge = charged ? (float)(g_chUntil - g_now) : 0;
    g_hud.guns = (float)fmax(0, g_s2Until - g_now);
    g_hud.overload = (float)fmax(0, g_s3Until - g_now);
    for (int i = 0; i < 3; i++)
    {
        g_hud.keys[i] = (uint32_t)s_keys[i].vk;
        memcpy(g_hud.key_label[i], s_keys[i].label, sizeof g_hud.key_label[i]);
    }
    return &g_hud;
}
/* in-game key bindings: the HUD registers her three skills with Mod Bindings Menu and passes each press here */
static _Atomic unsigned long long g_bindMs;
static atomic_int g_bindPress;
HF_MOD_EXPORT int exusiai_input(unsigned version, int skill)
{
    if (version != 1) return 0;
    atomic_store(&g_bindMs, GetTickCount64());
    if (skill >= 1 && skill <= 3) atomic_fetch_or(&g_bindPress, 1 << (skill - 1));
    return 1;
}
HF_MOD_EXPORT int exusiai_key(unsigned version, int skill, int* vk, int* mods)
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
    if (!g_inMission) return;
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
        if (press && GetTickCount64() - fired[i] > 300 && g_alive && atomic_load(&g_avatar) && (fired[i] = GetTickCount64(), 1))
        {
            if (i == 0) ChargingMode();
            else if (i == 1) SummonGuns();
            else OverloadingMode();
        }
        prev[i] = down;
    }
    if (g_alive) GunsFrame();
    Exhaustion();
    atomic_exchange(&g_ownStopped, 0);
    atomic_exchange(&g_kdStopped, 0);
}

/* ---- Mod Options Menu ------------------------------------------------------------------------------------------- */
static const KbOption kOptions[] = {
    {"fire_rate", &s_fireRate, NULL}, {"bless_mult", &s_blessMult, NULL}, {"ch_cooldown", &s_chCd, NULL}, {"ch_hits", NULL, &s_chHits},
    {"ch_mult", &s_chMult, NULL}, {"s2_cooldown", &s_s2Cd, NULL}, {"s2_seconds", &s_s2Seconds, NULL}, {"gun_every", &s_gunEvery, NULL},
    {"s3_cooldown", &s_s3Cd, NULL}, {"s3_seconds", &s_s3Seconds, NULL}, {"s3_every", &s_s3Every, NULL}, {"s3_range", &s_s3Range, NULL},
    {"no_friendly_fire", NULL, &s_noFriendlyFire}, {"hp_mult", &s_hpMult, NULL}, {"stamina_mult", &s_staminaMult, NULL},
    {"hellbomb_on_exhaustion", NULL, &s_hellbomb},
};
static void OptionAfter(const char* name, int changed)
{
    if (changed && g_chHits > s_chHits) g_chHits = s_chHits;
}
HF_MOD_EXPORT int exusiai_option_get(const char* name, double* out)
{
    return OptionGet(name, out);
}
HF_MOD_EXPORT int exusiai_option_set(const char* name, double value)
{
    return OptionSet(name, value);
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    kb_ini = "exusiai.ini";
    kb_key_names[0] = "key_charge";
    kb_key_names[1] = "key_guns";
    kb_key_names[2] = "key_overload";
    KB_SETTINGS(kSettings);
    KB_OPTIONS(kOptions);
    kb_option_after = OptionAfter;
    kb_damage_taken = DamageTaken;
    kb_damage_dealt = DamageDealt;
    kb_hit_taken = HitTaken;
    /* her body: a Sankta's, not a T-Doll's (the kit base's defaults are the T-Dolls') */
    s_hpMult = 1.13f;
    s_staminaMult = 1.0f;
    s_hellbomb = 0;
    if (!api || api->version < 13 || api->size < offsetof(HfApi, code_matches) + sizeof api->code_matches) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    LoadSettings();
    api->log(mod, "Exusiai %s: keys %s / %s / %s (Charging Mode, the summoned guns, Overloading Mode)", VERSION, s_keys[0].label,
             s_keys[1].label, s_keys[2].label);
    if (s_badKey) api->log(mod, "exusiai.ini: a key could not be read (bits %d: charge, guns, overload); its default stays", s_badKey);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) &&
             api->subscribe(mod, HF_ON_WEAPON, OnWeaponExu, NULL, 0) && api->on_game_thread(mod, Tick, NULL);
    if (!ok) api->log(mod, "events or the game thread are unavailable: Exusiai stays off");
    return 0;
}
