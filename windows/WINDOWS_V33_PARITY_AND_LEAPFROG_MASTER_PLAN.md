# DMC Native Reader for Windows — v33 Parity + Desktop Leapfrog Master Contract

Date: 2026-09-20
Repository: `VrUaCom/DMC-Native-Reader`
Primary target: Windows x64
Owner intent: Windows must first reach the current Android product level, then deliberately move ahead of Android as the primary desktop inspection/workbench surface.

> This document is an implementation contract, roadmap, acceptance contract, and copy-ready agent prompt. It is not evidence that any listed future capability already exists.

## 0. Mission

The Windows build must not remain a frozen v24/v28-style preview. It must become the most capable DMC Native Reader front end while still sharing the same portable product architecture as Android.

Target direction:

`DMC Rengine canonical read-side knowledge -> DMCNativeReader::Core -> WorkspaceGraph -> Spider C++ -> Black Widow -> platform shell`

Windows is allowed to exceed Android in desktop UX, orchestration, visualization, diagnostics, workspace management and inspection throughput. Windows is **not** allowed to create a second format/runtime semantic authority.

Native Reader remains read-only. Editing, canonical writing, archive repacking and game rebuild belong to DMC Rengine, not Native Reader.

## 1. Source of truth and current snapshot

### 1.1 Live authority rule

Do not treat SHAs in this document as permanent authority. Immediately before implementation:

1. resolve current `main` HEAD;
2. resolve PR #33 live `head_sha` from `feature/png-export-multi-mod-v27`;
3. read `docs/PROJECT_AI_CONTEXT.md`, `docs/STATUS.md`, `docs/MODULAR_SPIDER_V33.md` and active project issues under #34;
4. compare Windows `main` with the live PR #33 HEAD;
5. record the exact identities used for the work.

Historical comments, old release text, old APK metadata and copied SHAs never override the live repository state.

### 1.2 Snapshot on 2026-09-20

- published Windows preview tag: `windows-v1.0.0-preview`;
- preview release commit: `c74589fe27e405341489cdea56e5227215da1612`;
- Windows release text conservatively describes v24 parity, but its history already contains accepted v26 and the device-confirmed v28/1.0.1 PNG-export hotfix;
- current `main` after additional Windows work: `6a982756917e3b84a6402441960db40e16fe15af` at the time this contract was written;
- current v33 development line: PR #33 / `feature/png-export-multi-mod-v27`; live HEAD must be re-read before work;
- v33 product identity currently documented as `1.0.6 / 33`;
- Native Reader product code in v33 targets strict target-scoped ISO C++23;
- pinned DMC Rengine `ReaderCore` remains a read-only target-scoped C++20 dependency;
- v33 production module direction: MOD / SCM / DDS / PTX / EventTbl;
- v33 carries WorkspaceGraph, composite state, Spider C++, canonical MOD/SCM authority, PTX RuntimeCompat, multi-MOD composition/placement and a larger regression contract.

### 1.3 Windows work already on current main and must not regress

Preserve or improve all already-landed Windows behavior, including:

- native Win32 shell over `DMCNativeReader::Core`;
- full-bleed dark viewer;
- MOD/SCM interactive 3D;
- drag rotation and wheel zoom;
- wireframe / hierarchy / bounds / skin / normals / UV overlays where capability-authorized;
- DDS/PTX preview;
- PTX companion attachment;
- child-resource navigation;
- UV gallery and inspection;
- PNG export;
- per-user Explorer/Open-With integration;
- right-click inspection analogues to Android long-press;
- F11/Escape fullscreen;
- sibling file navigation;
- corrected aspect-ratio handling;
- standalone DDS companion fallback;
- real perspective camera in the shared renderer.

These are baseline assets. Do not throw them away while importing v33 architecture.

## 2. Non-negotiable architecture boundaries

### 2.1 One portable product core

Windows and Android must consume the same portable Native Reader product implementation. Platform shells may differ in:

- window/lifecycle handling;
- file picker and drag/drop transport;
- keyboard/mouse/touch input;
- desktop layout;
- file-system integration;
- presentation backend;
- accessibility/platform services.

They may **not** differ in:

- DMC binary-format semantics;
- MOD/SCM/PTX runtime interpretation;
- texture ownership rules;
- WorkspaceGraph identity/binding rules;
- Spider product operation semantics;
- Black Widow capability policy;
- scene-composition semantics;
- canonical hierarchy/placement meaning.

