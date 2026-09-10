# DMC Native Reader — Status

## Product role

**Mission:** Make DMC resources feel like ordinary files.

DMC Native Reader is the viewing/accessibility product of the DMC Rengine tooling ecosystem. Its purpose is to open, recognize, inspect, visualize, navigate and explain promoted resources without requiring ordinary users to understand their binary formats.

The current stable implementation is Android. Native iOS and Windows shells are now implemented as **preview** paths over the same C++20 semantic core. Web remains planned through C++20/WebAssembly rather than a second JavaScript/TypeScript parser stack.

**DMC Rengine is the central decompilation/reimplementation engine and modding foundation.** Native Reader is one specialized product built on top of its capabilities. Pocket GDS is another specialized tool in the same ecosystem, focused on resource work and authoring workflows.

Native Reader is not the primary archive-management, editing or repacking workspace. Those responsibilities belong to DMC Rengine-backed authoring/resource-management tooling such as Pocket GDS and to the central writer/resource architecture exposed by DMC Rengine.

See [`PRODUCT_VISION.md`](PRODUCT_VISION.md) and [`CROSS_PLATFORM.md`](CROSS_PLATFORM.md).

## Current stable baseline

- release: `1.0.0`;
- versionCode: `20`;
- production package: `com.dmcrengine.nativereader`;
- stable shell: Android / `arm64-v8a`;
- production registry: MOD / SCM / DDS / PTX;
- repository: `VrUaCom/DMC-Native-Reader`;
- central engine/modding repository: `VrUaCom/dmc-rengine-cpp`;
- frozen stable ref: `baseline/v1.0.0`.

## Platform state

| Platform | Status | Implementation |
| --- | --- | --- |
| Android | **stable v1.0.0** | Android UI + JNI over Architecture v2 |
| iOS | **preview** | SwiftUI + Objective-C++ over `PortableSession` |
| Windows | **preview** | native Win32/x64 over `PortableSession` |
| Web | planned | browser shell over C++20/WebAssembly |

The iOS and Windows source exists, but those platforms are not called stable until their platform CI and real-corpus/device acceptance passes complete.

## Canonical release surface

Stable Android release page:

`https://github.com/VrUaCom/DMC-Native-Reader/releases/tag/v1.0.0`

Direct APK:

`https://github.com/VrUaCom/DMC-Native-Reader/releases/download/v1.0.0/DMC-Native-Reader-v1.0.0.apk`

Accepted APK SHA-256:

`a81ef5555ecc67e0213d2f1f6baa351609a659c5898ba8f71f81d2fc2cefc68c`

Pinned production certificate SHA-256:

`2d82bd3e77b2c1882d3f8143fe8760fc4c834aa65fc5e7b1b12082afcb7718d1`

The URLs above are the canonical stable distribution paths once the GitHub Release/tag is published and the accepted APK is attached.

Preview release lines:

- iOS: `ios-unsigned-latest` — retained and converted in place to **DMC Native Reader for iOS — Preview** after a successful replacement build;
- Windows: `windows-preview-latest` — created/updated only from a successful Windows preview build.

## Production architecture

```text
DMC Rengine capability / resource authority
  -> NativeModuleRegistry
      -> MOD | SCM | DDS | PTX
          -> Architecture v2 projection
              -> PipelineResult
                  -> Android JNI Session
                  -> PortableSession
                       -> iOS SwiftUI bridge
                       -> Windows Win32 shell
```

Current production registry size: **4**.

There is no wildcard fallback and no broad recognition-only catalog in the supported v1 product path. Files outside the four promoted families fail closed.

## Supported core

### MOD

- renderable;
- canonical DMC Rengine structural parser;
- `RenderScene` geometry;
- hierarchy/spatial projection where canonical authority is available;
- skin/weight inspection state;
- texture/material state;
- typed `InspectionDocument`.

### SCM

- renderable;
- canonical DMC Rengine structural parser;
- `RenderScene` geometry;
- scene hierarchy and transforms;
- texture/material state;
- typed `InspectionDocument`.

