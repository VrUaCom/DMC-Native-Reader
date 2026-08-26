# DMC Native Reader — Status

## Current milestone

v0.4.0-samsung-routing

The project has moved from the temporary `pocket-gdspace` build branch to the standalone canonical repository `VrUaCom/DMC-Native-Reader`.

## Proven in source / CI boundary

- Exact 4-byte DMC resource probes: `SCM ` and `MOD `.
- Bounded read-only native decoding path.
- SCM/MOD -> normalized static Mesh.
- Corpus validation used during development: 74 SCM + 90 MOD = 164/164 decoded.
- 300,466 vertices and 192,413 triangles materialized in that corpus pass.
- CPU 3D rendering, rotate, pinch zoom, reset and wireframe.
- JNI bridge and read-only Android file-descriptor path.
- Samsung-oriented Android intent routes covering typed and untyped `content://` / `file://`, broad MIME fallback, `OPENABLE`, VIEW/EDIT and SEND fallback.
- Runtime PackageManager routing self-test and incoming-intent diagnostics.
- ARM64 APK build configuration.
- Disposable stable test signing identity for install-over compatibility.

## Not yet claimed as fully reversed

- Full SCM `triCmd` opcode semantics.
- Full textures/material system.
- Exact MOD skeletal skinning semantics.
- Generality beyond the observed SCM/MOD corpus/version families.

## Current device-test boundary

The remaining immediate acceptance test is on the physical Samsung device:

1. Install the current v4+ APK.
2. Tap a real `.mod` or `.scm` in Samsung My Files.
3. Confirm Android resolves DMC Native Viewer instead of Play Store search.
4. Capture the app routing self-test and actual incoming Intent diagnostics.
5. Confirm the native decoder accepts the file.
6. Confirm non-empty real geometry is rendered and interactive controls work.

A failure at any step must be classified at the exact boundary: Android resolver, provider/URI access, native probe/decode, or renderer.

## Build acceptance

A CI build is accepted only if all of these pass:

- APK exists and ZIP integrity passes.
- `classes.dex` exists.
- `lib/arm64-v8a/libdmcviewer.so` exists.
- Compiled binary manifest contains required routing actions/categories/MIME/schemes.
- APK signature verifies and matches the development certificate fingerprint.
- APK SHA-256 is emitted as build evidence.
