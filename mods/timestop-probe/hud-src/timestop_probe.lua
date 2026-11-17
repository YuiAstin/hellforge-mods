-- Time Stop Probe (mods/yui/timestop_probe): the Lua half of the HellForge probe yui.timestop-probe. A probe, not a
-- release. The DLL (timestop_probe.dll) picks the enemies and times the stop; this finds each enemy's unit in the game's
-- Lua and switches its animation state machine off for the stop, then on again. Everything it learns is written to the
-- DLL's log (mod-yui.timestop-probe.log) through timestop_probe_log.
-- Requires Bingus Shared Loader and HD2Runtime (by SkyeShade, https://github.com/SkyeShade/HD2Runtime; its overlay
-- gives the frame callback). The rare-type rule for reading a unit's place is Exo Autopilot's finding (marti).
local OWNER = 'mods/yui/timestop_probe'

local ok, hd2 = pcall(require, 'mods/skyeshade/hd2runtime')
if not ok or type(hd2) ~= 'table' or type(hd2.ui) ~= 'table' or type(hd2.ui.overlay) ~= 'function' then
    pcall(print, '[Time Stop Probe] HD2Runtime with hd2.ui.overlay is not available: the pose part is off')
    return
end

local ffi = require('ffi')
pcall(ffi.cdef, [[
typedef struct tsp1_target { uint32_t entity, unit; uint64_t object; float pos[3]; uint32_t pad; } tsp1_target;
typedef struct tsp1_state { uint32_t version, seq, active, method, count, rare_max; uint32_t player_unit, in_mission;
    uint64_t player_object; float player_pos[3], left; tsp1_target t[48]; } tsp1_state;
typedef int (*tsp1_log)(unsigned int version, const char *text);
void *tsp1_GetModuleHandleA(const char *name) __asm__("GetModuleHandleA");
void *tsp1_GetProcAddress(void *module, const char *name) __asm__("GetProcAddress");
]])
local k32 = ffi.load('kernel32')

local state, logfn, pending = nil, nil, {}
local function log(m)
    m = tostring(m)
    pcall(print, '[Time Stop Probe] ' .. m)
    if logfn then pcall(logfn, 1, m) elseif #pending < 200 then pending[#pending + 1] = m end
end
local find_wait = 0
local function connect(dt)
    if state ~= nil then return state end
    find_wait = find_wait - dt
    if find_wait > 0 then return nil end
    find_wait = 2
    local module = k32.tsp1_GetModuleHandleA('timestop_probe.dll')
    if module == nil then return nil end
    local p1, p2 = k32.tsp1_GetProcAddress(module, 'timestop_probe_state'), k32.tsp1_GetProcAddress(module, 'timestop_probe_log')
    if p1 == nil or p2 == nil then return nil end
    local s = ffi.cast('tsp1_state *(*)(void)', p1)()
    if s == nil or s.version ~= 1 then return nil end
    logfn = ffi.cast('tsp1_log', p2)
    state = s
    for _, m in ipairs(pending) do pcall(logfn, 1, m) end
    pending = {}
    log('connected to the DLL')
    return state
end

local function S_() return rawget(_G, 'stingray') end

