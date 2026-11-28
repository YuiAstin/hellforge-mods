/* Hina (yui.hina): Sorasaki Hina's kit from Blue Archive (the Gehenna Prefect, MG) as abilities on keys, for your own
 * Helldiver (solo or host), built on the kit base (kitbase/, docs/kit-base.md; her design: docs/hina-design.md). Her
 * identity (Yui): put as many rounds down range as possible, as efficiently as possible.
 *
 *   Destroyer of Denouement (key_s1, default Z): her signature gun in her hands, the way W's King of Hearts gives the
 *     GL-21 (the Wield Probe's swap): a copy of the MG-43 Machine Gun (gun_base 1, the default), or of the M-1000
 *     Maxigun (gun_base 0, when this profile owns it; else the MG-43's) with gun_rounds rounds, gun_rpm rounds a
 *     minute and armour penetration gun_ap (its own entity, round and damage: add_entity, add_projectile, add_damage;
 *     kit base principle 6). It goes when she switches to another weapon (her primary or secondary for gun_grace s;
 *     a stratagem or a grenade in her hands does not count, Yui), drops it, spends its rounds or presses Z again; her
 *     own weapon comes back and the cooldown (gun_cooldown) starts then.
 *     The Maxigun's copy comes with its backpack (0.1.1): the game's Maxigun has no magazine and draws its rounds from
 *     the Maxigun Backpack it is linked to (WeaponLinkedAmmo; HD2Runtime's catalogue: backpack-dependent), so a copy of
 *     the backpack, linked to her gun and holding gun_rounds, goes on her back first (the inventory's take, slot 6),
 *     and her own backpack comes back with her weapon, a new one of its kind, as her support weapon does.
 *     While she holds it she has a shield (kitbase/kb_allies.h, Alva's): shield_max, refilled to shield_max every
 *     shield_every s, never above it. Once the gun is gone the shield drains, shield_decay a second, until a refill.
 *   Lock & Load (key_s2, default X): the gun in her hands refilled in full (magazine and spares); on her own gun also
 *     +ll_ergonomics ergonomics and no recoil (ll_recoil 1) for the rest of that gun's time out: a new one of it,
 *     built with them, full, takes its place (weapon handling is read when a weapon is built).
 *   Ishbosheth (key_s3, default C; her EX, Denouement: Ishbosheth): readied by the key, set off by her next trigger
 *     pull (any gun): a barrage of ish_duration s (Blue Archive's EX, about 10 s): every ish_every s a volley across a
 *     fan of ish_cone degrees each side of her aim, ish_per_section tracers in each of its three sections, and every ish_tick s the enemies in it out to ish_range m take
 *     a tracer each, ish_mult times her gun's round over the whole barrage (Blue Archive: 636%). Its tracers are copies
 *     of the APW-1's round (Koleda's Full Speed tracers) dealing her gun's damage, slowed to ish_speed m/s. Cooldown
 *     ish_cooldown from its end.
 *   Lock & Load (passive, Blue Archive's Normal skill): a magazine she empties is reloaded at once (free: no spare
 *     used; not her own gun, whose rounds are its whole supply), and her shots deal reload_buff more for
 *     reload_seconds s.
 *   Prefect Team Member Judgment (passive): her own gun fires judgment_rate faster (on top of gun_rpm).
 *   Out-and-Out (passive, her Sub skill): her shots deal oao_bonus more while she is crouched or prone (her head lower
 *     than oao_head m over her feet; Blue Archive: against enemies not in cover).
 *
 * The passives' damage bonuses are her hits' own, through the damage event (as every kit's buffs); nothing of the game's
 * own weapons, rounds or damage types is changed (her gun, its rounds and Ishbosheth's are copies of her own).
 *
 * Research credit: the kit base's (Helldiver Survival Rework by enochlophobianumerophobia, MIT; HD2Runtime by
 * SkyeShade); the camera pose as Apache - Rise Above (RSNLRiggs, after Khi) reads it. See THIRD_PARTY_NOTICES.md. */
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
#include "hellforge_functions.h"
#include "hellforge_globals.h"

#define VERSION "0.1.4"
#define BUILD "25480438"

#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"
#include "kitbase/kb_allies.h"
#include "kitbase/kb_damage.h"
#include "kitbase/kb_hits.h"     /* 0.1.4: her gun's hits told as hers (the hit hooks: a gun's shots name the gun as their dealer) */
#include "kitbase/kb_wield.h"
#include "kitbase/kb_settings.h"
#include "kitbase/kb_options.h"
#include "kitbase/kb_roster.h"

KB_ROSTER_EXPORTS(hina)
KB_OPTIONS_EXPORTS(hina)

/* ---- settings ------------------------------------------------------------------------------------------------- */
static int s_gunBase = 1, s_gunRounds = 6000, s_gunAp = 5;
static int s_gunDrum = 0, s_drumErgonomics = 30;   /* 0.1.4 (Yui): her MG-43's belt, a 250-round box (the MG-43's own way) or a 50-round drum, +30 ergonomics */   /* 0.1.4 (Yui): the MG-43 by default; the Maxigun an option */
static int GunBelt(void) { return s_gunDrum ? 50 : 250; }
static float s_gunRpm = 1200,   /* 0.1.4 (Yui): her MG42's rate (Blue Archive's Destroyer of Denouement is one) */
    s_gunCooldown = 30, s_gunGrace = 0.5f;
static float s_shieldMax = 3000, s_shieldEvery = 300, s_shieldDecay = 100;
static float s_llCooldown = 20, s_llErgonomics = 80;
static int s_llRecoil = 1;
static float s_ishCooldown = 30, s_ishMult = 6.36f, s_ishCone = 30, s_ishRange = 40;
static int s_ishTargets = 12;
static int s_ishPerSection = 2;
static float s_ishDuration = 10, s_ishEvery = 0.1f, s_ishTick = 0.5f, s_ishSpeed = 200;   /* 0.1.4: the barrage */
static int s_reloadFree = 1;
static float s_reloadBuff = 0.21f, s_reloadSeconds = 16, s_judgmentRate = 0.14f, s_oaoBonus = 0.027f, s_oaoHead = 1.2f;
static int s_optionsKey = 0x78;        /* options_key: the virtual key that opens the options screen (120: F9; 0: none) */

static const KbSetting kSettings[] = {
    KB_I("gun_base", s_gunBase, 0, 1), KB_I("gun_drum", s_gunDrum, 0, 1), KB_I("drum_ergonomics", s_drumErgonomics, 0, 200), KB_I("gun_rounds", s_gunRounds, 1, 100000), KB_F("gun_rpm", s_gunRpm, 60, 12000), KB_I("gun_ap", s_gunAp, 0, 10),
    KB_F("gun_cooldown", s_gunCooldown, 0, 600), KB_F("gun_grace", s_gunGrace, 0, 10),
    KB_F("shield_max", s_shieldMax, 0, 100000), KB_F("shield_every", s_shieldEvery, 1, 3600), KB_F("shield_decay", s_shieldDecay, 0, 100000),
    KB_F("ll_cooldown", s_llCooldown, 0, 600), KB_F("ll_ergonomics", s_llErgonomics, 0, 200), KB_I("ll_recoil", s_llRecoil, 0, 1),
    KB_F("ish_cooldown", s_ishCooldown, 0, 600), KB_F("ish_mult", s_ishMult, 0.1, 50), KB_F("ish_cone", s_ishCone, 1, 90), KB_F("ish_range", s_ishRange, 5, 300),
    KB_I("ish_targets", s_ishTargets, 1, 32), KB_F("ish_duration", s_ishDuration, 1, 60), KB_F("ish_every", s_ishEvery, 0.02, 2), KB_I("ish_per_section", s_ishPerSection, 1, 8),
    KB_F("ish_tick", s_ishTick, 0.1, 10), KB_F("ish_speed", s_ishSpeed, 0, 3000),
    KB_I("reload_free", s_reloadFree, 0, 1), KB_F("reload_buff", s_reloadBuff, 0, 5), KB_F("reload_seconds", s_reloadSeconds, 0, 120),
    KB_F("judgment_rate", s_judgmentRate, 0, 3), KB_F("oao_bonus", s_oaoBonus, 0, 5), KB_F("oao_head", s_oaoHead, 0.2, 2),
    KB_I("options_key", s_optionsKey, 0, 255),
};

static __attribute__((unused)) void DefaultIni(FILE* f)
{
    fprintf(f, "; Hina %s. Keys: a letter, digit, F1-F24, Mouse3, Mouse4 or Mouse5, with Shift+, Ctrl+ or Alt+ in front for a\n"
               "; combo (Shift+Q). The game still sees the keys too (Shift sprints, R reloads), so pick ones it leaves alone.\n"
               "key_s1 = %s\nkey_s2 = %s\nkey_s3 = %s\n"
               "; Destroyer of Denouement: her gun in her hands, a copy of the MG-43 Machine Gun (gun_base 1) or of the M-1000\n"
               "; Maxigun (gun_base 0, when this profile owns it), with gun_rounds rounds, gun_rpm rounds a minute (her MG42's 1200)\n"
               "; and armour penetration gun_ap (5: tank). It goes when she is on her primary or secondary for gun_grace s, drops\n"
               "; it or spends it; the cooldown starts then. Takes effect at the next start. The MG-43's copy reloads from them a belt\n"
               "; at a time: gun_drum 0 a 250-round box, 1 a 50-round drum with drum_ergonomics more ergonomics.\n"
               "gun_base = %d\ngun_drum = %d\ndrum_ergonomics = %d\ngun_rounds = %d\ngun_rpm = %g\ngun_ap = %d\ngun_cooldown = %g\ngun_grace = %g\n"
               "; Her shield while she holds it: shield_max, refilled to shield_max every shield_every s; once the gun is gone it\n"
               "; drains shield_decay a second until the next refill.\n"
               "shield_max = %g\nshield_every = %g\nshield_decay = %g\n"
               "; Lock & Load: cooldown; the gun in her hands refilled; her own gun also +ll_ergonomics and no recoil (ll_recoil 1).\n"
               "ll_cooldown = %g\nll_ergonomics = %g\nll_recoil = %d\n"
               "; Ishbosheth: cooldown (from its end); readied by its key, set off by her next shot: for ish_duration s, every\n"
               "; ish_every s a volley across ish_cone degrees each side of her aim, ish_per_section tracers in each of its three\n"
               "; sections (two outer, one inner), at ish_speed m/s (0: the round's own; at most 80 tracers a second in all),\n"
               "; and every ish_tick s the ish_targets nearest enemies there out to ish_range m take a tracer each: ish_mult times\n"
               "; her gun's damage over the whole barrage. It fires only while she holds the trigger. (ish_mult, ish_duration and\n"
               "; ish_tick take effect at the next start.)\n"
               "ish_cooldown = %g\nish_mult = %g\nish_cone = %g\nish_range = %g\nish_targets = %d\nish_duration = %g\nish_every = %g\nish_per_section = %d\nish_tick = %g\nish_speed = %g\n"
               "; Passives. Lock & Load: an emptied magazine reloads at once (reload_free 1; not her own gun) and her shots deal\n"
               "; reload_buff more for reload_seconds s. Prefect Team Member Judgment: her own gun fires judgment_rate faster.\n"
               "; Out-and-Out: her shots deal oao_bonus more while her head is lower than oao_head m over her feet (crouched, prone).\n"
               "reload_free = %d\nreload_buff = %g\nreload_seconds = %g\njudgment_rate = %g\noao_bonus = %g\noao_head = %g\n"
               "; The options screen's key (120: F9; 0: none).\noptions_key = %d\n",
            VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, s_gunBase, s_gunDrum, s_drumErgonomics, s_gunRounds, (double)s_gunRpm, s_gunAp, (double)s_gunCooldown,
            (double)s_gunGrace, (double)s_shieldMax, (double)s_shieldEvery, (double)s_shieldDecay, (double)s_llCooldown, (double)s_llErgonomics,
            s_llRecoil, (double)s_ishCooldown, (double)s_ishMult, (double)s_ishCone, (double)s_ishRange, s_ishTargets, (double)s_ishDuration, (double)s_ishEvery, s_ishPerSection, (double)s_ishTick,
            (double)s_ishSpeed, s_reloadFree,
            (double)s_reloadBuff, (double)s_reloadSeconds, (double)s_judgmentRate, (double)s_oaoBonus, (double)s_oaoHead, s_optionsKey);
}

