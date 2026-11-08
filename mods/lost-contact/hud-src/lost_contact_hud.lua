-- Lost Contact options (mods/yui/lost_contact_hud): Lost Contact's settings as rows on the escape menu's MODS tab
-- (ESC > OPTIONS > MODS), through Mod Options Menu by CowboyBingus (its public Lua api 1, the menu the public Vanilla
-- Plus Megapack has). Lost Contact is a HellForge code mod; this reads and sets its settings through LuaJIT's ffi
-- (lost_contact.dll exports lost_contact_menu, lost_contact_option_get and lost_contact_option, version 1). The DLL
-- keeps the settings and saves the tab's changes (%LOCALAPPDATA%\HellForgeMods\lost-contact.ini), so the rows always
-- show what lost_contact.dll runs, the keys (Ctrl+F9 / Ctrl+F10) included. Draws nothing: HD2Runtime's overlay is only
-- its per-frame step. Requires Bingus Shared Loader and HD2Runtime; uses HD2Runtime by SkyeShade
-- (https://github.com/SkyeShade/HD2Runtime) through its public API. Without them Lost Contact works as before.
local OWNER = 'mods/yui/lost_contact_hud'

local sink, sink_tried
local function log(message)
    pcall(print, message)
    if not sink_tried then
        sink_tried = true
        local ok, result = pcall(function() return CowboyBingusModLoader.open_log('LostContactHUD.log') end)
        if ok then sink = result end
    end
    if sink then
        if not pcall(function() sink:write(message .. '\n'); if sink.flush then sink:flush() end end) then sink = nil end
    end
end

local ok, hd2 = pcall(require, 'mods/skyeshade/hd2runtime')
if not ok or type(hd2) ~= 'table' or type(hd2.ui) ~= 'table' or type(hd2.ui.overlay) ~= 'function' then
    log('[Lost Contact] HD2Runtime with hd2.ui.overlay is not available: no MODS tab rows from Lua (' .. tostring(hd2) .. ')')
    return
end

local ffi = require('ffi')
pcall(ffi.cdef, [[
void *lcopt1_GetModuleHandleA(const char *name) __asm__("GetModuleHandleA");
void *lcopt1_GetProcAddress(void *module, const char *name) __asm__("GetProcAddress");
typedef int (*lcopt1_menu)(unsigned int version, int claim);
typedef int (*lcopt1_get)(unsigned int version, const char *key, double *out);
typedef int (*lcopt1_set)(unsigned int version, const char *key, double value);
]])
local k32 = ffi.load('kernel32')

-- The rows, as lost_contact.c's HD2_MOD_OPTIONS_v1 rows (labels, choices, one description), then the numbers of
-- lost-contact.ini with its ranges. {key, type, label, choices or {min, max, step}, gap}
local MODES = {'Off', 'On', 'Observe (log only)'}
local STATUS = {'Running', 'Off (your setting)', 'Off: the active kit (Character kits can limit)', 'Observing'}
local TEXT = 'Break contact: an enemy that loses sight of you stops tracking you. Patrols rerouted: a patrol sent at you walks ' ..
    'to an outpost away from you. A character kit can turn one off while it is your active kit (Lemuen: patrols only). ' ..
    'Turn Character kits can limit off to keep both as set here. The Now rows show what runs.'
local ROWS = {
    {'break', 'choice', 'Break contact', MODES},
    {'patrols', 'choice', 'Patrols rerouted', MODES},
    {'kits', 'toggle', 'Character kits can limit'},
    {'break_now', 'status', 'Break contact now', STATUS},
    {'patrols_now', 'status', 'Patrols now', STATUS},
    {'lose_after', 'slider', 'Break contact: Lose After', {0.5, 60, 0.5}, true,
        'Seconds an enemy after you (with its squad) must not sense you before it loses you and searches where it last saw you.'},
    {'break_range', 'slider', 'Break contact: Range', {10, 500, 5}, false, 'Metres around you within which enemies can lose you.'},
    {'seek_radius', 'slider', 'Patrols: Seek Radius', {10, 300, 5}, true,
        'A patrol walking to within this many metres of you is one sent at you, and is sent to an outpost instead.'},
    {'keep_away', 'slider', 'Patrols: Keep Away', {20, 600, 10}, false, 'Metres from you an outpost a patrol is sent to must be, at least.'},
    {'patrol_range', 'slider', 'Patrols: Range', {50, 1000, 25}, false, 'Metres around you within which patrols are looked at.'},
    {'resend', 'slider', 'Patrols: Resend', {0.5, 30, 0.5}, false, 'Seconds between sending a rerouted patrol on again (the game aims them again).'},
    {'pass_by', 'slider', 'Patrols: Pass By', {0, 150, 5}, true,
        'Metres from you a patrol setting out walks past, instead of onto you (under 20: off).'},
}
local MOD, MOD_ID = 'Lost Contact', 'yui.lost-contact'

local S = {wait = 0, done = false, failed = false, menu = nil, get = nil, set = nil, out = ffi.new('double[1]'),
    shown = {}, ids = {}, told_old = false, sync = 0}

-- the DLL's value of a row, as the menu holds it (a 1-based choice, a boolean, a number); nil if it has none
local function live(row)
    if S.get(1, row[1], S.out) ~= 1 then return nil end
    local v = tonumber(S.out[0])
    if row[2] == 'toggle' then return v ~= 0 end
    if row[2] == 'slider' then return v end
    return math.floor(v + 0.5) + 1
end

local function register(menu)
    local count = 0
    for i = 1, #ROWS do
        local row = ROWS[i]
        local current = live(row)
        if current ~= nil then
            local id = MOD_ID .. '.' .. row[1]
            local spec = {label = row[3], mod = MOD, mod_id = MOD_ID, description = row[6] or TEXT, gap = row[5] or nil}
            if row[2] == 'toggle' then spec.type = 'toggle'
            elseif row[2] == 'slider' then
                spec.type, spec.min, spec.max, spec.step = 'slider', row[4][1], row[4][2], row[4][3]
                current = math.max(row[4][1], math.min(row[4][2], current))
            else spec.type, spec.choices = 'choice', row[4] end
            spec.default = current
            local okR, why = menu.register_option(id, spec)
            if okR then
                count = count + 1
                S.ids[i] = id
                local key, kind = row[1], row[2]
                menu.on_change(id, function(value)
                    if kind == 'status' then S.shown[i] = nil return end      -- only shows: put back on the next step
                    local v = value
                    if kind == 'toggle' then v = value and 1 or 0 elseif kind == 'choice' then v = (tonumber(value) or 1) - 1 end
                    if type(v) ~= 'number' or S.set(1, key, v) ~= 1 then
                        log('[Lost Contact] ' .. key .. ' = ' .. tostring(value) .. ' was refused by lost_contact.dll')
                    end
                    S.shown[i] = nil
                end)
                S.shown[i] = nil                                              -- the DLL's value, over the menu's saved one
            else
                log('[Lost Contact] ' .. row[1] .. ' not added to the MODS tab: ' .. tostring(why))
            end
        end
    end
    log(('[Lost Contact] %d of %d settings on the MODS tab (Mod Options Menu, api %s, version %s)'):format(count, #ROWS,
        tostring(menu.api), tostring(menu.version)))
end

-- the rows follow the DLL (the keys in a mission, a kit limiting them, the ini): checked twice a second
local function sync(menu)
    for i = 1, #ROWS do
        local id = S.ids[i]
        if id then
            local v = live(ROWS[i])
            if v ~= nil and v ~= S.shown[i] then
                if menu.get(id) ~= v then menu.set(id, v) end
                S.shown[i] = v
            end
        end
    end
end

local function step(dt)
    if S.done then
        S.sync = S.sync - dt
        if S.sync > 0 then return end
        S.sync = 0.5
        sync(S.menu)
        return
    end
    S.wait = S.wait - dt
    if S.wait > 0 then return end
    S.wait = 2
    local menu = rawget(_G, 'ModOptionsMenu')
    if type(menu) ~= 'table' or menu.api ~= 1 or type(menu.register_option) ~= 'function' then return end
    local module = k32.lcopt1_GetModuleHandleA('lost_contact.dll')
    if module == nil then return end                                         -- not installed, or not loaded yet
    local mp = k32.lcopt1_GetProcAddress(module, 'lost_contact_menu')
    local gp = k32.lcopt1_GetProcAddress(module, 'lost_contact_option_get')
    local sp = k32.lcopt1_GetProcAddress(module, 'lost_contact_option')
    if mp == nil or gp == nil or sp == nil then
        if not S.told_old then S.told_old = true; log('[Lost Contact] this lost_contact.dll has no Lua options (0.3.0 or later has)') end
        S.wait = 30
        return
    end
    -- 1: the rows are ours; 0: the C side's HD2_MOD_OPTIONS_v1 menu has them, or it has not looked for one yet (ask again)
    if ffi.cast('lcopt1_menu', mp)(1, 1) ~= 1 then return end
    S.get, S.set, S.menu, S.done = ffi.cast('lcopt1_get', gp), ffi.cast('lcopt1_set', sp), menu, true
    register(menu)
end

local overlay = hd2.ui.overlay({id = 'options', owner = OWNER})
overlay:draw(function(d, dt)
    if S.failed then return end
    local okS, err = pcall(step, tonumber(dt) or 0)
    if not okS then S.failed = true; log('[Lost Contact] MODS tab rows failed, off for this session: ' .. tostring(err)) end
end)

log('[Lost Contact] options ready (waiting for lost_contact.dll and Mod Options Menu)')
