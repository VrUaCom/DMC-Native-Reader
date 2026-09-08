# DMC Native Reader v1.0.0 release gates

This document defines the minimum gates for shipping, rebuilding, or maintaining the stable v1.0 line.

## Release identity

- versionName: `1.0.0`;
- versionCode: `20`;
- production package: `com.dmcrengine.nativereader`;
- stable v1 shell: Android / `arm64-v8a`;
- production registry: exactly MOD / SCM / DDS / PTX.

## Architecture

- Native format data is parsed once in the C++20 module/canonical path.
- DMC Rengine is the central engine/modding foundation for reusable semantics and canonical resource capabilities.
- Android/Java contains no MOD/SCM binary-layout parser.
- `InspectionDocument` is the inspection authority.
- `RenderScene` is the geometry/hierarchy render authority.
- `ImagePreview` is the generic image-preview contract.
- `ChildResource[]` is the generic nested-resource contract.
- UI availability comes from resource capabilities / `ResourceUiState`.
- Overlays use typed render flags; no per-format renderer/JNI parser path is introduced.
- Production registry remains exactly MOD / SCM / DDS / PTX until another family is explicitly promoted.
- Future iOS/Windows/Web shells must reuse the same C++20 semantics. Web resource semantics are compiled to WebAssembly rather than recreated as JS/TS parsers.

## Evidence

- Unknown fields stay unknown or preserved undecoded.
- MOD spatial hierarchy is enabled only when canonical authority supports it.
- MOD node positions come from canonical transform/world-matrix state, never invented from mesh vertices.
- MOD/SCM texture/material state comes only from typed confirmed fields.
- Unresolved companion/resource mapping must not be invented.
- No UI convenience is sufficient reason to create a second parser authority.

## Reader acceptance

Release/device tests cover at least:

- MOD: open, rotate, zoom, wireframe, Inspector, supported hierarchy/spatial state, skin/weights and texture-state inspection;
- SCM: open, placement, rotate, zoom, wireframe, scene hierarchy and texture-state inspection;
- DDS: inspection and image preview without requiring 3D rendering;
- PTX: inspection, texture gallery, validated DDS children, child image preview and parent navigation;
- rejected/malformed resources: fail closed without crashes, stale geometry or unbounded allocation.

The accepted v1.0 Samsung path has passed all of the above.

## Android identity and routing

Production:

- package id: `com.dmcrengine.nativereader`;
- user-visible label: `DMC Native Reader`;
- versionName: `1.0.0`;
- versionCode: `20`;
- Samsung/OEM / SAF `Open with` routing remains device-tested.

Public-source debug:

- package id: `com.dmcrengine.nativereader.debug`;
- local Android debug signer only;
- must not update or impersonate the production package.

## Signing

- No private key material may be tracked in Git.
- Normal Gradle `release` output remains unsigned.
- Production signing material is injected only through the protected GitHub `production` environment/repository secrets.
- Current release CI must never upload a keystore or signing password as an artifact.
- The production certificate fingerprint is pinned and verified before an APK is accepted.

Pinned v1 production certificate SHA-256:

`2d82bd3e77b2c1882d3f8143fe8760fc4c834aa65fc5e7b1b12082afcb7718d1`

Required protected secrets:

- `ANDROID_RELEASE_KEYSTORE_B64`;
- `ANDROID_RELEASE_STORE_PASSWORD`;
- `ANDROID_RELEASE_KEY_ALIAS`;
- `ANDROID_RELEASE_KEY_PASSWORD`.

The `production` environment should require owner/reviewer approval before a signing job can access its secrets.

## Accepted binary evidence

Official accepted v1.0.0 APK SHA-256:

`a81ef5555ecc67e0213d2f1f6baa351609a659c5898ba8f71f81d2fc2cefc68c`

Canonical release page:

`https://github.com/VrUaCom/DMC-Native-Reader/releases/tag/v1.0.0`

Canonical direct download path:

`https://github.com/VrUaCom/DMC-Native-Reader/releases/download/v1.0.0/DMC-Native-Reader-v1.0.0.apk`

A GitHub Release is acceptable only when the attached APK matches the recorded SHA-256 and production certificate.

## CI / artifact evidence

A release candidate must pass:

- canonical vendor provenance checks where applicable;
- native four-module registry regression;
- MOD/SCM native pipeline regression;
- MOD spatial/material regression;
- DDS/PTX validation regression;
- capability/UI policy regression;
- `RenderScene` regression;
- Android NDK/APK build;
- package/version/manifest checks;
- native module-marker and anti-legacy checks;
- signing-boundary check proving no key material is tracked;
- production certificate verification for the final signed APK;
- APK SHA-256 recording;
- diff audit confirming no accidental canonical vendor edits or retired decoder reintroduction.

## License gate

Public source distribution requires:

- project `LICENSE` present: **DMC Native Reader Personal Non-Commercial License 1.0**;
- `NOTICE.md` present;
- `THIRD_PARTY_NOTICES.md` present;
- vendored DMC Rengine MIT license preserved;
- contribution terms compatible with the project license and Capcom Special Grant.

The project must be described as **source-available**, not OSI open source.

## Merge policy

Feature PRs remain focused and must pass all applicable CI. A green CI run alone is not sufficient for changes whose correctness is visible only on real resources/device rendering; those changes require device/corpus acceptance evidence before merge.
