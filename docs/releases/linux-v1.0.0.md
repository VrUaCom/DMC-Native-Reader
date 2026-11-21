# DMC Native Reader for Linux — v1.0.0 (x86_64)

The initial standalone desktop release of DMC Native Reader for Linux, bringing the Devil May Cry 3 asset viewer and reverse engineering suite to 100% feature parity with macOS, Windows, and Android.

- **Target Architecture:** Linux x86_64
- **Language / Core:** Portable Standard C++23 (`DMCNativeReader::Core` / `capi/include/dmc_reader.h`)
- **Format:** Standalone, self-contained AppImage (`DMC_Native_Reader-x86_64.AppImage`) & portable AppDir archive (`DMC_Native_Reader-x86_64.tar.gz`)
- **Compatibility:** Zero external dependencies; runs on any modern Linux distribution (Ubuntu 20.04+, Debian 11+, Fedora 36+, Arch Linux, openSUSE, Pop!_OS, Linux Mint) under X11 or Wayland (XWayland).

---

## Key Features in this Release

### 1. Portable Standalone AppImage
- Bundled into a single executable `DMC_Native_Reader-x86_64.AppImage`.
- Launches by double-clicking without installing libraries or compiling code.
- Includes embedded `AppRun` that transparently handles both the graphical 3D viewer (`dmc_native_reader_linux`) and the headless CLI tool (`--cli`).
- Single-command per-user desktop integration via `linux/register-desktop.sh` (installs `.desktop` menu launcher, high-res app icon, and file type associations for `.pac`, `.mod`, `.scm`, `.ptx`, `.mot`).

### 2. Custom Texture Hot-Swapping for Modding Preview (PNG / DDS / PTX)
- Drag and drop custom textures (`.png`, `.dds`, `.ptx`) directly from GNOME Files (Nautilus), KDE Dolphin, or Thunar onto the 3D viewport to preview modified character costumes on Dante (`pl000.pac`) or enemy models in real time without repackaging game archives.
- Dedicated **`[ MOD TEX ]`** header action button and **`Shift + T`** hotkey with file picker filter.
- Clean-room PNG truecolor decoder (`linux_png::DecodePngToRgba`) using the built-in RFC 1950/1951 decompressor; converts RGBA to modern BC7/BC3 DDS blocks on-the-fly and binds directly to the active model.

### 3. Native Wayland Compositor Awareness & Fractional High-DPI Scaling
- Auto-detects Wayland environments (`$WAYLAND_DISPLAY`) under GNOME Mutter, KDE KWin, Sway, and Hyprland.
- Supports fractional display scaling factors (125%, 150%, 175%, 200%) by querying `$DMC_SCALE`, `$GDK_SCALE`, `$QT_SCALE_FACTOR`, and X11 `Xft.dpi` resources.
- Window size, UI buttons, fonts, and framebuffer automatically scale to native screen resolution, eliminating the compositor blur common with unscaled X11 applications on high-DPI displays.
- Registers standard window manager hints (`WM_CLASS="dmc_native_reader"`, `_NET_WM_PID`, `_NET_WM_WINDOW_TYPE`, `_NET_WM_NAME`) for accurate taskbar grouping and window identification.

### 4. 100% Comprehensive PAC Corpus Verification
- Tested across **3,733 / 3,733 `.pac` game archives** (2.13 GB corpus across all retail game versions):
  - **100.00% pass rate with 0 engine crashes and 0 read failures**.
  - **21,816** internal sub-resources extracted and verified (5,067 PTX textures, 4,894 MOD meshes, 919 MOT animation banks, 281 HITS collision records, 498 SNDHD/VAG sound banks, rooms, cameras, cutscenes).

### 5. Full Desktop Feature Parity (Phases 1–4)
- **Character Kit & Weapon Attachment:** Auto-discovers sibling weapon archives (Rebellion, Ebony & Ivory, Shotgun) and DT meshes (`dmc_character_kit`), allowing one-click character reassembly.
- **Variant Look Selector:** Multi-state look switcher (`C` key) for character and enemy material variants.
- **60 Hz Motion Playback & Sound Sync:** Real-time animation timeline with scrub slider, frame stepping (`[` / `]`), play/pause (`Space`), and multi-backend audio (PipeWire, PulseAudio, ALSA).
- **Stage Scenes, Rooms & Destruction:** Base room loading, authoring floor spot navigation (`N` key), and BREAK destruction geometry substitution (`B` key).
- **Stage Marker Cards:** Interactive 3D trigger cards with metadata parameters and actionable sub-archive loading buttons (`dmc_marker_card`).
- **Hitbox & Collision Diagnostics:** Wireframe attack hitbox cycling (`H` key) and stage collision surface classification (floors, walls, ceilings).
- **Photo Camera Mode & 16-Bit RAW DNG:** Interactive viewfinder with AF autofocus, camera dials, depth-of-field, and 16-bit linear RAW DNG capture (`dmc_capture_dng`).
- **Dynamic EGL/GLES Context Loader:** Runtime `dlopen` of hardware acceleration libraries with automatic CPU software raster fallback.

### 6. Headless CLI Companion (`dmc_native_reader_cli`)
- Fully portable CLI for automated server, CI, and batch inspection tasks:
  - `inspect <file> [topic]`
  - `collision <file> [--attack]`
  - `report <file> --json <out.json>`
  - `workspace-scan <folder> --json <out.json>`
  - `export <file> <out.png>`
  - `export-dng <file> <out.dng>`
  - `reencode <file> <format>`
  - `benchmark <file> [seconds]`

---

## Verification & Quality Assurance
- **Unit Tests:** 59 / 59 tests passed (`ctest --test-dir build_test`) in 2.31s.
- **Smoke Tests:** Automated Linux smoke test suite verified with 0 failures (1,497 indexed assets).
- **Architecture Contract:** Zero Direct3D / Windows API dependencies; strictly standard C++23.