/* ---- her gun, its rounds, Ishbosheth's rounds: copies of her own (index 0 the Maxigun's, 1 the MG-43's) --------- */
static const uint64_t kBaseEntity[2] = {HF_ENTITY_M1000_MAXIGUN, HF_ENTITY_MG43_MACHINE_GUN};
static const uint32_t kBaseRound[2] = {HF_PROJECTILE_M1000_MAXIGUN, HF_PROJECTILE_MG43_MACHINE_GUN};
/* 0.1.4: the round the MG-43 really fires: its record names 148 (0x94; played), not HellForge's 49. Her MG-43 copy's
 * round is a copy of it */
static const uint32_t kFiredRound[2] = {HF_PROJECTILE_M1000_MAXIGUN, 148u};
static const uint32_t kBaseDamage[2] = {HF_DAMAGE_M1000_MAXIGUN, HF_DAMAGE_EXO45_PATRIOT_EXOSUIT_RIGHT_GUN_AND_1_OTHER};
static const char* const kBaseName[2] = {"M-1000 Maxigun", "MG-43 Machine Gun"};
static uint32_t g_round[2], g_damage[2], g_ishRound[2], g_ishDamage[2];
#define TRACER_ROUND 226u                /* the APW-1 Anti-Materiel Rifle's round: a bright trail (Koleda's tracers) */
static uint32_t g_tracer[2][2];         /* Ishbosheth's tracers by gun copy: [0] on targets (x ish_mult), [1] the sweep */
static uint64_t g_gunRes[2];
static uint64_t g_bpRes;               /* her gun's backpack: a copy of the Maxigun Backpack, linked to the Maxigun's copy */
static int g_rowsReady, g_entsReady;
static int g_owned = -1;               /* the Maxigun: -1 not asked yet, 0 not owned (or unknown), 1 owned */
static int g_use;                      /* which copy this session uses */
static int g_tuned;                    /* Lock & Load's handling is in her gun's records (the next one built has it) */

#define MAXIGUN_MIX 0xDA20CFA7u   /* the M-1000 Maxigun's mixId (its itemId is HF_ITEM_M1000_MAXIGUN, cdfbf3c2) */
static void Gun(void)
{
    /* 0.1.4: asked again each second until it answers (played: at the mission's start HellForge said "not the reviewed
     * one (or not decoded yet)" and the kit's own pin did not match either: the game's code there may not be decoded
     * yet, so the answer is not settled at the first try) */
    static double nextAsk;
    static unsigned tries;
    if (g_owned < 0 && g_now >= nextAsk)
    {
        nextAsk = g_now + 1;
        tries++;
        HfFn_HasCatalogueItem has = hf_fn_HasCatalogueItem(self);
        void* progression = hf_global_Progression(self);
        int own = 0;
        /* the kit's own pin, against the game build's code as read from the dump (a loop over the account's items). Read
         * and compared by the kit (played: the code there read the same, yet code_matches said no; HellForge's own
         * check refuses it the same way: its pin is not this build's) */
        static const uint8_t kHas[36] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x85, 0xd2, 0x74, 0x26, 0x44, 0x8b, 0x81, 0xe0, 0x1c,
                                         0x00, 0x00, 0x33, 0xc0, 0x45, 0x85, 0xc0, 0x74, 0x18, 0x4c, 0x8d, 0x89, 0xe4, 0x9c, 0x0b, 0x00, 0x41, 0x39};
        int differs = -1;
        if (!has && game)
        {
            uint8_t live[36];
            if (ReadAt(game + 0x136FB40, live, sizeof live))
            {
                differs = 0;
                /* the two field offsets (bytes 16-19: the account's item count, 30-33: its items) may differ from the dump's
                 * (played: +0x1CF0 live where the dump has +0x1CE0): the code's shape is what is compared */
                for (int k = 0; k < 36 && !differs; k++)
                    if (!(k >= 16 && k <= 19) && !(k >= 30 && k <= 33) && live[k] != kHas[k]) differs = k + 1;
                if (!differs && tries == 1)
                    Log("HasCatalogueItem: its code has the dump's shape (its fields at +0x%02x%02x and +0x%02x%02x%02x)", live[17], live[16], live[32], live[31], live[30]);
                if (!differs) { has = (HfFn_HasCatalogueItem)(game + 0x136FB40); own = 1; }
                else if (tries == 1 || tries == 30) Log("HasCatalogueItem: the code differs from the dump's at byte %d (%02x, the dump %02x)", differs - 1, live[differs - 1], kHas[differs - 1]);
            }
        }
        if (has && progression)
        {
            /* 0.1.4: the function compares a record's +4, the item's mixId, not its itemId (the progression table: +4 mixId,
             * +8 itemId). The two are the same for an armory weapon (the Liberator) but not for a stratagem's weapon: the
             * Maxigun's itemId cdfbf3c2, its mixId da20cfa7 (the game's item catalogue). Played: asked by itemId, "not
             * owned", though Yui can bring it. Its live code reads HellForge's moved catalogue (state_moves.c): hence
             * offsets unlike the dump's */
            g_owned = has(progression, MAXIGUN_MIX) ? 1 : 0;
            Log("HasCatalogueItem by mixId: M-1000 Maxigun %d; for comparison AR-23 Liberator %d, MG-43 %d, M-105 Stalwart %d (by itemId the Maxigun reads %d)",
                g_owned, has(progression, 0xAA035309u) ? 1 : 0, has(progression, 0x37B6732Cu) ? 1 : 0, has(progression, 0x75E7ABE4u) ? 1 : 0,
                has(progression, HF_ITEM_M1000_MAXIGUN) ? 1 : 0);
            Log("the M-1000 Maxigun: %s (asked %u time(s)%s)", g_owned ? "owned: her gun is its copy" : "not owned: her gun is the MG-43's copy", tries,
                own ? "; through the kit's own pin" : "");
        }
        else if (tries == 1 || tries == 30)
        {
            uint8_t b[16] = {0};
            if (game) ReadAt(game + 0x136FB40, b, sizeof b);
            Log("the M-1000 Maxigun: its ownership can't be read yet (HellForge's function %s, the progression %s; the code there now: "
                "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x): the MG-43's copy until it can (asked each second)",
                hf_fn_HasCatalogueItem(self) ? "given" : "refused", progression ? "found" : "not found", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9],
                b[10], b[11], b[12], b[13], b[14], b[15]);
        }
    }
    g_use = s_gunBase == 0 && g_owned == 1 && g_gunRes[0] ? 0 : 1;
}
/* the copies' rows, once the game has its projectiles: armour penetration (the heavy machine gun's pattern: gun_ap at
 * every angle but the most glancing, two less there) */
