#!/usr/bin/env python3
"""The em000 family's effect events, read by running dmc3.exe's own code.

Usage: em000_events.py <dmc3.exe> <out.inc>    (needs: pip install unicorn)

1. The enemy event handler 0x1401C3130(iface, code, pos) runs for every code
   0..0xCE on a fabricated enemy:
   - actor position (1, 2, 3), yaw 90 degrees;
   - body joint k at x = 10000 + 1000 k (entries of obj+0x6D8);
   - the given position (100, 200, 300).
   Every call of the spawn API is caught. Its placement is read from the
   effect's live parent (+0xC0 / +0xC8, a joint matrix) or from the matrix
   it was given.
2. Every command of CComEm000 (vtable 0x1404C6A18: update in slot 2k+1, start
   in slot 2k+2) runs for 600 motion frames on a fake enemy interface. The
   motion frame restarts whenever the command plays an action. The interface
   records:
   - the commands played (slot +0x20: table 1, +0x28: table 2);
   - the event codes sent (slot +0x80) and the frame each was sent at.
3. The command tables of CEm000..CEm004 (byte pairs bank, action) are copied
   as they are.

Only codes, ids, joints, frames and table entries leave this script.
"""
import re
import struct
import subprocess
import sys

from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED, \
    UC_MEM_FETCH_UNMAPPED
from unicorn.x86_const import UC_X86_REG_RAX, UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_R8, UC_X86_REG_R9, \
    UC_X86_REG_RIP, UC_X86_REG_RSP, UC_X86_REG_XMM0

EXE, OUT = sys.argv[1], sys.argv[2]
B, PAGE = 0x140000000, 0x1000
SPAWN = {0x1402E7A90: 'a90', 0x1402E7AB0: 'ab0', 0x1402E7CA0: 'ca0',
         0x140312720: 'P', 0x1402E3B10: 'E', 0x1402EBA10: 'G', 0x140324460: 'V'}
HANDLER, COMMANDS = 0x1401C3130, 0x1404C6A18
TABLES = [('CEm000', 0x1405A3300, 0x1405A31E8), ('CEm001', 0x1405A4370, 0x1405A4278),
          ('CEm002', 0x1405A54B0, 0x1405A53A8), ('CEm003', 0x1405A6410, 0x1405A6318),
          ('CEm004', 0x1405A7050, 0x1405A6F68)]
TABLE_ENTRIES = 128
STUB, FAKE, RET = 0x20000000, 0x21000000, 0x30000000

data = open(EXE, 'rb').read()
pe = struct.unpack_from('<I', data, 0x3c)[0]
mu = Uc(UC_ARCH_X86, UC_MODE_64)
mu.mem_map(B, (struct.unpack_from('<I', data, pe + 24 + 56)[0] + PAGE - 1) & ~(PAGE - 1))
mu.mem_write(B, data[:PAGE])
for i in range(struct.unpack_from('<H', data, pe + 6)[0]):
    o = pe + 24 + struct.unpack_from('<H', data, pe + 20)[0] + 40 * i
    vs, va, rs, ra = struct.unpack_from('<IIII', data, o + 8)
    mu.mem_write(B + va, data[ra:ra + min(rs, vs or rs)])
for base, size in ((0x7000000, 0x100000), (0x10000000, 0x8000000), (STUB, PAGE), (FAKE, PAGE), (RET, PAGE)):
    mu.mem_map(base, size)
mu.mem_write(STUB, b'\x31\xc0\xc3' + b'\xc3' * (PAGE - 3))  # xor eax, eax; ret
mu.mem_write(FAKE, b'\xc3' * PAGE)
heap = [0x10000000]


def alloc(n, content=b''):
    p = heap[0]
    heap[0] += (n + 0xFF) & ~0xFF
    if content:
        mu.mem_write(p, content)
    return p


def unmapped(uc, access, addr, size, value, user):
    page = addr & ~(PAGE - 1)
    try:
        uc.mem_map(page, PAGE)
    except UcError:
        return False
    if access == UC_MEM_FETCH_UNMAPPED:  # a call into a DLL: return 0
        uc.mem_write(page, b'\xc3' * PAGE)
        uc.mem_write(addr, b'\x31\xc0\xc3')
    return True


def ret(value):
    rsp = mu.reg_read(UC_X86_REG_RSP)
    mu.reg_write(UC_X86_REG_RIP, struct.unpack('<Q', mu.mem_read(rsp, 8))[0])
    mu.reg_write(UC_X86_REG_RSP, rsp + 8)
    mu.reg_write(UC_X86_REG_RAX, value)


spawns = []


