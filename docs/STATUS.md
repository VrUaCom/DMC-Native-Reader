# DMC Native Reader — Status

Last updated: 2026-09-11.

## Accepted baseline (`main`)

- Product line: **Native Reader 1.0**
- versionName: `1.0`
- versionCode: `26`
- accepted v26 main: `0148f0bd1b384fa1d2b43124b88423fd7b66c379`
- accepted through: PR #32
- package: `com.dmcrengine.nativereader`
- ABI: `arm64-v8a`
- minSdk / targetSdk: `26 / 36`
- production module registry: **4 modules — MOD, SCM, DDS, PTX**
- canonical reverse/read-side authority: `VrUaCom/dmc-rengine-cpp` / pinned `ReaderCore`
- archived pre-cleanup implementation: `main.2` — backlog/reference only

## Acceptance evidence

The owner confirmed the required physical Samsung/device checks for v26 on
2026-09-10 and explicitly approved PR #32 for promotion to `main`. The accepted
v26 line therefore includes the v24 architecture/size cleanup, v25 UV slot
gallery and v26 focused long-press inspection tools.

Build-side evidence carried by the accepted candidate includes the portable
native regressions and verified arm64 APK/package gates documented in the v25/v26
evidence files. The v26 candidate APK was versionName `1.0`, versionCode `26`,
597,665 bytes, with SHA-256
`b80be422197ff8270f67049dbdd596603b0ebcf4f41884f6b22a5936fedf4596`.

GitHub-hosted Actions remain affected by a runner/pre-step failure where a job can
terminate before checkout with `steps: []`; CI is not claimed green on that basis.
Physical-device acceptance and verified build/regression evidence remain the
promotion authority for the accepted v26 merge.

See `SIZE_AND_MODULES_V24.md`, `UV_GALLERY_V25.md` and
`TOOL_INSPECTION_V26.md` for the bounded evidence slices.

## Current architecture

```text
resource bytes
  -> bounded probe / DMC Rengine ReaderCore
  -> NativeModuleRegistry (MOD | SCM | DDS | PTX)
  -> Spider Crusader execution plan
      -> typed format adapter / TextureSet
  -> InspectionDocument / RenderScene / ImagePreview / ChildResource[]
  -> DMCNativeReader::Core
      -> resource_session
      -> scene_projection
      -> texture/material binding
      -> Spider Black Widow typed application state
      -> direct C++ rendering
  -> thin Android JNI + Java shell
      -> direct native RGBA -> Android Bitmap pixel transport
```

Unknown/unpromoted formats fail closed. Java does not parse DMC binary layouts and
the renderer does not own format parsers.

## Accepted capabilities

### MOD

- canonical structural parsing;
- renderable geometry;
- rotate / zoom / wireframe;
- typed inspection;
- hierarchy/spatial projection when canonical authority is valid;
- skin weights;
- canonical texture-slot and legacy GS state;
- PTX companion attachment for valid model texture bindings;
- per-texture-slot UV gallery;
- focused UV/mesh/hierarchy information views.

### SCM

- canonical structural parsing;
- renderable geometry;
- canonical scene hierarchy and transforms;
- rotate / zoom / wireframe;
- typed inspection;
- texture-slot state;
- PTX companion attachment through the shared texture path;
- per-texture-slot UV gallery;
- focused UV/mesh/hierarchy information views.

### DDS

- bounded DXT1/DXT5 parsing/decoding;
- generic RGBA image preview;
- malformed/overflow rejection.

### PTX

- bounded texture-bundle framing;
- generic DDS child resources;
- thumbnail/gallery presentation;
- child preview and parent-session navigation;
- shared TextureSet path for model companion application.

## Active development candidate

Draft PR #33 on `feature/png-export-multi-mod-v27` carries **v27**
(`versionCode 27`, `versionName 1.0`). It contains four connected layers:

1. **PNG export + direct Bitmap transport**
   - `↓` replaces the shared reset button only on exportable UV/image sessions;
   - UV gallery exports all maps to a selected system folder;
   - opened UV exports one 1024×1024 PNG through the system save dialog;
   - PTX gallery exports every DDS texture as an individual PNG;
   - opened PTX/DDS texture exports one PNG;
   - ordinary 3D MOD/SCM keeps `🔄` reset;
   - large PTX children omitted from resident RGBA gallery memory can be lazily
     materialized from retained encoded DDS bytes through the canonical decoder;
   - Java no longer receives image frames as `int[]`: Android allocates/reuses an
     `ARGB_8888` Bitmap and JNI copies native RGBA rows directly into locked pixels;
   - `jnigraphics` is linked only to the Android JNI target, never to portable Core;
   - APK verification gates the Java/C++ direct-Bitmap ABI and rejects legacy
     `int[]` image declarations.

