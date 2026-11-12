-- Cloak Probe HUD (mods/yui/cloak_probe_hud): the material side of the Cloak Probe (research; see cloak_probe.c).
-- Reads cloak_probe.dll's cloak_probe_state every frame; on its key counts it:
--   F12        logs the engine's mesh / material calls, then the nearest Stalker's unit: its meshes and materials
--   Ctrl+F12   copies the Stalker's materials onto your Helldiver's meshes (each way the engine offers, logged)
--   Shift+F12  puts your own back (the materials it saved first)
-- Uses HD2Runtime by SkyeShade through its public API (hd2.ui.overlay as a frame hook) and the engine's Lua API.
local OWNER = 'mods/yui/cloak_probe_hud'
local sink, sink_tried
local function log(message)
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
local function rare_units(s, w)
    local oku, all = pcall(s.World.units, w)
    if not oku or type(all) ~= 'table' then return nil end
    local res, count = {}, {}
    for i, u in ipairs(all) do
        local okr, r = pcall(s.Unit.resource_name, u)
        r = okr and tostring(r) or '?'
        res[i] = r
        count[r] = (count[r] or 0) + 1
    end
    local out = {}
    for i, u in ipairs(all) do if count[res[i]] <= 4 and res[i] ~= '?' then out[#out + 1] = u end end
    return out, #all
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
        local okp, v = pc(U.world_position, u, RI)
        local x, y, z
        if okp then x, y, z = xyz(v) end            -- 0.1.1: (okp and xyz(v)) kept only x
        if x then
            local d2 = (x - px) ^ 2 + (y - py) ^ 2 + (z - pz) ^ 2
            if d2 < bd then best, bd = u, d2 end
        end
    end
    return best, best and math.sqrt(bd) or ('no unit within ' .. within .. ' m of ' .. #all .. ' rare-type units (' .. tostring(total) .. ' in all)')
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
    if not (okn and oks) then log('[Cloak Probe] cloak: mesh counts unreadable'); return end
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
    if #src == 0 then log('[Cloak Probe] cloak: no material on the Stalker'); return end
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
    local done, failed, how, tried = 0, 0, nil, 0
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
                        st.saved[#st.saved + 1] = {slot = (how.slot == 'Mesh.material_slot_id') and slot or j, mat = (how.mat == 'Material.id64' and okid) and myid or mine}
                    end
                else failed = failed + 1 end
            end
        end
    end
    log(('[Cloak Probe] cloak: %d material slots set (%s), %d failed'):format(done, how and (how.slot .. ' + ' .. how.mat) or 'no way worked', failed))
    if done == 0 then
        local okv, ev = pc(U.set_unit_visibility, me, false)
        st.hidden = okv and me or nil
        log('[Cloak Probe] cloak: no material could be set: Unit.set_unit_visibility(me, false): ' .. (okv and 'ok (your Helldiver is hidden outright; Shift+F12 shows her again)' or tostring(ev)))
    end
end
local function restore()
    local U = lib('Unit')
    if st.hidden then
        local okv, ev = pc(U.set_unit_visibility, st.hidden, true)
        log('[Cloak Probe] uncloak: Unit.set_unit_visibility(me, true): ' .. (okv and 'ok' or tostring(ev)))
        st.hidden = nil
    end
    if not st.saved or #st.saved == 0 then log('[Cloak Probe] uncloak: no material to put back'); st.saved = nil; return end
    local n = 0
    for _, s in ipairs(st.saved) do if pc(U.set_material, st.me, s.slot, s.mat) then n = n + 1 end end
    log(('[Cloak Probe] uncloak: %d of %d slots put back'):format(n, #st.saved))
    st.saved = nil
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
    for k = 1, 3 do
        local count = b[6 + k]
        if st.seen[k] < 0 then st.seen[k] = count
        elseif count ~= st.seen[k] then
            st.seen[k] = count
            local okr, err = pcall(function()
                if k == 1 then api_list() end
                local me = unit_near(b[0], b[1], b[2], 1.5)
                if not me then log('[Cloak Probe] your Helldiver\'s unit was not found near you') return end
                st.me = me
                if k == 3 then restore() return end
                if b[6] < 0.5 then
                    if k == 2 and st.cached then log('[Cloak Probe] no Stalker near: the materials kept from the last one'); copy_materials(me, nil) return end
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
                else copy_materials(me, stalker) end
            end)
            if not okr then log('[Cloak Probe] failed: ' .. tostring(err)) end
        end
    end
end)
log('[Cloak Probe] HUD side loaded (waiting for cloak_probe.dll)')
