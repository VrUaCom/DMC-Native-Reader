# DMC Native Reader — Cross-platform contract

## Status

| Platform | Current state | Shell | Binary semantics |
| --- | --- | --- | --- |
| Android | **stable v1.0.0** | Kotlin/Java Android UI + JNI | C++20 Architecture v2 |
| iOS | **preview implementation** | SwiftUI + Objective-C++ | same C++20 Architecture v2 |
| Windows | **preview implementation** | native Win32/x64 | same C++20 Architecture v2 |
| Web | planned | browser UI | C++20 compiled to WebAssembly |

The stable product claim remains Android v1.0.0 until each additional platform passes its own build and real-corpus/device acceptance gates.

## One semantic core

The platform rule is strict:

```text
resource bytes
  -> NativeModuleRegistry
      -> MOD | SCM | DDS | PTX
          -> PipelineResult
              -> InspectionDocument
              -> RenderScene
              -> ImagePreview
              -> ChildResource[]
                  -> thin platform shell
```

Platform code may own file pickers, windows, gestures, menus, image presentation and operating-system integration. It may **not** own a competing MOD/SCM/DDS/PTX binary parser.

`PortableSession` is the platform-neutral C++20 owner introduced for non-Android shells. It runs the same `run_decode_pipeline()` authority, materializes render geometry once, exposes typed inspection and child/image contracts, and preserves fail-closed behavior.

## iOS preview

The old pre-v1 iOS source/release is not deleted. Instead, the current plan deliberately **replaces it in place** with the present product:

- release tag retained: `ios-unsigned-latest`;
- release title becomes: **DMC Native Reader for iOS — Preview**;
- old asset retired: `DMCReader-unsigned.ipa`;
- replacement asset: `DMC-Native-Reader-iOS-v1.0.0-unsigned.ipa`;
- old HITS/TXT/index product claims are removed;
- supported preview registry is MOD / SCM / DDS / PTX only.

The iOS shell lives under `ios/` and uses:

- SwiftUI for presentation;
- Files / Share-sheet document opening;
- Objective-C++ only as the ABI bridge into the C++20 core;
- CPU render output for MOD/SCM;
- generic `ImagePreview` for DDS;
- generic `ChildResource[]` for PTX galleries;
- generic flattened `InspectionDocument` presentation.

The CI artifact is unsigned by design. A signed/App Store/TestFlight distribution requires Apple Developer provisioning and must be treated as a separate signing gate.

## Windows preview

The Windows shell lives under `windows/` and is deliberately dependency-light:

- native Win32 application;
- x64 build via CMake + Visual Studio C++ tools;
- File > Open and drag-and-drop;
- CPU render output for MOD/SCM;
- DDS image preview;
- PTX child navigation;
- Inspector text generated from the same typed inspection tree;
- no Windows-specific format decoder.

The preview package is:

`DMC-Native-Reader-Windows-v1.0.0-preview.zip`

The moving preview release tag is intended to be:

`windows-preview-latest`

## Platform CI

`.github/workflows/platform-previews.yml` builds both preview shells independently:

1. macOS runner -> XcodeGen -> unsigned iOS build -> IPA artifact;
2. Windows runner -> CMake/MSVC -> x64 EXE -> ZIP artifact;
3. optional owner-triggered publish stage updates the retained iOS preview release and creates/updates the Windows preview release.

Publishing is permitted only after both platform jobs pass. This prevents the historical iOS release from being relabeled without a replacement binary.

## Acceptance before calling a platform stable

A platform may move from `preview` to `stable` only when all applicable gates pass:

- native build is reproducible;
- MOD opens and renders against accepted corpus files;
- SCM opens and renders against accepted corpus files;
- DDS produces validated image previews;
- PTX exposes validated child textures;
- unsupported/malformed resources fail closed;
- Inspector shows the same semantic facts as the Android/core path;
- no retired HITS/TXT/index/wildcard parser path is reintroduced;
- source files remain read-only;
- installer/signing identity is defined for that platform;
- release artifact and checksum are published and verified.

Until then, documentation and GitHub Releases must say **Preview** explicitly.