static void RowsReady(HfMod* mod, HfEvent event, void* user)
{
    for (int i = 0; i < 2; i++)
    {
        uint32_t ids[2] = {g_damage[i], g_ishDamage[i]};
        for (int k = 0; k < 2; k++)
        {
            HfDamageInfo* row = ids[k] && hf->damage_row ? (HfDamageInfo*)hf->damage_row(ids[k]) : NULL;
            if (!row) { Log("damage %u (%s, %s): its row is not there", ids[k], kBaseName[i], k ? "Ishbosheth" : "her gun"); continue; }
            uint32_t ap = (uint32_t)s_gunAp, before = row->armor_penetration[0];
            for (int a = 0; a < 4; a++) row->armor_penetration[a] = a < 3 ? ap : (ap > 2 ? ap - 2 : ap);
            Log("damage %u (%s, %s): %d / %d, armour penetration %u -> %u", ids[k], kBaseName[i], k ? "Ishbosheth" : "her gun", row->standard_damage,
                row->durable_damage, before, ap);
        }
    }
    /* 0.1.4: the tracers slowed so their trails are seen (Yui; Koleda's round_speed: at ~1000 m/s only the hits show) */
    for (int i = 0; i < 2 && s_ishSpeed > 0; i++)
        for (int t = 0; t < 2; t++)
        {
            if (!g_tracer[i][t]) continue;
            HfField f = HF_FIELD(ProjectileSettings, velocity);
            HfValue v = HF_F32(s_ishSpeed);
            HfLiveValue was;
            memset(&was, 0, sizeof was);
            hf->get_live(self, (HfRef)g_tracer[i][t], &f, &was);
            int ok = hf->set_live(self, (HfRef)g_tracer[i][t], &f, &v, 0) != 0;
            Log("Ishbosheth's tracer %u: %.0f m/s -> %.0f (%s)", g_tracer[i][t], was.base.number, (double)s_ishSpeed, ok ? "set" : "refused");
        }
    g_rowsReady = 1;
}
/* her guns' records, once the entities are in: fire rate, rounds, and the round in the magazine's pattern */
static void GunRecords(int i)
{
    uint64_t res = g_gunRes[i];
    if (!res) return;
    int rate = 0, mag = 0, pat = 0;
    float rpm = s_gunRpm * (1.0f + s_judgmentRate);
    for (int k = 0; k < 3; k++)
    {
        HfField f = HF_FIELD_AT(ProjectileWeapon, fire_rate, k);
        HfValue v = HF_F32(rpm);
        rate += hf->set_live(self, res, &f, &v, 0) != 0;
    }
    int proj = 0;
    if (g_round[i])
    {
        HfField pf = HF_FIELD(ProjectileWeapon, projectile);
        HfValue pv = HF_U32(g_round[i]);
        proj = hf->set_live(self, res, &pf, &pv, 0) != 0;
        /* 0.1.2: refused for the MG-43's copy (played: "projectile 370 is not one HellForge's catalogue knows the packages
         * of"), so it fired the game's own round. Her round is a copy of the gun's own, with its look: the gun's package,
         * which the kit loads before the gun is made and at the mission's start, holds everything it needs. Written into
         * her gun's own record (own_all: it is no other entity's) */
        /* 0.1.4: the round is not at the record's start (played: nothing was written). set_live put it just before
         * fire_rate[0] (the Maxigun's copy: projectile at ...550, fire_rate[0] at ...554): found as the base round's id
         * followed by the fire rate just set */
        uint32_t stride = 0, w[2] = {0};
        uintptr_t r = proj ? 0 : hf->component_record("ProjectileWeaponComponentData", res, &stride);
        float want = rpm;
        uint32_t wantBits;
        memcpy(&wantBits, &want, 4);
        if (!proj)
        {
            int at = -1;
            for (int o = 0; r && o + 8 <= 0x200; o += 4)
                if (ReadAt(r + o, w, 8) && (w[0] == kBaseRound[i] || w[0] == kFiredRound[i] || w[0] == g_round[i]) && w[1] == wantBits) { at = o; break; }
            if (at >= 0 && w[0] == g_round[i]) proj = 2;   /* (written before: the options screen's change) */
            else if (at >= 0&& !hf->write(r + at, &g_round[i], 4)) { proj = 2; Log("her gun (%s's copy): its round %u written into its own record (+0x%X)", kBaseName[i], g_round[i], at); }
            else
            {
                uint32_t h[4] = {0};
                if (r) ReadAt(r, h, sizeof h);
                Log("her gun (%s's copy): its round could not be written (record %s, the base round %s; its first words %08x %08x %08x %08x, wanted %u then %08x)",
                    kBaseName[i], r ? "found" : "not found", at >= 0 ? "found, the write refused" : "not found before fire_rate", h[0], h[1], h[2], h[3], kBaseRound[i], wantBits);
            }
        }
    }
    HfField cap = HF_FIELD(WeaponMagazine, capacity);
    HfValue vc = HF_U32(GunBelt());   /* 0.1.4: a belt (250 or 50), its rounds the kit's count (gun_rounds) */
    mag = hf->set_live(self, res, &cap, &vc, 0) != 0;
    for (int k = 0; k < 32 && g_round[i]; k++)
    {
        HfField f = HF_FIELD_AT(WeaponMagazine, pattern, k);
        HfLiveValue cur;
        memset(&cur, 0, sizeof cur);
        if (!hf->get_live(self, res, &f, &cur) || ((uint32_t)cur.now.number != kBaseRound[i] && (uint32_t)cur.now.number != kFiredRound[i])) continue;
        HfValue v = HF_U32(g_round[i]);
        pat += hf->set_live(self, res, &f, &v, 0) != 0;
    }
    HfField fp = HF_FIELD(WeaponMagazine, first_projectile);
    HfLiveValue cf;
    memset(&cf, 0, sizeof cf);
    if (g_round[i] && hf->get_live(self, res, &fp, &cf) && ((uint32_t)cf.now.number == kBaseRound[i] || (uint32_t)cf.now.number == kFiredRound[i])) { HfValue v = HF_U32(g_round[i]); hf->set_live(self, res, &fp, &v, 0); }
    Log("her gun (%s's copy, entity %016llx): fire rate %.0f in %d of 3 slots, %d rounds %s, its round %u %s (and in %d pattern slots)", kBaseName[i],
        (unsigned long long)res, (double)rpm, rate, s_gunRounds, mag ? "set" : "REFUSED", g_round[i], proj == 2 ? "written" : proj ? "set" : "REFUSED", pat);
}
/* Lock & Load's handling on her gun's records (on: +ll_ergonomics, no recoil; off: back to its own) */
static double DrumErg(void) { return g_use == 1 && s_gunDrum ? s_drumErgonomics : 0; }   /* (the MG-43's copy only) */
/* the drum's ergonomics on her MG-43 copy (from the records' readiness, and again when Lock & Load's handling goes) */
static void DrumHandling(void)
{
    HfField erg = HF_FIELD(WeaponData, ergonomics);
    uint64_t res = g_gunRes[1];
    if (!res) return;
    if (!s_gunDrum || !s_drumErgonomics) { hf->set_live(self, res, &erg, NULL, 0); return; }
    HfLiveValue e;
    memset(&e, 0, sizeof e);
    double base = hf->get_live(self, res, &erg, &e) ? e.base.number : 0;
    HfValue v = HF_F32(base + s_drumErgonomics);
    int ok = hf->set_live(self, res, &erg, &v, 0) != 0;
    Log("her MG-43's drum: ergonomics %.0f -> %.0f (%s)", base, base + s_drumErgonomics, ok ? "set" : "refused");
}
static void Handling(int on)
{
    uint64_t res = g_gunRes[g_use];
    if (!res || on == g_tuned) return;
    HfField erg = HF_FIELD(WeaponData, ergonomics);
    HfField recoil[6] = {HF_FIELD(WeaponData, recoil_drift_horizontal), HF_FIELD(WeaponData, recoil_drift_vertical), HF_FIELD(WeaponData, recoil_climb_horizontal),
                         HF_FIELD(WeaponData, recoil_climb_vertical), HF_FIELD(WeaponData, recoil_multiplier_horizontal), HF_FIELD(WeaponData, recoil_multiplier_vertical)};
    int ok = 0;
    if (on)
    {
        HfLiveValue e;
        memset(&e, 0, sizeof e);
        double base = hf->get_live(self, res, &erg, &e) ? e.base.number : 0;
        HfValue v = HF_F32(base + DrumErg() + s_llErgonomics);
        ok += hf->set_live(self, res, &erg, &v, 0) != 0;
        if (s_llRecoil) for (int k = 0; k < 6; k++) { HfValue z = HF_F32(0); ok += hf->set_live(self, res, &recoil[k], &z, 0) != 0; }
        Log("Lock & Load: her gun's handling: ergonomics %.0f -> %.0f%s (%d fields set)", base + DrumErg(), base + DrumErg() + s_llErgonomics, s_llRecoil ? ", no recoil" : "", ok);
    }
    else
    {
        hf->set_live(self, res, &erg, NULL, 0);
        for (int k = 0; k < 6; k++) hf->set_live(self, res, &recoil[k], NULL, 0);
        if (g_use == 1) DrumHandling();
        Log("her gun's handling back to its own");
    }
    g_tuned = on;
}
/* her gun and its backpack linked to each other: wherever the gun's linked-ammo record names the Maxigun Backpack, it
 * names hers; where her backpack names the Maxigun as the weapon it feeds, it names her gun (in case the game pairs
 * them by their kinds). Its rounds: gun_rounds at its call-in. */
static void Link(void)
{
    uint32_t stride = 0;
    if (!g_gunRes[0] || !g_bpRes) { Log("her gun's backpack: %s", g_bpRes ? "no Maxigun copy" : "not added"); return; }
    uintptr_t rec = hf->component_record("WeaponLinkedAmmoComponentData", g_gunRes[0], &stride);
    int named = 0;
    if (rec && stride >= 8 && stride <= 4096)
    {
        uint8_t buf[4096];
        if (ReadAt(rec, buf, stride))
        {
            Log("her gun's linked-ammo record (%u bytes): %08x %08x %08x %08x %08x %08x %08x %08x", stride, Word(buf), Word(buf + 4), Word(buf + 8),
                Word(buf + 12), Word(buf + 16), Word(buf + 20), Word(buf + 24), Word(buf + 28));
            for (uint32_t o = 0; o + 8 <= stride; o += 4)
                if (Quad(buf + o) == HF_ENTITY_MAXIGUN_BACKPACK && !hf->write(rec + o, &g_bpRes, 8)) { named++; Log("  +0x%X named the Maxigun Backpack: now hers", o); }
        }
    }
    else Log("her gun's linked-ammo record: not its own (%s)", rec ? "an odd size" : "none");
    HfField feed = HF_FIELD(Deposit, assisted_reload_weapon);
    HfLiveValue f;
    memset(&f, 0, sizeof f);
    int fed = 0;
    if (hf->get_live(self, g_bpRes, &feed, &f) && f.now.u64 == HF_ENTITY_M1000_MAXIGUN)
    {
        HfValue v = HF_U64(g_gunRes[0]);
        fed = hf->set_live(self, g_bpRes, &feed, &v, 0) != 0;
    }
    uintptr_t dep = hf->component_record("DepositComponentData", g_bpRes, &stride);
    uint64_t now = 0;
    if (dep && ReadAt(dep + 0x88, &now, 8) && now == HF_ENTITY_M1000_MAXIGUN && !hf->write(dep + 0x88, &g_gunRes[0], 8)) fed = 1;
    HfField cap = HF_FIELD(Deposit, capacity), start = HF_FIELD(Deposit, start_amount);
    HfValue vc = HF_U32(s_gunRounds), vs = HF_I32(s_gunRounds);
    int a = hf->set_live(self, g_bpRes, &cap, &vc, 0) != 0, b = hf->set_live(self, g_bpRes, &start, &vs, 0) != 0;
    Log("her gun's backpack (entity %016llx): the gun's record names it in %d place(s); it feeds her gun %s; %d rounds %s / %s", (unsigned long long)g_bpRes,
        named, fed ? "(set)" : "(it named no Maxigun: left)", s_gunRounds, a ? "capacity set" : "capacity REFUSED", b ? "at call-in" : "call-in REFUSED");
}
static void EntitiesReady(HfMod* mod, HfEvent event, void* user)
{
    for (int i = 0; i < 2; i++) GunRecords(i);
    DrumHandling();
    Link();
    g_entsReady = 1;
}

/* ---- reads: health, positions, her view -------------------------------------------------------------------------- */
static int HealthOf(uint32_t entity, int32_t* health)
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
        uint8_t r[24];
        if (!ReadAt(records + 440u * index, r, sizeof r)) return 0;
        *health = (int32_t)Word(r + 20);
        return 1;
    }
    return 0;
}
static int Alive(uint32_t e) { int32_t hp; return HealthOf(e, &hp) && hp > 0; }
static int TargetPos(uint32_t e, uint64_t res, float out[3])
{
    uint8_t d[24];
    if (!Descriptor(e, d) || (res && Quad(d) != res) || !UnitNode(Word(d + 12), 0, out)) return 0;
    out[2] += 1.0f;
    return 1;
}
/* her camera: where it is and where it looks */
static int View(float pos[3], float fwd[3])
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
static int HerHead(float out[3])
{
    uint8_t d[24];
    return Descriptor(atomic_load(&g_avatar), d) && UnitNode(Word(d + 12), NODE_HEAD, out);
}
/* Out-and-Out: her head this low over her feet (crouched, prone); refreshed four times a second */
static int g_low;
static double g_lowAt = -100;
static float g_headHeight;
static void StanceFrame(void)
{
    float feet[3], head[3];
    if (g_now - g_lowAt < 0.25) return;
    g_lowAt = g_now;
    if (!PlayerPose(feet, NULL) || !HerHead(head)) { g_low = 0; return; }
    g_headHeight = head[2] - feet[2];
    int low = g_headHeight < s_oaoHead;
    static unsigned told;
    if (low != g_low && told++ < 12) Log("Out-and-Out: her head %.2f m over her feet: %s", (double)g_headHeight, low ? "low (crouched or prone): +bonus" : "standing");
    g_low = low;
}

/* ---- the weapon swap (W's King of Hearts, from the Wield Probe) ------------------------------------------------- */
/* (the swap primitives: kitbase/kb_wield.h, from W's King of Hearts and this kit) */
static uint64_t g_package[2];             /* the gun's base's loadout package (~0: none) */
static int g_pkg[2];

enum { K_IDLE, K_PACKAGE, K_WAITGL, K_PUTAWAY, K_TAKE, K_ASK, K_HELD, K_LETGO, K_REMOVE, K_WAITS, K_DROPOLD, K_RETAKE, K_HANDDROP, K_HANDGONE };
/* 0.1.4: a SEAF trooper as a hand for the drop (the Escort Probe 0.5.0, played: a trooper has an inventory record like
 * a Helldiver's; the inventory's take put an MG-43 in its support slot, its drop left a pickup, and the trooper was
 * taken away). Her own slots are not touched, so a weapon she picked up stays in her hands */
