/* Veterancy System: what the main file shares with the gear modules. */
#pragma once
#include <stdint.h>
#include "hellforge_mod.h"

double fp_clock(void);
int fp_forced_level(void);
int fp_level2_kills(void);
int fp_level3_kills(void);
int fp_gm_level2_kills(void);
int fp_gm_level3_kills(void);
float fp_confusion_seconds(void);
float fp_gas_taken_mult(void);
float fp_gas_dealt_mult(void);
int fp_gas_vs_players(void);
int fp_gas_level2_kills(void);
int fp_gas_level3_kills(void);
float fp_gas_cooldown_mult(void);
int fp_gm_shield(void);
float fp_gm_ammo_seconds(void);
float fp_gm_stack_seconds(void);
int fp_gm_stack_cap(void);
float fp_gm_pop_damage(void);
float fp_gm_pop_radius(void);
float fp_gm_pop_radius_step(void);
int fp_gm_pop_row(void);
float fp_gm_pop_step(void);
float fp_gm_stack_decay(void);
void gas_tick(double now);
float fp_cloud_radius(void);
float fp_cloud_seconds(void);
int fp_in_mission(void);

int gas_init(const HfApi* hf, HfMod* mod, int element, uint64_t orbital, uint64_t eagle);
void gas_kills(uint64_t resource, int added);
void gas_reset(void);
void gas_set_immune(const char* list);  /* hex entity resources, any separators */

/* gas_doctrine.c, for the others: a hit's dealer (network id) as an entity resource, once mapped */
uint64_t gas_dealer_resource(uint32_t network, uint32_t* entity);
uint64_t gas_entity_resource(uint32_t entity);
float fp_gat_he_rate(void);
int fp_gat_self_immune(void);
int fp_gat_round_base(void);
int fp_gat_he_every(void);
float fp_gat_l3_rpm(void);
/* gatling.c */
int gat_init(const HfApi* hf, HfMod* mod);
void gat_kills(uint64_t resource, int added);
void gat_reset(void);
int fp_gat_level2_kills(void);
int fp_gat_level3_kills(void);
float fp_gat_ramp_per_hit(void);
float fp_gat_ramp_max(void);
int fp_gat_ap(void);
float fp_gat_he_damage(void);
int fp_gat_he_explosion(void);

/* field_promotions.c: an earned level-up's banner for the HUD addon */
void fp_toast(const char* gear, int level);
/* ... with its own line (NULL: the usual one) */
void fp_toast_line(const char* gear, int level, const char* line);
/* the HUD's rank badges: each promotable stratagem's level now */
typedef struct FpGear { uint32_t stratagem, level; } FpGear;
int gas_hud_gear(FpGear* out, int max);
int gat_level(void);
int mg_level(void);

/* laser_sentry.c */
#include <stdio.h>
int ls_init(const HfApi* hf, HfMod* mod);
void ls_kills(uint64_t resource, int added);
void ls_reset(void);
void ls_tick(double now);
int ls_level(void);
int ls_setting(const char* key, double v);
void ls_write_defaults(FILE* f);
/* field_promotions.c: every setting's default, as the shipped .ini (tools/inigen.py) */
void fp_write_defaults(FILE* f);

/* maxigun.c */
int mx_init(const HfApi* hf, HfMod* mod);
void mx_kills(uint64_t resource, int added);
void mx_shots(uint64_t resource, int added);
void mx_reset(void);
void mx_tick(double now);
int mx_level(void);
int mx_setting(const char* key, double v);
void mx_write_defaults(FILE* f);

/* rocket_sentry.c */
int rs_init(const HfApi* hf, HfMod* mod);
void rs_kills(uint64_t resource, int added);
void rs_shots(uint64_t resource, int added);
void rs_reset(void);
void rs_tick(double now);
int rs_level(void);
int rs_setting(const char* key, double v);
void rs_write_defaults(FILE* f);

/* mg_sentry.c */
int mg_init(const HfApi* hf, HfMod* mod);
void mg_kills(uint64_t resource, int added);
void mg_reset(void);
int fp_mg_level2_kills(void);
int fp_mg_level3_kills(void);
float fp_mg_damage_taken(void);
int fp_mg_friendly_immune(void);
float fp_mg_shield_delay(void);
int fp_mg_shield_hits(void);
float fp_mg_shield_recharge(void);
int fp_mg_shield_min_hit(void);
float fp_mg_lifetime_mult(void);
