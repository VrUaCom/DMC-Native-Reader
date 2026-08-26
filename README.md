# DMC Native Reader

Native Android reader/viewer for Devil May Cry resource files, starting with DMC3 `SCM` and `MOD`.

## Current scope

- Built-in DMC file browser that finds `.scm` / `.mod` without any file manager.
- Android file routing for `.scm` / `.mod`, including Samsung My Files compatibility paths.
- Dedicated exported `DmcOpenActivity` system-open entry point.
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

## Opening a file

There are two independent ways in, because the first one is not fully under
this app's control.

### 1. Built-in browser (always available)

`Browse DMC files` on the main screen scans for `.scm` / `.mod` itself and
opens the tapped result directly. It needs no file manager cooperation, and it
offers two access modes:

- **Grant file access** — Android's *All files access* (`MANAGE_EXTERNAL_STORAGE`).
  SCM/MOD are ordinary non-media files, so `MediaStore` cannot see them and
  `READ_MEDIA_*` does not apply; all-files access is the only permission that
  allows a direct scan of storage for them. On API 29 and below the legacy
  `READ_EXTERNAL_STORAGE` permission is requested instead.
- **Pick folder** — a Storage Access Framework tree chosen once. This requires
  no permission at all, is persisted across launches, and is the recommended
  path if you prefer not to grant all-files access.

### 2. Tapping the file in a file manager

Handled by the exported `DmcOpenActivity`. This works with the stock Android
resolver and most third-party file managers, but an OEM file manager can
decline to consult the resolver at all for unknown extensions — see
`docs/SAMSUNG_MY_FILES_BOUNDARY.md`. That case is exactly what the built-in
browser exists for.

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

- versionCode: `8`
- versionName: `0.8.0-builtin-browser`
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

Current milestone: **v8 built-in DMC browser + hardened system routing + real SCM/MOD static geometry preview**.

v8 removes the hard dependency on the OEM file manager: if Samsung My Files
still refuses to route `.scm` / `.mod`, the file is reachable through
`Browse DMC files` instead, and the decoder/renderer path is identical.

Next verification boundary: physical Samsung install -> direct launch self-test
-> `Browse DMC files` -> native decoder -> real 3D viewer, and separately the
My Files -> Android intent resolution route.

See `docs/STATUS.md` for the exact evidence boundary and remaining work.
