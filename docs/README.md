# DMC Native Reader documentation

This directory mixes **current product specifications**, **accepted release evidence**, and **historical development evidence**. Read them according to the categories below rather than assuming every versioned document describes the current application.

## Current source of truth

1. `../README.md` — product overview and accepted/candidate split.
2. `STATUS.md` — exact accepted `main` baseline and active development candidate.
3. `ARCHITECTURE_V2.md` — current portable core/module/session architecture.
4. `ROADMAP.md` — completed phases and next work.
5. `RELEASE_GATES_V1.md` — promotion/acceptance rules for the v1 line.
6. `SPIDER_FAMILY.md` — Black Widow / Crusader / Tarantula responsibility contract.

When a statement conflicts with an old version-specific evidence file, the current accepted code/build configuration plus `STATUS.md` take precedence for **current product state**. Historical evidence still remains authoritative for what happened in that historical build.

## Accepted release evidence

- `SIZE_AND_MODULES_V24.md` — accepted v24 footprint/module/JNI evidence and Android device acceptance.
- `PTX_MODEL_V23_EVIDENCE.md` — v23 PTX/model-texture milestone evidence that fed the accepted v24 line.

These documents record exact historical artifacts; do not rewrite their hashes/sizes to match a later release.

## Historical development evidence

- `BUILD_EVIDENCE_V4.md` — standalone-repository migration-era v4 APK evidence; not current identity/support state.
- `ANDROID_FILE_MANAGER_BOUNDARY.md` — v6/v7 OEM routing investigation, now prefaced with the resolved v24 product status.
- `CI_PROBE.md` — hosted-runner/probe history and the current rule for classifying no-step Actions failures.

Historical version names/module counts/routing blockers must not be copied into current README/status/support claims without revalidation.

## Public/release administration

- `PUBLIC_RELEASE_CHECKLIST.md` — current private-to-public/source/distribution checklist.

Repository administration, licensing, visibility and production signing are separate from parser/reader acceptance.

## Active candidate documentation

The active `feature/dds-ptx-v1-acceptance` branch / draft PR #32 additionally contains:

- `UV_GALLERY_V25.md` — per-texture-slot UV gallery implementation/evidence;
- `TOOL_INSPECTION_V26.md` — long-press UV/object/mesh/hierarchy information implementation/evidence.

These files describe a development candidate and are intentionally not treated as accepted `main` behavior until physical-device acceptance and merge.

## Documentation maintenance rule

When a change is promoted to accepted `main`, update in the same bounded slice:

- root `README.md`;
- `STATUS.md`;
- `ROADMAP.md`;
- root `CHANGELOG.md`;
- architecture/release-gate docs when contracts change;
- exact evidence files only when a new artifact/evidence milestone exists.

Never replace historical evidence with current numbers; instead classify it clearly and add a new evidence document for the new build.
