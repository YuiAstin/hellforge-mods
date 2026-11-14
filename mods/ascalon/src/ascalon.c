/* Ascalon (Arknights) for HellForge: the always-hidden kit. Design: docs/ascalon-design.md.
 *
 * 0.1.0 is the core only, "hidden until she makes noise":
 *   - Hidden: enemies' visual detection of her is cut (the game's per-entity visibility row, as the CAMO-EX01 Optical
 *     Cloak Pack found it), whoever still targets her forgets her (Lost Contact's break contact, without its wait), her
 *     body and gear are not drawn (the HUD side: Unit.set_mesh_visibility on her units, Cloak Probe 0.1.14).
 *   - What reveals her is the game's own senses (0.1.1, the user's idea): an enemy whose perception entry of her has
 *     sense bits set senses her now (it heard a shot or a blast, or she is right in front of it), and keeps her; only
 *     the ones that hold her without sensing her forget her. No invented noise ranges. While anyone senses her (and
 *     reveal_hold s after) she is drawn and the dodge is off.
 *     0.1.2: the sense bits turned out to be sight only (two test missions: "sense bits 1" at 1 to 27 m, never from a
 *     shot). Hearing shows as the user said it would: an enemy that takes her as its target, or gains an entry of her,
 *     without seeing her. Such an acquisition within noise_window s of her last shot is "it heard her": that enemy
 *     keeps her for heard_hold s. The same without a shot (stale memory, squad chatter, a melee hit) is forgotten.
 *   - The user's rule: with a backpack on, or a support weapon in her hands, there is no functional hiding at all.
 *   - Ambusher: while hidden, half the hits that still land on her miss (dodge).
 * 0.2.0, her trait and her first skill (Yui, 2026-11-14: "hit everything should always be on, in a radius of 15 m";
 * Pursuit without the gas: "the slow, AoE damage, double hits and the multiplier, AoE as the Executor's"):
 *   - Ambusher (always on): every hit of hers (melee, bullet, beam) also strikes every living enemy within area_radius m
 *     of her that she has a clear line to, for area_mult times that hit's damage. As the Executor's area rounds: one
 *     spawned round an enemy, its hit's amount set in the damage event. At most one volley every area_every s, area_max
 *     enemies. The rounds are the mod's, not her gun's: they are not noise.
 *   - Pursuit (S1, by itself, pursuit_charges charges, one back every pursuit_every s): her next hit deals pursuit_mult
 *     times its damage and lands twice (a second volley that takes in the enemy she hit), on everyone in range, and
 *     slows each one it strikes (the flamer slow, pursuit_slow build-up).
 * 0.2.1:
 *   - Mortal Interrogation (passive): each hit of hers, and each strike, puts a stack on its target (mi_stacks at most),
 *     for mi_seconds from the last one. A marked enemy takes mi_damage times the marking hit's damage a second for each
 *     stack, and a dose of the slow (mi_slow a stack). The damage is a round of the mod's spawned beside the enemy (no
 *     line from her needed), its amount set in the damage event.
 *   - Damage numbers (the HUD, as the Executor's and Lemuen's): every hit, strike and tick of hers, Pursuit's in orange.
 * 0.2.2 (Yui: "there's a bleed damage DoT that should fit Ascalon nicely"): the mark's damage is the game's own bleed
 * (status 2 of HD2Runtime's status catalogue; mi_status), a dose a second for each stack (mi_status_buildup). If the game
 * refuses it, or with mi_status 0, the spawned-round tick of 0.2.1 does it instead.
 * 0.3.0, her two keyed skills:
 * 0.4.12 (Yui: "constantly teleporting upward even on flat road that has nothing in front"; the log: every dash on
 *     that road from 7.5 to 9.59): a level sheet 2 m over the road, met only from above, made the road read as buried
 *     and was taken as the spot. What is over her own spot and is not a roof is left out of the spots (g_phantom).
 * 0.4.11 (Yui: "the ticks just literally miss the target as they get knocked around"; the log: 10 of 43 never
 *     landed): a tick whose round has not landed 0.15 s on is sent again, from straight above, then from the far
 *     side: up to three rounds a tick.
 * 0.4.10 (Yui: "feels like the bleed only applies to 3 targets max"): nothing caps the marks at three (64 marks, 8
 *     ticks a frame); the likeliest loss is the tick's round, sent level from 2.5 m on her side, taken by another
 *     enemy of the crowd. It is sent from 1.6 m out, above the marked enemy, and the log says every 10 s how many
 *     enemies are marked and how many ticks were sent and landed.
 * 0.4.9 (Yui: "dashes still get refused and the parry sound is still just as loud with that default 0.6"): the look
 *     for something in the way met her own body and something else of hers (the log named their filters): bodies and
 *     that filter are no walls now. The clash's volume is squared (a loudness scale) and parry_volume is 0.4.
 *     In game the Warp Pack's fit test answered (two parries, both "fits"), without trouble.
 * 0.4.7 (Yui: "0.4.5 refuses to do any dashes even on open flat ground"): two of 0.4.5's checks were wrong. The look
 *     for something in the way started inside her and met her own gear: it starts 0.7 m ahead of her now, and logs
 *     what it meets. The look for the spot from 40 m up met something high over open ground and read the ground
 *     under it as buried: from 12 m again, as 0.4.4 did.
 * 0.4.6 (Yui: "have an agent look at the dump for you"): a fifth check before she is set anywhere, the game's own:
 *     the Warp Pack takes a destination only when game.dll+0x68C340 says the wearer's mover shape overlaps nothing
 *     there (and nothing is above it, looking down from 500 m, as SafeSpot's first check does from 40). GameFits calls
 *     it, after reading everything it would read (it checks nothing itself); game_fit 0 turns it off.
 * 0.4.5 (Yui: "the dash now feels incredibly random because standing still dashes you as well even while not stuck,
 *     can't we just do multiple checks before teleporting so the player doesn't get stuck in the first place"):
 *     the dash is only while she moves again (the way out of a stuck spot is gone), and a spot is taken only after
 *     four checks (SafeSpot): a real surface she fits on, seen from 40 m above; at most 0.6 m up per metre and 3 m
 *     down; nothing solid between her and it at hip and head height, both ways; and its ground under her feet there.
 *     The parry's step behind the attacker takes the same checks.
 *     Sound: the parry's clash is played through Windows from the mod's sounds folder (parry_sound, ParrySound); the
 *     bank patch is out of data/ (it never played, and it replaced two of the game's banks and its event metadata).
 * 0.4.4 (Yui: "more prone to teleport me underground while walking up hill and no audio still"):
 *   - Underground: a dash uphill looked for its spot from 2.5 m above her, under the hill's surface 6 m ahead (terrain
 *     is solid only from above), and took a buried surface; walking uphill slower than 2 m/s the melee press ran
 *     0.4.3's way out of a stuck spot instead, onto whatever was highest within 40 m. Spots are now looked for from
 *     12 m up (the parry's too), a dash rises at most as far as it goes, the way out wants her in place for a second,
 *     and from under the ground it goes straight up onto the ground above her first.
 *   - Sound: the event is known to the game now, but the sound engine still did not post it, so the bank it was added
 *     to was not loaded or not taken. The event is now in two banks (the Hellbomb's and the shared stratagem bank),
 *     and the first parry logs what the same two ways answer for sounds of the game's own.
 * 0.4.3 (Yui: "doesnt have the sound still", "the teleport doesnt do damage to enemies on the path", "she can get
 *     stuck under building with no way to teleport back out since it needs velocity"):
 *   - Sound: the bank was never the problem to see: the Wwise plugin's has_event answers from the game's event
 *     metadata (wwise_metadata 0x11AB54001F79F739), not from the loaded banks, and hd2.sounds refuses an event it
 *     does not list. The mod's data now also carries that resource with a record for ascalon_parry, and the HUD
 *     script posts the event itself when HD2Runtime refuses it.
 *   - Dash: its strikes have a list of their own (the melee hit of the same press set off an area strike that wiped
 *     the shared one, so the dash's rounds landed with their own small amount); the path is a metre wider and reaches
 *     the enemy she stops in front of.
 *   - Stuck: standing still, the melee press dashes her out of a spot she cannot walk out of (under the ground, or no
 *     room to stand where she has been for 3 s): towards where she looks, else any way, onto any height.
 * 0.4.2 (Yui: "fix the sound and add proper UI and mods options"):
 *   - The parry's sound: 0.4.1's bank of its own was never loaded (no package lists it; Wwise.load_bank only takes a
 *     resident resource), so the event and the recordings are now added to a bank the game always has loaded (the
 *     Hellbomb objective's, obj_hellbomb: tools/build_bank.py), shipped as that bank's replacement in data/.
 *   - Her skills beside the stratagem list as the other kits show theirs (Pursuit's charges, Endowment, Descent: key,
 *     time left, cooldown), key bindings in the game's own binding pages (ascalon_input / ascalon_key: Endowment,
 *     Descent, the dash), and her settings as rows on the MODS tab (ascalon_option_get / _set, as before).
 * 0.4.1 (Yui: "give the parry a sound"; "look at what other mods in HellForge do"): the parry plays a clash, one of
 *     nine of Yui's recordings, from a sound bank of the mod's own (data/007e093ca718ca1a.patch_0, built by
 *     tools/build_bank.py: the CQC-2 Saber bank's structure under new ids with the recordings as PCM media, the way
 *     Scope of Authority's Nova bank is the Quasar's with the Nova's recordings). ascalon_parry_seq counts parries;
 *     the HUD script loads the bank (Wwise.load_bank) and posts the event (ascalon_parry) when the count rises.
 * 0.4.0 (Yui: "give her her parry since she is a melee fighter too", and the dash):
 *   - Parry (Texas's, from the Parry Probe 0.1.5): with a dedicated melee weapon in hand, a right-click press within
 *     parry_window s before a hit lands is a perfect parry: the hit is nulled, she takes no hit for parry_invuln s, she is
 *     set behind the attacker, it is stunned, and her counter strikes everyone in range for parry_damage.
 *   - Dash: while she is moving, the melee key (melee_key_code, 70: F), or a left click with a dedicated melee weapon in
 *     hand, dashes her dash_distance m the way she moves (Voymastina's landing checks) and strikes every enemy within
 *     dash_width m of the path for dash_damage. Once every dash_cooldown s.
 *   - Endowment (S2, key_endowment, default X; e_seconds, then e_cooldown): her hits deal e_mult times their damage
 *     (strikes with them); every enemy within e_radius m of her gets a dose of the slow (e_slow) twice a second; an
 *     enemy that dies within 30 m of her leaves a Mortal Interrogation stack on every enemy within e_spread m of it.
 *   - Descent (S3, key_descent, default C; d_seconds, then d_cooldown): she steps out of hiding on purpose. No hit on
 *     her lands (the hit hook nulls each one), her hits deal d_mult times their damage, and each enemy that dies within
 *     30 m of her adds d_per_kill s, up to d_cap s left. While it lasts she is drawn, enemies see her as anyone, and
 *     nobody forgets her. It ends with her hidden again after d_grace s.
 *
 * Built on the kit base (src/kitbase). Solo or host. */
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

#define VERSION "0.4.12"
#define BUILD "25480438"

#include "kitbase/kb_core.h"
#include "kitbase/kb_player.h"
#include "kitbase/kb_friends.h"
#include "kitbase/kb_body.h"
#include "kitbase/kb_hits.h"
#include "kitbase/kb_damage.h"
#include "kitbase/kb_settings.h"
#include "kitbase/kb_roster.h"

KB_ROSTER_EXPORTS(ascalon)

/* ---- settings ------------------------------------------------------------------------------------------------- */
static float s_areaRadius = 15, s_areaMult = 1, s_areaEvery = 0.25f, s_pursuitMult = 2.1f, s_pursuitEvery = 7, s_pursuitSlow = 100;
static int s_areaMax = 12, s_areaRound = 7, s_pursuitCharges = 3;
static float s_miSeconds = 25, s_miDamage = 0.10f, s_miSlow = 18;
static float s_eSeconds = 35, s_eCooldown = 20, s_eMult = 2.3f, s_eRadius = 15, s_eSlow = 60, s_eSpread = 5;
static float s_dSeconds = 5, s_dCooldown = 45, s_dMult = 1.5f, s_dPerKill = 2, s_dCap = 15, s_dGrace = 2;
static float s_parryVolume = 0.4f, s_parryWindow = 0.3f, s_parryInvuln = 1, s_parryDamage = 300, s_parryBehind = 2.2f, s_parryStun = 300;
static float s_dashDistance = 6, s_dashCooldown = 1.2f, s_dashDamage = 150, s_dashWidth = 2;
static int s_parry = 1, s_dash = 1, s_meleeVk = 70, s_parrySound = 1, s_gameFit = 1;
static int s_miStacks = 3, s_numbers = 1, s_miStatus = 2;
static float s_miBuildup = 100;
static float s_visualCut = 95, s_revealHold = 1.5f, s_noiseWindow = 1.0f, s_heardHold = 5;
static float s_dodge = 0.5f, s_forgetEvery = 0.5f, s_forgetRange = 150;
static int s_cloakVisual = 1, s_hideGear = 1, s_gearRule = 1;

