/* Andoris (yui.andoris): Andoris's kit from Girls' Frontline 2 as abilities on keys, for your own Helldiver (solo or
 * host). A Bulwark: she takes the hits, mends herself and sets enemies up for Electric damage. Design:
 * docs/andoris-design.md.
 *
 * 0.1.0 (first build, untested): her Z and her passive; her stick bugs (X) and Reinforced Program (C) come next.
 *   Simplified Decision (key_s1, default Z): her next shot that lands on an enemy is an Electric shot: it deals
 *     sd_mult times more (1.1: +110%), an arc goes off on the hit (arc_explosion: the G-31 Arc grenade's), and the enemy
 *     is Electrocharged for ec_seconds s. Cooldown sd_cooldown s from the shot.
 *   Electrocharge: her hits on it heal her ec_heal of the damage they deal and deal ec_mult more; every ec_spread s it
 *     spreads to the enemies within ec_radius m of it.
 *   Delayed Response (passive):
 *     Self-Repair: once a life, armed sr_arm s after she lands: a hit that would kill her is cancelled and she is mended
 *       to sr_health of her health.
 *     Dullness: each hit on her gives a stack (dull_max at most), each dull_per less damage taken; one is lost every
 *       dull_decay s without a hit.
 *     Taking damage heals: hurt_heal of her health back after a hit, at most once every hurt_every s.
 *   T-Doll body: hp_mult health, stamina_mult sprint, and a Hellbomb on her if her stamina ever runs dry.
 *
 * Research credit: the hit-result layout, the hooked functions, the entity and scene-graph reads come from Helldiver
 * Survival Rework 0.3.2 by enochlophobianumerophobia (MIT) and Scope of Authority's Apex knockdown notes, whose research
 * used HD2Runtime by SkyeShade's catalogues (https://github.com/SkyeShade/HD2Runtime). See THIRD_PARTY_NOTICES.md. */
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
#include "kitbase/kb_guard.h"
#include "kitbase/kb_settings.h"
#include "kitbase/kb_options.h"
#include "kitbase/kb_roster.h"

KB_ROSTER_EXPORTS(andoris)
KB_OPTIONS_EXPORTS(andoris)

/* ---- settings ------------------------------------------------------------------------------------------------- */
static float s_sdCd = 6, s_sdMult = 1.1f;
static int s_arcRow = 157;
static float s_ecSeconds = 10, s_ecHeal = 0.2f, s_ecMult = 0.15f, s_ecSpread = 5, s_ecRadius = 3;
static float s_srArm = 30, s_srHealth = 0.6f;
static int s_srOn = 1, s_dullMax = 8;
static float s_dullPer = 0.03f, s_dullDecay = 2, s_hurtHeal = 0.05f, s_hurtEvery = 1;
static int s_optionsKey = 0x78;   /* options_key: the key that opens the options screen (120: F9; 0: none) */
static const KbSetting kSettings[] = {
    KB_F("sd_cooldown", s_sdCd, 0, 600), KB_F("sd_mult", s_sdMult, 0, 20), KB_I("arc_explosion", s_arcRow, 0, 100000),
    KB_F("ec_seconds", s_ecSeconds, 0, 120), KB_F("ec_heal", s_ecHeal, 0, 5), KB_F("ec_mult", s_ecMult, 0, 5), KB_F("ec_spread", s_ecSpread, 0.5, 60),
    KB_F("ec_radius", s_ecRadius, 0, 30), KB_I("sr_on", s_srOn, 0, 1), KB_F("sr_arm", s_srArm, 0, 600), KB_F("sr_health", s_srHealth, 0.05, 1),
    KB_I("dull_max", s_dullMax, 0, 50), KB_F("dull_per", s_dullPer, 0, 0.2), KB_F("dull_decay", s_dullDecay, 0.1, 60),
    KB_F("hurt_heal", s_hurtHeal, 0, 1), KB_F("hurt_every", s_hurtEvery, 0, 60),
    KB_F("hp_mult", s_hpMult, 0.1, 10), KB_F("stamina_mult", s_staminaMult, 0.1, 20), KB_I("hellbomb_on_exhaustion", s_hellbomb, 0, 1),
    KB_I("hellbomb_explosion", s_hellbombRow, 0, 100000), KB_F("hellbomb_hold", s_hellbombHold, 0, 10), KB_I("options_key", s_optionsKey, 0, 255),
};

