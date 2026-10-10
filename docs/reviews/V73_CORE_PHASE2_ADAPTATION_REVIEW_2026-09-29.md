# Native Reader v73 — Core and Phase-2 Adaptation Review

Date: 2026-09-29  
Repository: `VrUaCom/DMC-Native-Reader`

## Reviewed refs

- `main`: `52210874f74d5526c2f1bd9000aaeab4594a03de`
- `NR-Luna-v73`: `9fec80b165c27679ec2e3f85da7fc0095c7d6749`
- Phase-2 consolidated snapshot: `phase2/consolidated` /
  `e8f380113b7372210745957befcd8f890b9caa7a`
- Phase-3 cumulative pre-Lady snapshot:
  `phase3/consolidated-pre-lady` /
  `d0154abca364f9fb1194e72770fc3de760b9a4b9`

## Integration verdict

### Phase 1

The current core and product architecture are already in the accepted `main`
ancestor. No separate blind Phase-1 merge is required.

### Phase 2

Phase 2 is already merged into `main` through merge commit
`bfdb99f2520b71d852bb299b5a07881131e80aa0`. The old Phase-2 branches are
historical checkpoints, not independent product code that must be replayed.

`NR-Luna-v73` is now based on the current core and contains:

- the Lady/MotionScript/effects continuation;
- the reviewed Phase-2 review document;
- the core Phase-2 acceptance inventory merge;
- current status/context corrections;
- an exact-head test inventory adapted to the v73 CMake test set.

The v73 CMake inventory contains 28 native tests:

- inherited Phase-2/core regressions;
- `effect_visualization`;
- `effect_runtime`;
- `motion_playback`;
- `pac_assembly`;
- `player_coat`.

The exact-head runner was previously stale at the old 23-test inventory. This
was corrected for `main` and `NR-Luna-v73`.

## Code review — passes

- C++23 is target-scoped in CMake, including the vendored ReaderCore target.
- Native Reader has one product core target; platform shells remain thin.
- CMake rejects duplicate core source entries and duplicate test entries.
- Raw MOT playback remains separate from MotionScript playback.
- Effect resource identity uses exact `(kind, u16 id, resource_slot)`
  provenance and does not substitute a guessed bank.
- Evidence gates reject structural/undecoded records as runtime instances.
- Parent matrices are deferred when not authoritative; there is no joint9
  fallback.
- The runtime keeps dynamic actor state separate from persistent components.
- Effects visibility is modeled as a presentation projection, not a runtime
  reset.
- The UI already omits Script buttons when the selected controller has no real
  link to that MOT.
- Phase-3 cumulative snapshots were not blindly merged over the newer v73
  core.

## Code review — blockers/corrections

### EFX-1 — universal profile boundary is incomplete

The shared motion attachment module still contains:

- `em034_effect_bindings()`;
- `install_effect_bindings()`;
- `em034` archive matching;
- the fixed Lady FXBANK slot 28 rule.

The EffectRuntime API is generic, but the profile provider is still physically
located in a shared Lady-aware attachment file. Unknown profiles correctly stay
effect-free, but this is not yet the final universal architecture.

Required correction:

- introduce a profile/effect registration boundary;
- move Lady bindings into a Lady profile adapter/data unit;
- let other characters/enemies/weapons register their own evidence-backed
  providers without modifying MotionPlayer or EffectRuntime.

### EFX-2 — P/G/V renderer/update coverage is incomplete

The runtime retains the complete V/P/E/G dependency graph, but the portable
presentation path currently materializes only decoded E-sprite children.

Current behavior:

- E children can produce a confirmed sprite tile;
- P and G children are retained but not rendered;
- V children with a non-zero activation offset remain preserved but are not
  advanced by a proven V-local clock;
- animated E resources currently use the canonical first atlas frame because
  the EXE animation clock is not yet bridged.

This is evidence-safe and avoids a fake grenade/trajectory/explosion, but it
means a complete Lady grenade effect is not yet physically reconstructed.

Required correction:

- reverse and implement the generic V local update clock;
- implement confirmed P/G presentation/update consumers;
- bind A animation progression to the EXE-owned clock;
- preserve each child resource as a separate dependency;
- add canonical retire/callback handling.

### EFX-3 — replay is not yet proven for every stateful subsystem

Lady state signals are replayed from the state entry, but generic bridge
callbacks and stateful presentation updates are not yet covered by a complete
frame-by-frame replay contract.

The current motion path also caps cloth stepping for large forward seeks. That
may be acceptable for a visual approximation, but it is not sufficient as
proof of exact sequential-vs-scrub determinism for every runtime subsystem.

Required correction:

- reset runtime state on reverse seek;
- replay state entry, signals, actors, effects and local effect clocks up to the
  target frame;
- add sequential 0->45 versus scrub 0->45 equality tests;
- add reverse 45->10 reset/replay tests;
- compare actor/effect event streams, parent matrices, active instances and
  rendered effect state.

### EFX-4 — Effect ON/OFF is not exposed through the Android shell

The native runtime has a presentation-only visibility API, but the current
Android `NativeBridge`/MainActivity surface does not expose a user toggle for
it. The existing Visual/Info dual-preview mode is separate from runtime effect
visibility.

Required correction:

- add JNI methods for effect presentation visibility;
- add the toggle through the existing reusable UI/menu surface;
- prove that toggling visibility does not change events, lifetime or active
  instances.

### EFX-5 — generic binding graph ownership needs hardening

`EffectBinding` and child records contain spans. The Lady table uses static
storage, which is safe for the current profile. A future profile provider could
pass temporary child vectors and leave dangling spans after registration.

Required correction:

- either deep-copy the binding/child graph into session-owned storage;
- or enforce and validate a documented static-lifetime provider contract.
Deep-copying is safer for a generic multi-profile runtime.

## Phase-2 compliance

### GO

- C++23/Spider core authority is retained.
- Phase-2 evidence tooling is preserved and adapted to current test
  inventories.
- Main and v73 no longer rely on a stale Phase-2 branch as live authority.
- The newer v73 runtime is not overwritten by an older cumulative Phase-3
  snapshot.

### PENDING

- exact-head host/Android run for the current v73 SHA;
- native test execution for all 28 v73 tests;
- Android APK build from the reviewed v73 head;
- physical Lady costume 1/2 acceptance;
- complete P/G/V effect runtime and grenade lifecycle;
- Windows/iOS wrapper builds against the current reviewed core.

## Next implementation order

1. Create the generic effect-profile registration boundary and move Lady data
   out of shared attachment code.
2. Complete generic V/P/G/A runtime update and presentation paths from EXE
   evidence.
3. Add deterministic sequential/scrub/reverse effect replay tests.
4. Expose the presentation-only Effects ON/OFF toggle.
5. Run exact-head CI and device verification on the resulting v73 head.
6. Only after acceptance, selectively promote stable core corrections to
   `main` and clean historical branch refs using preserved tags.

## Decision

`NR-Luna-v73` is the correct integration/review line.

Phase 2 is integrated and adapted at the source-contract level. v73 is not
ready for final physical acceptance yet because the complete universal
P/G/V effect runtime and exact-head execution evidence remain open.
