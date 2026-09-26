# DMC Native Reader

Native Android and Windows reader for Devil May Cry 3: Special Edition resources from Devil May Cry HD Collection, built around a reusable **C++23** core and canonical DMC Rengine read-side architecture.

## Releases

| Platform | Status | Current public release |
| --- | --- | --- |
| Android | Current / promoted to `main` | **v68 / 1.0.41** |
| Windows x64 | Technical preview | **v1.0.0 Preview** |

- Android release notes: [`docs/releases/android-v68-1.0.41.md`](docs/releases/android-v68-1.0.41.md)
- Windows preview release: [`windows-v1.0.0-preview`](https://github.com/VrUaCom/DMC-Native-Reader/releases/tag/windows-v1.0.0-preview)

The Windows preview is an earlier public snapshot and must not be described as feature-identical to Android v68 until Windows parity work is completed and released.

## What it does

DMC Native Reader is designed for fast, safe inspection of game resources without editing the original files.

Core capabilities include:

- native structural parsing and inspection;
- interactive 3D viewing for supported model/scene resources;
- texture preview and texture-slot inspection;
- animation and motion playback where supported;
- PAC/PNST resource browsing and assembled-model workflows;
- UV, hierarchy, collision, shadow and effect inspection where the corresponding native module is available;
- fail-closed handling for unknown or unsupported data;
- read-only operation.

## Current native registry

The current Android `main` registry contains bounded native modules for:

- **MOD**
- **SCM**
- **DDS**
- **PTX**
- **EventTbl**
- **PAC**
- **MOT**
- **PNST**
- **SHW**
- **TSC**
- **CLT**
- **EFM**
- motion scripts
- collision shape/index data
- effect banks

Legacy `.tm2` logical names that contain the DMC wrapped-DDS form use the validated DDS path; they are not treated as a fabricated standalone TIM2 implementation.

Platform releases do not necessarily expose every current `main` capability. Check the release notes for the exact platform/version being used.

## Architecture

```text
resource bytes
  -> bounded probe
  -> NativeModuleRegistry
  -> canonical DMC Rengine / native format authority
  -> DMCNativeReader::Core (C++23)
  -> resource session / typed inspection / renderer
  -> platform shell
       -> Android
       -> Windows
```

DMC Native Reader consumes canonical read-side format knowledge from **DMC Rengine** rather than reimplementing format semantics separately in each UI.

## Product boundary

DMC Native Reader is a **reader**, not an editor.

Editing, rebuilding and repacking resources belong to **DMC Rengine** and future authoring tools. Native Reader should not silently become a second writer implementation.

## Naming convention

Use the official product names consistently:

- **Devil May Cry HD Collection** — the collection.
- **Devil May Cry 3: Special Edition** — the game currently targeted by DMC Native Reader.
- **DMC3** — acceptable shorthand after the full game name has already been established.

Do **not** use “Devil May Cry 3 HD Collection” as a product title.

## Android

Current promoted release:

- versionCode: **68**
- versionName: **1.0.41**
- ABI: **arm64-v8a**
- minSdk / targetSdk: **26 / 36**
- package: `com.dmcrengine.nativereader`
- native language: target-scoped **C++23**

See [Android v68 release notes](docs/releases/android-v68-1.0.41.md).

## Windows

The public Windows build is currently **v1.0.0 Preview**.

It is a portable Windows x64 technical preview with read-only MOD / SCM / DDS / PTX viewing from the shared native architecture. Windows parity with the newer Android line is active work and should be described as such until a newer Windows release is published.

## Documentation

Start here:

- [Documentation index](docs/README.md)
- [Current status](docs/STATUS.md)
- [Roadmap](docs/ROADMAP.md)
- [Architecture v2](docs/ARCHITECTURE_V2.md)
- [Release gates](docs/RELEASE_GATES_V1.md)
- [Changelog](CHANGELOG.md)
- [Security policy](SECURITY.md)
- [Contributing](CONTRIBUTING.md)

Versioned research/evidence documents are historical records unless explicitly marked as current.

## Legal

DMC Native Reader is an independent fan-made interoperability, reverse-engineering and modding project. It is not affiliated with, endorsed by, sponsored by, or otherwise associated with Capcom Co., Ltd.

**Devil May Cry**, **Devil May Cry 3: Special Edition**, **Devil May Cry HD Collection** and related trademarks, characters, game data and other intellectual property belong to their respective rights holders.

This repository does not distribute Capcom game archives, proprietary game assets, proprietary source code or game executable binaries.

See [NOTICE.md](NOTICE.md).
