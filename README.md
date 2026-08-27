# DMC Native Reader

Native Android reader/viewer for Devil May Cry resource files: DMC3 `SCM`, `MOD`, `HITS`, stage `.txt` and `.index`.

## Current scope

- Built-in DMC file browser that finds `.scm` / `.mod` / `.hits` / `.ukn` / `.index` (and qualifying `.txt`) without any file manager.
- Android file routing for `.scm` / `.mod`, including Samsung My Files compatibility paths.
- Dedicated exported `DmcOpenActivity` system-open entry point.
- Exact magic probing (`SCM ` / `MOD ` / `HITS`) with fail-closed rejection of unrelated files.
- HITS collision decoding: bounded 0x44 header, 3-D spatial grid, 0x38 triangle/plane records.
- Text families: DMC3 stage `.txt` lexing and `.index` manifest parsing, shown in a text view.
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

`Browse DMC files` on the main screen scans for `.scm` / `.mod` / `.hits` /
`.ukn` / `.index` itself and opens the tapped result directly. It needs no file manager cooperation, and it
offers two access modes:

- **Grant file access** — Android's *All files access* (`MANAGE_EXTERNAL_STORAGE`).
  DMC resources are ordinary non-media files, so `MediaStore` cannot see them and
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

## HITS collision resources

v9 adds DMC3 `HITS` — the stage collision resource: a bounded `0x44` header, a
3-D spatial acceleration grid, and fixed `0x38` triangle/plane records.

Structural authority is `VrUaCom/dmc-rengine-cpp` (`docs/formats/hits.md`,
`src/formats/hits.cpp`). The Android decoder is a port of that parser, checked
against the same fixture layout the authority is tested with.

Two deliberate differences from the authority, both because this is a viewer
rather than a validator:

- **Cell-table defects are reported, not fatal.** By the time the grid walk
  runs, the magic, the bounded header and the whole triangle array have already
  validated, so a defect there is surfaced in the status line instead of hiding
  geometry that decoded cleanly. The authority treats the same defect as a hard
  error.
- **Two extra bounds.** The grid cell count is built with an explicit overflow
  guard (three `u32` dimensions can exceed 64 bits), and the cell walk carries a
  work budget, since this parser runs on a phone against files of unknown
  provenance.

`raw_flags` is decoded but not interpreted: its bit-level semantics remain
evidence-gated upstream.

`.ukn` is recognised because a HITS resource is routinely shipped under that
name. Recognition is by the four-byte `HITS` magic, never by extension — the
superseded five-byte `HITS$` reading is not accepted, and a `.ukn` holding
anything else is still rejected.

## Text resources

v10 adds the two DMC3 text families. Neither is geometry, so a decoded text
resource opens in a scrollable monospace view instead of the 3D view; a result
carries either a mesh or text, never both.

### Stage `.txt`

Structural authority: `src/formats/stage_txt.cpp`. A tokenized configuration
text with `#SET` directives, `DOOR` / `BOXIN` / `NEXTROOM` keywords, the
stage-set values (`DUMMY`, `STAY`, `BREAK`, `ORBREAK`, `SEAL`, `SWITCH`,
`YURE`), `//` and `/* */` comments, quoted strings and numbers. Keywords are
matched case-insensitively. NUL bytes disqualify the resource as text.

The status line reports the token census: `tokens= set= door= boxin= nextroom=
stageSet= ident= num= str=`, with any lexical defect in brackets.

### `.index`

Structural authority: `docs/formats/pnst-readonly-parser.md` and
`LooseContainerListPolicy`. A line-based extraction/naming manifest: `/` opens a
comment, blank lines are skipped, `dummy` marks a declared sparse slot, and the
file may open with a magic directive line — the real corpus uses the literal
line `PNST`.

That leading `PNST` is textual metadata and is never read as binary PNST
container magic; the authority is explicit that misclassifying it would turn
text manifests into fake containers. The status line states the boundary:
`.index` is metadata, not a container and not runtime lookup authority.

### Why `.txt` is not a system route

`.txt` is registered nowhere in the manifest. Claiming it would put this app in
the Android chooser for every text file on the device, which is not a trade the
reader should make for one game format. A `.txt` still opens when it arrives
through any route, and the built-in browser admits one only after sniffing a
bounded prefix for DMC3 stage keywords — the extension alone is not evidence.
`.index` is specific enough to register normally.

