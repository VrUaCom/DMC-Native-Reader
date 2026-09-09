# Spider Family — Native Reader architecture

Status: canonical naming and responsibility contract for DMC Native Reader 1.x.

This document defines three distinct Spider roles. They share the same general principle — compact C++20 orchestration over reusable native modules — but they solve different problems and must not collapse into one oversized abstraction.

## Spider Black Widow

Purpose: progressively displace Java/Kotlin application business logic while keeping Android platform integration thin.

Black Widow owns decisions and state, not Android widgets or Android framework calls.

Black Widow responsibilities:

- application/session state machine;
- capability-derived UI policy;
- companion-resource state;
- action availability and action state;
- navigation/application decisions that are platform-neutral;
- typed status/error/result values for the platform shell;
- orchestration entry points that the Android shell can invoke without understanding DMC formats.

The Android Java layer remains only a mechanical platform shell for:

- Activity lifecycle;
- Storage Access Framework / Uri / ParcelFileDescriptor;
- Android View creation and event forwarding;
- showing already-resolved native state/results.

Java must not infer business state from diagnostic strings. In particular, code such as:

`detail.startsWith("PTX companion attached:")`

is forbidden as a long-term contract. Black Widow must publish a typed/bitmask state such as `TextureCompanionAttachable` and `TextureCompanionAttached`.

Target direction:

`Android event -> thin Java shell -> Black Widow -> native modules -> typed state/result -> thin Java shell`

The long-term goal is to make the Java layer replaceable by another platform shell without moving DMC or application business logic.

## Spider Tarantula

Purpose: provide a compact C++20 workflow/scripting layer for tasks that would otherwise be written as Python orchestration.

Tarantula is not a parser and not a replacement for canonical C++ modules. It composes reusable operations into concise higher-level workflows.

Candidate responsibilities:

- declarative resource-processing workflows;
- batch pipelines;
- reusable transformation/inspection scripts;
- conditional execution and data-flow between modules;
- tooling automation where direct handwritten C++ orchestration becomes repetitive.

Tarantula must not be placed in hot inner loops such as rasterization, barycentric interpolation, UV math, matrix math, or texture sampling. Those stay direct C++.

Tarantula is not required for Native Reader 1.0 PTX attachment and must not be introduced merely for naming symmetry.

## Spider Crusader

Purpose: simplify C++ module orchestration and dependency execution without replacing the modules themselves.

Crusader is the role currently closest to the existing generic native executor used by the Native Reader texture route.

Crusader responsibilities:

- compact native plans;
- dependency ordering;
- operation binding;
- fail-closed execution;
- reuse of the same operations across several product flows;
- keeping orchestration code out of individual parsers/codecs/render loops.

Current PTX example:

`PTX framing -> DDS projection`

Future companion example:

`PTX framing -> TextureSet -> required-slot resolution -> decode required slots -> validate binding -> attach material set`

Crusader does not own DDS, PTX, MOD, SCM, UV, rendering, or format offsets. Those remain in their canonical modules.

The current pinned ReaderCore generic native executor is used as the underlying execution primitive. DMC Native Reader may expose the product-facing name "Crusader" without changing dmc-rengine-cpp.

## Naming rule

Use the names only for these roles:

- **Black Widow** = Java/application-shell displacement.
- **Tarantula** = Python/workflow displacement.
- **Crusader** = C++ orchestration simplification.

Do not call every helper or parser a Spider. A normal reusable C++ function/module stays a normal module when Spider adds no value.

## PTX -> MOD/SCM application

The intended architecture is:

`MOD/SCM canonical adapter -> RenderScene (geometry + UV + texture slots)`

`PTX -> texture module -> TextureSet`

`Black Widow -> exposes attach action/state to Android shell`

`Crusader -> orchestrates PTX/TextureSet/slot-resolution attachment steps`

`material projection -> per-triangle texture slot mapping`

`renderer -> direct C++ UV interpolation + RGBA sampling`

Black Widow must publish typed state for the `.PTX` button. Java must not inspect diagnostic strings or duplicate capability policy.

## Duplication rule

Before adding new code:

1. Check whether a canonical parser/codec/module already owns the operation.
2. If two call sites need the same logic, extract one reusable module instead of copying it.
3. Use Crusader when repeated dependency/orchestration code is the duplication.
4. Use Black Widow when repeated platform-shell state/business decisions are the duplication.
5. Use Tarantula only when higher-level workflow/script repetition justifies it.
6. Keep hot, simple numerical code as direct C++.

This keeps Native Reader modular without turning modularity itself into overhead.
