"""Patch dmc3.exe so that Dante's coat model (PAC slot 12) hangs from body
joint 3 + coat MOD header byte +0x13 instead of always from joint 3.

Retail coats carry +0x13 = 0 (pl000, pl001), so they keep joint 3 (chest).
A coat authored with +0x13 = 11 hangs from joint 14 (pelvis), which a skirt
needs. Works only on the canonical executable; writes a patched copy.

Evidence (canonical dmc3.exe, SHA-256 e454272e...dd082):
  CPlDante coat update  0x1402120C4  mov rdx,[rsi+0x1898]  (joint 3 pointer)
                        0x1402120E0  call [coat vtbl+0x190](joint->world)
  CPlDante 0x140218960  0x140218EFD / 0x140218F67  mov rdx,[rdi+0x1898]
                        then call [coat vtbl+0x198](joint->world)
  Joint pointers: player +0x1880 + 8*j.
  Coat object: player +0x7540 (vtable 0x1404C9010); its MOD manager is at
  +0x80 (vtbl+0x50 = 0x140089960 -> 0x1402FDF00 -> 0x140303AE0 ->
  0x1402F9570), which copies header +0x13 to manager +0xFA
  (0x1402F960E..0x1402F9616). So the byte is at player +0x76BA.
Each site becomes `call cave; nop; nop`; the cave (int3 padding between
functions, outside every .pdata range) does
  movzx eax, byte [reg+0x76BA]; mov rdx, [reg+rax*8+0x1898]; ret
rax is dead at every site (the next instructions reload it).

Usage: python3 coatjoint_patch.py dmc3.exe dmc3_coatjoint.exe
"""
import hashlib
import struct
import sys

CANONICAL = 'e454272ed0fb0247fcbcf300e5d55d7a3e96d50b89b9ffaff81bb978dcbdd082'
TEXT_VA, TEXT_RAW = 0x140001000, 0x400

def off(va): return va - TEXT_VA + TEXT_RAW

CAVE_RSI = 0x140346CF2   # 20 bytes of int3 between 0x140346BD8 and 0x140346D10
CAVE_RDI = 0x1403455D5   # 17 bytes of int3 between 0x1403455D5 and 0x1403455F0
CAVES = {
    CAVE_RSI: bytes.fromhex('0fb686ba760000' '488b94c698180000' 'c3'),
    CAVE_RDI: bytes.fromhex('0fb687ba760000' '488b94c798180000' 'c3'),
}
SITES = {  # va: (original bytes, cave)
    0x1402120C4: (bytes.fromhex('488b9698180000'), CAVE_RSI),
    0x140218EFD: (bytes.fromhex('488b9798180000'), CAVE_RDI),
    0x140218F67: (bytes.fromhex('488b9798180000'), CAVE_RDI),
}

def main():
    src, dst = sys.argv[1:3]
    exe = bytearray(open(src, 'rb').read())
    digest = hashlib.sha256(exe).hexdigest()
    if digest != CANONICAL:
        sys.exit(f'not the canonical dmc3.exe (sha256 {digest})')
    for va, code in CAVES.items():
        o = off(va)
        assert exe[o:o + len(code)] == b'\xcc' * len(code), hex(va)
        exe[o:o + len(code)] = code
    for va, (orig, cave) in SITES.items():
        o = off(va)
        assert exe[o:o + 7] == orig, hex(va)
        exe[o:o + 7] = b'\xe8' + struct.pack('<i', cave - (va + 5)) + b'\x90\x90'
    open(dst, 'wb').write(exe)
    print('written', dst, hashlib.sha256(exe).hexdigest())

if __name__ == '__main__':
    main()
