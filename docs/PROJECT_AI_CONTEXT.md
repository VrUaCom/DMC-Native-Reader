# DMC Native Reader — Project AI Context, Standards & Rules

Date: 2026-09-16
Scope: `VrUaCom/DMC-Native-Reader` only.
Audience: project owner + AI/engineering agents working inside this private repository/project.

> Read this document before planning or modifying Native Reader. If a task conflicts with this document, stop and resolve the architecture/review conflict before implementation.

## 1. Product role

DMC Native Reader is a **read-only inspection/preview product** built on top of canonical DMC Rengine read-side knowledge. Native Reader must not become a second general reverse-engineering engine or duplicate canonical format/runtime semantics without an explicit bounded owner decision and review.

Canonical ownership direction:

`Rengine knowledge -> Native Reader typed product model -> Spider orchestration -> renderer/inspection/UI`

Editing, canonical writing, repacking and game rebuild do not belong to Native Reader.

## 2. Evidence and semantics

Use evidence-first engineering. Never assign canonical meaning because a field/file/order visually appears plausible.

Evidence states used by the broader project include:
- `EXE_CONFIRMED`
- `CORPUS_CONFIRMED`
- `EXE_AND_CORPUS_CONFIRMED`
- `STRUCTURAL_CONFIRMED`
- `SEMANTIC_CANDIDATE`
- `PRESERVED_UNDECODED`
- `RESERVED_OBSERVED_ZERO`
- `REJECTED`

Unknowns remain unknown until evidence closes them.

### Fail closed
If ownership, attachment, hierarchy, texture binding, selector meaning, resource relation or another runtime relation is unresolved/ambiguous, do not invent an answer. Keep source state, report unresolved status, or require explicit selection.

### Forbidden authority shortcuts
The following are not canonical authority by themselves:
- filename/prefix heuristics;
- picker/array order;
- visual proximity;
- identical RGBA pixels;
- convenient vector indices;
- Java URI ordering;
- guessed resource-family conventions.

They may be candidate evidence only.

## 3. Architecture boundaries

### DMC Rengine boundary
`VrUaCom/dmc-rengine-cpp` is an **absolute read-only external repository** for the Native Reader migration program. Native Reader work must not create or modify Rengine code, CMake, docs, tests, branches, commits, issues, PRs, comments, APIs, targets or submodule-side state.

Rengine remains canonical authority for reverse-engineered format/runtime semantics consumed by Native Reader.

#### Narrow PTX-only copy exception
Viktor explicitly authorized one bounded exception to the normal “consume, do not copy canonical runtime implementation” rule:

Native Reader may read the PTX reverse source/evidence at read-only Rengine commit `50d070e158e484937238d9cb02b2bc6affb2f502`, copy only the PTX runtime slice approved by Project Review #50 into `VrUaCom/DMC-Native-Reader`, port/adapt it to ISO C++23, and maintain that Reader-owned compatibility projection locally.

This authorization:
- does **not** permit any Rengine write;
- applies only to the #50-approved PTX pool/config/placement/minimum-manager-reset slice;
- is not general permission to copy other Rengine subsystems;
- does not authorize a second serialized PTX parser;
- does not authorize reverse harness/evidence tooling in production;
- does not authorize guessed palette/finalizer/materializer/cleanup/lifecycle behavior.

PTX Review #50 completed with `GO_WITH_CORRECTIONS`; implementation #51 is source-complete; Review #52 completed with `ARCHITECTURE GO / EXECUTION_PENDING`.

### Native Reader core
Portable native product logic lives in `DMCNativeReader::Core`.

Normal direction:

`resource bytes -> bounded probe -> NativeModuleRegistry -> canonical adapter/Rengine -> typed IR -> product modules -> renderer/inspection`

### PTX two-layer invariant
Native Reader exposes one PTX product route with two internal responsibilities:

`Serialized PTX framing/TextureSet/DDS + lazy Reader-owned RuntimeCompat`

