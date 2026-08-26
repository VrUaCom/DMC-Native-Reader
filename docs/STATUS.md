# DMC Native Reader — Status

## Current milestone

v0.6.0-install-identity

Canonical repository: `VrUaCom/DMC-Native-Reader`.

## Proven in source / CI boundary

- Exact 4-byte DMC resource probes: `SCM ` and `MOD `.
- Bounded read-only native decoding path.
- SCM/MOD -> normalized static Mesh.
- Development corpus validation: 74 SCM + 90 MOD = 164/164 decoded.
- 300,466 vertices and 192,413 triangles materialized in that corpus pass.
- CPU 3D rendering, rotate, pinch zoom, reset and wireframe.
- JNI bridge and read-only Android file-descriptor path.
- Dedicated exported `DmcResourceOpenHandler` activity alias.
- Samsung-oriented Android routes covering typed/untyped `content://` and `file://`, broad MIME fallback, `OPENABLE`, VIEW/EDIT and SEND fallback.
- Runtime PackageManager routing self-test and incoming-intent diagnostics.
- ARM64 APK build configuration.
- Standalone GitHub Actions build in this repository.
- Compiled package identity verification with `aapt2 dump badging`.
- Compiled binary manifest routing verification with `aapt2 dump xmltree`.
- APK Signature Scheme v2 + v3 verification.

## v6 install-identity correction

Physical-device testing exposed an installation failure before app launch. The installation boundary was classified as an APK identity/signing conflict, not a decoder or Samsung routing failure.

Verified signer fingerprints:

- legacy v2/v3 test APK: `e909ea08a78e1dcdf9909bd2dd0ac68291fe05d8217035b8f33dd8516b2aa733`
- canonical v4+ test signer: `f483539463f89dd957a8f7c68a3bb75da17450163f2e8767b4c47d5f1899adac`

Android correctly refuses an in-place update when the package name is the same but the signing identity differs. The canonical repository does not contain the legacy signer private key, so v6 establishes a new permanent test application identity:

`com.dmcrengine.nativereader`

This allows v6 to install alongside a legacy v2/v3 installation without requiring its removal. Future v6+ test APKs must keep both this applicationId and the canonical test signer so upgrades remain compatible.

## Verified v6 CI build

PR #4 / workflow run `33007067465` passed the complete build acceptance boundary:

- APK build: PASS
- ZIP integrity: PASS
- `classes.dex`: present
- `lib/arm64-v8a/libdmcviewer.so`: present
- package: `com.dmcrengine.nativereader`
- versionCode: `6`
- versionName: `0.6.0-install-identity`
- launchable activity: `com.dmcrengine.nativeviewer.MainActivity`
- `DmcResourceOpenHandler`: present in compiled manifest
- canonical DMC MIME routes: present
- `application/octet-stream`: present
- `audio/x-mod`: present
- typed/untyped `content://` and `file://` routes: present
- APK Signature Scheme v2: PASS
- APK Signature Scheme v3: PASS
- signer SHA-256: `f483539463f89dd957a8f7c68a3bb75da17450163f2e8767b4c47d5f1899adac`
- APK SHA-256: `3f093f6f9088423753f32a0690de315f0b9e5ae0cb8c5703763b157bc55673d0`

## Not yet claimed as fully reversed

- Full SCM `triCmd` opcode semantics.
- Full textures/material system.
- Exact MOD skeletal skinning semantics.
- Generality beyond the observed SCM/MOD corpus/version families.

## Current device-test boundary

The next physical Samsung acceptance pass is:

1. Install v6 while the legacy test package may remain installed.
2. Confirm installation succeeds.
3. Launch `DMC Native Reader v6` directly once and capture the route self-test.
4. Tap a real `.mod` and `.scm` in Samsung My Files.
5. Confirm Android resolves `DMC Native Reader v6` instead of Play Store search.
6. Capture the actual incoming Intent diagnostics (`action`, `type`, `scheme`, `categories`, `flags`).
7. Confirm the native decoder accepts the real file.
8. Confirm non-empty real geometry renders.
9. Confirm rotate, pinch zoom, reset and wireframe work.

A failure at any step must be classified at the exact boundary: package installation, Android resolver, provider/URI access, native probe/decode, or renderer.

## Build acceptance

A CI build is accepted only if all of these pass:

- APK exists and ZIP integrity passes.
- `classes.dex` exists.
- `lib/arm64-v8a/libdmcviewer.so` exists.
- compiled package identity and version match the intended milestone.
- launchable activity is the real Java activity class.
- compiled binary manifest contains the dedicated handler and required routing actions/categories/MIME/schemes.
- APK signature verifies and matches the canonical test certificate fingerprint.
- APK SHA-256 is emitted as build evidence.
