# How DMC3 HD picks a shader for each draw (dmc3.exe)

Read 2026-11-21 from the HD Collection's `dmc3.exe` (sha256 e454272e…),
static analysis only. Names and rules are written in our own words; no
Capcom code is kept here. Companion to `dmc3-hd-shaders-exe.md`.

## Short answer

The game does not run all its shaders at once. Every draw sets exactly one
vertex and one pixel shader, chosen by an ID. Over a whole play session
most of the 67 get used, but which one a draw gets is decided per draw:
by the kind of object (vertex shader) and by the texture state of the mesh
(pixel shader). The Reader has to make the same choice per batch.

## The tables (.data)

- Vertex shaders: 29 entries of 32 bytes at 0x1405612D0 (blob pointer,
  size, ID, input-layout number, then the created shader object).
- Pixel shaders: 38 entries of 24 bytes at 0x140561670 (blob pointer, size,
  ID, then the shader object).
- 0x140045D00 creates them all at start-up (labelled "VS:n" / "PS:n").

### Vertex shader IDs

| ID | shader | ID | shader |
| --- | --- | --- | --- |
| 0 | VCPL | 64 | VCPL_FOG |
| 1 | VTVCPL | 65 | VTVCPL_FOG |
| 2 | OCPL | 66 | OCPL_FOG |
| 3 | VTOCPL | 67 | VTOCPL_FOG |
| 4 | VTPL | 68 | VTPL_FOG |
| 5 | DMC3_MOD | 13 | DMC3_STG |
| 6 | DMC3_MOD_SP | 77 | DMC3_STG_FOG |
| 7 | DMC3_MOD_STX | 14 | DMC3_PTCL |
| 8 | DMC3_EFM | 78 | DMC3_PTCL_FOG |
| 9 | DMC3_EFM_SP | 15 | DMC3_SHW |
| 10 | DMC3_EFM_VA | 31 | FX_DOF_V |
| 11 | DMC3_EFM_VA_SP | 32 | FX_COL_CORR_V |
| 12 | DMC3_EFM_STX | 33 | FX_1x4F_PASS |
| | | 34 | FX_2x4F_PASS |
| | | 35 | FX_DIR_GLOW_V |
| | | 36 | VIDEO_FRAME_V |

Bit 0x40 of a vertex ID is "with fog".

### Pixel shader IDs

The ID is a bit field: texture function (0-4) + 32 for fog + 64 for
punch-through alpha (A1).

| function | plain | +fog (32) | +A1 (64) | +fog +A1 (96) |
| --- | --- | --- | --- | --- |
| 0 MODULATE (TEX_COL_MOD2) | 0 | 32 | 64 | 96 |
| 1 DECAL (TEX_DECAL) | 1 | 33 | 65 | 97 |
| 2 HIGHLIGHT (TEX_COL_HL) | 2 | 34 | — | — |
| 3 HIGHLIGHT2 (TEX_COL_HL2) | 3 | 35 | — | — |
| 4 RECOLOUR (TEX_COL_RECOL2) | 4 | 36 | 68 | 100 |

Others: 10 COL, 12 FX_DOF, 13 FX_COL_CORR, 14 FX_GRAD_FOG, 15 FX_GLOW_SETUP,
16 FX_BLOOM_GLARE, 17 FX_BLOOM_GLOW, 18 FX_STAR_GLARE, 19 FX_PROC_FOG_GEN,
20 FX_DIR_GLOW, 21 fx_decal_clut, 22 TEX_DECAL_AFULL, 23 FX_EDGE_DET,
24 FX_EDGE_DET2, 25 DX_Z_COPY, 26 TEX_PTCL_END, 27 DX_COLOUR_AND_Z_COPY,
28 FX_EDGE_AA2, 29 FX_EDGE_AA, 30 DX_COLOUR_COPY, 31 FX_COL_CORR2,
37 VIDEO_FRAME.

## Setting a pair: 0x140045FE0

Takes a vertex ID and a pixel ID and looks each one up in its table.
When the exact vertex ID is missing and it has the fog bit (0x40), the
same ID without fog is used. When the exact pixel ID is missing and it has
the fog bit (0x20), the same ID without fog is used. There is no fallback
for the A1 bit, so HIGHLIGHT / HIGHLIGHT2 with A1 never reach it.
22 callers: the mesh draw below, particles, and the full-screen passes
(which pass fixed pairs, for example 0x24/0x25 for the movie frame).

## The mesh draw: 0x140044310

The draw kind (global at 0x1405E1748) picks the vertex shader:

| kind | vertex shader |
| --- | --- |
| 2, 3 | 13 DMC3_STG (the stage; its material word comes from the record) |
| 4 | 5 DMC3_MOD |
| 5 | 8 DMC3_EFM |
| 7 | 10 DMC3_EFM_VA (no A1) |
| 8 | 11 DMC3_EFM_VA_SP (no A1) |
| 9 | 9 DMC3_EFM_SP |
| 10 | 7 DMC3_MOD_STX |
| 11 | 12 DMC3_EFM_STX |
| 12 | 6 DMC3_MOD_SP |
| 6 | nothing drawn here |

The pixel shader:
1. Base: the texture function the texture state left (global 0x1405D95D8,
   below).
2. RECOLOUR (4) instead when the draw has recolour data whose bytes +3 or +7
   are non-zero (the record's field at +0x50).
3. Fog: when the scene fog is on (0x140C0CDD1 and 0x140553009), the vertex
   ID gets +0x40 and the pixel ID +0x20.
4. A1: when the current alpha setting (field +0x28 of the object at
   0x1405E1728) is non-zero, the pixel ID gets +0x40 (not for the two
   EFM_VA kinds).

## Where the texture function comes from

- 0x14002CD60 replays PS2 GS register writes (A+D); TEX0_1 / TEX0_2 land at
  0x1405D9200 / 0x1405D9208.
- 0x14002BC30 (per draw) takes the context bit (PRIM bit 9) to pick TEX0_1
  or TEX0_2, projects CLAMP into the sampler, and, when texturing is on
  (PRIM bit 4), calls 0x14002CC10.
- 0x14002CC10 binds the texture by TEX0's low 14 bits (TBP0) and keeps
  TEX0 bits 36-37 as the base pixel-shader key (0-3).
- For stages the TEX0 qword is built from the SCM material by
  0x1402F99B0 / 0x1402F9AC0 (vendor scm_render.hpp,
  `legacy_gs_tex0_ps_base_key`). For models it comes from the GS packet the
  model's mesh sends.

## Particles: 0x140045590

Vertex 14 (DMC3_PTCL); pixel 0 (MODULATE) when the material word has bit
0x10 (texture on), else 10 (COL).

## What the Reader needs

- Per batch: the object kind (MOD / EFM and its variant / SCM / particle),
  the TEX0 key (0-3) of its mesh, recolour on or off, fog, and the A1
  setting. Then the same ID rule and the same fog fallback.
- Today `nr_draw()` always uses MODULATE. Still to read: how the Reader can
  get each MOD mesh's TEX0 key and recolour data from the file, and what
  sets the A1 field at +0x28.
