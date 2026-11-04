"""Table refs: every function in a game.dll memory dump that touches an inline table of fixed-size rows.

    python table_refs.py --dump game.dll.25480438.dump --table 0x32FDA90 --row 56 --rows 21

The dump is the one the Booster Table Probe writes (game.dll as loaded, laid out by RVA, unpacked). Never commit it.

Pass 1 (numpy): offsets whose 4 bytes, read as a rip-relative displacement (with 0/1/2/4 bytes of immediate after) or
as an absolute 32-bit displacement, land in the rows. Pass 2 (capstone): each function holding one is disassembled
whole; we record direct references, forward-propagated base registers (lea/mov of the table address, then
[reg+...] accesses, until the register is overwritten), imul by the row size, and cmp/test against small constants (bounds
candidates) in the same function. Code outside .pdata (gaps, .winlice, .boot) is covered by a linear sweep around
each hit. Output: functions.json (per function) and sites.tsv (per instruction).
"""
import bisect, json, struct, sys
import numpy as np
import argparse
import capstone as cs
from capstone import x86

ap = argparse.ArgumentParser()
ap.add_argument('--dump', required=True)
ap.add_argument('--table', type=lambda x: int(x, 0), required=True, help='RVA of row 0')
ap.add_argument('--row', type=lambda x: int(x, 0), required=True, help='row size in bytes')
ap.add_argument('--rows', type=int, required=True)
ap.add_argument('--out', default='.')
A = ap.parse_args()
TABLE, ROW, N = A.table, A.row, A.rows
LO, HI = TABLE - ROW, TABLE + (N + 1) * ROW
d = open(A.dump, 'rb').read()
img = np.frombuffer(d, dtype=np.uint8)

# sections (the dump is laid out by RVA)
pe = struct.unpack_from('<I', d, 0x3C)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
opt = struct.unpack_from('<H', d, pe + 20)[0]
secs = []
for s in range(nsec):
    o = pe + 24 + opt + 40 * s
    name = d[o:o + 8].rstrip(b'\0').decode('latin1').strip() or f'sec{s}'
    vsize, va, flags = struct.unpack_from('<II', d, o + 8) + (struct.unpack_from('<I', d, o + 36)[0],)
    secs.append((name, va, vsize, flags))
exe = [(n, va, sz) for n, va, sz, f in secs if f & 0x20000000]

