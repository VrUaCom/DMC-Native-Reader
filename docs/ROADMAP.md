# DMC Native Reader — Roadmap

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
- MOD/SCM canonical adapters;
- generic image and child-resource contracts for DDS/PTX;
- capability-driven Android UI;
- host + Android CI proving the four paths and absence of retired modules.

## Phase 2 — Device validation

**Status: complete for the v1.0.0 acceptance baseline**

Accepted practical behavior includes:

1. MOD render/inspection;
2. SCM render/inspection;
3. DDS preview;
4. PTX gallery;
5. PTX -> DDS child preview;
6. child -> parent navigation;
7. malformed/unsupported input fails closed without stale UI state.

## Phase 3 — Public repository opening

**Status: active**

Before visibility changes to public:

- finish public documentation and repository hygiene;
- isolate debug package/signing identity from production;
- ensure production signing material exists only in protected secrets;
- select and commit the source-code license;
- audit historical branches and commit metadata that will become public;
- configure GitHub About/topics, branch rules and private vulnerability reporting;
- publish the v1.0.0 release APK with checksum/signing evidence.

See [`PUBLIC_RELEASE_CHECKLIST.md`](PUBLIC_RELEASE_CHECKLIST.md).

## Phase 4 — One-by-one format promotion

Historical readers return only as Architecture v2 modules with canonical/evidence closure and regression coverage. Do not restore the old multi-format implementation wholesale.

Candidate families are selected according to canonical readiness in `dmc-rengine-cpp`, for example HITS, DCA, LIG2, Stage TXT, PAC/PNST or other families once their product integration contract is clean.

NBZ remains special: it should follow the canonical source/materialization architecture rather than being reintroduced as an ad-hoc ordinary format parser.

## Phase 5 — Deeper model/texture semantics

- richer SCM material/scene semantics;
- deeper MOD skeletal/material closure;
- texture/material linkage between model slots and texture resources;
- promote shared texture authority into canonical core where doing so removes duplication rather than creating a second parser.

## Phase 6 — Product UX

- richer Model Inspector presentation;
- evidence-aware hierarchy/skeleton overlays;
- clearer unknown/partial semantic presentation;
- improved large-file and malformed-input diagnostics;
- public tester workflow and reproducible issue capture.

## Phase 7 — Authoring

Native Reader v1 remains read-only. Editing/repacking belongs to later tooling and must reuse DMC Rengine writer contracts instead of adding Android-only writers.

```text
clean authority
  -> bounded read
      -> real corpus/device validation
          -> semantic promotion
              -> author/write
```
