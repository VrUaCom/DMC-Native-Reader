"""pl011: move the skirt (and its side ribbon) out of the body MOD into the
slot-12 coat MOD as cloth. The coat root follows body joint 3 (0x1402120A7);
eight waist chains (anchor + 3 links, local -Y toward the hem, Y toward the
parent as the CLT 'Y' axis expects) carry the skirt; the thigh capsules of
the player coat (joints 15/16/19/20) push block 0 out of the legs."""
import math, struct, sys
import numpy as np
from fixmod import pac_slots, build_pac, shw_for
from modwriter import Mesh, Obj, Mod, read_mod, write_mod, transform_record

def mesh_arrays(me):
    n = me.count
    pos = np.frombuffer(me.pos, '<f4').reshape(n, 3).astype(np.float64)
    nrm = np.frombuffer(me.nrm, '<f4').reshape(n, 3).astype(np.float64)
    uv = np.frombuffer(me.uv, '<i2').reshape(n, 2).astype(np.float64) / 4096.0
    bl = np.frombuffer(me.blend, 'u1').reshape(n, 4)
    ctl = np.frombuffer(me.ctl, '<u2')
    return pos, nrm, uv, bl, ctl

def weights(ctl):
    c = ctl.astype(np.int64) & 0x7fff
    return np.stack([c & 31, (c >> 5) & 31, (c >> 10) & 31], 1)

# fixed.pac: output of fixmod.py; pl000.pac: Dante (coat header, SHW headers).
FIXED, DANTE, OUT = sys.argv[1:4]

CHAINS, LINKS = 8, 3
STIFFNESS = 0.35

slots = pac_slots(open(FIXED, 'rb').read())
dante = pac_slots(open(DANTE, 'rb').read())
body = read_mod(slots[1])
me = body.objects[2].meshes[0]
pos, nrm, uv, bl, ctl = mesh_arrays(me)
W = weights(ctl); Bn = bl[:, 1:4] // 4
dom = Bn[np.arange(len(Bn)), W.argmax(1)]
br = (ctl & 0x8000) != 0
assert all(list(br[i:i + 3]) == [True, True, False] for i in range(0, me.count, 3)), 'mesh 2 is not a triangle list'
tris = [(i, i + 1, i + 2) for i in range(0, me.count, 3)]

# ---- components (welded by position) ------------------------------------
key = {}
vid = [key.setdefault(tuple(np.round(p, 3)), len(key)) for p in pos]
par = list(range(len(key)))
def find(x):
    while par[x] != x: par[x] = par[par[x]]; x = par[x]
    return x
for a, b, c in tris:
    par[find(vid[a])] = find(vid[b]); par[find(vid[b])] = find(vid[c])
comp = [find(vid[t[0]]) for t in tris]
groups = {}
for k, r in enumerate(comp): groups.setdefault(r, []).append(k)
skirt, ribbon = None, None
for r, ks in groups.items():
    vs = [i for k in ks for i in tris[k]]
    P = pos[vs]; U = uv[vs]
    if P[:, 1].min() < 80 or P[:, 1].max() > 117: continue
    if U[:, 0].max() < 0.16 and 0.32 < U[:, 1].min() and U[:, 1].max() < 0.51: skirt = ks
    elif len(ks) < 40 and P[:, 0].min() > 10: ribbon = ks
assert skirt and ribbon, (skirt, ribbon)
cut = set(skirt) | set(ribbon)
print('skirt triangles', len(skirt), 'ribbon triangles', len(ribbon))

# ---- body without the skirt ---------------------------------------------
keep = [i for k, t in enumerate(tris) if k not in cut for i in t]
def take(me_, idx):
    out = Mesh(); out.record = me_.record
    for kind, size in (('pos', 12), ('nrm', 12), ('uv', 4), ('blend', 4), ('ctl', 2)):
        src = getattr(me_, kind)
        setattr(out, kind, b''.join(src[i * size:(i + 1) * size] for i in idx))
    return out
body.objects[2].meshes[0] = take(me, keep)
body_bytes = write_mod(body)

# ---- rest world of body joint 3 -----------------------------------------
T = [struct.unpack('<8f', t) for t in body.transforms]
world = {}
for i, n in enumerate(body.order):
    p = body.parent[i]
    base = world[p] if p != 255 else np.zeros(3, np.float32)
    world[n] = (base + np.array(T[n][:3], np.float32)).astype(np.float32)
