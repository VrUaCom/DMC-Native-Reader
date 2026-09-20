# DMC Native Reader v33 — modular architecture review — 2026-09-15

> **HISTORICAL PRE-INTEGRATION REVIEW SNAPSHOT.** This document records the architecture state and recommendations at the time of that review. Several P0 items described below were implemented later the same day. Do **not** use this file as current architecture authority. For current rules/state read `docs/PROJECT_AI_CONTEXT.md`, `docs/MODULAR_SPIDER_V33.md`, `docs/STATUS.md`, Project card #46 and the active Phase/Review issues under #34.

Target reviewed: PR #33, branch `feature/png-export-multi-mod-v27`.

## Review result

The v33 direction is correct: one portable C++ core, one Android JNI DSO, registry-owned format routing, Spider Crusader orchestration and Black Widow capability policy. The main remaining architectural risk is not the registry itself; it is feature growth inside broad session/action translation units.

At review time the largest concentration points were:

- `modules/resource_session.cpp` — session materialization, render caches, composite construction, composite texture state, description and rendering concerns in one unit;
- `spider/session_actions.cpp` — composition plus single/shared/per-part PTX policy in one action unit;
- `MainActivity.java` — platform UI orchestration is still large and should be split after the native boundaries stabilize;
- model adapters remain intentionally format-specific, but should not absorb cross-resource placement policy.

The rule for the next line is therefore:

```text
format parser/adapter
    -> source-local typed scene
    -> explicit product module
    -> Spider action
    -> derived session projection
    -> renderer/UI
```

Cross-MOD placement must not be implemented inside JNI, `MainActivity`, MOD parsing, PTX binding, or the generic renderer.

## Implemented in this review

### 1. Composite model state extracted

`CompositePart` and placement state now live in:

`include/dmcresource/composite_model.h`

The generic `Session` includes the type but no longer defines the MOD-composite data model itself.

Each part retains:

- source-local `RenderScene` authority;
- source-local triangle texture-slot projection;
- texture namespace state;
- explicit derived placement state.

The source scene is never rewritten by placement.

### 2. Placement is a dedicated core module

New module:

`modules/composite_placement.cpp`

Public contract:

`include/dmcresource/composite_placement.h`

Supported operations:

- explicit host-part + host-joint placement;
- reset to source coordinates.

The module fails closed on:

- invalid part indices;
- host == child;
- unavailable joint;
- joint without spatial authority;
- malformed source/composite projection;
- rejected/non-finite matrices;
- allocation failure.

No filenames, selection order, model names or guessed body-part semantics are used as attachment authority.

### 3. Shared matrix convention

New header:

`include/dmcresource/matrix_ops.h`

It centralizes the row-vector operations required by placement:

- affine finite validation;
- point transform;
- `left * right` matrix composition;
- translation extraction.

This prevents the attachment path from inventing a second coordinate convention.

### 4. Spider owns placement execution

New Spider boundary:

- `include/dmcresource/spider/model_placement_actions.h`
- `spider/model_placement_actions.cpp`

Actions:

- `attach_mod_part_to_host_joint(...)`;
- `reset_mod_part_placement(...)`.

The action is executed through Spider Crusader. Platform code does not receive permission to mutate composite placement directly.

### 5. Regression coverage

New regression:

`app/src/test/native/composite_placement_test.cpp`

It verifies:

- source-coordinate mode remains the default;
- explicit host-joint placement transforms the child render projection;
- child hierarchy world matrices receive the same root placement;
- source-local part scene stays available for reset;
- reset restores source coordinates without reparsing;
- invalid joint fails closed;
- a host joint without spatial authority fails closed.

## Relation to the DMC3 MOD reverse result

This refactor intentionally separates architecture from semantic promotion.

The reverse work has identified a runtime path where an attachment-selected matrix becomes the MOD external/root matrix before `currentWorld[]` propagation. Native Reader now has a module boundary capable of representing that operation without embedding the rule into generic session/JNI code.

However, automatic `child MOD +0x13 -> host relationship` resolution is **not** promoted by this review. The current placement action requires explicit host part and explicit joint selector. Automatic binding should be added only after the canonical DMC Rengine API exposes the required cross-model attachment relation with evidence-backed authority.

## Remaining modularization work

### P0 — next

1. Move MOD-composite construction helpers out of `resource_session.cpp` into a dedicated `composite_builder` module.
2. Split PTX actions from `spider/session_actions.cpp` into texture-specific action units while preserving the existing public `spider::actions` namespace.
3. Add a canonical Rengine-facing attachment descriptor once host/child ownership is closed; Native Reader must consume it rather than rediscover EXE semantics.

### P1

4. Split `MainActivity.java` into file/open controller, model-composition controller, export controller and view state binding; keep parsing/action policy native.
5. Move session description formatting away from runtime state mutation.
6. Introduce explicit derived-cache invalidation instead of manually refreshing each cache from feature code.

### P2

7. Add placement-aware Black Widow state only when the UI exposes the action.
8. Add device corpus acceptance for a known body + attached-part pair after canonical relation evidence is available.

## Verification boundary

GitHub-hosted workflows are currently observed failing before runner assignment (`runner_id=0`, `steps=[]`). Such runs execute no checkout/build/test and are not source-regression evidence.

The new placement path must therefore remain unmerged until an exact-head host build executes the complete native CTest suite, including `composite_placement_test`, followed by the normal APK verifier and device gates.
