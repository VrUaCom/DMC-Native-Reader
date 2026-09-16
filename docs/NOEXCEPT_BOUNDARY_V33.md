# Native Reader v33 — C++23 Exception Boundary Contract

Status: Phase 2 migration evidence / implementation note.
Scope: `VrUaCom/DMC-Native-Reader` only.

## Objective

Keep `noexcept` only where Native Reader can actually guarantee it. Allocating
portable C++23 helpers may throw internally; exceptions must be converted to
fail-closed state at explicit product/runtime boundaries rather than causing
`std::terminate`.

## Canonical boundary model

```text
allocating Core/adapters/helpers
        |
        v
NativeModule ModuleRun / Spider OperationFn
(noexcept + catch-all + no-allocation fallback)
        |
        v
Session/product API
        |
        v
JNI shell catch-all
```

`module_support::reject_minimal(probe)` is the no-allocation fallback for
NativeModule/Spider catch paths. Normal diagnostic rejection may allocate.

## Findings from Phase 2 static review

Confirmed false/noisy `noexcept` patterns existed in or around:

- format probing: extension normalization allocates;
- registry first-use: `std::vector<NativeModule>` initialization allocates;
- structural/diagnostic PipelineResult construction;
- Crusader `Plan` first-use construction through vector `push_back`;
- EventTbl projection and inspection construction;
- texture projection/PTX child/diagnostic construction;
- Spider compose/placement/texture action plan initialization;
- some diagnostic string updates inside `noexcept` helpers.

MOD/SCM canonical adapters already wrap their main allocating projections in
catch-all blocks. Literal guard/catch diagnostics use the fail-closed literal
`module_support::reject` overload so diagnostic allocation cannot terminate the
process.

## Phase 2 rules

1. Do not change Rengine's repository or executor.
2. Do not remove `noexcept` from `NativeModule::ModuleRun` or Crusader
   OperationFn boundaries; they are product execution contracts.
3. Internal helpers that allocate must not advertise false `noexcept` unless
   they catch every possible exception themselves.
4. Every noexcept execution boundary must catch first-use Plan allocation as
   well as operation/diagnostic allocation.
5. Catch paths must not require a new diagnostic allocation. Use a minimal
   rejected result/state when allocation itself failed.
6. Do not turn this into syntax modernization or semantic format changes.
7. Keep JNI as the final platform exception boundary; C++ exceptions must never
   cross JNI.

## Review checklist

- `run_decode_pipeline` is a fail-closed catch-all boundary.
- `probe`/registry/structural helpers have truthful exception specifications.
- model / texture / EVT ModuleRun functions catch Plan initialization and
  post-execution diagnostic allocation.
- model / texture / EVT OperationFn callbacks cannot terminate on helper
  allocation failure.
- Spider compose / placement / texture public noexcept actions catch first-use
  Plan initialization.
- no Android/JNI API appears in portable Core while fixing boundaries.
- no duplicate executor, parser, Plan implementation, or compatibility module is
  introduced.
- full native CTest and Android APK build are executed once at the end of the
  batched Phase 2 migration, not on every intermediate commit.

## AI execution prompt

> Work only in `VrUaCom/DMC-Native-Reader`. Treat this as a Phase-2 compatibility
> subtask. Audit all functions marked `noexcept` in Native Reader paths touched
> by C++23/Spider migration. If a function allocates (`std::string`, vector,
> stream, parser/IR construction, dynamic Crusader Plan initialization), either
> remove false `noexcept` from the internal helper or catch all exceptions inside
> the function. Preserve `noexcept` on NativeModule ModuleRun and Spider
> OperationFn execution boundaries, but make them catch-all and return a
> no-allocation fail-closed result/state. Do not change Rengine, JNI semantics,
> format semantics, or application behavior. Keep the PR draft and do not run
> hosted CI until the full Phase-2 source transition is ready. Update this
> document and Project subtask with any additional boundary discovered.

## Exit criteria

- no known allocating internal helper is falsely relied upon as noexcept in the
  C++23 migration path;
- all ModuleRun and Spider OperationFn boundaries fail closed rather than
  terminate on allocation/diagnostic/Plan initialization failure;
- public Core remains platform-neutral;
- exact-head static review is complete;
- one final batched CMake/CTest/APK/verifier run provides real execution evidence
  before Review Gate #41.