J3 = world[3].astype(np.float64)
print('joint 3 rest', J3)

# ---- chain layout from the skirt shape ----------------------------------
sk = sorted({i for k in skirt for i in tris[k]})
P = pos[sk]
cx, cz = P[:, 0].mean(), P[:, 2].mean()
ang = np.degrees(np.arctan2(P[:, 0] - cx, P[:, 2] - cz)) % 360
rad = np.hypot(P[:, 0] - cx, P[:, 2] - cz)
ytop, ybot = P[:, 1].max(), P[:, 1].min()
chains = []
for c in range(CHAINS):
    th = c * 360.0 / CHAINS
    m = np.abs(((ang - th + 180) % 360) - 180) < 180.0 / CHAINS
    y = P[m, 1]; r = rad[m]
    rt = r[y > y.max() - 3].mean(); rb = r[y < y.min() + 3].mean(); yb = y[y < y.min() + 3].mean()
    h = np.array([math.sin(math.radians(th)), 0.0, math.cos(math.radians(th))])
    top = np.array([cx, ytop, cz]) + h * rt
    bot = np.array([cx, yb, cz]) + h * rb
    D = bot - top; L = np.linalg.norm(D) / LINKS; D /= np.linalg.norm(D)
    chains.append((th, top, D, L))
    print(f'  chain {c}: {th:5.1f} deg anchor {np.round(top,1)} dir {np.round(D,2)} link {L:.2f}')

# ---- coat nodes ----------------------------------------------------------
nodes = 1 + CHAINS * (1 + LINKS)
parent = [255]; transforms = [transform_record(0, 0, 0)]
for c, (th, top, D, L) in enumerate(chains):
    a = top - J3
    u = -D                                  # local +Y (toward the parent)
    sx = math.hypot(u[0], u[2])
    rx = math.atan2(sx, u[1]); ry = math.atan2(u[0], u[2])
    parent.append(0); transforms.append(transform_record(*a, rx, ry, 0.0))
    for k in range(LINKS):
        parent.append(len(parent) - 1); transforms.append(transform_record(0.0, -L, 0.0))

def anchor_node(c): return 1 + c * (1 + LINKS)

# ---- coat skin -------------------------------------------------------------
KNOTS = [0.0] + [k + 0.5 for k in range(LINKS)]     # anchor, n1, n2, n3
def chain_weights(c, v):
    th, top, D, L = chains[c]
    s = float(np.dot(v - top, D)) / L
    out = {}
    if s <= KNOTS[0]: out[0] = 1.0
    elif s >= KNOTS[-1]: out[LINKS] = 1.0
    else:
        for k in range(LINKS):
            if KNOTS[k] <= s <= KNOTS[k + 1]:
                t = (s - KNOTS[k]) / (KNOTS[k + 1] - KNOTS[k])
                out[k] = 1 - t; out[k + 1] = t
                break
    return {anchor_node(c) + k: w for k, w in out.items()}

def skin(v):
    a = (math.degrees(math.atan2(v[0] - cx, v[2] - cz)) % 360) / (360.0 / CHAINS)
    c0 = int(math.floor(a)) % CHAINS; c1 = (c0 + 1) % CHAINS; f = a - math.floor(a)
    acc = {}
    for c, wc in ((c0, 1 - f), (c1, f)):
        for n, w in chain_weights(c, v).items(): acc[n] = acc.get(n, 0) + wc * w
    top3 = sorted(acc.items(), key=lambda x: -x[1])[:3]
    tot = sum(w for _, w in top3)
    q = [w / tot * 31 for _, w in top3]
    qi = [int(x) for x in q]
    for j in sorted(range(len(q)), key=lambda j: -(q[j] - qi[j]))[:31 - sum(qi)]: qi[j] += 1
    pairs = [(n, w) for (n, _), w in zip(top3, qi) if w > 0]
    while len(pairs) < 3: pairs.append((0, 0))
    return pairs

coat_mesh = take(me, [i for k in sorted(cut) for i in tris[k]])
n = coat_mesh.count
cpos = np.frombuffer(coat_mesh.pos, '<f4').reshape(n, 3).astype(np.float64)
blend = bytearray(); ctlw = bytearray(); newpos = bytearray()
for i in range(n):
    pairs = skin(cpos[i])
    blend += bytes([0] + [p[0] * 4 for p in pairs])
    w = pairs[0][1] | (pairs[1][1] << 5) | (pairs[2][1] << 10)
    if i % 3 != 2: w |= 0x8000
    ctlw += struct.pack('<H', w)
    newpos += struct.pack('<3f', *(cpos[i] - J3))
