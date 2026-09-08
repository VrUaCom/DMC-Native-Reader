# Changelog

## 1.1.0-dev — Canonical ReaderCore / texture architecture

**Status:** active development line after the released 1.0.0 baseline.

### Architecture

- Native Reader now consumes the canonical DMC Rengine reader slice instead of maintaining duplicate format knowledge in the Android product;
- introduced the `DMCRengine::ReaderCore` link boundary for the minimal cross-platform C++20 read-side used by product shells;
- pinned `dmc-rengine-cpp` as the canonical source authority rather than manually copying and enumerating parser implementation files;
- kept Android-specific work as projection into generic `InspectionDocument`, `ChildResource`, `ImagePreview`, and render contracts;
- preserved the rule that format algorithms belong to DMC Rengine while orchestration belongs to Spider and platform presentation belongs to Native Reader.

### DDS / PTX

- standalone bounded DXT1/DXT5 DDS parsing and base-mip RGBA8 preview now use the shared Rengine `codecs::dds_bc` codec;
- standalone DDS may use a bounded partial mip chain without weakening the strict DMC3 authoring/evidence profile;
- descriptor-backed DDS and PTX texture bundles use the canonical `TextureSlotFramingParser`;
- removed Native Reader-local `formats/dds.cpp/.h` parser/decoder duplication;
- removed raw app-side `0x70`, `+0x38`, and `+0x64` descriptor parsing from the texture module;
- PTX children continue to project through the generic child-resource and image-preview contracts.

### Build / validation

- version line advanced to `versionCode 21` / `versionName 1.1.0-dev` while keeping the existing package identity;
- Rengine upstream build for the portable DDS codec and ReaderCore slice is green;
- Native Reader PR validation remains gated by GitHub-hosted runner availability before promotion from draft.

### Next

- finish Native Reader host + Android gates on the canonical ReaderCore path;
- genericize the Spider C++20 execution kernel without moving DDS/PTX/MOD/SCM algorithms into Spider;
- connect the proven generic Spider execution layer to the portable session boundary;
- keep future Android/iOS/Windows/Web shells on the same canonical ReaderCore contracts.

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
