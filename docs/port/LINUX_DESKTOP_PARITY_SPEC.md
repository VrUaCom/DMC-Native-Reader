# DMC Native Reader — Linux Desktop Parity & Architecture Specification

**Status:** Target Specification & Architectural Blueprint  
**Reference Parity Target:** Android (v151 / 1.0.124) & macOS (`NR-Luna-v73`)  
**Core Boundary Authority:** `capi/include/dmc_reader.h` (C API v2)  
**Target Executable:** `dmc_native_reader_linux` (`app/src/main/cpp/linux/`)  
**Document Location:** `docs/port/LINUX_DESKTOP_PARITY_SPEC.md`  

---

## 1. Executive Summary & Comparative Platform Analysis

### 1.1 Overview & Motivation
DMC Native Reader has evolved from a mobile-first Android application into a unified cross-platform reverse-engineering viewer and inspector for *Devil May Cry 3: Special Edition* (DMC HD Collection). The platform consolidation roadmap establishes a single C++23 core (`app/src/main/cpp/`) that encapsulates all format parsing, scene materialization, layout computation, rasterization, animation posing, and diagnostic logic.

The boundary between the portable core and platform-specific presentations is strictly governed by the **C API v2** (`capi/include/dmc_reader.h`), covering over 190 functions organized into 10 cohesive subsystem groups.

To achieve true first-class status across desktop operating systems, the Linux platform must reach **100% functional and behavioral parity** with Android (v151) and macOS (`NR-Luna-v73`), eliminating the severe functional regressions and architectural stagnation currently observed in the legacy Windows Win32 shell.

---

### 1.2 Deep Comparative Analysis: macOS Shell vs. Windows Shell vs. Android Reference

| Dimension | macOS SwiftUI Shell (`apple/Sources/DMCNativeReader/`) | Windows Win32 Shell (`app/src/main/cpp/win_shell.cpp`) | Android Native Reference (v151 Reference) | Proposed Linux Desktop Target (`dmc_native_reader_linux`) |
| :--- | :--- | :--- | :--- | :--- |
| **Architectural Pattern** | MVVM with reactive state (`ViewerModel.swift`, `@Published`, `DispatchQueue`). | Monolithic Win32 C++ application with global mutable state `g_state` inside a single `WndProc`. | Android Application Component Lifecycle (`MainActivity`, `DmcRenderView`, custom views). | Reactive Model-View Controller (`LinuxShell` state machine + ImGui immediate presentation). |
| **Core Integration** | Pure **C API v2** (`CDMCReader` module via `dmc_reader.h`). Handles owned with RAII. | **Direct C++ internal coupling** (`dmcresource::Session`, `spider::black_widow`). Bypasses C API. | JNI bridging `app_native.cpp` (actively migrating to thin C API wrapper). | Pure **C API v2** (`dmc_reader.h`) via C++23 RAII handle wrappers (`ReaderSession`). |
| **Windowing & Display** | AppKit (`NSWindow`, `NSApplicationDelegate`) + SwiftUI `WindowGroup`. | Win32 API (`CreateWindowExW`, `WndProc`, custom manual hit-testing). | Android Surface / `ANativeWindow` lifecycle events. | Multi-backend Windowing (GLFW3 / native EGL-X11 / Wayland) supporting fractional DPI. |
| **Rendering Pipeline** | Dual-mode: Metal GPU presentation (`CAMetalLayer`, MSL shaders) + CPU raster blit fallback. | Single-mode: **Strictly CPU Software Rasterizer** blitted via GDI `StretchDIBits`. | Dual-mode: EGL + OpenGL ES 3.0 (67 DMC3 shaders) + CPU software fallback. | Dual-mode: EGL / OpenGL ES 3.0 (GLES3 backend) + Multi-threaded CPU software blit fallback. |
| **File I/O & Dialogs** | `NSOpenPanel`, `NSSavePanel`, native drag-and-drop (`onDrop`). | `GetOpenFileNameW`, `GetSaveFileNameW`, Win32 `WM_DROPFILES`. | Storage Access Framework (SAF), Content URIs, FD passing. | XDG Desktop Portal (`org.freedesktop.portal.FileChooser`), Zenity/KDialog fallback, XDND / Wayland DnD. |
| **Navigation & Archives** | Hierarchical left sidebar (`HSplitView`), breadcrumbs, back-stack (`stack`). | Horizontal tile gallery overlay mode (`child_browser_open`), basic back. | Modal child browser dialog with thumbnail mosaic, back-stack. | Responsive collapsible left archive sidebar + child thumbnail grid gallery + back-stack. |
| **Animation Strip & Playback** | Full interactive animation strip (`dmc_motion_strip_build`), 60 Hz timer, script menus. | **NONE (0% Parity).** No motion strip, no playback, no script buttons. | Horizontal motion strip, scrub bar, card script actions, 60 Hz clock. | Full horizontal animation carousel with MotionScript dropdowns, scrub controls, 60 Hz loop. |
| **Character Kit & Weapons** | Dedicated Kit menu: weapons, motions, Devil Trigger, look variants bar. | **NONE (0% Parity).** No kit discovery, no DT toggle, no look selection. | Companion menu kit items, weapon checkboxes, DT switches, variant dropdown. | Dedicated Character Kit panel, interactive weapon/motion/DT checkboxes, Variant Look bar. |
| **Stage Ops & Rooms** | Base room loading (`dmc_room_load`), spawn spots, BREAK toggle, marker cards. | **NONE (0% Parity).** No room loading, no spawn navigation, no BREAK. | Stage ops menu, room presets, marker cards, spawn navigation, BREAK. | Full Stage Ops: room loader, presets menu, spawn spot navigation, BREAK destruction, marker cards. |
| **Collision Inspection** | Hitbox attack cycle ('H'), stage collision toggle, full collision report sheet. | Primitive bounding inspection text; **no hitbox cycling or stage collision**. | Hitbox attack cycle button, stage collision toggle, colored collision reports. | Hitbox attack cycle ('H'), stage collision wireframe toggle, colored collision report panel. |
| **Audio & Cutscenes** | AVAudioEngine playing 16-bit PCM (`dmc_sound_pcm`) & streaming cutscene audio. | **NONE (0% Parity).** No audio decoding, no streaming, no cutscenes. | AudioTrack PCM playback, cutscene audio streaming, subtitles, vibration. | Embedded `miniaudio` engine (ALSA/Pulse/PipeWire), PCM sound player, cutscene audio streaming. |
| **Photo Camera & DNG** | Viewfinder overlay, interactive dials, AF focus click, focus ring/lock, RAW DNG. | **NONE (0% Parity).** No viewfinder, no dials, no AF, no RAW DNG. | Camera mode viewfinder, exposure/aperture dials, AF lock, DNG exporter. | Full Camera Mode: interactive viewfinder overlay, dial controls, AF focus, polarizer, RAW DNG. |
| **Settings Management** | Dynamic schema evaluation (`dmc_settings_schema`), stored in `UserDefaults`. | Hardcoded 3-item radio menu (Low/Medium/High). Bypasses settings schema. | Dynamic schema settings activity, shared preferences persistence. | Dynamic schema form generated from core schema, stored in `$XDG_CONFIG_HOME/settings.json`. |
| **Crash Diagnostics** | `CrashReports` (`dmc_crash_install`), crash note display, describe-only recovery. | **NONE.** Crashes terminate process without diagnostic note or safe recovery. | Java crash handler + native signal recorder, safe description open. | Native signal handler (`dmc_crash_install`), crash notice dialog, safe description recovery. |

