#!/usr/bin/env python3
"""Builds Ascalon's parry sound bank: the CQC-2 Saber bank's structure (one 2D one-shot random container on the player
bus, with its mixer chain), under new ids, with our recordings as PCM media. Usage:
build_bank.py <shell patch from HellForge build-data> <out patch> <wav>..."""
import struct,sys,wave,hashlib,array
BANK='ascalon_parry'; PATH='content/audio/ascalon_parry'; EVENT='ascalon_parry'
SHELL_EVENT=0xFF783C88; XOR=0xA3107038; ARCHIVE_MAGIC=0xF0000011
T_BANK=0x535A7BD3E650D799; T_DEP=0xAF32095C82F2B070
def fnv(s):
    h=2166136261
    for c in s.lower().encode(): h=(h*16777619)&0xffffffff; h^=c
    return h
def mm(key):
    m=0xc6a4a7935bd1e995; M=(1<<64)-1; h=(len(key)*m)&M; n=len(key)//8
    for i in range(n):
        k=struct.unpack_from('<Q',key,i*8)[0]; k=k*m&M; k^=k>>47; k=k*m&M; h^=k; h=h*m&M
    t=key[n*8:]
    if t: h^=int.from_bytes(t,'little'); h=h*m&M
    h^=h>>47; h=h*m&M; h^=h>>47; return h
