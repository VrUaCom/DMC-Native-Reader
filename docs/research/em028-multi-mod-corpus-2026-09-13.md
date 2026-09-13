# em028 multi-MOD corpus acceptance — 2026-09-13

## Purpose

This note binds the Native Reader multi-MOD/shared-PTX acceptance path to the owner-supplied `em028-extract.zip` corpus. The ZIP is test evidence only and is not committed to the repository.

The main boss model is the four sibling MOD resources at the `em028/` root. MOD files nested inside `em028_009.pnst` belong to the effect-resource domain and MUST NOT be recursively merged into the boss body scene.

## Root model resources

| Resource | Size | SHA-256 | MOD header +0x10/+0x11/+0x12/+0x13 | Mesh texture slots | Vertices | Reconstructed triangles |
|---|---:|---|---|---|---:|---:|
| `em028_001.mod` | 128592 | `4877bc9a0a3d8130e641ffe4e44285f8ff41d1d4136cbdcc2b9962332e46a126` | `02 17 04 01` | `1,1` | 3183 | 2362 |
| `em028_004.mod` | 35776 | `9d81fa3792a62ac88643c6d88517152eb332dcc84d3e17a566c2d1b8140b93f8` | `03 17 04 00` | `0,0,0` | 859 | 630 |
| `em028_005.mod` | 44160 | `50b0453ba27dcd244054084eb83420239183c47eb80000abef9837d4a484f315` | `04 0e 04 01` | `2,2,3,3` | 1073 | 823 |
| `em028_006.mod` | 14640 | `5df1900270ec94d1226a2e236e0c7b3fcff5a727b8641490d555cb9621485ce0` | `01 0b 04 00` | `2` | 349 | 268 |

Combined geometry contract: **5464 vertices / 4083 reconstructed triangles**.

All four root MOD headers serialize `texture_slot_count = 4` at `+0x12`. Mesh-local usage is sparse, but every part belongs to the same four-slot companion domain.

## Shared texture companion

`em028_000.ptx`

- size: `618496`
- SHA-256: `af14a7e8ad852f57eb571fd938f5e2e40b9f3eb1eaf8ed47e600b9a913dd261e`
- texture count: `4`
- sector spans: `86, 86, 86, 43`
- exact physical size: `(1 + 86 + 86 + 86 + 43) * 0x800 = 618496`

| Slot | DDS | Mips | Secondary dimensions | Aux pair |
|---:|---|---:|---|---|
| 0 | `256x512 DXT5` | 10 | `128x256` | mode `2`, value `0x1D308000` |
| 1 | `512x512 DXT1` | 10 | `256x256` | mode `2`, value `0x21310000` |
| 2 | `512x512 DXT1` | 10 | `256x256` | mode `2`, value `0x21310000` |
| 3 | `256x512 DXT1` | 10 | `128x256` | mode `2`, value `0x1D308000` |

Every slot satisfies the pinned ReaderCore descriptor envelope: full mip chain, exact DDS/payload size, DXT descriptor encoding, row bytes, 1x/2x secondary-dimension rule, reciprocal fields, bounded auxiliary pair, zero-required descriptor fields and zero sector padding. All four DDS images decode successfully.

## Composite slot remap

Native Reader keeps each source MOD texture namespace local and remaps only the flattened render projection. With the current compact non-overlapping layout:

- `em028_001`: local slot `1` -> global slot `1`, base `0`, span `2`
- `em028_004`: local slot `0` -> global slot `2`, base `2`, span `1`
- `em028_005`: local slots `2,3` -> global slots `5,6`, base `3`, span `4`
- `em028_006`: local slot `2` -> global slot `9`, base `7`, span `3`

Required global slots are therefore `{1,2,5,6,9}`. One `em028_000.ptx` must be decoded in each source-local slot domain and published into those remapped global slots transactionally.

## Effect-domain exclusion

The same extraction contains ten `.mod` files nested below `em028_009.pnst`. These are PNST/effect children, not sibling body parts. The multi-MOD scene path must not recursively discover or auto-merge them with `em028_001/_004/_005/_006`.

## Device acceptance

1. Open `em028_001.mod`.
2. Use `Add .MOD part(s)` and select `em028_004.mod`, `em028_005.mod`, `em028_006.mod` together.
3. Confirm the title reports `MOD scene · 4 parts` and all four source parts are present.
4. Choose `Attach .PTX texture` -> `Shared PTX · all MOD parts`.
5. Select `em028_000.ptx` once.
6. Confirm all four parts render textured and the session reports the shared PTX as attached.
7. Open UV view and verify the composite retains the expected remapped texture-slot coverage.
8. Do not include MOD resources nested under `em028_009.pnst` in this acceptance scene.

## Evidence level

- Root file identity/hashes/sizes: `CORPUS_CONFIRMED`
- MOD `texture_slot_count=4`: `CORPUS_CONFIRMED`
- Per-mesh texture-slot sequence: `CORPUS_CONFIRMED`
- Combined vertex/triangle totals using Native Reader topology rule: `CORPUS_CONFIRMED`
- PTX physical framing and DDS decode: `CORPUS_CONFIRMED`
- Shared PTX global remap contract: regression-backed product behavior; final promotion still requires device acceptance on the exact APK head.
