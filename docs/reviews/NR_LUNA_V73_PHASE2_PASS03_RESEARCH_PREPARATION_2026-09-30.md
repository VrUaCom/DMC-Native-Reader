# NR Luna v73 — Phase 2 Pass 03: research and implementation preparation

Date: 2026-09-30
Repository: `VrUaCom/DMC-Native-Reader`
Working branch: `NR-Luna-v73`
Reviewed HEAD: `efd8de37413d63eeab6d88fa2349e6c02d1e99ad`
Rengine submodule: `caf445226c7d61841292384a10e93e4f58ae29f9`

## 1. Scope lock

This pass is research and preparation only. It does not claim that the current
APK closes the physical Lady acceptance issues.

All work remains on `NR-Luna-v73`. The following refs are read-only and are not
part of this pass:

- `main`;
- `platform/android`;
- `platform/windows`;
- `platform/ios`.

Phase 1 remains closed. This work continues Phase 2 and does not reopen the
Phase 1 baseline.

## 2. Physical test facts carried into the investigation

The latest device test reports:

- the rocket reaches approximately the Kalina muzzle area but remains close to
  vertical and does not follow the launcher orientation;
- some body actions play while the weapon/component does not participate;
- pistol and mini-Uzi actions have no visible effect path;
- the tether/cable is visible in one action but absent in another action that
  also appears to require it;
- general playback is stable and the APK did not crash in the reported pass.

These are acceptance observations, not evidence that identifies an unknown
effect resource or an exact EXE owner. Unknown resources and semantics remain
gated by the project evidence policy.

## 3. Current code trace

### 3.1 Script and MOT ownership

The current playback state is centered on one `MotionState` and one selected
MOT payload:

- `load_scripted_motion()` resolves one script controller and one MOT;
- `MotionState::parts` contains the parts driven by that selected MOT;
- `apply_motion_frame()` evaluates those parts at one frame;
- the Lady body and Lady component-0 controllers are discovered separately in
  `pac_assembly.cpp`, but the active playback path does not form a generic
  synchronized multi-controller track set.

The Lady state bridge in `apply_lady_script_runtime()` applies recovered
placement/control-domain changes and emits a limited set of dynamic actor
events. It does not constitute a universal body/component/weapon animation
coordinator.

**Implication:** a body action can visibly run while the component that owns
Kalina or another weapon remains at a static placement or is driven by a
separate controller that was not scheduled for the same script frame.

### 3.2 Attachment and orientation

Persistent model parts already use the generic `HostJointSkeleton` placement
path in `part_attachment.cpp`. Dynamic Lady actors and effects use a separate
path in `motion_player.cpp`:

- Shl02 is spawned from Lady component-0 local node 0;
- the render actor receives `shl02_actor_matrix()`;
- V423 receives `shl02_effect_parent_matrix()`;
- `DynamicActorEvent` carries a resolved `world` snapshot;
- `EffectRuntime::apply_actor_event()` stores that snapshot and only changes it
  when a later event supplies another authoritative matrix.

There is no shared per-frame parent-pose resolver that takes an attachment
source, its current joint/node basis and a local transform and resolves the
result for every consumer. The Shl02 and V423 matrices are therefore separate
special paths, not one reusable attachment contract.

**Implication:** a translation can appear approximately correct while the
local forward axis, roll, or muzzle offset is wrong. This matches the reported
near-muzzle but near-vertical rocket symptom.

### 3.3 Effect presentation

The current Lady profile registers five confirmed V roots:

- Shl00 → V463;
- Shl02 → V423;
- Shl04 → V488 and V475;
- Shl05 → V276.

`resource_session.cpp` currently presents decoded E children. P and G children
are retained as exact dependencies but are not rasterized by the portable
presentation path. Non-zero V activation offsets remain deferred because the
V-local clock is not proven equal to MotionScript frame units. Animated E
records use the canonical first atlas frame until the EXE-owned A clock is
bridged.

**Implication:** an action can emit an actor event and still show no visible
effect when its required P/G/V/A update path is not closed. The absence of a
pistol or mini-Uzi effect cannot be repaired safely by assigning a guessed
texture or guessed effect ID.

### 3.4 Tether lifecycle

The current slot-30 tether presentation is a special CEm034 path:

- actor 3 is spawned only by the recovered state/signal cases currently listed
  in `apply_lady_state_entry()` and `apply_lady_signal()`;
- the dynamic presentation uses component-0 node 2 as its anchor;
- the endpoint comes from the dynamic visual world;
- the five-node chain is reconstructed in `skin_lady_slot30()`.

The endpoint/anchor pair is not represented as a generic runtime binding, and
the actor-3 spawn table is not a complete evidence-backed action-to-lifecycle
map.

