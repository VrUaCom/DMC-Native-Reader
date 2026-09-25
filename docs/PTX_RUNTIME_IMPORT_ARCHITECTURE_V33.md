# DMC Native Reader — PTX Runtime Import Architecture v33

Status: architecture contract for review/implementation inside `VrUaCom/DMC-Native-Reader` only.
Source evidence snapshot: read-only `VrUaCom/dmc-rengine-cpp` commit `50d070e158e484937238d9cb02b2bc6affb2f502`.

## 1. Repository boundary

`VrUaCom/dmc-rengine-cpp` is absolute READ-ONLY for this work. Native Reader may inspect/read source and evidence from commit `50d070e...`, but must not write, commit, branch, comment, create issues/PRs, change CMake, tests, docs, APIs, targets or any other resource in Rengine.

VrUaCom has explicitly authorized one narrow exception to the normal no-copy rule: PTX runtime behavior recovered in `50d070e...` may be copied into **DMC Native Reader** and adapted/ported to ISO C++23. This authorization applies only to the bounded PTX runtime slice required for Native Reader and does not grant general permission to copy other Rengine subsystems.

## 2. One PTX module, two internal layers

Native Reader keeps one user/product PTX module with two internal responsibilities:

```text
PTX Module
├── Serialized PTX layer
│   ├── TextureSlotFramingParser / existing framing authority
│   ├── TextureSet
│   ├── DDS BC decoder
│   └── Preview / Gallery / PNG / UV
└── Runtime PTX compatibility layer [lazy]
    ├── Manager state
    ├── Pool state
    ├── Placement
    ├── Reservation
    ├── Cache-key lifecycle
    ├── Palette stage [extension point only until evidence imported/closed]
    └── Finalizer/cleanup [extension point only until evidence imported/closed]
```

Do not create a second serialized PTX parser. Runtime state answers "what DMC3 does with PTX in memory"; existing framing/TextureSet answers "what bytes are serialized in the PTX file".

## 3. Copy/import rule

Only copy the minimum evidence-backed algorithms/data structures required from the read-only `50d070e...` snapshot. During import:
- port/adapt to Native Reader C++23 conventions;
- remove reverse-tool-only harness infrastructure from production sources;
- preserve provenance comments linking the imported behavior to `50d070e...` and relevant EXE evidence/function identifiers;
- do not preserve Rengine namespace/ownership in a way that pretends the copied code is still linked authority;
- do not introduce raw EXE addresses into public Native Reader API;
- keep unknown cleanup/palette behavior unresolved rather than guessed;
- avoid byte-for-byte duplicate helper implementations when an existing Native Reader helper already provides the same non-semantic mechanism.

Once copied, the Native Reader copy is a local compatibility implementation maintained only in `DMC-Native-Reader`. Rengine remains untouched.

## 4. C++23 runtime wrapper

Target Reader-side surface:
- `include/dmcresource/ptx_runtime_compat.h`
- `modules/ptx_runtime_compat.cpp`

The public/product API should expose typed operations at the level of:
- initialize runtime state;
- configure reservation;
- validate/place a record;
- reset manager/cache keys according to confirmed behavior;
- inspect runtime state;
- release/reset only behaviors whose order/semantics are proven.

Prefer typed `std::expected<T, Error>` where a runtime operation has meaningful fail-closed states. Avoid sentinel/error-string APIs when a compact enum is sufficient.

Public wrapper must not expose reverse-only raw image structures, EXE addresses or guessed lifecycle semantics.

## 5. Lazy integration

The ordinary PTX read/preview path must remain lightweight:

`frame PTX -> TextureSet/DDS decode -> preview/gallery`

Runtime compatibility is instantiated only when runtime inspection/validation is requested. Opening a PTX for PNG/preview must not allocate/simulate the runtime pool/manager unnecessarily.

## 6. First implementation slice

Import only behavior already recovered/validated in `50d070e...`:
- PTX pool initialization;
- pool reservation configuration;
- placement behavior;
- manager key/count reset behavior associated with reservation changes;
- preservation of pool records/occupancy/payload where confirmed;
- confirmed acquire/release/reset semantics that are present in the copied evidence slice.

Do not implement guessed palette/finalizer/cleanup order. Create explicit extension points/status for unresolved stages.

## 7. Evidence/testing split

Do not copy the entire reverse harness into Native Reader. The source evidence snapshot reports 134 EXE comparison cases; Native Reader needs compact integration regressions proving the imported wrapper preserves confirmed behavior at product boundaries.

Minimum Native Reader regression matrix:
- initialization;
- automatic/default placement cases represented by imported behavior;
- explicit placement;
- no-space/failure path;
- reservation resize;
- manager keys/count reset;
- payload preserved when required;
- occupancy/records preserved when required;
- source serialized PTX bytes remain unchanged;
- existing PTX framing/DDS/gallery/PNG/attachment/render regressions remain unchanged.

Add one bounded `ptx_runtime_compat_test.cpp` into the existing `DMCNativeReader::Core` test graph; do not create a second test framework.

## 8. Spider/product integration

Runtime compatibility is a dependency of the existing PTX route, not a separate user-facing NativeModule. Spider C++ may orchestrate an optional runtime-inspection action, but must not become the PTX parser or runtime semantic authority.

Suggested flow:

`PTX bytes -> existing TextureSet -> optional PtxRuntimeCompat -> typed inspection state -> existing product/UI projection`

## 9. Size/dedup constraints

This integration must obey the project hard gates:
- APK <= 4 MiB;
- installed `StorageStats.getAppBytes()` <= 4 MiB;
- one runtime DSO;
- duplicate runtime implementations/payloads = 0;
- no second PTX parser;
- avoid importing reverse-only tables/harness/data into production binary unless required at runtime.

The import review must attribute any native/APK growth to the runtime layer and remove unused copied code/data.

## 10. Review flow

This architecture is implemented only after the dedicated PTX architecture review issues GO. After implementation, a PTX-specific review must check provenance, semantics, C++23 boundaries, lazy behavior, tests, duplicate/weight impact and unresolved extension points before the broader C++23 Phase-2 review proceeds.