static const KbSetting kSettings[] = {
    KB_I("parry", s_parry, 0, 1), KB_I("parry_sound", s_parrySound, 0, 1), KB_F("parry_volume", s_parryVolume, 0, 2), KB_I("game_fit", s_gameFit, 0, 1), KB_F("parry_window", s_parryWindow, 0.05, 2), KB_F("parry_invuln", s_parryInvuln, 0, 10), KB_F("parry_damage", s_parryDamage, 0, 100000),
    KB_F("parry_behind", s_parryBehind, 0, 10), KB_F("parry_stun", s_parryStun, 0, 1000), KB_I("dash", s_dash, 0, 1), KB_F("dash_distance", s_dashDistance, 1, 20),
    KB_F("dash_cooldown", s_dashCooldown, 0, 60), KB_F("dash_damage", s_dashDamage, 0, 100000), KB_F("dash_width", s_dashWidth, 0.5, 10), KB_I("melee_key_code", s_meleeVk, 1, 255),
    KB_F("e_seconds", s_eSeconds, 1, 300), KB_F("e_cooldown", s_eCooldown, 0, 600), KB_F("e_mult", s_eMult, 1, 20), KB_F("e_radius", s_eRadius, 0, 60),
    KB_F("e_slow", s_eSlow, 0, 1000), KB_F("e_spread", s_eSpread, 0, 30), KB_F("d_seconds", s_dSeconds, 1, 120), KB_F("d_cooldown", s_dCooldown, 0, 600),
    KB_F("d_mult", s_dMult, 1, 20), KB_F("d_per_kill", s_dPerKill, 0, 30), KB_F("d_cap", s_dCap, 1, 120), KB_F("d_grace", s_dGrace, 0, 30),
    KB_F("mi_seconds", s_miSeconds, 0, 120), KB_F("mi_damage", s_miDamage, 0, 5), KB_F("mi_slow", s_miSlow, 0, 1000), KB_I("mi_stacks", s_miStacks, 0, 9),
    KB_I("numbers", s_numbers, 0, 1), KB_I("mi_status", s_miStatus, 0, 200), KB_F("mi_status_buildup", s_miBuildup, 0, 1000),
    KB_F("area_radius", s_areaRadius, 0, 60), KB_F("area_mult", s_areaMult, 0, 10), KB_F("area_every", s_areaEvery, 0.05, 5),
    KB_I("area_max", s_areaMax, 0, 24), KB_I("area_round", s_areaRound, 1, 100000), KB_F("pursuit_mult", s_pursuitMult, 1, 20),
    KB_F("pursuit_every", s_pursuitEvery, 0.5, 120), KB_F("pursuit_slow", s_pursuitSlow, 0, 1000), KB_I("pursuit_charges", s_pursuitCharges, 0, 9),
    KB_F("visual_cut", s_visualCut, 0, 99.9), KB_F("reveal_hold", s_revealHold, 0, 60), KB_F("noise_window", s_noiseWindow, 0, 10), KB_F("heard_hold", s_heardHold, 0, 60), KB_F("dodge", s_dodge, 0, 1),
    KB_F("forget_every", s_forgetEvery, 0.1, 5), KB_F("forget_range", s_forgetRange, 10, 500),
    KB_I("cloak_visual", s_cloakVisual, 0, 1), KB_I("hide_gear", s_hideGear, 0, 1), KB_I("gear_rule", s_gearRule, 0, 1),
    KB_F("hp_mult", s_hpMult, 0.1, 10), KB_F("stamina_mult", s_staminaMult, 0.1, 20), KB_I("hellbomb_on_exhaustion", s_hellbomb, 0, 1),
    KB_I("hellbomb_explosion", s_hellbombRow, 0, 100000), KB_F("hellbomb_hold", s_hellbombHold, 0, 10),
};

/* The default ascalon.ini: tools/inigen.py writes the file from this text (the mod never writes it). */
static __attribute__((unused)) void DefaultIni(FILE* f)
{
    fprintf(f, "; Ascalon %s. Keys: a letter, digit, F1-F24, Mouse3, Mouse4 or Mouse5, with Shift+, Ctrl+ or Alt+ in front for a\n"
               "; combo (Shift+Q). (Her skills are not in this build: the keys are kept for them.)\n"
               "key_spare = %s\nkey_endowment = %s\nkey_descent = %s\n"
               "; Ambusher (always on): every hit of hers also strikes every enemy within area_radius m of her that she has a\n"
               "; clear line to, for area_mult times that hit's damage: at most area_max enemies, one volley every area_every s.\n"
               "; area_round: the vanilla projectile the strikes fly as (7: a rifle round).\n"
               "area_radius = %g\narea_mult = %g\narea_max = %d\narea_every = %g\narea_round = %d\n"
               "; Pursuit (by itself): pursuit_charges charges, one back every pursuit_every s. A hit with a charge deals\n"
               "; pursuit_mult times its damage, lands twice on everyone in range and slows them (pursuit_slow: 100 a full dose).\n"
               "pursuit_charges = %d\npursuit_every = %g\npursuit_mult = %g\npursuit_slow = %g\n"
               "; Parry (parry 1): with a dedicated melee weapon in hand (a CQC weapon), right-click within parry_window s before a\n"
               "; hit lands: the hit does nothing, nothing hits her for parry_invuln s, she is set parry_behind m behind the\n"
               "; attacker, it is stunned (parry_stun), and her counter strikes everyone in range for parry_damage.\n"
               "; parry_sound 1 plays a clash on a parry: a .wav of the mod's sounds folder (parry_clash_01.wav to _09.wav; put\n"
               "; your own there, 16-bit PCM), through Windows, at the game's Master x SFX volume times parry_volume.\n"
               "parry = %d\nparry_sound = %d\nparry_volume = %g\nparry_window = %g\nparry_invuln = %g\nparry_damage = %g\nparry_behind = %g\nparry_stun = %g\n"
               "; Dash (dash 1): while she is moving, the melee key (melee_key_code: its Windows key code; 70 is F), or a left\n"
               "; click with a dedicated melee weapon in hand, dashes her dash_distance m the way she moves and strikes every\n"
               "; enemy within dash_width m of the path for dash_damage. Once every dash_cooldown s.\n"
               "; game_fit 1 also asks the game's own fit test (the Warp Pack's) before she is set anywhere; 0 leaves it to\n"
               "; the mod's own checks (set it to 0 if the game crashes on a dash or a parry).\n"
               "dash = %d\nmelee_key_code = %d\ndash_distance = %g\ndash_cooldown = %g\ndash_damage = %g\ndash_width = %g\ngame_fit = %d\n"
               "; Endowment (key_endowment): for e_seconds her hits deal e_mult times their damage, every enemy within e_radius m\n"
               "; of her is slowed (e_slow a dose, twice a second), and an enemy that dies near her leaves a Mortal Interrogation\n"
               "; stack on every enemy within e_spread m of it. Then e_cooldown s before it can be cast again.\n"
               "e_seconds = %g\ne_cooldown = %g\ne_mult = %g\ne_radius = %g\ne_slow = %g\ne_spread = %g\n"
               "; Descent (key_descent): she steps out of hiding: for d_seconds no hit on her lands and her hits deal d_mult\n"
               "; times their damage; each enemy that dies near her adds d_per_kill s (never more than d_cap s left). She is\n"
               "; seen and drawn while it lasts, and hidden again d_grace s after. Then d_cooldown s.\n"
               "d_seconds = %g\nd_cooldown = %g\nd_mult = %g\nd_per_kill = %g\nd_cap = %g\nd_grace = %g\n"
               "; Mortal Interrogation: each hit or strike of hers puts a stack on its target (mi_stacks at most) for mi_seconds\n"
               "; from the last one. A marked enemy takes mi_damage times the marking hit's damage a second for each stack, and\n"
               "; a dose of the slow (mi_slow a stack; 100 a full dose).\n"
               "mi_stacks = %d\nmi_seconds = %g\nmi_damage = %g\nmi_slow = %g\n"
               "; mi_status: the game status that does the mark's damage (2: bleed), a dose of mi_status_buildup a stack every\n"
               "; second (100 a full dose). 0: no status; the mark's damage is mi_damage, above, dealt by the mod itself (as it\n"
               "; is when the game refuses the status).\n"
               "mi_status = %d\nmi_status_buildup = %g\n"
               "; numbers 1: the HUD shows a number over every hit, strike and tick of hers.\nnumbers = %d\n"
               "; Hidden: while she makes no noise, enemies see her visual_cut percent less far, and whoever within forget_range m\n"
               "; still targets her forgets her every forget_every s.\n"
               "visual_cut = %g\nforget_every = %g\nforget_range = %g\n"
               "; What reveals her is the game's own hearing and sight: an enemy that senses her right now (it heard her gun, or\n"
               "; she is right in front of it) keeps her; she counts as revealed while any does, and reveal_hold s after.\n"
               "reveal_hold = %g\n"
               "; Hearing: an enemy that picks her up without seeing her within noise_window s of her last shot heard it, and\n"
               "; keeps her for heard_hold s (the game decides who hears which gun, and how far).\n"
               "noise_window = %g\nheard_hold = %g\n"
               "; gear_rule 1: with a backpack on or a support weapon in her hands she is not hidden at all (she still looks it).\n"
               "gear_rule = %d\n"
               "; Ambusher: while hidden, this share of the hits that still land on her miss (0.5: half).\ndodge = %g\n"
               "; cloak_visual 1: her body is not drawn while hidden (needs Bingus Shared Loader and HD2Runtime); hide_gear 1: nor\n"
               "; are her weapons and backpack.\ncloak_visual = %d\nhide_gear = %d\n"
               "; Her body: hp_mult times a Helldiver's health, stamina_mult times her sprint.\n"
               "hp_mult = 1\nstamina_mult = 1.2\nhellbomb_on_exhaustion = 0\n",
            VERSION, s_keys[0].label, s_keys[1].label, s_keys[2].label, (double)s_areaRadius, (double)s_areaMult, s_areaMax, (double)s_areaEvery,
            s_areaRound, s_pursuitCharges, (double)s_pursuitEvery, (double)s_pursuitMult, (double)s_pursuitSlow, s_parry, s_parrySound, (double)s_parryVolume, (double)s_parryWindow, (double)s_parryInvuln,
            (double)s_parryDamage, (double)s_parryBehind, (double)s_parryStun, s_dash, s_meleeVk, (double)s_dashDistance, (double)s_dashCooldown,
            (double)s_dashDamage, (double)s_dashWidth, s_gameFit, (double)s_eSeconds, (double)s_eCooldown,
            (double)s_eMult, (double)s_eRadius, (double)s_eSlow, (double)s_eSpread, (double)s_dSeconds, (double)s_dCooldown, (double)s_dMult,
            (double)s_dPerKill, (double)s_dCap, (double)s_dGrace, s_miStacks, (double)s_miSeconds,
            (double)s_miDamage, (double)s_miSlow, s_miStatus, (double)s_miBuildup, s_numbers, (double)s_visualCut, (double)s_forgetEvery, (double)s_forgetRange,
            (double)s_revealHold, (double)s_noiseWindow, (double)s_heardHold, s_gearRule, (double)s_dodge, s_cloakVisual,
            s_hideGear);
}

static uint32_t UnitOf(uint32_t e)
{
    uint8_t d[24];
    return e && e != UINT32_MAX && Descriptor(e, d) ? Word(d + 12) : 0;
}
#include "as_world.h"
#include "as_dash.h"

/* ---- state ---------------------------------------------------------------------------------------------------- */
enum { HIDDEN = 0, QUIET = 1 /* unused since 0.1.1 */, REVEALED = 2, GEAR = 3, AWAY = 4, DESCENT = 5 };   /* AWAY: not on the ground */
static atomic_int g_state = AWAY;
static atomic_int g_melee;               /* a dedicated melee weapon in her hands */
static atomic_ullong g_pressMs;          /* her last right-click press (GetTickCount64) */
static atomic_ullong g_invulnUntilMs;
typedef struct Counter { uint32_t dealer, owner; float origin[3]; } Counter;
static Counter g_counter;
static atomic_int g_counterReady;
static unsigned g_parries, g_counters, g_dashes, g_dashHits;
/* The parry's clash (0.4.5): one of the .wav files in the mod's sounds folder (parry_clash_01.wav to _09.wav), played
 * through Windows (winmm's PlaySound, found at run time so nothing new is linked), beside the game's own sound. Four
 * tries at a Wwise bank of the mod's own did not get the event into the sound engine (0.4.1 to 0.4.4: a patch on
 * 9ba626afa44a3aa3 replaces the game's event metadata but not a bank, which the game takes from the archive of the
 * package that loads it; which archives hold a bank is not known yet: tools/build_bank.py keeps what was learned). */
/* 0.4.8 (Yui: "kinda jarring that it doesn't obey the sound settings"): the recordings are read into memory and each
 * play is scaled by the game's own Master and SFX volumes (audio_volume, sfx_volume of the profile's
 * user_settings.config, read at each parry: what the game last saved) times parry_volume. */