## iOS

An iPhone/iPad target lives in `ios/`. It shares the decoder, probe and CPU
renderer with the Android app **verbatim** — only `app_native.cpp` (JNI) is
platform-specific, so `ios/DMCReader/Bridge/DmcBridge.mm` replaces it and there
is exactly one parser for both platforms rather than a fork.

- SwiftUI screen with the same two content modes: mesh rendering with drag and
  pinch mapped onto the shared yaw/pitch/zoom state, or a scrollable monospace
  view for stage `.txt` and `.index`.
- `Info.plist` exports UTIs for `scm`, `mod`, `hits`, `index` and `ukn`, plus
  `LSSupportsOpeningDocumentsInPlace` and `UIFileSharingEnabled`. This is the
  iOS counterpart of the Android intent-routing work.
- `public.plain-text` is deliberately not claimed, matching the Android refusal
  to register `.txt`: it would put the reader in the "Open with" sheet for
  every text file on the device. A stage `.txt` still opens through the in-app
  picker, which is user-initiated. CI asserts its absence.
- The project is generated from `ios/project.yml` with XcodeGen rather than a
  checked-in `.pbxproj`.

### Building it

```sh
brew install xcodegen
cd ios && xcodegen generate
open DMCReader.xcodeproj
```

Then pick a signing team in Signing & Capabilities and run on a device. A free
Apple ID works; the installed app expires after 7 days and must be reinstalled.
A paid Apple Developer account extends that to a year.

Verified in CI on `macos-latest`: the shared decoder tests run under Apple
clang, the app builds for the iOS Simulator (which needs no signing identity),
and the compiled bundle's identity, version and declared document types are
checked. **No `.ipa` is produced** — signing requires an Apple Developer
identity that CI does not hold, so installing on a device means building
locally on a Mac.

## v6 package identity

Physical-device testing exposed a signer mismatch between early v2/v3 test APKs and the canonical v4+ signer. Android correctly rejects an update when the same package name is signed by a different certificate.

v6 therefore establishes the permanent test package identity:

`com.dmcrengine.nativereader`

It can install alongside the legacy test package, so the user does not need to remove the old installation just to test v6. Future v6+ test builds must preserve this applicationId and the canonical test signer.

Current milestone:

- versionCode: `11`
- versionName: `0.10.1-window-insets`
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

Verified v8 CI build (run `33017225523`, commit `7dc620f`):

- package identity: PASS (`com.dmcrengine.nativereader`, versionCode `8`)
- system handler routing: PASS
- `DmcBrowserActivity` + `MANAGE_EXTERNAL_STORAGE` in compiled manifest: PASS
- APK Signature Scheme v2: PASS
- APK Signature Scheme v3: PASS
- signer SHA-256: `f483539463f89dd957a8f7c68a3bb75da17450163f2e8767b4c47d5f1899adac`
- APK SHA-256: `3c09e605a07964f609c8ca8cc507670d992e12392f48071480f11664481d6aee`

Previous verified v6 CI build (PR #4 / run `33007067465`):

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

Current milestone: **v10 text resources + HITS collision decoding + built-in DMC browser + confirmed Samsung routing**.

Samsung My Files routing is confirmed working on v8: tapping a real `.scm` /
`.mod` opens the app instead of the "Search in Play Store?" dialog that had
blocked every build since v2. v8 also removes the hard dependency on the OEM
file manager entirely — `Browse DMC files` reaches the same decoder/renderer
path without one.

Decoding and rendering are confirmed on a physical device: a real `st001.scm`
opened from the file manager decodes to 23,049 vertices / 14,261 triangles and
renders. See `docs/STATUS.md`.

Rotate and wireframe are confirmed on device too, and a real external
`ACTION_VIEW` intent with an empty MIME type has been captured — the untyped
`content://` route is what carries it.

Next verification boundary: the two built-in browser sources, and real shipped
`HITS`, stage `.txt` and `.index` files (those decoders are verified against
authority-shaped fixtures, not against game files).

See `docs/STATUS.md` for the exact evidence boundary and remaining work.
