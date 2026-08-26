# v4 Build Evidence

This document records the last fully verified v4 APK build produced during migration to the standalone repository.

## Identity

- applicationId: `com.dmcrengine.nativeviewer`
- versionCode: `4`
- versionName: `0.4.0-samsung-routing`
- compileSdk: `36`
- targetSdk: `36`
- minSdk: `26`
- ABI: `arm64-v8a`

## Verified APK

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

The key is development/test-only and is preserved solely for install-over compatibility with the v4 device-test series.

## Compiled manifest evidence

The compiled binary manifest was inspected from the built APK, not inferred from source XML. It contained:

- `android.intent.action.VIEW`
- `android.intent.action.EDIT`
- `android.intent.action.SEND`
- `android.intent.category.DEFAULT`
- `android.intent.category.BROWSABLE`
- `android.intent.category.OPENABLE`
- `application/vnd.dmc.scm`
- `application/vnd.dmc.mod`
- `application/octet-stream`
- `application/x-mod`
- `application/x-scm`
- `application/mod`
- `application/scm`
- `audio/mod`
- `audio/x-mod`
- `*/*`
- `content` URI scheme
- `file` URI scheme

## Build provenance

The verified v4 binary was built before the standalone repository migration using the temporary `VrUaCom/pocket-gdspace` build branch. The corresponding successful GitHub Actions run was `32989174059`; APK artifact ID `9614044434`, evidence artifact ID `9614045176`.

The source has now been moved to `VrUaCom/DMC-Native-Reader`, which is the canonical repository. Its own `.github/workflows/android.yml` reproduces the same build and verification boundary.

## Remaining runtime acceptance

The compiled package-side routing is proven. The next evidence must come from the physical Samsung device:

`Samsung My Files -> Android resolver -> DMC Native Viewer -> read-only URI/FD -> native SCM/MOD decoder -> non-empty 3D render`.