/* The default andoris.ini: tools/inigen.py writes the file from this text (the mod never writes it) */
static __attribute__((unused)) void DefaultIni(FILE* f)
{
fprintf(f, "; Andoris %s. Keys: a letter, digit, F1-F24, Mouse3, Mouse4 or Mouse5, with Shift+, Ctrl+ or Alt+ in front for a\n"
               "; combo (Shift+Q), or Shift, Ctrl or Alt alone. The game still sees the keys too, so pick ones it leaves alone.\n"
               "key_s1 = %s\nkey_s2 = %s\nkey_s3 = %s\n"
               "; Simplified Decision: cooldown; her next shot on an enemy deals sd_mult more (1.1: +110%%) and sets off\n"
               "; arc_explosion on it (157: the G-31 Arc grenade's; 0: none).\nsd_cooldown = %g\nsd_mult = %g\narc_explosion = %d\n"
               "; Electrocharge: how long it lasts; her hits on it heal her ec_heal of their damage and deal ec_mult more; it\n"
               "; spreads every ec_spread s to the enemies within ec_radius m.\n"
               "ec_seconds = %g\nec_heal = %g\nec_mult = %g\nec_spread = %g\nec_radius = %g\n"
               "; Self-Repair (sr_on 1): once a life, sr_arm s after she lands, a hit that would kill her is cancelled and she\n"
               "; is mended to sr_health of her health.\nsr_on = %d\nsr_arm = %g\nsr_health = %g\n"
               "; Dullness: a stack for each hit on her (dull_max at most), each dull_per less damage taken; one lost every\n"
               "; dull_decay s without a hit. Taking damage heals: hurt_heal of her health after a hit, once each hurt_every s.\n"
               "dull_max = %d\ndull_per = %g\ndull_decay = %g\nhurt_heal = %g\nhurt_every = %g\n"
               "; The T-Doll's body: hp_mult times a Helldiver's health, stamina_mult times her sprint before the bar empties.\n"
               "hp_mult = %g\nstamina_mult = %g\n"
               "; If her stamina ever runs out, hellbomb_on_exhaustion (1) sets off hellbomb_explosion (125: the B-100 Portable\n"
               "; Hellbomb's) on her, once the bar has stayed empty hellbomb_hold s (it flickers to 0 at times).\n"
               "hellbomb_on_exhaustion = %d\nhellbomb_explosion = %d\nhellbomb_hold = %g\n"
               "; The options screen's key (120: F9; 0: none).\noptions_key = %d\n",
        VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, (double)s_sdCd, (double)s_sdMult, s_arcRow,
        (double)s_ecSeconds, (double)s_ecHeal, (double)s_ecMult, (double)s_ecSpread, (double)s_ecRadius,
        s_srOn, (double)s_srArm, (double)s_srHealth, s_dullMax, (double)s_dullPer, (double)s_dullDecay, (double)s_hurtHeal, (double)s_hurtEvery,
        (double)s_hpMult, (double)s_staminaMult, s_hellbomb, s_hellbombRow, (double)s_hellbombHold, s_optionsKey);
}

/* ---- reads: health, positions ------------------------------------------------------------------------------------ */
/* An entity's health record (HD2Runtime's research: the health manager at game+0x3326688, 440-byte records, health at
 * +20, life at +412: 0 alive) */
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
        if (life) *life = Word(r + 412);
        return 1;
    }
    return 0;
}
static int Alive(uint32_t e) { int32_t hp; uint32_t life; return HealthOf(e, &hp, &life) && hp > 0 && life == 0; }
static int TargetPos(uint32_t e, float out[3])
{
    uint8_t d[24];
    if (!Descriptor(e, d) || !UnitNode(Word(d + 12), 0, out)) return 0;
    out[2] += 1.0f;
    return 1;
}

