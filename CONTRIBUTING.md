# Contributing to DMC Native Reader

DMC Native Reader currently has an accepted Android v1 baseline on `main` (versionName `1.0`, versionCode `24`) and evidence-gated development candidates. The production registry is intentionally limited to **MOD / SCM / DDS / PTX**.

Contributions are welcome when they improve correctness, safety, real-file compatibility, inspection/rendering quality, Android integration, evidence quality or a bounded format promotion.

## Ground rule: do not invent format semantics

This project is evidence-aware. Distinguish between:

- filename/extension recognition;
- content-confirmed identity;
- structural parsing;
- semantic interpretation;
- presentation/rendering behavior;
- original-game/runtime behavior.

Generated names, plausible guesses or UI convenience are not reverse-engineering evidence. If a field or relationship is unknown, keep it unknown/preserved until evidence supports promotion.

## Architecture rules

A contribution must preserve the current direction:

```text
DMC Rengine / native canonical authority
  -> NativeModuleRegistry
  -> typed Architecture v2 projection
  -> portable DMCNativeReader::Core
  -> thin platform shell
```

Please do not:

- add Java/Kotlin DMC binary parsers;
- add renderer-owned format parsers;
- reintroduce wildcard family decoding;
- restore the old recognition-only production catalog;
- duplicate MOD/SCM/DDS/PTX offsets or codecs already owned by DMC Rengine/native modules;
- infer spatial hierarchy from mesh vertices or fabricate missing transforms;
- parse diagnostic strings to make application/UI decisions;
- add Android-only writers/repackers to the read-only product.

## High-value contributions

- reproducible failures using legally obtained user-owned DMC3 HD resources;
- bounds/memory-safety hardening;
- MOD/SCM geometry, hierarchy, UV, skin or texture-binding regressions backed by canonical evidence;
- DDS/PTX decode/framing/gallery/companion fixes;
- capability-driven UI/inspection improvements that reuse typed native contracts;
- Android routing and real-device diagnostics;
- documentation corrections tied to actual code/evidence;
- a new family promotion backed by canonical DMC Rengine readiness and dedicated tests.

## New format promotion

Historical branches contain more resource families than production `main`. That does **not** make those families supported.

A new family should document:

- family identity and evidence;
- canonical parser/read-side authority;
- bounded byte/layout invariants;
- typed projection contract;
- unresolved fields/semantics;
- malformed-input behavior;
- regression/corpus evidence;
- UI capabilities actually justified by that evidence.

Prefer consuming the corresponding `VrUaCom/dmc-rengine-cpp` authority rather than rediscovering the format in Native Reader.

## Testing and acceptance

Before opening/promoting a PR:

1. run the relevant portable/native regressions;
2. keep unknown/unpromoted input fail-closed;
3. verify a clean Android build when Android code/package behavior changes;
4. update APK identity/module/JNI/signing checks when the interface changes;
5. add a regression for the bug/feature rather than relying on visual inspection alone;
6. for device-visible behavior, keep the PR draft until the required real-device/corpus acceptance succeeds;
7. update repository documentation in the same promotion slice when accepted state changes.

A hosted CI failure that executes no steps is infrastructure evidence, not proof that the source is broken; likewise it is not a green gate. Report exactly what did and did not execute.

## Bug reports

Useful reports include:

- Native Reader version/versionCode and commit when self-built;
- device/Android version and file-opening path;
- resource extension/family and size/hash when safe to share;
- exact visible result;
- whether the failure is routing, parse, inspection, render, hierarchy, texture/companion, gallery/navigation or memory-safety;
- screenshots/logs or a minimal legally shareable fixture when available.

Do not upload copyrighted game archives, proprietary executables or redistribution-restricted assets unless you have the right to do so.

## AI-assisted contributions

AI-assisted development is allowed, but generated code/explanations do not count as reverse-engineering evidence. Contributors remain responsible for correctness, provenance, testing and licensing.

## Legal boundary

DMC Native Reader is an independent fan-made interoperability/modding project and is not affiliated with Capcom. Do not submit proprietary Capcom source code, leaked materials, game binaries or assets you cannot legally redistribute.
