# DMC Native Reader — C++23 + Spider C++ migration

Date: 2026-09-15
Scope: `VrUaCom/DMC-Native-Reader` only.

## Read first

Private project governance and AI context:

- `docs/PROJECT_AI_CONTEXT.md`
- Project card: #46
- Program tracker: #34

The project context document is the canonical source for architecture rules, evidence policy, task format and review-gate workflow. This migration document records the specific C++23/Spider program.

## Decision

DMC Native Reader product code adopts **C++23** as its canonical native language standard.

The migration does **not** change the DMC Rengine repository or its canonical format/runtime authority. The vendored `DMCRengine::ReaderCore` remains an external dependency boundary. Native Reader may consume its existing API while compiling the Native Reader targets as C++23.

Android canonical toolchain target for this migration is **Android NDK r30 LTS (`30.0.16248370`)** with LLVM/Clang and libc++.

## Language authority

C++23 is owned **only by Native Reader CMake targets**. `DMCNativeReader::Core`, the Android JNI target and Native Reader regression targets require strict ISO C++23 through target-scoped CMake properties:

- `cxx_std_23`;
- `CXX_STANDARD 23`;
- `CXX_STANDARD_REQUIRED ON`;
- `CXX_EXTENSIONS OFF`.

Do not set repository-global `CMAKE_CXX_STANDARD` and do not pass `-std=` through Gradle `cppFlags`. Either mechanism could propagate Native Reader's language decision into vendored dependency targets. Gradle owns Android toolchain selection, ABI and packaging; CMake owns each Native Reader target's language contract.

The pinned Rengine ReaderCore boundary has been audited directly at gitlink `caf445226c7d61841292384a10e93e4f58ae29f9`: `cmake/reader_core.cmake` declares `target_compile_features(dmc_rengine_reader_core PUBLIC cxx_std_20)` and does **not** set global `CMAKE_CXX_STANDARD`. Native Reader links that target as a dependency while declaring its own `cxx_std_23`; usage requirements propagate from the dependency into its consumer, not backward into the dependency compilation. The Phase-2 exact-head runner now fails closed if this pinned C++20 target-scoped Rengine contract changes.

## Review findings

Before migration the Native Reader core and JNI targets were explicitly C++20, Gradle passed `-std=c++20`, and Android workflows/verifier pinned NDK r28c (`28.2.13676358`). Spider Crusader was already a zero-overhead facade over the Rengine native executor. Therefore a safe migration must preserve the executor authority and avoid creating a second Spider runtime.

The first product subsystem that materially benefits from C++23 is `WorkspaceGraph`: mutation operations used invalid-ID sentinels to represent multiple distinct failure modes. `std::expected` lets the product keep fail-closed behavior while making errors typed and explicit.

## Research conclusions

C++23 is the target because it gives the product mature language/library facilities needed by the architecture without moving the production dependency graph onto an unfinished future standard. The required product profile currently includes:

- `std::expected`;
- `std::byteswap`;
- `std::to_underlying`.

Other facilities may be adopted only when they solve a concrete product problem and pass the bounded modernization/review process.

NDK r30 LTS is selected for Android migration so the C++23 product contract is paired with a current LLVM/libc++ toolchain rather than only changing a compiler flag.

## Toolchain compatibility evidence

The language/toolchain decision is backed by current upstream documentation, not only repository assumptions:

- CMake added `CXX_STANDARD 23` and the `cxx_std_23` compile-feature meta-feature in CMake **3.20**. The repository pins CMake **3.22.1**, so target-scoped C++23 selection is supported by the configured CMake version.
- Android currently publishes **NDK r30 `30.0.16248370` as the latest LTS NDK**. This is the canonical Android NDK pin for the migration.
- NDK r30 updates the Android LLVM toolchain to **`clang-r574158c`**.
- Android NDK has sourced libc++ directly from its LLVM toolchain since r26, so an LLVM toolchain update also updates the NDK libc++ implementation.
- The three required C++23 library facilities predate the r30 toolchain by a wide margin in upstream libc++: `std::to_underlying` is complete since libc++/LLVM 13, `std::byteswap` since 14, and `std::expected` since 16.
- Current libc++ feature-test macros expose the required product values (`__cpp_lib_to_underlying=202102L`, `__cpp_lib_byteswap=202110L`, and a `std::expected` macro newer than the product minimum). The build still validates these at compile time rather than trusting version numbers alone.
- The product does not trust one exact `__cplusplus` date value as proof of C++23 because valid toolchains can report different transition values. CMake selects the language mode, while `cpp23_profile.h` rejects C++20-or-older and proves the concrete required library facilities using SD-6 feature-test macros. MSVC uses `_MSVC_LANG` for selected `/std` mode when necessary.
- The pinned `DMCRengine::ReaderCore` remains target-scoped C++20 and contains no global C++ standard override; the exact-head runner validates this dependency boundary before compiling Native Reader.

The canonical Phase-2 evidence runner records the **actual host C++ compiler identity/path** and the **actual installed NDK `clang++ --version` output** in addition to configured package revisions. This allows review/release evidence to prove the compilers actually selected rather than infer them only from CMake/NDK numbers.

Source references used during migration research:
- CMake 3.20 release notes: `https://cmake.org/cmake/help/latest/release/3.20.html`
- Android NDK downloads: `https://developer.android.com/ndk/downloads`
- Android NDK r30 release/changelog: `https://github.com/android/ndk/releases/tag/r30`
- Android NDK C++ library support: `https://developer.android.com/ndk/guides/cpp-support`
- libc++ C++23 status: `https://libcxx.llvm.org/Status/Cxx23.html`
- libc++ feature-test macros: `https://libcxx.llvm.org/FeatureTestMacroTable.html`

## Spider C++

**Spider C++** is the Native Reader embedded C++23 orchestration profile.

It is **not** a duplicated executor, new repository, or replacement for canonical Rengine knowledge. The initial profile provides:

- a compile-time C++23 requirement;
- typed `Result<T, E>` / `Status<E>` contracts based on `std::expected`;
- concepts for Spider product action state/error contracts;
- a typed wrapper over the existing Spider Crusader executor;
- profile markers that can be verified by CI/APK hardening.

Future Spider C++ evolution may add bounded compile-time plan declarations, stronger operation concepts and `consteval` validation, but executor/runtime duplication is forbidden.

## Migration flow

The program is adaptive. A completed implementation phase unlocks a **review gate**, not the next implementation phase directly.

Current flow:

`#35 -> #36 -> #41 -> #37 -> #42 -> #38 -> #43 -> #39 -> #44 -> #40 -> #45`

- #35 — review/research — completed;
- #36 — target-scoped C++23 build baseline — active;
- #47 — Phase-2 execution/runner evidence recovery — active blocker;
- #48 — Phase-2 package/installed-size evidence and 4 MiB hard-limit contract — active review input;
- #41 — Review Gate A: baseline -> modernization;
- #37 — bounded C++23 modernization;
- #42 — Review Gate B: modernization -> C++ Spider;
- #38 — C++ Spider execution language/orchestration;
- #43 — Review Gate C: Spider -> WorkspaceGraph binding migration;
- #39 — stable WorkspaceGraph resource bindings;
- #44 — Review Gate D: bindings -> release verification;
- #40 — exact-head automated/device release readiness;
- #45 — final architecture/device handoff review.

Each review gate must inspect exact HEAD/evidence, classify blockers/corrections/optimizations/deferred work, update or split the next phase, and issue explicit GO/NO-GO.

## Phase intent

1. **Review/research** — inventory language locks, dependency boundaries, toolchain support, Spider authority and migration risks before implementation.
2. **Toolchain baseline** — make target-scoped CMake the sole C++ standard authority; require strict C++23 and Android NDK r30 LTS without changing vendored dependency language mode. This phase also freezes package-weight/dedup evidence policy so the language migration cannot silently bloat the product.
3. **Bounded modernization** — adopt C++23 features only where they materially improve safety or clarity.
4. **Spider C++** — evolve the embedded C++23 language/profile layer above Crusader without changing the Rengine executor.
5. **Dependency graph** — move resource bindings onto stable native IDs instead of Java URI/vector-position semantics.
6. **Verification** — exact-head host CTest, clean APK build, package verifier, review gate and physical Samsung acceptance before merge/release.

