# DMC Native Reader — Status

Last updated: 2026-09-15.

## Accepted baseline (`main`)

- current `main`: `561385e24e7246da11631e594ad5a86ca619fa74`
- device-confirmed product baseline: v26 line accepted through PR #32
- package: `com.dmcrengine.nativereader`
- ABI: `arm64-v8a`
- minSdk / targetSdk: `26 / 36`
- later `main` maintenance added Android/Windows release-publishing workflow; it did not constitute a new device-accepted reader build

The v26 line was physically tested on Samsung on 2026-09-10 and explicitly approved for promotion to `main`.

## Active candidate — v33

- branch: `feature/png-export-multi-mod-v27`
- PR: #33, draft
- versionName: `1.0.6`
- versionCode: `33`
- canonical Native Reader language: **C++23**
- C++ standard authority: **target-scoped CMake; strict ISO C++23**
- Spider product language/profile: **Spider C++ (`spider.cpp23`)**
- Android native toolchain: **NDK r30 LTS `30.0.16248370`**
- Android build stack: **AGP 9.3.0 / Gradle 9.5.0 / JDK 17 / Build Tools 36.0.0 / Android CMake 3.22.1**
- production modules: **MOD / SCM / DDS / PTX / EventTbl**
- canonical DMC Rengine ReaderCore pin: `caf445226c7d61841292384a10e93e4f58ae29f9`

The current candidate SHA is deliberately **not hard-coded in this status file** because committing the document would immediately make that value stale. Exact source identity is taken from PR #33 at execution time and is recorded in the Phase-2 evidence manifest.

PR #33 must not be merged until an exact-head build actually executes the full host regressions, passes `tools/verify_device_apk.py`, produces the canonical single-DSO APK, and passes Samsung device acceptance.

## v33 architecture

```text
resource bytes
  -> bounded probe
  -> NativeModuleRegistry
  -> Spider C++23
      -> Spider Crusader
          -> MOD / SCM / DDS / PTX / EventTbl native modules
          -> pinned Rengine executor / ReaderCore
  -> portable DMCNativeReader::Core (C++23)
      -> resource session
      -> WorkspaceGraph / stable resource identity
      -> composite model state
      -> composite builder
      -> Rengine-backed default-joint resolver
      -> composite placement
      -> scene projection
      -> texture companion binding
      -> Black Widow typed capability state
      -> renderer / inspection / UV / PNG export
  -> thin JNI / Android shell
```

Android remains transport/presentation only. It must not parse DMC layouts, decide model attachment semantics or implement texture-binding policy.

The C++23 migration is scoped to DMC Native Reader. The Rengine repository, gitlink and canonical format/runtime authority are unchanged. Spider C++ is a typed C++23 layer over Crusader; it does not duplicate the Rengine executor.

## C++23 migration

The current candidate enforces C++23 as a product contract rather than merely changing a compiler flag:

1. `DMCNativeReader::Core`, JNI and Native Reader regression targets require `cxx_std_23` plus `CXX_STANDARD 23`, `CXX_STANDARD_REQUIRED ON`, and `CXX_EXTENSIONS OFF`;
2. Android Gradle pins NDK r30 LTS but does **not** pass `-std=c++*`; CMake owns the Native Reader language mode so vendored dependency targets retain their own contract;
3. `cpp23_profile.h` rejects C++20-or-older and requires concrete C++23 facilities: `std::expected`, `std::byteswap`, and `std::to_underlying`;
4. the profile uses `_MSVC_LANG` on MSVC and `__cplusplus` elsewhere so portable core validation does not depend on one compiler's macro-reporting behavior;
5. core warning flags are compiler-scoped: MSVC receives `/W4`, while GCC/Clang receive `-Wall -Wextra -Wpedantic`;
6. pinned `DMCRengine::ReaderCore` was audited at `caf445226c7d61841292384a10e93e4f58ae29f9`: its own `reader_core.cmake` requires target-scoped `cxx_std_20` and does not impose global `CMAKE_CXX_STANDARD`; the Phase-2 runner fails closed if this boundary changes;
7. `WorkspaceGraph` mutation APIs currently return typed `std::expected` results with explicit error codes;
8. `spider/cpp23_language.h` defines the initial Spider C++ result/concept profile above Crusader;
9. multi-MOD compose currently exercises the initial Spider C++ typed wrapper;
10. active PR workflows explicitly checkout `${{ github.event.pull_request.head.sha || github.sha }}` so evidence is tied to the candidate source head rather than GitHub's synthetic pull-request merge ref;
11. `tools/run_phase2_exact_head.py` is the shared exact-head execution baseline for hosted CI and authorized local/self-hosted recovery: it checks the complete Native Reader + pinned-Rengine language/toolchain contract, runs host CMake/CTest, clean Android debug/release builds and the APK verifier, and writes toolchain/artifact SHA-256 evidence;
12. the evidence manifest records the actual host C++ compiler identity/path and actual installed NDK `clang++ --version`, not only configured package numbers;
13. both debug and unsigned-release APKs must contain the runtime `spider.crusader` and `spider.cpp23` markers;
14. `tools/verify_device_apk.py` independently requires the built `spider.cpp23` DSO marker, one-DSO/ABI/JNI/signing/alignment/size contracts, exact Rengine checkout, and robustly accepts zero-valued `aapt2` representations of `extractNativeLibs=false`;
15. active CI and the APK verifier gate the target-scoped C++23/NDK contract.

