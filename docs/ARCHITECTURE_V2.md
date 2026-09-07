# Native Reader Architecture v2 — stable v1 core contract

**Status:** stable v1.0.0 core baseline  
**Stable v1 product shell:** Android  
**Preview shells:** iOS / Windows  
**Core product surface:** MOD / SCM / DDS / PTX  
**Central engine/modding foundation:** DMC Rengine C++20

## Dependency direction

```text
resource bytes
        ↓
bounded / DMC Rengine-backed C++20 authority
        ↓
NativeModuleRegistry
        ↓
format module / canonical adapter
        ↓
PipelineResult
        ↓
InspectionDocument | RenderScene | ImagePreview | ChildResource[] | ResourceCapabilities
        ↓
platform-neutral ownership / bridge
        ↓
platform presentation shell
```

For stable Android v1.0.0, the ownership/bridge path is the existing JNI Session. The cross-platform preview branch adds `PortableSession`, a C++20 owner around the same `PipelineResult` contracts for iOS and Windows.

`PortableSession` is not a second decoder. It calls the same `run_decode_pipeline()`, materializes `RenderScene` once for CPU rendering, exposes the same `ImagePreview` / `ChildResource[]` data, and flattens the same typed `InspectionDocument` for thin platform presentation.

No platform-UI component owns a promoted format parser. No renderer owns a format parser. No wildcard module or broad recognition-only catalog is present in the v1 core.

## DMC Rengine authority rule

DMC Rengine is the central decompilation/reimplementation engine and modding foundation. Reusable engine-level semantics, typed resource contracts, readers, writers, validation and recovered runtime behavior belong there when they are part of the common modding core.

Native Reader consumes the read-side capabilities needed for viewing and inspection. It must not create a private incompatible interpretation merely to satisfy one platform UI.

## Core modules

`NativeModuleRegistry` contains exactly four promoted v1 modules:

- **MOD** — canonical DMC Rengine parser -> Architecture v2 adapter -> typed inspection, geometry, hierarchy/spatial state, skin weights and texture/material state where supported;
- **SCM** — canonical DMC Rengine parser -> Architecture v2 adapter -> typed inspection, scene hierarchy, transforms, geometry and texture/material state;
- **DDS** — bounded DMC3 DXT1/DXT5 texture module -> generic `ImagePreview`;
- **PTX** — bounded texture-bundle module -> generic `ChildResource[]`, with each DDS child using the same generic preview/session path.

DDS/PTX are direct Architecture v2 modules in the current v1 snapshot. Future authority synchronization with DMC Rengine must remove duplication rather than reintroduce a compatibility bridge.

## Core contracts

### ResourceCapabilities

The native module declares capabilities; the platform shell consumes them. Current core capabilities include Inspection, Geometry, Wireframe, NodeHierarchy, SkeletalSkinning, SkinWeights, TextureBinding, ImagePreview and ChildResources as appropriate.

### InspectionDocument

Generic evidence-aware presentation tree. Format-specific typed data is projected into it once. It is presentation IR, not a second parser.

### RenderScene

The only downstream geometry/hierarchy render authority. MOD and SCM publish geometry, nodes and optional skin/texture bindings through `RenderScene`. There is no `DecodeResult -> Mesh -> RenderScene` compatibility bridge in the stable v1 path.

### ImagePreview

Generic image-preview contract used by DDS and PTX DDS children. Platform presentation code does not contain a second DDS binary parser.

### ChildResource

Generic nested-resource projection. PTX publishes DDS children through the same typed contracts used by a top-level resource. Platform code may present a gallery/navigation model but may not decode PTX bytes independently.

### PortableSession

Cross-platform preview owner with these responsibilities only:

- call the existing Architecture v2 pipeline;
- reject any resource the core rejects;
- cache materialized render geometry for a static decoded resource;
- expose root image preview and child resources;
- expose typed inspection as presentation text where a richer native tree UI has not yet been built;
- preserve the same read-only/fail-closed contract.

It may not add format recognition, binary layout knowledge or alternate semantics.

## Platform shells

### Android — stable v1.0.0

```text
generic native Session
        ↓
JNI bridge
        ↓
ResourceUiState / capability-driven Android UI
```

This remains the accepted stable v1 platform implementation.

### iOS — preview

```text
PortableSession
      ↓
Objective-C++ ABI bridge
      ↓
SwiftUI / Files integration
```

The current iOS preview compiles the same four-format C++20 module/adapters and replaces the old pre-v1 iOS experiment. HITS/TXT/index decoders are not revived.

### Windows — preview

```text
PortableSession
      ↓
native Win32 shell
```

The current x64 preview uses the same CPU renderer, image preview and child-resource contracts. Windows-specific code owns only file/window/input/presentation concerns.

### Web — planned

```text
C++20 core
   ↓
WebAssembly
   ↓
thin browser presentation layer
```

JavaScript/TypeScript may own DOM, browser file APIs and rendering integration but not a second DMC binary parser.

See [`CROSS_PLATFORM.md`](CROSS_PLATFORM.md).

## Not promoted into the stable v1 core

The following historical/experimental paths are intentionally absent from the supported registry/build:

- HITS;
- TXT and `.index`;
- DCA;
- LIG / LIG2;
- PAC / PNST;
- NBZ ordinary-format module;
- EFM / MRP / SHW partial adapters;
- broad recognition-only catalog;
- legacy `DecodeResult` compatibility API and its old decoders.

Historical branches may preserve experiments for research reference, but they are not production authority. Promotion requires an Architecture v2 module with clean canonical/evidence authority and regression coverage.

## Non-negotiable v1 gates

- exactly four registered production modules until an explicit promotion is accepted;
- unknown and unpromoted families fail closed;
- retired source paths may not re-enter the core build tree accidentally;
- MOD and SCM pass full pipeline projection tests, not only registry checks;
- DDS and PTX pass valid and malformed-input regressions;
- the Android APK contains the four expected module IDs and none of the retired module IDs;
- Android v1 manifest explicit DMC MIME exposure is limited to MOD, SCM, DDS and PTX;
- public Android debug identity is isolated from production;
- production signing material remains outside Git history and public artifacts;
- iOS/Windows preview work reuses the C++20 semantic core instead of introducing independent platform parsers;
- iOS/Windows remain labelled **Preview** until their own compile + corpus/device acceptance gates pass.