Rules:
- existing `TextureSlotFramingParser -> TextureSet -> DDS` remains the only serialized/disk-format PTX authority;
- `PtxRuntimeCompat` models only the explicitly imported confirmed runtime state-machine behavior;
- RuntimeCompat is lazy and is not created by ordinary preview/gallery/PNG flow;
- no second user-facing PTX NativeModule is created;
- no `TextureSet::Slot -> runtime 0x50 record` mapping may be inferred until separately evidenced/reviewed;
- initializer represented storage is `0xCB50`, while placement/configure operate on the confirmed `0xCB48` prefix; the final 8 bytes remain opaque clear/preserved tail only;
- palette `0x140331BD0`, finalizer `0x140331A80`, parser/backend runtime mapping `0x1403365B0`, full graphics-config type/live values, actual caller/teardown ordering and full manager acquire/release lifecycle remain deferred unless separately reviewed.

### Spider C++
Spider C++ is the **typed C++23 product orchestration language/profile** of Native Reader.

Authority chain:

`Native Reader product action -> Spider C++ -> Spider Crusader -> existing executor / canonical modules`

Spider may own:
- operations/actions;
- dependency orchestration;
- execution plans/domains;
- typed action state and error propagation;
- bounded compile-time plan validation.

Spider must not own:
- binary-format parsing authority;
- reverse-engineered field semantics;
- a duplicate Rengine executor;
- PTX RuntimeCompat semantics;
- hidden UI/JNI semantic policy.

A future standalone Spider language/compiler requires a separate research/review gate. Do not build one opportunistically inside normal feature work.

### Black Widow
Black Widow is typed application/capability state authority. Platform UI must not reconstruct capabilities from filenames, extensions or resource counts.

### JNI / Android shell — current phase
Until Phase #54, JNI/Java own platform transport/lifecycle/presentation wiring only: URI/FD handling, Bitmap transport, dialogs, Views and Android lifecycle. Do not put DMC parsing, attachment resolution, graph semantics or texture ownership policy in Java/JNI.

### Future native Android shell / Java retirement
Phase #54 begins **only after** Phase 5 stable WorkspaceGraph bindings and Review Gate #44 GO.

Authority split for that phase:
- Spider C++ = product orchestration/actions;
- Black Widow = capability/application-policy authority;
- WorkspaceGraph = stable resource identity/binding authority;
- portable C++ controller = navigation/session/presentation model;
- Android platform layer = lifecycle/window/input/document transport/presentation only.

Preferred target is zero authored Java/Kotlin application source and zero app DEX using supported `android.app.NativeActivity`/NDK contracts. This is evidence-gated, not an unconditional metric: current SAF open/create/tree workflows depend on result-returning Android Intents while documented `ANativeActivityCallbacks` has no `onActivityResult`. #54 must prove a supported public zero-DEX path end-to-end. If required UX cannot be preserved, only a #55-reviewed minimal framework callback shim may remain.

Forbidden shortcuts:
- hidden/private Android APIs;
- reflection hacks;
- generated/obfuscated DEX merely to claim “zero Java”;
- removing required SAF/open/export functionality solely to hit the metric;
- moving DMC semantics into the platform layer.

No Android `Uri`, `Intent`, `Activity`, `View`, `Bitmap` or JNI object may become part of portable Core/WorkspaceGraph/format APIs.

### Session / product modules
`Session` is state/container ownership, not a dumping ground for format/product logic. Composition, placement, texture actions and graph operations belong in bounded modules.

## 4. Source authority vs derived caches

Parsed MOD/SCM/PTX/node/mesh/slot data remain authoritative source-local structures.

Derived-only examples:
- flattened composite mesh;
- world-space preview placement;
- compact renderer texture bank;
- presentation vector order;
- UI projection;
- Reader-owned PTX runtime compatibility state used for explicit runtime inspection/testing.

Derived state must never silently replace source authority. PTX RuntimeCompat must not manufacture a serialized-slot-to-runtime-record relation that has not been proven.

## 5. Stable resource identity

Semantic resource identity must migrate toward native stable IDs:
- `AssetId`
- `InstanceId`
- `BindingId`

Long-term dependency direction:

`typed ResourceAsset -> BindingEdge -> stable ModelInstance[]`

