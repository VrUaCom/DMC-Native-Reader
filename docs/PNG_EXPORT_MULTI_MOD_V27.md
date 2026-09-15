# Native Reader 1.0 v27 — PNG export + multi-MOD scenes

> **HISTORICAL v27 SNAPSHOT — NOT CURRENT v33 AUTHORITY.** This document preserves the v27 candidate behavior and device plan, including the then-deliberate source-coordinate-only multi-MOD policy. v33 later introduced a canonical primary-host/default-joint placement path, C++23, EventTbl, WorkspaceGraph identity and additional review gates. For current behavior read `docs/PROJECT_AI_CONTEXT.md`, `docs/MODULAR_SPIDER_V33.md`, `docs/STATUS.md` and the active Project issues under #34.

Base main: `0148f0bd1b384fa1d2b43124b88423fd7b66c379` (accepted v26 merge).
Work branch: `feature/png-export-multi-mod-v27`.
Draft PR: #33.
Android identity: versionName `1.0`, versionCode `27`, arm64-v8a.

## PNG export behavior

The existing shared `🔄` tool is capability-driven by Spider Black Widow. It
remains `🔄` in ordinary 3D model rendering and changes to `↓` only when the
current native session exposes `CanExportPng`.

| Current surface | `↓` action |
| --- | --- |
| UV gallery | choose a system folder; export every UV slot as a separate PNG |
| opened UV slot | system create-document dialog; save this UV map as PNG |
| PTX gallery | choose a system folder; export every DDS texture as a separate PNG |
| opened PTX/DDS texture | system create-document dialog; save this image as PNG |
| ordinary 3D MOD/SCM view | no PNG action; `🔄` remains reset |

UV PNGs reuse the existing native UV renderer at 1024×1024. PTX/DDS exports use
the canonical decoded base mip. Java does not add a second texture or UV codec.
Generated names combine the root source name with the native child title, so UV
slot and PTX/DDS slot numbers remain visible in exported filenames.

All writes go through Android Storage Access Framework (`ACTION_CREATE_DOCUMENT`
or `ACTION_OPEN_DOCUMENT_TREE`). No broad storage permission or hard-coded path
was added.

### Large PTX bundles

PTX gallery preview memory remains bounded. The existing 4M-pixel resident RGBA
budget is not removed. If a validated DDS child falls outside that resident
preview budget, the parent session retains only that child's encoded DDS payload.
Opening/exporting it lazily routes those bytes back through the canonical DDS
pipeline. This allows `↓ all` without keeping every large decoded texture in RAM.

### Direct Bitmap JNI transport

The v27 Android image ABI no longer returns Java `int[]` frames. Android allocates
an `ARGB_8888` `Bitmap`, JNI validates its dimensions/format/stride, locks its pixel
storage with the NDK `AndroidBitmap_*` API, and copies native RGBA rows directly
into that destination. `jnigraphics` is linked only by the Android `dmcviewer` JNI
target; `DMCNativeReader::Core` remains platform-neutral.

This removes the former full-frame native ARGB conversion buffer and the Java
`int[]` transport allocation. `DmcRenderView` also reuses the same writable Bitmap
across rotate/zoom frames while its dimensions remain unchanged. Static DDS/PTX
previews and PNG export use the same direct destination contract, while gallery
export materializes/recycles one child Bitmap at a time.

The JNI path fails closed on a null destination, invalid dimensions, wrong Bitmap
format, insufficient stride, native RGBA size mismatch, or pixel-lock failure.
Every successful pixel lock is paired with `AndroidBitmap_unlockPixels`, including
the defensive null-pixel case. `tools/verify_device_apk.py` additionally gates the
Java/C++ direct-Bitmap ABI and the Android-only `jnigraphics` link boundary.

## Multi-MOD composition

The open picker supports multi-select. Two or more selected resources are composed
only when every source is a content-confirmed, renderable canonical MOD. Mixed or
invalid selections fail closed.

Each input is retained as a native `CompositePart` with its own:

- source name;
- source-local authoritative `RenderScene` and node namespace;
- source-local triangle texture-slot projection;
- texture-slot base/span;
- PTX attachment state.

A second per-part flattened `Mesh` is deliberately **not** retained. PTX validation
runs directly against the authoritative source-local `RenderScene`, so adding body,
cape, gear and weapon parts does not duplicate each part's vertices/indices/UV only
for companion validation.

The top-level session retains the merged hierarchy namespace plus **one** flattened
`render_mesh` projection for the shared camera/render path. Vertex/index data is
concatenated once into that projection, texture slots are remapped into
non-overlapping global ranges, and source-local `RenderScene` data remains owned by
its `CompositePart`. There is no second top-level `RenderScene.meshes` geometry copy.

Placement uses each MOD's canonical source coordinates. The reader does **not**
infer weapon-to-bone, cape-to-skeleton, or other cross-file attachments. That
boundary was deliberate for v27; current v33 placement behavior is documented in
`MODULAR_SPIDER_V33.md` and must not be inferred from this historical section.

### PTX in composite scenes

