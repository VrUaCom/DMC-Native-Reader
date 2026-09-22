# DMC Native Reader — Status

Last updated: 2026-09-22.

## Accepted baseline (`main`)

- current `main`: `aaf02dcf7992cb58b491c5172884d8c04018d1cc`
- device-confirmed product baseline: v26 line accepted through PR #32
- package: `com.dmcrengine.nativereader`
- ABI: `arm64-v8a`
- minSdk / targetSdk: `26 / 36`

The v26 line was physically tested on Samsung on 2026-09-10 and explicitly approved for promotion to `main`.

## v33 source state — integrated in `main`, execution evidence pending

- PR #33 source head: `6711f6af9edc30b00cca828a9d85e8dc9dce5047`
- PR #33 merge commit: `3e9197086e3037aa9a1d1c2e8e2d3fa7b320582f`
- current `main`: `aaf02dcf7992cb58b491c5172884d8c04018d1cc`
- versionName / versionCode: `1.0.6 / 33`
- Native Reader production language: **strict target-scoped ISO C++23**
- Spider product profile: **`spider.cpp23`** over Crusader
- Android native toolchain: **NDK r30 LTS `30.0.16248370`**
- build stack: **AGP 9.3.0 / Gradle 9.5.0 / JDK 17 / Android 36 / Build Tools 36.0.0 / Android CMake 3.22.1**
- production modules: **MOD / SCM / DDS / PTX / EventTbl**
- pinned Rengine ReaderCore gitlink: `caf445226c7d61841292384a10e93e4f58ae29f9`
- package pre-gate: **debug APK <=4 MiB; unsigned release APK <=4 MiB**
- final installed hard gate: **path-correct Android `StorageStats.getAppBytes()` <=4 MiB** on the exact production-signed artifact

Execution identity is no longer tied to closed PR #33. Before every canonical run, #36/#41 must nominate one exact reviewed 40-hex candidate HEAD. The same SHA is passed to bootstrap and the exact-head runner. If no newer reviewed PR is explicitly nominated, the default candidate is the current reviewed `main` HEAD.

## Current program state

Canonical flow:

`#35 -> #36 -> (#49 + #50 -> #51 -> #52) -> #41 -> #37 -> #42 -> #38 -> #43 -> #39 -> #44 -> #54 -> #55 -> #40 -> #45`

Current status:
- #35 — DONE;
- #49 — DONE source/static exception-boundary hardening;
- #50 — DONE `GO_WITH_CORRECTIONS`;
- #51 — source complete;
- #52 — DONE `ARCHITECTURE GO / EXECUTION_PENDING`;
- #36 — **SOURCE/STATIC READY, EXECUTION BLOCKED**;
- #47 — current blocker: real exact-head CMake/CTest/Android execution;
- #53 — owner action: provide one guarded Linux x64 execution route;
- #48 — waiting for real artifact/device evidence;
- #41 — blocked until #36 has one complete real evidence set;
- PR #33 — merged; merge status does **not** substitute for #36/#41 execution evidence;
- post-merge Android candidate stack #60 -> #62 -> #64 — draft/unmerged and under review #65;
- #54/#55 — future Android-shell/Java-retirement phase and review, blocked until #44 GO.

No Phase 3 implementation may start before #41 explicitly issues GO.

## Why Phase 2 is still blocked

GitHub-hosted attempts have failed **before runner assignment** with `runner_id=0` and no executed steps. The deliberate 2026-09-16 probe also executed zero steps.

Therefore:
- runner-less jobs are infrastructure evidence only;
- they do not prove source PASS or FAIL;
- do not repeatedly retry them while allocation/account state is unchanged;
- use an authorized Ubuntu/WSL2 x64 environment as the immediate fallback if hosted capacity remains unavailable.

Canonical direct path:

```bash
EXPECTED_HEAD="<reviewed candidate HEAD>"
test "$(git rev-parse HEAD)" = "$EXPECTED_HEAD"
bash tools/bootstrap_phase2_self_hosted_ubuntu.sh --expected-head "$EXPECTED_HEAD"
source build/phase2-self-hosted-env.sh
python3 tools/run_phase2_exact_head.py \
  --sdk "$ANDROID_SDK_ROOT" \
  --gradle "$GRADLE_HOME/bin/gradle" \
  --java "$JAVA_HOME/bin/java" \
  --expected-head "$EXPECTED_HEAD"
```

The run must use the exact reviewed candidate HEAD nominated by the active Phase/Review card, not a SHA copied from historical comments.

### Pre-provisioned / offline validation path

When the exact toolchain is already present on a Linux x64 host, bootstrap may run in validation-only mode with no apt/download/sdkmanager/submodule-fetch work:

```bash
export PHASE2_JAVA_HOME="/path/to/jdk-17"
export PHASE2_GRADLE_HOME="/path/to/gradle-9.5.0"
export PHASE2_ANDROID_SDK_ROOT="/path/to/android-sdk"

bash tools/bootstrap_phase2_self_hosted_ubuntu.sh \
  --expected-head "$EXPECTED_HEAD" \
  --preprovisioned

source build/phase2-self-hosted-env.sh
python3 tools/run_phase2_exact_head.py \
  --sdk "$ANDROID_SDK_ROOT" \
  --gradle "$GRADLE_HOME/bin/gradle" \
  --java "$JAVA_HOME/bin/java" \
  --expected-head "$EXPECTED_HEAD"
```

The supplied SDK must already contain:
- `platform-tools`;
- `platforms/android-36`;
- `build-tools/36.0.0`;
- `ndk/30.0.16248370`;
- `cmake/3.22.1`.

The pinned Rengine checkout must already exist at the repository gitlink and remain clean/read-only. `--preprovisioned` only validates and prepares the environment handoff; it does not itself produce Phase-2 PASS evidence. The canonical exact-head runner remains the evidence authority.

## Phase-2 exact-head evidence contract

`tools/run_phase2_exact_head.py` must actually execute and prove:
- clean worktree at start/end;
- exact `--expected-head` match;
- Rengine checkout == gitlink and clean/read-only before/after;
- final repository HEAD, Rengine gitlink and Rengine checkout still equal initial identities;
- `source_identity_stable=true`;
- real compiler/CMake/CTest/Gradle/JDK/NDK evidence;
- host CMake configure/build;
- **exact 23 registered canonical native tests**;
- JUnit contains the exact same 23 names;
- every testcase `status="run"`;
- executed=23, non_run=0, skipped=0, failure=0, error=0;
- clean Android debug + unsigned release builds;
- full APK verifier on **both** artifacts;
- exact APK SHA-256 and package metrics;
- retained CTest inventory/log/JUnit and exact APK artifacts.

Static inspection, YAML correctness or runner-less records cannot substitute for execution.

## Symmetric debug / unsigned-release package verification

Both Phase-2 APKs receive the same package/ABI/JNI/layout/dedup/size verification. Their intentional difference is signing policy.

### Debug APK
Must prove expected package/version/ABI, stable public test signer, valid APK v2, structural APK Signing Block, one runtime DSO, Java NativeBridge ↔ JNI parity, uncompressed 16 KiB-aligned DSO, ELF PT_LOAD >=16 KiB, hard size budgets and zero duplicate waste.

### Unsigned release APK
Must independently prove the same common contract plus:
- no valid signer;
- no APK Signing Block;
- no JAR signature material.

A failed signature verification alone is not proof of unsigned. Each verifier report is SHA-bound to its exact APK and the runner independently validates critical fields.

## C++23 / Rengine boundary

Native Reader targets require `cxx_std_23`, `CXX_STANDARD 23`, `CXX_STANDARD_REQUIRED ON`, `CXX_EXTENSIONS OFF`. Gradle does not own global `-std=c++*` policy.

Pinned `DMCRengine::ReaderCore` remains a read-only external target with target-scoped `cxx_std_20`.

Important C++23 facilities currently used/guarded: `std::expected`, `std::byteswap`, `std::to_underlying`.

## Exception boundary status

#49 is DONE on source/static review.

- allocating internal helpers expose truthful throwing-capable semantics unless caught locally;
- `run_decode_pipeline()` is Core catch-all;
- NativeModule `ModuleRun` and Crusader `OperationFn` remain noexcept/fail-closed;
- public Spider noexcept actions catch first-use Plan/helper allocation;
- JNI is final platform catch-all;
- no C++ exception crosses JNI;
- catch fallback does not require allocating diagnostics.

Real execution remains required before global acceptance.

## PTX RuntimeCompat status

Native Reader has one user-facing PTX route:

`TextureSlotFramingParser -> TextureSet -> DDS` + lazy Reader-owned `PtxRuntimeCompat`.

Accepted architecture:
- serialized framing/TextureSet/DDS is the only disk-format authority;
- RuntimeCompat is not a second parser/module;
- runtime state is lazy;
- imported behavior is bounded to reviewed Rengine `50d070e...`;
- represented storage `0xCB50`;
- placement/configure prefix `0xCB48`;
- final 8 bytes opaque tail;
- no inferred TextureSet-slot -> runtime-record materialization;
- palette/finalizer/cleanup/materializer/full lifecycle remain unknown/deferred.

#52 issued architecture GO but execution remains pending until exact-head CTest runs.

## Android installed-size evidence — v3

Hard maximum: **4,194,304 bytes** from Android `StorageStats.getAppBytes()` exact package-storage `code:` bytes.

`tools/measure_installed_footprint.py` is device evidence, not an APK verifier. Common contract:
- schema `dmc-native-reader.installed-footprint.v3`;
- fail closed if authoritative StorageStats unavailable;
- no `du` fallback;
- one installed `base.apk`;
- installed SHA == reviewed local APK SHA;
- `--user current` resolves once to one numeric user/profile;
- all package-path/StorageStats calls use that frozen user;
- requested/resolved/current user + device/build/version provenance recorded;
- final base.apk path + SHA are re-read and must equal initial installed identity;
- successful report has `installed_package_identity_stable=true`.

