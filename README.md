# DMC Native Reader

**DMC Native Reader** is a native 3D model viewer, texture viewer, animation player and game asset inspector for **Devil May Cry 3: Special Edition**, as distributed in **Devil May Cry HD Collection (DMC3 HD)**. It helps modders and researchers open and inspect supported DMC3 resources without editing original game files, using a reusable **C++23** core and canonical DMC Rengine read-side architecture.

Public packages exist for Android, Windows x64, Linux x86_64 and unsigned iOS, with different release and validation levels. Features vary by platform and build.

**[GitHub Releases](https://github.com/VrUaCom/DMC-Native-Reader/releases)** · **[Nexus Mods](https://www.nexusmods.com/devilmaycryhdcollection/mods/533)** · **[DMC Modding Hub](https://vruacom.github.io/dmc-rengine-cpp/)** · **[File format capabilities](https://vruacom.github.io/dmc-rengine-cpp/capabilities/)**

## Start with your task

The [DMC Modding Hub](https://vruacom.github.io/dmc-rengine-cpp/) connects the reader to practical, edition-specific workflows:

- [Start modding DMC3 in HD Collection](https://vruacom.github.io/dmc-rengine-cpp/guides/start-modding-dmc3/) — choose the correct edition and prepare your workspace.
- [Extract NBZ resources](https://vruacom.github.io/dmc-rengine-cpp/guides/extract-nbz/) — follow the archive-to-resource workflow before opening a supported leaf file.
- [Open and inspect MOD models](https://vruacom.github.io/dmc-rengine-cpp/guides/inspect-mod/) and [SCM scenes](https://vruacom.github.io/dmc-rengine-cpp/guides/inspect-scm/) — Windows v1.0.0 Preview walkthroughs.
- [Inspect DDS/PTX textures](https://vruacom.github.io/dmc-rengine-cpp/guides/extract-textures/) — understand texture resources and model relationships.

Use [Downloads](https://vruacom.github.io/dmc-rengine-cpp/downloads/) to select an exact public package and [Capabilities](https://vruacom.github.io/dmc-rengine-cpp/capabilities/) to check platform/version limits. Source capabilities, preview packages and device-test builds have separate evidence; an available download is not automatically a stable or device-accepted release.

Native Reader opens supported resources read-only. The NBZ guide describes a separate extraction workflow; it does not turn the reader into an NBZ unpacker, editor or repacker.

## Download DMC Native Reader

Choose a package for your operating system. Public artifacts, accepted source baselines and device-test builds are not interchangeable.

| Platform | Public package | Status |
| --- | --- | --- |
| Android (arm64-v8a) | [v151 / 1.0.124](https://github.com/VrUaCom/DMC-Native-Reader/releases/tag/android-v151-1.0.124) | Debug-signed device-test APK; emulator checked, physical-phone acceptance not reported for this build |
| Windows x64 | [v1.0.0 Preview](https://github.com/VrUaCom/DMC-Native-Reader/releases/tag/windows-v1.0.0-preview) | Portable technical preview |
| Linux x86_64 | [v1.0.0](https://github.com/VrUaCom/DMC-Native-Reader/releases/tag/linux-v1.0.0) | Initial standalone desktop release; portable tar.gz and SHA-256 manifest attached |
| iOS | [Unsigned IPA](https://github.com/VrUaCom/DMC-Native-Reader/releases/tag/ios-unsigned-latest) | Unsigned preview; requires signing before installation |

- Accepted Android v68 / 1.0.41 baseline: [release notes](docs/releases/android-v68-1.0.41.md).
- Windows is an earlier public snapshot; do not assume parity with the Android development line.
- The Linux release notes describe an AppImage, but the public release currently attaches the portable archive rather than a separate AppImage.
- No standalone macOS package is listed in GitHub Releases at the time of this update.

Release inventory checked 2026-11-28. See [all releases](https://github.com/VrUaCom/DMC-Native-Reader/releases) for subsequent builds and download checksums where provided.

## Quick start: open DMC3 game assets

1. Download the appropriate package from the table above and read its release notes.
2. Prepare resources from your own copy of Devil May Cry HD Collection. If you need to extract an NBZ archive first, follow the separate [NBZ extraction guide](https://vruacom.github.io/dmc-rengine-cpp/guides/extract-nbz/).
3. On Windows, extract the preview ZIP and launch the included application. Open a supported MOD, SCM, DDS or PTX resource. The optional `Register-OpenWith.ps1` adds per-user Explorer integration; `Unregister-OpenWith.ps1` reverses it.
4. On Android or Linux, follow the corresponding release instructions and select a supported resource. The iOS IPA needs signing before it can be installed.

For examples, see the [MOD model guide](https://vruacom.github.io/dmc-rengine-cpp/guides/inspect-mod/), [SCM scene guide](https://vruacom.github.io/dmc-rengine-cpp/guides/inspect-scm/) and [DDS/PTX texture guide](https://vruacom.github.io/dmc-rengine-cpp/guides/extract-textures/).

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

## DMC3 file formats and viewing capabilities

| Resource | Typical inspection workflow |
| --- | --- |
| `.mod` | Interactive 3D model and mesh inspection |
| `.scm` | 3D stage/scene geometry inspection |
| `.dds` | Direct texture preview |
| `.ptx` | Packed texture resources and texture preview |
| `.pac`, `.pnst` | Resource browsing and assembly where supported |
| `.mot` | Motion and animation inspection/playback where supported |
| `.shw`, `.tsc`, `.clt`, `.efm` | Specialized inspection via supported native modules |

This is a discovery summary, not a claim that every release supports every format or all semantic fields. Windows v1.0.0 Preview covers MOD, SCM, DDS and PTX; consult the exact build's documentation for other formats.

## Frequently asked questions

**How do I open DMC3 MOD files?** Use DMC Native Reader with a supported Devil May Cry 3 HD MOD resource. The Windows preview includes interactive model viewing, and the [MOD guide](https://vruacom.github.io/dmc-rengine-cpp/guides/inspect-mod/) explains the workflow. No Blender import is required for this standalone preview.

**How do I view DMC3 SCM scenes and PTX textures?** Open a supported SCM or PTX resource in the reader. See the [SCM guide](https://vruacom.github.io/dmc-rengine-cpp/guides/inspect-scm/) and [texture guide](https://vruacom.github.io/dmc-rengine-cpp/guides/extract-textures/).

**Can I play DMC3 animations or browse PAC files?** Supported development builds provide motion playback and PAC/PNST assembly or browsing. Check the specific release notes before downloading; the older Windows preview does not promise these newer features.

**Can Native Reader extract NBZ archives or edit game files?** No. It is a read-only viewer and inspector. Archive extraction is a separate workflow, and editing/repacking belongs to [DMC Rengine](https://github.com/VrUaCom/dmc-rengine-cpp).

**Where can I download DMC Native Reader?** Use the platform-specific [GitHub Releases](https://github.com/VrUaCom/DMC-Native-Reader/releases) or the [Nexus Mods page](https://www.nexusmods.com/devilmaycryhdcollection/mods/533), checking the version and release status.

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

Accepted baseline (not the newest public test APK):

- versionCode: **68**
- versionName: **1.0.41**
- ABI: **arm64-v8a**
- minSdk / targetSdk: **26 / 36**
- package: `com.dmcrengine.nativereader`
- native language: target-scoped **C++23**

See [Android v68 release notes](docs/releases/android-v68-1.0.41.md). The newer [v151 / 1.0.124 device-test APK](https://github.com/VrUaCom/DMC-Native-Reader/releases/tag/android-v151-1.0.124) is published from `NR-Luna-v73` and is not a production release.

## Windows

The public Windows build is currently **v1.0.0 Preview**.

It is a portable Windows x64 technical preview with read-only MOD / SCM / DDS / PTX viewing from the shared native architecture. Windows parity with the newer Android line is active work and should be described as such until a newer Windows release is published.

See the [Windows preview overview and practical guides](docs/releases/windows-v1.0.0-preview.md).

## Linux and iOS

Linux has an initial [x86_64 release](https://github.com/VrUaCom/DMC-Native-Reader/releases/tag/linux-v1.0.0) with a portable archive. The [unsigned iOS preview](https://github.com/VrUaCom/DMC-Native-Reader/releases/tag/ios-unsigned-latest) requires signing and lists SCM, MOD, HITS, stage TXT and index resources in its release notes. These packages have their own validation status; this README does not assert cross-platform feature parity.

## Related project

[DMC Rengine](https://github.com/VrUaCom/dmc-rengine-cpp) provides evidence-first reverse engineering, format research and guarded authoring infrastructure. DMC Native Reader is the standalone viewing and inspection product; the [DMC Modding Hub](https://vruacom.github.io/dmc-rengine-cpp/) connects users to practical guides and downloads.

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
