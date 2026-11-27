/* Hina (yui.hina): Sorasaki Hina's kit from Blue Archive (the Gehenna Prefect, MG) as abilities on keys, for your own
 * Helldiver (solo or host), built on the kit base (kitbase/, docs/kit-base.md; her design: docs/hina-design.md). Her
 * identity (Yui): put as many rounds down range as possible, as efficiently as possible.
 *
 *   Destroyer of Denouement (key_s1, default Z): her signature gun in her hands, the way W's King of Hearts gives the
 *     GL-21 (the Wield Probe's swap): a copy of the M-1000 Maxigun (gun_base 0, when this profile owns it) or of the
 *     MG-43 Machine Gun (the fallback: without the Maxigun, or gun_base 1) with gun_rounds rounds, gun_rpm rounds a
 *     minute and armour penetration gun_ap (its own entity, round and damage: add_entity, add_projectile, add_damage;
 *     kit base principle 6). It goes when she switches to another weapon (her primary or secondary for gun_grace s;
 *     a stratagem or a grenade in her hands does not count, Yui), drops it, spends its rounds or presses Z again; her
 *     own weapon comes back and the cooldown (gun_cooldown) starts then.
 *     While she holds it she has a shield (kitbase/kb_allies.h, Alva's): shield_max, refilled to shield_max every
 *     shield_every s, never above it. Once the gun is gone the shield drains, shield_decay a second, until a refill.
 *   Lock & Load (key_s2, default X): the gun in her hands refilled in full (magazine and spares); on her own gun also
 *     +ll_ergonomics ergonomics and no recoil (ll_recoil 1) for the rest of that gun's time out: a new one of it,
 *     built with them, full, takes its place (weapon handling is read when a weapon is built).
 *   Ishbosheth (key_s3, default C; her EX, Denouement: Ishbosheth): readied by the key, set off by her next trigger
 *     pull (any gun): every enemy in a fan of ish_cone degrees (each side of her aim) out to ish_range m takes a round
 *     dealing ish_mult times her gun's round (Blue Archive: 636%), and ish_spray rounds of her gun sweep across the fan
 *     (Koleda's Full Speed cone). Cooldown ish_cooldown from then.
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

#define VERSION "0.1.0"
#define BUILD "25480438"

#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"
#include "kitbase/kb_allies.h"
#include "kitbase/kb_damage.h"
#include "kitbase/kb_settings.h"
#include "kitbase/kb_options.h"
#include "kitbase/kb_roster.h"

KB_ROSTER_EXPORTS(hina)
KB_OPTIONS_EXPORTS(hina)

/* ---- settings ------------------------------------------------------------------------------------------------- */
static int s_gunBase = 0, s_gunRounds = 6000, s_gunAp = 5;
static float s_gunRpm = 6000, s_gunCooldown = 30, s_gunGrace = 0.5f;
static float s_shieldMax = 3000, s_shieldEvery = 300, s_shieldDecay = 100;
static float s_llCooldown = 20, s_llErgonomics = 80;
static int s_llRecoil = 1;
static float s_ishCooldown = 30, s_ishMult = 6.36f, s_ishCone = 30, s_ishRange = 40;
static int s_ishTargets = 12, s_ishSpray = 16;
static int s_reloadFree = 1;
static float s_reloadBuff = 0.21f, s_reloadSeconds = 16, s_judgmentRate = 0.14f, s_oaoBonus = 0.027f, s_oaoHead = 1.2f;
static int s_optionsKey = 0x78;        /* options_key: the virtual key that opens the options screen (120: F9; 0: none) */

