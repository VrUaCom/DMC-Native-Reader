# Native Reader v33 — Resource Dependency Graph

This document extends `MODULAR_SPIDER_V33.md` with the canonical ownership and dependency model for composite resources.

## Problem

A composed DMC scene cannot assume either:

- one texture container per MOD; or
- one shared texture container for every MOD.

Both patterns exist, and mixed patterns must be representable without copying resource payloads or inventing dependencies.

Examples the architecture must support:

1. `MOD A + MOD B + MOD C + MOD D -> one PTX`;
2. `MOD A -> PTX A`, `MOD B -> PTX B`, `MOD C -> PTX C`;
3. `MOD A + MOD B -> PTX X`, `MOD C -> PTX Y`, `MOD D -> PTX Z`;
4. one model part bound to more than one companion bank when canonical evidence explicitly defines disjoint slot domains;
5. the same physical companion resource referenced by several model parts without decoding or storing duplicate payloads;
6. separate logical resources that happen to contain equal pixels must remain distinct when their slot/material/provenance identity differs.

## Terminology

A source container is not the same thing as a runtime resource.

- **SourceContainer**: PAC / PNST / NBZ or another physical parent archive.
- **ResourceAsset**: MOD / SCM / PTX / DDS / EventTbl / future MOT etc.
- **ContentBlob**: immutable encoded bytes identified by content key/hash.
- **ModelInstance**: a workspace instance of a model asset.
- **TextureBank**: one decoded slot-indexed view of a PTX/texture asset.
- **BindingEdge**: typed relationship between one resource and one or more targets.
- **WorkspaceGraph**: the authoritative set of nodes and edges for the currently assembled scene.

## Ownership layers

```text
Platform Source Provider
  -> URI / FD / persistent permission only
  -> ContentStore
       -> immutable ContentBlob (deduplicated by exact content identity)
       -> ResourceAsset nodes
            -> ModelAsset
            -> TextureAsset / TextureBank
            -> MotionAsset
            -> PhysicsAsset
            -> ClothAsset
            -> ...
  -> WorkspaceGraph
       -> ModelInstance nodes
       -> Companion nodes
       -> BindingEdge[]
  -> Spider Resolver / Actions
  -> RenderPlan
```

Java/Swift/Win32 shells may retain platform source tokens needed to reopen a resource, but they must not own semantic MOD-to-PTX binding state.

## Stable identifiers

Bindings must never depend on array position such as `modelPartPtxUris[index]`.

```cpp
using AssetId = std::uint64_t;
using InstanceId = std::uint64_t;
using BindingId = std::uint64_t;
using ContentKey = Hash256;
```

An asset can survive model reordering, insertion or removal without changing every relationship.

## Resource node

Conceptually:

```cpp
struct ResourceAsset {
    AssetId id;
    ResourceKind kind;
    ContentKey content;
    SourceProvenance provenance;
    TypedResource typed;
};
```

Two logical assets may reference the same `ContentBlob` if their bytes are identical. This deduplicates storage without erasing provenance or logical identity.

## Model instance

```cpp
struct ModelInstance {
    InstanceId id;
    AssetId model_asset;
    Transform placement;
    LocalResourceNamespace local;
};
```

A MOD/SCM remains authoritative in its own local node, mesh and texture-slot namespace.

## Texture bank

A PTX is decoded once per `(ContentKey, decode policy)` and produces one slot-indexed bank:

```cpp
struct TextureBank {
    AssetId source_asset;
    std::vector<ImagePreview> slots;
};
```

The bank is immutable after successful decode. Several bindings may point to it.

## Binding edges

The graph represents relationships rather than copying assets.

```cpp
enum class BindingRole {
    Texture,
    Motion,
    Physics,
    Cloth,
    Effect,
};

struct SlotBinding {
    std::uint32_t model_local_slot;
    std::uint32_t resource_local_slot;
};

struct BindingEdge {
    BindingId id;
    AssetId source_asset;
    BindingRole role;
    std::vector<InstanceId> targets;
    std::vector<SlotBinding> texture_slots;
    BindingEvidence evidence;
};
```

A single PTX shared by four MOD parts is one texture asset + one bank + one edge with four targets. Four independent PTX companions are four texture assets/banks + four edges. A mixed scene is simply a mixed graph.

## No forced global texture container

The renderer must not require one global PTX namespace as format authority.

Instead, Spider resolves each triangle/material reference to a compact runtime handle:

```text
(ModelInstance, local texture slot)
    -> BindingEdge
    -> TextureBank
    -> bank-local slot
```

