-- Cloak Probe HUD (mods/yui/cloak_probe_hud): the material side of the Cloak Probe (research; see cloak_probe.c).
-- Reads cloak_probe.dll's cloak_probe_state every frame; on its key counts it:
--   F12        logs the engine's mesh / material calls, then the nearest Stalker's unit: its meshes and materials
--   Ctrl+F12   first press: fades your own materials (opacity, opacity_stealth); second: the Stalker's materials if one
--              is known, else every mesh of every part hidden
--   Shift+F12  puts your own back (the materials it saved first)
-- Uses HD2Runtime by SkyeShade through its public API (hd2.ui.overlay as a frame hook) and the engine's Lua API.
local OWNER = 'mods/yui/cloak_probe_hud'
local sink, sink_tried
local function stamp() local ok, t = pcall(function() return os.date('%H:%M:%S') end); return ok and tostring(t) or '' end
local function log(message)
    if type(message) == 'string' and message:sub(1, 13) == '[Cloak Probe]' and not message:find('^%[Cloak Probe%]   ') then message = '[Cloak Probe] ' .. stamp() .. message:sub(14) end
    pcall(print, message)
    if not sink_tried then
        sink_tried = true
        local ok, result = pcall(function() return CowboyBingusModLoader.open_log('CloakProbe.log') end)
        if ok then sink = result end
    end
    if sink then pcall(function() sink:write(message .. '\n'); if sink.flush then sink:flush() end end) end
end
local ok, hd2 = pcall(require, 'mods/skyeshade/hd2runtime')
if not ok or type(hd2) ~= 'table' or type(hd2.ui) ~= 'table' or type(hd2.ui.overlay) ~= 'function' then
    log('[Cloak Probe] HD2Runtime with hd2.ui.overlay is not available')
    return
end
local ffi = require('ffi')
pcall(ffi.cdef, [[
void *clkhud1_GetModuleHandleA(const char *name) __asm__("GetModuleHandleA");
void *clkhud1_GetProcAddress(void *module, const char *name) __asm__("GetProcAddress");
typedef int (*clkstate1)(unsigned int version, float *out);
]])
local k32 = ffi.load('kernel32')
local S = rawget(_G, 'stingray') or _G
local function lib(name) local t = rawget(_G, name) or (type(S) == 'table' and S[name]) or nil; return type(t) == 'table' and t or nil end
local function pc(f, ...) if type(f) ~= 'function' then return false, 'missing' end return pcall(f, ...) end

local st = {fn = nil, buf = ffi.new('float[12]'), seen = {-1, -1, -1}, saved = nil}

local function xyz(v)
    if v == nil then return nil end
    local V = lib('Vector3')
    local okv, x, y, z = pcall(function() return V.to_elements(v) end)
    if okv and x then return x, y, z end
    local ok2, a, b, c = pcall(function() return v.x, v.y, v.z end)
    if ok2 then return a, b, c end
