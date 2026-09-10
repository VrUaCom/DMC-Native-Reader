# Changelog

This changelog distinguishes accepted `main` history from development candidates. Historical build/evidence documents remain useful provenance but are not current support claims.

## Unreleased — Native Reader 1.0 v26 candidate

**Status:** draft PR #32 on `feature/dds-ptx-v1-acceptance`; device acceptance pending; not yet part of accepted `main`.

### v25 — UV slot gallery

- UV inspection is grouped by canonical texture slot instead of overlaying all model UV triangles in one view;
- each texture slot gets its own gallery entry and zoomable UV map with triangle count;
- PTX and generated UV children share the same native gallery/session infrastructure;
- model texture binding remains the single required-slot validation authority;
- invalid/incomplete bindings fail closed instead of guessing slot ownership;
- all eight portable/native regressions for the v25 slice pass.

### v26 — focused tool information

- long-press UV shows texture slots and triangle counts;
- long-press wireframe/model structure shows objects, nested mesh groups and counts;
- long-press bones/hierarchy shows explicit parent relationships, roots and invalid/unknown references without fabricating hierarchy;
- hierarchy information availability is separated from spatial hierarchy authority;
- focused reports reuse the typed `InspectionDocument` path rather than reparsing model bytes;
- all nine portable/native regressions pass;
- verified candidate APK: versionName `1.0`, versionCode `26`, arm64-v8a, 597,665 bytes, 19 declared JNI exports, no Kotlin runtime;
- candidate APK SHA-256: `b80be422197ff8270f67049dbdd596603b0ebcf4f41884f6b22a5936fedf4596`.

Promotion remains blocked only by the required physical-device/corpus acceptance pass.

## Native Reader 1.0 v24 — accepted `main`

**Accepted:** 2026-09-10  
**Main commit:** `5a69a3cde2cd4af3534ad7056ea55b09f0e91659`  
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
