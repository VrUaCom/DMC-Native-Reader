# DMC Native Reader — Roadmap

Last updated: **2026-09-26**.

DMC Native Reader is a **fast, read-only viewer and inspector** for resources from **Devil May Cry 3: Special Edition** in **Devil May Cry HD Collection**.

Its job is to open supported resources directly, visualize them quickly, expose useful structure, assemble supported model/resource sets, and let the user inspect/play supported animation data without first importing everything into Blender, 3ds Max or another DCC application.

It is **not** the authoring/repacking layer. Editing, canonical writing, archive reintegration and deeper Stage Ops workflows belong to DMC Rengine and the wider DMC Rengine toolchain.

---

# Status legend

- ✅ **DONE / RELEASED** — implemented and present in a public release.
- 🟦 **DONE IN SOURCE** — implemented in current source but not necessarily present in the older public platform release.
- 🟨 **NEXT** — current priority.
- ⬜ **PLANNED** — wanted after the current priority.
- 🔬 **EVIDENCE-GATED** — only promote when reverse-engineering evidence and regressions are strong enough.

---

# 1. Shared native core

## ✅ DONE / RELEASED

- one reusable native reader architecture instead of separate Android/Windows format implementations;
- target-scoped **C++23** product core;
- canonical DMC Rengine read-side authority where available;
- bounded reads and fail-closed routing;
- typed resource/session/inspection projections;
- shared rendering and resource-session logic;
- read-only product boundary;
- Android and Windows shells built around the same core direction.

## 🟨 NEXT

- keep Android and Windows consuming the same current core without platform-specific semantic forks;
- remove stale documentation or platform assumptions whenever a release moves ahead;
- keep format semantics in the native core, not in Android Java or Windows shell code.

---

# 2. Android

## ✅ DONE / RELEASED — v68 / 1.0.41

Current public Android release:

- **versionCode:** 68
- **versionName:** 1.0.41
- **ABI:** arm64-v8a
- **minSdk / targetSdk:** 26 / 36
- package: `com.dmcrengine.nativereader`

The current Android line already includes the broader Native Reader direction:

- MOD / SCM 3D viewing;
- DDS / PTX texture viewing;
- legacy `.tm2` logical-name support through the validated wrapped-DDS path;
- PAC/PNST resource assembly/browsing paths;
- MOT playback and motion-related inspection;
- composite model handling and attachments;
- cloth/coat behavior;
- shadows;
- collision/debug visualization;
- effect-bank inspection;
- TSC / CLT / EFM and related native module paths;
- room/stage backdrop presentation around models;
- improved native rendering performance;
- gesture controls and settings;
- PNG/inspection/export support where capability-authorized.

## ⬜ PLANNED

- continue performance and renderer cleanup;
- improve presentation of complex assembled resources;
- reduce UI friction while keeping the shell thin;
- add new resource semantics only when the native evidence is strong enough.

Android is no longer the only product target. New portable work should be designed so Windows can consume it without a second implementation.

---

# 3. Windows

## ✅ DONE / RELEASED — v1.0.0 Preview

The first public Windows release established the desktop product:

- portable Windows x64 application;
- no traditional installer required;
- read-only resource handling;
- MOD / SCM 3D viewing;
- DDS / PTX preview;
- interactive rotate / zoom / wireframe;
- structural Inspector;
- drag-and-drop opening;
- PTX child navigation;
- optional per-user Explorer/Open-With registration.

This release proved that DMC Native Reader can exist as a standalone Windows tool instead of requiring a DCC application.

## 🟦 DONE IN SOURCE AFTER THE FIRST PREVIEW

Windows-side work already exists beyond the original public preview, including parts of:

- richer overlays/inspection;
- hierarchy/bounds/skin/normals/UV presentation;
- PTX companion attachment;
- UV gallery;
- PNG export;
- fullscreen behavior;
- sibling navigation;
- improved aspect handling;
- perspective rendering;
- additional desktop shell work.

These source-side improvements must be validated against current `main` before being advertised as released Windows functionality.

## 🟨 NEXT — Windows parity with current Android/main

The next major task is to replace the old preview baseline with a Windows build based on the **current v68-era shared core**.

Required catch-up work:

- build Windows from current `main`, not the old preview snapshot;
- consume the current native module registry where Windows presentation support exists;
- validate MOD / SCM / DDS / PTX against the current core;
- validate PAC / PNST assembly;
- validate MOT playback and animation selection;
- validate composite model/attachment behavior;
- validate shadows, cloth, collision and effect inspection where portable support already exists;
- keep all Windows controls capability-driven;
- preserve read-only behavior;
- run a clean Windows build + smoke-test gate;
- publish a new Windows release only after the tested artifact matches the documented feature set.

**Goal:** Windows must stop trailing Android at the native-core level.

---

# 4. Windows desktop leapfrog

## ⬜ PLANNED

After parity, Windows should become the strongest **desktop inspection surface** while still using the same native semantic authority.

Wanted desktop-first features:

### Workspace / folder mode

- open a folder as a bounded browsing workspace;
- recursively discover supported resources;
- show unsupported files as unsupported instead of guessing;
- recent workspaces;
- fast switching between neighboring resources.

### Multi-session tabs

- several resources/workspaces open at once;
- independent model, texture and inspection sessions;
- deterministic release of native resources when a tab closes.

