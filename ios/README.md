# DMC Native Reader — iOS shell

A thin iPhone/iPad shell over `DMCNativeReader::Core`, the same Core the
Android JNI shell and the Win32 shell link. It owns only file transport,
presentation and gestures; parsing, capabilities (Spider Black Widow),
texture binding and rendering all stay native.

```text
SwiftUI (ContentView, ResourceStore)
  -> DmcBridge.mm      Objective-C++ type conversion only
  -> reader_bridge.cpp portable C++23 boundary, final catch-all (like JNI)
  -> DMCNativeReader::Core  built from app/src/main/cpp by build_core.sh
```

Supported, as on Android: MOD and SCM models (render, drag to rotate, pinch
to zoom, wireframe), DDS/TM2 texture preview, PTX texture banks as a child
browser, PTX/DDS texture companions attached to a model, and the Core's
session description and inspection tree. Not ported yet: multi-MOD
composition, per-part texture attach, UV gallery and PNG export.

## Build and run on a device

Requires a Mac with Xcode 16+, CMake and XcodeGen.

```sh
git submodule update --init
brew install cmake xcodegen
cd ios && xcodegen generate
open DMCReader.xcodeproj
```

Choose your team under Signing & Capabilities and Run. The first build also
builds the Core for the selected platform (`build_core.sh`, a pre-build
phase). A free Apple ID works; the installed app then expires after 7 days.

CI (`.github/workflows/ios.yml`) additionally produces an **unsigned** IPA as
a workflow artifact. It installs only through a tool that re-signs it with
your own Apple ID (AltStore, SideStore, Sideloadly).

## Verification

- `ios/tests` builds the exact bridge translation unit the app compiles
  against the real Core and drives it with fixtures borrowed from the
  canonical Core regressions: SCM/MOD render and wireframe, DDS preview, PTX
  child browsing and child lifetime, texture attach, and rejection of
  foreign, empty and null input.
  ```sh
  cmake -S ios/tests -B build-bridge-test && cmake --build build-bridge-test
  ctest --test-dir build-bridge-test --output-on-failure
  ```
  It is kept out of the canonical native test inventory in
  `app/src/main/cpp/CMakeLists.txt`, whose exact count is Phase-2 evidence.
- `ios/tests` also runs first in the iOS workflow, under Apple clang.
- The Swift and Objective-C++ layers compile only on macOS; they are covered
  by the workflow's simulator and device builds.
