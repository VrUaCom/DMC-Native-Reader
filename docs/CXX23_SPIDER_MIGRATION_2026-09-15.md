# DMC Native Reader — C++23 + Spider C++ migration

Date: 2026-09-15
Scope: `VrUaCom/DMC-Native-Reader` only.

## Decision

DMC Native Reader product code adopts **C++23** as its canonical native language standard.

The migration does **not** change the DMC Rengine repository or its canonical format/runtime authority. The vendored `DMCRengine::ReaderCore` remains an external dependency boundary. Native Reader may consume its existing API while compiling the Native Reader targets as C++23.

Android canonical toolchain target for this migration is **Android NDK r30 LTS (`30.0.16248370`)** with LLVM/Clang and libc++.

## Review findings

Before migration the Native Reader core and JNI targets were explicitly C++20, Gradle passed `-std=c++20`, and Android workflows/verifier pinned NDK r28c (`28.2.13676358`). Spider Crusader was already a zero-overhead facade over the Rengine native executor. Therefore a safe migration must preserve the executor authority and avoid creating a second Spider runtime.

The first product subsystem that materially benefits from C++23 is `WorkspaceGraph`: mutation operations currently use invalid-ID sentinels to represent multiple distinct failure modes. `std::expected` lets the product keep fail-closed behavior while making errors typed and explicit.

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

1. **Toolchain contract** — set Native Reader CMake/Gradle targets to C++23 and Android NDK r30 LTS.
2. **Compile gate** — add a C++23 profile header and regression that fails when `std::expected`/C++23 are unavailable.
3. **Real product adoption** — migrate `WorkspaceGraph` mutation results from invalid-ID sentinels to typed `std::expected` errors.
4. **Spider C++ v1** — add the embedded C++23 language/profile layer above Crusader without changing the Rengine executor.
5. **CI/verifier** — make active v33 Android/hardening and device verifier enforce C++23 + NDK r30 + Spider C++ markers.
6. **Architecture docs** — update the v33 architecture contract from portable C++20 to portable C++23 and document the Rengine boundary.
7. **Verification** — exact-head host CTest, clean APK build, package verifier and physical Samsung acceptance remain mandatory before merge/release.

## Boundary rules

- Do not modify any repository other than DMC Native Reader for this migration.
- Do not alter the Rengine submodule pin as part of the language migration.
- C++23 types may exist in Native Reader product APIs; do not require Rengine headers to adopt them.
- JNI remains transport only.
- Spider C++ remains orchestration/product language, not format authority.
- No filename/order heuristics become semantic authority.
- Release promotion remains blocked until real build/test/device evidence exists.

## Legacy workflow note

Historical release workflows that still target obsolete v1.0.1 branches/artifacts are not migration authority. Their broader release-semantic cleanup is a separate bounded task; the active v33 core/hardening/verifier paths define this C++23 migration gate.