/* ---- state ---------------------------------------------------------------------------------------------------- */
static double g_cdSd;                    /* Simplified Decision ready at */
static int g_armed;                      /* her next shot is the Electric shot */
static int g_dull;                       /* Dullness stacks */
static double g_dullAt, g_hurtHealAt, g_landedAt;
static int g_repaired;                   /* Self-Repair spent this life */
static int32_t g_hpMax;                  /* her health at its highest this life (her maximum, as near as is read) */
static unsigned g_casts[3], g_ecHits, g_spreads, g_dullHits, g_healsHurt, g_healsEc, g_selfSpared, g_repairs;
static double g_healed;
typedef struct Charge { uint32_t e; double until, spreadAt; } Charge;
#define CHARGES 64
static Charge g_ec[CHARGES];
typedef struct OwnArc { float p[3]; double at; } OwnArc;
static OwnArc g_arcs[8];
static unsigned g_arcNext;
static struct { float p[3]; double until; } g_arcRetry[6];
static int g_arcRetryN;

static Charge* ChargeOf(uint32_t e)
{
    for (int i = 0; i < CHARGES; i++) if (g_ec[i].e == e && g_ec[i].until > g_now) return &g_ec[i];
    return NULL;
}
static int Electrocharge(uint32_t e)
{
    if (!e || e == atomic_load(&g_avatar)) return 0;
    Charge* c = ChargeOf(e);
    if (!c)
        for (int i = 0; i < CHARGES; i++)
            if (g_ec[i].until <= g_now) { c = &g_ec[i]; c->e = e; c->spreadAt = g_now + s_ecSpread; break; }
    if (!c) return 0;
    c->until = g_now + s_ecSeconds;
    return 1;
}
static int Charged(void) { int n = 0; for (int i = 0; i < CHARGES; i++) n += g_ec[i].until > g_now; return n; }

/* her arcs: the explosion at a point, after its packages if need be; where and when, so they spare her */
static void ArcAt(const float at[3])
{
    if (s_arcRow <= 0) return;
    int r = hf->spawn_explosion(self, (uint32_t)s_arcRow, at);
    if (r == HF_ACT_WAITING && g_arcRetryN < 6) { memcpy(g_arcRetry[g_arcRetryN].p, at, 12); g_arcRetry[g_arcRetryN++].until = g_now + 2; return; }
    if (!Act("Simplified Decision's arc", r)) return;
    OwnArc* a = &g_arcs[g_arcNext++ % 8];
    memcpy(a->p, at, 12);
    a->at = g_now;
}
static void ArcRetryFrame(void)
{
    for (int i = 0; i < g_arcRetryN; i++)
    {
        int r = hf->spawn_explosion(self, (uint32_t)s_arcRow, g_arcRetry[i].p);
        if (r != HF_ACT_WAITING || g_now > g_arcRetry[i].until)
        {
            if (Act("Simplified Decision's arc (after its packages)", r)) { OwnArc* a = &g_arcs[g_arcNext++ % 8]; memcpy(a->p, g_arcRetry[i].p, 12); a->at = g_now; }
            g_arcRetry[i] = g_arcRetry[--g_arcRetryN];
            i--;
        }
    }
}
static int NearOwnArc(const float at[3])
{
    for (int i = 0; i < 8; i++) if (g_arcs[i].at > 0 && g_now - g_arcs[i].at <= 1.5 && Dist2(at, g_arcs[i].p) <= 100.0f) return 1;
    return 0;
}

/* ---- the abilities ---------------------------------------------------------------------------------------------- */
static void SimplifiedDecision(void)
{
    if (g_armed) { Log("Simplified Decision: already readied (her next shot)"); return; }
    if (g_now < g_cdSd) { Log("Simplified Decision: %.0f s of cooldown left", g_cdSd - g_now); return; }
    g_armed = 1;
    g_casts[0]++;
    Log("Simplified Decision: readied (her next shot on an enemy)");
}
static void NotYet(int i)
{
    static double told[3];
    if (g_now < told[i]) return;
    told[i] = g_now + 5;
    Log("%s: not in this build yet (Andoris %s: Simplified Decision and her passive)", i == 1 ? "Tender Offensive" : "Reinforced Program", VERSION);
}

