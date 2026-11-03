-- AI Probe HUD (mods/yui/ai_probe_hud): shows which mode the AI Probe's break-contact is in, and its live counts, so
-- the test needs no log to read. Reads ai_probe.dll's export ai_probe_hud (layout version 1) every frame through
-- LuaJIT's ffi. Research only. Uses HD2Runtime by SkyeShade (https://github.com/SkyeShade/HD2Runtime) through its
-- public API (hd2.ui.overlay).
local OWNER = 'mods/yui/ai_probe_hud'

local ok, hd2 = pcall(require, 'mods/skyeshade/hd2runtime')
if not ok or type(hd2) ~= 'table' or type(hd2.ui) ~= 'table' or type(hd2.ui.overlay) ~= 'function' then return end

local ffi = require('ffi')
pcall(ffi.cdef, [[
void *aiprobehud1_GetModuleHandleA(const char *name) __asm__("GetModuleHandleA");
void *aiprobehud1_GetProcAddress(void *module, const char *name) __asm__("GetProcAddress");
typedef int (*aiprobehud1_fn)(unsigned version, int *out, int n);
]])
local k32 = ffi.load('kernel32')
local out = ffi.new('int[9]')
local fn, next_look = nil, 0

local function read(t)
    if fn == nil then
        if t < next_look then return nil end
        next_look = t + 2   -- the DLL loads with the session: look again every 2 s until it is there
        local module = k32.aiprobehud1_GetModuleHandleA('ai_probe.dll')
        if module == nil then return nil end
        local proc = k32.aiprobehud1_GetProcAddress(module, 'ai_probe_hud')
        if proc == nil then return nil end
        fn = ffi.cast('aiprobehud1_fn', proc)
    end
    local okc, n = pcall(fn, 1, out, 9)
    if not okc or n ~= 9 then return nil end
    return out
end

local MODES = {
    [0] = {'BREAK-CONTACT: OFF', {170, 170, 170, 200}},
    [1] = {'BREAK-CONTACT: ON', {90, 230, 120, 255}},
    [2] = {'BREAK-CONTACT: OBSERVE', {255, 200, 60, 255}},
}

local t = 0
local overlay = hd2.ui.overlay({id = 'mode', owner = OWNER})
overlay:draw(function(d, dt)
    t = t + (tonumber(dt) or 0)
    local s = read(t)
    if not s or s[8] ~= 1 then return end
    local k = d.scale or 1
    local x, y = 24 * k, d.height * 0.32
    local m = MODES[s[0]] or MODES[0]
    d:text('AI PROBE  ' .. m[1], x, y, {size = 18 * k, colour = m[2], z = 5})
    if s[0] ~= 0 then
        d:text(('within 80 m %d   after you %d   sensing you %d   unsensed %d'):format(s[1], s[2], s[3], s[4]), x, y + 24 * k,
            {size = 15 * k, colour = {235, 235, 235, 230}, z = 5})
        d:text(('%s since set: %d'):format(s[0] == 2 and 'would have lost you' or 'lost you', s[5]), x, y + 44 * k,
            {size = 15 * k, colour = m[2], z = 5})
    end
    local extra = {}
    if s[6] == 1 then extra[#extra + 1] = 'HIDE / FORGET RUNNING' end
    if s[7] == 1 then extra[#extra + 1] = 'PATROL TRACE' end
    if #extra > 0 then
        d:text(table.concat(extra, '   '), x, y + (s[0] ~= 0 and 64 or 24) * k, {size = 15 * k, colour = {120, 200, 255, 230}, z = 5})
    end
end)
