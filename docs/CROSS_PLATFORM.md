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
- ordinary x64 build via CMake + MSVC on Windows;
- CI-capable x86-64 cross-build via MinGW on `ubuntu-latest`;
- Unicode `wWinMain` entry point with the MinGW `-municode` startup path;
- self-contained MinGW preview linking where supported;
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

`.github/workflows/platform-previews.yml` is designed to prove the shells independently:

1. trivial `ubuntu-latest` runner probe;
2. macOS runner -> XcodeGen -> unsigned iOS build -> IPA artifact;
3. Ubuntu runner -> MinGW x86-64 -> Windows EXE -> ZIP artifact;
4. optional owner-triggered publish stage updates the retained iOS preview release and creates/updates the Windows preview release.

The Ubuntu probe is intentional: if it cannot execute even one `echo`, the failure is outside source compilation and platform build results must not be inferred from the run.

At the time of this public-prep pass, the current Actions attempts fail before any job step executes, including the Ubuntu probe. See [`CI_RUNNER_BLOCKER.md`](CI_RUNNER_BLOCKER.md). Publishing therefore remains disabled until GitHub actually executes the build jobs.

Publishing is permitted only after both platform builds pass. This prevents the historical iOS release from being relabeled without a replacement binary and prevents a Windows release from being created without a real executable.

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