/* ---- Electrocharge's spread --------------------------------------------------------------------------------------- */
static void SpreadFrame(void)
{
    for (int i = 0; i < CHARGES; i++)
    {
        Charge* c = &g_ec[i];
        if (c->until <= g_now || g_now < c->spreadAt) continue;
        c->spreadAt = g_now + s_ecSpread;
        float p[3], q[3];
        if (!Alive(c->e) || !TargetPos(c->e, p)) { c->until = 0; continue; }
        int n = 0;
        for (int k = 0; k < FOES && s_ecRadius > 0; k++)
        {
            uint32_t e = g_foe[k];
            if (!e || e == c->e || ChargeOf(e) || !TargetPos(e, q) || Dist2(p, q) > s_ecRadius * s_ecRadius || !Alive(e)) continue;
            if (Electrocharge(e)) n++;
        }
        if (n) { g_spreads += (unsigned)n; static unsigned told; if (told++ < 10) Log("Electrocharge: spread from %u to %d enem%s", c->e, n, n == 1 ? "y" : "ies"); }
    }
}

/* ---- her health ------------------------------------------------------------------------------------------------- */
static int32_t HpNow(void) { int32_t hp = 0; uint32_t life = 0; return HealthOf(atomic_load(&g_avatar), &hp, &life) && life == 0 ? hp : -1; }
static void Mend(double amount, unsigned* count)
{
    if (amount < 1 || !hf->heal) return;
    if (Act("a heal", hf->heal(self, (float)amount))) { g_healed += amount; if (count) (*count)++; }
}

/* ---- events ------------------------------------------------------------------------------------------------------ */
/* A hit on her */
static void DamageTaken(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    /* her own arcs spare her */
    if (d->kind != 0 && NearOwnArc(d->position)) { e->cancel = 1; g_selfSpared++; return; }
    if (d->amount == 0) return;
    int32_t hp = HpNow();
    if (hp > g_hpMax) g_hpMax = hp;
    /* Self-Repair: once a life, a hit that would kill her */
    if (s_srOn && !g_repaired && g_alive && g_now - g_landedAt >= s_srArm && hp > 0 && GuardFatal(d, hp))
    {
        e->cancel = 1;
        g_repaired = 1;
        g_repairs++;
        double want = s_srHealth * (g_hpMax > 0 ? g_hpMax : hp) - hp;
        Mend(want, NULL);
        Log("Self-Repair: a hit for %d (kind %u) would have killed her (%d health): cancelled, mended by %.0f", d->amount, d->kind, hp, want);
        return;
    }
    /* Dullness: the stacks she has take their share off; then the hit adds one */
    if (g_dull > 0 && s_dullPer > 0) { GuardShare(e, d, 1.0 - s_dullPer * g_dull); g_dullHits++; }
    if (g_dull < s_dullMax) g_dull++;
    g_dullAt = g_now;
    /* taking damage heals (after the hit, at most once every hurt_every s) */
    if (!e->cancel && s_hurtHeal > 0 && g_now - g_hurtHealAt >= s_hurtEvery && g_hpMax > 0)
    {
        g_hurtHealAt = g_now;
        Mend(s_hurtHeal * g_hpMax, &g_healsHurt);
    }
}
/* Any other hit: hers on enemies */
static void DamageDealt(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    if (d->amount <= 0 || d->target == me) return;
    int mine = IsShot(d) || (MineHit(d) && d->kind != 0);
    if (!mine) return;
    NoteTarget(d);
    double mult = 1;
    if (g_armed && IsShot(d))
    {
        g_armed = 0;
        g_cdSd = g_now + s_sdCd;
        mult += s_sdMult;
        ArcAt(d->position);
        Electrocharge(d->target);
        Log("Simplified Decision: the Electric shot on %u (%d x %.2f), an arc on it, Electrocharged %.0f s", d->target, d->amount, 1 + (double)s_sdMult, (double)s_ecSeconds);
    }
    Charge* c = ChargeOf(d->target);
    if (c)
    {
        mult += s_ecMult;
        g_ecHits++;
        if (s_ecHeal > 0) Mend(d->amount * mult * s_ecHeal, &g_healsEc);
    }
    if (mult != 1) ScaleDamage(d, mult);
}

