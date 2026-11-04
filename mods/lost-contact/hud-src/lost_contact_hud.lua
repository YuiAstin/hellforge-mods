-- Lost Contact HUD (mods/yui/lost_contact_hud): which mode break contact and the patrols are in, and their live
-- counts. Reads lost_contact.dll's export lost_contact_hud (layout version 1) every frame through LuaJIT's ffi. Uses
-- HD2Runtime by SkyeShade (https://github.com/SkyeShade/HD2Runtime) through its public API (hd2.ui.overlay).
local OWNER = 'mods/yui/lost_contact_hud'

local ok, hd2 = pcall(require, 'mods/skyeshade/hd2runtime')
if not ok or type(hd2) ~= 'table' or type(hd2.ui) ~= 'table' or type(hd2.ui.overlay) ~= 'function' then return end

local ffi = require('ffi')
pcall(ffi.cdef, [[
void *lchud1_GetModuleHandleA(const char *name) __asm__("GetModuleHandleA");
void *lchud1_GetProcAddress(void *module, const char *name) __asm__("GetProcAddress");
typedef int (*lchud1_fn)(unsigned version, int *out, int n);
]])
local k32 = ffi.load('kernel32')
local out = ffi.new('int[13]')
local fn, next_look = nil, 0

local function read(t)
    if fn == nil then
        if t < next_look then return nil end
        next_look = t + 2
        local module = k32.lchud1_GetModuleHandleA('lost_contact.dll')
        if module == nil then return nil end
        local proc = k32.lchud1_GetProcAddress(module, 'lost_contact_hud')
        if proc == nil then return nil end
        fn = ffi.cast('lchud1_fn', proc)
    end
    local okc, n = pcall(fn, 1, out, 13)
    if not okc or n ~= 13 then return nil end
    return out
end

local MODES = {
    [0] = {'OFF', {170, 170, 170, 200}},
    [1] = {'ON', {90, 230, 120, 255}},
    [2] = {'OBSERVE', {255, 200, 60, 255}},
}
local WHITE = {235, 235, 235, 230}

local t = 0
local overlay = hd2.ui.overlay({id = 'modes', owner = OWNER})
overlay:draw(function(d, dt)
    t = t + (tonumber(dt) or 0)
    local s = read(t)
    if not s or s[12] ~= 1 then return end
    local k = d.scale or 1
    local x, y = 24 * k, d.height * 0.32
    local mb, mp = MODES[s[0]] or MODES[0], MODES[s[7]] or MODES[0]
    d:text('BREAK CONTACT: ' .. mb[1], x, y, {size = 17 * k, colour = mb[2], z = 5})
    y = y + 22 * k
    if s[0] ~= 0 then
        d:text(('near %d   after you %d   sensing %d   unsensed %d'):format(s[1], s[2], s[3], s[4]), x, y, {size = 14 * k, colour = WHITE, z = 5})
        y = y + 18 * k
        d:text(('%s: %d   back unsensed: %d'):format(s[0] == 2 and 'would lose you' or 'lost you', s[5], s[6]), x, y, {size = 14 * k, colour = mb[2], z = 5})
        y = y + 24 * k
    end
    d:text('PATROLS: ' .. mp[1], x, y, {size = 17 * k, colour = mp[2], z = 5})
    y = y + 22 * k
    if s[7] ~= 0 then
        d:text(('outposts %d   aimed at you %d   %s %d   re-aimed %d'):format(s[8], s[9], s[7] == 2 and 'would route' or 'routed', s[10], s[11]), x, y,
            {size = 14 * k, colour = WHITE, z = 5})
    end
end)
