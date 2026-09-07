# DMC Native Reader

> **A system-integrated native Android file reader for Devil May Cry 3 HD resources.**
>
> Open a DMC3 resource from your file manager, let Android route it to DMC Native Reader, and inspect or preview it through an evidence-aware C++20 module selected by its real resource family.

**DMC Native Reader** is an Android application built to make Devil May Cry 3 HD Collection resource files behave more like first-class files on a modern device.

Instead of treating `.mod`, `.scm`, `.ptx`, `.dds`, stage text, collision data and container files as anonymous binary blobs, the app identifies the resource family and routes it into an explicit native reader module.

The application integrates with Android's normal file-opening flow (`Open with`, SAF `content://` URIs, OEM file managers such as Samsung My Files) while parsing, inspection, geometry projection and image decoding stay in the native C++ core.

**It is not a kernel driver or filesystem replacement.** It is a read-only, system-integrated resource reader.

---

## Current release line

### Native Reader v1.0.0 RC1

- `versionCode`: **18**
- `versionName`: **1.0.0-rc1**
- package: `com.dmcrengine.nativereader`
- application label: `DMC Native Reader`
- ABI: `arm64-v8a`
- minimum Android: API 26
- target / compile SDK: 36
- native language: **C++20**
- Android NDK: 28.2.13676358
- CMake: 3.22.1

RC1 freezes the first stable v1 architecture after real-device acceptance of the primary MOD, SCM, DDS and PTX flows. It does **not** mean every recognized DMC family is semantically complete.

See [`docs/V1_RC1.md`](docs/V1_RC1.md), [`docs/STATUS.md`](docs/STATUS.md) and [`docs/RELEASE_GATES_V1.md`](docs/RELEASE_GATES_V1.md).

---

## Architecture

The production path is intentionally modular:

```text
bytes
  ↓
NativeModuleRegistry
  ↓
explicit NativeModule
  ↓
family-specific canonical/native reader
  ↓
PipelineResult
  ├── InspectionDocument
  ├── RenderScene
  ├── ImagePreview
  ├── ChildResource[]
  └── ResourceCapabilities
  ↓
JNI Session
  ↓
generic Android presentation
```

Important invariants:

- no central family `if/else` decoder;
- no wildcard structural parser;
- no "try SCM/MOD and hope" fallback;
- no unknown-family success path;
- no duplicate MOD/SCM geometry representation;
- no DDS/PTX/MOD/SCM-specific Android viewer classes;
- `InspectionDocument` is inspection authority;
- `RenderScene` is geometry/hierarchy authority;
- `ResourceCapabilities` / `ResourceUiState` decide which UI actions are available;
- `RenderFlags` control overlays;
- nested resources use generic `ChildResource` + Session navigation.

The reverse/evidence authority for DMC3 HD semantics lives in the companion project `VrUaCom/dmc-rengine-cpp`. New confirmed fields are promoted there first, then consumed by Native Reader.

---

## Real-device accepted v1 flows

The current v1 surface has been exercised on a real Samsung Android device using user-supplied DMC3 HD resources.

### MOD

- 3D geometry preview;
- rotation / zoom / wireframe;
- canonical model-space node hierarchy;
- generic hierarchy/bone overlay gated by per-document spatial authority;
- skin weights / influence inspection;
- typed texture slot;
- legacy GS CLAMP / REGION_REPEAT state;
- unresolved bitmap companion mapping stays explicitly unresolved.

### SCM

- 3D geometry preview;
- canonical scene hierarchy / world matrices;
- generic scene-hierarchy overlay;
- texture binding and legacy GS sampler inspection.

### DDS

- bounded DXT1 / DXT5 validation;
- full mip-chain structural validation;
- base-mip image preview in the shared viewport;
- standalone DDS uses the same generic ImagePreview path as nested DDS.

### PTX

- bundle inspection;
- embedded DDS validation;
- preview-first child-resource gallery;
- real DDS texture thumbnails;
- tap child → full DDS preview through an ordinary child Session;
- explicit `←` and Android system Back return to the existing parent PTX Session without reparsing it;
- per-tile fallback label remains available if an image preview cannot be safely materialized.

---

## Format support

Native Reader v1 contains **71 explicit recognized family contracts**. Recognition is deliberately separate from semantic completeness.

### Core v1 modding formats

| Family | v1 status | Current product surface |
|---|---|---|
| **MOD** | ✅ structural / renderable | Geometry, hierarchy, world transforms, skin/weights, UV, texture slot, GS state |
| **SCM** | ✅ structural / renderable | Scene geometry, hierarchy/world transforms, texture binding, GS sampler state |
| **DDS** | ✅ structural / image preview | DXT1/DXT5, mip validation, bounded base-mip preview |
| **PTX** | ✅ structural / child resources | DDS bundle validation, thumbnails, child navigation |
| **TXT** | ✅ bounded structural text | Stage lexer / known parser-token boundary |
| **`.index`** | ✅ textual manifest | Naming/manifest parsing with PAC/PNST precedence handling |

### Additional promoted readers

| Family | Status | Purpose |
|---|---|---|
| **HITS** | ✅ structural / renderable | Collision mesh / spatial collision inspection |
| **DCA** | ✅ structural | Bounded record envelope |
| **LIG / LIG2** | ✅ structural | Stage-lighting record envelopes |
| **PAC** | ✅ container inspection | Relative-slot container structure |
| **PNST** | ✅ container inspection | Relative-slot container structure with distinct family identity |
| **NBZ** | ✅ top-level inspection boundary | DMC HD resource-volume boundary |

