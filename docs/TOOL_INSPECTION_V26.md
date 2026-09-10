# Native Reader 1.0 v26 — tool inspection on long press

Base: `ef165cedfe6986830fda9ccfc6a17cef744e8b88` (UV gallery v25).
Branch: `feature/dds-ptx-v1-acceptance`. No ReaderCore or Rengine changes.

## Interaction

- Hold UV: list canonical texture slots and triangle counts. Available in the
  model, UV gallery and individual UV-map sessions. The selected map is marked.
- Hold W: object count, mesh count and mesh groups nested under their objects.
  The typed inspection document preserves even objects with zero meshes.
- Hold the bones button: list nodes/bones with explicit parent names and indices,
  roots, unconfirmed relationships and invalid references.
- Short taps preserve UV gallery, wireframe and spatial hierarchy actions.
  A button can support information-only long press when its render action is
  unavailable. Consumed long presses do not trigger the short-tap action.

All three use the existing MainActivity information dialog (one scrollable,
selectable text view, one dialog implementation). The full information action
continues to display the existing complete report.

## Ownership and reuse

`session_inspection` creates focused InspectionDocuments; `inspection_format`
formats both focused and full reports. Objects/meshes are projected from
InspectionKind records, not parsed again or reconstructed from render geometry.
UV gallery construction and text information share `summarize_uv_maps`, which
reuses the single model_texture_binding validator. Text-only inspection does
not allocate UV coordinates, mesh positions, triangle-index copies or thumbnails.

MOD and SCM adapters publish an explicit `RenderNode.parent_authority` using
existing canonical hierarchy validity. This is separate from spatial transform
authority: missing spatial transforms must not hide known parent relationships,
and an unknown parent must not become a fabricated root. The information list
uses explicit parent references without recursive hierarchy expansion.

Black Widow publishes CanInspectUv / CanInspectMeshes / CanInspectHierarchy.
Java consumes those flags and transports a typed inspection-topic ID over one
JNI method. It contains no DMC format knowledge, slot calculation or hierarchy
inference. No new Android dependency or duplicate information window was added.

## Verification

All nine CTest regressions passed. New coverage checks empty objects, nested mesh
counts, sparse UV slots shared with gallery grouping, incomplete UV bindings,
selected-map labels, unknown/invalid parent references, non-topological node
storage and information availability without render actions. MOD/SCM integration
and MOD non-spatial-but-known hierarchy authority are covered. Existing texture,
UV isolation and model-render tests remain passing.

Clean Android build succeeded. APK gates passed version 1.0/26, arm64-v8a,
stable signer, ZIP CRC, v2 signature, all 19 declared JNI exports,
module markers, no Kotlin runtime, extractNativeLibs and legacy packaging.

- APK: 597,665 bytes (v25: 590,107; increase: 7,558 bytes / 1.28%).
- Native library: 1,676,448 bytes.
- DEX: 151,936 bytes.
- SHA-256: `b80be422197ff8270f67049dbdd596603b0ebcf4f41884f6b22a5936fedf4596`.
- Signer: `f483539463f89dd957a8f7c68a3bb75da17450163f2e8767b4c47d5f1899adac`.

Device acceptance pending. On MOD and SCM, verify short tap vs long press on UV,
W and bones; object/mesh counts and parent relationships; dismissing reports;
UV gallery navigation and returning to the textured model. The installed Android
size must be measured on the phone; APK size is not installed-size evidence.
