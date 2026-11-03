-- Faelynn HUD (mods/yui/faelynn_hud): Faelynn's abilities beside the mission's stratagem list (shown with it, as the
-- stratagems are: icon, name, key, cooldown) and her Combo Pounce and Hunt Tracking at the bottom middle, drawn with
-- HD2Runtime's screen overlay (hd2.ui.overlay). Faelynn is a HellForge code mod; this reads its state through LuaJIT's
-- ffi (faelynn.dll exports faelynn_hud_state) every frame. Requires Bingus Shared Loader and HD2Runtime; uses HD2Runtime by
-- SkyeShade (https://github.com/SkyeShade/HD2Runtime) through its public API and, to follow the stratagem list, its
-- internal slot overlay.
local OWNER = 'mods/yui/faelynn_hud'

local sink, sink_tried
local function log(message)
    pcall(print, message)
    if not sink_tried then
        sink_tried = true
        local ok, result = pcall(function() return CowboyBingusModLoader.open_log('FaelynnHUD.log') end)
        if ok then sink = result end
    end
    if sink then
        if not pcall(function() sink:write(message .. '\n'); if sink.flush then sink:flush() end end) then sink = nil end
    end
end

local ok, hd2 = pcall(require, 'mods/skyeshade/hd2runtime')
if not ok or type(hd2) ~= 'table' or type(hd2.ui) ~= 'table' or type(hd2.ui.overlay) ~= 'function' then
    log('[Faelynn HUD] HD2Runtime with hd2.ui.overlay is not available: no HUD (' .. tostring(hd2) .. ')')
    return
end

local ffi = require('ffi')
pcall(ffi.cdef, [[
typedef struct faelynnhud2_state { uint32_t version, in_mission, cp, cp_max, ht, ht_max; float cd[3], cd_max[3]; float pursuit, hunt, bonus_left, bonus; uint32_t pursuit_stage, marked, in_mist, keys[3]; char key_label[3][24]; float stamina; uint32_t hellbomb; } faelynnhud2_state;
void *faehud1_GetModuleHandleA(const char *name) __asm__("GetModuleHandleA");
void *faehud1_GetProcAddress(void *module, const char *name) __asm__("GetProcAddress");
]])
local k32 = ffi.load('kernel32')

local RED, WARN_AT, WARN = {255, 48, 40}, 0.25, {t = 0}
local ACID, YELLOW, INK, PANEL, TEXT, MUTED = {150, 232, 60}, {255, 232, 0}, {8, 9, 10}, {12, 14, 16}, {240, 243, 245}, {150, 156, 160}
local ELECTRIC = ACID
local NAMES = {'TRIPLE MAUL', 'KNIFE-EARED HUNT', 'LOYAL DASH'}

-- the kit, for the shared parts below
local KIT = {name = 'Faelynn', id = 'faelynn', module = k32.faehud1_GetModuleHandleA, proc = k32.faehud1_GetProcAddress}
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
local SHORT = {'MAUL', 'HUNT', 'DASH'}

local state, refresh, find_wait = nil, nil, 0
local function find(dt)
    if state ~= nil then refresh() return state end
    find_wait = find_wait - dt
    if find_wait > 0 then return nil end
    find_wait = 2
    local module = k32.faehud1_GetModuleHandleA('faelynn.dll')
    if module == nil then return nil end
    local proc = k32.faehud1_GetProcAddress(module, 'faelynn_hud_state')
    if proc == nil then return nil end
    local fn = ffi.cast('faelynnhud2_state *(*)(void)', proc)
    local s = fn()
    if s == nil or s.version < 2 then return nil end
    refresh = function() fn() end
    state = s
    log('[Faelynn HUD] connected to Faelynn')
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

local SOS_BEACON = 145   -- StratagemType of the SOS Beacon
local ORANGE, GREY = {255, 138, 36}, {120, 124, 128}
local ICONS = {}
for i, id in ipairs({'faelynn_maul', 'faelynn_hunt', 'faelynn_dash'}) do
    local okI, handle = pcall(hd2.resources.image, id)
    if okI then ICONS[i] = handle else log('[Faelynn HUD] image ' .. id .. ': ' .. tostring(handle)) end
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
    local a = alpha
    -- the icon's tile, framed like a stratagem's
    local t = math.max(1, 2 * s)
    local frame = ready and rgba(ACID, a) or rgba(GREY, a * 0.8)
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
        heavy(d, SHORT[i], x + size / 2, y + size * 0.3, math.max(14 * s, size * 0.3), 'title', ready and rgba(ACID, a) or rgba(MUTED, a), 'center')
    end
    -- Pursuit's window shows on Loyal Dash's tile
    if i == 3 and state.pursuit > 0 then
        heavy(d, ({'I', 'II', 'III'})[tonumber(state.pursuit_stage) + 1] or '', x + size - 4 * s, y + 2 * s, math.max(14 * s, size * 0.3), 'title', rgba(YELLOW, a), 'right')
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
    local label = X.labels[i] or (ffi.string(state.key_label[i - 1]))
    local ksize = math.max(18 * s, size * 0.4)
    heavy(d, label, tx, y + size * 0.5, ksize, 'title', ready and rgba(YELLOW, a) or rgba(MUTED, a))
    if not ready then
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
    d:rect(x - 10 * s, y - 10 * s, 3 * s, step * #shown + 20 * s - (step - size), rgba(ACID, a), 1)
    for k, i in ipairs(shown) do ability(d, i, x, y + (k - 1) * step, size, a) end
end

------------------------------------------------------------------------------------ CI and effects, bottom middle --
local function ci_bar(d)
    local s = d.scale
    local H = d.height or (d.width * 9 / 16)
    local cp, max = tonumber(state.cp), math.max(1, tonumber(state.cp_max))
    local ht = tonumber(state.ht)
    local pw, ph, gap = 30 * s, 10 * s, 6 * s
    local total = max * pw + (max - 1) * gap
    local cx = d.width / 2
    local y = H - 64 * s
    local label = 'POUNCE ' .. cp
    local lw = d:text_width(label, 22 * s, 'title') or 80 * s
    local htl = 'HUNT ' .. ht
    local hw = d:text_width(htl, 22 * s, 'title') or 60 * s
    local x0 = cx - (total + lw + hw + 40 * s) / 2
    heavy(d, label, x0, y - 6 * s, 22 * s, 'title', cp > 0 and rgba(ACID, 1) or rgba(TEXT, 1))
    local px = x0 + lw + 14 * s
    for i = 1, max do
        d:rect(px + (i - 1) * (pw + gap), y, pw, ph, i <= cp and rgba(ACID, 1) or rgba(MUTED, 0.35), 2)
    end
    heavy(d, htl, px + total + 26 * s, y - 6 * s, 22 * s, 'title', ht > 0 and rgba(YELLOW, 1) or rgba(MUTED, 1))
    local bits = {}
    if state.pursuit > 0 then bits[#bits + 1] = string.format('PURSUIT %s %.0fs', ({'I', 'II', 'III'})[tonumber(state.pursuit_stage) + 1] or '', math.ceil(state.pursuit)) end
    if state.bonus_left > 0 then bits[#bits + 1] = string.format('+%.0f%% %.0fs', state.bonus * 100, math.ceil(state.bonus_left)) end
    if state.hunt > 0 then bits[#bits + 1] = string.format('HUNT %.0fs', math.ceil(state.hunt)) end
    if state.in_mist ~= 0 then bits[#bits + 1] = 'IN MIST' end
    if tonumber(state.marked) > 0 then bits[#bits + 1] = 'MARKED ' .. tonumber(state.marked) end
    local effects_y = y - 34 * s
    if #bits > 0 then heavy(d, table.concat(bits, '   '), cx, effects_y, 19 * s, 'title', rgba(ACID, 1), 'center') end
    -- running dry sets off the Hellbomb: a flashing red warning above it all while her stamina runs low
    if state.version >= 2 and state.hellbomb ~= 0 then
        local st = tonumber(state.stamina)
        if st and st >= 0 and st < WARN_AT then
            WARN.t = WARN.t + (WARN.dt or 0)
            local urgency = 1 - st / WARN_AT                       -- 0 at the threshold, 1 empty
            local rate = 2 + 6 * urgency                           -- flashes faster as it empties
            local pulse = 0.5 + 0.5 * math.sin(WARN.t * rate * math.pi * 2)
            local a = 0.35 + 0.65 * pulse
            local wy = effects_y - (#bits > 0 and 34 or 0) * s - 36 * s
            heavy(d, 'T-DOLL CATASTROPHIC FAILURE IMMINENT', cx, wy, 24 * s, 'title', rgba(RED, a), 'center')
            local bw, bh = 300 * s, 6 * s
            d:rect(cx - bw / 2, wy + 32 * s, bw, bh, rgba(INK, 0.7), 1)
            d:rect(cx - bw / 2, wy + 32 * s, bw * math.max(0, st), bh, rgba(RED, a), 2)
        else
            WARN.t = 0
        end
    end
end

------------------------------------------------------------------------- settings in the Mod Options Menu --
-- Faelynn's numbers as rows on the escape menu's MODS tab (Mod Options Menu by CowboyBingus, its public api 1), read
-- from and written to faelynn.dll (faelynn_option_get / faelynn_option_set, the faelynn.ini names). The menu keeps what the
-- player applies; until then a row starts from faelynn.ini. Keys stay in faelynn.ini.
-- (Mod Options Menu takes at most 32 rows a mod: the rest stay in faelynn.ini)
local OPTIONS = {
    {'cp_max', 'slider', 'Max Combo Pounce', 1, 6, 1, 'Most Combo Pounce stacks she holds. Each Pursuit Instinct dash spends one.'},
    {'ht_max', 'slider', 'Max Hunt Tracking', 0, 20, 1, 'Most Hunt Tracking stacks, from her hits on marked enemies.'},
    {'ht_per_stack', 'slider', 'Hunt Tracking: Damage Per Stack', 0, 0.2, 0.01, 'Extra damage per stack on marked enemies; doubled while she stands in her mist.'},
    {'tm_cooldown', 'slider', 'Triple Maul: Cooldown', 0, 60, 1, 'Seconds before Triple Maul can be used again.', true},
    {'tm_radius', 'slider', 'Triple Maul: Radius', 2, 15, 0.5, 'Metres around the target the gas blast reaches.'},
    {'tm_range', 'slider', 'Triple Maul: Range', 10, 100, 5, 'How far along her aim it looks for a target.'},
    {'pursuit_window', 'slider', 'Pursuit: Window', 1, 10, 0.5, 'Seconds to press Loyal Dash again for the next Pursuit Instinct dash.'},
    {'pursuit_bonus', 'slider', 'Pursuit: Damage Per Stage', 0, 2, 0.1, 'Extra damage on marked enemies per stage (I, II, III).'},
    {'pursuit_speed', 'slider', 'Pursuit: Speed Per Stage', 0, 0.5, 0.05, 'Extra movement speed per stage, stacking with Knife-Eared Hunt.'},
    {'kh_cooldown', 'slider', 'Knife-Eared Hunt: Cooldown', 0, 120, 5, 'Seconds before Knife-Eared Hunt can be used again.', true},
    {'kh_seconds', 'slider', 'Knife-Eared Hunt: Duration', 1, 30, 1, 'Seconds of speed, endless stamina, extra damage and free Pursuit dashes.'},
    {'kh_speed', 'slider', 'Knife-Eared Hunt: Speed', 1, 2, 0.05, 'Her jog and sprint speed during the hunt.'},
    {'kh_mult', 'slider', 'Knife-Eared Hunt: Damage', 1, 3, 0.05, 'Her damage during the hunt.'},
    {'ld_cooldown', 'slider', 'Loyal Dash: Cooldown', 0, 120, 5, 'Seconds before Loyal Dash can be used again.', true},
    {'ld_distance', 'slider', 'Loyal Dash: Distance', 3, 25, 1, 'Metres the dash covers along her aim.'},
    {'ld_width', 'slider', 'Loyal Dash: Strip Width', 1, 8, 0.5, 'Metres either side of her path the gas reaches.'},
    {'corrosion_per_stack', 'slider', 'Corrosion: Gas Damage Per Stack', 0, 2, 0.1, 'Each Corrosion stack on an enemy adds this much to the gas damage it takes. Her gas hits add stacks; Pursuit I-III add 2, 3, 4.', true},
    {'corrosion_max', 'slider', 'Corrosion: Max Stacks', 1, 12, 1, 'Most Corrosion stacks an enemy can carry.'},
    {'shred_per_stack', 'slider', 'Corrosion: Armor Pierce Per Stack', 0, 2, 0.1, 'Extra armor penetration every hit on a corroded enemy gets, per stack (rounded). Anyone\'s hits.', true},
    {'shred_max', 'slider', 'Corrosion: Max Armor Pierce', 0, 6, 1, 'The most extra armor penetration Corrosion can give a hit.'},
    {'mark_mult', 'slider', 'Scent Trail: Damage', 1, 3, 0.05, 'Marked enemies take this times the damage.', true},
    {'mark_seconds', 'slider', 'Scent Trail: Duration', 1, 20, 1, 'Seconds a mark lasts.'},
    {'mist_seconds', 'slider', 'Toxic Mist: Duration', 0, 20, 1, 'Seconds her gas zones count for her toughness, Hunt Tracking, and spare her.'},
    {'damage_taken', 'slider', 'Toughness: Damage Taken In Gas', 0, 1, 0.05, 'Share of any damage she takes while standing in her own gas. 0.1 = 90% less; 0 = invulnerable there, instant kills included.', true},
    {'damage_taken_outside', 'slider', 'Toughness: Damage Taken Outside', 0, 1, 0.05, 'Share of damage she takes outside her gas. 1 = full damage.'},
    {'hp_mult', 'slider', 'Body: Health', 0.5, 4, 0.1, 'Her health, times a Helldiver\'s.', true},
    {'stamina_mult', 'slider', 'Body: Stamina', 0.5, 6, 0.1, 'How long she sprints before the bar empties, times a Helldiver\'s.'},
    {'hellbomb_on_exhaustion', 'toggle', 'Body: Hellbomb When Exhausted', nil, nil, nil, 'Running her stamina dry sets off a Hellbomb on her. For fun.'},
    {'steady', 'toggle', 'Counterpounce: Never Knocked Down', nil, nil, nil, 'She is never knocked down or ragdolled.', true},
    {'gas_immune', 'toggle', 'Gas Immunity', nil, nil, nil, 'Any gas leaves her alone: no damage, cough or slow. Uses the helmet passive slot (Advanced Filtration), solo only.', true},
    {'bark_blasts', 'slider', 'Last Bark: Blasts', 0, 12, 1, 'Gas blasts in a ring where she goes down. 0: off.'},
    {'bark_damage', 'slider', 'Last Bark: Gas Damage', 0, 5000, 100, 'Least damage each hit of her dying gas deals. 0: the gas\'s own.'},
}
pcall(ffi.cdef, [[
typedef int (*faelynnopt_get)(const char *name, double *out);
typedef int (*faelynnopt_set)(const char *name, double value);
]])
local O = {done = false, wait = 0}
local function options_step(dt)
    if O.done then return end
    O.wait = O.wait - dt
    if O.wait > 0 then return end
    O.wait = 2
    local menu = rawget(_G, 'ModOptionsMenu')
    if not menu or menu.api ~= 1 then return end
    local module = k32.faehud1_GetModuleHandleA('faelynn.dll')
    if module == nil then return end
    local gp, sp = k32.faehud1_GetProcAddress(module, 'faelynn_option_get'), k32.faehud1_GetProcAddress(module, 'faelynn_option_set')
    if gp == nil or sp == nil then O.done = true; log('[Faelynn HUD] options: this faelynn.dll has no options (update Faelynn)') return end
    O.done = true
    local get, set = ffi.cast('faelynnopt_get', gp), ffi.cast('faelynnopt_set', sp)
    local out = ffi.new('double[1]')
    local count = 0
    for _, o in ipairs(OPTIONS) do
        local key, kind = o[1], o[2]
        if get(key, out) == 1 then
            local current = tonumber(out[0])
            local id = 'yui.faelynn.' .. key
            local spec = {type = kind, label = o[3], mod = 'Faelynn', mod_id = 'yui.faelynn', description = o[7], gap = o[8] or nil}
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
                log('[Faelynn HUD] options: ' .. key .. ' not added: ' .. tostring(why))
            end
        end
    end
    log(('[Faelynn HUD] options: %d settings on the MODS tab'):format(count))
end

local overlay = hd2.ui.overlay({id = 'kit', owner = OWNER})
overlay:draw(function(d, dt)
    dt = tonumber(dt) or 0
    WARN.dt = dt
    gfl2_step(dt)
    if not O.failed then
        local okO, errO = pcall(options_step, dt)
        if not okO then O.failed = true; log('[Faelynn HUD] options failed: ' .. tostring(errO)) end
    end
    if not find(dt) then return end
    if state.in_mission == 0 then return end
    ci_bar(d)
    local okL, box = pcall(list_box, d, dt)
    if not okL then L.on = false; log('[Faelynn HUD] following the list failed, abilities always shown: ' .. tostring(box)); box = nil end
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

log('[Faelynn HUD] ready (waiting for Faelynn)')
