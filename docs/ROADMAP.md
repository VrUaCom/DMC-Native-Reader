# DMC Native Reader — Roadmap

Last updated: 2026-09-10.

This roadmap separates **accepted `main` capability** from **candidate work**. A host test or APK build does not by itself promote a visible feature; device/corpus acceptance remains required where behavior depends on real DMC resources or Android interaction.

## Phase 1 — Clean Architecture v2 core

**Status: COMPLETE / accepted in `main`.**

Production registry is intentionally limited to:

- MOD;
- SCM;
- DDS;
- PTX.

Completed properties:

- exactly four registered production modules;
- unknown/unpromoted formats fail closed;
- no wildcard or recognition-only fallback in `main`;
- no legacy `DecodeResult -> Mesh -> RenderScene` bridge;
- canonical MOD/SCM adapters;
- generic `InspectionDocument`, `RenderScene`, `ImagePreview` and child-resource contracts;
- capability-driven UI;
- reusable C++20 `DMCNativeReader::Core` separated from the Android JNI shell.

The pre-cleanup multi-format implementation remains on `main.2` as backlog/reference, not as a second production architecture.

## Phase 2 — v24 device and footprint acceptance

**Status: COMPLETE / accepted 2026-09-10.**

Confirmed on Android device:

- MOD opens/renders;
- SCM opens/renders;
- standalone DDS previews;
- PTX opens as a gallery and child DDS previews work;
- PTX texture application to supported MOD/SCM models works;
- installed size is 2.32 MB versus 6.27 MB before the v24 cleanup.

v24 also reduced public native exports to the declared JNI boundary and removed the unintended Kotlin runtime dependency from the Java-only shell.

## Phase 3 — UV and focused model inspection

**Status: IMPLEMENTED IN DRAFT v26 / DEVICE ACCEPTANCE PENDING.**

Existing branch: `feature/dds-ptx-v1-acceptance`, draft PR #32. No new branch is required for this line.

Implemented candidate capability:

- UV gallery grouped by canonical texture slot;
- separate zoomable UV map per slot with triangle counts;
- shared gallery/session infrastructure for PTX and generated UV children;
- long-press UV information;
- long-press object -> mesh structure information;
- long-press node/bone parent relationship information;
- hierarchy-information availability independent from spatial transform availability;
- short taps preserve normal UV/wireframe/hierarchy actions.

Promotion gate: Android device MOD/SCM + PTX validation, short-vs-long press behavior, navigation/return state, relationship/count correctness, and installed-size check.

## Phase 4 — Model Inspector and visual debugging depth

**Status: NEXT.**

After v26 acceptance:

- turn focused reports into a coherent Model Inspector surface without duplicating parsers;
- improve object/mesh/material/texture-slot navigation;
- expose skeletal/skin information at the strongest available evidence level;
- keep spatial bone overlays disabled when MOD lacks canonical spatial transforms;
- preserve capability-driven controls rather than format-specific Android screens;
- add selective visibility/filtering for object and mesh groups where it can be implemented from canonical `RenderScene` / inspection authority.

## Phase 5 — Deeper MOD / SCM semantics

**Status: ACTIVE REVERSE DEPENDENCY.**

Native Reader should consume newly promoted facts from `dmc-rengine-cpp`, not rediscover them in Android code.

Targets include:

- deeper MOD transform/skeleton closure;
- richer SCM material/scene semantics;
- stronger model <-> texture companion coherence;
- material/filter/alpha semantics only when canonical evidence is promoted;
- preservation of unknown/undecoded fields instead of guessed labels.

## Phase 6 — One-by-one format promotion

**Status: BACKLOG, evidence-gated.**

Candidate families may include HITS, DCA, LIG2, Stage TXT, PAC/PNST and others, but each must return as an Architecture v2 module with its own evidence and regressions. Do not restore the old wide registry wholesale.

NBZ remains a special source/materialization problem and should follow DMC Rengine's canonical archive/VFS architecture rather than an ad-hoc ordinary file parser.

## Phase 7 — Cross-platform shells

**Status: EXPERIMENTAL / separate PR line.**

iOS and Windows preview work exists in PR #29 over the portable C++20 core. It is not part of accepted Android `main` and must not be described as stable until platform builds and real-device/corpus acceptance succeed.

Longer-term web direction is a C++20/WebAssembly shell over the same portable core, not a duplicate JavaScript parser stack.

## Phase 8 — Authoring boundary

Native Reader remains read-only. Editing/repacking belongs to DMC Rengine / authoring products and must reuse canonical writer contracts rather than introducing Android-only writers.

```text
canonical evidence
  -> bounded read
  -> portable typed projection
  -> device/corpus acceptance
  -> richer inspection
  -> format promotion
  -> authoring in the engine/tooling layer
```