static void OnKill(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfKillEvent* k = (const HfKillEvent*)info;
    DropFoe(k->victim);
    Charge* c = ChargeOf(k->victim);
    if (c) c->until = 0;
    if (k->victim == g_lastTarget) g_lastTarget = 0;
}
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
static void NewLife(void)
{
    g_landedAt = g_now;
    g_repaired = 0;
    g_hpMax = 0;
    g_dull = 0;
    g_armed = 0;
}
static void OnPlayer(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    if (!p->local) return;
    if (info->kind == HF_ON_PLAYER_SPAWN)
    {
        if (p->entity && p->entity != atomic_load(&g_avatar)) { atomic_store(&g_avatar, p->entity); g_avatarNet = 0; }
        g_alive = 1;
        NewLife();
        LearnNet();
    }
    else { g_alive = 0; g_armed = 0; }
}
/* her Helldiver's network id from the player list (Hina 0.1.4: no spawn event of hers may name it). PlayerManager: count
 * +0x84, slot i's peer +0x2C8 + 0x38 i, its Helldiver's network id +0x3A8 + 0x20 i; LocalUser's peer +0xB398 */
static void NetFromPlayers(void)
{
    static double at;
    if (g_now < at) return;
    at = g_now + 1;
    uintptr_t pm = (uintptr_t)hf_global_PlayerManager(self), lu = (uintptr_t)hf_global_LocalUser(self);
    uint64_t mine = 0, peer = 0;
    uint32_t count = 0, net = 0;
    if (!pm || !lu || !ReadAt(lu + 0xB398, &mine, 8) || !mine || !ReadAt(pm + 0x84, &count, 4) || count > 4) return;
    for (uint32_t i = 0; i < count; i++)
        if (ReadAt(pm + 0x2C8 + 0x38u * i, &peer, 8) && peer == mine && ReadAt(pm + 0x3A8 + 0x20u * i, &net, 4) && NetOk(net))
        {
            g_avatarNet = net;
            Log("her Helldiver's network id %u (the player list, slot %u)", net, i);
            return;
        }
}

static void Reset(const char* why)
{
    int start = !strcmp(why, "mission start");
    if (start) KitDecide();
    if (g_casts[0] || g_dullHits || g_repairs)
        Log("%s: Simplified Decision %u; %u hits on Electrocharged enemies, %u spreads; %u hits on her eased by Dullness, %u Self-Repairs, %u own arcs spared her; "
            "heals %u (hurt) + %u (Electrocharge), %.0f health in all",
            why, g_casts[0], g_ecHits, g_spreads, g_dullHits, g_repairs, g_selfSpared, g_healsHurt, g_healsEc, g_healed);
    g_boomPending = 0;
    Body(0);
    if (start) Body(1);
    if (start && s_hellbomb && s_hellbombRow == 125 && !hf->load_package(self, UINT64_C(0x346D785D6CA13FF4)))
        Log("the Hellbomb's package could not be loaded ahead");   /* loadout/hellbomb_backpack */
    memset(g_ec, 0, sizeof g_ec);
    memset(g_arcs, 0, sizeof g_arcs);
    memset(g_casts, 0, sizeof g_casts);
    g_arcRetryN = 0;
    g_cdSd = 0;
    g_ecHits = g_spreads = g_dullHits = g_healsHurt = g_healsEc = g_selfSpared = g_repairs = 0;
    g_healed = 0;
    g_lastTarget = 0;
    NewLife();
    if (start) g_avatarNet = 0;
    memset(g_foe, 0, sizeof g_foe);
    memset(g_foeRes, 0, sizeof g_foeRes);
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gameSize = 0, exeSize = 0;
    uintptr_t g = hf->module_base("game.dll", &gameSize), x = hf->module_base("helldivers2.exe", &exeSize);
    if (!g || !x || gameSize != 74727424u || exeSize != 60719104u) { Log("REFUSED: the game's modules are not build " BUILD "'s: her kit stays off"); return; }
    game = g;
    exe = x;
    HitsInstall("her hits");
    Log("ready: Simplified Decision on %s (cooldown %.0f s, +%.0f%%, arc %d); Delayed Response (Self-Repair %s, Dullness up to %d x %.0f%%, %.0f%% back per hit)",
        s_keys[0].label, (double)s_sdCd, (double)(s_sdMult * 100), s_arcRow, s_srOn ? "on" : "off", s_dullMax, (double)(s_dullPer * 100), (double)(s_hurtHeal * 100));
}

