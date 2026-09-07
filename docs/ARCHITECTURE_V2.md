# Native Reader Architecture v2 — clean core contract

**Status:** active baseline on `main`  
**Core product surface:** MOD / SCM / DDS / PTX  
**Archived pre-cleanup state:** `main.2` — до опрацювання

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

No Java/UI component parses format bytes. No renderer owns a format parser. No wildcard module or recognition-only catalog is present in the v1 core.

## Core modules

`NativeModuleRegistry` contains exactly four promoted modules:

- **MOD** — canonical `dmc-rengine-cpp` parser -> Architecture v2 adapter -> typed inspection, geometry, hierarchy, skin weights and texture-slot state;
- **SCM** — canonical `dmc-rengine-cpp` parser -> Architecture v2 adapter -> typed inspection, scene hierarchy, transforms, geometry and texture-slot state;
- **DDS** — bounded DMC3 DXT1/DXT5 texture module -> generic `ImagePreview`;
- **PTX** — bounded texture-bundle module -> generic `ChildResource[]`, each DDS child using the same generic preview/session UI path.

DDS/PTX remain direct Architecture v2 modules in the current Native Reader snapshot. Their future parser-authority synchronization with newer `dmc-rengine-cpp` revisions is a separate controlled migration; it must not reintroduce a legacy compatibility bridge.

## Contracts

### ResourceCapabilities

The module declares capabilities; Android only consumes them. Current core capabilities include Inspection, Geometry, Wireframe, NodeHierarchy, SkeletalSkinning, SkinWeights, TextureBinding, ImagePreview and ChildResources as appropriate.

### InspectionDocument

Generic evidence-aware tree. Format-specific typed data is projected into it once. It is presentation IR, not a second parser.

### RenderScene

The only downstream geometry authority. MOD and SCM publish geometry, nodes and optional skin/texture bindings through `RenderScene`. There is no `DecodeResult -> Mesh -> RenderScene` compatibility bridge in `main`.

### ImagePreview

Generic RGBA preview contract used by DDS and PTX DDS children. Android does not contain a DDS-specific viewer.

### ChildResource

Generic nested-resource projection. PTX publishes DDS children; opening a child creates the same generic JNI Session used for a top-level resource. Parent navigation is session-based, not PTX-specific Java parsing.

## Removed from main

The following pre-v2 / unpromoted paths are intentionally absent from the core registry and build:

- HITS;
- TXT and `.index`;
- DCA;
- LIG / LIG2;
- PAC / PNST;
- NBZ format module;
- EFM / MRP / SHW partial adapters;
- broad recognition-only catalog;
- `DecodeResult` compatibility API and its HITS/TXT decoders.

Their previous implementation remains recoverable from branch `main.2`. Promotion back to `main` requires a module that conforms to this Architecture v2 contract and has its own regression evidence.

## Non-negotiable gates

- exactly four registered core modules until an explicit promotion is accepted;
- unknown and archived families fail closed;
- no old source files may exist in the core build tree;
- MOD and SCM must pass full pipeline projection tests, not only registry checks;
- DDS and PTX must pass valid and malformed-input regressions;
- the APK must contain the four expected module IDs and none of the archived module IDs;
- Android manifest explicit DMC MIME exposure is limited to MOD, SCM, DDS and PTX;
- release signing remains outside repository history.
