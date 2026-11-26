------------------------------------------------- KB_WORLD: her camera and world points on the overlay (one table: KBW) --
-- Parts adapted from Chef Shared Loader by Th3chef (github.com/Th3chef/Chef-Shared-Loader, src/loader.lua): finding the
-- view camera (a living unit with Unit.num_cameras > 0, its Unit.camera(u, 1), searched a few hundred units a frame),
-- projecting a world point with Camera.world_to_screen (bottom-left origin) and the behind-the-camera check, with
-- Th3chef's permission. The camera taken is the one standing nearest her view (the DLL's <kit>_view, kb_world.h's
-- KB_VIEW_EXPORTS): the first camera found was, in a mission, one 6 km under the map (Lemuen 0.1.6c). Without the
-- export, the first camera. Lifted 2026-11-26 from Lemuen, Alva, Ascalon, Executor, Koleda, Vector and W.
-- Use: local v = KBW.view(d, t) once a frame (nil: no camera yet); KBW.project(v, x, y, z) -> sx, sy, distance, or nil
-- behind the camera or off screen.
local KBW = (function()
    pcall(ffi.cdef, [[ typedef int (*kbw_view1)(unsigned int version, float *out); ]])
    local C = {unit = nil, list = nil, i = 0, retry = 0, checked = 0, told = 0, fn = nil, look = 0, buf = ffi.new('float[3]')}
    local function S() return rawget(_G, 'stingray') end
    local function her_view(t)
        if not C.fn then
            if t < C.look then return nil end
            C.look = t + 2
            local module = KIT.module(KIT.id .. '.dll')
            local proc = module ~= nil and KIT.proc(module, KIT.id .. '_view') or nil
            if proc == nil then return nil end
            C.fn = ffi.cast('kbw_view1', proc)
        end
        if C.fn(1, C.buf) ~= 1 then return nil end
        return C.buf[0], C.buf[1], C.buf[2]
    end
    local function gap(s, u, hx, hy, hz)
        local okc, c = pcall(s.Unit.camera, u, 1)
        if not (okc and c) then return nil end
        local okp, p = pcall(s.Camera.world_position, c)
        if not (okp and p) then return nil end
        local x, y, z = s.Vector3.to_elements(p)
        if type(x) ~= 'number' then return nil end
        return math.sqrt((x - hx) ^ 2 + (y - hy) ^ 2 + (z - hz) ^ 2)
    end
    local function camera(s, t)
        local hx, hy, hz = her_view(t)
        if C.unit then
            local ok, alive = pcall(s.Unit.alive, C.unit)
            if ok and alive == true then
                if not hx or t < C.checked then return C.unit end
                C.checked = t + 1
                local g = gap(s, C.unit, hx, hy, hz)
                if g and g < 6 then return C.unit end
            end
            C.unit = nil
        end
        if not C.list then
            if t < C.retry then return nil end
            local okw, w = pcall(s.Application.main_world)
            if not (okw and w) then C.retry = t + 2; return nil end
            local oku, units = pcall(s.World.units, w)
            if not (oku and type(units) == 'table') then C.retry = t + 2; return nil end
            C.list, C.i, C.best, C.gap, C.seen = units, 0, nil, nil, 0
        end
        local last = math.min(#C.list, C.i + 500)
        for i = C.i + 1, last do
            local u = C.list[i]
            local oka, alive = pcall(s.Unit.alive, u)
            if oka and alive == true then
                local okn, n = pcall(s.Unit.num_cameras, u)
                if okn and type(n) == 'number' and n > 0 then
                    C.seen = C.seen + 1
                    if not hx then C.unit, C.list = u, nil; return u end
                    local g = gap(s, u, hx, hy, hz)
                    if g and (not C.gap or g < C.gap) then C.best, C.gap = u, g end
                end
            end
        end
        C.i = last
        if last >= #C.list then
            local found = C.best and C.gap and C.gap < 6
            if C.told < 4 then
                C.told = C.told + 1
                log(('[%s HUD] camera: %d cameras in the world, nearest to her view %s'):format(KIT.name, C.seen,
                    C.gap and ('%.1f m away%s'):format(C.gap, found and ' (taken)' or ' (too far: none taken)') or 'none'))
            end
            C.list = nil
            if found then C.unit = C.best; C.checked = t + 1; return C.unit end
            C.retry = t + 2
        end
        return nil
    end
    local function view(d, t)
        local s = S()
        if type(s) ~= 'table' or not (s.Unit and s.Camera and s.Vector3 and s.Quaternion and s.World and s.Application) then return nil end
        local u = camera(s, t or 0)
        if not u then return nil end
        local c = s.Unit.camera(u, 1)
        if not c then return nil end
        local cx, cy, cz = s.Vector3.to_elements(s.Camera.world_position(c))
        local fx, fy, fz = s.Vector3.to_elements(s.Quaternion.forward(s.Camera.world_rotation(c)))
        local W, H = d.width, d.height or (d.width * 9 / 16)
        -- the engine's screen pixels against the overlay's (the same at most resolutions; scaled if not)
        local bw, bh = W, H
        if s.Application.back_buffer_size then
            local okb, a, b = pcall(s.Application.back_buffer_size)
            if okb and type(a) == 'number' and a > 0 then bw, bh = a, b end
        end
        return {s = s, c = c, cx = cx, cy = cy, cz = cz, fx = fx, fy = fy, fz = fz, W = W, H = H, kx = W / bw, ky = H / bh}
    end
    local function project(v, x, y, z)
        local ax, ay, az = x - v.cx, y - v.cy, z - v.cz
        if ax * v.fx + ay * v.fy + az * v.fz <= 0.5 then return nil end
        local sx, sy = v.s.Vector3.to_elements(v.s.Camera.world_to_screen(v.c, v.s.Vector3(x, y, z)))
        if type(sx) ~= 'number' then return nil end
        sx, sy = sx * v.kx, v.H - sy * v.ky                    -- the overlay's origin is its top-left
        if sx <= 0 or sx >= v.W or sy <= 0 or sy >= v.H then return nil end
        return sx, sy, math.sqrt(ax * ax + ay * ay + az * az)
    end
    return {S = S, view = view, project = project}
end)()