Vector position and Java URI arrays are lifecycle/presentation state, not semantic identity.

Deduplication may reuse identical content/decoded storage, but must not erase logical slot/resource/binding identity or provenance.

## 6. Native language standard

Canonical Native Reader production standard: **ISO C++23**.

Current rule:
- `DMCNativeReader::Core`: C++23;
- Native Reader JNI target: C++23;
- Native Reader native tests: C++23;
- standard is target-scoped in CMake;
- `CXX_STANDARD_REQUIRED ON`;
- `CXX_EXTENSIONS OFF`;
- Gradle must not impose a global `-std=` on vendored dependency targets.

Pinned `DMCRengine::ReaderCore` remains an external read-only target-scoped C++20 dependency and must not be pulled globally into the Native Reader language policy.

C++23 facilities are adopted only when they solve a concrete product/architecture problem. Preferred examples:
- `std::expected` for typed fail-closed result/error APIs;
- `std::byteswap` for explicit endian conversion;
- `std::to_underlying` for typed enum boundaries;
- monadic `optional/expected` flows where they clarify validation;
- ranges only when binary invariants remain obvious.

### Exception boundary contract
Allocating Core/adapters/helpers must have truthful exception specifications. A helper that creates/appends `std::string`, vectors, streams, parser/IR data, decoded images or first-use dynamic plans must not rely on false `noexcept` unless every possible exception is caught locally.

Explicit product/runtime boundaries remain fail-closed:
- `run_decode_pipeline()` is the portable Core catch-all;
- NativeModule `ModuleRun` and Crusader `OperationFn` remain `noexcept` and catch all;
- public Spider actions intentionally marked `noexcept` catch first-use Plan/helper allocation;
- JNI is the final platform catch-all; no C++ exception may cross JNI;
- catch-path fallback must not depend on allocating a diagnostic string.

`docs/NOEXCEPT_BOUNDARY_V33.md` is the current technical evidence note for this contract. #49 is DONE on the source/static side; real compile/CTest evidence remains part of Phase 2.

Do not perform mass syntax modernization for style alone. C++26/C++29 may be researched separately but are not production dependencies for this migration program.

## 7. Android/native runtime and package contract

Canonical current Android architecture:
- exactly one packaged native runtime DSO: `lib/arm64-v8a/libdmcviewer.so`;
- `DMCNativeReader::Core` and `DMCRengine::ReaderCore` link statically into it;
- no recovery shim/core DSO chain;
- no `dlopen`/`dlsym` delegation architecture;
- direct Bitmap transport during the current Java-shell phase;
- JNI export parity;
- 16 KiB ZIP/ELF page-alignment requirements;
- package/native/Dex size budgets remain release gates.

### Weight and duplicate discipline
Application size is an architecture constraint, not a final cleanup task.

Hard package pre-gates for the current v33 Java-shell phase:
- **debug APK <= 4 MiB**;
- **unsigned release APK <= 4 MiB**;
- native DSO <= 4 MiB;
- Dex total <= 1 MiB.

Every exact-head build/review must:
- record APK, native DSO and Dex byte sizes;
- surface largest packaged entries;
- reject duplicate ZIP entry names;
- reject duplicate native/runtime implementations and duplicate `.so`/`.dex` payloads;
- require unexplained large duplicate payload waste = 0;
- reject duplicate CMake source/test entries;
- prefer shared decoded/content storage where exact identity permits it while preserving logical identity/provenance.

Historical v26 growth percentages are not acceptance authority unless exact artifact, packaging and measurement provenance are proven comparable.

### Phase-2 debug / unsigned-release evidence
The canonical Phase-2 runner is `tools/run_phase2_exact_head.py`. It must use the live candidate HEAD and produce two independent SHA-bound verifier reports.

Debug APK must prove:
- package/version/ABI identity;
- expected stable public test signer;
- valid APK v2 signature;
- structurally present APK Signing Block;
- one DSO / JNI export parity;
- ZIP + ELF 16 KiB alignment;
- size and dedup contracts.

