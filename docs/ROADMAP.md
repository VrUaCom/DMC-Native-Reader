# DMC Native Reader — Roadmap

Last updated: **2026-09-26**.

This roadmap separates released platform state from ongoing development.

## 1. Shared native reader core

**Status: ACTIVE / established.**

Maintain one reusable C++23 reader core for platform shells rather than separate format implementations for Android and Windows.

Core principles:

- bounded reads;
- fail-closed format routing;
- typed projections;
- canonical DMC Rengine read-side authority where available;
- no platform-specific duplication of binary semantics;
- read-only product boundary.

## 2. Android

**Status: CURRENT PUBLIC RELEASE — v68 / 1.0.41.**

Continue hardening the v68 line:

- rendering/performance;
- model + room presentation;
- motion and attachment behavior;
- PAC/PNST resource assembly;
- cloth, shadow, collision and effect inspection;
- gesture/settings UX;
- resource-family regressions.

New Android claims require evidence from the exact release artifact.

## 3. Windows parity

**Status: NEXT MAJOR PLATFORM TASK.**

Current public Windows release: **v1.0.0 Preview**.

Bring Windows forward so it no longer trails the newer shared-core/Android line:

- consume the current shared native core;
- expose current supported resource families where Windows UI support exists;
- preserve read-only behavior;
- keep portable launch/open-with flow;
- validate 3D, texture, PAC/MOT and inspection paths;
- publish a new Windows release only after build and smoke-test acceptance.

Do not describe the existing v1.0.0 Preview as equivalent to Android v68.

## 4. Resource coverage

**Status: EVIDENCE-GATED.**

Current `main` registry includes MOD, SCM, DDS, PTX, EventTbl, PAC, MOT, PNST, SHW, TSC, CLT, EFM, motion scripts, collision data and effect banks.

Future work should deepen semantics and presentation rather than adding guessed formats.

Unknown fields remain preserved/unknown until evidence supports a stronger interpretation.

## 5. Cross-platform presentation

Keep platform shells thin:

- Android owns Android lifecycle/input/presentation;
- Windows owns Windows shell/input/presentation;
- native format semantics remain in the shared core.

Additional platforms are experimental unless a release artifact is explicitly published and supported.

## 6. Authoring boundary

Native Reader remains a reader.

Editing, rebuilding, canonical writing and archive reintegration belong to **DMC Rengine** and dedicated authoring tools.

```text
canonical evidence
  -> bounded read
  -> typed native projection
  -> platform presentation
  -> corpus/device acceptance
  -> deeper inspection
  -> authoring in DMC Rengine
```

## Naming policy

Use **Devil May Cry 3: Special Edition** for the game and **Devil May Cry HD Collection** for the collection. “DMC3” is shorthand; “Devil May Cry 3 HD Collection” is not used as a product title.
