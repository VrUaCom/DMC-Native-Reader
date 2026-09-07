# Changelog

## 1.0.0-rc1 — Release Candidate 1

**Milestone:** the v1 architecture has completed its first real-device acceptance loop and is frozen for release hardening.

### Android / UX

- capability-driven UI2 toolbar and generic `ResourceUiState` policy;
- generic hierarchy overlay with `RenderFlags`;
- explicit parent-session `←` navigation for nested resources;
- DDS image preview in the shared viewport;
- PTX child-resource gallery with preview-first texture tiles and safe fallback labels;
- PTX child DDS opens through the same Session / Inspector / ImagePreview pipeline;
- application label normalized to `DMC Native Reader`.

### MOD

- canonical MOD parser path only; retired duplicate decoder removed;
- canonical model-space spatial hierarchy and world matrices;
- instance-level spatial authority gate;
- skin weights and influence information exposed to Inspector;
- typed texture slot and legacy GS CLAMP / REGION_REPEAT state promoted from canonical `dmc-rengine-cpp`;
- unresolved bitmap companion mapping remains explicitly unresolved.

### SCM

- canonical scene hierarchy and world matrices exposed through `RenderScene`;
- generic hierarchy overlay device-tested;
- texture binding and legacy GS sampler state exposed in Inspector.

### DDS / PTX

- bounded DXT1/DXT5 base-mip image preview;
- complete mip-chain validation remains structural authority;
- PTX publishes generic child DDS resources rather than inventing a bundle-level primary texture;
- malformed/overflow/trailing-data/sector-span/padding cases fail closed;
- aggregate image-preview memory budget and Android bitmap allocation failures are bounded/fail-closed.

### Device acceptance

Real Samsung acceptance passed for:

- MOD 3D + hierarchy overlay;
- SCM 3D + hierarchy overlay;
- MOD texture-slot / GS-state Inspector;
- SCM texture / GS-state Inspector;
- PTX texture thumbnails;
- PTX -> DDS -> image preview -> parent navigation;
- standalone DDS image preview;
- Android `Open with` / Samsung My Files routing used in the tested flows.

### Release boundary

- versionCode 18 / versionName `1.0.0-rc1`;
- debug builds retain the disposable repository development signer for internal update compatibility;
- Gradle release remains unsigned until an external production signing authority is provisioned;
- a self-contained v1 RC workflow records package/version/signing and APK SHA-256 evidence.

## 1.0.0-debug-baseline — Native Reader v1

**Milestone:** initial architecture/build-out complete; project enters device and real-corpus debugging.

### Architecture

- replaced central format dispatch with `NativeModuleRegistry`;
- 71 known resource families represented by 71 explicit module contracts;
- removed wildcard structural fallback;
- unknown families fail closed;
- module contract owns family identity, format authority, kind, renderability and runner;
- kept shared Model Family implementation for SCM/MOD without reintroducing a shared dispatcher.

### Core modding readers

- MOD structural / renderable reader;
- SCM structural / renderable reader;
- DDS bounded DXT1/DXT5 texture reader;
- PTX bundle reader with DDS-child validation;
- stage TXT lexer / bounded structural text reading;
- `.index` manifest reader with text-vs-PNST/PAC precedence fix.

### Additional promoted readers

- HITS collision;
- DCA structural records;
- LIG/LIG2 structural lighting records;
- PAC / PNST container inspection;
- NBZ top-level inspection boundary.

### Evidence-gated modules

- EFM, MRP and SHW have explicit family adapters without claims of complete semantics;
- SO remains outside the closed v1 semantic-reader set;
- remaining known families use explicit recognition-only contracts rather than a generic parser.

### Android

- system `Open with` / SAF integration;
- OEM/Samsung file-manager routing surface;
- explicit DDS/PTX and DMC format routes;
- read-only file handling;
- stale geometry prevention for non-renderable sessions;
- ARM64 native build.

### Validation

The v1 exact-head build passed:

- host modular-reader regression;
- Android NDK build;
- APK build/integrity checks;
- package/version verification;
- native module-ID verification;
- wildcard-absence check;
- signing verification;
- APK SHA-256 evidence generation.

## Earlier development milestones

The project previously used internal v4-v9 development/test APK lines while format support and architecture were being stabilized. v1 is the first milestone intentionally frozen as the public-debug baseline.
