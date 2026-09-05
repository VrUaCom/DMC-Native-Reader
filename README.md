# DMC Native Reader

Native Android reader/viewer for Devil May Cry resource files. This repository is the Android product. `VrUaCom/dmc-rengine-cpp` remains the canonical reverse/evidence source for DMC3 HD format semantics.

## Current milestone — v9 explicit family modules

- versionCode: `9`
- versionName: `0.9.0-explicit-family-modules`
- applicationId: `com.dmcrengine.nativereader`
- ABI: `arm64-v8a`
- Android compile/target SDK: `36`
- minSdk: `26`
- NDK: `28.2.13676358`
- CMake: `3.22.1`

The package id and canonical development signer remain unchanged, so v9 is intended to install as an update over v8.

## Architecture

The production native path is:

`probe -> NativeModuleRegistry -> explicit NativeModule -> format runner`

There is no central family `if/else` decode dispatcher and no wildcard structural fallback.

`NativeModule` owns:

- stable module id;
- resource family;
- Native Reader `Format` authority;
- module kind;
- renderability;
- runner contract.

The runner receives its owning module contract, which lets multiple families share safe implementation helpers without losing family-specific identity or traceability.

## Registry completeness

The current catalog contains **71 unique recognized families** and the registry contains **71 explicit module contracts**.

### Promoted readers

These families have dedicated semantic/structural readers:

- SCM — corpus-backed mesh reader + scene transform adapter;
- MOD — corpus-backed mesh reader + model adapter;
- HITS — collision mesh reader;
- TXT — stage text lexer;
- `.index` — textual manifest reader;
- DDS — bounded DXT1/DXT5 full-mip validation;
- PTX — bundle reader with bounded DDS child validation;
- DCA — `0x10` header + `0x410` record envelope;
- LIG / LIG2 — bounded lighting record envelopes;
- PAC / PNST — relative-slot container inspection;
- NBZ — top-level volume inspection boundary;
- EFM / MRP / SHW — explicit evidence-gated partial family adapters.

### Recognition-only modules

The remaining 55 known families have explicit `Recognition` modules rather than falling through a wildcard. They open as inspection sessions, preserve the catalog evidence/support metadata, and expose the semantic decoder as `[TODO]` instead of fabricating a schema.

Representative families include PACK, AFS namespace, TIM2, PTZ, SEF/EFE/EFW, MOT variants, MCV, CAM, HID variants, TSC, stage placement families, audio/bank families, media-capability families, saves, legacy UI resources, EventTbl and SPUMAPDT.

Unknown/unmapped families have no module and are rejected.

## Evidence policy

Recognition does not imply a recovered binary schema. Native Reader preserves the distinction between:

- content-confirmed identity;
- filename/extension identity;
- structural decoding;
- mesh decoding;
- partial/evidence-gated support;
- recognition-only support.

Incomplete families must not reuse SCM/MOD layouts or invent geometry, offsets, names or semantics.

## Android behavior

- read-only file descriptors;
- 512 MiB native mapping cap;
- concrete exported `DmcOpenActivity` for OEM/Samsung routing;
- typed/untyped `content://` and `file://` fallbacks;
- distinctive DMC extension routes;
- explicit DDS/PTX routing;
- non-renderable sessions never display a stale previous mesh.

## CI gates

GitHub Actions verifies:

1. host C++ registry regression;
2. exactly 71 explicit registry entries;
3. no module for an unknown family;
4. promoted synthetic valid/invalid cases;
5. representative recognition-only module traces;
6. Android NDK/ARM64 build;
7. APK ZIP/classes/native-library integrity;
8. application id and versionCode/versionName;
9. compiled explicit module ids in `libdmcviewer.so`;
10. absence of `formats.generic.structural-inspector`;
11. canonical development signer;
12. APK SHA-256 evidence.

See `docs/STATUS.md` for the current evidence boundary and remaining reverse work.
