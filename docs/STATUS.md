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

Current v73 Pass 02 code checkpoint: `6cae2e1df5906236228a32d051358e71074c72b4`.
This checkpoint contains the Pass 01 runtime ownership/lifecycle base plus the
Pass 02 exact P/E/G/V child-graph presentation gate. Pass 01 remains the
historical ownership checkpoint; the active v73 implementation surface is now
the Pass 02 checkpoint.

## Phase 1 baseline review

Phase 1 was reviewed read-only against the current `NR-Luna-v73` source,
commit ancestry and project documentation before this status update.

- ✅ **Implementation baseline: CLOSED** — architecture/scope lock, C++23-first
  Native Reader boundary, canonical resource/session pipeline, raw MOT vs Script
  Play separation, generic Effect Profile Registry, unknown-profile isolation,
  nested graph ownership/copyability, exact resource gating and terminal
  lifecycle foundations are evidenced by code and reviewed refs.
- ✅ Regression coverage for the Phase 1 foundations is authored and registered
  in the current v73 CMake inventory.
- 🟡 **Execution evidence: OPEN** — exact-head configure/build/CTest has not been
  run for the current v73 candidate; no test PASS or APK/device PASS is claimed.
- ⚫ Full P/E/G/V renderer/update clocks, deterministic replay/reverse seek, JNI
  Effects ON/OFF, APK production evidence, physical Android acceptance and full
  grenade/world-collision lifecycle are Phase 2/acceptance work, not missing
  Phase 1 implementation.

Review record: [`docs/reviews/NR_LUNA_V73_PHASE1_REVIEW_2026-09-29.md`](reviews/NR_LUNA_V73_PHASE1_REVIEW_2026-09-29.md).

## Android acceptance — initial v73 device pass

The first physical test pass used the debug APK built from
`NR-Luna-v73`, source commit `55f874d575203792d86fd88cc49c3209b8db4f14`,
workflow run `36587531306`. The APK installed and passed the repository
APK verifier.

- ✅ APK installed without a crash.
- ✅ Opening through both available routes worked.
- ✅ Screen rotation from the external file manager route preserved the opened
  resource.
- ✅ MOD, SCM, DDS and PTX baseline opening passed in the first pass.
- ✅ Reopening another file did not retain stale effects from the previous file.
- 🔴 **Acceptance regression:** after opening `PL000`, then opening
  `EM028` through Native Reader and rotating the screen, the restored
  session shows `PL000`; current `EM028` disappears. The selected
  slot/costume also resets to the first entry in comparable cases.
- 🟡 Video evidence identifies the MotionScript case: in
  `em034.pac · assembled` / `Lady · costume 1`, the short yellow-white effect
  repeats around the weapon handle/body area rather than the muzzle during
  `S12` acts 5, 4 and 3 (`slot_0005.mot`, `slot_0004.mot`,
  `slot_0003.mot`). A separate rocket-like visual is briefly near-vertical.
  This is classified as an attachment/orientation regression.
- ✅ Exact trace is now closed on `NR-Luna-v73`: actions 3/4/5 resolve to
  `CEm034Shl02 -> V423` from `em034.pac` FXBANK slot 28; the visible
  presentable child is `E752`, with local `T=(60,0,0)`. The parent chain
  resolves through Lady slot20 node0, with active placement on body joint 9
  and local `T=(-8.4,-1.0,-1.3), Rz=pi`. The remaining implementation
  suspect is the Shl02 parent-basis/muzzle alignment, not the FXBANK ID. See
  [the detailed trace](reviews/NR_LUNA_V73_ANDROID_ACCEPTANCE_PASS01_2026-09-29.md).

Acceptance disposition: APK installation and baseline opening are green, but
rotation/state restoration is NO-GO for Android acceptance until the current
Native Reader session, selected resource and selected costume/slot survive
configuration change. No code was changed in this report.

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
