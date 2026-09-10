# v4 Build Evidence — historical record

> **Historical evidence only.** This file records the v4 standalone-repository migration build and must not be used as the current Native Reader identity, support matrix or routing status. The accepted baseline is Native Reader 1.0 / versionCode 24 on `main`; see `STATUS.md` and `SIZE_AND_MODULES_V24.md`.

This document records the last fully verified v4 APK build produced during migration to the standalone repository.

## Identity at v4

- applicationId: `com.dmcrengine.nativeviewer`
- versionCode: `4`
- versionName: `0.4.0-samsung-routing`
- compileSdk: `36`
- targetSdk: `36`
- minSdk: `26`
- ABI: `arm64-v8a`

The current accepted package is `com.dmcrengine.nativereader`; the old v4 identity below is retained only for provenance.

## Verified v4 APK

- filename: `DMC-Native-Reader-v4-Samsung-routing.apk`
- size: 1,892,507 bytes
- SHA-256: `2f92dc8e8d11774735609aad4964e674dd598ef4b6b464bd2dfb1dea4114f17d`
- ZIP integrity: PASS
- `classes.dex`: present
- `lib/arm64-v8a/libdmcviewer.so`: present

## Signature

- APK Signature Scheme v2: PASS
- APK Signature Scheme v3: PASS
- signer: `CN=DMC Native Reader Test, O=DMC Rengine, C=ES`
- certificate SHA-256: `f483539463f89dd957a8f7c68a3bb75da17450163f2e8767b4c47d5f1899adac`

The key is development/test-only and was retained for install-over compatibility with device-test builds. It is not a production signing authority.

## Compiled manifest evidence at v4

The compiled binary manifest was inspected from the built APK and contained the then-tested ACTION/MIME/URI registration surface, including DMC SCM/MOD custom types plus several broad fallbacks.

Those v4 filters are historical routing evidence. The current product routing/support contract must be read from the current manifest/build and `STATUS.md`, not reconstructed from this list.

## Build provenance

The verified v4 binary was built before standalone repository migration using a temporary `VrUaCom/pocket-gdspace` build branch. The corresponding successful GitHub Actions run was `32989174059`; APK artifact ID `9614044434`, evidence artifact ID `9614045176`.

`VrUaCom/DMC-Native-Reader` is now the canonical Native Reader repository.

## Historical runtime boundary

At v4, package-side registration was proven but the next required evidence was still a physical Samsung route from file manager to Native Reader and native render.

That unresolved statement is **historical**. Later device testing progressed beyond it; accepted v24 confirms that MOD, SCM, DDS and PTX open successfully on the tested Samsung device and PTX model texture application works.