static BOOL (WINAPI* g_playSound)(LPCSTR, HMODULE, DWORD);
static struct { uint8_t* file; uint8_t* play; uint32_t size, data, bytes; } g_clash[9];
static int g_nClash;
static char g_userSettings[MAX_PATH];
/* the newest user_settings.config of HellForge's profiles (<installation>\userdata\profiles\<profile>\) */
static void FindUserSettings(void)
{
    char pattern[MAX_PATH], best[MAX_PATH] = "";
    FILETIME newest = {0, 0};
    WIN32_FIND_DATAA fd;
    snprintf(pattern, sizeof pattern, "%s\\..\\..\\userdata\\profiles\\*", hf->mod_folder(self));
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
        char path[MAX_PATH];
        WIN32_FILE_ATTRIBUTE_DATA a;
        snprintf(path, sizeof path, "%s\\..\\..\\userdata\\profiles\\%s\\user_settings.config", hf->mod_folder(self), fd.cFileName);
        if (GetFileAttributesExA(path, GetFileExInfoStandard, &a) && CompareFileTime(&a.ftLastWriteTime, &newest) > 0) { newest = a.ftLastWriteTime; memcpy(best, path, sizeof best); }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    memcpy(g_userSettings, best, sizeof best);
}
/* the game's Master times SFX volume as last saved, 0 to 1; 1 when it cannot be read */
static float GameVolume(void)
{
    static char text[16384];
    float master = 1, sfx = 1;
    FILE* f = g_userSettings[0] ? fopen(g_userSettings, "rb") : NULL;
    if (!f) return 1;
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    for (char* line = text; line && *line; line = strchr(line, '\n'), line = line ? line + 1 : NULL)
    {
        float v;
        if (sscanf(line, "audio_volume = %f", &v) == 1) master = v;
        else if (sscanf(line, "sfx_volume = %f", &v) == 1) sfx = v;
    }
    if (master > 1.5f) master /= 100;   /* (should the game save per cent) */
    if (sfx > 1.5f) sfx /= 100;
    float v = master * sfx;
    return v < 0 ? 0 : v > 1 ? 1 : v;
}
static void ParrySoundInit(void)
{
    HMODULE winmm = LoadLibraryA("winmm.dll");
    g_playSound = winmm ? (BOOL (WINAPI*)(LPCSTR, HMODULE, DWORD))(void*)GetProcAddress(winmm, "PlaySoundA") : NULL;
    g_nClash = 0;
    for (int i = 1; i <= 9; i++)
    {
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%s\\sounds\\parry_clash_%02d.wav", hf->mod_folder(self), i);
        FILE* f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t* buf = size > 44 && size < (16 << 20) ? malloc((size_t)size) : NULL;
        uint8_t* play = buf ? malloc((size_t)size) : NULL;
        if (play && fread(buf, 1, (size_t)size, f) == (size_t)size && !memcmp(buf, "RIFF", 4) && !memcmp(buf + 8, "WAVE", 4))
        {
            /* its samples: the data chunk of a 16-bit PCM file */
            uint32_t at = 12, data = 0, bytes = 0, pcm16 = 0;
            while (at + 8 <= (uint32_t)size)
            {
                uint32_t len = Word(buf + at + 4);
                if (!memcmp(buf + at, "fmt ", 4) && len >= 16) pcm16 = buf[at + 8] == 1 && buf[at + 9] == 0 && buf[at + 22] == 16;
                if (!memcmp(buf + at, "data", 4)) { data = at + 8; bytes = len <= (uint32_t)size - data ? len : (uint32_t)size - data; break; }
                at += 8 + len + (len & 1);
            }
            if (pcm16 && bytes)
            {
                g_clash[g_nClash].file = buf; g_clash[g_nClash].play = play; g_clash[g_nClash].size = (uint32_t)size;
                g_clash[g_nClash].data = data; g_clash[g_nClash].bytes = bytes;
                g_nClash++;
                buf = play = NULL;
            }
        }
        free(buf); free(play);
        fclose(f);
    }
    FindUserSettings();
    Log("parry sound: %d recording(s) of the mod's sounds folder, %s; the game's volume (Master x SFX) reads %.2f from %s", g_nClash,
        g_playSound ? "played through Windows" : "winmm's PlaySound not found: no sound", (double)GameVolume(), g_userSettings[0] ? g_userSettings : "nowhere (its settings file was not found: full volume)");
}
static void ParrySound(void)
{
    static unsigned last = 99, told;
    if (!s_parrySound || !g_playSound || !g_nClash) return;
    unsigned k = (unsigned)rand() % (unsigned)g_nClash;
    if (k == last && g_nClash > 1) k = (k + 1) % (unsigned)g_nClash;   /* never the same twice in a row */
    last = k;
    float game = GameVolume(), vol = game * s_parryVolume;
    vol *= vol;   /* loudness, not amplitude (0.4.9: 0.6 of the amplitude is 4 dB down, which nobody hears; squared it is 9) */
    if (told++ < 6) Log("parry sound: recording %u at amplitude %.2f (the game's %.2f x parry_volume %.2f, squared)", k + 1, (double)vol, (double)game, (double)s_parryVolume);
    if (vol <= 0.001f) return;
    memcpy(g_clash[k].play, g_clash[k].file, g_clash[k].size);
    int16_t* smp = (int16_t*)(void*)(g_clash[k].play + g_clash[k].data);
    int scale = (int)(vol * 4096.0f + 0.5f);
    for (uint32_t i = 0, n = g_clash[k].bytes / 2; i < n; i++)
    {
        int v = smp[i] * scale / 4096;
        smp[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    g_playSound((LPCSTR)g_clash[k].play, NULL, 0x0004 | 0x0001 | 0x0002);   /* SND_MEMORY | SND_ASYNC | SND_NODEFAULT */
}
static atomic_uint g_parrySeq;   /* every parry since the DLL loaded: the HUD script plays the clash when it rises (0.4.1) */
static int g_linksOk;   /* the unit links are where the research has them */
static double g_sensedUntil, g_forgetAt;
static int g_sensing;   /* enemies that see or have heard her, at the last pass */
static double g_noiseAt = -100, g_heardUntil[FOES];
static uint32_t g_heardId[FOES];
static unsigned g_heard, g_shots;
static uint32_t g_heldSel;
static uint64_t g_heldRes;
static unsigned g_dodged, g_forgot, g_reveals;
static const char* kStateName[] = {"hidden", "-", "revealed (an enemy sees or heard her)", "not hidden (backpack or support weapon)", "away", "in Descent"};
static double g_eUntil, g_eReady, g_dUntil, g_dReady, g_eSlowAt;
static unsigned g_eCasts, g_dCasts, g_dKills, g_spread, g_nulled;

/* ---- the visibility row (game.dll+0x3326540: map +0x20, rows +0x40, 20 bytes each: base +0, floor +12) ------------- */
static float g_oldBase, g_wroteBase, g_wroteFloor, g_oldFloor;
static int g_cut;
static uintptr_t VisRow(void)
{
    uint32_t me = atomic_load(&g_avatar), i;
    uintptr_t m = Pointer(game + 0x3326540), rows;
    if (!me || !m || !Lookup(m + 0x20, me, 1048576, &i) || i >= 262144 || !(rows = Pointer(m + 0x40))) return 0;
    return rows + 20ull * i;
}
static void VisCut(int on)
{
    uintptr_t row = VisRow();
    float base, floor;
    if (!row || !ReadAt(row, &base, 4) || !ReadAt(row + 12, &floor, 4)) { g_cut = 0; return; }
    float mult = 1 - s_visualCut / 100;
    if (on)
    {
        if (!(base >= 0 && base <= 10) || !(floor >= 0 && floor <= 10)) return;
        if (!g_cut || fabsf(base - g_wroteBase) > 1e-6f)
        {
            if (!g_cut && !(base >= 0.1f)) return;                       /* not a row the research knows */
            if (!g_cut) Log("hidden: her visibility %.3f -> %.3f (floor %.3f -> %.3f)", base, base * mult, floor, floor * mult);
            g_oldBase = base; g_wroteBase = base * mult;
            hf->write(row, &g_wroteBase, 4);
        }
        if (!g_cut || fabsf(floor - g_wroteFloor) > 1e-6f)
        {
            g_oldFloor = floor; g_wroteFloor = floor * mult;
            hf->write(row + 12, &g_wroteFloor, 4);
        }
        g_cut = 1;
    }
    else if (g_cut)
    {
        if (fabsf(base - g_wroteBase) <= 1e-6f) hf->write(row, &g_oldBase, 4);
        if (fabsf(floor - g_wroteFloor) <= 1e-6f) hf->write(row + 12, &g_oldFloor, 4);
        g_cut = 0;
    }
}

/* ---- forgetting her (Lost Contact 0.4.1's records: behaviour game+0x3326740, perception game+0x3326548) ------------ */
static uintptr_t Behaviour(uint32_t e)
{
    uintptr_t m = Pointer(game + 0x3326740), recs;
    uint32_t i, count;
    if (!m || !Lookup(m + 0x40, e, 1048576, &i) || !ReadAt(m + 0x20, &count, 4) || i >= count || i >= 16384 || !(recs = Pointer(m + 0x60))) return 0;
    return recs + 0x1F8ull * i;
}
static uintptr_t Perception(uint32_t e)
{
    uintptr_t m = Pointer(game + 0x3326548), recs;
    uint32_t i;
    if (!m || !Lookup(m + 0x30, e, 1048576, &i) || i >= 16384 || !(recs = Pointer(m + 0x50))) return 0;
    return recs + 0x13F8ull * i;
}
static const struct { uint32_t count, entries; } kLists[3] = {{0x310, 0x318}, {0x818, 0x820}, {0xD20, 0xD28}};
static const uint32_t kSingles[3] = {0x1228, 0x1278, 0x12C8};
/* the sense bits (+0x4C) of her entries in a perception record, or'd: nonzero while that perceiver senses her now */
static uint32_t SensedIn(uintptr_t pr, uint32_t me)
{
    uint32_t bits = 0;
    for (int l = 0; l < 3; l++)
    {
        uint32_t c = 0;
        uint8_t ent[0x50];
        if (!ReadAt(pr + kLists[l].count, &c, 4) || c > 16) continue;
        for (uint32_t k = 0; k < c; k++)
            if (ReadAt(pr + kLists[l].entries + 0x50u * k, ent, sizeof ent) && Word(ent) == me) bits |= Word(ent + 0x4C);
    }
    return bits;
}
/* does a perception record hold an entry of her at all? */
static int HoldsIn(uintptr_t pr, uint32_t me)
{
    for (int l = 0; l < 3; l++)
    {
        uint32_t c = 0, id = 0;
        if (!ReadAt(pr + kLists[l].count, &c, 4) || c > 16) continue;
        for (uint32_t k = 0; k < c; k++) if (ReadAt(pr + kLists[l].entries + 0x50u * k, &id, 4) && id == me) return 1;
    }
    for (int k = 0; k < 3; k++)
    {
        uint32_t id = 0, used = 0;
        if (ReadAt(pr + kSingles[k], &id, 4) && id == me && ReadAt(pr + kSingles[k] + 0x48, &used, 4) && used) return 1;
    }
    return 0;
}
/* her entries taken out of a perception record (the caller has checked nobody there senses her): entries removed */
static int ForgetIn(uintptr_t pr, uint32_t me)
{
    int n = 0;
    for (int l = 0; l < 3; l++)
    {
        uint32_t c = 0;
        if (!ReadAt(pr + kLists[l].count, &c, 4) || c > 16 || !c) continue;
        uint8_t list[16 * 0x50];
        if (!ReadAt(pr + kLists[l].entries, list, 0x50u * c)) continue;
        uint32_t keep = 0;
        for (uint32_t k = 0; k < c; k++)
            if (Word(list + 0x50u * k) != me) { if (keep != k) memmove(list + 0x50u * keep, list + 0x50u * k, 0x50); keep++; }
        if (keep == c) continue;
        if (keep && hf->write(pr + kLists[l].entries, list, 0x50u * keep)) continue;
        if (hf->write(pr + kLists[l].count, &keep, 4)) continue;
        n += (int)(c - keep);
    }
    for (int s = 0; s < 3; s++)
    {
        uintptr_t at = pr + kSingles[s];
        uint32_t id = 0, used = 0;
        static const uint32_t zero = 0;
        if (ReadAt(at, &id, 4) && id == me && ReadAt(at + 0x48, &used, 4) && used && !hf->write(at + 0x48, &zero, 4)) n++;
    }
    return n;
}
static int FoeRoot(uint32_t e, uint64_t res, float p[3])
{
    uint8_t d[24];
    return Descriptor(e, d) && Quad(d) == res && UnitNode(Word(d + 12), 0, p);
}
/* every enemy within forget_range m: one that senses her now keeps her (and counts); one that only remembers or
 * targets her forgets her */
static void ForgetPass(void)
{
    uint32_t me = atomic_load(&g_avatar);
    float mp[3], p[3];
    if (!me || !PlayerPose(mp, NULL)) return;
    int n = 0, sensing = 0;
    for (int j = 0; j < FOES; j++)
    {
        uint32_t id = g_foe[j];
        if (!id) continue;
        if (!FoeRoot(id, g_foeRes[j], p)) { g_foe[j] = 0; continue; }
        float d2 = Dist2(p, mp);
        if (d2 > s_forgetRange * s_forgetRange) continue;
        uintptr_t br = Behaviour(id), pr = Perception(id), sq = 0;
        uint8_t b[0xA0];
        uint32_t perceiver = 0, bits = 0;
        int target = 0, had = 0;
        if (br && ReadAt(br, b, sizeof b)) { perceiver = Word(b + 0x68); target = Word(b + 0x18) == me; }
        if (perceiver && perceiver != id) sq = Perception(perceiver);
        if (pr) bits |= SensedIn(pr, me);
        if (sq) bits |= SensedIn(sq, me);
        if (bits)
        {
            sensing++;
            static unsigned told;
            if (told < 40) { told++; Log("sensed by enemy %u (%016llx) at %.0f m: sense bits %x%s", id, (unsigned long long)g_foeRes[j], sqrtf(d2), bits, target ? ", its target" : ""); }
            continue;
        }
        int holds = target || (pr && HoldsIn(pr, me)) || (sq && HoldsIn(sq, me));
        if (holds)
        {
            if (g_heardId[j] != id) { g_heardId[j] = id; g_heardUntil[j] = 0; }
            if (g_now >= g_heardUntil[j] && g_now - g_noiseAt <= s_noiseWindow)
            {
                g_heardUntil[j] = g_now + s_heardHold;
                g_heard++;
                static unsigned told;
                if (told < 40) { told++; Log("heard by enemy %u (%016llx) at %.0f m%s", id, (unsigned long long)g_foeRes[j], sqrtf(d2), target ? ", its target" : ""); }
            }
            if (g_now < g_heardUntil[j]) { sensing++; continue; }
        }
        if (target)
        {
            static const uint32_t z = 0;
            static const uint64_t z8 = 0;
            hf->write(br + 0x18, &z, 4); hf->write(br + 0x78, &z, 4); hf->write(br + 0x98, &z8, 8);
            had = 1;
        }
        if (pr && ForgetIn(pr, me)) had = 1;
        if (sq && ForgetIn(sq, me)) had = 1;
        n += had;
    }
    g_forgot += (unsigned)n;
    g_sensing = sensing;
    if (sensing) g_sensedUntil = g_now + s_revealHold;
}

/* ---- what reveals her ------------------------------------------------------------------------------------------ */
static void OnStat(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfStatEvent* st = (const HfStatEvent*)info;
    if (st->stat != HF_STAT_SHOTS || st->added <= 0 || !kb_kit_on || !g_alive) return;
    /* only shots the game credits to the gun in her hands: her area strikes are spawned rounds (credited to her
     * Helldiver, as Exusiai 0.3.1 found) and are not noise */
    int mine = 0;
    for (uint32_t k = 0; k < st->count && k < HF_STAT_SOURCES; k++)
        if (g_heldRes && st->sources[k].resource == g_heldRes && st->sources[k].added > 0) mine += st->sources[k].added;
    if (mine <= 0) return;
    g_noiseAt = g_now;
    g_shots += (unsigned)mine;
}
static void OnWeaponAsc(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfWeaponEvent* w = (const HfWeaponEvent*)info;
    if (!w->avatar || w->avatar != atomic_load(&g_avatar)) return;
    g_heldSel = w->selection;
    g_heldRes = w->resource;
    static const uint64_t kMelee[] = {HF_ENTITY_CQC20_BREACHING_HAMMER, HF_ENTITY_CQC72_ENTRENCHMENT_TOOL, HF_ENTITY_CQC9_DEFOLIATION_TOOL,
                                      HF_ENTITY_CQC1_ONE_TRUE_FLAG, HF_ENTITY_CQC19_STUN_LANCE, HF_ENTITY_CQC2_SABER, HF_ENTITY_CQC30_STUN_BATON,
                                      HF_ENTITY_CQC42_MACHETE, HF_ENTITY_CQC5_COMBAT_HATCHET};
    int melee = 0;
    for (unsigned i = 0; i < sizeof kMelee / sizeof kMelee[0]; i++) if (w->resource == kMelee[i]) melee = 1;
    if (melee != atomic_exchange(&g_melee, melee)) { static unsigned told; if (told++ < 12) Log("she holds %016llx: %s", (unsigned long long)w->resource, melee ? "a melee weapon (the parry is armed; left click dashes)" : "not a melee weapon"); }
}
static int Item(int k) { uint32_t e = atomic_load(&g_myItems[k]); return e && e != UINT32_MAX ? (int)e : 0; }
static int StateNow(void)
{
    if (!g_alive || !atomic_load(&g_avatar)) return AWAY;
    if (g_now < g_dUntil) return DESCENT;
    if (s_gearRule && (Item(3) || g_heldSel == 3)) return GEAR;
    if (g_now < g_sensedUntil) return REVEALED;
    return HIDDEN;
}

/* ---- hits on her: the Ambusher's dodge --------------------------------------------------------------------------- */
static int HitTaken(const uint8_t* h, void* result, unsigned long long now)
{
    int st = atomic_load(&g_state);
    uint32_t dealer = Word(h + 0x80), owner = Word(h + 0x100), self_ = atomic_load(&g_avatar);
    if (self_ && (dealer == self_ || owner == self_)) return 0;   /* her own (bleeding): no attack, never parried or dodged */
    if (now < atomic_load(&g_invulnUntilMs)) { memset(result, 0, RESULT_SIZE); g_nulled++; return 1; }
    if (st == DESCENT) { memset(result, 0, RESULT_SIZE); g_nulled++; return 1; }   /* Descent: nothing lands */
    /* the parry: a right-click press within the window before this hit, a melee weapon in her hands */
    if (s_parry && st != AWAY && atomic_load(&g_melee))
    {
        unsigned long long press = atomic_load(&g_pressMs);
        if (press && now >= press && (double)(now - press) / 1000.0 <= s_parryWindow)
        {
            memset(result, 0, RESULT_SIZE);
            g_parries++;
            atomic_fetch_add(&g_parrySeq, 1);
            ParrySound();
            atomic_store(&g_invulnUntilMs, now + (unsigned long long)(s_parryInvuln * 1000));
            if (!atomic_load(&g_counterReady))
            {
                g_counter.dealer = dealer; g_counter.owner = owner;
                memcpy(g_counter.origin, h + 0x40, 12);
                atomic_store(&g_counterReady, 1);
            }
            return 1;
        }
    }
    if ((st != HIDDEN && st != QUIET) || s_dodge <= 0) return 0;
    static _Atomic unsigned seed = 0x9E3779B9u;
    unsigned x = atomic_load(&seed);
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    atomic_store(&seed, x);
    if ((x & 0xFFFF) / 65536.0f >= s_dodge) return 0;
    memset(result, 0, RESULT_SIZE);
    g_dodged++;
    return 1;
}
static void DamageTaken(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    (void)me;
    if (GetTickCount64() < atomic_load(&g_invulnUntilMs) && d->amount > 0) e->cancel = 1;   /* the parry's backstop */
}

/* ---- Ambusher: her hits strike everyone in range; Pursuit --------------------------------------------------------- */
static int Friend(uint32_t e)
{
    uint8_t d[24];
    return Descriptor(e, d) && (Quad(d) == AVATAR || FriendRes(Quad(d)));
}
#define VOL 24
static struct { double at; int n, pursuit; int32_t damage; uint32_t tgt[VOL]; uint8_t left[VOL]; } g_vol;
static int g_volOwed, g_volPursuit, g_charges;
static struct { double at; int n; int32_t damage; uint32_t tgt[VOL]; uint8_t left[VOL]; } g_dashVol;   /* the dash's strikes */
static int32_t g_volDamage;
static uint32_t g_volPrimary;
static double g_areaAt = -100, g_chargeAt;
static uint32_t g_slowQ[32];
static int g_nslow;
static unsigned g_volleys, g_strikes, g_pursuits;
static void SlowLater(uint32_t e) { if (s_pursuitSlow > 0 && g_nslow < 32) g_slowQ[g_nslow++] = e; }
/* her hits for the HUD's damage numbers (ascalon_hits; the Executor's ring): the damage event writes, the HUD reads */
#define HITRING 64
typedef struct { float x, y, z, amount; uint32_t boosted, seq; } HitNote;
static HitNote g_ring[HITRING];
static volatile uint32_t g_ringSeq;
static void NoteHit(const HfDamageEvent* d, int boosted)
{
    if (!s_numbers || !kb_kit_on || !g_inMission) return;
    uint32_t q = g_ringSeq + 1;
    HitNote* h = &g_ring[q % HITRING];
    h->x = d->position[0]; h->y = d->position[1]; h->z = d->position[2];
    h->amount = (float)d->amount; h->boosted = (uint32_t)boosted;
    h->seq = q;
    g_ringSeq = q;
}
HF_MOD_EXPORT int ascalon_hits(unsigned version, float* out, int max, uint32_t* seq)
{
    if (version != 1 || !out || !seq || max <= 0) return 0;
    uint32_t now = g_ringSeq, from = *seq;
    if (now - from > HITRING) from = now - HITRING;
    int n = 0;
    for (uint32_t q = from + 1; q - 1 != now && n < max; q++)
    {
        HitNote h = g_ring[q % HITRING];
        if (h.seq != q) continue;
        out[n * 5] = h.x; out[n * 5 + 1] = h.y; out[n * 5 + 2] = h.z; out[n * 5 + 3] = h.amount; out[n * 5 + 4] = (float)h.boosted;
        n++;
    }
    *seq = now;
    return n;
}
/* her view for the HUD (ascalon_view; the Executor's CamView: her unit's world, its camera matrix at +0x230920) */
static float g_viewOut[6];
static volatile int g_viewOk;
static void ViewFrame(void)
{
    uint8_t d[24];
    g_viewOk = 0;
    if (!Descriptor(atomic_load(&g_avatar), d)) return;
    uint32_t unit = Word(d + 12), slot = unit & 0x3FFFFF, id = 0;
    uintptr_t registry = Pointer(exe + 0x1A100F0), objects, object, w;
    float m[16];
    if (!registry || !(objects = Pointer(registry + 0x88)) || !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit ||
        !(w = Pointer(object + 0x18)) || !ReadAt(w + 0x230920, m, 64)) return;
    for (int k = 0; k < 16; k++) if (!isfinite(m[k])) return;
    if (fabsf(m[15] - 1) > 1e-3f) return;
    float n = sqrtf(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
    if (!(n > 0.5f)) return;
    memcpy(g_viewOut, m + 12, 12);
    for (int k = 0; k < 3; k++) g_viewOut[3 + k] = m[4 + k] / n;
    g_viewOk = 1;
}
HF_MOD_EXPORT int ascalon_view(unsigned version, float* out)
{
    if (version != 1 || !out || !g_viewOk || !g_inMission) return 0;
    memcpy(out, g_viewOut, 24);
    return 1;
}
/* ---- Mortal Interrogation: the marks -------------------------------------------------------------------------------- */
#define MARKS 64
static struct { uint32_t e; int stacks, pending; double until, next; int32_t base; double sent; int tries; } g_mark[MARKS];
static unsigned g_marked, g_ticks, g_bleeds;
static unsigned g_tickSent, g_tickSentLine, g_tickLandLine, g_tickLate, g_tickAgain;   /* for the log's Mortal Interrogation line */
static double g_tickLineAt;
static int g_statusBad;   /* the game refused mi_status this session: the spawned-round tick instead */
static int Marked(uint32_t e)
{
    for (int i = 0; i < MARKS; i++) if (g_mark[i].e == e && g_mark[i].until > g_now) return 1;
    return 0;
}
static void Mark(uint32_t e, int32_t damage)
{
    if (s_miStacks < 1 || s_miSeconds <= 0) return;
    int free = -1;
    for (int i = 0; i < MARKS; i++)
    {
        if (g_mark[i].e == e && g_mark[i].until > g_now)
        {
            if (g_mark[i].stacks < s_miStacks) g_mark[i].stacks++;
            if (damage > g_mark[i].base) g_mark[i].base = damage;   /* (0: a stack spread by Endowment keeps the mark's own) */
            g_mark[i].until = g_now + s_miSeconds;   /* a new hit starts the time again */
            return;
        }
        if (free < 0 && (!g_mark[i].e || g_mark[i].until <= g_now)) free = i;
    }
    if (free < 0) return;
    g_mark[free].e = e; g_mark[free].stacks = 1; g_mark[free].pending = 0; g_mark[free].tries = 0; g_mark[free].base = damage > 0 ? damage : 60;
    g_mark[free].until = g_now + s_miSeconds; g_mark[free].next = g_now + 1;
    g_marked++;
}
static void DamageDealt(HfDamagePreEvent* e, HfDamageEvent* d, uint32_t me)
{
    (void)e; (void)me;
    /* damage over time on an enemy she has marked: the bleed at work (a number for it; the first ones in the log) */
    if (d->kind == 2 && s_miStatus > 0 && !g_statusBad && d->amount > 0 && Marked(d->target))
    {
        static unsigned told;
        if (told < 10) { told++; Log("damage over time on marked entity %u: %d (element %u, dealer %u, owner %u, %s)", d->target, d->amount, d->element, d->dealer, d->owner, MineHit(d) ? "hers" : "not credited to her"); }
        g_bleeds++;
        NoteHit(d, 0);
        return;
    }
    if (!MineHit(d) || d->amount <= 0 || Friend(d->target) || s_areaMax < 1 || s_areaRadius <= 0) return;
    /* one of the dash's strikes landing (its own list since 0.4.3: her melee hit of the same press set off an area
     * strike that wiped the shared one, and the dash's rounds landed with their own small amount) */
    if (d->kind == 0 && g_dashVol.n && g_now - g_dashVol.at <= 1.0)
        for (int i = 0; i < g_dashVol.n; i++)
            if (g_dashVol.tgt[i] == d->target && g_dashVol.left[i])
            {
                g_dashVol.left[i]--;
                d->amount = g_dashVol.damage < 1 ? 1 : g_dashVol.damage;
                g_strikes++;
                Mark(d->target, g_dashVol.damage);
                NoteHit(d, 0);
                return;
            }
    /* one of the last volley's strikes landing: its amount is the hit's that set it off */
    if (d->kind == 0 && g_vol.n && g_now - g_vol.at <= 1.0)
        for (int i = 0; i < g_vol.n; i++)
            if (g_vol.tgt[i] == d->target && g_vol.left[i])
            {
                g_vol.left[i]--;
                double a = (double)g_vol.damage * s_areaMult * (g_vol.pursuit ? s_pursuitMult : 1);
                d->amount = a < 1 ? 1 : a > 2000000 ? 2000000 : (int32_t)a;
                if (g_vol.pursuit) SlowLater(d->target);
                g_strikes++;
                Mark(d->target, g_vol.damage);
                NoteHit(d, g_vol.pursuit);
                return;
            }
    /* a tick of Mortal Interrogation landing: its amount is the mark's */
    if (d->kind == 0)
        for (int i = 0; i < MARKS; i++)
            if (g_mark[i].e == d->target && g_mark[i].pending > 0)
            {
                g_mark[i].pending--;
                double a = (double)g_mark[i].base * s_miDamage * g_mark[i].stacks;
                d->amount = a < 1 ? 1 : a > 2000000 ? 2000000 : (int32_t)a;
                g_ticks++;
                NoteHit(d, 0);
                return;
            }
    /* a hit of her own, told as Voymastina tells them: a shot (IsShot: a projectile, a ricochet, a beam), or melee: a hit
     * of hers of kind 5, 6 or 7 (impact, ability, flat: bayonet, lance, punches are kind 6) that is not on herself and
     * lands within 6 m of her */
    int shot = IsShot(d);
    float mePos[3];
    int melee = !shot && (d->kind == 5 || d->kind == 6 || d->kind == 7) && d->target != atomic_load(&g_avatar) && PlayerPose(mePos, NULL) &&
                Dist2(d->position, mePos) < 6.0f * 6.0f;
    if (!shot && !melee) return;
    /* a bullet's hit counts only when her gun has just fired (the shot counter): a strike round that hits a bystander on
     * its way would otherwise set off the next volley, and that one the next */
    if (shot && d->kind != 3 && g_now - g_noiseAt > 1.0) return;
    Mark(d->target, d->amount);
    if (g_now - g_areaAt < s_areaEvery)
    {
        double b = (double)d->amount * (g_now < g_eUntil ? s_eMult : 1) * (g_now < g_dUntil ? s_dMult : 1);
        d->amount = b > 2000000 ? 2000000 : (int32_t)b;
        NoteHit(d, 0);
        return;
    }
    g_areaAt = g_now;
    int pursuit = g_charges > 0;
    double skill = (g_now < g_eUntil ? s_eMult : 1) * (g_now < g_dUntil ? s_dMult : 1);
    { double b = (double)d->amount * skill; d->amount = b > 2000000 ? 2000000 : (int32_t)b; }
    g_volDamage = d->amount;
    g_volPrimary = d->target;
    g_volPursuit = pursuit;
    g_volOwed = pursuit ? 2 : 1;
    static unsigned told;
    if (told < 12) { told++; Log("%s of hers (kind %u, %d damage) on entity %u: everyone within %.0f m is struck%s", melee ? "a melee hit" : "a shot", d->kind, d->amount, d->target, (double)s_areaRadius, pursuit ? " (Pursuit: twice, and slowed)" : ""); }
    if (pursuit)
    {
        if (g_charges == s_pursuitCharges) g_chargeAt = g_now;
        g_charges--;
        g_pursuits++;
        double a = (double)d->amount * s_pursuitMult;
        d->amount = a > 2000000 ? 2000000 : (int32_t)a;
        SlowLater(d->target);
    }
    NoteHit(d, pursuit);
}
/* the GAME THREAD: each marked enemy's tick a second: a round of the mod's spawned 2.5 m from its body on her side (from
 * her, when she is nearer than that), flying into it; and its dose of the slow */
/* One tick's round at a marked enemy's body, from 1.6 m out: from her side and above, then (a tick that missed,
 * the enemy knocked about under it) from straight above, then from the far side. 1 sent */
static int TickRound(int i, int try, const float pos[3], int have)
{
    uint8_t d[24];
    float r[3], off[3], u[3] = {0, 0, 1.6f}, from[3];
    if (!Descriptor(g_mark[i].e, d) || !UnitNode(Word(d + 12), 0, r)) return -1;
    if (BodyOffset(Word(d + 12), r, off)) for (int t = 0; t < 3; t++) r[t] += off[t];
    else r[2] += 1.0f;
    if (have && try != 1)
    {
        float v[3] = {pos[0] - r[0], pos[1] - r[1], pos[2] + 1.3f - r[2]}, vn = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        float side = try == 0 ? 0.9f : -0.9f;
        if (vn > 0.1f) { for (int k = 0; k < 3; k++) u[k] = v[k] / vn * side; u[2] = fabsf(u[2]) + 1.3f; }
    }
    for (int k = 0; k < 3; k++) from[k] = r[k] + u[k];
    float dir[3] = {-u[0], -u[1], -u[2]}, dn = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (!(dn > 0.05f)) return 0;
    for (int k = 0; k < 3; k++) dir[k] /= dn;
    int rr = hf->spawn_projectile(self, (uint32_t)s_areaRound, from, dir);
    if (rr == HF_ACT_WAITING || !Act("Mortal Interrogation's tick", rr)) return 0;
    g_mark[i].pending = 1; g_mark[i].sent = g_now; g_mark[i].tries = try + 1;
    g_tickSent++;
    return 1;
}
static void MarkFrame(void)
{
    float pos[3];
    int have = PlayerPose(pos, NULL), spawned = 0;
    for (int i = 0; i < MARKS && spawned < 8; i++)
    {
        uint32_t en = g_mark[i].e;
        if (!en) continue;
        if (g_mark[i].until <= g_now || !Living(en)) { g_mark[i].e = 0; continue; }
        /* a tick that has not landed 0.15 s on missed (0.4.11: three in ten did, the enemy knocked about by her hits
         * between the look at where it is and the round getting there): again, up to three rounds a tick */
        if (g_mark[i].pending && g_mark[i].tries < 3 && g_now - g_mark[i].sent >= 0.15 && g_now < g_mark[i].next)
        {
            g_tickAgain++;
            if (TickRound(i, g_mark[i].tries, pos, have) > 0) spawned++;
            else g_mark[i].tries = 3;
            continue;
        }
        if (g_now < g_mark[i].next) continue;
        g_mark[i].next = g_now + 1;
        if (g_mark[i].pending) g_tickLate++;           /* none of its rounds landed */
        g_mark[i].pending = 0;                         /* a tick that never landed is not owed */
        g_mark[i].tries = 0;
        if (s_miSlow > 0 && hf->apply_status) hf->apply_status(self, en, HF_STATUS_FLAMER_SLOWED, s_miSlow * g_mark[i].stacks);
        if (s_miStatus > 0 && !g_statusBad && hf->apply_status)
        {
            float dose = s_miBuildup * g_mark[i].stacks;
            int ar = hf->apply_status(self, en, (uint32_t)s_miStatus, dose > 1000 ? 1000 : dose);
            if (ar == HF_ACT_DONE || ar == HF_ACT_REQUESTED || ar == HF_ACT_QUEUED) continue;
            g_statusBad = 1;
            Log("Mortal Interrogation: the game refused status %d (%s): the mark's damage is the mod's own from here", s_miStatus, hf->last_log ? hf->last_log(self) : "");
        }
        if (s_miDamage <= 0) continue;
        int tr = TickRound(i, 0, pos, have);
        if (tr < 0) { g_mark[i].e = 0; continue; }
        if (tr > 0) spawned++;
    }
    /* every 10 s with marks about: how many, and how the ticks fared */
    if (g_now - g_tickLineAt >= 10)
    {
        int alive = 0, stacks = 0;
        for (int i = 0; i < MARKS; i++) if (g_mark[i].e && g_mark[i].until > g_now) { alive++; stacks += g_mark[i].stacks; }
        if (alive || g_tickSent != g_tickSentLine)
        {
            static unsigned told;
            if (told++ < 80) Log("Mortal Interrogation: %d enemies marked now (%d stacks); in the last 10 s %u rounds sent, %u ticks landed; in all %u rounds sent again, %u ticks never landed", alive, stacks,
                g_tickSent - g_tickSentLine, g_ticks - g_tickLandLine, g_tickAgain, g_tickLate);
        }
        g_tickSentLine = g_tickSent; g_tickLandLine = g_ticks; g_tickLineAt = g_now;
    }
}
/* the GAME THREAD: the volleys owed (one a frame), the slows owed, Pursuit's charges */
static void AreaFrame(void)
{
    for (int i = 0; i < g_nslow; i++) if (hf->apply_status) Act("Pursuit's slow", hf->apply_status(self, g_slowQ[i], HF_STATUS_FLAMER_SLOWED, s_pursuitSlow));
    g_nslow = 0;
    if (g_charges < s_pursuitCharges && g_now - g_chargeAt >= s_pursuitEvery) { g_charges++; g_chargeAt = g_now; }
    if (g_volOwed <= 0) return;
    int second = g_volPursuit && g_volOwed == 1;   /* Pursuit's second landing takes in the enemy she hit */
    g_volOwed--;
    float pos[3], aim[3];
    if (!PlayerPose(pos, aim)) return;
    float from[3] = {pos[0], pos[1], pos[2] + 1.3f};
    struct { uint32_t e; float d2; float q[3]; } c[VOL];
    int nc = 0, cap = s_areaMax < VOL ? s_areaMax : VOL;
    for (int k = 0; k < FOES; k++)
    {
        uint32_t en = g_foe[k];
        uint8_t d[24];
        float r[3];
        if (!en || (en == g_volPrimary && !second) || !Descriptor(en, d) || Quad(d) != g_foeRes[k] || !UnitNode(Word(d + 12), 0, r)) continue;
        float d2 = Dist2(r, pos);
        if (d2 > s_areaRadius * s_areaRadius || (nc == cap && d2 >= c[cap - 1].d2) || !Living(en)) continue;
        int j = nc < cap ? nc++ : cap - 1;
        while (j > 0 && c[j - 1].d2 > d2) { c[j] = c[j - 1]; j--; }
        c[j].e = en; c[j].d2 = d2;
        float off[3];
        if (BodyOffset(Word(d + 12), r, off)) for (int t = 0; t < 3; t++) r[t] += off[t];
        else r[2] += 1.0f;
        memcpy(c[j].q, r, 12);
    }
    if (!nc) return;
    if (!second) { memset(&g_vol, 0, sizeof g_vol); g_vol.damage = g_volDamage; g_vol.pursuit = g_volPursuit; }
    g_vol.at = g_now;
    int fired = 0;
    for (int j = 0; j < nc; j++)
    {
        if (!ClearShot(from, c[j].q)) continue;
        float v[3] = {c[j].q[0] - from[0], c[j].q[1] - from[1], c[j].q[2] - from[2]};
        float vn = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (!(vn > 0.1f)) continue;
        for (int k = 0; k < 3; k++) v[k] /= vn;
        int i = 0;
        while (i < g_vol.n && g_vol.tgt[i] != c[j].e) i++;
        if (i == g_vol.n) { if (g_vol.n == VOL) continue; g_vol.tgt[g_vol.n++] = c[j].e; }
        g_vol.left[i]++;                       /* before the spawn: the round may land at once */
        int r = hf->spawn_projectile(self, (uint32_t)s_areaRound, from, v);
        if (r == HF_ACT_WAITING || !Act("an area strike", r)) { g_vol.left[i]--; if (r == HF_ACT_WAITING) g_volOwed++; break; }
        fired++;
    }
    if (fired)
    {
        g_volleys++;
        static unsigned told;
        if (told++ < 10) Log("area strike: %d enem%s of the %d within %.0f m (projectile %d)%s", fired, fired == 1 ? "y" : "ies", nc, (double)s_areaRadius, s_areaRound, second ? ", Pursuit's second" : "");
    }
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
    uint32_t victim = ((const HfKillEvent*)info)->victim;
    uint8_t d[24];
    float vp[3], me[3], p[3];
    int skill = kb_kit_on && g_alive && (g_now < g_eUntil || g_now < g_dUntil);
    if (skill && !Friend(victim) && Descriptor(victim, d) && FoeKind(Quad(d)) && UnitNode(Word(d + 12), 0, vp) && PlayerPose(me, NULL) && Dist2(vp, me) < 30.0f * 30.0f)
    {
        if (g_now < g_dUntil && s_dPerKill > 0)
        {
            double until = g_dUntil + s_dPerKill;
            g_dUntil = until > g_now + s_dCap ? g_now + s_dCap : until;
            g_dKills++;
        }
        if (g_now < g_eUntil && s_eSpread > 0)
            for (int j = 0; j < FOES; j++)
            {
                uint32_t id = g_foe[j];
                if (!id || id == victim || !FoeRoot(id, g_foeRes[j], p) || Dist2(p, vp) > s_eSpread * s_eSpread || !Living(id)) continue;
                Mark(id, 0);
                g_spread++;
            }
    }
    DropFoe(victim);
}
static uintptr_t UnitObject(uint32_t unit);
/* ---- the parry's counter and the dash (the game thread) ------------------------------------------------------------- */
/* a strike on one enemy: a round of the mod's spawned 2.5 m from its body on `toward`'s side, flying into it */
static int StrikeBeside(uint32_t en, const float toward[3])
{
    uint8_t d[24];
    float r[3], off[3];
    if (!Descriptor(en, d) || !UnitNode(Word(d + 12), 0, r)) return 0;
    if (BodyOffset(Word(d + 12), r, off)) for (int t = 0; t < 3; t++) r[t] += off[t];
    else r[2] += 1.0f;
    float v[3] = {toward[0] - r[0], toward[1] - r[1], toward[2] + 1.3f - r[2]}, vn = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!(vn > 0.1f)) { v[0] = 0; v[1] = 0; v[2] = 1; vn = 1; }
    float back = vn < 2.5f ? vn : 2.5f, from[3], dir[3];
    for (int k = 0; k < 3; k++) { from[k] = r[k] + v[k] / vn * back; dir[k] = -v[k] / vn; }
    int rr = hf->spawn_projectile(self, (uint32_t)s_areaRound, from, dir);
    return rr != HF_ACT_WAITING && Act("a strike", rr);
}
static int UnitMatrix(uint32_t unit, uint32_t node, float m[16])
{
    uintptr_t object = UnitObject(unit), nodes;
    uint32_t count;
    if (!object || !ReadAt(object + 0x70, &count, 4) || node >= count || count > 4096 || !(nodes = Pointer(object + 0x88)) || !ReadAt(nodes + 64u * node, m, 64)) return 0;
    for (int i = 0; i < 16; i++) if (!isfinite(m[i])) return 0;
    return 1;
}
static void NearFoeUnits(const float pos[3], float reach)
{
    uint8_t fid[24];
    float fp[3];
    g_nFoeUnits = 0;
    for (int i = 0; i < FOES && g_nFoeUnits < 256; i++)
    {
        uint32_t en = g_foe[i];
        if (!en || !Descriptor(en, fid) || Quad(fid) != g_foeRes[i] || !UnitNode(Word(fid + 12), 0, fp) || Dist2(fp, pos) > reach * reach) continue;
        g_foeUnits[g_nFoeUnits++] = Word(fid + 12);
    }
}
/* The game's own fit test (0.4.6; the LIFT-182 Warp Pack's, from the code dump: its update takes a candidate only when
 * game.dll+0x68C340 says the wearer's mover shape overlaps nothing there): FitsAt(manager [game+0x3326598], out
 * {u32 ok; float pos[3]}, an entity record of which only the id at +8 and the unit at +0xC are read, mode 0, filter
 * 0x48D6C53A, the feet position, tolerance 0.1). It looks the entity's mover up without a check (a missing entity
 * indexes -1), so everything it reads is read here first and the call is skipped unless all of it is there:
 * the entity in the manager's hash, its descriptor (whose unit must be her unit), the descriptor's resource in the
 * mover table, the mover and its shape (the physics table's own two calls), and the query slot.
 * 1 fits, 0 does not, -1 not asked (the caller keeps its own checks) */
typedef struct { uint32_t ok; float pos[3]; } GameFit;
typedef GameFit* (*FitsAtFn)(void*, GameFit*, const void*, uint32_t, uint32_t, const float*, float);
typedef uint32_t (*MoverFn)(uint32_t, uint32_t);
typedef void* (*ShapeFn)(uint32_t);
static FitsAtFn g_fitsAt;
static unsigned g_fitAsked, g_fitNo, g_fitSkipped;
static int GameFits(uint32_t entity, uint32_t unit, const float pos[3])
{
    static unsigned told;
    const char* miss = NULL;
    uintptr_t mgr, ents, descs, desc, em, tab, phys, f0, f60, qt;
    uint32_t invalid = 0, cap = 0, empty = 0, mult = 0, idx = 0xFFFFFFFFu, name = 0, dunit = 0;
    uint64_t res = 0;
    if (!s_gameFit || !g_fitsAt) return -1;
    if (!(mgr = Pointer(game + 0x3326598)) || !ReadAt(game + 0x3483C20, &invalid, 4) || entity == invalid) miss = "the manager";
    else if (!(ents = Pointer(mgr + 0x18)) || !ReadAt(mgr + 0x20, &cap, 4) || !ReadAt(mgr + 0x24, &empty, 4) || !ReadAt(mgr + 0x28, &mult, 4) ||
             !cap || (cap & (cap - 1)) || cap > (1u << 20)) miss = "the manager's hash";
    else
    {
        for (uint32_t i = 0; i < cap; i++)
        {
            uint32_t e[2];
            if (!ReadAt(ents + 8ull * ((i + entity * mult) & (cap - 1)), e, 8)) break;
            if (e[0] == entity) { idx = e[1]; break; }
            if (e[0] == empty) break;
        }
        if (idx == 0xFFFFFFFFu || idx > (1u << 20)) miss = "her entity in the manager";
        else if (!(descs = Pointer(mgr + 0x30)) || !(desc = Pointer(descs + 8ull * idx)) || !ReadAt(desc, &res, 8) || !res ||
                 !ReadAt(desc + 0xC, &dunit, 4) || dunit != unit) miss = "her descriptor";
        else if (!(em = Pointer(game + 0x346BF98)) || !(tab = Pointer(em + 0xF12840))) miss = "the mover table";
        else
        {
            int found = 0;
            for (uint32_t k = 0, b = (uint32_t)(res % 44); k < 44 && !found; k++, b = (b + 1) % 44)
            {
                uint8_t bucket[16];
                uint64_t key;
                if (!ReadAt(tab + 16ull * b, bucket, 16)) break;
                memcpy(&key, bucket, 8);
                if (!key) break;
                if (key == res) { found = ReadAt(tab + 0x2C0 + 0xF0ull * Word(bucket + 8) + 0x48, &name, 4); break; }
            }
            if (!found) miss = "her mover's record";
            else if (!(phys = Pointer(game + 0x3326388)) || !(f0 = Pointer(phys)) || !(f60 = Pointer(phys + 0x60)) ||
                     !(qt = Pointer(game + 0x3326328)) || !Pointer(qt + 0x80)) miss = "the physics tables";
            else
            {
                uint32_t mover = ((MoverFn)f0)(unit, name);
                if (!mover || mover == 0xFFFFFFFFu || !((ShapeFn)f60)(mover)) miss = "her mover or its shape";
            }
        }
    }
    if (miss)
    {
        g_fitSkipped++;
        if (told++ < 6) Log("the game's fit test is not asked: %s is not there (her own checks decide)", miss);
        return -1;
    }
    struct { uint64_t unused; uint32_t id, unit; } ent = {0, entity, unit};
    GameFit out = {0};
    g_fitsAt((void*)mgr, &out, &ent, 0, 0x48D6C53Au, pos, 0.1f);
    g_fitAsked++;
    if (!out.ok) g_fitNo++;
    return out.ok ? 1 : 0;
}
/* A spot she can be put on without ending up in or under anything (0.4.5: every check before the move, none after):
 *   1. the spot: as SpotAt finds it, looking down from 12 m above her (a look from 2.5 m started under a hill's
 *      surface, terrain being solid only from above, and took what lies under it); a surface with another above it
 *      counts only when looking up from it meets that one (a real roof), and she must fit standing on it;
 *   2. the height: at most `rise` m up and 3 m down;
 *   3. the way: nothing solid between where she stands and the spot, at hip and at head height, both ways (a wall, a
 *      hillside, a rock, a building's shell are met from outside; enemies are not walls);
 *   4. the ground: looking down from her head at the spot meets its surface, not something else first.
 * *why says which check refused (for the log). 1 found */
/* Nothing solid between two points. The stretch within `skip` m of `a` is left out when `a` is where she stands: a
 * ray that starts inside her meets her own gear (0.4.5 refused every dash on open flat ground at this check). The
 * first refusals are logged with what was met. */
static int Clear(uint32_t unit, const float a[3], const float b[3], float skipA, float skipB)
{
    float v[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len < skipA + skipB + 0.1f) return 1;
    for (int k = 0; k < 3; k++) v[k] /= len;
    float from[3] = {a[0] + v[0] * skipA, a[1] + v[1] * skipA, a[2] + v[2] * skipA};
    len -= skipA + skipB;
    uintptr_t world = Pointer(game + 0x346BFA0);
    if (!world || !g_raycast || !g_worldId) return 0;
    uint32_t id = g_worldId((const void*)world);
    if (id >= 4) return 0;
    uint8_t hits[16 * 44];
    memset(hits, 0, sizeof hits);
    uint32_t n = g_raycast(id, from, v, len, 2, 1, 0, 0x80000009u, unit, hits, 16);
    for (uint32_t i = 0; i < n && i < 16; i++)
    {
        float d;
        memcpy(&d, hits + 44u * i + 24, 4);
        uint32_t hu = Word(hits + 44u * i + 28);
        if (hu == unit || !isfinite(d) || d < 0.02f || d > len || IsFoeUnit(hu)) continue;
        uint32_t filt = ActorFilter(Word(hits + 44u * i + 32), hu);
        /* not walls: leaves; bodies (0x236122F0, 0xD5B33D56: enemies, corpses, and her own, which 0.4.7's look back
         * at her met a metre before her); and 0xEDC5EBE0, which every look forward met 0.7 m in, wherever she stood
         * (something of hers that moves with her). The ground has no unit and always counts. */
        if (filt == FILTER_FOLIAGE || (hu && (filt == 0x236122F0u || filt == 0xD5B33D56u || filt == 0xEDC5EBE0u))) continue;
        static unsigned told;
        if (told++ < 24) Log("in the way: %.2f m along a %.1f m look from %.1f %.1f %.1f: unit %u, filter %08x (%u hits)", (double)d, (double)len, (double)from[0], (double)from[1], (double)from[2], hu, filt, n);
        return 0;
    }
    return 1;
}
static int SafeSpot(uint32_t unit, const float pos[3], const float dir[2], float dist, float rise, float out[3], int* why)
{
    /* what is over her where she stands (on real ground) and is not a roof is no ground anywhere on this look */
    {
        float zs[16], keepC = s_lpClimb;
        g_nPhantom = 0;
        s_lpClimb = 12;
        int n = Surfaces(unit, pos[0], pos[1], pos[2], zs, 16);
        s_lpClimb = keepC;
        for (int i = 0; i < n && g_nPhantom < 8; i++)
            if (zs[i] > pos[2] + 1.0f && !Roofed(unit, pos[0], pos[1], pos[2], zs[i] - pos[2]))
            {
                static unsigned told;
                if (told++ < 6) Log("not ground: something %.2f m over her (at %.2f) that a look up does not meet; left out of the spots", (double)(zs[i] - pos[2]), (double)zs[i]);
                g_phantom[g_nPhantom++] = zs[i];
            }
    }
    float keep = s_lpClimb;
    s_lpClimb = 12;   /* (40 in 0.4.5: something high over open ground, met only from above, made the ground under it read as buried) */
    int r = SpotAt(unit, pos, dir, dist, out, NULL, 0);
    s_lpClimb = keep;
    if (!r) { *why = 1; return 0; }
    if (out[2] - pos[2] > rise || pos[2] - out[2] > 3) { *why = 2; return 0; }
    for (int h = 0; h < 2; h++)
    {
        float up = h ? 1.6f : 0.9f, a[3] = {pos[0], pos[1], pos[2] + up}, b[3] = {out[0], out[1], out[2] + up};
        if (!Clear(unit, a, b, 0.7f, 0) || !Clear(unit, b, a, 0, 0.7f)) { *why = 3; return 0; }
    }
    float head[3] = {out[0], out[1], out[2] + 1.7f}, down[3] = {0, 0, -1};
    float g = RayWith(unit, head, down, 2.2f, 0, 0x80000009u, NULL);
    if (g < 0 || fabsf(g - 1.7f) > 0.35f) { *why = 4; return 0; }
    /* 5. the game's own answer: her mover shape overlaps nothing there. On a slope the shape set exactly on the surface
     * touches the uphill side, so a little above it is tried too (she is set where it fits) */
    static unsigned told;
    uint32_t me = atomic_load(&g_avatar);
    for (int k = 0; k < 3; k++)
    {
        float at[3] = {out[0], out[1], out[2] + (k == 0 ? 0.05f : k == 1 ? 0.2f : 0.4f)};
        int fit = GameFits(me, unit, at);
        if (fit < 0) return 1;                       /* not asked: the four above decide */
        if (fit) { if (told++ < 12) Log("the game's fit test: fits at %.1f %.1f %.2f (%.2f above the surface)", (double)at[0], (double)at[1], (double)at[2], (double)(at[2] - out[2])); out[2] = at[2] - 0.05f; return 1; }
    }
    if (told++ < 12) Log("the game's fit test: does not fit at %.1f %.1f %.2f", (double)out[0], (double)out[1], (double)out[2]);
    *why = 5;
    return 0;
}
static void CounterFrame(void)
{
    if (!atomic_load(&g_counterReady)) return;
    Counter c = g_counter;
    atomic_store(&g_counterReady, 0);
    uint32_t me = atomic_load(&g_avatar), foe = 0, myUnit = UnitOf(me);
    float mine[3], m[16], q[3];
    for (int k = 0; k < FOES && !foe; k++) if (g_foe[k] && (g_foe[k] == c.dealer || g_foe[k] == c.owner)) foe = g_foe[k];
    if (!foe)
    {
        float bd = 36;
        for (int k = 0; k < FOES; k++)
            if (g_foe[k] && FoeRoot(g_foe[k], g_foeRes[k], q) && Dist2(q, c.origin) < bd) { bd = Dist2(q, c.origin); foe = g_foe[k]; }
    }
    if (!PlayerPose(mine, NULL)) return;
    /* the counter strikes whoever is in range, wherever she ends up */
    double skill = (g_now < g_eUntil ? s_eMult : 1) * (g_now < g_dUntil ? s_dMult : 1);
    g_volDamage = (int32_t)(s_parryDamage * skill);
    g_volPrimary = 0;
    g_volPursuit = 0;
    g_volOwed = s_parryDamage > 0 ? 1 : 0;
    g_areaAt = g_now;
    g_counters++;
    uint32_t unit = UnitOf(foe);
    if (!foe || !unit || !UnitMatrix(unit, 0, m)) { Log("PARRY: the attacker is not known (dealer %u, owner %u): no step behind it", c.dealer, c.owner); return; }
    if (hf->apply_status && s_parryStun > 0) Act("the parry's stun", hf->apply_status(self, foe, HF_STATUS_STUN_MEDIUM, s_parryStun));
    float at[3] = {m[12], m[13], m[14]}, fwd[2] = {m[4], m[5]}, h = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1]);
    if (h < 0.1f) { fwd[0] = at[0] - mine[0]; fwd[1] = at[1] - mine[1]; h = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1]); }
    float dist = sqrtf(Dist2(at, mine));
    if (h < 0.01f || dist > 25 || !g_setPosition || s_parryBehind <= 0 || !QueryIdle()) { Log("PARRY: entity %u stunned, %.0f m away: no step behind it", foe, (double)dist); return; }
    float back[2] = {-fwd[0] / h, -fwd[1] / h}, dest[3];
    NearFoeUnits(at, 20);
    g_buried = 0;
    /* behind it, as its spot is seen from where it stands; and nothing solid between her and that spot */
    int why = 0, okSpot = SafeSpot(myUnit, at, back, s_parryBehind, 2.0f, dest, &why) || SafeSpot(myUnit, at, back, s_parryBehind + 1.5f, 2.0f, dest, &why);
    if (okSpot)
        for (int hh = 0; hh < 2 && okSpot; hh++)
        {
            float up = hh ? 1.6f : 0.9f, a[3] = {mine[0], mine[1], mine[2] + up}, b[3] = {dest[0], dest[1], dest[2] + up};
            if (!Clear(myUnit, a, b, 0.7f, 0)) { okSpot = 0; why = 3; }
        }
    if (!okSpot) { Log("PARRY: entity %u stunned; no safe spot behind it (check %d)", foe, why); return; }
    float zero[3] = {0, 0, 0};
    dest[2] += 0.05f;
    g_setPosition(NULL, me, dest);
    g_setVelocity(NULL, me, zero);
    static unsigned told;
    if (told++ < 20) Log("PARRY: behind entity %u (%.1f m from her), stunned; her counter strikes everyone in range for %d", foe, (double)dist, g_volDamage);
}
/* the way she moves: her root each update; her movement is the last 0.15 to 0.5 s of it, at 2 m/s or more */
static float g_moveP[16][3];
static double g_moveT[16], g_dashReady;
static unsigned g_moveN;
static float g_anchor[3];
static double g_anchorAt;   /* since when she has stayed within half a metre */
static void MoveTrack(void)
{
    float p[3];
    if (!PlayerPose(p, NULL)) return;
    unsigned k = g_moveN++ % 16;
    memcpy(g_moveP[k], p, sizeof p);
    g_moveT[k] = g_now;
    if (Dist2(p, g_anchor) > 0.25f) { memcpy(g_anchor, p, sizeof p); g_anchorAt = g_now; }   /* she got somewhere */
}
static int MoveDir(float dir[2])
{
    if (g_moveN < 2) return 0;
    unsigned last = (g_moveN - 1) % 16;
    for (unsigned back = 1; back < 16 && back < g_moveN; back++)
    {
        unsigned k = (g_moveN - 1 - back) % 16;
        double dt = g_moveT[last] - g_moveT[k];
        if (dt < 0.15) continue;
        if (dt > 0.5) return 0;
        float dx = g_moveP[last][0] - g_moveP[k][0], dy = g_moveP[last][1] - g_moveP[k][1], d = sqrtf(dx * dx + dy * dy);
        if (d / dt < 2.0) return 0;
        dir[0] = dx / d; dir[1] = dy / d;
        return 1;
    }
    return 0;
}
static void Dash(void)
{
    float dir[2], pos[3], dest[3], p[3];
    uint32_t me = atomic_load(&g_avatar), unit = UnitOf(me);
    /* only while she moves (0.4.5: 0.4.3's way out of a stuck spot, a dash while standing still, fired where she was
     * not stuck; the spot is checked before the move instead, so there is nothing to get out of) */
    if (!s_dash || g_now < g_dashReady || !g_setPosition || !g_setVelocity || !g_raycast || !unit || !MoveDir(dir) || !PlayerPose(pos, NULL) || !QueryIdle()) return;
    NearFoeUnits(pos, s_dashDistance + 20);
    g_buried = 0;
    float dist = 0;
    int found = 0, stuck = 0, why = 0, whys[6] = {0};
    for (float d = s_dashDistance; !found && d >= 1.5f; d -= 0.75f)
    {
        if (SafeSpot(unit, pos, dir, d, d * 0.6f > 1.0f ? d * 0.6f : 1.0f, dest, &why)) { found = 1; dist = d; }
        else whys[why]++;
    }
    if (!found)
    {
        static unsigned told;
        if (told++ < 20) Log("dash: no safe spot ahead (refused: %d no surface she fits on, %d too high or too low, %d something in the way, %d not the ground she would stand on, %d the game's fit test)", whys[1], whys[2], whys[3], whys[4], whys[5]);
        return;
    }
    g_dashReady = g_now + s_dashCooldown;
    /* who stands on the path, before she moves: from a metre behind her to the enemy she stops in front of (a spot an
     * enemy stands on is never taken, so the one that cut the dash short is 2 to 3 m past where she lands) */
    uint32_t hit[VOL];
    int nh = 0;
    for (int j = 0; j < FOES && nh < VOL; j++)
    {
        uint32_t id = g_foe[j];
        if (!id || !FoeRoot(id, g_foeRes[j], p)) continue;
        float rx = p[0] - pos[0], ry = p[1] - pos[1], along = rx * dir[0] + ry * dir[1], across = fabsf(ry * dir[0] - rx * dir[1]);
        if (along < -1.0f || along > dist + 3.0f || across > s_dashWidth + 1.0f || fabsf(p[2] - pos[2]) > 4 || !Living(id)) continue;
        hit[nh++] = id;
    }
    float zero[3] = {0, 0, 0};
    dest[2] += 0.05f;
    g_setPosition(NULL, me, dest);
    g_setVelocity(NULL, me, zero);
    g_dashes++;
    if (nh && s_dashDamage > 0)
    {
        double skill = (g_now < g_eUntil ? s_eMult : 1) * (g_now < g_dUntil ? s_dMult : 1);
        memset(&g_dashVol, 0, sizeof g_dashVol);
        g_dashVol.damage = (int32_t)(s_dashDamage * skill);
        g_dashVol.at = g_now;
        for (int i = 0; i < nh; i++)
        {
            g_dashVol.tgt[g_dashVol.n] = hit[i]; g_dashVol.left[g_dashVol.n] = 1; g_dashVol.n++;
            if (!StrikeBeside(hit[i], dest)) { g_dashVol.n--; continue; }
            g_dashHits++;
        }
    }
    static unsigned told;
    if (told++ < 60) Log("dash: %.1f m%s, from %.1f %.1f %.1f to %.1f %.1f %.1f, %d enem%s on the path", (double)dist,
        stuck == 1 ? " out from under the ground" : stuck == 2 ? " out of a spot she was wedged in" : "", (double)pos[0], (double)pos[1], (double)pos[2],
        (double)dest[0], (double)dest[1], (double)dest[2], nh, nh == 1 ? "y" : "ies");
}
/* ---- her keyed skills ----------------------------------------------------------------------------------------------- */
static void Endowment(void)
{
    if (g_now < g_eUntil || g_now < g_eReady) return;
    g_eUntil = g_now + s_eSeconds;
    g_eReady = g_eUntil + s_eCooldown;
    g_eSlowAt = 0;
    g_eCasts++;
    Log("Endowment: %.0f s, her hits x%.2f, enemies within %.0f m slowed", (double)s_eSeconds, (double)s_eMult, (double)s_eRadius);
}
static void Descent(void)
{
    if (g_now < g_dUntil || g_now < g_dReady) return;
    g_dUntil = g_now + s_dSeconds;
    g_dCasts++;
    Log("Descent: %.0f s out of hiding, no hit on her lands, her hits x%.2f; each kill near her adds %.0f s", (double)s_dSeconds, (double)s_dMult, (double)s_dPerKill);
}
/* In-game key bindings (0.4.2): the HUD script registers Endowment, Descent and the dash with Mod Bindings Menu and
 * passes each press here (skill 1 Endowment, 2 Descent, 3 the dash); ascalon_key gives the .ini keys as their defaults. */
