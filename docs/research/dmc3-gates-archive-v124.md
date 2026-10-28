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

## Gates 44–45, checked against our disassembly

Gates 44–45 traced the switch at `0x1402AA2FE`: `lea rcx, [rdi+0x940]`,
then `call 0x140089DE0`, which returns `rcx + 0x80`. Read further:

- **`0x140089DE0` is not a light function.** It is the body
  `lea rax, [rcx+0x80]; ret`, called from 2 695 sites. The linker folded
  every identical "address of the member at +0x80" accessor into it, so
  its callers say nothing about lights.
- **What the branch does.** The function `0x1402AA2B0` is a step machine
  on `this+0x09` (steps 0..12). The branch at `0x1402AA300` calls
  `0x1402F7350(model, 1)` and `0x1402F7350(model, 2)` on the model at
  `this+0x940` (+0x80). Then it advances the step.
- **`0x1402F7350(model, i)`.** If `i < model+0xE8` (u16, object count),
  it clears bit 0 of the first word of object `i`. Objects are
  `model+0x100 + i·0x380`, the MOD runtime objects. It hides object `i`.
  - `0x1402F74E0` sets the same bit (show).
  - `0x1402F73A0` clears `model+0xE0` bit 0 and loops over every object
    (hide all).
- **So the scene's `+0x940` is a model** whose objects 1 and 2 are hidden
  at that step. It is not LIG2. The match with the LIG2 object's `+0x940`
  is a coincidence.

### The LIG2 object itself (constructor `0x14023ECB0`)

| Offset | Content |
| --- | --- |
| +0x00 | vtable `0x1404E3128` |
| +0x08 | `LIG2` |
| +0x28 | the 0x900 bytes of records (48 × 0x30) |
| +0x928 | 48 entries of 0x20 bytes, built by `0x140345B24` (constructor `0x1400F7FF0`, destructor `0x1400F8310`) |

Each entry `k` holds at its `+0x18` (object `+0x940 + 0x20·k`) the
pointer to record `k` (`+0x28 + 0x30·k`).

- **Who builds it.** It is constructed into `this+0x4078` by `0x14023EA50`
  and into `this+0x1D80` by `0x14023EEF0` (two different owners). The
  vtable is also written at `0x14023F616`, `0x14023F75D`, `0x14023F957` and
  `0x14023FCB9`.
- **Next lead for the binding.** Find the code that walks the entries
  (stride 0x20 from `+0x928`, pointer at `+0x18`) or calls through vtable
  `0x1404E3128`. Reaching `0x1402EE560`'s source lists from there would
  prove (or rule out) LIG2 → `CLightMgr`.
