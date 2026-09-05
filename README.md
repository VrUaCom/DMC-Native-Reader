# DMC Native Reader

Native Android reader/viewer for Devil May Cry resource files. This repository is the Android product. `VrUaCom/dmc-rengine-cpp` remains the canonical reverse/evidence source for DMC3 HD format semantics, while format delivery in this app is performed through independent native modules.

## Current milestone — v8 modular Native Reader

- versionCode: `8`
- versionName: `0.8.0-modular-native-reader`
- applicationId: `com.dmcrengine.nativereader`
- ABI: `arm64-v8a`
- Android compile/target SDK: `36`
- minSdk: `26`
- NDK: `28.2.13676358`
- CMake: `3.22.1`

The package id and canonical development signer remain unchanged from v6/v7, so v8 is intended to install as an update over the canonical Native Reader test line.

## Modular architecture

The product path is now:

`resource -> bounded probe -> NativeModuleRegistry -> per-format module -> inspection/mesh session -> Android UI`

`decode_pipeline.cpp` no longer contains a growing format `if/else` dispatcher. Recognition and decoding remain deliberately separate: recognizing a format never authorizes invented semantics.

The registry currently owns independent modules for:

- `SCM` — mesh reader + scene-transform adapter
- `MOD` — mesh reader + skin/topology adapter
- `HITS` — collision mesh reader
- stage `TXT` — bounded lexer
- `.index` — extraction/naming manifest reader
- `DDS` — DXT1/DXT5 structural reader with complete mip-chain validation
- `PTX` — texture-bundle reader with bounded DDS-child validation
- `DCA` — `0x10` header / `0x410` record reader
- `LIG` / `LIG2` — `0x20` header / `0x30` record readers
- `PAC` / `PNST` — relative-slot structural readers
- `NBZ` — container inspection module
- `EFM`, `MRP`, `SHW` — explicit evidence-gated family adapters
- a generic structural module for recognized catalog families whose exact standalone schema is not yet promoted

SCM and MOD share a model-family mesh core, while their format-specific scene/skin/topology adapters remain separate. The legacy `decode_resource()` entry point remains only as compatibility API; the product route dispatches through the module registry.

## Support levels

The reader separates four practical levels:

1. **Recognition** — identify a catalogued DMC resource family from content, filename or extension.
2. **Structural inspection** — validate bounded headers, records, offsets or payload framing.
3. **Native semantic decode** — materialize recovered format-specific data such as text tokens, texture framing or collision geometry.
4. **3D preview** — materialize a normalized mesh only where the decoder has sufficient evidence.

Incomplete reverse work is visible as a partial/evidence-gated module rather than being silently routed through another format's parser.

## Native 3D preview

Current renderable native modules are:

- `SCM` — stage/scene geometry
- `MOD` — actor/object geometry
- `HITS` — collision geometry

Development corpus evidence retained from the model decoder milestone:

- SCM: 74/74 decoded
- MOD: 90/90 decoded
- Total model corpus: 164/164 decoded
- 300,466 vertices materialized
- 192,413 triangles materialized
- no index out-of-bounds observed in that corpus pass

The Model Family path remains fail-closed on unsupported versions, invalid tables and out-of-bounds streams.

## Texture modules

### DDS

The DDS module validates the DMC3-HD reader subset rather than accepting arbitrary DDS bytes:

- exact `DDS ` magic
- 128-byte serialized DDS envelope (`dwSize = 124`, pixel-format size `32`)
- non-zero dimensions
- complete mip chain down to `1x1`
- DXT1 or DXT5 FourCC
- exact block-compressed payload size
- exact EOF at the end of the mip payload

### PTX

The PTX module validates the recovered texture-bundle framing:

- `0x800` bundle header
- bounded texture count / sector-span table
- `0x70` per-texture descriptors
- `0x800` sector framing
- descriptor DDS-size and payload-size agreement
- bounded DXT1/DXT5 DDS child resources
- final zero-span exact-EOF behavior
- zero alignment padding for sector-bounded entries