static atomic_int g_bindPress;
HF_MOD_EXPORT int ascalon_input(unsigned version, int skill)
{
    if (version != 1) return 0;
    if (skill >= 1 && skill <= 3) atomic_fetch_or(&g_bindPress, 1 << (skill - 1));
    return 1;
}

static void SkillFrame(void)
{
    static int inDescent;
    if (g_now < g_dUntil) inDescent = 1;
    else if (inDescent)
    {
        inDescent = 0;
        g_dReady = g_now + s_dCooldown;
        g_sensedUntil = g_now + s_dGrace;      /* hidden again after the grace */
        Log("Descent over (%u kills so far this mission): hidden again in %.0f s", g_dKills, (double)s_dGrace);
    }
    if (g_now < g_eUntil && s_eSlow > 0 && g_now >= g_eSlowAt && hf->apply_status)
    {
        g_eSlowAt = g_now + 0.5;
        float me[3], p[3];
        if (PlayerPose(me, NULL))
            for (int j = 0; j < FOES; j++)
            {
                uint32_t id = g_foe[j];
                if (!id || !FoeRoot(id, g_foeRes[j], p) || Dist2(p, me) > s_eRadius * s_eRadius) continue;
                hf->apply_status(self, id, HF_STATUS_FLAMER_SLOWED, s_eSlow);
            }
    }
    static int prev[3], prevR, prevM;
    int focused = Focused();
    int rdown = focused && (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    if (rdown && !prevR) atomic_store(&g_pressMs, GetTickCount64());
    prevR = rdown;
    int mdown = focused && ((GetAsyncKeyState(s_meleeVk) & 0x8000) != 0 || (atomic_load(&g_melee) && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0));
    if (mdown && !prevM && g_alive) Dash();
    prevM = mdown;
    int binds = atomic_exchange(&g_bindPress, 0);
    int mods = focused ? ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? KM_SHIFT : 0) | ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? KM_CTRL : 0) |
                         ((GetAsyncKeyState(VK_MENU) & 0x8000) ? KM_ALT : 0) : 0;
    for (int i = 0; i < 3; i++)
    {
        int own = s_keys[i].vk == VK_SHIFT ? KM_SHIFT : s_keys[i].vk == VK_CONTROL ? KM_CTRL : s_keys[i].vk == VK_MENU ? KM_ALT : 0;
        int down = focused && (GetAsyncKeyState(s_keys[i].vk) & 0x8000) != 0 && (mods & ~own) == s_keys[i].mods;
        static unsigned long long fired[3];
        int bound = (i == 1 && (binds & 1)) || (i == 2 && (binds & 2));   /* the in-game binding's press (0.4.2) */
        if (((down && !prev[i]) || bound) && GetTickCount64() - fired[i] > 300 && g_alive && atomic_load(&g_avatar) && (fired[i] = GetTickCount64(), 1))
        {
            if (i == 1) Endowment();
            else if (i == 2) Descent();
        }
        prev[i] = down;
    }
    if ((binds & 4) && g_alive) Dash();
}
static void OnPlayer(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    if (!p->local) return;
    if (info->kind == HF_ON_PLAYER_SPAWN)
    {
        if (p->entity && p->entity != atomic_load(&g_avatar)) { atomic_store(&g_avatar, p->entity); g_avatarNet = 0; g_cut = 0; }
        g_alive = 1;
        g_sensedUntil = 0; g_sensing = 0;
        LearnNet();
    }
    else { g_alive = 0; g_cut = 0; }
}
static void Reset(const char* why)
{
    if (g_forgot || g_reveals || g_dodged)
        Log("%s: %u shots, heard %u times, revealed %u times, %u times an enemy forgot her, %u hits dodged; %u area volleys (%u strikes landed), Pursuit %u times; %u enemies marked, %u ticks landed, %u bleed hits",
            why, g_shots, g_heard, g_reveals, g_forgot, g_dodged, g_volleys, g_strikes, g_pursuits, g_marked, g_ticks, g_bleeds);
    int start = !strcmp(why, "mission start");
    if (start) KitDecide();
    Body(start);
    g_boomPending = 0; g_boomArmed = 0;
    g_sensedUntil = g_forgetAt = 0;
    g_sensing = 0;
    g_forgot = g_reveals = g_dodged = g_heard = g_shots = 0;
    g_volleys = g_strikes = g_pursuits = 0;
    g_marked = g_ticks = g_bleeds = 0; g_tickSent = g_tickSentLine = g_tickLandLine = g_tickLate = g_tickAgain = 0;
    if (g_eCasts || g_dCasts) Log("%s: Endowment %u times (%u stacks spread), Descent %u times (%u kills, %u hits on her nulled)", why, g_eCasts, g_spread, g_dCasts, g_dKills, g_nulled);
    if (g_parries || g_dashes) Log("%s: %u parries (%u counters), %u dashes (%u enemies struck on the way)", why, g_parries, g_counters, g_dashes, g_dashHits);
    g_parries = g_counters = g_dashes = g_dashHits = 0;
    g_dashReady = 0; g_moveN = 0;
    atomic_store(&g_counterReady, 0);
    g_eUntil = g_eReady = g_dUntil = g_dReady = 0;
    g_eCasts = g_dCasts = g_dKills = g_spread = g_nulled = 0;
    memset(g_mark, 0, sizeof g_mark);
    g_volOwed = 0; g_nslow = 0; g_vol.n = 0; g_dashVol.n = 0;
    g_charges = s_pursuitCharges; g_chargeAt = g_now;
    g_noiseAt = -100;
    memset(g_heardUntil, 0, sizeof g_heardUntil);
    g_cut = 0;
    atomic_store(&g_state, AWAY);
}
static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gameSize = 0, exeSize = 0;
    uintptr_t g = hf->module_base("game.dll", &gameSize), x = hf->module_base("helldivers2.exe", &exeSize);
    if (!g || !x || gameSize != 74727424u || exeSize != 60719104u) { Log("REFUSED: the game's modules are not build " BUILD "'s: her kit stays off"); return; }
    game = g;
    exe = x;
    HitsInstall("the dodge");
    if (hf->code_matches(exe + 0x79F860, "33 c0 4c 8d 05 c7 b2 01 02 0f 1f 80 00 00 00 00 49 39 0c c0 74 0c ff c0 83 f8 04 75 f3") &&
        hf->code_matches(exe + 0x7F4590, "48 8b c4 f3 0f 11 58 20 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48"))
    {
        g_worldId = (WorldFn)(exe + 0x79F860);
        g_raycast = (RayFn)(exe + 0x7F4590);
    }
    else Log("the game's raycast is not the reviewed code: her area strikes do not check for walls");
    if (g_raycast && hf->code_matches(game + 0x5A2BB0, "48 89 5c 24 08 48 89 6c 24 10 56 57 41 54 41 56 41 57 48 83 ec 40") &&
        hf->code_matches(game + 0x4A7550, "48 89 5c 24 18 57 48 83 ec 20 3b 15 ?? ?? ?? ?? 48 8b 1d"))
    {
        g_setPosition = (SetVecFn)(game + 0x5A2BB0);
        g_setVelocity = (SetVecFn)(game + 0x4A7550);
    }
    else Log("the position setter is not the reviewed code: no dash, and the parry does not set her behind the attacker");
    if (hf->code_matches(game + 0x68C340, "40 55 53 56 57 41 56 48 8d ac 24 e0 fe ff ff 48 81 ec 20 02 00 00 0f 29") &&
        hf->code_matches(game + 0x68C230, "40 53 48 83 ec 30 3b 15 e4 79 df 02 4c 8b d1 48 89 7c 24 50"))
        g_fitsAt = (FitsAtFn)(game + 0x68C340);
    else Log("the game's fit test is not the reviewed code: her own checks decide where she may land");
    g_linksOk = hf->code_matches(exe + 0x2BEB9A, "48 8d b9 d8 01 00 00") && hf->code_matches(exe + 0x2BEBFA, "48 81 c7 e0 01 00 00");
    if (!g_linksOk) Log("the engine's unit links are not where the research has them: her gear is found by position (it may stay drawn)");
    g_staminaOk = hf->code_matches(game + 0xA50E2D, "f3 42 0f 11 8c 11 54 78 54 00");
}

