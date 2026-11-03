-- Gun Probe (mods/yui/gun_probe): research for Exusiai's summoned guns. Can an HD2Runtime addon spawn one of the game's
-- own gun models as a visual only (no gameplay), keep it hovering beside the view and turn it, through the engine's own
-- Lua calls (World.spawn_unit, Unit.set_local_position / rotation, the view camera as Treasure Radar finds it)?
--
--   F6        spawn two guns (Ctrl+F6: the next loaded gun model) beside the view for 10 s (left and right of the camera, a little ahead), then remove them;
--             while they are out each "fires" the Guard Dog's firing sound (hd2.sounds, HD2Runtime's sound API, marked
--             offline-tested only) on its own model every 0.5 s, the two a quarter-second apart
--   Shift+F6  log which engine calls this addon can reach, which gun models are loaded right now, and what HD2Runtime's
--             sound API says about the Guard Dog's firing sound
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
local NAMES = {'World', 'Unit', 'Camera', 'Vector3', 'Quaternion', 'Application', 'Actor'}
-- 0.1.2: the engine's libraries sit in the global `stingray` table (as HD2Runtime's own modules reach them), not as bare
-- globals: 0.1.1 found all six nil
local S = rawget(_G, 'stingray')
for _, n in ipairs(NAMES) do E[n] = rawget(_G, n) or (type(S) == 'table' and S[n]) or nil end
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

-- the gun models. 0.1.4: drone_mg is the Guard Dog's body alone (0.1.3's test: "an empty dog without the gun"), so the
-- support weapons come first; Ctrl+F6 steps to the next loaded one
local MODELS = {
    'content/fac_helldivers/equipment/support_weapons/machinegun/machinegun',
    'content/fac_helldivers/equipment/support_weapons/heavy_mg/heavy_mg',
    'content/fac_helldivers/equipment/support_weapons/expendable_machinegun/expendable_machinegun',
    'content/fac_helldivers/equipment/backpacks/drone_weapons/drone_assault_rifle/drone_assault_rifle',
    'content/fac_helldivers/equipment/backpacks/drone_weapons/drone_laser_rifle/drone_laser_rifle',
    'content/fac_helldivers/equipment/backpacks/drone_mg/drone_mg',
}
local pick = 0   -- 0: the first loaded; else the index Ctrl+F6 stepped to
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

-- the Guard Dog's firing sound (HD2Runtime's weapon firing-sound catalogue): its bank asked for once; a loop entry is
-- stopped after a short burst, a shot entry plays as it is
local SOUND = 'backpack/guard_dog'
local SND = {asked = false, kind = nil, played = 0, failed = 0}
local function sound_prepare()
    if SND.asked then return end
    SND.asked = true
    local snd = hd2.sounds
    if type(snd) ~= 'table' or type(snd.play) ~= 'function' then log('[Gun Probe] sound: hd2.sounds is not available') return end
    local okd, info = pcall(snd.describe, SOUND)
    SND.kind = okd and type(info) == 'table' and (info.kind or info.type) or nil
    log('[Gun Probe] sound: ' .. SOUND .. ' is ' .. (okd and type(info) == 'table' and ('a ' .. tostring(SND.kind) .. ' entry') or ('not described: ' .. tostring(info))))
    if type(snd.asset) == 'function' and type(hd2.require_assets) == 'function' then
        local oka, target = pcall(snd.asset, SOUND)
        if oka and target then
            local okr, why = pcall(hd2.require_assets, {id = 'gun-probe-guard-dog', targets = {target}})
            log('[Gun Probe] sound: its bank requested: ' .. tostring(okr) .. ' ' .. tostring(why))
        else log('[Gun Probe] sound: no asset target: ' .. tostring(target)) end
    end
