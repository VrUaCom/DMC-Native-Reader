# Native Reader 1.0 / versionCode 23: PTX model attachment

Work continues on `feature/dds-ptx-v1-acceptance`, from
`60d1f47bd2555119515ba5a5e7b1722b1197148e` (newer than the handoff).
ReaderCore remains pinned to `1fc62d3484251aa56b4a05415e10e9a7759d8982`.
No Rengine source or dependency revision was changed.

## Changes

- `model_texture_binding` rejects partially assigned texture slots, invalid
  vertex indices and non-finite referenced UV coordinates. Black Widow and
  companion attachment use this same validation authority.
- Host regressions link the same `DMCNativeReader::Core` CMake target as Android.
  This removes CI source-list duplication and the stale DDS/PTX gate that
  omitted TextureSet and the isolated PTX compatibility module.
- The active hardening gate checks release name `1.0` and versionCode `23`.
- `tools/verify_device_apk.py` checks the device APK's identity, stable signer,
  ZIP integrity, arm64 ABI, JNI exports and extracted-library packaging.

Existing UI was inspected: permanent top-left Back; top-right `.PTX` uses the
Android picker and FD; attached styling is derived from native Black Widow
state. No diagnostic-string state checks were introduced.

## Host evidence

Built with GCC 13.3, C++20, assertions enabled. All seven CTest regressions pass:

| Regression | Evidence |
| --- | --- |
| black_widow_state | Native attach/attached flags; rejects partial mapping, NaN UV and out-of-range index |
| core_model_pipeline | Synthetic MOD/SCM canonical parse, UV and RenderScene materialization; native attachable state; SCM slot 0 and MOD slot 5 preserved |
| dds_ptx_v1 | DDS/wrapped DDS/PTX; retail-like DXT1 auxiliary compatibility; four-slot whole-bundle attachment and actual renderer colours from slots 1 and 3 |
| mod_spatial_adapter | Existing spatial adapter regression |
| module_registry | Existing module registration regression |
| ptx_model_texture | Render-side texture slot projection and UV sampling |
| render_scene | Existing neutral scene/render regression |

The four-slot attachment regression verifies unused slots 0 and 2 have no
decoded RGBA images, source bytes remain unchanged, and a missing requested
slot fails without publishing partial textures. Fixtures are synthetic; this
does not claim new retail-corpus or phone acceptance.

Reproduce from an initialized submodule checkout:

```sh
cmake -S app/src/main/cpp -B build/host \
  -DDMC_NATIVE_READER_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host --parallel 2
ctest --test-dir build/host --output-on-failure
gradle --no-daemon :app:assembleDebug
python3 tools/verify_device_apk.py app/build/outputs/apk/debug/app-debug.apk
```

## Built APK evidence

Local Gradle 9.5.0 / full JDK 17 / NDK 28.2.13676358 build completed successfully.
`verify_device_apk.py` passed against the resulting signed debug APK:

- Application ID: `com.dmcrengine.nativereader`
- Version: `1.0`, versionCode `23`; ABI: `arm64-v8a`
- APK SHA-256: `43c1b78ab22bd2b7ec7129fc750dd616905f5400093d870eddc8614fdb217859`
- Signer SHA-256: `f483539463f89dd957a8f7c68a3bb75da17450163f2e8767b4c47d5f1899adac`
- ZIP CRC checks and APK v2 signature verification passed.
- All 18 declared NativeBridge JNI methods have exported native symbols.
- `extractNativeLibs=true`; JNI library is compressed for legacy installation.
- Required native module markers are present.

This is local build evidence, not a claim that GitHub Actions passed.

## Device acceptance — user confirmed, 2026-09-10

After receiving the v23 APK and the MOD/SCM + whole PTX test instructions,
Victor reported: «Все чітко працює, можна виносити в мейн».
This records user-reported v23 device acceptance and explicit authorization to
merge into main. No per-file test log was supplied; individual em000/pl000
results are not independently asserted here.

The tested code is `f715973be9c0ece60705f7c0c75096741647a393`.
The acceptance-record update changes documentation only.

GitHub Actions for that code revision reported failures without executed job
steps (core, DDS/PTX and hardening); the core job had runner_id=0. These are
not successful CI results and provide no compile/test failure evidence.
The merge evidence is the local seven-test pass, verified signed APK and
user-confirmed device acceptance recorded above.
