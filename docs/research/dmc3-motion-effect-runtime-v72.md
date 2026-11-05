# v72 MotionScript effect runtime bridge

> v73 update: the V-local clock, E lifetimes, the spawn parent domains and
> the CEm034Shl02 flight are closed in
> [dmc3-shell-effect-runtime-exe-v73.md](dmc3-shell-effect-runtime-exe-v73.md).
> Statements below about frozen Shl02 poses, `ParentActorRetire` for V423 and
> non-zero activation thresholds being unpresentable are superseded there.

This Reader slice keeps raw MOT playback separate from the MotionScript
runtime. `EffectRuntime` consumes typed dynamic-actor events emitted by the
character bridge; it does not infer an effect from a MOT name, frame guess or
body joint.

## Confirmed profile bindings

The em034 profile table uses the canonical FXBANK source `em034.pac` slot 28
(`em034_028.pnst`) and FXBANK identity `(kind, u16 id)`:

| owner | kind/id | parent domain | trigger gate | status |
|---|---|---|---|---|
| Shl00 | V/463 | DynamicActor | lane1/channel0/value1 | EXE_AND_CORPUS_CONFIRMED |
| Shl02 | V/423 | DynamicActor | lane1/channel0/value1 | EXE_AND_CORPUS_CONFIRMED |
| Shl04 | V/488 | DynamicActor | lane1/channel0/value1 | EXE_AND_CORPUS_CONFIRMED |
| Shl04 | V/475 | DynamicActor | lane1/channel0/value1 | EXE_AND_CORPUS_CONFIRMED |
| Shl05 | V/276 | DynamicActor | lane1/channel0/value1 | EXE_AND_CORPUS_CONFIRMED |

`E765`, `V435`, `V277` and `V473` are not promoted by this table. Their
runtime/collision/lifetime paths are distinct or world-dependent as recorded
by the reverse evidence.

The assembled Session records direct FXBANK slots before descending into the
nested PNST. PAC assembly assigns one generic `install_effect_bindings()`
registry callback for every archive. The current registry provider installs
the em034 table only when slot 28 is really present and identified as FXBANK;
a missing bank or an unregistered profile produces no substitute effect
binding.

The installation boundary is profile-neutral. A non-em034 reverse bridge adds a
provider to the same registry and places only its confirmed records into
`Session::script_effect_bindings` through `set_script_effect_bindings()` before
using the same `ensure_effect_runtime()` path. `MotionPlayer` does not inspect
character names, semantic effect labels or MOT names. The profile still owns
the evidence and the actor/event producer; the shared runtime owns resource
gating, parent ownership, replay and retire. For a non-Lady profile, the
actor/event producer is registered through the `ScriptEffectBridge`
prepare/reset/step hooks; the hooks run only in Script Play and are reset on
reverse seek. This keeps profile data and event production separate from the
one shared runtime implementation.

The current `em034_028.pnst` corpus also confirms the first-level V child
dependencies without assigning human names:

| V key | direct child graph |
|---|---|
| V276 | E571, E571 (second occurrence has the recovered local Z rotation) |
| V423 | E752, E887, P337 |
| V463 | E741 |
| V475 | E404 |
| V488 | P18, P3, P16, P2, E1, V8 |
| V8 | E10, E10, E10, E2 |

The Reader now retains exact `(kind,u16 id,source slot)` catalog keys from the
FXBANK manifest and requires both the root key and every confirmed first-level
child key to be present before creating an effect instance. The resource gate
now walks the nested graph as well, so V488 also requires V8's E10/E2
dependencies. Child graph transforms remain resource data; no semantic names
or renderer behavior are inferred from them.

The V consumer also uses the signed `i16` at `entry + 0x08` as an activation
gate against its own local accumulator. The confirmed em034 values are
retained in the child records, but are not converted to MotionScript-frame
timers because the V-local clock has not been proven equal to script-frame
units. The V488 E1 child retains its corpus translation `(0,15,0)` and scale
`(1.1,1.1,1.1)`.

## Transform boundary

Shl02 uses the already materialized exact actor matrix derived from the live
slot20 node0 world. Shl00, Shl04 and Shl05 bindings are retained as deferred
effect events until their exact actor-domain matrix is available in Reader;
identity is never used as a spatial fallback. A deferred event is not
presentable and does not create an active instance.

The Shl02 event also carries an explicit
`requires_gameplay_world_context` marker. Its spawn matrix is canonical, but
post-spawn steering goes through the EXE shared target/query helper. Standalone
Reader freezes the recoverable pose and exposes the dependency to inspection;
it does not synthesize a trajectory or target.

