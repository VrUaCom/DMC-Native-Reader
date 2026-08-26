# DMC Native Reader

Native Android reader/viewer for Devil May Cry resource files, starting with DMC3 `SCM` and `MOD`.

## Current scope

- Android file routing for `.scm` / `.mod`, including Samsung My Files compatibility paths.
- Exact magic probing (`SCM ` / `MOD `) with fail-closed rejection of unrelated files.
- Native C++ bounded binary reader and corpus-backed SCM/MOD mesh decoder.
- JNI bridge using read-only file descriptors.
- Normalized mesh representation.
- CPU 3D renderer with rotate, pinch zoom, reset and wireframe.
- Android intent diagnostics and PackageManager self-test for routing failures.
- Standalone CI build and compiled-manifest/signature verification.

## Evidence status

The decoder was validated against the available project corpus used during development:

- SCM: 74/74 decoded
- MOD: 90/90 decoded
- Total: 164/164 decoded
- 300,466 vertices materialized
- 192,413 triangles materialized
- No index out-of-bounds observed in the corpus pass

This does **not** claim that all opaque format semantics are reversed. In particular, full `triCmd` opcode semantics, textures/materials and exact MOD skeletal skinning remain separate reverse-engineering boundaries.

## Android routing v4

The v4 routing pass is designed for Samsung/Android providers that may supply unknown DMC files with unexpected or null MIME types. The compiled APK is required to contain routes covering:

- `ACTION_VIEW`
- `ACTION_EDIT`
- `CATEGORY_DEFAULT`
- `CATEGORY_BROWSABLE`
- `CATEGORY_OPENABLE`
- canonical DMC MIME types
- `application/octet-stream`
- legacy MOD audio MIME variants
- `*/*`
- `content://`
- `file://`
- `ACTION_SEND` fallback

Native decoding remains fail-closed, so broad Android routing does not make unrelated files parse as DMC resources.

## Build

This repository is the canonical build host. It is a standalone Android Gradle project with a native C++ module. CI installs the Android SDK/NDK/CMake toolchain, builds the ARM64 APK, verifies the compiled binary manifest and checks the APK signature.

Toolchain baseline:

- Android compile/target SDK: 36
- minSdk: 26
- Android NDK: 28.2.13676358
- CMake: 3.22.1
- Gradle: 9.5.0 in CI
- ABI: arm64-v8a

## Test signing

`keys/dmc-native-reader-test.jks` is intentionally committed as a **disposable development/test key**. It exists only so successive test APKs remain install-compatible with the v4 build already distributed for device testing.

It is not a production credential and must never be used as a production/release signing identity. A future production release must use a separate protected signing key supplied outside Git history.

Current test certificate SHA-256 fingerprint:

`f483539463f89dd957a8f7c68a3bb75da17450163f2e8767b4c47d5f1899adac`

## Status

Current milestone: v4 Samsung routing + real SCM/MOD static geometry preview.

Next verification boundary: physical-device test from Samsung My Files -> Android intent resolution -> DMC Native Reader -> native decoder -> real 3D viewer.

See `docs/STATUS.md` for the evidence boundary and remaining work.