### DDS

- bounded DMC3 DXT1/DXT5 validation;
- complete mip-chain checks;
- generic image preview;
- malformed/overflow rejection.

### PTX

- bounded texture-bundle validation;
- descriptor/DDS size coherence;
- generic DDS child resources;
- real child image previews through the generic image contract;
- child navigation through generic session data.

## Not in the supported v1 registry

Earlier experiments included additional DMC families, but they are not part of the stable v1 product surface. HITS, TXT, `.index`, DCA, LIG/LIG2, PAC/PNST, NBZ, EFM/MRP/SHW and the broader recognition catalog must be re-promoted one by one through the same v2/canonical authority rules.

The old iOS prerelease previously advertised HITS/TXT/index support. That claim is retired. The iOS preview is being rebuilt on the same MOD/SCM/DDS/PTX registry as the current product.

Historical branches are development evidence, not supported release lines.

## CI gates

Normal release/public-prep CI proves the Android v1 baseline and central Architecture v2 invariants.

The cross-platform preview workflow adds two independent build gates:

1. macOS/XcodeGen unsigned iOS build;
2. Windows/MSVC x64 build.

Only after both preview builds succeed may the optional publish stage replace the old iOS preview asset and create/update the Windows preview package.

Current public-prep GitHub Actions attempts have recently failed before runner execution with zero executed job steps. This remains an execution/infrastructure gate and is not being treated as a passed test.

## Production signing

Official stable Android production package: `com.dmcrengine.nativereader`.

Android production signing is performed only through a protected release environment using secrets that are not committed or uploaded as artifacts by the current release workflow.

The accepted v1.0.0 Android production binary was signed and device-tested before public-prep documentation work.

iOS preview CI intentionally produces an unsigned IPA. A signed/TestFlight/App Store path requires an Apple Developer identity and provisioning profile and is a separate future signing gate.

Windows preview packaging currently produces an unsigned x64 ZIP/EXE. A future stable Windows line should define Authenticode signing/installer identity before being called stable.

## Accepted Android device boundary

The accepted Samsung/OEM behavior remains the practical stable v1 UI target:

- MOD opens/renders;
- SCM opens/renders;
- standalone DDS previews;
- PTX opens as a texture gallery;
- DDS child preview opens;
- parent navigation returns to the PTX session;
- malformed/unsupported inputs fail closed without stale geometry.

## Preview acceptance still required

Before iOS or Windows is promoted from preview to stable, each platform must independently prove:

- MOD open/render against accepted corpus files;
- SCM open/render against accepted corpus files;
- DDS preview;
- PTX child preview/navigation;
- malformed/unsupported fail-closed behavior;
- inspection parity with the central C++20 pipeline;
- reproducible packaging and defined signing/distribution identity.

## Ecosystem boundary

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

Native Reader should consume DMC Rengine capabilities, not fork them. If a recovered semantic, parser, writer or resource capability belongs in the central engine, it should be promoted there first and then exposed through the appropriate downstream tool.

## License

DMC Native Reader is **source-available** under the **DMC Native Reader Personal Non-Commercial License 1.0**.

Personal non-commercial use is allowed under the exact terms of `LICENSE`; third-party commercial use requires separate written authorization. Capcom Co., Ltd. and its controlled affiliates receive the rights defined by the Capcom Special Grant. Vendored third-party code remains under its own licenses, including the MIT-licensed DMC Rengine slice.

## Success metric

The primary Native Reader product metric is:

> **How many opaque DMC resource types have become directly understandable and viewable?**

The project should prefer adding honest, familiar representations for new resource families over adding unrelated editor features to the Reader itself.

## Next promotion rule

No historical family returns to `main` merely because old code exists. Each future format must enter through the same Architecture v2 module contracts and, wherever possible, reuse the corresponding DMC Rengine parser/source authority.

A promoted format must also define the natural user-facing representation it enables: model, scene, image, gallery, hierarchy, animation, graph, bounds/volume or another evidence-backed view.