### 2.2 Rengine remains canonical and read-only

`VrUaCom/dmc-rengine-cpp` is read-only from Native Reader work. Consume the pinned ReaderCore or explicitly reviewed APIs. Do not modify Rengine code, branches, tests, docs, issues or semantics from this Windows task.

The only existing bounded exception remains the reviewed Reader-owned PTX RuntimeCompat import already governed by #50/#51/#52. Do not widen it.

### 2.3 Fail closed

Never infer semantic ownership from filenames, picker order, vector index, URI order, visual proximity or convenient UI state.

If attachment, hierarchy, resource binding, texture ownership, skeleton relation or runtime mapping is unresolved, preserve the unresolved state and require explicit user selection or future evidence.

### 2.4 Windows shell is a host, not a parser

`win_shell.cpp` and any future Windows platform layer may orchestrate UI and transport only. No raw DMC header offsets, ad-hoc format parsers, guessed companion rules or copied Android business logic may accumulate there.

## 3. End-state architecture

Target Windows architecture:

```text
Windows filesystem / Explorer / drag-drop / file picker
  -> Windows transport adapter
  -> portable WorkspaceController
  -> WorkspaceGraph stable identities/bindings
  -> Spider C++ operations
  -> Black Widow capability state
  -> DMCNativeReader::Core
      -> NativeModuleRegistry
      -> canonical MOD/SCM/DDS/PTX/EventTbl adapters
      -> DMC Rengine ReaderCore
      -> composite scene / placement
      -> inspection / UV / image / report
      -> shared render scene
  -> Windows presentation
      -> native desktop UI
      -> CPU renderer now
      -> optional hardware presentation/render backend later
```

Android must be able to reuse all portable advances made for Windows without reimplementation.

## 4. Definition of parity with Android v33

Windows is not at parity merely because it can open the same extensions. It reaches parity only when the portable semantics and product behavior match the current v33 candidate.

Required parity areas:

### 4.1 Toolchain and core

- Native Reader Windows targets strict ISO C++23;
- MSVC/CMake target-scoped standard configuration, no global language-mode hacks;
- pinned Rengine ReaderCore remains isolated as its supported C++20 dependency;
- all portable core sources used by Android v33 are compiled for Windows unless explicitly platform-only;
- no duplicate Windows-only copies of portable modules.

### 4.2 Module registry

Bring Windows to the same promoted module set as the current v33 authority:

- MOD;
- SCM;
- DDS;
- PTX;
- EventTbl.

Any additional format remains unsupported until separately promoted with evidence and regression coverage.

### 4.3 Canonical MOD and SCM

Windows must consume the same canonical adapters and authority chain as Android v33, including:

- canonical geometry;
- canonical hierarchy/spatial projection;
- canonical texture-slot / legacy GS state where promoted;
- canonical SCM world transforms and object binding;
- canonical topology/break behavior;
- no legacy local decoder resurrection.

### 4.4 Multi-MOD composition

Implement full v33 composite behavior on Windows:

- open/add multiple MOD parts;
- retain source-local authoritative scenes;
- stable part identity independent of UI order;
- one derived flattened render projection;
- global texture-slot remapping without destroying local ownership;
- explicit primary host;
- canonical default-joint resolution where current v33 authority permits it;
- fail closed when host/joint/spatial authority is absent or invalid;
- reset/rebuild derived projection without reparsing source bytes.

### 4.5 Texture companions

Windows must match current portable companion semantics:

- PTX attachment to single models;
- PTX attachment to an explicitly selected part of a composite;
- standalone DDS companion where current shared core permits it;
- per-part texture ownership via native stable identity, not UI index;
- partial composite texturing when permitted by native policy;
- no automatic guessed PTX-to-part binding.

### 4.6 PNG and gallery export

Match the accepted product semantics:

- single UV PNG export;
- UV-gallery export-all;
- single DDS/PTX image export;
- PTX-gallery export-all;
- deterministic filenames that preserve source/slot context;
- no Java-only export semantics;
- Windows chooses native filesystem destinations through desktop UX.

### 4.7 WorkspaceGraph

Windows must consume stable native resource identity and bindings rather than implementing its own file-list semantics.

Required behaviors:

