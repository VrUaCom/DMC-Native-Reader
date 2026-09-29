# DMC Native Reader — Phase 2 + Phase 3 Review

Date: 2026-09-29  
Review branch: `review/phase2-phase3-20260929`  
Review baseline: `NR-Luna-v73` @ `b1f75d66886bb325513838da647dcd88e5b6c66c`

## Purpose

Close branch sprawl from Phase 2, establish one canonical Phase-2 snapshot, identify the cumulative Phase-3 line, and review both phases before further product work.

This review is intentionally **reconciliation-first**. It does not blindly merge an older Phase-3 tree over current `main`/`NR-Luna-v73`.

## Canonical branch aliases created

### Phase 2

Canonical consolidated snapshot:

`phase2/consolidated` -> `e8f380113b7372210745957befcd8f890b9caa7a`

The following historical Phase-2 branches were checked against `phase2/evidence-unblock-integration`. Every one is an ancestor of the integration head; none contains unique commits outside that head:

- phase2/android-sdk-metadata
- phase2/bootstrap-sdk-metadata
- phase2/candidate-identity-regression
- phase2/evidence-unblock-integration
- phase2/execution-identity-reconcile
- phase2/preflight-contract-regression
- phase2/preflight-operator-flow
- phase2/preflight-report
- phase2/preprovisioned-docs
- phase2/preprovisioned-regression
- phase2/preprovisioned-toolchain
- phase2/require-reviewed-head
- phase2/sdk-metadata-regression

Therefore `phase2/consolidated` is lossless with respect to that Phase-2 branch family.

PR #95 is now **merged**. Its exact head was `e8f380113b7372210745957befcd8f890b9caa7a`; GitHub records merge commit `bfdb99f2520b71d852bb299b5a07881131e80aa0`, which is current `main`.

### Phase 3

Canonical cumulative pre-Lady snapshot alias:

`phase3/consolidated-pre-lady` -> `d0154abca364f9fb1194e72770fc3de760b9a4b9`

This is the existing `NR-Luna-phases-1-3` state under an explicit Phase-3 name. It is cumulative, not a Phase-3-only delta.

Current continuation:

`NR-Luna-v73` -> `b1f75d66886bb325513838da647dcd88e5b6c66c`

Current `main`:

`main` -> `bfdb99f2520b71d852bb299b5a07881131e80aa0`

`NR-Luna-v73` is exactly two commits ahead of current `main` and contains the reviewed core Phase-2 merge plus the v72/Lady/effects continuation.

## Phase 2 review

### PASS — branch completeness

`phase2/evidence-unblock-integration` contains every Phase-2 branch listed above. The older branch heads are historical checkpoints, not independent work that still needs merging.

### PASS — merge status

Phase 2 is already merged into `main` through PR #95. `main` is 166 commits ahead of the Phase-2 integration head and zero commits behind it. `NR-Luna-v73` is 168 commits ahead and zero behind.

### CORRECTION — stale documentation

Current documentation still says the Phase-2 integration stack is a future merge candidate and that PR #95 / `phase2/evidence-unblock-integration` is the live execution candidate. That is stale after the 2026-09-28 merge.

Required correction after this review:

- identify `phase2/consolidated` as historical immutable Phase-2 snapshot;
- identify `main` as the accepted merged product line;
- keep exact-head Phase-2 tooling only as evidence/regression infrastructure;
- stop calling merged PR #95 a live merge candidate.

### PASS — branch cleanup safety

The old Phase-2 branch family can be retired/deleted without losing commits, provided `phase2/consolidated`, Git history and PR #95 remain.

## Phase 3 review

### Finding P3-1 — direct merge is unsafe

`phase3/consolidated-pre-lady` and current `main` are divergent:

- Phase-3 line: 39 unique commits;
- current `main`: 107 commits beyond the common base.

Therefore Phase 3 must be reconciled by responsibility/content, not merged wholesale.

### Finding P3-2 — much of FXBANK Phase-3 core is already in main

At tree level, several important Phase-3 files are byte-identical between the Phase-3 snapshot and current `main`, including:

- `app/src/main/cpp/CMakeLists.txt`;
- `app/src/main/cpp/include/dmcresource/effect_bank.h`;
- `app/src/main/cpp/modules/effect_bank.cpp`;
- `app/src/main/java/com/dmcrengine/nativeviewer/DmcRenderView.java`;
- `app/src/test/native/player_coat_test.cpp`.

Current `main` also contains the later merged post-v68 FXBANK/Visual-Info work, so those commits must not be replayed blindly.

### Finding P3-3 — Windows parity is not fully promoted to current main

The Phase-3 branch contains `.github/workflows/windows.yml`; current `main` and `NR-Luna-v73` do not.

The Phase-3 `win_shell.cpp`, Open-With registration scripts and Windows build/release path also differ from current `main`.

This is the clearest candidate for selective Phase-3 promotion after review against the current C++23 core.

### Finding P3-4 — overlapping runtime/UI files need semantic reconciliation

Phase-3 and current `main` differ in:

- `app_native.cpp`;
- `resource_session.h/.cpp`;
- `motion_player.h/.cpp`;
- `part_attachment.h/.cpp`;
- `pac_assembly.cpp`;
- Android `MainActivity.java` / `NativeBridge.java`;
- `win_shell.cpp`;
- Phase-2 evidence tooling.

These paths have newer work in current `main` and/or `NR-Luna-v73`. Older Phase-3 versions must not overwrite current runtime semantics.

### Finding P3-5 — current v73 is the correct review target

`NR-Luna-v73` is current `main` plus two commits and carries the Lady/MotionScript/effect-runtime continuation.

Phase-3 reconciliation should therefore be performed **onto this review branch based on v73**, not by moving `main` backward to the old cumulative Phase-3 tree.

## Review classification

### BLOCKER

Blind merge of `NR-Luna-phases-1-3` / `phase3/consolidated-pre-lady` into `main` or `NR-Luna-v73`.

### CORRECTION

Update stale Phase-2 status/context docs after the review closes.

### CORRECTION

Port/reconcile Windows parity from Phase 3 onto the current core, then test a current Windows artifact.

### CORRECTION

Semantic-diff the overlapping Phase-3 runtime/UI files and retain only behavior not already superseded by main/v73.

### OPTIMIZATION

After the review and one consolidated Phase-2 branch are accepted, remove the historical Phase-2 branch clutter from GitHub. Git history and PR #95 preserve the evidence.

## Review execution order

1. Freeze `phase2/consolidated` and `phase3/consolidated-pre-lady` as reference snapshots.
2. Treat `review/phase2-phase3-20260929` / current v73 as the integration review surface.
3. Reconcile Windows parity first.
4. Audit FXBANK Visual/Info differences against current main/v73.
5. Audit resource-session / PAC / motion overlaps.
6. Run inherited native regressions plus current v72/v73 tests.
7. Build Android and Windows from the same reviewed head.
8. Update `STATUS.md` and `PROJECT_AI_CONTEXT.md`.
9. Issue explicit GO/NO-GO for the next phase.

## Current verdict

Phase 2: **GO / CONSOLIDATED / ALREADY MERGED INTO MAIN**.

Phase 3: **REVIEW REQUIRED / NO BLIND MERGE**.

The core direction is preserved. The remaining work is selective reconciliation of Phase-3 Windows and overlapping runtime/UI changes onto the current v73 line.
