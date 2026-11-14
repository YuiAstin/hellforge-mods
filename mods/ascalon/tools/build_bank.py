#!/usr/bin/env python3
"""Adds Ascalon's parry sound to a game bank that is always loaded (the Hellbomb objective's, obj_hellbomb): one event,
ascalon_parry, playing one of our recordings at random. Its structure is the CQC-2 Saber bank's (a 2D one-shot random
container on the player bus, with its mixer chain) under new ids; the recordings are PCM media. The result replaces the
host bank's resource (a bank of its own, 0.4.1, was never loaded: no package lists it). Usage:
build_bank.py <Saber patch> <host patch> <out patch> <wav>...   (both patches: HellForge.Cli build-data --out, an
include of the bank's wwise_bank and wwise_dep: Saber 0x550387E4C38E63B4, host 0xD2C617C603DE1B92)"""
import struct,sys,wave,hashlib,array
BANK='ascalon_parry'; EVENT='ascalon_parry'; HOST='obj_hellbomb'
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
def bank_of(path):
    d=open(path,'rb').read(); i=d.find(b'BKHD'); size=u32(d,i-12); b=d[i:i+size]
    ch={}; o=0
    while o<len(b): ch[b[o:o+4]]=(o+8,u32(b,o+4)); o+=8+u32(b,o+4)
    assert list(ch)==[b'BKHD',b'DIDX',b'DATA',b'HIRC'],list(ch)
    o,_=ch[b'HIRC']; n=u32(b,o); p=o+4; items=[]
    for _ in range(n): s=u32(b,p+1); items.append((b[p],b[p+5:p+5+s])); p+=5+s
    return d,i,b,ch,items
