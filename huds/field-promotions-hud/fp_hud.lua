-- Field Promotions HUD (mods/yui/field_promotions_hud): a banner for each level Field Promotions' gear earns in a
-- mission, drawn with HD2Runtime's screen overlay (hd2.ui.overlay). Field Promotions is a HellForge code mod; this
-- reads its state through LuaJIT's ffi (field_promotions.dll exports fp_hud_state, layout version 1) every frame.
-- Requires Bingus Shared Loader (v15+) and HD2Runtime (0.50.1 tested against). Uses HD2Runtime by SkyeShade
-- (https://github.com/SkyeShade/HD2Runtime) through its public API.
local OWNER = 'mods/yui/field_promotions_hud'

local ok, hd2 = pcall(require, 'mods/skyeshade/hd2runtime')
if not ok or type(hd2) ~= 'table' or type(hd2.ui) ~= 'table' or type(hd2.ui.overlay) ~= 'function' then
    print('[Field Promotions HUD] HD2Runtime with hd2.ui.overlay is not available: no banners (' .. tostring(hd2) .. ')')
    return
end

local ffi = require('ffi')
-- private names (Bingus AUTHORING: the first cdef of a name wins for the whole session)
pcall(ffi.cdef, [[
typedef struct fphud1_toast { uint32_t level; char title[64]; char line[160]; } fphud1_toast;
typedef struct fphud1_state { uint32_t version, seq, in_mission, toasts_on; fphud1_toast toasts[8]; } fphud1_state;
void *fphud1_GetModuleHandleA(const char *name) __asm__("GetModuleHandleA");
void *fphud1_GetProcAddress(void *module, const char *name) __asm__("GetProcAddress");
]])
local k32 = ffi.load('kernel32')

local SHOW, FADE_IN, FADE_OUT = 5.0, 0.25, 0.8   -- seconds
local YELLOW, INK, PANEL, TEXT, MUTED = {255, 232, 0}, {8, 9, 10}, {12, 14, 16}, {240, 243, 245}, {170, 176, 180}

local state, last_seq, find_wait = nil, nil, 0
local queue, current, age = {}, nil, 0

-- Field Promotions' state, found again every 2 s until the DLL is there.
local function find(dt)
    if state ~= nil then return state end
    find_wait = find_wait - dt
    if find_wait > 0 then return nil end
    find_wait = 2
    local module = k32.fphud1_GetModuleHandleA('field_promotions.dll')
    if module == nil then return nil end
    local proc = k32.fphud1_GetProcAddress(module, 'fp_hud_state')
    if proc == nil then return nil end
    local s = ffi.cast('fphud1_state *(*)(void)', proc)()
    if s == nil or s.version ~= 1 then return nil end
    state = s
    last_seq = tonumber(s.seq)   -- only level-ups from now on
    print('[Field Promotions HUD] connected to Field Promotions')
    return state
end

-- New toasts since the last frame (at most the 8 the ring keeps).
local function poll()
    local seq = tonumber(state.seq)
    if seq < last_seq then last_seq = seq end   -- the DLL was reloaded
    if seq - last_seq > 8 then last_seq = seq - 8 end
    while last_seq < seq do
        local t = state.toasts[last_seq % 8]
        queue[#queue + 1] = {level = tonumber(t.level), title = ffi.string(t.title), line = ffi.string(t.line)}
        last_seq = last_seq + 1
    end
end

local function with_alpha(c, a) return {c[1], c[2], c[3], math.floor(255 * a + 0.5)} end

-- One chevron (an upward ^) of small blocks: apex at (cx, top), `w` wide.
local function chevron(d, cx, top, w, colour, z)
    local step = w / 9
    for k = 0, 4 do
        local y = top + k * step * 0.9
        d:rect(cx - (k + 0.5) * step - step * 0.5, y, step * 1.25, step * 1.6, colour, z)
        d:rect(cx + (k + 0.5) * step - step * 0.75, y, step * 1.25, step * 1.6, colour, z)
    end
end

-- The rank badge: black square, yellow frame, one chevron for level 2, two for level 3.
local function badge(d, x, y, size, level, a)
    local frame, ink = with_alpha(YELLOW, a), with_alpha(INK, a * 0.95)
    local t = math.max(2, size * 0.06)
    d:rect(x, y, size, size, ink, 1)
    d:rect(x, y, size, t, frame, 2)
    d:rect(x, y + size - t, size, t, frame, 2)
    d:rect(x, y, t, size, frame, 2)
    d:rect(x + size - t, y, t, size, frame, 2)
    local w = size * 0.62
    if level >= 3 then
        chevron(d, x + size / 2, y + size * 0.20, w, frame, 3)
        chevron(d, x + size / 2, y + size * 0.48, w, frame, 3)
    else
        chevron(d, x + size / 2, y + size * 0.34, w, frame, 3)
    end
end

local overlay = hd2.ui.overlay({id = 'toasts', owner = OWNER})
overlay:draw(function(d, dt)
    dt = tonumber(dt) or 0
    if not find(dt) then return end
    poll()
    if state.toasts_on == 0 then queue, current = {}, nil return end
    if not current then
        current = table.remove(queue, 1)
        age = 0
        if not current then return end
    end
    age = age + dt
    -- a backlog shows each toast for less time
    local show = #queue > 0 and math.max(2.0, SHOW - #queue) or SHOW
    if age >= show then current = nil return end
    local a = math.min(1, age / FADE_IN, (show - age) / FADE_OUT)
    if a <= 0 then return end

    local s = d.scale
    local w, h = 660 * s, 96 * s
    local x, y = (d.width - w) / 2, 150 * s
    d:rect(x, y, w, h, with_alpha(PANEL, a * 0.86), 0)
    d:rect(x, y, w, 3 * s, with_alpha(YELLOW, a), 1)
    local b = 72 * s
    badge(d, x + 12 * s, y + (h - b) / 2, b, current.level, a)
    local tx = x + 12 * s + b + 16 * s
    local tw = x + w - 16 * s - tx
    d:text(current.title, tx, y + 10 * s, {size = 24 * s, font = 'title', colour = with_alpha(YELLOW, a), z = 2})
    d:text('FIELD PROMOTION  -  LEVEL ' .. tostring(current.level), tx, y + 40 * s,
        {size = 16 * s, font = 'body', colour = with_alpha(TEXT, a), z = 2})
    local lines = d:wrap(current.line, tw, {size = 14 * s, font = 'body', lines = 1})
    if lines and lines[1] then
        d:text(lines[1], tx, y + 64 * s, {size = 14 * s, font = 'body', colour = with_alpha(MUTED, a), z = 2})
    end
end)
print('[Field Promotions HUD] ready (waiting for Field Promotions)')
