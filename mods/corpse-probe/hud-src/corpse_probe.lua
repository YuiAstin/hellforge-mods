-- Corpse Probe (mods/yui/corpse_probe): the Lua half of the HellForge probe yui.corpse-probe. A probe, not a release.
-- The DLL (corpse_probe.dll) sees the enemies die and measures the frame rate; this finds each dead enemy's unit in the
-- game's Lua and switches its physics off (Unit.disable_physics), and on again when the freeze is turned off. What it
-- does is written to the DLL's log (mod-yui.corpse-probe.log).
-- Requires Bingus Shared Loader and HD2Runtime (by SkyeShade, https://github.com/SkyeShade/HD2Runtime; its overlay
-- gives the frame callback). A unit's Lua value is its unit id * 4 + 1 (the Time Stop Probe's finding).
local OWNER = 'mods/yui/corpse_probe'

local ok, hd2 = pcall(require, 'mods/skyeshade/hd2runtime')
if not ok or type(hd2) ~= 'table' or type(hd2.ui) ~= 'table' or type(hd2.ui.overlay) ~= 'function' then
    pcall(print, '[Corpse Probe] HD2Runtime with hd2.ui.overlay is not available: nothing is frozen')
    return
end

local ffi = require('ffi')
pcall(ffi.cdef, [[
typedef struct cpp1_dead { uint32_t serial, unit; uint64_t res; float pos[3]; uint32_t pad; } cpp1_dead;
typedef struct cpp1_state { uint32_t version, enabled, in_mission, restore; uint32_t last, ready; float batch, pad; cpp1_dead dead[256]; } cpp1_state;
typedef int (*cpp1_log)(unsigned int version, const char *text);
typedef int (*cpp1_report)(unsigned int version, unsigned int frozen, unsigned int gone, unsigned int failed, unsigned int restored, unsigned int none, float scan_ms);
void *cpp1_GetModuleHandleA(const char *name) __asm__("GetModuleHandleA");
void *cpp1_GetProcAddress(void *module, const char *name) __asm__("GetProcAddress");
]])
local k32 = ffi.load('kernel32')

local state, logfn, reportfn, pending = nil, nil, nil, {}
local function log(m)
    m = tostring(m)
    pcall(print, '[Corpse Probe] ' .. m)
    if logfn then pcall(logfn, 1, m) elseif #pending < 200 then pending[#pending + 1] = m end
end
local find_wait = 0
local function connect(dt)
    if state ~= nil then return state end
    find_wait = find_wait - dt
    if find_wait > 0 then return nil end
    find_wait = 2
    local module = k32.cpp1_GetModuleHandleA('corpse_probe.dll')
    if module == nil then return nil end
    local p1, p2, p3 = k32.cpp1_GetProcAddress(module, 'corpse_probe_state'), k32.cpp1_GetProcAddress(module, 'corpse_probe_log'),
        k32.cpp1_GetProcAddress(module, 'corpse_probe_report')
    if p1 == nil or p2 == nil or p3 == nil then return nil end
    local s = ffi.cast('cpp1_state *(*)(void)', p1)()
    if s == nil or s.version ~= 1 then return nil end
    logfn = ffi.cast('cpp1_log', p2)
    reportfn = ffi.cast('cpp1_report', p3)
    state = s
    for _, m in ipairs(pending) do pcall(logfn, 1, m) end
    pending = {}
    log('connected to the DLL')
    return state
end

local function S_() return rawget(_G, 'stingray') end
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
local function clock(s)
    local okC, t = pcall(s.Application.time_since_launch)
    return okC and type(t) == 'number' and t or 0
end

local was_in, was_on = false, false
local done = 0                -- the last death looked at
local wait = 0                -- until the next look-up
local frozen = {}             -- the units whose physics this switched off
local n_frozen, n_gone, n_failed, n_restored, scan_ms = 0, 0, 0, 0, 0
local told, flash, flash_text = 0, 0, ''
local function report() if reportfn then pcall(reportfn, 1, n_frozen, n_gone, n_failed, n_restored, 0, scan_ms) end end

