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

**Status: complete for the Android v1.0.0 acceptance baseline**

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
- canonical Android v1.0.0 release/download URLs documented;
- Code of Conduct, Support, Security, contribution and issue/PR templates prepared;
- cross-platform preview work separated from the accepted Android v1 binary.

Remaining before visibility changes to Public:

- delete or confirm expiry of the historical one-day production-key backup artifact;
- rebuild the retained `ios-unsigned-latest` line as the current **DMC Native Reader for iOS — Preview** and replace the obsolete `DMCReader-unsigned.ipa` only after the replacement build passes;
- publish/update the Windows preview line only after its x64 build passes;
- publish the actual stable GitHub Release `v1.0.0` and attach the accepted signed Android APK;
- verify the canonical direct APK link and checksum;
- audit/prune historical branches and decide commit-email exposure;
- configure GitHub About/topics, `main` rules and private vulnerability/security features;
- verify protected Android production signing environment;
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

**Status: active preview implementation**

Android remains the first stable implementation. The same C++20 semantic contracts are now being carried to native iOS and Windows shells without platform-specific parser forks.

### Implemented preview foundation

- [x] platform-neutral C++20 `PortableSession` over the current Architecture v2 `PipelineResult`;
- [x] iOS SwiftUI + Objective-C++ shell using the current four-format registry;
- [x] iOS Files/Share-sheet opening, MOD/SCM render, DDS preview, PTX gallery and Inspector path;
- [x] Windows native Win32/x64 shell using the same C++20 registry/adapters/renderer;
- [x] Windows file open, drag-and-drop, MOD/SCM render, DDS preview, PTX child navigation and Inspector path;
- [x] separate macOS and Windows preview build jobs;
- [x] guarded publish path that retains and replaces the historical iOS preview only after successful builds;
- [x] intended Windows moving preview release `windows-preview-latest`.

### Still required before platform promotion

- [ ] successful iOS CI build from the current branch;
- [ ] successful Windows x64 CI build from the current branch;
- [ ] real iPhone/iPad corpus acceptance for MOD / SCM / DDS / PTX;
- [ ] real Windows corpus acceptance for MOD / SCM / DDS / PTX;
- [ ] define signed iOS distribution path if moving beyond unsigned technical preview;
- [ ] define Windows signing/installer identity before calling Windows stable;
- [ ] only then promote either platform from **Preview** to **Stable**.

The old iOS release/tag is deliberately **not deleted**. `ios-unsigned-latest` becomes the moving iOS preview line. Its obsolete pre-v1 asset and HITS/TXT/index claims are replaced only when the current four-format build succeeds.

See [`CROSS_PLATFORM.md`](CROSS_PLATFORM.md).

### Web

Web remains the next shell direction:

- C++20 core compiled to **WebAssembly**;
- JavaScript/TypeScript limited to browser/file/DOM/Canvas/WebGL/WebGPU presentation concerns;
- no second DMC binary parser in the browser layer.

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
