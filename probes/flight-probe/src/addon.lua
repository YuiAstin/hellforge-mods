-- Flight Probe (mods/yui/flight_probe): research for a flight mechanic on the player's Helldiver
-- (docs/research/flight.md). Gravity is one world constant (-9.82, in code); the game keeps a body in the air with its
-- airborne flag (game.dll+0x5A3D80) and moves it with SetVelocity (game.dll+0x4A7550), as JumpTest does. Flight here is
-- that pair every frame: the upward speed set to climb + 9.82 x dt so gravity's pull that frame is cancelled.
--
-- Keys (in a mission; none is F5, F8, F9, F10 (HD2Runtime's settings), Page Up / Page Down / End (Maxi Mag Probe, Escort Probe)):
--   F6          flight on / off. While on: Numpad 8 climbs, Numpad 2 descends, Up arrow flies along where she faces
--               (her body's facing); nothing held = hover. Off: the flag is cleared and she falls the game's way.
--   F7          hover test, 10 s: 5 s with the flag set every frame, then 5 s with only the speed set (does the game's
--               ground check / slow-speed rule drop her?). Height drift logged each 0.5 s.
--   Ctrl+F7     drop test: climbs to 5 m, then 10 m, then 20 m above where it started (one height a press), lets go,
--               and logs the speed at touchdown and the health lost (the fall-damage threshold).
--   Ctrl+F6     sends the animation event 'fall' (a pose for the air?) and logs the layers before and after.
-- While flying, every 0.5 s: height over the start, vertical speed, the animation layers (animation_get_state), the
-- backpack worn. Uses HD2Runtime by SkyeShade through its public API; the two game functions are called only for her
-- own avatar and only while their first bytes are this build's (JumpTest's checks). Research only.
local hd2 = require('mods/skyeshade/hd2runtime')
local mod = hd2.mod()
local S = rawget(_G, 'stingray')
local AVATAR = 'content/fac_helldivers/cha_avatar/avatar_helldiver'
local G = 9.82
local CLIMB, FLY = 6.0, 8.0     -- m/s
local function log(fmt, ...) mod:log(('Flight Probe: ' .. fmt):format(...)) end

local airborne, set_velocity, vbuf, why
do
    local okf, ffi = pcall(require, 'ffi')
    if not okf then why = 'no FFI' else
        pcall(ffi.cdef, 'void* GetModuleHandleA(const char*);')
        local base = ffi.C.GetModuleHandleA('game.dll')
        if base == nil then why = 'game.dll not found' else
            local at = ffi.cast('uintptr_t', base) + 0x5A3D80
            local want = '\x41\x56\x3b\x15\x98\xfe\xed\x02\x45\x0f\xb6\xf0\x4c\x8b\x1d\xc5'
            if ffi.string(ffi.cast('const char*', at), #want) == want then airborne = ffi.cast('void(*)(void*,uint32_t,uint8_t)', at)
            else why = 'the airborne flag is not this build\'s' end
            local sv = ffi.cast('uintptr_t', base) + 0x4A7550
            local want_sv = '\x48\x89\x5c\x24\x18\x57\x48\x83\xec\x20\x3b\x15\xc0\xc6\xfd\x02'
            if ffi.string(ffi.cast('const char*', sv), #want_sv) == want_sv then
                set_velocity = ffi.cast('void(*)(void*,uint32_t,const float*)', sv)
                vbuf = ffi.new('float[3]')
            else why = (why and why .. '; ' or '') .. 'SetVelocity is not this build\'s' end
        end
    end
end

-- 0.1.2: HD2Runtime 0.50.1's player:avatar() and player:position() read nil on HellForge 0.5 (it moved the entity
-- records: "the avatar has no health record"), so her avatar's id comes from the player list itself (HD2Runtime's
-- event_world, as its own avatar() does before that read) and her unit and place from the engine (the Helldiver unit:
-- the only one solo, else the one nearest where she last was)
local okW, W = pcall(require, 'hd2runtime/runtime/event_world')
local lastPos
local function unit_pos(unit)
    local ok, x, y, z = pcall(function() return S.Vector3.to_elements(S.Unit.world_position(unit, 1)) end)
    if ok and x then return { x = x, y = y, z = z } end
end
local function my_unit()
    if not S then return nil end
    local best, bestd, n = nil, nil, 0
    for _, world in ipairs(S.Application.worlds() or {}) do
        local ok, units = pcall(S.World.units_by_resource, world, AVATAR)
        if ok and type(units) == 'table' then
            for _, unit in ipairs(units) do
                local p = unit_pos(unit)
                if p then
                    n = n + 1
                    local d = lastPos and ((p.x - lastPos.x) ^ 2 + (p.y - lastPos.y) ^ 2 + (p.z - lastPos.z) ^ 2) or 0
                    if not bestd or d < bestd then best, bestd = unit, d end
                end
            end
        end
    end
    if best then lastPos = unit_pos(best) end
    return best, n
end
local get_state = S and S.Unit and S.Unit.animation_get_state
local function layers(unit)
    if not get_state or not unit then return 'n/a' end
    local ok, r = pcall(function() return { get_state(unit) } end)
    return ok and table.concat(r, ',') or 'n/a'
end
local function facing(unit)
    local ok, x, y = pcall(function()
        local q = S.Unit.world_rotation(unit, 1)
        return S.Vector3.to_elements(S.Quaternion.forward(q))
    end)
    if not ok or not x then return 0, 0 end
    local l = math.sqrt(x * x + y * y)
    if l < 0.01 then return 0, 0 end
    return x / l, y / l
end
local function me()
    if not okW then return nil, nil, "HD2Runtime's event_world module is not there: " .. tostring(W) end
    local okO, world, why = pcall(W.open)
    if not okO or not world then return nil, nil, 'no world (' .. tostring(why or world) .. ')' end
    local okP, players = pcall(W.players, world, true)
    if not okP then return nil, nil, 'the player list: ' .. tostring(players) end
    local id
    for _, pl in ipairs(players) do if pl['local'] then id = tonumber(pl.avatar) end end
    if not id or id <= 0 then return nil, nil, 'the local player has no avatar now' end
    local unit, n = my_unit()
    if not unit then return nil, nil, 'her unit was not found' end
    return { id = id, unit = unit, units = n }, id
end
local function hp() return 'n/a (HD2Runtime cannot read health on HellForge 0.5)' end
local function push(id, vx, vy, vz, flag)
    if not set_velocity then return false end
    vbuf[0], vbuf[1], vbuf[2] = vx, vy, vz
    local ok = pcall(set_velocity, nil, id, vbuf)
    if flag and airborne then ok = pcall(airborne, nil, id, 1) and ok end
    return ok
end
local function pack_name()
    local player = hd2.local_player()
    local ok, p = pcall(function() return player and player:backpack() end)
    return ok and type(p) == 'table' and tostring(p.name or p.catalog or p.id or '?') or 'unknown'
end

-- held keys
local held = {}
local function hold(name, key)
    hd2.input.bind('flight_probe.' .. name, { key = key, on_press = function() held[name] = true end, on_release = function() held[name] = false end })
end
hold('up', 'Numpad8'); hold('down', 'Numpad2'); hold('fwd', 'Up')   -- (0.1.1: not Page Up / Page Down / End: the Maxi Mag Probe's)

-- what runs each frame: one mode at a time
local mode, t, z0, lastz, vz, flagged, health0, nextlog, target
local function stop(why_)
    local _, id = me()
    if id and airborne then pcall(airborne, nil, id, 0) end
    log('%s off (%s) after %.1f s', tostring(mode), why_, t or 0)
    mode = nil
end
local function begin(m)
    local player, id, missing = me()
    if not player then log('%s: %s', m, tostring(missing)); return false end
    if not set_velocity or not airborne then log('%s: unavailable (%s)', m, tostring(why)); return false end
    local p = unit_pos(player.unit)
    if not p then log('%s: her place could not be read', m); return false end
    mode, t, z0, lastz, vz, nextlog = m, 0, p.z, p.z, 0, 0
    health0 = hp()
    log('%s ON: entity %d, height %.1f, %d Helldiver units, backpack %s, layers %s', m, id, p.z, player.units, pack_name(), layers(player.unit))
    return true
end

local dropHeights, dropNext = { 5, 10, 20 }, 1
local droppedAt, fallPeak
hd2.on_frame(function(dt)
    if not mode or dt <= 0 then return end
    local player, id = me()
    if not player then stop('no avatar'); return end
    local p = unit_pos(player.unit)
    if not p then return end
    t = t + dt
    vz = (p.z - lastz) / dt
    lastz = p.z
    local h = p.z - z0
    if mode == 'flight' then
        local up = (held.up and CLIMB or 0) - (held.down and CLIMB or 0)
        local fx, fy = 0, 0
        if held.fwd then fx, fy = facing(player.unit); fx, fy = fx * FLY, fy * FLY end
        push(id, fx, fy, up + G * dt, true)
    elseif mode == 'hover' then
        push(id, 0, 0, G * dt, t < 5)
        if t >= 10 then log('hover test: done, %.2f m from the start height', h); stop('done') end
    elseif mode == 'drop' then
        if not droppedAt then
            push(id, 0, 0, CLIMB + G * dt, true)
            if h >= target then
                droppedAt = t; fallPeak = 0
                pcall(airborne, nil, id, 0)
                log('drop test: at %.1f m, let go', h)
            end
        else
            if vz < fallPeak then fallPeak = vz end
            if t - droppedAt > 0.3 and vz > -0.5 then
                local hp1 = hp()
                hd2.after(0.5, function()
                    local hp2 = hp()
                    log('drop test: from %d m: touchdown at %.1f m/s (fastest fall), health %s -> %s (0.5 s later %s)', target, fallPeak, tostring(health0), tostring(hp1), tostring(hp2))
                end, { scope = 'session' })
                droppedAt = nil; mode = nil
                return
            end
            if t - droppedAt > 8 then log('drop test: no touchdown in 8 s'); droppedAt = nil; mode = nil; return end
        end
    end
    if t >= nextlog and mode then
        nextlog = t + 0.5
        log('%s +%.1f s: height %.2f, vertical %.2f m/s, layers %s', mode, t, h, vz, layers(player.unit))
    end
end, { scope = 'session' })

hd2.input.bind('flight_probe.flight', { key = 'F6', on_press = function()
    if mode == 'flight' then stop('F6') elseif not mode then begin('flight') end
end })
hd2.input.bind('flight_probe.hover', { key = 'F7', on_press = function()
    if not mode and begin('hover') then log('hover test: flag every frame for 5 s, then the speed only') end
end })
hd2.input.bind('flight_probe.drop', { key = 'Ctrl+F7', on_press = function()
    if mode then return end
    target = dropHeights[dropNext]
    dropNext = dropNext % #dropHeights + 1
    if begin('drop') then droppedAt = nil; log('drop test: climbing to %d m', target) end
end })
hd2.input.bind('flight_probe.fall', { key = 'Ctrl+F6', on_press = function()
    local unit = my_unit()
    if not unit then log('fall event: no unit'); return end
    local before = layers(unit)
    local ok = pcall(S.Unit.animation_event, unit, 'fall')
    hd2.after(0.2, function() log('fall event: %s; layers %s -> %s', ok and 'sent' or 'FAILED', before, layers(unit)) end, { scope = 'session' })
end })
log('0.1.2 loaded (airborne flag %s, SetVelocity %s, layer getter %s): F6 flight (Numpad 8 / Numpad 2 / Up arrow), F7 hover test, Ctrl+F7 drop test, Ctrl+F6 fall event',
    airborne and 'ready' or 'NO', set_velocity and 'ready' or 'NO', get_state and 'yes' or 'no')
if why then log('not this build: %s', why) end
