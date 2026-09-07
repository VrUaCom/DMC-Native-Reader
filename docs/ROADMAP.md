# DMC Native Reader — Roadmap

## Phase 1 — Clean Architecture v2 core

**Status: active acceptance**

Target production surface:

- MOD;
- SCM;
- DDS;
- PTX.

Required properties:

- exactly four registered modules;
- fail-closed unknown/unpromoted formats;
- no wildcard or recognition-only fallback;
- no legacy `DecodeResult` compatibility bridge;
- MOD/SCM canonical adapters;
- generic image and child-resource contracts for DDS/PTX;
- capability-driven Android UI;
- host + Android CI proving the four paths and absence of archived modules.

The pre-cleanup multi-format implementation is preserved on `main.2` and marked **до опрацювання**.

## Phase 2 — Device validation

After CI is green, validate the clean APK on Samsung:

1. MOD render;
2. SCM render;
3. DDS preview;
4. PTX gallery;
5. PTX -> DDS child preview;
6. child -> parent navigation;
7. malformed/unsupported input fails closed without stale UI state.

## Phase 3 — One-by-one format promotion

Removed families return only as Architecture v2 modules with evidence and regression coverage. Do not restore the old module implementation wholesale.

Likely promotion candidates are selected from `main.2` according to canonical readiness in `dmc-rengine-cpp`, for example HITS, DCA, LIG2, Stage TXT, PAC/PNST or other families once their product integration contract is clean.

NBZ remains special: it should follow the canonical source/materialization architecture rather than being reintroduced as an ad-hoc ordinary format parser.

## Phase 4 — Deeper model/texture semantics

- richer SCM material/scene semantics;
- deeper MOD skeletal and material closure;
- texture/material linkage between model slots and texture resources;
- move DDS/PTX parser authority to an appropriately pinned canonical core revision when that migration can be done without duplicating reader logic.

## Phase 5 — Authoring

Native Reader remains read-only. Editing/repacking belongs to later tooling and must reuse DMC Rengine writer contracts instead of adding Android-only writers.

```text
clean authority
  -> bounded read
      -> real corpus/device validation
          -> semantic promotion
              -> author/write
```
