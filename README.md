# DMC Native Reader

[![Core architecture](https://github.com/VrUaCom/DMC-Native-Reader/actions/workflows/android.yml/badge.svg)](https://github.com/VrUaCom/DMC-Native-Reader/actions/workflows/android.yml)
[![DDS/PTX gate](https://github.com/VrUaCom/DMC-Native-Reader/actions/workflows/dds-ptx-v1.yml/badge.svg)](https://github.com/VrUaCom/DMC-Native-Reader/actions/workflows/dds-ptx-v1.yml)
[![v1 hardening](https://github.com/VrUaCom/DMC-Native-Reader/actions/workflows/v1-hardening.yml/badge.svg)](https://github.com/VrUaCom/DMC-Native-Reader/actions/workflows/v1-hardening.yml)

> **Make DMC resources feel like ordinary files.**

**DMC Native Reader** is the viewability/accessibility product of the DMC Rengine ecosystem. It turns promoted Devil May Cry 3 HD Collection resource files into familiar representations that can be opened and understood without first learning their binary layouts.

The stable **v1.0.0** implementation targets Android. The architecture is designed so the same C++20 resource semantics can later serve Android, iOS, Windows and Web without reimplementing format logic per platform.

## Download v1.0.0

**Android / arm64-v8a**

[**Download DMC Native Reader v1.0.0 APK**](https://github.com/VrUaCom/DMC-Native-Reader/releases/download/v1.0.0/DMC-Native-Reader-v1.0.0.apk)

[View the v1.0.0 GitHub Release](https://github.com/VrUaCom/DMC-Native-Reader/releases/tag/v1.0.0)

Release identity:

- package: `com.dmcrengine.nativereader`
- versionName: `1.0.0`
- versionCode: `20`
- ABI: `arm64-v8a`
- minimum Android: API 26 / Android 8.0
- target / compile SDK: 36
- native standard: C++20

Official v1.0.0 APK SHA-256:

```text
a81ef5555ecc67e0213d2f1f6baa351609a659c5898ba8f71f81d2fc2cefc68c
```

Production signing certificate SHA-256:

```text
2d82bd3e77b2c1882d3f8143fe8760fc4c834aa65fc5e7b1b12082afcb7718d1
```

Historical development/debug builds used a different signing authority. Android may require uninstalling a development build before installing the production-signed v1.0.0 APK.

## v1.0.0 supported formats

The production registry is intentionally limited to four promoted resource families:

| Format | v1 representation | Authority |
| --- | --- | --- |
| **MOD** | 3D model + hierarchy/skin/material inspection | canonical DMC Rengine reader through Architecture v2 adapter |
| **SCM** | 3D scene + hierarchy/transforms/material-state inspection | canonical DMC Rengine reader through Architecture v2 adapter |
| **DDS** | validated DXT1/DXT5 image preview + inspection | bounded C++20 DDS path |
| **PTX** | texture gallery + navigable DDS children | bounded C++20 PTX path |

Unknown and unpromoted formats fail closed. v1 does not contain a wildcard decoder or broad recognition-only fallback.

## Product role

Native Reader is about making resources **viewable**, not turning the Reader into a second authoring suite.

Its core verbs are:

```text
Open -> Recognize -> Inspect -> Visualize -> Navigate -> Understand
```

A normal user should be able to tap an unfamiliar resource and get the most natural evidence-backed representation available:

```text
model.mod    -> 3D model
scene.scm    -> 3D scene
texture.dds  -> image
bundle.ptx   -> texture gallery
```

The long-term product doctrine is documented in [`docs/PRODUCT_VISION.md`](docs/PRODUCT_VISION.md).

## DMC Rengine ecosystem

**DMC Rengine is the central project and modding foundation.** It is the decompilation/reimplementation engine and shared C++20 core from which specialized tools are built.

```text
                         DMC Rengine
                 central engine + modding core
                              |
             +----------------+----------------+
             |                                 |
       Native Reader                       Pocket GDS
  view / inspect / visualize       manage / edit / author / repack
             |
   Android / iOS / Windows / Web
```

DMC Rengine owns the central technical capabilities where they belong: recovered runtime behavior, typed resource models, parsers, writers, validation, resource architecture and reusable modding logic.

Native Reader consumes the read-side capabilities needed for direct viewing. Pocket GDS focuses on broader resource workflows and authoring. Downstream tools must not create private incompatible parser authorities when a capability belongs in DMC Rengine.

Central project: [`VrUaCom/dmc-rengine-cpp`](https://github.com/VrUaCom/dmc-rengine-cpp)

## Architecture v2

The v1 path is:

```text
resource bytes
    |
    v
bounded / DMC Rengine-backed C++20 authority
    |
    v
NativeModuleRegistry
    |
    +--> InspectionDocument
    +--> RenderScene
    +--> ImagePreview
    +--> ChildResource[]
    +--> ResourceCapabilities
    |
    v
generic native Session
    |
    v
capability-driven platform UI
```

Core rules:

- format bytes are interpreted in native C++;
- MOD/SCM structural authority comes from the vendored canonical DMC Rengine core;
- platform UI code does not duplicate binary layouts;
- no renderer-owned format parser;
- no wildcard family decoder;
- unsupported resources fail closed;
- stale geometry is never reused by non-renderable sessions;
- new resource semantics that belong in the central engine are promoted in DMC Rengine first;
- new formats extend shared module/capability contracts instead of adding standalone format viewers.

See [`docs/ARCHITECTURE_V2.md`](docs/ARCHITECTURE_V2.md) and [`docs/V1_BASELINE.md`](docs/V1_BASELINE.md).

## Cross-platform direction

Android is the first stable product shell, not the semantic boundary.

The intended platform model is one C++20 engine foundation with thin native presentation layers:

```text
DMC Rengine / Native Reader C++20 core
        |
        +--> Android
        +--> iOS
        +--> Windows
        `--> WebAssembly -> Web UI
```

For Web, parsing, validation, typed resource models, `InspectionDocument`, `RenderScene`, `ImagePreview`, `ChildResource`, capabilities and evidence remain in C++20 and are compiled to WebAssembly. JavaScript/TypeScript should remain a thin browser/presentation layer rather than a second DMC parser implementation.

## Build from source

Prerequisites:

- JDK 17
- Android SDK 36
- Android build-tools 36.0.0
- Android NDK `28.2.13676358`
- CMake `3.22.1`
- Gradle `9.5.x`

Debug build:

```bash
gradle --no-daemon :app:assembleDebug
```

Output:

```text
app/build/outputs/apk/debug/app-debug.apk
```

Public-source debug builds intentionally use the separate package `com.dmcrengine.nativereader.debug` and the ordinary local Android debug signer. They cannot update or impersonate the official production package.

Unsigned release build:

```bash
gradle --no-daemon :app:assembleRelease
```

Official production signing is performed separately through the protected release workflow. Production private-key material is not stored in the repository.

## Evidence policy

Recognition is not semantic proof.

Contributions and documentation must keep these layers separate:

- extension / filename recognition;
- content-confirmed identity;
- structural decoding;
- semantic interpretation;
- rendering behavior;
- original-game/runtime behavior.

Generated explanations, naming guesses or visual similarity are not evidence by themselves. Unknown fields remain unknown until stronger evidence exists.

## Contributing

Contributions are welcome when they preserve Architecture v2, DMC Rengine authority boundaries, evidence discipline and the project license.

Read [`CONTRIBUTING.md`](CONTRIBUTING.md) before opening a pull request. Do not upload proprietary game archives, executable binaries, leaked source, signing secrets or other material you do not have the right to redistribute.

Security reports should follow [`SECURITY.md`](SECURITY.md).

## License

DMC Native Reader is **source-available for personal, non-commercial use** under the [`DMC Native Reader Personal Non-Commercial License 1.0`](LICENSE).

In short:

- individuals may use, study, modify and share it for personal non-commercial purposes;
- redistribution and derivative versions must remain free/non-commercial under the same terms;
- third-party commercial use is prohibited without separate written permission;
- **Capcom Co., Ltd. and its controlled affiliates receive the separate commercial rights stated in the Capcom Special Grant**;
- third-party/vendored code remains under its own licenses.

This is intentionally **not an OSI-approved open-source license** because commercial and organizational use is restricted.

See [`NOTICE.md`](NOTICE.md) and [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

## Legal notice

DMC Native Reader is an independent fan-made interoperability/reverse-engineering/modding project. It is not affiliated with, endorsed by, or sponsored by Capcom Co., Ltd.

"Devil May Cry", "Devil May Cry 3" and related game assets, trademarks and intellectual property belong to their respective rights holders. This repository does not include Capcom game archives, proprietary game assets, proprietary source code, or DMC3 executable binaries.

The Capcom Special Grant in the project license is a voluntary license from the Native Reader copyright holders; it does not represent an endorsement or transfer ownership of independently authored project code.

## Status

- stable release: **v1.0.0**
- production registry: **MOD / SCM / DDS / PTX**
- product role: direct resource viewing/accessibility
- central engine/modding foundation: **DMC Rengine C++20**
- next resource families are promoted one by one only after clean authority/evidence closure

See [`docs/STATUS.md`](docs/STATUS.md) and [`docs/ROADMAP.md`](docs/ROADMAP.md).
