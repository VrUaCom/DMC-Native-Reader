# PTX Runtime Import Status v33

Repository: `VrUaCom/DMC-Native-Reader`
External evidence source: read-only `VrUaCom/dmc-rengine-cpp@50d070e158e484937238d9cb02b2bc6affb2f502`

## Current state
- Architecture contract authored.
- Execution ТЗ authored.
- Review template authored.
- Rengine mutation forbidden.
- PTX reverse-code copy/import explicitly authorized only for the bounded `50d070e...` PTX runtime slice.
- Implementation not yet accepted until dedicated architecture review issues GO.

## Required sequence
1. PTX Runtime Architecture Review.
2. C++23 Reader-only import/integration.
3. PTX Runtime Review Gate.
4. Global C++23 Phase-2 Review Gate #41.

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