#define SEAF_TROOPER UINT64_C(0x75F7E96AF7DCD303)
static uint32_t g_hand;
static double g_handUntil;
static int HandPackage(void)
{
    static uint64_t pkg;
    if (!pkg && !LoadoutPackage(SEAF_TROOPER, &pkg)) pkg = ~0ull;
    return pkg == ~0ull ? HF_PACKAGE_RESIDENT : hf->load_package(self, pkg);
}
static int g_k;
static atomic_uint g_gl;                  /* her gun (0: none) */
static double g_kAt, g_madeAt, g_readyAt, g_awaySince, g_droppedSince;
static double g_putAt;
static unsigned g_frame, g_madeFrame;   /* 0.1.4: frames, for spawn waits counted in frames, not seconds */
static double g_emptySince, g_roundsLogAt;   /* 0.1.4: her rounds at 0 for a second before the gun goes (a reload reads 0) */
static uint32_t g_picked;                 /* 0.1.4: the weapon she picked up with the gun out (it ends the gun; she keeps it) */
static int g_dropped;                     /* 0.1.4: her gun dropped (or left for a pickup): her old support weapon dropped too */
static uint32_t g_kSel, g_kOld, g_kNew, g_swapOld;   /* her selection at the start; the support weapon she had; its replacement; the gun a swap replaces */
static uint64_t g_kOldRes;
static atomic_uint g_bp;                  /* her gun's backpack (the Maxigun's copy only; 0: none) */
static uint32_t g_bpOld, g_bpNew;         /* the backpack she wore; its replacement */
static uint64_t g_bpOldRes;
static double g_bpOffSince;
static int g_swap;                        /* Lock & Load is putting a new gun (with its handling) in place of this one */
static unsigned g_uses, g_swaps;
static uint64_t GunRes(void) { return g_gunRes[g_use]; }
/* the gun's package (its base's loadout package: her gun's model and her rounds' look): HF_PACKAGE_RESIDENT, LOADING,
 * or 0 when there is none to load (then the gun and its rounds go ahead and the game's own copy decides) */
static int GunPackage(void)
{
    if (!g_package[g_use])
    {
        if (!LoadoutPackage(kBaseEntity[g_use], &g_package[g_use])) g_package[g_use] = ~0ull;
    }
    if (g_package[g_use] == ~0ull) return 0;
    if (g_pkg[g_use] != HF_PACKAGE_RESIDENT)
    {
        g_pkg[g_use] = hf->load_package(self, g_package[g_use]);
        if (!g_pkg[g_use]) { Log("the %s's package %016llx could not be held: her gun and rounds may not show", kBaseName[g_use], (unsigned long long)g_package[g_use]); g_package[g_use] = ~0ull; }
        else if (g_pkg[g_use] == HF_PACKAGE_RESIDENT) Log("the %s's package %016llx is loaded", kBaseName[g_use], (unsigned long long)g_package[g_use]);
    }
    return g_pkg[g_use];
}
static int Pack(void) { return g_use == 0 && g_bpRes; }   /* her gun comes with its backpack */
/* her gun's rounds: the backpack's for the Maxigun's copy, the magazine's for the MG-43's */
/* 0.1.4: the MG-43 copy's rounds are the kit's own count (played: its magazine never held more than 2047, the
 * magazine's network field, though 6000 were added). The magazine is kept topped up from the count (under 1024 it gets
 * up to 2047 more); what it loses a frame is spent (a drop of over 64 at once is a reload, not shots). The Maxigun's copy
 * reads its backpack (the editor widens deposit_value to 20 bits). */