coat_mesh.pos = bytes(newpos); coat_mesh.blend = bytes(blend); coat_mesh.ctl = bytes(ctlw)

coat = Mod()
dcoat = read_mod(dante[12])
coat.header = dcoat.header
obj = Obj()
rec = bytearray(body.objects[2].record)
local = cpos - J3
centre = (local.min(0) + local.max(0)) / 2
radius = float(np.linalg.norm(local - centre, axis=1).max())
struct.pack_into('<4f', rec, 0x30, *centre, radius)
obj.record = bytes(rec); obj.meshes = [coat_mesh]
coat.objects = [obj]
coat.parent = parent; coat.order = list(range(nodes)); coat.adapter = [0] * nodes
coat.transforms = transforms
coat_bytes = write_mod(coat)

# ---- CLT -------------------------------------------------------------------
# Two blocks, as pl001_02.clt: only block 0 gets the six player-coat capsules
# (0x1402151E7). The joint-3 capsule (z +10, r 15, 40 down) sits in front of
# the hips and would shove the front of a short skirt outward, so the front
# chains go to block 1 (no collision); sides and back keep the thigh and
# waist capsules.
FRONT = [c for c in range(CHAINS) if min(c, CHAINS - c) * 360.0 / CHAINS < 60]
blocks = [[c for c in range(CHAINS) if c not in FRONT], FRONT]
clt = ';pl011_02.clt\r\n\r\nClothNum\t2\r\n\r\n\r\n\r\n'
for bi, chs in enumerate(blocks):
    if bi: clt += '\r\n\r\n'
    clt += (f'ClothNo     {bi}\r\nClothId     {bi}\r\n'
            'Gravity     0.000000  -0.010000  0.000000\r\n'
            'SpringForce 0.020000\r\nMaxSpeed    50.000000\r\n'
            f'Stiffness   {STIFFNESS:.6f}\r\n'
            'Wind        0.000000  0.000000  0.000000\r\n'
            'WindLocal   1\r\nWindParent  0\r\nWindType    1\r\n')
    for c in chs:
        for k in range(1, LINKS + 1):
            clt += f'Bone      {anchor_node(c) + k}    Y\r\n'
    clt += 'End\r\n'
clt += '$\r\n'
print('cloth blocks', blocks)
clt_bytes = clt.encode('ascii')
clt_bytes += bytes((-len(clt_bytes)) % 16 + 16)

# ---- shadows ---------------------------------------------------------------
def dump_skin(mod, path, offset=np.zeros(3)):
    with open(path, 'w') as f, open(path + '.pos', 'w') as g:
        for oi, o in enumerate(mod.objects):
            for me_ in o.meshes:
                p_, _, _, b_, c_ = mesh_arrays(me_)
                w_ = weights(c_); d_ = (b_[:, 1:4] // 4)[np.arange(len(b_)), w_.argmax(1)]
                for i in range(me_.count):
                    f.write(f'{oi} {i} {d_[i]}\n'); g.write('%g %g %g\n' % tuple(p_[i] + offset))
dump_skin(read_mod(body_bytes), 'nb_body.txt')
dump_skin(read_mod(coat_bytes), 'nb_coat.txt')
body_groups = [(5, [5, 23]), (3, [3, 6, 10]), (4, [4]), (14, [14]), (15, [15]), (19, [19]),
               (9, [9]), (16, [16]), (17, [17, 18]), (20, [20]), (21, [21, 22]), (8, [8]),
               (7, [7]), (11, [11]), (12, [12]), (13, [13]), (2, [2])]
print('SHW slot 8 (body):')
shw8 = shw_for('nb_body.txt', body_groups, dante[8], len(body.transforms))
print('SHW slot 14 (skirt):')
coat_groups = []
for c in range(CHAINS):
    a = anchor_node(c)
    coat_groups.append((a + 2, [a, a + 1, a + 2, a + 3]))
shw14 = shw_for('nb_coat.txt', coat_groups, dante[14], nodes)

out = list(slots)
out[1] = body_bytes; out[8] = shw8; out[12] = coat_bytes; out[13] = clt_bytes; out[14] = shw14
pac = build_pac(out)
open(OUT, 'wb').write(pac)
print('written', OUT, len(pac), 'bytes', len(out), 'slots; coat', len(coat_bytes), 'body', len(body_bytes))
