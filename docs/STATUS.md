# DMC Native Reader — Status

Last updated: 2026-09-10.

## Accepted baseline (`main`)

- Product line: **Native Reader 1.0**
- versionName: `1.0`
- versionCode: `24`
- accepted v24 code baseline: `5a69a3cde2cd4af3534ad7056ea55b09f0e91659`
- package: `com.dmcrengine.nativereader`
- ABI: `arm64-v8a`
- minSdk / targetSdk: `26 / 36`
- production module registry: **4 modules — MOD, SCM, DDS, PTX**
- canonical reverse/read-side authority: `VrUaCom/dmc-rengine-cpp` / pinned `ReaderCore`
- archived pre-cleanup implementation: `main.2` — backlog/reference only

Documentation commits may advance `main` beyond the accepted code-baseline SHA without changing the accepted v24 APK/code behavior.

## Acceptance evidence

v24 was accepted on a physical Samsung device on 2026-09-10. The owner confirmed that all four supported file types open successfully and PTX texture application works. Android reported **2.32 MB installed size**, down from 6.27 MB before the v24 cleanup.

Build-side evidence for v24 includes seven passing local/native regressions, verified arm64 APK identity/signature/ZIP/module gates, only 18 declared public JNI exports, and removal of the unintended Kotlin runtime dependency. GitHub-hosted Actions jobs on the tested revision failed before executing steps, so CI is **not** claimed green; the accepted evidence is local regression + APK verification + physical-device acceptance.

See `SIZE_AND_MODULES_V24.md` for exact artifact measurements and hashes.

## Current architecture

```text
resource bytes
  -> bounded probe / DMC Rengine ReaderCore
  -> NativeModuleRegistry (MOD | SCM | DDS | PTX)
  -> typed module/adapter projection
  -> InspectionDocument / RenderScene / ImagePreview / ChildResource[]
  -> DMCNativeReader::Core
      -> resource_session
      -> scene_projection
      -> texture/material binding
      -> Black Widow typed state
      -> direct C++ rendering
  -> thin Android JNI + Java shell
```

Unknown/unpromoted formats fail closed. Java does not parse DMC binary layouts and the renderer does not own format parsers.

## Accepted capabilities

### MOD

- canonical structural parsing;
- renderable geometry;
- rotate / zoom / wireframe;
- typed inspection;
- hierarchy/spatial projection when canonical authority is valid;
- skin weights;
- canonical texture-slot and legacy GS state;
- PTX companion attachment for valid model texture bindings.

### SCM

- canonical structural parsing;
- renderable geometry;
- canonical scene hierarchy and transforms;
- rotate / zoom / wireframe;
- typed inspection;
- texture-slot state;
- PTX companion attachment through the shared texture path.

### DDS

- bounded DXT1/DXT5 parsing/decoding;
- generic RGBA image preview;
- malformed/overflow rejection.

### PTX

- bounded texture-bundle framing;
- generic DDS child resources;
- thumbnail/gallery presentation;
- child preview and parent-session navigation;
- shared TextureSet path for model companion application.

## Active development candidate

Draft PR #32 on `feature/dds-ptx-v1-acceptance` currently carries **v26** (`versionCode 26`, `versionName 1.0`). It adds:

- v25 per-texture-slot UV gallery with per-slot triangle grouping, zoom/reset and shared gallery infrastructure;
- v26 long-press information for UV slots/triangle counts;
- long-press object/mesh structure report;
- long-press node/bone parent relationship report;
- separate hierarchy-information authority from spatial-render authority;
- 9 portable/native regressions and verified v26 APK gates.

v26 is **not yet accepted** because Samsung/device validation is still pending. Until that closes, v24 remains the stable code/product baseline in `main`.

## Not in production registry

HITS, TXT, `.index`, DCA, LIG/LIG2, PAC/PNST, NBZ, EFM/MRP/SHW and the previous wide recognition catalog are absent from the current `main` registry/build. Their existence in historical branches or reverse documentation does not make them supported Native Reader modules.

Future promotion requires a bounded Architecture v2 module, canonical/evidence-backed authority and regression/device evidence appropriate to the feature.
