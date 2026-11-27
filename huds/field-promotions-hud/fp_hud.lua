-- Veterancy HUD (mods/yui/field_promotions_hud): a banner for each level Veterancy System's gear earns in a
-- mission (the M-1000 Maxigun's with a green "call it in" box under it), drawn with HD2Runtime's screen overlay
-- (hd2.ui.overlay). Veterancy System is a HellForge code mod; this
-- reads its state through LuaJIT's ffi (field_promotions.dll exports fp_hud_state, layout version 3; 1 and 2 are still read)
-- every frame. Its banner settings are rows on the MODS tab (Mod Options Menu), through fp_option_get / fp_option_set.
-- Requires Bingus Shared Loader (v15+) and HD2Runtime (0.50.1 tested against). Uses HD2Runtime by SkyeShade
-- (https://github.com/SkyeShade/HD2Runtime) through its public API, and the rank badges use code / research from
-- HD2Runtime by SkyeShade: its stratagem slot overlay's mission HUD list and icon rectangles
-- (runtime/stratagem_slot_overlay.lua hud_entries and rect, behind its stratagem_selector proof), which are not part of
-- its public API and may change with an HD2Runtime update (the badges then switch themselves off; the banners stay).
local OWNER = 'mods/yui/field_promotions_hud'

-- Our log: VeterancyHUD.log beside Bingus' own (print goes nowhere in a HellForge session).
local sink, sink_tried
local function log(message)
    pcall(print, message)
    if not sink_tried then
        sink_tried = true
        local ok, result = pcall(function() return CowboyBingusModLoader.open_log('VeterancyHUD.log') end)
        if ok then sink = result end
    end
    if sink then
        if not pcall(function() sink:write(message .. '\n'); if sink.flush then sink:flush() end end) then sink = nil end
    end
end

local ok, hd2 = pcall(require, 'mods/skyeshade/hd2runtime')
if not ok or type(hd2) ~= 'table' or type(hd2.ui) ~= 'table' or type(hd2.ui.overlay) ~= 'function' then
    log('[Veterancy HUD] HD2Runtime with hd2.ui.overlay is not available: no banners (' .. tostring(hd2) .. ')')
    return
end

local ffi = require('ffi')
-- private names (Bingus AUTHORING: the first cdef of a name wins for the whole session)
pcall(ffi.cdef, [[
typedef struct fphud1_toast { uint32_t level; char title[64]; char line[160]; } fphud1_toast;
typedef struct fphud1_state { uint32_t version, seq, in_mission, toasts_on; fphud1_toast toasts[8]; } fphud1_state;
typedef struct fphud2_gear { uint32_t stratagem, level; } fphud2_gear;
typedef struct fphud2_state { uint32_t version, seq, in_mission, toasts_on; fphud1_toast toasts[8]; uint32_t badges_on, gear_count; fphud2_gear gear[16]; } fphud2_state;
typedef struct fphud3_state { uint32_t version, seq, in_mission, toasts_on; fphud1_toast toasts[8]; uint32_t badges_on, gear_count; fphud2_gear gear[16]; float toast_seconds; } fphud3_state;
typedef int (*fphud3_opt_get)(const char *name, double *out);
typedef int (*fphud3_opt_set)(const char *name, double value);
void *fphud1_GetModuleHandleA(const char *name) __asm__("GetModuleHandleA");
void *fphud1_GetProcAddress(void *module, const char *name) __asm__("GetProcAddress");
]])
local k32 = ffi.load('kernel32')

local SHOW, FADE_IN, FADE_OUT = 5.0, 0.25, 0.8   -- seconds
local YELLOW, INK, PANEL, TEXT, MUTED = {255, 232, 0}, {8, 9, 10}, {12, 14, 16}, {240, 243, 245}, {170, 176, 180}
local READY = {64, 224, 96}   -- the Maxigun's "call it in" box
local MAXIGUN_EXTRA = 2.0      -- seconds more a Maxigun banner stays (its box needs reading)

local state, last_seq, find_wait = nil, nil, 0
local refresh   -- fp_hud_state: called every frame, as it refreshes the levels and the mission flag it returns
local queue, current, age = {}, nil, 0

-- Veterancy System's state, found again every 2 s until the DLL is there.
local function find(dt)
    if state ~= nil then refresh() return state end
    find_wait = find_wait - dt
    if find_wait > 0 then return nil end
    find_wait = 2
    local module = k32.fphud1_GetModuleHandleA('field_promotions.dll')
    if module == nil then return nil end
    local proc = k32.fphud1_GetProcAddress(module, 'fp_hud_state')
    if proc == nil then return nil end
    local fn = ffi.cast('fphud1_state *(*)(void)', proc)
    local s = fn()
    if s == nil or s.version < 1 or s.version > 3 then return nil end
    if s.version >= 3 then s = ffi.cast('fphud3_state *', s)
    elseif s.version >= 2 then s = ffi.cast('fphud2_state *', s) end
    refresh = function() fn() end
    state = s
    last_seq = tonumber(s.seq)   -- only level-ups from now on
    log('[Veterancy HUD] connected to Veterancy System')
    return state
end

-- New toasts since the last frame (at most the 8 the ring keeps).
local function poll()
    local seq = tonumber(state.seq)
    if seq < last_seq then last_seq = seq end   -- the DLL was reloaded
    if seq - last_seq > 8 then last_seq = seq - 8 end
    while last_seq < seq do
        local t = state.toasts[last_seq % 8]
        queue[#queue + 1] = {level = tonumber(t.level), title = ffi.string(t.title), line = ffi.string(t.line)}
        last_seq = last_seq + 1
    end
end

local function with_alpha(c, a) return {c[1], c[2], c[3], math.floor(255 * a + 0.5)} end

-- Heavier text: the fonts are thin distance-field faces, so each line is drawn with a soft black shadow and twice,
-- a pixel or so apart, for a bolder stroke (BOLD: 0 draws it once).
local BOLD, SHADOW = 1, 2   -- pixels at 1080p
local function heavy(d, str, x, y, size, font, colour, a, z)
    local s = d.scale
    local sh = math.max(1, SHADOW * s)
    d:text(str, x + sh, y + sh, {size = size, font = font, colour = with_alpha(INK, a * 0.7), z = z})
    d:text(str, x, y, {size = size, font = font, colour = colour, z = z + 1})
    if BOLD > 0 then
        d:text(str, x + math.max(1, BOLD * s), y, {size = size, font = font, colour = colour, z = z + 1})
    end
end

-- One chevron (an upward ^) of small blocks: apex at (cx, top), `w` wide; fewer, chunkier blocks when small.
local function chevron(d, cx, top, w, colour, z)
    local n = w < 30 and 3 or 5
    local step = w / (2 * n - 1)
    for k = 0, n - 1 do
        local y = top + k * step * 0.9
        d:rect(cx - (k + 0.5) * step - step * 0.5, y, step * 1.25, step * 1.6, colour, z)
        d:rect(cx + (k + 0.5) * step - step * 0.75, y, step * 1.25, step * 1.6, colour, z)
    end
end

-- The rank badge: black square, yellow frame, one chevron for level 2, two for level 3.
local function badge(d, x, y, size, level, a)
    local frame, ink = with_alpha(YELLOW, a), with_alpha(INK, a * 0.95)
    local t = math.max(2, size * 0.06)
    d:rect(x, y, size, size, ink, 1)
    d:rect(x, y, size, t, frame, 2)
    d:rect(x, y + size - t, size, t, frame, 2)
    d:rect(x, y, t, size, frame, 2)
    d:rect(x + size - t, y, t, size, frame, 2)
    local w = size * 0.62
    if level >= 3 then
        chevron(d, x + size / 2, y + size * 0.20, w, frame, 3)
        chevron(d, x + size / 2, y + size * 0.48, w, frame, 3)
    else
        chevron(d, x + size / 2, y + size * 0.34, w, frame, 3)
    end
end

-------------------------------------------------------------------------------------------------- rank badges --
-- A badge on each promoted stratagem's icon in the mission's stratagem list, while the list shows it. The list's
-- entries and their icons' rectangles come from HD2Runtime's slot overlay (code / research from HD2Runtime by
-- SkyeShade); any failure turns the badges off for the session, quietly but for one log line.
local B = {on = true, entries = nil, wait = 0, proven = false, seen = {}}
local okA, SO = pcall(require, 'hd2runtime/runtime/stratagem_slot_overlay')
local okB, W = pcall(require, 'hd2runtime/runtime/event_world')
local okC, SEL = pcall(require, 'hd2runtime/runtime/stratagem_selector')
if not (okA and okB and okC and type(SO) == 'table' and type(SO.hud_entries) == 'function' and type(SO.rect) == 'function'
        and type(W) == 'table' and type(W.open) == 'function' and type(SEL) == 'table' and type(SEL.prove) == 'function') then
    B.on = false
    log('[Veterancy HUD] rank badges off: this HD2Runtime has no stratagem slot overlay to follow')
end

-- why the badges are not drawn: each distinct reason once
local said = {}
local function why_not(what, why)
    local key = what .. ': ' .. tostring(why)
    if said[key] then return end
    said[key] = true
    log('[Veterancy HUD] badges: ' .. key)
end

local function badges_off(why)
    B.on = false
    log('[Veterancy HUD] rank badges off: ' .. tostring(why))
end

-- {[stratagem type] = level} of the gear above level 1
local last_view
local function promoted()
    local out, any = {}, false
    local version = tonumber(state.version)
    local badges_on = version >= 2 and tonumber(state.badges_on) or -1
    local count = version >= 2 and tonumber(state.gear_count) or -1
    local view = ('version %d, badges %d, in mission %d, gear %d:'):format(version, badges_on, tonumber(state.in_mission), count)
    if version >= 2 then
        for i = 0, math.min(16, count) - 1 do
            local g = state.gear[i]
            local kind, level = tonumber(g.stratagem), tonumber(g.level)
            view = view .. (' %d=L%d'):format(kind, level)
            if level >= 2 then out[kind] = level; any = true end
        end
    end
    if view ~= last_view then last_view = view; log('[Veterancy HUD] state: ' .. view) end
    if version < 2 or badges_on == 0 or tonumber(state.in_mission) == 0 then return {}, false end
    return out, any
end

local function draw_badges(d, dt)
    if not B.on then return end
    local levels, any = promoted()
    if not any then B.entries = nil return end
    if not B.announced then
        B.announced = true
        local list = {}
        for k, v in pairs(levels) do list[#list + 1] = k .. '=L' .. v end
        log('[Veterancy HUD] badges: promoted stratagem types ' .. table.concat(list, ', '))
    end
    local ok, err = pcall(function()
        local world = W.open()
        if not world then why_not('no world', 'HD2Runtime has not opened the game world') return end
        if not B.proven then
            local proven, why = SEL.prove(world)
            if not proven then why_not('not proven yet', why) return end
            B.proven = true
            log('[Veterancy HUD] badges: the stratagem list can be followed on this build')
        end
        -- the list's entries, read again every half second (their places stay put while the HUD is up)
        B.wait = B.wait - dt
        if not B.entries or B.wait <= 0 then
            B.wait = 0.5
            local entries, why = SO.hud_entries(world)
            B.entries = entries
            if not entries then why_not('no list', why) end
        end
        if not B.entries then return end
        local H = d.height or (d.width * 9 / 16)
        for _, e in pairs(B.entries) do
            local kind = tonumber(e.type)
            if kind and not B.seen[kind] then
                B.seen[kind] = true
                log(('[Veterancy HUD] stratagem list shows type %d%s'):format(kind, levels[kind] and (' (level ' .. levels[kind] .. ')') or ''))
            end
            local level = kind and levels[kind]
            if level then
                local r, rwhy = SO.rect(world, e.icon)
                if not r then why_not('icon of type ' .. kind, rwhy) end
                if r then
                    if not B.drawn then B.drawn = true; log(('[Veterancy HUD] badges: first badge drawn (type %d at %.0f, %.0f, %.0f px)'):format(kind, r.x, r.y, r.h)) end
                    -- flush in the icon's top-right corner
                    local size = math.max(8, r.h * 0.28)
                    local x = r.x + r.w - size
                    local top = H - (r.y + r.h)
                    if x + size > d.width then x = d.width - size end
                    if top < 0 then top = 0 end
                    badge(d, x, top, size, level, r.alpha or 1)
                end
            end
        end
    end)
    if not ok then badges_off(err) end
end

------------------------------------------------------------------------- settings in the Mod Options Menu --
-- The banner settings as rows on the escape menu's MODS tab (Mod Options Menu by CowboyBingus, its public api 1), read
-- from and written to field_promotions.dll (fp_option_get / fp_option_set, the field-promotions.ini names). The menu
-- keeps what the player applies; until then a row starts from field-promotions.ini.
local OPTIONS = {
    {'hud_toasts', 'toggle', 'Promotion Banners', nil, nil, nil, 'A banner on screen each time your gear ranks up. Off: none (rank badges stay).'},
    {'hud_toast_seconds', 'slider', 'Banner Time', 2, 10, 0.5, 'Seconds a promotion banner stays on screen. The Maxigun\'s stays 2 s more for its call-in box.'},
}
local O = {done = false, wait = 0}
local function options_step(dt)
    if O.done then return end
    O.wait = O.wait - dt
    if O.wait > 0 then return end
    O.wait = 2
    local menu = rawget(_G, 'ModOptionsMenu')
    if not menu or menu.api ~= 1 then return end
    local module = k32.fphud1_GetModuleHandleA('field_promotions.dll')
    if module == nil then return end
    local gp, sp = k32.fphud1_GetProcAddress(module, 'fp_option_get'), k32.fphud1_GetProcAddress(module, 'fp_option_set')
    if gp == nil or sp == nil then O.done = true; log('[Veterancy HUD] options: this field_promotions.dll has no options (update Veterancy System)') return end
    O.done = true
    local get, set = ffi.cast('fphud3_opt_get', gp), ffi.cast('fphud3_opt_set', sp)
    local out = ffi.new('double[1]')
    local count = 0
    for _, o in ipairs(OPTIONS) do
        local key, kind = o[1], o[2]
        if get(key, out) == 1 then
            local current = tonumber(out[0])
            local id = 'yui.field-promotions.' .. key
            local spec = {type = kind, label = o[3], mod = 'Veterancy System', mod_id = 'yui.field-promotions', description = o[7]}
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
                log('[Veterancy HUD] options: ' .. key .. ' not added: ' .. tostring(why))
            end
        end
    end
    log(('[Veterancy HUD] options: %d settings on the MODS tab'):format(count))
end

local overlay = hd2.ui.overlay({id = 'toasts', owner = OWNER})
overlay:draw(function(d, dt)
    dt = tonumber(dt) or 0
    if not O.failed then
        local okO, errO = pcall(options_step, dt)
        if not okO then O.failed = true; log('[Veterancy HUD] options failed: ' .. tostring(errO)) end
    end
    if not find(dt) then return end
    poll()
    local okb, errb = pcall(draw_badges, d, dt)
    if not okb then badges_off(errb) end
    if state.toasts_on == 0 then queue, current = {}, nil return end
    if not current then
        current = table.remove(queue, 1)
        age = 0
        if not current then return end
    end
    age = age + dt
    -- a backlog shows each toast for less time
    local maxigun = current.title:find('MAXIGUN', 1, true) ~= nil
    local linger = SHOW
    if state.version >= 3 and tonumber(state.toast_seconds) > 0 then linger = tonumber(state.toast_seconds) end
    local base = maxigun and linger + MAXIGUN_EXTRA or linger
    local show = #queue > 0 and math.max(2.0, base - #queue) or base
    if age >= show then current = nil return end
    local a = math.min(1, age / FADE_IN, (show - age) / FADE_OUT)
    if a <= 0 then return end

    local s = d.scale
    local w, h = 740 * s, 118 * s
    local x, y = (d.width - w) / 2, 150 * s
    d:rect(x, y, w, h, with_alpha(PANEL, a * 0.88), 0)
    d:rect(x, y, w, 4 * s, with_alpha(YELLOW, a), 1)
    local b = 90 * s
    badge(d, x + 14 * s, y + (h - b) / 2, b, current.level, a)
    local tx = x + 14 * s + b + 18 * s
    local tw = x + w - 18 * s - tx
    heavy(d, current.title, tx, y + 12 * s, 30 * s, 'title', with_alpha(YELLOW, a), a, 2)
    heavy(d, current.level >= 3 and 'PROMOTED  -  ELITE' or 'PROMOTED  -  VETERAN', tx, y + 50 * s, 20 * s, 'title', with_alpha(TEXT, a), a, 2)
    local lines = d:wrap(current.line, tw, {size = 17 * s, font = 'body', lines = 1})
    if lines and lines[1] then
        heavy(d, lines[1], tx, y + 82 * s, 17 * s, 'body', with_alpha(MUTED, a), a, 2)
    end
    -- the Maxigun's level-ups: a box of their own saying a fresh gun is waiting (its call-in refunded, the line says)
    if maxigun then
        local by, bh = y + h + 8 * s, 50 * s
        local pulse = 0.82 + 0.18 * math.abs(math.sin(age * 3.2))
        local ready = current.line:find('is ready', 1, true) ~= nil
        d:rect(x, by, w, bh, with_alpha(READY, a * pulse), 0)
        d:rect(x, by, 6 * s, bh, with_alpha(INK, a), 1)
        d:rect(x + w - 6 * s, by, 6 * s, bh, with_alpha(INK, a), 1)
        local say = ready and '>>  FRESH MAXIGUN READY  -  CALL IT IN  <<' or '>>  CALL IN A FRESH MAXIGUN  <<'
        local size = 26 * s
        d:text(say, x + 26 * s, by + (bh - size) / 2, {size = size, font = 'title', colour = with_alpha(INK, a), z = 2})
        d:text(say, x + 26 * s + math.max(1, 1.5 * s), by + (bh - size) / 2, {size = size, font = 'title', colour = with_alpha(INK, a), z = 2})
    end
end)
log('[Veterancy HUD] ready (waiting for Veterancy System)')
