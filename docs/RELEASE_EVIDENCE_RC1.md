# DMC Native Reader v1.0.0 RC1 — release evidence template

This file defines what must be copied from the final exact-head CI run before RC1 is accepted.

## Exact head

- commit: `PENDING_CI`
- versionCode: `18`
- versionName: `1.0.0-rc1`
- package: `com.dmcrengine.nativereader`
- label: `DMC Native Reader`
- ABI: `arm64-v8a`

## Automated gates

- Android provenance / modular / MOD spatial / UI policy / RenderScene: `PENDING`
- DDS/PTX malformed + child-resource gate: `PENDING`
- v1 hardening / unsigned-release boundary: `PENDING`
- self-contained v1 RC gate: `PENDING`

## Artifact evidence

- debug APK SHA-256: `PENDING`
- debug signer certificate SHA-256: `f483539463f89dd957a8f7c68a3bb75da17450163f2e8767b4c47d5f1899adac` (development-only)
- unsigned release APK SHA-256: `PENDING`
- release signing state: `UNSIGNED_PENDING_PRODUCTION_AUTHORITY`

## Real-device evidence

Prior v1 device acceptance has already confirmed MOD, SCM, PTX and DDS feature paths. RC1 requires one final smoke pass using the exact RC1 debug artifact; record the result here before promotion.

- Samsung RC1 smoke: `PENDING`

## Stable v1.0 blocker

Production signing authority is intentionally not stored in the repository. Stable `1.0.0` distribution remains blocked until a protected production signer is provisioned and the production certificate digest + final APK SHA-256 are recorded.