#define MAG_NET_MAX 2047
static int g_pool = -1, g_lastMag = -1;
static int GunRounds(uint32_t gl) { return Pack() ? DepositAmount(atomic_load(&g_bp)) : g_pool >= 0 ? g_pool : MagRounds(gl); }
/* 0.1.4 (Yui): a belt at a time (250 or 50), reloaded the game's way; the spares kept stocked while the count lasts */
static void PoolTop(uint32_t gl)
{
    int m = MagRounds(gl);
    if (m < 0) return;
    g_lastMag = m;
}
static void PoolSpares(uint32_t gl)
{
    static double at;
    HfFn_WeaponRefill refill = hf_fn_WeaponRefill(self);
    if (g_now < at || !refill || g_pool <= 0) return;
    at = g_now + 2;
    refill(0, gl, 1.0f);
}
static void PoolFrame(uint32_t gl)
{
    if (Pack() || g_pool < 0) return;
    int m = MagRounds(gl);
    if (m < 0) return;
    if (g_lastMag >= 0 && m < g_lastMag && g_lastMag - m <= 64) { g_pool -= g_lastMag - m; if (g_pool < 0) g_pool = 0; }
    g_lastMag = m;
    PoolSpares(gl);
}
static void GunFill(uint32_t gl)
{
    if (Pack()) { DepositSet(atomic_load(&g_bp), s_gunRounds); return; }
    g_pool = s_gunRounds;
    g_lastMag = -1;
    {
        int have = MagRounds(gl), cap = MagCapacity(gl), want = cap > 0 ? cap : GunBelt();
        if (want > g_pool) want = g_pool;
        if (have >= 0 && have < want) MagAdd(gl, want - have);
    }
    PoolTop(gl);

}
static void GunStart(void)
{
    uint32_t me = atomic_load(&g_avatar), slots[8] = {0};
    uintptr_t rec;
    if (!WieldReady()) { Log("Destroyer of Denouement: off (the game's code is not the reviewed one)"); return; }
    Gun();
    if (!GunRes()) { Log("Destroyer of Denouement: her gun was not added (the log's start says why)"); return; }
    if (!(rec = InventoryRecord(me)) || !ReadAt(rec, slots, sizeof slots) || !SwitchObject(me)) { Log("Destroyer of Denouement: her inventory or her switch object was not found: nothing done"); return; }
    Handling(0);   /* a new call: her gun as it is (Lock & Load's handling is for the gun it was used on) */
    g_kSel = slots[7] >= 1 && slots[7] <= 3 ? slots[7] : 1;
    g_kOld = slots[2];
    g_kOldRes = ResOf(g_kOld);
    g_kNew = 0;
    g_bpOld = Pack() ? slots[3] : 0;
    g_bpOldRes = ResOf(g_bpOld);
    g_bpNew = 0;
    g_swap = 0;
    g_uses++;
    Log("Destroyer of Denouement %u: the %s's copy%s; her selection %u, her support weapon %u (%016llx), her backpack %u (%016llx)", g_uses, kBaseName[g_use],
        Pack() ? " with its backpack" : "", g_kSel, g_kOld, (unsigned long long)g_kOldRes, g_bpOld, (unsigned long long)g_bpOldRes);
    g_k = K_PACKAGE;
    g_kAt = g_now + 8;
}
static void GunEnd(const char* why)
{
    if (g_k != K_HELD) return;
    Log("Destroyer of Denouement %u over (%s): %d rounds left", g_uses, why, GunRounds(atomic_load(&g_gl)));
    WieldAsk(g_kSel == 3 ? 1 : g_kSel, "her own weapon back in hand");
    g_k = K_LETGO;
    g_kAt = g_now + 1.0;
    g_readyAt = g_now + s_gunCooldown;
}
/* she died or the mission ended with the gun out: nothing of hers is left to put back */
static void GunAbort(const char* why)
{
    HfFn_RemoveEntity remove = hf_fn_RemoveEntity(self);
    void* em = hf_global_EntityManager(self);
    uint32_t gl = atomic_load(&g_gl);
    if (g_k == K_IDLE) return;
    Log("Destroyer of Denouement %u cut short (%s)", g_uses, why);
    if (g_inMission && gl && ResOf(gl) == GunRes() && remove && em) remove(em, gl);
    if (g_inMission && g_swapOld && ResOf(g_swapOld) == GunRes() && remove && em) remove(em, g_swapOld);
    uint32_t bp = atomic_load(&g_bp);
    if (g_inMission && bp && ResOf(bp) == g_bpRes && remove && em) remove(em, bp);
    atomic_store(&g_bp, 0);
    atomic_store(&g_gl, 0);
    g_swapOld = 0;
    g_swap = 0;
    g_k = K_IDLE;
    g_readyAt = g_now + s_gunCooldown;
}
/* Lock & Load on her own gun: a new one built with the handling, full, in its place */
static void GunSwap(void)
{
    if (g_k != K_HELD) return;
    Handling(1);
    g_swapOld = atomic_load(&g_gl);
    g_swap = 1;
    g_swaps++;
    Log("Lock & Load: a new gun with the handling in place of %u", g_swapOld);
    g_k = K_PACKAGE;
    g_kAt = g_now + 8;
}
static void GunFrame(void)
{
    void* im = hf_global_InventoryManager(self);
    void* em = hf_global_EntityManager(self);
    HfFn_RemoveEntity remove = hf_fn_RemoveEntity(self);
    uint32_t me = atomic_load(&g_avatar), gl = atomic_load(&g_gl);
    uint64_t res = GunRes();
    uintptr_t rec;
    if (g_k == K_IDLE) return;
    if (!g_alive || !im || !ResOf(me) || !(rec = InventoryRecord(me))) { GunAbort("she is down or gone"); return; }
    switch (g_k)
    {
    case K_PACKAGE:
        if (GunPackage() == HF_PACKAGE_LOADING) { if (g_now > g_kAt) GunAbort("its package did not load"); break; }
        if (Pack() && !g_swap)
        {
            static uint64_t bpPkg;
            static int bpState;
            if (!bpPkg)
            {
                if (!LoadoutPackage(HF_ENTITY_MAXIGUN_BACKPACK, &bpPkg)) bpPkg = ~0ull;
            }
            if (bpPkg != ~0ull && bpState != HF_PACKAGE_RESIDENT)
            {
                bpState = hf->load_package(self, bpPkg);
                if (bpState == HF_PACKAGE_LOADING) { if (g_now > g_kAt) GunAbort("its backpack's package did not load"); break; }
                if (!bpState) bpPkg = ~0ull;
            }
            uint32_t bp = SpawnNear(g_bpRes, "her gun's backpack");
            if (!bp) { GunAbort("no backpack"); break; }
            atomic_store(&g_bp, bp);
        }
        gl = SpawnNear(res, "her gun");
        if (!gl) { GunAbort("no gun"); break; }
        atomic_store(&g_gl, gl);
        g_madeAt = g_now;
        g_madeFrame = g_frame;
        g_k = K_WAITGL;
        break;
    case K_WAITGL:
        /* its spawn has to finish before it is taken (taken in its spawn frame it stayed where it was made: W's Wield
         * Probe 0.5.1). 0.1.4 (Yui: the delay shows): two frames and readable, not a flat 0.75 s (the Escort Probe 0.9.0:
         * a spawned gun reads the very frame it is asked for) */
        if (g_frame < g_madeFrame + 2 || (ResOf(gl) != res && g_now < g_madeAt + 1.5)) break;
        if (ResOf(gl) != res) { GunAbort("her gun does not read as one"); break; }
        if (Pack() && !g_swap)
        {
            uint32_t bp = atomic_load(&g_bp);
            if (ResOf(bp) != g_bpRes) { GunAbort("her gun's backpack does not read as one"); break; }
            WieldTake(me, bp, KB_SLOT_BACKPACK);   /* the inventory's take, slot 6: the backpack (her own is dropped by it) */
            Log("Destroyer of Denouement %u: her gun's backpack %u on her back", g_uses, bp);
        }
        if (g_swap || (g_kSel == 3 && g_kOld)) { WieldAsk(1, "a support weapon is in her hands: put away first"); g_k = K_PUTAWAY; g_kAt = g_now + 0.8; g_putAt = g_now; }
        else g_k = K_TAKE;
        break;
    case K_PUTAWAY:
    {
        /* 0.1.4: on as soon as her selection reads the primary (0.1 s at least), not a flat 0.8 s */
        uintptr_t r = InventoryRecord(me);
        uint32_t sel = 0;
        if (r) ReadAt(r + 0x1C, &sel, 4);
        if (g_now >= g_kAt || (sel == 1 && g_now >= g_putAt + 0.1)) g_k = K_TAKE;
        break;
    }
    case K_TAKE:
        WieldTake(me, gl, KB_SLOT_SUPPORT);
        g_k = K_ASK; g_kAt = g_now + 0.1;
        break;
    case K_ASK:
    {
        if (g_now < g_kAt) break;
        /* what the take dropped: her own support weapon (the first call) or the gun a swap replaces */
        if (!g_swap && g_kOld && ResOf(g_kOld) == g_kOldRes && remove && em) remove(em, g_kOld);
        if (g_swap && g_swapOld && ResOf(g_swapOld) == res && remove && em) remove(em, g_swapOld);
        if (!g_swap && g_bpOld && ResOf(g_bpOld) == g_bpOldRes && remove && em) remove(em, g_bpOld);   /* the backpack she wore, dropped by the take */
        WieldAsk(3, "her gun in hand");
        GunFill(gl);
        g_k = K_HELD;
        g_awaySince = g_droppedSince = g_bpOffSince = g_emptySince = g_roundsLogAt = 0;
        g_dropped = 0;   /* (played: a pickup with no old support weapon left these set, and the next gun's end acted on them: a crash) */
        g_picked = 0;
        Log("Destroyer of Denouement %u%s: gun %u, %d rounds (%s %d)", g_uses, g_swap ? ", Lock & Load's new one" : " on", gl, GunRounds(gl),
            Pack() ? "in its backpack; the gun's own magazine" : "capacity", Pack() ? MagRounds(gl) : MagCapacity(gl));
        g_swap = 0;
        g_swapOld = 0;
        break;
    }
    case K_HELD:
    {
        uint32_t sel = 0, support = 0;
        if (ResOf(gl) != res) { GunAbort("her gun is gone"); break; }
        PoolFrame(gl);
        {
            int left = GunRounds(gl);
            /* 0.1.4: played, the first gun went 9 s in ("its rounds are spent"): a magazine reads 0 while it reloads, so
             * only 0 for a whole second counts; and her rounds are logged every 5 s (the first guns) */
            if (g_uses <= 3 && g_now >= g_roundsLogAt) { g_roundsLogAt = g_now + 5; Log("Destroyer of Denouement %u: %d rounds", g_uses, left); }
            if (left != 0) g_emptySince = 0;
            else if (!g_emptySince) { g_emptySince = g_now; Log("Destroyer of Denouement %u: its rounds read 0 (a reload, or spent)", g_uses); }
            else if (g_now - g_emptySince > 1.0) { GunEnd("its rounds are spent"); break; }
        }
        ReadAt(rec + 0x1C, &sel, 4);
        ReadAt(rec + 8, &support, 4);
        if (Pack())
        {
            uint32_t back = 0, bp = atomic_load(&g_bp);
            ReadAt(rec + 0xC, &back, 4);
            if (back == bp) g_bpOffSince = 0;
            else if (!g_bpOffSince) g_bpOffSince = g_now;
            else if (g_now - g_bpOffSince > 0.3) { GunEnd("its backpack is off her back"); break; }
        }
        /* dropped: no longer in her support slot (a moment's grace: the slot is rewritten on a switch) */
        if (support == gl) g_droppedSince = 0;
        else if (!g_droppedSince) g_droppedSince = g_now;
        else if (g_now - g_droppedSince > 0.3)
        {
            /* 0.1.4 (Yui): her old support weapon takes the dropped gun's place on the ground, as if she dropped it; a
             * weapon picked up in its place stays hers, in hand (played: her old one took the picked one's slot) */
            g_picked = support && support != gl ? support : 0;
            g_dropped = 1;
            Log("Destroyer of Denouement %u over (%s): %d rounds left", g_uses, g_picked ? "she picked up another weapon" : "she dropped it", GunRounds(gl));
            WieldDrop(me, atomic_load(&g_bp), 0xC);
            g_readyAt = g_now + s_gunCooldown;
            g_k = K_REMOVE; g_kAt = g_now;
            break;
        }
        /* switched off it: her primary or secondary in hand for gun_grace s (a stratagem, a grenade, a held item: not) */
        if (sel == 1 || sel == 2)
        {
            if (!g_awaySince) g_awaySince = g_now;
            else if (g_now - g_awaySince > s_gunGrace) { g_kSel = sel; GunEnd("she switched to another weapon"); }
        }
        else
        {
            static uint32_t told[8];
            if (sel != 3 && sel < 8 && told[sel]++ < 3) Log("her gun out, her selection %u (not a weapon switch: the gun stays)", sel);
            g_awaySince = 0;
        }
        break;
    }
    case K_LETGO:
        if (g_now < g_kAt) break;
        WieldDrop(me, gl, 0x8);
        if (atomic_load(&g_bp) && ResOf(atomic_load(&g_bp)) == g_bpRes) WieldDrop(me, atomic_load(&g_bp), 0xC);
        g_k = K_REMOVE; g_kAt = g_now + 0.4;
        break;
    case K_REMOVE:
        if (g_now < g_kAt) break;
        if (gl && ResOf(gl) == res && remove && em) remove(em, gl);
        atomic_store(&g_gl, 0);
        {
            uint32_t bp = atomic_load(&g_bp);
            if (bp && ResOf(bp) == g_bpRes && remove && em) remove(em, bp);
            atomic_store(&g_bp, 0);
        }
        g_kNew = g_kOldRes ? SpawnNear(g_kOldRes, "a new support weapon of her kind") : 0;
        g_hand = 0;
        if (g_kNew && g_dropped && HandPackage() == HF_PACKAGE_RESIDENT)
        {
            float feet[3], aim[3], at[3];
            if (PlayerPose(feet, aim)) { at[0] = feet[0] + aim[0]; at[1] = feet[1] + aim[1]; at[2] = feet[2]; g_hand = SpawnAt(SEAF_TROOPER, at, "a trooper to drop her old weapon"); }
        }
        g_bpNew = g_bpOldRes ? SpawnNear(g_bpOldRes, "a new backpack of her kind") : 0;
        /* with a trooper, no flat wait: it and her weapon are taken the first frame both are there (the Escort Probe
         * 0.9.0: they read the very frame of the spawn, and the trooper was gone 51 ms later, the weapon left a pickup) */
        if (g_kNew || g_bpNew) { g_k = K_WAITS; g_kAt = g_now + (g_hand ? 0 : 0.75); g_handUntil = g_now + 3; }
        else { FillUp(g_weapon[g_kSel]); g_k = K_IDLE; g_dropped = 0; g_picked = 0; }
        break;
    case K_WAITS:
        if (g_now < g_kAt) break;
        if (g_dropped && g_hand && !(InventoryRecord(g_hand) && ResOf(g_hand) == SEAF_TROOPER && ResOf(g_kNew) == g_kOldRes) && g_now < g_handUntil) break;
        if (g_bpNew)
        {
            if (ResOf(g_bpNew) == g_bpOldRes) { WieldTake(me, g_bpNew, KB_SLOT_BACKPACK); Log("Destroyer of Denouement %u: her new backpack %u on her back", g_uses, g_bpNew); }
            else Log("Destroyer of Denouement %u: the new backpack does not read as hers: left", g_uses);
            g_bpNew = 0;
        }
        if (!g_kNew) { FillUp(g_weapon[g_kSel]); g_k = K_IDLE; g_dropped = 0; g_picked = 0; break; }
        /* 0.1.4: dropped (or left for a pickup): her old support weapon to the ground, through the game's own take and
         * drop (played: one spawned on the ground could not be picked up; the drop makes it a pickup), at her feet where
         * the gun fell; a weapon she picked up meanwhile is dropped by that take and taken back after */
        if (g_dropped)
        {
            g_dropped = 0;
            if (ResOf(g_kNew) != g_kOldRes) { Log("Destroyer of Denouement %u: the new support weapon does not read as hers: left", g_uses); g_k = K_IDLE; break; }
            FillUp(g_kNew);
            if (g_hand && ResOf(g_hand) == SEAF_TROOPER && InventoryRecord(g_hand))
            {
                WieldTake(g_hand, g_kNew, KB_SLOT_SUPPORT);
                g_k = K_HANDDROP;
                g_kAt = g_now;
                break;
            }
            if (g_hand) { HfFn_RemoveEntity rm = hf_fn_RemoveEntity(self); void* em2 = hf_global_EntityManager(self); if (rm && em2 && ResOf(g_hand) == SEAF_TROOPER) rm(em2, g_hand); g_hand = 0; }
            Log("Destroyer of Denouement %u: no trooper for the drop: her own hands do it", g_uses);
            WieldTake(me, g_kNew, KB_SLOT_SUPPORT);
            g_k = K_DROPOLD;
            g_kAt = g_now + 0.3;
            break;
        }
        if (ResOf(g_kNew) == g_kOldRes)
        {
            WieldTake(me, g_kNew, KB_SLOT_SUPPORT);
            FillUp(g_kNew);
            if (g_kSel == 3) WieldAsk(3, "she had it in hand when the gun came");
            else FillUp(g_weapon[g_kSel]);
            Log("Destroyer of Denouement %u: her new support weapon %u taken", g_uses, g_kNew);
        }
        else Log("Destroyer of Denouement %u: the new support weapon does not read as hers: left", g_uses);
        g_k = K_IDLE;
        break;
    case K_HANDDROP:
        if (g_now < g_kAt) break;
        {
            /* the drop the frame the take shows in its support slot (21 ms in the probe); WieldDrop clears the slot's entry
             * after, or the trooper's removal takes the weapon along (Escort Probe 0.8.0) */
            uintptr_t r = InventoryRecord(g_hand);
            uint32_t now = 0;
            if (r) ReadAt(r + 8, &now, 4);
            if (now != g_kNew && g_now < g_handUntil) break;
        }
        WieldDrop(g_hand, g_kNew, 0x8);
        Log("Destroyer of Denouement %u: her old support weapon %u dropped by a trooper where her gun fell%s", g_uses, g_kNew, g_picked ? " (the weapon she picked up stays in her hands)" : "");
        g_picked = 0;
        g_k = K_HANDGONE;
        g_kAt = g_now;   /* the next frame */
        break;
    case K_HANDGONE:
        if (g_now < g_kAt) break;
        if (g_hand && ResOf(g_hand) == SEAF_TROOPER && remove && em) remove(em, g_hand);
        g_hand = 0;
        g_k = K_IDLE;
        break;
    case K_DROPOLD:
        if (g_now < g_kAt) break;
        WieldDrop(me, g_kNew, 0x8);
        Log("Destroyer of Denouement %u: her old support weapon %u dropped where her gun fell", g_uses, g_kNew);
        if (g_picked) { g_k = K_RETAKE; g_kAt = g_now + 0.3; }
        else g_k = K_IDLE;
        break;
    case K_RETAKE:
        if (g_now < g_kAt) break;
        {
            uintptr_t r = InventoryRecord(me);
            uint32_t now = 0;
            if (r) ReadAt(r + 8, &now, 4);
            if (!ResOf(g_picked)) Log("Destroyer of Denouement %u: the weapon she picked up (%u) is gone", g_uses, g_picked);
            else if (now != g_picked) WieldTake(me, g_picked, KB_SLOT_SUPPORT);
            if (ResOf(g_picked)) WieldAsk(3, "the weapon she picked up in hand");
        }
        Log("Destroyer of Denouement %u: she keeps the weapon she picked up (%u)", g_uses, g_picked);
        g_picked = 0;
        g_k = K_IDLE;
        break;
    default: g_k = K_IDLE;
    }
}
static int GunHeld(void) { return g_k == K_HELD || g_swap; }