---

### 1.3 Critical Architectural Insights
1. **The Win32 Shell Stagnation:** The Windows shell (`win_shell.cpp`) was frozen in September 2026 (v33 baseline). It contains 2,411 lines of legacy code that bypasses the C API, directly includes internal C++ headers (`dmcresource/resource_session.h`), relies entirely on synchronous GDI blitting (`StretchDIBits`), and lacks over 80% of the functionality present in Android and macOS. It must not serve as the design model for Linux.
2. **The macOS Shell as Behavioral Gold Standard:** The macOS shell (`apple/Sources/DMCNativeReader/`) represents the current reference implementation of C API v2 integration. It strictly delegates all decisions, string generation, layout calculations, and validation to the C++ core via `dmc_reader.h`. The Linux frontend architecture will mirror this clean separation.
3. **The Single-Boundary Rule:** No frontend shell may implement custom DMC3 format parsing, gesture math, or state inference. If a function is needed, it must be invoked through `capi/include/dmc_reader.h`.

---

## 2. Comprehensive Functional Requirements for Linux Parity

```
+----------------------------------------------------------------------------------------------------+
|                                    LINUX DESKTOP PARITY MATRIX                                     |
+----------------------------------------------------------------------------------------------------+
|  [Core C API v2] <---> [Linux Shell Controller] <---> [GLFW3 / X11 / Wayland]                      |
|                                |                                                                   |
|   +----------------------------+-----------------------------+---------------------------------+   |
|   | 3D Viewport & Navigation   | Resource Navigation & Kits  | Animation & Cutscenes           |   |
|   | - Orbit / Pan / Zoom       | - Child Drawer & Gallery    | - 60 FPS Strip & Carousel       |   |
|   | - Inertia Fling / Spin     | - Weapon & DT Attachments   | - MotionScript Dropdowns        |   |
|   | - Twist & Camera Roll      | - Variant Look Selector     | - Cutscene Audio & Subtitles    |   |
|   | - Fly Mode & Walk Eye      | - Composite Parts Matrix    | - AudioTrack PCM (miniaudio)    |   |
|   +----------------------------+-----------------------------+---------------------------------+   |
|   | Stage Ops & Destruction    | Diagnostics & Health        | Camera Pro & Export Tools       |   |
|   | - Base Room Loading (stNNN)| - Visual Health Badge       | - Viewfinder Overlay & Dials    |   |
|   | - Floor Spawn Navigation   | - Health Issue Reports      | - AF Tap & Focus Ring           |   |
|   | - BREAK Set Destruction    | - Diagnostics & Inspection  | - Viewport PNG Export           |   |
|   | - Hitbox Attack Cycling    | - Safe-Mode Crash Recovery  | - RAW DNG Capture with Preview  |   |
|   +----------------------------+-----------------------------+---------------------------------+   |
+----------------------------------------------------------------------------------------------------+
```

### 2.1 Window Lifecycle & Display Server Integration
* **Display Server Parity:** Must operate natively on both **Wayland** (via `wayland-client` / `wayland-egl`) and **X11** (via `Xlib` / `XCB`), adapting automatically based on the running compositor environment (`WAYLAND_DISPLAY` or `DISPLAY`).
* **Graphics Context:**
  - Primary: **EGL + OpenGL ES 3.0** context creation, sharing textures, shaders, and framebuffers with the core GLES3 backend (`android/gles_view_backend.cpp`).
  - Fallback: **Software Raster Blit Mode**. In virtual machines, CI containers, or legacy hardware where GLES3/EGL is unavailable, the core software rasterizer (`dmc_render` writing to RGBA8) must blit into a host texture or software surface without crash.
* **Window Dimensions & Constraints:** Default launch window at 1280×800; minimum window dimension clamped at 640×480 (matching core render minimums). Support seamless resize, maximize, and borderless fullscreen toggle via **F11**.
* **High-DPI / Scale Factor:** Dynamically listen to display scale changes (Wayland fractional scaling protocol and X11 Xft.dpi). Propagate framebuffer dimensions (`pixel_width`, `pixel_height`) and logical UI scale to both the ImGui overlay and `dmc_render`.
* **Drag-and-Drop File Loading:** Implement native file drop handling:
  - X11: XDND protocol (`XdndEnter`, `XdndPosition`, `XdndDrop`, `XdndSelection`).
  - Wayland: `wl_data_device` drag-and-drop listener.
  - Forward dropped file paths directly to `LinuxShell::OpenFile(path)`.
* **Desktop File Dialogs:** Provide non-blocking open and save file dialogs:
  - Primary: FreeDesktop XDG Desktop Portal (`org.freedesktop.portal.FileChooser` via D-Bus).
  - Secondary fallback: `zenity` or `kdialog` process invocation or lightweight in-process fallback (`tinyfiledialogs`).
