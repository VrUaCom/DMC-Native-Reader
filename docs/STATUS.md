# DMC Native Reader — Status

Last updated: 2026-09-16.

## Accepted baseline (`main`)

- current `main`: `561385e24e7246da11631e594ad5a86ca619fa74`
- device-confirmed product baseline: v26 line accepted through PR #32
- package: `com.dmcrengine.nativereader`
- ABI: `arm64-v8a`
- minSdk / targetSdk: `26 / 36`

The v26 line was physically tested on Samsung on 2026-09-10 and explicitly approved for promotion to `main`.

## Active candidate — v33

- branch: `feature/png-export-multi-mod-v27`
- PR: #33, draft
- versionName / versionCode: `1.0.6 / 33`
- Native Reader production language: **strict target-scoped ISO C++23**
- Spider product profile: **`spider.cpp23`** over Crusader
- Android native toolchain: **NDK r30 LTS `30.0.16248370`**
- build stack: **AGP 9.3.0 / Gradle 9.5.0 / JDK 17 / Android 36 / Build Tools 36.0.0 / Android CMake 3.22.1**
- production modules: **MOD / SCM / DDS / PTX / EventTbl**
- pinned Rengine ReaderCore gitlink: `caf445226c7d61841292384a10e93e4f58ae29f9`
- package pre-gate: **debug APK <=4 MiB; unsigned release APK <=4 MiB**
- physical acceptance hard gate: **StorageStats.getAppBytes() <=4 MiB** on the exact reviewed installable artifact

The candidate SHA is deliberately **not hard-coded in this status file**. Every execution/review must fetch PR #33 live `head_sha`; committing a SHA here would immediately make it stale.

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
- #47 — current blocking task: real exact-head CMake/CTest/Android execution;
- #53 — owner action: restore hosted capacity or start canonical direct Linux/WSL run;
- #48 — waiting for real artifact/device size evidence;
- #41 — blocked until #36 has one complete real evidence set;
- #54/#55 — future native Android shell / Java-retirement phase and review, blocked until #44 GO.

No Phase 3 implementation may start before #41 explicitly issues GO.

## Why Phase 2 is still blocked

Current source/static review is not the blocker. GitHub-hosted jobs have repeatedly failed **before runner assignment** with `runner_id=0` and no executed steps. The deliberate 2026-09-16 probe also failed with zero steps.

Therefore:
- these runs are infrastructure evidence only;
- they do not prove source PASS or FAIL;
- do not repeatedly rerun hosted jobs while capacity/account state is unresolved;
- use an authorized Ubuntu/WSL2 x64 environment as the immediate fallback.

Canonical direct path:

```bash
bash tools/bootstrap_phase2_self_hosted_ubuntu.sh
source build/phase2-self-hosted-env.sh
python3 tools/run_phase2_exact_head.py \
  --sdk "$ANDROID_SDK_ROOT" \
  --gradle "$GRADLE_HOME/bin/gradle" \
  --expected-head "$(git rev-parse HEAD)"
```

This command must run on the live PR #33 candidate HEAD, not a SHA copied from historical comments.

## Phase-2 evidence contract

`tools/run_phase2_exact_head.py` is the canonical shared evidence runner. It must actually execute and record:
- clean worktree + exact HEAD guard;
- pinned Rengine gitlink/checkout identity;
- package/evidence policy regressions;
- host CMake configure/build;
- full host CTest, including C++23/PTX regressions;
- clean Android debug + unsigned-release builds;
- full APK verifier on **both** artifacts;
- actual compiler/CMake/CTest/Gradle/JDK/NDK identity;
- exact APK SHA-256 values and machine-readable package metrics;
- final clean-tree guard.

Static inspection, YAML correctness, runner-less records or source review cannot substitute for this execution.

## Symmetric debug / release package verification

The earlier evidence gap where only debug received the full verifier has been closed.

Both Phase-2 APKs now receive the same package/ABI/JNI/layout/dedup/size verification. Their only intentional difference is signing policy.

