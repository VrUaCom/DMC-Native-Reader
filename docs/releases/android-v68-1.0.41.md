# DMC Native Reader for Android — v68 / 1.0.41

Android release of DMC Native Reader promoted to `main`.

- **versionCode:** 68
- **versionName:** 1.0.41
- **ABI:** arm64-v8a
- **minSdk / targetSdk:** 26 / 36
- **Native language:** target-scoped C++23
- **Package:** `com.dmcrengine.nativereader`

## Highlights

This release carries the v60–v68 development line, including:

- expanded player/costume and coat/cloth handling;
- PAC-driven coat constraints and costume capsule support;
- motion/attachment and composite-model improvements;
- faster native rendering and room rasterisation;
- room-backdrop presentation around models;
- expanded touch/gesture controls and follow-camera behavior;
- settings and right-handed view controls;
- DDS/PTX/TM2 texture-resource paths and PTX compatibility work already present in the v68 line.

The Android release workflow builds the APK from the exact tagged commit, runs the repository package-policy tests and APK verifier, then publishes the APK and SHA-256 alongside this release.
