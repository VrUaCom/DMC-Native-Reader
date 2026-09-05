# DMC Native Reader — Status

## Current milestone

`v0.9.0-explicit-family-modules`

Canonical product repository: `VrUaCom/DMC-Native-Reader`.
Canonical DMC3 reverse/evidence source: `VrUaCom/dmc-rengine-cpp`.

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

- 16 promoted semantic/structural/partial modules;
- 55 explicit evidence-gated recognition modules;
- 0 wildcard modules.

The host regression asserts the registry count and checks that an unknown family resolves to no module.

## Promoted support

### Mesh/renderable

- SCM — corpus-backed mesh decode + scene transform adapter;
- MOD — corpus-backed mesh decode + model adapter;
- HITS — collision mesh decode.

### Text

- stage TXT lexer;
- `.index` textual manifest reader, including the PNST/PAC first-line precedence correction.

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
- SHW — family adapter, exact triangle/external pool linkage still open.

## Recognition-only coverage

Every remaining catalogued family has its own registry entry with `ModuleKind::Recognition` and a unique module id.

These modules intentionally:

- accept a recognized family as an inspection session;
- preserve probe support/evidence metadata;
- do not claim a recovered schema;
- mark the semantic decoder as TODO in the module trace;
- never produce geometry.

Families include PACK, `.lst`, AFS namespace, `.ukn`, `.bin`, TIM2, PTZ, SEF/EFE/EFW, C1D/CLT, MOT variants, MCV, CAM, HID variants, TSC, EVE/POS/ITM/STE/EST, ADX/OGG/VAGp and bank families, SPUMAPDT, video/media capability families, saves, FON/ICO/icon.sys and EventTbl.

## Evidence and safety boundary

Recognition is not semantic reverse.

Native Reader must not:

- route incomplete families through SCM/MOD;
- fabricate offsets, fields or names;
- display stale geometry for a non-renderable session;
- promote filename-only recognition to content-confirmed identity;
- hide unresolved semantics behind a generic success path.

Native file access remains read-only and mapped input is capped at 512 MiB.

## CI evidence

The v9 branch is gated by:

1. host C++ compile with all module translation units;
2. 71-entry registry assertion;
3. unknown-family rejection assertion;
4. synthetic DDS/PTX/DCA/LIG2/TXT/.index/PAC/NBZ regressions;
5. explicit recognition-module regression (`MOT` representative);
6. Android NDK ARM64 compile;
7. APK build and ZIP/native-library integrity;
8. package `com.dmcrengine.nativereader`;
9. versionCode `9` / versionName `0.9.0-explicit-family-modules`;
10. representative promoted and recognition-only ids present in `libdmcviewer.so`;
11. wildcard module string absent;
12. canonical development signing certificate and APK SHA-256 evidence.

## Device-test boundary

After final CI:

1. install v9 over v8;
2. confirm the displayed BuildConfig version is `0.9.0-explicit-family-modules`;
3. open known SCM/MOD/HITS and verify render regression;
4. open DDS/PTX/DCA/PAC/PNST and verify inspection output;
5. open a recognition-only family such as MOT and verify an inspection session with a visible TODO semantic module;
6. confirm an unknown extension is rejected;
7. confirm non-renderable resources never show a previous mesh frame;
8. record Samsung My Files routing diagnostics for any extension that bypasses `DmcOpenActivity`.

## Remaining reverse work

Architectural modularization does not mean every DMC format is semantically reversed. Remaining work belongs in individual family modules and should be promoted only when evidence is sufficient.

Major open areas include full SCM triCmd semantics, full material/texture semantics, exact MOD skeletal skinning, EFM/MRP/SHW unresolved bindings, animation/control schemas, several stage/effect families, audio-bank schemas and deeper NBZ child materialization.
