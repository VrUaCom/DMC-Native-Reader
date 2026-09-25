import struct, sys
def u32(b,o): return struct.unpack_from('<I',b,o)[0]
def pac_slots(b):
    assert b[:4]==b'PAC\0'
    n=u32(b,4); offs=[u32(b,8+4*i) for i in range(n)]
    ends=sorted(set(offs+[len(b)]))
    out=[]
    for o in offs:
        e=min(x for x in ends if x>o); out.append(b[o:e])
    return out
def mips(w,h):
    d=max(w,h); c=1
    while d>1: d//=2; c+=1
    return c
def payload(w,h,m,bb):
    t=0
    for _ in range(m):
        t+=max(1,(w+3)//4)*max(1,(h+3)//4)*bb; w=max(1,w//2); h=max(1,h//2)
    return t
def expected(desc_bytes_dds, aux):
    d=desc_bytes_dds
    h=u32(d,12); w=u32(d,16); m=u32(d,28); cc=d[84:88]
    if cc==b'DXT1': fmt,low,bpw,bb=0,0x86,2,8
    elif cc==b'DXT5': fmt,low,bpw,bb=4,0x88,4,16
    else: return None
    pl=payload(w,h,m,bb)
    e={0x08:0x20000|(m<<8)|low,0x0C:0xAAE4,0x10:(h<<16)|w,0x14:1,0x18:w*bpw,0x20:0x40,0x38:pl,
       0x60:fmt,0x64:128+pl,0x68:8}
    for z in (0,4,0x1C,0x24,0x28,0x2C,0x30,0x34,0x50,0x54,0x58,0x5C,0x6C): e[z]=0
    return e,(w,h,m,cc.decode(),pl)
def check(ptx, label):
    n=u32(ptx,0); print(f"== {label}: {n} textures, {len(ptx)} bytes")
    off=0x800
    for i in range(n):
        span=u32(ptx,4+4*i)
        dds=ptx[off+0x70:]
        ex=expected(dds,None)
        if ex is None: print(i,'unsupported',dds[84:88]); break
        e,(w,h,m,cc,pl)=ex
        bad=[]
        for k,v in sorted(e.items()):
            got=u32(ptx,off+k)
            if got!=v: bad.append(f"+{k:02X}={got:#x}(want {v:#x})")
        # secondary dims, reciprocals, aux
        sec=u32(ptx,off+0x44); sw,sh=sec&0xFFFF,sec>>16
        ok_sec = (sw,sh) in ((w,h),(w//2,h//2))
        if not ok_sec: bad.append(f"+44 sec={sw}x{sh}")
        else:
            rw=struct.unpack('<I',struct.pack('<f',1.0/sw))[0]; rh=struct.unpack('<I',struct.pack('<f',1.0/sh))[0]
            if u32(ptx,off+0x48)!=rw: bad.append(f"+48={u32(ptx,off+0x48):#x}(want {rw:#x})")
            if u32(ptx,off+0x4C)!=rh: bad.append(f"+4C={u32(ptx,off+0x4C):#x}(want {rh:#x})")
        am,av=u32(ptx,off+0x3C),u32(ptx,off+0x40)
        if am>2 or ((am==0)!=(av==0)): bad.append(f"aux={am},{av}")
        # dds header mip count sanity
        if m!=mips(w,h): bad.append(f"dds mips {m} != full {mips(w,h)}")
        print(f" t{i}: {w}x{h} {cc} mips {m} sec {sw}x{sh} aux {am},{av:#x} span {span} -> {'OK' if not bad else ' '.join(bad)}")
        off+=span*0x800 if span else 0
if __name__ == '__main__':
    for path in sys.argv[1:]:
        b=open(path,'rb').read(); check(pac_slots(b)[0],path)
