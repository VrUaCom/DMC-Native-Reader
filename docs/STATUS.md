# DMC Native Reader — Status

## Current milestone

`v0.8.0-format-catalog-inspection`

Canonical repository: `VrUaCom/DMC-Native-Reader`.
Canonical DMC3 reverse/evidence source: `VrUaCom/dmc-rengine-cpp`.

## Proven in source / prior CI

- stable Android applicationId: `com.dmcrengine.nativereader`
- canonical v6+ test signer retained
- exported concrete `DmcOpenActivity` for Samsung/OEM file routing
- typed/untyped `content://` and `file://` routes
- read-only JNI file-descriptor path with 512 MiB mapping cap
- bounded native binary access helpers
- SCM/MOD -> normalized static Mesh
- development corpus: 74 SCM + 90 MOD = 164/164 decoded
- 300,466 vertices and 192,413 triangles materialized in that corpus pass
- CPU 3D rendering, rotate, pinch zoom, reset and wireframe

## v8 integration boundary

v8 adds a central native DMC3 format catalog and explicitly separates:

- `mesh-preview`: native 3D decode is implemented and corpus-backed
- `structural`: bounded format-specific fields/record envelopes are inspected
- `recognized`: identity/purpose is known but binary schema is not promoted here
- `runtime-only`: executable runtime family identity is known but standalone Android schema/preview is not claimed
- `research-only`: evidence exists but exact schema/consumer remains incomplete
- `capability-only`: the executable exposes support/capability but Native Reader does not claim a game-specific schema
- `fallback-only`: filename/extension is useful for navigation but does not establish semantic identity

This is intentionally not a claim that every catalogued family is fully reversed.

## Native structural inspection added in v8

- `PAC`: relative-slot topology summary with populated/empty/alias counts and offset validation
- `PNST`: same bounded relative-slot structural summary, kept distinct from PAC
- `HITS`: collision grid dimensions, triangle count and section-relative offsets
- `DCA`: `0x10` header and `0x410` record envelope
- `LIG2`: `0x20` header and `0x30` record envelope
- `DDS`: basic width/height when DDS content is confirmed
- `NBZ`: extension/name identity plus ZIP-prefix observation; no binary AFS claim is inferred

SCM/MOD remain the only formats sent through the current mesh decoder.

## Runtime/model-family recognition

The v8 catalog incorporates the current canonical executable boundary for the primary model/render families:

- SCM — mesh-bearing stage/scene model
- MOD — mesh-bearing actor/object model
- EFM — mesh-bearing effect model, but exact real-payload stream semantic binding remains open
- MRP — render-side companion; standalone mesh ownership is not proven
- SHW — shadow geometry/topology companion; not promoted to a self-contained textured mesh
- MCV — runtime-recognized motion/control family; exact field schema remains open

Recognition never routes these incomplete families through the SCM/MOD decoder.

## HITS / HITS$ correction

Historical `HITS$` is rejected.

Canonical EXE reverse for SHA-256
`e454272ed0fb0247fcbcf300e5d55d7a3e96d50b89b9ffaff81bb978dcbdd082`
records:

- registry-content probe `0x1402DB1F0`: `MOD`, `EFM`, `SCM`, `MRP`, `SHW`
- family-mask probe `0x1402FD650`: `MOD `, `EFM `, `SCM `, `MRP `, `MCV `, `SHW `
- bounded ASCII census: zero `HITS` occurrences in the canonical EXE

The separate collision parser has data/corpus evidence for a four-byte `HITS` payload signature. Therefore v8 uses `HITS` only as a structural data identity and does not describe it as an EXE registry tag.

CI contains a guard so `HITS$` cannot silently reappear in Android routing.

## Android routing policy

Extension-specific OEM routes are expanded for distinctive DMC families such as NBZ/PAC/PNST, SCM/MOD, HITS/DCA, PTX/TM2/PTZ, EFM/MRP/SHW, MOT/MCV/HID/CLT/C1D, stage placement/effect formats and legacy DMC media/bank extensions.

Generic extensions such as `.bin`, `.txt`, `.sav` and common media are not claimed via the distinctive-extension handler because doing so would make Native Reader advertise itself for unrelated user files. They remain openable from the app picker or generic provider route.

## Still not claimed as fully reversed

- full SCM `triCmd` opcode semantics
- full textures/material system
- exact MOD skeletal skinning semantics
- EFM real-payload stream-to-shader semantic binding
- MRP exact fields/downstream owner
- SHW exact ownership/linkage of external spatial pool
- MCV exact field semantics
- CLT/C1D exact schemas
- HID/TSC exact schemas
- several stage/effect/audio-bank research-only families
- binary AFS writer/reader identity from the logical `GData.afs/` namespace

## v8 acceptance boundary

A v8 promotion is accepted only if:

1. Android/NDK CI compiles the branch successfully.
2. APK contains `classes.dex` and `lib/arm64-v8a/libdmcviewer.so`.
3. compiled package is `com.dmcrengine.nativereader` versionCode `8`.
4. compiled versionName is `0.8.0-format-catalog-inspection`.
5. concrete `DmcOpenActivity` remains exported and the legacy alias does not return.
6. representative PAC/PNST/HITS/DCA/EFM/MRP/SHW MIME and extension routes are present.
7. rejected `HITS$` is absent.
8. APK signature matches the canonical v6+ development signer.
9. physical Samsung test proves v7 -> v8 install/update and real file-open behavior.
10. SCM/MOD still render non-empty real geometry, while non-mesh resources show inspection information and never a stale mesh frame.

## Current device-test boundary

After CI is green, the next physical-device pass is:

1. Install the v8 APK over canonical v7.
2. Launch Native Reader directly and confirm the v0.8 status line.
3. Open a known real SCM and MOD and confirm 3D preview/regression behavior.
4. Open representative non-mesh resources (prefer PAC/PNST, HITS, DCA, EFM/MRP/SHW where real payloads are available).
5. Confirm the resource family/support/evidence line is correct.
6. Confirm structural formats show bounded metadata rather than `Rejected by native reader`.
7. Confirm non-mesh resources do not display the previous SCM/MOD frame.
8. Record Samsung My Files intent/provider diagnostics for any extension that still bypasses the handler.