The `WorkspaceGraph std::expected` migration and initial Spider C++ seed entered the branch before formal Phase/Review gates were established. Phase 2 does not expand them further. Review Gate #41 must explicitly classify them retain/correct/defer/revert before Phase 3/4 progression.

Migration review/research/plan: `docs/CXX23_SPIDER_MIGRATION_2026-09-15.md`.
Private project/AI context: `docs/PROJECT_AI_CONTEXT.md`, Project card #46.
Program tracking: #34; Phase 1 (#35) completed; Phase 2 (#36) remains active until a real exact-head build executes. CI execution recovery is tracked in #47.

## Multi-MOD / body-hair placement

The old v33 compositor flattened all MOD parts in source coordinates. That behavior explains the observed body/hair problem: separate parts were rendered around their own model-space origin instead of receiving the runtime-style host-joint root transform.

The current candidate now has a modular attachment path:

1. the MOD module publishes canonical `Header::default_joint_index()` as typed `RenderScene::default_attachment_selector`;
2. `composite_builder` treats the already-open/base MOD as an explicit primary host;
3. appended MOD parts resolve their selector through `dmc::rengine::formats::mod::attachment`;
4. valid host spatial authority + in-range selector yields a host joint matrix;
5. `composite_placement` applies that matrix only to the derived flattened render/hierarchy projection;
6. the source-local child `RenderScene` remains unchanged and can be reset without reparsing.

The resolver does not infer a different host from filenames, visual proximity, `runtime_metadata_u32` or arbitrary candidate scanning. Missing/out-of-range selectors and hosts without canonical spatial authority fail closed to source coordinates.

## Modular split completed in this pass

- `include/dmcresource/cpp23_profile.h` — canonical C++23 compile/result profile
- `include/dmcresource/spider/cpp23_language.h` — initial Spider C++ typed product-language layer, pending formal gate disposition
- `include/dmcresource/workspace_graph.h` / `modules/workspace_graph.cpp` — stable identities + typed C++23 mutation results, pending formal gate disposition
- `include/dmcresource/composite_model.h` — source-part + placement state
- `include/dmcresource/composite_builder.h` / `modules/composite_builder.cpp` — product composition policy
- `include/dmcresource/mod_attachment_resolver.h` / `modules/mod_attachment_resolver.cpp` — Rengine-backed selector resolution
- `include/dmcresource/composite_placement.h` / `modules/composite_placement.cpp` — derived placement projection
- `spider/session_compose_actions.cpp` — compose action
- `spider/session_texture_actions.cpp` — texture actions
- `spider/model_placement_actions.cpp` — explicit placement/reset actions

The old monolithic `spider/session_actions.cpp` is no longer compiled.

## PTX transaction safety

Per-part PTX replacement is fully staged. Texture storage and triangle-slot projection are copied into temporary state; decode, range validation, slot validation and compaction complete before the live session is replaced. A failed replacement therefore preserves the previous valid texture bank and render projection.