* **Crash Handling & Context Recorder:**
  - Register POSIX signal handlers (`SIGSEGV`, `SIGBUS`, `SIGFPE`, `SIGILL`, `SIGABRT`) via `dmc_crash_install(path)` to record crash notes to `$XDG_DATA_HOME/dmc-native-reader/crash_native.txt`.
  - Update activity state via `dmc_crash_context("opening filename")` and `dmc_crash_context("viewing filename")`.
  - On restart, inspect crash logs: if the previous run terminated abruptly, display a prominent warning banner and automatically open the offending resource via `dmc_open_description()` (safe description mode) instead of crashing.

---

### 2.2 3D Viewport Navigation & Pointer Gestures (#45 Math Parity)
The Linux shell must delegate all camera gesture calculations to the C++ core mathematical functions (`modules/view_gesture.cpp`), guaranteeing identical mouse and touch feel across platforms.

* **Grab-and-Turn Orbit (`dmc_view_drag`):**
  - Left Mouse Button (LMB) drag: Moves camera yaw and pitch.
  - Sensitivity constant: 0.008 rad/pixel. Pitch clamped strictly to `[-1.55, +1.55]` radians.
* **Inertia Fling & Spin (`dmc_fling`, `dmc_spin_step`):**
  - On LMB release, measure pointer velocity vector $(v_x, v_y)$ in pixels per second.
  - Pass to `dmc_fling(vx, vy, density, &spin)`. If threshold ($> 900 \times \text{density} / 2.5$) is met, activate a 60 Hz inertia decay timer.
  - Update `dmc_spin_step(&view, &spin, dt)` every frame until velocity drops to zero.
* **Pan Camera (`dmc_view_pan`):**
  - Middle Mouse Button (MMB) drag OR `Shift + LMB` drag: Shifts `view.pan_x` and `view.pan_y`.
  - Distance formula: $2.96 / (\text{zoom} \times \min(\text{width}, \text{height}))$.
* **Zoom Control (`dmc_pinch_zoom`):**
  - Mouse Scroll Wheel OR Trackpad Pinch: Updates `view.zoom`.
  - Clamped strictly between `DMC_ZOOM_MIN` ($0.15$) and `DMC_ZOOM_MAX` ($8.0$).
* **Twist & In-Plane Roll (`dmc_view_twist`, `dmc_angle_delta`):**
  - Right Mouse Button (RMB) drag OR `Ctrl + LMB` drag OR Trackpad two-finger rotation:
  - If base room is loaded and visible (`room_shown`), rotates the model's yaw relative to the room floor.
  - Otherwise, rotates the viewport's camera roll angle.
* **Reset Camera:**
  - Shortcut **'0'** OR Viewport Scope Button: Invokes `dmc_view_reset(&view)` for models, or `dmc_stage_start_view()` for stages.
* **Fly Mode & Ground Walk:**
  - Toggle via **'F'** key or fly toolbar button (`dmc_fly_enter`, `dmc_fly_leave`).
  - First-person navigation using standard keys: **W/S** (forward/backward), **A/D** (strafe left/right), **Q/E** or **PageUp/PageDown** (elevation up/down).
  - Arrow keys look around (yaw/pitch).
  - Ground walk mode (`dmc_walk`) locks eye elevation to the room floor collision mesh.
* **Interactive Viewfinder & Photo Camera Mode:**
  - Toggle via **'V'** key (`dmc_camera_mode()`).
  - Overlay 2D viewfinder frame and rule-of-thirds grid lines generated by `dmc_viewfinder()`.
  - Interactive camera bar with live dials: ISO, Shutter Speed, Aperture, Exposure Compensation (`dmc_camera_dials_list()`). Clicking steps dial settings.
  - Keyboard Focus Ring: **','** (step focus near), **'.'** (step focus far).
  - Keyboard Focus Lock: **'K'** (`dmc_camera_focus_lock()`).
  - Keyboard Camera Level: **'L'** (`dmc_camera_level()`).
  - Raycast Tap-to-Focus: Click in camera mode calls `dmc_camera_focus_at()` to acquire subject distance.
* **Raycast Pick & Floor Placement (`dmc_pick`):**
  - Viewport Left Click: Triggers raycast into 3D scene.
  - If a model joint is intersected, display joint ID and semantic bone name.
  - If stage config marker is clicked, display marker ID and trigger marker card action.
  - If stage floor is clicked and a base room is loaded (`place = true`), standing actor is teleported to the clicked floor coordinate (`DMC_PICK_PLACED`).
* **Long-Press / Context Inspection (`dmc_hold`):**
  - Holding mouse button for $\ge 500$ ms at coordinates $(x, y)$ evaluates `dmc_hold()`.
  - Executes shell step outcome: opens joint card, marker card, or starts motion scrub.

---

### 2.3 Resource Loading & Child Archive Navigation
* **Supported Formats:** Open any supported DMC resource directly from disk or memory: PAC, PNST, SCM, MOD, MOT, PTX, DDS, TM2, SND, IPU.
* **Fail-Closed Safe Opening:** Pass raw buffer to `dmc_open(name, bytes, size)`. If rejected (`DMC_OPENED_REJECTED`), display the core's explanatory rejection title string without crashing.
* **Child Navigation Hierarchy:**
  - If `dmc_child_count(session) > 0`, populate the responsive left child file sidebar.
  - Display item index, slot number, and semantic title via `dmc_child_title(session, index)`.
  - Double-clicking or selecting a child invokes `dmc_open_child(session, index)` and pushes the parent session to the back-navigation stack.
* **Child Previews & Thumbnails:**
  - Asynchronously fetch thumbnail RGBA8 textures via `dmc_child_preview_size()` and `dmc_child_preview()`.
  - Render icon previews in the left sidebar or in an expandable mosaic grid gallery.
* **Navigation Stack:**
  - Maintain a standard `std::vector<dmc_session*>` history stack.
  - **Back** button (keyboard **Backspace** or **Alt + LeftArrow**) pops the current session, closes it via `dmc_close()`, and restores the previous parent session.
* **Original / Assembled Source Export:**
  - Check `dmc_source(session, &name, &bytes, &size)`.
  - If true, enable "Save File..." action, allowing the user to export the cleanly assembled PAC or re-encoded PTX back to disk.

---