**Implication:** one animation may activate the known actor-3 path while
another animation with similar visible semantics does not, which explains the
reported cable inconsistency without assuming that the cable is a texture
problem.

### 3.5 Raw `em034.pac` verification

The supplied raw corpus was read without modifying it. Input fingerprint:

- `em034.pac` SHA-256: `1a5a245c8348dee3fa37ef1a83da39f15f5c1576e252daf5adc897e349f56dff`;
- top-level PAC: 35 slots;
- top-level slot 12: 7,872-byte MotionScript, five banks;
- top-level slot 13: 512-byte MotionScript, five banks;
- top-level slot 6: body MOT pack, 61 declared slots / 27 populated MOTs;
- top-level slot 11: component-0 MOT pack, 43 declared slots / 10 populated MOTs;
- top-level slot 28: FXBANK with 75 named records and 80 physical inner slots.

The raw scripts close the first controller pairing needed for a bounded track
slice:

| script owner | top-level script | bank/action | Script Play resource | resolved MOT pack/slot | opcode-3 signal |
|---|---:|---:|---:|---:|---|
| Lady body | 12 | 4/3 | 403 | slot 6 / MOT 3 | frame 4: `[1,0,0,0,0]` |
| Lady body | 12 | 4/4 | 404 | slot 6 / MOT 4 | frame 4: `[1,0,0,0,0]` |
| Lady body | 12 | 4/5 | 405 | slot 6 / MOT 5 | frame 4: `[1,0,0,0,0]` |
| Lady component 0 | 13 | 4/3 | 400 | slot 11 / MOT 0 | none |
| Lady component 0 | 13 | 4/4 | 400 | slot 11 / MOT 0 | none |
| Lady component 0 | 13 | 4/5 | 400 | slot 11 / MOT 0 | none |

This is direct evidence that equal action numbers do not identify one
complete animation. The body and component controllers use different Script
Play resources, different MOT archives and different channel domains. The
first track-coordinator slice can therefore pair these confirmed tracks at one
shared script frame without inventing a new mapping. Actions outside this
closed table remain deferred until their controller/resource links are
confirmed.

The same corpus confirms the current `V423` graph in FXBANK slot 28: three
children, `E752` at activation offset 0, `E887` at offset 3, and `P337` at
offset 0. All three carry local translation `(60,0,0)`; `P337` carries a
90-degree Y rotation. This closes resource identity and local graph data, but
does not by itself close the P runtime subtype, projectile clock, or the final
render-axis contract. The rocket-like image must not be assigned to `P337`
solely from its appearance.

## 4. Confirmed facts versus open evidence

### Confirmed and safe to build on

- exact `(kind, id, resource_slot)` FXBANK identity;
- generic `EffectRuntime` spawn/update/deferred/retire model;
- raw MOT and Script Play separation;
- known CEm034 actor ownership and the five confirmed V roots;
- persistent component placement contracts and host-joint attachment path;
- slot-30 five-node source geometry and current skin projection;
- no identity fallback for a missing authoritative world matrix;
- no fabricated projectile trajectory or collision result.
- exact body/component controller pairing for bank 4 actions 3, 4 and 5:
  body resources 403/404/405 through top-level slot 6, component resource 400
  through top-level slot 11;
- the raw `V423` child graph and local transform values listed above.

### Open and requiring further research

- exact CEm034 body-state → component-0 MOT action mapping;
- how the EXE schedules multiple controller tracks against one body action;
- the correct Shl02 render-axis construction and its relationship to the
  Kalina model basis and muzzle node;
- exact P337 runtime subtype and its projectile/visual update contract;
- exact G consumers and their update/lifetime inputs;
- V-local update clock and A animation clock;
- complete actor-3 spawn/update/retire coverage for all tether actions;
- weapon-fire event ownership and exact pistol/mini-Uzi effect bindings;
- configuration-change persistence of the current Android session and
  selected slot/costume.

The raw research narrows the first implementation slice but does not close the
following acceptance gates:

- Shl02 actor/render basis and Kalina muzzle-node relationship;
- P337 subtype update/retire semantics and its relationship to the observed
  rocket-like silhouette;
- G consumers and A/V clock ownership;
- complete actor-3 tether lifecycle coverage;
- weapon-fire ownership for pistol and mini-Uzi effects.

## 5. Target architecture for the next implementation slice

The next implementation must separate profile evidence from shared execution.

```text
script controllers
        ↓
one synchronized script-frame coordinator
        ↓
generic component/attachment pose resolver
        ↓
actor, projectile, tether and effect events
        ↓
generic P/E/G/V runtime, clocks, presentation and retire
```

