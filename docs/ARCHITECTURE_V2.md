# Native Reader Architecture v2 — stable v1 core contract

**Status:** stable v1.0.0 baseline  
**Stable v1 product shell:** Android  
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
InspectionDocument | RenderScene | ImagePreview | ChildResource[] | ResourceCapabilities
        ↓
generic native Session
        ↓
platform presentation shell
```

For the stable v1.0.0 product, the platform shell is Android and the Session bridge is JNI. The architecture itself is not Android-specific: future iOS, Windows and Web shells must reuse the same C++20 semantic contracts rather than reimplementing DMC binary semantics independently.

For Web, the intended path is C++20 compiled to WebAssembly with a thin browser binding/presentation layer.

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

Generic nested-resource projection. PTX publishes DDS children; opening a child creates the same generic native Session used for a top-level resource. Parent navigation is session-based, not PTX-specific platform parsing.

## Stable v1 Android bridge

The Android implementation uses:

```text
generic native Session
        ↓
JNI bridge
        ↓
ResourceUiState / capability-driven Android UI
```

This is the accepted v1 platform implementation, not a rule that future shells must use JNI.

## Future shells

The semantic direction is:

```text
                 DMC Rengine / Native Reader C++20 core
                              |
            +-----------------+-----------------+
            |                 |                 |
         Android             iOS             Windows
            |
            `---------------- WebAssembly -> Web UI
```

Platform-specific code may own file pickers, gestures, windows, OS thumbnails, DOM/Canvas/WebGL/WebGPU integration and other presentation concerns. It may not fork binary semantics that belong to the C++20 core.

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
- the APK contains the four expected module IDs and none of the retired module IDs;
- Android v1 manifest explicit DMC MIME exposure is limited to MOD, SCM, DDS and PTX;
- public debug identity is isolated from production;
- production signing material remains outside Git history and public artifacts;
- cross-platform work reuses the C++20 semantic core instead of introducing independent platform parsers.
