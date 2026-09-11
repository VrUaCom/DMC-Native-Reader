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

GitHub-hosted Actions remain affected by the known runner/pre-step failure where
a job can terminate before checkout with `steps: []`; CI is not claimed green on
that basis. Physical-device acceptance and the verified build/regression evidence
remain the promotion authority for the accepted v26 merge.

See `SIZE_AND_MODULES_V24.md`, `UV_GALLERY_V25.md` and
`TOOL_INSPECTION_V26.md` for the bounded evidence slices.

## Current architecture

```text
resource bytes
  -> bounded probe / DMC Rengine ReaderCore
  -> NativeModuleRegistry (MOD | SCM | DDS | PTX)
  -> Spider Crusader execution plan
      -> model route: MOD | SCM -> separate canonical adapters
      -> texture route: DDS | PTX -> TextureSet / framing / DDS codec
  -> typed module projection
  -> InspectionDocument / RenderScene / ImagePreview / ChildResource[]
  -> DMCNativeReader::Core
      -> resource_session
      -> scene_projection
      -> texture/material binding
      -> Spider Black Widow typed application state
      -> direct C++ rendering
  -> thin Android JNI + Java shell
```

Unknown/unpromoted formats fail closed. Java does not parse DMC binary layouts and
the renderer does not own format parsers. MOD and SCM now share one Spider Crusader
model execution entry point while retaining separate format adapters. DDS and PTX
continue to share the Spider-backed texture execution route. Successful model and
texture pipelines publish `spider.crusader` in their module trace.

`DMCNativeReader::Core` remains the portable C++20 product target. Android owns
file descriptors, URIs, Storage Access Framework dialogs, lifecycle and widget
presentation only. DMC parsing, composition, texture binding and runtime semantic
rules remain native. See `MODULAR_SPIDER_V27.md` for the hard architecture contract.

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
(`versionCode 27`, `versionName 1.0`). It now contains three bounded layers:

1. **PNG export**
   - `↓` replaces the shared reset button only on exportable UV/image sessions;
   - UV gallery exports all maps to a selected system folder;
   - opened UV exports one 1024×1024 PNG through the system save dialog;
   - PTX gallery exports every DDS texture as an individual PNG;
   - opened PTX/DDS texture exports one PNG;
   - ordinary 3D MOD/SCM keeps `🔄` reset;
   - large PTX children omitted from resident RGBA gallery memory can be lazily
     materialized from retained encoded DDS bytes through the canonical decoder.

2. **Multi-MOD scene composition**
   - multi-select canonical MOD files into one render session;
   - retain every source as a separate `CompositePart` with its own scene,
     node namespace, mesh, texture-slot projection and PTX state;
   - remap only the top-level render projection into non-overlapping texture-slot
     ranges;
   - PTX attachment requires explicit MOD-part selection;
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

The distinction is deliberate: **animation/physics execution is deferred, but the
attachment and selection foundation is already part of v27.** Unpromoted companion
formats are staged without fabricated parsing or runtime semantics. Future MOT,
TM2, physics and cloth promotion must enter through a native module and Spider
execution plan rather than adding parsing logic to Android.

Portable regression targets added for the slice now include
`composite_mod_scene_test`, `png_export_session_test` and
`spider_model_execution_test`, with Black Widow export coverage extended as well.
The Spider regression verifies that the registry still contains exactly the four
promoted families, MOD/SCM share the Spider-backed model entry point, DDS/PTX share
the Spider-backed texture entry point, and a canonical MOD pipeline publishes a
successful `spider.crusader` trace marker.

Hosted PR jobs currently hit the same repository pre-step infrastructure failure,
so a real build/test pass is still required.

See `PNG_EXPORT_MULTI_MOD_V27.md` and `MODULAR_SPIDER_V27.md`. Keep PR #33 draft
until the physical-device acceptance checklist is completed.

## Not in production registry

HITS, TXT, `.index`, DCA, LIG/LIG2, PAC/PNST, NBZ, EFM/MRP/SHW and the previous
wide recognition catalog are absent from the current `main` registry/build. Their
existence in historical branches or reverse documentation does not make them
supported Native Reader modules.

Future promotion requires a bounded Architecture v2 module,
canonical/evidence-backed authority and regression/device evidence appropriate to
the feature.
