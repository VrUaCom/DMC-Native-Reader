# DMC Native Reader v1.0 release gates

This document defines the minimum gates for promoting a development build to the first stable v1.0 release.

## Architecture

- Native format data is parsed once in the C++20 canonical module path.
- Android/Java contains no MOD/SCM binary-layout parsing.
- `InspectionDocument` is the inspection authority.
- `RenderScene` is the geometry/hierarchy render authority.
- UI availability comes from `ResourceCapabilities` / `ResourceUiState`.
- Overlays use `RenderFlags`; no per-format renderer/JNI APIs are introduced.

## Evidence

- Unknown fields stay unknown or preserved undecoded.
- MOD spatial hierarchy is enabled only for documents passing canonical `supports_spatial_hierarchy()`.
- MOD node positions come from canonical model-space world matrices, never mesh vertices.
- MOD texture slot / GS CLAMP state comes only from typed canonical `dmc-rengine-cpp` fields.
- Unresolved MOD bitmap/companion mapping must not be invented.

## Reader acceptance

Before v1.0, real-device tests must cover at least:

- MOD: open, rotate, zoom, wireframe, Inspector, hierarchy overlay, skin/weights, texture-slot/GS-state inspection.
- SCM: open, placement, rotate, zoom, wireframe, scene hierarchy overlay, texture-slot/GS-state inspection.
- DDS: inspection and image capability path without requiring 3D rendering.
- PTX: inspection, child DDS discovery and safe handling without 3D rendering.
- rejected/malformed resources: fail closed without crashes or unbounded allocation.

## Android identity and routing

- Package id: `com.dmcrengine.nativereader`.
- User-visible application label: `DMC Native Reader` with no legacy internal version suffix.
- Samsung My Files / SAF Open with routing remains device-tested.
- System-bar, display-cutout and navigation-bar insets remain correct.

## Signing

- Repository test/debug signing material is not a production release authority.
- Debug APKs may use the Android development signer.
- The normal Gradle `release` build remains unsigned until a protected production signing key is injected by a dedicated release pipeline.
- Production key material, passwords and signing secrets must not be committed to the repository.
- The final v1.0 release artifact must be signed by the production authority and its certificate digest recorded with the release evidence.

## CI / artifact evidence

The release candidate must pass:

- canonical vendor provenance checks;
- native modular-reader regression;
- MOD spatial/material regression;
- capability/UI policy regression;
- RenderScene/overlay regression;
- Android NDK/APK build;
- package/version/manifest checks;
- native module marker checks;
- signer verification appropriate to the build type;
- APK SHA-256 recording;
- diff audit confirming no accidental canonical vendor edits or retired decoder reintroduction.

## Merge policy

Feature PRs stay draft until their specific device acceptance is complete. A green CI run alone is not sufficient for features whose correctness is visible only on real resources/device rendering.
