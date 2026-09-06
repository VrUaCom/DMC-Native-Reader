# DMC Native Reader — Status

## Current milestone

`1.0.0-debug-baseline` / versionCode `10`

Canonical product repository: `VrUaCom/DMC-Native-Reader`.
Canonical DMC3 reverse/evidence source: `VrUaCom/dmc-rengine-cpp`.

The initial architecture/build-out milestone is accepted. The project is entering the **debug and device/corpus validation phase**. See `V1_BASELINE.md` for the fixed milestone contract.

## Architecture status

Production path:

`probe -> NativeModuleRegistry -> explicit NativeModule -> runner -> inspection/mesh session -> Android UI`

Closed architecture boundaries:

- central family `if/else` decode dispatcher: **removed**;
- legacy `decode_resource()` SCM/MOD dispatcher: **removed**;
- wildcard `formats.generic.structural-inspector`: **removed**;
- unknown family fallback: **removed**;
- module owns id/format/kind/renderability: **implemented**;
- runner receives owning module contract: **implemented**.

SCM and MOD intentionally share the recovered Model Family mesh implementation while retaining separate family adapters. Shared implementation is not a shared dispatcher.

## Registry coverage

Current recognized unique families: **71**.
Current explicit registry entries: **71**.

Breakdown:

- promoted semantic/structural/partial modules for the current product readers;
- explicit evidence-gated recognition modules for the remaining known families;
- 0 wildcard modules.

The host regression asserts the registry count and checks that an unknown family resolves to no module.

## v1 primary reader milestone

### Structural/renderable

- SCM — corpus-backed mesh decode + scene transform adapter;
- MOD — corpus-backed mesh decode + model adapter;
- HITS — collision mesh decode.

### Text

- stage TXT lexer / bounded structural reader;
- `.index` textual manifest reader, including PNST/PAC first-line precedence correction.

### Texture

- DDS — bounded complete DXT1/DXT5 full mip chain validation;
- PTX — texture bundle parsing with bounded DDS child validation and descriptor-size coherence.

### Structural/container

- DCA — `0x10` header + integral `0x410` records;
- LIG / LIG2 — `0x20` header + integral `0x30` record envelope;
- PAC / PNST — relative-slot header/table inspection;
- NBZ — top-level volume inspection boundary.

### Partial/evidence-gated

- EFM — family adapter, exact vertex/material/topology binding still open;
- MRP — family adapter, exact record schema/downstream owner still open;
- SHW — family adapter, strong reverse/corpus evidence exists but guarded semantic reader closure remains open.

### Outside the v1 closed semantic-reader set

- SO — reverse work exists, but v1 does not claim a completed product semantic reader;
- recognition-only families — explicit identity/inspection contracts, no fabricated schema.

## Evidence and safety boundary

Recognition is not semantic reverse.

Native Reader must not:

- route incomplete families through SCM/MOD;
- fabricate offsets, fields or names;
- display stale geometry for a non-renderable session;
- promote filename-only recognition to content-confirmed identity;
- hide unresolved semantics behind a generic success path.

Native file access remains read-only and mapped input is capped at 512 MiB.

## v1 CI gate

The v1 debug baseline must pass:

1. host C++ compile with all module translation units;
2. 71-entry registry assertion;
3. unknown-family rejection assertion;
4. synthetic DDS/PTX/DCA/LIG2/TXT/.index/PAC/NBZ regressions;
5. explicit recognition-module regression;
6. Android NDK ARM64 compile;
7. APK build and ZIP/native-library integrity;
8. package `com.dmcrengine.nativereader`;
9. versionCode `10` / versionName `1.0.0-debug-baseline`;
10. representative promoted and recognition-only ids present in `libdmcviewer.so`;
11. wildcard module string absent;
12. canonical development signing certificate and APK SHA-256 evidence.

## Device-test boundary — active phase

After v1 CI:

1. install v1 over v9;
2. confirm displayed BuildConfig version is `1.0.0-debug-baseline`;
3. open known SCM/MOD/HITS and verify render regression;
4. open DDS/PTX/DCA/PAC/PNST and verify inspection output;
5. open stage TXT and confirm structural text routing;
6. open EFM/MRP/SHW and verify only evidence-gated partial behavior is exposed;
7. confirm SO is not incorrectly presented as a completed v1 semantic reader;
8. open a recognition-only family and verify a visible TODO semantic module;
9. confirm an unknown extension is rejected;
10. confirm non-renderable resources never show a previous mesh frame;
11. record Samsung My Files routing diagnostics for any extension that bypasses `DmcOpenActivity`.

## Remaining reverse work

Architectural modularization does not mean every DMC format is semantically reversed. Remaining work belongs in individual family modules and should be promoted only when evidence is sufficient.

Major open areas include full SCM triCmd/material semantics, deeper MOD skeletal semantics, SO product-reader promotion, EFM/MRP/SHW unresolved bindings, animation/control schemas, several stage/effect families, audio-bank schemas and deeper NBZ child materialization.

The v1 baseline is now the reference point for regression and field-debug work.