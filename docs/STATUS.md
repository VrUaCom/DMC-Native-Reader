# DMC Native Reader — Status

Last updated: 2026-09-15.

## Accepted baseline (`main`)

- current `main`: `561385e24e7246da11631e594ad5a86ca619fa74`
- device-confirmed product baseline: v26 line accepted through PR #32
- package: `com.dmcrengine.nativereader`
- ABI: `arm64-v8a`
- minSdk / targetSdk: `26 / 36`
- later `main` maintenance added Android/Windows release-publishing workflow; it did not constitute a new device-accepted reader build

The v26 line was physically tested on Samsung on 2026-09-10 and explicitly approved for promotion to `main`.

## Active candidate — v33

- branch: `feature/png-export-multi-mod-v27`
- PR: #33, draft
- versionName: `1.0.6`
- versionCode: `33`
- production modules: **MOD / SCM / DDS / PTX / EventTbl**
- canonical DMC Rengine ReaderCore pin: `caf445226c7d61841292384a10e93e4f58ae29f9`

PR #33 must not be merged until an exact-head build actually executes the full host regressions, passes `tools/verify_device_apk.py`, produces the canonical single-DSO APK, and passes Samsung device acceptance.

## v33 architecture

```text
resource bytes
  -> bounded probe
  -> NativeModuleRegistry
  -> Spider Crusader
      -> MOD / SCM / DDS / PTX / EventTbl native modules
  -> portable DMCNativeReader::Core
      -> resource session
      -> composite model state
      -> composite builder
      -> Rengine-backed default-joint resolver
      -> composite placement
      -> scene projection
      -> texture companion binding
      -> Black Widow typed capability state
      -> renderer / inspection / UV / PNG export
  -> thin JNI / Android shell
```

Android remains transport/presentation only. It must not parse DMC layouts, decide model attachment semantics or implement texture-binding policy.

## Multi-MOD / body-hair placement

The old v33 compositor flattened all MOD parts in source coordinates. That behavior explains the observed body/hair problem: separate parts were rendered around their own model-space origin instead of receiving the runtime-style host-joint root transform.

The current candidate now has a modular attachment path:

1. the MOD module publishes canonical `Header::default_joint_index()` as typed `RenderScene::default_attachment_selector`;
2. `composite_builder` treats the already-open/base MOD as an explicit primary host;
3. appended MOD parts resolve their selector through `dmc::rengine::formats::mod::attachment`;
4. valid host spatial authority + in-range selector yields a host joint matrix;
5. `composite_placement` applies that matrix only to the derived flattened render/hierarchy projection;
6. the source-local child `RenderScene` remains unchanged and can be reset without reparsing.

The resolver does not infer a different host from filenames, visual proximity, `runtime_metadata_u32` or arbitrary candidate scanning. Missing/out-of-range selectors and hosts without canonical spatial authority fail closed to source coordinates.

## Modular split completed in this pass

- `include/dmcresource/composite_model.h` — source-part + placement state
- `include/dmcresource/composite_builder.h` / `modules/composite_builder.cpp` — product composition policy
- `include/dmcresource/mod_attachment_resolver.h` / `modules/mod_attachment_resolver.cpp` — Rengine-backed selector resolution
- `include/dmcresource/composite_placement.h` / `modules/composite_placement.cpp` — derived placement projection
- `spider/session_compose_actions.cpp` — compose action
- `spider/session_texture_actions.cpp` — texture actions
- `spider/model_placement_actions.cpp` — explicit placement/reset actions

The old monolithic `spider/session_actions.cpp` is no longer compiled.

## PTX transaction safety

Per-part PTX replacement is now fully staged. Texture storage and triangle-slot projection are copied into temporary state; decode, range validation, slot validation and compaction complete before the live session is replaced. A failed replacement therefore preserves the previous valid texture bank and render projection.

New regression: `ptx_transaction_test`.

## Current regression set added/strengthened

Important v33-specific regressions now include:

- `composite_builder_test` — automatic primary-host/default-joint placement and fail-closed fallback;
- `composite_placement_test` — explicit host-joint projection, row-vector transform order and reset;
- `ptx_transaction_test` — failed per-part PTX replacement preserves live state;
- `composite_mod_scene_test` — low-copy composition and shared PTX bank;
- `scm_authority_test` — canonical SCM world-space authority;
- existing module/Spider/texture/PNG/render/inspection regressions.

## APK/runtime contract

v33 requires:

- exactly one packaged native DSO: `lib/arm64-v8a/libdmcviewer.so`;
- static `DMCNativeReader::Core` + `DMCRengine::ReaderCore`;
- `extractNativeLibs=false`;
- native DSO stored uncompressed and 16 KiB ZIP aligned;
- every ELF `PT_LOAD` alignment >= 16 KiB;
- exact Java `NativeBridge` ↔ JNI export parity;
- no recovery `dmcshim` / `dmccore00` path;
- direct Android Bitmap transport;
- APK <= 8 MiB, DSO <= 4 MiB, Dex <= 1 MiB;
- exact Rengine gitlink and checkout at `caf445226c7d61841292384a10e93e4f58ae29f9`.

`tools/verify_device_apk.py` has been updated for the split compose/texture action modules and the new Rengine pin.

## CI state

GitHub-hosted jobs on this repository continue to be observed failing before runner assignment. The characteristic failure is `runner_id=0` with `steps=[]`; checkout, CMake, Gradle and tests never start. Such a run is not evidence that the current source fails to compile, but it is also not acceptance evidence.

Until a real exact-head build executes, v33 remains **not release-approved**.

## Production boundary

DMC Native Reader is read-only. Editing/repacking belongs to DMC Rengine. HITS, TXT/index, DCA, LIG/LIG2, PAC/PNST, NBZ, MOT, EFM/MRP/SHW and other researched formats remain outside the production Native Reader registry until individually promoted with native authority and regression coverage.
