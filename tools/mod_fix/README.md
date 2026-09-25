# Player-PAC mod repair (analysis tools)

These tools repair a community player PAC, such as `pl011.pac` (the anime
girl mod built on Dante's `pl000.pac`), so that it matches the canonical
layout the game reads. They use only the user's own files and write the
repaired PAC wherever you point them. No game file belongs in this directory.

## What they fix

| Part | Problem in the mod | Repair |
| --- | --- | --- |
| PTX (slot 0) | The descriptors were written by a community tool: format, row bytes, payload size and constants are zero. Two textures have one mip level instead of a full chain. | Rebuild every 0x70-byte descriptor from its DDS header. The rules are in rengine `texture_slot_framing.cpp`. Missing mip levels are generated with a 2×2 box filter and DXT5 re-encoding. |
| SHW (slot 8) | A byte copy of Dante's body shadow, so the floor shows Dante's silhouette. | Build one closed convex hull per body joint (Dante's 17-joint set) from the model's own rest-space vertices, grouped by each vertex's dominant skin joint. |
| SHW (slot 14) | Missing. The player init passes the slot pointer to the shadow object without a null check; see below. | Build hulls for the slot 12 model the same way. |

Each hull is built like this:

- It takes the extreme points of the joint's vertices along 18 directions.
- It builds their convex hull (scipy / Qhull).
- It writes the triangles counter-clockwise, facing outward.
- It records, for each triangle, the neighbour across each edge (vᵢ, vᵢ₊₁).
- It checks that `T = 2V - 4`.

The layout copies Dante's files exactly: a 0x20 header, 0x40-byte hull
records, and per hull the triangles, adjacency, vertices (w = 1) and
selectors, each block aligned to 16 bytes. Header byte `+0x11` holds the
model's node count.

## Use

```sh
# 1. Rest-space vertices with their dominant joint (links the Native Reader core).
./skinexport_bin mod.pac slot_0001.mod body.txt
./skinexport_bin mod.pac slot_0012.mod tail.txt
# 2. Rebuild (Dante's pl000.pac gives the SHW header and DDS header templates).
python3 fixmod.py mod.pac pl000.pac mod_fixed.pac body.txt tail.txt
# 3. Check: canonical PTX descriptors, canonical SHW parse.
python3 ptxcheck.py mod_fixed.pac
./shwcheck_bin mod_fixed.pac
```

Build the two C++ helpers the same way as the other probes: link
`libdmc_native_reader_core.a`, `libdmc_native_reader_rengine_viewer.a` and
`libdmc_rengine_reader_core.a`. `shwcheck` needs only
`libdmc_rengine_reader_core.a` and rengine's `src/formats/shw.cpp`.

## Result on `pl011.pac`

The input is `pl011.pac`, SHA-256
`7d7ec53a7b2c9c64faef2c2b5e7fac76c40286cc0afb6be6ab10cec135800356`. The output
is 1,178,816 bytes with 15 slots, SHA-256
`de580ff960152640cf51f0b86e1c6e1f369a309c1419863db8cc6309ab435596`:

- All three textures pass the canonical descriptor validator (t1: 8 mips,
  t2: 9 mips).
- Native Reader reports no non-canonical note.
- Both SHW slots parse with no diagnostics.
- The floor shadow is the girl's own silhouette.

## Costume as cloth (`coat_patch.py` + `costume.py`)

These two tools turn the costume (skirt, side ribbon and both sleeves) into
cloth that hangs from the right bones.

```sh
python3 coat_patch.py dmc3.exe dmc3_coat.exe             # canonical EXE only
python3 costume.py mod_fixed.pac pl000.pac mod_costume.pac
python3 coat_patch.py --verify-asm coat_constraints.s     # needs binutils
```

### EXE patch (`coat_patch.py`)

The script refuses any file other than the canonical `dmc3.exe` and checks
every original byte before changing it. It drops the Authenticode
certificate, because a patched file cannot keep a valid signature. It also
recomputes the PE checksum.

1. **Coat root joint.** CPlDante always hung the coat from joint 3.
   - Three places load that joint pointer: `0x1402120C4`, `0x140218EFD` and
     `0x140218F67`.
   - They now call a 16-byte routine placed in `int3` padding. The routine
     reads byte `+0x13` of the coat MOD header, then takes joint `3 + that
     byte`.
   - The byte comes from `player+0x76BA`, which is the coat object
     (`+0x7540`, class CDraw), then its MOD manager (`+0x80`), then the
     manager's copy of header `+0x13` (`+0xFA`).
   - Retail coats have 0 there, so they stay on the chest.
2. **Coat node constraints.** A new section `.dmcx` (VA `0x140DAC000`)
   holds `coat_constraints.s`.
   - Both coat setups of CPlDante's load call it: `0x1402151C3` for most
     costumes, and `0x140215373` when costume byte `+0x3E9E` is 1 or 4.
   - Each call comes after `0x14030F850` has bound the coat joints (which
     clears every joint `+0x100`), after the capsule shapes are written to
     `player+0xB630` and after the cloth parse.
   - It reads PAC slot 15 (`'CCNS'`). The retail game never reads that slot,
     so PACs without it behave as before.
   - For each listed coat node it builds a mode-1 constraint, the same
     object CEm028 uses for Nevan's sleeves (vtable `0x1404CC1F8`,
     `0x1402CBBE0`). The node's world is then offset × body joint world.
   - The constraints live in the section, in pools of 16 per player (4
     pools).
   - Slot 15 can also replace the six collision capsule shapes for this
     costume. The step `0x1402C97F0` reads them every frame.

Slot 15 layout:
- `+0` `'CCNS'`, `+4` version 1, `+8` node count (at most 16), `+0x0C`
  capsule count (at most 6);
- from `+0x10`, one 0x50-byte record per node: u32 coat node, u32 body
  joint, u64 0, then `f32[16]` offset in row-vector layout;
- then one 0x40-byte record per capsule: u32 shape index, 12 reserved
  bytes, then A, B and radius as `f32[4]` each. Each record is written over
  `player+0xB630 + index·0x50 + 0x10`.

Capsule shape: A at `+0x10` and B at `+0x20`, both in host-joint space,
radius at `+0x30`. The six shapes sit on joints 3, 2, 15, 16, 19 and 20.

Limits from the EXE:
- 0x1401DE820 allocates 39 coat joints, so a coat can have at most 39
  nodes.
- CPlDante has room for one cloth chain: it sits at `+0xA210`, and
  `+0xA300` is the next joint table. A Dante CLT must therefore use
  `ClothNum 1`. Vergil has room for two (`+0xA230`, `+0xA410`).
- Only that chain collides with the six player coat capsules.

Checked by emulating the patched image with unicorn:
- the hook installs the constraints from slot 15 of the real costume PAC;
- the game's own `0x1402CBBE0` yields offset × host;
- PACs without slot 15 (retail pl000, the original pl011) install nothing.

### Costume builder (`costume.py`)

- **Cut.** The script takes the skirt, the ribbon and the fabric of both
  sleeves out of body mesh 2:
  - the skirt is picked by the pleat UV block of texture 2;
  - the sleeves are the triangles on bones 7/8 and 11/12, keeping only
    fabric, told apart from skin by texel colour.
- **Coat (37 nodes).** The root hangs from the pelvis (header `+0x13 = 11`).
  - **Skirt:** 8 waist chains, each an anchor and 2 links.
  - **Sleeves:** each arm has three anchors on body joints 6/7/8 (10/11/12)
    with identity offsets, placed at the joints' rest positions. The
    sleeve's own weights on those joints carry over one to one.
  - **Sleeve cloth:** a 3-link chain runs from the elbow anchor along the
    bottom of the bell. It takes the lower half of the bell, more toward
    the cuff.
- **CLT.** One block with 22 bones: every skirt chain simulates.
- **Capsules.** Slot 15 replaces the six shapes with ones that fit this body:
  - the torso stops above the waist: joint 3, from (0, 20, 1) to
    (0, −12, 1), r 11;
  - hips: r 12;
  - legs: r 9.

  Dante's chest capsule runs 40 units down in front of the hips and would
  push the front of a short skirt forward. With these shapes, a raised
  thigh lifts the front of the skirt instead of passing through it.
- **Shadows.** SHW slot 8 is rebuilt without the costume. SHW slot 14 holds
  hulls per skirt chain and per sleeve part.

Native Reader follows the same rules: the coat hangs from joint `3 + coat
+0x13`, and slot 15 gives the node constraints.
