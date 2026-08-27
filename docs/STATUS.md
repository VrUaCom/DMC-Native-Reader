# DMC Native Reader — Status

## Current milestone

v0.10.0-text-resources

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

- Exact 4-byte DMC resource probes: `SCM `, `MOD ` and `HITS`.
- Extension-classified text families: stage `.txt` and `.index`.
- Bounded read-only native decoding path.
- SCM/MOD -> normalized static Mesh.
- HITS -> normalized collision triangle Mesh.
- Stage `.txt` / `.index` -> token/entry census plus full text.
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

## iOS target

Adds an iPhone/iPad app in `ios/` sharing the decoder, probe and CPU renderer
with Android verbatim; only the JNI entry point is platform-specific.

Proven in CI (`macos-latest`, run `33057431477`):

- shared decoder tests pass under Apple clang with `-Werror`;
- the app compiles and links for the iOS Simulator;
- compiled bundle identity `com.dmcrengine.nativereader`, version `0.10.0` /
  `10`, and exported UTIs for scm/mod/hits/index/ukn are present;
- `public.plain-text` is absent from the document types — asserted, not assumed.

Defect caught by that CI on the first run: the generated bundle had no
`CFBundleIdentifier`. An Xcode-IDE project inherits a template Info.plist that
maps the standard keys onto build settings; a project generated from
`project.yml` with `GENERATE_INFOPLIST_FILE` off supplies none of them, and
such a bundle does not install on a device. The keys are now declared
explicitly and CI asserts the substitutions happened.

Not claimed:

- no `.ipa`, and no device install: signing requires an Apple Developer
  identity this environment does not have;
- the app has never been run — CI builds for the simulator but does not launch
  it, so the SwiftUI screen, the gestures and the UIImage conversion are
  compile-verified only.

## v10 text resources

Adds the two DMC3 text families. Neither is geometry: a decoded text resource
opens in a scrollable monospace view, and a `DecodeResult` carries either a mesh
or text, never both.

Structural authority: `VrUaCom/dmc-rengine-cpp` — `src/formats/stage_txt.cpp`
for the stage lexer, `docs/formats/pnst-readonly-parser.md` and
`LooseContainerListPolicy` for the manifest grammar.

Proven in source / CI:

- 44 host checks run in CI before the APK build, compiled with
  `-Wall -Wextra -Wpedantic -Werror`:
  stage token census (`#SET`, `DOOR`, `BOXIN`, `NEXTROOM`, stage-set values,
  strings, numbers), case-insensitive keywords, block-comment skipping, and
  reporting of unterminated comments and strings; rejection of NUL bytes, empty
  and token-less input; `.index` grammar including the literal `PNST` text line,
  `#` directives, `/` comments, blank lines, `dummy` sparse slots, LF-only line
  endings, and rejection of binary and empty payloads.
- `application/vnd.dmc.index` and `.index` routing present in the compiled
  manifest.
- No `.txt` route present in the compiled manifest — asserted, not assumed.

Classification boundary:

- The text families have no binary magic, so path extension is the
  classification authority, exactly as the upstream classifier requires. The
  decoders still validate the payload as text, so a binary file under either
  extension is rejected.
- A `.index` whose first line is the literal `PNST` is textual metadata. It must
  never be promoted to a binary PNST container, and it is not runtime lookup
  authority.

Product decision:

- `.txt` is deliberately not registered as a system route. Claiming it would put
  this app in the Android chooser for every text file on the device. It still
  opens through any route that reaches the app, and the built-in browser admits
  a `.txt` only after sniffing a bounded prefix for DMC3 stage keywords.

Not claimed:

- stage `.txt` grammar above the token level (no parse tree, no semantics);
- `.index` linkage to the payloads it names;
- verification against real shipped `.txt` / `.index` files — only authority-
  shaped fixtures have been exercised so far.

## v9 HITS collision support

Adds DMC3 `HITS` stage collision resources to the reader.

Structural authority: `VrUaCom/dmc-rengine-cpp` — `docs/formats/hits.md` and
`src/formats/hits.cpp`. The Android decoder ports that parser: four-byte `HITS`
magic, bounded `0x44` header, relative offsets based at `+0x08`, 3-D spatial
grid with `-1` terminated per-cell lists, `0x38` triangle/plane records.

Proven in source / CI:

- 30 host checks over the authority's fixture layout, run in CI before the APK
  build, compiled with `-Wall -Wextra -Wpedantic -Werror`:
  exact vertex values; rejection of foreign magic (including the superseded
  five-byte `HITS$`), truncated headers, zero grid dimensions, non-positive
  cell sizes, triangle arrays past end of file, zero declared triangles,
  non-finite geometry and empty input; reporting of grid overflow, end-offset
  mismatch and unterminated cell lists.
- `.hits` and `.ukn` routing present in the compiled manifest.
- `application/vnd.dmc.hits` route present in the compiled manifest.

Deliberate deviations from the authority, both because this is a viewer:

- cell-table defects are reported in the status line rather than treated as
  fatal, since the magic, header and full triangle array have already validated
  by the time the grid is walked;
- the cell count is built with an explicit overflow guard and the cell walk
  carries a work budget, because this parser runs on a phone against files of
  unknown provenance.

Not claimed:

- `raw_flags` bit-level semantics (evidence-gated upstream);
- verification against real shipped `HITS` files — only the authority's fixture
  layout has been exercised so far.

## v8 device evidence

Physical Samsung test of the v8 APK:

- install over the previous build: PASS
- tapping a real `.mod` / `.scm` in Samsung My Files opens DMC Native Reader:
  **PASS**
- the My Files "Search in Play Store?" unsupported-file dialog no longer
  intercepts the tap

This closes the routing boundary that had been open since v2. See
`docs/SAMSUNG_MY_FILES_BOUNDARY.md` for the revised classification and for why
the `pathSuffix` filter is the probable, but not proven, cause.

Not yet reported back from the device, so still unverified:

- native decoder acceptance of the tapped file;
- non-empty geometry render;
- rotate / pinch zoom / reset / wireframe;
- the built-in browser paths (all-files scan and picked SAF folder).

## Verified v8 CI build

Workflow run `33017225523` on commit `7dc620f` passed the complete build
acceptance boundary:

- APK build: PASS
- package: `com.dmcrengine.nativereader`
- versionCode: `8`
- versionName: `0.8.0-builtin-browser`
- launchable activity: `com.dmcrengine.nativeviewer.MainActivity`
- `DmcOpenActivity`: present in compiled manifest
- `DmcBrowserActivity`: present in compiled manifest
- `MANAGE_EXTERNAL_STORAGE`: present in compiled manifest
- routing actions/categories/MIME/scheme checks: PASS
- APK Signature Scheme v2: PASS
- APK Signature Scheme v3: PASS
- signer SHA-256: `f483539463f89dd957a8f7c68a3bb75da17450163f2e8767b4c47d5f1899adac`
- APK SHA-256: `3c09e605a07964f609c8ca8cc507670d992e12392f48071480f11664481d6aee`

This is a build-acceptance result only. The v8 device-test boundary below is
still open.

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

1. Install v9 over the existing install.
2. Confirm installation succeeds.
3. Launch `DMC Native Reader v9` directly once and capture the route self-test
   and `system MIME: mod=... scm=...`.
4. Open `Browse DMC files`, then either grant all-files access or pick the
   folder holding the DMC resources.
5. Confirm the real `.scm` / `.mod` / `.hits` / `.ukn` files are listed.
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
