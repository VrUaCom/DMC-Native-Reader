# DMC Native Reader v1.0.0 — Stable Baseline

**Release:** `1.0.0`  
**versionCode:** `20`  
**Production package:** `com.dmcrengine.nativereader`  
**Stable v1 shell:** Android / arm64-v8a  
**Preview shells:** iOS / Windows (not part of the accepted stable binary baseline)  
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

The accepted **stable release claim is Android v1.0.0**. Native iOS and Windows implementations may reuse this same four-format semantic core as previews, but their existence does not retroactively make them part of the accepted Android release evidence.

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
  -> PipelineResult
  -> InspectionDocument / RenderScene / ImagePreview / ChildResource[] / ResourceCapabilities
  -> platform bridge/session
```

Stable Android v1 uses the JNI Session/capability-driven UI. Cross-platform preview work may use `PortableSession`, but it must consume the same `PipelineResult` and must not introduce a second parser authority.

Forbidden in the v1 production baseline and its preview shells:

- wildcard format dispatcher;
- broad recognition-only fallback;
- platform-UI-owned MOD/SCM/DDS/PTX format parsers;
- renderer-owned binary parsers;
- legacy `DecodeResult -> Mesh -> RenderScene` compatibility bridge;
- archived HITS/TXT/DCA/PAC/PNST/etc. module translation units in the production registry;
- private downstream semantic forks of capabilities that belong in DMC Rengine.

## Accepted format contracts

### MOD

Canonical DMC Rengine reader with Architecture v2 projection into geometry, hierarchy/spatial state, skin/weight inspection and texture/material presentation contracts where supported by canonical authority.

### SCM

Canonical DMC Rengine reader with Architecture v2 projection into scene hierarchy, transforms, geometry and texture/material presentation contracts.

### DDS

Bounded DMC3 DXT1/DXT5 reader with strict mip/payload validation and generic image preview.

### PTX

Bounded texture-bundle reader with descriptor validation and generic DDS child resources. Child previews and navigation use the same generic typed contracts as top-level resources.

## Android v1.0.0 release acceptance gate

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

Public-source Android debug builds are intentionally isolated under `com.dmcrengine.nativereader.debug` and do not share the production trust chain.

## Cross-platform preview boundary

The current iOS and Windows work is deliberately outside the accepted stable-binary evidence until separately validated.

### iOS preview

- reuses MOD / SCM / DDS / PTX Architecture v2 core through `PortableSession`;
- retains the historical `ios-unsigned-latest` release line but replaces its old pre-v1 binary/claims only after a successful current build;
- remains an unsigned technical preview until Apple signing/provisioning is defined;
- must pass real-device/corpus acceptance before any stable claim.

### Windows preview

- reuses the same four-format C++20 core through `PortableSession`;
- builds as native Win32/x64;
- remains an unsigned technical preview until build/corpus acceptance and Windows signing/distribution identity are defined.

See [`CROSS_PLATFORM.md`](CROSS_PLATFORM.md).

## License baseline

DMC Native Reader is source-available under the **DMC Native Reader Personal Non-Commercial License 1.0**.

The project permits personal non-commercial use under the exact license terms, prohibits third-party commercial use without separate written authorization, and contains the Capcom Special Grant. Vendored third-party components remain under their own licenses, including the MIT-licensed DMC Rengine slice.

## Stable Android device regression

The accepted practical UI target covers:

- MOD render/inspection;
- SCM render/inspection;
- DDS preview;
- PTX gallery with texture thumbnails;
- PTX -> DDS child preview;
- child -> parent navigation;
- malformed/unsupported input fails closed without stale UI state.

Future format and platform work must preserve this baseline rather than reopening the old broad decoder architecture.