-- ---- once: what this game's Lua has (the names only; nothing is called) ----
local dumped = false
local WORDS = {'anim', 'time', 'pause', 'speed', 'scale', 'step', 'freez', 'froz', 'delta', 'simulat'}
local function names_of(t)
    local out = {}
    for k, v in pairs(t) do if type(k) == 'string' and type(v) == 'function' then out[#out + 1] = k end end
    table.sort(out)
    return out
end
local function log_list(title, list)
    local line = title
    for _, n in ipairs(list) do
        if #line + #n + 1 > 330 then log(line); line = title end
        line = line .. ' ' .. n
    end
    if line ~= title then log(line) end
end
local function dump()
    dumped = true
    local s = S_()
    if type(s) ~= 'table' then log('no stingray table in this Lua: the pose part cannot run') return end
    if type(s.Unit) == 'table' then
        local all = names_of(s.Unit)
        log(('stingray.Unit has %d functions'):format(#all))
        log_list('Unit:', all)
    else log('no stingray.Unit') end
    for name, t in pairs(s) do
        if type(name) == 'string' and name ~= 'Unit' and type(t) == 'table' then
            local hits = {}
            for _, n in ipairs(names_of(t)) do
                local l = n:lower()
                for _, w in ipairs(WORDS) do if l:find(w, 1, true) then hits[#hits + 1] = n break end end
            end
            if #hits > 0 then log_list(name .. ':', hits) end
        end
    end
end

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
local function hex(n)
    if not n then return '?' end
    local hi, lo = math.floor(n / 2 ^ 32), n % 2 ^ 32
    return hi > 0 and ('%x%08x'):format(hi, lo) or ('%x'):format(lo)
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
local function anim_layers(s, u)
    local okH, has = pcall(s.Unit.has_animation_state_machine, u)
    if not okH then return nil, 'has_animation_state_machine failed: ' .. tostring(has) end
    if has ~= true then return 0 end
    local r = {pcall(s.Unit.animation_get_state, u)}
    if not r[1] then return nil, 'animation_get_state failed: ' .. tostring(r[2]) end
    return #r - 1
end

local cur = nil      -- the stop being held: {seq, method, units = {{u, k}}, off = n}
local screen = ''

local function begin(st)
    local s = S_()
    cur = {seq = st.seq, method = st.method, units = {}, off = 0}
    screen = ('STOP %d: method %d'):format(st.seq, st.method)
    if type(s) ~= 'table' or not (s.Unit and s.World and s.Application and s.Vector3) then log('stop ' .. st.seq .. ': no stingray Unit / World / Application here') return end
    local okW, w = pcall(s.Application.main_world)
    if not okW or not w then log('stop ' .. st.seq .. ': no main world: ' .. tostring(w)) return end
    local okU, all = pcall(s.World.units, w)
    if not okU or type(all) ~= 'table' then log('stop ' .. st.seq .. ': World.units failed: ' .. tostring(all)) return end
    local n = st.count
    local targets = {}
    for k = 0, n - 1 do
        local t = st.t[k]
        targets[#targets + 1] = {k = k, unit = t.unit, object = tonumber(t.object), x = t.pos[0], y = t.pos[1], z = t.pos[2]}
    end
    -- every unit by its Lua value
    local byptr, nptr = {}, 0
    for _, u in ipairs(all) do
        local p = uptr(u)
        if p then byptr[p] = u; nptr = nptr + 1 end
    end
    local rare = rare_units(s, all, st.rare_max)
    log(('stop %d: the world has %d units (%d with a readable Lua value, %d of rare types); a unit is a %s, prints as %s'):format(st.seq, #all, nptr, #rare,
        type(all[1]), tostring(all[1])))
    -- the anchor: the player's own unit, found by its place among the rare types; how its Lua value holds its unit id
    local px, py, pz = st.player_pos[0], st.player_pos[1], st.player_pos[2]
    local anchor, best, near = nil, 0.09, 0
    for _, u in ipairs(rare) do
        local x, y, z = upos(s, u)
        if x then
            local d2 = (x - px) ^ 2 + (y - py) ^ 2 + (z - pz) ^ 2
            if d2 < 0.09 then near = near + 1 end
            if d2 < best then
                local layers = anim_layers(s, u)
                if layers and layers > 0 then anchor, best = u, d2 end
            end
        end
    end
    local mode, shift, low, high = nil, 0, 0, 0
    if anchor then
        local P, U, O = uptr(anchor), st.player_unit, tonumber(st.player_object)
        log(('the player\'s unit in Lua: value %s (%s), unit id %08x, object %s; %d rare units within 0.3 m'):format(hex(P),
            tostring(anchor), U, hex(O), near))
        if P and P == O then mode = 'object'
        elseif P then
            for k = 0, 24 do
                if math.floor(P / 2 ^ k) % 2 ^ 32 == U then mode, shift, low, high = 'id', k, P % 2 ^ k, math.floor(P / 2 ^ (k + 32)) break end
            end
        end
        if mode == 'object' then log('a Lua unit value is the unit object\'s address')
        elseif mode == 'id' then log(('a Lua unit value holds the unit id at bit %d (low bits %s, high bits %s)'):format(shift, hex(low), hex(high)))
        else log('a Lua unit value is neither the unit object\'s address nor its id shifted: units are found by place only') end
    else
        log(('the player\'s unit was not found by its place (%d rare units within 0.3 m, none with a state machine): units are found by place only'):format(near))
    end
    -- the targets' units
    local found, how = 0, mode or 'place'
    if mode then
        for _, t in ipairs(targets) do
            local key = mode == 'object' and t.object or (high * 2 ^ (shift + 32) + t.unit * 2 ^ shift + low)
            local u = byptr[key]
            if u then t.u = u; found = found + 1 end
        end
    end
    if found < #targets then
        local byplace = 0
        for _, u in ipairs(rare) do
            local x, y, z = upos(s, u)
            if x then
                for _, t in ipairs(targets) do
                    if not t.u and (x - t.x) ^ 2 + (y - t.y) ^ 2 + (z - t.z) ^ 2 < 0.25 then
                        local layers = anim_layers(s, u)
                        if layers and layers > 0 then t.u = u; found = found + 1; byplace = byplace + 1 end
                        break
                    end
                end
            end
        end
        if byplace > 0 then how = how .. (mode and (' and place (' .. byplace .. ')') or '') end
    end
    log(('stop %d: %d of %d enemies\' units found (by %s)'):format(st.seq, found, #targets, how))
    -- the state machine
    local told = 0
    for _, t in ipairs(targets) do
        if t.u then
            local layers, why = anim_layers(s, t.u)
            if told < 6 then
                told = told + 1
                log(('  target %d: %s, state machine %s'):format(t.k, tostring(t.u), layers and (layers > 0 and (layers .. ' layers') or 'none') or tostring(why)))
            end
            if layers and layers > 0 then cur.units[#cur.units + 1] = {u = t.u, k = t.k} end
        end
    end
    screen = ('STOP %d: method %d, %d of %d units found'):format(st.seq, st.method, found, #targets)
    if st.method == 1 then return end
    if type(s.Unit.disable_animation_state_machine) ~= 'function' or type(s.Unit.enable_animation_state_machine) ~= 'function' then
        log('Unit.disable_animation_state_machine / enable_animation_state_machine are not in this game\'s Lua: no pose freeze this way')
        cur.units = {}
        return
    end
    local errs = 0
    for _, e in ipairs(cur.units) do
        local okD, err = pcall(s.Unit.disable_animation_state_machine, e.u)
        if okD then e.off = true; cur.off = cur.off + 1
        elseif errs < 3 then errs = errs + 1; log('  disable_animation_state_machine failed on target ' .. e.k .. ': ' .. tostring(err)) end
    end
    log(('stop %d: the state machine switched off on %d of %d'):format(st.seq, cur.off, #cur.units))
    screen = screen .. (', %d held in pose'):format(cur.off)
end

local function finish(st, touch)
    local c = cur
    cur = nil
    screen = ''
    if not c or c.off == 0 then return end
    if not touch then log(('stop %d: the mission ended, its units left alone'):format(c.seq)) return end
    local s = S_()
    local on, gone, errs = 0, 0, 0
    for _, e in ipairs(c.units) do
        if e.off then
            local okA, alive = pcall(s.Unit.alive, e.u)
            if okA and alive == true then
                local okE, err = pcall(s.Unit.enable_animation_state_machine, e.u)
                if okE then on = on + 1 elseif errs < 3 then errs = errs + 1; log('  enable_animation_state_machine failed on target ' .. e.k .. ': ' .. tostring(err)) end
            else gone = gone + 1 end
        end
    end
    log(('stop %d: the state machine switched on again on %d (%d units gone)'):format(c.seq, on, gone))
end

local overlay = hd2.ui.overlay({id = 'probe', owner = OWNER})
local failed = false
overlay:draw(function(d, dt)
    if failed then return end
    local st = connect(dt or 0)
    if not st then return end
    local okF, err = pcall(function()
        if not dumped then dump() end
        if cur and (st.in_mission == 0 or st.active == 0 or st.seq ~= cur.seq) then finish(st, st.in_mission ~= 0) end
        if st.in_mission ~= 0 and st.active ~= 0 and not cur then
            local okB, why = pcall(begin, st)
            if not okB then log('stop ' .. st.seq .. ': finding or holding the units failed: ' .. tostring(why)) end
        end
    end)
    if not okF then failed = true; log('failed, off for this session: ' .. tostring(err)) return end
    if cur and screen ~= '' then
        local sc = d.scale or 1
        local text = ('%s  (%.1f s)'):format(screen, math.max(0, st.left))
        d:text(text, d.width / 2 + 2, 120 * sc + 2, {size = 22 * sc, colour = {0, 0, 0, 200}, z = 4, align = 'center'})
        d:text(text, d.width / 2, 120 * sc, {size = 22 * sc, colour = {255, 255, 255, 255}, z = 5, align = 'center'})
    end
end)
pcall(print, '[Time Stop Probe] ready (waiting for timestop_probe.dll)')