def on_spawn(uc, addr, size, user):
    if addr not in SPAWN:
        return
    api = SPAWN[addr]
    rcx, rdx, r8, r9 = (uc.reg_read(r) & 0xFFFFFFFFFFFFFFFF for r in
                        (UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_R8, UC_X86_REG_R9))
    if api in ('a90', 'ab0', 'ca0'):
        kind = 'PEGV'[rcx & 3] if (rcx & 0xFFFFFFFF) < 4 else '?'
        ident, prep = rdx & 0xFFFF, r9 & 0xFFFFFFFF
        matrix = struct.unpack('<16f', uc.mem_read(r8, 64)) if r8 else None
    else:
        kind, ident, prep, matrix = api, rcx & 0xFFFF, 0, None
    effect = alloc(0x1000)
    site = struct.unpack('<Q', uc.mem_read(uc.reg_read(UC_X86_REG_RSP), 8))[0] - 5
    spawns.append({'kind': kind, 'id': ident, 'api': api, 'prep': prep, 'matrix': matrix, 'effect': effect,
                   'site': site})
    ret(effect)


state = {'frame': 0.0, 'flag': 0, 'log': [], 'base': 0, 'step': 0}


def on_interface(uc, addr, size, user):
    slot = (addr - FAKE) // 8
    edx = uc.reg_read(UC_X86_REG_RDX) & 0xFFFFFFFF
    if slot in (4, 5):  # +0x20 / +0x28: play a command of table 1 / 2
        state['log'].append(('play', slot - 3, edx, state['frame']))
        state['base'] = state['step']
    elif slot == 16:  # +0x80: send an event code
        state['log'].append(('code', 0, edx, state['frame']))
    if slot == 8:  # +0x40: the motion frame
        uc.reg_write(UC_X86_REG_XMM0, struct.unpack('<I', struct.pack('<f', state['frame']))[0])
    ret(state['flag'] if slot == 9 else 0)  # +0x48: the motion's end


mu.hook_add(UC_HOOK_MEM_UNMAPPED, unmapped)
mu.hook_add(UC_HOOK_CODE, on_spawn, begin=min(SPAWN), end=max(SPAWN))
mu.hook_add(UC_HOOK_CODE, on_interface, begin=FAKE, end=FAKE + PAGE - 1)


def call(fn, args, limit=2_000_000):
    rsp = 0x7080000 - 8  # entry rsp is 8 mod 16, as after a call
    mu.mem_write(rsp, struct.pack('<Q', RET))
    for reg, v in zip((UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_R8, UC_X86_REG_R9), args):
        mu.reg_write(reg, v)
    mu.reg_write(UC_X86_REG_RSP, rsp)
    try:
        mu.emu_start(fn, RET, count=limit)
        return True
    except UcError:
        return False


def u64(a):
    return struct.unpack('<Q', mu.mem_read(a, 8))[0]


def u32(a):
    return struct.unpack('<I', mu.mem_read(a, 4))[0]


# ---- 1. the handler ---------------------------------------------------------
stub_vtable = alloc(0x800, struct.pack('<256Q', *([STUB] * 256)))


def enemy(etype):
    obj = alloc(0x4000)
    mu.mem_write(obj, struct.pack('<Q', stub_vtable))
    mu.mem_write(obj + 0x80, struct.pack('<4f', 1, 2, 3, 1))
    mu.mem_write(obj + 0xC0, struct.pack('<h', 0x4000))
    mu.mem_write(obj + 0x670, struct.pack('<I', etype))
    joints = {}
    for k in range(64):
        m = alloc(64, struct.pack('<16f', 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10000 + 1000 * k, 0, 0, 1))
        entry = alloc(0x260)
        mu.mem_write(entry + 0x110, struct.pack('<Q', m))
        mu.mem_write(obj + 0x6D8 + 8 * k, struct.pack('<Q', entry))
        joints[m] = k
    return obj, joints


def rdata_floats(va, n):
    return struct.unpack('<%df' % n, bytes(mu.mem_read(va, 4 * n)))


def scale_at(site):
    """The uniform scale the handler gives the spawn matrix before `site`:
    the vector its scale helper 0x1403304F0 multiplies by (read from the
    code, as the emulated matrix helpers are not exact)."""
    asm = subprocess.run(['objdump', '-d', '-M', 'intel', '--no-show-raw-insn', '--start-address=%#x' % (site - 0x60),
                          '--stop-address=%#x' % (site + 5), EXE], capture_output=True, text=True).stdout
    lines = [l for l in asm.splitlines() if re.match(r'\s+[0-9a-f]+:', l)]
    vector = None
    for line in lines:
        m = re.search(r'movaps\s+xmm0,XMMWORD PTR \[rip\+0x[0-9a-f]+\]\s+# 0x([0-9a-f]+)', line)
        if m:
            vector = int(m.group(1), 16)
        if 'call   0x1403304f0' in line and vector is not None:
            x, y, z, _ = rdata_floats(vector, 4)
            if abs(x - y) < 1e-6 and abs(y - z) < 1e-6:
                return round(x, 4)
    return 1.0