2. **Low-copy multi-MOD scene composition**
   - multi-select canonical MOD files into one render session;
   - retain every source as a separate `CompositePart` with an authoritative
     source-local `RenderScene`, node namespace, local texture-slot projection,
     texture-slot base/span and PTX state;
   - do not retain a second per-part flattened `Mesh`;
   - top level retains merged hierarchy nodes plus one flattened `render_mesh`;
   - do not duplicate composite geometry in top-level `RenderScene.meshes`;
   - remap only the flattened render projection into non-overlapping texture-slot
     ranges;
   - PTX attachment requires explicit MOD-part selection and validates directly
     against that part's local `RenderScene`;
   - adding more MOD parts rebuilds the composite while preserving/restoring known
     per-part PTX URI attachments in the Android shell;
   - no inferred cape/weapon/bone attachment.

3. **Companion / animation UI foundation**
   - a top-right `⋮` menu replaces the single-purpose PTX header button;
   - the menu can add MOD parts, attach PTX, and stage motion, texture, physics,
     cloth or other companion resources;
   - staged motion files create a second horizontal 48 dp card row directly above
     the main bottom toolbar;
   - each card presents format/extension on top and a compact source stem below
     (for example `MOT` + `EM000`);
   - the strip scrolls left/right and tracks selected motion state;
   - the strip is root-scene UI and is hidden while browsing UV/PTX child sessions;
   - animation playback, retargeting, root motion, physics and cloth simulation
     remain disabled until canonical native runtimes are promoted.

4. **Modular + Spider hardening**
   - `NativeModuleRegistry` remains the only production format entrance and still
     exposes exactly MOD / SCM / DDS / PTX;
   - MOD and SCM share a Spider Crusader model execution plan while keeping their
     canonical adapters separate;
   - DDS/PTX share the Spider Crusader texture execution plan;
   - successful promoted routes publish `spider.crusader` in the pipeline trace;
   - Black Widow owns render/UV/PTX/export policy plus `CanAddModelPart` and
     `CanStageCompanion`;
   - the registry's typed capabilities distinguish promoted MOD model sessions
     (`SkeletalSkinning`) from SCM without Java checking filenames;
   - Android no longer infers model-part actions from `.mod` filenames or from its
     retained URI list; those lists are storage/lifecycle bookkeeping only;
   - future MOT/TM2/physics/cloth support must be promoted as native modules with
     Spider execution and typed runtime contracts before they gain semantics.

The distinction is deliberate: **animation/physics execution is deferred, but the
attachment and selection foundation is already part of v27.** Unpromoted companion
formats are staged without fabricated parsing or runtime semantics.

Portable regression targets include `module_registry_test`,
`spider_model_execution_test`, `core_model_pipeline_test`, `dds_ptx_v1_test`,
`black_widow_state_test`, `composite_mod_scene_test`, `png_export_session_test`,
`ptx_model_texture_test`, `render_scene_test`, `uv_gallery_test`,
`session_inspection_test` and `mod_spatial_adapter_test`. The APK verifier also
checks the direct-Bitmap source/ABI boundary in addition to package/signature/JNI
export/module-marker gates.

## v27 build state

No v27 APK has been accepted or published yet. The GitHub-hosted jobs observed on
this development line have failed before runner assignment: jobs report no executed
steps, so checkout, CMake, Gradle and tests did not run. These failures are not
code-regression evidence and are not green evidence either.

PR #33 stays draft until a real build of the exact current head executes the native
suite, produces and verifies the arm64 APK, and the resulting APK passes the Samsung
device checklist including rotate/zoom direct-Bitmap behavior.

See `PNG_EXPORT_MULTI_MOD_V27.md` and `MODULAR_SPIDER_V27.md`.

## Not in production registry

HITS, TXT, `.index`, DCA, LIG/LIG2, PAC/PNST, NBZ, EFM/MRP/SHW and the previous
wide recognition catalog are absent from the current `main` registry/build. Their
existence in historical branches or reverse documentation does not make them
supported Native Reader modules.

Future promotion requires a bounded Architecture v2 module,
canonical/evidence-backed authority and regression/device evidence appropriate to
the feature.