- asset identity;
- instance identity where applicable;
- binding identity;
- stale-ID rejection;
- stable behavior under UI reorder;
- explicit replacement/reset lifetime;
- provenance path kept separate from semantic identity.

### 4.8 Spider C++

Windows product actions must use the same typed Spider operation/orchestration layer accepted for v33.

Spider may orchestrate operations and dependencies. It may not become a format parser or second executor.

### 4.9 Black Widow

All Windows controls, menus, shortcuts and context actions must be gated from native capability state. Do not enable actions from extension strings or hand-maintained Windows booleans when Black Widow already owns that decision.

### 4.10 PTX RuntimeCompat

Integrate the same lazy Reader-owned RuntimeCompat boundaries as v33 without replacing the serialized `TextureSlotFramingParser -> TextureSet -> DDS` authority and without inventing the still-unproven serialized-slot-to-runtime-record mapping.

## 5. Windows leapfrog target — Windows must go beyond Android

Parity is an intermediate checkpoint, not the finish line.

Windows is considered **ahead of Android** only when all parity requirements above are satisfied and the Windows product adds a coherent desktop workbench layer that Android does not currently provide.

### 5.1 Mandatory desktop-first leapfrog features

These are the first Windows-exclusive targets and should be implemented before declaring the Windows catch-up complete:

#### A. Drag & drop

- drop one DMC resource into the viewer;
- drop multiple MOD resources to create/extend a composite through the same native operation path as picker-based composition;
- drop PTX/DDS onto a model and require explicit part selection when ownership is ambiguous;
- drop folders to open/index a workspace, never to guess semantic bindings.

#### B. Workspace/folder mode

Create a desktop workspace shell over native resource identity:

- open a folder as a browsing workspace;
- recursive file discovery with bounded/cancellable scanning;
- filter by promoted resource family;
- keep unsupported files visible as unsupported/staged rather than falsely decoded;
- recent workspaces;
- restore presentation state only where identity remains valid;
- do not invent persistent semantic graph serialization unless separately reviewed.

#### C. Multi-session tabs

- multiple independent open sessions/workspaces;
- tabs for models, textures, event tables and reports;
- unsaved state is presentation/session state only because Native Reader is read-only;
- closing a tab must release native session resources deterministically;
- no cross-tab attachment by accidental vector/index reuse.

#### D. Desktop dependency graph visualization

Build a visual graph from WorkspaceGraph authority:

- nodes: assets/instances where native graph exposes them;
- edges: explicit native bindings/dependencies only;
- click node -> inspect resource;
- click edge -> inspect binding provenance/status;
- unresolved candidate relationships shown as unresolved, never promoted by the UI;
- graph filtering by family/status.

#### E. Semantic comparison / diff

Add a read-only comparison workbench:

- compare two MOD/SCM/PTX/EventTbl resources at typed inspection level;
- compare object/mesh/node/slot counts and native typed fields;
- compare hierarchy/transform/binding state when canonical;
- compare texture dimensions/format/slot occupancy for texture resources;
- export a machine-readable and human-readable diff report;
- raw byte diff may be offered as supplementary evidence, not as semantic authority.

#### F. Batch export and report pipeline

Desktop batch operations through Spider/native product actions:

- export all UV maps in a workspace selection;
- export all promoted image children;
- export inspection reports;
- export WorkspaceGraph/dependency reports;
- export semantic diff reports;
- bounded progress/cancellation;
- no format-specific parsing in Windows UI.

#### G. Headless CLI companion

Provide a Windows command-line frontend over the same core for automation and regression workflows, for example:

```text
dmc-native-reader.exe inspect <file>
dmc-native-reader.exe report <file> --json <out>
dmc-native-reader.exe uv-export <file> --out <dir>
dmc-native-reader.exe texture-export <file> --out <dir>
dmc-native-reader.exe diff <a> <b> --json <out>
dmc-native-reader.exe workspace-scan <folder> --json <out>
```

Exact CLI names/options may change, but semantics must delegate to portable operations rather than duplicate them.

#### H. Hot reload / file watcher

- watch open source files for external change;
- surface changed-on-disk state;
- explicit reload, never silent semantic replacement;
- reparse through the canonical pipeline;
- invalidate stale native identities safely;
- preserve camera/presentation state only when safe;
- never auto-reattach companions by guessed path/name after reload.

#### I. Windows diagnostics

