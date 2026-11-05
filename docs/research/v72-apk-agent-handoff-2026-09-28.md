# v72 APK agent handoff — MotionScript-driven effects runtime

Date: 2026-09-28

This handoff is for the agent that has the Android SDK/NDK/CMake/Gradle
environment and a physical Android device. The source work is prepared in the
Reader worktree; APK build and physical acceptance are still external steps.

## Source locations

Native Reader:

- repository: VrUaCom/DMC-Native-Reader
- branch: `NR-Luna-v73`
- local worktree used here: /workspace/scratch/d80c358d4d3b/native-reader-v72
- base: `origin/main` `d3bf1732ded403bd9631663226bcaf8ad19682bb`
- exact merged source implementation head: `8b33b62feb1a4581e7824f3c003997974f5086e2`
- merge parents: `d3bf1732ded403bd9631663226bcaf8ad19682bb` + `caf56169f7a25ced7d5f3e1dba54d65a18ffa32c`
- latest Reader commit: `Merge v72 runtime into NR-Luna-v73`
- PR #104 is merged and closed. Its merge commit is c9e7e3e6f1f867047889444d001f59647dee64c8.
- This handoff describes the integrated NR-Luna-v73 continuation; the old “keep PR #104 DRAFT” instruction is obsolete.

Rengine evidence:

- repository: VrUaCom/dmc-rengine-cpp
- branch: reverse/em034-lady-runtime-20260927
- local worktree used here: /workspace/scratch/d80c358d4d3b/rengine-em034-evidence
- current local evidence head: c60fdeac3a34f394f9f61b0043c2f9d15e64df57
- canonical executable SHA-256:
  e454272ed0fb0247fcbcf300e5d55d7a3e96d50b89b9ffaff81bb978dcbdd082

## Main-integration status at handoff time

`NR-Luna-v73` is a local integration branch created from
`origin/main` `d3bf1732ded403bd9631663226bcaf8ad19682bb`. It contains the
post-main v72 implementation from `fix/em034-lady-assembly-info-export`,
including the generic EffectRuntime, unified ScriptEffectBridge, canonical
MOT-domain fix, link-driven script-button visibility, generic effect-binding
registry and stage HITS environment-collision integration. The merge was
resolved so the main-side FXBANK Visual Info/Gallery work remains present and
both `effect_visualization_test` and `effect_runtime_test` are retained.
The branch has not been pushed to GitHub yet. Use `git rev-parse HEAD` after
importing the bundle as the authoritative source head.

The Rengine evidence worktree contains the FX dispatch evidence and the new
stage-room HITS census through `c60fdeac3a34f394f9f61b0043c2f9d15e64df57`.
The local remote-tracking feature ref is still `23d81babb0a34140d519de257bd6c12c09a1b265`;
the local commits must be pushed or transferred when the APK agent uses a
separate clone.

Input files already available in the workspace:

- upload/em034(2).pac
- upload/dmc3(8).exe
- inputs/dmc3(8).exe

Transfer artifact for the Android build agent:

- integrated bundle: `/workspace/scratch/d80c358d4d3b/nr-luna-v73-merged-8b33.bundle`
- bundle tip: `8b33b62feb1a4581e7824f3c003997974f5086e2`
- bundle prerequisites: `23d81babb0a34140d519de257bd6c12c09a1b265` and `d3bf1732ded403bd9631663226bcaf8ad19682bb`
- bundle SHA-256: `97b7b10e6d882b739943eda662c0a0bafad811846c1886810a4332054503bcf7`

The bundle contains the integrated main + v72 source, including stage HITS
environment-collision support. The APK agent should verify
`git rev-parse HEAD` after importing it and run the exact-head workflow from
this document.

## Reader implementation

The important source files are:

- app/src/main/cpp/include/dmcresource/motion/effect_runtime.h
- app/src/main/cpp/modules/motion/effect_runtime.cpp
- app/src/main/cpp/include/dmcresource/motion/motion_player.h
- app/src/main/cpp/modules/motion/motion_player.cpp
- app/src/main/cpp/include/dmcresource/motion/part_attachment.h
- app/src/main/cpp/modules/motion/part_attachment.cpp
- app/src/main/cpp/modules/effect_bank.cpp
- app/src/main/cpp/modules/pac_assembly.cpp
- app/src/main/cpp/modules/resource_session.cpp
- app/src/main/cpp/include/dmcresource/view_renderer.h
- app/src/main/cpp/view_renderer.cpp
- app/src/main/java/com/dmcrengine/nativeviewer/MainActivity.java

Motion strip UI contract:

- A Script Play button is created only when the native MotionScript link map
  says that the controller can play the current MOT.
- An absent link is hidden, not rendered as a disabled or faded button.
- The raw MOT button remains independent and is always retained for a valid
  MOT entry.
