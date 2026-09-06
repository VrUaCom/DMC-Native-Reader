# Contributing to DMC Native Reader

DMC Native Reader is currently in the **v1 debug / corpus-validation phase**.

Contributions are welcome, especially when they improve real-file compatibility, Android routing, parser diagnostics, evidence quality or format-specific tests.

## Ground rule: do not invent format semantics

This project is evidence-aware.

A parser change should distinguish between:

- extension/filename recognition;
- content-confirmed identity;
- structural parsing;
- semantic interpretation;
- rendering behavior;
- original-game/runtime behavior.

Do not promote a guess into a field name, offset meaning, geometry layout or runtime claim without evidence.

If a field is unknown, preserve it as unknown.

## High-value contributions

- reproducible bugs using legally obtained user-owned DMC3 HD resources;
- minimal failing fixtures or byte ranges that demonstrate a parser issue;
- Android `Open with` / SAF / Samsung My Files routing diagnostics;
- bounds-checking and malformed-input hardening;
- format-specific unit/regression tests;
- documentation corrections supported by code/corpus/executable evidence;
- new family modules backed by sufficiently strong reverse evidence;
- UI improvements that preserve parser/evidence distinctions.

## Before opening a pull request

Please make sure that:

1. the change stays inside the modular `NativeModuleRegistry` architecture;
2. no wildcard or central family decoder is reintroduced;
3. unknown families still fail closed;
4. recognition-only families do not fabricate decoded semantics;
5. non-renderable sessions cannot display stale geometry;
6. file access remains read-only unless a future authoring milestone explicitly changes that contract;
7. host/native tests are updated where applicable;
8. Android build still completes.

## Format work

For a new or upgraded format module, document:

- family name;
- identity evidence;
- parser maturity (`recognition`, `partial`, `structural`, `mesh/renderable`, etc.);
- validated byte boundaries;
- unresolved fields/semantics;
- test/corpus evidence used;
- whether the implementation comes from or must be synchronized with `VrUaCom/dmc-rengine-cpp`.

## Bug reports

Useful bug reports include:

- Native Reader version;
- Android device / Android version;
- file extension and detected family;
- exact visible error/result;
- whether the failure is routing, recognition, parsing or rendering;
- file size and, when safe to share, a hash;
- whether the same file is accepted by another known DMC Rengine tool.

Do **not** upload copyrighted game archives or proprietary executable files unless you have the right to redistribute them.

## AI-assisted contributions

AI-assisted development is allowed. The project itself uses AI-assisted implementation/review workflows.

However, generated code or explanations do not count as reverse-engineering evidence by themselves. Contributors remain responsible for correctness, provenance, testing and licensing of submitted code.

## Legal boundary

This repository is an independent fan-made interoperability/modding project and is not affiliated with Capcom.

Do not submit proprietary Capcom source code, leaked materials, game binaries or redistribution-restricted assets.
