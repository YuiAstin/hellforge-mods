-- Exusiai HUD (mods/yui/exusiai_hud): Exusiai's skills beside the mission's stratagem list (shown with it, as the
-- stratagems are: icon or short name, name, key, cooldown) and her charge and guns at the bottom middle, drawn with
-- HD2Runtime's screen overlay (hd2.ui.overlay). Exusiai is a HellForge code mod; this reads its state through LuaJIT's
-- ffi (exusiai.dll exports exusiai_hud_state, layout version 1) every frame. Requires Bingus Shared Loader and
-- HD2Runtime; uses HD2Runtime by SkyeShade (https://github.com/SkyeShade/HD2Runtime) through its public API.
local OWNER = 'mods/yui/exusiai_hud'

local sink, sink_tried
local function log(message)
    pcall(print, message)
    if not sink_tried then
        sink_tried = true
        local ok, result = pcall(function() return CowboyBingusModLoader.open_log('ExusiaiHUD.log') end)
        if ok then sink = result end
    end
    if sink then
        if not pcall(function() sink:write(message .. '\n'); if sink.flush then sink:flush() end end) then sink = nil end
    end
end

local ok, hd2 = pcall(require, 'mods/skyeshade/hd2runtime')
if not ok or type(hd2) ~= 'table' or type(hd2.ui) ~= 'table' or type(hd2.ui.overlay) ~= 'function' then
    log('[Exusiai HUD] HD2Runtime with hd2.ui.overlay is not available: no HUD (' .. tostring(hd2) .. ')')
    return
end

local ffi = require('ffi')
pcall(ffi.cdef, [[
typedef struct exuhud1_state { uint32_t version, in_mission; float cd[3], cd_max[3]; uint32_t charge, charge_max; float free_bursts, guns, overload; uint32_t keys[3]; char key_label[3][24]; } exuhud1_state;
typedef int (*exuammo1)(unsigned int version, uint32_t entity, int rounds);
void *exuhud1_GetModuleHandleA(const char *name) __asm__("GetModuleHandleA");
void *exuhud1_GetProcAddress(void *module, const char *name) __asm__("GetProcAddress");
]])
local k32 = ffi.load('kernel32')

local ELECTRIC, YELLOW, INK, PANEL, TEXT, MUTED = {255, 92, 92}, {255, 232, 0}, {8, 9, 10}, {12, 14, 16}, {240, 243, 245}, {150, 156, 160}
local NAMES = {'CHARGING MODE', 'SUMMONED GUNS', 'OVERLOADING MODE'}

-- the kit, for the shared parts below
local KIT = {name = 'Exusiai', id = 'exusiai', module = k32.exuhud1_GetModuleHandleA, proc = k32.exuhud1_GetProcAddress}
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
local function gfl2_options(dt)
    if X.opt_done then return end
    X.opt_wait = X.opt_wait - dt
    if X.opt_wait > 0 then return end
    X.opt_wait = 2
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
local function gfl2_step(dt)
    local ok, err = pcall(gfl2_options, dt)
    if not ok then X.opt_done = true; log('[' .. X.kit .. ' HUD] cooldowns option failed: ' .. tostring(err)) end
    ok, err = pcall(gfl2_bindings, dt)
    if not ok then X.bind_done = 'off'; log('[' .. X.kit .. ' HUD] key bindings failed, the .ini keys stay: ' .. tostring(err)) end
end
-- kitbase:bindings.lua end
local SHORT = {'CHG', 'GUN', 'OVL'}

local state, refresh, find_wait = nil, nil, 0
local function find(dt)
    if state ~= nil then refresh() return state end
    find_wait = find_wait - dt
    if find_wait > 0 then return nil end
    find_wait = 2
    local module = k32.exuhud1_GetModuleHandleA('exusiai.dll')
    if module == nil then return nil end
    local proc = k32.exuhud1_GetProcAddress(module, 'exusiai_hud_state')
    if proc == nil then return nil end
    local fn = ffi.cast('exuhud1_state *(*)(void)', proc)
    local s = fn()
    if s == nil or s.version < 1 then return nil end
    refresh = function() fn() end
    state = s
    log('[Exusiai HUD] connected to Exusiai')
    return state
end

local function rgba(c, a) return {c[1], c[2], c[3], math.floor(255 * (a or 1) + 0.5)} end
local function key_name(code)
    code = tonumber(code) or 0
    if (code >= 65 and code <= 90) or (code >= 48 and code <= 57) then return string.char(code) end
    return '#' .. code
end
-- text with a drop shadow; sizes stay at 17 px and up (at 1080p) so the strokes stay solid
local function heavy(d, str, x, y, size, font, colour, align)
    local s = d.scale
    d:text(str, x + math.max(1, 2 * s), y + math.max(1, 2 * s), {size = size, font = font, colour = rgba(INK, 0.75), z = 4, align = align})
    d:text(str, x, y, {size = size, font = font, colour = colour, z = 5, align = align})
end

local RED, WARN_AT, WARN = {255, 48, 40}, 0.25, {t = 0}
local SOS_BEACON = 145   -- StratagemType of the SOS Beacon
local ORANGE, GREY = {255, 138, 36}, {120, 124, 128}
local ICONS = {}
for i, id in ipairs({'exusiai_charge', 'exusiai_guns', 'exusiai_overload'}) do
    local okI, handle = pcall(hd2.resources.image, id)
    if okI and handle then ICONS[i] = handle end   -- (no images yet: the tiles show short names)
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
    local passive = i == 1                 -- Charging Mode: charged by her bursts, no cooldown
    local charge, charge_max = tonumber(state.charge), math.max(1, tonumber(state.charge_max))
    if passive then ready = charge >= charge_max end
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
    end
    if not ICONS[i] then
        heavy(d, SHORT[i], x + size / 2, y + size * 0.3, math.max(17 * s, size * 0.34), 'title', ready and rgba(TEXT, a) or rgba(MUTED, a), 'center')
    end
    if passive then
        heavy(d, ('%d/%d'):format(charge, charge_max), x + size / 2, y + size * 0.62, math.max(17 * s, size * 0.3), 'title', ready and rgba(YELLOW, a) or rgba(TEXT, a), 'center')
    elseif not ready then
        -- the part of the cooldown left, darkening the icon from the top
        d:rect(x + t, y + t, size - 2 * t, (size - 2 * t) * math.min(1, left / full), rgba(INK, a * 0.55), 4)
        heavy(d, string.format('%.0f', math.ceil(left)), x + size / 2, y + size * 0.25, math.max(20 * s, size * 0.42), 'title', rgba(TEXT, a), 'center')
    end
    -- the name, and the key where a stratagem shows its code
    local tx = x + size + 10 * s
    local nsize = math.max(17 * s, size * 0.36)
    heavy(d, NAMES[i], tx, y + size * 0.02, nsize, 'title', ready and rgba(TEXT, a) or rgba(MUTED, a))
    local label = passive and (ready and 'NEXT BURST' or 'PASSIVE') or X.labels[i] or ffi.string(state.key_label[i - 1])
    local ksize = math.max(18 * s, size * 0.4)
    heavy(d, label, tx, y + size * 0.5, ksize, 'title', ready and rgba(YELLOW, a) or rgba(MUTED, a))
    if not ready and not passive then
        local kw = d:text_width(label, ksize, 'title') or (ksize * #label * 0.6)
        heavy(d, string.format('%.0fs', math.ceil(left)), tx + kw + 12 * s, y + size * 0.5, ksize, 'title', rgba(TEXT, a * 0.8))
    end
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
    for i = 1, 3 do if not only or tonumber(state.cd[i - 1]) > 0.05 then shown[#shown + 1] = i end end
    if #shown == 0 then return end
    -- a panel like the list's
    local w = size + 10 * s + math.max(230 * s, size * 5)
    d:rect(x - 10 * s, y - 10 * s, w + 20 * s, step * #shown + 20 * s - (step - size), rgba(PANEL, a * 0.72), 0)
    d:rect(x - 10 * s, y - 10 * s, 3 * s, step * #shown + 20 * s - (step - size), rgba(ELECTRIC, a), 1)
    for k, i in ipairs(shown) do ability(d, i, x, y + (k - 1) * step, size, a) end
end

------------------------------------------------------------------------------ her charge and guns, bottom middle --
local function status_bar(d)
    local s = d.scale
    local H = d.height or (d.width * 9 / 16)
    local cx, y = d.width / 2, H - 64 * s
    local bits = {}
    local charge, charge_max = tonumber(state.charge), tonumber(state.charge_max)
    if state.free_bursts > 0 then bits[#bits + 1] = 'FREE BURSTS'
    elseif charge_max > 0 then bits[#bits + 1] = 'CHARGE ' .. string.rep('|', charge) .. string.rep('.', math.max(0, charge_max - charge)) end
    if state.guns > 0 then bits[#bits + 1] = string.format('GUNS %.0fs', math.ceil(state.guns)) end
    if state.overload > 0 then bits[#bits + 1] = string.format('OVERLOADING %.0fs', math.ceil(state.overload)) end
    if #bits > 0 then heavy(d, table.concat(bits, '   '), cx, y - 6 * s, 20 * s, 'title', rgba(ELECTRIC, 1), 'center') end
end

------------------------------------------------------------------------- settings in the Mod Options Menu --
-- Exusiai's numbers as rows on the escape menu's MODS tab (Mod Options Menu by CowboyBingus, its public api 1), read
-- from and written to exusiai.dll (exusiai_option_get / exusiai_option_set, the exusiai.ini names). The menu keeps what
-- the player applies; until then a row starts from exusiai.ini. Keys stay in exusiai.ini.
local OPTIONS = {
    {'fire_rate', 'slider', 'Fast Cartridge: Fire Rate', 1, 2, 0.05, 'Her guns fire this many times faster (from when she next equips each one).'},
    {'bless_mult', 'slider', "Angel's Blessings: Damage", 1, 2, 0.01, 'Her shots deal this times their damage.'},
    {'ch_every', 'slider', 'Charging Mode: Bursts To Charge', 1, 10, 1, 'Bursts she fires to charge it; the next one sets off the free bursts.', true},
    {'ch_free', 'slider', 'Charging Mode: Free Bursts', 0, 6, 1, 'Extra bursts of her gun\'s rounds the charged burst sets off.'},
    {'s2_cooldown', 'slider', 'Summoned Guns: Cooldown', 0, 120, 5, 'Seconds before the guns can be summoned again.', true},
    {'s2_seconds', 'slider', 'Summoned Guns: Duration', 5, 60, 1, 'Seconds the two guns stay at her shoulders.'},
    {'gun_every', 'slider', 'Summoned Guns: Fire Interval', 0.05, 1, 0.05, 'Each gun fires at most once in this many seconds.'},
    {'s3_cooldown', 'slider', 'Overloading Mode: Cooldown', 0, 180, 5, 'Seconds before Overloading Mode can be used again.', true},
    {'s3_seconds', 'slider', 'Overloading Mode: Duration', 5, 60, 1, 'Seconds the four extra guns stay.'},
    {'s3_every', 'slider', 'Overloading Mode: Fire Interval', 0.1, 2, 0.05, 'Each extra gun fires once in this many seconds.'},
    {'s3_range', 'slider', 'Overloading Mode: Range', 10, 120, 5, 'Metres the extra guns look for targets in.'},
    {'no_friendly_fire', 'toggle', 'Guns: Spare Allies', nil, nil, nil, 'While a gun is out, her shots do not hurt her allies.', true},
    {'hp_mult', 'slider', 'Body: Health', 0.5, 4, 0.05, 'Her health, times a Helldiver\'s.', true},
    {'stamina_mult', 'slider', 'Body: Stamina', 0.5, 6, 0.1, 'How long she sprints before the bar empties, times a Helldiver\'s.'},
}
pcall(ffi.cdef, [[
typedef int (*exuopt_get)(const char *name, double *out);
typedef int (*exuopt_set)(const char *name, double value);
]])
local O = {done = false, wait = 0}
local function options_step(dt)
    if O.done then return end
    O.wait = O.wait - dt
    if O.wait > 0 then return end
    O.wait = 2
    local menu = rawget(_G, 'ModOptionsMenu')
    if not menu or menu.api ~= 1 then return end
    local module = k32.exuhud1_GetModuleHandleA('exusiai.dll')
    if module == nil then return end
    local gp, sp = k32.exuhud1_GetProcAddress(module, 'exusiai_option_get'), k32.exuhud1_GetProcAddress(module, 'exusiai_option_set')
    if gp == nil or sp == nil then O.done = true; log('[Exusiai HUD] options: this exusiai.dll has no options (update Exusiai)') return end
    O.done = true
    local get, set = ffi.cast('exuopt_get', gp), ffi.cast('exuopt_set', sp)
    local out = ffi.new('double[1]')
    local count = 0
    for _, o in ipairs(OPTIONS) do
        local key, kind = o[1], o[2]
        if get(key, out) == 1 then
            local current = tonumber(out[0])
            local id = 'yui.exusiai.' .. key
            local spec = {type = kind, label = o[3], mod = 'Exusiai', mod_id = 'yui.exusiai', description = o[7], gap = o[8] or nil}
            if kind == 'toggle' then spec.default = current ~= 0
            else
                spec.min, spec.max, spec.step = o[4], o[5], o[6]
                spec.default = math.max(o[4], math.min(o[5], current))
            end
            local ok, why = menu.register_option(id, spec)
            if ok then
                count = count + 1
                local function push(value)
                    if type(value) == 'boolean' then value = value and 1 or 0 end
                    if type(value) == 'number' then set(key, value) end
                end
                local saved = menu.get(id)
                if saved ~= nil then push(saved) end
                menu.on_change(id, function(value) push(value) end)
            else
                log('[Exusiai HUD] options: ' .. key .. ' not added: ' .. tostring(why))
            end
        end
    end
    log(('[Exusiai HUD] options: %d settings on the MODS tab'):format(count))
end

------------------------------------------------------------------------- her magazine, for Charging Mode --
-- The rounds in the gun in hand (HD2Runtime's player:ammo()), passed to exusiai.dll every frame (exusiai_ammo): the DLL
-- counts her bursts from the rounds going down.
local A = {fn = nil, failed = false}
local function ammo_step()
    if A.failed then return end
    if not A.fn then
        local module = k32.exuhud1_GetModuleHandleA('exusiai.dll')
        if module == nil then return end
        local proc = k32.exuhud1_GetProcAddress(module, 'exusiai_ammo')
        if proc == nil then A.failed = true; log('[Exusiai HUD] ammo: exusiai.dll has no ammo entry (update Exusiai)') return end
        A.fn = ffi.cast('exuammo1', proc)
    end
    local me = hd2.local_player and hd2.local_player()
    if not me then return end
    local ok, a = pcall(me.ammo, me)
    if not ok or type(a) ~= 'table' or type(a.rounds) ~= 'number' then A.fn(1, 0, -1) return end
    A.fn(1, tonumber(a.entity_id) or 0, a.rounds)
    if not A.told then A.told = true; log(('[Exusiai HUD] ammo: %s (%s), %d rounds: reported each frame'):format(tostring(a.name), tostring(a.feed), a.rounds)) end
end

local overlay = hd2.ui.overlay({id = 'kit', owner = OWNER})
overlay:draw(function(d, dt)
    dt = tonumber(dt) or 0
    WARN.dt = dt
    gfl2_step(dt)
    if not O.failed then
        local okO, errO = pcall(options_step, dt)
        if not okO then O.failed = true; log('[Exusiai HUD] options failed: ' .. tostring(errO)) end
    end
    if not find(dt) then return end
    if state.in_mission == 0 then return end
    local okA, errA = pcall(ammo_step)
    if not okA then A.failed = true; log('[Exusiai HUD] ammo failed: ' .. tostring(errA)) end
    status_bar(d)
    local okL, box = pcall(list_box, d, dt)
    if not okL then L.on = false; log('[Exusiai HUD] following the list failed, abilities always shown: ' .. tostring(box)); box = nil end
    if box == false then                 -- the list is closed: the skills on cooldown only, under where the open list ends
        if X.always then                 -- (beside the closed list they covered Reinforce and the stratagems recharging)
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

log('[Exusiai HUD] ready (waiting for Exusiai)')