PTX validation therefore composes the DDS reader instead of treating texture children as opaque bytes.

## Text and metadata

Stage `TXT` and `.index` are independent modules. `.index` is extraction/naming metadata, not a runtime container authority.

A canonical `.index` file may begin with the literal text line `PNST` or `PAC`. The modular pipeline explicitly preserves `.index` filename identity in that case so the text prefix cannot be misclassified as binary PNST/PAC magic.

## Wider format catalog

The native catalog still covers the broader DMC3 HD resource namespace, including:

- containers/materialization: `NBZ`, `PAC`, `PNST`, `PACK`, `.index`, `.lst`, AFS namespace
- geometry/render: `SCM`, `MOD`, `EFM`, `MRP`, `SHW`
- collision/camera/lighting: `HITS`, `DCA`, `CAM`, `LIG`, `LIG2`
- textures: `DDS`, `PTX`, `TIM2/TM2`, `PTZ`
- animation/control: `MOT` variants, `MCV`, `HID` variants, `CLT`, `C1D`, `TSC`
- stage/gameplay: `EVE`, `POS`, `ITM`, `STE`, `EST`, stage `TXT`, `EventTblNN.bin`
- audio/video: `ADX`, `OGG`, `VAGp`, `PHD`, `TSB`, `BD`, `SPUMAPDT`, `SFD`, `WMV`, media-capability families
- persistence/UI: `dmc3.sav`, `options.sav`, `FON`, `ICO`, `icon.sys`

Catalog presence is not a claim of full reverse. Families without a promoted decoder enter the generic/partial inspection path and remain non-renderable.

## HITS correction

`HITS$` is **not** a canonical DMC3 format identity and must not be reintroduced.

Current canonical EXE reverse establishes that the model-family registries cover `MOD`, `EFM`, `SCM`, `MRP`, `MCV`, `SHW`, while a separate data/corpus parser establishes a real four-byte `HITS` collision payload identity. Native Reader therefore treats `HITS` as a collision data format, not as an EXE registry tag.

## Android routing

The exported concrete `DmcOpenActivity` is the Samsung/OEM system-open entry point. The app retains typed/untyped `content://` and `file://` fallbacks plus extension-specific routes for distinctive DMC formats.

Generic extensions such as `.bin`, `.txt`, `.sav` and common media are intentionally not broadly claimed by the extension-specific handler. They remain available through the picker/generic provider route.

## Native safety boundary

- resources are opened read-only through Android file descriptors
- mapped resource size is capped at 512 MiB
- all modules use bounded reads before structural interpretation
- SCM/MOD decoding is fail-closed on unsupported/invalid layouts
- non-renderable modules cannot reuse/display a stale previous mesh frame
- recognition never implies complete semantic reverse
- EFM/MRP/SHW remain explicit partial modules rather than aliases of SCM/MOD

## Build and CI

CI now has two native gates before an APK is accepted:

1. **host modular-reader regression** — compiles the complete registry and exercises representative DDS -> PTX composition, DCA, LIG2, TXT, `.index`, PAC and NBZ cases, including malformed rejection;
2. **Android arm64 build** — compiles the same module translation units into `libdmcviewer.so`.

The APK verification then checks:

- ZIP integrity
- `classes.dex`
- `lib/arm64-v8a/libdmcviewer.so`
- production package/version identity
- compiled `DmcOpenActivity` routes
- presence of every promoted module ID in the native `.so`
- APK Signature Scheme verification
- canonical development signer fingerprint
- APK SHA-256 evidence

Canonical development/test certificate SHA-256:

`f483539463f89dd957a8f7c68a3bb75da17450163f2e8767b4c47d5f1899adac`

The repository test key is disposable development infrastructure, not a production signing identity.

See `docs/STATUS.md` for the current evidence and physical-device test boundary.
