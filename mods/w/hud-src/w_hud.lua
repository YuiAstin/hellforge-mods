-- W HUD (mods/yui/w_hud): W's skills beside the mission's stratagem list (shown with it, as the stratagems are: icon,
-- name, key, cooldown), her mines and marks marked in the world, and the pose freeze of what her skills hold, drawn
-- with HD2Runtime's screen overlay (hd2.ui.overlay). W is a HellForge code mod; this reads its state through LuaJIT's
-- ffi (w.dll exports w_hud_state, layout version 1, and w_state for the freeze) every frame. Requires Bingus Shared
-- Loader and HD2Runtime; uses HD2Runtime by SkyeShade (https://github.com/SkyeShade/HD2Runtime) through its public API.
-- The freeze: the DLL picks the enemies her skills hold and times each one's hold; this finds each enemy's unit in the
-- game's Lua and switches its animation state machine off while it is held, then on again (the Time Stop Probe's pose
-- freeze, per enemy); one that died held gets its "dead" animation event. The rare-type rule for reading a unit's
-- place is Exo Autopilot's finding (marti).
local OWNER = 'mods/yui/w_hud'

local sink, sink_tried
local function log(message)
    pcall(print, message)
    if not sink_tried then
        sink_tried = true
        local ok, result = pcall(function() return CowboyBingusModLoader.open_log('WHUD.log') end)
        if ok then sink = result end
    end
    if sink then
        if not pcall(function() sink:write(message .. '\n'); if sink.flush then sink:flush() end end) then sink = nil end
    end
end

local ok, hd2 = pcall(require, 'mods/skyeshade/hd2runtime')
if not ok or type(hd2) ~= 'table' or type(hd2.ui) ~= 'table' or type(hd2.ui.overlay) ~= 'function' then
    log('[W HUD] HD2Runtime with hd2.ui.overlay is not available: no HUD, no pose freeze (' .. tostring(hd2) .. ')')
    return
end

local ffi = require('ffi')
pcall(ffi.cdef, [[
typedef struct whud1_state { uint32_t version, in_mission; float cd[3], cd_max[3]; uint32_t s1_on, rounds, charges, charges_max, mines_out, marks, marks_max, blowing, ambush; uint32_t keys[4]; char key_label[4][24]; } whud1_state;
typedef struct wk1_target { uint32_t entity, unit; uint64_t object; float pos[3]; uint32_t pad; } wk1_target;
typedef struct wk1_state { uint32_t version, seq, active, skill, count, rare_max; uint32_t player_unit, in_mission;
    uint64_t player_object; float player_pos[3], value; wk1_target t[48]; uint32_t dead_seq, dead[16]; } wk1_state;
typedef int (*wk1_mines)(unsigned int version, float *out, int max);
typedef int (*wk1_view)(unsigned int version, float *out);
typedef int (*wk1_log)(unsigned int version, const char *text);
void *wk1_GetModuleHandleA(const char *name) __asm__("GetModuleHandleA");
void *wk1_GetProcAddress(void *module, const char *name) __asm__("GetProcAddress");
]])
local k32 = ffi.load('kernel32')

local ELECTRIC, YELLOW, INK, PANEL, TEXT, MUTED = {224, 58, 58}, {255, 232, 0}, {8, 9, 10}, {12, 14, 16}, {240, 243, 245}, {150, 156, 160}
local NAMES = {'KING OF HEARTS', 'JACK IN THE BOX', 'D12'}

-- the kit, for the shared parts below
local KIT = {name = 'W', id = 'w', module = k32.wk1_GetModuleHandleA, proc = k32.wk1_GetProcAddress}
-- kitbase:bindings.lua begin. Shared by the GFL2 kits' HUDs: written by tools/kitbase_sync.py from kitbase/hud/bindings.lua, so edit
-- it there (the kit names itself in KIT, above).
-------------------------------- GFL2_SHARED: cooldowns while the list is closed, in-game key bindings (same in each kit) --
-- Cooldowns: with the GFL2 KITS HUD option "<Kit>: Always Show Cooldowns" on (the default), the skills on cooldown stay
-- on screen while the stratagem list is closed, each on its own (only the ones recharging), where the list shows them.
-- Key bindings: the three skills are registered with Mod Bindings Menu (Vanilla Plus Megapack: ESC > OPTIONS > the
-- keyboard or controller binding page > MODS) as native game actions, the game's own binding rows (they start unbound:
-- only the menu's fixed slots get keyboard defaults); with every native action taken they fall back to extended bindings
-- (MORE MODS) with the .ini's keys as defaults. Each press is passed to the DLL (<kit>_input); the .ini's keys keep
-- working beside them (a press of both within 0.3 s counts once).
local X = {kit = KIT.name, id = KIT.id, always = true, opt_done = false, opt_wait = 0, bind_done = false, bind_wait = 0,
    ids = {}, prev = {}, labels = {}, beat = 0, label_wait = 0, last_box = nil}
pcall(ffi.cdef, [[
typedef int (*gfl2bind_input)(unsigned int version, int skill);
typedef int (*gfl2bind_key)(unsigned int version, int skill, int *vk, int *mods);
]])
local VK_NAMES = {[0x08] = 'backspace', [0x09] = 'tab', [0x0D] = 'enter', [0x14] = 'caps lock', [0x20] = 'space',
    [0x21] = 'page up', [0x22] = 'page down', [0x23] = 'end', [0x24] = 'home', [0x25] = 'left', [0x26] = 'up',
    [0x27] = 'right', [0x28] = 'down', [0x2D] = 'insert', [0x2E] = 'delete', [0x10] = 'left shift', [0x11] = 'left ctrl',
    [0x12] = 'left alt', [0xA0] = 'left shift', [0xA1] = 'right shift', [0xA2] = 'left ctrl', [0xA3] = 'right ctrl',
    [0xA4] = 'left alt', [0xA5] = 'right alt'}