u32=lambda b,o: struct.unpack_from('<I',b,o)[0]
def wem(path):
    w=wave.open(path,'rb'); ch,sw,rate,n=w.getnchannels(),w.getsampwidth(),w.getframerate(),w.getnframes(); pcm=w.readframes(n)
    assert sw==2
    if ch==2:
        a=array.array('h',pcm); pcm=array.array('h',[(a[k]+a[k+1])//2 for k in range(0,len(a),2)]).tobytes()
    fmt=struct.pack('<HHIIHHHHI',0xFFFE,1,rate,rate*2,2,16,6,0,0x4101)
    digest=hashlib.md5(pcm).digest()
    body=b'WAVE'+b'fmt '+struct.pack('<I',len(fmt))+fmt+b'hash'+struct.pack('<I',16)+digest+b'data'+struct.pack('<I',len(pcm))+pcm
    return b'RIFF'+struct.pack('<I',len(body))+body,digest[:4]
def main():
    shell,out,wavs=sys.argv[1],sys.argv[2],sys.argv[3:]
    d=open(shell,'rb').read(); i=d.find(b'BKHD'); size=u32(d,i-12); b=d[i:i+size]
    ch={}; o=0
    while o<len(b): ch[b[o:o+4]]=(o+8,u32(b,o+4)); o+=8+u32(b,o+4)
    assert list(ch)==[b'BKHD',b'DIDX',b'DATA',b'HIRC'],list(ch)
    o,_=ch[b'HIRC']; n=u32(b,o); p=o+4; items=[]
    for _ in range(n): s=u32(b,p+1); items.append((b[p],b[p+5:p+5+s])); p+=5+s
    byid={u32(x,0):(t,x) for t,x in items}
    t,ev=byid[SHELL_EVENT]; assert t==4 and ev[4]==1
    t,act=byid[u32(ev,5)]; assert t==3 and act[4:6]==b'\x03\x04'
    assert u32(act,14)==fnv('wep_melee_ceremonial_sword')
    cid=u32(act,6); t,con=byid[cid]; assert t==5
    # the container's children: [u32 n][ids][u16 n][(id, weight)] at its end
    kids=None
    for k in range(1,32):
        at=len(con)-(4+k*4+2+k*8)
        if at>0 and u32(con,at)==k and struct.unpack_from('<H',con,at+4+k*4)[0]==k: kids=at; nk=k
    assert kids,'the container\'s child list'
    t,snd=byid[u32(con,kids+4)]; assert t==2 and snd[4:8]==bytes.fromhex('01000400') and snd.count(struct.pack('<I',cid))==1
    # what it needs kept: its mixer chain, and what those name (attenuations, effects)
    keep=set(); todo=[u32(con,12)]
    while todo:
        k=todo.pop()
        if k in keep or k not in byid: continue
        t,x=byid[k]
        if t in (2,3,4,5,6,9): continue
        keep.add(k)
        if t==7: todo.append(u32(x,12))
        else: todo+= [u32(x,q) for q in range(4,len(x)-3)]
    for k in (con,snd): todo+=[u32(k,q) for q in range(4,len(k)-3)]
    while todo:
        k=todo.pop()
        if k in keep or k not in byid or byid[k][0] not in (14,16,17): continue
        keep.add(k)
    newcid=fnv(BANK+'/container'); bankid=fnv(BANK)
    media=[]; sounds=[]
    for j,path in enumerate(wavs):
        data,h4=wem(path); sid=fnv('%s/sound_%02d'%(BANK,j+1)); mid=fnv('%s/media_%02d'%(BANK,j+1))&0x3fffffff
        s=bytearray(snd); struct.pack_into('<I',s,0,sid); s[4:8]=bytes.fromhex('01000100'); struct.pack_into('<I',s,9,mid)
        s[13:17]=h4; struct.pack_into('<I',s,17,len(data)); s=bytes(s).replace(struct.pack('<I',cid),struct.pack('<I',newcid))
        sounds.append((sid,s)); media.append((mid,data))
    c=bytearray(con[:kids]); struct.pack_into('<I',c,0,newcid)
    c+=struct.pack('<I',len(sounds))+b''.join(struct.pack('<I',s) for s,_ in sorted(sounds))
    c+=struct.pack('<H',len(sounds))+b''.join(struct.pack('<Ii',s,50000) for s,_ in sorted(sounds))
    a=bytearray(act); aid=fnv(BANK+'/action'); struct.pack_into('<I',a,0,aid); struct.pack_into('<I',a,6,newcid); struct.pack_into('<I',a,14,bankid)
    e=struct.pack('<IBI',fnv(EVENT),1,aid)
    hirc=[(2,s) for _,s in sorted(sounds)]+[(5,bytes(c))]+[(t,x) for t,x in items if u32(x,0) in keep]+[(3,bytes(a)),(4,e)]
    ids=[u32(x,0) for _,x in hirc]; assert len(set(ids))==len(ids)
    for k in ids[:len(sounds)+1]+[aid,fnv(EVENT)]: assert k not in byid
    H=struct.pack('<I',len(hirc))+b''.join(bytes([t])+struct.pack('<I',len(x))+x for t,x in hirc)
    media.sort(); didx=b''; data=b''
    for mid,w in media:
        data+=b'\0'*(-len(data)%16); didx+=struct.pack('<III',mid,len(data),len(w)); data+=w
    o,s=ch[b'BKHD']; hd=bytearray(b[o:o+s]); assert u32(hd,4)==fnv('wep_melee_ceremonial_sword')^XOR; struct.pack_into('<I',hd,4,bankid^XOR)
    chunk=lambda tag,x: tag+struct.pack('<I',len(x))+x
    bank=chunk(b'BKHD',bytes(hd))+chunk(b'DIDX',didx)+chunk(b'DATA',data)+chunk(b'HIRC',H)
    rid=mm(PATH.encode())
    res=d[i-16:i-12]+struct.pack('<IQ',len(bank),rid)+bank
    name=PATH.encode()+b'\0'; dep=d[i-16:i-12]+struct.pack('<I',len(name))+name
    al=lambda v,a: (v+a-1)&~(a-1)
    off0=al(0x48+2*32+2*80,256); off1=al(off0+len(res),16)
    hdr=bytearray(0x48); struct.pack_into('<III',hdr,0,ARCHIVE_MAGIC,2,2); struct.pack_into('<Q',hdr,0x20,al(len(res),256)+al(len(dep),256))
    types=b''.join(struct.pack('<QQQII',0,t,1,0x10,0x40) for t in (T_BANK,T_DEP))
    ent=lambda t,off,size,idx: struct.pack('<QQQQQQQIIIIII',rid,t,off,0,0,0,0,size,0,0,0x10,0x40,idx)
    f=bytes(hdr)+types+ent(T_BANK,off0,len(res),0)+ent(T_DEP,off1,len(dep),1)
    f+=b'\0'*(off0-len(f))+res; f+=b'\0'*(off1-len(f))+dep
    # the shell's own file rebuilt the same way must come out byte for byte (the layout is right)
    open(out,'wb').write(f)
    print('bank %08x resource %016x event %s %08x container %08x sounds %d kept %d bank %d bytes patch %d'%(bankid,rid,EVENT,fnv(EVENT),newcid,len(sounds),len(keep),len(bank),len(f)))
    for k in sorted(keep): print('  kept type %d %08x %d bytes'%(byid[k][0],k,len(byid[k][1])))
main()
