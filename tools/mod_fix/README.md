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
