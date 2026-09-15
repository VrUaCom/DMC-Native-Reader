# DMC Native Reader v33 — Modular + Spider architecture contract

This document is the canonical architecture boundary for the v33 line.

Project/AI governance, evidence rules and phase/review workflow are defined in `docs/PROJECT_AI_CONTEXT.md` and Project card #46. If this architecture contract and a historical review snapshot disagree, this contract plus the current Project review-gate decision are authoritative.

## Product layers

```text
Android / future platform shell
  -> file descriptors, URI lifecycle, save/open dialogs, widgets
  -> JNI transport only
  -> DMCNativeReader::Core (portable C++23)
       -> bounded probe
       -> NativeModuleRegistry
            -> MOD      -> Spider Crusader -> MOD adapter -> Rengine ReaderCore
            -> SCM      -> Spider Crusader -> SCM adapter -> Rengine ReaderCore
            -> DDS/PTX  -> Spider Crusader -> TextureSet / Rengine codecs
            -> EventTbl -> Spider Crusader -> Rengine EVT parser
       -> explicit product modules
            -> Composite model state
            -> Composite builder
            -> Rengine-backed MOD attachment resolver
            -> Composite placement projection
            -> Texture companion binding
            -> WorkspaceGraph / stable resource identity
       -> Spider C++23 product language
            -> typed Result / Status contracts
            -> compile-time concepts
            -> typed facade over Spider Crusader
       -> Spider session actions
            -> Compose MOD parts
            -> Explicit host-joint placement / reset
            -> Attach shared PTX bank
            -> Attach PTX to one part
       -> Spider Black Widow capability/application-state policy
       -> renderer / inspection / UV gallery
```

The platform shell must never become a second parser, module registry, capability
rules engine, model/texture composition implementation or cross-model placement
resolver.

## One canonical runtime image

Android packages exactly one native DSO:

`lib/arm64-v8a/libdmcviewer.so`

`DMCNativeReader::Core` and `DMCRengine::ReaderCore` are static link-time
components of that DSO. `libdmcshim*`, `libdmccore00.so`, `dlopen` and `dlsym`
recovery delegation are forbidden.

The DSO is stored uncompressed with `extractNativeLibs=false`. Both its APK ZIP
data offset and every ELF `PT_LOAD` alignment must satisfy 16 KiB page support.
Unreachable static sections are removed with function/data sections and
`--gc-sections`.

## Canonical Rengine authority

v33 pins DMC Rengine ReaderCore to:

`caf445226c7d61841292384a10e93e4f58ae29f9`

This pin adds the read-side MOD cross-model default-joint attachment contract while
remaining on the canonical reverse/mod-completion line. SCM authority continues to
descend from baseline:

`809824882c60487962e99ee41f16bca7e3ccbc83`

Native Reader must consume format knowledge from Rengine rather than copy it into
Android or create a second canonical parser.

The Native Reader C++23 migration does not change Rengine's repository, submodule
pin, format authority or language policy. `DMCRengine::ReaderCore` is an external
boundary consumed by the C++23 Native Reader product target.

## Promoted production modules

The production registry contains five promoted families:

- `MOD`
- `SCM`
- `DDS`
- `PTX`
- `EventTbl`

Every promoted route executes through Spider Crusader. Format-specific parsing
remains in its canonical adapter/parser; Spider owns operation/dependency
orchestration rather than absorbing format implementations.

A recognized resource without a registered module fails closed. Future MOT,
physics, cloth, container or other families require their own evidence-backed
native module and regression gate before becoming supported.

## C++23 + Spider C++ product language

C++23 is the canonical language standard for `DMCNativeReader::Core`, the Android
JNI target and Native Reader native regressions. Android uses NDK r30 LTS
(`30.0.16248370`). The language decision is **target-scoped in CMake**:

- `cxx_std_23`;
- `CXX_STANDARD 23`;
- `CXX_STANDARD_REQUIRED ON`;
- `CXX_EXTENSIONS OFF`.

Gradle owns Android toolchain selection, ABI and packaging, but deliberately does
**not** pass a global `-std=c++*` flag. A Gradle-global language flag could also
alter vendored dependency targets, which would violate the Native Reader/Rengine
boundary.

`cpp23_profile.h` is the compile-time product capability profile. CMake selects
strict ISO C++23; the profile rejects C++20-or-older and proves the concrete
required library facilities through SD-6 feature-test macros. The current required
profile includes:

- `std::expected`;
- `std::byteswap`;
- `std::to_underlying`.

The profile intentionally does not require one compiler-specific
`__cplusplus == 202302L` value. Product modules may use C++23 facilities when they
solve a bounded architectural problem and pass the Project review process.

