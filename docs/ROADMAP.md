# DMC Native Reader — Roadmap

## Product mission

**Make DMC resources feel like ordinary files.**

Native Reader is the viewability/accessibility product of the DMC Rengine ecosystem. Its job is to turn opaque resources into familiar, directly understandable representations across everyday devices.

The primary measure of progress is not feature count. It is:

> **How many previously opaque DMC resource types have become directly understandable and viewable?**

See [`PRODUCT_VISION.md`](PRODUCT_VISION.md).

## Phase 1 — Clean Architecture v2 core

**Status: complete in v1.0.0**

Stable production surface:

- MOD;
- SCM;
- DDS;
- PTX.

Completed properties:

- exactly four registered production modules;
- fail-closed unknown/unpromoted formats;
- no wildcard or recognition-only fallback;
- no legacy `DecodeResult` compatibility bridge;
- MOD/SCM canonical DMC Rengine adapters;
- generic image and child-resource contracts for DDS/PTX;
- capability-driven Android v1 UI;
- host + Android CI proving the four paths and absence of retired modules.

## Phase 2 — Device validation

**Status: complete for the v1.0.0 acceptance baseline**

Accepted practical behavior includes:

1. MOD render/inspection;
2. SCM render/inspection;
3. DDS preview;
4. PTX gallery with real texture thumbnails;
5. PTX -> DDS child preview;
6. child -> parent navigation;
7. malformed/unsupported input fails closed without stale UI state.

## Phase 3 — Public repository opening

**Status: active — source/docs substantially prepared, GitHub admin gates remain**

Completed in public-prep:

- public documentation and v1 product identity aligned;
- debug package/signing identity isolated from production;
- current production workflow keeps signing material in protected secrets rather than source/artifacts;
- source license selected: **DMC Native Reader Personal Non-Commercial License 1.0** with Capcom Special Grant;
- third-party licensing documented separately;
- canonical v1.0.0 release/download URLs documented;
- Code of Conduct, Support, Security, contribution and issue/PR templates prepared.

Remaining before visibility changes to Public:

- delete or confirm expiry of the historical one-day production-key backup artifact;
- remove obsolete `ios-unsigned-latest` prerelease/tag;
- publish the actual GitHub Release `v1.0.0` and attach the accepted signed APK;
- verify the canonical direct APK link and checksum;
- audit/prune historical branches and decide commit-email exposure;
- configure GitHub About/topics, `main` rules and private vulnerability/security features;
- verify protected production signing environment;
- perform a logged-out final repository/release audit.

See [`PUBLIC_RELEASE_CHECKLIST.md`](PUBLIC_RELEASE_CHECKLIST.md).

## Phase 4 — One-by-one format promotion

Historical readers return only as Architecture v2 modules with canonical/evidence closure and regression coverage. Do not restore the old multi-format implementation wholesale.

Candidate families are selected according to canonical readiness in DMC Rengine, for example HITS, DCA, LIG2, Stage TXT, PAC/PNST, SHW, EFM, MOT, SO or other families once their product integration contract is clean.

Each promoted family must answer two questions:

1. what is the canonical/bounded authority for its bytes and structure?;
2. what is the most natural familiar representation for an ordinary user?

NBZ remains special: it should follow the canonical source/materialization architecture rather than being reintroduced as an ad-hoc ordinary format parser.

## Phase 5 — Deeper model/texture semantics

- richer SCM material/scene semantics;
- deeper MOD skeletal/material closure;
- texture/material linkage between model slots and texture resources;
- promote shared texture authority into DMC Rengine where doing so removes duplication rather than creating a second parser.

These improvements should make existing resources easier to understand, not turn Native Reader into an authoring suite.

## Phase 6 — Product UX

- richer Model Inspector presentation;
- evidence-aware hierarchy/skeleton overlays;
- clearer unknown/partial semantic presentation;
- improved large-file and malformed-input diagnostics;
- public tester workflow and reproducible issue capture;
- familiar previews rather than format-centric debug screens;
- resource thumbnails/gallery representations where capability data supports them.

The desired user experience remains:

```text
file -> open -> useful representation
```

not:

```text
file -> learn binary format -> choose specialist decoder -> inspect
```

## Phase 7 — Cross-platform Native Reader

Android is the first stable implementation. The strategic direction is to carry the same C++20 semantic contracts to iOS, Windows and Web without creating platform-specific parser forks.

Targets include:

- Android Open with / SAF integration;
- iOS Files / Share / document-opening integration;
- Windows desktop file opening;
- Windows Explorer preview/thumbnail integration where practical;
- Web UI backed by the same C++20 core compiled to **WebAssembly**;
- consistent `InspectionDocument`, `RenderScene`, `ImagePreview`, `ChildResource` and capability semantics across all shells.

For Web, JavaScript/TypeScript remains a thin browser/presentation layer for DOM, file input, routing and Canvas/WebGL/WebGPU integration. DMC binary semantics remain in C++20/WebAssembly.

Platform UI may differ. Binary meaning must not.

## Phase 8 — Ecosystem integration boundary

DMC Rengine remains the central decompilation/reimplementation engine and C++20 modding foundation.

Native Reader remains focused on:

```text
Open -> View -> Inspect -> Navigate -> Understand
```

DMC Rengine-backed authoring/resource-management tools such as Pocket GDS remain focused on:

```text
Browse archives -> Extract -> Replace -> Edit -> Repack -> Manage
```

Possible future integration includes handing a viewed resource to an authoring tool or sharing canonical reader/writer contracts. Editing and repacking are **not** core Native Reader roadmap requirements unless a future decision explicitly changes the product role.

This separation prevents competing private toolchains from growing around the same resource formats.

## Long-term end state

The project should make an ordinary DMC resource directory progressively more visual and less opaque:

```text
MOD -> visible 3D model
SCM -> visible scene
DDS -> visible image
PTX -> visible texture gallery
future animation -> visible playback/timeline
future collision/bounds -> visible geometry/overlay
future graph/structure -> visible tree/graph
```

The strongest version of Native Reader is not the one with the most buttons. It is the one users stop thinking about because opening a DMC resource simply works.

```text
DMC Rengine authority
  -> bounded C++20 read
      -> familiar representation
          -> real corpus/device validation
              -> Android / iOS / Windows / Web
```
