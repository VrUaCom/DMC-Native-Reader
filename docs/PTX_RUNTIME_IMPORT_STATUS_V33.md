# PTX Runtime Import Status v33

Repository: `VrUaCom/DMC-Native-Reader`
External evidence source: read-only `VrUaCom/dmc-rengine-cpp@50d070e158e484937238d9cb02b2bc6affb2f502`

## Current state
- Architecture contract authored.
- Execution ТЗ authored.
- Review template authored.
- Rengine mutation forbidden.
- PTX reverse-code copy/import explicitly authorized only for the bounded `50d070e...` PTX runtime slice.
- Architecture Review #50 completed: **GO_WITH_CORRECTIONS**.
- Phase #51 Reader-only C++23 implementation is in progress.
- Reader-owned `PtxRuntimeCompat` header/source added.
- One bounded `ptx_runtime_compat_test.cpp` regression added.
- RuntimeCompat source and regression are registered in the canonical `DMCNativeReader::Core` / CTest graph.
- Static parity review corrected an important model boundary: initializer storage extent is `0xCB50`, while placement/configure operate on the confirmed `0xCB48` pool prefix; the final 8 bytes remain clear/preserved tail only.
- Existing serialized PTX route remains unchanged; there is still no inferred `TextureSet::Slot -> runtime 0x50 record` mapping.
- Hosted heavy CI remains intentionally deferred until the complete Phase-2 checkpoint.

## Required sequence
1. PTX Runtime Architecture Review #50 — **DONE / GO_WITH_CORRECTIONS**.
2. C++23 Reader-only import/integration #51 — **IN PROGRESS**.
3. PTX Runtime Review Gate #52 — pending.
4. Global C++23 Phase-2 Review Gate #41 — pending after #52 GO and final exact-head evidence.

## Approved first-slice implementation
- runtime config words corresponding to confirmed graphics-config +0x4C/+0x4E reads;
- lazy internal pool storage `0xCB48 prefix + 8-byte tail`;
- minimum manager image/reset semantics required by configure;
- pool initializer `0x140331910` behavior;
- reservation/configure `0x140331D90` behavior;
- placement `0x140331520` behavior;
- typed C++23 result/error surface using `std::expected`;
- compact Reader regression instead of importing reverse harness/evidence tables.

## Explicitly deferred
- serialized `TextureSet::Slot -> runtime record` materialization mapping;
- palette helper `0x140331BD0`;
- finalizer `0x140331A80`;
- parser/backend runtime mapping `0x1403365B0`;
- graphics-config full type/live values;
- actual caller/lifecycle/teardown ordering;
- full manager acquire/release/destroy lifecycle.

## Non-negotiable invariants
- one serialized PTX parser authority;
- one user-facing PTX module;
- lazy runtime layer;
- no guessed palette/finalizer/cleanup;
- no Rengine writes;
- no duplicate runtime/harness;
- APK <= 4 MiB pre-gate;
- installed StorageStats <= 4 MiB downstream;
- no hosted CI per intermediate migration commit.

## Next checkpoint
Complete #51 static/source review, verify no serialized PTX or Spider semantic authority was altered, update #52 review inputs, then run the dedicated PTX review gate before the global #41 gate.
