# DMC Test Reader

This branch builds a standalone Android test application for the modular DMC decoder architecture.

## Android identity

- applicationId: `com.dmcrengine.testreader`
- label: `DMC Test Reader`
- versionCode: `1`
- versionName: `0.1.0-modular-test-reader`

It is intentionally a different Android application from `com.dmcrengine.nativereader`, so both can be installed on the same device.

## Decoder purpose

The app is an experimental reader for comparing reusable decoder-module combinations. It carries the shared modular pipeline from the v9 experiment:

- identity probe
- bounded read guard
- common model-family/envelope modules
- existing SCM/MOD mesh decoder modules
- EFM family adapter and explicit unresolved bindings
- MRP render-family adapter and explicit unresolved bindings
- SHW shadow-family adapter and explicit unresolved bindings

Unresolved modules remain visible as TODO boundaries and must not fabricate geometry.

This test application is not a replacement for DMC Native Reader and must not use the Native Reader Android applicationId.
