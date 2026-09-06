# DMC Native Reader

> **A system-integrated native Android file reader for Devil May Cry 3 HD resources.**
>
> Open a DMC3 resource from your file manager, let Android route it to DMC Native Reader, and inspect it through an evidence-aware C++ parser selected by its real resource family.

**DMC Native Reader** is an Android application built to make Devil May Cry 3 HD Collection resource files behave more like first-class files on a modern device.

Instead of treating `.mod`, `.scm`, `.ptx`, `.dds`, stage text, collision data and container files as anonymous binary blobs, the app identifies the resource family and routes the file into an explicit native reader module.

The application integrates with Android's standard file-opening flow (`Open with`, SAF `content://` URIs, OEM file managers such as Samsung My Files) while the actual parsing work happens in a native C++ core.

**It is not a kernel driver or a filesystem replacement.** It is a system-integrated Android file reader that registers supported DMC resource types with the OS and provides native, read-only inspection of those files.

---

## Why this project exists

DMC3 HD resources were never designed to be user-facing documents. They live inside a game-specific ecosystem of archives, model formats, texture bundles, stage configuration, collision structures and runtime-only resource families.

For modding, reverse engineering and preservation work this creates a practical problem: the operating system sees a pile of unknown extensions, while the useful structure is hidden several layers deeper.

DMC Native Reader is an attempt to close that gap.

The long-term idea is simple:

```text
Tap a DMC file
    ↓
Android recognizes that DMC Native Reader can open it
    ↓
Native probe identifies the resource family
    ↓
NativeModuleRegistry selects exactly one explicit reader
    ↓
The reader validates and exposes only structure supported by evidence
    ↓
Renderable formats can be visualized; structural formats can be inspected
```

No wildcard parser pretends to understand everything. Unknown data stays unknown.

---

## Current release line

### Native Reader v1 — Debug Baseline

- `versionCode`: **10**
- `versionName`: **1.0.0-debug-baseline**
- package: `com.dmcrengine.nativereader`
- ABI: `arm64-v8a`
- minimum Android: API 26
- target / compile SDK: 36
- native language: C++17
- Android NDK: 28.2.13676358
- CMake: 3.22.1

**v1 marks the end of the initial architecture/build-out phase and the beginning of device + real-corpus debugging.**

The milestone does **not** mean every known DMC format is fully reversed. It means the Native Reader architecture is fixed, the main modding formats have real readers, recognized families have explicit contracts, and unsupported semantics are no longer hidden behind generic fallbacks.

See [`docs/V1_BASELINE.md`](docs/V1_BASELINE.md) for the frozen milestone contract.

---

## What makes it "native"

There are two different meanings here, and both matter.

### Native Android integration

DMC Native Reader participates in Android's normal document-opening system:

- `Open with` integration;
- Storage Access Framework (`content://`) input;
- file URI fallback where available;
- explicit DMC MIME / extension routes;
- exported `DmcOpenActivity` for OEM file-manager routing;
- dedicated handling for common DMC modding extensions such as `.mod`, `.scm`, `.ptx` and `.dds`.

The goal is that a modder can browse files normally and open a supported DMC resource directly from the system file manager.

### Native C++ reader core

The parser path is native C++ and intentionally modular:

```text
probe
  -> NativeModuleRegistry
      -> explicit NativeModule
          -> family-specific runner
              -> inspection / mesh session
                  -> Android UI
```

There is:

- **no central family `if/else` decoder**;
- **no wildcard structural parser**;
- **no "try SCM/MOD and hope" fallback**;
- **no unknown-family success path**.

Each known family owns a stable module contract: identity, `Format`, module kind, renderability and runner.

---

## Format support

Native Reader v1 contains **71 explicit recognized family contracts**.

That number is intentionally separate from "71 fully decoded formats". Some families have full structural readers, some are partial and evidence-gated, and the rest are explicit recognition modules waiting for enough reverse evidence.

### Core v1 modding formats

| Family | v1 status | What Native Reader currently knows |
|---|---|---|
| **MOD** | ✅ structural / renderable | Corpus-backed model mesh path, model adapter, positions/normals/UV and recovered model-family structure |
| **SCM** | ✅ structural / renderable | Scene/model mesh path, hierarchy/transform adapter and recovered scene-model structure |
| **DDS** | ✅ structural texture reader | DMC3 HD DDS validation, DXT1/DXT5 and bounded full mip-chain validation |
| **PTX** | ✅ structural texture bundle | Texture bundle framing with bounded embedded DDS-child validation |
| **TXT** | ✅ bounded structural text | Stage text lexer and known parser/token boundary; individual command semantics remain partially open |
| **`.index`** | ✅ textual manifest reader | Naming/manifest parsing with PAC/PNST text-vs-binary precedence handling |

### Additional promoted readers

| Family | Status | Purpose |
|---|---|---|
| **HITS** | ✅ structural / renderable | Collision mesh / spatial collision inspection |
| **DCA** | ✅ structural | `0x10` header + bounded `0x410` record envelope |
| **LIG / LIG2** | ✅ structural | Stage-lighting record envelopes |
| **PAC** | ✅ container inspection | Relative-slot container structure |
| **PNST** | ✅ container inspection | Relative-slot container structure with distinct family identity |
| **NBZ** | ✅ top-level inspection boundary | DMC HD resource-volume boundary |

### Partial / evidence-gated families

These are real DMC families with dedicated module identities, but v1 deliberately does not claim complete schemas:

- **EFM** — effect/model family adapter; exact vertex/material/topology binding still open;
- **MRP** — runtime family is confirmed; exact record schema and downstream ownership remain open;
- **SHW** — strong executable + real-payload evidence for shadow-hull geometry, but the guarded product reader is not yet closed;
- **SO** — research exists, but v1 does not claim a completed semantic Native Reader module.

