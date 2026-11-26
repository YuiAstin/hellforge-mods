-- Escort Probe, its Lua half (mods/yui/escort_probe): research for Andoris's stick bugs. Where does the game carry out
-- the comm wheel's orders to SEAF (Follow, Hold, Move)? The C side found no trace of them in the troopers' records and
-- no native code reacting to comm_follow's hash, so the game's Lua is the likely place.
--
-- It walks the game's Lua globals (and one level into each table) for names with follow, comm, seaf, squad, npc,
-- friendly, order, command, ally, escort or companion in them and logs what exists; and it wraps every function among
-- them whose own name has follow, comm, order, command or leader, logging the first calls of each with their arguments.
-- Scans at 3 s, 20 s and 60 s, then each minute (new names only). Logs through escort_probe.dll (the probe's own log),
-- else Bingus's log folder (EscortProbeLua.log). Research only: nothing is changed but the wrapped functions, which
-- pass every call on unchanged.
local OWNER = 'mods/yui/escort_probe'

local ffi = require('ffi')
pcall(ffi.cdef, [[
typedef int (*ep1_log)(unsigned int version, const char *text);
void *ep1_GetModuleHandleA(const char *name) __asm__("GetModuleHandleA");
void *ep1_GetProcAddress(void *module, const char *name) __asm__("GetProcAddress");
]])
local k32 = ffi.load('kernel32')

local dll_log, sink, sink_tried, lines = nil, nil, false, 0
local function log(message)
    lines = lines + 1
    if lines > 4000 then return end
    if not dll_log then
        local ok = pcall(function()
            local m = k32.ep1_GetModuleHandleA('escort_probe.dll')
            if m ~= nil then
                local p = k32.ep1_GetProcAddress(m, 'escort_lua_log')
                if p ~= nil then dll_log = ffi.cast('ep1_log', p) end
            end
        end)
    end
    if dll_log and pcall(dll_log, 1, message) then return end
    pcall(print, message)
    if not sink_tried then
        sink_tried = true
        local ok, result = pcall(function() return CowboyBingusModLoader.open_log('EscortProbeLua.log') end)
        if ok then sink = result end
    end
    if sink then pcall(function() sink:write(message .. '\n'); if sink.flush then sink:flush() end end) end
end

local WATCH = {'follow', 'comm', 'seaf', 'squad', 'npc', 'friendly', 'order', 'command', 'ally', 'allies', 'escort', 'companion'}
local WRAP = {'follow', 'comm', 'order', 'command', 'leader'}
-- a name's words: split at underscores and camel-case humps (FollowLeader, follow_leader: follow, leader); a word matches
-- when it starts with one of the list's (comm: comm, command, commander; not common, commit)
local SKIP = {common = true, commit = true, committed = true, commits = true, commodity = true}
local function words(name)
    local s = string.gsub(name, '(%l)(%u)', '%1_%2')
    s = string.gsub(s, '(%u)(%u%l)', '%1_%2')
    local out = {}
    for w in string.gmatch(string.lower(s), '[%a]+') do out[#out + 1] = w end
    return out
end
local function has(name, list)
    for _, w in ipairs(words(name)) do
        if not SKIP[w] then
            for _, p in ipairs(list) do if string.sub(w, 1, #p) == p then return true end end
        end
    end
    return false
end

local function show(v, depth)
    local t = type(v)
    if t == 'number' or t == 'boolean' or t == 'nil' then return tostring(v) end
    if t == 'string' then return string.format('%q', #v > 60 and (string.sub(v, 1, 60) .. '...') or v) end
    if t == 'table' then
        if (depth or 0) > 0 then return 'table' end
        local parts, n = {}, 0
        for k, x in pairs(v) do
            n = n + 1
            if n <= 6 then parts[#parts + 1] = tostring(k) .. '=' .. show(x, 1) end
        end
        return '{' .. table.concat(parts, ', ') .. (n > 6 and (', ... ' .. n .. ' keys') or '') .. '}'
    end
    local ok, s = pcall(tostring, v)
    return t .. ':' .. (ok and s or '?')
end
local function args(...)
    local n = select('#', ...)
    local out = {}
    for i = 1, math.min(n, 6) do out[i] = show((select(i, ...))) end
    if n > 6 then out[#out + 1] = '... ' .. n .. ' args' end
    return table.concat(out, ', ')
end

local seen, wrapped, wraps = {}, setmetatable({}, {__mode = 'k'}), 0
local function wrap(tbl, key, path)
    local f = rawget(tbl, key)
    if type(f) ~= 'function' or wrapped[f] or wraps >= 300 then return end
    local calls = 0
    local w = function(...)
        calls = calls + 1
        if calls <= 25 then pcall(log, 'CALL ' .. path .. '(' .. args(...) .. ')' .. (calls == 25 and '  [no more of these logged]' or '')) end
        return f(...)
    end
    wrapped[w] = true
    wrapped[f] = true
    if pcall(rawset, tbl, key, w) then
        wraps = wraps + 1
        log('  wrapped ' .. path)
    end
end

local function scan(stage)
    local before = 0
    for _ in pairs(seen) do before = before + 1 end
    for k, v in pairs(_G) do
        if type(k) == 'string' then
            local top = has(k, WATCH)
            if top and not seen[k] then seen[k] = true; log('G ' .. k .. ' (' .. type(v) .. ')') end
            if type(v) == 'table' and v ~= _G and v ~= package.loaded then
                local n = 0
                local ok = pcall(function()
                    for k2, v2 in pairs(v) do
                        n = n + 1
                        if n > 5000 then break end
                        if type(k2) == 'string' and (top or has(k2, WATCH)) then
                            local path = k .. '.' .. k2
                            if not seen[path] then seen[path] = true; log('  ' .. path .. ' (' .. type(v2) .. ')') end
                            if type(v2) == 'function' and has(k2, WRAP) then wrap(v, k2, path) end
                        end
                    end
                end)
            end
        end
    end
    local after = 0
    for _ in pairs(seen) do after = after + 1 end
    log(string.format('scan %d: %d names so far (%d new), %d functions wrapped', stage, after, after - before, wraps))
end

log('Escort Probe Lua 0.1.0: looking for where the comm wheel\'s orders to SEAF are carried out')
local ok, hd2 = pcall(require, 'mods/skyeshade/hd2runtime')
if not ok or type(hd2) ~= 'table' or type(hd2.ui) ~= 'table' or type(hd2.ui.overlay) ~= 'function' then
    log('HD2Runtime with hd2.ui.overlay is not available: one scan now, no later ones')
    pcall(scan, 0)
    return
end
local t, stage, nexts = 0, 0, {3, 20, 60}
local overlay = hd2.ui.overlay({id = 'escort_probe', owner = OWNER})
overlay:draw(function(d, dt)
    t = t + (tonumber(dt) or 0)
    local due = nexts[stage + 1] or (60 * (stage - 1))
    if stage < 20 and t >= due then
        stage = stage + 1
        local okS, err = pcall(scan, stage)
        if not okS then log('scan ' .. stage .. ' failed: ' .. tostring(err)) end
    end
end)