end
-- the world's units of rare types only (at most 4 of a type), as Exo Autopilot (marti) found: reading the position of
-- a unit of a mass-instanced type crashed the game (helldivers2.exe+0x9E27E, a read at 0x30)
-- 0.1.10: every world (Application.worlds, as the Optical Cloak Pack walks them), and types of up to 64 units, not 4:
-- 0.1.9 found 5 units at the root and none was the body. The crash that set the limit of 4 (a root read on a unit
-- with no scene graph) is guarded instead: root() asks Unit.num_scene_graph_items first, as the pack does.
local TYPE_LIMIT = 64
local function rare_units(s, w)
    local worlds = {}
    local okws, ws = pcall(function() return s.Application.worlds() end)
    if okws and type(ws) == 'table' and #ws > 0 then worlds = ws else worlds = {w} end
    local units, res, count, total = {}, {}, {}, 0
    for _, world in ipairs(worlds) do
        local oku, all = pcall(s.World.units, world)
        if oku and type(all) == 'table' then
            for _, u in ipairs(all) do
                local okr, r = pcall(s.Unit.resource_name, u)
                r = okr and tostring(r) or '?'
                units[#units + 1] = u; res[#units] = r
                count[r] = (count[r] or 0) + 1
                total = total + 1
            end
        end
    end
    if total == 0 then return nil end
    local out = {}
    for i, u in ipairs(units) do if count[res[i]] <= TYPE_LIMIT and res[i] ~= '?' then out[#out + 1] = u end end
    return out, total, #worlds, count
end
local function root(U, u, ri)
    local okn, n = pcall(U.num_scene_graph_items, u)
    if not okn or type(n) ~= 'number' or n < 1 or n > 10000 then return false, nil end
    return pcall(U.world_position, u, ri)
end
local function node_index()
    local s = rawget(_G, 'stingray')
    local ok, off = pcall(function() return s.Script.index_offset() end)
    return ok and type(off) == 'number' and off or 1
end
local RI = node_index()
-- the unit nearest a point (World.units, then each unit's root position)
local function unit_near(px, py, pz, within)
    local App, W, U = lib('Application'), lib('World'), lib('Unit')
    if not (App and W and U) then return nil, 'Application / World / Unit missing' end
    local okw, world = pc(App.main_world)
    if not okw or not world then return nil, 'no main world' end
    local all, total = rare_units(S, world)
    if not all then return nil, 'World.units unavailable' end
    local best, bd = nil, within * within
    for _, u in ipairs(all) do
        local okp, v = root(U, u, RI)
        local x, y, z
        if okp then x, y, z = xyz(v) end            -- 0.1.1: (okp and xyz(v)) kept only x
        if x then
            local d2 = (x - px) ^ 2 + (y - py) ^ 2 + (z - pz) ^ 2
            if d2 < bd then best, bd = u, d2 end
        end
    end
    return best, best and math.sqrt(bd) or ('no unit within ' .. within .. ' m of ' .. #all .. ' rare-type units (' .. tostring(total) .. ' in all)')
end
-- 0.1.11: the Helldiver by its type, not by "the unit nearest you": 0.1.10's nearest unit was a piece of the ground
-- that follows you (hiding it hid the ground), and the one unit of the avatar type was not within 3 m of it.
local AVATAR = '4d1c334d294dfa97'
local function avatar_unit(px, py, pz, verbose)
    local App, U = lib('Application'), lib('Unit')
    local okw, world = pc(App.main_world)
    local all = rare_units(S, okw and world or nil)
    if not all then return nil end
    local best, bd, seen, bx, by, bz = nil, math.huge, 0, nil, nil, nil
    for _, u in ipairs(all) do
        local okr, r = pcall(U.resource_name, u)
        if okr and tostring(r):lower():find(AVATAR, 1, true) then
            seen = seen + 1
            local okp, v = root(U, u, RI)
            local x, y, z
            if okp then x, y, z = xyz(v) end
            if x then
                local d2 = (x - px) ^ 2 + (y - py) ^ 2 + (z - pz) ^ 2
                if d2 < bd then best, bd, bx, by, bz = u, d2, x, y, z end
            elseif verbose then
                local okn, n = pcall(U.num_scene_graph_items, u)
                log(('[Cloak Probe]   an avatar-type unit has no readable root (scene graph items: %s)'):format(okn and tostring(n) or '?'))
            end
        end
    end
    if verbose then
        log(('[Cloak Probe] avatar: %d units of the type; you are at (%.1f, %.1f, %.1f) by the DLL; the nearest one\'s root is %s'):format(seen, px, py, pz,
            best and ('(%.1f, %.1f, %.1f), %.2f m away'):format(bx, by, bz, math.sqrt(bd)) or 'unreadable'))
    end
    return best
end
local function api_list()
    for _, name in ipairs({'Unit', 'Mesh', 'Material'}) do
        local t = lib(name)
        local names = {}
        if t then for k, v in pairs(t) do if type(v) == 'function' and (name ~= 'Unit' or tostring(k):find('[Mm]ateri') or tostring(k):find('mesh') or tostring(k):find('visib') or tostring(k):find('shader')) then names[#names + 1] = tostring(k) end end end
        table.sort(names)
        log(('[Cloak Probe] %s: %s'):format(name, t and table.concat(names, ', ') or 'MISSING'))
    end
end
local function describe(u, label)
    local U, M = lib('Unit'), lib('Mesh')
    local okn, n = pc(U.num_meshes, u)
    log(('[Cloak Probe] %s: %s meshes'):format(label, okn and tostring(n) or ('? ' .. tostring(n))))
    if not okn or type(n) ~= 'number' then return end
    for i = 1, math.min(n, 40) do
        local okm, mesh = pc(U.mesh, u, i)
        local okc, c = false, nil
        if okm then okc, c = pc(M and M.num_materials, mesh) end   -- 0.1.3: 'okm and pc(...)' kept only pc's first result (every count read nil)
        local mats = {}
        if okc and type(c) == 'number' then
            for j = 1, math.min(c, 8) do
                local okt, mat = pc(M.material, mesh, j)
                mats[#mats + 1] = okt and tostring(mat) or ('?' .. tostring(mat))
            end
        end
        log(('[Cloak Probe]   mesh %d: %s, %s materials: %s'):format(i, okm and tostring(mesh) or '?', okc and tostring(c) or '?', table.concat(mats, ' | ')))
    end
end
-- every way the engine offers to put another unit's material on a mesh, tried in order; the first that works is used
local function copy_materials(me, stalker)
    local U, M = lib('Unit'), lib('Mesh')
    local okn, n = pc(U.num_meshes, me)
    local oks, ns = true, 0
    if stalker then oks, ns = pc(U.num_meshes, stalker) end
    if not (okn and oks) then return 0 end
    -- the Stalker's body materials (its first mesh with materials; 0.1.1: else the ones kept)
    local src = {}
    if not stalker then src = st.cached or {} end
    for i = 1, ns do
        local okm, mesh = pc(U.mesh, stalker, i)
        local okc, c = false, nil
        if okm then okc, c = pc(M.num_materials, mesh) end   -- 0.1.3: 'okm and pc(...)' kept only pc's first result (every count read nil)
        if okc and type(c) == 'number' and c > 0 then
            for j = 1, c do local okt, mat = pc(M.material, mesh, j); if okt and mat then src[#src + 1] = mat end end
        end
        if #src > 0 then break end
    end
    if #src == 0 then return 0 end
    st.cached = src
    -- 0.1.4: Mesh has no set_material here (0.1.3: "missing" on all 31 slots). The engine's own is Unit.set_material(unit,
    -- slot, material resource): each way of naming the slot and the Stalker's material is tried on the first slot and
    -- logged; the first that works does the rest. If none does, the whole unit is hidden (Unit.set_unit_visibility).
    local Mat = lib('Material')
    st.saved = st.saved or {}
    local function ids(mat)
        local out = {}
        local ok1, a = pc(Mat and Mat.id64, mat); if ok1 and a ~= nil then out[#out + 1] = {'Material.id64', a} end
        local ok2, b2 = pc(Mat and Mat.id, mat); if ok2 and b2 ~= nil then out[#out + 1] = {'Material.id', b2} end
        out[#out + 1] = {'the material itself', mat}
        return out
    end
    local done, failed, how, tried = 0, 0, st.how, st.tried or 0
    for i = 1, n do
        local okm, mesh = pc(U.mesh, me, i)
        local okc, c = false, nil
        if okm then okc, c = pc(M.num_materials, mesh) end
        if okc and type(c) == 'number' then
            for j = 1, c do
                local okt, mine = pc(M.material, mesh, j)
                local oksl, slot = pc(M.material_slot_id, mesh, j)
                local mat = src[((j - 1) % #src) + 1]
                local slots = {}
                if oksl and slot ~= nil then slots[#slots + 1] = {'Mesh.material_slot_id', slot} end
                slots[#slots + 1] = {'the slot index', j}
                local set = false
                if how then
                    local sv = how.slot == 'Mesh.material_slot_id' and slot or j
                    local mv = mat
                    if how.mat == 'Material.id64' then local _, v = pc(Mat.id64, mat); mv = v elseif how.mat == 'Material.id' then local _, v = pc(Mat.id, mat); mv = v end
                    set = pc(U.set_material, me, sv, mv)
                else
                    for _, sl in ipairs(slots) do
                        for _, id in ipairs(ids(mat)) do
                            local okset, err = pc(U.set_material, me, sl[2], id[2])
                            if okset then local okb, back = pc(M.material, mesh, j); local ok1, i1 = pc(Mat and Mat.id, mine); local ok2, i2 = false, nil; if okb then ok2, i2 = pc(Mat and Mat.id, back) end; if ok1 and ok2 and tostring(i1) == tostring(i2) then okset, err = false, 'no error, but the material did not change' end end
                            tried = tried + 1
                            if tried <= 8 then log(('[Cloak Probe]   Unit.set_material(me, %s = %s, %s = %s): %s'):format(sl[1], tostring(sl[2]), id[1], tostring(id[2]), okset and 'ok' or tostring(err))) end
                            if okset then how = {slot = sl[1], mat = id[1]}; set = true; break end
                        end
                        if set then break end
                    end
                end
                if set then
                    done = done + 1
                    if okt then
                        local okid, myid = pc(Mat and Mat.id64, mine)
                        st.saved[#st.saved + 1] = {unit = me, slot = (how.slot == 'Mesh.material_slot_id') and slot or j, mat = (how.mat == 'Material.id64' and okid) and myid or mine}
                    end
                else failed = failed + 1 end
            end
        end
    end
    log(('[Cloak Probe] cloak: %d material slots set (%s), %d failed'):format(done, how and (how.slot .. ' + ' .. how.mat) or 'no way worked', failed))
    st.how, st.tried = how, tried
    return done
end
-- 0.1.5, as the Optical Cloak Pack (CAMO-EX01) does it: a Helldiver is not one unit. The avatar and every armour part
-- (body pieces, helmet, cape) are units of their own sharing one root position; its "compatibility" cloak hides each
-- visible mesh of each (Unit.set_mesh_visibility) and keeps what it hid to show again.
local function parts_of(me)
    local App, W, U = lib('Application'), lib('World'), lib('Unit')
    local okw, world = pc(App.main_world)
    local okp, v = root(U, me, RI)
    local mx, my, mz
    if okp then mx, my, mz = xyz(v) end
    if not (okw and world and mx) then return {me} end
    local out = {}
    for _, u in ipairs(rare_units(S, world) or {}) do
        local ok2, v2 = root(U, u, RI)
        local x, y, z
        if ok2 then x, y, z = xyz(v2) end
        if x and (x - mx) ^ 2 + (y - my) ^ 2 + (z - mz) ^ 2 < 0.0004 then out[#out + 1] = u end
    end
    if #out == 0 then out[1] = me end
    return out
end
local function hide_parts(parts, quiet)
    local U, M = lib('Unit'), lib('Mesh')
    st.hidden = st.hidden or {}
    local hid, units = 0, 0
    for _, u in ipairs(parts) do
        local okn, n = pc(U.num_meshes, u)
        local any = false
        for i = 1, (okn and type(n) == 'number' and n <= 512) and n or 0 do
            local okm, mesh = pc(U.mesh, u, i)
            local okv, vis = false, nil
            if okm then okv, vis = pc(M.visibility, mesh) end
            if okm and (not okv or vis == true) then
                if pc(U.set_mesh_visibility, u, i, false) then
                    local key = tostring(u) .. ':' .. i
                    st.hidkeys = st.hidkeys or {}
                    if not st.hidkeys[key] then st.hidkeys[key] = true; st.hidden[#st.hidden + 1] = {unit = u, mesh = i, key = tostring(u)} end
                    hid = hid + 1; any = true
                end
            end
        end
        if any then units = units + 1 end
    end
    if not quiet then log(('[Cloak Probe] cloak: %d meshes of %d units hidden (Ctrl+F12 or Shift+F12 shows them again)'):format(hid, units)) end
    return hid
end
local function restore()
    local U = lib('Unit')
    if st.hidden and #st.hidden > 0 then
        local n = 0
        local live = {}
        local okw, world = pc(lib('Application').main_world)
        for _, u in ipairs(rare_units(S, okw and world or nil) or {}) do live[tostring(u)] = true end
        for _, h in ipairs(st.hidden) do if live[h.key] and pc(U.set_mesh_visibility, h.unit, h.mesh, true) then n = n + 1 end end
        log(('[Cloak Probe] uncloak: %d of %d meshes shown again'):format(n, #st.hidden))
    end
    st.hidden = nil; st.hidkeys = nil; st.gear = nil; st.origin = nil
    if not st.saved or #st.saved == 0 then log('[Cloak Probe] uncloak: no material to put back'); st.saved = nil; return end
    local n = 0
    for _, s in ipairs(st.saved) do if pc(U.set_material, s.unit, s.slot, s.mat) then n = n + 1 end end
    log(('[Cloak Probe] uncloak: %d of %d slots put back'):format(n, #st.saved))
    st.saved = nil
end

-- 0.1.6: the cheapest cloak there could be: the Helldiver's own materials, faded. Sets the two inputs the Optical Cloak
-- Pack drives on its material (opacity, opacity_stealth) on every material of every part. Whether the armour's shader
-- has them is the question: the calls can succeed and change nothing, so the answer is what you see.
-- 0.1.8: with a model mod (IAS) the body you see is not one of the units at the avatar's root: F12 lists every
-- rare-type unit within 3 m of you (type, distance from your root, meshes, how many are visible) to find it.
local function list_near(me)
    local App, U, M = lib('Application'), lib('Unit'), lib('Mesh')
    local okw, world = pc(App.main_world)
    local okp, v = root(U, me, RI)
    local mx, my, mz
    if okp then mx, my, mz = xyz(v) end
    if not (okw and world and mx) then log('[Cloak Probe] near: no world or root') return end
    local all, total, nworlds, count = rare_units(S, world)
    local rows = {}
    for _, u in ipairs(all or {}) do
        local ok2, v2 = root(U, u, RI)
        local x, y, z
        if ok2 then x, y, z = xyz(v2) end
        if x then
            local d = math.sqrt((x - mx) ^ 2 + (y - my) ^ 2 + (z - mz) ^ 2)
            if d < 3 then
                local okn, n = pc(U.num_meshes, u)
                local vis, mats = 0, 0
                for i = 1, (okn and type(n) == 'number' and n <= 512) and n or 0 do
                    local okm, mesh = pc(U.mesh, u, i)
                    if okm then
                        local okv, vv = pc(M.visibility, mesh); if okv and vv == true then vis = vis + 1 end
                        local okc, c = pc(M.num_materials, mesh); if okc and type(c) == 'number' then mats = mats + c end
                    end
                end
                local okd, name = pc(U.debug_name, u)
                rows[#rows + 1] = {d = d, text = ('%s  %.2f m  %s meshes (%d visible), %d materials'):format(okd and tostring(name) or '?', d, okn and tostring(n) or '?', vis, mats)}
            end
        end
    end
    table.sort(rows, function(a, b2) return a.d < b2.d end)
    local av = 0
    for r, c in pairs(count or {}) do if tostring(r):lower():find('4d1c334d294dfa97', 1, true) then av = av + c end end
    log(('[Cloak Probe] near: %d units within 3 m (types of up to %d; %s units in %s worlds; %d of the avatar type)'):format(#rows, TYPE_LIMIT, tostring(total), tostring(nworlds), av))
    for i = 1, math.min(#rows, 60) do log('[Cloak Probe]   ' .. rows[i].text) end
end
local function fade_own(me, value)
    local U, M, Mat = lib('Unit'), lib('Mesh'), lib('Material')
    local parts = parts_of(me)
    local mats, ok, firsterr = 0, {opacity = 0, opacity_stealth = 0}, nil
    for _, u in ipairs(parts) do
        local okn, n = pc(U.num_meshes, u)
        for i = 1, (okn and type(n) == 'number' and n <= 512) and n or 0 do
            local okm, mesh = pc(U.mesh, u, i)
            local okc, c = false, nil
            if okm then okc, c = pc(M.num_materials, mesh) end
            for j = 1, (okc and type(c) == 'number' and c <= 64) and c or 0 do
                local okt, mat = pc(M.material, mesh, j)
                if okt and mat then
                    mats = mats + 1
                    for _, name in ipairs({'opacity', 'opacity_stealth'}) do
                        local oks, err = pc(Mat and Mat.set_scalar, mat, name, value)
                        if oks then ok[name] = ok[name] + 1 elseif not firsterr then firsterr = name .. ': ' .. tostring(err) end
                    end
                end
            end
        end
    end
    log(('[Cloak Probe] fade: %d units, %d materials; set to %.2f: opacity on %d, opacity_stealth on %d%s'):format(#parts, mats, value, ok.opacity, ok.opacity_stealth, firsterr and (' (first error: ' .. firsterr .. ')') or ''))
end
local function cloak(me, stalker)
    local parts = parts_of(me)
    -- 0.1.7: the Stalker-material trials are gone (0.1.6: nothing they tried took, and the game crashed in its renderer
    -- 32 s after three rounds of them: Unit.set_material given things it did not want is the first suspect)
    log(('[Cloak Probe] cloak: %d units share your root'):format(#parts))
    hide_parts(parts)
    -- 0.1.12: weapons, backpack and tools are units of their own, rooted where they hang (0.1.11: six units 1.0 to 1.5 m
    -- from the root stayed visible). Kept as candidates: every unit within 2 m now that is not a part; gear_tick hides
    -- the ones still within 2 m once you have moved 1.5 m (they follow you; what lay on the ground does not).
    local U = lib('Unit')
    local okp, v = root(U, me, RI)
    local mx, my, mz
    if okp then mx, my, mz = xyz(v) end
    st.gear, st.origin, st.gearlog = {}, mx and {mx, my, mz} or nil, false
    if not mx then return end
    local ispart = {}
    for _, u in ipairs(parts) do ispart[tostring(u)] = true end
    local okw, world = pc(lib('Application').main_world)
    local n = 0
    for _, u in ipairs(rare_units(S, okw and world or nil) or {}) do
        local key = tostring(u)
        if not ispart[key] then
            local ok2, v2 = root(U, u, RI)
            local x, y, z
            if ok2 then x, y, z = xyz(v2) end
            if x and (x - mx) ^ 2 + (y - my) ^ 2 + (z - mz) ^ 2 < 4 then st.gear[key] = u; n = n + 1 end
        end
    end
    log(('[Cloak Probe] cloak: %d other units within 2 m: the ones that follow you 1.5 m are hidden as gear'):format(n))
end
local function gear_tick(me)
    if not (st.gear and st.origin) then return 0 end
    local U = lib('Unit')
    local okp, v = root(U, me, RI)
    local mx, my, mz
    if okp then mx, my, mz = xyz(v) end
    if not mx then return 0 end
    local moved = math.sqrt((mx - st.origin[1]) ^ 2 + (my - st.origin[2]) ^ 2 + (mz - st.origin[3]) ^ 2)
    local okw, world = pc(lib('Application').main_world)
    local live = {}
    for _, u in ipairs(rare_units(S, okw and world or nil) or {}) do live[tostring(u)] = true end
    local follow = {}
    for key, u in pairs(st.gear) do
        local keep = false
        if live[key] then
            local ok2, v2 = root(U, u, RI)
            local x, y, z
            if ok2 then x, y, z = xyz(v2) end
            if x and (x - mx) ^ 2 + (y - my) ^ 2 + (z - mz) ^ 2 < 4 then keep = true; follow[#follow + 1] = u end
        end
        if not keep then st.gear[key] = nil end
    end
    if moved < 1.5 then return 0 end
    local hid = hide_parts(follow, true)
    if not st.gearlog then st.gearlog = true; log(('[Cloak Probe] gear: %d units followed you %.1f m: %d meshes hidden'):format(#follow, moved, hid)) end
    return hid
end

local overlay = hd2.ui.overlay({id = 'cloak_probe', owner = OWNER})
overlay:draw(function(d, dt)
    if not st.fn then
        local module = k32.clkhud1_GetModuleHandleA('cloak_probe.dll')
        if module == nil then return end
        local proc = k32.clkhud1_GetProcAddress(module, 'cloak_probe_state')
        if proc == nil then return end
        st.fn = ffi.cast('clkstate1', proc)
        log('[Cloak Probe] HUD side ready')
    end
    if st.fn(1, st.buf) == 0 then return end
    local b = st.buf
    -- 0.1.9: the game turns meshes back on by itself (0.1.8: 30 of 31 body meshes were visible again after the hide), as
    -- the Optical Cloak Pack's own re-hide pass says. While hidden, twice a second: find the parts again and hide again.
    if st.stage == 2 then
        st.acc = (st.acc or 0) + (type(dt) == 'number' and dt or 0.016)
        if st.acc >= 0.5 then
            st.acc = 0
            pcall(function()
                local me = avatar_unit(b[0], b[1], b[2], false)
                if not me then return end
                st.me = me
                local again = hide_parts(parts_of(me), true)
                gear_tick(me)
                if again > 0 then
                    st.rehid = (st.rehid or 0) + 1
                    if st.rehid <= 6 then log(('[Cloak Probe] %s re-hid %d meshes the game had shown again'):format(stamp(), again)) end
                end
            end)
        end
    end
    for k = 1, 3 do
        local count = b[6 + k]
        if st.seen[k] < 0 then st.seen[k] = count
        elseif count ~= st.seen[k] then
            st.seen[k] = count
            local okr, err = pcall(function()
                if k == 1 then api_list() end
                local me = avatar_unit(b[0], b[1], b[2], true)
                if not me then log('[Cloak Probe] no unit of the avatar type has a readable root') return end
                st.me = me
                if k == 1 then list_near(me) end
                if k == 3 then
                    if st.stage and st.stage >= 1 then fade_own(me, 1.0) end
                    if st.stage == 2 then restore() end
                    st.stage = 0
                    return
                end
                if k == 2 then
                    st.stage = (st.stage or 0) + 1
                    if st.stage == 1 then
                        fade_own(me, 0.15)
                        log('[Cloak Probe] step 1 of 2: your own materials faded. Still solid? Ctrl+F12 again for step 2 (every mesh hidden)')
                        return
                    end
                    if st.stage > 2 then fade_own(me, 1.0); restore(); st.stage = 0; log('[Cloak Probe] third press: shown again') return end
                    local stalker = nil
                    if b[6] >= 0.5 then stalker = unit_near(b[3], b[4], b[5], 1.5) end
                    cloak(me, stalker)
                    return
                end
                if b[6] < 0.5 then
                    if k == 2 and st.cached then log('[Cloak Probe] no Stalker near: the materials kept from the last one'); cloak(me, nil) return end
                    log('[Cloak Probe] no Stalker known (a Terminid mission with Stalkers)') return
                end
                local stalker, how = unit_near(b[3], b[4], b[5], 1.5)
                if not stalker then log('[Cloak Probe] the Stalker\'s unit was not found: ' .. tostring(how)) return end
                if k == 1 then
                    describe(stalker, ('the Stalker (entity %d, %.0f m)'):format(b[11], b[10])); describe(me, 'your Helldiver')
                    local U2, M2 = lib('Unit'), lib('Mesh')
                    local okc, nc = pc(U2.num_meshes, stalker)
                    local kept = {}
                    for i = 1, (okc and nc or 0) do
                        local okm, mesh = pc(U2.mesh, stalker, i)
                        local okq, c = false, nil
                        if okm then okq, c = pc(M2.num_materials, mesh) end   -- 0.1.3: 'okm and pc(...)' kept only pc's first result (every count read nil)
                        if okq and type(c) == 'number' and c > 0 then
                            for j = 1, c do local okt, mat = pc(M2.material, mesh, j); if okt and mat then kept[#kept + 1] = mat end end
                            break
                        end
                    end
                    if #kept > 0 then st.cached = kept; log(('[Cloak Probe] kept %d of its materials for Ctrl+F12 later'):format(#kept)) end
                else cloak(me, stalker) end
            end)
            if not okr then log('[Cloak Probe] failed: ' .. tostring(err)) end
        end
    end
end)
log('[Cloak Probe] HUD side loaded (waiting for cloak_probe.dll)')