The existing typed `WorkspaceGraph` results and initial Spider C++ seed entered the
feature branch before the formal Phase/Review workflow was introduced. Their
presence is not automatic acceptance: Review Gate #41 must explicitly classify
those early changes as retain/correct/defer/revert before broader modernization.
Phase 2 must not opportunistically expand them.

**Spider C++** is the embedded C++23 orchestration language/profile for Native
Reader. It is not a second executor or separate runtime. `spider/cpp23_language.h`
adds typed result aliases, concepts and typed state execution while delegating to
`spider::crusader`, which remains a zero-overhead facade over the pinned Rengine
native executor.

Current authority chain:

```text
Native Reader C++23 action
  -> Spider C++ typed profile
  -> Spider Crusader facade
  -> pinned Rengine native executor
```

Future Spider C++ features may include compile-time plan declarations and stronger
`consteval` validation, but they must not duplicate the executor, dependency graph
runtime or canonical format knowledge. Expansion beyond the current seed remains
frozen until its designated Project phase/review gate.

## Spider session actions

Composition, placement and texture attachment are product actions, not JNI behavior.
`dmcresource::spider::actions` is the production action boundary for:

- composing canonical MOD sessions;
- explicitly placing one composite MOD part against one host joint;
- resetting one part to source coordinates;
- attaching one PTX to a normal model;
- attaching one shared PTX bank to a composite;
- attaching PTX to one explicit composite part.

The former monolithic `spider/session_actions.cpp` is no longer compiled. Compose
and texture operations are split into `session_compose_actions.cpp` and
`session_texture_actions.cpp`. JNI only maps bytes/handles and invokes these actions.

The MOD compose path is the first production action routed through the existing
Spider C++ seed. Its typed wrapper still executes the canonical Crusader plan rather
than replacing it; broader Spider C++ language work is governed by the Project
phase/review sequence.

## Shared PTX — no per-part RGBA duplication

When several MOD parts use the same PTX companion, Native Reader:

1. collects the union of required source-local PTX slots;
2. parses the PTX once;
3. decodes each required local slot once;
4. stores one slot-indexed texture bank;
5. maps every composite triangle directly to the appropriate bank index.

A local slot used by several MOD parts therefore owns one decoded RGBA image.
Different PTX slot identities are **not** merged merely because their current RGBA
pixels happen to be identical; slot identity is preserved for future material and
sampler semantics.

The pre-attachment composite may use synthetic non-overlapping slot ranges to keep
source namespaces unambiguous. Once an explicit shared bank is accepted, the
renderer projection switches to actual shared bank indices.

Per-part PTX replacement is transactional. New textures and remapped triangle slots
are staged independently; the live texture bank is replaced only after decode,
range validation, required-slot validation and compaction all succeed. A failed
replacement therefore preserves the previous valid bank and projection.

## Multi-MOD geometry ownership

Each `CompositePart` retains its authoritative source-local `RenderScene`, node
namespace and local texture-slot projection. The top-level composite keeps a
flattened render projection as a derived cache for the current software renderer.
It is not format authority and must never be reparsed or treated as a second model
source.

Composite part data and placement state are defined outside the generic `Session`
contract in the composite-model module. Cross-MOD placement is implemented in a
separate placement module rather than in the parser, renderer, JNI or texture path.

### WorkspaceGraph identity

Each composite model part receives stable native `AssetId` and `InstanceId` values.
Cross-resource bindings target stable instance identity instead of presentation
vector positions. `CompositePlacement` retains `host_instance_id` as semantic
identity; `host_part_index` is a derived cache for the current flattened order.

Workspace graph mutations currently use C++23 `std::expected` through
`WorkspaceResult`, so failures such as missing asset, wrong asset kind, missing
target or duplicate target remain typed instead of collapsing to an invalid-ID
sentinel. Because this modernization predates formal Phase gates, Review Gate #41
must explicitly decide whether this implementation is retained, corrected, deferred
or reverted before Phase 3 proceeds.

### Composite builder and primary host

Production composition enters through `composite_builder`. The product supplies an
explicit primary/base MOD; in the current Android flow this is the model that was
already open before the user adds additional MOD parts. Appended parts are not
promoted to hosts merely because of filenames or visual proximity.

After low-level source-scene composition, the builder may resolve each appended
part against that explicit primary host. If resolution fails, the child remains in
source coordinates.

### Rengine-backed default-joint placement

DMC Rengine owns the cross-model selector contract:

```text
child MOD header +0x13
  -> Header::default_joint_index()
  -> explicit host world-matrix domain
  -> bounds-checked host joint matrix
```

Native Reader transports the typed selector in `RenderScene` and delegates selector
indexing to `dmc::rengine::formats::mod::attachment`. It does not reread raw header
offsets in the compositor.