local VK_MOUSE = {[0x04] = 'middle', [0x05] = 'extra 1', [0x06] = 'extra 2'}
-- the .ini key as a binding default ({button, modifiers} with the engine's names), nil when it has no engine name
local function default_spec(vk, mods)
    local button, device
    if vk >= 0x41 and vk <= 0x5A then button = string.char(vk + 32)
    elseif vk >= 0x30 and vk <= 0x39 then button = string.char(vk)
    elseif vk >= 0x70 and vk <= 0x87 then button = 'f' .. (vk - 0x6F)
    elseif vk >= 0x60 and vk <= 0x69 then button = 'numpad ' .. (vk - 0x60)
    elseif VK_MOUSE[vk] then button, device = VK_MOUSE[vk], 'mouse'
    else button = VK_NAMES[vk] end
    if not button then return nil end
    local m = {}
    if mods % 2 == 1 and button ~= 'left shift' then m[#m + 1] = 'left shift' end
    if math.floor(mods / 2) % 2 == 1 and button ~= 'left ctrl' then m[#m + 1] = 'left ctrl' end
    if math.floor(mods / 4) % 2 == 1 and button ~= 'left alt' then m[#m + 1] = 'left alt' end
    return {{button = button, device = device, modifiers = #m > 0 and m or nil}}
end
local function gfl2_dll(name)
    local module = KIT.module(X.id .. '.dll')
    if module == nil then return nil end
    local p = KIT.proc(module, name)
    return p ~= nil and p or nil
end
-- "<Kit>: Always Show Cooldowns" on the MODS tab (its own GFL2 KITS HUD entry: each kit's own entry is full)
pcall(ffi.cdef, [[
typedef int (*gfl2opt_screen)(unsigned int version);
typedef int (*gfl2opt_get)(const char *name, double *out);
]])
-- the character kits' options screen (HellForge's screens over the game) is there: nothing on the Mod Options Menu,
-- "Always Show Cooldowns" read from the screen's HUD row once a second
local function gfl2_screen()
    if X.screen == nil then
        local sp, gp = gfl2_dll(X.id .. '_options_screen'), gfl2_dll(X.id .. '_option_get')
        if not sp then return false end                             -- its DLL not loaded yet, or from before the screen
        X.screen = ffi.cast('gfl2opt_screen', sp)(1) == 1 and gp ~= nil
        if X.screen then X.get, X.out = ffi.cast('gfl2opt_get', gp), ffi.new('double[1]') end
    end
    return X.screen
end
local function gfl2_options(dt)
    if X.opt_done == 'screen' then
        X.opt_wait = X.opt_wait - dt
        if X.opt_wait > 0 then return end
        X.opt_wait = 1
        if X.get('hud_cooldowns', X.out) == 1 then X.always = X.out[0] ~= 0 end
        return
    end
    if X.opt_done then return end
    X.opt_wait = X.opt_wait - dt
    if X.opt_wait > 0 then return end
    X.opt_wait = 2
    if gfl2_screen() then
        X.opt_done = 'screen'
        log('[' .. X.kit .. ' HUD] the GFL2 Kits HUD rows (cooldowns, the character picker) are on the options screen, not the Mod Options Menu')
        return
    end
    local menu = rawget(_G, 'ModOptionsMenu')
    if not menu or menu.api ~= 1 then return end
    X.opt_done = true
    local id = 'yui.gfl2hud.' .. X.id .. '.cooldowns'
    local ok, why = menu.register_option(id, {type = 'toggle', label = X.kit .. ': Always Show Cooldowns', mod = 'GFL2 Kits HUD',
        mod_id = 'yui.gfl2hud', default = true,
        description = X.kit .. "'s skills on cooldown stay on screen while the stratagem list is closed."})
    if not ok then log('[' .. X.kit .. ' HUD] cooldowns option not added: ' .. tostring(why)) return end
    local saved = menu.get(id)
    if saved ~= nil then X.always = saved and true or false end
    menu.on_change(id, function(value) X.always = value and true or false end)
end
-- the bound keys' names for the list ('CTRL+A'), refreshed once a second
local function binding_label(mbm, id)
    local ok, list = pcall(mbm.binding, id)
    if not ok or type(list) ~= 'table' then return nil end
    local alt = list[1]
    if type(alt) ~= 'table' then return 'UNBOUND' end
    local parts = {}
    for _, m in ipairs(type(alt.modifiers) == 'table' and alt.modifiers or {}) do parts[#parts + 1] = tostring(m):gsub('^left ', ''):gsub('^right ', '') end
    parts[#parts + 1] = tostring(alt.button or '?')
    return string.upper(table.concat(parts, '+'))
end
local function gfl2_bindings(dt)
    if X.bind_done == 'off' then return end
    local mbm = rawget(_G, 'ModBindingsMenu')
    if not X.bind_done then
        X.bind_wait = X.bind_wait - dt
        if X.bind_wait > 0 then return end
        X.bind_wait = 2
        if type(mbm) ~= 'table' or type(mbm.register_binding) ~= 'function' or type(mbm.is_down) ~= 'function' then return end
        if (tonumber(mbm.version) or 0) < 4 then X.bind_done = 'off'; log('[' .. X.kit .. ' HUD] key bindings: Mod Bindings Menu is too old (version ' .. tostring(mbm.version) .. ')') return end
        local ip, kp = gfl2_dll(X.id .. '_input'), gfl2_dll(X.id .. '_key')
        if not ip or not kp then
            if gfl2_dll(X.id .. '_hud_state') then X.bind_done = 'off'; log('[' .. X.kit .. ' HUD] key bindings: this ' .. X.id .. '.dll has none (update it)') end
            return
        end
        X.input = ffi.cast('gfl2bind_input', ip)
        local key = ffi.cast('gfl2bind_key', kp)
        local vk, mods = ffi.new('int[1]'), ffi.new('int[1]')
        for i = 1, 3 do
            local spec = nil
            if key(1, i, vk, mods) == 1 then spec = default_spec(tonumber(vk[0]), tonumber(mods[0])) end
            local id = 'yui.' .. X.id .. '.key' .. i   -- not '.skill': those ids were extended bindings (0.2.6), which stay extended
            local ok, res, why = pcall(mbm.register_binding, id, X.kit .. ': ' .. NAMES[i], nil, {category = X.kit, default = spec})
            if not ok or not res then
                X.bind_done = 'off'
                log('[' .. X.kit .. ' HUD] key bindings: ' .. id .. ' not registered (' .. tostring(ok and why or res) .. '): the .ini keys stay')
                return
            end
            X.ids[i] = id
        end
        X.bind_done = true
        local info = mbm.binding_info and mbm.binding_info(X.ids[1])
        log('[' .. X.kit .. ' HUD] key bindings: registered with Mod Bindings Menu as ' .. tostring(type(info) == 'table' and info.backend or '?') .. ' bindings (the binding pages\' MODS tab); the .ini keys keep working')
    end
    for i = 1, 3 do
        local down = mbm.is_down(X.ids[i]) == true
        if down and not X.prev[i] then X.input(1, i) end
        X.prev[i] = down
    end
    X.beat = X.beat - dt
    if X.beat <= 0 then X.beat = 0.5; X.input(1, 0) end
    X.label_wait = X.label_wait - dt
    if X.label_wait <= 0 then
        X.label_wait = 1
        for i = 1, 3 do X.labels[i] = binding_label(mbm, X.ids[i]) end
    end
end
-- The roster: with two or more character kits installed, an "Active Character" choice on the MODS tab (GFL2 Kits HUD);
-- only the picked kit acts, from the next mission start (kitbase/kb_roster.h). Each HUD passes the pick to its own DLL;
-- every kit reads the same shared answer. One kit alone: no row, it simply acts.
pcall(ffi.cdef, [[
typedef int (*gfl2roster_text)(char *out, int size);
typedef int (*gfl2roster_pick)(const char *id);
]])
local R = {done = false, wait = 0, tries = 0}
local function gfl2_roster(dt)
    if R.done then return end
    R.wait = R.wait - dt
    if R.wait > 0 then return end
    R.wait = 2
    if gfl2_screen() then R.done = true return end                  -- the screen's character column picks
    local menu = rawget(_G, 'ModOptionsMenu')
    if not menu or menu.api ~= 1 then return end
    local tp, pp = gfl2_dll(X.id .. '_roster'), gfl2_dll(X.id .. '_pick')
    if not tp or not pp then R.done = true return end              -- a DLL from before the roster
    local buf = ffi.new('char[1024]')
    local n = ffi.cast('gfl2roster_text', tp)(buf, 1024)
    R.tries = R.tries + 1
    if n < 2 then if R.tries >= 5 then R.done = true end return end  -- alone: nothing to pick
    R.done = true
    local list, active = ffi.string(buf):match('^(.*)|(.*)$')
    local ids, names, default = {}, {}, 1
    for entry in (list or ''):gmatch('[^;]+') do
        local id, name = entry:match('^([^:]+):(.*)$')
        if id then ids[#ids + 1] = id; names[#names + 1] = name; if id == active then default = #ids end end
    end
    if #ids < 2 or #ids > 16 then return end
    local key = 0                                                    -- a short id for this roster (a new kit: a new row)
    for _, id in ipairs(ids) do for c = 1, #id do key = (key * 31 + id:byte(c)) % 2147483647 end end
    local option = 'yui.gfl2hud.character.' .. key
    local ok, why = menu.register_option(option, {type = 'choice', label = 'Active Character', mod = 'GFL2 Kits HUD', mod_id = 'yui.gfl2hud',
        choices = names, default = default,
        description = 'Several character kits are installed: only the picked one acts (keys, body, skills, HUD). Takes effect at the next mission start.'})
    if not ok then log('[' .. X.kit .. ' HUD] roster: the picker row was not added here (' .. tostring(why) .. '): another kit\'s HUD may have it') end
    local pick = ffi.cast('gfl2roster_pick', pp)
    local saved = menu.get(option)
    if type(saved) == 'number' and ids[saved] then pick(ids[saved]) end
    menu.on_change(option, function(value) if ids[value] then pick(ids[value]) end end)
    log('[' .. X.kit .. ' HUD] roster: ' .. #ids .. ' character kits installed: ' .. table.concat(names, ', ') .. '; picked: ' ..
        tostring(names[(type(saved) == 'number' and ids[saved]) and saved or default]))
end
local function gfl2_step(dt)
    local okR, errR = pcall(gfl2_roster, dt)
    if not okR then R.done = true; log('[' .. X.kit .. ' HUD] roster failed: ' .. tostring(errR)) end
    local ok, err = pcall(gfl2_options, dt)
    if not ok then X.opt_done = true; log('[' .. X.kit .. ' HUD] cooldowns option failed: ' .. tostring(err)) end
    ok, err = pcall(gfl2_bindings, dt)
    if not ok then X.bind_done = 'off'; log('[' .. X.kit .. ' HUD] key bindings failed, the .ini keys stay: ' .. tostring(err)) end
end
-- kitbase:bindings.lua end
local SHORT = {'S1', 'S2', 'S3'}

local state, refresh, find_wait = nil, nil, 0
local function find(dt)
    if state ~= nil then refresh() return state end
    find_wait = find_wait - dt
    if find_wait > 0 then return nil end
    find_wait = 2
    local module = k32.wk1_GetModuleHandleA('w.dll')
    if module == nil then return nil end
    local proc = k32.wk1_GetProcAddress(module, 'w_hud_state')
    if proc == nil then return nil end
    local fn = ffi.cast('whud1_state *(*)(void)', proc)
    local s = fn()
    if s == nil or s.version < 1 then return nil end
    refresh = function() fn() end
    state = s
    log('[W HUD] connected to W')
    return state
end

local function rgba(c, a) return {c[1], c[2], c[3], math.floor(255 * (a or 1) + 0.5)} end
-- text with a drop shadow; sizes stay at 17 px and up (at 1080p) so the strokes stay solid
local function heavy(d, str, x, y, size, font, colour, align)
    local s = d.scale
    d:text(str, x + math.max(1, 2 * s), y + math.max(1, 2 * s), {size = size, font = font, colour = rgba(INK, 0.75), z = 4, align = align})
    d:text(str, x, y, {size = size, font = font, colour = colour, z = 5, align = align})
end

local RED, WARN_AT, WARN = {255, 48, 40}, 0.25, {t = 0}
local SOS_BEACON = 145   -- StratagemType of the SOS Beacon
local ORANGE, GREY = {224, 58, 58}, {120, 124, 128}
local ICONS = {}
for i, id in ipairs({'w_s1', 'w_s2', 'w_s3'}) do
    local okI, handle = pcall(hd2.resources.image, id)
    if okI and handle then ICONS[i] = handle end
end

-- kitbase:list.lua begin. Shared by the GFL2 kits' HUDs: written by tools/kitbase_sync.py from kitbase/hud/list.lua, so edit
-- it there (the kit names itself in KIT, above).
-------------------------------------------------------------------------- the stratagem list, to sit beside --
-- The list's entries and their icons' rectangles come from HD2Runtime's slot overlay (code / research from
-- HD2Runtime by SkyeShade, its internal modules); without them the abilities show at a fixed place under the list.
local L = {on = true, entries = nil, wait = 0, proven = false}
local okA, SO = pcall(require, 'hd2runtime/runtime/stratagem_slot_overlay')
local okB, W = pcall(require, 'hd2runtime/runtime/event_world')
local okC, SEL = pcall(require, 'hd2runtime/runtime/stratagem_selector')
if not (okA and okB and okC and type(SO) == 'table' and type(SO.hud_entries) == 'function' and type(SO.rect) == 'function'
        and type(W) == 'table' and type(W.open) == 'function' and type(SEL) == 'table' and type(SEL.prove) == 'function') then
    L.on = false
    log('[' .. X.kit .. ' HUD] the stratagem list cannot be followed on this HD2Runtime: abilities always shown')
end
local said = {}
local function why_not(what, why)
    local key = what .. ': ' .. tostring(why)
    if said[key] then return end
    said[key] = true
    log('[' .. X.kit .. ' HUD] list: ' .. key)
end
-- {left, top, right, size, step, alpha} of the open list (top-left origin), false when it is closed, nil when unknown
local function list_box(d, dt)
    if not L.on then return nil end
    local world = W.open()
    if not world then why_not('no world', 'not open') return nil end
    if not L.proven then
        local proven, why = SEL.prove(world)
        if not proven then why_not('not proven yet', why) return nil end
        L.proven = true
        log('[' .. X.kit .. ' HUD] the stratagem list can be followed on this build')
    end
    L.wait = L.wait - dt
    if not L.entries or L.wait <= 0 then
        L.wait = 0.5
        local entries, why = SO.hud_entries(world)
        L.entries = entries
        if not entries then why_not('no list', why) end
    end
    if not L.entries then L.full = nil return nil end
    local H = d.height or (d.width * 9 / 16)
    local box, tops, total = nil, {}, 0
    local seen, sos, sos_shown, reinforce = {}, false, false, nil
    for idx, e in pairs(L.entries) do
        if idx == 0 then reinforce = e end
        local kind = tonumber(e.type) or 0
        if kind == SOS_BEACON then sos = true end
        local used = kind ~= 0 and kind ~= 0xFFFFFFFF
        if used then total = total + 1 end
        local r = SO.rect(world, e.icon)
        seen[#seen + 1] = string.format('%s:%d%s', tostring(idx), kind, r and string.format('@%.0f', H - (r.y + r.h)) or '-')
        if r and kind == SOS_BEACON then sos_shown = true end
        if r then
            local top = H - (r.y + r.h)
            tops[#tops + 1] = top
            if not box then box = {left = r.x, top = top, bottom = top + r.h, size = r.h, alpha = r.alpha or 1}
            else
                box.left = math.min(box.left, r.x); box.top = math.min(box.top, top); box.bottom = math.max(box.bottom, top + r.h)
                box.alpha = math.max(box.alpha, r.alpha or 1)
            end
        end
    end
    table.sort(seen)
    local view = string.format('%d of %d used entries shown: %s', #tops, total, table.concat(seen, ' '))
    if view ~= L.last_view and (L.views or 0) < 40 then L.last_view = view; L.views = (L.views or 0) + 1; log('[' .. X.kit .. ' HUD] list: ' .. view) end
    -- open or closed, by how many icons show: the open list shows the whole loadout (7, more with event stratagems;
    -- limited-use ones stay listed), and its most this mission is learned the first time it opens; the closed list
    -- keeps only Reinforce and the few stratagems inbound or recharging. Open: the most, or one fewer (the SOS Beacon
    -- hidden while it recharges).
    if not box then return false end
    L.full = math.max(L.full or 0, #tops)
    if L.full < 4 or #tops < L.full - 1 then return false end
    table.sort(tops)
    local step
    for i = 2, #tops do
        local gap = tops[i] - tops[i - 1]
        if gap > box.size * 0.5 and (not step or gap < step) then step = gap end
    end
    step = step or box.size * 1.2
    box.step = step
    return box
end

-- kitbase:list.lua end
---------------------------------------------------------------------------------------------- the abilities --
local function ability(d, i, x, y, size, alpha)
    local s = d.scale
    local left, full = tonumber(state.cd[i - 1]), math.max(0.01, tonumber(state.cd_max[i - 1]))
    local ready = left <= 0.05
    local on = i == 1 and state.s1_on ~= 0                                  -- the launcher is out
    local marks, charges = tonumber(state.marks), tonumber(state.charges)
    local a = alpha
    -- the icon's tile, framed like a stratagem's
    local t = math.max(1, 2 * s)
    local frame = ready and rgba(ELECTRIC, a) or rgba(GREY, a * 0.8)
    d:rect(x, y, size, size, rgba(INK, a * 0.9), 1)
    d:rect(x, y, size, t, frame, 2); d:rect(x, y + size - t, size, t, frame, 2)
    d:rect(x, y, t, size, frame, 2); d:rect(x + size - t, y, t, size, frame, 2)
    if ICONS[i] then
        local pad = size * 0.08
        d:image(ICONS[i], x + pad, y + pad, size - 2 * pad, size - 2 * pad, {z = 3,
            colours = ready and {r = rgba(ORANGE, 1), g = {255, 255, 255, 255}} or {r = rgba(GREY, 1), g = {150, 154, 158, 255}},
            colour = {255, 255, 255, math.floor(255 * a + 0.5)}})
    else
        heavy(d, SHORT[i], x + size / 2, y + size * 0.3, math.max(17 * s, size * 0.34), 'title', ready and rgba(TEXT, a) or rgba(MUTED, a), 'center')
    end
    -- on the tile: the launcher's rounds, the mine charges, the marks
    local count
    if i == 1 and on then count = state.rounds > 0 and ('%d'):format(tonumber(state.rounds)) or '...'
    elseif i == 2 and ready then count = ('%d'):format(charges)
    elseif i == 3 and ready and marks > 0 then count = ('%d/%d'):format(marks, tonumber(state.marks_max)) end
    if count then heavy(d, count, x + size / 2, y + size * 0.58, math.max(17 * s, size * 0.34), 'title', rgba(YELLOW, a), 'center') end
    if not ready then
        -- the part of the cooldown left, darkening the icon from the top
        d:rect(x + t, y + t, size - 2 * t, (size - 2 * t) * math.min(1, left / full), rgba(INK, a * 0.55), 4)
        heavy(d, string.format('%.0f', math.ceil(left)), x + size / 2, y + size * 0.25, math.max(20 * s, size * 0.42), 'title', rgba(TEXT, a), 'center')
    end
    -- the name, and the key where a stratagem shows its code
    local tx = x + size + 10 * s
    local nsize = math.max(17 * s, size * 0.36)
    heavy(d, NAMES[i], tx, y + size * 0.02, nsize, 'title', ready and rgba(TEXT, a) or rgba(MUTED, a))
    local label = X.labels[i] or ffi.string(state.key_label[i - 1])
    local ksize = math.max(18 * s, size * 0.4)
    heavy(d, label, tx, y + size * 0.5, ksize, 'title', ready and rgba(YELLOW, a) or rgba(MUTED, a))
    local kw = d:text_width(label, ksize, 'title') or (ksize * #label * 0.6)
    local more
    if not ready then more = string.format('%.0fs', math.ceil(left))
    elseif i == 1 and on then more = 'AGAIN TO END'
    elseif i == 2 then more = ('%d OUT'):format(tonumber(state.mines_out))
    elseif i == 3 and marks > 0 then more = ffi.string(state.key_label[3]) .. ' TO DETONATE' end
    if more then heavy(d, more, tx + kw + 12 * s, y + size * 0.5, ksize, 'title', rgba(TEXT, a * 0.8)) end
end

local function abilities(d, box, only)
    local s = d.scale
    local size, step, x, y, a
    if box then
        size, step, a = box.size, box.step, box.alpha
        x = box.left + 290 * s          -- past the list's names
        y = box.top
    else
        size, step, a = 44 * s, 52 * s, 1
        x, y = 330 * s, 90 * s
    end
    local shown = {}
    for i = 1, 3 do
        -- while the list is closed: the ones recharging, the launcher while it is out, her marks while she has some
        local busy = tonumber(state.cd[i - 1]) > 0.05 or (i == 1 and state.s1_on ~= 0) or (i == 3 and state.marks > 0) or (i == 2 and state.charges < state.charges_max)
        if not only or busy then shown[#shown + 1] = i end
    end
    if #shown == 0 then return end
    -- a panel like the list's
    local w = size + 10 * s + math.max(230 * s, size * 5)
    d:rect(x - 10 * s, y - 10 * s, w + 20 * s, step * #shown + 20 * s - (step - size), rgba(PANEL, a * 0.72), 0)
    d:rect(x - 10 * s, y - 10 * s, 3 * s, step * #shown + 20 * s - (step - size), rgba(ELECTRIC, a), 1)
    for k, i in ipairs(shown) do ability(d, i, x, y + (k - 1) * step, size, a) end
end

-------------------------------------------------------------------------------------- Ambush, bottom middle --
local function status_bar(d)
    if state.ambush == 0 then return end
    local s = d.scale
    local H = d.height or (d.width * 9 / 16)
    heavy(d, 'AMBUSH', d.width / 2, H - 70 * s, 20 * s, 'title', rgba(ELECTRIC, 1), 'center')
end

-------------------------------------------------------------- the pose freeze, and her mines and marks in the world --
-- (in a function of its own: the main chunk's locals are nearly used up)
local world = (function()
local state, logfn, pending, lines = nil, nil, {}, 0
local minesfn, viewfn = nil, nil
local function log(m)
    m = tostring(m)
    lines = lines + 1
    if lines > 300 then return end
    pcall(print, '[W HUD] ' .. m)
    if logfn then pcall(logfn, 1, m) elseif #pending < 100 then pending[#pending + 1] = m end
end
local find_wait = 0
local function connect(dt)
    if state ~= nil then return state end
    find_wait = find_wait - dt
    if find_wait > 0 then return nil end
    find_wait = 2
    local module = k32.wk1_GetModuleHandleA('w.dll')
    if module == nil then return nil end
    local p1, p2 = k32.wk1_GetProcAddress(module, 'w_state'), k32.wk1_GetProcAddress(module, 'w_log')
    if p1 == nil or p2 == nil then return nil end
    local s = ffi.cast('wk1_state *(*)(void)', p1)()
    if s == nil or s.version ~= 1 then return nil end
    logfn = ffi.cast('wk1_log', p2)
    local p3, p4 = k32.wk1_GetProcAddress(module, 'w_mines'), k32.wk1_GetProcAddress(module, 'w_view')
    if p3 ~= nil then minesfn = ffi.cast('wk1_mines', p3) end
    if p4 ~= nil then viewfn = ffi.cast('wk1_view', p4) end
    state = s
    for _, m in ipairs(pending) do pcall(logfn, 1, m) end
    pending = {}
    log('connected to the DLL')
    return state
end

local function S_() return rawget(_G, 'stingray') end

-- ---- units ----
local function node_index()
    local s = S_()
    local okI, off = pcall(function() return s.Script.index_offset() end)
    return okI and type(off) == 'number' and off or 1
end
local RI
local function upos(s, u)
    RI = RI or node_index()
    local okP, v = pcall(s.Unit.world_position, u, RI)
    if not okP or not v then return nil end
    local x, y, z = s.Vector3.to_elements(v)
    return x, y, z
end
-- a unit's value in Lua as a number (a light userdata's pointer), or nil
local function uptr(u)
    local okF, str = pcall(string.format, '%p', u)
    local hex = okF and type(str) == 'string' and (str:match('0x(%x+)') or str:match('^(%x+)$')) or nil
    if not hex then
        local okT, text = pcall(tostring, u)
        hex = okT and type(text) == 'string' and text:match('0x(%x+)') or nil
    end
    return hex and tonumber(hex, 16) or nil
end
-- the units of types with at most `most` units in the world (reading the place of a unit of a mass-instanced type has
-- crashed the game: Exo Autopilot's finding)
local function rare_units(s, all, most)
    local res, count = {}, {}
    for i, u in ipairs(all) do
        local okR, r = pcall(s.Unit.resource_name, u)
        r = okR and tostring(r) or '?'
        res[i] = r
        count[r] = (count[r] or 0) + 1
    end
    local out = {}
    for i, u in ipairs(all) do if res[i] ~= '?' and count[res[i]] <= most then out[#out + 1] = u end end
    return out
end
local function has_machine(s, u)
    local okH, has = pcall(s.Unit.has_animation_state_machine, u)
    return okH and has == true
end

-- how a Lua unit value holds the unit (learned once from the player's own unit): 'object', 'id' or false (by place only)
local mode, shift, low, high = nil, 0, 0, 0
local function learn_mode(s, st, rare)
    local px, py, pz = st.player_pos[0], st.player_pos[1], st.player_pos[2]
    local anchor, best = nil, 0.09
    for _, u in ipairs(rare) do
        local x, y, z = upos(s, u)
        if x then
            local d2 = (x - px) ^ 2 + (y - py) ^ 2 + (z - pz) ^ 2
            if d2 < best and has_machine(s, u) then anchor, best = u, d2 end
        end
    end
    if not anchor then return end
    local P, U, O = uptr(anchor), st.player_unit, tonumber(st.player_object)
    if P and P == O then mode = 'object'
    elseif P then
        for k = 0, 24 do
            if math.floor(P / 2 ^ k) % 2 ^ 32 == U then mode, shift, low, high = 'id', k, P % 2 ^ k, math.floor(P / 2 ^ (k + 32)) break end
        end
    end
    if mode == nil then mode = false end
    log('a Lua unit value ' .. (mode == 'object' and 'is the unit object\'s address' or mode == 'id' and ('holds the unit id at bit ' .. shift) or
        'is neither the unit object nor its id: units are found by place only'))
end

local held = {}       -- entity -> {u = unit, off = bool}
local last_seq = -1
local can_freeze = nil

-- an enemy that died while held: its state machine was off as it died, so the animation its death asks for never
-- started (a big enemy stood dead in its pose and was not cleared away). Its machine is switched on again and the
-- first of these events its machine has is asked for; which it has is logged.
local DEATH = {'death', 'dead', 'die', 'dying', 'killed', 'death_default', 'death_front', 'death_back', 'ragdoll', 'to_ragdoll', 'death_ragdoll', 'explode', 'gib'}
local dead_seq, dead, death_told, unit_told = 0, {}, 0, false
local function died(s, u)
    if type(s.Unit.has_animation_event) ~= 'function' or type(s.Unit.animation_event) ~= 'function' then
        if death_told < 1 then death_told = 1; log('Unit.has_animation_event / animation_event are not in this game\'s Lua: a death while held cannot be put right here') end
        return
    end
    local has, fired = {}, nil
    for _, name in ipairs(DEATH) do
        local okH, h = pcall(s.Unit.has_animation_event, u, name)
        if okH and h == true then
            has[#has + 1] = name
            if not fired and pcall(s.Unit.animation_event, u, name) then fired = name end
        end
    end
    if death_told < 8 then
        death_told = death_told + 1
        local okR, r = pcall(s.Unit.resource_name, u)
        log(('died while held: %s: its machine has %s; asked for %s'):format(okR and tostring(r) or '?', #has > 0 and table.concat(has, ', ') or 'none of the events tried', fired or 'nothing'))
    end
end
local function release(s, e, touch)
    local h = held[e]
    held[e] = nil
    if not h or not h.off or not touch then return 0 end
    local okA, alive = pcall(s.Unit.alive, h.u)
    if okA and alive == true then
        local okE = pcall(s.Unit.enable_animation_state_machine, h.u)
        if okE and dead[e] then dead[e] = nil; pcall(died, s, h.u) end
        return okE and 1 or 0
    end
    return 0
end

local function sync(st)
    local s = S_()
    last_seq = st.seq
    if type(s) ~= 'table' or not (s.Unit and s.World and s.Application and s.Vector3) then return end
    while dead_seq ~= st.dead_seq do
        if st.dead_seq - dead_seq > 16 then dead_seq = st.dead_seq - 16 end
        dead[st.dead[dead_seq % 16]] = true
        dead_seq = dead_seq + 1
    end
    if not unit_told then
        unit_told = true
        local names = {}
        for k, f in pairs(s.Unit) do
            if type(k) == 'string' and type(f) == 'function' and (k:find('anim') or k:find('state_machine') or k:find('ragdoll') or k:find('time_scale')) then names[#names + 1] = k end
        end
        table.sort(names)
        local line = ''
        for _, k in ipairs(names) do
            if #line + #k > 360 then log('Unit: ' .. line); line = '' end
            line = line .. k .. ' '
        end
        if line ~= '' then log('Unit: ' .. line) end
    end
    if can_freeze == nil then
        can_freeze = type(s.Unit.disable_animation_state_machine) == 'function' and type(s.Unit.enable_animation_state_machine) == 'function'
        if not can_freeze then log('Unit.disable_animation_state_machine / enable_animation_state_machine are not in this game\'s Lua: no pose freeze') end
    end
    if not can_freeze then return end
    local want, fresh = {}, {}
    for k = 0, st.count - 1 do
        local t = st.t[k]
        local e = t.entity
        want[e] = true
        if not held[e] then fresh[#fresh + 1] = {e = e, unit = t.unit, object = tonumber(t.object), x = t.pos[0], y = t.pos[1], z = t.pos[2]} end
    end
    local on = 0
    for e in pairs(held) do if not want[e] then on = on + release(s, e, true) end end
    if #fresh == 0 then
        if on > 0 then log(('freeze %d: %d let go'):format(st.seq, on)) end
        return
    end
    local okW, w = pcall(s.Application.main_world)
    if not okW or not w then return end
    local okU, all = pcall(s.World.units, w)
    if not okU or type(all) ~= 'table' then return end
    local rare = nil
    if mode == nil then rare = rare_units(s, all, st.rare_max); learn_mode(s, st, rare) end
    local found, off = 0, 0
    if mode then
        local byptr = {}
        for _, u in ipairs(all) do
            local p = uptr(u)
            if p then byptr[p] = u end
        end
        for _, t in ipairs(fresh) do
            local key = mode == 'object' and t.object or (high * 2 ^ (shift + 32) + t.unit * 2 ^ shift + low)
            local u = key and byptr[key]
            if u then t.u = u; found = found + 1 end
        end
    end
    if found < #fresh then
        rare = rare or rare_units(s, all, st.rare_max)
        for _, u in ipairs(rare) do
            local x, y, z = upos(s, u)
            if x then
                for _, t in ipairs(fresh) do
                    if not t.u and (x - t.x) ^ 2 + (y - t.y) ^ 2 + (z - t.z) ^ 2 < 0.25 and has_machine(s, u) then t.u = u; found = found + 1 break end
                end
            end
        end
    end
    for _, t in ipairs(fresh) do
        local h = {u = t.u, off = false}
        if t.u and has_machine(s, t.u) then
            local okD = pcall(s.Unit.disable_animation_state_machine, t.u)
            if okD then h.off = true; off = off + 1 end
        end
        held[t.e] = h
    end
    log(('freeze %d: %d new (%d units found, %d held in pose), %d let go, %d held in all'):format(st.seq, #fresh, found, off, on, st.count))
end

-- ---- Jack in the Box's mines on the HUD (0.4.1) -------------------------------------------------------------------
-- Parts adapted from Chef Shared Loader by Th3chef (github.com/Th3chef/Chef-Shared-Loader, src/loader.lua), as the other
-- kits' markers are, with Th3chef's permission: the view camera (a living unit with Unit.num_cameras > 0, searched a few
-- hundred units a frame; the one nearest her head, which w.dll's w_view gives), a world point projected with
-- Camera.world_to_screen (bottom-left origin) and the behind-the-camera check.
local CAM = {unit = nil, list = nil, i = 0, retry = 0, checked = 0, told = 0}
local vbuf, mbuf, mk_t = ffi.new('float[3]'), ffi.new('float[256]'), 0
local function cam_gap(s, u, hx, hy, hz)
    local okc, c = pcall(s.Unit.camera, u, 1)
    if not (okc and c) then return nil end
    local okp, p = pcall(s.Camera.world_position, c)
    if not (okp and p) then return nil end
    local x, y, z = s.Vector3.to_elements(p)
    if type(x) ~= 'number' then return nil end
    return math.sqrt((x - hx) ^ 2 + (y - hy) ^ 2 + (z - hz) ^ 2)
end
local function cam_unit(s, t)
    if not viewfn or viewfn(1, vbuf) ~= 1 then return nil end
    local hx, hy, hz = vbuf[0], vbuf[1], vbuf[2]
    if CAM.unit then
        local ok, alive = pcall(s.Unit.alive, CAM.unit)
        if ok and alive == true then
            if t < CAM.checked then return CAM.unit end
            CAM.checked = t + 1
            local gap = cam_gap(s, CAM.unit, hx, hy, hz)
            if gap and gap < 6 then return CAM.unit end
        end
        CAM.unit = nil
    end
    if not CAM.list then
        if t < CAM.retry then return nil end
        local okw, w = pcall(s.Application.main_world)
        if not (okw and w) then CAM.retry = t + 2; return nil end
        local oku, units = pcall(s.World.units, w)
        if not (oku and type(units) == 'table') then CAM.retry = t + 2; return nil end
        CAM.list, CAM.i, CAM.best, CAM.gap, CAM.seen = units, 0, nil, nil, 0
    end
    local last = math.min(#CAM.list, CAM.i + 500)
    for i = CAM.i + 1, last do
        local u = CAM.list[i]
        local oka, alive = pcall(s.Unit.alive, u)
        if oka and alive == true then
            local okn, n = pcall(s.Unit.num_cameras, u)
            if okn and type(n) == 'number' and n > 0 then
                CAM.seen = CAM.seen + 1
                local gap = cam_gap(s, u, hx, hy, hz)
                if gap and (not CAM.gap or gap < CAM.gap) then CAM.best, CAM.gap = u, gap end
            end
        end
    end
    CAM.i = last
    if last >= #CAM.list then
        local found = CAM.best and CAM.gap and CAM.gap < 6
        if CAM.told < 4 then
            CAM.told = CAM.told + 1
            log(('camera: %d cameras in the world, nearest to her head %s'):format(CAM.seen, CAM.gap and ('%.1f m away%s'):format(CAM.gap, found and ' (taken)' or ' (too far: none taken)') or 'none'))
        end
        CAM.list = nil
        if found then CAM.unit = CAM.best; CAM.checked = t + 1; return CAM.unit end
        CAM.retry = t + 2
    end
    return nil
end
-- a mine's marker: the face on Jack in the Box's icon (w_mine; the square of before without the image)
local MINE_ICON
do
    local okI, handle = pcall(hd2.resources.image, 'w_mine')
    if okI and handle then MINE_ICON = handle end
end
local function mines_draw(d)
    local s = S_()
    if not minesfn or type(s) ~= 'table' or not (s.Unit and s.Camera and s.Vector3 and s.Quaternion and s.World and s.Application) then return end
    local n = minesfn(1, mbuf, 64)
    if n <= 0 then return end
    local u = cam_unit(s, mk_t)
    if not u then return end
    local c = s.Unit.camera(u, 1)
    if not c then return end
    local cx, cy, cz = s.Vector3.to_elements(s.Camera.world_position(c))
    local fx, fy, fz = s.Vector3.to_elements(s.Quaternion.forward(s.Camera.world_rotation(c)))
    local W, H = d.width, d.height or (d.width * 9 / 16)
    local bw, bh = W, H
    if s.Application.back_buffer_size then
        local okb, a, b = pcall(s.Application.back_buffer_size)
        if okb and type(a) == 'number' and a > 0 then bw, bh = a, b end
    end
    local sc = d.scale or 1
    for i = 0, n - 1 do
        local x, y, z, lit, mark = mbuf[i * 4], mbuf[i * 4 + 1], mbuf[i * 4 + 2] + 0.25, mbuf[i * 4 + 3] > 0.5 and mbuf[i * 4 + 3] < 1.5, mbuf[i * 4 + 3] > 1.5
        local ax, ay, az = x - cx, y - cy, z - cz
        if ax * fx + ay * fy + az * fz > 0.5 then
            local sx, sy = s.Vector3.to_elements(s.Camera.world_to_screen(c, s.Vector3(x, y, z)))
            if type(sx) == 'number' then
                sx, sy = sx * W / bw, H - sy * H / bh
                if sx > 0 and sx < W and sy > 0 and sy < H then
                    local dist = math.sqrt(ax * ax + ay * ay + az * az)
                    local half = math.max(4, math.min(12, 90 / math.max(dist, 1))) * sc
                    local col = lit and {255, 230, 80, math.floor(120 + 135 * (math.sin(mk_t * 20) * 0.5 + 0.5))} or mark and {255, 255, 255, 240} or {224, 58, 58, 235}
                    if mark then half = half * 1.3; d:rect(sx - half * 0.45, sy - half * 0.45, half * 0.9, half * 0.9, {224, 58, 58, 220}, 6) end
                    if MINE_ICON and not mark then
                        local r = math.max(9, math.min(22, 200 / math.max(dist, 1))) * sc
                        if lit then r = r * (1.15 + 0.15 * math.sin(mk_t * 20)) end
                        d:image(MINE_ICON, sx - r, sy - r, 2 * r, 2 * r, {z = 6, colours = {r = lit and {255, 200, 40, 255} or {224, 58, 58, 255}, g = {255, 255, 255, 255}},
                            colour = lit and {255, 215, 50, 255} or {255, 255, 255, 240}})
                        if dist < 60 then d:text(('%dm'):format(math.floor(dist + 0.5)), sx, sy + r + 2 * sc, {size = 12 * sc, colour = lit and {255, 230, 80, 255} or {255, 255, 255, 235}, z = 6, align = 'center'}) end
                    else
                    local tk = math.max(1, 2 * sc)
                    d:rect(sx - half, sy - half, 2 * half, tk, col, 6); d:rect(sx - half, sy + half - tk, 2 * half, tk, col, 6)
                    d:rect(sx - half, sy - half, tk, 2 * half, col, 6); d:rect(sx + half - tk, sy - half, tk, 2 * half, col, 6)
                    d:rect(sx - 2 * sc, sy - 2 * sc, 4 * sc, 4 * sc, col, 6)
                    if dist < 60 then d:text(('%dm'):format(math.floor(dist + 0.5)), sx, sy + half + 2 * sc, {size = 12 * sc, colour = col, z = 6, align = 'center'}) end
                    end
                end
            end
        end
    end
end
local mines_failed = false
local failed = false
return function(d, dt)
    local st = connect(dt or 0)
    if not st then return end
    if not failed then
        local okF, err = pcall(function()
            if st.in_mission == 0 then
                -- the mission ended: forget them, their units left alone
                if next(held) ~= nil then held = {} end
                last_seq = st.seq
            elseif st.seq ~= last_seq then sync(st) end
        end)
        if not okF then failed = true; log('the freeze failed, off for this session: ' .. tostring(err)) end
    end
    if st.in_mission == 0 then return end
    mk_t = mk_t + (tonumber(dt) or 0)
    if not mines_failed then
        local okM, errM = pcall(mines_draw, d)
        if not okM then mines_failed = true; log('the mines\' markers failed, off for this session: ' .. tostring(errM)) end
    end
end
end)()

local overlay = hd2.ui.overlay({id = 'kit', owner = OWNER})
overlay:draw(function(d, dt)
    dt = tonumber(dt) or 0
    WARN.dt = dt
    gfl2_step(dt)
    local okW, errW = pcall(world, d, dt)
    if not okW and not WARN.world then WARN.world = true; log('[W HUD] the world part failed: ' .. tostring(errW)) end
    if not find(dt) then return end
    if state.in_mission == 0 then return end
    status_bar(d)
    local okL, box = pcall(list_box, d, dt)
    if not okL then L.on = false; log('[W HUD] following the list failed, abilities always shown: ' .. tostring(box)); box = nil end
    if box == false then                 -- the list is closed: the skills in use or on cooldown only, under where the open list ends
        if X.always then
            local s = d.scale
            local H = d.height or (d.width * 9 / 16)
            local lb = X.last_box
            local size = lb and lb.size or 44 * s
            local step = lb and lb.step or 52 * s
            local left = math.max(20 * s, lb and lb.left or 40 * s)
            local top = lb and (lb.bottom + step * 0.6) or H * 0.45
            abilities(d, {alpha = 1, size = size, step = step, left = left - 290 * s, top = top}, true)
        end
        return
    end
    -- remembered only while the list is fully shown and on screen (it slides off to the left as it closes)
    if box and (box.alpha or 1) >= 0.95 and box.left >= 0 and (not X.last_box or box.left >= X.last_box.left - 2) then
        X.last_box = {left = box.left, top = box.top, bottom = box.bottom, size = box.size, step = box.step}
    end
    abilities(d, box or nil)
end)

log('[W HUD] ready (waiting for W)')
