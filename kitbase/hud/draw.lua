------------------------------------------------------------- KB_DRAW: the kits' HUD lettering (one table: KBD) --------
-- rgba(c, a): a colour {r, g, b} at an alpha 0..1. heavy(d, text, x, y, size, font, colour, align): text with a drop
-- shadow (sizes 17 px and up at 1080p keep the strokes solid). key(vk, mods): a key as the options screen names it
-- (kb_core.h's labels and order: Space, Num0, M4, Ctrl+Alt+Shift+Q; #32 only for a key with no name). Lifted 2026-11-26 from every kit's HUD.
local KBD = (function()
    local INK = {8, 9, 10}
    local NAMES = {[0x20] = 'Space', [0x09] = 'Tab', [0x0D] = 'Enter', [0x14] = 'Caps', [0x08] = 'Bksp', [0x2D] = 'Ins',
        [0x2E] = 'Del', [0x24] = 'Home', [0x23] = 'End', [0x21] = 'PgUp', [0x22] = 'PgDn', [0x26] = 'Up', [0x28] = 'Down',
        [0x25] = 'Left', [0x27] = 'Right', [0x6A] = 'Num*', [0x6B] = 'Num+', [0x6D] = 'Num-', [0x6E] = 'Num.', [0x6F] = 'Num/',
        [0xBA] = ';', [0xBB] = '=', [0xBC] = ',', [0xBD] = '-', [0xBE] = '.', [0xBF] = '/', [0xC0] = '`', [0xDB] = '[',
        [0xDC] = '\\', [0xDD] = ']', [0xDE] = "'", [0x04] = 'M3', [0x05] = 'M4', [0x06] = 'M5', [0x10] = 'Shift', [0x11] = 'Ctrl',
        [0x12] = 'Alt'}
    local function rgba(c, a) return {c[1], c[2], c[3], math.floor(255 * (a or 1) + 0.5)} end
    local function heavy(d, str, x, y, size, font, colour, align)
        local s = d.scale or 1
        d:text(str, x + math.max(1, 2 * s), y + math.max(1, 2 * s), {size = size, font = font, colour = rgba(INK, 0.75), z = 4, align = align})
        d:text(str, x, y, {size = size, font = font, colour = colour, z = 5, align = align})
    end
    local function key(vk, mods)
        vk = tonumber(vk) or 0
        local name
        if (vk >= 65 and vk <= 90) or (vk >= 48 and vk <= 57) then name = string.char(vk)
        elseif vk >= 0x70 and vk <= 0x87 then name = 'F' .. (vk - 0x6F)
        elseif vk >= 0x60 and vk <= 0x69 then name = 'Num' .. (vk - 0x60)
        else name = NAMES[vk] or ('#' .. vk) end
        mods = tonumber(mods) or 0
        local pre = (math.floor(mods / 2) % 2 == 1 and 'Ctrl+' or '') .. (math.floor(mods / 4) % 2 == 1 and 'Alt+' or '') .. (mods % 2 == 1 and 'Shift+' or '')
        name = pre .. name
        return name
    end
    return {INK = INK, rgba = rgba, heavy = heavy, key = key}
end)()