- deterministic log file with build SHA and module/toolchain identity;
- copyable diagnostic report;
- native crash minidump path for Windows release/debug builds;
- rejected-resource reason reporting;
- capability snapshot;
- loaded-source hashes where practical;
- no private user file contents copied into reports unless explicitly requested.

### 5.2 Strong desktop features after first leapfrog

After the mandatory leapfrog set, continue with:

- searchable inspector tree with typed field filtering;
- dockable panels / layout presets;
- side-by-side synchronized 3D comparison;
- selection highlighting of objects/meshes/nodes/triangles;
- click-to-inspect from viewport;
- hierarchy tree synchronized with viewport;
- texture-slot panel synchronized with selected mesh;
- composite-part panel with explicit host/attachment state;
- bookmarks/pins for inspection locations;
- measurement tools for geometry/bounds;
- screenshot/export of viewport with metadata;
- configurable keyboard shortcuts;
- high-DPI/per-monitor DPI support;
- accessibility/keyboard navigation;
- session history and recent files;
- optional portable workspace manifest only after a separately reviewed persistence schema.

## 6. Rendering direction

### 6.1 Preserve correctness first

Keep the corrected shared perspective camera and current CPU rendering path as the reference behavior while parity work is in flight.

### 6.2 Desktop hardware backend roadmap

Windows may later add a hardware backend (preferably a bounded Direct3D 11/12 path) for presentation/performance, but it must consume the same `RenderScene`/typed scene state.

Rules:

- no format parsing in GPU backend;
- no GPU-only semantic interpretation;
- reference CPU path remains available for deterministic regression until replacement is proven;
- compare camera, hierarchy and texture-slot outputs against the reference path;
- wireframe/hierarchy/UV/debug overlays retain equivalent meaning;
- hardware backend must not block core inspection if GPU initialization fails.

Future renderer work may include:

- GPU vertex/index upload;
- perspective-correct interpolation;
- mip/filter state matching where canonical;
- alpha/blend diagnostics;
- normal visualization;
- skeletal debug drawing;
- offscreen high-resolution export;
- frame-time / geometry statistics.

## 7. Future capability roadmap beyond current v33

These capabilities are directional backlog. They are **not** automatically supported by adding UI buttons. Each must pass its own native authority/evidence gate.

### 7.1 Animation / MOT

Target when canonical MOT/runtime authority is promoted:

- open MOT-family resources;
- typed motion inspection;
- motion list/strip;
- explicit model/skeleton target binding;
- timeline;
- play/pause/scrub;
- frame stepping;
- root-motion inspection;
- bone transform visualization;
- side-by-side base pose vs animated pose;
- export motion inspection/report data.

Until then, MOT may only be staged/identified where current product policy allows; no fake playback.

### 7.2 Physics / cloth / secondary motion

Future only after format/runtime authority exists:

- inspect physics/cloth resources;
- explicit binding to model/skeleton parts;
- visualize constraints/anchors/collision primitives;
- toggle simulation/debug layers;
- deterministic step mode;
- report unresolved bindings.

No guessed cloth/physics simulation.

### 7.3 HITS collision

Promote only through native Architecture-v2 style review:

- collision resource open/inspection;
- collision primitive/record visualization;
- overlay collision over compatible model/stage resources only with proven binding;
- record filtering and inspection;
- corpus/regression coverage.

### 7.4 Stage TXT / index / DCA / LIG/LIG2

Future read-only inspection may include:

- typed stage-set/config inspection;
- token/state reports;
- door/effect/config relationships where evidence-backed;
- light/config record visualization;
- cross-resource graph edges only when canonical;
- no `.index` runtime-manifest fiction; keep extraction/naming metadata separate from runtime resolution.

### 7.5 PAC / PNST / NBZ / AFS container browsing

Longer-term desktop reader target:

- read-only container tree;
- slot-preserving identity/display;
- lazy child extraction;
- nested navigation;
- single child export;
- resource graph projection;
- provenance and container offsets;
- unsupported/unknown payload preservation and inspection.

Writing/repacking remains DMC Rengine, not Native Reader.

### 7.6 EFM / MRP / SHW and other researched families

Only promote formats individually when:

- canonical/evidence-backed reader exists;
- typed IR exists;
- malformed input rejection exists;
- regression corpus exists;
- UI behavior is capability-driven;
- product owner explicitly accepts promotion.

### 7.7 Stage/workspace visualization

Long-term Windows advantage:

