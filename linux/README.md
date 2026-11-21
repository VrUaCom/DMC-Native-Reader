# DMC Native Reader for Linux

A native Linux desktop application and companion CLI for Devil May Cry game resource formats (PAC archives, MOD models, SCM models, DDS/PTX textures, MOT animations, and stage collision), powered by the portable C++23 core (`DMCNativeReader::Core` / `capi/include/dmc_reader.h`).

This brings native Linux desktop capabilities to full feature parity with macOS, Windows, and Android.

---

## 1. Prerequisites and Installation

The Linux desktop application uses standard **X11 (Xlib / XShm / XPutImage)** for zero-dependency universal Linux compatibility across all distributions, desktop environments, and Xwayland.

### Ubuntu / Debian / Linux Mint / Pop!_OS
```sh
sudo apt update
sudo apt install build-essential cmake libx11-dev libxext-dev
```

### Fedora / RHEL / AlmaLinux / Rocky Linux
```sh
sudo dnf install gcc-c++ cmake libX11-devel libXext-devel
```

### Arch Linux / Manjaro
```sh
sudo pacman -S base-devel cmake libx11 libxext
```

### Optional GUI File Dialog Helpers
The shell integrates with standard Linux desktop file pickers when opening files via the GUI `[ OPEN ]` button:
```sh
# On GNOME / XFCE / MATE / Cinnamon:
sudo apt install zenity

# On KDE Plasma:
sudo apt install kdialog
```
*(If neither is installed, files can be passed via command-line arguments: `dmc_native_reader_linux <path>`)*.

---

## 2. Build and Run

A helper script is provided:

```sh
# Build release binaries in linux/.build/bin:
linux/build-linux.sh

# Build and launch the desktop viewer:
linux/build-linux.sh --run

# Open a specific file directly:
linux/build-linux.sh --run path/to/pl000.pac

# Run tests:
linux/build-linux.sh --test
```

### Manual CMake Build

```sh
cmake -S app/src/main/cpp -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target dmc_native_reader_linux dmc_native_reader_cli -j$(nproc)

# Binaries:
#   build/dmc_native_reader_linux  (Desktop viewer)
#   build/dmc_native_reader_cli    (Headless CLI companion)
```

### Verification & Smoke Testing
Mirroring `tools/emulator_smoke.sh` for Android, Linux includes an automated smoke verification suite:

```sh
# Run automated smoke test over repository assets:
linux/linux_smoke.sh

# Run smoke test on a specific folder with game assets:
linux/linux_smoke.sh path/to/game/files
```

### Desktop Integration & File Associations
Register the desktop launcher and DMC MIME type associations (`.pac`, `.mod`, `.scm`, `.ptx`, `.mot`) for your desktop user without root privileges:

```sh
# Install .desktop launcher and MIME associations:
linux/register-desktop.sh

# Uninstall:
linux/register-desktop.sh --remove
```

### Standalone AppImage Packaging
Build a self-contained, portable `DMC_Native_Reader-x86_64.AppImage` bundle that runs with a double-click on any Linux distribution (Ubuntu, Fedora, Arch, Debian, openSUSE) without installing dependencies:

```sh
# Package release binaries into standalone AppImage:
linux/package-appimage.sh

# Package existing binaries into AppDir and AppImage:
linux/package-appimage.sh --no-build

# Only assemble AppDir structure:
linux/package-appimage.sh --appdir-only
```

The resulting package will be in `dist/DMC_Native_Reader-x86_64.AppImage` (or `dist/DMC_Native_Reader-x86_64.tar.gz` portable archive).
You can run it directly:
```sh
chmod +x dist/DMC_Native_Reader-x86_64.AppImage
./dist/DMC_Native_Reader-x86_64.AppImage path/to/pl000.pac
```
To run the headless CLI companion via AppImage:
```sh
./dist/DMC_Native_Reader-x86_64.AppImage --cli inspect path/to/pl000.pac
```

---

## 3. Desktop Application Features (`dmc_native_reader_linux`)

### Visual Design
- **Near-black theme** matching Android, Windows, and macOS (`#0B0B0E` shell, `#121216` viewport).
- **Header Bar**:
  - `[ < BACK ]`: Navigates up from child resources or nested PAC archives.
  - **Resource Title**: Displays current archive or model name.
  - **File Health Dot**: Live health status (Green = Clean, Orange = Warning/Notice, Red = Problem/Error).
  - `[ KIT ]`: Opens the Character Kit & Attachment panel (when viewing characters like `pl000.pac`).
  - `[ OPEN ]`: Triggers desktop file selection dialog.
- **Bottom Toolbar**:
  - `WIRE`: Toggles wireframe mesh overlay.
  - `BONES`: Toggles skeleton joint hierarchy visualization.
  - `NORMS`: Toggles vertex normal vectors.
  - `BOUNDS`: Toggles object bounding boxes.
  - `UV`: Toggles UV layout wireframe or enters UV gallery.
  - `INFO`: Displays resource information, diagnostics, and file health report.
  - `COLLIS`: Cycles attack hitboxes and stage collision overlays.
  - `RESET`: Resets camera yaw, pitch, zoom, and pan to default front three-quarter view.

