# DMC Native Reader

[![Core architecture](https://github.com/VrUaCom/DMC-Native-Reader/actions/workflows/android.yml/badge.svg)](https://github.com/VrUaCom/DMC-Native-Reader/actions/workflows/android.yml)
[![DDS/PTX gate](https://github.com/VrUaCom/DMC-Native-Reader/actions/workflows/dds-ptx-v1.yml/badge.svg)](https://github.com/VrUaCom/DMC-Native-Reader/actions/workflows/dds-ptx-v1.yml)
[![v1 hardening](https://github.com/VrUaCom/DMC-Native-Reader/actions/workflows/v1-hardening.yml/badge.svg)](https://github.com/VrUaCom/DMC-Native-Reader/actions/workflows/v1-hardening.yml)

> **Make DMC resources feel like ordinary files.**

**DMC Native Reader** is a resource-viewing and accessibility product built on the DMC Rengine ecosystem for Devil May Cry 3 HD Collection files. The stable v1 implementation targets Android; the architecture is intended to carry the same C++20 semantic truth to Android, iOS, Windows and Web over time.

The product goal is simple: a user should be able to open an unfamiliar DMC resource and immediately get the most natural useful representation — a 3D model, scene, image, texture gallery, hierarchy or other evidence-backed view — without first learning the binary format.

Native Reader integrates with normal file-opening flows and routes promoted resources into a bounded C++20 reader pipeline. The project is intentionally evidence-aware: a format is promoted only when its identity and structure are supported strongly enough to expose without inventing semantics.

The long-term product doctrine is documented in [`docs/PRODUCT_VISION.md`](docs/PRODUCT_VISION.md).

## Ecosystem role

**DMC Rengine is the central project and modding foundation.** It is the decompilation/reimplementation engine and common C++20 core from which specialized tools are built. Its responsibilities include recovered runtime behavior, typed resource models, readers, writers, validation, resource architecture and reusable modding capabilities.

DMC Native Reader is one consumer of that foundation. Its role is the **viewability/accessibility layer**:

```text
Open -> Recognize -> Inspect -> Visualize -> Navigate -> Understand
```

Pocket GDS is another specialized tool built on the same DMC Rengine foundation, focused on browsing, extraction, replacement, editing, repacking and broader resource-management workflows.

The intended relationship is:

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

Native Reader should not become a second independent format authority or a competing authoring workspace. If a capability belongs in the central engine, it should be promoted in DMC Rengine and then consumed by downstream tools.

## v1.0.0 baseline

The stable v1.0.0 product surface is deliberately small:

| Format | Capability | Authority |
| --- | --- | --- |
| **MOD** | 3D scene + inspection | canonical `dmc-rengine-cpp` reader through an Architecture v2 adapter |
| **SCM** | 3D scene + inspection | canonical `dmc-rengine-cpp` reader through an Architecture v2 adapter |
| **DDS** | validated image preview + inspection | bounded DMC3 DDS reader |
| **PTX** | texture bundle + DDS child resources | bounded PTX reader with validated child boundaries |

Unknown or unpromoted formats fail closed. The v1 registry does not contain a wildcard decoder or a broad recognition-only fallback.

Current Android identity:

- package: `com.dmcrengine.nativereader`
- version: `1.0.0` (`versionCode 20`)
- ABI: `arm64-v8a`
- minimum Android: API 26 / Android 8.0
- target / compile SDK: 36
- native standard: C++20
- NDK: `28.2.13676358`
- CMake: `3.22.1`

## What it does

DMC Native Reader can participate in Android `Open with` / Storage Access Framework flows for its promoted resource types. The normal path is:

```text
Android file / content URI
        |
        v
bounded native probe
        |
        v
NativeModuleRegistry
        |
        +--> MOD adapter --> RenderScene + InspectionDocument
        +--> SCM adapter --> RenderScene + InspectionDocument
        +--> DDS module  --> ImagePreview + InspectionDocument
        `--> PTX module  --> ChildResource[] + InspectionDocument
        |
        v
generic JNI Session
        |
        v
capability-driven Android UI
```

The app is read-only. It does not rewrite game files as part of inspection.

## Cross-platform direction

Android is the first stable shell, not the semantic boundary of the project.

The strategic direction is:

```text
                         DMC Rengine
                 central C++20 modding core
                              |
             +----------------+----------------+
             |                                 |
       Native Reader                       other tools
             |
   +------+------+------+
   |      |      |      |
Android  iOS  Windows   Web
```

Platform integration can differ — Android Open with, iOS Files/Share integration, Windows Explorer previews/thumbnails, Web drag-and-drop/browser file input — but file semantics must not be reimplemented independently per platform.

For the future Web version, the resource engine remains C++20 and is intended to be compiled to WebAssembly. JavaScript/TypeScript should remain a thin browser/presentation layer for DOM, canvas/WebGL/WebGPU, routing and user interaction rather than becoming a second parser or semantic implementation.

## Architecture rules

`main` follows Architecture v2 and keeps parser ownership explicit:

- format bytes are interpreted in C++20;
- MOD/SCM structural authority comes from the vendored DMC Rengine canonical core;
- the platform UI consumes typed capabilities instead of parsing binary layouts itself;
- no renderer-owned format parser is allowed;
- no unknown family is accepted through a generic structural fallback;
- non-renderable sessions cannot reuse stale geometry;
- unknown or unresolved semantics stay unknown;
- new formats extend the module/capability system rather than creating standalone format viewers;
- capabilities that belong to the central modding engine are promoted in DMC Rengine first instead of being reimplemented privately in Native Reader;
- the future Web build must reuse the same C++20 resource logic through WebAssembly rather than reimplementing it in JavaScript/TypeScript.

The central decompilation/reimplementation and modding project is [`VrUaCom/dmc-rengine-cpp`](https://github.com/VrUaCom/dmc-rengine-cpp).

More detail: [`docs/ARCHITECTURE_V2.md`](docs/ARCHITECTURE_V2.md), [`docs/V1_BASELINE.md`](docs/V1_BASELINE.md), and [`docs/PRODUCT_VISION.md`](docs/PRODUCT_VISION.md).

## Build from source

Prerequisites:

- JDK 17;
- Android SDK 36;
- Android build-tools 36.0.0;
- Android NDK 28.2.13676358;
- CMake 3.22.1;
- Gradle 9.5.x.

Build a debug APK:

```bash
gradle --no-daemon :app:assembleDebug
```

Output:

```text
app/build/outputs/apk/debug/app-debug.apk
```

Public-source debug builds intentionally use the package `com.dmcrengine.nativereader.debug` and Android's ordinary local debug signer. They are isolated from the production package and cannot update or impersonate an official release.

Build an unsigned release APK:

```bash
gradle --no-daemon :app:assembleRelease
```

Official production signing is performed separately through a protected GitHub `production` environment. Production private-key material is never stored in the repository or uploaded as a CI artifact.

## Official release trust

The v1 production signing certificate is pinned by SHA-256:

```text
2d82bd3e77b2c1882d3f8143fe8760fc4c834aa65fc5e7b1b12082afcb7718d1
```

For the original v1.0.0 APK, the recorded APK SHA-256 is:

```text
a81ef5555ecc67e0213d2f1f6baa351609a659c5898ba8f71f81d2fc2cefc68c
```

A production APK should be treated as official only when its package identity, version and signing certificate match the published release evidence.

## Evidence policy

This project comes from reverse engineering and decompilation work in the DMC Rengine ecosystem, so recognition is not treated as semantic proof.

Contributions should keep these layers separate:

- extension / filename recognition;
- content-confirmed identity;
- structural decoding;
- semantic interpretation;
- rendering behavior;
- original-game/runtime behavior.

Generated explanations, naming guesses or visual similarity are not sufficient evidence by themselves. Unknown fields should remain unknown until stronger evidence exists.

## Contributing

Contributions are welcome, especially for:

- real-file compatibility;
- parser bounds / malformed-input hardening;
- MOD/SCM rendering or hierarchy regressions;
- DDS/PTX validation;
- platform file-routing behavior;
- regression tests and evidence-backed documentation.

Read [`CONTRIBUTING.md`](CONTRIBUTING.md) before opening a pull request. Please do not upload copyrighted game archives, proprietary executable binaries, leaked source, or other material you do not have the right to redistribute.

Security issues should follow [`SECURITY.md`](SECURITY.md), not a public issue with exploit details or signing material.

## Project status and roadmap

- stable product baseline: **v1.0.0**;
- current supported registry: **MOD / SCM / DDS / PTX**;
- DMC Rengine remains the central engine/modding foundation;
- next formats are promoted one by one only after canonical/evidence closure;
- primary progress metric: how many previously opaque DMC resources become directly understandable and viewable;
- authoring/repacking remains outside the Native Reader core product role and belongs to DMC Rengine-backed authoring/resource tools;
- future Android, iOS, Windows and Web shells share the same central C++20 resource semantics.

See [`docs/STATUS.md`](docs/STATUS.md) and [`docs/ROADMAP.md`](docs/ROADMAP.md).

## Legal notice

DMC Native Reader is an independent, fan-made interoperability, reverse-engineering and modding project. It is not affiliated with, endorsed by, or sponsored by Capcom Co., Ltd.

"Devil May Cry" and related names, game data and intellectual property belong to their respective rights holders. This repository does not include Capcom game archives, proprietary game assets, proprietary source code, or DMC3 executable binaries.

See [`NOTICE.md`](NOTICE.md).

## License

A source-code license must be selected and committed before the repository is presented as open source or source-available under explicit reuse terms. Repository visibility by itself does not grant reuse rights.
