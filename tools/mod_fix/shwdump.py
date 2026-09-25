import struct,sys
exec(open('ptxcheck.py').read().split('def mips')[0])
def dump(shw,label):
    print('==',label,len(shw),'bytes; header', shw[:0x20].hex(' '))
    n=shw[0x10]
    for h in range(n):
        r=0x20+h*0x40
        vc,tc=struct.unpack_from('<HH',shw,r)
        to,ao,vo,so=struct.unpack_from('<QQQQ',shw,r+0x10)
        sel=set(shw[so:so+vc])
        vs=[struct.unpack_from('<4f',shw,vo+16*i) for i in range(vc)]
        ys=[v[1] for v in vs]
        print(f' h{h:2}: V{vc:3} T{tc:3} tri@{to:#x} adj@{ao:#x} vtx@{vo:#x} sel@{so:#x} joints {sorted(sel)} y {min(ys):.0f}..{max(ys):.0f} w {set(round(v[3],3) for v in vs)} rec {shw[r+4:r+0x10].hex()} {shw[r+0x30:r+0x40].hex()}')
b=open(sys.argv[1],'rb').read(); s=pac_slots(b)
dump(s[8],'slot8'); dump(s[14],'slot14')
