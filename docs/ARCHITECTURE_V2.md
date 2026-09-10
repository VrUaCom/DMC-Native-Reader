# Native Reader Architecture v2

**Accepted baseline:** `main` / Native Reader 1.0 v24  
**Production registry:** MOD / SCM / DDS / PTX  
**Canonical read-side dependency:** pinned DMC Rengine `ReaderCore`  
**Archived pre-cleanup state:** `main.2` — backlog/reference only

## Core rule

DMC Native Reader is a portable C++20 read/inspection/render core with thin platform shells. Platform code may open files, forward user input and display already-resolved state, but it must not own DMC binary parsing or reverse-engineered layout rules.

```text
resource bytes / child resource
        |
        v
bounded probe + DMC Rengine ReaderCore
        |
        v
NativeModuleRegistry
        |
        +--> MOD adapter
        +--> SCM adapter
        +--> DDS/PTX texture module
        |
        v
InspectionDocument | RenderScene | ImagePreview | ChildResource[]
        |
        v
DMCNativeReader::Core
        |
        +--> resource_session
        +--> scene_projection
        +--> model_texture_binding
        +--> texture_companion / TextureSet
        +--> Black Widow typed state
        `--> view_renderer
        |
        v
Android JNI + Java shell
```

## Build boundary

`app/src/main/cpp/CMakeLists.txt` defines `dmc_native_reader_core` / `DMCNativeReader::Core` as the reusable platform-neutral target. Android builds a small `dmcviewer` JNI shared library that links the core instead of enumerating parser/renderer sources itself.

The accepted v24 refactor moved portable session ownership into `resource_session`, scene materialization into `scene_projection`, and common vector/limit helpers into neutral modules. The Android DSO exports only the declared JNI entry points; v24 verifies 18 public JNI exports rather than exposing the C++ implementation surface.

## Production modules

`NativeModuleRegistry` contains exactly four promoted families.

### MOD

Canonical `dmc-rengine-cpp` structural reader -> Architecture v2 adapter -> typed inspection, geometry, hierarchy evidence, skin weights and texture-slot/legacy GS state.

Spatial hierarchy is published only when the canonical MOD authority says it is valid. Structural hierarchy must not be converted into invented spatial positions.

### SCM

Canonical `dmc-rengine-cpp` structural reader -> Architecture v2 adapter -> typed inspection, scene hierarchy, world/scene transforms, geometry and texture-slot state.

### DDS / PTX

DDS and PTX use the shared native texture path and DMC Rengine read-side codec/framing authority. DDS projects a generic `ImagePreview`. PTX projects generic DDS `ChildResource` entries and may become a `TextureSet` for model companion attachment.

Android does not contain a DDS-specific parser/viewer or PTX binary parser.

## Presentation contracts

### `ResourceCapabilities`

Capabilities determine which UI actions exist. Android consumes capability/state results; it does not infer them from file extensions or diagnostic strings.

### `InspectionDocument`

Typed, evidence-aware presentation tree. Format-specific facts are projected once from canonical/native authority. It is not a second parser.

### `RenderScene`

Sole downstream geometry/hierarchy/material projection authority for model rendering. Renderers consume prepared scene data; they do not parse MOD/SCM bytes.

### `ImagePreview`

Generic RGBA image contract used by standalone DDS and PTX DDS children.

### `ChildResource`

Generic nested-resource contract. PTX children use ordinary sessions and parent-session navigation; this same mechanism is reusable for future nested resource families.

### `Session`

`resource_session` owns decoded presentation state, caches and companion state independently from JNI. Child sessions preserve lifetime safely after parent-session closure.

## Texture/model boundary

One model-texture-binding authority validates required canonical texture slots. PTX/DDS decoding and TextureSet creation remain outside the renderer. The renderer only receives prepared per-triangle slot mapping and decoded texture images.

A failed replacement companion must not destroy previously valid attached textures.

## Spider boundary

Spider names describe orchestration roles, not parsers:

- **Black Widow** — platform-neutral application/UI decision state;
- **Crusader** — reusable dependency/orchestration plans where repeated native flow justifies them;
- **Tarantula** — future higher-level C++20 workflow/scripting layer.

Hot numerical work (rasterization, interpolation, UV/math, texture sampling) remains direct C++.

See `SPIDER_FAMILY.md`.

## Accepted v24 responsibility split

| Component | Responsibility |
| --- | --- |
| `DMC Rengine ReaderCore` | canonical reusable read-side format/codec authority |
| `module_registry` | production family routing |
| MOD/SCM adapters | typed canonical -> Native Reader presentation projection |
| `resource_session` | portable session ownership, child/session lifetime, companion state |
| `scene_projection` | render mesh, hierarchy overlay and triangle-slot materialization |
| `model_texture_binding` | single model required-slot/binding validation authority |
| `texture_companion` / `TextureSet` | PTX/DDS companion decode/application state |
| `view_renderer` | direct C++ rasterization, texture sampling and UV drawing primitives |
| Black Widow | typed action/state policy |
| Android JNI/Java | FD/lifecycle/input/widget presentation only |

## Candidate v26 extension

Draft v26 adds `session_gallery`, `uv_gallery` and `session_inspection` to the portable core. UV slot grouping reuses model texture binding; focused object/mesh/hierarchy reports reuse typed inspection rather than reparsing bytes. Parent relationship authority is separated from spatial authority so known hierarchy can remain inspectable when a 3D bone overlay is not justified.

This candidate is architecturally implemented and regression-tested but remains outside accepted `main` until device acceptance closes.

## Forbidden regressions

- Java/Kotlin DMC binary-layout parsers;
- renderer-owned format parsers;
- wildcard family decoding;
- broad recognition catalog presented as supported readers;
- duplicated MOD/SCM/DDS/PTX offset logic in product layers;
- inferred/fabricated hierarchy transforms;
- diagnostic-string parsing as UI business logic;
- Android-only authoring/writer implementations.

New formats must enter through the same bounded module/contracts and evidence gates.