/* ---- the HUD state (read by the Ascalon HUD addon through LuaJIT's ffi) ------------------------------------------ */
typedef struct AscalonHud
{
    uint32_t version, in_mission;
    uint32_t state;                 /* 0 hidden, 2 revealed, 3 not hidden (gear), 4 away */
    float reveal_left;              /* seconds until hidden again once nobody senses her (state 2) */
    uint32_t cloak_visual, hide_gear;
    uint32_t units[5];              /* scene units: her Helldiver, then her primary, secondary, support weapon, backpack (0: none) */
    uint32_t sensing;               /* enemies that sense her now */
    float e_left, e_ready, d_left, d_ready;   /* 0.3.0: Endowment and Descent: seconds left of each, seconds until ready */
    uint32_t keys[3];
    char key_label[3][16];
    uint32_t charges, charges_max;  /* 0.2.0: Pursuit */
    float charge_left;              /* seconds until the next charge (0: full) */
    uint32_t nwanted;               /* 0.1.3: her scene units, by the engine's own links (0: not known, use units[]) */
    uint32_t wanted[256];
} AscalonHud;
static AscalonHud g_hud = {3};
/* 0.1.3: what she is made of, by the engine's own unit links (the Optical Cloak Pack's way): her Helldiver's unit and
 * every unit linked under it (first child at Unit +0x1D8, next sibling at +0x1E0), and the same from each item of her
 * equipment row (ten slots) that the attach manager (game+0x3326DC0: map +32, rows +64, 48 bytes, parent +4) says hangs
 * on her. 0.1.2 looked for units sharing an item's root and found the weapons' entity units but not what is drawn. */
