# DMC Native Reader v1.x — Promotion and Release Gates

Last updated: 2026-09-10.

This document defines the minimum evidence required to promote a change into the accepted Native Reader v1 line. The current accepted `main` baseline is versionName `1.0`, versionCode `24`.

## Architecture gates

- DMC resource bytes are parsed in the canonical/native C++20 path, never in Android Java/Kotlin.
- `NativeModuleRegistry` is the family-routing authority.
- `InspectionDocument` is the typed inspection presentation authority.
- `RenderScene` is the geometry/hierarchy/material projection authority.
- `ImagePreview` and `ChildResource` are generic image/nested-resource contracts.
- `resource_session` owns portable session state rather than JNI.
- UI availability and application state come from typed capabilities / Black Widow state, not diagnostic-string parsing.
- Renderers consume prepared data; they do not own MOD/SCM/DDS/PTX parsers.
- A new family must not reintroduce wildcard or recognition-only production routing.

## Evidence gates

- Unknown fields remain unknown/preserved until promoted by evidence.
- MOD spatial hierarchy is enabled only when canonical spatial authority is valid.
- Known structural parent relationships must not be converted into fabricated 3D transforms.
- MOD/SCM texture slots and legacy render state come from typed canonical fields.
- Texture companion application must validate required slots and fail closed on incomplete/conflicting bindings.
- A failed replacement companion must preserve the previously valid attached state.
- Product documentation must identify candidate/experimental capability separately from accepted `main` capability.

## Reader/device acceptance

For a change affecting an existing supported family, test the relevant real-resource path. The accepted v24 surface includes:

- MOD: open, render, rotate, zoom, wireframe, inspection, hierarchy when valid, skin/weights, texture state and PTX companion application;
- SCM: open, render, rotate, zoom, wireframe, scene hierarchy/transforms, inspection, texture state and PTX companion application;
- DDS: bounded parse/decode and image preview;
- PTX: bundle/gallery, DDS child preview, parent navigation and model companion use;
- malformed/unsupported input: fail closed without stale state, crash or unbounded allocation.

Visible interaction features that depend on real device/UI behavior remain draft until a physical-device/corpus pass succeeds. This applies to the current v26 UV gallery and long-press inspection candidate.

## Android identity and packaging

Current accepted production identity:

- package: `com.dmcrengine.nativereader`;
- application label: `DMC Native Reader`;
- versionName: `1.0`;
- accepted versionCode: `24`;
- ABI: `arm64-v8a`;
- minSdk: `26`;
- targetSdk: `36`.

Every promoted APK must verify package/version/ABI, manifest routes, ZIP integrity, expected native module markers and the exact declared JNI export boundary for that revision.

## Signing boundary

- The repository test JKS is development/test-only and is not a production trust authority.
- Normal Gradle release output must remain unsigned unless protected production signing material is explicitly injected by the release pipeline.
- Production keys/passwords must not be committed to repository history.
- Any official update APK must preserve the intended production signing continuity and record its certificate digest with release evidence.

## Native and APK regression gates

A promotion candidate should run the relevant portable/native regressions plus Android verification. At minimum, the maintained gates should cover:

- module registry/fail-closed behavior;
- canonical MOD/SCM adapter and model pipeline behavior;
- render-scene/hierarchy projection;
- DDS/PTX texture path;
- model texture binding/companion behavior;
- Black Widow typed UI state;
- feature-specific regressions (for example UV gallery or focused session inspection);
- clean Android NDK/Gradle build;
- APK identity, signer appropriate to build type, ZIP and JNI/module checks;
- diff/provenance audit preventing accidental parser duplication or retired decoder reintroduction.

## Hosted CI classification

A GitHub Actions job that fails before executing any steps and provides no build/test log is an infrastructure/execution-path failure. It is neither a green gate nor evidence of a source-code regression. Record it explicitly and rely only on evidence that actually executed.

When hosted jobs do execute, failures must be investigated before promotion unless an evidence-based reason demonstrates that the job itself is invalid.

## Merge policy

- Do not create a new branch when the work belongs to an existing active technical line.
- Keep visible feature PRs draft until their specific device/corpus acceptance is complete.
- Do not merge a candidate merely because its host tests pass when the unverified behavior is device-visible.
- After acceptance, update `README.md`, `docs/STATUS.md`, `docs/ROADMAP.md` and `CHANGELOG.md` in the same promotion slice so repository documentation never lags the accepted product state.
