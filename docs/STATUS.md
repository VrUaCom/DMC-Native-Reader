# DMC Native Reader — Status

## Current milestone

`1.0.0-rc1` / versionCode `18`

Canonical product repository: `VrUaCom/DMC-Native-Reader`.
Canonical DMC3 reverse/evidence source: `VrUaCom/dmc-rengine-cpp`.

The architecture/build-out and first real-device acceptance loop are complete. The project is in **release-candidate hardening**. See `V1_RC1.md` and `RELEASE_GATES_V1.md`.

## Architecture status

Production path:

`bytes -> NativeModuleRegistry -> explicit NativeModule -> PipelineResult -> InspectionDocument / RenderScene / ImagePreview / ChildResource -> JNI Session -> generic Android UI`

Closed boundaries:

- C++20 native path;
- central family `if/else` decode dispatcher removed;
- legacy duplicate SCM/MOD decoders removed;
- wildcard structural fallback removed;
- unknown family fallback removed;
- `InspectionDocument` is inspection authority;
- `RenderScene` is geometry/hierarchy authority;
- `ResourceCapabilities` / `ResourceUiState` control UI availability;
- `RenderFlags` controls overlays;
- `ChildResource` provides generic nested-resource presentation and navigation;
- no DDS/PTX/MOD/SCM-specific Android viewer classes.

## Registry coverage

Current recognized unique families: **71**.
Current explicit registry entries: **71**.
Wildcard modules: **0**.

Recognition remains separate from semantic reverse. Evidence-gated and recognition-only families do not borrow layouts from promoted formats.

## v1 accepted reader surface

### MOD

- canonical structural/renderable reader;
- positions, normals, UV and topology;
- canonical node hierarchy and model-space world matrices;
- instance-level spatial-authority gate;
- generic 3D hierarchy overlay;
- skin weights / influence inspection;
- typed texture slot and legacy GS CLAMP / REGION_REPEAT state;
- bitmap/companion mapping remains unresolved rather than invented.

### SCM

- canonical structural/renderable reader;
- canonical scene hierarchy and world matrices;
- generic scene-hierarchy overlay;
- texture binding and legacy GS sampler state inspection.

### DDS / PTX

- DDS bounded DXT1/DXT5 reader and base-mip image preview;
- complete mip-chain validation;
- PTX bundle reader with validated embedded DDS children;
- generic preview-first child-resource gallery;
- PTX child DDS opens through the same Session / Inspector / ImagePreview path;
- explicit `←` and Android Back restore the parent Session without reparsing;
- malformed/overflow/trailing-data/sector-span/padding cases fail closed.

### Other promoted v1 readers

- HITS collision;
- stage TXT bounded lexer / structural reader;
- `.index` textual manifest reader;
- DCA structural records;
- LIG/LIG2 structural lighting records;
- PAC / PNST relative-slot container inspection;
- NBZ top-level inspection boundary.

### Partial / evidence-gated

- EFM, MRP and SHW retain explicit family adapters without false semantic-completion claims;
- SO research exists but v1 does not claim a completed semantic product reader;
- remaining known families use explicit recognition-only contracts where necessary.

## Real-device acceptance

Samsung device acceptance completed for the current v1 feature set:

- MOD open/render/rotate/zoom/wireframe/Inspector;
- MOD hierarchy overlay and skin/weight inspection;
- MOD texture slot + GS CLAMP Inspector;
- SCM render + scene hierarchy overlay + texture/GS-state Inspector;
- PTX real texture thumbnails;
- PTX -> child DDS -> full image preview -> `←` / Android Back -> existing PTX gallery;
- standalone DDS image preview;
- application label `DMC Native Reader` without legacy `v8` suffix.

## Safety boundary

Native Reader remains read-only.

- mapped input cap: 512 MiB;
- image-preview allocations are bounded;
- DDS/PTX malformed and overflow paths fail closed;
- invalid MOD spatial data does not gain hierarchy-overlay authority;
- non-renderable resources cannot reuse stale geometry;
- unknown fields stay unknown / preserved where applicable;
- no production signing secret is stored in Git history.

## RC1 automated gates

The RC branch must pass:

1. canonical vendor diff/provenance guards;
2. host modular-reader regression;
3. MOD spatial/material regression;
4. DDS/PTX malformed + child-resource regression;
5. RenderScene/overlay regression;
6. Java capability/UI policy regression;
7. Android NDK / ARM64 build;
8. debug and unsigned release APK builds;
9. package `com.dmcrengine.nativereader`;
10. versionCode `18` / versionName `1.0.0-rc1`;
11. native marker / retired-decoder / wildcard guards;
12. development debug signer verification;
13. unsigned release signing-boundary verification;
14. APK SHA-256 evidence.

## Remaining v1.0 blocker

The normal Gradle release build is intentionally unsigned. The final stable `1.0.0` artifact requires a production signing authority provisioned outside the repository, plus recorded production certificate fingerprint and APK SHA-256.

No new format promotion is required to call the current reader architecture v1.0. New semantic reverse work should not expand RC scope unless needed to fix a release regression.
