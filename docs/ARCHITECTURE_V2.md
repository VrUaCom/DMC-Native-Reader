# Native Reader Architecture v2 — reusable resource presentation core

**Status:** implementation in progress  
**Branch:** `architecture/v2-resource-session-core`  
**Baseline preserved:** v1 / `1.0.1-debug-ui1`

## Goal

Keep format parsing evidence-aware and format-specific while making inspection,
rendering, hierarchy overlays, texture bindings and future tools reusable across
DMC3 resource families.

The dependency direction is one-way:

```text
binary primitives
      ↓
format parsers
      ↓
semantic / cross-resource analysis
      ↓
inspection + render adapters
      ↓
resource session / JNI
      ↓
Android UI
```

No UI component may parse MOD, SCM, PTX, DDS, SO, SHW or another resource directly.
No renderer may become a second format parser.

## v2 contracts

### ResourceCapabilities

Capabilities belong to `NativeModule`, not to Android conditionals. UI visibility
must be driven by capabilities such as:

- Inspection
- Geometry
- Wireframe
- NodeHierarchy
- SkeletalSkinning
- SkinWeights
- TextureBinding
- ImagePreview
- ChildResources
- Text
- Container
- Collision
- Adjacency
- TransformSelectors

A capability is published only when the module has evidence-backed data that can
support it. Recognition alone does not imply a semantic capability.

### InspectionDocument

Generic tree projection for the info/inspector UI. It carries:

- stable node id;
- title and node kind;
- optional source byte span;
- properties with an evidence level;
- children.

Format parsers retain authority over typed/raw data. `InspectionDocument` is a tool
projection and must not erase unresolved bytes or upgrade evidence.

### RenderScene

Reusable presentation IR containing:

- independent mesh primitives;
- scene/bone/selector nodes;
- optional skin bindings;
- optional texture-slot bindings.

MOD, SCM, HITS, SHW and future geometry families should adapt into this IR instead
of teaching the renderer their binary layout.

## Migration rule

v1 `Mesh` remains temporarily as a compatibility projection. During migration:

1. existing module decodes once;
2. pipeline projects the v1 mesh into `RenderScene` once;
3. format-specific v2 adapters progressively replace that flattened projection;
4. only after all consumers use `RenderScene` may the v1 mesh field be removed.

This avoids a big-bang rewrite and keeps the device-tested v1 rendering path usable.

## Model-family capability boundary

Current v2 contracts intentionally distinguish MOD from SCM:

```text
SCM
  Inspection
  Geometry
  Wireframe
  NodeHierarchy
  TextureBinding

MOD
  Inspection
  Geometry
  Wireframe
  NodeHierarchy
  SkeletalSkinning
  SkinWeights
  TextureBinding
```

SCM scene nodes must not be mislabeled as skeletal bones. A common hierarchy UI may
render both, but labels and capabilities remain semantically accurate.

## Canonical core reuse

`VrUaCom/dmc-rengine-cpp` remains the canonical reverse/evidence source. Native
Reader must move toward a pinned C++20 core snapshot/target rather than independently
reimplementing proven parsers.

Architecture v2 therefore raises the Android native target from C++17 to C++20.
This is a prerequisite for direct reuse of canonical reader modules based on
`std::span` and other C++20 APIs.

## Next migration slices

1. Shared binary/diagnostic helpers; remove duplicated reject/magic/bounds helpers.
2. Extract DDS parser from the PTX module so PTX consumes the same reusable DDS API.
3. Replace `decode_part*.inc` MOD/SCM implementation with pinned canonical Model
   Family C++20 modules.
4. Add typed MOD and SCM `InspectionDocument` adapters.
5. Add typed hierarchy and skin projections into `RenderScene`.
6. Expose capabilities/inspection through JNI without format switches in Java.
7. Build generic hierarchy/skeleton overlay renderer.
8. Reuse the same blocks for SHW, EFM, SO and later resource families.

## Non-negotiable rules

- one parsing pass per resource;
- no format-specific parsing in Java/UI;
- no format-specific binary parsing in renderer code;
- no duplicated DDS/model/hierarchy parser just for inspection;
- unknown/evidence-gated semantics remain explicit;
- module capabilities must have regression coverage;
- v1 behavior remains the fallback baseline until v2 device tests pass.