The composite builder then applies the resolved host joint as the child root
placement:

```text
source-local child RenderScene
  + explicit primary host
  + Rengine default-joint resolution
  -> host joint current/model-space matrix
  -> child root placement projection
  -> derived composite vertices + hierarchy overlay
```

The source-local child scene is never mutated. Reset reconstructs the derived child
projection from that retained source scene without reparsing bytes.

Placement fails closed when the selector is absent/out of range, the primary host
lacks canonical spatial authority, matrix data is rejected, or the flattened
composite cache no longer matches retained source-part ordering. Native Reader does
not infer a different host from filenames, `runtime_metadata_u32`, visual proximity
or an arbitrary scan of candidate models.

## SCM spatial authority

SCM rendering uses canonical scene hierarchy/object binding/world transforms from
Rengine. The regression path covers:

`serialized hierarchy/order/binding -> world matrices -> primitive node binding -> final world-space vertices`

The fixture uses parent rotation plus child translation so a translation-only or
local-space fallback cannot pass accidentally. This contract protects stage SCMs
such as the tested `st002` family.

## Black Widow

Black Widow owns typed application capability policy. Android consumes its bitmask
and may combine it only with platform lifecycle/navigation state.

Android must not infer actions from filenames, URI lists, diagnostics or raw format
names. Current native flags include rendering, wireframe, hierarchy, UV, inspection,
PNG export, texture-companion state, add-model-part and stage-companion capability.

Placement-specific UI capability must be added to Black Widow before any platform
shell exposes manual host/joint placement controls.

## JNI boundary

JNI may:

- mmap/read a user-selected file descriptor;
- translate Java values;
- own opaque native handles;
- lock/fill Android `ARGB_8888` Bitmaps;
- call portable Native Reader APIs / Spider actions.

JNI must not parse DMC formats, compose models, resolve model attachments, implement
texture binding policy or reconstruct Black Widow decisions.

All resource-facing JNI calls fail closed on native exceptions.

## APK size and ABI gates

The v33 verifier requires:

- package `com.dmcrengine.nativereader`;
- versionCode `33`, versionName `1.0.6`;
- ARM64 only;
- canonical Native Reader standard C++23 selected target-scoped in CMake;
- Android NDK r30 LTS `30.0.16248370`;
- C++23 product capability profile with required SD-6 library features;
- no Gradle-global `-std=c++*` authority;
- Spider C++ profile layered over Crusader;
- stable device-test signer;
- exactly one native DSO;
- APK <= 8 MiB;
- native DSO <= 4 MiB;
- total Dex <= 1 MiB;
- no Kotlin runtime;
- Java `NativeBridge` / exported JNI symbol exact parity;
- no recovery shim/core markers;
- direct Bitmap ABI;
- 16 KiB ZIP alignment;
- 16 KiB-or-greater ELF `PT_LOAD` alignment;
- canonical Rengine gitlink/checkout equality at `caf445226c7d61841292384a10e93e4f58ae29f9`;
- modular composite builder/resolver/placement sources compiled into the portable core;
- native `WorkspaceGraph` compiled into the portable core;
- split Spider compose/texture action sources compiled instead of the old monolith.

## Regression gates before device acceptance

At minimum the complete host CTest suite must run. Critical v33 regressions include:

- `cxx23_profile_test` — target-scoped C++23 plus required SD-6 product facilities and Spider C++ concept/profile contract;
- `workspace_graph_test` — stable identities and typed C++23 mutation failures, pending explicit Review Gate #41 disposition;
- `module_registry_test` — five promoted families;
- `spider_model_execution_test` — model/texture Spider routes and typed capability split;
- `gdata_legacy_test` — PTX/TM2/EventTbl compatibility;
- `composite_mod_scene_test` — Spider compose, shared PTX one-decode bank, no per-part RGBA duplication;
- `composite_builder_test` — primary-host default-joint auto placement plus source-coordinate fail-closed fallback;
- `composite_placement_test` — explicit host-joint root projection, DMC row-vector rotation order, reset and authority checks;
- `ptx_transaction_test` — failed per-part replacement preserves the previous live texture bank;
- `scm_authority_test` — retail versions, header authority and world-space placement;
- `ptx_model_texture_test` — canonical texture-slot binding;
- `black_widow_state_test` — native action/UI policy;
- `png_export_session_test` — export capability;
- `render_scene_test` and `mod_spatial_adapter_test` — render/spatial contracts.

A GitHub job that fails before runner assignment (`runner_id=0`, no steps) is neither
green evidence nor a source regression. A canonical APK is accepted only after a
real exact-head clean build, verifier pass, required review gates and physical
Samsung device test.