### Resource/dependency visualization

- visualize explicit WorkspaceGraph relationships;
- click resources/bindings to inspect them;
- never invent semantic links from filenames or UI order.

### Desktop inspection throughput

- richer side panels;
- searchable structural reports;
- fast keyboard/mouse navigation;
- batch inspection/report generation where it remains read-only;
- better large-screen layouts than Android.

This is the point where Windows may deliberately become more capable than Android in **desktop UX**, while the binary-format semantics remain shared.

---

# 5. PAC / model assembly / animation UX

## ✅ DONE / PARTIALLY AVAILABLE

The native code already contains PAC/PNST and MOT-related paths.

## 🟨 NEXT

Make the user-facing workflow much clearer:

- open a supported PAC as a **resource**, not merely as an archive dump;
- assemble the supported model/resource set automatically from canonical relationships;
- show the complete character/model where supported;
- expose available animations as a simple selectable list/strip;
- let the user click an animation and immediately play it on the assembled model;
- keep unsupported or unresolved slots visible without pretending they are decoded;
- keep file/resource inspection available alongside the visual result.

This is a core Native Reader goal: **quick visualization and inspection**, not archive-authoring.

---

# 6. Resource coverage

## ✅ CURRENT `main` NATIVE REGISTRY

Current `main` registers:

- MOD
- SCM
- DDS
- PTX
- EventTbl
- PAC
- MOT
- PNST
- SHW
- TSC
- CLT
- EFM
- motion scripts
- collision shape data
- collision index data
- effect banks

## 🔬 EVIDENCE-GATED

For every family:

- deepen semantics only when supported by executable/corpus/runtime evidence;
- preserve unknown fields as unknown;
- add malformed-input and regression coverage;
- avoid “support” claims based only on filename extensions;
- reuse canonical DMC Rengine authority instead of creating a Native Reader-only parser when possible.

Future format promotion is less important than making already-supported resources **useful and understandable**.

---

# 7. Viewer quality and performance

## ✅ DONE / ACTIVE

- native rendering;
- interactive camera;
- texture preview;
- model/scene presentation;
- Android rendering optimizations;
- Windows native desktop presentation.

## ⬜ PLANNED

- continue CPU renderer optimization;
- improve large-scene responsiveness;
- better progressive/preview rendering while interacting;
- optional hardware-backed presentation/rendering only if it does not fork semantic behavior;
- deterministic visual output for regression testing;
- cleaner handling of very large or malformed resources.

---

# 8. Public distribution

## ✅ DONE

- GitHub repository is public;
- GitHub Android release flow exists;
- Android **v68 / 1.0.41** is published;
- Windows **v1.0.0 Preview** is published on GitHub;
- Nexus Mods page for **DMC Native Reader for Windows** has been created and published;
- public descriptions, naming and platform boundaries have been cleaned up.

## 🟨 CURRENT EXTERNAL GATE

The Windows Nexus upload is currently waiting for Nexus' safety/manual review because the package contains a compiled Windows executable and optional PowerShell file-association helpers.

The Nexus page is public; the quarantined file should not be replaced merely to bypass the review process.

## ⬜ PLANNED

- keep release files reproducible and checksum-addressable;
- publish source/build instructions for compiled releases;
- improve Windows release packaging;
- consider Windows code signing when practical;
- keep GitHub and Nexus release descriptions synchronized;
- publish only capabilities present in the exact downloadable artifact.

---

# 9. Cross-platform direction

## ✅ CURRENT

- Android is the current feature-leading public build;
- Windows is the current desktop preview;
- both belong to one Native Reader product.

## ⬜ PLANNED

- bring Windows to current shared-core parity;
- let Windows lead in desktop inspection UX;
- keep portable improvements reusable by Android;
- treat any additional platform as experimental until an artifact is actually built, tested and published.

---

# 10. Explicit product boundary

Native Reader remains a **reader / viewer / inspector**.

It may:

- read resources;
- assemble supported read-side resource relationships;
- preview models/scenes/textures;
- play supported animation data;
- expose structural information;
- export read-side previews/reports where appropriate.

It must not become the canonical writer/repacker.

The following belong elsewhere:

- resource editing;
- canonical rebuilding;
- PAC/NBZ writing/repacking;
- game patch authoring;
- Stage Ops deep stage/resource operations;
- DMC Rengine decompilation/recompilation workflows.

Those systems may share the same canonical evidence and libraries, but they are separate products/workspaces.

---

# Immediate priority order

1. 🟨 **Update Windows from the old v1.0.0 Preview baseline to current v68-era shared-core parity.**
2. 🟨 **Make PAC → assembled model → animation selection/playback a first-class Windows workflow.**
3. 🟨 **Validate and publish the next Windows artifact with an exact feature list.**
4. ⬜ **Add desktop workspace/folder mode and multi-session tabs.**
5. ⬜ **Add dependency/resource graph inspection for explicit native relationships.**
6. 🔬 **Continue evidence-gated semantic depth for existing formats.**
7. ⬜ **Keep improving performance and cross-platform presentation without duplicating binary semantics.**

---

# Naming policy

Use:

- **Devil May Cry HD Collection** — collection title.
- **Devil May Cry 3: Special Edition** — game title.
- **DMC3** — shorthand after the full name has been established.

Do not use “Devil May Cry 3 HD Collection” as a product title.