### Debug APK
Must prove:
- expected package/version/ABI;
- stable public test signer matches the expected SHA-256;
- APK v2 verification succeeds;
- an APK Signing Block is structurally present;
- exactly one runtime DSO;
- Java NativeBridge ↔ JNI export parity;
- native DSO stored uncompressed and 16 KiB ZIP aligned;
- every ELF PT_LOAD supports >=16 KiB;
- APK/native/Dex hard budgets;
- zero duplicate ZIP/runtime/large-payload waste.

### Unsigned release APK
Must independently prove the same structural/package contract, plus:
- no valid signer;
- **no APK Signing Block**;
- **no JAR signature material** (`META-INF/*.SF`, `*.RSA`, `*.DSA`, `*.EC`).

A failed signature verification by itself is no longer accepted as proof of “unsigned”; a broken signing block must fail closed.

Each verifier report is SHA-256-bound to the exact APK, and the exact-head runner independently validates critical signing fields before accepting the manifest.

## C++23 / Rengine language boundary

Native Reader targets require:
- `cxx_std_23`;
- `CXX_STANDARD 23`;
- `CXX_STANDARD_REQUIRED ON`;
- `CXX_EXTENSIONS OFF`.

Gradle does not own global `-std=c++*` policy.

Pinned `DMCRengine::ReaderCore` remains a read-only external target with its own target-scoped `cxx_std_20` contract. Phase 2 must not leak Native Reader C++23 policy into Rengine.

Important Native Reader C++23 facilities currently used/guarded:
- `std::expected`;
- `std::byteswap`;
- `std::to_underlying`.

## Exception boundary status

#49 is DONE on source/static review.

Current contract:
- allocating internal helpers expose truthful throwing-capable semantics unless they catch locally;
- `run_decode_pipeline()` is the portable Core catch-all;
- NativeModule `ModuleRun` and Crusader `OperationFn` remain noexcept/fail-closed boundaries;
- public Spider noexcept actions catch first-use Plan/helper allocation;
- JNI is final platform catch-all;
- no C++ exception may cross JNI;
- catch-path fallback must not require allocating diagnostics.

Real CTest/Android execution is still required before global acceptance.

## PTX RuntimeCompat status

Native Reader has one user-facing PTX route with two internal responsibilities:

`TextureSlotFramingParser -> TextureSet -> DDS` + lazy Reader-owned `PtxRuntimeCompat`.

Accepted architecture:
- serialized framing/TextureSet/DDS remains the only disk-format PTX authority;
- RuntimeCompat is not a second parser or NativeModule;
- runtime state is lazy and ordinary preview/gallery/PNG does not instantiate it;
- imported behavior is bounded to the reviewed Rengine `50d070e...` slice;
- initializer represented storage = `0xCB50`;
- placement/configure pool prefix = `0xCB48`;
- final 8 bytes remain opaque tail storage;
- no inferred `TextureSet::Slot -> runtime 0x50 record` materialization;
- palette/finalizer/cleanup/materializer/full lifecycle remain explicit unknown/deferred.

#52 issued architecture GO but explicitly left execution `PENDING`; `ptx_runtime_compat_test` still must run in the exact-head CTest checkpoint.

## Android installed-size evidence

Hard acceptance maximum: **4,194,304 bytes** from Android `StorageStats.getAppBytes()`.

`tools/measure_installed_footprint.py` is device evidence, not an APK verifier. It must:
- query authoritative package-storage `code:` bytes;
- fail closed if unavailable;
- never use `du` fallback;
- require one installed `base.apk`;
- compare installed `base.apk` SHA-256 byte-for-byte with the reviewed APK;
- verify package version identity;
- use the same explicit Android `--user` scope for both `pm path` and `pm get-package-storage-stats`;
- default to `current` unless a numeric profile/user is intentionally targeted;
- record requested/resolved/current user IDs, device model, Android version and build fingerprint.

This closes the previous risk where Android shell defaults could mix USER_SYSTEM package-path lookup with USER_CURRENT StorageStats on multi-user/profile devices.

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

Android/Java currently owns transport/presentation/lifecycle only. It must not own DMC parsing, graph semantics, model attachment policy or texture ownership semantics.

## Future Phase 6 — native Android shell / Java retirement

Java retirement is now an explicit governed phase, not an opportunistic rewrite.

It begins only after:
- Phase 5 stable WorkspaceGraph bindings;
- Review Gate #44 GO.

