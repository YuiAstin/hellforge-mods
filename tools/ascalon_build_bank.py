#!/usr/bin/env python3
"""Adds Ascalon's parry sound to game banks that are loaded in a mission (by default the Hellbomb objective's and the
shared stratagem bank): one event, ascalon_parry, playing one of our recordings at random. Its structure is the CQC-2
Saber bank's (a 2D one-shot random container on the player bus, with its mixer chain) under new ids; the recordings
are PCM media. The result replaces each host bank's resource, and the game's event records with ascalon_parry added
(the Wwise plugin's has_event answers from them). The same objects in two banks are fine: the sound engine keeps one.
Usage: build_bank.py <Saber patch> <metadata patch> <out patch> <host name>=<host patch>... -- <wav>...
(the patches: HellForge.Cli build-data --out, an include of wwise_bank and wwise_dep of the Saber 0x550387E4C38E63B4
and of each host, obj_hellbomb 0xD2C617C603DE1B92, stratagems_shared 0x2248B2D9A8433820; and of wwise_metadata
0x11AB54001F79F739)"""
import struct,sys,wave,hashlib,array
BANK='ascalon_parry'; EVENT='ascalon_parry'
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
    a=sys.argv[1:]; cut=a.index('--'); shell,meta,out=a[0],a[1],a[2]; hosts=[h.split('=',1) for h in a[3:cut]]; wavs=a[cut+1:]
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
    # the container's mixers, up to the top, and the attenuations and effects they name
    def parent(x):
        n=x[5]; o=4+2+(1+n*6 if n else 0)+2
        return u32(x,o+4)
    assert parent(con)==u32(con,12)
    chain=[]; k=parent(con)
    while k in byid:
        t,x=byid[k]; assert t==7,(hex(k),t); chain.append(k); k=parent(x)
    extra=[]
    for x in [con,snd]+[byid[k][1] for k in chain]:
        for q in range(4,len(x)-3):
            v=u32(x,q)
            if v in byid and byid[v][0] in (14,16,17) and v not in extra: extra.append(v)
    chain=extra+chain
    newcid=fnv(BANK+'/container')
    media=[]; sounds=[]
    for j,path in enumerate(wavs):
        data,h4=wem(path); sid=fnv('%s/sound_%02d'%(BANK,j+1)); mid=fnv('%s/media_%02d'%(BANK,j+1))&0x3fffffff
        s=bytearray(snd); struct.pack_into('<I',s,0,sid); s[4:8]=bytes.fromhex('01000100'); struct.pack_into('<I',s,9,mid)
        s[13:17]=h4; struct.pack_into('<I',s,17,len(data)); s=bytes(s).replace(struct.pack('<I',cid),struct.pack('<I',newcid))
        sounds.append((sid,s)); media.append((mid,data))
    sounds.sort(); media.sort()
    c=bytearray(con[:kids]); struct.pack_into('<I',c,0,newcid)
    c+=struct.pack('<I',len(sounds))+b''.join(struct.pack('<I',s) for s,_ in sounds)
    c+=struct.pack('<H',len(sounds))+b''.join(struct.pack('<Ii',s,50000) for s,_ in sounds)
    aid=fnv(BANK+'/action'); e=struct.pack('<IBI',fnv(EVENT),1,aid)
    order={u32(x,0):n for n,(_,x) in enumerate(items)}
    chunk=lambda tag,x: tag+struct.pack('<I',len(x))+x
    al=lambda v,a: (v+a-1)&~(a-1)
    def patch(files):
        n=len(files); types=[]
        for t,_,_ in files:
            if t not in types: types.append(t)
        off=al(0x48+len(types)*32+n*80,256); cum=0; ents=b''; body=b''; start=off
        for idx,(t,r,x) in enumerate(files):
            if idx: off=al(off,16)
            ents+=struct.pack('<QQQQQQQIIIIII',r,t,off,0,0,cum,0,len(x),0,0,0x10,0x40,idx)
            body+=b'\0'*(off-start-len(body))+x; off+=len(x); cum+=al(len(x),256)
        hdr=bytearray(0x48); struct.pack_into('<III',hdr,0,ARCHIVE_MAGIC,len(types),n); struct.pack_into('<Q',hdr,0x20,cum)
        tt=b''.join(struct.pack('<QQQII',0,t,sum(1 for q,_,_ in files if q==t),0x10,0x40) for t in types)
        f=bytes(hdr)+tt+ents
        return f+b'\0'*(start-len(f))+body
    banks=[]; deps=[]
    for HOST,hpath in hosts:
        hd_,hi,hb,hch,hitems=bank_of(hpath)
        hids={u32(x,0) for _,x in hitems}
        o,s=hch[b'BKHD']; hostid=fnv(HOST); assert u32(hb,o+4)==hostid^XOR,'the host bank is not '+HOST
        a=bytearray(act); struct.pack_into('<I',a,0,aid); struct.pack_into('<I',a,6,newcid); struct.pack_into('<I',a,14,hostid)
        # in the Saber bank's own order (children before their parents, as the sound engine reads them)
        mine=[(order[u32(snd,0)],n,(2,s)) for n,(_,s) in enumerate(sounds)]+[(order[cid],0,(5,bytes(c)))]
        mine+=[(order[k],0,byid[k]) for k in chain if k not in hids]
        add=[x for _,_,x in sorted(mine,key=lambda m:m[:2])]+[(3,bytes(a)),(4,e)]
        ids=[u32(x,0) for _,x in add]; assert len(set(ids))==len(ids) and not (set(ids)&hids)
        for k in [sid for sid,_ in sounds]+[newcid,aid,fnv(EVENT)]: assert k not in byid and k not in hids
        hirc=hitems+add
        H=struct.pack('<I',len(hirc))+b''.join(bytes([t])+struct.pack('<I',len(x))+x for t,x in hirc)
        o,s=hch[b'DIDX']; do,ds=hch[b'DATA']; data=hb[do:do+ds]
        entries=[struct.unpack_from('<III',hb,o+q) for q in range(0,s,12)]; have={m for m,_,_ in entries}
        for mid,w in media:
            assert mid not in have
            data+=b'\0'*(-len(data)%16); entries.append((mid,len(data),len(w))); data+=w
        didx=b''.join(struct.pack('<III',*x) for x in sorted(entries))
        o,s=hch[b'BKHD']
        bank=chunk(b'BKHD',hb[o:o+s])+chunk(b'DIDX',didx)+chunk(b'DATA',data)+chunk(b'HIRC',H)
        PATH='content/audio/'+HOST; rid=mm(PATH.encode()); assert struct.unpack_from('<Q',hd_,hi-8)[0]==rid
        name=PATH.encode()+b'\0'; dep=hd_[hi-16:hi-12]+struct.pack('<I',len(name))+name
        assert hd_.endswith(dep),'the host patch\'s dep resource'
        # the patch writer is right when the host's own patch comes out as HellForge built it, byte for byte
        assert patch([(T_BANK,rid,hd_[hi-16:hi+len(hb)]),(T_DEP,rid,dep)])==hd_,'the patch layout differs from HellForge\'s'
        banks.append((T_BANK,rid,hd_[hi-16:hi-12]+struct.pack('<IQ',len(bank),rid)+bank)); deps.append((T_DEP,rid,dep))
        print('host %s %016x: %d + %d objects, %d + %d media, bank %d -> %d bytes'%(HOST,rid,len(hitems),len(add),len(have),len(media),len(hb),len(bank)))
    # the event's record in the game's event metadata
    md=open(meta,'rb').read(); T_META=0xD50A8B7E1C82B110; META=0x11AB54001F79F739
    assert struct.unpack_from('<QQ',md,0x48+32)==(META,T_META)
    mo=struct.unpack_from('<Q',md,0x48+32+16)[0]; ms=struct.unpack_from('<I',md,0x48+32+56)[0]; mraw=md[mo:mo+ms]
    assert mraw[:4]==d[i-16:i-12] and u32(mraw,4)==ms-8 and (ms-8)%28==0
    have_ev={u32(mraw,q) for q in range(8,ms,28)}; assert fnv(EVENT) not in have_ev
    shell_rec=[mraw[q:q+28] for q in range(8,ms,28) if u32(mraw,q)==SHELL_EVENT][0]
    longest=max(len(w) for _,w in media)/96000.0; shortest=min(len(w) for _,w in media)/96000.0
    rec=struct.pack('<I',fnv(EVENT))+shell_rec[4:8]+struct.pack('<ff',longest,shortest)+shell_rec[16:]
    mnew=mraw[:4]+struct.pack('<I',ms-8+28)+mraw[8:]+rec
    assert patch([(T_META,META,mraw)])==md,'the patch layout differs from HellForge\'s (metadata)'
    f=patch(banks+deps+[(T_META,META,mnew)]); open(out,'wb').write(f)
    print('event %s %08x, %d recordings, metadata %d + 1 events, patch %d bytes'%(EVENT,fnv(EVENT),len(media),len(have_ev),len(f)))
main()
