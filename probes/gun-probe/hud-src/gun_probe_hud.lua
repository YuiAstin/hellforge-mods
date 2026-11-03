-- Gun Probe (mods/yui/gun_probe): research for Exusiai's summoned guns. Can an HD2Runtime addon spawn one of the game's
-- own gun models as a visual only (no gameplay), keep it hovering beside the view and turn it, through the engine's own
-- Lua calls (World.spawn_unit, Unit.set_local_position / rotation, the view camera as Treasure Radar finds it)?
--
--   F6        spawn two guns beside the view for 10 s (left and right of the camera, a little ahead), then remove them
--   Shift+F6  log which engine calls this addon can reach and which gun models are loaded right now
--
-- A gun model is spawned only when the engine says it is loaded (Application.can_get): bring a Guard Dog (AR-23) or a
-- machine gun so one is. Everything is logged to GunProbe.log next to the loader's log. Research only; turn it off when
-- done. Uses HD2Runtime by SkyeShade (https://github.com/SkyeShade/HD2Runtime) through its public API; the way to find
-- the view camera (a unit with Unit.num_cameras > 0, its Unit.camera(u, 1)) is from reading Chef Shared Loader by
-- Th3chef; no code of it is copied.
local OWNER = 'mods/yui/gun_probe'

local sink, sink_tried
local function log(message)
    pcall(print, message)
    if not sink_tried then
        sink_tried = true
        local ok, result = pcall(function() return CowboyBingusModLoader.open_log('GunProbe.log') end)
        if ok then sink = result end
    end
    if sink then
        if not pcall(function() sink:write(message .. '\n'); if sink.flush then sink:flush() end end) then sink = nil end
    end
end

local ok, hd2 = pcall(require, 'mods/skyeshade/hd2runtime')
if not ok or type(hd2) ~= 'table' or type(hd2.ui) ~= 'table' or type(hd2.ui.overlay) ~= 'function' then
    log('[Gun Probe] HD2Runtime with hd2.ui.overlay is not available (' .. tostring(hd2) .. ')')
    return
end

local ffi = require('ffi')
pcall(ffi.cdef, [[ short gunprobe1_GetAsyncKeyState(int vk) __asm__("GetAsyncKeyState"); ]])
local u32 = ffi.load('user32')
local bit = require('bit')
local function down(vk) return bit.band(u32.gunprobe1_GetAsyncKeyState(vk), 0x8000) ~= 0 end

-- the engine's globals, as this addon sees them
local E = {}
local NAMES = {'World', 'Unit', 'Camera', 'Vector3', 'Quaternion', 'Application'}
for _, n in ipairs(NAMES) do E[n] = rawget(_G, n) end
local function fn(path)
    local t, k = path:match('^(%w+)%.([%w_]+)$')
    local lib = t and E[t]
    local f = type(lib) == 'table' and rawget(lib, k) or (lib and lib[k])
    return type(f) == 'function' and f or nil
end
-- one engine call, guarded: ok, results... (the failure logged once per call)
local failed = {}
local function call(path, ...)
    local f = fn(path)
    if not f then
        if not failed[path] then failed[path] = true; log('[Gun Probe] ' .. path .. ': not reachable') end
        return false
    end
    local r = {pcall(f, ...)}
    if not r[1] and not failed[path] then failed[path] = true; log('[Gun Probe] ' .. path .. ' failed: ' .. tostring(r[2])) end
    return unpack(r)
end
local function xyz(v)
    if v == nil then return nil end
    local okv, x, y, z = call('Vector3.to_elements', v)
    if okv and type(x) == 'number' then return x, y, z end
    if type(v) == 'userdata' or type(v) == 'table' then
        local okf, a, b, c = pcall(function() return v.x, v.y, v.z end)
        if okf and type(a) == 'number' then return a, b, c end
    end
    return nil
end

-- the gun models: the Guard Dog's rifle first (a floating gun already), then the machine guns
local MODELS = {
    'content/fac_helldivers/equipment/backpacks/drone_weapons/drone_assault_rifle/drone_assault_rifle',
    'content/fac_helldivers/equipment/backpacks/drone_mg/drone_mg',
    'content/fac_helldivers/equipment/backpacks/drone_weapons/drone_laser_rifle/drone_laser_rifle',
    'content/fac_helldivers/equipment/support_weapons/machinegun/machinegun',
    'content/fac_helldivers/equipment/support_weapons/heavy_mg/heavy_mg',
    'content/fac_helldivers/equipment/support_weapons/expendable_machinegun/expendable_machinegun',
}
local function loaded(path)
    local okc, r = call('Application.can_get', 'unit', path)
    return okc and r == true
end

-- the view camera: a living unit with a camera, kept while it lives
local C = {unit = nil}
local function world()
    local okw, w = call('Application.main_world')
    return okw and w or nil
end
local function camera(w)
    if C.unit then
        local oka, alive = call('Unit.alive', C.unit)
        if not (oka and alive == true) then C.unit = nil end
    end
    if not C.unit then
        local oku, units = call('World.units', w)
        if not oku or type(units) ~= 'table' then return nil end
        for i = 1, #units do
            local u = units[i]
            local okn, n = call('Unit.num_cameras', u)
            if okn and type(n) == 'number' and n > 0 then C.unit = u; log('[Gun Probe] view camera found on unit ' .. i .. ' of ' .. #units); break end
        end
        if not C.unit then return nil end
    end
    local okc, cam = call('Unit.camera', C.unit, 1)
    if not okc or not cam then return nil end
    local okp, pos = call('Camera.world_position', cam)
    local okr, rot = call('Camera.world_rotation', cam)
    if not (okp and okr and pos and rot) then return nil end
    local px, py, pz = xyz(pos)
    local _, f = call('Quaternion.forward', rot)
    local _, r = call('Quaternion.right', rot)
    local _, u = call('Quaternion.up', rot)
    local fx, fy, fz = xyz(f)
    local rx, ry, rz = xyz(r)
    local ux, uy, uz = xyz(u)
    if not (px and fx and rx and ux) then return nil end
    return {p = {px, py, pz}, f = {fx, fy, fz}, r = {rx, ry, rz}, u = {ux, uy, uz}, rot = rot}
end

-- the probe: two guns, 10 s
local G = {units = {}, until_t = 0, t = 0, moved = 0, model = nil}
local OFFSETS = {{-0.9, 0.35, 2.2}, {0.9, 0.35, 2.2}}   -- right, up, forward (m) from the camera
local function place(c, o)
    local x = c.p[1] + c.r[1] * o[1] + c.u[1] * o[2] + c.f[1] * o[3]
    local y = c.p[2] + c.r[2] * o[1] + c.u[2] * o[2] + c.f[2] * o[3]
    local z = c.p[3] + c.r[3] * o[1] + c.u[3] * o[2] + c.f[3] * o[3]
    local okv, v = call('Vector3', x, y, z)
    return okv and v or nil
end
local function clear(w)
    for i, u in ipairs(G.units) do
        local okd = call('World.destroy_unit', w, u)
        log('[Gun Probe] gun ' .. i .. ' removed: ' .. tostring(okd))
    end
    G.units = {}
end
local function spawn()
    local w = world()
    if not w then log('[Gun Probe] F6: no main world') return end
    local c = camera(w)
    if not c then log('[Gun Probe] F6: no view camera') return end
    clear(w)
    G.model = nil
    for _, path in ipairs(MODELS) do if loaded(path) then G.model = path; break end end
    if not G.model then log('[Gun Probe] F6: none of the gun models is loaded (bring a Guard Dog AR-23 or a machine gun)') return end
    for i, o in ipairs(OFFSETS) do
        local pos = place(c, o)
        local oks, unit = call('World.spawn_unit', w, G.model, pos, c.rot)
        log(('[Gun Probe] F6: gun %d %s (%s)'):format(i, oks and unit and 'spawned' or 'NOT spawned', G.model))
        if oks and unit then G.units[#G.units + 1] = unit end
    end
    G.until_t, G.moved = G.t + 10, 0
end
local function step(dt)
    G.t = G.t + dt
    if #G.units == 0 then return end
    local w = world()
    if not w then G.units = {}; return end
    if G.t >= G.until_t then
        log(('[Gun Probe] 10 s up: moved %d frames'):format(G.moved))
        clear(w)
        return
    end
    local c = camera(w)
    if not c then return end
    local all = true
    for i, u in ipairs(G.units) do
        local pos = place(c, OFFSETS[i])
        local okp = pos and call('Unit.set_local_position', u, 1, pos)
        local okr = call('Unit.set_local_rotation', u, 1, c.rot)
        all = all and okp and okr
    end
    if all then G.moved = G.moved + 1 end
    if G.moved == 1 then log('[Gun Probe] guns follow the view (first frame moved)') end
end
local function report()
    for _, n in ipairs(NAMES) do log('[Gun Probe] ' .. n .. ': ' .. type(E[n])) end
    for _, p in ipairs({'Application.main_world', 'Application.can_get', 'World.units', 'World.spawn_unit', 'World.destroy_unit', 'Unit.alive',
        'Unit.num_cameras', 'Unit.camera', 'Unit.set_local_position', 'Unit.set_local_rotation', 'Camera.world_position',
        'Camera.world_rotation', 'Quaternion.forward', 'Quaternion.right', 'Quaternion.up', 'Vector3', 'Vector3.to_elements'}) do
        log('[Gun Probe]   ' .. p .. ': ' .. (fn(p) and 'yes' or 'no'))
    end
    for _, path in ipairs(MODELS) do log('[Gun Probe]   loaded ' .. tostring(loaded(path)) .. ': ' .. path) end
end

local prev = false
local overlay = hd2.ui.overlay({id = 'probe', owner = OWNER})
overlay:draw(function(d, dt)
    dt = tonumber(dt) or 0
    local now = down(0x75)                                   -- F6
    if now and not prev then
        if down(0x10) then local okr, err = pcall(report); if not okr then log('[Gun Probe] report failed: ' .. tostring(err)) end
        else local oks, err = pcall(spawn); if not oks then log('[Gun Probe] spawn failed: ' .. tostring(err)) end end
    end
    prev = now
    local okt, err = pcall(step, dt)
    if not okt then log('[Gun Probe] step failed: ' .. tostring(err)); G.units = {} end
    if #G.units > 0 then
        d:text(('GUN PROBE: %d guns, %.0f s'):format(#G.units, math.max(0, G.until_t - G.t)), d.width / 2, 120 * (d.scale or 1),
            {size = 20 * (d.scale or 1), colour = {255, 232, 0, 255}, z = 5, align = 'center'})
    end
end)

log('[Gun Probe] ready: F6 spawns two gun models beside the view for 10 s; Shift+F6 logs what the engine lets this addon reach')