The presentation toggle changes only visibility state. Reset/replay and actor
retire events remain active in the runtime regardless of that toggle.

Evidence is also a materialization gate: `EXE_CONFIRMED` and
`EXE_AND_CORPUS_CONFIRMED` may create an effect instance. Corpus-only,
structural, semantic-candidate and preserved-undecoded records remain in the
event/evidence stream but cannot silently become a presentable effect.

Lifetime is carried separately from the factory binding. The current five V
bindings use the EXE-confirmed `ParentActorRetire` contract: V463 is owned by
Shl00, V423 by Shl02, V488/V475 by Shl04, and V276 by Shl05. The Reader does
not replace the actor state machine with a fixed child timer. In standalone
mode an actor can remain deferred when its exact world matrix or gameplay
collision context is unavailable; an authoritative actor retire event still
retires its effect instance. This distinction prevents a confirmed FXBANK
identity from being mistaken for a guessed effect lifetime.

## V composite update boundary

The canonical V update path is recorded separately from the presentation
path:

```text
V update 0x140324A80
  -> common delta 0x1403261B0
  -> V-local accumulator (+0xF0) advances by the recovered delta (+0x14)
  -> entry +0x08 gate is checked
  -> child 0x140324680 is created through the P/E/G/V dispatch map
  -> child pointer/active arrays are retained in the V object
  -> common retire 0x1403261E0 when the V owner reaches its retire path
```

The observed V object has eight child pointer/flag positions. Parent flags are
propagated to created children, but the exact mapping from this local V clock
to MotionScript frame units remains open. Reader therefore preserves the
activation offsets and graph dependencies without replaying them as guessed
script-frame events.

## Render boundary

`render_session()` now has a resource-backed presentation adapter in addition
to the already materialized `Session::lady_dynamic_visuals` Shl geometry.
PAC assembly retains the original FXBANK bytes, parses the exact `(kind,u16
id)` records, and decodes the bank-owned T textures. The generic presentation
walk resolves V child transforms and EXE-confirmed E records to their linked T
texture plus either the direct rectangle or the linked A frame rectangle. The
result is submitted to the normal software renderer through an explicit world
anchor; it never looks at an MOT name and never attaches an unknown effect to
a body joint.

This closes the first visible resource-backed path for the Lady grenade-side
graph (`V423 -> E752` at the canonical zero-threshold entry; `E887/A32` is
retained and decoded but its non-zero V activation remains pending the V-local
clock bridge). For an otherwise eligible A-linked E record, the adapter uses
A's exact first atlas frame until the EXE-local E animation clock is bridged;
it never uses Script Play age as a substitute clock. P/G records remain
preserved dependencies and are not replaced
by a guessed quad/particle renderer. The camera-facing quad and normal-alpha
composition are a portable presentation adapter, not a claim that every
downstream EXE blend/depth mode is pixel-identical. The exact Shl02 spawn
matrix remains canonical; its post-spawn steering/collision still requires
gameplay-world context.

The runtime therefore exposes two separate views:

- `active_instances()` — internal state/lifetime view used by replay and
  retire handling;
- `presentation_instances()` — presentation-only view, empty when the
  `Effects ON/OFF` switch is off while the internal state continues to run.

No P/G record is converted into a guessed quad, particle, semantic name,
timer or body-joint attachment. Non-zero V activation thresholds are retained
as runtime data and are not treated as MotionScript-frame timers. Raw MOT
remains isolated from this path.

## Canonical short MOT domain compatibility

The physical `main` APK video exposed a domain warning for
`slot_0011.pac/slot_0000.mot`. Reverse/corpus ownership is now explicit:
`em034_013 -> bank 4 -> PAC slot 11 -> slot20`, where slot20 is a three-node
component controller. The MOT header declares two channel masks.

The EXE binding loop at `0x140310A80` iterates the initialized CMotion joint
count and advances the serialized mask pointer once per initialized joint. For
this canonical resource, the aligned header tail contains the zero mask for
the third node. Reader commit `4ee976042b37eaccfe201d14c3dfc20931beb449`
passes a local padded document to the generic binding projection only when all
required entries remain inside the serialized header and are zero. Non-zero
padding and out-of-header access fail closed. This preserves the generic
runtime contract and avoids a character-specific model redirect.

## Verification scope

Native regressions cover FXBANK `M` physical companion consumption for PTX and
empty slots, lane gating, deferred transform promotion, owner retire, raw MOT
isolation, profile-neutral binding installation and evidence-safe rejection of unknown owners. The generic
`run_script_frame()` boundary also reports component transitions alongside
actor/effect events. A host/Android build must still run before these changes
can be called green; this workspace does not contain the native CMake
toolchain.
