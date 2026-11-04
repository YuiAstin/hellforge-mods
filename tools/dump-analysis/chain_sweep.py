"""Chain sweep: follow chained unwind info to each touching function's root, disassemble every fragment, and list
every instruction encoding a table address plus every constant equal to the row count (and count-1, count+1).
Run table_refs.py first (it writes functions.json).

    python chain_sweep.py --dump game.dll.25480438.dump --table 0x32FDA90 --row 56 --rows 21
"""
import argparse, struct, bisect, json, re, os
import capstone as cs
from capstone import x86
ap=argparse.ArgumentParser()
ap.add_argument('--dump',required=True); ap.add_argument('--table',type=lambda x:int(x,0),required=True)
ap.add_argument('--row',type=lambda x:int(x,0),required=True); ap.add_argument('--rows',type=int,required=True)
ap.add_argument('--out',default='.')
A=ap.parse_args()
d=open(A.dump,'rb').read()
TABLE,ROW,N=A.table,A.row,A.rows
END=TABLE+N*ROW
pe=struct.unpack_from('<I',d,0x3C)[0]
exc_rva,exc_size=struct.unpack_from('<II',d,pe+24+112+3*8)
pd=sorted(e for e in (struct.unpack_from('<III',d,exc_rva+12*i) for i in range(exc_size//12)) if e[0] or e[1])
st=[a for a,_,_ in pd]
def entry(r):
    i=bisect.bisect_right(st,r)-1
    return pd[i] if i>=0 and pd[i][0]<=r<pd[i][1] else None
def parent(e):
    u=e[2]; flags=d[u]>>3; cnt=d[u+2]
    if flags&4:
        o=u+4+2*(cnt+(cnt&1)); return struct.unpack_from('<III',d,o)
    return None
def root(e):
    seen=0
    while True:
        p=parent(e)
        if not p or seen>32: return e
        e=p; seen+=1
# group fragments by root
groups={}
for e in pd:
    groups.setdefault(root(e)[0],[]).append(e)
F=json.load(open(os.path.join(A.out,'functions.json')))
md=cs.Cs(cs.CS_ARCH_X86,cs.CS_MODE_64); md.detail=True
# 1) table-address sites: any instruction in .text whose rip/abs operand lands in [TABLE,END) — from all functions in all touched groups, plus gaps
touched_roots=set(); gaps=[]
for f in F:
    if f['pdata']: touched_roots.add(root(entry(f['begin']))[0])
    else: gaps.append((f['begin'],f['end']))
ranges=[]
for r in touched_roots: ranges+= [(a,b) for a,b,_ in groups[r]]
ranges+=gaps
sites=[]; consts=[]
for a,b in sorted(set(ranges)):
    for ins in md.disasm(d[a:b],a):
        for o in ins.operands:
            if o.type==x86.X86_OP_MEM:
                m=o.mem
                t = ins.address+ins.size+m.disp if m.base==x86.X86_REG_RIP else (m.disp if (m.disp>0 and TABLE-ROW<=m.disp<END+0x100) else None)
                if t is not None and TABLE<=t<END:
                    sites.append((ins.address, 'rip' if m.base==x86.X86_REG_RIP else 'abs', t-TABLE, f"{ins.mnemonic} {ins.op_str}", root(entry(a))[0] if entry(a) else a))
            if o.type==x86.X86_OP_IMM and o.imm in (N-1,N,N+1) and ins.mnemonic in ('cmp','mov','lea','sub','add'):
                consts.append((ins.address, f"{ins.mnemonic} {ins.op_str}", root(entry(a))[0] if entry(a) else a))
json.dump({'sites':sites,'consts':consts,'roots':sorted(touched_roots),'gaps':gaps},open(os.path.join(A.out,'sweep.json'),'w'))
print('roots',len(touched_roots),'gap ranges',len(gaps),'fragments',len(set(ranges)))
print('table-address sites',len(sites),'rip',sum(s[1]=='rip' for s in sites),'abs',sum(s[1]=='abs' for s in sites))
import collections
print('by row offset', collections.Counter((s[2]//ROW, s[2]%ROW) for s in sites).most_common(30))
hx=hex(N)
print('count constants', sum(hx in c[1] for c in consts), 'cmp',sum(c[1].startswith('cmp') and hx in c[1] for c in consts),'mov',sum(c[1].startswith('mov') and hx in c[1] for c in consts))
