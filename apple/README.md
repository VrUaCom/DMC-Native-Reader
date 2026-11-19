# DMC Native Reader for macOS

A SwiftUI shell over the reader's C++ core, through its C API
(`app/src/main/cpp/capi/include/dmc_reader.h`). The core decides everything
as on Android; this shell reads files, shows what the core draws and keeps the
window.

## Build and run

Needs the Xcode command line tools and CMake:

```sh
xcode-select --install    # once
brew install cmake        # once
apple/build-mac.sh --open
```

The app is `apple/.build/DMC Native Reader.app`, signed for this Mac only.
Everything the build writes stays in `apple/.build`. The first build also
downloads and builds the core's minimal FFmpeg (WMV movies, a few minutes);
`DMC_FFMPEG=OFF apple/build-mac.sh` builds without it.

Built and started on Apple M1 (macOS 15, Swift 6.1, Apple clang 17): the core passes its 56 host
tests there and `apple/build-mac.sh` produces the app without Swift warnings.

## What it does now (0.1)

- Opens MOD, SCM, PAC, PTX, DDS, MOT and the rest of the core's families:
  ⌘O, drag and drop onto the window, or Open With from Finder.
- Shows a texture as its picture and a model, a stage or an assembled PAC as
  its 3D view. Drag turns the camera (a flick keeps it turning), pinch zooms,
  a two-finger rotation turns the view; Reset view. The numbers are the core's
  touch math (`dmc_view_*`, #45), the same as on Android.
- An archive's files (a PAC's slots, a PTX's textures) are listed on the
  left; each opens on its own, Back returns.
- Info is written by the core; the Info button carries the file health dot
  (orange, red) and its report.
- Animations: the core's animation strip under the view (the library's
  motions; a card with MotionScripts lists them in its menu), play / pause,
  looping like Android; Load motion picks a MOT or motion PAC.
- The base room every model stands in (Room menu: load a stNNN file, clear),
  the effects the view can play (Effects menu), and Export PNG (the stage's
  export picture, else the view as it is).
- Long press on the view: the core's answer (a configuration marker's card
  with its rows and buttons, a joint, a note). Room menu → Load stage
  configuration (stNNNcfg.pac) for the opened stage or the base room.
- Cutscenes: their sound streamed from the core's mix (AVAudioEngine, looping
  with the motion), the subtitles where the core lays them out, and the
  cutscene's .ogg when the core asks for it.
- Camera menu: the photo camera's picture as PNG, or RAW as DNG (the core's
  capture), and a video of the view (.mov; the core's frames at its plan's
  size and rate, encoded by AVAssetWriter in H.264 or HEVC). Fly (F): W A S D move, the arrow keys look, the core's fly eye.
- Settings (⌘,): the core's settings schema, the same rows as Android, each
  choice stored and applied at once; a Camera tab holds the photo camera's
  settings (the core's schema). In camera mode the core's viewfinder (frame,
  guide lines, status) is drawn over the view, with its dials below.
- Collision: a character's attack hitboxes (all, each attack, H steps
  through them as the Android button) and a stage's HITS walls and floors,
  with the core's reports grouped by colour.
- Crashes: the core's crash note (dmc_crash_install) and the file being
  opened are kept; the next start shows them, and that file opens once as
  its description (dmc_open_description) instead of closing the reader again.
- UV maps: a model's UV layout as a list of maps, each drawn over its
  texture; a texture with an Info picture switches between the two.
- Actions the core leaves to the shell, as on Android: texture format
  (re-encode, then Save file…), add PACs to a character or MOD parts to a
  model, the parts placed on the host's joints, attach a PTX, the archive's
  own files, a stage's effect bank, files staged beside the model (listed
  in Info), diagnostics.
- A click picks what is under the pointer (a joint, a marker's card) and,
  with the base room shown, stands the actor on the floor there; the Room
  menu steps through spots (N) and spawn points and breaks the room; an
  enemy class's events fire from Events; Inspect shows the UV, mesh and
  hierarchy texts and runs the core's 10-second benchmark.
- A stage brings its own effect bank and configuration from beside it
  (dmc_stage_archives): opening st100.pac also loads st100_effect.pac and
  st100cfg.pac from its folder; a new base room keeps or drops the previous
  stage's ones as the core says (dmc_after_new_room) and finds its own.
- Camera mode (Camera → Viewfinder, V): a click focuses there, , and .
  turn the focus ring, K locks focus, L levels, two fingers turn the
  polarizer (or roll the camera), a dial clicked steps its setting; a
  setting's follow-ups (dmc_camera_follow_up_of) are applied with it.
- Sounds: a file's sounds (a VAG's chunks) from the core's sound menu,
  decoded by the core to PCM and played with AVAudioEngine.
- Actions: the core's companion menu (rows and "Other actions"); a file an
  action opens is shown, Back returns. A stage starts from the game's camera
  at its first spawn point, and Reset view goes back there.

## Not yet

- The view is drawn by the core's CPU renderer; a Metal backend comes next.
