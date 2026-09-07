# DMC Native Reader v1 — Clean Baseline

**Build:** `1.0.0-core-cleanup`  
**versionCode:** `19`  
**Production branch:** `main`  
**Archive/backlog branch:** `main.2` — до опрацювання

## Baseline decision

The v1 baseline is deliberately narrow. It contains only four promoted format families:

- MOD;
- SCM;
- DDS;
- PTX.

This is a structural decision, not a claim that other DMC3 formats do not matter. Other readers were removed from `main` because their current implementation did not meet the same Architecture v2/canonical-authority standard. Their old state is preserved on `main.2`.

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

Forbidden in the v1 main baseline:

- wildcard format dispatcher;
- broad recognition-only registry;
- Java format parsers;
- renderer-owned binary parsers;
- legacy `DecodeResult -> Mesh -> RenderScene` bridge;
- archived HITS/TXT/DCA/PAC/PNST/etc. module translation units in the main build.

## Accepted format contracts

### MOD

Canonical `dmc-rengine-cpp` reader with Architecture v2 projection into geometry, hierarchy, skin/weight and texture-slot presentation contracts.

### SCM

Canonical `dmc-rengine-cpp` reader with Architecture v2 projection into scene hierarchy, transforms, geometry and texture-slot presentation contracts.

### DDS

Bounded DMC3 DXT1/DXT5 reader with strict mip/payload validation and generic RGBA image preview.

### PTX

Bounded texture-bundle reader with descriptor validation and generic DDS child resources. Child previews and parent navigation use the same generic session/UI contracts as top-level resources.

## Completion gate

A v1 core candidate is acceptable only when CI proves:

1. registry size is exactly four;
2. MOD/SCM/DDS/PTX modules are present;
3. removed families do not resolve to modules;
4. MOD and SCM execute end-to-end through the pipeline and publish valid `RenderScene`/inspection state;
5. DDS accepts valid DXT1/DXT5 and rejects malformed/overflow cases;
6. PTX validates children, bounds and padding and publishes a generic DDS child preview;
7. no archived source path is present in the main build tree;
8. Android UI policy remains capability-driven;
9. ARM64 APK builds and contains the four expected module IDs;
10. archived module IDs are absent from `libdmcviewer.so`;
11. explicit DMC MIME exposure is limited to MOD, SCM, DDS and PTX;
12. debug signing remains test-only and release output remains unsigned until production authority is provisioned.

## Device regression

After CI is green, the cleanup build receives a short Samsung pass:

- MOD render;
- SCM render;
- DDS preview;
- PTX gallery;
- PTX -> DDS child preview;
- child `←` parent navigation.

This device pass verifies the cleaned routing surface. It is not an invitation to restore removed formats before they are properly promoted.
