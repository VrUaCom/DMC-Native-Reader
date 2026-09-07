# Changelog

## Unreleased — public repository hardening

- separated public debug builds from the production package with `com.dmcrengine.nativereader.debug`;
- retired the committed development test keystore from the current tree;
- moved production signing to protected GitHub environment/repository secrets;
- pinned the production signing certificate fingerprint in the release workflow;
- removed production-key backup uploads from CI;
- expanded public-facing documentation, contribution and security guidance.

## 1.0.0 — DMC Native Reader v1

**Release identity:** `versionName 1.0.0`, `versionCode 20`.

### Stable Architecture v2 core

The v1 production registry is intentionally limited to four promoted families:

- **MOD** — canonical `dmc-rengine-cpp` structural reader projected into `RenderScene` / `InspectionDocument`;
- **SCM** — canonical `dmc-rengine-cpp` structural reader projected into `RenderScene` / `InspectionDocument`;
- **DDS** — bounded DMC3 DDS validation with generic image preview;
- **PTX** — bounded texture-bundle reader with validated DDS child resources and parent navigation.

The release removed the older broad multi-format surface from `main` until those families can be promoted through the same canonical/evidence contract.

### Architecture

- `NativeModuleRegistry` is the single product routing surface;
- registry size is exactly four in v1;
- unknown/unpromoted formats fail closed;
- no wildcard structural parser;
- no Java-owned MOD/SCM binary parser;
- no legacy `DecodeResult -> Mesh -> RenderScene` compatibility bridge;
- generic JNI session and capability-driven Android UI;
- renderability and inspection state are format capabilities, not filename guesses.

### Model readers

- MOD geometry pipeline;
- MOD hierarchy/spatial projection where canonical authority is available;
- MOD skin/weight and texture-slot inspection state;
- SCM geometry, hierarchy, transform and texture-slot projection;
- renderer consumes typed `RenderScene` data rather than format bytes.

### Texture readers

- standalone DDS preview;
- DXT1/DXT5 bounded validation;
- complete mip-chain size validation;
- PTX descriptor/payload coherence checks;
- generic DDS child resources;
- PTX -> DDS child preview and child -> parent navigation.

### Android

- system `Open with` / SAF integration;
- Samsung/OEM file-manager routing path;
- explicit MOD/SCM/DDS/PTX MIME/extension exposure;
- read-only file access;
- ARM64 native build;
- capability-driven UI prevents stale geometry on non-renderable sessions.

### Validation

The accepted release head passed:

- four-module registry regression;
- MOD/SCM end-to-end native pipeline regression;
- MOD spatial adapter regression;
- DDS/PTX valid/malformed/bounds regression;
- `RenderScene` regression;
- capability UI policy regression;
- Android NDK/APK build;
- package/version/manifest checks;
- native module-marker and anti-legacy checks;
- production signing verification;
- APK SHA-256 evidence generation.

### Production trust

The v1 production APK is signed by a dedicated production authority distinct from the historical development signer.

Production certificate SHA-256:

`2d82bd3e77b2c1882d3f8143fe8760fc4c834aa65fc5e7b1b12082afcb7718d1`

v1.0.0 APK SHA-256:

`a81ef5555ecc67e0213d2f1f6baa351609a659c5898ba8f71f81d2fc2cefc68c`

## Earlier development milestones

Pre-v1 internal device-test and architecture branches are historical implementation evidence only. They are not supported distribution lines and may contain superseded parser or module experiments.
