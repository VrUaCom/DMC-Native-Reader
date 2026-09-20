# PTX Runtime Import — AI ТЗ v33

## Objective
Import the evidence-backed PTX runtime behavior recovered in read-only Rengine commit `50d070e158e484937238d9cb02b2bc6affb2f502` into **DMC Native Reader only**, port/adapt it to ISO C++23, wrap it behind one local runtime-compatibility API, and integrate it as a lazy internal layer of the existing PTX product route without creating a second serialized PTX parser.

## Repository authority
Write only to `VrUaCom/DMC-Native-Reader`.

`VrUaCom/dmc-rengine-cpp` is read-only. Reading/copying the authorized PTX slice is allowed; no mutation of any Rengine resource is allowed.

## Substages
### 1 — Source inventory and provenance
- inspect `50d070e...` read-only;
- identify the minimum production-relevant structures/functions for pool initialization, reservation, placement, manager/cache state and confirmed reset/release behavior;
- separate production logic from reverse harness/evidence generator/test-only data;
- record source symbol/function provenance and evidence identifiers before copying.

### 2 — C++23 local port
- create Reader-owned runtime compatibility types under `dmcresource`;
- use fixed-width types and typed errors/results;
- preserve confirmed branch/state behavior;
- remove reverse-development-only plumbing;
- do not expose EXE addresses in public API;
- apply project exception/noexcept rules.

### 3 — Wrapper boundary
Target files:
- `app/src/main/cpp/include/dmcresource/ptx_runtime_compat.h`
- `app/src/main/cpp/modules/ptx_runtime_compat.cpp`

Required product operations:
- initialize;
- configure reservation;
- place/validate record;
- reset confirmed manager/cache keys/count state;
- inspect typed runtime state;
- only expose release/reset operations whose semantics are confirmed.

### 4 — Existing PTX integration
- keep `TextureSlotFramingParser -> TextureSet -> DDS decoder` unchanged as serialized-file authority;
- do not create a second PTX parser;
- do not create a second user-facing NativeModule;
- add lazy RuntimeCompat activation after existing decode only when runtime inspection/validation is requested;
- ordinary preview/PNG/gallery must not initialize the runtime pool/manager.

### 5 — Extension points
- palette stage remains unresolved/disabled until its behavior is separately confirmed and authorized for import;
- finalizer/cleanup ordering remains unresolved/disabled until confirmed;
- use explicit status/extension-point representation rather than guesses.

### 6 — Tests
Add one bounded `ptx_runtime_compat_test.cpp` to the existing CMake/native test graph. Cover:
- initialize;
- automatic/default placement cases represented by the imported code;
- explicit placement;
- no-space/failure;
- reservation resize;
- key/count reset;
- pool records/occupancy/payload preservation where confirmed;
- source PTX bytes unchanged;
- lazy path does not alter existing preview results.

Retain all existing PTX/DDS/PNG/gallery/attachment/render regressions.

### 7 — Weight/dedup audit
- no copied reverse harness in production binary;
- no duplicated serialized PTX parser;
- no second runtime implementation;
- one DSO;
- APK pre-gate <= 4 MiB;
- downstream installed `StorageStats.getAppBytes()` <= 4 MiB;
- attribute any size growth to required runtime compatibility code/data.

## Constraints
- no Rengine writes of any kind;
- no changes to existing serialized PTX format semantics;
- no guessed palette/finalizer/cleanup behavior;
- no separate runtime app/module visible to users;
- no CI per intermediate commit; remain in migration batching mode;
- no merge/release until the PTX-specific review and global Phase-2 review both pass.

## AI execution prompt
> Працюй тільки в `VrUaCom/DMC-Native-Reader`. Прочитай `docs/PTX_RUNTIME_IMPORT_ARCHITECTURE_V33.md`, цей ТЗ, #46 і активний Phase-2 plan. `dmc-rengine-cpp` не змінюй за жодних умов. Можна тільки прочитати commit `50d070e158e484937238d9cb02b2bc6affb2f502` і скопіювати мінімально потрібний PTX runtime slice у Native Reader. Перенеси його на локальний C++23 contract, створи тонкий `PtxRuntimeCompat`, не створюй другий PTX parser або окремий NativeModule, залиш serialized framing/TextureSet/DDS authority без змін. Runtime layer має бути lazy. Не імпортуй reverse harness у production, не вгадуй palette/finalizer/cleanup. Додай один bounded regression test у наявний test graph, перевір exception boundaries, вагу й дублікати. Не запускай hosted CI на проміжних commit-ах. Після source completion передай exact HEAD у PTX Review Gate.

## Exit criteria
- imported PTX runtime slice exists only in Native Reader;
- provenance to `50d070e...` is documented;
- serialized PTX parser/decode path is unchanged as authority;
- C++23 wrapper is typed/fail-closed and lazy;
- confirmed initialization/reservation/placement/cache behavior is represented;
- unknown palette/finalizer/cleanup remains unresolved;
- integration regressions exist in the existing CTest graph;
- no duplicate parser/runtime/harness shipped;
- project size gates remain enforceable;
- PTX Review Gate can issue GO/NO-GO on one exact HEAD.
