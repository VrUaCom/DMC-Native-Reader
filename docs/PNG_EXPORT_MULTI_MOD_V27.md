# Native Reader 1.0 v27 — PNG export + multi-MOD scenes

Base main: `0148f0bd1b384fa1d2b43124b88423fd7b66c379` (accepted v26 merge).
Work branch: `feature/png-export-multi-mod-v27`.
Draft PR: #33.
Android identity: versionName `1.0`, versionCode `27`, arm64-v8a.

## PNG export behavior

The existing shared `🔄` tool is now capability-driven by Spider Black Widow.
It remains `🔄` in ordinary 3D model rendering. It changes to `↓` only when the
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

## Multi-MOD composition

The open picker supports multi-select. Two or more selected resources are composed
only when every source is a content-confirmed, renderable canonical MOD. Mixed or
invalid selections fail closed.

Each input is retained as a native `CompositePart` with its own:

- source name;
- source-local `RenderScene` and node namespace;
- source-local render mesh;
- source-local triangle texture-slot projection;
- PTX attachment state.

The top-level session creates one flattened render projection for the shared
camera. Mesh/node references are offset safely and part names are prefixed in the
flattened view. Texture slots are remapped into non-overlapping global ranges;
the retained `CompositePart` data is not rewritten.

Placement uses each MOD's canonical source coordinates. The reader does **not**
infer weapon-to-bone, cape-to-skeleton, or other cross-file attachments. That
boundary is deliberate: animation/physics attachment rules are deferred until
canonical authority exists.

### PTX in composite scenes

Automatic PTX-to-part matching is forbidden. Pressing `.PTX` in a composite scene
first asks which MOD part owns the companion. Native attachment then validates the
selected part against its own local UV and texture-slot mapping before publishing
its textures into that part's remapped global slot range.

Partial texturing is allowed for rendering. The `.PTX` control remains available
when at least one retained part has a complete local binding even if the merged
scene is not globally complete. The attached-state indicator becomes complete
only when all texture-requiring parts have valid companions.

## Regression coverage added

- `composite_mod_scene_test`: source-local ownership, source-coordinate placement,
  unique texture namespaces, Black Widow state, partial composite PTX routing,
  explicit-part requirement, and non-MOD rejection.
- `png_export_session_test`: Black Widow export state and lazy canonical DDS child
  materialization from retained encoded bytes.
- `black_widow_state_test`: `CanExportPng` projection for gallery/image sessions.

These tests are registered in the portable CMake suite. GitHub-hosted PR jobs are
currently affected by the repository's known runner/pre-step failure: the job can
terminate with `steps: []` before checkout. Such a run is not counted as a code
regression pass or failure.

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
6. Attach each PTX by explicitly choosing its MOD part. Verify one part's PTX does
   not recolor another part and partial attachment remains usable.
7. Confirm no inferred weapon/bone placement is introduced.

Keep PR #33 draft until this physical-device pass is confirmed.