/* ---- for the HUD (read by the Andoris HUD addon through LuaJIT's ffi; layout version 1, the same struct in andoris_hud.lua) */
typedef struct AndorisHud
{
    uint32_t version, in_mission;
    float cd[3], cd_max[3];            /* seconds left, full cooldowns: Simplified Decision, Tender Offensive, Reinforced Program */
    uint32_t armed;                    /* Simplified Decision readied */
    uint32_t charged;                  /* Electrocharged enemies */
    uint32_t dull, dull_max;           /* Dullness stacks */
    uint32_t repair;                   /* Self-Repair: 0 spent, 1 arming, 2 ready */
    float repair_in;                   /* seconds until it is armed */
    uint32_t keys[3];
    char key_label[3][24];
} AndorisHud;
static AndorisHud g_hud = {1};
HF_MOD_EXPORT AndorisHud* andoris_hud_state(void)
{
    g_hud.in_mission = (uint32_t)(g_inMission && g_alive && kb_kit_on);
    g_hud.cd[0] = (float)fmax(0, g_cdSd - g_now);
    g_hud.cd_max[0] = s_sdCd;
    g_hud.cd[1] = g_hud.cd[2] = 0;
    g_hud.cd_max[1] = g_hud.cd_max[2] = 1;
    g_hud.armed = (uint32_t)g_armed;
    g_hud.charged = (uint32_t)Charged();
    g_hud.dull = (uint32_t)g_dull;
    g_hud.dull_max = (uint32_t)s_dullMax;
    double in = s_srArm - (g_now - g_landedAt);
    g_hud.repair = !s_srOn || g_repaired ? 0 : in > 0 ? 1 : 2;
    g_hud.repair_in = in > 0 ? (float)in : 0;
    for (int i = 0; i < 3; i++)
    {
        g_hud.keys[i] = (uint32_t)s_keys[i].vk;
        memcpy(g_hud.key_label[i], s_keys[i].label, sizeof g_hud.key_label[i]);
    }
    return &g_hud;
}

/* ---- in-game key bindings: the HUD registers her three skills with Mod Bindings Menu and passes each press here ------ */
static _Atomic unsigned long long g_bindMs;
static atomic_int g_bindPress;
HF_MOD_EXPORT int andoris_input(unsigned version, int skill)
{
    if (version != 1) return 0;
    atomic_store(&g_bindMs, GetTickCount64());
    if (skill >= 1 && skill <= 3) atomic_fetch_or(&g_bindPress, 1 << (skill - 1));
    return 1;
}
HF_MOD_EXPORT int andoris_key(unsigned version, int skill, int* vk, int* mods)
{
    if (version != 1 || skill < 1 || skill > 3 || !vk || !mods) return 0;
    *vk = s_keys[skill - 1].vk;
    *mods = s_keys[skill - 1].mods;
    return 1;
}

