# DMC Native Reader — Project AI Context, Standards & Rules

Date: 2026-09-15
Scope: `VrUaCom/DMC-Native-Reader` only.
Audience: project owner + AI/engineering agents working inside this private repository/project.

> Read this document before planning or modifying Native Reader. If a task conflicts with this document, stop and resolve the architecture/review conflict before implementation.

## 1. Product role

DMC Native Reader is a **read-only inspection/preview product** built on top of canonical DMC Rengine read-side knowledge. Native Reader must not become a second reverse-engineering engine or duplicate canonical format/runtime semantics.

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
Rengine remains canonical authority for reverse-engineered format/runtime semantics consumed by Native Reader. Do not modify another repository as part of a Native Reader task unless Viktor explicitly authorizes it.

### Native Reader core
Portable native product logic lives in `DMCNativeReader::Core`.

Normal direction:

`resource bytes -> bounded probe -> NativeModuleRegistry -> canonical adapter/Rengine -> typed IR -> product modules -> renderer/inspection`

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
- hidden UI/JNI semantic policy.

A future standalone Spider language/compiler requires a separate research/review gate. Do not build one opportunistically inside normal feature work.

### Black Widow
Black Widow is typed application/capability state authority. Platform UI must not reconstruct capabilities from filenames, extensions or resource counts.

### JNI / Android shell
JNI/Java own platform transport/lifecycle only: URI/FD handling, Bitmap transport, dialogs/presentation wiring. Do not put DMC parsing, attachment resolution, graph semantics or texture ownership policy in Java/JNI.

### Session / product modules
`Session` is state/container ownership, not a dumping ground for format/product logic. Composition, placement, texture actions and graph operations belong in bounded modules.

## 4. Source authority vs derived caches

Parsed MOD/SCM/PTX/node/mesh/slot data remain authoritative source-local structures.

Derived-only examples:
- flattened composite mesh;
- world-space preview placement;
- compact renderer texture bank;
- presentation vector order;
- UI projection.

Derived state must never silently replace source authority.

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

C++23 facilities are adopted only when they solve a concrete product/architecture problem. Preferred examples:
- `std::expected` for typed fail-closed result/error APIs;
- `std::byteswap` for explicit endian conversion;
- `std::to_underlying` for typed enum boundaries;
- monadic `optional/expected` flows where they clarify validation;
- ranges only when binary invariants remain obvious.

Do not perform mass syntax modernization for style alone.

C++26/C++29 may be researched separately but are not production dependencies for this migration program.

## 7. Android/native runtime contract

Canonical Android architecture:
- exactly one packaged native runtime DSO: `lib/arm64-v8a/libdmcviewer.so`;
- `DMCNativeReader::Core` and `DMCRengine::ReaderCore` link statically into it;
- no recovery shim/core DSO chain;
- no `dlopen`/`dlsym` delegation architecture;
- direct Bitmap transport;
- JNI export parity;
- 16 KiB ZIP/ELF page-alignment requirements;
- package/native/Dex size budgets remain release gates.

### Weight and duplicate discipline
Application size is an architecture constraint, not a final release cleanup task.

Every exact-head build/review must:
- record APK, native DSO and Dex byte sizes;
- preserve hard package/native/Dex budgets unless a review explicitly changes them with evidence;
- record size delta against the accepted baseline when one exists;
- surface the largest packaged entries so unexpected growth is attributable;
- reject duplicate ZIP entry names;
- reject duplicate native/runtime implementations and duplicate `.so`/`.dex` payloads;
- treat large identical packaged payloads as a blocker until deduplicated or explicitly justified by review;
- reject duplicate CMake source/test entries rather than compiling the same responsibility twice;
- prefer shared decoded/content storage where exact identity permits it, while preserving logical resource/slot/binding identity and provenance.

Do not trade modularity for duplicated binaries, duplicated decoded banks, copied parsers, copied executors or parallel compatibility implementations. A smaller package is not allowed to erase semantic identity; deduplication must happen at the correct ownership/storage layer.

## 8. Repository hygiene

Work only in the explicitly authorized repository/branch set.

For the current migration program:
- repository: `VrUaCom/DMC-Native-Reader` only;
- current candidate branch: `feature/png-export-multi-mod-v27`;
- do not create a new repository or branch without a separate technical reason;
- do not duplicate modules, parsers, executors, workflows or compatibility files;
- remove dead duplicates once the replacement is canonical and Git history preserves the old version;
- keep changes bounded and reviewable.

## 9. Testing is architecture

A promoted semantic rule/module/API must have regression coverage. Compilation alone is not acceptance.

Important v33/C++23 gates include:
- `cxx23_profile_test`;
- `workspace_graph_test`;
- composite builder/placement regressions;
- PTX transaction regression;
- SCM authority regression;
- module/Spider/texture/PNG/render/inspection regressions;
- exact APK verifier;
- physical Samsung acceptance for the release candidate.

## 10. Release/evidence rule

Never promote based on stale SHA or an unexecuted workflow.

Required promotion evidence:
1. exact-head host build/tests actually execute and pass;
2. clean Android debug/release build;
3. package/APK verifier passes;
4. exact APK hash and package-size/dedup metrics are recorded;
5. required physical Samsung scenarios pass on that artifact;
6. Viktor explicitly approves merge/release.

`runner_id=0`, `steps=[]`, skipped workflows or pre-run infrastructure failures are **not** compile/test evidence.

## 11. Mandatory Project task format

Every Project phase/subtask must contain:
1. **Objective** — intended outcome.
2. **Substages** — bounded research/implementation slices where useful.
3. **Additional work / evidence needed** — missing research, access, measurements, builds or device evidence.
4. **AI ТЗ / execution prompt** — copy-ready prompt another AI/engineer can execute without guessing.
5. **Constraints / authority boundaries** — forbidden scope/shortcuts.
6. **Exit criteria** — proof required to close.
7. **Next review gate** — mandatory review before the next implementation phase.

## 12. Review-gate workflow

A completed phase does **not** directly unlock the next phase. It unlocks a review gate.

Each review gate must:
- inspect exact HEAD and real evidence;
- compare implementation against architecture/acceptance criteria;
- classify findings as blocker / correction / optimization / deferred;
- re-read and update the next phase specification;
- split/add subtasks if needed;
- issue explicit `GO` or `NO-GO`.

On `NO-GO`, return work to the responsible phase. Do not bypass the gate.

Current program flow:

`#35 -> #36 -> #41 -> #37 -> #42 -> #38 -> #43 -> #39 -> #44 -> #40 -> #45`

Master tracker: `#34`.

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

**Rengine knows canonical game semantics. Native Reader models/presents them. Spider organizes product execution. Black Widow governs capabilities. JNI/UI transport and display. WorkspaceGraph owns stable product identity. No layer silently substitutes itself for another.**