### 2.4 Character Kit, Weapon Attachments, DT Switches & Looks
* **Companion Kit Discovery:**
  - For character PACs (e.g., `pl000.pac`, `pl001.pac`, `pl005.pac`, `em034.pac`), query `dmc_character_kit(archive_name)`.
  - Parse tab-delimited records: `kind \t filename \t label \t start`.
  - Categories: `weapon` (attached weapon models), `motions` (weapon animation banks), `dt` (Devil Trigger body replacement).
* **Automatic Sibling Detection:**
  - Inspect the folder containing the character file for companion archives matching the kit requirements.
  - If found beside the file, enable attachment toggle checkboxes. If absent, display dimmed label with notice `(filename not beside it)`.
* **Dynamic Character Assembly:**
  - When kit checkboxes are toggled, open companion sources via `dmc_open_source()`.
  - Assemble composite scene via `dmc_assemble(archives, names, count, enemy_variant)`.
* **Variant Looks Bar:**
  - Query `dmc_variant_names(archive_name)` (e.g. Dante Costume 1 / Costume 2, Coatless, Vergil, Sparda).
  - If multiple looks exist, display a top **Variant Bar** above the viewport.
  - Multi-column decomposition: If variant names contain `" · "` delimiters (e.g., `Look · Weapon · DT`), render distinct segmented pickers for each orthogonal component.
* **Composite MOD Assembly & Joint Attachment:**
  - For multi-part models, open each via `dmc_open_source()` and merge with `dmc_compose(parts, names, count)`.
  - Query `dmc_parts_list(session)` to inspect composite part states and node names.
  - Allow user to attach part $N$ to joint $J$ of host part $0$ via `dmc_part_attach()`, or restore to origin via `dmc_part_reset()`.
* **Texture Attachment to Composite Parts:**
  - Swap or bind PTX textures to individual parts or the entire model using `dmc_attach_ptx(session, part_index, name, bytes, size)`.

---

### 2.5 Animation Strip & Playback Controls
* **Animation Strip Carousel:**
  - Call `dmc_motion_strip_build(session, with_library, staged_names, staged_count)`.
  - Layout horizontal scrolling strip below the 3D viewport.
  - Render cards with card stem/name, animation pack slot, library index, and separator dividers.
* **MotionScript Dropdown Menus:**
  - Cards containing scripted actions expose an interactive dropdown menu displaying all attached script buttons (`script_index`, `slot`, `label`, `description`).
* **Playback Loop & Timing:**
  - Dedicated 60.0 Hz playback timer (`tick()`).
  - Read active range via `dmc_motion_range(session, &end_frame, &loop_start)`.
  - Loop calculation: When `motion_frame > end_frame`, cleanly wrap back to `loop_start` according to core looping rules:
    $$\text{frame} = \text{loop\_start} + (\text{frame} - \text{end\_frame}) \pmod{\text{end\_frame} - \text{loop\_start}}$$
* **Playback Controls:**
  - **Spacebar**: Toggle Play / Pause.
  - **Left / Right Arrow**: Step backward / forward by 1 frame (when paused).
  - Interactive scrub slider showing current frame vs total duration.
* **External MOT Loading:**
  - "Load Motion..." toolbar action opens a file picker for external `.mot` or motion `.pac` files.
  - Evaluated via `dmc_load_motion(session, name, bytes, size)`.
* **Class Events:**
  - For enemy and character classes exposing special action events, query `dmc_class_events(session)`.
  - Populate "Events" menu; clicking an entry fires `dmc_trigger_class_event(session, index)`.
* **Cutscene Audio & Subtitle Sync:**
  - Check `dmc_cutscene_audio_format()` and `dmc_cutscene_audio_position(frame)`.
  - Stream synchronized cutscene PCM through the audio output device.
  - Query active subtitles via `dmc_cutscene_subtitle(session, &sub)`. Render 2D centered text with front and outline colors, scaling font size proportionally to viewport height.
* **Cutscene Camera Director Handoff:**
  - Track cutscene camera cuts via `dmc_camera_follows_cuts(session)`.
  - If user begins orbiting or zooming during cutscene playback, call `dmc_cutscene_take_camera()` to smoothly transition from director camera to interactive free camera.

---

### 2.6 Stage Scenes, Rooms, BREAK Destruction & Collision Mesh Inspection
* **Stage Archive Auto-Discovery:**
  - When opening a stage archive (e.g. `st001.pac`), call `dmc_stage_archives(stage_name, &effects, &config)`.
  - Automatically load matching effect bank and stage configuration files (`stNNNcfg.pac`) located in the same directory.
* **Base Room Lifecycle:**
  - "Load Base Room..." action loads any `stNNN` file into the core global room slot via `dmc_room_load()`.
  - Query `dmc_base_spawn_points()` to list all authoring spawn coordinates.
  - "Next Spot" (shortcut **'N'**) cycles through base room floor spots using `dmc_next_base_spot()`.
  - Teleport actor directly to spot index via `dmc_place_at_spawn(index)`.
* **BREAK Destruction Geometry:**
  - If room contains destructible set models (`can_break_room` state flag), display a prominent **Broken** toggle button.
  - Invokes `dmc_room_set_broken(bool)`, triggering core stage geometry substitution.
* **Stage Configuration Marker Cards:**
  - Clicking 3D markers in stage space evaluates `dmc_marker_card(marker_id)`.
  - Displays a detailed metadata card showing marker properties (name/value pairs) and interactive action buttons (e.g., opening referenced sub-archives via `dmc_run_card_action()`).
* **Hitbox & Attack Geometry Inspection:**
  - Shortcut **'H'** cycles through hitbox inspection states: Off $\rightarrow$ All Attacks (-1) $\rightarrow$ Attack ID $0 \rightarrow \dots \rightarrow$ Attack ID $K$.
  - Query attack list via `dmc_collision_attack_ids()`; select attack via `dmc_select_collision_attack()`.
* **Stage Collision Wireframe Inspection:**
  - Toggle stage collision mesh visibility (`dmc_stage_collision_source_count`).
  - Query collision surface categories (floors, walls, ceilings, flags) via `dmc_collision_kinds()`.
  - Display dedicated **Collision Report Panel** via `dmc_collision_report()`, rendering color-coded group badges matching wireframe vertex colors.

---

