# DMC Native Reader — Product Vision

## Mission

**Make DMC resources feel like ordinary files.**

DMC Native Reader exists to turn opaque Devil May Cry resource files into representations that ordinary users can open, recognize and understand on everyday devices.

A user should not need to know a binary layout, reverse-engineering terminology, offsets, FourCCs or the internal history of a format in order to inspect a resource. The ideal interaction is as ordinary as opening a photo:

```text
photo.jpg
  -> open
  -> image

model.mod
  -> open
  -> 3D model

scene.scm
  -> open
  -> scene

texture.dds
  -> open
  -> image

bundle.ptx
  -> open
  -> texture gallery
```

The long-term product goal is not to make users think about file formats more often. It is to make DMC resource formats disappear behind familiar, direct representations.

## What Native Reader is

Native Reader is the **viewability and accessibility layer** of the DMC tooling ecosystem.

Its core verbs are:

```text
Open
Recognize
Inspect
Visualize
Navigate
Understand
```

It should answer the basic question:

> "I have this DMC file. What is it, and what is inside it?"

The answer should be immediate, visual where possible, evidence-aware, and usable without specialist knowledge.

## What Native Reader is not

Native Reader is not intended to become the primary authoring, archive-management or repacking workspace.

Editing, replacement, extraction/repacking, archive workflows and broader resource-management belong to dedicated tools such as Pocket GDS and to canonical writer/authoring layers in DMC Rengine.

Some carefully selected interoperability actions may be added later — for example handing a resource off to an editor or authoring tool — but they must not turn Native Reader into a second competing resource manager.

The product must preserve a simple expectation:

> open a file -> immediately understand and view it.

## Audience

Native Reader is deliberately aimed at more than reverse engineers.

It should be useful to:

- people who have never studied DMC binary formats;
- casual modders who only want to see what a file contains;
- model and texture modders;
- technical modders validating resources;
- reverse engineers inspecting canonical structure;
- tool developers who need a consistent read-side representation.

The project succeeds when specialist file knowledge becomes less necessary for routine inspection.

## Product principle: familiar representations

Each promoted resource family should map to the most natural familiar representation available.

Examples:

| Resource | Native Reader representation |
| --- | --- |
| MOD | 3D model, hierarchy, skin/material inspection |
| SCM | 3D scene, hierarchy, transforms, material state |
| DDS | image preview |
| PTX | texture gallery with navigable DDS children |
| future animation resource | animation/timeline preview |
| future structural graph resource | graph/tree representation |
| future collision/bounds resource | geometric overlay/volume representation |

A format should not be forced into a 3D viewer, image viewer or tree simply because one already exists. The module publishes capabilities; the product chooses the representation appropriate to those capabilities.

## Architecture principle

Native Reader is not a collection of independent format viewers.

The architecture is:

```text
resource bytes
    |
    v
canonical / bounded C++20 reader authority
    |
    v
typed resource projection
    |
    +--> InspectionDocument
    +--> RenderScene
    +--> ImagePreview
    +--> ChildResource[]
    +--> ResourceCapabilities
    |
    v
generic session
    |
    v
platform presentation
```

A new format should normally mean:

```text
parser/source authority
  + typed adapter
  + capabilities
  + tests
```

It should not mean a new Android, iOS or Windows application/viewer.

## One semantic truth across devices

Android is the first stable product implementation, not the final architectural boundary.

The strategic platform target is:

```text
                  DMC Rengine
              canonical C++20 core
                     |
        +------------+------------+
        |            |            |
     Android         iOS        Windows
        |            |            |
        +------ DMC Native Reader -+
```

The same resource should mean the same thing on every platform. Platform code may differ in file-picker integration, windows, gestures, thumbnails and navigation, but binary semantics must not be reimplemented independently per platform.

Long-term platform integration can include ordinary OS-native behaviors such as:

- Open with / Share / Files integration;
- file thumbnails;
- preview panes / quick preview;
- familiar image-like browsing for texture resources;
- visual model/scene previews directly from the user's normal file workflow.

These are strategic directions, not claims about the current v1 platform surface.

## Relationship to DMC Rengine

DMC Rengine is the canonical reverse/evidence and typed format authority wherever that authority exists.

Native Reader consumes that knowledge and turns it into a user-facing representation. It must not create a second incompatible interpretation simply to satisfy UI needs.

The intended direction is:

```text
DMC resource
    -> DMC Rengine canonical knowledge
        -> Native Reader generic representation
            -> device-native viewing experience
```

## Relationship to Pocket GDS

Pocket GDS and Native Reader solve different problems.

Native Reader:

```text
Open -> View -> Inspect -> Navigate -> Understand
```

Pocket GDS / authoring tooling:

```text
Browse archives -> Extract -> Replace -> Edit -> Repack -> Manage
```

The two products may share canonical readers, writers and resource contracts, and they may eventually hand resources to each other. They should not duplicate each other's primary product role.

## Evidence remains part of the product

Making a file easy to view must never mean inventing unknown semantics.

Native Reader should prefer:

- confirmed structure over attractive guesses;
- explicit unknowns over fabricated names;
- capability-gated visualization over format-name assumptions;
- fail-closed behavior over plausible but unsupported decoding.

Ease of use and evidence discipline are not competing goals. The product exists to make confirmed knowledge accessible.

## Primary success metric

The most useful measure of progress is not the number of buttons or editor features.

It is:

> **How many opaque DMC resource types have become directly understandable and viewable?**

Secondary measures include:

- time from tapping a file to seeing a useful representation;
- percentage of promoted formats available through the same generic architecture;
- consistency of the same resource across Android, iOS and Windows;
- reduction in format-specific knowledge required for routine inspection;
- absence of duplicated parser authority or platform-specific semantic forks.

## v1.0.0 as the base layer

The v1.0.0 baseline intentionally starts small:

```text
MOD -> visible
SCM -> visible
DDS -> visible
PTX -> visible
```

This four-format core is the base, not the final scope.

Future formats should be added one by one only when they can enter the same architecture cleanly and produce an honest, useful representation.

## Product doctrine

The guiding rules are:

1. **Make DMC resources feel like ordinary files.**
2. **Opening and understanding comes before editing.**
3. **The user should not need binary-format knowledge for ordinary viewing.**
4. **One canonical semantic truth should serve every platform.**
5. **New formats extend the module system, not the number of standalone viewers.**
6. **Native Reader remains a viewing/accessibility layer; authoring remains a separate concern.**
7. **Unknown semantics stay unknown.**
8. **The best Native Reader eventually feels boring: the user taps a DMC file and simply expects it to open.**
