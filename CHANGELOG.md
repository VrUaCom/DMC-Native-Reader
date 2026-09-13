# Changelog

This changelog distinguishes accepted `main` history from development candidates. Historical build/evidence documents remain useful provenance but are not current support claims.

## Unreleased — Native Reader 1.0.6 / v33 candidate

**Status:** draft PR #33 on `feature/png-export-multi-mod-v27`; exact-head build/device acceptance pending; not yet part of accepted `main`.

### Modular + Spider architecture

- production `NativeModuleRegistry` contains five promoted families: MOD / SCM / DDS / PTX / EventTbl;
- every promoted format route executes through Spider Crusader while canonical adapters/parsers remain format authorities;
- composition and PTX attachment now use `dmcresource::spider::actions` instead of JNI orchestration;
- Black Widow remains the native application-state/capability authority;
- Android JNI is transport/handle/Bitmap glue only;
- `DMCNativeReader::Core` remains the reusable portable C++20 product target;
- canonical architecture authority is `docs/MODULAR_SPIDER_V33.md`.

### Canonical Rengine / SCM

- ReaderCore pin advanced to `660cd29909863dac4f8980b12d070ac3afd3036f`, a direct descendant of SCM authority baseline `809824882c60487962e99ee41f16bca7e3ccbc83`;
- SCM retail versions 0.83 / 0.90 / 1.00 / 1.01 remain supported by the canonical parser contract;
- `header +0x13` remains `lighting_reference_node_index`;
- confirmed structural resource-code family domain includes 3 / 4 / 7 / 8;
- SCM regression covers hierarchy/order/object binding/world transform through final world-space render vertices, protecting stage placement such as `st002`;
- compatibility selector and confirmed material GIF packet authority remain regression-gated.

### Multi-MOD + shared PTX bank

- multi-select canonical MOD resources compose through Spider session actions;
- each `CompositePart` retains source-local scene/node/texture-slot authority;
- pre-attachment synthetic slot ranges keep different source namespaces explicit;
- shared PTX attachment unions required source-local slots, parses once and decodes each required slot once;
- all compatible parts point into one slot-indexed decoded texture bank, eliminating per-part RGBA copies for the same local slot;
- distinct PTX slot identities are never merged merely because their pixel payloads happen to match;
- explicit per-part PTX replacement remains available and preserves local-slot identity;
- `composite_mod_scene_test` uses the em028-style `001/004/005/006 + em028_000.ptx` layout and requires four decoded slots instead of the former synthetic ten-slot duplication.

### PNG export + direct Bitmap transport

- the shared reset control becomes `↓` only when native Black Widow exposes `CanExportPng`;
- UV gallery exports every texture-slot map as PNG to a system-selected folder;
- an opened UV map exports one 1024×1024 PNG;
- PTX gallery exports DDS children and individual PTX/DDS images through Android SAF;
- large PTX children can be lazily decoded from retained bounded encoded bytes;
- JNI writes native RGBA directly into reusable Java-owned `ARGB_8888` Bitmaps;
- legacy Java `int[]` frame transport remains forbidden;
- JNI resource operations fail closed on C++ exceptions.

### EventTbl / legacy texture routes

- EventTbl identity requires `EVT\0` bytes and is exposed as the canonical `EventTbl` registry family;
- EventTbl now has its own Spider Crusader execution regression;
- logical `.tm2` names with DMC descriptor + DDS bytes continue through the validated wrapped-DDS path rather than a fabricated Sony TIM2 decoder;
- PTX/DDS framing and model-texture authority are consumed from canonical DMC Rengine ReaderCore.

### Android modular packaging and size gates

- versionName / versionCode are `1.0.6` / `33`;
- APK contains exactly one native DSO: `lib/arm64-v8a/libdmcviewer.so`;
- `DMCNativeReader::Core` and `DMCRengine::ReaderCore` link statically into that one DSO;
- recovery `libdmcshim*`, `libdmccore00.so`, `dlopen` and `dlsym` paths are forbidden;
- `extractNativeLibs=false`; native DSO remains uncompressed for direct mmap;
- verifier checks 16 KiB APK ZIP data alignment **and** every ELF `PT_LOAD` alignment;
- verifier checks Java/JNI exact symbol parity and that JNI composition/PTX actions route through Spider;
- APK budget <= 8 MiB, native DSO <= 4 MiB, total Dex <= 1 MiB;
- old v32 recovery build with four native DSOs is explicitly rejected and must not be used as a build base.

### v33 regression cleanup

- fixed `spider_model_execution_test` after EventTbl became the fifth production module;
- fixed registry lookup from stale `EVT` to canonical `EventTbl`;
- fixed EventTbl inspection identity expectation;
- aligned `v1-hardening.yml` with v33 / 1.0.6 and added host CTest before APK packaging;
- added `spider_event_execution_test`;
- retired the stale v27 Spider contract in favor of `MODULAR_SPIDER_V33.md`.

