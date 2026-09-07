# DMC Native Reader for iOS — Preview

This preview keeps the historical `ios-unsigned-latest` release line but replaces the old pre-v1 reader with the current **DMC Native Reader** product and Architecture v2 core.

## Product identity

- name: **DMC Native Reader**
- iOS shell: SwiftUI + Objective-C++ bridge
- semantic/parser authority: shared C++20 Architecture v2 core
- version surface: v1.0-compatible preview
- supported promoted families: **MOD / SCM / DDS / PTX**

The iOS shell does not carry a second Swift/Objective-C format parser. It compiles the same registry, adapters, DMC Rengine vendor readers, texture module and CPU renderer used by the stable product core.

## Capabilities

- Files / Share-sheet opening;
- MOD and SCM 3D rendering with rotate, zoom and wireframe;
- typed Inspector output;
- DDS image preview;
- PTX child texture gallery;
- fail-closed handling for unsupported/unpromoted formats;
- read-only source handling.

## Installation status

The published `.ipa` is **unsigned**. It is a technical preview artifact, not an App Store package and not an Apple-signed distribution build.

It can be re-signed with a user's own Apple development identity using tools such as AltStore, SideStore or Sideloadly, or built directly from source in Xcode. A future signed distribution requires an Apple Developer signing/provisioning setup.

## Build from source

```bash
brew install xcodegen
cd ios
xcodegen generate
open DMCNativeReader.xcodeproj
```

Select your signing team in Xcode and run on a device.

## Historical note

The previous `DMCReader-unsigned.ipa` on this release was a pre-v1 experiment that exposed SCM/MOD plus retired HITS/TXT/index paths. The release tag is intentionally preserved, but the current preview must use the stable four-family Architecture v2 contract instead.
