# DMC Native Reader — Status

## Current milestone

v0.8.0-builtin-browser

Canonical repository: `VrUaCom/DMC-Native-Reader`.

## v8 file-access correction

Reported symptom: tapping `.scm` / `.mod` in the device file manager does not
open the app. v7 had already exhausted useful APK-level intent registration,
so v8 stops treating the OEM file manager as the only way in and adds
`DmcBrowserActivity`, a built-in browser that locates DMC resources itself
through either all-files access or a persisted SAF tree.

System routing is retained and gained one non-duplicate route (`pathSuffix`,
API 31+, which matches decoded paths and therefore survives encoded SAF
document paths).

See `docs/SAMSUNG_MY_FILES_BOUNDARY.md` for the full reasoning.

## Proven in source / CI boundary

- Exact 4-byte DMC resource probes: `SCM ` and `MOD `.
- Bounded read-only native decoding path.
- SCM/MOD -> normalized static Mesh.
- Development corpus validation: 74 SCM + 90 MOD = 164/164 decoded.
- 300,466 vertices and 192,413 triangles materialized in that corpus pass.
- CPU 3D rendering, rotate, pinch zoom, reset and wireframe.
- JNI bridge and read-only Android file-descriptor path.
- Dedicated exported `DmcOpenActivity` system-open entry point.
- Built-in `DmcBrowserActivity` with filesystem-scan and SAF-tree sources.
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

1. Install v8 over the existing v6/v7 install.
2. Confirm installation succeeds.
3. Launch `DMC Native Reader v8` directly once and capture the route self-test
   and `system MIME: mod=... scm=...`.
4. Open `Browse DMC files`, then either grant all-files access or pick the
   folder holding the DMC resources.
5. Confirm the real `.scm` / `.mod` files are listed.
6. Tap one and confirm the native decoder accepts it.
7. Confirm non-empty real geometry renders.
8. Confirm rotate, pinch zoom, reset and wireframe work.
9. Separately, tap a real `.mod` / `.scm` in Samsung My Files and capture the
   incoming Intent diagnostics (`action`, `type`, `scheme`, `categories`,
   `flags`) if it routes at all.

A failure at any step must be classified at the exact boundary: package
installation, storage access, Android resolver, provider/URI access, native
probe/decode, or renderer. Steps 4-8 are the milestone acceptance path; step 9
is diagnostic only.

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
