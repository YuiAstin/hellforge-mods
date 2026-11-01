-- Lenna HUD (mods/yui/lenna_hud): Lenna's abilities beside the mission's stratagem list (shown with it, as the
-- stratagems are: icon, name, key, cooldown) and her CI at the bottom middle, drawn with HD2Runtime's screen overlay
-- (hd2.ui.overlay) and its mod images (images/*.png). Lenna is a HellForge code mod; this reads its state through LuaJIT's ffi (lenna.dll exports
-- lenna_hud_state, layout version 1) every frame. Requires Bingus Shared Loader and HD2Runtime; uses HD2Runtime by
-- SkyeShade (https://github.com/SkyeShade/HD2Runtime) through its public API.
local OWNER = 'mods/yui/lenna_hud'

local sink, sink_tried
local function log(message)
    pcall(print, message)
    if not sink_tried then
        sink_tried = true
        local ok, result = pcall(function() return CowboyBingusModLoader.open_log('LennaHUD.log') end)
        if ok then sink = result end
    end
    if sink then
        if not pcall(function() sink:write(message .. '\n'); if sink.flush then sink:flush() end end) then sink = nil end
    end
end

local ok, hd2 = pcall(require, 'mods/skyeshade/hd2runtime')
if not ok or type(hd2) ~= 'table' or type(hd2.ui) ~= 'table' or type(hd2.ui.overlay) ~= 'function' then
    log('[Lenna HUD] HD2Runtime with hd2.ui.overlay is not available: no HUD (' .. tostring(hd2) .. ')')
    return
end

local ffi = require('ffi')
pcall(ffi.cdef, [[
typedef struct lennahud1_state { uint32_t version, in_mission, ci, ci_max; float cd[3], cd_max[3]; float charge, arc, stealth, shield; uint32_t charge_hits, marked, keys[3]; } lennahud1_state;
typedef struct lennahud2_state { uint32_t version, in_mission, ci, ci_max; float cd[3], cd_max[3]; float charge, arc, stealth, shield; uint32_t charge_hits, marked, keys[3]; char key_label[3][24]; } lennahud2_state;
typedef struct lennahud3_state { uint32_t version, in_mission, ci, ci_max; float cd[3], cd_max[3]; float charge, arc, stealth, shield; uint32_t charge_hits, marked, keys[3]; char key_label[3][24]; float stamina; uint32_t hellbomb; } lennahud3_state;
void *lennahud1_GetModuleHandleA(const char *name) __asm__("GetModuleHandleA");
void *lennahud1_GetProcAddress(void *module, const char *name) __asm__("GetProcAddress");
]])
local k32 = ffi.load('kernel32')

local ELECTRIC, YELLOW, INK, PANEL, TEXT, MUTED = {96, 205, 255}, {255, 232, 0}, {8, 9, 10}, {12, 14, 16}, {240, 243, 245}, {150, 156, 160}
local NAMES = {'WILD EXTINCTION', 'LEAPING PURSUIT', 'HUNTING STRATEGY'}
local SHORT = {'WILD', 'LEAP', 'HUNT'}

local state, refresh, find_wait = nil, nil, 0
local function find(dt)
    if state ~= nil then refresh() return state end
    find_wait = find_wait - dt
    if find_wait > 0 then return nil end
    find_wait = 2
    local module = k32.lennahud1_GetModuleHandleA('lenna.dll')
    if module == nil then return nil end
    local proc = k32.lennahud1_GetProcAddress(module, 'lenna_hud_state')
    if proc == nil then return nil end
    local fn = ffi.cast('lennahud1_state *(*)(void)', proc)
    local s = fn()
    if s == nil or s.version < 1 or s.version > 3 then return nil end
    if s.version >= 3 then s = ffi.cast('lennahud3_state *', s)
    elseif s.version >= 2 then s = ffi.cast('lennahud2_state *', s) end
    refresh = function() fn() end
    state = s
    log('[Lenna HUD] connected to Lenna')
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
for i, id in ipairs({'lenna_wild', 'lenna_leap', 'lenna_hunt'}) do
    local okI, handle = pcall(hd2.resources.image, id)
    if okI then ICONS[i] = handle else log('[Lenna HUD] image ' .. id .. ': ' .. tostring(handle)) end
end

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
    log('[Lenna HUD] the stratagem list cannot be followed on this HD2Runtime: abilities always shown')
end
local said = {}
local function why_not(what, why)
    local key = what .. ': ' .. tostring(why)
    if said[key] then return end
    said[key] = true
    log('[Lenna HUD] list: ' .. key)
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
        log('[Lenna HUD] the stratagem list can be followed on this build')
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
    if view ~= L.last_view and (L.views or 0) < 40 then L.last_view = view; L.views = (L.views or 0) + 1; log('[Lenna HUD] list: ' .. view) end
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

---------------------------------------------------------------------------------------------- the abilities --
local function ability(d, i, x, y, size, alpha)
    local s = d.scale
    local left, full = tonumber(state.cd[i - 1]), math.max(0.01, tonumber(state.cd_max[i - 1]))
    local ready = left <= 0.05
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
    if not ready then
        -- the part of the cooldown left, darkening the icon from the top
        d:rect(x + t, y + t, size - 2 * t, (size - 2 * t) * math.min(1, left / full), rgba(INK, a * 0.55), 4)
        heavy(d, string.format('%.0f', math.ceil(left)), x + size / 2, y + size * 0.25, math.max(20 * s, size * 0.42), 'title', rgba(TEXT, a), 'center')
    end
    -- the name, and the key where a stratagem shows its code
    local tx = x + size + 10 * s
    local nsize = math.max(17 * s, size * 0.36)
    heavy(d, NAMES[i], tx, y + size * 0.02, nsize, 'title', ready and rgba(TEXT, a) or rgba(MUTED, a))
    local label = state.version >= 2 and ffi.string(state.key_label[i - 1]) or key_name(state.keys[i - 1])
    local ksize = math.max(18 * s, size * 0.4)
    heavy(d, label, tx, y + size * 0.5, ksize, 'title', ready and rgba(YELLOW, a) or rgba(MUTED, a))
    if not ready then
        local kw = d:text_width(label, ksize, 'title') or (ksize * #label * 0.6)
        heavy(d, string.format('%.0fs', math.ceil(left)), tx + kw + 12 * s, y + size * 0.5, ksize, 'title', rgba(TEXT, a * 0.8))
    end
end

local function abilities(d, box)
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
    -- a panel like the list's
    local w = size + 10 * s + math.max(230 * s, size * 5)
    d:rect(x - 10 * s, y - 10 * s, w + 20 * s, step * 3 + 20 * s - (step - size), rgba(PANEL, a * 0.72), 0)
    d:rect(x - 10 * s, y - 10 * s, 3 * s, step * 3 + 20 * s - (step - size), rgba(ELECTRIC, a), 1)
    for i = 1, 3 do ability(d, i, x, y + (i - 1) * step, size, a) end
end

------------------------------------------------------------------------------------ CI and effects, bottom middle --
local function ci_bar(d)
    local s = d.scale
    local H = d.height or (d.width * 9 / 16)
    local ci, max = tonumber(state.ci), math.max(1, tonumber(state.ci_max))
    local pw, ph, gap = 30 * s, 10 * s, 6 * s
    local total = max * pw + (max - 1) * gap
    local cx = d.width / 2
    local y = H - 64 * s
    local label = 'CI ' .. ci
    local lw = d:text_width(label, 22 * s, 'title') or 40 * s
    local x0 = cx - (total + lw + 14 * s) / 2
    heavy(d, label, x0, y - 6 * s, 22 * s, 'title', ci >= 3 and rgba(YELLOW, 1) or rgba(TEXT, 1))
    local px = x0 + lw + 14 * s
    for i = 1, max do
        local filled = i <= ci
        d:rect(px + (i - 1) * (pw + gap), y, pw, ph, filled and rgba(i <= 3 and YELLOW or ELECTRIC, 1) or rgba(MUTED, 0.35), 2)
    end
    -- effects above it
    local bits = {}
    if tonumber(state.charge_hits) > 0 then bits[#bits + 1] = string.format('CHARGED x%d %.0fs', tonumber(state.charge_hits), math.ceil(state.charge))
    elseif state.charge > 0 then bits[#bits + 1] = string.format('CHARGED %.0fs', math.ceil(state.charge)) end
    if state.arc > 0 then bits[#bits + 1] = string.format('ARC %.0fs', math.ceil(state.arc)) end
    if state.stealth > 0 then bits[#bits + 1] = string.format('STEALTH %.0fs', math.ceil(state.stealth)) end
    if state.shield > 0 then bits[#bits + 1] = 'STEADY' end
    if tonumber(state.marked) > 0 then bits[#bits + 1] = 'MARKED ' .. tonumber(state.marked) end
    local effects_y = y - 34 * s
    if #bits > 0 then heavy(d, table.concat(bits, '   '), cx, effects_y, 19 * s, 'title', rgba(ELECTRIC, 1), 'center') end
    -- running dry sets off the Hellbomb: a flashing red warning above it all while her stamina runs low
    if state.version >= 3 and state.hellbomb ~= 0 then
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
-- Lenna's numbers as rows on the escape menu's MODS tab (Mod Options Menu by CowboyBingus, its public api 1), read
-- from and written to lenna.dll (lenna_option_get / lenna_option_set, the lenna.ini names). The menu keeps what the
-- player applies; until then a row starts from lenna.ini. Keys stay in lenna.ini.
local OPTIONS = {
    {'ci_max', 'slider', 'Max CI', 3, 12, 1, 'Most CI she holds. Enhanced abilities spend 3.'},
    {'we_cooldown', 'slider', 'Wild Extinction: Cooldown', 0, 60, 1, 'Seconds before Wild Extinction can be used again.', true},
    {'we_window', 'slider', 'Wild Extinction: Duration', 1, 15, 0.5, 'Seconds her hits stay charged.'},
    {'we_mult', 'slider', 'Wild Extinction: Damage', 1, 5, 0.1, 'Charged hits deal this times their damage.'},
    {'we_mult_enhanced', 'slider', 'Wild Extinction: Enhanced Damage', 1, 8, 0.1, 'The same, when cast with 3 CI.'},
    {'lp_cooldown', 'slider', 'Leaping Pursuit: Cooldown', 0, 60, 1, 'Seconds before Leaping Pursuit can be used again.', true},
    {'lp_distance', 'slider', 'Leaping Pursuit: Distance', 3, 25, 1, 'Metres the teleport covers along her aim.'},
    {'lp_window', 'slider', 'Leaping Pursuit: Protection', 0, 10, 0.5, 'Seconds after the teleport with no knockdown, no fall damage and less damage taken.'},
    {'lp_damage_taken', 'slider', 'Leaping Pursuit: Damage Taken', 0, 1, 0.05, 'Share of damage she takes during the protection.'},
    {'lp_heal', 'slider', 'Leaping Pursuit: Heal', 0, 100, 5, 'Health restored on each teleport.'},
    {'lp_speed', 'slider', 'Leaping Pursuit: Speed Boost', 1, 2, 0.05, 'Her jog and sprint speed after the teleport.'},
    {'lp_speed_seconds', 'slider', 'Leaping Pursuit: Boost Duration', 0, 15, 0.5, 'Seconds the speed boost lasts.'},
    {'lp_stun', 'slider', 'Leaping Pursuit: Stun', 0, 10, 0.5, 'Seconds enemies near where she left and landed are stunned.'},
    {'hs_cooldown', 'slider', 'Hunting Strategy: Cooldown', 0, 120, 5, 'Seconds before Hunting Strategy can be used again.', true},
    {'hs_radius', 'slider', 'Hunting Strategy: Radius', 5, 40, 1, 'Metres around her the arc zap reaches.'},
    {'hs_pulses', 'slider', 'Hunting Strategy: Extra Zaps', 0, 6, 1, 'Zaps after the first, two seconds apart.'},
    {'hs_stun', 'slider', 'Hunting Strategy: Stun', 0, 20, 1, 'Seconds zapped enemies stay stunned.'},
    {'hs_arc', 'slider', 'Hunting Strategy: Electric Arc', 0, 20, 1, 'Seconds her hits stun and mark after the cast.'},
    {'hs_stealth', 'slider', 'Hunting Strategy: Stealth', 0, 30, 1, 'Seconds of Stealth when cast with 3 CI.'},
    {'mark_mult', 'slider', 'Conductivity: Damage', 1, 3, 0.05, 'Marked enemies take this times the damage.', true},
    {'mark_seconds', 'slider', 'Conductivity: Duration', 1, 20, 1, 'Seconds a mark lasts.'},
    {'hp_mult', 'slider', 'Body: Health', 0.5, 4, 0.1, 'Her health, times a Helldiver\'s.', true},
    {'stamina_mult', 'slider', 'Body: Stamina', 0.5, 6, 0.1, 'How long she sprints before the bar empties, times a Helldiver\'s.'},
    {'hellbomb_on_exhaustion', 'toggle', 'Body: Hellbomb When Exhausted', nil, nil, nil, 'Running her stamina dry sets off a Hellbomb on her. For fun.'},
}
pcall(ffi.cdef, [[
typedef int (*lennaopt_get)(const char *name, double *out);
typedef int (*lennaopt_set)(const char *name, double value);
]])
local O = {done = false, wait = 0}
local function options_step(dt)
    if O.done then return end
    O.wait = O.wait - dt
    if O.wait > 0 then return end
    O.wait = 2
    local menu = rawget(_G, 'ModOptionsMenu')
    if not menu or menu.api ~= 1 then return end
    local module = k32.lennahud1_GetModuleHandleA('lenna.dll')
    if module == nil then return end
    local gp, sp = k32.lennahud1_GetProcAddress(module, 'lenna_option_get'), k32.lennahud1_GetProcAddress(module, 'lenna_option_set')
    if gp == nil or sp == nil then O.done = true; log('[Lenna HUD] options: this lenna.dll has no options (update Lenna)') return end
    O.done = true
    local get, set = ffi.cast('lennaopt_get', gp), ffi.cast('lennaopt_set', sp)
    local out = ffi.new('double[1]')
    local count = 0
    for _, o in ipairs(OPTIONS) do
        local key, kind = o[1], o[2]
        if get(key, out) == 1 then
            local current = tonumber(out[0])
            local id = 'yui.lenna.' .. key
            local spec = {type = kind, label = o[3], mod = 'Lenna', mod_id = 'yui.lenna', description = o[7], gap = o[8] or nil}
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
                log('[Lenna HUD] options: ' .. key .. ' not added: ' .. tostring(why))
            end
        end
    end
    log(('[Lenna HUD] options: %d settings on the MODS tab'):format(count))
end

local overlay = hd2.ui.overlay({id = 'kit', owner = OWNER})
overlay:draw(function(d, dt)
    dt = tonumber(dt) or 0
    WARN.dt = dt
    if not O.failed then
        local okO, errO = pcall(options_step, dt)
        if not okO then O.failed = true; log('[Lenna HUD] options failed: ' .. tostring(errO)) end
    end
    if not find(dt) then return end
    if state.in_mission == 0 then return end
    ci_bar(d)
    local okL, box = pcall(list_box, d, dt)
    if not okL then L.on = false; log('[Lenna HUD] following the list failed, abilities always shown: ' .. tostring(box)); box = nil end
    if box == false then return end      -- the list is closed
    abilities(d, box or nil)
end)

log('[Lenna HUD] ready (waiting for Lenna)')
