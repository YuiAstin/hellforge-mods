-- Ascalon HUD (mods/yui/ascalon_hud): the side of Ascalon's kit that the engine's Lua has to do.
--   * While she is hidden her body is not drawn: every visible mesh of her Helldiver's units (the avatar and each armour
--     part are units of their own sharing one root) is switched off with Unit.set_mesh_visibility, and again four times
--     a second, since the game switches meshes back on by itself; with hide_gear her weapons and backpack too. The way
--     is the CAMO-EX01 Optical Cloak Pack's "compatibility" cloak (hide, remember, re-hide), found with the Cloak Probe.
--   * A line at the bottom middle says what she is: hidden, revealed (how many enemies sense her, then the seconds
--     left), or not hidden because of a backpack or support weapon.
-- Ascalon is a HellForge code mod; this reads ascalon.dll's state (ascalon_hud_state, layout version 1) through LuaJIT's
-- ffi every frame. Requires Bingus Shared Loader and HD2Runtime; uses HD2Runtime by SkyeShade
-- (https://github.com/SkyeShade/HD2Runtime) through its public API (hd2.ui.overlay).
local OWNER = 'mods/yui/ascalon_hud'

local sink, sink_tried
local function log(message)
    pcall(print, message)
    if not sink_tried then
        sink_tried = true
        local ok, result = pcall(function() return CowboyBingusModLoader.open_log('AscalonHUD.log') end)
        if ok then sink = result end
    end
    if sink then
        if not pcall(function() sink:write(message .. '\n'); if sink.flush then sink:flush() end end) then sink = nil end
    end
end

local ok, hd2 = pcall(require, 'mods/skyeshade/hd2runtime')
if not ok or type(hd2) ~= 'table' or type(hd2.ui) ~= 'table' or type(hd2.ui.overlay) ~= 'function' then
    log('[Ascalon HUD] HD2Runtime with hd2.ui.overlay is not available: she stays drawn, no status line (' .. tostring(hd2) .. ')')
    return
end

local ffi = require('ffi')
pcall(ffi.cdef, [[
typedef struct aschud1_state { uint32_t version, in_mission, state; float reveal_left; uint32_t cloak_visual, hide_gear; uint32_t units[5]; uint32_t sensing; } aschud1_state;
void *aschud1_GetModuleHandleA(const char *name) __asm__("GetModuleHandleA");
void *aschud1_GetProcAddress(void *module, const char *name) __asm__("GetProcAddress");
]])
local k32 = ffi.load('kernel32')
local S = rawget(_G, 'stingray') or _G
local function lib(name) local t = rawget(_G, name) or (type(S) == 'table' and S[name]) or nil; return type(t) == 'table' and t or nil end
local function pc(f, ...) if type(f) ~= 'function' then return false, 'missing' end return pcall(f, ...) end

local INK, PANEL, TEXT, TEAL, ORANGE, MUTED = {8, 9, 10}, {12, 14, 16}, {240, 243, 245}, {120, 220, 210}, {255, 138, 36}, {150, 156, 160}
local function rgba(c, a) return {c[1], c[2], c[3], math.floor(255 * (a or 1) + 0.5)} end

local state, fn, find_wait = nil, nil, 0
local function find(dt)
    if fn ~= nil then state = fn() return state end
    find_wait = find_wait - dt
    if find_wait > 0 then return nil end
    find_wait = 2
    local module = k32.aschud1_GetModuleHandleA('ascalon.dll')
    if module == nil then return nil end
    local proc = k32.aschud1_GetProcAddress(module, 'ascalon_hud_state')
    if proc == nil then return nil end
    fn = ffi.cast('aschud1_state *(*)(void)', proc)
    state = fn()
    if state == nil or state.version < 1 then fn = nil return nil end
    log('[Ascalon HUD] connected to Ascalon')
    return state
end

---------------------------------------------------------------------------------------------- her units --
local AVATAR = '4d1c334d294dfa97'
local TYPE_LIMIT = 64
local RI = (function()
    local okx, off = pcall(function() return rawget(_G, 'stingray').Script.index_offset() end)
    return okx and type(off) == 'number' and off or 1
end)()
local function xyz(v)
    if v == nil then return nil end
    local V = lib('Vector3')
    local okv, x, y, z = pcall(function() return V.to_elements(v) end)
    if okv and x then return x, y, z end
    local ok2, a, b, c = pcall(function() return v.x, v.y, v.z end)
    if ok2 then return a, b, c end
end
-- a unit's root, asked only of units with a scene graph (a root read on one without crashed the game once)
local function root(U, u)
    local okn, n = pcall(U.num_scene_graph_items, u)
    if not okn or type(n) ~= 'number' or n < 1 or n > 10000 then return nil end
    local okp, v = pcall(U.world_position, u, RI)
    if not okp then return nil end
    return xyz(v)
end
-- the scene unit id a unit handle carries (the Optical Cloak Pack's finding: the handle is the id times four)
local function unit_id(u)
    local okc, h = pcall(ffi.cast, 'uintptr_t', u)
    local v = okc and tonumber(h) or nil
    return v and math.floor(v / 4) or nil
end
-- every unit of a type with at most TYPE_LIMIT copies, in every world; also the ones whose id is asked for
local function scan(want)
    local App, W, U = lib('Application'), lib('World'), lib('Unit')
    if not (App and W and U) then return nil end
    local worlds = {}
    local okws, ws = pc(App.worlds)
    if okws and type(ws) == 'table' and #ws > 0 then worlds = ws else
        local okw, w = pc(App.main_world)
        if okw and w then worlds = {w} end
    end
    local units, res, count, byid, avatars = {}, {}, {}, {}, {}
    for _, world in ipairs(worlds) do
        local oku, all = pcall(W.units, world)
        if oku and type(all) == 'table' then
            for _, u in ipairs(all) do
                local okr, r = pcall(U.resource_name, u)
                r = okr and tostring(r):lower() or '?'
                units[#units + 1] = u; res[#units] = r
                count[r] = (count[r] or 0) + 1
                local id = unit_id(u)
                if id and want[id] then byid[id] = u end
                if r:find(AVATAR, 1, true) then avatars[#avatars + 1] = u end
            end
        end
    end
    local rare = {}
    for i, u in ipairs(units) do if count[res[i]] <= TYPE_LIMIT and res[i] ~= '?' then rare[#rare + 1] = u end end
    return rare, byid, avatars, #units
end
local function sharing(U, rare, x, y, z, out, seen)
    local n = 0
    for _, u in ipairs(rare) do
        local key = tostring(u)
        if not seen[key] then
            local ux, uy, uz = root(U, u)
            if ux and (ux - x) ^ 2 + (uy - y) ^ 2 + (uz - z) ^ 2 < 0.0004 then seen[key] = true; out[#out + 1] = u; n = n + 1 end
        end
    end
    return n
end

local cl = {on = false, units = nil, hidden = {}, keys = {}, acc = 0, scan_in = 0, ids = '', told = 0}
-- her units now: the avatar's root and everything sharing it, then (hide_gear) each of her items' roots likewise
local function collect(s)
    local U = lib('Unit')
    local want, ids = {}, {}
    for k = 0, 4 do local id = tonumber(s.units[k]); ids[#ids + 1] = id; if id ~= 0 then want[id] = true end end
    local rare, byid, avatars, total = scan(want)
    if not rare then return nil end
    local me = byid[tonumber(s.units[0])]
    local how = 'its unit id'
    if not me and #avatars == 1 then me = avatars[1]; how = 'its type (the only one)' end
    if not me then
        if cl.told < 4 then cl.told = cl.told + 1; log(('[Ascalon HUD] her Helldiver\'s unit was not found (id %d, %d units of the avatar type, %d units in all)'):format(tonumber(s.units[0]), #avatars, total)) end
        return nil
    end
    local mx, my, mz = root(U, me)
    if not mx then return nil end
    local out, seen = {}, {}
    local body = sharing(U, rare, mx, my, mz, out, seen)
    local gear = 0
    if s.hide_gear ~= 0 then
        for k = 1, 4 do
            local g = byid[tonumber(s.units[k])]
            if g then
                local gx, gy, gz = root(U, g)
                if gx and (gx - mx) ^ 2 + (gy - my) ^ 2 + (gz - mz) ^ 2 < 16 then
                    if not seen[tostring(g)] then seen[tostring(g)] = true; out[#out + 1] = g; gear = gear + 1 end
                    gear = gear + sharing(U, rare, gx, gy, gz, out, seen)
                end
            end
        end
    end
    local sig = table.concat(ids, ',') .. ':' .. body .. ':' .. gear
    if sig ~= cl.ids and cl.told < 12 then
        cl.told = cl.told + 1
        log(('[Ascalon HUD] her units: the Helldiver by %s, %d units at its root, %d of her gear (%d units in all)'):format(how, body, gear, total))
    end
    cl.ids = sig
    return out
end
local function alive(U, u)
    local oka, a = pc(U.alive, u)
    return oka and a == true
end
local function hide_units(units)
    local U, M = lib('Unit'), lib('Mesh')
    local hid = 0
    for _, u in ipairs(units) do
        if alive(U, u) then
            local okn, n = pc(U.num_meshes, u)
            for i = 1, (okn and type(n) == 'number' and n <= 512) and n or 0 do
                local okm, mesh = pc(U.mesh, u, i)
                local okv, vis = false, nil
                if okm then okv, vis = pc(M.visibility, mesh) end
                if okm and okv and vis == true and pc(U.set_mesh_visibility, u, i, false) then
                    local key = tostring(u) .. ':' .. i
                    if not cl.keys[key] then cl.keys[key] = true; cl.hidden[#cl.hidden + 1] = {unit = u, mesh = i} end
                    hid = hid + 1
                end
            end
        end
    end
    return hid
end
local function show_all()
    local U = lib('Unit')
    local n = 0
    for _, h in ipairs(cl.hidden) do if alive(U, h.unit) and pc(U.set_mesh_visibility, h.unit, h.mesh, true) then n = n + 1 end end
    cl.hidden, cl.keys, cl.units = {}, {}, nil
    return n
end
local can_alive = nil
local function cloak_frame(s, dt)
    local want = s ~= nil and s.in_mission ~= 0 and s.cloak_visual ~= 0 and (s.state == 0 or s.state == 3)
    if can_alive == nil then
        local U = lib('Unit')
        can_alive = U ~= nil and type(U.alive) == 'function'
        if not can_alive then log('[Ascalon HUD] Unit.alive is missing: she stays drawn (hiding a unit that is gone is not safe)') end
    end
    if not can_alive then return end
    if not want then
        if cl.on then
            cl.on = false
            local n = show_all()
            if cl.told < 12 then log(('[Ascalon HUD] shown again: %d meshes'):format(n)) end
        end
        return
    end
    cl.acc = cl.acc + dt
    cl.scan_in = cl.scan_in - dt
    if not cl.on then cl.on = true; cl.acc = 1; cl.scan_in = 0 end
    if cl.acc < 0.25 then return end
    cl.acc = 0
    pcall(function()
        if cl.scan_in <= 0 or not cl.units then
            cl.scan_in = 2
            local units = collect(s)
            if units then cl.units = units end
        end
        if cl.units then hide_units(cl.units) end
    end)
end

---------------------------------------------------------------------------------------------- the status line --
local LINES = {
    [0] = {'HIDDEN', TEAL},
    [2] = {'REVEALED', ORANGE},
    [3] = {'NOT HIDDEN: BACKPACK OR SUPPORT WEAPON', MUTED},
}
local overlay = hd2.ui.overlay({id = 'ascalon', owner = OWNER})
overlay:draw(function(d, dt)
    dt = tonumber(dt) or 0
    local s = find(dt)
    cloak_frame(s, dt)
    if s == nil or s.in_mission == 0 then return end
    local line = LINES[tonumber(s.state)]
    if not line then return end
    local sc = d.scale
    local text = line[1]
    local left = tonumber(s.reveal_left)
    local n = tonumber(s.sensing)
    if s.state == 2 and n > 0 then text = ('%s: %d %s'):format(text, n, n == 1 and 'ENEMY SENSES HER' or 'ENEMIES SENSE HER')
    elseif left > 0 then text = ('%s  %.1fs'):format(text, left) end
    local size = 16 * sc
    local cx, y = d.width / 2, d.height - 132 * sc
    local w, h = (#text * 10 + 36) * sc, 26 * sc
    d:rect(cx - w / 2, y, w, h, rgba(PANEL, 0.6), 0)
    d:rect(cx - w / 2, y + h - 2 * sc, w, 2 * sc, rgba(line[2], 0.9), 1)
    d:text(text, cx + math.max(1, sc), y + 4 * sc + math.max(1, sc), {size = size, font = 'title', colour = rgba(INK, 0.7), z = 4, align = 'center'})
    d:text(text, cx, y + 4 * sc, {size = size, font = 'title', colour = rgba(line[2], 1), z = 5, align = 'center'})
end)

log('[Ascalon HUD] ready (waiting for Ascalon)')
