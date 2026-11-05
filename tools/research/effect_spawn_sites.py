#!/usr/bin/env python3
"""Catalog of the effect spawn sites of dmc3.exe (HD Collection, PC).

Usage: effect_spawn_sites.py <dmc3.exe> <out.inc>

Finds every call of the effect spawn API (0x1402E7A90, 0x1402E7AB0,
0x1402E7CA0) and of the P/E/G/V factories. For each call it writes:
- the class that owns the calling function (RTTI vtables, nearest method);
- the kind and the id, by folding constants back along every path into the
  call;
- the prep mode or flags.

Only addresses, class names, kinds and ids leave this script. objdump must
be installed. The output is a C++ initializer list for
modules/effect_spawn_sites.cpp.
"""
import bisect, collections, re, struct, subprocess, sys

EXE, OUT = sys.argv[1], sys.argv[2]
d = open(EXE, 'rb').read()
B = 0x140000000
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
opt = struct.unpack_from('<H', d, pe + 20)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + opt + 40 * i
    secs.append((d[o:o + 8].rstrip(b'\0').decode(),) + struct.unpack_from('<IIII', d, o + 8))

# 1. Instructions, function starts, call edges.
asm = subprocess.run(['objdump', '-d', '-M', 'intel', '--no-show-raw-insn', '-j', '.text', EXE],
                     capture_output=True, text=True, check=True).stdout
ins = []
for line in asm.splitlines():
    m = re.match(r'\s+([0-9a-f]+):\s+(.*)', line)
    if m: ins.append((int(m.group(1), 16), m.group(2).strip()))
calls = set()
for a, t in ins:
    m = re.match(r'call\s+0x([0-9a-f]+)', t)
    if m: calls.add(int(m.group(1), 16))
starts = set(calls)
for i in range(1, len(ins)):
    if ins[i - 1][1].startswith('int3') and not ins[i][1].startswith('int3'): starts.add(ins[i][0])
    if ins[i - 1][1].startswith('ret') and not ins[i][1].startswith(('int3', 'nop', 'xchg')): starts.add(ins[i][0])
starts = sorted(starts)
def off2rva(off):
    for n,vs,va,rs,ra in secs:
        if ra<=off<ra+rs: return va+off-ra
text=[s for s in secs if s[0]=='.text'][0]; T0,T1=B+text[2],B+text[2]+text[1]
# RTTI: type descriptors -> COL (sig 1, pSelf) -> vtable
names={}
for m in re.finditer(rb'\.\?AV([A-Za-z0-9_]+)@@\x00',d):
    td=off2rva(m.start()-16); names[td]=m.group(1).decode()
cols={}
for off in range(0,len(d)-24,4):
    if d[off:off+4]!=b'\x01\x00\x00\x00': continue
    sig,o1,o2,ptd,pcd,pself=struct.unpack_from('<IIIIII',d,off)
    if ptd in names and pself==off2rva(off): cols[B+pself]=(names[ptd],o1)
methods=collections.defaultdict(set)
for colva,(name,o1) in cols.items():
    for m in re.finditer(re.escape(struct.pack('<Q',colva)),d):
        v=m.start()+8; n=0
        while v+8<=len(d):
            q=struct.unpack_from('<Q',d,v)[0]
            if not (T0<=q<T1): break
            methods[name].add(q); v+=8; n+=1
            if n>400: break
owner=sorted((a,n) for n,s in methods.items() for a in s)
oaddr=[a for a,_ in owner]
def func_of(a):
    i=bisect.bisect_right(starts,a)-1; return starts[i]
def class_of(f):
    i=bisect.bisect_right(oaddr,f)-1; j=i+1
    cands=[]
    if i>=0: cands.append((f-oaddr[i],owner[i][1]))
    if j<len(owner): cands.append((oaddr[j]-f,owner[j][1]))
    # exact method
    for a,n in owner:
        pass
    return min(cands)[1] if cands else '?'
exact={}
for a,n in owner: exact.setdefault(a,set()).add(n)
API={0x1402E7A90:('a90',None),0x1402E7AB0:('ab0',None),0x1402E7CA0:('ca0',None),0x140312720:('fac','P'),0x1402E3B10:('fac','E'),0x1402EBA10:('fac','G'),0x140324460:('fac','V')}
K='PEGV'
rows=[]
for i,(a,t) in enumerate(ins):
    m=re.match(r'(?:call|jmp)\s+0x([0-9a-f]+)',t)
    if not m: continue
    tgt=int(m.group(1),16)
    if tgt not in API: continue
    f=func_of(a)
    if f in API: continue  # the API's own wrappers
    api,fk=API[tgt]
    reg={}
    for j in range(i-1,max(i-40,0),-1):
        tt=ins[j][1]
        if re.match(r'(call|ret|jmp)\b',tt): break
        for r in ('ecx','edx','r8d','r9d'):
            if r in reg: continue
            mm=re.match(r'mov\s+'+r+r',0x([0-9a-f]+)$',tt)
            if mm: reg[r]=int(mm.group(1),16); continue
            if re.match(r'xor\s+'+r+','+r+'$',tt): reg[r]=0; continue
            if re.match(r'(mov|lea|movzx|movsx|add|sub|or|and|xor|inc|dec|imul)\s+'+r+r'\b',tt): reg[r]=None
    kind=fk if fk else (K[reg['ecx']] if reg.get('ecx') is not None and reg['ecx']<4 else None)
    idv=reg.get('edx')
    prep=reg.get('r9d') if api in ('a90','ab0') else None
    flags=reg.get('r9d') if api=='ca0' else None
    cls=sorted(exact[f])[0] if f in exact else class_of(f)
    rows.append((a,f,cls,api,kind,idv,prep,flags))
