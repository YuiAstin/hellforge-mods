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
-- the unit nearest a point (World.units, then each unit's root position)
local function unit_near(px, py, pz, within)
    local App, W, U = lib('Application'), lib('World'), lib('Unit')
    if not (App and W and U) then return nil, 'Application / World / Unit missing' end
    local okw, world = pc(App.main_world)
    if not okw or not world then return nil, 'no main world' end
    local oku, all = pc(W.units, world)
    if not oku or type(all) ~= 'table' then return nil, 'World.units unavailable: ' .. tostring(all) end
    local best, bd = nil, within * within
    for _, u in ipairs(all) do
        local okp, v = pc(U.world_position, u, 1)
        local x, y, z
        if okp then x, y, z = xyz(v) end            -- 0.1.1: (okp and xyz(v)) kept only x
        if x then
            local d2 = (x - px) ^ 2 + (y - py) ^ 2 + (z - pz) ^ 2
            if d2 < bd then best, bd = u, d2 end
        end
    end
    return best, best and math.sqrt(bd) or ('no unit within ' .. within .. ' m of ' .. #all .. ' units')
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
        local okc, c = okm and pc(M and M.num_materials, mesh)
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
        local okc, c = okm and pc(M.num_materials, mesh)
        if okc and type(c) == 'number' and c > 0 then
            for j = 1, c do local okt, mat = pc(M.material, mesh, j); if okt and mat then src[#src + 1] = mat end end
        end
        if #src > 0 then break end
    end
    if #src == 0 then log('[Cloak Probe] cloak: no material on the Stalker'); return end
    st.cached = src
    st.saved = st.saved or {}
    local done, failed, how = 0, 0, nil
    for i = 1, n do
        local okm, mesh = pc(U.mesh, me, i)
        local okc, c = okm and pc(M.num_materials, mesh)
        if okc and type(c) == 'number' then
            for j = 1, c do
                local okt, mine = pc(M.material, mesh, j)
                if okt then st.saved[#st.saved + 1] = {mesh = mesh, slot = j, mat = mine} end
                local mat = src[((j - 1) % #src) + 1]
                local okset, err = pc(M.set_material, mesh, j, mat)
                if okset then how = 'Mesh.set_material'; done = done + 1
                else
                    failed = failed + 1
                    if failed <= 3 then log('[Cloak Probe]   Mesh.set_material failed: ' .. tostring(err)) end
                end
            end
        end
    end
    log(('[Cloak Probe] cloak: %d material slots set (%s), %d failed, %d of yours saved'):format(done, how or 'none', failed, #(st.saved or {})))
    if done == 0 then
        -- the unit-level calls, with the slot names unknown: try what the engine offers and log it
        local ok1, e1 = pc(U.set_material_from_other_unit, me, 'body', stalker)
        log('[Cloak Probe]   Unit.set_material_from_other_unit(me, "body", stalker): ' .. (ok1 and 'ok' or tostring(e1)))
    end
end
local function restore()
    local M = lib('Mesh')
    if not st.saved then log('[Cloak Probe] uncloak: nothing saved'); return end
    local n = 0
    for _, s in ipairs(st.saved) do if pc(M.set_material, s.mesh, s.slot, s.mat) then n = n + 1 end end
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
                        local okq, c = okm and pc(M2.num_materials, mesh)
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