### Partial / evidence-gated families

- **EFM** — explicit family adapter; exact format-specific geometry/material bindings remain open;
- **MRP** — family identity confirmed; exact record schema remains open;
- **SHW** — strong reverse/corpus evidence exists, but v1 does not claim full product semantic closure;
- **SO** — reverse work exists, but v1 does not claim a completed semantic product reader.

The remaining known families use explicit recognition-only contracts where semantic evidence is insufficient. Unknown / unmapped families are rejected.

---

## Evidence-aware by design

"It opens" is not treated as proof that a format is understood.

Native Reader keeps separate:

- filename / extension recognition;
- content-confirmed identity;
- structural decoding;
- semantic interpretation;
- render authority;
- partial / evidence-gated support;
- recognition-only support.

The product must not:

- route incomplete families through SCM/MOD;
- invent field names or offsets;
- derive MOD bone positions from mesh vertices;
- invent a MOD bitmap companion when only a texture slot is confirmed;
- display stale geometry for non-renderable sessions;
- promote filename-only recognition to semantic authority;
- hide unresolved semantics behind a generic success path.

---

## Android behavior and safety boundary

DMC Native Reader is currently **read-only**.

Runtime boundaries include:

- input opened read-only;
- native mapped-input cap of 512 MiB;
- bounded image-preview allocation;
- bounded PTX aggregate gallery-preview budget;
- malformed / truncated / overflow DDS/PTX cases fail closed;
- invalid MOD spatial data does not gain hierarchy-overlay authority;
- Java Bitmap allocation failure is handled without crashing the native Session;
- non-renderable resources cannot reuse stale 3D geometry.

The project does not distribute Capcom game archives, proprietary game files or game executable binaries.

---

## CI and release evidence

The normal Android gate checks:

1. canonical vendor provenance;
2. modular-reader regression;
3. MOD spatial/material regression;
4. Java capability/UI policy;
5. RenderScene/overlay regression;
6. Android NDK / ARM64 build;
7. APK identity / version / manifest;
8. native module markers;
9. absence of retired wildcard / duplicate decoder paths;
10. development signer and APK SHA-256 evidence.

DDS/PTX has a dedicated malformed / child-resource gate.

RC1 adds a self-contained release-candidate workflow that repeats the critical native/UI regressions, builds both debug and release variants, verifies `1.0.0-rc1` identity, verifies the development debug signer, confirms the release APK is unsigned, and records SHA-256 evidence.

---

## Signing boundary

The committed `keys/dmc-native-reader-test.jks` is a **disposable development-only signer** used to keep internal debug APKs update-compatible.

It is not a production authority.

The normal Gradle `release` variant is intentionally **unsigned**. Stable public `1.0.0` distribution requires a separate production key stored outside Git history and injected through protected release infrastructure. The production certificate fingerprint and final APK SHA-256 must be recorded before stable promotion.

An unsigned RC release APK proves the production build path; it is not itself an official distributable release.

---

## Build from source

Prerequisites:

- JDK 17;
- Android SDK 36;
- Android build-tools 36.0.0;
- NDK 28.2.13676358;
- CMake 3.22.1;
- Gradle 9.5.x.

Debug APK:

```bash
gradle --no-daemon :app:assembleDebug
```

Unsigned release APK:

```bash
gradle --no-daemon :app:assembleRelease
```

Expected outputs:

```text
app/build/outputs/apk/debug/app-debug.apk
app/build/outputs/apk/release/app-release-unsigned.apk
```

---

## RC testing

The final RC device checklist is in [`docs/RC1_DEVICE_CHECKLIST.md`](docs/RC1_DEVICE_CHECKLIST.md).

The release-candidate focus is regression, not feature expansion. The four primary smoke surfaces are:

1. MOD;
2. SCM;
3. PTX → DDS child flow;
4. standalone DDS.

Any new format promotion belongs after the v1 freeze unless it is required to fix a release-blocking regression.

---

## Project origin and credits

DMC Native Reader is part of the wider **DMC Rengine** reverse-engineering effort around Devil May Cry 3 HD Collection.

The project follows an architecture-first workflow: recover evidence in the canonical C++ project, promote only bounded behavior into product modules, then validate on real devices and real resources.

**Project direction / product architecture:** VrUaCom / Viktor  
**AI-assisted implementation and review:** OpenAI ChatGPT and Anthropic Claude have contributed to development/review workflows under human project direction and acceptance.

AI can accelerate implementation and review, but reverse claims are accepted only when backed by code, corpus or executable evidence.

---

## Legal / project disclaimer

This is an independent fan-made reverse-engineering and modding tool.

It is **not affiliated with, endorsed by, or sponsored by Capcom**. Devil May Cry and related names/assets are trademarks and intellectual property of their respective owners.

This repository is intended for interoperability, research, preservation and modding workflows. It does not distribute the game, proprietary game data or Capcom executable binaries.

A repository license has **not yet been selected**. Until a license is explicitly added, publication of source code does not grant an open-source license by implication.

---

## Public opening status

The codebase is in **v1.0 release-candidate hardening**. Source-repository publication and production-signed APK publication remain separate owner decisions.

Manual public-opening items — license, branch/history/privacy review and production-signing policy — are tracked in [`docs/PUBLIC_RELEASE_CHECKLIST.md`](docs/PUBLIC_RELEASE_CHECKLIST.md).