Unsigned release APK must independently prove the same package/ABI/JNI/layout/size/dedup contract, plus:
- no valid signer;
- no APK Signing Block;
- no JAR signature material.

A broken/invalid signature is not accepted as “unsigned”. The runner independently validates critical signing fields from verifier JSON before writing the Phase-2 manifest.

### Installed-size hard gate and Android user scope
Android `StorageStats.getAppBytes()` on the acceptance Samsung must be **<= 4 MiB (4,194,304 bytes)** for the exact reviewed installable APK. Mutable data/cache are reported separately and are not part of this code-size gate.

`tools/measure_installed_footprint.py` must:
- use authoritative package StorageStats `code:` bytes;
- fail closed if the metric is unavailable;
- never substitute filesystem `du`;
- require one installed `base.apk`;
- hash installed `base.apk` and compare it to the reviewed local APK SHA-256;
- use the **same explicit Android `--user` scope** for `pm path` and `pm get-package-storage-stats`;
- default to `current` unless an explicit numeric user/profile is intentionally targeted;
- record requested/resolved/current user identity, device identity, build fingerprint and package version.

`APK <=4 MiB` is necessary but not sufficient: optimized/runtime artifacts can still make installed app bytes exceed the hard gate.

## 8. Repository hygiene

Work only in the explicitly authorized repository/branch set.

For the current migration program:
- repository: `VrUaCom/DMC-Native-Reader` only;
- current candidate branch: `feature/png-export-multi-mod-v27`;
- `VrUaCom/dmc-rengine-cpp` remains absolute READ-ONLY, including during the PTX copy exception;
- do not create a new repository or branch without a separate technical reason;
- do not duplicate modules, parsers, executors, workflows or compatibility files;
- remove dead duplicates once the replacement is canonical and Git history preserves the old version;
- keep changes bounded and reviewable.

Canonical PTX addendum files:
- `PTX_RUNTIME_IMPORT_DECISION_V33.md`;
- `PTX_RUNTIME_IMPORT_ARCHITECTURE_V33.md`;
- `PTX_RUNTIME_IMPORT_TZ_V33.md`;
- `PTX_RUNTIME_IMPORT_REVIEW_TEMPLATE_V33.md`;
- `PTX_RUNTIME_IMPORT_STATUS_V33.md`.

Do not recreate deleted marker/sync/queue/status-fragment documents. Execution state belongs in Project issues #50/#51/#52 and the canonical status file.

### Migration batching / CI budget discipline
During a large migration such as the C++23 transition, do **not** spend hosted-runner minutes on every intermediate commit.

Canonical migration mode:
- keep the migration PR in draft while implementation/evidence is incomplete;
- heavy PR workflows must not run on every draft `synchronize` event;
- use one deliberately chosen exact-head checkpoint;
- do not manually rerun known runner-less jobs repeatedly;
- preserve static review/regression authoring without weakening final evidence.

Known hosted failure signature is `runner_id=0`, `steps=[]`: this is infrastructure evidence only, never compile/test PASS or FAIL.

If hosted capacity remains unavailable, an authorized Ubuntu/WSL2 x64 execution of `tools/bootstrap_phase2_self_hosted_ubuntu.sh` + `tools/run_phase2_exact_head.py` is acceptable, provided it uses the live exact candidate HEAD and complete canonical contract.

## 9. Testing is architecture

A promoted semantic rule/module/API must have regression coverage. Compilation alone is not acceptance.

Important v33/C++23 gates include:
- `cxx23_profile_test`;
- `workspace_graph_test`;
- composite builder/placement regressions;
- PTX transaction regression;
- `ptx_runtime_compat_test` for the approved runtime slice and `0xCB48` placement boundary;
- SCM authority regression;
- module/Spider/texture/PNG/render/inspection regressions;
- `tools/test_verify_device_apk.py` for package/signing/dedup/4 MiB/user-scope policy;
- exact APK verifier for **both** Phase-2 APKs;
- physical Samsung acceptance for the final production-signed release candidate.

PTX #52 architecture acceptance does **not** mean the new runtime regression has executed. Until the final exact-head CMake/CTest checkpoint runs, its execution status remains pending.

