# The Rengine gates archive (Gates 01–43): what the Reader takes from it

Source: the user's `DMC3_Rengine_Reverse_Archive_Gates01-43_2026-10-28.zip`
(48 gate packages: scripts, tests, manifests; no game data). It was made
against the same `dmc3.exe` as our notes (SHA-256
`e454272ed0fb0247fcbcf300e5d55d7a3e96d50b89b9ffaff81bb978dcbdd082`). Only
addresses, offsets and rules are copied here. The archive itself is not
committed.

## Material state (Gates 17–19)

The vendored parser (`formats/mod.hpp`, `formats/scm_render.hpp`) already
reads these fields; the Reader does not use them yet.

| Field | Where | Rule |
| --- | --- | --- |
| Mesh +0x04/+0x06/+0x08/+0x0A | MINU, MAXU, MINV, MAXV | `0x1402F9890`: MINU == 0 → no CLAMP (sentinel); otherwise the GS CLAMP register `(MINU<<4) \| 0x0F \| (MAXU<<14) \| (MINV<<24) \| (MAXV<<34)` (REGION_REPEAT on both axes) |
| HD bridge | `0x14002BC30` | WMS/WMT 1 or 2 → D3D11 CLAMP; 0 or 3 → WRAP. REGION_REPEAT therefore samples as WRAP on PC |
| Object flags & 0x4000 | object source flags | TEX1 = 0: nearest filtering; otherwise TEX1 = 0x60: linear |
| Object +0x01 | `alpha_control` (copied by `0x1403029E0`) | 0x80 → alpha 128/255; above 0x80 → alpha forced to 1 |
| MOD postload | `0x1402FE3B0` | command stream run after load (Gate 17) |

## Lights (Gates 22–24)

- **Selection** `0x1402EE560`: the top 4 records (stride 0x30) by score.
  Main list needs mask & 4, the secondary list mask & 8. Low flag 1 → mask
  & 3, 2 → mask & 2, 3 → mask & 1.
- **Score** `0x1402EE2A0`: type 0 → `max(0, 1 − d² · scale)`; other types 1.
  Record: flags +0x00, colour modifier +0x01, type +0x02, position +0x10,
  aux w +0x1C, scale +0x20, RGB u16 +0x24/+0x26/+0x28. Dot product helper
  `0x140030D30`.
- **Packing** `CDrawSCM` `0x1402FD040`: Lc rows = (r, g, b, scale or 1);
  Lv rows = (light position − query point, aux w, or 0 when type ≠ 0).
  `0x1402FC850` writes both into the model constants from +0x280
  (`MDL_LIGHT_MAT`). SCM +0x13 is the node whose position is the query point.
- **LIG2 as the source: UNPROVEN.** Stage cfg PAC slot 1 (0x920 bytes =
  0x20 header + 48 × 0x30) has the record shape above, but no path from it
  into `CLightMgr` (constructor `0x14023ECB0`, inside `CSceneGame` at +0x4078
  and +0x1D80, switch at `0x1402AA2FE` / `0x1402AAC6C`) has been shown.

## The stage files

| Stage | LIG2 records | Type 0 (point) | Type 1 (far) | With a colour modifier |
| --- | --- | --- | --- | --- |
| st000 | 8 | 5 | 3 | 0 |
| st001 | 9 | 6 | 3 | 3 |
| st002 | 13 | 10 | 3 | 2 |
| st006 | 3 | 0 | 3 | 0 |

- **Light flags.** Type 1 lights have flags 2 (they pass mask & 2); point
  lights mostly have 3 (they pass mask & 1). Lights with flags 4 are black
  and are never picked.
- **Header.** The six u16 after the version look like a colour (st000
  220, 210, 200; st001 95, 95, 95; st002 128, 128, 140), perhaps the
  ambient. This is not proven, and the Reader does not use them.
- **Object alpha.** MOD objects are all 0x80. SCM objects are 0x80 or
  0xC0..0xC5 (st006: 18 of 20 objects). No object of these files has the
  nearest-filter flag.

## In the Reader (v124)

The rules were written clean-room (`docs/cleanroom/specs-v124/`):

- **Material.** `game_material.h`. The per-vertex material byte
  (`Mesh::blend0`) now carries:
  - bits 0..3: the blend mode, as before;
  - bit 4: nearest filtering;
  - bit 5: part alpha 1.

  MOD and SCM objects write it. GPU batches split on it: a nearest batch
  samples nearest, and the DMC3 shaders take `u_part_alpha` from it. The HD
  bridge turns region repeat into WRAP, which the Reader already uses.

  On st000 and st006 the frame is unchanged, because the 0xC* objects'
  texels are opaque.
- **Stage lights.** `game_lights.h`. This is the experimental setting
  Settings → Graphics → Stage lights (off by default).
  - **Stage.** It uses the four main-list point lights (mask 5) the game
    would pick for the framing centre, drawn by `stage_lit` (falloff from
    the record).
  - **Model.** The top four for the model's centre (mask 7). Up to three
    of them become the rig's directions, with colour × score (128 = 1), plus
    a constant term of 0.45.
  - **Calibrated by eye.** The colour scales (the stage gain 1.25 over the
    light's reach and the summed room normal, and the model's constant
    term) are the Reader's. The game's are not known.
  - **Probe** (Mesa, start camera):
    - st000: its point lights warm the floor and walls (78% of the pixels
      change).
    - st002: only Dante changes, tinted by the violet light nearby. Its
      rooms have no vertex colours and keep the Reader's shaders.
    - st006 has no point lights.
