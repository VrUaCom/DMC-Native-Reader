# DMC Native Reader — Status

## Current milestone

`v0.8.0-modular-native-reader`

Canonical product repository: `VrUaCom/DMC-Native-Reader`.
Canonical DMC3 reverse/evidence source: `VrUaCom/dmc-rengine-cpp`.

## Current architecture

Native Reader now uses a registry-driven product path:

`probe -> NativeModuleRegistry -> per-format module -> inspection/mesh session -> Android UI`

The central family `if/else` dispatcher has been removed from `decode_pipeline.cpp`. New format support is registered as a module instead of adding another branch to a monolithic dispatcher.

SCM/MOD share a recovered Model Family mesh core while retaining separate SCM scene and MOD skin/topology adapters. The old `decode_resource()` function remains only as compatibility API; the production pipeline does not dispatch through it.

## Registered modules

Promoted modules currently include:

- SCM — `formats.scm.mesh-reader`
- MOD — `formats.mod.mesh-reader`
- HITS — `formats.hits.collision-reader`
- stage TXT — `formats.stage-txt.lexer`
- `.index` — `formats.index.manifest-reader`
- DDS — `formats.dds.dmc3-reader`
- PTX — `formats.ptx.bundle-reader`
- DCA — `formats.dca.record-reader`
- LIG — `formats.lig.record-reader`
- LIG2 — `formats.lig2.record-reader`
- PAC — `formats.pac.relative-slot-reader`
- PNST — `formats.pnst.relative-slot-reader`
- NBZ — `formats.nbz.container-reader`
- EFM — `formats.efm.family-adapter`
- MRP — `formats.mrp.family-adapter`
- SHW — `formats.shw.family-adapter`
- generic recognized-family structural inspector

EFM/MRP/SHW remain explicitly partial and non-renderable where exact semantics are still unresolved.

## Proven model-reader baseline

Retained development corpus evidence:

- SCM: 74/74 decoded
- MOD: 90/90 decoded
- total model corpus: 164/164 decoded
- 300,466 vertices materialized
- 192,413 triangles materialized
- no index out-of-bounds observed in that corpus pass

SCM/MOD remain fail-closed on unsupported versions, invalid tables and out-of-bounds streams.

## Additional semantic/structural readers

### HITS

HITS is a data/corpus-confirmed collision payload identity and is decoded by an independent collision module. Historical `HITS$` remains rejected and must not be described as a canonical EXE registry tag.

### TXT / .index

Stage TXT has a bounded lexer. `.index` has a dedicated extraction/naming manifest reader.

A `.index` manifest may legitimately begin with the literal text line `PNST` or `PAC`. The product pipeline therefore preserves `.index` filename identity over a content-like four-byte prefix so textual metadata cannot be misclassified as a binary PNST/PAC container.

### DDS

DDS structural acceptance currently requires:

- exact `DDS ` magic
- serialized 128-byte DDS envelope
- header size 124 and pixel-format size 32
- non-zero dimensions
- full mip chain down to `1x1`
- DXT1 or DXT5
- exact block-compressed payload extent and EOF

### PTX

PTX structural acceptance currently validates:

- `0x800` bundle header
- bounded texture count / sector-span table
- `0x70` descriptors
- `0x800` sector framing
- descriptor payload/DDS-size agreement
- bounded DXT1/DXT5 DDS children
- final zero-span exact EOF
- zero alignment padding where sector spans are present

### DCA / LIG / LIG2

- DCA: `DCA\0`, `0x10` header, integral `0x410` records
- LIG/LIG2: `0x20` header plus integral `0x30` records at the current evidence level

### PAC / PNST / NBZ

PAC and PNST use separate relative-slot modules and remain distinct resource families. NBZ has its own container module; broad child materialization/repack semantics are not claimed merely from recognition.

## Runtime/model-family boundary

- SCM — mesh-bearing stage/scene model
- MOD — mesh-bearing actor/object model
- EFM — effect-model family; exact real-payload stream/material binding remains open
- MRP — render-side companion; standalone mesh ownership is not proven
- SHW — shadow geometry/topology companion; external spatial ownership remains incomplete

Incomplete family evidence is surfaced as partial modules, never by reusing the SCM/MOD parser as a shortcut.

## Android identity and routing

- applicationId: `com.dmcrengine.nativereader`
- versionCode: `8`
- versionName: `0.8.0-modular-native-reader`
- ABI: `arm64-v8a`
- exported concrete Samsung/OEM entry point: `DmcOpenActivity`
- resources are opened read-only through Android file descriptors
- mapped resource cap: 512 MiB

Distinctive DMC extensions use OEM-oriented routes. Generic extensions such as `.bin`, `.txt`, `.sav` and common media remain available through generic provider/picker paths rather than being globally claimed by Native Reader.

## CI acceptance boundary

A promotion is accepted only if:

1. host C++ modular-registry regression compiles and passes;
2. Android NDK compiles all promoted modules for arm64-v8a;
3. APK contains `classes.dex` and `lib/arm64-v8a/libdmcviewer.so`;
4. compiled package is `com.dmcrengine.nativereader` versionCode `8`;
5. compiled versionName is `0.8.0-modular-native-reader`;
6. `DmcOpenActivity` remains the concrete exported system-open component;
7. representative DMC MIME/extension routes are present;
8. promoted module IDs are physically present in the built native `.so`;
9. APK signature matches the canonical development signer;
10. physical Samsung testing verifies update/install and real resource-open behavior.

The host regression currently covers registry presence plus representative DDS -> PTX composition, DCA, LIG2, TXT, `.index`, PAC and NBZ paths, including malformed rejection. It also guards the `.index`/`PNST` identity-precedence case found during v11 integration.

## Still not claimed as fully reversed

- complete SCM topology/`triCmd` semantics beyond the recovered preview path
- full material/texture binding for model rendering
- final MOD skeletal authoring semantics
- EFM exact real-payload stream-to-shader binding
- MRP exact fields/downstream owner
- SHW exact ownership/linkage of external spatial pool
- MCV exact field semantics
- CLT/C1D exact schemas
- HID/TSC exact schemas
- several stage/effect/audio-bank research-only families
- Capcom-equivalent writers/repackers for formats where only read/inspection support is promoted

## Physical-device test boundary

After final CI/merge, test on the Samsung device:

1. install v8 over the existing canonical Native Reader line;
2. launch directly and confirm the v8 modular version identity;
3. open known real SCM and MOD and verify non-empty 3D geometry;
4. open a real HITS and verify collision preview;
5. open DDS and PTX and verify structural information instead of rejection;
6. open representative DCA/LIG2/PAC/PNST/NBZ/TXT/.index resources;
7. confirm the displayed family/support/evidence line and module trace;
8. confirm non-renderable resources never display a stale previous mesh;
9. record Samsung My Files routing/provider diagnostics for any extension that still bypasses `DmcOpenActivity`.
