# DMC Native Reader v1.0.0 RC1

Release-candidate contract for the first stable v1 line.

## Identity

- versionName: `1.0.0-rc1`
- versionCode: `18`
- package: `com.dmcrengine.nativereader`
- label: `DMC Native Reader`
- ABI: `arm64-v8a`
- native language: C++20
- minSdk: 26
- target / compile SDK: 36

## Architecture frozen for RC1

`bytes -> canonical/native module -> PipelineResult -> InspectionDocument / RenderScene / ImagePreview / ChildResource -> JNI Session -> generic Android presentation`

The RC does not introduce format-specific Android viewers. `ResourceCapabilities` / `ResourceUiState` control presentation. `RenderScene` remains the only geometry authority. DDS image preview and child-resource navigation use generic IR/JNI paths.

## Real-device evidence already accepted

Samsung acceptance completed for the current v1 feature set:

- MOD: 3D model rendering, rotation/zoom, wireframe, canonical model-space hierarchy overlay, skin/weights inspection, typed texture slot and legacy GS CLAMP state;
- SCM: 3D scene rendering, canonical scene hierarchy overlay, texture binding / GS sampler inspection;
- PTX: real bundle opens from Samsung My Files, gallery shows all child DDS resources as real thumbnails, child DDS opens in the same Reader, explicit `←` and Android Back return to the existing parent Session;
- DDS: standalone and PTX-child DDS image previews display correctly;
- Android identity: user-visible label is `DMC Native Reader` with no legacy `v8` suffix.

## Automated RC smoke gate

RC1 must pass one self-contained workflow plus the normal Android and hardening workflows:

- canonical vendor diff guard;
- modular-reader regression and unknown-family fail-closed behavior;
- MOD spatial/material regression;
- DDS/PTX DXT1/DXT5, malformed/truncated/overflow and child-resource regression;
- RenderScene/overlay regression;
- Java capability/UI policy regression;
- Android NDK + ARM64 APK build;
- package/version/label/manifest verification;
- native marker and retired-decoder/wildcard absence checks;
- debug development-signer verification;
- unsigned release boundary verification;
- SHA-256 evidence for debug and unsigned release APKs.

## Signing boundary

RC1 may produce two internal artifacts:

1. a development-signed debug APK for device acceptance;
2. an unsigned release APK proving the production build path.

The unsigned release APK is **not** an official distributable release. Stable v1.0 remains blocked until a separate production signing authority is provisioned outside Git history and its certificate fingerprint is recorded.

The committed development/test keystore is not a production trust root.

## Scope exclusions

RC1 remains read-only. It does not claim semantic completion for every recognized DMC family and does not promote unresolved SO/EFM/MRP/SHW semantics merely to reach a version number. New format promotion belongs after the v1 freeze unless required to fix a release regression.

## Promotion rule

`v1.0.0-rc1 -> v1.0.0` requires:

- all exact-head RC workflows green;
- one final Samsung smoke pass of MOD, SCM, DDS and PTX on the RC build;
- no new release-blocking regression;
- production signing authority provisioned and separated from the repository test signer;
- production-signed APK certificate digest and APK SHA-256 recorded.