- workspace-level view of DData/GData/resource families;
- stage resource dependency graph;
- model/texture/effect/config/collision grouping;
- scene preview assembled only from confirmed bindings;
- unresolved links visible as unresolved;
- jump from graph node to resource inspector.

### 7.8 DMC Rengine handoff

Native Reader Windows should become the fastest inspection/research front end for DMC assets. When a user needs editing/rebuild/repack, provide a clean handoff path to DMC Rengine rather than turning Native Reader into a competing writer.

Possible future handoff:

- copy canonical resource identity/path;
- open same source in DMC Rengine;
- export inspection evidence/report for Rengine;
- never silently modify original game data from Native Reader.

## 8. Work program

### W0 — Audit and freeze

Before changing implementation:

- resolve live `main` and PR #33 HEAD;
- produce a Windows-vs-v33 capability matrix;
- classify each changed file as portable core, Android-only, Windows-only, build/evidence, docs;
- identify Windows-specific commits that must survive integration;
- list current Windows release acceptance gaps;
- run all currently available Windows/native tests;
- do not begin by blindly rebasing hundreds of commits.

Output: exact integration plan and regression baseline.

### W1 — Bring v33 portable architecture to Windows

Integrate, in reviewable slices:

- C++23 Native Reader target policy;
- current canonical MOD/SCM adapters;
- EventTbl module;
- current WorkspaceGraph;
- current composite builder/placement;
- current Spider C++ actions;
- current Black Widow state;
- PTX RuntimeCompat;
- current texture/model binding behavior;
- current v33 core tests that are portable.

Preserve existing Windows camera/UI/export improvements.

Exit criteria: Windows compiles the same portable v33 product core without Windows-specific semantic forks.

### W2 — Product parity UI

Expose every current v33 product capability through native Windows UX:

- open/replace resource;
- multi-MOD add/remove/inspect;
- explicit primary host/part choice where required;
- PTX/DDS attachment and part choice;
- staged companion list;
- EventTbl inspection;
- gallery navigation;
- export-all;
- typed inspection;
- capability-driven controls.

Exit criteria: no meaningful portable v33 action is Android-only merely because Windows UI has no entry point.

### W3 — Parity acceptance

Run the parity matrix against representative corpus on Windows. Every item must be PASS, explicitly NOT_APPLICABLE, or blocked by a documented upstream/canonical limitation. No silent gaps.

### W4 — Mandatory desktop leapfrog

Implement at least the mandatory desktop features in §5.1, prioritizing:

1. drag/drop;
2. folder workspace;
3. multi-session tabs;
4. WorkspaceGraph visualization;
5. semantic diff;
6. batch export/report;
7. headless CLI;
8. hot reload;
9. diagnostics/minidump.

Do not stop merely because Android parity is reached.

### W5 — Windows release hardening

Produce a new Windows x64 preview/release candidate only after parity + leapfrog evidence exists.

Required release package direction:

- `DMC-Native-Reader.exe`;
- only required runtime DLLs;
- file association scripts or integrated per-user registration;
- version/build identity;
- SHA-256 manifest;
- concise README;
- no stale Android-v24 claim;
- release notes with exact supported module list and explicit unsupported/future list.

## 9. Windows build and test contract

### 9.1 Build

- x64;
- current supported MSVC toolset;
- CMake;
- Native Reader targets use strict C++23;
- warnings enabled appropriately for MSVC;
- `UNICODE`, `_UNICODE`, `NOMINMAX` only where platform layer requires them;
- no Android headers/libs leak into portable core;
- no Windows headers leak into portable public core.

### 9.2 Regression inheritance

The Windows program must inherit the live accepted portable regression suite from the v33 line. Do not hard-code an obsolete test count from this file.

Rules:

- every portable accepted test remains registered/executed;
- platform-inapplicable Android tests are classified, not silently dropped;
- add Windows-specific tests for shell/controller/drag-drop/workspace/CLI where feasible;
- no accepted semantic regression is waived to make Windows build.

### 9.3 New Windows-specific tests

At minimum add deterministic coverage for:

- command-line parsing;
- supported extension/family routing through native registry;
- drag/drop action classification;
- multi-file composite dispatch;
- explicit companion-part selection requirement;
- workspace scan filtering;
- stable identity under UI sort/reorder;
- hot-reload stale-ID invalidation;
- batch export naming/collision behavior;
- CLI exit codes and machine-readable report schema;
- Windows file-association registration logic where testable;
- camera/render-size behavior;
- PNG export.

