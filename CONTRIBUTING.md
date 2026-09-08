# Contributing to DMC Native Reader

DMC Native Reader v1.0.0 is the stable read-only resource-viewing product of the DMC Rengine ecosystem. Its production registry is intentionally narrow: **MOD, SCM, DDS and PTX**.

Contributions are welcome when they preserve the evidence-aware Architecture v2 boundary, DMC Rengine authority model, and the project's source-available license.

## Product role

Native Reader is the viewability/accessibility layer:

```text
Open -> Recognize -> Inspect -> Visualize -> Navigate -> Understand
```

Editing, archive management, replacement and repacking belong to DMC Rengine-backed authoring/resource-management tools such as Pocket GDS rather than to a second independent Reader stack.

## Ground rule: do not invent format semantics

A parser or representation change must keep these claims separate:

- extension / filename recognition;
- content-confirmed identity;
- structural parsing;
- semantic interpretation;
- rendering behavior;
- original-game/runtime behavior.

If a field or relationship is not supported strongly enough, keep it unknown or preserved undecoded. Generated explanations or naming guesses are not reverse-engineering evidence by themselves.

## High-value contributions

- reproducible bugs using legally obtained user-owned DMC3 HD resources;
- bounds-checking and malformed-input hardening;
- MOD/SCM rendering, hierarchy, transform, skin or texture-state regressions;
- DDS/PTX validation improvements;
- Android file-routing/OEM fixes;
- future iOS, Windows or Web presentation work that reuses the same C++20 semantic core;
- native regression tests;
- documentation corrections backed by code, corpus or executable evidence;
- new format promotion work that first establishes a clean canonical authority in DMC Rengine when that capability belongs there.

## Architecture requirements

Before opening a pull request, verify that your change:

1. stays inside the `NativeModuleRegistry` / Architecture v2 contract;
2. does not reintroduce a wildcard or central family decoder;
3. keeps unknown/unpromoted families fail-closed;
4. does not add platform-UI-owned binary parsing for resource formats;
5. does not let a non-renderable session reuse stale geometry;
6. keeps source-file access read-only unless the product contract explicitly changes in a future milestone;
7. promotes reusable engine-level semantics in DMC Rengine instead of privately forking them in Native Reader;
8. updates native/host tests where applicable;
9. still builds the affected product shell;
10. does not add signing keys, passwords, tokens, proprietary game assets or executable binaries to Git history.

## New format promotion

A new family should not enter `main` merely because an older branch contains working-looking code.

Document at minimum:

- family name and identity evidence;
- canonical parser/source authority;
- parser maturity (`recognition`, `partial`, `structural`, `renderable`, etc.);
- validated byte boundaries;
- unresolved fields/semantics;
- tests and corpus evidence;
- UI capabilities exposed by the module;
- the natural user-facing representation (model, scene, image, gallery, hierarchy, animation, graph, volume, etc.);
- why the new path cannot misroute unknown inputs.

Where a canonical DMC Rengine reader/capability exists, Native Reader should adapt that authority rather than fork a second platform-only implementation.

## Pull requests

Keep pull requests focused. Prefer one format, bug or architecture change per PR.

A PR should include:

- a concise problem statement;
- evidence/source authority for semantic claims;
- tests or an explanation of why a test is not possible;
- device/corpus acceptance notes when correctness is visible only on real files;
- no unrelated vendored-core changes.

CI is necessary but not always sufficient. Rendering and OS file-routing changes may require a real-device acceptance pass before merge.

## Contribution licensing

DMC Native Reader is distributed under the **DMC Native Reader Personal Non-Commercial License 1.0**.

By intentionally submitting a contribution for inclusion in DMC Native Reader, you represent that you have the right to submit it and agree that the contribution may be distributed under the project `LICENSE`, including the **Capcom Special Grant** contained in that license, unless a separate written agreement applies.

Do not submit code under incompatible terms or code copied from proprietary/leaked sources.

Vendored third-party components remain governed by their own licenses. See `THIRD_PARTY_NOTICES.md`.

## Bug reports

Useful reports include:

- Native Reader version;
- platform/device and OS version;
- file extension / detected family;
- exact visible result or error;
- whether the failure is routing, recognition, parsing or rendering;
- file size and, when safe to share, a hash;
- whether the same file is accepted by another known DMC Rengine tool.

Do **not** upload copyrighted game archives, proprietary executable files, leaked source, or redistribution-restricted assets unless you have the legal right to redistribute them.

## AI-assisted contributions

AI-assisted development is allowed. Contributors remain responsible for correctness, provenance, security, testing and licensing of submitted code.

AI output does not count as reverse-engineering evidence unless independently supported by code, corpus or executable evidence.

## Security

Do not put exploit details, private signing material or sensitive device data in a public issue. Follow [`SECURITY.md`](SECURITY.md).

## Legal boundary

This repository is an independent fan-made interoperability/reverse-engineering/modding project and is not affiliated with Capcom.

Do not submit proprietary Capcom source code, leaked material, game binaries or assets that cannot be legally redistributed.
