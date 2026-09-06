# DMC Native Reader v1 — Baseline Specification

**Milestone:** `1.0.0-debug-baseline`  
**versionCode:** `10`  
**Product repository:** `VrUaCom/DMC-Native-Reader`  
**Canonical reverse/evidence source:** `VrUaCom/dmc-rengine-cpp`  
**Phase transition:** architecture/build-out -> debug and device/corpus validation

## 1. Milestone decision

The project-management/architecture milestone is accepted: the production Native Reader now uses one explicit modular dispatch architecture and covers the primary DMC3 HD modding resource families needed for the first practical field-test cycle.

This closes the **v1 architecture + primary-reader implementation goal**. It does **not** claim complete reverse-engineering of every recognized DMC family or complete semantic understanding of every field in the promoted readers.

## 2. Architecture baseline — closed

Production path:

`probe -> NativeModuleRegistry -> explicit NativeModule -> runner -> inspection/mesh session -> Android UI`

Required v1 properties:

- central family `if/else` decoder: removed;
- legacy SCM/MOD `decode_resource()` dispatcher: removed;
- wildcard structural fallback: removed;
- unknown families: fail closed;
- every recognized catalog family: explicit module contract;
- current registry: 71 recognized families / 71 explicit entries;
- format identity, module id, kind and renderability: owned by the module contract;
- Android input remains read-only.

## 3. Primary v1 modding readers — accepted for debug phase

### MOD

Status: **structural / renderable**.

Current bounded capabilities include model-family mesh decoding, positions, normals, UVs, model adapter data and recovered skin/control handling. Remaining semantic/writer gaps do not block reader field testing.

### SCM

Status: **structural / renderable**.

Current bounded capabilities include model-family mesh decoding plus scene transform/hierarchy adapter behavior. Full material/topology semantics remain a later reverse boundary.

### PTX

Status: **structural texture-bundle reader**.

Current bounded capabilities include bundle/descriptor validation and validation of contained DDS children with size/span coherence.

### DDS

Status: **structural texture reader**.

Current bounded capabilities include DMC3 HD DDS header validation and complete bounded DXT1/DXT5 mip-chain validation.

### TXT

Status: **bounded structural text reader**.

The stage TXT lexer/parser is usable for field testing. Individual script/token semantics remain partially unresolved and must not be presented as fully reversed.

### .index

Status: **bounded textual manifest reader**.

Includes the PNST/PAC first-line identity precedence correction.

## 4. Additional promoted support

The v1 product also contains promoted support for:

- HITS collision reading;
- DCA structural records;
- LIG/LIG2 structural records;
- PAC relative-slot inspection;
- PNST relative-slot inspection;
- NBZ top-level volume inspection boundary.

## 5. Explicitly not closed by v1

These families must not be reported as complete semantic readers merely because they have explicit modules:

- **EFM** — evidence-gated partial family adapter; exact mesh/material/topology bindings remain open;
- **MRP** — evidence-gated partial family adapter; exact record schema/ownership remains open;
- **SHW** — evidence-gated partial family adapter; strong reverse/corpus evidence exists, but the product-level guarded semantic reader is not yet closed;
- **SO** — not part of the completed v1 Native Reader semantic-reader set;
- recognition-only families — explicit identity/inspection contracts only, with semantic decoder TODO where appropriate.

## 6. Definition of v1 completion

The architecture/implementation milestone is complete when CI proves:

1. host C++ modular registry regression passes;
2. 71 recognized families map to 71 explicit registry entries;
3. unknown family resolution fails closed;
4. promoted synthetic valid/invalid fixtures pass;
5. Android NDK ARM64 compilation succeeds;
6. APK is produced with package `com.dmcrengine.nativereader`;
7. APK reports versionCode `10` and versionName `1.0.0-debug-baseline`;
8. promoted module IDs are present in `libdmcviewer.so`;
9. wildcard structural module is absent;
10. canonical development signing certificate verifies;
11. APK SHA-256 receipt is captured.

After those gates, the project enters **debug/field-test phase**.

## 7. Debug/field-test phase

The project manager/device tester validates real corpus files on Android. Minimum first-pass matrix:

- known-good `.mod` — opens and renders expected model geometry;
- known-good `.scm` — opens and renders expected scene/model geometry;
- known-good `.ptx` — opens as texture bundle and reports coherent DDS children;
- known-good `.dds` DXT1/DXT5 — opens with valid structural report;
- known-good stage `.txt` — opens without false binary routing and exposes structural text diagnostics;
- known-good HITS — collision path still renders/inspects correctly;
- representative DCA/LIG/PAC/PNST/NBZ — inspection path remains stable;
- EFM/MRP/SHW — only the evidence-gated partial behavior is expected;
- SO — not used as a v1 pass/fail completion criterion;
- unknown extension/family — rejected;
- opening non-renderable content after renderable content — no stale previous mesh frame;
- Samsung/OEM file routing — record any extension/MIME route that bypasses `DmcOpenActivity`.

## 8. Exit from debug phase

Field defects are recorded against the exact file/hash and module id. Fixes remain format-local wherever possible. A format is promoted beyond the v1 baseline only with new evidence and regression coverage; the v1 baseline itself remains the reference point for architecture and initial practical reader coverage.