end
local pending_stops = {}
local function sound_at(unit, pos)
    local snd = hd2.sounds
    if type(snd) ~= 'table' or type(snd.play) ~= 'function' then return end
    local okp, h, why = pcall(snd.play, SOUND, unit and {unit = unit} or {position = pos})
    if okp and h then
        SND.played = SND.played + 1
        if SND.played <= 3 then log('[Gun Probe] sound: played on ' .. (unit and 'the gun model' or 'a position')) end
        if SND.kind == 'loop' and (type(h) == 'table' or type(h) == 'userdata') then pending_stops[#pending_stops + 1] = {h = h, t = 0.12} end
    else
        SND.failed = SND.failed + 1
        if SND.failed <= 3 then log('[Gun Probe] sound: not played: ' .. tostring(okp and why or h)) end
    end
end
local function sound_step(dt)
    for i = #pending_stops, 1, -1 do
        local s = pending_stops[i]
        s.t = s.t - dt
        if s.t <= 0 then
            if SND.kind == 'loop' then pcall(function() s.h:stop() end) end
            table.remove(pending_stops, i)
        end
    end
end

-- 0.1.3: Vector3 is a callable table (stingray.Vector3(x, y, z)), not a library function: 0.1.2's call('Vector3', ...)
-- could never reach it, so the guns spawned with no position and never moved ("moved 0 frames")
local vec_told = false
local function vec(x, y, z)
    local V = E.Vector3
    local okv, v = pcall(V, x, y, z)
    if okv and v ~= nil then return v end
    if type(V) == 'table' and type(rawget(V, 'new')) == 'function' then
        local okn, w = pcall(V.new, x, y, z)
        if okn and w ~= nil then return w end
    end
    if not vec_told then vec_told = true; log('[Gun Probe] Vector3(x, y, z) failed: ' .. tostring(v)) end
    return nil
end
-- the probe: two guns, 10 s
local G = {units = {}, until_t = 0, t = 0, moved = 0, model = nil, beat = {0, 0.25}}
local OFFSETS = {{-0.9, 0.35, 2.2}, {0.9, 0.35, 2.2}}   -- right, up, forward (m) from the camera
local function place(c, o)
    local x = c.p[1] + c.r[1] * o[1] + c.u[1] * o[2] + c.f[1] * o[3]
    local y = c.p[2] + c.r[2] * o[1] + c.u[2] * o[2] + c.f[2] * o[3]
    local z = c.p[3] + c.r[3] * o[1] + c.u[3] * o[2] + c.f[3] * o[3]
    return vec(x, y, z)
end
-- 0.1.4's test: the machine gun spawned as a dropped weapon, fell to the ground and only then followed the view (its
-- physics actor moves it until it rests). 0.1.5 takes its physics away: every actor kinematic with collision off, else
-- the actors destroyed, else Unit.disable_physics; which one worked is logged once
local still_told = false
local function still(u)
    local how = {}
    local okn, n = call('Unit.num_actors', u)
    if okn and type(n) == 'number' and n > 0 then
        local kin, gone = 0, 0
        for i = n, 1, -1 do
            local oka, a = call('Unit.actor', u, i)
            if oka and a then
                local okk = call('Actor.set_kinematic', a, true)
                if okk then kin = kin + 1; call('Actor.set_collision_enabled', a, false); call('Actor.set_scene_query_enabled', a, false)
                elseif call('Unit.destroy_actor', u, i) then gone = gone + 1 end
            end
        end
        how[#how + 1] = ('%d actors: %d kinematic, %d destroyed'):format(n, kin, gone)
    else how[#how + 1] = 'no actors listed' end
    if call('Unit.disable_physics', u) then how[#how + 1] = 'disable_physics' end
    if not still_told then still_told = true; log('[Gun Probe]   physics: ' .. table.concat(how, ', ')) end
end
local function clear(w)
    for i, u in ipairs(G.units) do
        local okd = call('World.destroy_unit', w, u)
        log('[Gun Probe] gun ' .. i .. ' removed: ' .. tostring(okd))
    end
    G.units = {}
end
local function spawn(step_model)
    local w = world()
    if not w then log('[Gun Probe] F6: no main world') return end
    local c = camera(w)
    if not c then log('[Gun Probe] F6: no view camera') return end
    clear(w)
    G.model = nil
    if step_model then
        for k = 1, #MODELS do
            local i = (pick + k - 1) % #MODELS + 1
            if loaded(MODELS[i]) then pick = i; break end
        end
    end
    if pick > 0 and loaded(MODELS[pick]) then G.model = MODELS[pick]
    else for i, path in ipairs(MODELS) do if loaded(path) then G.model = path; pick = i; break end end end
    if not G.model then log('[Gun Probe] F6: none of the gun models is loaded (bring a Guard Dog AR-23 or a machine gun)') return end
    for i, o in ipairs(OFFSETS) do
        local pos = place(c, o)
        if not pos then log('[Gun Probe] F6: no position for gun ' .. i .. ': not spawned') return end
        local oks, unit = call('World.spawn_unit', w, G.model, pos, c.rot)
        log(('[Gun Probe] F6: gun %d %s (%s)'):format(i, oks and unit and 'spawned' or 'NOT spawned', G.model))
        if oks and unit then
            local okq, err = pcall(still, unit)
            if not okq then log('[Gun Probe]   physics: ' .. tostring(err)) end
            G.units[#G.units + 1] = unit
            local okx, at = call('Unit.world_position', unit, 1)
            local ax, ay, az = xyz(okx and at or nil)
            log(('[Gun Probe]   at %s (camera at %.1f %.1f %.1f)'):format(ax and ('%.1f %.1f %.1f'):format(ax, ay, az) or 'unknown', c.p[1], c.p[2], c.p[3]))
        end
    end
    G.until_t, G.moved, G.why = G.t + 10, 0, nil
    still_told = false
    G.beat = {0, 0.25}
    local okp, err = pcall(sound_prepare)
    if not okp then log('[Gun Probe] sound: prepare failed: ' .. tostring(err)) end
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
    if all then G.moved = G.moved + 1
    elseif not G.why then G.why = true; log('[Gun Probe] a frame not moved (position or rotation refused)') end
    for i, u in ipairs(G.units) do
        G.beat[i] = (G.beat[i] or 0) - dt
        if G.beat[i] <= 0 then
            G.beat[i] = 0.5
            local okx, x, y, z = call('Unit.world_position', u, 1)
            local px, py, pz = xyz(okx and x or nil)
            sound_at(u, px and {px, py, pz} or nil)
        end
    end
    if G.moved == 1 then log('[Gun Probe] guns follow the view (first frame moved)') end
end
local function report()
    log('[Gun Probe] stingray: ' .. type(S))
    for _, n in ipairs(NAMES) do log('[Gun Probe] ' .. n .. ': ' .. type(E[n])) end
    for _, p in ipairs({'Application.main_world', 'Application.can_get', 'World.units', 'World.spawn_unit', 'World.destroy_unit', 'Unit.alive',
        'Unit.num_cameras', 'Unit.camera', 'Unit.set_local_position', 'Unit.set_local_rotation', 'Unit.num_actors', 'Unit.actor', 'Actor.set_kinematic', 'Actor.set_collision_enabled', 'Unit.destroy_actor', 'Unit.disable_physics', 'Camera.world_position',
        'Camera.world_rotation', 'Quaternion.forward', 'Quaternion.right', 'Quaternion.up', 'Vector3', 'Vector3.to_elements'}) do
        log('[Gun Probe]   ' .. p .. ': ' .. (fn(p) and 'yes' or 'no'))
    end
    for _, path in ipairs(MODELS) do log('[Gun Probe]   loaded ' .. tostring(loaded(path)) .. ': ' .. path) end
    local snd = hd2.sounds
    log('[Gun Probe]   hd2.sounds: ' .. type(snd) .. (type(snd) == 'table' and (', play ' .. type(snd.play) .. ', available ' .. type(snd.available)) or ''))
    if type(snd) == 'table' and type(snd.available) == 'function' then
        local oka, av = pcall(snd.available, SOUND)
        log('[Gun Probe]   ' .. SOUND .. ' available now: ' .. tostring(oka and av or av))
    end
end

local prev = false
local overlay = hd2.ui.overlay({id = 'probe', owner = OWNER})
overlay:draw(function(d, dt)
    dt = tonumber(dt) or 0
    local now = down(0x75)                                   -- F6
    if now and not prev then
        if down(0x10) then local okr, err = pcall(report); if not okr then log('[Gun Probe] report failed: ' .. tostring(err)) end
        else local oks, err = pcall(spawn, down(0x11)); if not oks then log('[Gun Probe] spawn failed: ' .. tostring(err)) end end
    end
    prev = now
    pcall(sound_step, dt)
    local okt, err = pcall(step, dt)
    if not okt then log('[Gun Probe] step failed: ' .. tostring(err)); G.units = {} end
    if #G.units > 0 then
        d:text(('GUN PROBE: %d guns, %.0f s'):format(#G.units, math.max(0, G.until_t - G.t)), d.width / 2, 120 * (d.scale or 1),
            {size = 20 * (d.scale or 1), colour = {255, 232, 0, 255}, z = 5, align = 'center'})
    end
end)

log('[Gun Probe] ready: F6 spawns two gun models beside the view for 10 s; Shift+F6 logs what the engine lets this addon reach')
