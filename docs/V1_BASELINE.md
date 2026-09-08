# DMC Native Reader v1.0.0 — Stable Baseline

**Release:** `1.0.0`  
**versionCode:** `20`  
**Production package:** `com.dmcrengine.nativereader`  
**Stable v1 shell:** Android / arm64-v8a  
**Production branch:** `main`  
**Frozen baseline ref:** `baseline/v1.0.0`  
**Central engine/modding foundation:** DMC Rengine C++20

## Baseline decision

The v1 production baseline is deliberately narrow. It contains exactly four promoted format families:

- MOD;
- SCM;
- DDS;
- PTX.

This is an architecture/evidence decision, not a claim that other DMC3 formats are unimportant. Earlier experimental readers are not part of the supported v1 product surface until they meet the same Architecture v2 and canonical-authority standard.

## Product mission

> **Make DMC resources feel like ordinary files.**

Native Reader v1 is a viewing/accessibility product. Its baseline purpose is to let a user open an unfamiliar promoted DMC resource and immediately see the most natural useful representation without requiring binary-format knowledge.

Editing, archive management, replacement and repacking are not part of the v1 Reader contract. Those workflows belong to DMC Rengine-backed authoring/resource tools such as Pocket GDS.

## Required architecture

```text
resource bytes
  -> bounded / DMC Rengine-backed C++20 authority
  -> four-entry NativeModuleRegistry
  -> format module / canonical adapter
  -> InspectionDocument / RenderScene / ImagePreview / ChildResource[] / ResourceCapabilities
  -> generic native Session
  -> Android v1 JNI/capability UI
```

The Android bridge is the stable v1 shell. Future iOS, Windows and Web shells must preserve the same C++20 semantic authority. Web semantics are intended to run through WebAssembly rather than a separate JavaScript/TypeScript parser implementation.

Forbidden in the v1 production baseline:

- wildcard format dispatcher;
- broad recognition-only fallback;
- platform-UI-owned MOD/SCM format parsers;
- renderer-owned binary parsers;
- legacy `DecodeResult -> Mesh -> RenderScene` compatibility bridge;
- archived HITS/TXT/DCA/PAC/PNST/etc. module translation units in the production build;
- private downstream semantic forks of capabilities that belong in DMC Rengine.

## Accepted format contracts

### MOD

Canonical DMC Rengine reader with Architecture v2 projection into geometry, hierarchy/spatial state, skin/weight inspection and texture/material presentation contracts where supported by canonical authority.

### SCM

Canonical DMC Rengine reader with Architecture v2 projection into scene hierarchy, transforms, geometry and texture/material presentation contracts.

### DDS

Bounded DMC3 DXT1/DXT5 reader with strict mip/payload validation and generic image preview.

### PTX

Bounded texture-bundle reader with descriptor validation and generic DDS child resources. Child previews and parent navigation use the same generic Session/UI contracts as top-level resources.

## Release acceptance gate

The accepted v1.0.0 candidate proved:

1. registry size is exactly four;
2. MOD/SCM/DDS/PTX modules are present;
3. retired families do not resolve to production modules;
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
14. release APK SHA-256 and signing evidence are recorded;
15. Samsung/device acceptance passes MOD, SCM, DDS, PTX gallery/child preview and parent navigation.

## Production trust

Pinned production certificate SHA-256:

`2d82bd3e77b2c1882d3f8143fe8760fc4c834aa65fc5e7b1b12082afcb7718d1`

v1.0.0 APK SHA-256:

`a81ef5555ecc67e0213d2f1f6baa351609a659c5898ba8f71f81d2fc2cefc68c`

Canonical release page:

`https://github.com/VrUaCom/DMC-Native-Reader/releases/tag/v1.0.0`

Canonical direct APK path:

`https://github.com/VrUaCom/DMC-Native-Reader/releases/download/v1.0.0/DMC-Native-Reader-v1.0.0.apk`

Public-source debug builds are intentionally isolated under `com.dmcrengine.nativereader.debug` and do not share the production trust chain.

## License baseline

DMC Native Reader is source-available under the **DMC Native Reader Personal Non-Commercial License 1.0**.

The project permits personal non-commercial use under the exact license terms, prohibits third-party commercial use without separate written authorization, and contains the Capcom Special Grant. Vendored third-party components remain under their own licenses, including the MIT-licensed DMC Rengine slice.

## Device regression

The accepted practical UI target covers:

- MOD render/inspection;
- SCM render/inspection;
- DDS preview;
- PTX gallery with texture thumbnails;
- PTX -> DDS child preview;
- child -> parent navigation;
- malformed/unsupported input fails closed without stale UI state.

Future format and platform work must preserve this baseline rather than reopening the old broad decoder architecture.