Regression: `ptx_transaction_test`.

## Current regression set added/strengthened

Important v33-specific regressions now include:

- `cxx23_profile_test` — statically validates the active C++23 language level, `std::expected`, `std::byteswap`, `std::to_underlying`, Spider C++ concepts/profile and success/error expected paths;
- `workspace_graph_test` — stable identities + typed graph failures;
- `composite_builder_test` — automatic primary-host/default-joint placement and fail-closed fallback;
- `composite_placement_test` — explicit host-joint projection, row-vector transform order and reset;
- `ptx_transaction_test` — failed per-part PTX replacement preserves live state;
- `composite_mod_scene_test` — low-copy composition and shared PTX bank;
- `scm_authority_test` — canonical SCM world-space authority;
- existing module/Spider/texture/PNG/render/inspection regressions.

PR-wide static call-site review found the production `WorkspaceGraph` mutation use in `composite_builder` already handles `std::expected` explicitly; no stale caller that assumes direct integer-ID returns was found. This is static evidence only until compilation executes.

## APK/runtime contract

v33 requires:

- strict target-scoped C++23 Native Reader product core;
- pinned Rengine ReaderCore retaining its own target-scoped C++20 contract;
- Spider C++ typed orchestration profile over Crusader, subject to formal review-gate disposition;
- Android NDK r30 LTS `30.0.16248370`;
- exactly one packaged native DSO: `lib/arm64-v8a/libdmcviewer.so`;
- static `DMCNativeReader::Core` + `DMCRengine::ReaderCore`;
- `extractNativeLibs=false`;
- native DSO stored uncompressed and 16 KiB ZIP aligned;
- every ELF `PT_LOAD` alignment >= 16 KiB;
- exact Java `NativeBridge` ↔ JNI export parity;
- no recovery `dmcshim` / `dmccore00` path;
- direct Android Bitmap transport;
- APK <= 8 MiB, DSO <= 4 MiB, Dex <= 1 MiB;
- exact Rengine gitlink and checkout at `caf445226c7d61841292384a10e93e4f58ae29f9`;
- runtime `spider.crusader` + `spider.cpp23` presence in both debug and unsigned release APKs.

`tools/verify_device_apk.py` gates C++23/Spider C++/NDK r30 plus the split compose/texture action modules and exact Rengine pin.

## CI state

GitHub-hosted jobs on current heads continue to fail before runner assignment. The characteristic failure is `runner_id=0` with `steps=[]` / `steps=null`; checkout, CMake, Gradle and tests never start. Manual rerun of an earlier failed exact-head core job also produced a new attempt that queued briefly and then failed without steps.

A recent exact-head example is source SHA `40050304120cfd185df4eca95002f0d563d20021`: core run `35005014015`, job `104502481063`, completed with no steps. This is retained as historical infrastructure evidence only; it is not a statement that this SHA remains the current PR head and it does not classify the source as passing or failing.

Historical GitHub Status incidents affected Actions on Sep 13 and runner startup on Sep 14, matching the onset window, but GitHub Status later returned operational while this repository continued to exhibit runner-less failures. #47 therefore tracks hosted-runner/account availability separately from source correctness.

Until a real exact-head build executes, v33 remains **not release-approved** and Phase 2 remains **in progress**. Review Gate #41 is not unlocked.

## Release-infrastructure status

Historical `.github/workflows/release-v1.yml` and `publish-platform-releases.yml` still encode old v1.0.1/versionCode21/28/NDK-r28/C++20-era assumptions. They are **not v33 release authority** and are intentionally deferred rather than opportunistically rewritten during Phase 2. Review Gate #44 must disposition them (retire/archive/migrate/replace) and update Phase #40 with one canonical exact-head Android release/signing/publish path. Windows preview must not accidentally block Android stable publication unless that coupling is explicitly accepted.

## Production boundary

DMC Native Reader is read-only. Editing/repacking belongs to DMC Rengine. HITS, TXT/index, DCA, LIG/LIG2, PAC/PNST, NBZ, MOT, EFM/MRP/SHW and other researched formats remain outside the production Native Reader registry until individually promoted with native authority and regression coverage.