# functions from the exception directory (.pdata): (begin, end)
exc_rva, exc_size = struct.unpack_from('<II', d, pe + 24 + 112 + 3 * 8)
pdata = []
for i in range(exc_size // 12):
    a, b, _ = struct.unpack_from('<III', d, exc_rva + 12 * i)
    if a or b:
        pdata.append((a, b))
pdata.sort()
starts = [a for a, _ in pdata]


def func_of(rva):
    i = bisect.bisect_right(starts, rva) - 1
    if i >= 0 and pdata[i][0] <= rva < pdata[i][1]:
        return pdata[i]
    return None


def slot(t):
    off = t - TABLE
    if off < 0:
        return ('default', off + ROW)
    if off >= (N) * ROW:
        return ('past', off - N * ROW)
    return (off // ROW, off % ROW)


# pass 1
hits = []  # (rva of disp, kind, target)
for name, va, sz in exe:
    seg = img[va:va + sz]
    if len(seg) < 8:
        continue
    # unaligned view: build int32 at every offset via strides
    s = np.lib.stride_tricks.as_strided(seg, shape=(len(seg) - 3, 4), strides=(1, 1))
    v = (s[:, 0].astype(np.int64) | (s[:, 1].astype(np.int64) << 8) | (s[:, 2].astype(np.int64) << 16) |
         (s[:, 3].astype(np.int64) << 24))
    sv = np.where(v >= 2 ** 31, v - 2 ** 32, v)
    pos = np.arange(len(sv), dtype=np.int64) + va
    for k in (0, 1, 2, 4):
        t = pos + 4 + k + sv
        idx = np.nonzero((t >= LO) & (t < HI))[0]
        for i in idx:
            hits.append((int(pos[i]), 'rip', int(t[i])))
    idx = np.nonzero((v >= LO) & (v < HI))[0]
    for i in idx:
        hits.append((int(pos[i]), 'abs', int(v[i])))
print('pass1 raw hits', len(hits), file=sys.stderr)

md = cs.Cs(cs.CS_ARCH_X86, cs.CS_MODE_64)
md.detail = True

funcs = {}  # begin -> (begin, end, in_pdata)
for p, kind, t in hits:
    f = func_of(p)
    if f:
        funcs.setdefault(f[0], (f[0], f[1], True))
    else:
        # the whole gap between the .pdata functions around it
        i = bisect.bisect_right(starts, p)
        gb = pdata[i - 1][1] if i > 0 else 0x1000
        ge = pdata[i][0] if i < len(pdata) else p + 0x400
        funcs.setdefault(gb, (gb, ge, False))

SMALL = set(range(0x10, 0x18)) | {0x38}


def regname(r):
    return md.reg_name(r) if r else None


def base64(r):
    # map sub-registers to their 64-bit parent name for the taint
    n = md.reg_name(r)
    m = {'e': 'r', }
    tbl = {'eax': 'rax', 'ebx': 'rbx', 'ecx': 'rcx', 'edx': 'rdx', 'esi': 'rsi', 'edi': 'rdi', 'ebp': 'rbp',
           'esp': 'rsp'}
    if n in tbl:
        return tbl[n]
    if n and n.startswith('r') and n[-1] in 'dwb' and n[1:-1].isdigit():
        return n[:-1]
    return n


text_va, text_size = exe[0][1], exe[0][2]
tseg = img[text_va:text_va + text_size]
op = np.nonzero(tseg[:-5] == 0xE8)[0]
rel = (tseg[op + 1].astype(np.int64) | (tseg[op + 2].astype(np.int64) << 8) | (tseg[op + 3].astype(np.int64) << 16) |
       (tseg[op + 4].astype(np.int64) << 24))
rel = np.where(rel >= 2 ** 31, rel - 2 ** 32, rel)
src = op + text_va
dst = src + 5 + rel
callers = {}
want = set(funcs)
ok = np.isin(dst, np.array(sorted(k for k in want), dtype=np.int64))
for a, t in zip(src[ok].tolist(), dst[ok].tolist()):
    f = func_of(a)
    if f:  # a call from inside a real function is a real call (filters false E8 bytes)
        callers.setdefault(t, set()).add(f[0])

out_funcs, sites = [], []
for key, (b, e, inp) in sorted(funcs.items(), key=lambda kv: kv[1][0]):
    code = d[b:e]
    recs, tainted = [], {}
    has_imul = has_cmp = False
    cmps = []
    for ins in md.disasm(code, b):
        ops = ins.operands
        # writes kill taint
        try:
            _, written = ins.regs_access()
        except cs.CsError:
            written = []
        target_here = None
        for o in ops:
            if o.type == x86.X86_OP_MEM:
                m = o.mem
                if m.base == x86.X86_REG_RIP:
                    t = ins.address + ins.size + m.disp
                    if LO <= t < HI:
                        target_here = ('rip', t)
                elif LO <= (m.disp & 0xFFFFFFFF) < HI and m.disp > 0:
                    target_here = ('abs', m.disp)
                elif m.base and base64(m.base) in tainted:
                    target_here = ('via ' + base64(m.base), tainted[base64(m.base)] + m.disp)
        if target_here:
            kind, t = target_here
            recs.append({'rva': ins.address, 'kind': kind, 'slot': slot(t), 'ins': f'{ins.mnemonic} {ins.op_str}'})
        if ins.mnemonic == 'imul' and any(o.type == x86.X86_OP_IMM and o.imm == ROW for o in ops):
            has_imul = True
            recs.append({'rva': ins.address, 'kind': 'imul_row', 'slot': None, 'ins': f'{ins.mnemonic} {ins.op_str}'})
        if ins.mnemonic in ('cmp',) and any(o.type == x86.X86_OP_IMM and o.imm in (N - 2, N - 1, N) for o in ops):
            cmps.append({'rva': ins.address, 'ins': f'{ins.mnemonic} {ins.op_str}'})
        # taint update
        for r in written:
            tainted.pop(base64(r), None)
        if ins.mnemonic == 'lea' and target_here and target_here[0] in ('rip',) and ops[0].type == x86.X86_OP_REG:
            tainted[base64(ops[0].reg)] = target_here[1]
        if ins.mnemonic == 'mov' and len(ops) == 2 and ops[0].type == x86.X86_OP_REG and ops[1].type == x86.X86_OP_REG \
                and base64(ops[1].reg) in tainted:
            tainted[base64(ops[0].reg)] = tainted[base64(ops[1].reg)]
    real = [r for r in recs if r['kind'] != 'imul_row']
    if not real:
        continue
    out_funcs.append({'begin': b, 'end': e, 'pdata': inp, 'refs': recs, 'bounds_candidates': cmps,
                      'callers': sorted(callers.get(b, ()))})
    for r in recs:
        sites.append((r['rva'], b, r['kind'], str(r['slot']), r['ins']))

import os
json.dump(out_funcs, open(os.path.join(A.out, 'functions.json'), 'w'), indent=1)
with open(os.path.join(A.out, 'sites.tsv'), 'w') as f:
    for s in sorted(sites):
        f.write('0x%07X\t0x%07X\t%s\t%s\t%s\n' % s)
print('functions', len(out_funcs), 'sites', sum(1 for s in sites if s[2] != 'imul_row'),
      'gap functions', sum(1 for f in out_funcs if not f['pdata']), file=sys.stderr)
