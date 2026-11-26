---------------------------------------- KB_HOLD: the pose hold's Lua half and the stunned sign (one table: KBH) ---------
-- The DLL (kb_hold.h) picks the enemies held and times each hold; this finds each one's unit in the game's Lua and
-- switches its animation state machine off while it is held, then on again (the Time Stop Probe's pose freeze, per
-- enemy). One that died held gets its "dead" animation event: with its machine off as it died, the death never started
-- and a big enemy stood dead in its pose, never cleared away (W 0.4.1; every enemy type played has the event). Draws the
-- stunned sign (the HUD image kb_stun, kitbase/hud/images/stun.png) over each held enemy, at the top of its skeleton.
-- The rare-type rule for reading a unit's place is Exo Autopilot's finding (marti). Needs the KBW region above.
-- Lifted 2026-11-26 from W 0.9.0. Use: KBH.step(d, dt) once a frame from the overlay's draw; KBH.state is the DLL's
-- state (nil until found).
local KBH = (function()
    pcall(ffi.cdef, [[
typedef struct kbh_target { uint32_t entity, unit; uint64_t object; float pos[3]; float top; } kbh_target;
typedef struct kbh_state { uint32_t version, seq, count, rare_max; uint32_t player_unit, in_mission; uint64_t player_object;
    float player_pos[3], pad; kbh_target t[48]; uint32_t dead_seq, dead[16]; } kbh_state;
typedef int (*kbh_log)(unsigned int version, const char *text);
]])
    local H = {state = nil, logfn = nil, wait = 0, held = {}, last_seq = -1, dead_seq = 0, dead = {}, mode = nil, shift = 0, low = 0,
        high = 0, can = nil, told = 0, unit_told = false, failed = false, sign_failed = false, t = 0, lines = 0}
    local function hlog(m)
        H.lines = H.lines + 1
        if H.lines > 300 then return end
        m = tostring(m)
        pcall(print, '[' .. KIT.name .. ' HUD] ' .. m)
        if H.logfn then pcall(H.logfn, 1, m) end
    end
    local function connect(dt)
        if H.state ~= nil then return H.state end
        H.wait = H.wait - dt
        if H.wait > 0 then return nil end
        H.wait = 2
        local module = KIT.module(KIT.id .. '.dll')
        if module == nil then return nil end
        local p1, p2 = KIT.proc(module, KIT.id .. '_holds'), KIT.proc(module, KIT.id .. '_hold_log')
        if p1 == nil then return nil end
        local s = ffi.cast('kbh_state *(*)(void)', p1)()
        if s == nil or s.version ~= 1 then return nil end
        if p2 ~= nil then H.logfn = ffi.cast('kbh_log', p2) end
        H.state = s
        hlog('the hold connected to the DLL')
        return s
    end
    local RI
    local function upos(s, u)
        if not RI then
            local okI, off = pcall(function() return s.Script.index_offset() end)
            RI = okI and type(off) == 'number' and off or 1
        end
        local okP, v = pcall(s.Unit.world_position, u, RI)
        if not okP or not v then return nil end
        return s.Vector3.to_elements(v)
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
        local res, count, out = {}, {}, {}
        for i, u in ipairs(all) do
            local okR, r = pcall(s.Unit.resource_name, u)
            r = okR and tostring(r) or '?'
            res[i] = r
            count[r] = (count[r] or 0) + 1
        end
        for i, u in ipairs(all) do if res[i] ~= '?' and count[res[i]] <= most then out[#out + 1] = u end end
        return out
    end
    local function has_machine(s, u)
        local okH, has = pcall(s.Unit.has_animation_state_machine, u)
        return okH and has == true
    end
    -- how a Lua unit value holds the unit, learned once from her own unit: 'object', 'id' or false (by place only)
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
        if P and P == O then H.mode = 'object'
        elseif P then
            for k = 0, 24 do
                if math.floor(P / 2 ^ k) % 2 ^ 32 == U then H.mode, H.shift, H.low, H.high = 'id', k, P % 2 ^ k, math.floor(P / 2 ^ (k + 32)) break end
            end
        end
        if H.mode == nil then H.mode = false end
        hlog('a Lua unit value ' .. (H.mode == 'object' and "is the unit object's address" or H.mode == 'id' and ('holds the unit id at bit ' .. H.shift) or
            'is neither the unit object nor its id: units are found by place only'))
    end
    local function died(s, u)
        if type(s.Unit.has_animation_event) ~= 'function' or type(s.Unit.animation_event) ~= 'function' then return end
        local okH, has = pcall(s.Unit.has_animation_event, u, 'dead')
        local fired = okH and has == true and pcall(s.Unit.animation_event, u, 'dead')
        if H.told < 6 then
            H.told = H.told + 1
            local okR, r = pcall(s.Unit.resource_name, u)
            hlog(('died while held: %s: %s'):format(okR and tostring(r) or '?', fired and 'its "dead" event asked for' or 'no "dead" event'))
        end
    end
    local function release(s, e)
        local h = H.held[e]
        H.held[e] = nil
        if not h or not h.off then return 0 end
        local okA, alive = pcall(s.Unit.alive, h.u)
        if okA and alive == true and pcall(s.Unit.enable_animation_state_machine, h.u) then
            if H.dead[e] then H.dead[e] = nil; pcall(died, s, h.u) end
            return 1
        end
        return 0
    end
    local function sync(st)
        local s = KBW.S()
        H.last_seq = st.seq
        if type(s) ~= 'table' or not (s.Unit and s.World and s.Application and s.Vector3) then return end
        if H.can == nil then
            H.can = type(s.Unit.disable_animation_state_machine) == 'function' and type(s.Unit.enable_animation_state_machine) == 'function'
            if not H.can then hlog("Unit.disable_animation_state_machine is not in this game's Lua: no pose hold") end
        end
        if not H.can then return end
        while H.dead_seq ~= st.dead_seq do
            if st.dead_seq - H.dead_seq > 16 then H.dead_seq = st.dead_seq - 16 end
            H.dead[st.dead[H.dead_seq % 16]] = true
            H.dead_seq = H.dead_seq + 1
        end
        local want, fresh = {}, {}
        for k = 0, math.min(st.count, 48) - 1 do
            local t = st.t[k]
            want[t.entity] = true
            if not H.held[t.entity] then fresh[#fresh + 1] = {e = t.entity, unit = t.unit, object = tonumber(t.object), x = t.pos[0], y = t.pos[1], z = t.pos[2]} end
        end
        local on = 0
        for e in pairs(H.held) do if not want[e] then on = on + release(s, e) end end
        if #fresh == 0 then return end
        local okW, w = pcall(s.Application.main_world)
        if not okW or not w then return end
        local okU, all = pcall(s.World.units, w)
        if not okU or type(all) ~= 'table' then return end
        local rare
        if H.mode == nil then rare = rare_units(s, all, st.rare_max); learn_mode(s, st, rare) end
        local found, off = 0, 0
        if H.mode then
            local byptr = {}
            for _, u in ipairs(all) do
                local p = uptr(u)
                if p then byptr[p] = u end
            end
            for _, t in ipairs(fresh) do
                local key = H.mode == 'object' and t.object or (H.high * 2 ^ (H.shift + 32) + t.unit * 2 ^ H.shift + H.low)
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
            if t.u and has_machine(s, t.u) and pcall(s.Unit.disable_animation_state_machine, t.u) then h.off = true; off = off + 1 end
            H.held[t.e] = h
        end
        hlog(('hold %d: %d new (%d units found, %d held in pose), %d let go, %d held in all'):format(st.seq, #fresh, found, off, on, st.count))
    end
    local SIGN
    do
        local okI, handle = pcall(hd2.resources.image, 'kb_stun')
        if okI and handle then SIGN = handle end
    end
    -- the stunned sign over its highest part, with room; 2.6 m up when its skeleton was not read
    local function signs(d, st)
        local n = tonumber(st.count)
        if not SIGN or n <= 0 then return end
        local v = KBW.view(d, H.t)
        if not v then return end
        local sc = d.scale or 1
        for k = 0, math.min(n, 48) - 1 do
            local t = st.t[k]
            local top = tonumber(t.top)
            local sx, sy, dist = KBW.project(v, t.pos[0], t.pos[1], t.pos[2] + (top > 0 and top + math.max(0.45, top * 0.12) or 2.6))
            if sx then
                local r = math.max(12, math.min(34, 420 / math.max(dist, 1))) * sc
                local sway = math.sin(H.t * 3 + k * 1.7)   -- a slow sway, each one out of step with the next
                d:image(SIGN, sx - r + sway * r * 0.08, sy - r, 2 * r, 2 * r, {z = 6, colour = {255, 255, 255, math.floor(215 + 40 * sway)}})
            end
        end
    end
    local function step(d, dt)
        dt = tonumber(dt) or 0
        H.t = H.t + dt
        local st = connect(dt)
        if not st then return end
        if not H.failed then
            local ok, err = pcall(function()
                if st.in_mission == 0 then
                    if next(H.held) ~= nil then H.held = {} end   -- the mission ended: their units are left alone
                    H.last_seq = st.seq
                elseif st.seq ~= H.last_seq then sync(st) end
            end)
            if not ok then H.failed = true; hlog('the hold failed, off for this session: ' .. tostring(err)) end
        end
        if st.in_mission ~= 0 and not H.sign_failed then
            local ok, err = pcall(signs, d, st)
            if not ok then H.sign_failed = true; hlog('the stunned signs failed, off for this session: ' .. tostring(err)) end
        end
    end
    return setmetatable({step = step}, {__index = function(_, k) if k == 'state' then return H.state end end})
end)()
