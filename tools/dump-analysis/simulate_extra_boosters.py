"""Simulate Extra Boosters' install on a copy of the dump, then check it two ways:
1. every patched instruction re-disassembled: sites now address the block at the same offset, names readers the new
   list, bounds hold count+1;
2. the game's own accessor functions emulated (unicorn) before and after: booster 21 is found by id, by index, and
   "not found" is now 22.
"""
import re, struct, capstone as cs
from capstone import x86
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_MEM_UNMAPPED
from unicorn.x86_const import *

d0 = open('game.dump', 'rb').read()
BASE = 0x7FF98F480000
TABLE, ROW, VAN, NAMES = 0x32FDA90, 56, 21, 0x21DD630
NEWID, BASEROW = 0x1041BB56, 3


def parse(h, name):
    body = re.search(name + r'\[\] = \{(.*?)\};', h, re.S).group(1)
    return [tuple(int(x, 0) for x in t) for t in re.findall(r'\{(0x[0-9A-F]+), (\d+), (\d+), (\d+), (0x[0-9A-F]+|\d+)\}', body)]


H = open('/home/claude/hellforge-mods/mods/extra-boosters/src/booster_sites.h').read()
B = open('/home/claude/hellforge-mods/mods/extra-boosters/src/booster_bounds.h').read()
sites = parse(H, 'kBoosterSites')
readers = parse(H, 'kBoosterNameReaders')
bounds = [tuple(int(x, 0) for x in t) for t in re.findall(r'\{(0x[0-9A-F]+), (\d+), (\d+), (\d+)\}', B)]
assert (len(sites), len(readers), len(bounds)) == (117, 2, 112)

IMAGE = len(d0)
BLOCK = BASE + ((IMAGE + 0xFFFF) & ~0xFFFF)          # first granule above the image, as NearBlock would try
ROWS_AT, NAMES_AT = 0, 23 * ROW
TEXT_AT = NAMES_AT + 23 * 8

# ---- build the block as Install() does
blk = bytearray(0x1000)
blk[0:VAN * ROW] = d0[TABLE:TABLE + VAN * ROW]
row = bytearray(d0[TABLE + BASEROW * ROW:TABLE + (BASEROW + 1) * ROW])
row[0:4] = struct.pack('<I', NEWID)
blk[VAN * ROW:(VAN + 1) * ROW] = row
names = list(struct.unpack_from('<22Q', d0, NAMES))
blk[TEXT_AT:TEXT_AT + 14] = b'ExtraBooster1\0'
lst = names[:21] + [BLOCK + TEXT_AT, names[21]]
struct.pack_into('<23Q', blk, NAMES_AT, *lst)

# ---- patch a copy
d = bytearray(d0)
for rva, at, size, rip, off in sites:
    tgt = BLOCK + ROWS_AT + off
    disp = tgt - (BASE + rva + size) if rip else tgt - BASE
    assert -2**31 <= disp < 2**31
    d[rva + at:rva + at + 4] = struct.pack('<i', disp)
for rva, at, size, rip, off in readers:
    d[rva + at:rva + at + 4] = struct.pack('<i', BLOCK + NAMES_AT - BASE)
for rva, at, w, v in bounds:
    d[rva + at:rva + at + w] = (v + 1).to_bytes(w, 'little')

# ---- check 1: disassembly
md = cs.Cs(cs.CS_ARCH_X86, cs.CS_MODE_64); md.detail = True
bad = 0
for rva, at, size, rip, off in sites:
    i = next(md.disasm(bytes(d[rva:rva + 16]), BASE + rva))
    m = next(o.mem for o in i.operands if o.type == x86.X86_OP_MEM)
    tgt = (i.address + i.size + m.disp) if m.base == x86.X86_REG_RIP else (BASE + m.disp)
    if i.size != size or tgt != BLOCK + off: bad += 1; print('SITE BAD', hex(rva), i.mnemonic, i.op_str)
for rva, at, w, v in bounds:
    i = next(md.disasm(bytes(d[rva:rva + 16]), rva))
    i0 = next(md.disasm(d0[rva:rva + 16], rva))
    if i.size != i0.size or i.mnemonic != i0.mnemonic or hex(v + 1) not in i.op_str: bad += 1; print('BOUND BAD', hex(rva), i.mnemonic, i.op_str)
print('disassembly check: %d sites, %d bounds, %d bad' % (len(sites), len(bounds), bad))

# ---- check 2: emulate accessors
def emu(mem, fn, rcx=0, rdx=0, out=False):
    u = Uc(UC_ARCH_X86, UC_MODE_64)
    u.mem_map(BASE, (IMAGE + 0xFFF) & ~0xFFF); u.mem_write(BASE, bytes(mem))
    u.mem_map(BLOCK, 0x1000); u.mem_write(BLOCK, bytes(blk))
    STK = 0x10000000; u.mem_map(STK, 0x10000)
    OUT = STK + 0x8000
    rsp = STK + 0x4000
    RET = 0x20000000; u.mem_map(RET, 0x1000); u.mem_write(RET, b'\xc3')
    u.mem_write(rsp, struct.pack('<Q', RET))
    u.reg_write(UC_X86_REG_RSP, rsp)
    u.reg_write(UC_X86_REG_RCX, OUT if out else rcx)
    u.reg_write(UC_X86_REG_RDX, rdx if out else rdx)
    if not out: u.reg_write(UC_X86_REG_RCX, rcx)
    u.emu_start(BASE + fn, RET, count=20000)
    rax = u.reg_read(UC_X86_REG_RAX)
    return (struct.unpack('<I', u.mem_read(OUT, 4))[0] if out else rax)

hashes = [struct.unpack_from('<I', d0, TABLE + ROW * r)[0] for r in range(21)]
idx = lambda mem, h: emu(mem, 0x11F81A0, rcx=h) & 0xFFFFFFFF
rowp = lambda mem, i: emu(mem, 0x11F83C0, rcx=i)
idof = lambda mem, i: emu(mem, 0x11F83E0, rdx=i, out=True)
isb = lambda mem, h: emu(mem, 0x11F8410, rcx=h) & 0xFF
print('before: index_by_id(Muscle) =', idx(d0, hashes[3]), '| index_by_id(new) =', idx(d0, NEWID),
      '| row_by_index(21) =', hex(rowp(d0, 21)), '| id_of(21) =', hex(idof(d0, 21)), '| is_booster(new) =', isb(d0, NEWID))
print('after:  index_by_id(Muscle) =', idx(d, hashes[3]), '| index_by_id(new) =', idx(d, NEWID),
      '| index_by_id(unknown) =', idx(d, 0xDEADBEEF), '| row_by_index(21) = block+%#x' % (rowp(d, 21) - BLOCK),
      '| row_by_index(22) =', hex(rowp(d, 22)), '| id_of(21) =', hex(idof(d, 21)), '| is_booster(new) =', isb(d, NEWID))
ok = (idx(d, hashes[3]) == 3 and idx(d, NEWID) == 21 and idx(d, 0xDEADBEEF) == 22 and rowp(d, 21) == BLOCK + 21 * ROW
      and rowp(d, 22) == 0 and idof(d, 21) == NEWID and all(idx(d, h) == r for r, h in enumerate(hashes)))
print('emulation check:', 'PASS' if ok else 'FAIL')