### 2.7 Texture Viewing & UV Map Gallery
* **Direct Texture Visualization:**
  - When a PTX or DDS texture resource is opened, query dimensions via `dmc_image_preview_size()`.
  - Fetch raw RGBA8 image data using `dmc_image_preview()`.
  - Display texture directly in the viewport with pan, zoom, and pixel-grid inspection, bypassing 3D scene rendering.
* **Dual Preview Toggle (Texture vs Diagnostic Info Image):**
  - For models supporting dual preview (`dmc_has_dual_preview()`), render a segmented toggle button: **[ Texture | Info Image ]**.
  - Toggling calls `dmc_set_info_preview(bool)`, switching the viewport between the model's primary diffuse texture and the core's diagnostic texture layout graphic.
* **UV Map Overlay Gallery:**
  - If session supports UV visualization (`can_show_uv` state flag), enable "UV Maps" toolbar button.
  - Invokes `dmc_open_uv_gallery()`, constructing a dedicated UV session where each texture coordinate layer is projected as a inspectable 2D wireframe child session.
* **Texture Re-encoding Tool:**
  - Access "Re-encode Textures..." dialog.
  - Retrieve supported target formats via `dmc_texture_format_names()` and `dmc_texture_format_labels()` (DXT1, DXT5, BC7, RGBA8, etc.).
  - Checkbox for "Always write DX10 header".
  - Invoke `dmc_reencode_textures(session, format, force_dx10, &detail)` to generate a newly formatted session ready for inspection and export.
* **Custom Texture Hot-Swapping (Modding Preview):**
  - Allows live preview of modified character costumes, weapons, and enemy textures (`.png`, `.dds`, `.ptx`) without repacking game PAC archives.
  - Interactive drag-and-drop (XDND v5 protocol), dedicated `[ MOD TEX ]` header action, and `Shift+T` hotkey.
  - Clean-room PNG truecolor decoder (`linux_png::DecodePngToRgba`) converting RGBA8 into modern BC7/BC3 DDS blocks via `dds_bcn::encode_dds` and attaching via `dmc_attach_ptx()`.

---

### 2.8 Info, Diagnostics & File Health System
* **Persistent Info Panel:**
  - Render an expandable side panel or modal drawer displaying `dmc_info_text(session, title, depth, back_title, staged, count)`.
  - Displays structural metadata, archive slot inventory, vertex/triangle counts, bone hierarchies, and bound textures.
* **Visual File Health Badge:**
  - Query health status via `dmc_file_health(session, filename, &report)`.
  - Return levels:
    - `0` (Green / OK): Valid format, clean headers, standard compression.
    - `1` (Orange / Notice): Tolerated irregularities, lenient decompression fallbacks, missing secondary mipmaps.
    - `2` (Red / Problem): Severely malformed descriptors, unhandled packet structures, truncated tables.
  - Render circular colored badge on the "Info" button. Clicking when health level $> 0$ displays the comprehensive Health Report text.
* **Deep Diagnostics Inspection:**
  - Provide dedicated inspection views:
    - Topic 1: UV Coordinate Layout & Binding Map (`dmc_inspect(session, 1)`).
    - Topic 2: Mesh Part Topology & Material Groups (`dmc_inspect(session, 2)`).
    - Topic 3: Bone Hierarchy & Transform Graph (`dmc_inspect(session, 3)`).
* **Automated Benchmark Mode:**
  - Run 10-second timed benchmark via `dmc_benchmark()`, testing draw throughput and posing overhead, returning an exhaustive text report.

---

### 2.9 Settings Schema & XDG Persistence
* **Dynamic Core Settings Schema:**
  - Query setting definitions via `dmc_settings_schema(describe_device)`.
  - Automatically construct settings UI categorized by sections: *Graphics*, *Shading*, *Lighting*, *Camera*, *Performance*.
  - Controls: Discrete choices (`DMC_SETTING_CHOICE`), continuous sliders (`DMC_SETTING_SLIDER`).
* **Real-time Core Application:**
  - Whenever a setting changes, collect key-value pairs and call `dmc_apply_settings(keys, values, count)`.
  - Core returns `dmc_applied_settings`: updates `render_flags`, `max_side` (clamped between 64 and 8192 px), target frame interval `frame_ms`, animation speed, and shadow rendering toggles.
* **XDG Configuration Storage:**
  - Store user choices in standard JSON format at:
    `$XDG_CONFIG_HOME/dmc-native-reader/settings.json` (fallback: `~/.config/dmc-native-reader/settings.json`).
  - Automatically load on startup and apply before presenting the initial frame.

---

### 2.10 Pro Export Tools
* **High-Resolution PNG Export:**
  - Query export dimensions via `dmc_export_image_size(session, &w, &h)`.
  - Render full uncompressed image via `dmc_render_export(session)`.
  - Encode to PNG using embedded `stb_image_write` and save to user-selected file path.
* **RAW DNG Capture with Motion Blur:**
  - In Photo Camera mode, provide "Take RAW Picture (DNG)..." action.
  - Open target file descriptor and call `dmc_capture_dng(session, width, height, &view, fps, fd, &developed)`.
  - Core generates a 16-bit linear RAW DNG with embedded TIFF metadata, color matrix calibration, and optional developed JPEG preview.
* **Video Recording Pipeline:**
  - Frame sequence capture via `dmc_video_begin()`, `dmc_video_frame_size()`, and `dmc_video_frame()`.
  - Feed RGBA frames to an embedded encoder or output sequential frame sequence.

---

## 3. Linux Native Architecture Blueprint

