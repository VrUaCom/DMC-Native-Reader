# Spider Family — Native Reader architecture

Last updated: 2026-10-21.

Status: canonical responsibility/naming contract for Native Reader 1.x. Spider names describe orchestration roles; they do **not** replace parsers, codecs or direct numerical C++.

## Spider Black Widow — active product role

Black Widow progressively moves application/business decisions out of the Android Java shell and into platform-neutral C++20 typed state.

Current responsibilities include:

- session/action state;
- capability-derived UI policy;
- companion-resource state;
- action availability/state;
- typed status/error/result transport;
- product decisions that do not require Android framework APIs.

The Android Java layer remains responsible only for platform mechanics such as Activity lifecycle, SAF/Uri/ParcelFileDescriptor handling, View creation/event forwarding and presentation of already-resolved native state.

Java must not infer application state from diagnostic strings and must not understand DMC offsets/layouts.

Candidate v26 extends Black Widow with independent focused-inspection availability for UV, mesh/object and hierarchy information. Information authority may exist even where a spatial render action is unavailable.

## Spider Crusader — orchestration role

Crusader is the compact C++20 dependency/execution role used when repeated multi-module flow would otherwise duplicate orchestration code.

Responsibilities:

- compact native operation plans;
- dependency ordering;
- operation binding;
- fail-closed execution;
- reusable composition across product flows.

Examples include texture framing/projection and model + texture companion workflows. Crusader never owns DDS, PTX, MOD, SCM, UV math, rendering or binary offsets; those stay in their canonical modules.

Do not introduce a second executor just to satisfy the Spider naming scheme. Use the existing reusable native execution primitive where it already solves the flow.

## Spider Tarantula — future workflow/scripting role

Tarantula is a planned compact C++20 workflow/scripting layer for higher-level tasks that might otherwise be implemented as Python orchestration.

Candidate uses:

- declarative resource-processing workflows;
- batch pipelines;
- reusable transformation/inspection scripts;
- conditional/data-flow composition;
- tooling automation where direct handwritten orchestration becomes repetitive.

Tarantula is **not required** for the accepted Native Reader v24 path and must not be inserted into hot inner loops or simple direct operations merely for symmetry.

### First Tarantula workflow: texture format change

DMC Rengine (branch `claude/devil-microy3-decompile-port-2v8pne`, merged with
`experiment/spider-python-migration`) runs `texture-reencode` as a Tarantula
workflow and offers an in-memory entry for viewers:
`spider::tarantula::run_texture_reencode(bytes, request)`.

- **Steps:** acquire → inspect → transform[slot] → assemble → validate →
  publish.
- **Replaces:** a scratch Python + etcpak prototype.

The Reader's "Texture format" action is the product counterpart:

- a Crusader plan (select source → re-encode → open result → verify) over
  the copied `texture_reencode` module;
- Black Widow flags `CanReencodeTextures` and `CanSaveSource` for the menu.

The output is byte-identical to the Rengine workflow. When the vendored
Rengine includes the workflow, the action's re-encode step should call it
and drop the copied modules.

### Plan builder and typed operations

`spider/plan_builder.h` mirrors DMC Rengine `spider/plan_builder.hpp`
(branch `experiment/spider-python-migration`).

- **Plans:** built from named nodes; dependencies can only point to earlier
  nodes.
- **Bad graph:** fails closed with `invalid_plan` before any operation runs.
- **Labels:** turn an `ExecutionReport` into the step that stopped.
- **Typed operations:** `spider::cpp23::bind<State, &fn>` adapts
  `bool fn(State&, operand) noexcept` to the Crusader ABI with plain
  function pointers.

The texture re-encode action is the first user. Its failures read
`"<step> failed: <reason>"`. A session opened from a PAC slot carries its
container natively (`Session::container_source`, Black Widow
`ReencodeRebuildsContainer`), so the shell passes no container or child
index.

### Stage configuration, long press, sound and export actions

`spider/stage_actions.h` holds the product actions of the stage
configuration over a stage, the view's long press, sounds and PNG export.

- **Black Widow** publishes `CanLoadStageConfig`, `StageConfigLoaded`,
  `StageMarkersShown` and `CanPlaySound`; the shell enables and toggles its
  tools from these bits only.
- **Load** is a Crusader plan: read members -> publish (markers placed in
  stage space by `stage_markers`).
- **Every action returns a typed outcome**: `ok`, the one platform step left
  to the shell (`PickStageConfig`, `ShowMarkerCard`, `StartScrub`,
  `OpenSession`), the line to show, written by the core, and the marker or
  opened session. Menus and the marker card come as typed rows and buttons
  with ids the shell sends back.
- **Long press** policy lives in `hold_view`: a stage-config marker first,
  then the joint, then scrubbing; the shell passes only the user's gesture
  settings.
- **Export**: `export_image_size` / `render_export_image` choose the picture
  (a UV map at its texture's resolution, anything else its preview).
- **Platform steps only** stay in Android: the SAF picker, the descriptor,
  the `AudioTrack` that plays the PCM the core decoded, the dialog that
  shows the rows. Java records `ActionOutcome`, `NativeMenu`, `MarkerCard`
  and `SoundPcm` are passive.

## Hot-path rule

Rasterization, barycentric interpolation, vector/matrix math, UV math, texture sampling and similarly small/hot numerical operations remain direct C++20.

Spider is useful only when it reduces duplicated orchestration or platform/business coupling without obscuring ownership.

## Current model + texture direction

```text
MOD/SCM canonical adapter
  -> RenderScene (geometry + UV + texture slots)

PTX / DDS native texture authority
  -> TextureSet

Black Widow
  -> typed attach/action/UI state

Crusader / reusable orchestration where justified
  -> dependency order / fail-closed composition

model_texture_binding + scene_projection
  -> validated per-triangle texture slot mapping

view_renderer
  -> direct C++ UV interpolation + RGBA sampling
```

The same ownership rule applies to candidate UV/inspection work: `uv_gallery` and `session_inspection` consume already typed/canonical data; Spider publishes state, not binary semantics.

## Naming rule

Use these names only for their defined roles:

- **Black Widow** = application/platform-shell displacement through typed state;
- **Crusader** = reusable C++ dependency/orchestration simplification;
- **Tarantula** = higher-level C++20 workflow/scripting layer.

A normal parser, codec, helper or mathematical function remains a normal module when Spider adds no architectural value.

## Duplication rule

Before adding code:

1. check whether DMC Rengine/native canonical code already owns the operation;
2. if two call sites need the same logic, extract one reusable module rather than copying it;
3. use Crusader when repeated dependency/orchestration flow is the duplication;
4. use Black Widow when platform-shell/business decisions are the duplication;
5. use Tarantula only when higher-level workflow repetition justifies a scripting abstraction;
6. keep direct hot/simple numerical code direct C++.
