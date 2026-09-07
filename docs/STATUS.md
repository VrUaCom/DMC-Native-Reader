# DMC Native Reader — Status

## Current baseline

`1.0.0-core-cleanup` / versionCode `19`

Repository: `VrUaCom/DMC-Native-Reader`  
Canonical reverse/evidence repository: `VrUaCom/dmc-rengine-cpp`  
Archived pre-cleanup branch: `main.2` — **до опрацювання**

## Main architecture

Production path:

```text
probe
  -> NativeModuleRegistry
      -> MOD | SCM | DDS | PTX
          -> Architecture v2 projection
              -> generic JNI Session
                  -> capability-driven Android UI
```

Current registry size: **4**.

There is no wildcard fallback and no recognition-only catalog in `main`. A file outside the four promoted families fails closed.

## Supported core

### MOD

- renderable;
- canonical `dmc-rengine-cpp` structural parser;
- `RenderScene` geometry;
- hierarchy/spatial projection when canonical authority is available;
- skin weights;
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
- generic `ImagePreview`;
- malformed/overflow rejection.

### PTX

- bounded texture-bundle validation;
- descriptor/DDS size coherence;
- generic DDS `ChildResource[]`;
- thumbnail/image previews through the generic image contract;
- child -> parent navigation through generic sessions.

## Removed from main

The old multi-format surface is not part of the clean v1 core. HITS, TXT, `.index`, DCA, LIG/LIG2, PAC/PNST, NBZ module, EFM/MRP/SHW and the broad recognition catalog are absent from the registry/build. Their pre-cleanup state is preserved on `main.2` for later canonical promotion.

The old `DecodeResult` compatibility path, HITS decoder and text decoder are removed from `main`.

## CI gates

The push/PR core workflow must prove:

1. archived legacy source paths are absent;
2. registry contains exactly MOD, SCM, DDS and PTX;
3. archived families resolve to no module;
4. MOD and SCM pass end-to-end synthetic pipeline projection tests;
5. MOD spatial adapter regression passes;
6. DDS and PTX pass valid, malformed, bounds and child-preview regressions;
7. `RenderScene` regression passes;
8. Java capability UI regression passes;
9. ARM64 APK builds;
10. the APK contains only the four promoted module IDs from the old module set;
11. explicit DMC MIME exposure in the manifest is limited to MOD/SCM/DDS/PTX.

## Device boundary

The previously accepted Samsung behavior remains the practical UI target:

- MOD opens/renders;
- SCM opens/renders;
- standalone DDS previews;
- PTX opens as a child gallery;
- DDS child preview opens;
- `←` returns to the PTX parent.

After the cleanup APK is green in CI it receives one short Samsung regression pass because the module routing surface and APK version changed.

## Next promotion rule

No archived family returns to `main` merely because old code exists. Each future format must enter through the same v2 module contracts and preferably reuse the corresponding canonical `dmc-rengine-cpp` parser/source authority. The `main.2` branch is backlog/reference, not a second production architecture.