### Mouse & Keyboard Interaction
| Key / Mouse Action | Function |
| :--- | :--- |
| **Left Click + Drag** | Rotates camera orbit (matching core `dmc_view_drag` touch math) |
| **Middle / Right Click + Drag** | Pans camera horizontally and vertically (`dmc_view_pan`) |
| **Scroll Wheel Up / Down** | Smooth zoom in / zoom out (`dmc_pinch_zoom`) |
| **`R`** or **`0`** | Resets camera to default orientation |
| **`F`** | Toggles **Fly Mode** (first-person free camera) |
| **`W` / `A` / `S` / `D`** | Moves fly eye (Forward / Left / Backward / Right) |
| **`Arrow Keys`** | Turns fly eye orientation (Look Up / Down / Left / Right) |
| **`Space`** | Toggles animation playback (Play / Pause) |
| **`[`** / **`]`** | Steps animation backward / forward 1 frame |
| **`H`** | Cycles attack hitboxes: Off &rarr; All Attacks &rarr; Attack 0..N &rarr; Off |
| **`I`** | Toggles Info and diagnostics panel |
| **`U`** | Toggles UV layout or gallery |
| **`K`** | Toggles Character Kit & Attachment controls |
| **`O`** | Opens file dialog |
| **`T`** | Toggles Dual Info Layout preview vs diffuse texture / cycles texture format |
| **`Shift + T`** | Triggers **Custom Texture Hot-Swap** file selection dialog (`.png`, `.dds`, `.ptx`) |
| **`Escape`** | Closes active overlay, navigates Back from child resource, or exits app |

### Custom Texture Hot-Swapping (Modding Preview)
Preview custom costumes, altered faces, and modified weapons without repackaging PAC game archives:
- **Direct Drag & Drop**: Drag any `.png`, `.dds`, or `.ptx` file from GNOME Files (Nautilus), KDE Dolphin, or Thunar directly into the viewport with Dante or any enemy model loaded.
- **`[ MOD TEX ]` Button**: Located in the top header bar when a 3D model is active; opens a file picker filtered to texture assets.
- **Automatic Clean-Room Conversion**: Truecolor PNGs (RGB/RGBA 8-bit) are decoded cleanly using built-in RFC 1950/1951 decompressors, re-encoded on-the-fly to DDS (BC7/BC3), and hot-swapped onto the active character model in real time.

### Wayland & Fractional High-DPI Scaling
- **Automatic Wayland Detection**: Detects `$WAYLAND_DISPLAY` and runs seamlessly with Wayland compositors (GNOME Mutter, KDE KWin, Sway, Hyprland).
- **Fractional DPI Scaling**: Resolves blurriness on 125%, 150%, 175%, and 200% displays by detecting `$DMC_SCALE`, `$GDK_SCALE`, `$QT_SCALE_FACTOR`, or X11 `Xft.dpi` resources, automatically scaling the window, buttons, fonts, and framebuffer for razor-sharp rendering.
- **Modern Window Manager Hints**: Sets `_NET_WM_PID`, `_NET_WM_WINDOW_TYPE`, `_NET_WM_NAME`, and `WM_CLASS` (`dmc_native_reader`, `DMCNativeReader`) for perfect taskbar grouping and desktop integration.

### Child Resource Navigation
- For container formats (nested PAC archives, multi-texture PTX, MOD composites), clicking child items opens them as independent sub-sessions.
- Pressing `Escape` or clicking `[ < BACK ]` restores the parent resource and its camera state.

### Character Kit & Weapon Attachments
- Automatically detects accompanying PACs from the same directory based on core character kit metadata (`dmc_character_kit`).
- Assembles weapons (e.g. Rebellion, Ebony & Ivory, Shotgun) and animation banks directly onto the host character model.
- Allows toggling Devil Trigger body replacement (`dt`) or adding the entire kit in one click.

---

## 4. Headless CLI Companion (`dmc_native_reader_cli`)

A portable command-line tool for automated inspection, validation, batch analysis, PNG export, and benchmarking without an X11 server:

```sh
# General inspection summary:
dmc_native_reader_cli inspect <file>

# Specific inspection topics (uv, mesh, hierarchy):
dmc_native_reader_cli inspect <file> uv
dmc_native_reader_cli inspect <file> mesh
dmc_native_reader_cli inspect <file> hierarchy

# JSON report output:
dmc_native_reader_cli report <file> --json output.json

# Recursive directory scan for DMC game assets:
dmc_native_reader_cli workspace-scan <folder> --json scan.json

# Render and export to PNG image:
dmc_native_reader_cli export <file> output.png

# Run 10-second core renderer benchmark:
dmc_native_reader_cli benchmark <file> 10
```

---

## 5. Architecture & Portability

- **Zero-Dependency Software Blitting**: Renders framebuffers directly into X11 TrueColor visuals using `XPutImage` and optional `XShmPutImage` shared memory extensions.
- **Embedded Crisp Font**: Built-in 8x12 pixel font ensures razor-sharp, pixel-perfect text independent of system font packages.
- **Extensible GPU Context**: Includes `HostContext` hooks allowing seamless integration of hardware EGL/GLES contexts.
- **Standard C++23**: Pure standard C++23, clean compilation, and zero `<windows.h>` dependencies.
