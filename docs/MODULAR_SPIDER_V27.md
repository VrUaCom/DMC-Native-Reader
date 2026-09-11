# Native Reader v27 — Modular + Spider architecture contract

This document is a hard architecture boundary for v27 and later work.

## 1. Product layers

```text
Android / future platform shell
  -> file descriptors, URIs, lifecycle, save/open dialogs, widget presentation
  -> JNI / platform bridge
  -> DMCNativeReader::Core (portable native C++)
       -> bounded probe
       -> NativeModuleRegistry
            -> MOD module -> MOD adapter -> canonical DMC Rengine ReaderCore
            -> SCM module -> SCM adapter -> canonical DMC Rengine ReaderCore
            -> DDS module -> TextureSet / canonical DDS codec
            -> PTX module -> TextureSet / framing / canonical DDS codec
       -> Spider Crusader execution plans
       -> resource_session / composite session orchestration
       -> scene_projection / texture_companion / UV gallery / inspection
       -> Spider Black Widow application-state policy
       -> renderer
```

Platform code must never become a second parser or a second rules engine.

## 2. Native module registry is the only production format entrance

`NativeModuleRegistry` is the production dispatch table. v27 still exposes exactly
four promoted format families:

- `MOD`
- `SCM`
- `DDS`
- `PTX`

The decode pipeline probes a candidate, resolves the registered module, and calls
that module. A recognized file without a registered native module fails closed.
No Android extension switch may bypass the registry to parse a DMC format.

Future `.MOT`, `.TM2`, physics, cloth, or other families must not become supported
merely because the `⋮` picker can select them. Promotion requires a bounded native
module, canonical/evidence-backed authority, tests, and an explicit registry entry.

## 3. Spider Crusader owns execution orchestration

The Native Reader uses the pinned DMC Rengine Spider native executor through the
zero-overhead `dmcresource::spider::crusader` facade.

v27 execution routes:

```text
MOD ----\
         -> Spider Crusader model plan -> format adapter -> typed RenderScene
SCM ----/

DDS ----\
         -> Spider Crusader texture plan -> TextureSet -> ImagePreview/children
PTX ----/                         \
                                  -> PTX framing dependency when required
```

MOD and SCM deliberately share one Spider-backed model entry point. DDS and PTX
share one Spider-backed texture entry point. Format-specific decoding remains in
separate adapters/modules; Spider orchestrates operations and dependencies rather
than absorbing parser implementation.

Every successful model/texture pipeline publishes `spider.crusader` in its module
trace. `spider_model_execution_test` protects the model route; the existing
texture regressions protect the DDS/PTX route.

## 4. Spider Black Widow owns application capability policy

Black Widow is the typed native application-state contract. Android consumes its
bitmask and must not reconstruct DMC semantic capability rules from diagnostics,
file extensions, titles, or retained URI lists.

Examples controlled through Black Widow include:

- render availability;
- wireframe;
- spatial hierarchy;
- UV inspection/gallery availability;
- PTX companion attachability and attached state;
- child browser mode;
- PNG export availability;
- focused inspection availability;
- `CanAddModelPart` for adding another MOD model part;
- `CanStageCompanion` for staging future motion/texture/physics/cloth companions.

`CanAddModelPart` and `CanStageCompanion` are emitted only for a promoted,
renderable MOD-model session. At the current production boundary MOD is the model
module that carries `SkeletalSkinning`; SCM intentionally does not. Android may
combine these native flags with platform navigation state (for example, being on
the root scene), but must not recreate the format decision itself.

A diagnostic string is never authority for enabling an action.

## 5. Multi-MOD composition remains native

Android can select several URIs and open file descriptors. It does not merge
models itself. `compose_mod_sessions` validates canonical MOD sessions and retains
each source as a `CompositePart` with its own scene, nodes, mesh, texture namespace,
name and PTX state.

Only the top-level render projection is flattened. Texture slots are remapped to
non-overlapping ranges. Source coordinates are preserved. Cross-file weapon,
cape, cloth or bone attachment is not inferred.

Android retains source URIs only so explicitly user-selected resources can be
reopened if a composite scene is rebuilt. The URI list is storage/lifecycle state,
not evidence that a file is MOD.

## 6. Companion / animation foundation boundary

The top-right `⋮` menu and horizontal motion strip are intentionally only a shell
for future promoted modules.

Android may own:

- `Uri` references returned by the Android Storage Access Framework;
- picker request/result lifecycle;
- temporary visual selection of a staged motion card;
- labels derived from the selected file name for presentation.

Android must not own:

- MOT/TM2/physics/cloth binary layouts;
- animation decoding;
- skeleton compatibility or retargeting rules;
- root-motion rules;
- weapon/cape attachment semantics;
- physics or cloth simulation;
- texture framing/decoding rules;
- companion-action capability policy.

Staging actions are visible only when native Black Widow exposes
`CanStageCompanion`. Staging a URI is not parsing and does not imply support.

When animation is promoted, the intended route is:

```text
selected motion FD
  -> native probe
  -> registered animation NativeModule
  -> Spider Crusader animation plan
  -> typed AnimationClip / binding evidence
  -> Black Widow playback capability
  -> renderer/runtime consumer
```

The same pattern applies to future physics and cloth modules.

## 7. JNI rule

`app_native.cpp` is transport and ownership glue only. It may map a file descriptor,
translate JNI primitive/string/array values, and call portable core APIs. It must
not implement DMC parsing algorithms or duplicate module policy.

JNI must also avoid avoidable full-frame copies. Image transfer should use bounded
chunks or another measured low-copy path instead of allocating a second full-size
native ARGB buffer beside the native RGBA frame and Java destination array.

## 8. C++ standard and performance policy

The language standard is a tool, not a product requirement. The current v27 build
baseline may remain C++20 while optimization work is in flight, but newer standards
are explicitly allowed when they produce a measurable or correctness benefit.

Policy:

- C++23 is approved for production modules when the pinned Android/desktop toolchains
  compile the required feature and the change preserves all supported targets;
- C++26 features are opt-in only behind compiler/library feature tests until the
  required implementations are mature across our target toolchains;
- no standard bump is accepted only because the standard is newer;
- parser/read-path changes must prioritize evidence-correct decoding, bounded reads,
  zero-copy or view-based access where ownership allows it, fewer allocations,
  fewer full-buffer copies, lazy materialization, and deterministic fail-closed
  behavior on malformed offsets/sizes;
- performance claims require before/after measurements or a directly provable
  memory reduction; code size and APK/native library size remain tracked constraints.

Preferred parser/runtime techniques include `std::span`/views, memory mapping where
safe, bounded endian readers, explicit ownership, pre-sized/reserved containers,
lazy decode, and avoiding duplicate parse/materialization passes.

Correctness outranks micro-optimization: no optimization may weaken structural
validation, evidence boundaries, reversible format understanding, or exact resource
identity.

## 9. Build rule

`DMCNativeReader::Core` is the reusable static native product target. Android links
that target instead of enumerating parser sources itself. Future Windows, iOS/macOS
or Web/WASM shells must link the same core so format behavior remains identical.

## 10. Required regression gates

Before v27 promotion, at minimum keep these passing:

- `module_registry_test`
- `spider_model_execution_test`
- `core_model_pipeline_test`
- `dds_ptx_v1_test`
- `black_widow_state_test`
- `composite_mod_scene_test`
- `png_export_session_test`
- `ptx_model_texture_test`
- `uv_gallery_test`
- `session_inspection_test`

`black_widow_state_test` must cover the companion-action flags as well as PNG and
existing model/image/container policy. `spider_model_execution_test` also protects
the typed MOD-vs-SCM capability distinction used by Black Widow, so Android never
needs filename policy.

Physical-device acceptance remains required for Android picker/export/menu/motion
strip behavior.

## 11. Build evidence boundary

A GitHub Actions job that never receives a runner is not build evidence. In
particular, a run with `runner_id: 0`, an empty runner name and `steps: []` has not
executed checkout, CMake, Gradle or tests and must not be reported as either a code
regression or a successful build.

No APK built from an older accepted revision may be relabeled as v27. A v27 APK is
accepted only after a real build of the current v27 head executes the native
regressions, produces the arm64 APK, passes package/signing/JNI/module-marker
verification, and is then physically exercised on the Samsung device.

## 12. Non-negotiable rule for later work

A new button is not a new format implementation. A recognized extension is not a
new format implementation. A staged URI is not a new format implementation.

A DMC family becomes supported only after canonical native parsing, typed output,
module registration, Spider orchestration where applicable, Black Widow capability
projection, regression evidence, and platform/device acceptance appropriate to the
feature.