idx={a:i for i,(a,_) in enumerate(ins)}
into=collections.defaultdict(list)
for i,(a,t) in enumerate(ins):
    m=re.match(r'j[a-z]+\s+0x([0-9a-f]+)',t)
    if m: into[int(m.group(1),16)].append(i)
R64={'rcx':'ecx','rdx':'edx','r8':'r8d','r9':'r9d','rax':'eax','rbx':'ebx','rsi':'esi','rdi':'edi','rbp':'ebp','r10':'r10d','r11':'r11d','r12':'r12d','r13':'r13d','r14':'r14d','r15':'r15d'}
def r32(r): return R64.get(r,r)
def paths(i,depth=48):
    out=[]
    def walk(j,seq,d,seen):
        if len(out)>64: return
        if d==0 or j<0: out.append(seq); return
        a,t=ins[j]
        if (re.match(r'(jmp|ret)\b',t) or t.startswith('int3')) and seq: 
            # not a fall-through predecessor
            srcs=into.get(ins[j+1][0],[])
            if not srcs: out.append(seq); return
            for s in srcs:
                if s in seen: continue
                walk(s-1,[ins[s]]+seq if not ins[s][1].startswith('j') else seq,d-1,seen|{s})
            return
        if re.match(r'call\b',t) and seq: out.append(seq); return
        nseq=[ins[j]]+seq
        # other entries into this instruction
        for s in into.get(a,[]):
            if s in seen or s==j-1: continue
            walk(s-1,nseq if True else nseq,d-1,seen|{s})
        walk(j-1,nseq,d-1,seen)
    walk(i-1,[],depth,frozenset())
    return out
def evaluate(seq):
    v={}
    for a,t in seq:
        m=re.match(r'(\w+)\s+(\w+),?(.*)',t)
        if not m: continue
        op,dst,src=m.group(1),r32(m.group(2)),m.group(3).strip()
        val=None; known=False
        if op=='mov' and re.match(r'0x[0-9a-f]+$',src): val=int(src,16); known=True
        elif op=='mov' and r32(src) in v: val=v[r32(src)]; known=True
        elif op=='xor' and r32(src)==dst: val=0; known=True
        elif op=='lea':
            mm=re.match(r'\[(\w+)([+-])0x([0-9a-f]+)\]$',src)
            if mm and r32(mm.group(1)) in v:
                val=v[r32(mm.group(1))]+(int(mm.group(3),16)*(1 if mm.group(2)=='+' else -1)); known=True
        elif op in('add','sub') and dst in v and re.match(r'0x[0-9a-f]+$',src):
            val=v[dst]+(int(src,16) if op=='add' else -int(src,16)); known=True
        elif op=='inc' and dst in v: val=v[dst]+1; known=True
        if known: v[dst]=val
        elif dst in v and op not in ('cmp','test','push','jmp','call') and not op.startswith('j'): del v[dst]
    return v
K='PEGV'
out=[]
for (a,f,cls,api,kind,idv,prep,flags) in rows:
    combos=set()
    for seq in paths(idx[a]):
        v=evaluate(seq)
        k=kind if kind and api=='fac' else (K[v['ecx']] if v.get('ecx') in (0,1,2,3) else None)
        if api=='fac': k=kind
        combos.add((k,v.get('edx'),v.get('r9d') if api!='fac' else None))
    good={c for c in combos if c[0] and c[1] is not None}
    out.append((a,f,cls,api,sorted(good) if good else sorted(combos,key=str)[:1],bool(good)))

CALL = {'a90': 0, 'ab0': 1, 'ca0': 2, 'fac': 3}
with open(OUT, 'w') as f:
    f.write('// Generated by tools/research/effect_spawn_sites.py from dmc3.exe; do not edit.\n')
    f.write('// {call site, function, owner, kind, id, call, mode}\n')
    for (a, fn, cls, api, combos, good) in out:
        for (k, i, mode) in (combos if good else [(None, None, None)]):
            kk = "'%s'" % k if k else "'?'"
            ii = '%d' % i if i is not None and 0 <= i < 0x10000 else '0xFFFF'
            mm = '%d' % mode if mode is not None else '-1'
            f.write('    {0x%X, 0x%X, "%s", %s, %s, %d, %s},\n' % (a, fn, cls, kk, ii, CALL[api], mm))
print('sites', len(out), 'resolved', sum(1 for r in out if r[5]))
