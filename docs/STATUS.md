# DMC Native Reader — Status

Last updated: **2026-09-29**.

## Product target

DMC Native Reader currently targets resource data from **Devil May Cry 3: Special Edition** as distributed in **Devil May Cry HD Collection**.

## Accepted `main`

- current `main`: `52210874f74d5526c2f1bd9000aaeab4594a03de`
- Android product line: **v72/v73 runtime source under review**
- Android package: `com.dmcrengine.nativereader`
- Android ABI: `arm64-v8a`
- minSdk / targetSdk: `26 / 36`
- native product language: target-scoped **C++23**
- Android release tag: `android-v68-1.0.41`

The v68 line, post-v68 FXBANK/Visual-Info integration and the reviewed Phase-2
evidence/tooling are merged into `main`. The current Lady/LEDi/FX and
MotionScript continuation is kept in `NR-Luna-v73` until exact-head CI and
physical Android acceptance are complete.

Current v73 review head: `09c8dfedfd8eeb5975ed974cf27e145905ff181b`.
This head is a merge of the current core acceptance contract and the v73
Lady/effects continuation; it is the active integration surface.

## Phase 2 evidence integration

Phase 2 is already merged into `main` through merge commit
`bfdb99f2520b71d852bb299b5a07881131e80aa0`. Its product code and Spider/C++23
core are not a pending merge candidate.

The retained Phase-2 branch `phase2/consolidated` is an immutable historical
snapshot. The exact-head tooling remains active as regression infrastructure,
but it must target the reviewed current commit rather than the old branch.

The v73 runner now inventories the inherited Phase-2 tests plus the current
MotionScript/effects tests. Preflight is diagnostic only and does not replace
the exact-head build/evidence run.

## Branch architecture

- `main` — shared C++23 Native Reader core;
- `platform/android` — Android shell and APK integration;
- `platform/windows` — Windows shell and portable artifact integration;
- `platform/ios` — iOS shell integration;
- `NR-Luna-v73` — current Lady, FX and MotionScript/effects core continuation.

Historical feature, CI and phase branches remain available as source evidence;
cleanup must preserve their exact heads/tags before any retirement.

## Platform status

### Android — current public release

**v68 / 1.0.41**

The current Android release carries the v60–v68 development line, including expanded model/composite handling, animation and motion work, PAC-driven assembly, cloth/coat behavior, room backdrops, improved rendering, gesture controls and the broader native module registry now present in `main`.

See [`releases/android-v68-1.0.41.md`](releases/android-v68-1.0.41.md).

### Windows x64 — technical preview

**v1.0.0 Preview**

The currently published Windows artifact is an earlier technical preview. It provides portable read-only MOD / SCM / DDS / PTX viewing through the shared native architecture.

It is **not** yet a released Windows equivalent of Android v68. Windows parity/update work should be tracked separately and only promoted when a newer Windows artifact has been built, verified and published.

## Current native module registry

Current `main` registers:

- SCM
- MOD
- DDS
- PTX
- EventTbl
- PAC
- MOT
- PNST
- SHW
- TSC
- CLT
- EFM
- motion script
- collision shape
- collision index
- effect bank

A module being registered does not mean every semantic field of that format is fully reverse engineered. Unknown semantics remain explicitly unknown.

## Architecture boundary

```text
resource bytes
  -> bounded probe
  -> NativeModuleRegistry
  -> canonical native / DMC Rengine authority
  -> DMCNativeReader::Core
  -> session / inspection / rendering
  -> Android or Windows shell
```

DMC Native Reader remains **read-only**.

Editing, rebuilding and repacking belong to DMC Rengine / authoring tooling rather than Native Reader.

## Naming policy

Use the official names:

- **Devil May Cry HD Collection** — collection.
- **Devil May Cry 3: Special Edition** — game.
- **DMC3** — shorthand.

Do not use “Devil May Cry 3 HD Collection” as a product title.

## Immediate work

1. keep Android release documentation and source identity synchronized;
2. run exact-head validation for the current v73 review head;
3. complete Lady effects renderer/lifecycle coverage and physical Android acceptance;
4. bring the Windows implementation forward from the public v1.0.0 Preview baseline toward current shared-core parity;
3. keep platform-specific release claims scoped to artifacts that were actually built and published;
4. continue promoting deeper resource behavior only with bounded native parsing, evidence and regressions;
5. keep editing/writing outside Native Reader.
