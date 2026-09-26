# DMC Native Reader — Status

Last updated: **2026-09-26**.

## Product target

DMC Native Reader currently targets resource data from **Devil May Cry 3: Special Edition** as distributed in **Devil May Cry HD Collection**.

## Accepted `main`

- current `main`: `f4548b2475f74e438dfade4d2e3653674fc81a36`
- Android versionCode / versionName: **68 / 1.0.41**
- Android package: `com.dmcrengine.nativereader`
- Android ABI: `arm64-v8a`
- minSdk / targetSdk: `26 / 36`
- native product language: target-scoped **C++23**
- Android release tag: `android-v68-1.0.41`

The v68 line is merged into and released from `main`.

## Platform status

### Android — current public release

**v68 / 1.0.41**

The current Android release carries the v60–v68 development line, including expanded model/composite handling, animation and motion work, PAC-driven assembly, cloth/coat behavior, room backdrops, improved rendering, gesture controls and the broader native module registry now present in `main`.

See [`releases/android-v68-1.0.41.md`](releases/android-v68-1.0.41.md).

### Windows x64 — technical preview

**v1.0.0 Preview**

The currently published Windows artifact is an earlier technical preview. It provides portable read-only MOD / SCM / DDS / PTX viewing through the shared native architecture.

It is **not** yet a released Windows equivalent of Android v68. Windows parity/update work should be tracked separately and only promoted when a newer Windows artifact has been built, verified and published.

## Current native module registry

Current `main` registers:

- SCM
- MOD
- DDS
- PTX
- EventTbl
- PAC
- MOT
- PNST
- SHW
- TSC
- CLT
- EFM
- motion script
- collision shape
- collision index
- effect bank

A module being registered does not mean every semantic field of that format is fully reverse engineered. Unknown semantics remain explicitly unknown.

## Architecture boundary

```text
resource bytes
  -> bounded probe
  -> NativeModuleRegistry
  -> canonical native / DMC Rengine authority
  -> DMCNativeReader::Core
  -> session / inspection / rendering
  -> Android or Windows shell
```

DMC Native Reader remains **read-only**.

Editing, rebuilding and repacking belong to DMC Rengine / authoring tooling rather than Native Reader.

## Naming policy

Use the official names:

- **Devil May Cry HD Collection** — collection.
- **Devil May Cry 3: Special Edition** — game.
- **DMC3** — shorthand.

Do not use “Devil May Cry 3 HD Collection” as a product title.

## Immediate work

1. keep Android v68 release documentation and source identity synchronized;
2. bring the Windows implementation forward from the public v1.0.0 Preview baseline toward current shared-core parity;
3. keep platform-specific release claims scoped to artifacts that were actually built and published;
4. continue promoting deeper resource behavior only with bounded native parsing, evidence and regressions;
5. keep editing/writing outside Native Reader.