## 10. Corpus acceptance matrix

Use real representative resources, not synthetic-only tests.

Acceptance families:

- MOD single model;
- MOD multi-part composite;
- SCM stage/scene model;
- DDS standalone;
- PTX multi-texture bundle;
- valid PTX/DDS companion;
- EventTbl representative;
- malformed/truncated cases for each promoted family;
- unsupported format file;
- mixed multi-select that must fail closed;
- ambiguous companion ownership requiring explicit selection.

For each record:

- source SHA-256;
- product build SHA;
- operation;
- expected capability state;
- actual result;
- inspection/render/export result;
- PASS/FAIL;
- notes.

## 11. Performance and stability goals

Windows leapfrog should also exploit desktop resources without compromising correctness.

Goals:

- responsive UI during large folder scans and export batches;
- bounded memory for large PTX galleries;
- cancellation of long scans/exports;
- no unbounded duplicate scene copies;
- deterministic native session destruction;
- no stale file handles after reload/close;
- no UI-thread parsing of large workspace scans when avoidable;
- crash diagnostics with exact build identity;
- large-workspace stress test.

Optimization rule: measure before changing semantic code. Prefer architecture improvements over hidden caches keyed by filenames or unstable indices.

## 12. UX direction

Windows should no longer be a phone UI copied into a desktop window. Keep visual consistency with Android, but use desktop affordances.

Recommended layout:

```text
+--------------------------------------------------------------+
| header: workspace / tab / source / primary actions           |
+----------------------+---------------------------------------+
| workspace/resource   |                                       |
| tree / graph / parts |          main viewport                |
|                      |      3D / texture / UV / diff          |
|                      |                                       |
+----------------------+----------------------+----------------+
| inspector/properties | timeline/companions  | status/evidence|
+--------------------------------------------------------------+
```

Panels should be hideable. F11 keeps a distraction-free full viewport. Current lightweight full-bleed mode must remain available.

## 13. Evidence/status language

Do not report a feature as implemented because:

- a button exists;
- an extension is registered;
- a file opens without error;
- a code path compiles;
- a synthetic test alone passes.

Use explicit status:

- IMPLEMENTED_SOURCE_ONLY;
- BUILD_PASS;
- REGRESSION_PASS;
- CORPUS_PASS;
- WINDOWS_ACCEPTED;
- DEFERRED_CANONICAL_AUTHORITY;
- UNSUPPORTED;
- REJECTED.

For game semantic evidence continue to respect the broader evidence vocabulary (`EXE_CONFIRMED`, `CORPUS_CONFIRMED`, `EXE_AND_CORPUS_CONFIRMED`, `STRUCTURAL_CONFIRMED`, `SEMANTIC_CANDIDATE`, `PRESERVED_UNDECODED`, `RESERVED_OBSERVED_ZERO`, `REJECTED`).

## 14. Definition of Done — Windows has surpassed Android

Do **not** declare success at simple parity.

Windows has surpassed the current Android product only when all of the following are true:

1. Windows compiles and uses the current portable v33 architecture rather than the older preview core.
2. All promoted v33 product modules available to Android are available on Windows.
3. Multi-MOD, canonical placement, WorkspaceGraph, Spider, Black Widow, PTX RuntimeCompat and EventTbl behavior are exposed and regression-covered on Windows.
4. Existing Windows perspective/render/export/navigation improvements are preserved.
5. There is no Windows-only duplicate parser or semantic fork.
6. Windows corpus acceptance is recorded.
7. Windows implements the mandatory desktop leapfrog set from §5.1.
8. At least one coherent Windows workflow is objectively unavailable on Android — e.g. folder workspace + dependency graph + semantic diff + batch report/CLI.
9. New Windows release notes describe actual current support, not v24-era parity.
10. The build can be handed to a Windows user as the preferred Native Reader inspection/workbench build.

## 15. Prioritization if time or execution environment is constrained

Never respond to constraints by inventing support or skipping evidence.

Priority order:

**P0 — architecture correctness**
- live v33 core integration;
- C++23 target;
- no semantic fork;
- build/tests.

**P1 — product parity**
- module parity;
- multi-MOD;
- WorkspaceGraph/Spider/Black Widow;
- companions/export/inspection.

