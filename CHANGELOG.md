# Changelog

This changelog distinguishes accepted `main` history from development candidates. Historical build/evidence documents remain useful provenance but are not current support claims.

## Unreleased — Native Reader 1.0 v27 candidate

**Status:** draft PR #33 on `feature/png-export-multi-mod-v27`; host/device acceptance pending; not yet part of accepted `main`.

### PNG export

- the shared `🔄` control becomes `↓` only when native Black Widow exposes `CanExportPng`;
- ordinary 3D MOD/SCM keeps `🔄` reset behavior;
- UV gallery exports every texture-slot map as a separate PNG to a system-selected folder;
- an opened UV map exports one 1024×1024 PNG through Android's create-document dialog;
- PTX gallery exports every DDS child as a separate PNG;
- an opened PTX/DDS image exports one PNG through the system save dialog;
- filenames preserve the root source identity plus native child/slot title;
- large PTX children outside the resident RGBA gallery budget can be lazily decoded from retained encoded DDS bytes, without removing the memory cap;
- all writes use Android Storage Access Framework rather than broad storage permissions.

### Multi-MOD scenes

- the open picker supports multi-select canonical MOD resources;
- each input remains a native `CompositePart` with source-local scene, nodes, mesh, texture-slot projection, name and PTX state;
- the flattened render projection safely offsets node/mesh references and remaps texture slots into non-overlapping global ranges;
- source coordinates are preserved; no weapon/cape/bone attachment is fabricated;
- PTX attachment requires explicit MOD-part selection and validates against that part's local binding;
- animation and physics are intentionally deferred.

### v27 evidence

- added `composite_mod_scene_test`;
- added `png_export_session_test`;
- extended Black Widow regression coverage for PNG export;
- versionName remains `1.0`; Android candidate versionCode is `27`;
- hosted PR jobs currently hit the repository's known pre-step infrastructure failure (`steps: []`), so no hosted green build is claimed yet.

See `docs/PNG_EXPORT_MULTI_MOD_V27.md`.

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
- hierarchy information availability is separated from spatial hierarchy authority;
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

The old HITS/TXT/index/DCA/LIG/PAC/PNST/NBZ/partial-adapter surface was removed from the production registry/build and preserved on `main.2` as backlog/reference. Future families must be promoted individually through Architecture v2 with canonical/evidence-backed authority and regressions.

## Historical development milestones

Earlier v4-v9 and pre-cleanup debug builds established Android routing, packaging, modular-reader and device-testing foundations. Their build identities, module counts and unresolved routing notes are historical evidence only; consult `docs/STATUS.md` for the current accepted state.
