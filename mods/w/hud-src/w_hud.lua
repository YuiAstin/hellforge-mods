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
    uint64_t player_object; float player_pos[3], value; wk1_target t[48]; } wk1_state;
typedef int (*wk1_log)(unsigned int version, const char *text);
void *wk1_GetModuleHandleA(const char *name) __asm__("GetModuleHandleA");
void *wk1_GetProcAddress(void *module, const char *name) __asm__("GetProcAddress");
]])
local k32 = ffi.load('kernel32')

local state, logfn, pending, lines = nil, nil, {}, 0
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

local function release(s, e, touch)
    local h = held[e]
    held[e] = nil
    if not h or not h.off or not touch then return 0 end
    local okA, alive = pcall(s.Unit.alive, h.u)
    if okA and alive == true then
        local okE = pcall(s.Unit.enable_animation_state_machine, h.u)
        return okE and 1 or 0
    end
    return 0
end

local function sync(st)
    local s = S_()
    last_seq = st.seq
    if type(s) ~= 'table' or not (s.Unit and s.World and s.Application and s.Vector3) then return end
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
    local sc = d.scale or 1
    local text, colour
    local skill, ambush = st.skill % 256, math.floor(st.skill / 256) % 2 == 1
    if skill == 1 then text, colour = ('KING OF HEARTS  %d'):format(math.max(0, math.floor(st.value + 0.5))), {255, 90, 90, 255}
    elseif skill == 3 then text, colour = 'KING OF HEARTS  ...', {255, 200, 120, 255}
    elseif skill == 2 then text, colour = ('King of Hearts  %.0f s'):format(math.max(0, st.value)), {170, 170, 170, 230}
    else text, colour = 'King of Hearts  ready', {255, 255, 255, 230} end
    if ambush then text = text .. '   |   AMBUSH' end
    local x, y = d.width / 2, (d.height or 1080 * sc) - 150 * sc
    d:text(text, x + 2, y + 2, {size = 20 * sc, colour = {0, 0, 0, 200}, z = 4, align = 'center'})
    d:text(text, x, y, {size = 20 * sc, colour = colour, z = 5, align = 'center'})
end)
pcall(print, '[W] ready (waiting for w.dll)')