```
+----------------------------------------------------------------------------------------------------+
|                                 dmc_native_reader_linux ARCHITECTURE                               |
+----------------------------------------------------------------------------------------------------+
|                                                                                                    |
|  [Main Thread]                                                                                     |
|   +--------------------------------------------------------------------------------------------+   |
|   | GLFW3 Event Loop (X11 / Wayland)                                                           |   |
|   |  - Mouse / Touch Gestures (#45 Math)                                                       |   |
|   |  - Keyboard Hotkeys (WASD, Space, F, V, H, N, 0)                                           |   |
|   |  - Drag & Drop (XDND / wl_data_device)                                                     |   |
|   |  - Window Resize / DPI Scaling                                                             |   |
|   +--------------------------------------------------------------------------------------------+   |
|                                |                                                                   |
|                                v                                                                   |
|   +--------------------------------------------------------------------------------------------+   |
|   | Linux UI Presenter (Dear ImGui)                                                            |   |
|   |  - Top Header & Look Variant Bar                                                           |   |
|   |  - Left Archive Navigation Drawer                                                          |   |
|   |  - Center 3D Viewport / PresentSurface                                                     |   |
|   |  - Bottom Animation Strip Carousel                                                         |   |
|   |  - Right Slide-out Sidebar (Info / Health / Cards / Reports / Settings)                    |   |
|   |  - Camera Bar Dials & Viewfinder Overlay                                                   |   |
|   +--------------------------------------------------------------------------------------------+   |
|                                |                                                                   |
|   +----------------------------+-----------------------------+---------------------------------+   |
|   |                            |                             |                                 |   |
|   v                            v                             v                                 v   |
| [LinuxShell State]      [Render Worker]             [Audio Engine]             [Crash & Config]    |
| - RAII Session Tree     - std::jthread              - miniaudio                - POSIX Signals     |
| - Active Motion Cards   - dmc_render()              - VAG PCM Sound            - dmc_crash_install |
| - Kit & Companion Map   - GPU EGL or CPU Blit       - Cutscene Stream          - XDG Base Dir JSON |
| - Settings Schema       - Pbuffer / FBO Swap        - 16-bit Mono/Stereo       - Safe Mode Guard   |
|   +----------------------------+-----------------------------+---------------------------------+   |
|                                |                                                                   |
|                                v                                                                   |
|   +--------------------------------------------------------------------------------------------+   |
|   | C API Boundary (capi/include/dmc_reader.h)                                                 |   |
|   +--------------------------------------------------------------------------------------------+   |
|                                |                                                                   |
|                                v                                                                   |
|   +--------------------------------------------------------------------------------------------+   |
|   | DMCNativeReader::Core (C++23)                                                              |   |
|   +--------------------------------------------------------------------------------------------+   |
+----------------------------------------------------------------------------------------------------+
```

### 3.1 Minimal, Robust Dependency Strategy
To ensure `dmc_native_reader_linux` runs reliably on **any** Linux distribution (Ubuntu, Debian, Fedora, Arch, Alpine, SteamOS/Steam Deck) without brittle dynamic link failures or desktop environment dependencies (avoiding large frameworks like Qt or GTK), the architecture specifies a lean, battle-tested stack:

| Component | Selected Library | Rationale & Characteristics | Distribution Package |
| :--- | :--- | :--- | :--- |
| **Windowing & Input** | **GLFW 3.4+** | Industry standard, lightweight (~300 KB). Dual X11 and Wayland support out-of-the-box. Handles DPI scaling, OpenGL ES / EGL context initialization, mouse/keyboard/trackpad events, clipboard, and drag-and-drop file delivery. Can be dynamically linked or fetched via CMake `FetchContent`. | `libglfw3-dev` / `glfw-devel` / `glfw` |
| **Immediate GUI** | **Dear ImGui (Docking Branch)** | Zero-overhead, header/source integration. Directly renders over EGL/OpenGL ES 3 or software raster buffers. Perfect fit for the near-black dark theme (`#0B0B0E`). No runtime dependencies. | Vendored in `third_party/imgui/` |
| **GPU Rendering** | **EGL + OpenGL ES 3.0** | Matches Android's native GPU backend (`android/gles_view_backend.cpp`). Evaluates all 67 DMC3 shaders natively. Broad support across Intel, AMD (Mesa/RadeonSI), NVIDIA (Proprietary/NVK), and ARM Mali/Adreno SoC devices. | `libegl1-mesa-dev`, `libgles2-mesa-dev` |
| **Audio Engine** | **miniaudio** | Single C header (`miniaudio.h`, MIT-0 / Public Domain). Automatically outputs to PipeWire, PulseAudio, ALSA, or JACK at runtime without requiring shared library linkages. Matches the unified cross-platform sound architecture planned in `docs/PLATFORM_PORT_PLAN.md` §3.5. | Embedded header |
| **File Dialogs** | **nativefiledialog-extended** or **portal** | Native XDG Desktop Portal integration via D-Bus, delegating to the user's native file chooser (GNOME Files, KDE Dolphin) without linking GTK/Qt libraries. Graceful fallback to `zenity`/`tinyfiledialogs`. | Vendored single source |
| **Image Export** | **stb_image_write** | Single C header for lossless PNG encoding of viewport buffers (`dmc_render_export`). | Embedded header |

---

### 3.2 Source Tree Layout
All Linux frontend sources will reside cleanly under `app/src/main/cpp/linux/`, leaving the portable C++23 core untouched:

```
app/src/main/cpp/linux/
├── CMakeLists.txt                  # Subdirectory CMake configuration
├── main.cpp                        # Entry point, CLI args, crash setup, GLFW event loop
├── linux_shell.h                   # Reactive state machine, session RAII, actions controller
├── linux_shell.cpp                 # Implementation of companion actions, kits, stage ops
├── linux_view_backend.h            # EGL/OpenGL ES 3.0 & Software raster surface manager
├── linux_view_backend.cpp          # GPU context creation, shader binding, surface blitting
├── linux_ui.h                      # Dear ImGui interface layout & widgets
├── linux_ui.cpp                    # Viewport, variant bar, animation carousel, side panels
├── linux_audio.h                   # Miniaudio player wrapper
├── linux_audio.cpp                 # Sound effects PCM playback & cutscene audio streamer
├── linux_dialogs.h                 # File open/save portal & desktop dialog wrapper
├── linux_dialogs.cpp               # Desktop portal D-Bus / zenity / fallback execution
├── linux_config.h                  # XDG Base Directory settings persistence
└── linux_config.cpp                # JSON serialization of settings schema
```

---

### 3.3 Concurrency & Threading Model
The Linux architecture implements a non-blocking dual-thread model guaranteeing smooth 60 FPS viewport interaction and UI responsiveness even while parsing massive 2 GB archives:

1. **Main UI Thread:**
   - Runs the GLFW event pump (`glfwPollEvents()`).
   - Gathers mouse, trackpad, and keyboard inputs, forwarding them to `view_gesture` functions.
   - Evaluates Dear ImGui UI layouts and dispatches actions to `LinuxShell`.
   - Advances animation time step ($\Delta t$) at 60 Hz.