### Recognition-only families

The remaining known resource families have their own explicit `Recognition` modules.

Examples include TIM2, PTZ, MOT variants, MCV, CAM, HID variants, CLT/C1D/TSC, EVE/POS/ITM/STE/EST, audio/bank resources, video/media families, saves, legacy UI resources, EventTbl and SPUMAPDT.

A recognition module can say **"this is a known DMC family"** without pretending that its binary schema has been recovered.

Unknown / unmapped families are rejected.

---

## Evidence-aware by design

This project comes from a reverse-engineering codebase, so "it opens" is not treated as proof that a format is understood.

Native Reader keeps several states separate:

- filename / extension recognition;
- content-confirmed identity;
- structural decoding;
- mesh decoding;
- partial / evidence-gated support;
- recognition-only support.

A module is not allowed to borrow another format's layout simply because the bytes look similar.

In particular:

- incomplete families must not be routed through SCM/MOD;
- unknown offsets are not given invented names;
- unknown bytes are not silently discarded;
- a filename extension is not promoted to semantic authority by itself;
- non-renderable files may not reuse stale geometry from a previously opened resource.

The reverse/evidence authority for DMC3 HD semantics lives in the companion project [`VrUaCom/dmc-rengine-cpp`](https://github.com/VrUaCom/dmc-rengine-cpp).

---

## Android behavior and safety boundary

DMC Native Reader is currently **read-only**.

Important runtime boundaries:

- input is opened read-only;
- native mapping is capped at 512 MiB;
- non-renderable resources open as inspection sessions rather than fake meshes;
- files are not rewritten as part of normal inspection;
- the application does not include Capcom game archives, executable files or proprietary DMC3 assets.

The v1 debug phase is focused on proving real-world routing and parser behavior against legitimate user-supplied game resources.

---

## The architecture milestone

The important v1 result is not only the number of formats.

It is that the reader now has one scalable rule for adding them:

```text
Known resource family
    -> explicit module contract
    -> evidence-appropriate reader
```

This replaces the early experimental architecture where format knowledge could accumulate inside central dispatch code.

Current registry invariants are tested in CI:

- **71 recognized families**;
- **71 explicit registry entries**;
- unknown family resolves to no module;
- no wildcard `formats.generic.structural-inspector` exists;
- promoted reader IDs are physically present in the compiled ARM64 `.so`.

---

## CI and reproducible APK evidence

Every production build runs through GitHub Actions and checks:

1. host C++ modular-reader regression;
2. registry completeness and unknown-family rejection;
3. synthetic valid/invalid cases for promoted readers;
4. Android NDK / ARM64 compilation;
5. APK ZIP, classes and native library integrity;
6. application id and version metadata;
7. compiled module IDs in `libdmcviewer.so`;
8. absence of the removed wildcard inspector;
9. development signing certificate;
10. APK SHA-256 evidence.

The v1 debug-baseline build passed this complete gate before the project entered device-testing phase.

---

## Build from source

Prerequisites:

- JDK 17;
- Android SDK 36;
- Android build-tools 36.0.0;
- NDK 28.2.13676358;
- CMake 3.22.1;
- Gradle 9.5.x.

Build the debug APK:

```bash
gradle --no-daemon :app:assembleDebug
```

Expected output:

```text
app/build/outputs/apk/debug/app-debug.apk
```

---

## Debug phase: what we need from testers

For v1 the most valuable reports are **real files that are recognized incorrectly, rejected unexpectedly, parsed differently from known evidence, or routed incorrectly by Android/OEM file managers**.

Useful test flow:

1. install the v1 debug baseline;
2. open known `.mod` and `.scm` samples and verify mesh sessions;
3. open `.dds` and `.ptx` samples and verify structural texture output;
4. open stage `.txt`, DCA, PAC/PNST and other structural resources;
5. try one recognition-only family and confirm the UI does not fabricate decoded semantics;
6. test `Open with` from Android Files / Samsung My Files;
7. report the exact filename, resource family, visible result and whether the issue is routing, recognition, parsing or rendering.

See [`docs/STATUS.md`](docs/STATUS.md) for the current technical boundary.

---

## Project origin and credits

DMC Native Reader is part of the wider **DMC Rengine** reverse-engineering effort around Devil May Cry 3 HD Collection.

The project is directed as an architecture-first modding/reverse-engineering tool: recover evidence in the canonical C++ project, promote only bounded behavior into product modules, then validate the result on real Android devices and real corpus files.

**Project direction / product architecture:** VrUaCom / Viktor  
**AI-assisted implementation and review:** OpenAI ChatGPT and Anthropic Claude have both contributed to development/review workflows under human project direction and acceptance.

The project is intentionally transparent about that collaboration: AI can accelerate implementation and review, but reverse claims are accepted only when they are backed by code, corpus or executable evidence.

---

## Legal / project disclaimer

This is an independent fan-made reverse-engineering and modding tool.

It is **not affiliated with, endorsed by, or sponsored by Capcom**. Devil May Cry and related names/assets are trademarks and intellectual property of their respective owners.

This repository is intended for interoperability, research, preservation and modding workflows. It does not distribute the game, proprietary game data or Capcom executable binaries.

A repository license has **not yet been selected**. Until a license is explicitly added, publication of the source code does not grant an open-source license by implication.

---

## Public opening status

The codebase has reached the **Native Reader v1 debug baseline** and is being prepared for its first public repository opening.

Public-opening checklist and remaining manual gates are tracked in [`docs/PUBLIC_RELEASE_CHECKLIST.md`](docs/PUBLIC_RELEASE_CHECKLIST.md).