### 5.1 Pose resolver contract

Introduce one core pose contract that can resolve:

- a world anchor;
- a body joint;
- a persistent component node;
- a dynamic actor node;
- a projectile/tether endpoint;

using the current per-frame parent basis plus an evidence-backed local
transform. Profile adapters declare the source and local data; they do not
duplicate matrix math in character-specific branches.

The resolver must preserve the distinction between:

- actor render basis;
- effect parent basis;
- projectile launch basis;
- tether anchor/endpoint basis.

Those domains may be different, but each must be explicit rather than an
implicit point-specific exception.

### 5.2 Synchronized controller contract

The runtime needs a synchronized track set rather than assuming that one
selected MOT is the complete action. A track set must carry:

- controller identity and role;
- selected MOT and script action;
- shared script frame;
- independent placement/control domain;
- reset and reverse-seek behavior;
- evidence for every controller-to-action link.

The first implementation slice must not invent missing controller mappings. It
must expose an unresolved mapping as deferred/diagnostic state and keep the
known body animation playable.

### 5.3 Generic event contract

All profile events should enter one shared boundary with typed fields for:

- owner actor/component;
- state/lane/channel/signal;
- parent pose source;
- authoritative world matrix status;
- resource identity and evidence;
- local clock/lifetime domain;
- retire reason.

MotionPlayer should consume the generic boundary, while CEm034 supplies only
its evidence-backed profile tables and event producers.

## 6. Bounded implementation order

1. **Pose resolver and matrix tests** — make parent source and basis explicit;
   reproduce the current confirmed Shl02/V423 domains without changing their
   resource IDs.
2. **Track coordinator contract** — represent body/component/weapon tracks at
   one script frame; start with the closed bank 4 actions 3/4/5 pairing:
   body `403/404/405` plus component `400`.
3. **Actor/tether endpoint contract** — move spawn/update/retire and
   anchor/endpoint resolution behind the generic runtime boundary.
4. **Confirmed P/E/G/V update slices** — only implement consumers after the
   corresponding EXE/corpus evidence is closed; keep unknown P/G semantics
   preserved and non-presentable.
5. **Replay tests** — sequential playback, scrub, reverse seek, reset and
   terminal retire must produce deterministic event/pose streams.
6. **Exact-head build and APK** — only after the native contract and tests are
   green; then repeat the physical Lady scenarios.

The Android session-restoration regression remains a separate acceptance
blocker and must not be silently considered fixed by the runtime work.

### 6.1 Preparation/test matrix

| area | first-pass check | pass condition | status before implementation |
|---|---|---|---|
| raw controller mapping | Parse `em034` slots 12/13 and resolve bank 4 actions 3/4/5 | body MOT 3/4/5 and component MOT 0 are present at the same script frame with exact resource IDs | closed |
| track coordinator | Replay one shared frame through the confirmed body/component pair | both tracks evaluate; an unresolved extra track is deferred and does not replace the known body track | to implement |
| pose resolver | Feed synthetic parent matrices through body-joint, component-node and dynamic-actor sources | source and local transform are explicit; matrices stay finite; no unproven axis correction is applied | to implement |
| Shl02/V423 bridge | Compare actor render basis and effect-parent basis independently | V423 keeps its confirmed `(60,0,0)` child graph; Shl02 orientation remains a diagnostic result until the axis evidence closes | to implement |
| P/E/G/V lifecycle | Run V423 with E752/E887 and deferred P337 | E children use authoritative worlds; P337 is retained/deferred, never replaced by a guessed projectile | to implement |
| replay/reset | Play, seek backward, reset, and replay the same confirmed action | deterministic track/pose/event sequence and no stale dynamic actor instance | to implement |
| device acceptance | Re-run the user’s Lady scenarios after native tests and exact-head APK build | component participates, orientation/FX/tether results are recorded separately; rotation/session regression remains a separate blocker | later |

## 7. Preparation acceptance criteria

Research/preparation is complete when:

- [x] exact branch, HEAD and submodule are recorded;
- [x] current user-observed failures are classified separately;
- [x] current source owners and missing generic boundaries are identified;
- [x] confirmed evidence and unknowns are separated;
- [x] next architecture and bounded order are documented;
- [x] exact controller/action mappings needed for the first track slice are
      closed for bank 4 actions 3/4/5; all other mappings are explicitly
      deferred;
- [ ] exact Shl02 axis/muzzle evidence is closed;
- [ ] exact P/G/V/A update evidence needed for each claimed effect is closed.

Until the remaining unchecked evidence gates are closed, no new effect ID,
trajectory, orientation offset, or controller mapping should be guessed.