- This rule is controller-count agnostic: it applies to one, two, or any
  number of MotionScript controllers without a Lady-specific branch.

Effect installation contract:

- PAC assembly assigns one generic `install_effect_bindings()` registry
  callback for every archive.
- The callback installs only profile providers with EXE/corpus-confirmed
  evidence; unknown profiles remain effect-free until their provider is
  reversed.
- MotionPlayer and EffectRuntime do not select effects by character name, MOT
  name, guessed frame or semantic label.
- app/src/main/cpp/include/dmcresource/resource_session.h
- app/src/main/cpp/CMakeLists.txt

The native test additions/changes are:

- app/src/test/native/effect_visualization_test.cpp (main-side Visual Info)
- app/src/test/native/effect_runtime_test.cpp
- app/src/test/native/motion_playback_test.cpp
- app/src/test/native/pac_assembly_test.cpp
- app/src/test/native/player_coat_test.cpp

The evidence summary for the Reader is:

- docs/research/dmc3-motion-effect-runtime-v72.md
- docs/research/v72-stage-room-hits-runtime-2026-09-28.md

The reverse-side corpus receipt for the supplied rooms is:

- `docs/research/dmc3-stage-room-hits-census-2026-09-28.md`
- `st001.pac`: HITS slots 3 and 6 (`294` + `20` triangle-plane records)
- `st002.pac`: HITS slot 3 (`477` triangle-plane records)

Reader retains these HITS sources by physical slot and exposes the optional
`HITS room collision` inspection toggle. It does not feed HITS into Shl02
steering without the EXE-confirmed gameplay target/query context.

## Resource-backed presentation pass

The current continuation adds a real presentation path for resource-backed E
records. PAC assembly retains each direct FXBANK source and its parsed bank in
`Session::effect_banks`; bank-owned T records are decoded through the existing
DDS/texture-set reader and are never replaced with an attached model texture.
`resource_session.cpp` walks the active generic `EffectRuntime` instances,
composes V local transforms with the authoritative actor matrix, resolves E's
T/A/rectangle fields and hands explicit `ViewState::EffectSprite` records to
`view_renderer.cpp`. Raw MOT never enters this path.

For the current em034 corpus the visible zero-threshold part is:

```text
S12 -> state 0x56..0x58 -> lane1/channel0/value1
   -> CEm034Shl02 exact slot20 node0 spawn matrix
   -> V423
   -> E752 -> T5 rectangle (128,64,64,64)
```

`V423 -> E887 -> A32/T26` is also retained and decoded, but its V activation
offset is `3`. The V-local clock has not been proven equal to MotionScript
frame age, so the portable presentation pass deliberately does not promote
that non-zero threshold to a guessed frame timer. `P337` remains a confirmed
dependency but its subtype is still `PRESERVED_UNDECODED`. Shl02 steering,
bounce/collision and world-triggered retire still require gameplay-world
context; standalone Reader must not synthesize a trajectory or explosion.

The effect renderer is generic: it consumes `EffectBinding`/FXBANK identity
and explicit parent matrices, not a Lady-specific `if`, semantic effect names,
MOT names or joint fallbacks. New character/enemy/weapon profiles add only
their evidence-backed provider/bindings and use the same retention, resource
lookup, replay and renderer boundary.

## Runtime state currently covered

Script Play is separate from raw MOT. Confirmed em034 FXBANK bindings are:

| owner | FXBANK key | gate |
|---|---:|---|
| Shl00 | V/463 | lane1/channel0/value1 |
| Shl02 | V/423 | lane1/channel0/value1 |
| Shl04 | V/488 | lane1/channel0/value1 |
| Shl04 | V/475 | lane1/channel0/value1 |
| Shl05 | V/276 | lane1/channel0/value1 |

The runtime keeps canonical kind/id/resource-slot provenance, evidence status,
parent domain, deferred transforms, actor ownership and retire events. It does
not infer an effect from a MOT name or guessed frame.

The installation path is profile-neutral. A reverse bridge for any other
character may copy its evidence-backed `EffectBinding` records into
`Session::script_effect_bindings` with `set_script_effect_bindings()`; the
generic `ensure_effect_runtime()` then applies the same FXBANK resource gate,
presentation toggle, replay and retire rules. MotionPlayer has no character
name or semantic-effect dispatch. Actual bindings and actor/event producers
still require profile-specific EXE/corpus evidence.

If the profile needs a custom actor/event producer, assign
`Session::script_effect_bridge` (`prepare`, `reset` and `step`). The prepare
hook registers the profile table before the generic runtime is configured;
the step hook is called only for Script Play frames and is reset on reverse
seek; raw MOT never calls it. The producer should emit only typed, evidence-backed
`DynamicActorEvent` records into the shared `EffectRuntime`.