/* ---- her shield (kitbase/kb_allies.h: her pool alone) ------------------------------------------------------------ */
static double g_shieldDue;                /* when the next refill comes (0: at once) */
static unsigned g_refills;
static double ShieldNow(void) { KbAlly* a = AllyOf(atomic_load(&g_avatar)); return a ? a->pool : 0; }
static void ShieldFrame(double dt)
{
    uint32_t me = atomic_load(&g_avatar);
    KbAlly* a = AllyOf(me);
    if (!me) return;
    if (!a) { AllySpawn(me, AVATAR); a = AllyOf(me); if (!a) return; }
    if (GunHeld())
    {
        if (g_now >= g_shieldDue && s_shieldMax > 0)
        {
            a->pool = s_shieldMax;
            g_shieldDue = g_now + s_shieldEvery;
            if (g_refills++ < 20) Log("her shield: %.0f (the next refill in %.0f s)", (double)s_shieldMax, (double)s_shieldEvery);
        }
    }
    else if (a->pool > 0 && s_shieldDecay > 0) { a->pool -= s_shieldDecay * dt; if (a->pool < 0) a->pool = 0; }
    if (a->pool > s_shieldMax) a->pool = s_shieldMax;
}
static void ShieldBroke(uint32_t e)
{
    static unsigned told;
    if (e == atomic_load(&g_avatar) && told++ < 10) Log("her shield broke");
}

/* ---- Lock & Load (X) ----------------------------------------------------------------------------------------------- */
static double g_cd[3];
static unsigned g_casts[3];
static uint32_t Held(uint32_t* selOut)
{
    uint32_t sel = 0, slots[3] = {0};
    uintptr_t rec = InventoryRecord(atomic_load(&g_avatar));
    if (!rec || !ReadAt(rec + 0x1C, &sel, 4) || !ReadAt(rec, slots, sizeof slots) || sel < 1 || sel > 3) return 0;
    if (selOut) *selOut = sel;
    return slots[sel - 1];
}
/* a fresh gun of the held one's kind: its package, spawned at her feet, 0.75 s, taken into its slot (the take drops the
 * old one, then removed) and drawn */
enum { LL_IDLE, LL_PACKAGE, LL_WAIT };
static int g_ll;
static uint32_t g_llOld, g_llNew, g_llSel;
static uint64_t g_llRes, g_llPkg;
static double g_llAt;
static unsigned g_llFrame;
static void LlFresh(uint32_t w)
{
    uint32_t sel = 0;
    if (g_ll != LL_IDLE || Held(&sel) != w || !WieldReady()) { Log("Lock & Load: busy, or the swap is off"); return; }
    g_llOld = w;
    g_llSel = sel;
    g_llRes = ResOf(w);
    if (!g_llRes || !LoadoutPackage(g_llRes, &g_llPkg)) g_llPkg = 0;
    g_ll = LL_PACKAGE;
    g_llAt = g_now + 8;
    Log("Lock & Load: a fresh gun of %u's kind (%016llx, selection %u)", w, (unsigned long long)g_llRes, sel);
}
static void LlFrame(void)
{
    HfFn_RemoveEntity remove = hf_fn_RemoveEntity(self);
    void* em = hf_global_EntityManager(self);
    uint32_t me = atomic_load(&g_avatar);
    switch (g_ll)
    {
    case LL_PACKAGE:
    {
        int st = g_llPkg ? hf->load_package(self, g_llPkg) : HF_PACKAGE_RESIDENT;
        if (st == HF_PACKAGE_LOADING) { if (g_now > g_llAt) { Log("Lock & Load: its package did not load"); g_ll = LL_IDLE; } break; }
        g_llNew = SpawnNear(g_llRes, "a fresh gun");
        if (!g_llNew) { g_ll = LL_IDLE; break; }
        g_ll = LL_WAIT;
        g_llAt = g_now + 1.5;   /* the latest; taken two frames after the spawn once it reads (0.1.4) */
        g_llFrame = g_frame;
        break;
    }
    case LL_WAIT:
    {
        if (g_frame < g_llFrame + 2 || (ResOf(g_llNew) != g_llRes && g_now < g_llAt)) break;
        g_ll = LL_IDLE;
    g_dropped = 0;
    g_picked = 0;
    g_hand = 0;
        uint32_t sel = 0;
        if (ResOf(g_llNew) != g_llRes) { Log("Lock & Load: the fresh gun does not read as one"); break; }
        if (Held(&sel) != g_llOld)
        {
            Log("Lock & Load: the gun left her hands meanwhile: the fresh one removed");
            if (remove && em) remove(em, g_llNew);
            break;
        }
        WieldTake(me, g_llNew, g_llSel);   /* drops the old one */
        if (ResOf(g_llOld) == g_llRes && remove && em) remove(em, g_llOld);
        WieldAsk(g_llSel, "the fresh gun in hand");
        FillUp(g_llNew);
        Log("Lock & Load: fresh gun %u in place of %u, %d rounds in its magazine", g_llNew, g_llOld, MagRounds(g_llNew));
        break;
    }
    default: g_ll = LL_IDLE;
    }
}
static void LockAndLoad(void)
{
    if (g_now < g_cd[1]) return;
    uint32_t w = Held(NULL);
    if (!w) { Log("Lock & Load: no gun in her hands"); return; }
    g_cd[1] = g_now + s_llCooldown;
    g_casts[1]++;
    uint32_t gl = atomic_load(&g_gl);
    if (gl && w == gl && g_k == K_HELD && (s_llErgonomics > 0 || s_llRecoil)) { GunSwap(); return; }
    if (gl && w == gl)
    {
        int before = GunRounds(gl);
        GunFill(gl);
        Log("Lock & Load: her gun %u refilled (%d -> %d rounds)", w, before, GunRounds(gl));
        return;
    }
    /* 0.1.4: any other gun filled where it is (its attachments kept), now that its magazine size reads (played: the
     * refill only filled the spares, the size read -1); a gun whose size still can't be read is replaced by a fresh one
     * of its kind (W's way: full, but with its default attachments) */
    int before = MagRounds(w), cap = MagCapacity(w);
    if (cap > 0)
    {
        FillUp(w);
        Log("Lock & Load: her gun %u refilled (%d -> %d rounds, capacity %d; spares too)", w, before, MagRounds(w), cap);
        return;
    }
    LlFresh(w);
}

/* ---- Ishbosheth (C) ---------------------------------------------------------------------------------------------- */
static int g_ishArmed;
static double g_ishUntil, g_ishNextTracer, g_ishNextTick, g_ishStart;
static int g_ishOn;
static unsigned g_ishTicks, g_ishHits0, g_ishSprayed0, g_ishTicks0;   /* (the counts at this barrage's start: the log's per barrage) */
static uint32_t g_ishHeld;
static int g_ishRounds = -1;
static unsigned g_ishHits, g_ishSprayed;
static int Shoot(uint32_t round, const float from0[3], const float dir0[3])
{
    float from[3], dir[3];
    memcpy(dir, dir0, 12);
    float n = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (!(n > 0.01f)) return 0;
    for (int k = 0; k < 3; k++) { dir[k] /= n; from[k] = from0[k] + dir[k] * 1.2f; }   /* clear of her own body */
    return hf->spawn_projectile(self, round, from, dir) == HF_ACT_REQUESTED;
}
static int IshPackage(void)
{
    static uint64_t pkg;
    static int state;
    uint64_t holder = g_tracer[g_use][0] || g_tracer[g_use][1] ? HF_ENTITY_APW1_ANTIMATERIEL_RIFLE : HF_ENTITY_MAXIGUN_BACKPACK;   /* the tracers' round, else the Maxigun's */
    if (!pkg && !LoadoutPackage(holder, &pkg)) pkg = ~0ull;
    if (pkg == ~0ull) return 0;
    if (state != HF_PACKAGE_RESIDENT)
    {
        state = hf->load_package(self, pkg);
        if (!state) { Log("Ishbosheth: its rounds' package %016llx could not be held", (unsigned long long)pkg); pkg = ~0ull; }
        else if (state == HF_PACKAGE_RESIDENT) Log("Ishbosheth: its rounds' package %016llx is loaded", (unsigned long long)pkg);
    }
    return state;
}
static void Ishbosheth(void)
{
    if (g_ishArmed || g_ishOn || g_now < g_cd[2]) return;
    g_ishArmed = 1;
    g_ishHeld = 0;
    g_ishRounds = -1;
    Log("Ishbosheth: readied (her next shot sets it off)");
}
/* 0.1.4: a barrage of ish_duration s (Yui: Blue Archive's EX lasts about 10 s, its tracers coming out the whole time):
 * from her feet + 1.4 m along her aim as it is each moment, a tracer every ish_every s swinging across the fan, and every
 * ish_tick s the ish_targets nearest enemies in the fan take a tracer each dealing a share of ish_mult (ish_mult in all
 * over the barrage, if she fires the whole time: it fires only while she holds the trigger). Tracers: copies of the APW-1's round (its bright trail) dealing her gun's damage copies, slowed to
 * ish_speed m/s so the trail is seen (Koleda's round_speed). Cooldown from its end. */