static uintptr_t UnitObject(uint32_t unit)
{
    if (!exe || !unit) return 0;
    uintptr_t registry = Pointer(exe + 0x1A100F0), generations, objects, object;
    uint32_t count, id, slot = unit & 0x3FFFFF;
    uint8_t generation;
    if (!registry || !ReadAt(registry + 0x98, &count, 4) || count > 0x400000 || slot >= count ||
        !(generations = Pointer(registry + 0xA0)) || !ReadAt(generations + slot, &generation, 1) || generation != (uint8_t)(unit >> 22) ||
        !(objects = Pointer(registry + 0x88)) || !(object = Pointer(objects + 8u * slot)) || !ReadAt(object + 8, &id, 4) || id != unit)
        return 0;
    return object;
}
static void HerUnits(void)
{
    uint32_t me = atomic_load(&g_avatar), ids[256], skip[10], i;
    int n = 0, nskip = 0, head = 0;
    g_hud.nwanted = 0;
    if (!g_linksOk || !me) return;
    uint32_t mine = UnitOf(me);
    if (!mine || !UnitObject(mine)) return;
    ids[n++] = mine;
    uintptr_t m = Pointer(game + 53634872u), recs, attach = Pointer(game + 0x3326DC0), rows = attach ? Pointer(attach + 64) : 0;
    uint8_t rec[48];
    if (m && rows && Lookup(m + 40, me, 1048576, &i) && i < 0x10000 && (recs = Pointer(m + 80)) && ReadAt(recs + 48u * i, rec, 48))
        for (int k = 0; k < 10; k++)
        {
            uint32_t e = Word(rec + 4u * k), ai, parent = 0, u;
            if (!e || e == UINT32_MAX || !Lookup(attach + 32, e, 8192, &ai) || ai >= 4096 || !ReadAt(rows + 48ull * ai + 4, &parent, 4) || parent != me) continue;
            if (!(u = UnitOf(e)) || !UnitObject(u)) continue;
            if (s_hideGear) { if (n < 256) ids[n++] = u; }
            else skip[nskip++] = u;
        }
    while (head < n)
    {
        uintptr_t child = Pointer(UnitObject(ids[head++]) + 0x1D8);
        for (int guard = 0; child && guard < 256; guard++)
        {
            uint32_t id = 0;
            if (!ReadAt(child + 8, &id, 4) || UnitObject(id) != child) break;
            int seen = 0;
            for (int k = 0; k < n && !seen; k++) seen = ids[k] == id;
            for (int k = 0; k < nskip && !seen; k++) seen = skip[k] == id;
            if (!seen && n < 256) ids[n++] = id;
            child = Pointer(child + 0x1E0);
        }
    }
    memcpy(g_hud.wanted, ids, 4u * (unsigned)n);
    g_hud.nwanted = (uint32_t)n;
    static int told = -1;
    if (n != told) { static unsigned times; if (times++ < 12) Log("her units by the engine's links: %d (her Helldiver's, its parts and %s)", n, s_hideGear ? "her gear" : "no gear"); told = n; }
}
/* How many parries so far (version 1): the HUD script plays the parry's clash (data/, ascalon_parry) when it rises. */
HF_MOD_EXPORT unsigned ascalon_parry_seq(unsigned version) { return version == 1 ? atomic_load(&g_parrySeq) : 0; }
HF_MOD_EXPORT AscalonHud* ascalon_hud_state(void)
{
    int st = atomic_load(&g_state);
    g_hud.in_mission = (uint32_t)(g_inMission && g_alive && kb_kit_on);
    g_hud.state = (uint32_t)st;
    g_hud.reveal_left = st == REVEALED && !g_sensing ? (float)fmax(0, g_sensedUntil - g_now) : 0;
    g_hud.e_left = (float)fmax(0, g_eUntil - g_now); g_hud.e_ready = (float)fmax(0, g_eReady - g_now);
    g_hud.d_left = (float)fmax(0, g_dUntil - g_now); g_hud.d_ready = g_now < g_dUntil ? 0 : (float)fmax(0, g_dReady - g_now);
    for (int i = 0; i < 3; i++) { g_hud.keys[i] = (uint32_t)s_keys[i].vk; snprintf(g_hud.key_label[i], sizeof g_hud.key_label[i], "%s", s_keys[i].label); }
    g_hud.sensing = (uint32_t)g_sensing;
    g_hud.charges = (uint32_t)g_charges;
    g_hud.charges_max = (uint32_t)s_pursuitCharges;
    g_hud.charge_left = g_charges < s_pursuitCharges ? (float)fmax(0, s_pursuitEvery - (g_now - g_chargeAt)) : 0;
    g_hud.cloak_visual = (uint32_t)s_cloakVisual;
    g_hud.hide_gear = (uint32_t)s_hideGear;
    return &g_hud;
}

