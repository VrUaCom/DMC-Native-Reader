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

## Skirt as the coat (`skirtcoat.py`)

`skirtcoat.py` builds a second variant from the repaired PAC. It moves the
skirt and its side ribbon out of the body MOD into the slot 12 coat MOD, so
the game runs cloth physics on them.

```sh
python3 skirtcoat.py mod_fixed.pac pl000.pac mod_skirt.pac
```

- **Cut:** body mesh 2 is a plain triangle list, with break bits `1,1,0` on
  every vertex triple. The skirt is the welded shell whose UVs lie in the
  pleat block of texture 2 (96 triangles). The ribbon is the small shell on
  the +X side (18 triangles). The body keeps everything else.
- **Coat MOD:**
  - Written with `modwriter.py`, the canonical MOD writer. It round-trips
    retail MODs byte for byte.
  - One object, one mesh, texture 2.
  - Vertices are in coat-root space, which is body rest minus joint 3 (the
    game sets the coat root to body joint 3 every frame).
  - The skeleton is 33 nodes: the root, then 8 waist chains of an anchor and
    3 links. Each anchor's local −Y points down the skirt, and each link is
    `(0, −L, 0)`, so every node's Y axis points at its parent, as the CLT `Y`
    axis expects.
  - Skin weights interpolate between the two nearest chains and along the
    chain, using up to 3 influences quantized to 31.
- **CLT:** `ClothNum 2`, following `pl001_02.clt`. Only block 0 gets the six
  player-coat capsules. The joint-3 capsule sits in front of the hips (z
  +10, r 15, extending 40 units down) and would push a short skirt forward.
  So the front chains go to block 1, and the side and back chains (block 0)
  collide with the waist and thighs.
- **SHW:** slot 8 is rebuilt without the skirt. Slot 14 has one hull per
  chain.

Limit: the root follows the chest (joint 3), not the pelvis (joint 14). At
the waistline, the gap between the two carriers is 2 to 5 units in idle and
run motions, 8 to 12 in many attacks, and up to 25 in flips
(`slot_0003.pac/slot_0009.mot`). In those poses the skirt visibly leaves the
hips. Sleeves and the shirt cannot move into the coat at all, because no coat
node follows the arms.
