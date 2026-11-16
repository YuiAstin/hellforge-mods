-- Commando Plus HUD (mods/yui/commando_plus_hud): a banner at the top of the screen once the Super Destroyer's window
-- runs past the game's own 60 s (Commando Plus buys it 15 s for every base cleared): "TIME BOUGHT FOR THE DESTROYER: 45s".
-- Drawn with HD2Runtime's screen overlay (hd2.ui.overlay); reads commando_plus.dll's exported state
-- (commando_plus_hud_state, layout version 1) through LuaJIT's ffi every frame. Requires Bingus Shared Loader and
-- HD2Runtime; uses HD2Runtime by SkyeShade (https://github.com/SkyeShade/HD2Runtime) through its public API.
local OWNER = 'mods/yui/commando_plus_hud'

local sink, sink_tried
local function log(message)
    pcall(print, message)
    if not sink_tried then
        sink_tried = true
        local ok, result = pcall(function() return CowboyBingusModLoader.open_log('CommandoPlusHUD.log') end)
        if ok then sink = result end
    end
    if sink then
        if not pcall(function() sink:write(message .. '\n'); if sink.flush then sink:flush() end end) then sink = nil end
    end
end

local ok, hd2 = pcall(require, 'mods/skyeshade/hd2runtime')
if not ok or type(hd2) ~= 'table' or type(hd2.ui) ~= 'table' or type(hd2.ui.overlay) ~= 'function' then
    log('[Commando Plus HUD] HD2Runtime with hd2.ui.overlay is not available: no banner (' .. tostring(hd2) .. ')')
    return
end

local ffi = require('ffi')
pcall(ffi.cdef, [[
typedef struct cphud1_state { uint32_t version, show; float bought_left, bought_total; uint32_t bases; } cphud1_state;
void *cphud1_GetModuleHandleA(const char *name) __asm__("GetModuleHandleA");
void *cphud1_GetProcAddress(void *module, const char *name) __asm__("GetProcAddress");
]])
local k32 = ffi.load('kernel32')

local YELLOW, INK, PANEL, TEXT = {255, 232, 0}, {8, 9, 10}, {12, 14, 16}, {240, 243, 245}
local function rgba(c, a) return {c[1], c[2], c[3], math.floor(255 * (a or 1) + 0.5)} end

local state, fn, find_wait = nil, nil, 0
local function find(dt)
    if fn ~= nil then state = fn() return state end
    find_wait = find_wait - dt
    if find_wait > 0 then return nil end
    find_wait = 2
    local module = k32.cphud1_GetModuleHandleA('commando_plus.dll')
    if module == nil then return nil end
    local proc = k32.cphud1_GetProcAddress(module, 'commando_plus_hud_state')
    if proc == nil then return nil end
    fn = ffi.cast('cphud1_state *(*)(void)', proc)
    state = fn()
    if state == nil or state.version < 1 then fn = nil return nil end
    log('[Commando Plus HUD] connected to Commando Plus')
    return state
end

-- the banner fades in over 0.3 s and out over 0.5 s
local fade = 0
local last_left = 0
local overlay = hd2.ui.overlay({id = 'banner', owner = OWNER})
overlay:draw(function(d, dt)
    dt = tonumber(dt) or 0
    local s = find(dt)
    local on = s ~= nil and s.show ~= 0
    if on then
        last_left = tonumber(s.bought_left)
        fade = math.min(1, fade + dt / 0.3)
    else
        fade = math.max(0, fade - dt / 0.5)
    end
    if fade <= 0 then return end
    local sc = d.scale
    local cx, y = d.width / 2, 96 * sc
    local secs = math.max(0, math.ceil(last_left))
    local title = 'TIME BOUGHT FOR THE DESTROYER'
    local value = ('%ds'):format(secs)
    local size, vsize = 20 * sc, 30 * sc
    local w, h = 520 * sc, 74 * sc
    d:rect(cx - w / 2, y, w, h, rgba(PANEL, 0.72 * fade), 0)
    d:rect(cx - w / 2, y, w, 3 * sc, rgba(YELLOW, fade), 1)
    d:rect(cx - w / 2, y + h - 3 * sc, w, 3 * sc, rgba(YELLOW, fade), 1)
    local function heavy(str, x, ty, sz, colour)
        d:text(str, x + math.max(1, 2 * sc), ty + math.max(1, 2 * sc), {size = sz, font = 'title', colour = rgba(INK, 0.75 * fade), z = 4, align = 'center'})
        d:text(str, x, ty, {size = sz, font = 'title', colour = colour, z = 5, align = 'center'})
    end
    heavy(title, cx, y + 8 * sc, size, rgba(TEXT, fade))
    heavy(value, cx, y + 32 * sc, vsize, rgba(secs <= 10 and {255, 138, 36} or YELLOW, fade))
end)

log('[Commando Plus HUD] ready (waiting for Commando Plus)')
