# Changelog

## Unreleased — public repository hardening and platform previews

Public-opening preparation after the accepted Android v1.0.0 binary. The stable Android v1 runtime/parser contract remains MOD / SCM / DDS / PTX; additional platform shells are preview work over the same core rather than new format authorities.

### Public/release preparation

- separated public Android debug builds from the production package with `com.dmcrengine.nativereader.debug`;
- retired the committed development test keystore from the current tree;
- moved Android production signing to protected GitHub environment/repository secrets;
- pinned the production signing certificate fingerprint in the release workflow;
- removed production-key backup generation/uploads from the current release workflow;
- added `DMC Native Reader Personal Non-Commercial License 1.0` with the Capcom Special Grant;
- documented third-party licensing separately, including the MIT-licensed vendored DMC Rengine slice;
- formalized the product mission: **Make DMC resources feel like ordinary files.**;
- documented DMC Rengine as the central decompilation/reimplementation engine and C++20 modding foundation;
- expanded README, release notes, contribution, support, security and public-opening guidance;
- added canonical Android v1.0.0 release and APK download paths.

### Cross-platform preview foundation

- added platform-neutral C++20 `PortableSession` on top of the existing Architecture v2 `PipelineResult`;
- added a current iOS SwiftUI + Objective-C++ shell that compiles the same MOD / SCM / DDS / PTX registry, DMC Rengine-backed readers, image/child contracts and CPU renderer;
- upgraded the iOS project from the old pre-v1 C++17 / `0.10.1` experiment to the current C++20 / `1.0.0` product identity;
- retired the old iOS HITS / stage TXT / `.index` product claims rather than reviving those removed modules;
- added iOS Files/Share-sheet opening, MOD/SCM 3D viewing, Inspector, DDS image preview and PTX child gallery;
- preserved `ios-unsigned-latest` as the moving iOS preview release line instead of deleting it;
- defined replacement iOS asset `DMC-Native-Reader-iOS-v1.0.0-unsigned.ipa` for publication only after a successful current build;
- added a native Win32/x64 shell with file open, drag-and-drop, MOD/SCM rendering, DDS preview, PTX child navigation and Inspector output;
- defined Windows preview package `DMC-Native-Reader-Windows-v1.0.0-preview.zip` and moving tag `windows-preview-latest`;
- added macOS/iOS and Windows/MSVC preview build jobs plus an owner-triggered guarded publish stage;
- documented that iOS and Windows remain **Preview** until real-corpus/device acceptance and platform signing/distribution gates are closed;
- kept Web as the next shell direction with binary semantics supplied by C++20/WebAssembly rather than a second JavaScript parser stack.

## 1.0.0 — DMC Native Reader v1

**Release identity:** `versionName 1.0.0`, `versionCode 20`.

### Stable Architecture v2 core

The v1 production registry is intentionally limited to four promoted families:

- **MOD** — canonical DMC Rengine structural reader projected into `RenderScene` / `InspectionDocument`;
- **SCM** — canonical DMC Rengine structural reader projected into `RenderScene` / `InspectionDocument`;
- **DDS** — bounded DMC3 DDS validation with generic image preview;
- **PTX** — bounded texture-bundle reader with validated DDS child resources and parent navigation.

The release removed the older broad multi-format surface from `main` until those families can be promoted through the same Architecture v2 / canonical-evidence contract.

### Architecture

- `NativeModuleRegistry` is the single v1 product routing surface;
- registry size is exactly four in v1;
- unknown/unpromoted formats fail closed;
- no wildcard structural parser;
- no platform-UI-owned MOD/SCM binary parser;
- no legacy `DecodeResult -> Mesh -> RenderScene` compatibility bridge;
- generic native Session with `InspectionDocument`, `RenderScene`, `ImagePreview`, `ChildResource[]` and capabilities;
- Android v1 UI availability is capability-driven rather than filename-driven;
- reusable engine-level semantics belong in DMC Rengine rather than in private product forks.

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

### Android v1 shell

- system `Open with` / SAF integration;
- Samsung/OEM file-manager routing path;
- explicit MOD/SCM/DDS/PTX MIME/extension exposure;
- read-only file access;
- ARM64 native build;
- capability-driven UI prevents stale geometry on non-renderable sessions.

### Device acceptance

The accepted Samsung/device path proved:

- MOD 3D render, rotate/zoom, wireframe, hierarchy/skeleton and Inspector;
- SCM 3D render, hierarchy and Inspector;
- standalone DDS image preview;
- PTX gallery with real DDS thumbnails;
- child DDS preview and explicit return to the parent PTX session;
- unsupported/malformed resources fail closed without stale geometry.

### Validation

The accepted release head passed:

- four-module registry regression;
- MOD/SCM end-to-end native pipeline regression;
- MOD spatial/material adapter regression;
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

### Canonical release URLs

Release page:

`https://github.com/VrUaCom/DMC-Native-Reader/releases/tag/v1.0.0`

Direct APK:

`https://github.com/VrUaCom/DMC-Native-Reader/releases/download/v1.0.0/DMC-Native-Reader-v1.0.0.apk`

These URLs are the canonical public distribution surface once the stable GitHub Release is published.

## Earlier development milestones

Pre-v1 internal device-test and architecture branches are historical implementation evidence only. They are not supported distribution lines and may contain superseded parser or module experiments.