/* ---- the frame -------------------------------------------------------------------------------------------------- */
static void Tick(HfMod* mod, float dt, void* user)
{
    g_now += dt > 0 && dt < 1 ? dt : 0;
    int inMission = hf->game_state(self) == HF_STATE_MISSION;
    if (inMission != g_inMission) { Reset(inMission ? "mission start" : "mission end"); g_inMission = inMission; }
    if (!g_inMission || !kb_kit_on) { atomic_store(&g_state, AWAY); return; }
    MyItems();
    int st = StateNow(), was = atomic_exchange(&g_state, st);
    if (st != was)
    {
        static unsigned told;
        if (told++ < 60) Log("she is %s", kStateName[st]);
        if (st == REVEALED) g_reveals++;
    }
    MoveTrack();
    SkillFrame();
    CounterFrame();
    VisCut(st == HIDDEN || st == REVEALED);   /* the cut stays while revealed: sight alone must not hold her revealed */
    if ((st == HIDDEN || st == REVEALED) && g_now >= g_forgetAt)
    {
        g_forgetAt = g_now + s_forgetEvery;
        ForgetPass();
    }
    static double unitsAt;
    if (g_now >= unitsAt) { unitsAt = g_now + 0.25; HerUnits(); }
    g_hud.units[0] = UnitOf(atomic_load(&g_avatar));
    for (int k = 0; k < 4; k++) g_hud.units[1 + k] = UnitOf(atomic_load(&g_myItems[k]));
    AreaFrame();
    MarkFrame();
    ViewFrame();
    Exhaustion();
    atomic_exchange(&g_ownStopped, 0);
    atomic_exchange(&g_kdStopped, 0);
}

