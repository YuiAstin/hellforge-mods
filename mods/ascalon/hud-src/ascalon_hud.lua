-- Ascalon HUD (mods/yui/ascalon_hud): the side of Ascalon's kit that the engine's Lua has to do.
--   * While she is hidden her body is not drawn: every visible mesh of her Helldiver's units (the avatar and each armour
--     part are units of their own sharing one root) is switched off with Unit.set_mesh_visibility, and again four times
--     a second, since the game switches meshes back on by itself; with hide_gear her weapons and backpack too. The way
--     is the CAMO-EX01 Optical Cloak Pack's "compatibility" cloak (hide, remember, re-hide), found with the Cloak Probe.
--   * A line at the bottom middle says what she is: hidden, revealed (how many enemies sense her, then the seconds
--     left), or not hidden because of a backpack or support weapon.
-- Ascalon is a HellForge code mod; this reads ascalon.dll's state (ascalon_hud_state, layout version 3) through LuaJIT's
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
typedef struct aschud3_state { uint32_t version, in_mission, state; float reveal_left; uint32_t cloak_visual, hide_gear; uint32_t units[5]; uint32_t sensing; float e_left, e_ready, d_left, d_ready; uint32_t keys[3]; char key_label[3][16]; uint32_t charges, charges_max; float charge_left; uint32_t nwanted; uint32_t wanted[256]; } aschud3_state;
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
    fn = ffi.cast('aschud3_state *(*)(void)', proc)
    state = fn()
    if state == nil or state.version < 3 then fn = nil return nil end
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
    local linked = math.min(tonumber(s.nwanted), 256)
    for k = 0, linked - 1 do want[tonumber(s.wanted[k])] = true end
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
    -- 0.1.3: her units by the engine's own links, when the DLL has them: exactly what hangs on her, nothing by position
    if linked > 1 then
        local out, found = {}, 0
        for k = 0, linked - 1 do
            local u = byid[tonumber(s.wanted[k])]
            if u then out[#out + 1] = u; found = found + 1 end
        end
        local sig = 'links:' .. linked .. ':' .. found
        if sig ~= cl.ids and cl.told < 12 then
            cl.told = cl.told + 1
            log(('[Ascalon HUD] her units: %d of the %d the engine links to her are in the worlds (%d units in all)'):format(found, linked, total))
        end
        cl.ids = sig
        if found > 0 then return out end
    end
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
    [5] = {'DESCENT', ORANGE},
}
-- ---- her damage numbers (0.2.1; the Executor's 0.2.4 block, itself Lemuen's): a number over every hit, strike and
-- tick of hers (ascalon.dll's ascalon_hits: x, y, z, damage, Pursuit's). The view camera: the engine camera standing
-- where her view is (ascalon_view). Parts adapted from Chef Shared Loader by Th3chef (github.com/Th3chef/Chef-Shared-
-- Loader, src/loader.lua): finding the view camera (a living unit with Unit.num_cameras > 0, its Unit.camera(u, 1)),
-- projecting a world point with Camera.world_to_screen (bottom-left origin) and the behind-the-camera check, with
-- Th3chef's permission (as Lemuen's HUD).
pcall(ffi.cdef, [[ typedef int (*aschits1)(unsigned int version, float *out, int max, uint32_t *seq);
typedef int (*ascview1)(unsigned int version, float *out); ]])
local MK = {told = false}
local CAM = {unit = nil, list = nil, i = 0, retry = 0}
local function SR() return rawget(_G, 'stingray') end
-- The camera standing where her view is (ascalon.dll's ascalon_view), checked every second (Lemuen 0.1.6b took the
-- first unit with a camera, one 6 km under the map, and every number was "behind" it).
local VW = {fn = nil, buf = ffi.new('float[6]'), look = 0, checked = 0, told = 0}
local function her_view(t)
    if not VW.fn then
        if t < VW.look then return nil end
        VW.look = t + 2
        local module = k32.aschud1_GetModuleHandleA('ascalon.dll')
        local proc = module ~= nil and k32.aschud1_GetProcAddress(module, 'ascalon_view') or nil
        if proc == nil then return nil end
        VW.fn = ffi.cast('ascview1', proc)
    end
    if VW.fn(1, VW.buf) ~= 1 then return nil end
    return VW.buf[0], VW.buf[1], VW.buf[2]
