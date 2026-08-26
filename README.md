# DMC Native Reader

Native Android reader/viewer for Devil May Cry resource files, starting with DMC3 `SCM` and `MOD`.

## Current scope

- Android file routing for `.scm` / `.mod`, including Samsung My Files compatibility paths.
- Dedicated exported `DmcResourceOpenHandler` system-open entry point.
- Exact magic probing (`SCM ` / `MOD `) with fail-closed rejection of unrelated files.
- Native C++ bounded binary reader and corpus-backed SCM/MOD mesh decoder.
- JNI bridge using read-only file descriptors.
- Normalized mesh representation.
- CPU 3D renderer with rotate, pinch zoom, reset and wireframe.
- Android intent diagnostics and PackageManager self-test for routing failures.
- Standalone CI build plus compiled package/manifest/signature verification.

## Evidence status

The decoder was validated against the available project corpus used during development:

- SCM: 74/74 decoded
- MOD: 90/90 decoded
- Total: 164/164 decoded
- 300,466 vertices materialized
- 192,413 triangles materialized
- No index out-of-bounds observed in the corpus pass

This does **not** claim that all opaque format semantics are reversed. Full `triCmd` opcode semantics, textures/materials and exact MOD skeletal skinning remain separate reverse-engineering boundaries.

## Android routing

The current system-open path is designed for Samsung/Android providers that may supply unknown DMC files with unexpected or null MIME types. The compiled APK is required to contain routes covering:

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

## v6 package identity

Physical-device testing exposed a signer mismatch between early v2/v3 test APKs and the canonical v4+ signer. Android correctly rejects an update when the same package name is signed by a different certificate.

v6 therefore establishes the permanent test package identity:

`com.dmcrengine.nativereader`

It can install alongside the legacy test package, so the user does not need to remove the old installation just to test v6. Future v6+ test builds must preserve this applicationId and the canonical test signer.

Current milestone:

- versionCode: `6`
- versionName: `0.6.0-install-identity`
- applicationId: `com.dmcrengine.nativereader`
- launchable Java activity: `com.dmcrengine.nativeviewer.MainActivity`

## Build

This repository is the canonical build host. CI installs the Android SDK/NDK/CMake toolchain, builds the ARM64 APK, verifies the compiled package identity and binary manifest, then verifies the APK signature.

Toolchain baseline:

- Android compile/target SDK: 36
- minSdk: 26
- Android NDK: 28.2.13676358
- CMake: 3.22.1
- Gradle: 9.5.0 in CI
- ABI: arm64-v8a

Verified v6 CI build (PR #4 / run `33007067465`):

- package identity: PASS
- system handler routing: PASS
- APK Signature Scheme v2: PASS
- APK Signature Scheme v3: PASS
- signer SHA-256: `f483539463f89dd957a8f7c68a3bb75da17450163f2e8767b4c47d5f1899adac`
- APK SHA-256: `3f093f6f9088423753f32a0690de315f0b9e5ae0cb8c5703763b157bc55673d0`

## Test signing

`keys/dmc-native-reader-test.jks` is intentionally a **disposable development/test key**. It is used so v6+ test APKs remain update-compatible with one another.

It is not a production credential and must never be used as a production/release signing identity. A future production release must use a separate protected signing key supplied outside Git history.

Canonical test certificate SHA-256 fingerprint:

`f483539463f89dd957a8f7c68a3bb75da17450163f2e8767b4c47d5f1899adac`

## Status

Current milestone: **v6 install identity + Samsung system handler + real SCM/MOD static geometry preview**.

Next verification boundary: physical Samsung install -> direct launch self-test -> Samsung My Files -> Android intent resolution -> DMC Native Reader -> native decoder -> real 3D viewer.

See `docs/STATUS.md` for the exact evidence boundary and remaining work.
