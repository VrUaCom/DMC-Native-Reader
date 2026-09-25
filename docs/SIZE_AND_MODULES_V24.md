# Native Reader 1.0 v24 — size and module boundaries

Base: main `9bc31c7b6e6afdcbf7848b2f266cf71c3e7dbfec`.
Work reuses `feature/dds-ptx-v1-acceptance`. ReaderCore stays pinned to
`1fc62d3484251aa56b4a05415e10e9a7759d8982`; no Rengine files changed.

## Measured APK results

These are byte counts from the delivered v23 APK and the newly built v24 APK,
not estimates of Android Settings storage accounting.

| Measurement | v23 | v24 |
| --- | ---: | ---: |
| APK file | 1,488,118 | 560,703 |
| All ZIP entries, uncompressed | 4,579,460 | 1,725,803 |
| DEX payloads, uncompressed | 2,568,108 | 149,020 |
| libdmcviewer.so, uncompressed | 1,945,992 | 1,564,520 |
| Defined public dynamic symbols | 2,951 | 18 |

APK reduction: approximately 62.3%. The user's 6.27 MB figure refers to the
installed application. APK retention, extracted native code and Android's
compiled-code accounting mean this figure cannot be replaced with the ZIP
entry total. The user subsequently reported 2.32 MB installed for v24, down
from 6.27 MB (approximately 63.0%). This is user-reported device evidence,
not a measurement taken by the build environment.

## Confirmed dependency cause and fix

With `kotlin.stdlib.default.dependency=true`, Gradle's debugRuntimeClasspath
contains `org.jetbrains.kotlin:kotlin-stdlib:2.2.10` and its transitive
`org.jetbrains:annotations:13.0`. The project itself has Java application sources.
Setting `kotlin.stdlib.default.dependency=false` yields `No dependencies` in
that runtime configuration. APK verification confirms no Kotlin type references
in the resulting DEX payloads. No global dependency exclusion or parser removal
is used.

An anonymous linker version script exports only NativeBridge JNI entry points.
The APK gate verifies equality between its defined public exports and the 18
methods declared by NativeBridge, preserving the complete JNI contract.
Exceptions, native algorithms and the stable signing key remain enabled.

## Responsibility boundaries

| Module | Responsibility |
| --- | --- |
| app_native.cpp | Android FD mapping, JNI handles and value/pixel marshaling |
| resource_session | Portable session creation, cache preparation, description, companion publication and render requests |
| scene_projection | RenderScene materialization, triangle texture-slot projection and spatial hierarchy overlay construction |
| view_renderer | Rasterization, texture sampling and UV-view drawing on prepared geometry |
| vector_math | Shared neutral vector arithmetic used by MOD and SCM adapters |
| resource_limits | Shared resource, vertex and index limits |
| Black Widow | Typed UI/action state; the session calls its existing evaluator |
| TextureSet / texture_companion | Existing single DDS/PTX authority and required-slot decoding |

Raster hot loops remain direct C++. Session construction shares one implementation
for owned decode results and copied child resources. No second Spider executor,
parser, texture-binding algorithm or Java DMC logic was introduced.

## Validation

- GCC 13.3/C++20: all seven CTest regressions pass after the refactor.
- Session tests cover MOD and SCM opening, slot preservation, render output
  equality, four-slot PTX attachment, retaining the previous textures after a
  rejected replacement, and child lifetime after the parent session is closed.
- Gradle 9.5.0, JDK 17, NDK 28.2.13676358: arm64 debug APK built successfully.
- APK identity: versionName `1.0`, versionCode `24`.
- APK SHA-256: `b06d53bc8b50ccfe0a6e399b3fb779e27372311f08b858094620a3cf43408d6f`.
- Stable signer SHA-256:
  `f483539463f89dd957a8f7c68a3bb75da17450163f2e8767b4c47d5f1899adac`.
- ZIP integrity, v2 signature, all 18 JNI exports, module markers,
  `extractNativeLibs=true` and compressed legacy JNI packaging passed.

## User-confirmed v24 acceptance

On 2026-09-10, after receiving the v24 APK and test instructions, VrUaCom
reported: «Є все працює і вага 2.32», then explicitly requested that the latest
version be merged into main. Record this as user-confirmed device acceptance
and 2.32 MB installed size; no individual per-file test log was supplied.

The tested code is `98153b7a1130d4f87492f883154a8acd2367d459`.
This acceptance update changes documentation only. GitHub Actions on the tested
revision reported failed core, DDS/PTX and hardening jobs with no executed
steps or available job logs; these results do not demonstrate a code failure
and are not claimed green. Merge evidence is the seven local test passes,
verified APK and user-confirmed device acceptance above.