static int IshOrigin(float head[3], float fwd[3])
{
    float cam[3], feet[3];
    if (!View(cam, fwd) || !PlayerPose(feet, NULL)) return 0;
    head[0] = feet[0]; head[1] = feet[1]; head[2] = feet[2] + 1.4f;   /* (her head node is not reliable: once 0.9 m under her feet) */
    return 1;
}
static void IshTick(const float head[3], const float fwd[3], uint32_t round)
{
    float q[3], flat = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1]);
    if (!(flat > 0.05f)) return;
    float cosMax = cosf(s_ishCone * 0.0174533f);
    struct { uint32_t e; float d2, q[3]; } t[32];
    int n = 0, cap = s_ishTargets > 32 ? 32 : s_ishTargets;
    for (int k = 0; k < FOES; k++)
    {
        uint32_t e = g_foe[k];
        float d2;
        if (!e || !TargetPos(e, g_foeRes[k], q) || (d2 = Dist2(q, head)) > s_ishRange * s_ishRange || d2 < 1) continue;
        float dx = q[0] - head[0], dy = q[1] - head[1], h = sqrtf(dx * dx + dy * dy);
        if (h < 0.1f || (dx * fwd[0] + dy * fwd[1]) / (h * flat) < cosMax || !Alive(e)) continue;
        int j;
        if (n == cap) { if (d2 >= t[n - 1].d2) continue; j = n - 1; }
        else j = n++;
        while (j > 0 && t[j - 1].d2 > d2) { t[j] = t[j - 1]; j--; }
        t[j].e = e; t[j].d2 = d2; memcpy(t[j].q, q, 12);
    }
    for (int i = 0; i < n; i++)
    {
        float d[3] = {t[i].q[0] - head[0], t[i].q[1] - head[1], t[i].q[2] - head[2]};
        g_ishHits += (unsigned)Shoot(round, head, d);
    }
    g_ishTicks++;
}
static void IshFire(void)
{
    if (IshPackage() == HF_PACKAGE_LOADING) return;   /* (asked for from the mission's start: only a moment's wait) */
    g_ishArmed = 0;
    g_ishOn = 1;
    g_ishStart = g_now;
    g_ishHits0 = g_ishHits; g_ishSprayed0 = g_ishSprayed; g_ishTicks0 = g_ishTicks;
    g_ishUntil = g_now + s_ishDuration;
    g_ishNextTracer = g_ishNextTick = g_now;
    g_casts[2]++;
    Log("Ishbosheth: on for %.0f s (a volley of 3 x %d tracers every %.2f s across %.0f degrees each side; every %.2f s the %d nearest enemies within %.0f m take a share of %.2f times "
        "her gun's round; tracers at %.0f m/s)", (double)s_ishDuration, s_ishPerSection, (double)s_ishEvery, (double)s_ishCone, (double)s_ishTick, s_ishTargets,
        (double)s_ishRange, (double)s_ishMult, (double)s_ishSpeed);
}
static void IshBarrage(void)
{
    if (!g_ishOn) return;
    if (g_now >= g_ishUntil || !g_alive)
    {
        g_ishOn = 0;
        g_cd[2] = g_now + s_ishCooldown;
        Log("Ishbosheth over: %u hits on enemies over %u ticks, %u tracers", g_ishHits - g_ishHits0, g_ishTicks - g_ishTicks0, g_ishSprayed - g_ishSprayed0);
        return;
    }
    float head[3], fwd[3];
    /* 0.1.4: it fires only while she does (Yui); the barrage's time runs on either way */
    /* (played: tied to the trigger it fired with no shot; now to her gun actually firing: the gun in her hands losing
     * rounds in the last quarter second; the trigger only for a gun whose rounds can't be read) */
    static uint32_t lastW;
    static int lastR = -1;
    static double lastShot = -1;
    uint32_t sel = 0, w = Held(&sel), gl = atomic_load(&g_gl);
    int r = !w || sel < 1 || sel > 3 ? -1 : (gl && w == gl ? GunRounds(gl) : MagRounds(w));
    if (w && w == lastW && r >= 0 && lastR >= 0 && r < lastR) lastShot = g_now;
    lastW = w;
    lastR = r;
    int shooting = (lastShot >= 0 && g_now - lastShot < 0.25) || (w && sel >= 1 && sel <= 3 && r < 0 && Focused() && (GetAsyncKeyState(VK_LBUTTON) & 0x8000));
    if (!shooting) { g_ishNextTracer = g_now; return; }
    if (!IshOrigin(head, fwd)) return;
    uint32_t round = g_tracer[g_use][0] ? g_tracer[g_use][0] : g_ishRound[0];
    uint32_t spray = g_tracer[g_use][1] ? g_tracer[g_use][1] : g_round[0];
    int guard = 0;
    while (spray && s_ishEvery > 0 && g_now >= g_ishNextTracer && guard++ < 4)
    {
        /* 0.1.4: a volley each time (Yui: the fan's shape, not one beam swinging): the fan in three sections, the two
         * outer and the inner one, ish_per_section tracers in each at a random angle inside it, a little above and below
         * her aim. At most 80 tracers a second in all (Koleda: thousands of spawned rounds crashed the game) */
        static double budget = 80, budgetAt;
        budget += (g_now - budgetAt) * 80;
        budgetAt = g_now;
        if (budget > 80) budget = 80;
        float cone = s_ishCone * 0.0174533f, third = cone * 2 / 3;
        for (int sec = 0; sec < 3; sec++)
            for (int b = 0; b < s_ishPerSection && budget >= 1; b++)
            {
                float lo = -cone + third * (float)sec;
                float a = lo + third * ((float)rand() / (float)RAND_MAX), ca = cosf(a), sa = sinf(a);
                float lift = 0.05f * (((float)rand() / (float)RAND_MAX) * 2 - 1);
                float d[3] = {fwd[0] * ca - fwd[1] * sa, fwd[0] * sa + fwd[1] * ca, fwd[2] + lift};
                g_ishSprayed += (unsigned)Shoot(spray, head, d);
                budget -= 1;
            }
        g_ishNextTracer += s_ishEvery;
    }
    if (g_now >= g_ishNextTracer) g_ishNextTracer = g_now + s_ishEvery;   /* (a long frame: not a burst to catch up) */
    if (round && g_now >= g_ishNextTick) { IshTick(head, fwd, round); g_ishNextTick = g_now + s_ishTick; }
}
/* readied: her next shot (the gun in her hands loses a round) sets it off; the left button too, for a gun without a
 * magazine to read */
static void IshFrame(void)
{
    IshBarrage();
    if (!g_ishArmed) return;
    uint32_t w = Held(NULL);
    int r = w ? MagRounds(w) : -1;
    int fired = 0;
    if (w && w == g_ishHeld && r >= 0 && g_ishRounds >= 0 && r < g_ishRounds) fired = 1;
    if (w && r < 0 && Focused() && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) fired = 1;   /* (not with a grenade or a stratagem in hand) */
    g_ishHeld = w;
    g_ishRounds = r;
    if (fired) IshFire();
}

/* ---- the passives -------------------------------------------------------------------------------------------------- */
static double g_buffUntil;
static uint32_t g_llHeld;
static int g_llRounds = -1;
static unsigned g_reloads, g_buffed;
static double g_buffDamage;
/* Lock & Load (passive): an emptied magazine of the gun in her hands reloads at once, and the buff */
static void ReloadFrame(void)
{
    uint32_t w = Held(NULL), gl = atomic_load(&g_gl);
    int r = w ? MagRounds(w) : -1;
    /* 0.1.4: not a weapon of one or two rounds a magazine (played: a one-shot launcher reloaded free after every shot
     * and kept the buff's timer restarting) */
    if (w && w == g_llHeld && r == 0 && g_llRounds > 0 && (MagCapacity(w) < 0 || MagCapacity(w) >= 3))
    {
        if (w != gl && s_reloadFree)
        {
            int cap = MagCapacity(w);
            if (cap > 0) { MagAdd(w, cap); r = MagRounds(w); }
            if (g_reloads < 12) Log("Lock & Load (passive): gun %u emptied: reloaded at once (%d rounds)", w, r);
        }
        g_reloads++;
        if (s_reloadBuff > 0 && s_reloadSeconds > 0) g_buffUntil = g_now + s_reloadSeconds;
    }
    g_llHeld = w;
    g_llRounds = r;
}
static void DamageDealt(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    (void)e; (void)me;
    uint8_t tid[24];
    if (d->amount <= 0 || !IsShot(d)) return;
    if (Descriptor(d->target, tid) && (Quad(tid) == AVATAR || FriendRes(Quad(tid)))) return;   /* no bonus on an ally */
    NoteTarget(d);
    double mult = 1;
    if (g_now < g_buffUntil) mult += s_reloadBuff;
    if (g_low) mult += s_oaoBonus;
    if (mult == 1) return;
    int before = d->amount;
    ScaleDamage(d, mult);
    g_buffed++;
    g_buffDamage += d->amount - before;
}
static void DamageTaken(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me) { (void)me; AllyAbsorb(e, d); }

/* ---- events --------------------------------------------------------------------------------------------------------- */
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
    AllyKill(k->victim);
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
        g_shieldDue = 0;   /* a new life: the next gun brings a full shield */
        LearnNet();
    }
    else { g_alive = 0; g_ishArmed = 0; }
}
/* 0.1.4: her Helldiver's network id from the player list (played: no spawn of hers named it, so every hit read as not
 * hers and her damage bonuses never applied). PlayerManager: count +0x84, slot i's peer +0x2C8 + 0x38 i, its Helldiver's
 * network id +0x3A8 + 0x20 i; LocalUser: this machine's peer +0xB398 (the SDK's globals) */
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
    if (g_uses || g_casts[1] || g_casts[2])
        Log("%s: her gun %u times (%u Lock & Load swaps), Lock & Load %u, Ishbosheth %u (%u enemies hit, %u sweep rounds); %u shield refills, %u hits on her shield "
            "(%.0f kept off, %u broke); %u reloads; %u hits buffed (+%.0f damage)",
            why, g_uses, g_swaps, g_casts[1], g_casts[2], g_ishHits, g_ishSprayed, g_refills, g_allyHits, g_allyAbsorbed, g_allyBroke, g_reloads, g_buffed, g_buffDamage);
    GunAbort(start ? "a mission started" : "the mission ended");
    AllyClear();
    memset(g_cd, 0, sizeof g_cd);
    memset(g_casts, 0, sizeof g_casts);
    g_readyAt = 0;
    g_shieldDue = 0;
    g_ishArmed = 0;
    g_ishOn = 0;
    g_buffUntil = 0;
    g_uses = g_swaps = g_refills = g_ishHits = g_ishSprayed = g_reloads = g_buffed = 0;
    g_buffDamage = 0;
    g_lastTarget = 0;
    g_ll = LL_IDLE;
    if (start) g_avatarNet = 0;   /* a new mission: a new network id, learned again */
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
    if (!WieldPins()) Log("Destroyer of Denouement is off (the swap's code is not the reviewed one)");
    HitsInstall("her hits");
    Log("ready: Destroyer of Denouement on %s (%d rounds, %.0f rpm + %.0f%%, AP %d, %.0f s cooldown; shield %.0f every %.0f s, -%.0f/s without it), "
        "Lock & Load on %s, Ishbosheth on %s", s_keys[0].label, s_gunRounds, (double)s_gunRpm, (double)(s_judgmentRate * 100), s_gunAp, (double)s_gunCooldown,
        (double)s_shieldMax, (double)s_shieldEvery, (double)s_shieldDecay, s_keys[1].label, s_keys[2].label);
}

/* ---- for the HUD (read by the Hina HUD addon through LuaJIT's ffi; layout version 1, the same struct in hina_hud.lua) */
typedef struct HinaHud
{
    uint32_t version, in_mission;
    float cd[3], cd_max[3];            /* seconds left, full cooldowns: Destroyer of Denouement, Lock & Load, Ishbosheth */
    uint32_t gun_on, rounds, rounds_max;
    float shield, shield_max;
    uint32_t ish_armed;
    float buff_left;                   /* Lock & Load (passive)'s buff, seconds left */
    uint32_t low;                      /* Out-and-Out is on (crouched or prone) */
    uint32_t base;                     /* her gun: 0 the Maxigun's copy, 1 the MG-43's */
    uint32_t keys[3];
    char key_label[3][24];
} HinaHud;
static HinaHud g_hud = {1};
HF_MOD_EXPORT HinaHud* hina_hud_state(void)
{
    g_hud.in_mission = (uint32_t)(g_inMission && g_alive && kb_kit_on);
    g_hud.cd[0] = g_k != K_IDLE ? 0 : (float)fmax(0, g_readyAt - g_now); g_hud.cd_max[0] = s_gunCooldown;
    g_hud.cd[1] = (float)fmax(0, g_cd[1] - g_now); g_hud.cd_max[1] = s_llCooldown;
    g_hud.cd[2] = (float)fmax(0, g_cd[2] - g_now); g_hud.cd_max[2] = s_ishCooldown;
    g_hud.gun_on = (uint32_t)(g_k != K_IDLE);
    int r = g_k == K_HELD ? GunRounds(atomic_load(&g_gl)) : 0;
    g_hud.rounds = (uint32_t)(r > 0 ? r : 0);
    g_hud.rounds_max = (uint32_t)s_gunRounds;
    g_hud.shield = (float)ShieldNow();
    g_hud.shield_max = s_shieldMax;
    g_hud.ish_armed = (uint32_t)(g_ishArmed || g_ishOn);
    g_hud.buff_left = (float)fmax(0, g_buffUntil - g_now);
    g_hud.low = (uint32_t)(g_low && s_oaoBonus > 0);
    g_hud.base = (uint32_t)g_use;
    for (int i = 0; i < 3; i++)
    {
        g_hud.keys[i] = (uint32_t)s_keys[i].vk;
        memcpy(g_hud.key_label[i], s_keys[i].label, sizeof g_hud.key_label[i]);
    }
    return &g_hud;
}
static atomic_int g_bindPress;
HF_MOD_EXPORT int hina_input(unsigned version, int skill)
{
    if (version != 1) return 0;
    if (skill >= 1 && skill <= 3) atomic_fetch_or(&g_bindPress, 1 << (skill - 1));
    return 1;
}
HF_MOD_EXPORT int hina_key(unsigned version, int skill, int* vk, int* mods)
{
    if (version != 1 || skill < 1 || skill > 3 || !vk || !mods) return 0;
    *vk = s_keys[skill - 1].vk;
    *mods = s_keys[skill - 1].mods;
    return 1;
}

