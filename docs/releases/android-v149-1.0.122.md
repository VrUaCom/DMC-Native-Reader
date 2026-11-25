# DMC Native Reader for Android — v149 (1.0.122)

A device-test APK built from the `NR-Luna-v73` source branch.

- **Package:** `com.dmcrengine.nativereader`
- **ABI:** `arm64-v8a`
- **versionCode:** `149`
- **versionName:** `1.0.122`
- **Signing:** the repository's stable debug/device-test signer, so it installs
  over earlier test builds. This is not a production release.

## Since v72 (1.0.45)

- Stages: the base room with five presets, stage configurations (spawn
  points, event and camera zones, lights, fog, placed effects), the game's
  camera at each spawn point, the game folder search for a stage's effect
  bank and configuration.
- Rendering: the GPU renderer straight to the screen, 67 clean-room DMC3
  shaders, the stage's lights (experimental), fog, up to 8K frames.
- Cutscenes: playback with the cutscene camera, sound, subtitles, cloth and
  the movie part; game videos.
- The photo and video camera, ARCore walking in camera mode.
- File health: an orange or red dot on Info and a downloadable report.
- Spider first: Info, the open plan, Settings, the base room's choices and
  the animation strip are decided by the core; a C API for the Apple shells.

See `docs/STATUS.md` for every version.

The APK and its SHA-256 are produced by the repository's Android release
workflow from the commit that adds these notes.
