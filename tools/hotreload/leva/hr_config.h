/* Socket config for Leva (yui.leva): copy to HellForge/mods/<leva>/src/ next to the socket source. */
#define HR_PLUG_PATH "Z:\\HD2HF\\dev\\leva\\leva_plug.dll"
/* leva_hud_state: 0 = copy sizeof(LevaHud) as the plug's HR_STATE_SIZE(leva_hud_state, LevaHud) reports it
 * (156 bytes for LevaHud version 2; put 156 here instead if the plug does not use HR_STATE_SIZE). */
#define HR_EXPORTS(X) X(STATE, leva_hud_state, 0) X(OPT_GET, leva_option_get, 0) X(OPT_SET, leva_option_set, 0)
