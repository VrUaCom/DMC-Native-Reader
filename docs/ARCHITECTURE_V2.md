# Native Reader Architecture v2 — reusable resource presentation core

**Status:** implemented and device-accepted for the v1 primary reader surface  
**Release line:** `1.0.0-rc1`

## Goal

Keep format parsing evidence-aware and format-specific while making inspection,
rendering, hierarchy overlays, image previews and nested-resource navigation reusable
across DMC3 resource families.

The dependency direction is one-way:

```text
bytes
  ↓
canonical / native format parser
  ↓
typed document + bounded analysis
  ↓
PipelineResult
  ├── ResourceCapabilities
  ├── InspectionDocument
  ├── RenderScene
  ├── ImagePreview
  └── ChildResource[]
  ↓
JNI Session
  ↓
generic Android presentation
```

No UI component parses MOD, SCM, PTX, DDS, SO, SHW or another resource directly.
No renderer is allowed to become a second format parser.

## Implemented v2 contracts

### ResourceCapabilities

Capabilities belong to the native module/result contract, not to Android filename or
format conditionals. UI availability is derived through `ResourceUiState` from
capabilities such as:

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

Generic tree projection used by the resource Inspector. It carries:

- stable node id;
- title and node kind;
- optional source byte span;
- properties with evidence level;
- children.

Format parsers retain authority over typed/raw data. `InspectionDocument` is a
presentation/tool projection and must not upgrade evidence or invent semantics.

### RenderScene

The sole reusable geometry/hierarchy presentation IR. It contains:

- independent mesh primitives;
- scene/bone/selector nodes;
- local/world matrices where spatial authority exists;
- optional skin bindings;
- optional texture-slot bindings.

MOD and SCM adapt into this IR. The retired flattened `PipelineResult.mesh` and the
old duplicate MOD/SCM decoder path have been physically removed.

### RenderFlags / overlays

Renderer options use a reusable bitmask rather than a growing JNI list of booleans.
The hierarchy overlay is built from `RenderScene.nodes` and can therefore serve
skeletal, scene or future spatial hierarchy resources without a format-specific
renderer.

Spatial authority is explicit. A node at the origin is still valid when the
canonical adapter authorizes its transform; structural-only hierarchy does not gain
3D authority through coordinate heuristics.

### ImagePreview

Static image presentation is a generic IR, not a DDS-specific Android viewer.

Current v1 use:

- DDS validates through the native DDS parser;
- bounded DXT1/DXT5 base-mip decode produces `ImagePreview`;
- Android displays the preview in the shared viewport;
- image allocation and Java Bitmap failure are bounded/fail-closed.

### ChildResource

Nested resources use a reusable projection containing child title/source span plus
ordinary presentation contracts (`ResourceCapabilities`, `InspectionDocument`,
`RenderScene`, `ImagePreview`, nested children).

Current v1 use:

- PTX publishes validated DDS children;
- generic `ChildResourceBrowserView` renders preview-first tiles;
- fallback text is used only when a preview cannot be materialized;
- tapping a child creates a normal native child Session;
- `←` and Android Back close only the child and restore the existing parent Session.

The Android presentation layer does not branch on PTX/DDS for this behavior. The
same contract can later serve PAC/PNST/SO or other nested resources.

## Model-family evidence boundaries

### SCM

Current capabilities include inspection, geometry, wireframe, node hierarchy and
texture binding. Scene nodes remain scene nodes; they are not mislabeled as bones.
Canonical SCM world matrices authorize the generic scene-hierarchy overlay.

### MOD

Current capabilities include inspection, geometry, wireframe, node hierarchy,
skeletal skinning, skin weights and texture binding.

The canonical MOD path provides:

- parent/order domain;
- local transform records;
- model-space world propagation;
- instance-level `supports_spatial_hierarchy()` gate;
- bone/node positions from world-matrix translation, never mesh vertices;
- typed texture slot and confirmed legacy GS CLAMP / REGION_REPEAT state.

The unresolved MOD bitmap/companion mapping remains unresolved.

## Canonical core reuse

`VrUaCom/dmc-rengine-cpp` is the canonical reverse/evidence source for promoted
DMC3 HD format semantics.

When Native Reader needs a newly confirmed field, the required direction is:

```text
dmc-rengine-cpp
  → typed parser promotion
  → tests
  → canonical merge
  → pinned Native Reader snapshot/provenance
  → adapter projection
```

Android adapters must not re-read canonical fields by raw offsets to bypass this
rule.

The Native Reader native target is C++20.

## v1 device acceptance

Architecture v2 has been exercised on a real Samsung device for the primary v1
surface:

- MOD geometry + generic hierarchy overlay + Inspector;
- SCM geometry + generic scene hierarchy overlay + Inspector;
- MOD skin/weights and texture/GS state;
- SCM texture/GS state;
- PTX preview-first DDS child gallery;
- child DDS full image preview;
- generic parent-session navigation;
- standalone DDS image preview.

## Non-negotiable rules

- one parsing pass per opened resource/session;
- no format-specific binary parsing in Java/UI;
- no format-specific binary parsing in renderer code;
- no duplicate DDS/model/hierarchy parser just for presentation;
- `RenderScene` remains the sole geometry authority;
- `InspectionDocument` remains the inspection projection;
- unknown/evidence-gated semantics remain explicit;
- capabilities require regression coverage;
- malformed/untrusted inputs fail closed and stay bounded;
- new format support is parser + adapter + capabilities, not a new Android viewer.

## Post-v1 extension direction

After the v1 release freeze, the same contracts are intended for evidence-ready
SHW, EFM, SO and deeper PAC/PNST/NBZ child-resource flows. Promotion remains
evidence-driven and does not expand RC scope merely to add more family names.