2. **Asynchronous Render Worker Thread:**
   - Background worker thread (`std::jthread`) dedicated to heavy scene rasterization.
   - Communicates with Main Thread via a double-buffered frame queue with atomic generation counters (`uint64_t target_generation`, `uint64_t rendered_generation`).
   - When camera parameters, active motion frame, or lighting flags change, Main Thread flags `frame_dirty = true`.
   - Render thread locks the core session mutex (`dmc_render`), executes drawing, and delivers the populated RGBA buffer (or GPU texture ID) to the Main Thread for presentation.
3. **Asynchronous File Loader Worker:**
   - Background thread for `dmc_open()` and `dmc_assemble()`.
   - As established in bug fix **B7** (`docs/port/C_API_MIGRATION.md`), resource parsing runs without holding the global session lock. The main window remains fully responsive and animated while opening large multi-slot archives.

---

### 3.4 Detailed UI & Layout Wireframe
The Linux interface adopts the polished, near-black professional workstation aesthetic established by Android and macOS:

```
+----------------------------------------------------------------------------------------------------+
| DMC Native Reader - [pl000.pac - Dante]                         [-][o][x]                          |
+----------------------------------------------------------------------------------------------------+
| [Back] [Open...] [Reset View] [Load Motion] [Actions v] [Kit v] [Effects v] [Inspect v] [Room v]...|
+----------------------------------------------------------------------------------------------------+
| Variant Look: [ Dante (Default) | Dante (Coatless) ]   Weapon: [ Rebellion | Ebony & Ivory ]       |
+--------------------------------------+-------------------------------------------------------------+
| ARCHIVE CONTENTS (42 slots)          | 3D VIEWPORT (EGL / GLES3 Accelerated)                       |
| [Search files...           ]         |                                                             |
|                                      |                                                             |
| > 000: pl000_model.mod               |                                                             |
|   001: pl000_tex.ptx                 |                      / \                                    |
|   002: dante_mot.mot                 |                     /   \                                   |
|   003: ebony_ivory.mod               |                    |  o  |                                  |
|   004: rebellion.mod                 |                     \---/                                   |
|   005: dante_dt.mod                  |                      | |                                    |
|   ...                                |                      / \                                    |
|                                      |                                                             |
|                                      |                                                             |
|                                      |                                                             |
|                                      |                                                             |
|                                      +-------------------------------------------------------------+
|                                      | ANIMATION STRIP (60 FPS)                                    |
|                                      | [ > ] [ || ] [ |< ] [ >| ] Frame: 45 / 120  [========o====] |
|                                      | [Idle 01] [Walk] [Run] [Stinger v] [High Time v] [Air Hike] |
+--------------------------------------+-------------------------------------------------------------+
| Ready. Joint: 14 (r_hand_joint) | Health: OK (0) | C API v2 | EGL GLES3 60.0 FPS                    |
+----------------------------------------------------------------------------------------------------+
```

---

### 3.5 CMake Build System Integration
The Linux executable will be integrated directly into `app/src/main/cpp/CMakeLists.txt` using the existing modular structure:

```cmake
# ---------------------------------------------------------------------------
# Linux Native Desktop Shell (Target: dmc_native_reader_linux)
# ---------------------------------------------------------------------------
if(UNIX AND NOT APPLE AND NOT ANDROID)
    message(STATUS "Configuring DMC Native Reader for Linux Desktop")

    # Find required system libraries
    find_package(PkgConfig REQUIRED)
    find_package(Threads REQUIRED)
    
    # GLFW3 for windowing, display server integration (X11 & Wayland), and input
    find_package(glfw3 3.3 QUIET)
    if(NOT glfw3_FOUND)
        pkg_check_modules(GLFW REQUIRED glfw3)
    endif()

    # EGL and OpenGL ES 3.0 for hardware-accelerated viewport rendering
    pkg_check_modules(EGL REQUIRED egl)
    pkg_check_modules(GLES3 REQUIRED glesv2)

    set(DMC_LINUX_SOURCES
        linux/main.cpp
        linux/linux_shell.cpp
        linux/linux_view_backend.cpp
        linux/linux_ui.cpp
        linux/linux_audio.cpp
        linux/linux_dialogs.cpp
        linux/linux_config.cpp
        # Vendored immediate GUI
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
        third_party/imgui/backends/imgui_impl_glfw.cpp
        third_party/imgui/backends/imgui_impl_opengl3.cpp
    )

    add_executable(dmc_native_reader_linux
        ${DMC_LINUX_SOURCES}
    )

    dmc_native_reader_require_cpp23(dmc_native_reader_linux PRIVATE)

    target_include_directories(dmc_native_reader_linux PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/capi/include
        ${CMAKE_CURRENT_SOURCE_DIR}/linux
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
        ${GLFW_INCLUDE_DIRS}
        ${EGL_INCLUDE_DIRS}
        ${GLES3_INCLUDE_DIRS}
    )

    target_compile_definitions(dmc_native_reader_linux PRIVATE
        DMC_NATIVE_READER_BUILD_SHA="${DMC_NATIVE_READER_BUILD_SHA}"
        DMC_LINUX_DESKTOP=1
        IMGUI_IMPL_OPENGL_ES3=1
    )

    target_link_libraries(dmc_native_reader_linux PRIVATE
        dmc_reader_capi
        DMCNativeReader::Core
        Threads::Threads
        ${GLFW_LIBRARIES}
        ${EGL_LIBRARIES}
        ${GLES3_LIBRARIES}
        m
        dl
    )

    # Clean packaging metadata
    set_target_properties(dmc_native_reader_linux PROPERTIES
        OUTPUT_NAME "dmc-native-reader"
    )
endif()
```

---

## 4. Implementation Phasing & Acceptance Verification

To guarantee quality and avoid untested drift, implementation will proceed in four verifiable phases:

### Phase 1: Core Lifecycle, Windowing & Display Surface
- Initialize GLFW with both Wayland and X11 detection.
- Establish EGL context with GLES3 profile (and software pbuffer fallback).
- Embed Dear ImGui with custom dark styling (`#0B0B0E`).
- Validate drag-and-drop file delivery and CLI parameter file loading (`./dmc-native-reader path/to/file.pac`).
- **Verification Gate:** Binary opens any MOD/PAC, displays viewport image via `dmc_render`, and cleanly quits without memory leaks (ASan clean).

