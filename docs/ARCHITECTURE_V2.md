# Native Reader Architecture v2 — stable core contract

**Status:** stable v1 baseline on `main`  
**Core product surface:** MOD / SCM / DDS / PTX

## Dependency direction

```text
bounded resource probe
        ↓
NativeModuleRegistry
        ↓
format module / canonical adapter
        ↓
InspectionDocument | RenderScene | ImagePreview | ChildResource[]
        ↓
generic JNI Session
        ↓
capability-driven Android UI
```

No Java/UI component parses format bytes. No renderer owns a format parser. No wildcard module or broad recognition-only catalog is present in the v1 core.

## Core modules

`NativeModuleRegistry` contains exactly four promoted modules:

- **MOD** — canonical `dmc-rengine-cpp` parser -> Architecture v2 adapter -> typed inspection, geometry, hierarchy/spatial state, skin weights and texture-slot state where supported;
- **SCM** — canonical `dmc-rengine-cpp` parser -> Architecture v2 adapter -> typed inspection, scene hierarchy, transforms, geometry and texture-slot state;
- **DDS** — bounded DMC3 DXT1/DXT5 texture module -> generic `ImagePreview`;
- **PTX** — bounded texture-bundle module -> generic `ChildResource[]`, with each DDS child using the same generic preview/session UI path.

DDS/PTX remain direct Architecture v2 modules in the current Native Reader snapshot. Future parser-authority synchronization with `dmc-rengine-cpp` must remove duplication rather than reintroduce a compatibility bridge.

## Contracts

### Resource capabilities

The module declares capabilities; Android only consumes them. Current core capabilities include Inspection, Geometry, Wireframe, NodeHierarchy, SkeletalSkinning, SkinWeights, TextureBinding, ImagePreview and ChildResources as appropriate.

### InspectionDocument

Generic evidence-aware presentation tree. Format-specific typed data is projected into it once. It is presentation IR, not a second parser.

### RenderScene

The only downstream geometry authority. MOD and SCM publish geometry, nodes and optional skin/texture bindings through `RenderScene`. There is no `DecodeResult -> Mesh -> RenderScene` compatibility bridge in `main`.

### ImagePreview

Generic RGBA preview contract used by DDS and PTX DDS children. Android does not contain a DDS-specific binary parser/viewer path.

### ChildResource

Generic nested-resource projection. PTX publishes DDS children; opening a child creates the same generic JNI Session used for a top-level resource. Parent navigation is session-based, not PTX-specific Java parsing.

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

Historical branches may preserve experiments for research reference, but they are not production authority. Promotion to `main` requires an Architecture v2 module with its own canonical/evidence source and regression coverage.

## Non-negotiable gates

- exactly four registered core modules until an explicit promotion is accepted;
- unknown and unpromoted families fail closed;
- retired source paths may not re-enter the core build tree accidentally;
- MOD and SCM pass full pipeline projection tests, not only registry checks;
- DDS and PTX pass valid and malformed-input regressions;
- the APK contains the four expected module IDs and none of the retired module IDs;
- Android manifest explicit DMC MIME exposure is limited to MOD, SCM, DDS and PTX;
- public debug identity is isolated from production;
- production signing material remains outside Git history and public artifacts.
