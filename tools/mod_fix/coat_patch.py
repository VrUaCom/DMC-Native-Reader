"""Patch the canonical dmc3.exe for data-driven Dante coats.

1. Coat root joint (CPlDante always used joint 3, the chest): the coat hangs
   from joint 3 + coat MOD header byte +0x13. Retail coats carry 0.
   Sites 0x1402120C4 (coat update, then vtbl+0x190), 0x140218EFD and
   0x140218F67 (CPlDante virtual 0x140218960, then vtbl+0x198) load
   `mov rdx, [player+0x1898]`; each becomes `call cave; nop; nop` and the
   cave (int3 padding between functions) does
       movzx eax, byte [player+0x76BA]; mov rdx, [player+rax*8+0x1898]; ret
   player+0x76BA = coat object (+0x7540, class CDraw) + MOD manager (+0x80)
   + header +0x13 copy (+0xFA, 0x1402F960E).
2. Coat node constraints: coat nodes can follow body joints (sleeves on the
   arms, a hem on the chest) like Nevan's sleeves (CEm028 init 0x140130480).
   A new section .dmcx (VA 0x140DAC000) holds coat_constraints.s; CPlDante's
   coat load calls it at 0x140215373, after the joint table binding and the
   cloth parse. It reads optional player PAC slot 15 ('CCNS'), which the
   retail game never reads, so PACs without it behave as before.

The certificate table (Authenticode) is dropped: the signature cannot stay
valid after any change. The PE checksum is recomputed.

Usage: python3 coat_patch.py dmc3.exe dmc3_coat.exe
"""
import hashlib
import struct
import sys

CANONICAL = 'e454272ed0fb0247fcbcf300e5d55d7a3e96d50b89b9ffaff81bb978dcbdd082'
BASE = 0x140000000
TEXT_VA, TEXT_RAW = 0x140001000, 0x400

def text_off(va): return va - TEXT_VA + TEXT_RAW

# ---- 1. coat root joint --------------------------------------------------
CAVE_RSI = 0x140346CF2   # 20 bytes of int3 between 0x140346BD8 and 0x140346D10
CAVE_RDI = 0x1403455D5   # 17 bytes of int3 between 0x1403455D5 and 0x1403455F0
CAVES = {
    CAVE_RSI: bytes.fromhex('0fb686ba760000' '488b94c698180000' 'c3'),
    CAVE_RDI: bytes.fromhex('0fb687ba760000' '488b94c798180000' 'c3'),
}
JOINT_SITES = {
    0x1402120C4: (bytes.fromhex('488b9698180000'), CAVE_RSI),
    0x140218EFD: (bytes.fromhex('488b9798180000'), CAVE_RDI),
    0x140218F67: (bytes.fromhex('488b9798180000'), CAVE_RDI),
}

# ---- 2. coat node constraints ----------------------------------------------
SECTION_VA = 0x140DAC000
SECTION_VSIZE = 0x4000            # code 0x000, owners 0x400, pools 0x1000..0x3FFF
SECTION_RAWSIZE = 0x200
HOOK_SITE = 0x140215373           # lea rdx, [r14+0x1880]
HOOK_ORIGINAL = bytes.fromhex('498d9680180000')
# coat_constraints.s linked at 0x140DAC000 with GETTER=0x1401B82C0,
# PACTABLE=0x140C99D30, CVTABLE=0x1404CC1F8, OWNERS=0x140DAC400,
# RRIDX=0x140DAC420, POOL=0x140DAD000 (see --verify-asm).
CODE = bytes.fromhex(
    '5356574883ec20488d0d22ddeeff31d2450fb7467841b90f000000e8a0c240ff4885c0'
    '0f8442010000813843434e530f8536010000837804010f852c0100008b700883fe1076'
    '05be10000000488d58104c8d15a903000031c94d3934ca742affc183f90472f331c949'
    '833cca00741affc183f90472f24c8d1da3030000418b0b8d510183e2034189134d8934'
    'ca4869f9000c00004c8d1d650f00004c01df85f60f84c50000008b0383f8270f83af00'
    '00008b530483fa600f83a30000004d8b84c6d0a000004d85c00f84920000004d8b8cd6'
    '801800004d85c90f84810000004d8b89100100004d85c974750f57c00f29070f294710'
    '0f2947200f2947300f2947400f2947500f2947600f2947704c8d1de40072ff4c891fc6'
    '472001c74728010000004c894f300f1043100f2987800000000f1043200f2987900000'
    '000f1043300f2987a00000000f1043400f2987b00000004989b8000100004881c7c000'
    '00004883c350ffcee933ffffff4883c4205f5e5b498d9680180000c3')