local function freeze(st)
    local s = S_()
    if type(s) ~= 'table' or not (s.Unit and s.World and s.Application) then return end
    if type(s.Unit.disable_physics) ~= 'function' then
        if told == 0 then told = 1; log('Unit.disable_physics is not in this game\'s Lua: nothing can be frozen this way') end
        done = st.ready
        return
    end
    local first = math.max(done + 1, st.ready - 254)
    local wanted, n = {}, 0
    for serial = first, st.ready do
        local c = st.dead[serial % 256]
        if c.serial == serial and c.unit ~= 0 then wanted[c.unit * 4 + 1] = {serial = serial, unit = c.unit, res = c.res}; n = n + 1 end
    end
    done = st.ready
    if n == 0 then return end
    local t0 = clock(s)
    local okW, w = pcall(s.Application.main_world)
    if not okW or not w then return end
    local okU, all = pcall(s.World.units, w)
    if not okU or type(all) ~= 'table' then log('World.units failed: ' .. tostring(all)) return end
    local hit, left = {}, n
    for _, u in ipairs(all) do
        local p = uptr(u)
        local rec = p and wanted[p]
        if rec then
            wanted[p] = nil
            rec.u = u
            hit[#hit + 1] = rec
            left = left - 1
            if left == 0 then break end
        end
    end
    n_gone = n_gone + left
    local froze, dead = 0, 0
    for _, rec in ipairs(hit) do
        local okA, alive = pcall(s.Unit.alive, rec.u)
        if okA and alive == true then
            local okD, err = pcall(s.Unit.disable_physics, rec.u)
            if okD then
                frozen[#frozen + 1] = rec.u
                n_frozen = n_frozen + 1
                froze = froze + 1
            else
                n_failed = n_failed + 1
                if told < 4 then told = told + 1; log('disable_physics failed on ' .. tostring(rec.u) .. ': ' .. tostring(err)) end
            end
        else n_gone = n_gone + 1; dead = dead + 1 end
    end
    scan_ms = (clock(s) - t0) * 1000
    if told < 12 then
        told = told + 1
        log(('%d bodies due: %d units found among %d, %d frozen, %d already gone; the look-up took %.1f ms%s'):format(n, #hit, #all, froze, left + dead, scan_ms,
            hit[1] and ('; the first is ' .. tostring(hit[1].u)) or ''))
    end
    report()
end

local function restore(touch)
    local s = S_()
    local back, gone = 0, 0
    if touch and type(s) == 'table' and s.Unit and type(s.Unit.enable_physics) == 'function' then
        for _, u in ipairs(frozen) do
            local okA, alive = pcall(s.Unit.alive, u)
            if okA and alive == true then
                if pcall(s.Unit.enable_physics, u) then back = back + 1 end
            else gone = gone + 1 end
        end
        n_restored = n_restored + back
        log(('the freeze is off: physics given back to %d bodies (%d had gone)'):format(back, gone))
    end
    frozen = {}
    report()
end

local overlay = hd2.ui.overlay({id = 'probe', owner = OWNER})
local failed = false
overlay:draw(function(d, dt)
    if failed then return end
    dt = dt or 0
    local st = connect(dt)
    if not st then return end
    local okF, err = pcall(function()
        local inm, on = st.in_mission ~= 0, st.enabled ~= 0
        if inm ~= was_in then
            was_in = inm
            -- a new mission starts clean; a finished one's units are left alone
            frozen = {}
            done = st.last
            was_on = false
            n_frozen, n_gone, n_failed, n_restored, scan_ms, told = 0, 0, 0, 0, 0, 0
            report()
        end
        if not inm then return end
        if on ~= was_on then
            was_on = on
            flash, flash_text = 3, on and 'CORPSE FREEZE ON' or 'CORPSE FREEZE OFF'
            if on then done = st.ready; wait = 0
            elseif st.restore ~= 0 then restore(true) end
        end
        if not on then done = st.ready return end
        wait = wait - dt
        if st.ready > done and wait <= 0 then
            wait = st.batch
            freeze(st)
        end
    end)
    if not okF then failed = true; log('failed, off for this session: ' .. tostring(err)) return end
    flash = flash - dt
    if st.in_mission ~= 0 and (flash > 0 or st.enabled ~= 0) then
        local sc = d.scale or 1
        local text = flash > 0 and flash_text or ('CORPSE FREEZE ON  %d frozen'):format(n_frozen)
        local size = flash > 0 and 22 or 14
        d:text(text, d.width / 2 + 2, 96 * sc + 2, {size = size * sc, colour = {0, 0, 0, 200}, z = 4, align = 'center'})
        d:text(text, d.width / 2, 96 * sc, {size = size * sc, colour = {255, 255, 255, 255}, z = 5, align = 'center'})
    end
end)
pcall(print, '[Corpse Probe] ready (waiting for corpse_probe.dll)')
