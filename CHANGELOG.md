# Changelog

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