def main():
    shell,host,out,wavs=sys.argv[1],sys.argv[2],sys.argv[3],sys.argv[4:]
    d,i,b,ch,items=bank_of(shell)
    byid={u32(x,0):(t,x) for t,x in items}
    t,ev=byid[SHELL_EVENT]; assert t==4 and ev[4]==1
    t,act=byid[u32(ev,5)]; assert t==3 and act[4:6]==b'\x03\x04'
    assert u32(act,14)==fnv('wep_melee_ceremonial_sword')
    cid=u32(act,6); t,con=byid[cid]; assert t==5
    kids=None
    for k in range(1,32):
        at=len(con)-(4+k*4+2+k*8)
        if at>0 and u32(con,at)==k and struct.unpack_from('<H',con,at+4+k*4)[0]==k: kids=at
    assert kids,'the container\'s child list'
    t,snd=byid[u32(con,kids+4)]; assert t==2 and snd[4:8]==bytes.fromhex('01000400') and snd.count(struct.pack('<I',cid))==1
    # the container's mixers, up to one of another bank, and the attenuations and effects they name
    def parent(x):
        n=x[5]; o=4+2+(1+n*6 if n else 0)+2
        return u32(x,o+4)
    assert parent(con)==u32(con,12)
    chain=[]; k=parent(con)
    while k in byid:
        t,x=byid[k]; assert t==7,(hex(k),t); chain.append(k); k=parent(x)
    top=k; extra=[]
    for x in [con,snd]+[byid[k][1] for k in chain]:
        for q in range(4,len(x)-3):
            v=u32(x,q)
            if v in byid and byid[v][0] in (14,16,17) and v not in extra: extra.append(v)
    chain=extra+chain[::-1]
    hd_,hi,hb,hch,hitems=bank_of(host)
    hids={u32(x,0) for _,x in hitems}
    o,s=hch[b'BKHD']; hostid=fnv(HOST); assert u32(hb,o+4)==hostid^XOR,'the host bank is not '+HOST
    newcid=fnv(BANK+'/container')
    media=[]; sounds=[]
    for j,path in enumerate(wavs):
        data,h4=wem(path); sid=fnv('%s/sound_%02d'%(BANK,j+1)); mid=fnv('%s/media_%02d'%(BANK,j+1))&0x3fffffff
        s=bytearray(snd); struct.pack_into('<I',s,0,sid); s[4:8]=bytes.fromhex('01000100'); struct.pack_into('<I',s,9,mid)
        s[13:17]=h4; struct.pack_into('<I',s,17,len(data)); s=bytes(s).replace(struct.pack('<I',cid),struct.pack('<I',newcid))
        sounds.append((sid,s)); media.append((mid,data))
    sounds.sort()
    c=bytearray(con[:kids]); struct.pack_into('<I',c,0,newcid)
    c+=struct.pack('<I',len(sounds))+b''.join(struct.pack('<I',s) for s,_ in sounds)
    c+=struct.pack('<H',len(sounds))+b''.join(struct.pack('<Ii',s,50000) for s,_ in sounds)
    a=bytearray(act); aid=fnv(BANK+'/action'); struct.pack_into('<I',a,0,aid); struct.pack_into('<I',a,6,newcid); struct.pack_into('<I',a,14,hostid)
    e=struct.pack('<IBI',fnv(EVENT),1,aid)
    # in the Saber bank's own order (children before their parents, as the sound engine reads them)
    order={u32(x,0):n for n,(_,x) in enumerate(items)}
    mine=[(order[u32(snd,0)],n,(2,s)) for n,(_,s) in enumerate(sounds)]+[(order[cid],0,(5,bytes(c)))]
    mine+=[(order[k],0,byid[k]) for k in chain if k not in hids]
    add=[x for _,_,x in sorted(mine,key=lambda m:m[:2])]+[(3,bytes(a)),(4,e)]
    ids=[u32(x,0) for _,x in add]; assert len(set(ids))==len(ids) and not (set(ids)&hids)
    for k in [sid for sid,_ in sounds]+[newcid,aid,fnv(EVENT)]: assert k not in byid and k not in hids
    hirc=hitems+add
    H=struct.pack('<I',len(hirc))+b''.join(bytes([t])+struct.pack('<I',len(x))+x for t,x in hirc)
    o,s=hch[b'DIDX']; do,ds=hch[b'DATA']; data=hb[do:do+ds]
    entries=[struct.unpack_from('<III',hb,o+q) for q in range(0,s,12)]; have={m for m,_,_ in entries}
    for mid,w in sorted(media):
        assert mid not in have
        data+=b'\0'*(-len(data)%16); entries.append((mid,len(data),len(w))); data+=w
    didx=b''.join(struct.pack('<III',*x) for x in sorted(entries))
    o,s=hch[b'BKHD']
    chunk=lambda tag,x: tag+struct.pack('<I',len(x))+x
    bank=chunk(b'BKHD',hb[o:o+s])+chunk(b'DIDX',didx)+chunk(b'DATA',data)+chunk(b'HIRC',H)
    PATH='content/audio/'+HOST; rid=mm(PATH.encode()); assert struct.unpack_from('<Q',hd_,hi-8)[0]==rid
    res=hd_[hi-16:hi-12]+struct.pack('<IQ',len(bank),rid)+bank
    name=PATH.encode()+b'\0'; dep=hd_[hi-16:hi-12]+struct.pack('<I',len(name))+name
    assert hd_.endswith(dep),'the host patch\'s dep resource'
    al=lambda v,a: (v+a-1)&~(a-1)
    def patch(res):
        off0=al(0x48+2*32+2*80,256); off1=al(off0+len(res),16)
        hdr=bytearray(0x48); struct.pack_into('<III',hdr,0,ARCHIVE_MAGIC,2,2); struct.pack_into('<Q',hdr,0x20,al(len(res),256)+al(len(dep),256))
        types=b''.join(struct.pack('<QQQII',0,t,1,0x10,0x40) for t in (T_BANK,T_DEP))
        ent=lambda t,off,size,idx,cum: struct.pack('<QQQQQQQIIIIII',rid,t,off,0,0,cum,0,size,0,0,0x10,0x40,idx)
        f=bytes(hdr)+types+ent(T_BANK,off0,len(res),0,0)+ent(T_DEP,off1,len(dep),1,al(len(res),256))
        f+=b'\0'*(off0-len(f))+res; f+=b'\0'*(off1-len(f))+dep
        return f
    # the patch writer is right when the host's own resource comes out as HellForge built it, byte for byte
    assert patch(hd_[hi-16:hi+len(hb)])==hd_,'the patch layout differs from HellForge\'s'
    open(out,'wb').write(patch(res))
    print('carried: %s; above them %08x (another bank\'s)'%(' '.join('%d:%08x'%(byid[k][0],k) for k in chain),top))
    print('host %s %016x: %d + %d objects, %d + %d media; event %s %08x; bank %d -> %d bytes'%(HOST,rid,len(hitems),len(add),len(have),len(media),EVENT,fnv(EVENT),len(hb),len(bank)))
main()
