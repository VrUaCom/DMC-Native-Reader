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

The fourth byte is not checked on this path. Native Reader mirrors that identity behavior but only promotes canonical `MOD ` and `SCM ` payloads to the validated mesh decoder. A payload such as `MODX` remains runtime-recognized but non-renderable.

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

## Additional direct content checks

A whole-`.text` immediate-comparison census exposed three more bounded content identities:

- `VAGp` — `0x140032970` compares the first DWORD directly to `VAGp` before the payload path continues;
- `DDS ` — `0x140049A8E` and `0x14004AD9D` compare the first DWORD to the DDS magic;
- `TM2\0` — `0x1403365BA` compares the first DWORD to `TM2\0` before continuing into the texture path.

This corrects an important Native Reader boundary: canonical DMC3 TM2 content is recognized from `TM2\0`; the previous `TIM2`-only content probe is not used as EXE authority. A legacy/data `TIM2` identity may still be recognized by the general catalog, but it is separate from this canonical EXE proof.

The same immediate-value census found DDS pixel-format FourCCs (`DXT1/2/3/4/5`, `ATI1/2`, `BC4U/BC4S`, `BC5U/BC5S`, `RGBG`, `GRGB`, `YUY2`, `DX10`). Those are DDS subformats, not standalone DMC resource families, and are not registered as separate formats.

## LIG2 object tag

`LIG2` is stronger than a loose printable occurrence. The constructor beginning at `0x14023ECB0` executes at `0x14023ECC9`:

`mov DWORD PTR [rcx+0x8], 0x3247494c`

which writes ASCII `LIG2` into object offset `+0x08`. Native Reader records this as `EXE_CONFIRMED_OBJECT_TAG`. This proves an executable-side LIG2 type/tag identity, but it still does not by itself prove that every `.lig`/`.lig2` field has been semantically recovered.

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
- Embedded shader compiler metadata such as `RDEF`, `SPDB`, `D3DSHDR`, `SHEX` and input-signature blobs are not DMC resource families and are intentionally excluded.
- DDS compression FourCCs are texture subformats, not standalone DMC resource formats.
- Random printable fragments from machine code are not format evidence unless they are connected to a bounded classifier, handler, content check, constructor tag, path, or other runtime evidence.

## Native Reader policy

Native Reader uses this evidence registry to show where a format identity comes from while keeping decoding fail-closed:

- `SCM` / `MOD`: validated mesh preview only for canonical mesh magic/layouts;
- structurally understood formats: bounded inspection;
- EXE-recognized but not structurally closed formats: recognition/inspection only;
- direct EXE content identities (`VAGp`, `TM2\0`, `DDS `) may be recognized without relying on filename extension;
- no guessed mesh decoder for `MRP`, `MCV`, `EFE`, `EFW`, or other unresolved families.
