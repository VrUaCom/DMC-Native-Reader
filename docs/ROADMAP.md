# DMC Native Reader — Public Roadmap

## Phase 1 — Native Reader v1 architecture

**Status: achieved**

- explicit modular reader architecture;
- no central family decoder;
- no wildcard structural fallback;
- fail-closed unknown-family behavior;
- 71 known-family contracts;
- production Android `Open with` path;
- core popular DMC3 modding formats promoted to real readers;
- reusable `InspectionDocument`, `RenderScene`, `ImagePreview` and `ChildResource` contracts;
- reproducible host + Android CI gates.

## Phase 2 — Device / corpus debug

**Status: v1 acceptance achieved; regression testing continues**

Accepted real-device flows now include:

1. MOD real-model rendering, hierarchy overlay, skin/weights and texture-state inspection;
2. SCM scene rendering, hierarchy overlay and texture/GS-state inspection;
3. PTX child-resource gallery with real DDS thumbnails;
4. PTX -> DDS -> full image preview -> parent-session navigation;
5. standalone DDS preview;
6. Samsung My Files / Android `Open with` routing used in the tested resource flows;
7. malformed/truncated DDS/PTX hardening covered by dedicated host regression.

Remaining device/corpus work is now regression coverage rather than a blocker for the v1 architecture milestone.

## Phase 2.5 — v1.0 release candidate

**Status: active**

- freeze feature scope;
- build `1.0.0-rc1` / versionCode 18;
- run self-contained RC smoke gate;
- keep release APK unsigned until production signing authority exists;
- record debug and unsigned release APK SHA-256 evidence;
- final Samsung RC smoke pass;
- provision external production signing authority before stable `1.0.0` distribution.

No new format is promoted merely to satisfy the release number.

## Phase 3 — Promote the next evidence-ready readers

After the v1 freeze, a family is promoted only when reverse evidence is strong enough to support a bounded product parser.

Current candidates:

- SHW — strong real-payload + executable evidence; guarded product parser still needs closure;
- EFM — mesh-bearing family evidence exists; exact format-specific bindings remain open;
- SO — significant research exists; product parser contract still needs promotion;
- MRP — family identity is confirmed, but exact binary schema remains open.

## Phase 4 — Deeper semantic interpretation

After v1 stability:

- richer SCM scene/material semantics;
- deeper MOD skeletal/skin semantics;
- resolved MOD texture-companion linkage when evidence is sufficient;
- animation/control families;
- stage/event semantics;
- deeper archive-to-resource provenance.

## Phase 5 — Authoring / write path

Native Reader v1 is intentionally read-only.

Editing/repacking belongs to a later milestone and must reuse canonical writer contracts from DMC Rengine rather than adding ad-hoc Android-only writers.

The order remains deliberate:

```text
recognize correctly
  -> parse safely
      -> validate on real corpus
          -> understand semantics
              -> only then author/write
```