Target authority split:
- Spider C++ = product orchestration/actions;
- Black Widow = capability/application policy;
- WorkspaceGraph = stable resource identity/bindings;
- portable C++ controller = navigation/session/presentation model;
- Android platform layer = lifecycle/window/input/document transport/presentation only.

Preferred target: **0 authored Java/Kotlin application source + 0 app DEX** through supported NativeActivity/NDK APIs.

This target is evidence-gated because current SAF document flows use result-returning Intents and documented `ANativeActivityCallbacks` has no `onActivityResult`. #54 must prove a supported public zero-DEX route. If required SAF/UX cannot be preserved, only #55-approved minimal framework callback shim may remain.

Forbidden:
- hidden/private Android APIs;
- reflection hacks;
- generated/obfuscated DEX merely to claim zero Java;
- deleting required open/export functionality to hit the metric.

## Final release artifact identity

Three different artifacts must never be conflated:
1. debug/device-test APK;
2. unsigned release APK — **pre-signing structural evidence only**;
3. production-signed release APK — **final installable Samsung/promotion artifact**.

Production signing changes APK bytes and SHA-256.

Phase #40/#45 therefore requires:
- production signing through the authorized release authority;
- explicit expected production certificate SHA-256;
- full applicable package/ABI/JNI/ZIP/ELF/16 KiB/dedup/size verification **after signing**;
- exact post-signing APK SHA-256;
- #45 review of that exact signed artifact;
- Samsung installation/StorageStats measurement against that same signed artifact;
- publication of that exact signed artifact/hash without rebuilding or re-signing a lookalike.

The final footprint invocation must be guarded with `--expected-apk-sha256 <post-signing-sha256>` and explicit Android `--user` scope. Installed `base.apk` SHA must equal the reviewed **post-signing** APK SHA, never the unsigned predecessor.

## Package weight and dedup authority

Current v33 Java-shell rules:
- debug APK <=4 MiB;
- unsigned-release APK <=4 MiB;
- packaged native DSO <=4 MiB;
- Dex total <=1 MiB;
- duplicate ZIP entry names = 0;
- duplicate runtime `.so`/`.dex` payloads = 0;
- unexplained large duplicate payload waste = 0;
- one runtime DSO only.

A sub-4-MiB APK can still fail Samsung acceptance because optimized/runtime app bytes can exceed 4 MiB.

Historical v26 size deltas are not v33 acceptance authority without proven comparable artifact/packaging/measurement provenance.

## Regression set relevant to current gate

Important v33 regressions include:
- `cxx23_profile_test`;
- `workspace_graph_test`;
- `composite_builder_test`;
- `composite_placement_test`;
- `ptx_transaction_test`;
- `ptx_runtime_compat_test`;
- `composite_mod_scene_test`;
- `scm_authority_test`;
- module/Spider/texture/PNG/render/inspection regressions;
- `tools/test_verify_device_apk.py`, including signing-structure and Android user-scope policy checks.

These tests being present is not acceptance; they must execute on the reviewed exact HEAD.

## Release-infrastructure status

Historical `.github/workflows/release-v1.yml` and `publish-platform-releases.yml` still encode old v1.0.1/versionCode21/NDK-r28-era assumptions. They are **not** v33 release authority and remain deferred until governed Phase 7 cleanup.

#40 now explicitly requires one canonical post-signing release path and prevents Android stable publication from being accidentally coupled to unrelated Windows preview work unless that coupling is deliberately accepted.

## Production boundary

DMC Native Reader is read-only. Editing/repacking belongs to DMC Rengine.

HITS, TXT/index, DCA, LIG/LIG2, PAC/PNST, NBZ, MOT, EFM/MRP/SHW and other researched formats remain outside the production Native Reader registry until individually promoted with native authority and regression coverage.

## Immediate next action

Do **not** add more source modernization merely because Phase 2 is waiting.

The next meaningful state transition is one real exact-head execution:
- restore hosted Actions capacity; or
- use authorized Ubuntu/WSL2 x64 direct execution.

If the run fails, fix the concrete build/test/package defect it reveals. If it passes, send the exact evidence set to #36/#48 and then Review Gate #41. No Phase 3 work begins before #41 GO.