end
local function cam_gap(s, u, hx, hy, hz)
    local okc, c = pcall(s.Unit.camera, u, 1)
    if not (okc and c) then return nil end
    local okp, p = pcall(s.Camera.world_position, c)
    if not (okp and p) then return nil end
    local x, y, z = s.Vector3.to_elements(p)
    if type(x) ~= 'number' then return nil end
    return math.sqrt((x - hx) ^ 2 + (y - hy) ^ 2 + (z - hz) ^ 2), x, y, z
end
local function cam_unit(s, t)
    local hx, hy, hz = her_view(t)
    if CAM.unit then
        local ok, alive = pcall(s.Unit.alive, CAM.unit)
        if ok and alive == true then
            if not hx or t < VW.checked then return CAM.unit end
            VW.checked = t + 1
            local gap = cam_gap(s, CAM.unit, hx, hy, hz)
            if gap and gap < 4 then return CAM.unit end
        end
        CAM.unit = nil
    end
    if not CAM.list then
        if t < CAM.retry then return nil end
        local okw, w = pcall(s.Application.main_world)
        if not (okw and w) then CAM.retry = t + 2; return nil end
        local oku, units = pcall(s.World.units, w)
        if not (oku and type(units) == 'table') then CAM.retry = t + 2; return nil end
        CAM.list, CAM.i, CAM.best, CAM.gap, CAM.seen = units, 0, nil, nil, 0
    end
    local last = math.min(#CAM.list, CAM.i + 500)
    for i = CAM.i + 1, last do
        local u = CAM.list[i]
        local oka, alive = pcall(s.Unit.alive, u)
        if oka and alive == true then
            local okn, n = pcall(s.Unit.num_cameras, u)
            if okn and type(n) == 'number' and n > 0 then
                CAM.seen = CAM.seen + 1
                if not hx then CAM.unit, CAM.list = u, nil; return u end
                local gap = cam_gap(s, u, hx, hy, hz)
                if gap and (not CAM.gap or gap < CAM.gap) then CAM.best, CAM.gap = u, gap end
            end
        end
    end
    CAM.i = last
    if last >= #CAM.list then
        local found = CAM.best and CAM.gap and CAM.gap < 4
        if VW.told < 6 then
            VW.told = VW.told + 1
            log(('[Ascalon HUD] camera: %d cameras in the world, nearest to her view %s'):format(CAM.seen,
                CAM.gap and ('%.1f m away%s'):format(CAM.gap, found and ' (taken)' or ' (too far: none taken)') or 'none'))
        end
        CAM.list = nil
        if found then CAM.unit = CAM.best; VW.checked = t + 1; return CAM.unit end
        CAM.retry = t + 2
    end
    return nil
end
local mk_t = 0
-- (0.1.3: her damage numbers, below, share this frame's view)
local HN = {fn = nil, buf = ffi.new('float[320]'), seq = ffi.new('uint32_t[1]'), live = {}, primed = false}
local function view(d)
    local s = SR()
    if type(s) ~= 'table' or not (s.Unit and s.Camera and s.Vector3 and s.Quaternion and s.World and s.Application) then return nil end
    local u = cam_unit(s, mk_t)
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
    local kx, ky = W / bw, H / bh
    if not MK.told then MK.told = true; log(('[Ascalon HUD] view: overlay %dx%d, screen %dx%d'):format(W, H, bw, bh)) end
    return {s = s, c = c, cx = cx, cy = cy, cz = cz, fx = fx, fy = fy, fz = fz, W = W, H = H, kx = kx, ky = ky}
end
-- a world point on the overlay, or nil when behind the camera or off screen
local function project(v, x, y, z)
    local ax, ay, az = x - v.cx, y - v.cy, z - v.cz
    if ax * v.fx + ay * v.fy + az * v.fz <= 0.5 then return nil end
    local sx, sy = v.s.Vector3.to_elements(v.s.Camera.world_to_screen(v.c, v.s.Vector3(x, y, z)))
    if type(sx) ~= 'number' then return nil end
    sx, sy = sx * v.kx, v.H - sy * v.ky                    -- the overlay's origin is its top-left
    if sx <= 0 or sx >= v.W or sy <= 0 or sy >= v.H then return nil end
    return sx, sy, math.sqrt(ax * ax + ay * ay + az * az)
end

local HN_LIFE, HN_RISE, HN_MAX = 2.2, 46, 48
local DG = {next = 5, told = 0, view_nil = 0, view_ok = 0, quiet = 0, seq = 0}
local function numbers_draw(d, dt, v)
    if not HN.fn then
        local module = k32.aschud1_GetModuleHandleA('ascalon.dll')
        local proc = module ~= nil and k32.aschud1_GetProcAddress(module, 'ascalon_hits') or nil
        if proc == nil then return end
        HN.fn = ffi.cast('aschits1', proc)
    end
    local n = HN.fn(1, HN.buf, 64, HN.seq)
    if not HN.primed then HN.primed = true; n = 0 end    -- (not the hits from before the HUD looked)
    for i = 0, n - 1 do
        if (DG.hits or 0) < 8 then
            DG.hits = (DG.hits or 0) + 1
            local hx, hy, hz = HN.buf[i * 5], HN.buf[i * 5 + 1], HN.buf[i * 5 + 2] + 0.6
            local sx, sy = nil, nil
            if v then sx, sy = project(v, hx, hy, hz) end
            log(('[Ascalon HUD] number %d: %.0f at %.1f %.1f %.1f, %s'):format(DG.hits, HN.buf[i * 5 + 3], hx, hy, hz,
                not v and 'no view this frame' or sx and ('drawn at %.0f %.0f'):format(sx, sy) or 'nowhere (behind or off screen)'))
        end
        if #HN.live >= HN_MAX then table.remove(HN.live, 1) end
        HN.live[#HN.live + 1] = {x = HN.buf[i * 5], y = HN.buf[i * 5 + 1], z = HN.buf[i * 5 + 2] + 0.6, amount = HN.buf[i * 5 + 3],
                                 boosted = HN.buf[i * 5 + 4] > 0.5, t = 0, jx = (math.random() - 0.5) * 18}
    end
    local sc = d.scale or 1
    for i = #HN.live, 1, -1 do
        local h = HN.live[i]
        h.t = h.t + dt
        if h.t >= HN_LIFE then table.remove(HN.live, i)
        elseif v then
            local sx, sy = project(v, h.x, h.y, h.z)
            if sx then
                local rise = HN_RISE * (1 - math.exp(-h.t * 7))  -- fast, then slowing to a stop
                local pop = h.t < 0.12 and (1.35 - h.t / 0.12 * 0.35) or 1
                local a = h.t > HN_LIFE - 0.45 and (HN_LIFE - h.t) / 0.45 or 1
                local size = (h.boosted and 24 or 18) * sc * pop
                local col = h.boosted and {255, 138, 36} or {150, 235, 225}
                local str = tostring(math.floor(h.amount + 0.5))
                local px, py = sx + h.jx * sc, sy - (rise + 6) * sc
                d:text(str, px + 2 * sc, py + 2 * sc, {size = size, colour = {20, 10, 5, math.floor(200 * a)}, z = 7, align = 'center'})
                d:text(str, px, py, {size = size, colour = {col[1], col[2], col[3], math.floor(255 * a)}, z = 8, align = 'center'})
            end
        end
    end
end


-- The parry's clash (0.4.1): ascalon.dll counts parries (ascalon_parry_seq); when the count rises, the event
-- ascalon_parry of the mod's own bank (data/, content/audio/ascalon_parry) is posted through HD2Runtime's hd2.sounds.
-- The bank's resource is resident with the mod's patch; the Wwise plugin's own Wwise.load_bank puts it in the sound
-- engine, once, when the engine does not know the event yet.
pcall(ffi.cdef, [[ typedef unsigned int (*ascparry1)(unsigned int version); ]])
local PS = {fn = nil, look = 0, seen = nil, loaded = false, told = 0, bank = 'content/audio/ascalon_parry', event = 'ascalon_parry'}
local function parry_sound(dt)
    PS.look = PS.look - dt
    if not PS.fn then
        if PS.look > 0 then return end
        PS.look = 2
        local module = k32.aschud1_GetModuleHandleA('ascalon.dll')
        local proc = module ~= nil and k32.aschud1_GetProcAddress(module, 'ascalon_parry_seq') or nil
        if proc == nil then return end
        PS.fn = ffi.cast('ascparry1', proc)
    end
    local seq = tonumber(PS.fn(1))
    if PS.seen == nil then PS.seen = seq end
    local s = SR()
    local W = s and s.Wwise
    if not PS.loaded and W and type(W.has_event) == 'function' and PS.look <= 0 then
        PS.look = 2
        local okh, has = pcall(W.has_event, PS.event)
        if okh and has == true then
            PS.loaded = true
            log('[Ascalon HUD] the parry sound is in the sound engine')
        elseif type(W.load_bank) == 'function' then
            local okl, err = pcall(W.load_bank, PS.bank)
            if PS.told < 3 then PS.told = PS.told + 1; log(('[Ascalon HUD] loading the parry bank: %s %s'):format(tostring(okl), tostring(err))) end
        end
    end
    if seq == PS.seen then return end
    PS.seen = seq
    if type(hd2.sounds) ~= 'table' or type(hd2.sounds.play) ~= 'function' then return end
    local ok, handle, code, why = pcall(hd2.sounds.play, PS.event)
    if PS.told < 12 then
        PS.told = PS.told + 1
        log(('[Ascalon HUD] parry %d: sound %s %s %s'):format(seq, tostring(ok and handle ~= nil), tostring(code), tostring(why or (not ok and handle) or '')))
    end
end

local overlay = hd2.ui.overlay({id = 'ascalon', owner = OWNER})
overlay:draw(function(d, dt)
    dt = tonumber(dt) or 0
    local s = find(dt)
    cloak_frame(s, dt)
    if not PS.failed then local okP, errP = pcall(parry_sound, dt); if not okP then PS.failed = true; log('[Ascalon HUD] parry sound failed, off for this session: ' .. tostring(errP)) end end
    if s == nil or s.in_mission == 0 then return end
    mk_t = mk_t + dt
    local okV, v = pcall(view, d)
    if not okV then
        if not HN.view_err then HN.view_err = true; log('[Ascalon HUD] view failed: ' .. tostring(v)) end
        v = nil
    end
    if not HN.failed then
        local okN, errN = pcall(numbers_draw, d, dt, v)
        if not okN then HN.failed = true; log('[Ascalon HUD] damage numbers failed, off for this session: ' .. tostring(errN)) end
    end
    local line = LINES[tonumber(s.state)]
    if not line then return end
    local sc = d.scale
    local text = line[1]
    local left = tonumber(s.reveal_left)
    local n = tonumber(s.sensing)
    if s.state == 2 and n > 0 then text = ('%s: %d %s'):format(text, n, n == 1 and 'ENEMY SENSES HER' or 'ENEMIES SENSE HER')
    elseif s.state == 5 then text = ('%s  %.1fs'):format(text, tonumber(s.d_left))
    elseif left > 0 then text = ('%s  %.1fs'):format(text, left) end
    if tonumber(s.charges_max) > 0 then
        local c, m, cl = tonumber(s.charges), tonumber(s.charges_max), tonumber(s.charge_left)
        text = ('%s   |   PURSUIT %d/%d%s'):format(text, c, m, (c < m and cl > 0) and ('  %.0fs'):format(cl) or '')
    end
    local size = 16 * sc
    local cx, y = d.width / 2, d.height - 132 * sc
    local w, h = (#text * 10 + 36) * sc, 26 * sc
    d:rect(cx - w / 2, y, w, h, rgba(PANEL, 0.6), 0)
    d:rect(cx - w / 2, y + h - 2 * sc, w, 2 * sc, rgba(line[2], 0.9), 1)
    d:text(text, cx + math.max(1, sc), y + 4 * sc + math.max(1, sc), {size = size, font = 'title', colour = rgba(INK, 0.7), z = 4, align = 'center'})
    d:text(text, cx, y + 4 * sc, {size = size, font = 'title', colour = rgba(line[2], 1), z = 5, align = 'center'})
    -- her keyed skills, on a second line: running (seconds left), cooling down (seconds), or ready (its key)
    local function skill(name, key, left2, ready)
        if left2 > 0 then return ('%s %.0fs'):format(name, left2), true end
        if ready > 0 then return ('%s in %.0fs'):format(name, ready), false end
        return ('[%s] %s'):format(key, name), false
    end
    local e_text, e_on = skill('ENDOWMENT', ffi.string(s.key_label[1]), tonumber(s.e_left), tonumber(s.e_ready))
    local d_text, d_on = skill('DESCENT', ffi.string(s.key_label[2]), tonumber(s.d_left), tonumber(s.d_ready))
    local t2 = e_text .. '   |   ' .. d_text
    local y2, size2 = y + h + 4 * sc, 13 * sc
    local w2 = (#t2 * 8 + 28) * sc
    d:rect(cx - w2 / 2, y2, w2, 20 * sc, rgba(PANEL, 0.5), 0)
    d:text(t2, cx, y2 + 3 * sc, {size = size2, font = 'title', colour = rgba((e_on or d_on) and ORANGE or TEXT, 0.95), z = 5, align = 'center'})
end)

log('[Ascalon HUD] ready (waiting for Ascalon)')
