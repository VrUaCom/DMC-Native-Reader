# DMC Native Reader

Native Android reader for Devil May Cry 3 HD Collection resources, built around a reusable **C++23** core and canonical DMC Rengine read-side authority.

## Current state

**Accepted `main`: device-confirmed v26 line plus release-workflow maintenance**  
**Current `main` commit:** `561385e24e7246da11631e594ad5a86ca619fa74`  
**Active candidate:** **v37 / versionName 1.0.10 / versionCode 37** (MOT playback, PAC, player coat, weapon PACs, community-made PTX; previous v36 / 1.0.9)  
**Candidate branch:** `feature/png-export-multi-mod-v27` / draft PR #33  
**Android:** arm64-v8a, minSdk 26, targetSdk 36, **NDK r30 LTS**  
**Native product language:** **strict target-scoped C++23 + Spider C++ (`spider.cpp23`)**  
**Production registry:** **MOD / SCM / DDS / PTX / EventTbl / PAC / MOT**  
**Weight contract:** **APK <= 4 MiB; installed package/code <= 4 MiB on the acceptance Samsung**

The accepted v26 line was physically tested on Samsung and approved for `main`. PR #33 is a larger candidate and remains draft until an exact-head clean host build, APK verifier pass and physical Samsung acceptance are all complete. GitHub-hosted jobs are currently observed failing before runner assignment (`runner_id=0`, `steps=[]`), which is neither green evidence nor a source-regression result.

## v34 candidate additions (branch `claude/devil-microy3-decompile-port-2v8pne`)

Read-only, see [`docs/MOTION_PAC_COMPANION_V34.md`](docs/MOTION_PAC_COMPANION_V34.md):

- **MOT playback**: tap a MOT card to play it on the open MOD / composite (tap again to pause); animated local matrix reconstructed from EXE `0x140310310`, compression 2 and 3 tracks, inverse-rest skinning;
- **companion MODs** (hair, coat, accessories) stay in character model space; MOD header `+0x13` is reported only (EXE uses it as a translation probe, never as a geometry root);
- **PAC**: opens as an assembled character/scene (MODs, slot-adjacent PTX, MOT library); every slot is browsable and opens in its own viewer;
- registry gains `PAC` and `MOT` (byte-identified only).

## v33 capabilities

### MOD

- canonical DMC Rengine structural parsing;
- 3D geometry, hierarchy and skin data;
- rotate / zoom / wireframe;
- typed inspection and UV projection;
- PTX companion attachment;
- multi-MOD composition through a dedicated `composite_builder`;
- source-local scenes retained as authority;
- Rengine-backed `default_joint_index` attachment resolution against the explicit primary/base MOD;
- child placement applied only to the derived composite projection, never to source data.

### SCM

- canonical retail SCM parsing through pinned Rengine ReaderCore;
- hierarchy/object-binding/world-space geometry projection;
- rotate / zoom / wireframe;
- typed inspection, UV and texture-slot state;
- shared PTX companion path.

### DDS / PTX

- bounded DXT1/DXT5 decoding;
- native RGBA previews;
- PTX bundle framing and child resources;
- native-backed PNG export;
- shared texture banks without per-part RGBA duplication;
- transactional per-part PTX replacement: failure preserves the previous live bank and slot projection.

### EventTbl

- promoted fifth production module;
- canonical native structural inspection through Spider execution.

Unknown and unpromoted resource families fail closed.

## C++23 / Spider C++

C++23 is owned by the Native Reader **CMake targets**, not by a repository-global flag. `DMCNativeReader::Core`, Android JNI and Native Reader regression targets require `cxx_std_23`, `CXX_STANDARD 23`, `CXX_STANDARD_REQUIRED ON`, and `CXX_EXTENSIONS OFF`. Gradle pins Android to NDK r30 LTS `30.0.16248370` but does not pass `-std=c++*`. Everything Native Reader compiles is C++23, including the vendored Rengine ReaderCore and the MOT/PAC slice: their targets get the same target-scoped `CXX_STANDARD 23` in Native Reader's CMake, without editing the submodule. The reverse of the original game itself lives in dmc-rengine-cpp.

`cpp23_profile.h` does not depend on one compiler-specific `__cplusplus == 202302L` value. CMake selects strict ISO C++23; the profile rejects C++20-or-older and proves the required product facilities through SD-6 feature checks for `std::expected`, `std::byteswap`, and `std::to_underlying`. CI/verifier gates reject fallback to the former C++20 contract or reintroduction of Gradle-owned language mode.

**Spider C++** is the embedded C++23 product-language layer for orchestration. It supplies typed result/concept contracts above Spider Crusader while preserving the canonical Rengine native executor underneath. It is not a second runtime or a copy of Rengine format logic.