The renderer may build a temporary compact `RenderTextureTable` for one frame/session, but that table is a derived cache. It is never the authoritative dependency model and must not duplicate RGBA payloads.

## Deduplication rules

### Allowed

- exact encoded `ContentBlob` deduplication by content identity;
- one decoded `TextureBank` reused by many binding edges;
- reuse of an already decoded bank slot for repeated references to the same source resource + source-local slot + decode policy;
- derived render/index caches when they do not become a second source of truth.

### Forbidden

- one RGBA copy per MOD merely because several MODs use the same PTX;
- merging different PTX slots solely because current pixels are equal;
- merging resources solely by filename;
- turning a derived flattened render namespace into canonical resource identity;
- storing semantic bindings only as Java URI arrays or array indices.

## Dependency discovery

Spider owns dependency resolution. Modules expose requirements/offers; Spider matches them.

Example texture requirement:

```cpp
struct TextureRequirement {
    InstanceId target;
    std::vector<std::uint32_t> required_local_slots;
};

struct TextureOffer {
    AssetId bank;
    std::vector<std::uint32_t> available_slots;
};
```

Resolution evidence order:

1. explicit canonical runtime/container relation;
2. explicit user binding;
3. structural resource identity / resource code / parent-container provenance;
4. exact required-slot coverage and format compatibility;
5. naming/prefix relationship as candidate evidence only;
6. unresolved/ambiguous -> require user choice; never fabricate.

Suggested statuses:

- `BOUND_CONFIRMED`;
- `BOUND_EXPLICIT`;
- `BOUND_STRUCTURAL`;
- `CANDIDATE_HEURISTIC`;
- `UNBOUND`;
- `REJECTED`.

## Spider responsibilities

Crusader remains the execution engine. New bounded actions should be introduced instead of Java state machines:

```text
ImportResource
CreateModelInstance
AddModelPart
ResolveDependencies
BindResource
BindResourceToTargets
ReplaceBinding
RemoveBinding
ComposeRenderPlan
StageCompanion
```

A resolver plan can inspect all modules in parallel where safe, then commit bindings transactionally.

## Black Widow responsibilities

Black Widow derives UI capabilities from `WorkspaceGraph`, for example:

- `CanAddModelPart`;
- `HasUnboundTextureRequirements`;
- `CanBindSharedTexture`;
- `CanBindTextureToSelection`;
- `HasAmbiguousDependencyCandidates`;
- `CanStageMotion`;
- `CanReplaceBinding`.

Android must not infer these from filenames, URI lists or resource counts.

## UI model

The PTX dialog must evolve from:

```text
Shared PTX for all
or
one PTX for one part
```

into a generic binding UI driven by native candidates:

```text
Resource: em028_000.ptx
Compatible targets:
[x] em028_001.mod
[x] em028_004.mod
[x] em028_005.mod
[x] em028_006.mod
```

For a different corpus it may naturally become:

```text
bank_A.ptx -> MOD 1, MOD 2
bank_B.ptx -> MOD 3
bank_C.ptx -> MOD 4
```

The UI edits graph edges; it does not own the graph.

## Migration from current v33 branch

Current state uses:

- `modelPartUris[]`;
- parallel `modelPartPtxUris[]`;
- one `sharedModelPtxUri`;
- `CompositePart::texture_slot_base/span`;
- one `Session::attached_textures` vector;
- one shared/per-part PTX action distinction.

Migration order:

1. add `ContentStore` + stable asset/instance IDs;
2. add `WorkspaceGraph` and typed `BindingEdge`;
3. move PTX binding ownership from Java URI arrays into native graph;
4. replace shared/per-part special cases with generic target-set binding action;
5. let Spider resolver produce candidate bindings from module requirements/offers;
6. make Black Widow expose graph-derived actions;
7. render through a derived binding table/view without RGBA duplication;
8. remove `sharedModelPtxUri`, `modelPartPtxUris[]` and synthetic global slot authority;
9. extend the same graph to MOT/physics/cloth/effects when those modules are promoted.

## Acceptance fixtures

At minimum regressions must cover:

1. em028-like `4 MOD -> 1 PTX`;
2. `4 MOD -> 4 PTX`;
3. mixed `MOD 1+2 -> PTX A`, `MOD 3 -> PTX B`, `MOD 4 -> PTX C`;
4. two byte-identical PTX files as separate logical assets sharing one ContentBlob but retaining distinct provenance;
5. equal RGBA in different PTX slots must not collapse slot identity;
6. model reordering must not break bindings because edges use stable IDs, not vector indices;
7. binding replacement/removal must not duplicate or leak decoded texture banks;
8. ambiguous auto-resolution must fail to candidate/manual selection instead of guessing.