## 10. Release and artifact-identity rule

Never promote based on stale SHA or an unexecuted workflow.

Keep three artifact identities separate:
1. **debug/device-test APK** — test-signed diagnostic/installable artifact;
2. **unsigned release APK** — pre-signing structural/package evidence;
3. **production-signed release APK** — final installable Samsung/promotion artifact.

Production signing changes APK bytes and SHA-256. Therefore:
- never use the unsigned release SHA as final Samsung/promotion identity;
- after production signing, rerun the full applicable package/ABI/JNI/ZIP/ELF/16 KiB/dedup/size verifier on the signed APK;
- verify the expected production certificate SHA-256, not merely “not the debug signer”;
- record the exact post-signing APK SHA-256;
- #45 and Samsung acceptance must use that same signed artifact/hash;
- publication must consume that exact verified signed artifact rather than rebuild/re-sign a lookalike.

Final device measurement must call `tools/measure_installed_footprint.py` with `--expected-apk-sha256 <post-signing-sha256>` and explicit `--user` scope. Installed `base.apk` SHA must equal the reviewed post-signing APK SHA.

Required promotion evidence eventually includes:
1. exact-head host build/tests actually execute and pass;
2. clean Android debug + unsigned release structural evidence;
3. production-signed post-signing verifier evidence;
4. exact package/hash/size/dedup metrics;
5. physical Samsung scenarios + `StorageStats.getAppBytes()` <=4 MiB on the exact signed artifact;
6. Viktor explicitly approves merge/release.

## 11. Mandatory Project task format

Every Project phase/subtask must contain:
1. **Objective** — intended outcome.
2. **Substages** — bounded research/implementation slices where useful.
3. **Additional work / evidence needed** — missing research, access, measurements, builds or device evidence.
4. **AI ТЗ / execution prompt** — copy-ready instruction another AI/engineer can execute without guessing.
5. **Constraints / authority boundaries** — forbidden scope/shortcuts.
6. **Exit criteria** — proof required to close.
7. **Next review gate** — mandatory review before the next implementation phase.

## 12. Review-gate workflow

A completed phase does **not** directly unlock the next phase. It unlocks a review gate.

Each review gate must:
- inspect exact HEAD and real evidence;
- compare implementation against architecture/acceptance criteria;
- classify findings as `BLOCKER / CORRECTION / OPTIMIZATION / DEFERRED`;
- re-read and update the next phase specification;
- split/add subtasks if needed;
- issue explicit `GO` or `NO-GO`.

On `NO-GO`, return work to the responsible phase. Do not bypass the gate.

Current program flow:

`#35 -> #36 -> (#49 + #50 -> #51 -> #52) -> #41 -> #37 -> #42 -> #38 -> #43 -> #39 -> #44 -> #54 -> #55 -> #40 -> #45`

Current state:
- #35 complete;
- #49 complete on source/static hardening;
- #50 complete: `GO_WITH_CORRECTIONS`;
- #51 source complete;
- #52 complete: `ARCHITECTURE GO / EXECUTION_PENDING`;
- #36 source/static ready but open because #47 real exact-head execution is unresolved;
- #48 waiting for real artifact/device evidence;
- #41 is blocked until #36 has one real complete evidence set;
- #54 is future platform migration and cannot start before #44 GO.

Master tracker: `#34`. Entry card: `#46`.

## 13. AI decision rule

Before implementing any fix:
1. identify the correct authority layer;
2. review current architecture and evidence;
3. check whether an existing module already owns the responsibility;
4. reject shortcuts that duplicate authority, hide ambiguity or push game semantics into UI/JNI;
5. make the smallest bounded change that preserves source authority;
6. add regression/evidence;
7. update the Project task and next review gate.

Working principle:

**Rengine knows canonical game semantics. Native Reader models/presents them. The explicit PTX RuntimeCompat exception is a bounded Reader-owned projection of reviewed evidence, not a second general reverse engine. Spider organizes product execution. Black Widow governs capabilities. WorkspaceGraph owns stable product identity. The Android platform layer transports and presents. No layer silently substitutes itself for another.**