**P2 — Windows overtakes Android**
- drag/drop;
- workspace;
- tabs;
- graph;
- diff;
- batch/CLI;
- hot reload;
- diagnostics.

**P3 — extended desktop experience**
- dockable panels;
- D3D backend;
- advanced selection/measurement;
- future promoted formats.

Do not spend P0/P1 time polishing cosmetic shell details while core parity is missing.

## 16. Copy-ready implementation prompt for an autonomous engineering agent

Use the following prompt as the handoff contract:

---

You are the lead Windows engineer for `VrUaCom/DMC-Native-Reader`.

Your mission is to bring the Windows x64 product to the **live current Android/v33 Native Reader capability and architecture level, then continue immediately until Windows is ahead of Android as a desktop inspection/workbench product**.

Read `windows/WINDOWS_V33_PARITY_AND_LEAPFROG_MASTER_PLAN.md` completely before editing.

Mandatory first actions:

1. Resolve the current `main` HEAD and PR #33 live `head_sha`.
2. Read `docs/PROJECT_AI_CONTEXT.md`, `docs/STATUS.md`, `docs/MODULAR_SPIDER_V33.md` and active #34 phase/review issues.
3. Compare Windows current main with live v33 and create a concrete parity-gap inventory.
4. Preserve current Windows-only improvements already on main.
5. Do not modify `VrUaCom/dmc-rengine-cpp`; it is read-only.

Architecture rules:

- one shared portable Native Reader core;
- Windows shell owns platform UX/transport only;
- no duplicate format parser/runtime semantics;
- WorkspaceGraph owns stable native resource identity/bindings;
- Spider C++ owns typed operation orchestration, not DMC semantics;
- Black Widow owns capabilities;
- DMC Rengine remains canonical read-side semantic authority;
- unknown/ambiguous relationships fail closed;
- Native Reader remains read-only.

Execution sequence:

`W0 audit -> W1 live v33 portable integration -> W2 Windows product parity UI -> W3 parity acceptance -> W4 mandatory desktop leapfrog -> W5 Windows release hardening`.

Do not stop at parity.

Windows must overtake Android with desktop-first workflows including drag/drop, folder workspace, multi-session tabs, WorkspaceGraph visualization, semantic diff, batch export/report, headless CLI, hot reload and Windows diagnostics. Build them over portable/native product operations so Android can later reuse the underlying core improvements.

Future MOT/physics/cloth/HITS/TXT/index/DCA/LIG/PAC/PNST/NBZ/EFM/MRP/SHW features are gated roadmap only. Do not claim support until canonical native authority and tests exist.

After every bounded slice:

- build;
- run relevant native tests;
- report exact HEAD;
- report changed files;
- state what is BUILD_PASS vs REGRESSION_PASS vs CORPUS_PASS;
- document remaining parity gaps;
- continue to the next highest-priority gap unless genuinely blocked by missing canonical authority or unavailable execution infrastructure.

Never use a stale hard-coded PR #33 SHA as authority. Re-read live head before evidence-sensitive execution.

Final success criterion: Windows is the preferred Native Reader desktop build, contains all current portable Android/v33 product capability, preserves Windows-specific improvements, adds the mandatory desktop workbench features, has no duplicated semantic authority, and has recorded Windows build/regression/corpus evidence.

---

## 17. Immediate first engineering checkpoint

The first implementation report created from this contract must answer exactly:

1. What is live `main` HEAD?
2. What is live PR #33 HEAD?
3. Which portable v33 files/modules are absent from Windows main?
4. Which current Windows commits/features would be lost by a naive merge/rebase?
5. What is the safest integration strategy?
6. Does Windows compile under strict C++23 after the first integration slice?
7. Which v33 tests execute on Windows?
8. What product parity gaps remain?
9. Which desktop-leapfrog feature will be implemented first immediately after parity?

Only after that audit should large-scale implementation proceed.

## 18. Product direction

The long-term role of Windows Native Reader is:

**the high-throughput desktop inspection, visualization, comparison, dependency-analysis and evidence workbench for DMC resources, backed by DMC Rengine canonical knowledge.**

Android remains the portable field inspector. Windows should be the deeper workbench. DMC Rengine remains the authoring/rebuild engine.

That separation lets Windows move aggressively in UX and analysis without corrupting format authority or turning Native Reader into a second engine.