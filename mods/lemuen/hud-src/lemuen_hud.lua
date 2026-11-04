-- Lemuen HUD (mods/yui/lemuen_hud): Lemuen's skills beside the mission's stratagem list (shown with it, as the
-- stratagems are: icon or short name, name, key, cooldown) and her marks and scope at the bottom middle, drawn with
-- HD2Runtime's screen overlay (hd2.ui.overlay). Lemuen is a HellForge code mod; this reads its state through LuaJIT's
-- ffi (lemuen.dll exports lemuen_hud_state, layout version 1) every frame. Requires Bingus Shared Loader and
-- HD2Runtime; uses HD2Runtime by SkyeShade (https://github.com/SkyeShade/HD2Runtime) through its public API.
local OWNER = 'mods/yui/lemuen_hud'

local sink, sink_tried
local function log(message)
    pcall(print, message)
    if not sink_tried then
        sink_tried = true
        local ok, result = pcall(function() return CowboyBingusModLoader.open_log('LemuenHUD.log') end)
        if ok then sink = result end
    end
    if sink then
        if not pcall(function() sink:write(message .. '\n'); if sink.flush then sink:flush() end end) then sink = nil end
    end
end

local ok, hd2 = pcall(require, 'mods/skyeshade/hd2runtime')
if not ok or type(hd2) ~= 'table' or type(hd2.ui) ~= 'table' or type(hd2.ui.overlay) ~= 'function' then
    log('[Lemuen HUD] HD2Runtime with hd2.ui.overlay is not available: no HUD (' .. tostring(hd2) .. ')')
    return
end

local ffi = require('ffi')
pcall(ffi.cdef, [[
typedef struct lemhud1_state { uint32_t version, in_mission; float cd[3], cd_max[3]; uint32_t s1_on, s2_on, marked, scoped; float hold, modus; uint32_t keys[3]; char key_label[3][24]; } lemhud1_state;
typedef int (*lemammo1)(unsigned int version, uint32_t entity, int rounds);
void *lemhud1_GetModuleHandleA(const char *name) __asm__("GetModuleHandleA");
void *lemhud1_GetProcAddress(void *module, const char *name) __asm__("GetProcAddress");
]])
local k32 = ffi.load('kernel32')

local ELECTRIC, YELLOW, INK, PANEL, TEXT, MUTED = {236, 214, 140}, {255, 232, 0}, {8, 9, 10}, {12, 14, 16}, {240, 243, 245}, {150, 156, 160}
local NAMES = {'SALVE ETIAM', 'HONESTA MISSIO', 'MEMENTO MORI'}

-- the kit, for the shared parts below
local KIT = {name = 'Lemuen', id = 'lemuen', module = k32.lemhud1_GetModuleHandleA, proc = k32.lemhud1_GetProcAddress}
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
    local module = k32.lemhud1_GetModuleHandleA('lemuen.dll')
    if module == nil then return nil end
    local proc = k32.lemhud1_GetProcAddress(module, 'lemuen_hud_state')
    if proc == nil then return nil end
    local fn = ffi.cast('lemhud1_state *(*)(void)', proc)
    local s = fn()
    if s == nil or s.version < 1 then return nil end
    refresh = function() fn() end
    state = s
    log('[Lemuen HUD] connected to Lemuen')
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
for i, id in ipairs({'lemuen_s1', 'lemuen_s2', 'lemuen_s3'}) do
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
    local passive = false
    local on = (i == 1 and state.s1_on ~= 0) or (i == 2 and state.s2_on ~= 0)   -- until she reloads
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
    if on then
        local txt = i == 2 and ('%d%%'):format(math.floor(100 * tonumber(state.hold) + 0.5)) or 'ON'
        heavy(d, txt, x + size / 2, y + size * 0.62, math.max(17 * s, size * 0.3), 'title', rgba(YELLOW, a), 'center')
    end
    if not ready then
        -- the part of the cooldown left, darkening the icon from the top
        d:rect(x + t, y + t, size - 2 * t, (size - 2 * t) * math.min(1, left / full), rgba(INK, a * 0.55), 4)
        heavy(d, string.format('%.0f', math.ceil(left)), x + size / 2, y + size * 0.25, math.max(20 * s, size * 0.42), 'title', rgba(TEXT, a), 'center')
    end
    -- the name, and the key where a stratagem shows its code
    local tx = x + size + 10 * s
    local nsize = math.max(17 * s, size * 0.36)
    heavy(d, NAMES[i], tx, y + size * 0.02, nsize, 'title', ready and rgba(TEXT, a) or rgba(MUTED, a))
    local label = on and 'UNTIL RELOAD' or X.labels[i] or ffi.string(state.key_label[i - 1])
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
    local marked = tonumber(state.marked)
    if marked > 0 then bits[#bits + 1] = 'MARKED ' .. marked end
    if state.s1_on ~= 0 then bits[#bits + 1] = 'SALVE ETIAM' end
    if state.modus <= 0 then bits[#bits + 1] = 'MODUS' end
    if #bits > 0 then heavy(d, table.concat(bits, '   '), cx, y - 6 * s, 20 * s, 'title', rgba(ELECTRIC, 1), 'center') end
    -- Honesta Missio's build-up while her scope holds a target
    if state.s2_on ~= 0 and state.scoped ~= 0 then
        local bw, bh, h = 260 * s, 8 * s, math.max(0, math.min(1, tonumber(state.hold)))
        d:rect(cx - bw / 2, y + 22 * s, bw, bh, rgba(INK, 0.7), 1)
        d:rect(cx - bw / 2, y + 22 * s, bw * h, bh, rgba(h >= 1 and YELLOW or ELECTRIC, 1), 2)
    end
end

------------------------------------------------------------------------- settings in the Mod Options Menu --
-- Lemuen's numbers as rows on the escape menu's MODS tab (Mod Options Menu by CowboyBingus, its public api 1), read
-- from and written to lemuen.dll (lemuen_option_get / lemuen_option_set, the lemuen.ini names). The menu keeps what
-- the player applies; until then a row starts from lemuen.ini. Keys stay in lemuen.ini.
local OPTIONS = {
    {'mark_range', 'slider', 'Persecution: Range', 20, 300, 10, 'Metres within which she marks the enemies she watches.'},
    {'mark_seconds', 'slider', 'Persecution: Watch Time', 0, 15, 0.5, 'Seconds she keeps an enemy in her sights to mark it.'},
    {'mark_mult', 'slider', 'Persecution: Damage', 1, 2, 0.01, 'Marked enemies take this times the damage from her.'},
    {'mark_armour', 'slider', 'Persecution: Armour', 0, 6, 1, 'Enemies with this armour or more can be marked (or with enough health).'},
    {'modus_mult', 'slider', 'Modus Extraditio: Damage', 1, 2, 0.01, 'Her shots deal this times their damage once she has been down a while.', true},
    {'s1_cooldown', 'slider', 'Salve Etiam: Cooldown', 0, 120, 1, 'Seconds before Salve Etiam can be used again.', true},
    {'s1_mult', 'slider', 'Salve Etiam: Damage', 1, 8, 0.1, 'Her shots deal this times their damage until she reloads.'},
    {'s1_any_gun', 'toggle', 'Salve Etiam: Any Gun', nil, nil, nil, 'Let it work with any gun, not only sniper and marksman rifles.'},
    {'s2_cooldown', 'slider', 'Honesta Missio: Cooldown', 0, 120, 1, 'Seconds before Honesta Missio can be used again.', true},
    {'s2_max', 'slider', 'Honesta Missio: Full Damage', 1, 10, 0.05, 'Her next shot on what she held deals up to this times its damage.'},
    {'s2_hold', 'slider', 'Honesta Missio: Hold Time', 0.5, 10, 0.5, 'Seconds of holding a target in her scope for the full damage.'},
    {'s3_cooldown', 'slider', 'Memento Mori: Cooldown', 0, 300, 5, 'Seconds before Memento Mori can be used again.', true},
    {'s3_max', 'slider', 'Memento Mori: Shells', 1, 16, 1, 'At most this many marked enemies get a shell.'},
    {'hp_mult', 'slider', 'Body: Health', 0.5, 4, 0.05, 'Her health, times a Helldiver\'s.', true},
    {'stamina_mult', 'slider', 'Body: Stamina', 0.5, 6, 0.1, 'How long she sprints before the bar empties, times a Helldiver\'s.'},
}
pcall(ffi.cdef, [[
typedef int (*lemopt_get)(const char *name, double *out);
typedef int (*lemopt_set)(const char *name, double value);
]])
local O = {done = false, wait = 0}
local function options_step(dt)
    if O.done then return end
    O.wait = O.wait - dt
    if O.wait > 0 then return end
    O.wait = 2
    local menu = rawget(_G, 'ModOptionsMenu')
    if not menu or menu.api ~= 1 then return end
    local module = k32.lemhud1_GetModuleHandleA('lemuen.dll')
    if module == nil then return end
    local gp, sp = k32.lemhud1_GetProcAddress(module, 'lemuen_option_get'), k32.lemhud1_GetProcAddress(module, 'lemuen_option_set')
    if gp == nil or sp == nil then O.done = true; log('[Lemuen HUD] options: this lemuen.dll has no options (update Lemuen)') return end
    O.done = true
    local get, set = ffi.cast('lemopt_get', gp), ffi.cast('lemopt_set', sp)
    local out = ffi.new('double[1]')
    local count = 0
    for _, o in ipairs(OPTIONS) do
        local key, kind = o[1], o[2]
        if get(key, out) == 1 then
            local current = tonumber(out[0])
            local id = 'yui.lemuen.' .. key
            local spec = {type = kind, label = o[3], mod = 'Lemuen', mod_id = 'yui.lemuen', description = o[7], gap = o[8] or nil}
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
                log('[Lemuen HUD] options: ' .. key .. ' not added: ' .. tostring(why))
            end
        end
    end
    log(('[Lemuen HUD] options: %d settings on the MODS tab'):format(count))
end

------------------------------------------------------------------------------- her magazine, for her reloads --
-- The rounds in the gun in hand (HD2Runtime's player:ammo()), passed to lemuen.dll every frame (lemuen_ammo): the DLL
-- sees a reload as the rounds going up, which ends Salve Etiam and Honesta Missio.
local A = {fn = nil, failed = false}
local function ammo_step()
    if A.failed then return end
    if not A.fn then
        local module = k32.lemhud1_GetModuleHandleA('lemuen.dll')
        if module == nil then return end
        local proc = k32.lemhud1_GetProcAddress(module, 'lemuen_ammo')
        if proc == nil then A.failed = true; log('[Lemuen HUD] ammo: lemuen.dll has no ammo entry (update Lemuen)') return end
        A.fn = ffi.cast('lemammo1', proc)
    end
    local me = hd2.local_player and hd2.local_player()
    if not me then return end
    local ok, a = pcall(me.ammo, me)
    if not ok or type(a) ~= 'table' or type(a.rounds) ~= 'number' then A.fn(1, 0, -1) return end
    A.fn(1, tonumber(a.entity_id) or 0, a.rounds)
    if not A.told then A.told = true; log(('[Lemuen HUD] ammo: %s (%s), %d rounds: reported each frame'):format(tostring(a.name), tostring(a.feed), a.rounds)) end
end


-- ---- Persecution's markers (0.1.2): a marker over each enemy she has marked --------------------------------------
-- Parts adapted from Chef Shared Loader by Th3chef (github.com/Th3chef/Chef-Shared-Loader, src/loader.lua): finding
-- the view camera (a living unit with Unit.num_cameras > 0, its Unit.camera(u, 1), searched a few hundred units a
-- frame), projecting a world point with Camera.world_to_screen (bottom-left origin) and the behind-the-camera check
-- (the point's offset along the camera's forward), with Th3chef's permission. lemuen.dll's lemuen_marks gives where
-- each mark stands (5 floats: x, y, z, seconds left, heavy).
pcall(ffi.cdef, [[ typedef int (*lemmarks1)(unsigned int version, float *out, int max);
typedef int (*lemhits1)(unsigned int version, float *out, int max, uint32_t *seq); ]])
local MK = {fn = nil, buf = ffi.new('float[160]'), look = 0, failed = false, told = false}
local CAM = {unit = nil, list = nil, i = 0, retry = 0}
local function S() return rawget(_G, 'stingray') end
local function cam_unit(s, t)
    if CAM.unit then
        local ok, alive = pcall(s.Unit.alive, CAM.unit)
        if ok and alive == true then return CAM.unit end
        CAM.unit = nil
    end
    if not CAM.list then
        if t < CAM.retry then return nil end
        local okw, w = pcall(s.Application.main_world)
        if not (okw and w) then CAM.retry = t + 2; return nil end
        local oku, units = pcall(s.World.units, w)
        if not (oku and type(units) == 'table') then CAM.retry = t + 2; return nil end
        CAM.list, CAM.i = units, 0
    end
    local last = math.min(#CAM.list, CAM.i + 500)
    for i = CAM.i + 1, last do
        local u = CAM.list[i]
        local oka, alive = pcall(s.Unit.alive, u)
        if oka and alive == true then
            local okn, n = pcall(s.Unit.num_cameras, u)
            if okn and type(n) == 'number' and n > 0 then CAM.unit, CAM.list = u, nil; return u end
        end
    end
    CAM.i = last
    if last >= #CAM.list then CAM.list = nil; CAM.retry = t + 2 end
    return nil
end
local mk_t = 0
-- (0.1.3: her damage numbers, below, share this frame's view)
local HN = {fn = nil, buf = ffi.new('float[320]'), seq = ffi.new('uint32_t[1]'), live = {}, primed = false}
local function view(d)
    local s = S()
    if type(s) ~= 'table' or not (s.Unit and s.Camera and s.Vector3 and s.Quaternion and s.World and s.Application) then return nil end
    local u = cam_unit(s, mk_t)
    if not u then return nil end
    local c = s.Unit.camera(u, 1)
    if not c then return nil end
    local cx, cy, cz = s.Vector3.to_elements(s.Camera.world_position(c))
    local fx, fy, fz = s.Vector3.to_elements(s.Quaternion.forward(s.Camera.world_rotation(c)))
    local W, H = d.width, d.height or (d.width * 9 / 16)
    -- the engine's screen pixels against the overlay's (the same at most resolutions; scaled if not)
    local bw, bh = W, H
    if s.Application.back_buffer_size then
        local okb, a, b = pcall(s.Application.back_buffer_size)
        if okb and type(a) == 'number' and a > 0 then bw, bh = a, b end
    end
    local kx, ky = W / bw, H / bh
    if not MK.told then MK.told = true; log(('[Lemuen HUD] view: overlay %dx%d, screen %dx%d'):format(W, H, bw, bh)) end
    return {s = s, c = c, cx = cx, cy = cy, cz = cz, fx = fx, fy = fy, fz = fz, W = W, H = H, kx = kx, ky = ky}
end
-- a world point on the overlay, or nil when behind the camera or off screen
local function project(v, x, y, z)
    local ax, ay, az = x - v.cx, y - v.cy, z - v.cz
    if ax * v.fx + ay * v.fy + az * v.fz <= 0.5 then return nil end
    local sx, sy = v.s.Vector3.to_elements(v.s.Camera.world_to_screen(v.c, v.s.Vector3(x, y, z)))
    if type(sx) ~= 'number' then return nil end
    sx, sy = sx * v.kx, v.H - sy * v.ky                    -- the overlay's origin is its top-left
    if sx <= 0 or sx >= v.W or sy <= 0 or sy >= v.H then return nil end
    return sx, sy, math.sqrt(ax * ax + ay * ay + az * az)
end
local function marks_draw(d, dt, v)
    if MK.failed then return end
    if not MK.fn then
        if mk_t < MK.look then return end
        MK.look = mk_t + 2
        local module = k32.lemhud1_GetModuleHandleA('lemuen.dll')
        local proc = module ~= nil and k32.lemhud1_GetProcAddress(module, 'lemuen_marks') or nil
        if proc == nil then return end
        MK.fn = ffi.cast('lemmarks1', proc)
    end
    local n = MK.fn(1, MK.buf, 32)
    if n <= 0 then return end
    local s, W, H = v.s, v.W, v.H
    local sc = d.scale or 1
    for i = 0, n - 1 do
        local x, y, z, left, heavy = MK.buf[i * 5], MK.buf[i * 5 + 1], MK.buf[i * 5 + 2], MK.buf[i * 5 + 3], MK.buf[i * 5 + 4] > 0.5
        z = z + (heavy and 3.5 or 2.2)
        local sx, sy, dist = project(v, x, y, z)
        if sx then
            do
                do
                    local half = (heavy and 11 or 8) * sc
                    local col = {230, 60, 50, left < 5 and math.floor(120 + 135 * (math.sin(mk_t * 8) * 0.5 + 0.5)) or 235}
                    local tk = math.max(1, 2 * sc)
                    d:rect(sx - half, sy - half, 2 * half, tk, col, 6); d:rect(sx - half, sy + half - tk, 2 * half, tk, col, 6)
                    d:rect(sx - half, sy - half, tk, 2 * half, col, 6); d:rect(sx + half - tk, sy - half, tk, 2 * half, col, 6)
                    d:rect(sx - 2 * sc, sy - 2 * sc, 4 * sc, 4 * sc, col, 6)
                    d:text(('%dm'):format(math.floor(dist + 0.5)), sx, sy + half + 2 * sc, {size = 13 * sc, colour = col, z = 6, align = 'center'})
                end
            end
        end
    end
end

-- ---- her damage numbers (0.1.3): an orange-red number over each hit, rising fast then easing off, ~2 s ------------
local HN_LIFE, HN_RISE, HN_MAX = 2.2, 46, 48
local function numbers_draw(d, dt, v)
    if not HN.fn then
        local module = k32.lemhud1_GetModuleHandleA('lemuen.dll')
        local proc = module ~= nil and k32.lemhud1_GetProcAddress(module, 'lemuen_hits') or nil
        if proc == nil then return end
        HN.fn = ffi.cast('lemhits1', proc)
    end
    local n = HN.fn(1, HN.buf, 64, HN.seq)
    if not HN.primed then HN.primed = true; n = 0 end    -- (not the hits from before the HUD looked)
    for i = 0, n - 1 do
        if #HN.live >= HN_MAX then table.remove(HN.live, 1) end
        HN.live[#HN.live + 1] = {x = HN.buf[i * 5], y = HN.buf[i * 5 + 1], z = HN.buf[i * 5 + 2] + 0.6, amount = HN.buf[i * 5 + 3],
                                 boosted = HN.buf[i * 5 + 4] > 0.5, t = 0, jx = (math.random() - 0.5) * 18}
    end
    local sc = d.scale or 1
    for i = #HN.live, 1, -1 do
        local h = HN.live[i]
        h.t = h.t + dt
        if h.t >= HN_LIFE then table.remove(HN.live, i)
        elseif v then
            local sx, sy = project(v, h.x, h.y, h.z)
            if sx then
                local rise = HN_RISE * (1 - math.exp(-h.t * 7))  -- fast, then slowing to a stop
                local pop = h.t < 0.12 and (1.35 - h.t / 0.12 * 0.35) or 1
                local a = h.t > HN_LIFE - 0.45 and (HN_LIFE - h.t) / 0.45 or 1
                local size = (h.boosted and 24 or 18) * sc * pop
                local col = h.boosted and {255, 120, 40} or {255, 95, 60}
                local str = tostring(math.floor(h.amount + 0.5))
                local px, py = sx + h.jx * sc, sy - (rise + 6) * sc
                d:text(str, px + 2 * sc, py + 2 * sc, {size = size, colour = {20, 10, 5, math.floor(200 * a)}, z = 7, align = 'center'})
                d:text(str, px, py, {size = size, colour = {col[1], col[2], col[3], math.floor(255 * a)}, z = 8, align = 'center'})
            end
        end
    end
end

-- 0.1.6: why nothing was drawn. 0.1.5's test drew no marker and no number with marks standing and boosted hits, and
-- logged nothing: every few seconds in a mission (12 times at most) this logs how far the drawing got
local DG = {next = 5, told = 0, view_nil = 0, view_ok = 0}
local function diag(v)
    if DG.told >= 12 or mk_t < DG.next then return end
    DG.next = mk_t + 5
    DG.told = DG.told + 1
    local parts = {('view %s (ok %d, none %d)'):format(v and 'yes' or 'no', DG.view_ok, DG.view_nil)}
    parts[#parts + 1] = 'marks fn ' .. (MK.fn and 'yes' or 'no') .. (MK.failed and ' (failed)' or '')
    parts[#parts + 1] = 'hits fn ' .. (HN.fn and 'yes' or 'no') .. ', seq ' .. tostring(HN.seq[0]) .. ', live ' .. #HN.live
    if MK.fn then
        local n = MK.fn(1, MK.buf, 32)
        parts[#parts + 1] = 'marks ' .. tostring(n)
        if n > 0 and v then
            local x, y, z = MK.buf[0], MK.buf[1], MK.buf[2] + 2.2
            local ax, ay, az = x - v.cx, y - v.cy, z - v.cz
            local ahead = ax * v.fx + ay * v.fy + az * v.fz
            local okw, r = pcall(v.s.Camera.world_to_screen, v.c, v.s.Vector3(x, y, z))
            local rx, ry, rz
            if okw and r ~= nil then rx, ry, rz = v.s.Vector3.to_elements(r) end
            local sx, sy = project(v, x, y, z)
            parts[#parts + 1] = ('first mark at %.1f %.1f %.1f, camera %.1f %.1f %.1f, ahead %.1f m, raw %s, drawn at %s'):format(
                x, y, z, v.cx, v.cy, v.cz, ahead, okw and (rx and ('%.1f %.1f %.1f'):format(rx, ry, rz or 0) or tostring(r)) or ('error ' .. tostring(r)),
                sx and ('%.0f %.0f'):format(sx, sy) or 'nowhere (behind or off screen)')
        end
    end
    log('[Lemuen HUD] draw check: ' .. table.concat(parts, '; '))
end

local overlay = hd2.ui.overlay({id = 'kit', owner = OWNER})
overlay:draw(function(d, dt)
    dt = tonumber(dt) or 0
    WARN.dt = dt
    gfl2_step(dt)
    if not O.failed then
        local okO, errO = pcall(options_step, dt)
        if not okO then O.failed = true; log('[Lemuen HUD] options failed: ' .. tostring(errO)) end
    end
    if not find(dt) then return end
    if state.in_mission == 0 then return end
    mk_t = mk_t + dt
    local okV, v = pcall(view, d)
    if not okV then
        if not DG.view_err then DG.view_err = true; log('[Lemuen HUD] view failed: ' .. tostring(v)) end
        v = nil
    end
    if v then DG.view_ok = DG.view_ok + 1 else DG.view_nil = DG.view_nil + 1 end
    pcall(diag, v)
    if v then
        local okM, errM = pcall(marks_draw, d, dt, v)
        if not okM then MK.failed = true; log('[Lemuen HUD] markers failed, off for this session: ' .. tostring(errM)) end
    end
    if not HN.failed then
        local okN, errN = pcall(numbers_draw, d, dt, v)
        if not okN then HN.failed = true; log('[Lemuen HUD] damage numbers failed, off for this session: ' .. tostring(errN)) end
    end
    local okA, errA = pcall(ammo_step)
    if not okA then A.failed = true; log('[Lemuen HUD] ammo failed: ' .. tostring(errA)) end
    status_bar(d)
    local okL, box = pcall(list_box, d, dt)
    if not okL then L.on = false; log('[Lemuen HUD] following the list failed, abilities always shown: ' .. tostring(box)); box = nil end
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

log('[Lemuen HUD] ready (waiting for Lemuen)')
