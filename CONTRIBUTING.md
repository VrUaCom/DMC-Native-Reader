# Contributing to DMC Native Reader

DMC Native Reader v1 is a stable, read-only Android reader with an intentionally narrow production registry: **MOD, SCM, DDS and PTX**.

Contributions are welcome when they preserve the evidence-aware Architecture v2 boundary.

## Ground rule: do not invent format semantics

A parser change must keep these claims separate:

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
- Android `Open with` / SAF / OEM file-manager routing fixes;
- native regression tests;
- documentation corrections backed by code, corpus or executable evidence;
- new format promotion work that first establishes a clean canonical authority.

## Architecture requirements

Before opening a pull request, verify that your change:

1. stays inside the `NativeModuleRegistry` / Architecture v2 contract;
2. does not reintroduce a wildcard or central family decoder;
3. keeps unknown/unpromoted families fail-closed;
4. does not add Java-owned MOD/SCM binary parsing;
5. does not let a non-renderable session reuse stale geometry;
6. keeps source file access read-only unless a future authoring milestone explicitly changes that product contract;
7. updates native/host tests where applicable;
8. still builds the Android application;
9. does not add signing keys, passwords, tokens, proprietary game assets or executable binaries to Git history.

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
- why the new path cannot misroute unknown inputs.

Where a canonical `dmc-rengine-cpp` reader exists, product code should adapt that authority rather than fork a second Android-only parser.

## Bug reports

Useful reports include:

- Native Reader version;
- Android device and Android version;
- file extension / detected family;
- exact visible result or error;
- whether the failure is routing, recognition, parsing or rendering;
- file size and, when safe to share, a hash;
- whether the same file is accepted by another known DMC Rengine tool.

Do **not** upload copyrighted game archives, proprietary executable files, leaked source, or redistribution-restricted assets unless you have the legal right to redistribute them.

## Pull requests

Keep pull requests focused. Prefer one format/bug/architecture change per PR.

A PR should include:

- a concise problem statement;
- evidence/source authority for semantic claims;
- tests or an explanation of why a test is not possible;
- device/corpus acceptance notes when correctness is visible only on real files;
- no unrelated vendored-core changes.

CI is necessary but not always sufficient. Rendering and OEM file-routing changes may require a real-device acceptance pass before merge.

## AI-assisted contributions

AI-assisted development is allowed. Contributors remain responsible for correctness, provenance, security, testing and licensing of submitted code.

AI output does not count as reverse-engineering evidence unless independently supported by code, corpus or executable evidence.

## Security

Do not put exploit details, private signing material or sensitive device data in a public issue. Follow [`SECURITY.md`](SECURITY.md).

## Legal boundary

This repository is an independent fan-made interoperability/modding project and is not affiliated with Capcom.

Do not submit proprietary Capcom source code, leaked material, game binaries or assets that cannot be legally redistributed.