/* ---- Mod Options Menu ------------------------------------------------------------------------------------------- */
static const KbOption kOptions[] = {
    {"area_radius", &s_areaRadius, NULL}, {"area_mult", &s_areaMult, NULL}, {"area_max", NULL, &s_areaMax}, {"pursuit_mult", &s_pursuitMult, NULL},
    {"pursuit_every", &s_pursuitEvery, NULL}, {"pursuit_slow", &s_pursuitSlow, NULL}, {"pursuit_charges", NULL, &s_pursuitCharges},
    {"e_seconds", &s_eSeconds, NULL}, {"e_cooldown", &s_eCooldown, NULL}, {"e_mult", &s_eMult, NULL}, {"e_radius", &s_eRadius, NULL},
    {"e_slow", &s_eSlow, NULL}, {"e_spread", &s_eSpread, NULL}, {"d_seconds", &s_dSeconds, NULL}, {"d_cooldown", &s_dCooldown, NULL},
    {"d_mult", &s_dMult, NULL}, {"d_per_kill", &s_dPerKill, NULL}, {"d_cap", &s_dCap, NULL},
    {"parry", NULL, &s_parry}, {"parry_sound", NULL, &s_parrySound}, {"parry_volume", &s_parryVolume, NULL}, {"parry_window", &s_parryWindow, NULL}, {"parry_damage", &s_parryDamage, NULL}, {"dash", NULL, &s_dash},
    {"dash_distance", &s_dashDistance, NULL}, {"dash_cooldown", &s_dashCooldown, NULL}, {"dash_damage", &s_dashDamage, NULL},
    {"mi_stacks", NULL, &s_miStacks}, {"mi_seconds", &s_miSeconds, NULL}, {"mi_damage", &s_miDamage, NULL}, {"mi_slow", &s_miSlow, NULL}, {"mi_status", NULL, &s_miStatus}, {"mi_status_buildup", &s_miBuildup, NULL},
    {"numbers", NULL, &s_numbers},
    {"visual_cut", &s_visualCut, NULL}, {"reveal_hold", &s_revealHold, NULL}, {"noise_window", &s_noiseWindow, NULL}, {"heard_hold", &s_heardHold, NULL}, {"dodge", &s_dodge, NULL},
    {"gear_rule", NULL, &s_gearRule}, {"cloak_visual", NULL, &s_cloakVisual}, {"hide_gear", NULL, &s_hideGear},
    {"hp_mult", &s_hpMult, NULL}, {"stamina_mult", &s_staminaMult, NULL},
};
HF_MOD_EXPORT int ascalon_key(unsigned version, int skill, int* vk, int* mods)
{
    if (version != 1 || skill < 1 || skill > 3 || !vk || !mods) return 0;
    if (skill == 3) { *vk = s_meleeVk; *mods = 0; return 1; }
    *vk = s_keys[skill].vk;
    *mods = s_keys[skill].mods;
    return 1;
}
HF_MOD_EXPORT int ascalon_option_get(const char* name, double* out) { return OptionGet(name, out); }
HF_MOD_EXPORT int ascalon_option_set(const char* name, double value) { return OptionSet(name, value); }

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    RosterJoin("ascalon", "Ascalon");
    kb_ini = "ascalon.ini";
    kb_key_names[0] = "key_spare";
    kb_key_names[1] = "key_endowment";
    kb_key_names[2] = "key_descent";
    KB_SETTINGS(kSettings);
    KB_OPTIONS(kOptions);
    kb_damage_taken = DamageTaken;
    kb_damage_dealt = DamageDealt;
    kb_hit_taken = HitTaken;
    /* her body: Arknights rules, no T-Doll Hellbomb */
    s_hpMult = 1.0f;
    s_staminaMult = 1.2f;
    s_hellbomb = 0;
    if (!api || api->version < 13 || api->size < offsetof(HfApi, code_matches) + sizeof api->code_matches) return 1;
    if (!api->build_id() || strcmp(api->build_id(), BUILD)) { api->log(mod, "REFUSED: game build must be " BUILD); return 1; }
    LoadSettings();
    ParrySoundInit();
    api->log(mod, "Ascalon %s: hidden until she makes noise; her hits strike everyone within %.0f m; Pursuit; Endowment %s, Descent %s", VERSION,
             (double)s_areaRadius, s_keys[1].label, s_keys[2].label);
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamagePre, NULL, 0) && api->subscribe(mod, HF_ON_KILL, OnKill, NULL, 0) &&
             api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) && api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) &&
             api->subscribe(mod, HF_ON_PLAYER_DOWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) &&
             api->subscribe(mod, HF_ON_WEAPON, OnWeaponAsc, NULL, 0) && api->subscribe(mod, HF_ON_STAT, OnStat, NULL, 0) &&
             api->on_game_thread(mod, Tick, NULL);
    if (!ok) api->log(mod, "events or the game thread are unavailable: Ascalon stays off");
    return 0;
}