static void Tick(HfMod* mod, float dt, void* user)
{
    KbOptionsTick();   /* the options screen's changes, in a mission or not */
    g_now += dt > 0 && dt < 1 ? dt : 0;
    int inMission = hf->game_state(self) == HF_STATE_MISSION;
    if (inMission != g_inMission) { Reset(inMission ? "mission start" : "mission end"); g_inMission = inMission; }
    if (!g_inMission || !game || !kb_kit_on) return;   /* (standing by: another kit is picked) */
    MyItems();
    {
        /* a new Helldiver (her landing, a reinforcement): a new life for Self-Repair and her stacks (the player spawn
         * event comes only on the ship: Hina 0.1.4) */
        static uint32_t lastAv;
        uint32_t av = atomic_load(&g_avatar);
        if (av && av != lastAv) { lastAv = av; NewLife(); Log("her Helldiver %u: a new life (Self-Repair arms in %.0f s)", av, (double)s_srArm); }
    }
    if (!NetOk(g_avatarNet)) NetFromPlayers();
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
            if (i == 0) SimplifiedDecision();
            else NotYet(i);
        }
        prev[i] = down;
    }
    ArcRetryFrame();
    SpreadFrame();
    if (g_dull > 0 && g_now - g_dullAt >= s_dullDecay) { g_dull--; g_dullAt = g_now; }
    int32_t hp = HpNow();
    if (hp > g_hpMax) g_hpMax = hp;
    Exhaustion();
}

/* ---- settings read and set by name (the options screen, the Mod Options Menu) ------------------------------------- */
static const KbOption kOptions[] = {
    {"sd_cooldown", &s_sdCd, NULL}, {"sd_mult", &s_sdMult, NULL}, {"ec_seconds", &s_ecSeconds, NULL}, {"ec_heal", &s_ecHeal, NULL}, {"ec_mult", &s_ecMult, NULL},
    {"ec_spread", &s_ecSpread, NULL}, {"ec_radius", &s_ecRadius, NULL}, {"sr_on", NULL, &s_srOn}, {"sr_arm", &s_srArm, NULL}, {"sr_health", &s_srHealth, NULL},
    {"dull_max", NULL, &s_dullMax}, {"dull_per", &s_dullPer, NULL}, {"dull_decay", &s_dullDecay, NULL}, {"hurt_heal", &s_hurtHeal, NULL}, {"hurt_every", &s_hurtEvery, NULL},
    {"hp_mult", &s_hpMult, NULL}, {"stamina_mult", &s_staminaMult, NULL}, {"hellbomb_on_exhaustion", NULL, &s_hellbomb},
};
static void OptionAfter(const char* name, int changed)
{
    if (g_dull > s_dullMax) g_dull = s_dullMax;
}
HF_MOD_EXPORT int andoris_option_get(const char* name, double* out) { return OptionGet(name, out); }
HF_MOD_EXPORT int andoris_option_set(const char* name, double value) { return OptionSet(name, value); }

#include "kit_options.h"   /* kOpRows: the options screen's rows (kitbase/kb_options.h) */
#include "kit_icon.h"      /* kOpIcon, KB_OP_ACCENT */
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 17 || api->size < offsetof(HfApi, global) + sizeof api->global) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    RosterJoin("andoris", "Andoris");
    kb_ini = "andoris.ini";
    kb_key_names[0] = "key_s1";
    kb_key_names[1] = "key_s2";
    kb_key_names[2] = "key_s3";
    KB_SETTINGS(kSettings);
    KB_OPTIONS(kOptions);
    kb_option_after = OptionAfter;
    kb_damage_taken = DamageTaken;
    kb_damage_dealt = DamageDealt;
    LoadSettings();
    KbOptionsStart(api, mod, "andoris", "Andoris", VERSION, kOpRows, (int)(sizeof kOpRows / sizeof kOpRows[0]), kOpIcon, KB_OP_ICON, KB_OP_ACCENT, s_optionsKey);
    api->log(mod, "Andoris %s: keys %s / %s / %s (Simplified Decision; Tender Offensive and Reinforced Program not in this build)", VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label);
    if (s_badKey) api->log(mod, "andoris.ini: a key could not be read (bits %d: s1, s2, s3); its default stays", s_badKey);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) &&
             api->on_game_thread(mod, Tick, NULL);
    if (!ok) api->log(mod, "events or the game thread are unavailable: Andoris stays off");
    return 0;
}