The first production C++23 upgrades are:

- `WorkspaceGraph` mutation APIs return typed `std::expected` results;
- stable `AssetId` / `InstanceId` / `BindingId` remain native resource identity;
- MOD composition executes through the typed `spider.cpp23` wrapper and then the existing Crusader/Rengine executor.

These early C++23 modernization pieces predate the formal phase/review-gate workflow and are therefore subject to explicit disposition at Review Gate #41 rather than being accepted merely because they already exist in the branch.

Migration work is tracked through #34, with #35 review/research completed and #36 C++23 build baseline still active until a real exact-head build executes. #46 is the private Project `READ FIRST` context card. #48 owns package/installed-size evidence and the 4 MiB device limit.

## Architecture

```text
resource bytes
  -> bounded probe
  -> NativeModuleRegistry
  -> Spider C++23
      -> Spider Crusader
          -> canonical MOD/SCM/texture/EventTbl modules
          -> pinned Rengine native executor / ReaderCore
  -> DMCNativeReader::Core
      -> resource session
      -> WorkspaceGraph / stable identity
      -> composite model state
      -> composite builder
      -> Rengine-backed MOD attachment resolver
      -> composite placement
      -> scene projection
      -> texture companion binding
      -> Black Widow capability state
      -> renderer / inspection / UV / PNG export
  -> thin Android JNI + Java shell
```

Multi-MOD product composition uses one explicit primary/base MOD: the model already open before additional MOD parts are appended. The builder may resolve each appended MOD's canonical `default_joint_index` against that primary host. It does not infer a different host from filenames, `runtime_metadata_u32`, visual proximity or arbitrary candidate scanning.

Spider session actions are split by responsibility:

- `spider/session_compose_actions.cpp` — composition;
- `spider/session_texture_actions.cpp` — PTX attachment;
- `spider/model_placement_actions.cpp` — explicit placement/reset.

The former monolithic `spider/session_actions.cpp` has been removed from the v33 source tree; its history remains available in Git.

## Canonical Rengine authority

v33 pins `app/src/main/cpp/vendor/dmc-rengine-cpp` to:

`caf445226c7d61841292384a10e93e4f58ae29f9`

That pin contains the canonical read-side MOD cross-model default-joint attachment contract. Native Reader consumes this authority rather than duplicating the selector/index semantics in Android or JNI. The C++23 migration changes only Native Reader; it does not alter the Rengine repository or its language policy.

## Android runtime contract

The canonical APK contains exactly one native runtime DSO:

`lib/arm64-v8a/libdmcviewer.so`

`DMCNativeReader::Core` and `DMCRengine::ReaderCore` link statically into that DSO. Recovery shim/core DSOs, `dlopen` and `dlsym` delegation are rejected. The APK verifier also gates target-scoped C++23, Spider C++, NDK r30, direct Bitmap transport, JNI export parity, 16 KiB ZIP/ELF alignment, absolute package-size/dedup rules and the exact Rengine gitlink.

Package pre-gates are **APK <= 4 MiB, DSO <= 4 MiB and Dex <= 1 MiB**. Duplicate ZIP/runtime payloads and unexplained large duplicate payload waste are not accepted. Physical Samsung acceptance separately requires **installed package/code footprint <= 4 MiB**, excluding mutable user data/cache, tied to the exact reviewed APK SHA-256. `tools/measure_installed_footprint.py` owns only this downstream device measurement.

## Product boundary

DMC Native Reader is read-only. Editing, writing and repacking belong to DMC Rengine / future authoring tooling. HITS, TXT/index, DCA, LIG/LIG2, PAC/PNST, NBZ, MOT, EFM/MRP/SHW and other families are not production modules merely because reverse-engineering work exists for them.

## Documentation

Start with:

- `docs/PROJECT_AI_CONTEXT.md` — private project/AI architecture, standards, evidence rules and review-gate workflow; Project pointer: #46;
- `docs/MODULAR_SPIDER_V33.md` — canonical v33 architecture contract;
- `docs/CXX23_SPIDER_MIGRATION_2026-09-15.md` — C++23 review, research, migration plan and boundaries;
- `docs/STATUS.md` — accepted baseline, candidate and verification state;
- `docs/RESOURCE_DEPENDENCY_GRAPH_V33.md` — v33 resource/dependency model;
- `docs/PUBLIC_RELEASE_CHECKLIST.md` — public-opening/admin history, not v33 release authority;
- `docs/MODULAR_REVIEW_2026-09-15.md` — historical pre-integration modular review snapshot; useful as decision history, not current architecture authority;
- `CHANGELOG.md` — history.

DMC Native Reader is an independent fan-made interoperability/modding project and is not affiliated with Capcom. See `NOTICE.md`.
