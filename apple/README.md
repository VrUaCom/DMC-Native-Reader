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
Everything the build writes stays in `apple/.build`.

Built and started on Apple M1 (macOS 15, Swift 6.1, Apple clang 17): the core passes its 55 host
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
- Settings (⌘,): the core's settings schema, the same rows as Android, each
  choice stored and applied at once.
- Actions: the core's companion menu (rows and "Other actions"); a file an
  action opens is shown, Back returns. A stage starts from the game's camera
  at its first spawn point, and Reset view goes back there.

## Not yet

- The view is drawn by the core's CPU renderer; a Metal backend comes next.
- The base room, stage configurations, effects, cutscenes,
  videos (the core is built without FFmpeg here), the photo camera and fly
  mode are on Android only for now; the C API already has all of them.
