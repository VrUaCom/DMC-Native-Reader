# DMC Native Reader — C++23 + Spider C++ migration

Date: 2026-09-15
Scope: `VrUaCom/DMC-Native-Reader` only.

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

## Review findings

Before migration the Native Reader core and JNI targets were explicitly C++20, Gradle passed `-std=c++20`, and Android workflows/verifier pinned NDK r28c (`28.2.13676358`). Spider Crusader was already a zero-overhead facade over the Rengine native executor. Therefore a safe migration must preserve the executor authority and avoid creating a second Spider runtime.

The first product subsystem that materially benefits from C++23 is `WorkspaceGraph`: mutation operations used invalid-ID sentinels to represent multiple distinct failure modes. `std::expected` lets the product keep fail-closed behavior while making errors typed and explicit.

## Research conclusions

C++23 is the target because it gives the product mature language/library facilities needed by the architecture without moving the production dependency graph onto an unfinished future standard. The first required facility is `std::expected`; other facilities may be adopted only when they solve a concrete problem.

NDK r30 LTS is selected for Android migration so the C++23 product contract is paired with a current long-term-support LLVM/libc++ toolchain rather than only changing a compiler flag.

## Spider C++

**Spider C++** is the Native Reader embedded C++23 orchestration profile.

It is **not** a duplicated executor, new repository, or replacement for canonical Rengine knowledge. The initial profile provides:

- a compile-time C++23 requirement;
- typed `Result<T, E>` / `Status<E>` contracts based on `std::expected`;
- concepts for Spider product action state/error contracts;
- a typed wrapper over the existing Spider Crusader executor;
- profile markers that can be verified by CI/APK hardening.

Future Spider C++ evolution may add bounded compile-time plan declarations, stronger operation concepts and `consteval` validation, but executor/runtime duplication is forbidden.

## Migration plan

1. **Review/research** — inventory language locks, dependency boundaries, toolchain support, Spider authority and migration risks before implementation.
2. **Toolchain baseline** — make target-scoped CMake the sole C++ standard authority; require strict C++23 and Android NDK r30 LTS without changing vendored dependency language mode.
3. **Compile gate** — use a C++23 profile header and regression that fail when the required language/library profile is unavailable.
4. **Bounded modernization** — adopt C++23 features only where they materially improve safety or clarity; `WorkspaceGraph` typed `std::expected` results are the first promoted case.
5. **Spider C++** — evolve the embedded C++23 language/profile layer above Crusader without changing the Rengine executor.
6. **Dependency graph** — move resource bindings onto stable native IDs instead of Java URI/vector-position semantics.
7. **CI/verifier** — make active v33 Android/hardening and device verifier enforce the target-scoped C++23 + NDK r30 + Spider C++ contract.
8. **Verification** — exact-head host CTest, clean APK build, package verifier and physical Samsung acceptance remain mandatory before merge/release.

## Boundary rules

- Do not modify any repository other than DMC Native Reader for this migration.
- Do not alter the Rengine submodule pin as part of the language migration.
- C++23 types may exist in Native Reader product APIs; do not require Rengine headers to adopt them.
- JNI remains transport only.
- Spider C++ remains orchestration/product language, not format authority.
- No filename/order heuristics become semantic authority.
- Release promotion remains blocked until real build/test/device evidence exists.

## Phase tracking

The migration is tracked through repository issues:

- #34 program tracker;
- #35 review/research — completed;
- #36 C++23 baseline — active;
- #37 bounded modernization;
- #38 C++ Spider;
- #39 stable WorkspaceGraph bindings;
- #40 verification/device/release readiness.

## Legacy workflow note

Historical release workflows that still target obsolete v1.0.1 branches/artifacts are not migration authority. Their broader release-semantic cleanup is a separate bounded task; the active v33 core/hardening/verifier paths define this C++23 migration gate.
