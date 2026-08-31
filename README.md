# DMC Native Reader

Native Android reader/viewer for Devil May Cry resource files. The Android app is a separate product repository, while `VrUaCom/dmc-rengine-cpp` remains the canonical reverse/evidence source for DMC3 HD format semantics.

## Current milestone — v8

- versionCode: `8`
- versionName: `0.8.0-format-catalog-inspection`
- applicationId: `com.dmcrengine.nativereader`
- ABI: `arm64-v8a`
- Android compile/target SDK: `36`
- minSdk: `26`
- NDK: `28.2.13676358`
- CMake: `3.22.1`

The package id and canonical development signer remain unchanged from v6/v7 so v8 is intended to install as an update over the canonical test line.

## Architecture

v8 deliberately separates three levels of support:

1. **Recognition** — identify a catalogued DMC resource family from content, filename or extension without inventing a schema.
2. **Structural inspection** — read bounded fields/records for formats whose structure is already evidenced strongly enough.
3. **3D preview** — materialize a normalized mesh only where the Native Reader decoder is actually validated.

This prevents a known failure mode in reverse-engineering tools: treating every model-adjacent format as if it shared one mesh layout.

## Proven 3D preview

SCM and MOD remain the only families currently promoted to the native mesh-preview path.

Development corpus evidence from the existing decoder milestone:

- SCM: 74/74 decoded
- MOD: 90/90 decoded
- Total: 164/164 decoded
- 300,466 vertices materialized
- 192,413 triangles materialized
- no index out-of-bounds observed in the corpus pass

Full `triCmd` semantics, textures/materials and exact MOD skeletal skinning remain open reverse boundaries.

## v8 format catalog

The native catalog is derived from the current DMC3 HD format-purpose/runtime evidence in `dmc-rengine-cpp` and includes container, geometry/render, texture, animation, camera, collision, lighting, item/stage, audio/video, persistence and legacy UI families.

Representative families include:

- containers/materialization: `NBZ`, `PAC`, `PNST`, `PACK`, `.index`, `.lst`, AFS namespace
- geometry/render: `SCM`, `MOD`, `EFM`, `MRP`, `SHW`
- collision/camera/lighting: `HITS`, `DCA`, `CAM`, `LIG`, `LIG2`
- textures: `DDS`, `PTX`, `TIM2/TM2`, `PTZ`
- animation/control: `MOT` variants, `MCV`, `HID` variants, `CLT`, `C1D`, `TSC`
- stage/gameplay: `EVE`, `POS`, `ITM`, `STE`, `EST`, stage `TXT`, `EventTblNN.bin`
- audio/video: `ADX`, `OGG`, `VAGp`, `PHD`, `TSB`, `BD`, `SPUMAPDT`, `SFD`, `WMV`, media-capability families
- persistence/UI: `dmc3.sav`, `options.sav`, `FON`, `ICO`, `icon.sys`

Not every catalog entry is claimed to have a recovered binary schema. The app exposes an evidence/support label such as `mesh-preview`, `structural`, `recognized`, `runtime-only`, `research-only`, `capability-only` or `fallback-only`.

## Structural inspection in v8

Current native inspection includes bounded summaries for:

- `PAC` / `PNST`: declared slots, populated/empty slots, alias offsets and offset-boundary validity
- `HITS`: collision grid dimensions, triangle count and relative section offsets
- `DCA`: `0x10` header + `0x410` record envelope
- `LIG2`: `0x20` header + `0x30` record envelope
- `DDS`: basic dimensions when the DDS content signature is present
- `NBZ`: filename/extension identity plus ZIP-prefix observation without pretending that this proves a binary AFS container

Additional families can be opened as recognition/inspection sessions without forcing them through the SCM/MOD mesh decoder.

## HITS correction

`HITS$` is **not** a canonical DMC3 format identity and must not be reintroduced.

Current canonical EXE reverse establishes:

- the scoped three-byte registry content probe recognizes `MOD`, `EFM`, `SCM`, `MRP`, `SHW`
- the four-byte family-mask classifier recognizes `MOD `, `EFM `, `SCM `, `MRP `, `MCV `, `SHW `
- the bounded canonical EXE sweep found zero ASCII `HITS` occurrences

The project collision parser nevertheless has data/corpus evidence for a real four-byte `HITS` payload identity. v8 therefore labels `HITS` as a structural collision payload identity, **not** as an EXE registry tag. Historical `HITS$` remains rejected.

## Android routing

The exported concrete `DmcOpenActivity` remains the OEM/Samsung system-open entry point. v8 retains typed/untyped `content://` and `file://` fallbacks and adds extension-specific routes for distinctive DMC resource families.

Generic extensions such as `.bin`, `.txt`, `.sav`, `.mp4` and other common user file types are intentionally not broadly claimed by the extension-specific handler. They remain available through the app picker/generic provider route so Native Reader does not advertise itself as the preferred handler for unrelated files.

## Native safety boundary

- files are opened read-only through Android file descriptors
- mapped resource size is capped at 512 MiB
- SCM/MOD decoding remains fail-closed on unsupported/invalid layouts
- non-mesh resources cannot reuse or display a stale previous mesh frame
- recognition does not imply complete semantic reverse

## Build and CI

GitHub Actions builds the ARM64 APK and verifies:

- ZIP integrity
- `classes.dex`
- `lib/arm64-v8a/libdmcviewer.so`
- package/version identity
- compiled manifest routes
- representative v8 DMC MIME/extension routes
- absence of rejected `HITS$` routing
- APK Signature Scheme v2/v3
- canonical test signer fingerprint
- APK SHA-256 evidence

Canonical development/test certificate SHA-256:

`f483539463f89dd957a8f7c68a3bb75da17450163f2e8767b4c47d5f1899adac`

The repository test key is disposable development infrastructure, not a production signing identity.

See `docs/STATUS.md` for the current evidence and device-test boundary.
