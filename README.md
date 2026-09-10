# DMC Native Reader

Native Android reader for Devil May Cry 3 HD Collection resources, built around a reusable C++20 core and canonical DMC Rengine read-side authority.

## Current state

**Accepted `main`: Native Reader 1.0 / versionCode 24**  
**Accepted v24 code baseline:** `5a69a3cde2cd4af3534ad7056ea55b09f0e91659`  
**Android:** arm64-v8a, minSdk 26, targetSdk 36  
**Production module registry:** exactly **MOD / SCM / DDS / PTX**

The v24 APK was accepted on a physical Samsung device on 2026-09-10. The owner confirmed all four supported file families open successfully, PTX texture application works, and Android reported 2.32 MB installed size versus 6.27 MB before the v24 size/module cleanup.

The latest development candidate is **v26** on `feature/dds-ptx-v1-acceptance` / draft PR #32. It adds per-texture-slot UV galleries and long-press information for UV, object/mesh structure and bone/node parent relationships. Its host/native/APK gates pass, but **device acceptance is still pending**, so v26 is not yet the accepted `main` baseline.

## What the accepted v24 reader does

### MOD

- canonical `dmc-rengine-cpp` structural parsing;
- 3D geometry through `RenderScene`;
- rotate / zoom / wireframe presentation;
- hierarchy and spatial projection only when canonical authority is valid;
- skin weights and typed inspection;
- canonical texture-slot / legacy GS state projection;
- PTX companion attachment for evidence-valid model texture bindings.

### SCM

- canonical `dmc-rengine-cpp` structural parsing;
- scene hierarchy, transforms and geometry;
- rotate / zoom / wireframe presentation;
- typed inspection and texture-slot state;
- PTX companion application through the shared native texture-binding path.

### DDS

- bounded DXT1/DXT5 validation and decoding;
- generic RGBA `ImagePreview`;
- malformed/overflow rejection.

### PTX

- bounded texture-bundle framing;
- DDS child resources;
- generic thumbnail/gallery presentation;
- child image preview and parent-session navigation;
- reusable `TextureSet` path for model companion attachment.

Unknown and unpromoted resource families fail closed.

## Architecture

```text
resource / child resource
        |
        v
bounded probe + DMC Rengine ReaderCore
        |
        v
NativeModuleRegistry
        |
        +--> MOD adapter --> InspectionDocument + RenderScene
        +--> SCM adapter --> InspectionDocument + RenderScene
        +--> DDS module  --> ImagePreview
        `--> PTX module  --> ChildResource[] / TextureSet
        |
        v
DMCNativeReader::Core
        |
        +--> resource_session
        +--> scene_projection
        +--> texture/material binding
        +--> Black Widow typed UI state
        `--> direct C++ renderer
        |
        v
thin Android JNI + Java shell
```

The Android shell does not parse DMC binary layouts. Format knowledge belongs to canonical/native modules; `InspectionDocument`, `RenderScene`, `ImagePreview` and child/session contracts are presentation IR. The v24 JNI DSO exposes only the declared JNI boundary instead of thousands of C++ symbols.

## Product boundaries

DMC Native Reader is intentionally **read-only**. Editing, writing and repacking belong to DMC Rengine / future authoring tooling, not to Android-only format writers.

The pre-cleanup multi-format implementation is preserved on branch `main.2` as backlog/reference. HITS, TXT/index, DCA, LIG/LIG2, PAC/PNST, NBZ, EFM/MRP/SHW and other families are not production modules until they are individually promoted through the current Architecture v2 contract with evidence and regression coverage.

## Documentation

Start with [`docs/README.md`](docs/README.md), then see:

- [`docs/STATUS.md`](docs/STATUS.md) — accepted baseline and active candidate;
- [`docs/ARCHITECTURE_V2.md`](docs/ARCHITECTURE_V2.md) — current module/session architecture;
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — completed and next work;
- [`docs/SIZE_AND_MODULES_V24.md`](docs/SIZE_AND_MODULES_V24.md) — v24 size/module evidence;
- [`CHANGELOG.md`](CHANGELOG.md) — release/development history.

## Authority and evidence

`MOD` and `SCM` read-side format authority is vendored through the pinned DMC Rengine `ReaderCore`. DDS/PTX framing/codec logic is likewise kept behind native reusable boundaries. Reverse-engineering claims remain evidence-scoped: unknown semantics stay unknown rather than being renamed from guesses.

DMC Native Reader is an independent fan-made interoperability/modding project and is not affiliated with Capcom. See [`NOTICE.md`](NOTICE.md).