/* ---- the frame -------------------------------------------------------------------------------------------------- */
static void Tick(HfMod* mod, float dt, void* user)
{
    KbOptionsTick();   /* the options screen's changes, in a mission or not */
    g_frame++;
    double step = dt > 0 && dt < 1 ? dt : 0;
    g_now += step;
    int inMission = hf->game_state(self) == HF_STATE_MISSION;
    if (inMission != g_inMission) { Reset(inMission ? "mission start" : "mission end"); g_inMission = inMission; }
    if (!g_inMission || !game || !kb_kit_on) return;
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
            if (i == 0)
            {
                if (g_k == K_HELD) GunEnd("the key again");
                else if (g_k == K_IDLE && g_now >= g_readyAt) { GunStart(); g_casts[0]++; }
                else if (g_k == K_IDLE) { static unsigned told; if (told++ < 5) Log("Destroyer of Denouement: %.0f s of cooldown left", g_readyAt - g_now); }
            }
            else if (i == 1) LockAndLoad();
            else Ishbosheth();
        }
        prev[i] = down;
    }
    if (!NetOk(g_avatarNet)) LearnNet();
    if (!NetOk(g_avatarNet) && g_inMission) NetFromPlayers();
    /* her gun's package, asked for from the mission's start (Ishbosheth's rounds need it before any gun is out) */
    {
        static double pkgAt;
        if (g_k == K_IDLE && g_now >= pkgAt) { pkgAt = g_now + 1; Gun(); GunPackage(); if (g_inMission) { IshPackage(); HandPackage(); } }
    }
    GunFrame();
    if (g_inMission) LlFrame();
    if (g_alive)
    {
        ShieldFrame(step);
        StanceFrame();
        ReloadFrame();
        IshFrame();
    }
}

/* ---- Mod Options Menu ------------------------------------------------------------------------------------------- */
static const KbOption kOptions[] = {
    {"gun_rounds", NULL, &s_gunRounds}, {"gun_rpm", &s_gunRpm, NULL}, {"gun_ap", NULL, &s_gunAp}, {"gun_cooldown", &s_gunCooldown, NULL},
    {"gun_grace", &s_gunGrace, NULL}, {"gun_base", NULL, &s_gunBase}, {"gun_drum", NULL, &s_gunDrum},
    {"drum_ergonomics", NULL, &s_drumErgonomics},
    {"shield_max", &s_shieldMax, NULL}, {"shield_every", &s_shieldEvery, NULL}, {"shield_decay", &s_shieldDecay, NULL},
    {"ll_cooldown", &s_llCooldown, NULL}, {"ll_ergonomics", &s_llErgonomics, NULL}, {"ll_recoil", NULL, &s_llRecoil},
    {"ish_cooldown", &s_ishCooldown, NULL}, {"ish_mult", &s_ishMult, NULL}, {"ish_cone", &s_ishCone, NULL}, {"ish_range", &s_ishRange, NULL},
    {"ish_targets", NULL, &s_ishTargets}, {"ish_duration", &s_ishDuration, NULL}, {"ish_every", &s_ishEvery, NULL}, {"ish_per_section", NULL, &s_ishPerSection},
    {"ish_tick", &s_ishTick, NULL}, {"ish_speed", &s_ishSpeed, NULL},
    {"reload_free", NULL, &s_reloadFree}, {"reload_buff", &s_reloadBuff, NULL}, {"reload_seconds", &s_reloadSeconds, NULL},
    {"judgment_rate", &s_judgmentRate, NULL}, {"oao_bonus", &s_oaoBonus, NULL}, {"oao_head", &s_oaoHead, NULL},
};
/* her gun's rate and rounds take effect on its records at once (the next gun built has them) */
static void OptionAfter(const char* name, int changed)
{
    if (!changed || !g_entsReady) return;
    if (!strcmp(name, "gun_rounds") || !strcmp(name, "gun_rpm") || !strcmp(name, "judgment_rate")) for (int i = 0; i < 2; i++) GunRecords(i);
}
HF_MOD_EXPORT int hina_option_get(const char* name, double* out) { return OptionGet(name, out); }
HF_MOD_EXPORT int hina_option_set(const char* name, double value) { return OptionSet(name, value); }

#include "kit_options.h"   /* kOpRows: the options screen's rows (kitbase/kb_options.h) */
#include "kit_icon.h"      /* kOpIcon, KB_OP_ACCENT */
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 17 || api->size < offsetof(HfApi, global) + sizeof api->global) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    RosterJoin("hina", "Hina");
    kb_ini = "hina.ini";
    kb_key_names[0] = "key_s1";
    kb_key_names[1] = "key_s2";
    kb_key_names[2] = "key_s3";
    KB_SETTINGS(kSettings);
    KB_OPTIONS(kOptions);
    kb_option_after = OptionAfter;
    kb_damage_taken = DamageTaken;
    kb_damage_dealt = DamageDealt;
    kb_ally_broke = ShieldBroke;
    LoadSettings();
    /* her gun, its round and Ishbosheth's: copies of her own, of the Maxigun's and of the MG-43's (the one used is
     * settled at her first call: the Maxigun's only with it owned) */
    for (int i = 0; i < 2; i++)
    {
        static const char* const dn[2] = {"yui/hina/destroyer_round_maxigun", "yui/hina/destroyer_round_mg43"};
        static const char* const in[2] = {"yui/hina/ishbosheth_round_maxigun", "yui/hina/ishbosheth_round_mg43"};
        static const char* const pn[2] = {"yui/hina/destroyer_shot_maxigun", "yui/hina/destroyer_shot_mg43"};
        static const char* const qn[2] = {"yui/hina/ishbosheth_shot_maxigun", "yui/hina/ishbosheth_shot_mg43"};
        static const char* const wn[2] = {"yui/hina/destroyer_of_denouement_maxigun", "yui/hina/destroyer_of_denouement_mg43"};
        HfDamage dmg = {sizeof dmg};
        dmg.base = kBaseDamage[i];
        dmg.name = dn[i];
        g_damage[i] = api->add_damage(mod, &dmg);
        HfDamage ish = {sizeof ish};
        ish.base = kBaseDamage[i];
        ish.name = in[i];
        ish.scale = s_ishMult * s_ishTick / s_ishDuration;   /* 0.1.4: a share each tick, ish_mult over the barrage */
        g_ishDamage[i] = api->add_damage(mod, &ish);
        if (g_damage[i])
        {
            HfProjectile p = {sizeof p};
            p.base = kFiredRound[i];
            p.name = pn[i];
            p.damage = g_damage[i];
            g_round[i] = api->add_projectile(mod, &p);
        }
        if (g_ishDamage[i])
        {
            HfProjectile p = {sizeof p};
            p.base = kBaseRound[i];
            p.name = qn[i];
            p.damage = g_ishDamage[i];
            g_ishRound[i] = api->add_projectile(mod, &p);
        }
        /* 0.1.4: Ishbosheth's rounds as tracers (Yui; Koleda's Full Speed tracers): copies of the APW-1's round (226,
         * its bright trail; its package the APW-1's, loaded from the mission's start) dealing her gun's damage copies */
        {
            static const char* const tn[2][2] = {{"yui/hina/ishbosheth_tracer_maxigun", "yui/hina/sweep_tracer_maxigun"},
                                                 {"yui/hina/ishbosheth_tracer_mg43", "yui/hina/sweep_tracer_mg43"}};
            uint32_t dmgs[2] = {g_ishDamage[i], g_damage[i]};
            for (int t = 0; t < 2; t++)
            {
                if (!dmgs[t]) continue;
                HfProjectile p = {sizeof p};
                p.base = TRACER_ROUND;
                p.name = tn[i][t];
                p.damage = dmgs[t];
                g_tracer[i][t] = api->add_projectile(mod, &p);
            }
        }
        /* an entity copy (add_entity), not an arsenal weapon: no armory entry, nothing in anyone's loadout (an arsenal
         * weapon needs a model of its own in the mod's data); every record its own (own_all) */
        HfEntity w = {sizeof w};
        w.base = kBaseEntity[i];
        w.name = wn[i];
        w.own_all = 1;
        w.designation = i ? "MG-43H" : "M-1000H";
        w.title = "Destroyer of Denouement";
        w.description = "Sorasaki Hina's machine gun: the Gehenna Prefect's own.";
        g_gunRes[i] = api->add_entity ? api->add_entity(mod, &w) : 0;
        api->log(mod, "her gun on the %s: entity %016llx, round %u (damage %u), Ishbosheth's round %u (damage %u, x%.2f)", kBaseName[i], (unsigned long long)g_gunRes[i], g_round[i],
                 g_damage[i], g_ishRound[i], g_ishDamage[i], (double)s_ishMult);
    }
    {
        HfEntity bp = {sizeof bp};
        bp.base = HF_ENTITY_MAXIGUN_BACKPACK;
        bp.name = "yui/hina/destroyer_backpack";
        bp.own_all = 1;
        bp.title = "Destroyer of Denouement Ammunition";
        g_bpRes = g_gunRes[0] && api->add_entity ? api->add_entity(mod, &bp) : 0;
        api->log(mod, "her gun's backpack (a copy of the Maxigun Backpack): entity %016llx", (unsigned long long)g_bpRes);
    }
    KbOptionsStart(api, mod, "hina", "Hina", VERSION, kOpRows, (int)(sizeof kOpRows / sizeof kOpRows[0]), kOpIcon, KB_OP_ICON, KB_OP_ACCENT, s_optionsKey);
    api->log(mod, "Hina %s: keys %s / %s / %s (Destroyer of Denouement, Lock & Load, Ishbosheth)", VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label);
    if (s_badKey) api->log(mod, "hina.ini: a key could not be read (bits %d: s1, s2, s3); its default stays", s_badKey);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    if (api->on(mod, HF_EVENT_PROJECTILES_READY, RowsReady, NULL)) api->log(mod, "her rounds' armour penetration can't be set: the base's");
    if (api->on(mod, HF_EVENT_ENTITIES_READY, EntitiesReady, NULL)) api->log(mod, "her gun's records can't be set: the base's numbers");
    int ok = api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) &&
             api->on_game_thread(mod, Tick, NULL);
    if (!ok) api->log(mod, "events or the game thread are unavailable: Hina stays off");
    return 0;
}