def placement(s, joints):
    e = s['effect']
    m = s['matrix']
    scale = scale_at(s['site'])
    for parent, mode_at in ((0xC0, 0xD8), (0xC8, 0xD4)):
        p = u64(e + parent)
        if p in joints:
            mode = u32(e + mode_at) if parent == 0xC8 else s['prep']
            return 'joint', joints[p], mode & 0xFF, scale
    if m is None:
        return None
    t = m[12:15]
    if abs(t[0] - 100) < 1e-3 and abs(t[2] - 300) < 1e-3:
        return 'given', 0, 0, scale
    if abs(t[0] - 1) < 1e-3 and abs(t[2] - 3) < 1e-3:
        return 'actor', 0, 0, scale
    if t[0] >= 9999 and abs((t[0] - 10000) % 1000) < 1e-2:
        return 'joint', round((t[0] - 10000) / 1000), 0, scale
    return None


handler_rows = []
for etype, tag in ((0, 0), (0x1A, 1)):
    for code in range(0xCF):
        obj, joints = enemy(etype)
        given = alloc(16, struct.pack('<4f', 100, 200, 300, 1))
        spawns.clear()
        call(HANDLER, [obj + 0x110, code, given])
        for s in spawns:
            place = placement(s, joints)
            if place is None or s['kind'] == '?':
                continue
            handler_rows.append((code, tag, s['kind'], s['id']) + place)

# ---- 2. the commands --------------------------------------------------------
fake_vtable = alloc(0x800, struct.pack('<256Q', *[FAKE + 8 * i for i in range(256)]))
slots = [u64(COMMANDS + 8 * i) for i in range(256)]
spawning_codes = {r[0] for r in handler_rows if r[1] == 0}
events = set()
for k in range(127):
    update, start = slots[2 * k + 1], slots[2 * k + 2]
    for flag in (0, 1):
        face = alloc(0x4000)
        mu.mem_write(face, struct.pack('<Q', fake_vtable))
        cmd = alloc(0x400)
        mu.mem_write(cmd, struct.pack('<Q', COMMANDS))
        mu.mem_write(cmd + 0xE0, struct.pack('<Q', face))
        mu.mem_write(cmd + 0x20, struct.pack('<Q', alloc(0x100)))
        mu.mem_write(cmd + 0x8, struct.pack('<Q', alloc(0x100)))
        mu.mem_write(cmd + 0xD0, struct.pack('<Q', fake_vtable))
        state.update(frame=0.0, flag=flag, log=[], base=0, step=0)
        if not call(start, [cmd], 200000):
            continue
        for f in range(600):
            state['step'] = f
            state['frame'] = float(f - state['base'])
            if not call(update, [cmd], 200000):
                break
        playing, last = None, {}
        for entry in state['log']:
            kind, table, value, frame = entry
            if kind == 'play':
                playing, last = (table, value), {}
                continue
            if playing is None or value not in spawning_codes:
                continue
            # A code sent on every frame past its gate is one event.
            if last.get(value) == frame - 1:
                last[value] = frame
                continue
            last[value] = frame
            events.add((playing[0], playing[1], frame, value))

# ---- 3. the class tables ----------------------------------------------------


def pairs(address):
    raw = bytes(mu.mem_read(address, 2 * TABLE_ENTRIES))
    return [(raw[2 * i], raw[2 * i + 1]) for i in range(TABLE_ENTRIES)]


PLACE = {'joint': 0, 'actor': 1, 'given': 2}
with open(OUT, 'w') as out:
    out.write('// Generated by tools/research/em000_events.py from dmc3.exe; do not edit.\n')
    out.write('#ifdef EM000_HANDLER_ROW\n')
    out.write('// {code, enemy type 0x1A, kind, id, place (0 joint, 1 actor, 2 given), joint, mode, scale}\n')
    for code, tag, kind, ident, place, joint, mode, scale in handler_rows:
        out.write("EM000_HANDLER_ROW(0x%02X, %d, '%s', %d, %d, %d, %d, %.4fF)\n"
                  % (code, tag, kind, ident, PLACE[place], joint, mode, scale))
    out.write('#endif\n#ifdef EM000_COMMAND_EVENT\n// {table 1 or 2, command, frame, code}\n')
    for table, command, frame, code in sorted(events):
        out.write('EM000_COMMAND_EVENT(%d, 0x%02X, %.1fF, 0x%02X)\n' % (table, command, frame, code))
    out.write('#endif\n#ifdef EM000_CLASS_TABLES\n// {class, table 1 (bank, action) x %d, table 2}\n' % TABLE_ENTRIES)
    for name, t1, t2 in TABLES:
        row = ', '.join('{%d, %d}' % p for p in pairs(t1) + pairs(t2))
        out.write('EM000_CLASS_TABLES("%s", %s)\n' % (name, row))
    out.write('#endif\n')
print('handler rows', len(handler_rows), 'command events', len(events))
