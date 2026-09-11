# Native Reader 1.0 v27 — PNG export + multi-MOD scenes

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
boundary is deliberate: animation/physics attachment rules remain disabled until
canonical authority exists.

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

The production path remains:

`probe -> NativeModuleRegistry -> Spider Crusader -> format adapter/TextureSet -> typed session`

The registry still exposes exactly MOD / SCM / DDS / PTX. MOD/SCM share the
Spider Crusader model entry point but keep separate canonical adapters. DDS/PTX
share the Spider texture entry point. Successful promoted routes publish
`spider.crusader` in the module trace.

Future MOT/TM2/physics/cloth support must be added as bounded native modules and
Spider execution plans before any staged resource gains runtime semantics. See
`MODULAR_SPIDER_V27.md` for the hard boundary.

## Regression coverage added

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

These tests/gates are registered for the v27 candidate, but they are not claimed
passing until a real runner executes the current head.

## Current hosted build state

The current v27 GitHub-hosted jobs are failing before runner assignment. Observed
jobs report `runner_id: 0`, an empty runner name and `steps: []`; the same behavior
persisted while probing different hosted runner labels. Those runs did not execute
checkout, CMake, Gradle or tests and therefore are not treated as either green
build evidence or code-regression failures. The workflow runner labels were
restored after the probes.

No v27 APK is accepted or published until a real build executes and passes the
gates below.

## Device acceptance required before merge

On the Samsung device:

1. Open MOD/SCM 3D and confirm `🔄` still resets the view.
2. Open a UV gallery; confirm the same tool becomes `↓`; choose a folder and
   verify every slot is written as a separate readable PNG.
3. Open one UV slot; save it through the system filename/location dialog and
   verify the PNG is 1024×1024 and matches the displayed map.
4. Open PTX directly; export the full gallery to a folder, then open one DDS child
   and export it individually. Verify filenames preserve source + slot identity.
5. Multi-select a body MOD plus cape/gear/weapon MODs. Verify all render together
   in source coordinates and remain identifiable as separate parts in Info.
6. Use `⋮` to attach each PTX by explicitly choosing its MOD part. Verify one
   part's PTX does not recolor another part and partial attachment remains usable.
7. Add extra MOD parts from `⋮` and confirm previously attached part PTX resources
   are restored to the rebuilt composite scene.
8. Stage several motion files through `⋮`. Confirm the second horizontal row
   appears above the bottom toolbar, cards show extension + source stem, the row
   scrolls left/right, and selection changes visually without claiming playback.
9. Stage texture/physics/cloth/other companion files. Confirm they are listed in
   Info as staged and do not silently activate unsupported native behavior.
10. Enter UV/PTX child navigation and confirm the motion strip is hidden there,
    then returns on the root scene.
11. Confirm SCM/DDS/PTX views do not expose MOD-only add-model/staging actions
    merely because of filenames or picker state.
12. Confirm no inferred weapon/bone/cape placement, animation playback, physics,
    or cloth simulation is introduced.
13. Rotate/zoom a 3D scene repeatedly and verify the direct-Bitmap path does not
    create the previous per-frame Java `int[]`/Bitmap churn or visible corruption.

Keep PR #33 draft until a real build/regression run produces a verified arm64 APK
and this physical-device pass is confirmed.
