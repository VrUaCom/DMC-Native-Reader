# Native Reader 1.0 v25 — UV slot gallery

Base main: `5a69a3cde2cd4af3534ad7056ea55b09f0e91659` (user-merged v24).
Work branch: `feature/dds-ptx-v1-acceptance`.
ReaderCore gitlink stays `1fc62d3484251aa56b4a05415e10e9a7759d8982`.
No Rengine files or signing keys changed.

## Problem and behavior

The UV button previously rendered every mesh triangle together. MOD/SCM
materials using different texture slots consequently overlapped in one view.
The UV button now opens a native child-gallery session. Each tile represents
one canonical texture slot, labelled with its slot number and triangle count.
Multiple primitives using the same slot share a map; different slots are never
mixed. Selecting a tile opens only that map with pinch zoom and reset. Back
returns to the gallery, then to the original model with its attached PTX intact.
PTX attachment is not required to inspect UVs when the model binding is complete.
Incomplete or invalid bindings disable the UV action; no slot is guessed.
Overlaps within a single slot remain visible because they belong to the source UVs.

## Reuse and memory

- `model_texture_binding` remains the single validator/required-slot authority,
  reused by Black Widow, companion attachment and UV grouping.
- `uv_gallery` groups validated triangle indices by slot. A gallery owns one
  UV-coordinate snapshot; selected maps share it through immutable shared
  ownership. Positions, full scenes and texture pixels are not copied per map.
- `session_gallery` provides the same native count/title/preview/open-child
  contract for decoded PTX children and generated UV children. Existing PTX
  preview pixels are borrowed; UV thumbnails are generated on demand at 256².
- `ChildResourceBrowserView` is one Android GridView with recycled tiles for
  both galleries. It holds visible/recycled tile bitmaps instead of eagerly
  building every bitmap and widget. Captions identify entries in both galleries.
- Existing UV drawing and canvas allocation are reused by full-map rendering
  and thumbnails. Bounds use only the selected map's indices. Rendering stays
  direct C++; Spider owns typed state, including `UvMapView`.
- Java only transports handles and presents native state and gallery entries.
  No parser, slot validation, texture mapping or second executor was added.

## Verification

All eight portable CTest regressions pass. Coverage includes MOD/SCM parser-to-
session-to-gallery integration, sparse slots, shared slots, pixel witnesses for
map isolation, per-map bounds, preview/full-view equality, zoom, invalid child
indices and malformed bindings, shared ownership after parent destruction,
borrowed PTX previews, and unchanged textured rendering after UV navigation.
The existing DDS/PTX compatibility and renderer regressions also pass.

Clean Android build: Gradle 9.5.0 / JDK 17 / NDK 28.2.13676358.
`debugRuntimeClasspath`: No dependencies. The first incremental build retained
old Kotlin DEX output and was rejected by the APK gate. A clean build removed
that stale output; only the verified clean artifact is delivered.

| Measurement | v24 | v25 |
| --- | ---: | ---: |
| APK bytes | 560,703 | 590,107 |
| Uncompressed ZIP entry bytes | 1,725,803 | 1,821,971 |
| Native library bytes | 1,564,520 | 1,659,120 |
| DEX bytes | 149,020 | 150,588 |
| Public JNI exports | 18 | 18 |

APK growth: 29,404 bytes (5.24%). These are artifact sizes, not Android's
installed storage accounting. Installed v25 size requires a phone measurement.

Identity: versionName `1.0`, versionCode `25`, ABI `arm64-v8a`.
SHA-256: `9055ac6343a2bf0ceb6e85613f72cffa0cca1be1a412b697faddec06037d515d`.
Signer SHA-256: `f483539463f89dd957a8f7c68a3bb75da17450163f2e8767b4c47d5f1899adac`.
ZIP integrity, v2 signature, all declared JNI exports, module markers,
legacy compressed JNI packaging, `extractNativeLibs=true` and absence of
Kotlin runtime passed `tools/verify_device_apk.py`.

## Device acceptance pending

On Samsung, test MOD and SCM with corresponding PTX (especially em000/pl000):
open model, attach PTX, open UV gallery, select several different slots, pinch
zoom/reset, return to gallery and model, confirm textures remain correct.
Also open PTX directly and scroll/select/back through its shared gallery.
Confirm installed size in Android Settings. Host evidence does not substitute
for these device checks. Keep this change separate from main until acceptance.
