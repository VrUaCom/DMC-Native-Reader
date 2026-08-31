# Canonical DMC3 EXE format evidence

This document records the Native Reader evidence boundary for format identities found in the canonical DMC3 HD executable.

Canonical artifact:

- SHA-256: `e454272ed0fb0247fcbcf300e5d55d7a3e96d50b89b9ffaff81bb978dcbdd082`
- size: `6,356,432`
- PE32+ x86-64
- ImageBase: `0x140000000`

`EXE identity evidence` and `decoder support` are independent dimensions. A format may be definitely recognized by the game runtime while its complete on-disk schema remains unknown.

## Runtime type paths

### Three-byte registry/content probe — `0x1402DB1F0`

- `MOD` -> 0
- `EFM` -> 1
- `SCM` -> 2
- `MRP` -> 3
- `SHW` -> 7

The fourth byte is not checked on this path.

### Container dispatcher — `0x1401B9FA0`

Normal handlers:

- `MOD` -> `0x1402FE3B0`
- `EFM` -> `0x1402F7A90`
- `SCM` -> `0x1403051B0`
- `SHW` -> `0x1403204C0`

Additional identities:

- `EFE` — explicitly recognized, no normal handler on this path
- `EFW` — explicitly recognized, no normal handler on this path
- `PNST` — exact four-byte recursive container identity

`MRP` is recognized by the registry probe but does not have a generic handler in this dispatcher.

### Four-byte family-mask probe — `0x1402FD650`

This path requires the trailing ASCII space:

- `MOD ` -> `0x10000000`
- `EFM ` -> `0x20000000`
- `SCM ` -> `0x30000000`
- `MRP ` -> `0x40000000`
- `MCV ` -> `0x50000000`
- `SHW ` -> `0x60000000`

`MCV` is therefore EXE-confirmed as a runtime family identity even though it is absent from the three-byte registry probe.

## Extension classifiers

The canonical EXE directly checks these extension families:

- primary classifier: `.ptx`, `.clt`, `.c1d` with case variants;
- auxiliary classifier: `?.mot`/`.MOT`, `.mcv`, `.cam`, `.hid`, `.clt`, `.tsc` with uppercase variants;
- media capability classifier: `.PSS`, `.THP`, `.PAM`, `.XMV`, `.WMV`, `.PMF`, `.AVI`, `.MPG`, `.BIK`, `.MP4`.

Media capability recognition does not imply that Native Reader owns those common media formats or needs to register itself as their system default handler.

## Runtime path / filename evidence

The executable also contains runtime references for:

- `NBZ` (`%sDMC3-%d.nbz`)
- `AFS` namespaces (`GData.afs/`, `GDataX360.afs/`)
- large `.pac` resource-name corpus
- `.adx`, `.ogg`, `.sfd`, `.tm2`
- `basic.ptz`, `LOADERICON.dds`
- `.fon`, `.ico`, `icon.sys`
- `options.sav`, `dmc3.sav`
- `snd_sys.phd`, `snd_sys.tsb`, `snd_sys.bd`
- `SpuMap.bin`, `EventTblNN.bin`
- large `.txt` corpus
- explicit `SPUMAPDT` string

These are reference/path identities, not automatically complete format schemas.

## Boundaries

- `HITS` has data/corpus structural evidence, but the canonical EXE bounded sweep has zero ASCII `HITS` occurrences. It must not be promoted to an EXE runtime tag.
- `HITS$` is rejected and must not be reintroduced.
- `LIG2` has an ASCII occurrence in the EXE, but a single occurrence alone is not enough to claim a runtime format-dispatch identity.
- Embedded shader compiler metadata such as `RDEF`, `SPDB`, `D3DSHDR`, `SHEX` and input-signature blobs are not DMC resource families and are intentionally excluded.
- Random printable fragments from machine code are not format evidence unless they are connected to a bounded classifier, handler, path, or other runtime evidence.

## Native Reader policy

Native Reader uses this evidence registry to show where a format identity comes from while keeping decoding fail-closed:

- `SCM` / `MOD`: validated mesh preview;
- structurally understood formats: bounded inspection;
- EXE-recognized but not structurally closed formats: recognition/inspection only;
- no guessed mesh decoder for `MRP`, `MCV`, `EFE`, `EFW`, or other unresolved families.