### Current build boundary

No v33 APK is accepted yet. Both `ubuntu-latest` and a bounded `macos-15` probe have been observed failing before runner assignment with `runner_id=0` and no executed steps. Checkout, CMake, Gradle and tests therefore did not run in those jobs; these failures are infrastructure evidence, not green or red source evidence. The temporary macOS probe workflow was removed after confirming the account-level behavior.

PR #33 remains draft until a real exact-head clean build runs the full native suite, produces a verifier-clean ARM64 APK, and the Samsung device checklist is completed.

## Native Reader 1.0 v26 — accepted `main`

**Accepted:** 2026-09-10  
**Accepted main:** `0148f0bd1b384fa1d2b43124b88423fd7b66c379`  
**Merged through:** PR #32  
**versionName / versionCode:** `1.0` / `26`

### v25 — UV slot gallery

- UV inspection is grouped by canonical texture slot instead of overlaying all model UV triangles in one view;
- each texture slot gets its own gallery entry and zoomable UV map with triangle count;
- PTX and generated UV children share the same native gallery/session infrastructure;
- model texture binding remains the single required-slot validation authority;
- invalid/incomplete bindings fail closed instead of guessing slot ownership.

### v26 — focused tool information

- long-press UV shows texture slots and triangle counts;
- long-press wireframe/model structure shows objects, nested mesh groups and counts;
- long-press bones/hierarchy shows explicit parent relationships, roots and invalid/unknown references without fabricating hierarchy;
- hierarchy information availability is separated from spatial-render authority;
- focused reports reuse the typed `InspectionDocument` path rather than reparsing model bytes;
- verified candidate APK: versionName `1.0`, versionCode `26`, arm64-v8a, 597,665 bytes, 19 declared JNI exports, no Kotlin runtime;
- candidate APK SHA-256: `b80be422197ff8270f67049dbdd596603b0ebcf4f41884f6b22a5936fedf4596`.

### Acceptance

- the owner confirmed the required Samsung/device tests on 2026-09-10 and explicitly approved PR #32 for merge;
- the bounded v25/v26 regression and APK evidence was accepted with that physical-device confirmation;
- GitHub-hosted jobs on the accepted line can fail before running any step, so hosted CI is not claimed green.

See `docs/UV_GALLERY_V25.md` and `docs/TOOL_INSPECTION_V26.md`.

## Native Reader 1.0 v24 — accepted historical baseline

**Accepted:** 2026-09-10  
**Accepted v24 code baseline:** `5a69a3cde2cd4af3534ad7056ea55b09f0e91659`  
**versionName / versionCode:** `1.0` / `24`

### Size and module-boundary cleanup

- removed the unintended Kotlin runtime dependency from the Java-only Android shell;
- reduced the APK from 1,488,118 bytes (v23) to 560,703 bytes (v24), approximately 62.3%;
- physical Samsung installed-size report dropped from 6.27 MB to 2.32 MB;
- reduced the public native dynamic-symbol surface from 2,951 symbols to the 18 declared JNI entry points;
- moved portable session ownership out of JNI into `resource_session`;
- moved scene materialization out of rasterization into `scene_projection`;
- centralized shared vector math and resource limits;
- kept the production module registry exactly MOD / SCM / DDS / PTX.

### Acceptance

- seven local/native regressions passed;
- arm64 APK identity, ZIP, signing, module markers and JNI export gates passed;
- owner confirmed on Samsung that all four supported file families open and PTX model texture application works;
- GitHub-hosted jobs on the tested revision failed before executing any steps and produced no useful job logs, so CI is not claimed green.

See `docs/SIZE_AND_MODULES_V24.md`.

## Native Reader 1.0 v23 — PTX model texturing

- completed the shared DDS/PTX texture path over DMC Rengine read-side codec/framing authority;
- model + PTX companion attachment validates required canonical texture slots before applying textures;
- MOD/SCM UV streams remain typed canonical inputs;
- failed texture replacement preserves the previously valid companion state;
- seven local/native regressions and verified APK gates passed;
- physical-device acceptance was confirmed before promotion to `main`.

See `docs/PTX_MODEL_V23_EVIDENCE.md`.

## Native Reader v1 clean-core transition

The repository deliberately replaced the earlier broad multi-format/recognition surface with a four-module production core:

- MOD;
- SCM;
- DDS;
- PTX.

That four-module statement describes the historical v1 clean-core transition. The current v33 candidate promotes EventTbl as a fifth bounded production module. The old HITS/TXT/index/DCA/LIG/PAC/PNST/NBZ/partial-adapter surface remains excluded from the production registry/build and preserved as backlog/reference. Future families must be promoted individually through the current modular + Spider contract with canonical/evidence-backed authority and regressions.
