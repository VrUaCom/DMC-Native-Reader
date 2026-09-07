# DMC Native Reader v1.0 release gates

This document defines the minimum gates for promoting a development build to the first stable v1.0 release.

## Architecture

- Native format data is parsed once in the C++20 canonical/native module path.
- Android/Java contains no MOD/SCM/DDS/PTX binary-layout parsing.
- `InspectionDocument` is the inspection authority.
- `RenderScene` is the geometry/hierarchy render authority.
- `ImagePreview` is the static image-preview authority.
- `ChildResource` is the generic nested-resource projection used by container-like resources.
- UI availability comes from `ResourceCapabilities` / `ResourceUiState`.
- Overlays use `RenderFlags`; no per-format renderer/JNI APIs are introduced.
- Child navigation is Session-stack based; parent resources are restored without reparsing.

## Evidence

- Unknown fields stay unknown or preserved undecoded.
- MOD spatial hierarchy is enabled only for documents passing canonical `supports_spatial_hierarchy()`.
- MOD node positions come from canonical model-space world matrices, never mesh vertices.
- MOD texture slot / GS CLAMP state comes only from typed canonical `dmc-rengine-cpp` fields.
- Unresolved MOD bitmap/companion mapping must not be invented.
- PTX exposes validated DDS children but does not invent a bundle-level primary texture.
- Runtime gallery/cache state is not promoted to file-format evidence.

## Reader acceptance

Real-device v1 feature acceptance has covered:

- MOD: open, rotate, zoom, wireframe, Inspector, hierarchy overlay, skin/weights, texture-slot/GS-state inspection;
- SCM: open, placement, rotate, zoom, wireframe, scene hierarchy overlay, texture-slot/GS-state inspection;
- DDS: standalone image preview and inspection;
- PTX: bundle inspection, child texture thumbnails, child DDS image preview, explicit parent navigation and Android Back;
- Android routing through Samsung My Files for the tested resources.

The final RC artifact must repeat a smoke pass of these four primary surfaces without a release-blocking regression.

Rejected/malformed resources must fail closed without crashes or unbounded allocation. DDS/PTX malformed, overflow, trailing-data, sector-span and padding cases are covered by dedicated host regression.

## Android identity and routing

RC1 identity:

- package id: `com.dmcrengine.nativereader`;
- versionCode: `18`;
- versionName: `1.0.0-rc1`;
- user-visible label: `DMC Native Reader` with no legacy internal version suffix;
- ABI: `arm64-v8a`.

System-bar, display-cutout and navigation-bar insets must remain correct.

## Signing

- Repository test/debug signing material is not a production release authority.
- Debug APKs may use the stable repository development signer for internal update compatibility.
- The normal Gradle `release` build remains unsigned until a protected production signing key is injected by a dedicated release pipeline.
- Production key material, passwords and signing secrets must not be committed to the repository.
- The final stable v1.0 release artifact must be signed by the production authority and its certificate digest recorded with release evidence.
- The unsigned RC release APK is evidence that the production build path works; it is not an official distributable release.

## CI / artifact evidence

The release candidate must pass:

- canonical vendor provenance / diff guard;
- native modular-reader regression;
- MOD spatial/material regression;
- DDS/PTX malformed + child-resource regression;
- capability/UI policy regression;
- RenderScene/overlay regression;
- Android NDK/APK build;
- debug + unsigned release builds;
- package/version/manifest checks;
- native module marker checks;
- retired wildcard/duplicate-decoder absence checks;
- signer verification appropriate to build type;
- debug and unsigned-release APK SHA-256 recording.

RC1 uses a self-contained `v1-rc.yml` workflow in addition to the normal Android, DDS/PTX and hardening workflows.

## RC merge policy

The RC PR stays draft until:

1. all exact-head workflows are green;
2. the exact-head debug APK is smoke-tested on Samsung for MOD, SCM, PTX and DDS;
3. no release-blocking regression remains.

Merging RC1 to `main` does not by itself authorize a public stable APK because production signing is a separate final gate.

## Stable v1.0 promotion

`1.0.0-rc1 -> 1.0.0` requires:

- RC merge complete;
- production signer provisioned outside Git history;
- production-signed APK built from an accepted stable head;
- production certificate SHA-256 recorded;
- final APK SHA-256 recorded;
- final device install/smoke acceptance of the production-signed artifact or an explicitly documented signer-migration/reinstall path.
