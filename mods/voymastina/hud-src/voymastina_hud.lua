-- Voymastina HUD (mods/yui/voymastina_hud): Voymastina's abilities beside the mission's stratagem list (shown with it, as
-- the stratagems are: icon, name, key, cooldown) and her Predation charge and Stability at the bottom middle, drawn with
-- HD2Runtime's screen overlay (hd2.ui.overlay). Voymastina is a HellForge code mod; this reads its state through LuaJIT's
-- ffi (voymastina.dll exports voymastina_hud_state) every frame. Requires Bingus Shared Loader and HD2Runtime; uses
-- HD2Runtime by SkyeShade (https://github.com/SkyeShade/HD2Runtime) through its public API and, to follow the stratagem
-- list, its internal slot overlay.
local OWNER = 'mods/yui/voymastina_hud'

local sink, sink_tried
local function log(message)
    pcall(print, message)
    if not sink_tried then
        sink_tried = true
        local ok, result = pcall(function() return CowboyBingusModLoader.open_log('VoymastinaHUD.log') end)
        if ok then sink = result end
    end
    if sink then
        if not pcall(function() sink:write(message .. '\n'); if sink.flush then sink:flush() end end) then sink = nil end
    end
end

local ok, hd2 = pcall(require, 'mods/skyeshade/hd2runtime')
if not ok or type(hd2) ~= 'table' or type(hd2.ui) ~= 'table' or type(hd2.ui.overlay) ~= 'function' then
    log('[Voymastina HUD] HD2Runtime with hd2.ui.overlay is not available: no HUD (' .. tostring(hd2) .. ')')
    return
end

local ffi = require('ffi')
-- keep in step with VoymastinaHud in voymastina.c (version 3: her two kits, the Sinbreaker's and the AK-15's on foot)
pcall(ffi.cdef, [[
typedef struct voymhud3_state { uint32_t version, in_mission; float cd[3], cd_max[3]; uint32_t pp, pp_max, empowered, broken; float stab, stab_max; float eye, follow, shield; uint32_t eye_shots, eye_max, keys[3]; char key_label[3][24]; float stamina; uint32_t hellbomb; float pas[2], pas_max[2]; uint32_t pas_on[2];
    uint32_t mode, mpb_ready, monitored, mpb_on; float ak_cd, ak_cd_max, ak_eye, overload, overload_max, overload_lock, rage, howl; } voymhud3_state;
void *voyhud1_GetModuleHandleA(const char *name) __asm__("GetModuleHandleA");
void *voyhud1_GetProcAddress(void *module, const char *name) __asm__("GetProcAddress");
]])
local k32 = ffi.load('kernel32')

local RED, WARN_AT, WARN = {255, 48, 40}, 0.25, {t = 0}
local STEEL, YELLOW, INK, PANEL, TEXT, MUTED = {150, 200, 255}, {255, 232, 0}, {8, 9, 10}, {12, 14, 16}, {240, 243, 245}, {150, 156, 160}
local EMPOWER = {255, 170, 60}
local NAMES = {'SIRIUS FALL', 'EYE OF THE WHITE MASTIFF', 'PILE BUNKER'}
local OVERLOAD_RED = {255, 90, 70}

-- the kit, for the shared parts below
local KIT = {name = 'Voymastina', id = 'voymastina', module = k32.voyhud1_GetModuleHandleA, proc = k32.voyhud1_GetProcAddress}
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
local SHORT = {'FALL', 'EYE', 'PILE'}

local state, refresh, find_wait = nil, nil, 0
local function find(dt)
    if state ~= nil then refresh() return state end
    find_wait = find_wait - dt
    if find_wait > 0 then return nil end
    find_wait = 2
    local module = k32.voyhud1_GetModuleHandleA('voymastina.dll')
    if module == nil then return nil end
    local proc = k32.voyhud1_GetProcAddress(module, 'voymastina_hud_state')
    if proc == nil then return nil end
    local fn = ffi.cast('voymhud3_state *(*)(void)', proc)
    local s = fn()
    if s == nil or s.version < 1 then return nil end
    refresh = function() fn() end
    state = s
    log('[Voymastina HUD] connected to Voymastina')
    return state
end

local function rgba(c, a) return {c[1], c[2], c[3], math.floor(255 * (a or 1) + 0.5)} end
-- on foot with her AK-15 kit (a DLL before 0.3.0 has one kit: the GFL2 one)
local function on_foot() return state.version >= 3 and state.mode == 0 end
-- text with a drop shadow; sizes stay at 17 px and up (at 1080p) so the strokes stay solid
local function heavy(d, str, x, y, size, font, colour, align)
    local s = d.scale
    d:text(str, x + math.max(1, 2 * s), y + math.max(1, 2 * s), {size = size, font = font, colour = rgba(INK, 0.75), z = 4, align = align})
    d:text(str, x, y, {size = size, font = font, colour = colour, z = 5, align = align})
end

local SOS_BEACON = 145   -- StratagemType of the SOS Beacon
local ORANGE, GREY = {255, 138, 36}, {120, 124, 128}
-- No skill icons yet: the tiles show short names. Once images/voymastina_sirius.png, voymastina_eye.png and
-- voymastina_pile.png exist, load them here (hd2.resources.image(id) for each, as the other GFL2 HUDs do).
local ICONS, ICON = {}, {}
local function load_icon(id)
    local okI, handle = pcall(hd2.resources.image, id)
    if okI then return handle end
    log('[Voymastina HUD] image ' .. id .. ': ' .. tostring(handle))
end
for i, id in ipairs({'voymastina_sirius', 'voymastina_eye', 'voymastina_pile'}) do ICONS[i] = load_icon(id) end
ICON.ak_eye, ICON.howl, ICON.pledge = load_icon('voymastina_ak_eye'), load_icon('voymastina_howl'), load_icon('voymastina_pledge')

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
    local foot = on_foot()
    if foot then left, full = tonumber(state.ak_cd), math.max(0.01, tonumber(state.ak_cd_max)) end
    local watching = foot and state.ak_eye > 0
    local locked = foot and state.overload_lock > 0
    local ready = left <= 0.05
    local follow = (i == 3 and state.follow > 0) or watching
    local a = alpha
    local accent = state.empowered ~= 0 and EMPOWER or STEEL
    -- the icon's tile, framed like a stratagem's
    local t = math.max(1, 2 * s)
    local frame = (ready or follow) and rgba(accent, a) or rgba(GREY, a * 0.8)
    d:rect(x, y, size, size, rgba(INK, a * 0.9), 1)
    d:rect(x, y, size, t, frame, 2); d:rect(x, y + size - t, size, t, frame, 2)
    d:rect(x, y, t, size, frame, 2); d:rect(x + size - t, y, t, size, frame, 2)
    local icon = foot and ICON.ak_eye or ICONS[i]
    if icon then
        local pad = size * 0.08
        d:image(icon, x + pad, y + pad, size - 2 * pad, size - 2 * pad, {z = 3,
            colours = ready and {r = rgba(ORANGE, 1), g = {255, 255, 255, 255}} or {r = rgba(GREY, 1), g = {150, 154, 158, 255}},
            colour = {255, 255, 255, math.floor(255 * a + 0.5)}})
    end
    if not icon then
        heavy(d, SHORT[i], x + size / 2, y + size * 0.3, math.max(14 * s, size * 0.3), 'title', ready and rgba(accent, a) or rgba(MUTED, a), 'center')
    end
    if not ready and not follow then
        -- the part of the cooldown left, darkening the icon from the top
        d:rect(x + t, y + t, size - 2 * t, (size - 2 * t) * math.min(1, left / full), rgba(INK, a * 0.55), 4)
        heavy(d, string.format('%.0f', math.ceil(left)), x + size / 2, y + size * 0.25, math.max(20 * s, size * 0.42), 'title', rgba(TEXT, a), 'center')
    end
    -- the name, and the key where a stratagem shows its code
    local tx = x + size + 10 * s
    local nsize = math.max(17 * s, size * 0.36)
    local name = NAMES[i]
    if foot then name = watching and 'EYE: MONITORING' or locked and 'EYE: OVERLOADED' or 'EYE (AK-15)'
    elseif follow then name = 'PILE BUNKER: DASH' end
    heavy(d, name, tx, y + size * 0.02, nsize, 'title', (ready or follow) and rgba(TEXT, a) or rgba(MUTED, a))
    local label = X.labels[i] or (ffi.string(state.key_label[i - 1]))
    local ksize = math.max(18 * s, size * 0.4)
    heavy(d, label, tx, y + size * 0.5, ksize, 'title', (ready or follow) and rgba(YELLOW, a) or rgba(MUTED, a))
    local kw = d:text_width(label, ksize, 'title') or (ksize * #label * 0.6)
    if watching then
        heavy(d, string.format('%.0fs  %d IN SIGHT', math.ceil(state.ak_eye), tonumber(state.monitored)), tx + kw + 12 * s, y + size * 0.5, ksize, 'title', rgba(STEEL, a))
    elseif follow then
        heavy(d, string.format('%.0fs', math.ceil(state.follow)), tx + kw + 12 * s, y + size * 0.5, ksize, 'title', rgba(STEEL, a))
    elseif not ready then
        heavy(d, string.format('%.0fs', math.ceil(left)), tx + kw + 12 * s, y + size * 0.5, ksize, 'title', rgba(TEXT, a * 0.8))
    end
end

-- a passive: a smaller tile with its short name, READY (yellow) or the seconds until it is
local PASSIVE_NAMES = {'SUPPORT ACTION', 'PILE BUNKER: CHARGE'}
local PASSIVE_SHORT = {'SUP', 'CHG'}
local PASSIVE_HINT = {'NEXT HIT', 'HIT IN REACH'}
local function passive(d, j, x, y, size, a)
    local s = d.scale
    local left = tonumber(state.pas[j - 1])
    local ready = left <= 0.05
    local t = math.max(1, 2 * s)
    local inset = size * 0.15
    local tx0, ty0, ts = x + inset, y + inset, size - 2 * inset
    local frame = ready and rgba(YELLOW, a) or rgba(GREY, a * 0.8)
    d:rect(tx0, ty0, ts, ts, rgba(INK, a * 0.9), 1)
    d:rect(tx0, ty0, ts, t, frame, 2); d:rect(tx0, ty0 + ts - t, ts, t, frame, 2)
    d:rect(tx0, ty0, t, ts, frame, 2); d:rect(tx0 + ts - t, ty0, t, ts, frame, 2)
    heavy(d, PASSIVE_SHORT[j], x + size / 2, ty0 + ts * 0.28, math.max(12 * s, ts * 0.32), 'title', ready and rgba(YELLOW, a) or rgba(MUTED, a), 'center')
    local tx = x + size + 10 * s
    local nsize = math.max(15 * s, size * 0.32)
    heavy(d, PASSIVE_NAMES[j], tx, y + size * 0.05, nsize, 'title', ready and rgba(TEXT, a) or rgba(MUTED, a))
    local ksize = math.max(16 * s, size * 0.36)
    if ready then heavy(d, 'READY: ' .. PASSIVE_HINT[j], tx, y + size * 0.5, ksize, 'title', rgba(YELLOW, a))
    else heavy(d, string.format('%.0fs', math.ceil(left)), tx, y + size * 0.5, ksize, 'title', rgba(TEXT, a * 0.8)) end
end

-- Pile Bunker's key as the list shows it ('C'): pressed again while it recharges, it sets off the follow-up
local function pile_key()
    local bound = X.labels[3]
    if bound and bound ~= 'UNBOUND' then return bound end
    return ffi.string(state.key_label[2])   -- the .ini's key, which works either way
end
-- the kit's other passives (version 3): {short, name, lit, text}
local function kit_passives()
    local list = {}
    if state.version < 3 then return list end
    if state.mode == 0 then
        local rage = tonumber(state.rage)
        list[#list + 1] = {'RGE', 'RAGE', rage > 0, rage > 0 and string.format('RAGING %.0fs', math.ceil(rage)) or 'A CHANCE ON EACH HIT'}
        local howl = tonumber(state.howl)
        list[#list + 1] = {'HWL', 'BLOOD-CURDLING HOWL', howl > 0, icon = ICON.howl, howl >= 98 and 'IMMUNE: GETTING UP' or howl > 0 and string.format('IMMUNE %.1fs', howl) or 'WHEN THE EYE SEES MANY'}
    elseif state.mpb_on ~= 0 then
        local ready = state.mpb_ready ~= 0
        list[#list + 1] = {'HB', 'PILE BUNKER: HELLBOMB', ready, ready and ('READY: ' .. pile_key() .. ' AGAIN') or 'KILL A HEAVY WITH PILE BUNKER'}
    end
    return list
end
local function kit_passive(d, p, x, y, size, a)
    local s = d.scale
    local lit = p[3]
    local t = math.max(1, 2 * s)
    local inset = size * 0.15
    local tx0, ty0, ts = x + inset, y + inset, size - 2 * inset
    local frame = lit and rgba(YELLOW, a) or rgba(GREY, a * 0.8)
    d:rect(tx0, ty0, ts, ts, rgba(INK, a * 0.9), 1)
    d:rect(tx0, ty0, ts, t, frame, 2); d:rect(tx0, ty0 + ts - t, ts, t, frame, 2)
    d:rect(tx0, ty0, t, ts, frame, 2); d:rect(tx0 + ts - t, ty0, t, ts, frame, 2)
    if p.icon then
        local pad = ts * 0.06
        d:image(p.icon, tx0 + pad, ty0 + pad, ts - 2 * pad, ts - 2 * pad, {z = 3,
            colours = lit and {r = rgba(ORANGE, 1), g = {255, 255, 255, 255}} or {r = rgba(GREY, 1), g = {150, 154, 158, 255}},
            colour = {255, 255, 255, math.floor(255 * a + 0.5)}})
    else
        heavy(d, p[1], x + size / 2, ty0 + ts * 0.28, math.max(12 * s, ts * 0.32), 'title', lit and rgba(YELLOW, a) or rgba(MUTED, a), 'center')
    end
    local tx = x + size + 10 * s
    heavy(d, p[2], tx, y + size * 0.05, math.max(15 * s, size * 0.32), 'title', lit and rgba(TEXT, a) or rgba(MUTED, a))
    heavy(d, p[4], tx, y + size * 0.5, math.max(16 * s, size * 0.36), 'title', lit and rgba(YELLOW, a) or rgba(TEXT, a * 0.8))
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
    if on_foot() then
        if not only or tonumber(state.ak_cd) > 0.05 or state.ak_eye > 0 then shown[1] = 2 end
    else
        for i = 1, 3 do if not only or tonumber(state.cd[i - 1]) > 0.05 then shown[#shown + 1] = i end end
    end
    -- the passives the other way round: with the list closed, shown only while ready
    local pshown = {}
    if state.version >= 2 then
        for j = 1, 2 do
            if state.pas_on[j - 1] ~= 0 and not on_foot() and (not only or tonumber(state.pas[j - 1]) <= 0.05) then pshown[#pshown + 1] = j end
        end
    end
    local kshown = {}
    for _, p in ipairs(kit_passives()) do if not only or p[3] then kshown[#kshown + 1] = p end end
    local rows = #shown + #pshown + #kshown
    if rows == 0 then return end
    -- a panel like the list's
    local w = size + 10 * s + math.max(300 * s, size * 6.5)
    d:rect(x - 10 * s, y - 10 * s, w + 20 * s, step * rows + 20 * s - (step - size), rgba(PANEL, a * 0.72), 0)
    d:rect(x - 10 * s, y - 10 * s, 3 * s, step * rows + 20 * s - (step - size), rgba(state.empowered ~= 0 and EMPOWER or STEEL, a), 1)
    for k, i in ipairs(shown) do ability(d, i, x, y + (k - 1) * step, size, a) end
    for k, j in ipairs(pshown) do passive(d, j, x, y + (#shown + k - 1) * step, size, a) end
    for k, p in ipairs(kshown) do kit_passive(d, p, x, y + (#shown + #pshown + k - 1) * step, size, a) end
end

------------------------------------------------------------------- Predation and Stability, bottom middle --
local function status_bar(d)
    local s = d.scale
    local H = d.height or (d.width * 9 / 16)
    local cx = d.width / 2
    local y = H - 64 * s
    -- Predation Protocol: the charge as pips, EMPOWERED once full
    local pp, max = tonumber(state.pp), math.max(1, tonumber(state.pp_max))
    local emp = state.empowered ~= 0
    local label = emp and 'EMPOWERED' or ('PREDATION ' .. pp)
    local foot = on_foot()
    if foot then   -- on foot: the AK-15 Eye's overload as pips (one per 4)
        max = math.max(1, math.ceil(tonumber(state.overload_max) / 4))
        pp = math.min(max, math.floor(tonumber(state.overload) / 4 + 0.5))
        emp = state.overload_lock > 0
        label = emp and string.format('OVERLOADED %.0fs', math.ceil(state.overload_lock)) or 'OVERLOAD'
        if emp then pp = max end
    end
    local lw = d:text_width(label, 22 * s, 'title') or 120 * s
    local pw, ph, gap = 18 * s, 10 * s, 4 * s
    local pips = max * pw + (max - 1) * gap
    -- Stability: a bar, BROKEN while it refills
    local bw, bh = 200 * s, 10 * s
    local broken = state.broken ~= 0
    local sl = broken and 'BROKEN' or 'STABILITY'
    local sw = d:text_width(sl, 22 * s, 'title') or 110 * s
    local total = lw + 14 * s + pips + 40 * s + sw + 14 * s + bw
    local x0 = cx - total / 2
    local hot = foot and OVERLOAD_RED or EMPOWER
    heavy(d, label, x0, y - 6 * s, 22 * s, 'title', emp and rgba(hot, 1) or (pp > 0 and rgba(STEEL, 1) or rgba(TEXT, 1)))
    local px = x0 + lw + 14 * s
    for i = 1, max do
        d:rect(px + (i - 1) * (pw + gap), y, pw, ph, (emp or i <= pp) and rgba(emp and hot or (foot and i > max * 0.6 and OVERLOAD_RED or STEEL), 1) or rgba(MUTED, 0.35), 2)
    end
    local sx = px + pips + 40 * s
    if ICON.pledge then   -- Mastiff's Pledge: her Stability
        local is = 30 * s
        d:image(ICON.pledge, sx - is - 6 * s, y - 12 * s, is, is, {z = 3,
            colours = broken and {r = rgba(RED, 1), g = {255, 255, 255, 255}} or {r = rgba(ORANGE, 1), g = {255, 255, 255, 255}},
            colour = {255, 255, 255, 255}})
    end
    heavy(d, sl, sx, y - 6 * s, 22 * s, 'title', broken and rgba(RED, 1) or rgba(TEXT, 1))
    local bx = sx + sw + 14 * s
    local frac = math.max(0, math.min(1, tonumber(state.stab) / math.max(1, tonumber(state.stab_max))))
    d:rect(bx, y, bw, bh, rgba(INK, 0.7), 1)
    d:rect(bx, y, bw * frac, bh, broken and rgba(RED, 0.8) or rgba(STEEL, 1), 2)
    local bits = {}
    if state.eye > 0 then bits[#bits + 1] = string.format('OVERWATCH %.0fs  %d/%d', math.ceil(state.eye), tonumber(state.eye_shots), tonumber(state.eye_max)) end
    if state.follow > 0 then bits[#bits + 1] = string.format('DASH READY %.0fs', math.ceil(state.follow)) end
    if state.shield > 0 then bits[#bits + 1] = 'DROP-IN SHIELD' end
    if state.version >= 3 then
        if state.mode == 0 then
            if state.ak_eye > 0 then bits[#bits + 1] = string.format('MONITORING %.0fs  %d', math.ceil(state.ak_eye), tonumber(state.monitored)) end
            if state.rage > 0 then bits[#bits + 1] = 'RAGE' end
            if state.howl > 0 then bits[#bits + 1] = 'HOWL: IMMUNE' end
        elseif state.mpb_on ~= 0 and state.mpb_ready ~= 0 then
            bits[#bits + 1] = 'HELLBOMB READY: ' .. pile_key() .. ' AGAIN'
        end
    end
    local effects_y = y - 34 * s
    if #bits > 0 then heavy(d, table.concat(bits, '   '), cx, effects_y, 19 * s, 'title', rgba(STEEL, 1), 'center') end
    -- running dry sets off the Hellbomb: a flashing red warning above it all while her stamina runs low
    if state.hellbomb ~= 0 then
        local st = tonumber(state.stamina)
        if st and st >= 0 and st < WARN_AT then
            WARN.t = WARN.t + (WARN.dt or 0)
            local urgency = 1 - st / WARN_AT                       -- 0 at the threshold, 1 empty
            local rate = 2 + 6 * urgency                           -- flashes faster as it empties
            local pulse = 0.5 + 0.5 * math.sin(WARN.t * rate * math.pi * 2)
            local wa = 0.35 + 0.65 * pulse
            local wy = effects_y - (#bits > 0 and 34 or 0) * s - 36 * s
            heavy(d, 'T-DOLL CATASTROPHIC FAILURE IMMINENT', cx, wy, 24 * s, 'title', rgba(RED, wa), 'center')
            local ww, wh = 300 * s, 6 * s
            d:rect(cx - ww / 2, wy + 32 * s, ww, wh, rgba(INK, 0.7), 1)
            d:rect(cx - ww / 2, wy + 32 * s, ww * math.max(0, st), wh, rgba(RED, wa), 2)
        else
            WARN.t = 0
        end
    end
end

------------------------------------------------------------------------- settings in the Mod Options Menu --
-- Voymastina's numbers as rows on the escape menu's MODS tab (Mod Options Menu by CowboyBingus, its public api 1), read
-- from and written to voymastina.dll (voymastina_option_get / voymastina_option_set, the voymastina.ini names). The menu
-- keeps what the player applies; until then a row starts from voymastina.ini. Keys and explosions stay in voymastina.ini.
-- (Mod Options Menu takes at most 32 rows a mod, and slider minimums below 0 read back as 0: the rest stay in the ini)
local OPTIONS = {
    {'sf_cooldown', 'slider', 'Sirius Fall: Cooldown', 0, 60, 1, 'Seconds before Sirius Fall can be used again.', true},
    {'sa_on', 'toggle', 'Support Action', nil, nil, nil, 'Her own shots on an enemy set off a follow-up blast on it.'},
    {'sa_every', 'slider', 'Support Action: Every', 0.5, 20, 0.5, 'Least seconds between two Support Action blasts.'},
    {'eye_cooldown', 'slider', 'Eye of the White Mastiff: Cooldown', 0, 120, 1, 'Seconds before the Eye can be used again. Using it makes Sirius Fall ready.', true},
    {'eye_seconds', 'slider', 'Eye: Duration', 1, 30, 1, 'Seconds of overwatch.'},
    {'eye_radius', 'slider', 'Eye: Radius', 5, 60, 1, 'Metres around her the overwatch covers.'},
    {'eye_max', 'slider', 'Eye: Most Lock Ons', 0, 60, 1, 'Most Lock On blasts per use.'},
    {'eye_move', 'slider', 'Eye: Movement That Triggers', 0.5, 6, 0.1, 'Metres an enemy must move in half a second to draw a Lock On (enemies that hit her always do).'},
    {'drop_on', 'toggle', 'Drop-in', nil, nil, nil, 'After each Hellpod drop, blasts on the nearest enemies, and she is shielded until they are out.', true},
    {'drop_targets', 'slider', 'Drop-in: Targets', 1, 12, 1, 'How many of the nearest enemies (within 40 m) take a blast.'},
    {'pb_cooldown', 'slider', 'Pile Bunker: Cooldown', 0, 60, 1, 'Seconds before Pile Bunker can be used again.', true},
    {'pb_lunge', 'slider', 'Pile Bunker: Lunge', 0, 10, 0.5, 'Metres she lunges along her aim before striking.'},
    {'pb_window', 'slider', 'Pile Bunker: Follow-up Window', 0, 10, 0.5, 'Seconds to press it again for a free dash. 0: no follow-up.'},
    {'pb_dash', 'slider', 'Pile Bunker: Follow-up Dash', 2, 20, 1, 'Metres the follow-up dash covers.'},
    {'pbp_on', 'toggle', 'Pile Bunker Charge', nil, nil, nil, 'When her shot or melee hits an enemy in reach, a line of blasts runs from her toward it, with a railcannon on it. She does not move.', true},
    {'pbp_radius', 'slider', 'Charge: Reach', 5, 40, 1, 'Metres: enemies she hits within it draw the charge (default 10).'},
    {'pbp_points', 'slider', 'Charge: Blasts', 1, 8, 1, 'Blasts in the line ahead of her, 2 m apart.'},
    {'pbp_every', 'slider', 'Charge: Every', 1, 30, 0.5, 'Least seconds between two charges.'},
    {'steady', 'toggle', "Mastiff's Pledge: Never Knocked Down", nil, nil, nil, 'She is never knocked down or ragdolled.', true},
    {'dr_base', 'slider', "Mastiff's Pledge: Damage Kept Off", 0, 1, 0.05, 'Share of any damage kept off her, always (0.3 = 30% less).'},
    {'dr_stable', 'slider', 'Stability: Damage Kept Off', 0, 1, 0.05, 'Share kept off while her Stability holds (0.8 = 80% less; 1 = none).'},
    {'stab_max', 'slider', 'Stability: Points', 10, 500, 10, 'How much Stability she has.'},
    {'stab_per_damage', 'slider', 'Stability: Drain Per Damage', 0, 3, 0.05, 'Stability a hit drains per point of its damage (at least 5 a hit).'},
    {'stab_regen', 'slider', 'Stability: Refill Per Second', 0, 100, 1, 'Stability back each second once she has gone unhurt a while.'},
    {'stab_delay', 'slider', 'Stability: Refill Delay', 0, 10, 0.5, 'Seconds unhurt before Stability refills.'},
    {'ap_bonus', 'slider', 'Armor Pierce', 0, 4, 1, 'Extra armor penetration on her hits and blasts.'},
    {'pp_max', 'slider', 'Predation: Charge Needed', 2, 12, 1, 'Charge (2 a skill) that empowers her next skill.', true},
    {'pp_mult', 'slider', 'Predation: Empowered Damage', 1, 4, 0.1, 'Damage of an empowered skill\'s blasts (they also pierce 2 more armor).'},
    {'hp_mult', 'slider', 'Body: Health', 0.5, 4, 0.1, 'Her health, times a Helldiver\'s.', true},
    {'stamina_mult', 'slider', 'Body: Stamina', 0.5, 6, 0.1, 'How long she sprints before the bar empties, times a Helldiver\'s.'},
    {'hellbomb_on_exhaustion', 'toggle', 'Body: Hellbomb When Exhausted', nil, nil, nil, 'Running her stamina dry sets off a Hellbomb on her. For fun.'},
}
pcall(ffi.cdef, [[
typedef int (*voymopt_get)(const char *name, double *out);
typedef int (*voymopt_set)(const char *name, double value);
]])
local O = {done = false, wait = 0}
local function options_step(dt)
    if O.done then return end
    O.wait = O.wait - dt
    if O.wait > 0 then return end
    O.wait = 2
    local menu = rawget(_G, 'ModOptionsMenu')
    if not menu or menu.api ~= 1 then return end
    local module = k32.voyhud1_GetModuleHandleA('voymastina.dll')
    if module == nil then return end
    local gp, sp = k32.voyhud1_GetProcAddress(module, 'voymastina_option_get'), k32.voyhud1_GetProcAddress(module, 'voymastina_option_set')
    if gp == nil or sp == nil then O.done = true; log('[Voymastina HUD] options: this voymastina.dll has no options (update Voymastina)') return end
    O.done = true
    local get, set = ffi.cast('voymopt_get', gp), ffi.cast('voymopt_set', sp)
    local out = ffi.new('double[1]')
    local count = 0
    for _, o in ipairs(OPTIONS) do
        local key, kind = o[1], o[2]
        if get(key, out) == 1 then
            local current = tonumber(out[0])
            local id = 'yui.voymastina.' .. key
            local spec = {type = kind, label = o[3], mod = 'Voymastina', mod_id = 'yui.voymastina', description = o[7], gap = o[8] or nil}
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
                log('[Voymastina HUD] options: ' .. key .. ' not added: ' .. tostring(why))
            end
        else
            log('[Voymastina HUD] options: voymastina.dll does not know ' .. key)
        end
    end
    log(('[Voymastina HUD] options: %d settings on the MODS tab'):format(count))
end

-- her ammo, told to the DLL four times a second: 1 when her primary and secondary are both empty (no rounds, no spare
-- magazines), read through HD2Runtime's player:ammo
local B = {wait = 0, fn = nil, failed = false, last = nil}
-- a slot's state for the DLL: 0 loaded, 1 empty (no rounds, no spare magazines), 2 not readable here (shell-fed, heat
-- or charge guns: the DLL reads shell-fed ones itself)
local function ammo_state(me, slot)
    local ok, a, why = pcall(me.ammo, me, slot)
    if not ok then return 0, 'error: ' .. tostring(a) end
    if type(a) ~= 'table' then
        why = tostring(why)
        if why:find('UNKNOWN_FEED', 1, true) then return 2, why end
        return 0, why
    end
    local r, m = tonumber(a.rounds) or -1, tonumber(a.spare_magazines) or 0
    return (r == 0 and m == 0) and 1 or 0, ('%s %s: %d rounds, %d spare'):format(tostring(a.name or a.type), tostring(a.feed), r, m)
end
local AMMO_NAMES = {[0] = 'loaded', 'empty', 'read by the DLL'}
local function bare_step(dt)
    B.wait = B.wait - dt
    if B.wait > 0 then return end
    B.wait = 0.25
    if not B.fn then
        local module = k32.voyhud1_GetModuleHandleA('voymastina.dll')
        if module == nil then return end
        local proc = k32.voyhud1_GetProcAddress(module, 'voymastina_ammo')
        if proc == nil then B.failed = true; log('[Voymastina HUD] ammo: voymastina.dll has no ammo entry') return end
        B.fn = ffi.cast('int (*)(unsigned int, int)', proc)
    end
    local me = hd2.local_player and hd2.local_player()
    if not me then return end
    local p, pw = ammo_state(me, 'primary')
    local s, sw = ammo_state(me, 'secondary')
    local key = p + s * 4
    if key ~= B.last then
        B.last = key
        B.told = (B.told or 0) + 1
        if B.told <= 40 then log(('[Voymastina HUD] ammo: primary %s (%s), secondary %s (%s)'):format(AMMO_NAMES[p], pw, AMMO_NAMES[s], sw)) end
    end
    if B.fn(2, key) == 0 and not B.warned then B.warned = true; log('[Voymastina HUD] ammo: voymastina.dll refused the report (older DLL?)') end
end

local overlay = hd2.ui.overlay({id = 'kit', owner = OWNER})
overlay:draw(function(d, dt)
    dt = tonumber(dt) or 0
    WARN.dt = dt
    gfl2_step(dt)
    if not O.failed then
        local okO, errO = pcall(options_step, dt)
        if not okO then O.failed = true; log('[Voymastina HUD] options failed: ' .. tostring(errO)) end
    end
    if not find(dt) then return end
    if state.in_mission == 0 then return end
    if not B.failed then
        local okB, errB = pcall(bare_step, dt)
        if not okB then B.failed = true; log('[Voymastina HUD] ammo reading failed: ' .. tostring(errB)) end
    end
    status_bar(d)
    local okL, box = pcall(list_box, d, dt)
    if not okL then L.on = false; log('[Voymastina HUD] following the list failed, abilities always shown: ' .. tostring(box)); box = nil end
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

log('[Voymastina HUD] ready (waiting for Voymastina)')
