# v72 stage-room HITS environment collision

Date: 2026-09-28
Reader branch: `fix/em034-lady-assembly-info-export`
Canonical executable SHA-256: `e454272ed0fb0247fcbcf300e5d55d7a3e96d50b89b9ffaff81bb978dcbdd082`

This change adds a generic, read-only `EnvironmentCollision` surface for
stage PACs. It is separate from character attack collision (`COLINDEX` plus
shape records) and from the visible SCM/MOD room mesh.

## Corpus receipt

The supplied stage surfaces are recorded in the Rengine evidence branch:

`docs/research/dmc3-stage-room-hits-census-2026-09-28.md`

| PAC | HITS slots | grid / triangles / cell references |
|---|---|---|
| `st001.pac` | slot 3 + slot 6 | `9x6x7 / 294 / 1648`; `4x2x2 / 20 / 82` |
| `st002.pac` | slot 3 | `12x9x18 / 477 / 3114` |

The HITS parser accepts the real four-byte `HITS` magic. The raw words at
`+0x20..+0x2B` are retained as `u32[3]` because the supplied retail surfaces
contain `500/500/500` or `300/300/300` while the older shared view calls that
lane `Vec3f`. No cell-size meaning is promoted until the EXE scalar contract
is closed.

## Reader contract

- `Format::Hits` is content-identified and registered as `Collision`.
- PAC children retain their physical `slot-N` identity.
- Every HITS payload is parsed into an independent
  `stage_room::Room::collision_sources` record.
- Source 0 and source 1 are not merged or reinterpreted.
- The optional `HITS room collision` setting draws only the first explicit
  source as a light-blue inspection overlay; it does not affect runtime state.
- The overlay uses the same room pivot/yaw/offset as the visible stage mesh.
- HITS is never routed through the character attack collision path.

The first source is an inspection default only. It is not a claim that every
profile uses source 0 for every collision query.

## Shl02 boundary

The canonical EXE path confirms that Shl02 movement refreshes steering from a
live gameplay target/query manager. HITS is a stage collision resource, but it
does not provide that target. Therefore v72 does not synthesize a target,
straight-line trajectory, bounce, or explosion from HITS alone. Future generic
world-context integration must supply an explicit EXE-backed target/query
service and then use the exact HITS source/query contract.

Until that boundary is closed, Reader may show the exact Shl02 spawn pose and
the retained V/E resource graph, but it must not invent post-spawn motion.

## Verification

The native regression suite now covers:

- direct HITS recognition and `Collision` capability;
- canonical HITS parse with raw cell-size preservation;
- PAC assembly retaining HITS by physical slot;
- room construction retaining HITS separately from visible geometry;
- six line pairs per synthetic triangle for the optional overlay.

Android build and physical device acceptance remain CI/device gates.
