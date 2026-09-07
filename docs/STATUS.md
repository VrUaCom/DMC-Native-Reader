# DMC Native Reader — Status

## Product role

**Mission:** Make DMC resources feel like ordinary files.

DMC Native Reader is the viewing/accessibility layer of the DMC tooling ecosystem. Its purpose is to open, recognize, inspect, visualize, navigate and explain promoted resources without requiring ordinary users to understand their binary formats.

The current stable implementation is Android. The architecture is intended to preserve the same C++20 semantic truth across future Android, iOS and Windows shells.

**DMC Rengine is the central decompilation/reimplementation engine and modding foundation.** Native Reader is one specialized product built on top of its capabilities. Pocket GDS is another specialized tool in the same ecosystem, focused on resource work and authoring workflows.

Native Reader is not the primary archive-management, editing or repacking workspace. Those responsibilities belong to DMC Rengine-backed authoring/resource-management tooling such as Pocket GDS and to the central writer/resource architecture exposed by DMC Rengine.

See [`PRODUCT_VISION.md`](PRODUCT_VISION.md).

## Current stable baseline

`1.0.0` / versionCode `20`

Repository: `VrUaCom/DMC-Native-Reader`  
Central engine/modding repository: `VrUaCom/dmc-rengine-cpp`  
Frozen stable ref: `baseline/v1.0.0`

## Production architecture

```text
DMC Rengine capability / resource authority
  -> NativeModuleRegistry
      -> MOD | SCM | DDS | PTX
          -> Architecture v2 projection
              -> generic JNI Session
                  -> capability-driven Android UI
```

Current production registry size: **4**.

There is no wildcard fallback and no broad recognition-only catalog in the supported v1 product path. Files outside the four promoted families fail closed.

## Supported core

### MOD

- renderable;
- canonical `dmc-rengine-cpp` structural parser;
- `RenderScene` geometry;
- hierarchy/spatial projection where canonical authority is available;
- skin/weight inspection state;
- texture-slot state;
- typed `InspectionDocument`.

### SCM

- renderable;
- canonical `dmc-rengine-cpp` structural parser;
- `RenderScene` geometry;
- scene hierarchy and transforms;
- texture-slot state;
- typed `InspectionDocument`.

### DDS

- bounded DMC3 DXT1/DXT5 validation;
- complete mip-chain checks;
- generic image preview;
- malformed/overflow rejection.

### PTX

- bounded texture-bundle validation;
- descriptor/DDS size coherence;
- generic DDS child resources;
- child image previews through the generic image contract;
- child -> parent navigation through generic sessions.

## Not in the supported v1 registry

Earlier experiments included additional DMC families, but they are not part of the stable v1 product surface. HITS, TXT, `.index`, DCA, LIG/LIG2, PAC/PNST, NBZ, EFM/MRP/SHW and the broader recognition catalog must be re-promoted one by one through the same v2/canonical authority rules.

Historical branches are development evidence, not supported release lines.

## CI gates

The public PR/push workflow proves:

1. archived legacy source paths are absent;
2. registry contains exactly MOD, SCM, DDS and PTX;
3. retired families resolve to no module;
4. MOD and SCM pass end-to-end native pipeline projection tests;
5. MOD spatial adapter regression passes;
6. DDS and PTX pass valid, malformed, bounds and child-preview regressions;
7. `RenderScene` regression passes;
8. Java capability UI regression passes;
9. ARM64 debug APK builds;
10. the APK contains the four promoted module IDs and no retired module IDs;
11. explicit DMC MIME exposure is limited to MOD/SCM/DDS/PTX;
12. public-source debug package identity is isolated as `com.dmcrengine.nativereader.debug`;
13. no signing/private-key material is tracked in the public tree;
14. normal Gradle release output remains unsigned.

## Production signing

Official production package: `com.dmcrengine.nativereader`.

Pinned v1 production certificate SHA-256:

`2d82bd3e77b2c1882d3f8143fe8760fc4c834aa65fc5e7b1b12082afcb7718d1`

Production signing is performed only through a protected release environment using secrets that are not committed or uploaded as artifacts.

## Device boundary

The accepted Samsung/OEM behavior remains the practical UI target:

- MOD opens/renders;
- SCM opens/renders;
- standalone DDS previews;
- PTX opens as a child gallery;
- DDS child preview opens;
- parent navigation returns to the PTX session;
- malformed/unsupported inputs fail closed without stale geometry.

## Ecosystem boundary

```text
                         DMC Rengine
                 central engine + modding core
                              |
             +----------------+----------------+
             |                                 |
       Native Reader                       Pocket GDS
  view / inspect / visualize       manage / edit / author / repack
             |
      Android / iOS / Windows
```

Native Reader should consume DMC Rengine capabilities, not fork them. If a recovered semantic, parser, writer or resource capability belongs in the central engine, it should be promoted there first and then exposed through the appropriate downstream tool.

## Success metric

The primary Native Reader product metric is:

> **How many opaque DMC resource types have become directly understandable and viewable?**

The project should prefer adding honest, familiar representations for new resource families over adding unrelated editor features to the Reader itself.

## Next promotion rule

No historical family returns to `main` merely because old code exists. Each future format must enter through the same Architecture v2 module contracts and, wherever possible, reuse the corresponding DMC Rengine parser/source authority.

A promoted format must also define the natural user-facing representation it enables: model, scene, image, gallery, hierarchy, animation, graph, bounds/volume or another evidence-backed view.