## Exact-head evidence contract

PR evidence must bind to the source head that will later be reviewed and handed to physical device acceptance. GitHub `pull_request` workflows normally expose a synthetic merge commit/ref, so active v33 workflows explicitly checkout `github.event.pull_request.head.sha || github.sha` and verify `git rev-parse HEAD` before build execution.

`tools/run_phase2_exact_head.py` is the shared execution baseline used by hosted CI and authorized local/self-hosted recovery. It requires a clean worktree and exact submodule checkout, validates the Native Reader C++23 contract plus the pinned Rengine target-scoped C++20 boundary, validates the canonical AGP/Gradle/JDK/SDK/CMake/NDK stack, records the actual host compiler and NDK Clang, runs full host CMake/CTest, clean Android debug/release builds and `tools/verify_device_apk.py`, requires `spider.crusader` + `spider.cpp23` in both built APK runtime DSOs, then writes an evidence manifest with APK SHA-256 values.

The same evidence contract now includes product weight integrity:

- debug and unsigned-release APK must each be **<= 4 MiB**;
- DSO <= 4 MiB and Dex <= 1 MiB;
- duplicate ZIP names/runtime payloads/large duplicate payload waste must be zero;
- verifier reports absolute current sizes and largest package entries;
- historical v26 package/native values are not growth authority without proven comparable provenance;
- downstream Samsung acceptance requires installed package/code allocation **<= 4 MiB**, excluding mutable user data/cache, measured by `tools/measure_installed_footprint.py` and tied to exact APK SHA-256/device identity.

`APK <= 4 MiB` is a necessary precondition for the installed gate, not a replacement for physical-device measurement.

## Boundary rules

- Do not modify any repository other than DMC Native Reader for this migration.
- Do not alter the Rengine submodule pin as part of the language migration.
- C++23 types may exist in Native Reader product APIs; do not require Rengine headers to adopt them.
- JNI remains transport only.
- Spider C++ remains orchestration/product language, not format authority.
- No filename/order heuristics become semantic authority.
- Release promotion remains blocked until real build/test/device evidence exists.

## Evidence rule

Phase status is tied to exact HEAD. A GitHub Actions record with `runner_id=0`, `steps=[]`, skipped jobs, or another pre-run infrastructure failure is not compile/test evidence.

Phase 2 does not close until real CMake/CTest execution plus clean Android debug/release builds and the package verifier succeed on the candidate SHA. The final 4 MiB installed package/code gate remains a downstream physical-device requirement and is not inferred from APK size alone.

## Legacy workflow note

Historical release workflows that still target obsolete v1.0.1 branches/artifacts are not migration authority. Their broader release-semantic cleanup is deferred to Review Gate #44 / Phase #40 rather than mixed into the C++23 compatibility baseline.

## Update 2026-09-23 — everything in Native Reader is C++23

Policy change requested by the maintainer: all code compiled by Native Reader
is C++23. `app/src/main/cpp/CMakeLists.txt` now applies
`dmc_native_reader_require_cpp23` to `dmc_rengine_reader_core` and to the
Native Reader-owned MOT/PAC slice `dmc_native_reader_rengine_viewer` as well.
The pinned `reader_core.cmake` still declares `cxx_std_20` as a minimum
feature and is not edited; the target-scoped `CXX_STANDARD 23` sets the real
mode (host build: 82/82 translation units `-std=c++23`, no warnings, 25/25
CTest). The Phase-2 runner and CI now fail if either target loses the C++23
requirement. The statements above that ReaderCore "remains target-scoped
C++20" describe the 2026-09-15 state and are superseded. The reverse of the
original game stays in dmc-rengine-cpp.