Automatic PTX-to-part matching is forbidden. Pressing the PTX action in a
composite scene first asks which MOD part owns the companion. Native attachment
then validates the selected part against its own local UV and texture-slot mapping
directly from its retained `RenderScene`, before publishing textures into that
part's remapped global slot range.

Partial texturing is allowed for rendering. PTX attachment remains available when
at least one retained part has a complete local binding even if the merged scene
is not globally complete. The attached-state indicator becomes complete only when
all texture-requiring parts have valid companions.

## Companion / animation foundation

v27 also establishes a UI-level companion layer without pretending that unpromoted
formats are already decoded or simulated.

### Spider ownership

Companion action availability is not inferred in Java. Spider Black Widow exposes
`CanAddModelPart` and `CanStageCompanion` for promoted renderable MOD-model sessions.
The Android shell may additionally require root-scene navigation state, but it does
not infer model semantics from `.mod` filenames, titles or retained URI lists.

A user-selected URI is storage/lifecycle state only. It becomes a semantic model
part only after the native pipeline accepts it and native composition validates it.

### Top-right `⋮` companion menu

The former dedicated PTX header button is replaced by a single top-right `⋮`
entry point. On a Black-Widow-approved root MOD scene it provides context-aware
actions for:

- opening/replacing the current resource;
- adding one or more `.MOD` parts to the current composite scene;
- attaching `.PTX` to a model or to an explicitly selected composite MOD part;
- staging animation / motion resources (for example `.MOT`-family files);
- staging texture companions such as `.TM2`, `.DDS`, or other not-yet-promoted texture assets;
- staging future physics resources;
- staging future cloth / clothing resources;
- staging other companion files without fabricating semantics.

Only already-promoted operations execute native semantics. Unpromoted animation,
physics, cloth and extra texture families are retained as staged companion URIs
and are clearly reported as staged; they are not parsed, played, simulated, or
automatically bound.

### Motion strip

After one or more motion/animation files are staged, a second horizontal row
appears directly above the existing bottom tool bar. It stays hidden when no
motion files exist and while browsing child resources.

Each motion is represented by the same 48 dp square footprint as the main tools.
The card uses two visual levels:

- format/extension at the top, e.g. `MOT`;
- source stem in smaller text below, e.g. `EM000`.

The row scrolls horizontally, so an arbitrary number of staged animations can be
browsed without shrinking the primary toolbar. Tapping a card selects it and
updates the active visual state. **Selection exists now; playback does not.**
Playback, skeleton retargeting, root motion, physics coupling and cloth simulation
remain disabled until the matching native/canonical runtime is promoted.

This distinction is intentional: v27 creates the attachment and selection UX that
future animation/physics work can plug into, while preserving evidence boundaries.

## Modular + Spider execution

The production path at v27 remained:

`probe -> NativeModuleRegistry -> Spider Crusader -> format adapter/TextureSet -> typed session`

The registry at that historical point exposed exactly MOD / SCM / DDS / PTX.
Future/current module promotion is governed by the v33 authority documents rather
than this snapshot.

## Regression coverage added at v27

- `spider_model_execution_test`: exact four-module registry, shared Spider
  model/texture entry points and successful MOD `spider.crusader` trace;
- `composite_mod_scene_test`: source-local ownership, source-coordinate placement,
  unique texture namespaces, one flattened top-level render projection,
  scene-native local PTX validation, Black Widow state, partial composite PTX
  routing, explicit-part requirement, and non-MOD rejection;
- `png_export_session_test`: Black Widow export state and lazy canonical DDS child
  materialization from retained encoded bytes;
- `black_widow_state_test`: PNG export plus `CanAddModelPart` /
  `CanStageCompanion` policy and negative non-model cases;
- `tools/verify_device_apk.py`: exact public JNI export set plus direct-Bitmap
  Java/C++ ABI and Android-only `jnigraphics` boundary checks.

These tests/gates were registered for the v27 candidate; current v33 evidence must
come from current exact-head gates.

## Historical hosted build state

The v27 GitHub-hosted jobs were observed failing before runner assignment. Observed
jobs reported `runner_id: 0`, an empty runner name and `steps: []`; those runs did
not execute checkout, CMake, Gradle or tests and were not treated as either green
build evidence or code-regression failures.

## Historical v27 device acceptance plan

The following plan is preserved for history only. It is **not** the current v33
Samsung acceptance script; current device testing is generated by Phase #40 and
Final Review Gate #45.

1. Open MOD/SCM 3D and confirm reset behavior.
2. Open a UV gallery and verify PNG export.
3. Export an individual UV slot.
4. Open PTX/DDS and verify gallery/individual PNG export.
5. Multi-select body/cape/gear/weapon MODs under the then-current v27 source-coordinate policy.
6. Attach PTX by explicit MOD part.
7. Add extra MOD parts and verify retained PTX state.
8. Stage motion files and verify selection-only UI.
9. Stage unpromoted companions without claiming runtime semantics.
10. Verify child-navigation UI state.
11. Verify MOD-only actions do not leak to other formats.
12. Verify unsupported animation/physics/cloth behavior remains disabled.
13. Exercise rotate/zoom and direct-Bitmap transport.

PR #33 is now governed by the v33 exact-head build/verifier/review/device gates, not
this historical v27 plan.