### Phase 2: Navigation, Gesture Math & Archive Hierarchy
- Implement left collapsible archive sidebar populated via `dmc_child_count()` and `dmc_child_title()`.
- Implement navigation stack (push child, back button pops).
- Wire all pointer gestures to C API math (`dmc_view_drag`, `dmc_pinch_zoom`, `dmc_view_pan`, `dmc_view_twist`, `dmc_fling`, `dmc_spin_step`).
- Implement raycast pick (`dmc_pick`) and joint identifier HUD.
- Implement Fly Mode (`dmc_fly_*`) with keyboard WASD controls.
- **Verification Gate:** Smooth 60 FPS orbit, zoom, pan, and inertia fling matching exact Android/macOS camera behavior across all test files.

### Phase 3: Character Kits, Looks, Animation Strip & Playback
- Construct top Variant Look selector bar (`dmc_variant_names`, multi-column segmented picker).
- Build Character Kit panel (`dmc_character_kit`, sibling archive detection, weapon/DT checkboxes).
- Implement horizontal Animation Strip carousel (`dmc_motion_strip_build`).
- Implement 60 Hz playback clock, frame scrub slider, play/pause, loop range handling (`dmc_motion_range`).
- Wire MotionScript sub-menus and trigger scripted motions.
- Integrate miniaudio engine for VAG sound playback (`dmc_sound_pcm`) and cutscene audio sync.
- Render subtitle overlays (`dmc_cutscene_subtitle`).
- **Verification Gate:** Dante (`pl000.pac`) loads with Rebellion and Ebony & Ivory attached; animations play smoothly with audio synchronization.

### Phase 4: Stage Ops, Collision Inspection, Camera Pro & Export
- Implement Base Room loader (`dmc_room_load`) and floor spot navigation (`dmc_next_base_spot`).
- Implement BREAK destruction toggle (`dmc_room_set_broken`).
- Implement Stage Marker Cards (`dmc_marker_card`, `dmc_run_card_action`).
- Implement Hitbox cycling ('H') and Stage Collision wireframe viewer (`dmc_collision_report`).
- Implement Photo Camera Mode: viewfinder grid (`dmc_viewfinder`), camera dials, AF focus, and RAW DNG exporter (`dmc_capture_dng`).
- Implement Settings modal (`dmc_settings_schema`, `dmc_apply_settings`, XDG JSON file storage).
- Implement crash recorder signal handlers (`dmc_crash_install`).
- **Verification Gate:** Full test suite execution; verified stage navigation on `st100.pac`; successful DNG and PNG image exports.

---

## 5. Architectural Non-Negotiables & Rules of Parity

1. **Strict C API Decoupling:** The Linux shell must never include headers from `modules/` or `spider/` directly. All functionality must pass through `capi/include/dmc_reader.h`.
2. **Handle Lifetime Safety:** Every `dmc_session*` returned by `dmc_open*`, `dmc_assemble`, `dmc_compose`, or `dmc_open_uv_gallery` must be tracked via RAII and freed exactly once via `dmc_close()`.
3. **Core Decides All Strings:** UI buttons, rejection explanations, health notes, inspection text, and diagnostic reports must be retrieved from the core rather than formatted in the shell.
4. **Resolution Range Contract:** Framebuffer rendering must respect the core's bounds: $64 \le \text{side} \le 8192$ pixels.
5. **No Heavyweight Framework Dependencies:** Do not introduce dependencies on GTK, Qt, or desktop-specific daemons. Keep dependencies restricted to GLFW, EGL/GLES, Dear ImGui, and single-header C utilities.
6. **Clean-Room Integrity:** Maintain strict clean-room separation: no proprietary game engine headers, Capcom binary disassemblies, or copyrighted assets may be committed to the repository.

---

## 6. Parity Acceptance & Verification Status

| Milestone / Requirement | Status | Verification Evidence |
|---|---|---|
| **Phase 1: Lifecycle & Windowing** | **100% COMPLETE** | X11 shell, XDND drag-and-drop, dynamic EGL/GLES3 loader with CPU fallback, clean teardown. |
| **Phase 2: Navigation & Gestures** | **100% COMPLETE** | Modal in-engine file browser, child thumbnail gallery, inertial fling (`dmc_fling`), twist roll, WASD fly mode, pick raycast. |
| **Phase 3: Kits, Looks & Motion** | **100% COMPLETE** | Variant Look selector (C), Character Kit panel (K), weapon/DT reassembly, 60 Hz motion carousel (M), multi-backend audio (PipeWire/Pulse/ALSA), subtitle overlays. |
| **Phase 4: Stages, Collision & Pro Export** | **100% COMPLETE** | Stage companion discovery (`stNNN_effect`, `stNNNcfg`), base room floor spots, BREAK destruction toggle, Stage Marker Cards (`dmc_marker_card`), Collision & Hitbox report tab (`dmc_collision_report`), 16-bit linear RAW DNG (`dmc_capture_dng`), XDG settings persistence. |
| **Packaging & Distribution (AppImage / Standalone)** | **100% COMPLETE** | `linux/package-appimage.sh` producing standalone `DMC_Native_Reader-x86_64.AppImage`, `AppRun` dispatcher, desktop file & icon associations (`register-desktop.sh`). |
| **Modding Texture Hot-Swapping** | **100% COMPLETE** | Live PNG/DDS/PTX hot-swapping directly in UI (`[ MOD TEX ]`, `Shift+T`, drag & drop) with clean-room PNG decoder and DDS BC7 encoder. |
| **Wayland & Fractional Scaling** | **100% COMPLETE** | Automatic `$WAYLAND_DISPLAY` detection, fractional scale factor parsing (125%, 150%, 200%), and `WM_CLASS` / `_NET_WM_PID` desktop registration. |
| **Comprehensive PAC Corpus Verification** | **100.00% PASS** | 3,733 / 3,733 game archives read with 0 crashes, 21,816 sub-resources extracted (see [PAC_COMPREHENSIVE_TEST_REPORT.md](PAC_COMPREHENSIVE_TEST_REPORT.md)). |
| **Automated Test Suite** | **100% PASS** | 59 / 59 CTest unit tests passed (8.77s execution time). Linux smoke test suite passed with 0 failures (1,497 indexed assets). |
