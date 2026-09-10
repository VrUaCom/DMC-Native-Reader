# DMC Native Reader v1 — Accepted Baseline

Last updated: 2026-09-10.

**Accepted build:** Native Reader `1.0` / versionCode `24`  
**Production branch:** `main`  
**Accepted v24 code baseline:** `5a69a3cde2cd4af3534ad7056ea55b09f0e91659`  
**Archive/backlog branch:** `main.2` — reference only

Documentation-only commits may advance `main` without changing the accepted v24 code/APK baseline.

## Baseline decision

The accepted v1 production surface is deliberately narrow. It contains only four promoted format families:

- MOD;
- SCM;
- DDS;
- PTX.

This is an architecture/evidence decision, not a claim that other DMC3 resource families are unimportant. Previous wider readers were removed from production `main` because they did not meet the current canonical-authority and Architecture v2 promotion standard. Their historical implementation remains recoverable from `main.2`.

## Required architecture

```text
resource bytes
  -> bounded probe / DMC Rengine ReaderCore
  -> four-entry NativeModuleRegistry
  -> format module / canonical adapter
  -> InspectionDocument / RenderScene / ImagePreview / ChildResource[]
  -> portable DMCNativeReader::Core session/state/render path
  -> thin Android JNI + Java presentation shell
```

Forbidden in the accepted v1 production baseline:

- wildcard family parsing;
- broad recognition-only registry presented as product support;
- Java/Kotlin DMC binary parsers;
- renderer-owned format parsers;
- legacy `DecodeResult -> Mesh -> RenderScene` compatibility bridges;
- duplicated canonical MOD/SCM/DDS/PTX layout logic in UI/product layers;
- invented hierarchy transforms or material semantics;
- Android-only write/repack implementations.

## Accepted format contracts

### MOD

Canonical DMC Rengine structural read -> Architecture v2 projection into geometry, typed inspection, hierarchy evidence, skin weights and canonical texture-slot/legacy GS state. Spatial hierarchy is exposed only when canonical transform authority is valid.

### SCM

Canonical DMC Rengine structural read -> Architecture v2 projection into scene hierarchy, transforms, geometry, typed inspection and texture-slot state.

### DDS

Bounded DXT1/DXT5 read/decode -> generic RGBA `ImagePreview`, with malformed/overflow rejection.

### PTX

Bounded texture-bundle framing -> generic DDS children and preview/gallery sessions. The same native texture path can build a validated `TextureSet` for MOD/SCM companion attachment.

## Accepted v24 behavior

The 2026-09-10 Samsung acceptance confirms the production baseline at the device level:

- MOD opens/renders;
- SCM opens/renders;
- DDS opens/previews;
- PTX opens as a texture gallery with child navigation;
- PTX texture application works for supported model bindings;
- Android reported 2.32 MB installed size after the v24 cleanup.

Build-side evidence also confirms seven passing local/native regressions, verified arm64 APK gates, no Kotlin runtime dependency in the Java-only shell and only the declared 18 public JNI exports.

GitHub-hosted jobs on the tested revision failed before executing steps; therefore v24 acceptance is based on local/native regression, verified APK and physical-device evidence rather than a claim of green hosted CI.

## Promotion rule after v24

A new format or visible feature does not become part of the baseline merely because code exists. Promotion requires:

1. canonical/evidence-backed authority;
2. bounded implementation through existing Architecture v2 contracts;
3. regression coverage;
4. Android/APK verification where relevant;
5. real device/corpus acceptance for visible behavior that cannot be established by host tests alone.

The current v26 UV/focused-inspection work follows this rule and remains a draft candidate until device acceptance closes.