def call_rel(site, target):
    return b'\xe8' + struct.pack('<i', target - (site + 5))

def pe_checksum(data, checksum_offset):
    total = 0
    for i in range(0, len(data) & ~1, 2):
        if checksum_offset <= i < checksum_offset + 4:
            continue
        total += data[i] | (data[i + 1] << 8)
        total = (total & 0xFFFF) + (total >> 16)
    if len(data) & 1:
        total += data[-1]
        total = (total & 0xFFFF) + (total >> 16)
    total = (total & 0xFFFF) + (total >> 16)
    return (total + len(data)) & 0xFFFFFFFF

def patch(exe):
    exe = bytearray(exe)
    digest = hashlib.sha256(exe).hexdigest()
    if digest != CANONICAL:
        raise SystemExit(f'not the canonical dmc3.exe (sha256 {digest})')
    # 1. caves and joint sites
    for va, code in CAVES.items():
        o = text_off(va)
        assert exe[o:o + len(code)] == b'\xcc' * len(code), hex(va)
        exe[o:o + len(code)] = code
    for va, (orig, cave) in JOINT_SITES.items():
        o = text_off(va)
        assert exe[o:o + 7] == orig, hex(va)
        exe[o:o + 7] = call_rel(va, cave) + b'\x90\x90'
    # 2. hook
    o = text_off(HOOK_SITE)
    assert exe[o:o + 7] == HOOK_ORIGINAL
    exe[o:o + 7] = call_rel(HOOK_SITE, SECTION_VA) + b'\x90\x90'

    pe = struct.unpack_from('<I', exe, 0x3C)[0]
    nsec = struct.unpack_from('<H', exe, pe + 6)[0]
    opt = pe + 24
    opt_size = struct.unpack_from('<H', exe, pe + 20)[0]
    table = opt + opt_size
    assert nsec == 8
    header = table + 40 * nsec
    assert header + 40 <= 0x400 and exe[header:header + 40] == bytes(40)
    size_of_image = struct.unpack_from('<I', exe, opt + 56)[0]
    assert size_of_image == SECTION_VA - BASE
    # Drop the certificate table (it sits at the end of the file).
    sec_dir = opt + 112 + 8 * 4
    cert_off, cert_size = struct.unpack_from('<II', exe, sec_dir)
    assert cert_off + cert_size == len(exe)
    del exe[cert_off:]
    struct.pack_into('<II', exe, sec_dir, 0, 0)
    raw = len(exe)
    assert raw % 0x200 == 0
    body = CODE + bytes(SECTION_RAWSIZE - len(CODE))
    exe += body
    exe[header:header + 40] = (b'.dmcx\0\0\0' + struct.pack(
        '<IIIIIIHHI', SECTION_VSIZE, SECTION_VA - BASE, SECTION_RAWSIZE, raw,
        0, 0, 0, 0, 0xE0000060))
    struct.pack_into('<H', exe, pe + 6, nsec + 1)
    struct.pack_into('<I', exe, opt + 56, SECTION_VA - BASE + SECTION_VSIZE)
    struct.pack_into('<I', exe, opt + 64, 0)
    struct.pack_into('<I', exe, opt + 64, pe_checksum(exe, opt + 64))
    return bytes(exe)

def verify_asm(source):
    """Reassemble coat_constraints.s with binutils and compare with CODE."""
    import os, subprocess, tempfile
    with tempfile.TemporaryDirectory() as d:
        o, elf, binf = (os.path.join(d, n) for n in ('c.o', 'c.elf', 'c.bin'))
        subprocess.run(['as', '--64', '-o', o, source], check=True)
        syms = dict(GETTER=0x1401B82C0, PACTABLE=0x140C99D30, CVTABLE=0x1404CC1F8,
                    OWNERS=0x140DAC400, RRIDX=0x140DAC420, POOL=0x140DAD000)
        cmd = ['ld', f'-Ttext={SECTION_VA:#x}', '-e', '_start', '-o', elf, o]
        for k, v in syms.items(): cmd += ['--defsym', f'{k}={v:#x}']
        subprocess.run(cmd, check=True)
        subprocess.run(['objcopy', '-O', 'binary', '-j', '.text', elf, binf], check=True)
        built = open(binf, 'rb').read()
    print('assembled code matches' if built == CODE else 'MISMATCH')
    return built == CODE

if __name__ == '__main__':
    if sys.argv[1] == '--verify-asm':
        sys.exit(0 if verify_asm(sys.argv[2]) else 1)
    src, dst = sys.argv[1:3]
    out = patch(open(src, 'rb').read())
    open(dst, 'wb').write(out)
    print('written', dst, len(out), 'bytes', hashlib.sha256(out).hexdigest())
