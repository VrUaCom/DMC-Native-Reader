# DMC Native Reader v1 — Stable Baseline

**Release:** `1.0.0`  
**versionCode:** `20`  
**Production branch:** `main`  
**Frozen baseline ref:** `baseline/v1.0.0`

## Baseline decision

The v1 production baseline is deliberately narrow. It contains exactly four promoted format families:

- MOD;
- SCM;
- DDS;
- PTX.

This is an architecture/evidence decision, not a claim that other DMC3 formats are unimportant. Earlier experimental readers are not part of the supported v1 product surface until they meet the same Architecture v2 and canonical-authority standard.

## Required architecture

```text
resource bytes
  -> bounded probe
  -> four-entry NativeModuleRegistry
  -> format module / canonical adapter
  -> InspectionDocument / RenderScene / ImagePreview / ChildResource[]
  -> generic JNI Session
  -> capability-driven Android UI
```

Forbidden in the v1 production baseline:

- wildcard format dispatcher;
- broad recognition-only fallback;
- Java-owned MOD/SCM format parsers;
- renderer-owned binary parsers;
- legacy `DecodeResult -> Mesh -> RenderScene` compatibility bridge;
- archived HITS/TXT/DCA/PAC/PNST/etc. module translation units in the main build.

## Accepted format contracts

### MOD

Canonical `dmc-rengine-cpp` reader with Architecture v2 projection into geometry, hierarchy/spatial state, skin/weight inspection and texture-slot presentation contracts where supported by canonical authority.

### SCM

Canonical `dmc-rengine-cpp` reader with Architecture v2 projection into scene hierarchy, transforms, geometry and texture-slot presentation contracts.

### DDS

Bounded DMC3 DXT1/DXT5 reader with strict mip/payload validation and generic image preview.

### PTX

Bounded texture-bundle reader with descriptor validation and generic DDS child resources. Child previews and parent navigation use the same generic session/UI contracts as top-level resources.

## Release acceptance gate

The accepted v1.0.0 candidate proved:

1. registry size is exactly four;
2. MOD/SCM/DDS/PTX modules are present;
3. retired families do not resolve to modules;
4. MOD and SCM execute end-to-end through the pipeline and publish valid render/inspection state;
5. DDS accepts valid DXT1/DXT5 and rejects malformed/overflow cases;
6. PTX validates children, bounds and padding and publishes generic DDS child resources;
7. archived source paths are absent from the production build tree;
8. Android UI policy remains capability-driven;
9. ARM64 APK builds and contains the four expected module IDs;
10. retired module IDs are absent from `libdmcviewer.so`;
11. explicit DMC MIME exposure is limited to MOD, SCM, DDS and PTX;
12. package/version identity is `com.dmcrengine.nativereader` / `1.0.0` / `20`;
13. the production APK is signed by the dedicated production authority;
14. release APK SHA-256 and signing evidence are recorded.

## Production trust

Pinned production certificate SHA-256:

`2d82bd3e77b2c1882d3f8143fe8760fc4c834aa65fc5e7b1b12082afcb7718d1`

v1.0.0 APK SHA-256:

`a81ef5555ecc67e0213d2f1f6baa351609a659c5898ba8f71f81d2fc2cefc68c`

Public-source debug builds are intentionally isolated under `com.dmcrengine.nativereader.debug` and do not share the production trust chain.

## Device regression

The accepted practical UI target covers:

- MOD render/inspection;
- SCM render/inspection;
- DDS preview;
- PTX gallery;
- PTX -> DDS child preview;
- child -> parent navigation;
- malformed/unsupported input fails closed without stale UI state.

Future format work must preserve this baseline rather than reopening the old broad decoder architecture.
