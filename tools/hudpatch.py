"""Builds (and unpacks) a Bingus Shared Loader addon archive (9ba626afa44a3aa3.patch_0) for one plaintext Lua entry.

    python tools/hudpatch.py build mods/yui/field_promotions_hud huds/field-promotions-hud/fp_hud.lua mods/field-promotions/data/9ba626afa44a3aa3.patch_0
    python tools/hudpatch.py extract mods/yui/field_promotions_hud mods/field-promotions/data/9ba626afa44a3aa3.patch_0 out.lua

Written from the format in BingusSharedLoader's scripts/archive.py and build_addon.py (read, not imported)."""
import struct, sys

TYPE = 0xA14E8DFA2CD117E2

def murmur64a(data):
    mask, mix = (1 << 64) - 1, 0xC6A4A7935BD1E995
    h = len(data) * mix & mask
    end = len(data) // 8 * 8
    for (w,) in struct.iter_unpack('<Q', data[:end]):
        w = w * mix & mask
        w ^= w >> 47
        h = (h ^ (w * mix & mask)) * mix & mask
    if data[end:]:
        h = (h ^ int.from_bytes(data[end:], 'little')) * mix & mask
    h ^= h >> 47
    h = h * mix & mask
    return h ^ (h >> 47)

def build(name, lua):
    marker = ('-- HD2-Addon: ' + name + '\n').encode()
    if lua.startswith(b'-- HD2-Addon:'):
        lua = lua.partition(b'\n')[2]
    body = marker + lua
    resource = struct.pack('<II', len(body), 2) + body
    key = murmur64a(name.encode())
    offset = (104 + 80 + 15) & ~15
    out = bytearray(offset)
    entry = struct.pack('<7Q6I', key, TYPE, offset, 0, 0, 0, 0, len(resource), 0, 0, 16, 16, 0)
    out += resource
    out += b'\0' * (-len(out) % 16)
    header = struct.pack('<III20sQQ24s', 0xF0000011, 1, 1, b'', len(out), 0, b'')
    types = struct.pack('<IIQIIII', 0, 0, TYPE, 1, 0, 16, 16)
    out[:104 + 80] = header + types + entry
    return bytes(out)

def extract(data, name):
    magic, types, count = struct.unpack_from('<III', data)
    assert magic == 0xF0000011
    key = murmur64a(name.encode())
    for i in range(count):
        e = struct.unpack_from('<7Q6I', data, 72 + types * 32 + i * 80)
        if e[0] == key:
            res = data[e[2]:e[2] + e[7]]
            return res[8:].partition(b'\n')[2]
    return None

if __name__ == '__main__':
    cmd, name = sys.argv[1], sys.argv[2]
    if cmd == 'extract':
        open(sys.argv[4], 'wb').write(extract(open(sys.argv[3], 'rb').read(), name))
    elif cmd == 'build':
        open(sys.argv[4], 'wb').write(build(name, open(sys.argv[3], 'rb').read()))