static const KbSetting kSettings[] = {
    KB_I("gun_base", s_gunBase, 0, 1), KB_I("gun_rounds", s_gunRounds, 1, 100000), KB_F("gun_rpm", s_gunRpm, 60, 12000), KB_I("gun_ap", s_gunAp, 0, 10),
    KB_F("gun_cooldown", s_gunCooldown, 0, 600), KB_F("gun_grace", s_gunGrace, 0, 10),
    KB_F("shield_max", s_shieldMax, 0, 100000), KB_F("shield_every", s_shieldEvery, 1, 3600), KB_F("shield_decay", s_shieldDecay, 0, 100000),
    KB_F("ll_cooldown", s_llCooldown, 0, 600), KB_F("ll_ergonomics", s_llErgonomics, 0, 200), KB_I("ll_recoil", s_llRecoil, 0, 1),
    KB_F("ish_cooldown", s_ishCooldown, 0, 600), KB_F("ish_mult", s_ishMult, 0.1, 50), KB_F("ish_cone", s_ishCone, 1, 90), KB_F("ish_range", s_ishRange, 5, 300),
    KB_I("ish_targets", s_ishTargets, 1, 32), KB_I("ish_spray", s_ishSpray, 0, 64),
    KB_I("reload_free", s_reloadFree, 0, 1), KB_F("reload_buff", s_reloadBuff, 0, 5), KB_F("reload_seconds", s_reloadSeconds, 0, 120),
    KB_F("judgment_rate", s_judgmentRate, 0, 3), KB_F("oao_bonus", s_oaoBonus, 0, 5), KB_F("oao_head", s_oaoHead, 0.2, 2),
    KB_I("options_key", s_optionsKey, 0, 255),
};

static __attribute__((unused)) void DefaultIni(FILE* f)
{
    fprintf(f, "; Hina %s. Keys: a letter, digit, F1-F24, Mouse3, Mouse4 or Mouse5, with Shift+, Ctrl+ or Alt+ in front for a\n"
               "; combo (Shift+Q). The game still sees the keys too (Shift sprints, R reloads), so pick ones it leaves alone.\n"
               "key_s1 = %s\nkey_s2 = %s\nkey_s3 = %s\n"
               "; Destroyer of Denouement: her gun in her hands, a copy of the M-1000 Maxigun (gun_base 0, with the Maxigun's\n"
               "; warbond) or of the MG-43 Machine Gun (without it, or gun_base 1), with gun_rounds rounds, gun_rpm rounds a minute\n"
               "; and armour penetration gun_ap (5: tank). It goes when she is on her primary or secondary for gun_grace s, drops\n"
               "; it or spends it; the cooldown starts then. Takes effect at the next start.\n"
               "gun_base = %d\ngun_rounds = %d\ngun_rpm = %g\ngun_ap = %d\ngun_cooldown = %g\ngun_grace = %g\n"
               "; Her shield while she holds it: shield_max, refilled to shield_max every shield_every s; once the gun is gone it\n"
               "; drains shield_decay a second until the next refill.\n"
               "shield_max = %g\nshield_every = %g\nshield_decay = %g\n"
               "; Lock & Load: cooldown; the gun in her hands refilled; her own gun also +ll_ergonomics and no recoil (ll_recoil 1).\n"
               "ll_cooldown = %g\nll_ergonomics = %g\nll_recoil = %d\n"
               "; Ishbosheth: cooldown; readied by its key, set off by her next shot: every enemy within ish_cone degrees of her aim\n"
               "; (each side) out to ish_range m (the ish_targets nearest) takes a round of ish_mult times her gun's damage, and\n"
               "; ish_spray rounds sweep the fan.\n"
               "ish_cooldown = %g\nish_mult = %g\nish_cone = %g\nish_range = %g\nish_targets = %d\nish_spray = %d\n"
               "; Passives. Lock & Load: an emptied magazine reloads at once (reload_free 1; not her own gun) and her shots deal\n"
               "; reload_buff more for reload_seconds s. Prefect Team Member Judgment: her own gun fires judgment_rate faster.\n"
               "; Out-and-Out: her shots deal oao_bonus more while her head is lower than oao_head m over her feet (crouched, prone).\n"
               "reload_free = %d\nreload_buff = %g\nreload_seconds = %g\njudgment_rate = %g\noao_bonus = %g\noao_head = %g\n"
               "; The options screen's key (120: F9; 0: none).\noptions_key = %d\n",
            VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, s_gunBase, s_gunRounds, (double)s_gunRpm, s_gunAp, (double)s_gunCooldown,
            (double)s_gunGrace, (double)s_shieldMax, (double)s_shieldEvery, (double)s_shieldDecay, (double)s_llCooldown, (double)s_llErgonomics,
            s_llRecoil, (double)s_ishCooldown, (double)s_ishMult, (double)s_ishCone, (double)s_ishRange, s_ishTargets, s_ishSpray, s_reloadFree,
            (double)s_reloadBuff, (double)s_reloadSeconds, (double)s_judgmentRate, (double)s_oaoBonus, (double)s_oaoHead, s_optionsKey);
}