run_script_frame() is the generic public boundary. Presentation visibility is
separate from runtime state; turning effects off must not stop replay, lifetime,
actor events or retire events.

## Resolved MOT-domain warning

The physical video from the previous `main` APK exposed this warning:
`slot_0011.pac/slot_0000.mot could not drive any model part ...
mot-channel-domain-differs-from-model-nodes`.

The resource ownership is now closed generically: `em034_013 -> bank 4 ->
top-level PAC slot 11 -> slot20`, and slot20 is the canonical 3-node component
controller. `slot_0011/slot_0000.mot` declares two channel masks, while the
slot20 model has three initialized nodes. The canonical EXE binding loop at
`0x140310A80` consumes one serialized mask per initialized CMotion joint; the
third mask is the aligned zero tail already present in this MOT header.

Reader commit `4ee976042b37eaccfe201d14c3dfc20931beb449` extends a shorter
declared domain only when the required mask entries are inside the serialized
header and are physically zero. A non-zero tail or an out-of-header entry is
rejected. This is a generic MOT compatibility rule, not an em034/Lady branch,
and it does not synthesize joints or redirect the motion to another model.

Native coverage for this case is in
`app/src/test/native/motion_playback_test.cpp`: the canonical 2-mask/3-node
case must load and a non-zero padding case must fail closed.

Shl02 requires gameplay-world target/query context after its exact spawn pose.
Standalone Reader must keep the canonical spawn transform and must not invent a
trajectory.

## Rengine evidence files changed for this pass

In the Rengine worktree, inspect:

- docs/research/dmc3-fxbank-effect-runtime-records-2026-09-27.md
- data/reverse/dmc3-fxbank-em034-runtime-20260927.json

These record the FXBANK loader identity, M companion ABI, V child graph and
activation offsets, P structural constructor envelope, G update boundary, and
E state/mode/world-space preparation boundary. P subtype semantics remain
PRESERVED_UNDECODED.

## Android build procedure

Required versions are defined by the repository and should not be changed:

- Java 17
- Gradle 9.5.0
- Android platform 36
- Build tools 36.0.0
- NDK 30.0.16248370
- CMake 3.22.1
- ABI arm64-v8a

From the Native Reader repository, after the source changes are present:

    python3 tools/test_verify_device_apk.py
    gradle --no-daemon clean :app:assembleDebug
    python3 tools/verify_device_apk.py \
      --sdk "$ANDROID_SDK_ROOT" \
      --signing-policy stable-debug \
      app/build/outputs/apk/debug/app-debug.apk

For the complete exact-head evidence path, use a clean checkout:

    python3 tools/run_phase2_exact_head.py \
      --sdk "$ANDROID_SDK_ROOT" \
      --gradle gradle \
      --expected-head "$(git rev-parse HEAD)"

The expected APK is:

app/build/outputs/apk/debug/app-debug.apk

Expected Android identity:

- versionCode = 72
- versionName = 1.0.45
- stable debug signer SHA-256:
  f483539463f89dd957a8f7c68a3bb75da17450163f2e8767b4c47d5f1899adac

Canonical workflows:

- .github/workflows/android.yml
- .github/workflows/android-release.yml

## Physical acceptance checklist

Record screenshots and the APK SHA-256 for each relevant state.

1. Open em034.pac with costume1.
2. Raw MOT: skeleton animation only; no Script Play effects or script signals.
3. S12: verify actions 3, 4 and 5 (Shl02/resource-backed effect spawn), then
   actions 13, 44, 46 and 50, including equipment, dynamic actors and
   confirmed effect events. For actions 3/4/5 the exact spawn resource must be
   visible at the canonical slot20 node0 pose; do not mark trajectory,
   bounce/collision or world-triggered retire as standalone-confirmed.
4. S13: verify independent slot20/controller behaviour and ensure its signals
   do not enter the body lane.
5. Check Shl02 canonical spawn pose and mark post-spawn movement as requiring
   gameplay-world context; do not accept a fabricated straight trajectory.
6. Check Shl03 slot26/slot30 and the five-point tether deformation.
7. Scrub 0 -> 45, then reverse 45 -> 10. Compare with sequential playback.
8. Switch to costume2 and repeat the same runtime actions. Behaviour must be
   identical while body/hair/textures differ.
9. PR #104 is already merged and closed; do not use its old Draft state as an
   acceptance gate. Record the tested APK commit and SHA-256 instead.

## Important non-claims

This handoff does not claim that a complete portable V/P/E/G pixel renderer is
finished. The current Reader has a resource-backed E/T/A presentation path for
the decoded zero-threshold V children and renders the already closed Shl
geometry paths. It must not invent a P/G quad, particle semantic, muzzle flash,
smoke, explosion, fallback joint or guessed timer from an undecoded record.

The current worktree has passed the available host validation, but no Android
build or physical-device acceptance has been run in this environment.