### ART policy is conditional on final #55 artifact

**Path B / any DEX present:** final Samsung run uses `--art-compile-mode speed`; record baseline StorageStats, run package-scoped full-AOT `cmd package compile -m speed -f <package>`, record stress StorageStats, and gate on `max(baseline, stress) <=4 MiB`.

**Path A / exact zero DEX:** exemption is allowed only when post-signing package verification proves zero DEX entries + `android:hasCode=false`. Then use `--art-compile-mode none`, authoritative baseline StorageStats <=4 MiB, and record ART stress as `NOT_APPLICABLE_ZERO_DEX`.

A minimal shim or any DEX is Path B and must receive speed stress. For both paths final #40/#45 requires explicit `--expected-apk-sha256` of the exact post-signing production APK.

If Samsung/AOSP does not expose authoritative package-storage `code:` bytes, final device gate is NO-GO until an equally authoritative separately reviewed path exists.

## Current Android architecture

```text
resource bytes
  -> NativeModuleRegistry / canonical adapters
  -> DMCNativeReader::Core (C++23)
     -> Session / WorkspaceGraph / composite state
     -> Spider C++ / Crusader actions
     -> Black Widow capability state
     -> renderer / inspection / UV / PNG
  -> JNI
  -> current Java Android transport/presentation shell
```

Android/Java currently owns transport/presentation/lifecycle only, not DMC semantics.

## Future Phase 6 — native Android shell / Java retirement

Begins only after Phase 5 stable bindings + #44 GO.

Preferred target: **0 authored Java/Kotlin application source + 0 app DEX** through supported NativeActivity/NDK APIs. Because current SAF flows use result-returning Intents, #54 must prove a supported public zero-DEX route. If required UX cannot be preserved, only #55-approved minimal framework callback shim may remain.

Forbidden: hidden/private APIs, reflection hacks, generated/obfuscated DEX merely to claim zero Java, or deleting required open/export UX.

## Final release artifact identity

Never conflate:
1. debug/device-test APK;
2. unsigned release APK — pre-signing structural evidence;
3. production-signed release APK — final Samsung/promotion artifact.

Production signing changes APK bytes and SHA.

#40/#45 therefore require production signing, expected production certificate SHA, full post-signing verifier, exact post-signing APK SHA, #45 review of that exact artifact, and Samsung measurement on that same artifact.

Final footprint always uses `--expected-apk-sha256 <post-signing-sha>`. ART mode is selected from exact signed package evidence: any DEX -> `speed`; zero DEX + `hasCode=false` -> `none`.

## Package weight and dedup authority

Current Phase-2 Java-shell rules:
- debug APK <=4 MiB;
- unsigned release APK <=4 MiB;
- packaged native DSO <=4 MiB;
- Dex total <=1 MiB;
- duplicate ZIP names = 0;
- duplicate runtime `.so`/`.dex` payloads = 0;
- unexplained large duplicate payload waste = 0;
- one runtime DSO.

Historical v26 size deltas are not v33 acceptance authority without comparable provenance.

## Regression set relevant to current gate

Canonical CTest inventory contains **23 exact native tests**. Important named gates include `cxx23_profile`, `workspace_graph`, `composite_builder`, `composite_placement`, `ptx_transaction`, `ptx_runtime_compat`, `composite_mod_scene`, `scm_authority`, Spider/model/texture/render/session tests.

`tools/test_verify_device_apk.py` additionally protects signing structure, duplicate/size policy, frozen Android user, installed package identity stability, exact ART `speed` command shape and baseline/stress max selection.

Presence is not acceptance; all 23 tests must execute as `status="run"` on the reviewed exact HEAD.

## Release-infrastructure status

Historical `release-v1.yml` and `publish-platform-releases.yml` still encode old v1.0.1/versionCode21/NDK-r28-era assumptions. They are **not** v33 release authority and remain deferred until Phase 7 governed cleanup.

#40 requires one canonical post-signing release path and prevents Android stable publication from being accidentally coupled to unrelated Windows preview work unless deliberately accepted.

## Production boundary

DMC Native Reader is read-only. Editing/repacking belongs to DMC Rengine.

HITS, TXT/index, DCA, LIG/LIG2, PAC/PNST, NBZ, MOT, EFM/MRP/SHW and other researched formats remain outside the production Native Reader registry until individually promoted with native authority and regression coverage.

## Immediate next action

Do **not** add more source modernization merely because Phase 2 is waiting.

The next meaningful state transition is one real exact-head execution:
- restore hosted Actions capacity; or
- use authorized Ubuntu/WSL2 x64 direct execution.

If the run fails, fix the concrete build/test/package defect it reveals. If it passes, send the exact evidence set to #36/#48 and then Review Gate #41. No Phase 3 work begins before #41 GO.
