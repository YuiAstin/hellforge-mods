-- W (mods/yui/w_hud): the Lua half of the HellForge kit yui.w, first build. The DLL (w.dll) picks the enemies King of
-- Hearts' rounds hold and times each one's hold; this finds each enemy's unit in the game's Lua and switches its
-- animation state machine off while it is held, then on again (the Time Stop Probe's pose freeze, per enemy). It also
-- draws the skill's line. What it learns is written to the DLL's log (mod-yui.w.log) through w_log.
-- Requires Bingus Shared Loader and HD2Runtime (by SkyeShade, https://github.com/SkyeShade/HD2Runtime; its overlay
-- gives the frame callback). The rare-type rule for reading a unit's place is Exo Autopilot's finding (marti).
local OWNER = 'mods/yui/w_hud'

local ok, hd2 = pcall(require, 'mods/skyeshade/hd2runtime')
if not ok or type(hd2) ~= 'table' or type(hd2.ui) ~= 'table' or type(hd2.ui.overlay) ~= 'function' then
    pcall(print, '[W] HD2Runtime with hd2.ui.overlay is not available: the pose freeze and the skill line are off')
    return
end

local ffi = require('ffi')
pcall(ffi.cdef, [[
typedef struct wk1_target { uint32_t entity, unit; uint64_t object; float pos[3]; uint32_t pad; } wk1_target;
typedef struct wk1_state { uint32_t version, seq, active, skill, count, rare_max; uint32_t player_unit, in_mission;
    uint64_t player_object; float player_pos[3], value; wk1_target t[48]; uint32_t dead_seq, dead[16]; } wk1_state;
typedef int (*wk1_mines)(unsigned int version, float *out, int max);
typedef int (*wk1_view)(unsigned int version, float *out);
typedef int (*wk1_log)(unsigned int version, const char *text);
void *wk1_GetModuleHandleA(const char *name) __asm__("GetModuleHandleA");
void *wk1_GetProcAddress(void *module, const char *name) __asm__("GetProcAddress");
]])
local k32 = ffi.load('kernel32')

local state, logfn, pending, lines = nil, nil, {}, 0
local minesfn, viewfn = nil, nil
local function log(m)
    m = tostring(m)
    lines = lines + 1
    if lines > 300 then return end
    pcall(print, '[W] ' .. m)
    if logfn then pcall(logfn, 1, m) elseif #pending < 100 then pending[#pending + 1] = m end
end
local find_wait = 0
local function connect(dt)
    if state ~= nil then return state end
    find_wait = find_wait - dt
    if find_wait > 0 then return nil end
    find_wait = 2
    local module = k32.wk1_GetModuleHandleA('w.dll')
    if module == nil then return nil end
    local p1, p2 = k32.wk1_GetProcAddress(module, 'w_state'), k32.wk1_GetProcAddress(module, 'w_log')
    if p1 == nil or p2 == nil then return nil end
    local s = ffi.cast('wk1_state *(*)(void)', p1)()
    if s == nil or s.version ~= 1 then return nil end
    logfn = ffi.cast('wk1_log', p2)
    local p3, p4 = k32.wk1_GetProcAddress(module, 'w_mines'), k32.wk1_GetProcAddress(module, 'w_view')
    if p3 ~= nil then minesfn = ffi.cast('wk1_mines', p3) end
    if p4 ~= nil then viewfn = ffi.cast('wk1_view', p4) end
    state = s
    for _, m in ipairs(pending) do pcall(logfn, 1, m) end
    pending = {}
    log('connected to the DLL')
    return state
end

local function S_() return rawget(_G, 'stingray') end

-- ---- units ----
local function node_index()
    local s = S_()
    local okI, off = pcall(function() return s.Script.index_offset() end)
    return okI and type(off) == 'number' and off or 1
end
local RI
local function upos(s, u)
    RI = RI or node_index()
    local okP, v = pcall(s.Unit.world_position, u, RI)
    if not okP or not v then return nil end
    local x, y, z = s.Vector3.to_elements(v)
    return x, y, z
end
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
-- the units of types with at most `most` units in the world (reading the place of a unit of a mass-instanced type has
-- crashed the game: Exo Autopilot's finding)
local function rare_units(s, all, most)
    local res, count = {}, {}
    for i, u in ipairs(all) do
        local okR, r = pcall(s.Unit.resource_name, u)
        r = okR and tostring(r) or '?'
        res[i] = r
        count[r] = (count[r] or 0) + 1
    end
    local out = {}
    for i, u in ipairs(all) do if res[i] ~= '?' and count[res[i]] <= most then out[#out + 1] = u end end
    return out
end
local function has_machine(s, u)
    local okH, has = pcall(s.Unit.has_animation_state_machine, u)
    return okH and has == true
end

-- how a Lua unit value holds the unit (learned once from the player's own unit): 'object', 'id' or false (by place only)
local mode, shift, low, high = nil, 0, 0, 0
local function learn_mode(s, st, rare)
    local px, py, pz = st.player_pos[0], st.player_pos[1], st.player_pos[2]
    local anchor, best = nil, 0.09
    for _, u in ipairs(rare) do
        local x, y, z = upos(s, u)
        if x then
            local d2 = (x - px) ^ 2 + (y - py) ^ 2 + (z - pz) ^ 2
            if d2 < best and has_machine(s, u) then anchor, best = u, d2 end
        end
    end
    if not anchor then return end
    local P, U, O = uptr(anchor), st.player_unit, tonumber(st.player_object)
    if P and P == O then mode = 'object'
    elseif P then
        for k = 0, 24 do
            if math.floor(P / 2 ^ k) % 2 ^ 32 == U then mode, shift, low, high = 'id', k, P % 2 ^ k, math.floor(P / 2 ^ (k + 32)) break end
        end
    end
    if mode == nil then mode = false end
    log('a Lua unit value ' .. (mode == 'object' and 'is the unit object\'s address' or mode == 'id' and ('holds the unit id at bit ' .. shift) or
        'is neither the unit object nor its id: units are found by place only'))
end

local held = {}       -- entity -> {u = unit, off = bool}
local last_seq = -1
local can_freeze = nil

-- an enemy that died while held: its state machine was off as it died, so the animation its death asks for never
-- started (a big enemy stood dead in its pose and was not cleared away). Its machine is switched on again and the
-- first of these events its machine has is asked for; which it has is logged.
local DEATH = {'death', 'dead', 'die', 'dying', 'killed', 'death_default', 'death_front', 'death_back', 'ragdoll', 'to_ragdoll', 'death_ragdoll', 'explode', 'gib'}
local dead_seq, dead, death_told, unit_told = 0, {}, 0, false
local function died(s, u)
    if type(s.Unit.has_animation_event) ~= 'function' or type(s.Unit.animation_event) ~= 'function' then
        if death_told < 1 then death_told = 1; log('Unit.has_animation_event / animation_event are not in this game\'s Lua: a death while held cannot be put right here') end
        return
    end
    local has, fired = {}, nil
    for _, name in ipairs(DEATH) do
        local okH, h = pcall(s.Unit.has_animation_event, u, name)
        if okH and h == true then
            has[#has + 1] = name
            if not fired and pcall(s.Unit.animation_event, u, name) then fired = name end
        end
    end
    if death_told < 8 then
        death_told = death_told + 1
        local okR, r = pcall(s.Unit.resource_name, u)
        log(('died while held: %s: its machine has %s; asked for %s'):format(okR and tostring(r) or '?', #has > 0 and table.concat(has, ', ') or 'none of the events tried', fired or 'nothing'))
    end
end
local function release(s, e, touch)
    local h = held[e]
    held[e] = nil
    if not h or not h.off or not touch then return 0 end
    local okA, alive = pcall(s.Unit.alive, h.u)
    if okA and alive == true then
        local okE = pcall(s.Unit.enable_animation_state_machine, h.u)
        if okE and dead[e] then dead[e] = nil; pcall(died, s, h.u) end
        return okE and 1 or 0
    end
    return 0
end

local function sync(st)
    local s = S_()
    last_seq = st.seq
    if type(s) ~= 'table' or not (s.Unit and s.World and s.Application and s.Vector3) then return end
    while dead_seq ~= st.dead_seq do
        if st.dead_seq - dead_seq > 16 then dead_seq = st.dead_seq - 16 end
        dead[st.dead[dead_seq % 16]] = true
        dead_seq = dead_seq + 1
    end
    if not unit_told then
        unit_told = true
        local names = {}
        for k, f in pairs(s.Unit) do
            if type(k) == 'string' and type(f) == 'function' and (k:find('anim') or k:find('state_machine') or k:find('ragdoll') or k:find('time_scale')) then names[#names + 1] = k end
        end
        table.sort(names)
        local line = ''
        for _, k in ipairs(names) do
            if #line + #k > 360 then log('Unit: ' .. line); line = '' end
            line = line .. k .. ' '
        end
        if line ~= '' then log('Unit: ' .. line) end
    end
    if can_freeze == nil then
        can_freeze = type(s.Unit.disable_animation_state_machine) == 'function' and type(s.Unit.enable_animation_state_machine) == 'function'
        if not can_freeze then log('Unit.disable_animation_state_machine / enable_animation_state_machine are not in this game\'s Lua: no pose freeze') end
    end
    if not can_freeze then return end
    local want, fresh = {}, {}
    for k = 0, st.count - 1 do
        local t = st.t[k]
        local e = t.entity
        want[e] = true
        if not held[e] then fresh[#fresh + 1] = {e = e, unit = t.unit, object = tonumber(t.object), x = t.pos[0], y = t.pos[1], z = t.pos[2]} end
    end
    local on = 0
    for e in pairs(held) do if not want[e] then on = on + release(s, e, true) end end
    if #fresh == 0 then
        if on > 0 then log(('freeze %d: %d let go'):format(st.seq, on)) end
        return
    end
    local okW, w = pcall(s.Application.main_world)
    if not okW or not w then return end
    local okU, all = pcall(s.World.units, w)
    if not okU or type(all) ~= 'table' then return end
    local rare = nil
    if mode == nil then rare = rare_units(s, all, st.rare_max); learn_mode(s, st, rare) end
    local found, off = 0, 0
    if mode then
        local byptr = {}
        for _, u in ipairs(all) do
            local p = uptr(u)
            if p then byptr[p] = u end
        end
        for _, t in ipairs(fresh) do
            local key = mode == 'object' and t.object or (high * 2 ^ (shift + 32) + t.unit * 2 ^ shift + low)
            local u = key and byptr[key]
            if u then t.u = u; found = found + 1 end
        end
    end
    if found < #fresh then
        rare = rare or rare_units(s, all, st.rare_max)
        for _, u in ipairs(rare) do
            local x, y, z = upos(s, u)
            if x then
                for _, t in ipairs(fresh) do
                    if not t.u and (x - t.x) ^ 2 + (y - t.y) ^ 2 + (z - t.z) ^ 2 < 0.25 and has_machine(s, u) then t.u = u; found = found + 1 break end
                end
            end
        end
    end
    for _, t in ipairs(fresh) do
        local h = {u = t.u, off = false}
        if t.u and has_machine(s, t.u) then
            local okD = pcall(s.Unit.disable_animation_state_machine, t.u)
            if okD then h.off = true; off = off + 1 end
        end
        held[t.e] = h
    end
    log(('freeze %d: %d new (%d units found, %d held in pose), %d let go, %d held in all'):format(st.seq, #fresh, found, off, on, st.count))
end

-- ---- Jack in the Box's mines on the HUD (0.4.1) -------------------------------------------------------------------
-- Parts adapted from Chef Shared Loader by Th3chef (github.com/Th3chef/Chef-Shared-Loader, src/loader.lua), as the other
-- kits' markers are, with Th3chef's permission: the view camera (a living unit with Unit.num_cameras > 0, searched a few
-- hundred units a frame; the one nearest her head, which w.dll's w_view gives), a world point projected with
-- Camera.world_to_screen (bottom-left origin) and the behind-the-camera check.
local CAM = {unit = nil, list = nil, i = 0, retry = 0, checked = 0, told = 0}
local vbuf, mbuf, mk_t = ffi.new('float[3]'), ffi.new('float[256]'), 0
local function cam_gap(s, u, hx, hy, hz)
    local okc, c = pcall(s.Unit.camera, u, 1)
    if not (okc and c) then return nil end
    local okp, p = pcall(s.Camera.world_position, c)
    if not (okp and p) then return nil end
    local x, y, z = s.Vector3.to_elements(p)
    if type(x) ~= 'number' then return nil end
    return math.sqrt((x - hx) ^ 2 + (y - hy) ^ 2 + (z - hz) ^ 2)
end
local function cam_unit(s, t)
    if not viewfn or viewfn(1, vbuf) ~= 1 then return nil end
    local hx, hy, hz = vbuf[0], vbuf[1], vbuf[2]
    if CAM.unit then
        local ok, alive = pcall(s.Unit.alive, CAM.unit)
        if ok and alive == true then
            if t < CAM.checked then return CAM.unit end
            CAM.checked = t + 1
            local gap = cam_gap(s, CAM.unit, hx, hy, hz)
            if gap and gap < 6 then return CAM.unit end
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
                local gap = cam_gap(s, u, hx, hy, hz)
                if gap and (not CAM.gap or gap < CAM.gap) then CAM.best, CAM.gap = u, gap end
            end
        end
    end
    CAM.i = last
    if last >= #CAM.list then
        local found = CAM.best and CAM.gap and CAM.gap < 6
        if CAM.told < 4 then
            CAM.told = CAM.told + 1
            log(('camera: %d cameras in the world, nearest to her head %s'):format(CAM.seen, CAM.gap and ('%.1f m away%s'):format(CAM.gap, found and ' (taken)' or ' (too far: none taken)') or 'none'))
        end
        CAM.list = nil
        if found then CAM.unit = CAM.best; CAM.checked = t + 1; return CAM.unit end
        CAM.retry = t + 2
    end
    return nil
end
local function mines_draw(d)
    local s = S_()
    if not minesfn or type(s) ~= 'table' or not (s.Unit and s.Camera and s.Vector3 and s.Quaternion and s.World and s.Application) then return end
    local n = minesfn(1, mbuf, 64)
    if n <= 0 then return end
    local u = cam_unit(s, mk_t)
    if not u then return end
    local c = s.Unit.camera(u, 1)
    if not c then return end
    local cx, cy, cz = s.Vector3.to_elements(s.Camera.world_position(c))
    local fx, fy, fz = s.Vector3.to_elements(s.Quaternion.forward(s.Camera.world_rotation(c)))
    local W, H = d.width, d.height or (d.width * 9 / 16)
    local bw, bh = W, H
    if s.Application.back_buffer_size then
        local okb, a, b = pcall(s.Application.back_buffer_size)
        if okb and type(a) == 'number' and a > 0 then bw, bh = a, b end
    end
    local sc = d.scale or 1
    for i = 0, n - 1 do
        local x, y, z, lit, mark = mbuf[i * 4], mbuf[i * 4 + 1], mbuf[i * 4 + 2] + 0.25, mbuf[i * 4 + 3] > 0.5 and mbuf[i * 4 + 3] < 1.5, mbuf[i * 4 + 3] > 1.5
        local ax, ay, az = x - cx, y - cy, z - cz
        if ax * fx + ay * fy + az * fz > 0.5 then
            local sx, sy = s.Vector3.to_elements(s.Camera.world_to_screen(c, s.Vector3(x, y, z)))
            if type(sx) == 'number' then
                sx, sy = sx * W / bw, H - sy * H / bh
                if sx > 0 and sx < W and sy > 0 and sy < H then
                    local dist = math.sqrt(ax * ax + ay * ay + az * az)
                    local half = math.max(4, math.min(12, 90 / math.max(dist, 1))) * sc
                    local col = lit and {255, 230, 80, math.floor(120 + 135 * (math.sin(mk_t * 20) * 0.5 + 0.5))} or mark and {255, 255, 255, 240} or {224, 58, 58, 235}
                    if mark then half = half * 1.3; d:rect(sx - half * 0.45, sy - half * 0.45, half * 0.9, half * 0.9, {224, 58, 58, 220}, 6) end
                    local tk = math.max(1, 2 * sc)
                    d:rect(sx - half, sy - half, 2 * half, tk, col, 6); d:rect(sx - half, sy + half - tk, 2 * half, tk, col, 6)
                    d:rect(sx - half, sy - half, tk, 2 * half, col, 6); d:rect(sx + half - tk, sy - half, tk, 2 * half, col, 6)
                    d:rect(sx - 2 * sc, sy - 2 * sc, 4 * sc, 4 * sc, col, 6)
                    if dist < 60 then d:text(('%dm'):format(math.floor(dist + 0.5)), sx, sy + half + 2 * sc, {size = 12 * sc, colour = col, z = 6, align = 'center'}) end
                end
            end
        end
    end
end
local mines_failed = false

local overlay = hd2.ui.overlay({id = 'w', owner = OWNER})
local failed = false
overlay:draw(function(d, dt)
    if failed then return end
    local st = connect(dt or 0)
    if not st then return end
    local okF, err = pcall(function()
        if st.in_mission == 0 then
            -- the mission ended: forget them, their units left alone
            if next(held) ~= nil then held = {} end
            last_seq = st.seq
        elseif st.seq ~= last_seq then sync(st) end
    end)
    if not okF then failed = true; log('failed, off for this session: ' .. tostring(err)) return end
    if st.in_mission == 0 then return end
    mk_t = mk_t + (tonumber(dt) or 0)
    if not mines_failed then
        local okM, errM = pcall(mines_draw, d)
        if not okM then mines_failed = true; log('the mines\' markers failed, off for this session: ' .. tostring(errM)) end
    end
    local sc = d.scale or 1
    local text, colour
    local skill, ambush = st.skill % 256, math.floor(st.skill / 256) % 2 == 1
    if skill == 1 then text, colour = ('KING OF HEARTS  %d'):format(math.max(0, math.floor(st.value + 0.5))), {255, 90, 90, 255}
    elseif skill == 3 then text, colour = 'KING OF HEARTS  ...', {255, 200, 120, 255}
    elseif skill == 2 then text, colour = ('King of Hearts  %.0f s'):format(math.max(0, st.value)), {170, 170, 170, 230}
    else text, colour = 'King of Hearts  ready', {255, 255, 255, 230} end
    text = text .. ('   |   Jack in the Box  %d  (%d out)'):format(math.floor(st.skill / 65536) % 256, math.floor(st.skill / 16777216) % 256)
    local marks, cooling = math.floor(st.skill / 512) % 16, math.floor(st.skill / 8192) % 2 == 1
    text = text .. (cooling and '   |   D12  cooldown' or ('   |   D12  %d marked'):format(marks))
    if ambush then text = text .. '   |   AMBUSH' end
    local x, y = d.width / 2, (d.height or 1080 * sc) - 150 * sc
    d:text(text, x + 2, y + 2, {size = 20 * sc, colour = {0, 0, 0, 200}, z = 4, align = 'center'})
    d:text(text, x, y, {size = 20 * sc, colour = colour, z = 5, align = 'center'})
end)
pcall(print, '[W] ready (waiting for w.dll)')