/* ---- her gun, its rounds, Ishbosheth's rounds: copies of her own (index 0 the Maxigun's, 1 the MG-43's) --------- */
static const uint64_t kBaseEntity[2] = {HF_ENTITY_M1000_MAXIGUN, HF_ENTITY_MG43_MACHINE_GUN};
static const uint32_t kBaseRound[2] = {HF_PROJECTILE_M1000_MAXIGUN, HF_PROJECTILE_MG43_MACHINE_GUN};
static const uint32_t kBaseDamage[2] = {HF_DAMAGE_M1000_MAXIGUN, HF_DAMAGE_EXO45_PATRIOT_EXOSUIT_RIGHT_GUN_AND_1_OTHER};
static const char* const kBaseName[2] = {"M-1000 Maxigun", "MG-43 Machine Gun"};
static uint32_t g_round[2], g_damage[2], g_ishRound[2], g_ishDamage[2];
static uint64_t g_gunRes[2];
static int g_rowsReady, g_entsReady;
static int g_owned = -1;               /* the Maxigun: -1 not asked yet, 0 not owned (or unknown), 1 owned */
static int g_use;                      /* which copy this session uses */
static int g_tuned;                    /* Lock & Load's handling is in her gun's records (the next one built has it) */

static void Gun(void)
{
    if (g_owned < 0)
    {
        HfFn_HasCatalogueItem has = hf_fn_HasCatalogueItem(self);
        void* progression = hf_global_Progression(self);
        g_owned = has && progression ? (has(progression, HF_ITEM_M1000_MAXIGUN) ? 1 : 0) : 0;
        Log("the M-1000 Maxigun: %s", has && progression ? (g_owned ? "owned: her gun is its copy" : "not owned: her gun is the MG-43's copy")
                                                          : "its ownership can't be read: her gun is the MG-43's copy");
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
    if (g_round[i]) { HfField pf = HF_FIELD(ProjectileWeapon, projectile); HfValue pv = HF_U32(g_round[i]); proj = hf->set_live(self, res, &pf, &pv, 0) != 0; }
    HfField cap = HF_FIELD(WeaponMagazine, capacity);
    HfValue vc = HF_U32(s_gunRounds);
    mag = hf->set_live(self, res, &cap, &vc, 0) != 0;
    for (int k = 0; k < 32 && g_round[i]; k++)
    {
        HfField f = HF_FIELD_AT(WeaponMagazine, pattern, k);
        HfLiveValue cur;
        memset(&cur, 0, sizeof cur);
        if (!hf->get_live(self, res, &f, &cur) || (uint32_t)cur.now.number != kBaseRound[i]) continue;
        HfValue v = HF_U32(g_round[i]);
        pat += hf->set_live(self, res, &f, &v, 0) != 0;
    }
    HfField fp = HF_FIELD(WeaponMagazine, first_projectile);
    HfLiveValue cf;
    memset(&cf, 0, sizeof cf);
    if (g_round[i] && hf->get_live(self, res, &fp, &cf) && (uint32_t)cf.now.number == kBaseRound[i]) { HfValue v = HF_U32(g_round[i]); hf->set_live(self, res, &fp, &v, 0); }
    Log("her gun (%s's copy, entity %016llx): fire rate %.0f in %d of 3 slots, %d rounds %s, its round %u %s (and in %d pattern slots)", kBaseName[i],
        (unsigned long long)res, (double)rpm, rate, s_gunRounds, mag ? "set" : "REFUSED", g_round[i], proj ? "set" : "REFUSED", pat);
}
/* Lock & Load's handling on her gun's records (on: +ll_ergonomics, no recoil; off: back to its own) */
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
        HfValue v = HF_F32(base + s_llErgonomics);
        ok += hf->set_live(self, res, &erg, &v, 0) != 0;
        if (s_llRecoil) for (int k = 0; k < 6; k++) { HfValue z = HF_F32(0); ok += hf->set_live(self, res, &recoil[k], &z, 0) != 0; }
        Log("Lock & Load: her gun's handling: ergonomics %.0f -> %.0f%s (%d fields set)", base, base + s_llErgonomics, s_llRecoil ? ", no recoil" : "", ok);
    }
    else
    {
        hf->set_live(self, res, &erg, NULL, 0);
        for (int k = 0; k < 6; k++) hf->set_live(self, res, &recoil[k], NULL, 0);
        Log("her gun's handling back to its own");
    }
    g_tuned = on;
}
static void EntitiesReady(HfMod* mod, HfEvent event, void* user)
{
    for (int i = 0; i < 2; i++) GunRecords(i);
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
typedef void (*TakeFn)(void* inventory, uint32_t entity, uint32_t item, uint32_t slot, uint8_t flag);
typedef void (*DropFn)(void* inventory, uint32_t entity, uint32_t item, uint8_t flag, uint64_t zero, uint8_t one);
typedef void (*RequestFn)(void* object);
static TakeFn g_take;
static DropFn g_drop;
static RequestFn g_request;
static uint8_t g_context[0x8A0];
static uint64_t g_package[2];
static int g_pkg[2];
static uintptr_t InventoryRecord(uint32_t e)
{
    uintptr_t m = (uintptr_t)hf_global_InventoryManager(self), records;
    uint32_t i;
    if (!m || !Lookup(m + 0x28, e, 1048576, &i) || i >= 65536 || !(records = Pointer(m + 0x50))) return 0;
    return records + 0x30ull * i;
}
static uint64_t ResOf(uint32_t e) { uint8_t d[24]; return e && Descriptor(e, d) ? Quad(d) : 0; }
static uintptr_t SwitchObject(uint32_t e)
{
    uintptr_t am = (uintptr_t)hf_global_AvatarManager(self), obj;
    uint32_t i, who = 0;
    if (!am || !Lookup(am + 0xF8, e, 1048576, &i) || i >= 1024) return 0;
    obj = am + 0x53DCD0 + (uintptr_t)0x1238 * i;
    return ReadAt(obj, &who, 4) && who == e ? obj : 0;
}
static int Ask(uint32_t selection, const char* why)
{
    uintptr_t obj = SwitchObject(atomic_load(&g_avatar));
    if (!obj || !g_request || hf->write(obj + 4, &selection, 4)) { Log("the switch request could not be made (%s)", why); return 0; }
    g_request((void*)obj);
    return 1;
}
static uint32_t SpawnRes(uint64_t res, const char* what)
{
    HfFn_RequestEntitySpawn spawn = hf_fn_RequestEntitySpawn(self);
    float p[3];
    uint32_t out = 0;
    if (!spawn || !PlayerPose(p, NULL)) { Log("%s could not be asked for", what); return 0; }
    struct { uint32_t one, network; float pose[16]; void* context; uint64_t pad; } info;
    _Static_assert(sizeof info == 0x58, "the spawn record is 0x58 bytes");
    memset(&info, 0, sizeof info);
    memset(g_context, 0, sizeof g_context);
    info.one = 1;
    info.network = 0x7FFF;
    info.pose[0] = info.pose[5] = info.pose[10] = info.pose[15] = 1.0f;
    info.pose[12] = p[0]; info.pose[13] = p[1]; info.pose[14] = p[2] + 1.0f;
    info.context = g_context;
    spawn(&out, res, &info);
    if (!out || out == UINT32_MAX) { Log("%s: no entity came back", what); return 0; }
    return out;
}
static int Rounds(uint32_t weapon)
{
    HfFn_MagazineRounds rounds = hf_fn_MagazineRounds(self);
    void* wm = hf_global_WeaponManager(self);
    return rounds && wm && weapon ? rounds(wm, weapon) : -1;
}
static int Capacity(uint32_t weapon)
{
    HfFn_MagazineCapacity capacity = hf_fn_MagazineCapacity(self);
    uint32_t stride = 0;
    uint64_t res = ResOf(weapon);
    return capacity && res && hf->component_record("WeaponMagazineComponentData", res, &stride) ? capacity(NULL, weapon) : -1;
}
/* a weapon's magazine to its capacity and its spares in full */
static void FillUp(uint32_t weapon)
{
    HfFn_AddMagazineRounds add = hf_fn_AddMagazineRounds(self);
    HfFn_WeaponRefill refill = hf_fn_WeaponRefill(self);
    if (!weapon || !ResOf(weapon)) return;
    int have = Rounds(weapon), cap = Capacity(weapon);
    if (add && have >= 0 && cap > have) add(NULL, weapon, cap - have);
    if (refill) for (int i = 0; i < 6; i++) refill(0, weapon, 1.0f);
}

enum { K_IDLE, K_PACKAGE, K_WAITGL, K_PUTAWAY, K_TAKE, K_ASK, K_HELD, K_LETGO, K_REMOVE, K_WAITS };
static int g_k;
static atomic_uint g_gl;                  /* her gun (0: none) */
static double g_kAt, g_madeAt, g_readyAt, g_awaySince, g_droppedSince;
static uint32_t g_kSel, g_kOld, g_kNew, g_swapOld;   /* her selection at the start; the support weapon she had; its replacement; the gun a swap replaces */
static uint64_t g_kOldRes;
static int g_swap;                        /* Lock & Load is putting a new gun (with its handling) in place of this one */
static unsigned g_uses, g_swaps;
static uint64_t GunRes(void) { return g_gunRes[g_use]; }
static void GunStart(void)
{
    uint32_t me = atomic_load(&g_avatar), slots[8] = {0};
    uintptr_t rec;
    if (!g_take || !g_drop || !g_request) { Log("Destroyer of Denouement: off (the game's code is not the reviewed one)"); return; }
    Gun();
    if (!GunRes()) { Log("Destroyer of Denouement: her gun was not added (the log's start says why)"); return; }
    if (!(rec = InventoryRecord(me)) || !ReadAt(rec, slots, sizeof slots) || !SwitchObject(me)) { Log("Destroyer of Denouement: her inventory or her switch object was not found: nothing done"); return; }
    Handling(0);   /* a new call: her gun as it is (Lock & Load's handling is for the gun it was used on) */
    g_kSel = slots[7] >= 1 && slots[7] <= 3 ? slots[7] : 1;
    g_kOld = slots[2];
    g_kOldRes = ResOf(g_kOld);
    g_kNew = 0;
    g_swap = 0;
    g_uses++;
    Log("Destroyer of Denouement %u: the %s's copy; her selection %u, her support weapon %u (%016llx)", g_uses, kBaseName[g_use], g_kSel, g_kOld, (unsigned long long)g_kOldRes);
    g_k = K_PACKAGE;
    g_kAt = g_now + 8;
}
static void GunEnd(const char* why)
{
    if (g_k != K_HELD) return;
    Log("Destroyer of Denouement %u over (%s): %d rounds left", g_uses, why, Rounds(atomic_load(&g_gl)));
    Ask(g_kSel == 3 ? 1 : g_kSel, "her own weapon back in hand");
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
    uint32_t me = atomic_load(&g_avatar), gl = atomic_load(&g_gl), now = 0;
    uint64_t res = GunRes();
    uintptr_t rec;
    if (g_k == K_IDLE) return;
    if (!g_alive || !im || !ResOf(me) || !(rec = InventoryRecord(me))) { GunAbort("she is down or gone"); return; }
    switch (g_k)
    {
    case K_PACKAGE:
        if (!g_package[g_use])
        {
            uint32_t stride = 0;
            uintptr_t r = hf->component_record("LoadoutPackageComponentData", kBaseEntity[g_use], &stride);
            if (!r || !ReadAt(r + 8, &g_package[g_use], 8) || !g_package[g_use]) g_package[g_use] = ~0ull;
        }
        if (g_package[g_use] != ~0ull && g_pkg[g_use] != HF_PACKAGE_RESIDENT)
        {
            g_pkg[g_use] = hf->load_package(self, g_package[g_use]);
            if (g_pkg[g_use] == HF_PACKAGE_LOADING) { if (g_now > g_kAt) GunAbort("its package did not load"); break; }
            if (!g_pkg[g_use]) g_package[g_use] = ~0ull;
        }
        gl = SpawnRes(res, "her gun");
        if (!gl) { GunAbort("no gun"); break; }
        atomic_store(&g_gl, gl);
        g_madeAt = g_now;
        g_k = K_WAITGL;
        break;
    case K_WAITGL:
        /* its spawn has to finish before it is taken (taken in its spawn frame it stayed where it was made) */
        if (g_now < g_madeAt + 0.75) break;
        if (ResOf(gl) != res) { GunAbort("her gun does not read as one"); break; }
        if (g_swap || (g_kSel == 3 && g_kOld)) { Ask(1, "a support weapon is in her hands: put away first"); g_k = K_PUTAWAY; g_kAt = g_now + 0.8; }
        else g_k = K_TAKE;
        break;
    case K_PUTAWAY:
        if (g_now >= g_kAt) g_k = K_TAKE;
        break;
    case K_TAKE:
        g_take(im, me, gl, 3, 0);
        g_k = K_ASK; g_kAt = g_now + 0.1;
        break;
    case K_ASK:
    {
        HfFn_AddMagazineRounds add = hf_fn_AddMagazineRounds(self);
        if (g_now < g_kAt) break;
        /* what the take dropped: her own support weapon (the first call) or the gun a swap replaces */
        if (!g_swap && g_kOld && ResOf(g_kOld) == g_kOldRes && remove && em) remove(em, g_kOld);
        if (g_swap && g_swapOld && ResOf(g_swapOld) == res && remove && em) remove(em, g_swapOld);
        Ask(3, "her gun in hand");
        int have = Rounds(gl);
        if (add && have >= 0 && have < s_gunRounds) add(NULL, gl, s_gunRounds - have);
        g_k = K_HELD;
        g_awaySince = g_droppedSince = 0;
        Log("Destroyer of Denouement %u%s: gun %u, %d rounds (capacity %d)", g_uses, g_swap ? ", Lock & Load's new one" : " on", gl, Rounds(gl), Capacity(gl));
        g_swap = 0;
        g_swapOld = 0;
        break;
    }
    case K_HELD:
    {
        uint32_t sel = 0, support = 0;
        if (ResOf(gl) != res) { GunAbort("her gun is gone"); break; }
        if (Rounds(gl) == 0) { GunEnd("its rounds are spent"); break; }
        ReadAt(rec + 0x1C, &sel, 4);
        ReadAt(rec + 8, &support, 4);
        /* dropped: no longer in her support slot (a moment's grace: the slot is rewritten on a switch) */
        if (support == gl) g_droppedSince = 0;
        else if (!g_droppedSince) g_droppedSince = g_now;
        else if (g_now - g_droppedSince > 0.3)
        {
            Log("Destroyer of Denouement %u over (she dropped it): %d rounds left", g_uses, Rounds(gl));
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
        g_drop(im, me, gl, 0, 0, 1);
        if (ReadAt(rec + 8, &now, 4) && now == gl) { now = 0; hf->write(rec + 8, &now, 4); }
        g_k = K_REMOVE; g_kAt = g_now + 0.4;
        break;
    case K_REMOVE:
        if (g_now < g_kAt) break;
        if (gl && ResOf(gl) == res && remove && em) remove(em, gl);
        atomic_store(&g_gl, 0);
        if (g_kOldRes) { g_kNew = SpawnRes(g_kOldRes, "a new support weapon of her kind"); g_k = g_kNew ? K_WAITS : K_IDLE; g_kAt = g_now + 0.75; }
        else { FillUp(g_weapon[g_kSel]); g_k = K_IDLE; }
        break;
    case K_WAITS:
        if (g_now < g_kAt) break;
        if (ResOf(g_kNew) == g_kOldRes)
        {
            g_take(im, me, g_kNew, 3, 0);
            FillUp(g_kNew);
            if (g_kSel == 3) Ask(3, "she had it in hand when the gun came");
            else FillUp(g_weapon[g_kSel]);
            Log("Destroyer of Denouement %u: her new support weapon %u taken", g_uses, g_kNew);
        }
        else Log("Destroyer of Denouement %u: the new support weapon does not read as hers: left", g_uses);
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
static void LockAndLoad(void)
{
    if (g_now < g_cd[1]) return;
    uint32_t w = Held(NULL);
    if (!w) { Log("Lock & Load: no gun in her hands"); return; }
    g_cd[1] = g_now + s_llCooldown;
    g_casts[1]++;
    uint32_t gl = atomic_load(&g_gl);
    if (gl && w == gl && g_k == K_HELD && (s_llErgonomics > 0 || s_llRecoil)) { GunSwap(); return; }
    int before = Rounds(w);
    FillUp(w);
    if (gl && w == gl) { HfFn_AddMagazineRounds add = hf_fn_AddMagazineRounds(self); int have = Rounds(w); if (add && have >= 0 && have < s_gunRounds) add(NULL, w, s_gunRounds - have); }
    Log("Lock & Load: her gun %u refilled (%d -> %d rounds)", w, before, Rounds(w));
}

/* ---- Ishbosheth (C) ---------------------------------------------------------------------------------------------- */
static int g_ishArmed;
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
static void Ishbosheth(void)
{
    if (g_ishArmed || g_now < g_cd[2]) return;
    g_ishArmed = 1;
    g_ishHeld = 0;
    g_ishRounds = -1;
    Log("Ishbosheth: readied (her next shot sets it off)");
}
static void IshFire(void)
{
    float cam[3], fwd[3], head[3], q[3];
    if (!View(cam, fwd) || !HerHead(head)) return;
    uint32_t round = g_ishRound[g_use] ? g_ishRound[g_use] : g_round[g_use], spray = g_round[g_use];
    float flat = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1]);
    if (!(flat > 0.05f) || !round) return;
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
    int hit = 0, sprayed = 0;
    for (int i = 0; i < n; i++)
    {
        float d[3] = {t[i].q[0] - head[0], t[i].q[1] - head[1], t[i].q[2] - head[2]};
        hit += Shoot(round, head, d);
    }
    /* the sweep: rounds of her gun across the fan, level with her aim */
    for (int i = 0; i < s_ishSpray && spray; i++)
    {
        float a = s_ishSpray > 1 ? (-1.0f + 2.0f * (float)i / (float)(s_ishSpray - 1)) * s_ishCone * 0.0174533f : 0, ca = cosf(a), sa = sinf(a);
        float d[3] = {fwd[0] * ca - fwd[1] * sa, fwd[0] * sa + fwd[1] * ca, fwd[2]};
        sprayed += Shoot(spray, head, d);
    }
    g_ishHits += (unsigned)hit;
    g_ishSprayed += (unsigned)sprayed;
    g_ishArmed = 0;
    g_cd[2] = g_now + s_ishCooldown;
    g_casts[2]++;
    Log("Ishbosheth: %d of %d enemies in the fan (%.0f degrees each side, %.0f m) take a round of %.2f times her gun's; %d sweep rounds", hit, n,
        (double)s_ishCone, (double)s_ishRange, (double)s_ishMult, sprayed);
}
/* readied: her next shot (the gun in her hands loses a round) sets it off; the left button too, for a gun without a
 * magazine to read */
static void IshFrame(void)
{
    if (!g_ishArmed) return;
    uint32_t w = Held(NULL);
    int r = w ? Rounds(w) : -1;
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
    int r = w ? Rounds(w) : -1;
    if (w && w == g_llHeld && r == 0 && g_llRounds > 0)
    {
        if (w != gl && s_reloadFree)
        {
            HfFn_AddMagazineRounds add = hf_fn_AddMagazineRounds(self);
            int cap = Capacity(w);
            if (add && cap > 0) { add(NULL, w, cap); r = Rounds(w); }
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
    g_buffUntil = 0;
    g_uses = g_swaps = g_refills = g_ishHits = g_ishSprayed = g_reloads = g_buffed = 0;
    g_buffDamage = 0;
    g_lastTarget = 0;
    memset(g_foe, 0, sizeof g_foe);
    memset(g_foeRes, 0, sizeof g_foeRes);
}
static void Pin(void** slot, uintptr_t at, const char* pattern, const char* what)
{
    if (hf->code_matches(at, pattern)) *slot = (void*)at;
    else Log("%s is not the reviewed code: Destroyer of Denouement is off", what);
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gameSize = 0, exeSize = 0;
    uintptr_t g = hf->module_base("game.dll", &gameSize), x = hf->module_base("helldivers2.exe", &exeSize);
    if (!g || !x || gameSize != 74727424u || exeSize != 60719104u) { Log("REFUSED: the game's modules are not build " BUILD "'s: her kit stays off"); return; }
    game = g;
    exe = x;
    Pin((void**)&g_take, game + 0x9AE030, "40 53 55 41 55 48 83 ec 40 48 89 7c 24 68 41 8b e8 4c 89 64 24 70 8b da", "the inventory's pickup (0x9AE030)");
    Pin((void**)&g_drop, game + 0x9A8840, "40 53 56 41 56 48 81 ec a0 00 00 00 48 8b 05 bd 37 c9 01 48 33 c4 48 89", "the inventory's drop (0x9A8840)");
    Pin((void**)&g_request, game + 0xA93430, "48 89 5c 24 18 48 89 6c 24 20 56 57 41 54 41 56 41 57 48 83 ec 50 48 8b 05 c3", "the Helldiver's switch request (0xA93430)");
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
    int r = g_k == K_HELD ? Rounds(atomic_load(&g_gl)) : 0;
    g_hud.rounds = (uint32_t)(r > 0 ? r : 0);
    g_hud.rounds_max = (uint32_t)s_gunRounds;
    g_hud.shield = (float)ShieldNow();
    g_hud.shield_max = s_shieldMax;
    g_hud.ish_armed = (uint32_t)g_ishArmed;
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
    GunFrame();
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
    {"gun_grace", &s_gunGrace, NULL}, {"gun_base", NULL, &s_gunBase},
    {"shield_max", &s_shieldMax, NULL}, {"shield_every", &s_shieldEvery, NULL}, {"shield_decay", &s_shieldDecay, NULL},
    {"ll_cooldown", &s_llCooldown, NULL}, {"ll_ergonomics", &s_llErgonomics, NULL}, {"ll_recoil", NULL, &s_llRecoil},
    {"ish_cooldown", &s_ishCooldown, NULL}, {"ish_mult", &s_ishMult, NULL}, {"ish_cone", &s_ishCone, NULL}, {"ish_range", &s_ishRange, NULL},
    {"ish_targets", NULL, &s_ishTargets}, {"ish_spray", NULL, &s_ishSpray},
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
        ish.scale = s_ishMult;
        g_ishDamage[i] = api->add_damage(mod, &ish);
        if (g_damage[i])
        {
            HfProjectile p = {sizeof p};
            p.base = kBaseRound[i];
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
