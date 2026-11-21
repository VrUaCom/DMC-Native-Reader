// Native Linux Desktop Shell for DMC Native Reader.
// Provides an X11 window host (Xlib / XShm / XPutImage) with near-black theme,
// header bar, bottom toolbar, camera touch/drag math, fly mode, animation playback,
// collision/hitbox toggles, child resource navigation, character kit & attachments,
// and extensible architecture for EGL/GLES contexts.
// Standard C++23.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#if defined(DMC_HAS_XSHM) || __has_include(<X11/extensions/XShm.h>)
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#ifndef DMC_HAS_XSHM
#define DMC_HAS_XSHM 1
#endif
#endif

#include "capi/include/dmc_reader.h"
#include "linux_png.inl"
#include "dmcresource/view_renderer.h"

#ifndef DMC_NATIVE_READER_BUILD_SHA
#define DMC_NATIVE_READER_BUILD_SHA "unknown"
#endif

namespace fs = std::filesystem;

namespace {

// ---- Palette (matching macOS / Android / Windows near-black theme) ---------
constexpr uint32_t kColorBg = 0x0B0B0E;         // Shell outer background
constexpr uint32_t kColorViewport = 0x121216;   // 3D viewport canvas
constexpr uint32_t kColorHeaderBg = 0x101014;   // Header bar
constexpr uint32_t kColorToolbarBg = 0x0E0E12;  // Bottom toolbar
constexpr uint32_t kColorBtnNormal = 0x1A1A22;  // Button normal
constexpr uint32_t kColorBtnHover = 0x262634;   // Button hover
constexpr uint32_t kColorBtnActive = 0x2A3D66;  // Button toggled/active
constexpr uint32_t kColorBtnBorder = 0x333342;  // Button border
constexpr uint32_t kColorText = 0xEBEBEE;       // Primary text
constexpr uint32_t kColorTextDim = 0x7E7E88;    // Secondary / dimmed text
constexpr uint32_t kColorAccent = 0x78AAFF;     // Blue accent
constexpr uint32_t kColorHealthClean = 0x2ECC71;   // Green
constexpr uint32_t kColorHealthNotice = 0xE67E22;  // Orange / Warning
constexpr uint32_t kColorHealthProblem = 0xE74C3C; // Red

constexpr int kHeaderHeight = 42;
constexpr int kToolbarHeight = 48;
constexpr int kDefaultWindowWidth = 1280;
constexpr int kDefaultWindowHeight = 800;

// ---- 8x12 Embedded Crisp Font ---------------------------------------------
// ASCII 32 (' ') through 126 ('~')
#include "linux_font.inl"
#include "linux_font_data.inl"

// ---- Geometry & Math Helpers ----------------------------------------------
struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    [[nodiscard]] bool contains(int px, int py) const noexcept {
        return px >= x && px < (x + w) && py >= y && py < (y + h);
    }
};

std::vector<uint8_t> ReadFileBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size <= 0) return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

// ---- Character Kit & Attachment Model -------------------------------------
struct KitItem {
    std::string kind;   // "weapon", "motions", "dt"
    std::string file;   // PAC file name
    std::string label;  // human readable label
    bool start = false; // load on start
    fs::path disk_path;
    bool found_on_disk = false;
    bool active = false;
};

// ---- Navigation Stack Entry -----------------------------------------------
struct NavEntry {
    dmc_session* session = nullptr;
    std::string title;
    std::string path;
    dmc_view view{};
};

// ---- Framebuffer & 2D Drawing ---------------------------------------------
class Canvas {
public:
    void resize(int w, int h) {
        if (width_ != w || height_ != h) {
            width_ = w;
            height_ = h;
            pixels_.assign(static_cast<size_t>(w * h), kColorBg);
        }
    }

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] uint32_t* data() noexcept { return pixels_.data(); }
    [[nodiscard]] const uint32_t* data() const noexcept { return pixels_.data(); }

    void fill(uint32_t color) {
        std::fill(pixels_.begin(), pixels_.end(), color);
    }

    void fill_rect(int rx, int ry, int rw, int rh, uint32_t color) {
        if (rw <= 0 || rh <= 0) return;
        const int x0 = std::clamp(rx, 0, width_);
        const int y0 = std::clamp(ry, 0, height_);
        const int x1 = std::clamp(rx + rw, 0, width_);
        const int y1 = std::clamp(ry + rh, 0, height_);
        for (int y = y0; y < y1; ++y) {
            uint32_t* row = pixels_.data() + y * width_;
            std::fill(row + x0, row + x1, color);
        }
    }

    void draw_rect_outline(int rx, int ry, int rw, int rh, uint32_t color) {
        if (rw <= 0 || rh <= 0) return;
        fill_rect(rx, ry, rw, 1, color);
        fill_rect(rx, ry + rh - 1, rw, 1, color);
        fill_rect(rx, ry, 1, rh, color);
        fill_rect(rx + rw - 1, ry, 1, rh, color);
    }

    void draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
        int dx = std::abs(x1 - x0);
        int sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0);
        int sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        while (true) {
            if (x0 >= 0 && x0 < width_ && y0 >= 0 && y0 < height_) {
                pixels_[y0 * width_ + x0] = color;
            }
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    }

    void draw_circle(int cx, int cy, int radius, uint32_t color) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx * dx + dy * dy <= radius * radius) {
                    const int px = cx + dx;
                    const int py = cy + dy;
                    if (px >= 0 && px < width_ && py >= 0 && py < height_) {
                        pixels_[py * width_ + px] = color;
                    }
                }
            }
        }
    }

    void draw_char(int x, int y, char c, uint32_t color) {
        if (c < 32 || c > 126) c = '?';
        const int glyph_idx = c - 32;
        const uint8_t* glyph = kFont8x12[glyph_idx];
        for (int row = 0; row < 12; ++row) {
            const int py = y + row;
            if (py < 0 || py >= height_) continue;
            const uint8_t bits = glyph[row];
            for (int col = 0; col < 8; ++col) {
                const int px = x + col;
                if (px < 0 || px >= width_) continue;
                if ((bits >> (7 - col)) & 1) {
                    pixels_[py * width_ + px] = color;
                }
            }
        }
    }

    int draw_text(int x, int y, std::string_view text, uint32_t color) {
        int cur_x = x;
        for (const char c : text) {
            if (c == '\n') {
                cur_x = x;
                y += 14;
                continue;
            }
            draw_char(cur_x, y, c, color);
            cur_x += 8;
        }
        return cur_x;
    }

    void draw_text_centered(int rx, int ry, int rw, int rh, std::string_view text, uint32_t color) {
        const int text_w = static_cast<int>(text.size()) * 8;
        const int tx = rx + (rw - text_w) / 2;
        const int ty = ry + (rh - 12) / 2;
        draw_text(tx, ty, text, color);
    }

    void blit_rgba_to_bgra(int dst_x, int dst_y, int src_w, int src_h, const uint8_t* rgba) {
        if (src_w <= 0 || src_h <= 0 || rgba == nullptr) return;
        const int x0 = std::max(0, dst_x);
        const int y0 = std::max(0, dst_y);
        const int x1 = std::min(width_, dst_x + src_w);
        const int y1 = std::min(height_, dst_y + src_h);

        for (int y = y0; y < y1; ++y) {
            const int sy = y - dst_y;
            const uint8_t* src_row = rgba + static_cast<size_t>(sy * src_w * 4);
            uint32_t* dst_row = pixels_.data() + y * width_;
            for (int x = x0; x < x1; ++x) {
                const int sx = x - dst_x;
                const uint8_t r = src_row[sx * 4 + 0];
                const uint8_t g = src_row[sx * 4 + 1];
                const uint8_t b = src_row[sx * 4 + 2];
                // TrueColor 24/32-bit little-endian: 0x00RRGGBB
                dst_row[x] = (static_cast<uint32_t>(r) << 16) |
                             (static_cast<uint32_t>(g) << 8) |
                             static_cast<uint32_t>(b);
            }
        }
    }

    void blit_rgba_scaled_to_bgra(int dst_x, int dst_y, int dst_w, int dst_h,
                                  int src_w, int src_h, const uint8_t* rgba) {
        if (dst_w <= 0 || dst_h <= 0 || src_w <= 0 || src_h <= 0 || rgba == nullptr) return;
        const int x0 = std::max(0, dst_x);
        const int y0 = std::max(0, dst_y);
        const int x1 = std::min(width_, dst_x + dst_w);
        const int y1 = std::min(height_, dst_y + dst_h);

        for (int y = y0; y < y1; ++y) {
            const int sy = std::clamp((y - dst_y) * src_h / dst_h, 0, src_h - 1);
            const uint8_t* src_row = rgba + static_cast<size_t>(sy * src_w * 4);
            uint32_t* dst_row = pixels_.data() + y * width_;
            for (int x = x0; x < x1; ++x) {
                const int sx = std::clamp((x - dst_x) * src_w / dst_w, 0, src_w - 1);
                const uint8_t r = src_row[sx * 4 + 0];
                const uint8_t g = src_row[sx * 4 + 1];
                const uint8_t b = src_row[sx * 4 + 2];
                dst_row[x] = (static_cast<uint32_t>(r) << 16) |
                             (static_cast<uint32_t>(g) << 8) |
                             static_cast<uint32_t>(b);
            }
        }
    }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<uint32_t> pixels_;
};

// ---- UI Toolbar Button ----------------------------------------------------
struct Button {
    int id = 0;
    std::string label;
    std::string tooltip;
    Rect rect{};
    bool visible = true;
    bool enabled = true;
    bool active = false;
};

enum ButtonId : int {
    kBtnBack = 100,
    kBtnOpen = 101,
    kBtnKit = 102,
    kBtnBrowse = 103,
    kBtnSave = 104,
    kBtnWireframe = 201,
    kBtnHierarchy = 202,
    kBtnNormals = 203,
    kBtnBounds = 204,
    kBtnUv = 205,
    kBtnInfo = 206,
    kBtnCollision = 207,
    kBtnSound = 208,
    kBtnCamera = 209,
    kBtnResetView = 210,
    kBtnEvents = 211,
    kBtnEffects = 212,
    kBtnLook = 213,
    kBtnMotion = 214,
    kBtnTexFormat = 215,
};

struct MotionCardEntry {
    std::string name;
    int library_index = 0;
    std::string description;
    std::string kind;
    size_t script_count = 0;
};

struct CameraDialButton {
    int key = 0;
    Rect rect{};
};

// ---- Linux Audio Playback Engine ------------------------------------------
class LinuxAudioPlayer {
public:
    LinuxAudioPlayer() = default;
    ~LinuxAudioPlayer() { stop(); }

    void play_pcm(const int16_t* samples, size_t sample_count, uint32_t rate) {
        stop();
        if (!samples || sample_count == 0 || rate == 0) return;

        stop_requested_.store(false);
        std::vector<int16_t> pcm_data(samples, samples + sample_count);
        worker_ = std::thread([this, rate, data = std::move(pcm_data)]() {
            std::string cmd = "aplay -q -f S16_LE -c 1 -r " + std::to_string(rate) + " 2>/dev/null";
            FILE* pipe = popen(cmd.c_str(), "w");
            if (!pipe) return;
            const size_t chunk_size = 4096;
            for (size_t offset = 0; offset < data.size() && !stop_requested_.load(); offset += chunk_size) {
                const size_t to_write = std::min(chunk_size, data.size() - offset);
                fwrite(data.data() + offset, sizeof(int16_t), to_write, pipe);
            }
            pclose(pipe);
        });
    }

    void stop() {
        stop_requested_.store(true);
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    std::atomic<bool> stop_requested_{false};
    std::thread worker_{};
};

// ---- POSIX Crash Handler (Signal Recorder) --------------------------------
class LinuxCrashHandler {
public:
    static std::string init() {
        fs::path data_dir;
        const char* xdg_data = std::getenv("XDG_DATA_HOME");
        if (xdg_data && *xdg_data) {
            data_dir = fs::path(xdg_data) / "dmc-native-reader";
        } else {
            const char* home = std::getenv("HOME");
            data_dir = home ? (fs::path(home) / ".local" / "share" / "dmc-native-reader") : fs::path("/tmp/dmc-native-reader");
        }
        std::error_code ec;
        fs::create_directories(data_dir, ec);
        const fs::path note_path = data_dir / "crash_native.txt";

        std::string previous_note;
        if (fs::exists(note_path, ec)) {
            std::ifstream in(note_path, std::ios::binary);
            if (in) {
                previous_note.assign(std::istreambuf_iterator<char>(in), {});
            }
            fs::remove(note_path, ec);
        }

        dmc_crash_install(note_path.string().c_str());
        return previous_note;
    }

    static void set_opening(const std::string& name) {
        dmc_crash_context(("opening " + name).c_str());
    }

    static void set_viewing(const std::string& name) {
        dmc_crash_context(("viewing " + name).c_str());
    }
};

// ---- Extensible Host & EGL/GLES Context Structure -------------------------
struct HostContext {
    Display* display = nullptr;
    Window window = 0;
    Visual* visual = nullptr;
    int depth = 0;
    GC gc = nullptr;
    Atom wm_delete_window = 0;

    // XDND Drag-and-Drop protocol atoms (v5)
    Atom xdnd_aware = 0;
    Atom xdnd_enter = 0;
    Atom xdnd_position = 0;
    Atom xdnd_status = 0;
    Atom xdnd_type_list = 0;
    Atom xdnd_action_copy = 0;
    Atom xdnd_drop = 0;
    Atom xdnd_finished = 0;
    Atom xdnd_selection = 0;
    Atom text_uri_list = 0;
    Atom primary = 0;
    Window xdnd_source_window = 0;

    // Extensible EGL/GLES hook structure
    void* egl_display = nullptr;
    void* egl_surface = nullptr;
    void* egl_context = nullptr;
    bool egl_active = false;

#if defined(DMC_HAS_XSHM)
    bool use_xshm = false;
    XShmSegmentInfo shminfo{};
    XImage* shm_image = nullptr;
#endif

    XImage* ximage = nullptr;

    bool init_egl_context() {
        // Architecture hook: allows transparent initialization of EGL/GLES if present
        return false;
    }

    void swap_egl_buffers() {
        // Architecture hook for hardware context
    }

    void destroy_egl_context() {
        egl_active = false;
    }
};

// ---- Application State ----------------------------------------------------
class App {
public:
    App() {
        dmc_view_reset(&view_);
        init_buttons();
    }

    ~App() {
        close_all_sessions();
        teardown_x11();
    }

    bool init_x11(int width, int height);
    void teardown_x11();
    void run();
    void open_file(const fs::path& path);

private:
    void init_buttons();
    void update_layout(int width, int height);
    void update_button_states();
    void render_frame();
    void present_frame();

    void handle_button_click(int id);
    void handle_key_press(KeySym sym);
    void handle_key_release(KeySym sym);

    void toggle_fly_mode();
    void toggle_animation();
    void step_animation(float delta);
    void cycle_hitboxes();
    void toggle_info_panel();
    void toggle_uv_view();
    void toggle_kit_panel();
    void reset_view();
    void go_back();

    void load_character_kit(const std::string& archive_name, const fs::path& archive_path);
    void toggle_kit_item(size_t index);
    void add_whole_kit();
    void reassemble_character();

    void open_child_resource(size_t index);
    void trigger_open_dialog();
    void toggle_file_browser();
    void refresh_file_browser(const fs::path& dir);
    void load_stage_companion_archives(const fs::path& path);
    void play_next_sound();
    void trigger_next_class_event();
    void trigger_next_effect();
    void cycle_variant();
    void cycle_motion();
    void cycle_texture_format();
    void refresh_session_metadata();

    void init_camera();
    void toggle_camera_mode();
    void camera_focus_at(int x, int y);
    void camera_focus_ring(float steps);
    void camera_focus_lock();
    void camera_level();
    void step_camera_dial(int key);
    void capture_photo_png();
    void export_high_res_png();
    void export_source_file();
    void load_settings();
    void save_settings();

    void close_all_sessions() {
        child_thumb_cache_.clear();
        while (!nav_stack_.empty()) {
            if (nav_stack_.back().session) dmc_close(nav_stack_.back().session);
            nav_stack_.pop_back();
        }
        if (session_) {
            dmc_close(session_);
            session_ = nullptr;
        }
    }

    HostContext host_{};
    Canvas canvas_{};

    int window_width_ = kDefaultWindowWidth;
    int window_height_ = kDefaultWindowHeight;
    bool running_ = true;

    // DMC Core State
    dmc_session* session_ = nullptr;
    std::string current_title_ = "DMC Native Reader";
    fs::path current_path_{};
    dmc_view view_{};
    std::vector<NavEntry> nav_stack_{};

    // Render flags & options
    uint32_t render_flags_ = 0;
    bool child_browser_mode_ = false;
    size_t child_count_ = 0;
    int child_hover_ = -1;

    // Health
    int file_health_ = 0; // 0 clean, 1 warning, 2 error
    std::string health_report_;

    // Enemy class events & effects
    std::vector<std::string> class_events_{};
    int active_class_event_ = -1;
    std::vector<std::pair<int, std::string>> effect_items_{};
    int active_effect_idx_ = -1;

    // Variant look selector (dmc_variant_names)
    std::vector<std::string> variant_names_{};
    int variant_index_ = 0;

    // Motion cards & strip (dmc_motion_strip_build)
    std::vector<MotionCardEntry> motion_cards_{};
    int current_motion_idx_ = -1;

    // In-engine file browser overlay
    bool show_file_browser_ = false;
    fs::path browser_dir_{};
    struct BrowserEntry {
        std::string name;
        fs::path full_path;
        bool is_dir = false;
        Rect rect{};
    };
    std::vector<BrowserEntry> browser_entries_{};
    int browser_scroll_ = 0;
    int hover_browser_idx_ = -1;

    // Texture re-encoding state (dmc_reencode_textures)
    std::vector<std::string> tex_format_names_{};
    std::vector<std::string> tex_format_labels_{};
    int current_tex_format_idx_ = -1;

    // Animation state
    bool animation_playing_ = false;
    float motion_end_frame_ = 0.0f;
    float motion_loop_start_ = 0.0f;

    // Fly mode state
    bool fly_mode_ = false;
    bool fly_key_w_ = false;
    bool fly_key_a_ = false;
    bool fly_key_s_ = false;
    bool fly_key_d_ = false;
    bool fly_key_up_ = false;
    bool fly_key_down_ = false;
    bool fly_key_left_ = false;
    bool fly_key_right_ = false;

    // Hitbox / Collision state
    int hitbox_attack_ = -2; // -2 off, -1 all, >=0 specific attack
    std::vector<int> attack_ids_{};

    // Sound playback state
    int current_sound_index_ = 0;
    LinuxAudioPlayer audio_{};

    // UI Panels
    bool show_info_panel_ = false;
    int info_topic_ = 0; // 0: Overview, 1: UV Layout, 2: Meshes, 3: Bones
    std::array<Rect, 4> info_tab_rects_{};
    bool show_kit_panel_ = false;

    // Character Kit
    std::vector<KitItem> kit_items_{};
    std::string base_character_name_{};
    fs::path base_character_path_{};
    std::string dt_body_file_{};

    // Interaction & Drag
    bool drag_left_ = false;
    bool drag_pan_ = false;
    bool drag_twist_ = false;
    int last_mouse_x_ = 0;
    int last_mouse_y_ = 0;
    int hover_btn_id_ = -1;

    // Inertia fling & spin (dmc_fling, dmc_spin_step)
    dmc_spin spin_{};
    bool is_spinning_ = false;
    std::chrono::steady_clock::time_point drag_sample_time_{};
    int drag_sample_x_ = 0;
    int drag_sample_y_ = 0;
    float drag_velocity_x_ = 0.0f;
    float drag_velocity_y_ = 0.0f;

    // Buttons
    std::vector<Button> header_buttons_{};
    std::vector<Button> toolbar_buttons_{};

    // Photo Camera state
    bool camera_mode_ = false;
    std::string camera_message_{};
    std::vector<int> camera_choices_{};
    std::vector<CameraDialButton> camera_dial_buttons_{};

    // Stage destruction & Dual preview state
    bool room_broken_ = false;
    bool dual_info_preview_ = false;

    // Child thumbnail cache
    struct ChildThumb {
        int w = 0;
        int h = 0;
        std::vector<uint8_t> rgba{};
    };
    std::unordered_map<size_t, ChildThumb> child_thumb_cache_{};

    // Pixel buffers
    std::vector<uint8_t> scene_rgba_{};
};


// ---- Implementation: App Lifecycle & X11 Host ----------------------------

bool App::init_x11(int width, int height) {
    window_width_ = width;
    window_height_ = height;

    std::string prev_crash = LinuxCrashHandler::init();
    if (!prev_crash.empty()) {
        std::cout << "[Crash Handler] Recovered note from previous run:\n" << prev_crash << "\n";
        camera_message_ = "Crash note recovered from previous run";
        health_report_ = "Recovered Crash: " + prev_crash.substr(0, 80);
    }

    host_.display = XOpenDisplay(nullptr);
    if (!host_.display) {
        std::cerr << "error: could not open X11 display. Check $DISPLAY.\n";
        return false;
    }

    const int screen = DefaultScreen(host_.display);
    host_.visual = DefaultVisual(host_.display, screen);
    host_.depth = DefaultDepth(host_.display, screen);

    XSetWindowAttributes attr{};
    attr.background_pixel = BlackPixel(host_.display, screen);
    attr.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask |
                      ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                      StructureNotifyMask;

    host_.window = XCreateWindow(
        host_.display, RootWindow(host_.display, screen),
        100, 100, static_cast<unsigned int>(window_width_), static_cast<unsigned int>(window_height_),
        0, host_.depth, InputOutput, host_.visual,
        CWBackPixel | CWEventMask, &attr);

    if (!host_.window) {
        std::cerr << "error: failed to create X11 window\n";
        return false;
    }

    XStoreName(host_.display, host_.window, "DMC Native Reader (Linux)");
    host_.wm_delete_window = XInternAtom(host_.display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(host_.display, host_.window, &host_.wm_delete_window, 1);

    // Register XDND Drag-and-Drop protocol support (v5)
    host_.xdnd_aware = XInternAtom(host_.display, "XdndAware", False);
    host_.xdnd_enter = XInternAtom(host_.display, "XdndEnter", False);
    host_.xdnd_position = XInternAtom(host_.display, "XdndPosition", False);
    host_.xdnd_status = XInternAtom(host_.display, "XdndStatus", False);
    host_.xdnd_type_list = XInternAtom(host_.display, "XdndTypeList", False);
    host_.xdnd_action_copy = XInternAtom(host_.display, "XdndActionCopy", False);
    host_.xdnd_drop = XInternAtom(host_.display, "XdndDrop", False);
    host_.xdnd_finished = XInternAtom(host_.display, "XdndFinished", False);
    host_.xdnd_selection = XInternAtom(host_.display, "XdndSelection", False);
    host_.text_uri_list = XInternAtom(host_.display, "text/uri-list", False);
    host_.primary = XInternAtom(host_.display, "PRIMARY", False);

    const Atom xdnd_ver = 5;
    XChangeProperty(host_.display, host_.window, host_.xdnd_aware, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(&xdnd_ver), 1);

    host_.gc = XCreateGC(host_.display, host_.window, 0, nullptr);

    // Disable auto-repeat for smooth game/fly mode key holding
    XAutoRepeatOff(host_.display);

    canvas_.resize(window_width_, window_height_);
    update_layout(window_width_, window_height_);
    load_settings();

    XMapWindow(host_.display, host_.window);
    XFlush(host_.display);

    return true;
}

void App::teardown_x11() {
    save_settings();
    if (host_.display) {
        XAutoRepeatOn(host_.display);
        if (host_.gc) XFreeGC(host_.display, host_.gc);
        if (host_.ximage) {
            host_.ximage->data = nullptr;
            XDestroyImage(host_.ximage);
            host_.ximage = nullptr;
        }
#if defined(DMC_HAS_XSHM)
        if (host_.shm_image) {
            XShmDetach(host_.display, &host_.shminfo);
            XDestroyImage(host_.shm_image);
            shmdt(host_.shminfo.shmaddr);
            shmctl(host_.shminfo.shmid, IPC_RMID, nullptr);
            host_.shm_image = nullptr;
        }
#endif
        if (host_.window) XDestroyWindow(host_.display, host_.window);
        XCloseDisplay(host_.display);
        host_.display = nullptr;
    }
}

void App::init_buttons() {
    header_buttons_ = {
        {kBtnBack, "< BACK", "Return to parent resource", {}, false, false, false},
        {kBtnKit, "KIT", "Character kit and attachments", {}, false, false, false},
        {kBtnSave, "SAVE", "Export assembled or re-encoded file", {}, false, false, false},
        {kBtnBrowse, "FILES", "Browse directory files (O)", {}, true, true, false},
        {kBtnOpen, "OPEN", "Open DMC game file dialog", {}, true, true, false},
    };

    toolbar_buttons_ = {
        {kBtnWireframe, "WIRE", "Toggle wireframe view (W)", {}, true, true, false},
        {kBtnHierarchy, "BONES", "Toggle skeleton hierarchy", {}, true, true, false},
        {kBtnNormals, "NORMS", "Toggle normal vectors", {}, true, true, false},
        {kBtnBounds, "BOUNDS", "Toggle bounding boxes", {}, true, true, false},
        {kBtnUv, "UV", "Toggle UV layout / gallery (U)", {}, true, true, false},
        {kBtnInfo, "INFO", "File health and inspection (I)", {}, true, true, false},
        {kBtnCollision, "COLLIS", "Cycle hitboxes and collision (H)", {}, true, true, false},
        {kBtnSound, "SOUND", "Play resource sounds (S)", {}, true, true, false},
        {kBtnEvents, "EVENT", "Trigger enemy class event (E)", {}, false, false, false},
        {kBtnEffects, "EFFECT", "Play effect bank action (X)", {}, false, false, false},
        {kBtnLook, "LOOK", "Cycle appearance variant (C)", {}, false, false, false},
        {kBtnMotion, "MOT", "Cycle library motions (M)", {}, false, false, false},
        {kBtnTexFormat, "TEX", "Re-encode textures to modern formats (T)", {}, false, false, false},
        {kBtnCamera, "CAM", "Toggle Photo Camera viewfinder (V)", {}, true, true, false},
        {kBtnResetView, "RESET", "Reset camera view (R / 0)", {}, true, true, false},
    };
}

void App::update_layout(int width, int height) {
    window_width_ = width;
    window_height_ = height;
    canvas_.resize(width, height);

    // Header buttons
    int hx = 10;
    for (auto& btn : header_buttons_) {
        if (btn.id == kBtnBack) {
            btn.rect = {hx, 6, 75, 30};
            hx += 85;
        } else if (btn.id == kBtnKit) {
            btn.rect = {hx, 6, 60, 30};
        } else if (btn.id == kBtnSave) {
            btn.rect = {width - 220, 6, 60, 30};
        } else if (btn.id == kBtnBrowse) {
            btn.rect = {width - 155, 6, 65, 30};
        } else if (btn.id == kBtnOpen) {
            btn.rect = {width - 80, 6, 70, 30};
        }
    }

    // Toolbar buttons centered along bottom (filter by visibility)
    int visible_count = 0;
    for (const auto& btn : toolbar_buttons_) {
        if (btn.visible) ++visible_count;
    }
    const int btn_w = 68;
    const int btn_h = 32;
    const int spacing = 4;
    const int total_w = visible_count * btn_w + (visible_count > 0 ? (visible_count - 1) * spacing : 0);
    int tx = std::max(6, (width - total_w) / 2);
    const int ty = height - kToolbarHeight + (kToolbarHeight - btn_h) / 2;

    for (auto& btn : toolbar_buttons_) {
        if (!btn.visible) {
            btn.rect = {0, 0, 0, 0};
            continue;
        }
        btn.rect = {tx, ty, btn_w, btn_h};
        tx += btn_w + spacing;
    }
}

void App::update_button_states() {
    for (auto& btn : header_buttons_) {
        if (btn.id == kBtnBack) {
            btn.visible = !nav_stack_.empty();
            btn.enabled = btn.visible;
        } else if (btn.id == kBtnKit) {
            btn.visible = !kit_items_.empty();
            btn.enabled = btn.visible;
            btn.active = show_kit_panel_;
        } else if (btn.id == kBtnBrowse) {
            btn.active = show_file_browser_;
        } else if (btn.id == kBtnSave) {
            const char* sname = nullptr;
            const uint8_t* sbytes = nullptr;
            size_t ssize = 0;
            btn.visible = (session_ != nullptr && dmc_source(session_, &sname, &sbytes, &ssize) && ssize > 0);
            btn.enabled = btn.visible;
        }
    }

    for (auto& btn : toolbar_buttons_) {
        switch (btn.id) {
            case kBtnWireframe:
                btn.active = (render_flags_ & dmcresource::render_flag(dmcresource::RenderFlag::Wireframe)) != 0;
                break;
            case kBtnHierarchy:
                btn.active = (render_flags_ & dmcresource::render_flag(dmcresource::RenderFlag::Hierarchy)) != 0;
                break;
            case kBtnNormals:
                btn.active = (render_flags_ & dmcresource::render_flag(dmcresource::RenderFlag::Normals)) != 0;
                break;
            case kBtnBounds:
                btn.active = (render_flags_ & dmcresource::render_flag(dmcresource::RenderFlag::Bounds)) != 0;
                break;
            case kBtnUv:
                btn.active = (render_flags_ & dmcresource::render_flag(dmcresource::RenderFlag::UvLayout)) != 0;
                break;
            case kBtnInfo:
                btn.active = show_info_panel_;
                break;
            case kBtnCollision:
                btn.active = (hitbox_attack_ != -2);
                break;
            case kBtnSound:
                btn.enabled = (session_ != nullptr);
                break;
            case kBtnEvents:
                btn.visible = !class_events_.empty();
                btn.enabled = btn.visible;
                btn.active = (active_class_event_ >= 0);
                break;
            case kBtnEffects:
                btn.visible = !effect_items_.empty();
                btn.enabled = btn.visible;
                btn.active = (active_effect_idx_ >= 0);
                break;
            case kBtnLook:
                btn.visible = (variant_names_.size() > 1);
                btn.enabled = btn.visible;
                btn.active = (variant_index_ > 0);
                break;
            case kBtnMotion:
                btn.visible = !motion_cards_.empty();
                btn.enabled = btn.visible;
                btn.active = animation_playing_;
                break;
            case kBtnTexFormat:
                btn.visible = (session_ != nullptr && !child_browser_mode_);
                btn.enabled = btn.visible;
                btn.active = (current_tex_format_idx_ >= 0);
                if (current_tex_format_idx_ >= 0 && current_tex_format_idx_ < static_cast<int>(tex_format_names_.size())) {
                    btn.label = tex_format_names_[current_tex_format_idx_];
                    std::transform(btn.label.begin(), btn.label.end(), btn.label.begin(), ::toupper);
                } else {
                    btn.label = "TEX";
                }
                break;
            case kBtnCamera:
                btn.active = dmc_camera_mode();
                break;
            case kBtnResetView:
                btn.active = false;
                break;
            default:
                break;
        }
    }
}

void App::open_file(const fs::path& path) {
    const auto bytes = ReadFileBytes(path);
    if (bytes.empty()) {
        std::cerr << "error: file not found or empty: " << path.string() << "\n";
        return;
    }

    close_all_sessions();
    current_path_ = path;
    current_title_ = path.filename().string();
    dt_body_file_.clear();
    show_info_panel_ = false;
    show_kit_panel_ = false;
    fly_mode_ = false;

    LinuxCrashHandler::set_opening(path.filename().string());
    auto res = dmc_open(path.string().c_str(), bytes.data(), bytes.size());
    if (res.session == nullptr) {
        std::cerr << "open rejected: " << (res.title ? res.title : "unknown reason") << "\n";
        current_title_ = std::string("Rejected: ") + (res.title ? res.title : "Unknown");
        dmc_string_free(res.title);
        session_ = nullptr;
        return;
    }

    session_ = res.session;
    if (res.title && std::strlen(res.title) > 0) {
        current_title_ = res.title;
    }
    dmc_string_free(res.title);
    LinuxCrashHandler::set_viewing(current_title_);

    // View reset
    dmc_view_reset(&view_);
    render_flags_ = 0;
    view_.render_flags = 0;

    // Session metadata & children
    refresh_session_metadata();

    // File health
    char* report_str = nullptr;
    file_health_ = dmc_file_health(session_, path.filename().string().c_str(), &report_str);
    health_report_ = report_str ? report_str : "";
    if (report_str) dmc_string_free(report_str);

    // Character kit loading
    base_character_name_ = path.filename().string();
    base_character_path_ = path;
    load_character_kit(base_character_name_, path);

    // Stage companion archives auto-discovery (stNNN_effect.pac, stNNNcfg.pac, static_effect.pac)
    load_stage_companion_archives(path);

    std::cout << "Opened " << path.string() << " (" << current_title_ << ")\n";
    if (host_.display && host_.window) {
        XStoreName(host_.display, host_.window, ("DMC Native Reader - " + current_title_).c_str());
    }
}

void App::load_character_kit(const std::string& archive_name, const fs::path& archive_path) {
    kit_items_.clear();
    variant_names_.clear();
    variant_index_ = 0;

    dmc_strings* raw_vars = dmc_variant_names(archive_name.c_str());
    if (raw_vars) {
        const size_t vcnt = dmc_strings_count(raw_vars);
        for (size_t i = 0; i < vcnt; ++i) {
            const char* v = dmc_strings_at(raw_vars, i);
            if (v && *v) variant_names_.emplace_back(v);
        }
        dmc_strings_free(raw_vars);
    }

    dmc_strings* raw_kit = dmc_character_kit(archive_name.c_str());
    if (!raw_kit) return;

    const size_t count = dmc_strings_count(raw_kit);
    const fs::path dir = archive_path.parent_path();

    for (size_t i = 0; i < count; ++i) {
        const char* line = dmc_strings_at(raw_kit, i);
        if (!line) continue;
        // Format: kind \t file \t label \t start
        std::string s(line);
        std::vector<std::string> tokens;
        size_t pos = 0;
        while ((pos = s.find('\t')) != std::string::npos) {
            tokens.push_back(s.substr(0, pos));
            s.erase(0, pos + 1);
        }
        tokens.push_back(s);
        if (tokens.size() == 4) {
            KitItem item;
            item.kind = tokens[0];
            item.file = tokens[1];
            item.label = tokens[2];
            item.start = (tokens[3] == "1");

            // Look for matching file in the same directory (case-insensitive)
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (entry.is_regular_file(ec)) {
                    std::string fname = entry.path().filename().string();
                    if (strcasecmp(fname.c_str(), item.file.c_str()) == 0) {
                        item.found_on_disk = true;
                        item.disk_path = entry.path();
                        break;
                    }
                }
            }
            kit_items_.push_back(item);
        }
    }
    dmc_strings_free(raw_kit);

    // Auto-assemble items marked with start=1 if found on disk
    bool needs_assemble = false;
    for (auto& item : kit_items_) {
        if (item.start && item.found_on_disk && item.kind != "dt") {
            item.active = true;
            needs_assemble = true;
        }
    }
    if (needs_assemble) {
        reassemble_character();
    }
}

void App::reassemble_character() {
    if (base_character_path_.empty()) return;
    std::vector<dmc_session*> open_sources;
    std::vector<std::string> source_names;
    std::vector<std::vector<uint8_t>> source_byte_buffers;

    // Base body: use DT body if selected, else base character file
    fs::path body_path = base_character_path_;
    if (!dt_body_file_.empty()) {
        for (const auto& k : kit_items_) {
            if (k.kind == "dt" && k.file == dt_body_file_ && k.found_on_disk) {
                body_path = k.disk_path;
                break;
            }
        }
    }

    auto body_bytes = ReadFileBytes(body_path);
    if (body_bytes.empty()) return;
    source_byte_buffers.push_back(std::move(body_bytes));
    source_names.push_back(body_path.filename().string());

    dmc_session* body_sess = dmc_open_source(
        source_names.back().c_str(),
        source_byte_buffers.back().data(),
        source_byte_buffers.back().size());
    if (!body_sess) return;
    open_sources.push_back(body_sess);

    // Attached archives (active weapons and motions)
    for (const auto& item : kit_items_) {
        if (item.active && item.found_on_disk && item.kind != "dt") {
            auto b = ReadFileBytes(item.disk_path);
            if (!b.empty()) {
                source_byte_buffers.push_back(std::move(b));
                source_names.push_back(item.disk_path.filename().string());
                dmc_session* s = dmc_open_source(
                    source_names.back().c_str(),
                    source_byte_buffers.back().data(),
                    source_byte_buffers.back().size());
                if (s) open_sources.push_back(s);
            }
        }
    }

    std::vector<const char*> name_ptrs;
    name_ptrs.reserve(source_names.size());
    for (const auto& n : source_names) name_ptrs.push_back(n.c_str());

    dmc_session* assembled = dmc_assemble(open_sources.data(), name_ptrs.data(), open_sources.size(),
                                          static_cast<size_t>(std::max(0, variant_index_)));
    for (auto* s : open_sources) dmc_close(s);

    if (assembled) {
        if (session_) dmc_close(session_);
        session_ = assembled;
        std::string look = (!variant_names_.empty() && variant_index_ < static_cast<int>(variant_names_.size()))
                           ? (" · " + variant_names_[variant_index_]) : "";
        current_title_ = base_character_name_ + look + " (Assembled)";
        refresh_session_metadata();
        if (host_.display && host_.window) {
            XStoreName(host_.display, host_.window, ("DMC Native Reader - " + current_title_).c_str());
        }
    }
}

void App::cycle_variant() {
    if (variant_names_.empty()) return;
    variant_index_ = (variant_index_ + 1) % static_cast<int>(variant_names_.size());
    reassemble_character();
    camera_message_ = "Look Variant [" + std::to_string(variant_index_ + 1) + "/" +
                      std::to_string(variant_names_.size()) + "]: " + variant_names_[variant_index_];
    std::cout << camera_message_ << "\n";
}

void App::toggle_kit_item(size_t index) {
    if (index >= kit_items_.size()) return;
    auto& item = kit_items_[index];
    if (!item.found_on_disk) return;

    if (item.kind == "dt") {
        if (dt_body_file_ == item.file) {
            dt_body_file_.clear();
            item.active = false;
        } else {
            dt_body_file_ = item.file;
            for (auto& other : kit_items_) {
                if (other.kind == "dt") other.active = (other.file == dt_body_file_);
            }
        }
    } else {
        item.active = !item.active;
    }
    reassemble_character();
}

void App::add_whole_kit() {
    for (auto& item : kit_items_) {
        if (item.found_on_disk && item.kind != "dt") {
            item.active = true;
        }
    }
    reassemble_character();
}

void App::refresh_session_metadata() {
    if (!session_) return;
    child_count_ = dmc_child_count(session_);
    const uint64_t state_bits = dmc_state_flags(session_);
    child_browser_mode_ = (state_bits & (1ULL << 15)) != 0;
    child_thumb_cache_.clear();

    animation_playing_ = false;
    dmc_motion_range(session_, &motion_end_frame_, &motion_loop_start_);

    attack_ids_.resize(32);
    const size_t attack_count = dmc_collision_attack_ids(session_, attack_ids_.data(), attack_ids_.size());
    attack_ids_.resize(attack_count);
    hitbox_attack_ = -2;

    // Enemy class events (special actions)
    class_events_.clear();
    active_class_event_ = -1;
    dmc_strings* evs = dmc_class_events(session_);
    if (evs) {
        const size_t n = dmc_strings_count(evs);
        for (size_t i = 0; i < n; ++i) {
            const char* s = dmc_strings_at(evs, i);
            if (s) class_events_.emplace_back(s);
        }
        dmc_strings_free(evs);
    }

    // Effect menu
    effect_items_.clear();
    active_effect_idx_ = -1;
    dmc_menu* eff = dmc_effect_menu(session_);
    if (eff) {
        const size_t n = dmc_menu_count(eff);
        for (size_t i = 0; i < n; ++i) {
            const auto* it = dmc_menu_at(eff, i);
            if (it && it->label) effect_items_.emplace_back(it->id, it->label);
        }
        dmc_menu_free(eff);
    }

    // Motion strip & library motions
    motion_cards_.clear();
    current_motion_idx_ = -1;
    dmc_motion_strip* strip = dmc_motion_strip_build(session_, true, nullptr, 0);
    if (strip) {
        const size_t cnt = dmc_motion_strip_count(strip);
        for (size_t i = 0; i < cnt; ++i) {
            const auto* card = dmc_motion_strip_card(strip, i);
            if (card && card->name) {
                MotionCardEntry entry;
                entry.name = card->name;
                entry.library_index = card->library_index;
                entry.description = card->description ? card->description : "";
                entry.kind = card->kind ? card->kind : "";
                entry.script_count = card->script_count;
                motion_cards_.push_back(entry);
            }
        }
        dmc_motion_strip_free(strip);
    }

    // Reset texture format re-encoding state
    tex_format_names_.clear();
    tex_format_labels_.clear();
    current_tex_format_idx_ = -1;

    update_button_states();
    update_layout(window_width_, window_height_);
}

void App::open_child_resource(size_t index) {
    if (!session_ || index >= child_count_) return;
    dmc_session* child = dmc_open_child(session_, index);
    if (!child) return;

    char* c_title = dmc_child_title(session_, index);
    std::string child_name = c_title ? c_title : ("Child " + std::to_string(index));
    if (c_title) dmc_string_free(c_title);

    // Push parent to navigation stack
    nav_stack_.push_back({session_, current_title_, current_path_.string(), view_});

    session_ = child;
    current_title_ = child_name;
    dmc_view_reset(&view_);

    refresh_session_metadata();

    std::cout << "Navigated into child: " << current_title_ << "\n";
}

void App::go_back() {
    if (nav_stack_.empty()) return;
    auto prev = nav_stack_.back();
    nav_stack_.pop_back();

    if (session_) dmc_close(session_);
    session_ = prev.session;
    current_title_ = prev.title;
    current_path_ = prev.path;
    view_ = prev.view;

    refresh_session_metadata();

    std::cout << "Navigated back to: " << current_title_ << "\n";
}

void App::trigger_open_dialog() {
    // Standard Linux file dialogs: Zenity, kdialog, or console prompt
    std::string chosen_path;
    FILE* p = popen("zenity --file-selection --title=\"Open DMC Resource\" 2>/dev/null", "r");
    if (p) {
        char buf[1024];
        if (fgets(buf, sizeof(buf), p)) {
            chosen_path = buf;
            while (!chosen_path.empty() && (chosen_path.back() == '\n' || chosen_path.back() == '\r')) {
                chosen_path.pop_back();
            }
        }
        pclose(p);
    }
    if (chosen_path.empty()) {
        p = popen("kdialog --getopenfilename . 2>/dev/null", "r");
        if (p) {
            char buf[1024];
            if (fgets(buf, sizeof(buf), p)) {
                chosen_path = buf;
                while (!chosen_path.empty() && (chosen_path.back() == '\n' || chosen_path.back() == '\r')) {
                    chosen_path.pop_back();
                }
            }
            pclose(p);
        }
    }
    if (!chosen_path.empty()) {
        open_file(chosen_path);
    } else {
        std::cout << "External dialog unavailable or cancelled. Opening in-engine file browser.\n";
        toggle_file_browser();
    }
}

void App::load_stage_companion_archives(const fs::path& path) {
    char* fx_name = nullptr;
    char* cfg_name = nullptr;
    const std::string filename = path.filename().string();
    if (!dmc_stage_archives(filename.c_str(), &fx_name, &cfg_name)) {
        return;
    }

    const fs::path dir = path.parent_path();
    std::string wanted_fx = fx_name ? fx_name : "";
    std::string wanted_cfg = cfg_name ? cfg_name : "";
    if (fx_name) dmc_string_free(fx_name);
    if (cfg_name) dmc_string_free(cfg_name);

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string ename = entry.path().filename().string();
        if (!wanted_fx.empty() && strcasecmp(ename.c_str(), wanted_fx.c_str()) == 0) {
            auto b = ReadFileBytes(entry.path());
            if (!b.empty()) {
                char* report = dmc_room_effects_load(ename.c_str(), b.data(), b.size(), 1);
                std::cout << "[Stage] Loaded stage effects from " << ename << (report ? (" (" + std::string(report) + ")") : "") << "\n";
                if (report) dmc_string_free(report);
            }
        } else if (!wanted_cfg.empty() && strcasecmp(ename.c_str(), wanted_cfg.c_str()) == 0) {
            auto b = ReadFileBytes(entry.path());
            if (!b.empty()) {
                dmc_outcome out = dmc_stage_config_load(ename.c_str(), b.data(), b.size(), 1);
                std::cout << "[Stage] Loaded stage config from " << ename << (out.message ? (" (" + std::string(out.message) + ")") : "") << "\n";
                dmc_outcome_free(&out);
            }
        } else if (strcasecmp(ename.c_str(), "static_effect.pac") == 0) {
            auto b = ReadFileBytes(entry.path());
            if (!b.empty()) {
                char* report = dmc_common_effects_load(ename.c_str(), b.data(), b.size());
                std::cout << "[Stage] Loaded common effects from " << ename << (report ? (" (" + std::string(report) + ")") : "") << "\n";
                if (report) dmc_string_free(report);
            }
        }
    }
}

void App::toggle_file_browser() {
    show_file_browser_ = !show_file_browser_;
    if (show_file_browser_) {
        fs::path start_dir = current_path_.empty() ? fs::current_path() : current_path_.parent_path();
        refresh_file_browser(start_dir);
    }
}

void App::refresh_file_browser(const fs::path& dir) {
    browser_dir_ = dir;
    browser_entries_.clear();
    browser_scroll_ = 0;

    // Parent directory entry
    if (dir.has_parent_path() && dir.parent_path() != dir) {
        BrowserEntry parent_entry;
        parent_entry.name = ".. [Parent Directory]";
        parent_entry.full_path = dir.parent_path();
        parent_entry.is_dir = true;
        browser_entries_.push_back(parent_entry);
    }

    std::error_code ec;
    std::vector<BrowserEntry> dirs;
    std::vector<BrowserEntry> files;

    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        std::string fname = entry.path().filename().string();
        if (fname.empty() || fname[0] == '.') continue;

        if (entry.is_directory(ec)) {
            BrowserEntry de;
            de.name = fname + "/";
            de.full_path = entry.path();
            de.is_dir = true;
            dirs.push_back(de);
        } else if (entry.is_regular_file(ec)) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".pac" || ext == ".mod" || ext == ".scm" || ext == ".ptx" ||
                ext == ".mot" || ext == ".tm2" || ext == ".bin" || ext == ".dds" ||
                ext == ".ogg" || ext == ".vag" || ext.rfind(".st", 0) == 0) {
                BrowserEntry fe;
                fe.name = fname;
                fe.full_path = entry.path();
                fe.is_dir = false;
                files.push_back(fe);
            }
        }
    }

    std::sort(dirs.begin(), dirs.end(), [](const BrowserEntry& a, const BrowserEntry& b) {
        return a.name < b.name;
    });
    std::sort(files.begin(), files.end(), [](const BrowserEntry& a, const BrowserEntry& b) {
        return a.name < b.name;
    });

    for (auto& d : dirs) browser_entries_.push_back(std::move(d));
    for (auto& f : files) browser_entries_.push_back(std::move(f));
}

// ---- Rendering & UI Drawing -----------------------------------------------

void App::render_frame() {
    canvas_.fill(kColorBg);

    const int viewport_y = kHeaderHeight;
    const int viewport_h = std::max(64, window_height_ - kHeaderHeight - kToolbarHeight);
    const int viewport_w = window_width_;

    // Viewport background
    canvas_.fill_rect(0, viewport_y, viewport_w, viewport_h, kColorViewport);

    // 1. Viewport Content
    if (!session_) {
        // Welcome / Empty view
        canvas_.draw_text_centered(
            0, viewport_y + viewport_h / 2 - 40, viewport_w, 20,
            "DMC Native Reader (Linux Desktop)", kColorAccent);
        canvas_.draw_text_centered(
            0, viewport_y + viewport_h / 2 - 10, viewport_w, 20,
            "Click [ OPEN ] or pass a file as a command-line argument", kColorText);
        canvas_.draw_text_centered(
            0, viewport_y + viewport_h / 2 + 15, viewport_w, 20,
            "Supported formats: .pac, .mod, .scm, .dds, .ptx, .mot, .st*, .tm2", kColorTextDim);
    } else if (child_browser_mode_ && child_count_ > 0) {
        // Child Gallery Grid with Thumbnails
        const int cols = std::max(1, viewport_w / 200);
        const int tile_w = 184;
        const int tile_h = 74;
        const int pad = 12;
        for (size_t i = 0; i < child_count_; ++i) {
            const int col = static_cast<int>(i) % cols;
            const int row = static_cast<int>(i) / cols;
            const int tx = pad + col * (tile_w + pad);
            const int ty = viewport_y + pad + row * (tile_h + pad);
            if (ty + tile_h > viewport_y + viewport_h) break;

            const bool hot = (child_hover_ == static_cast<int>(i));
            canvas_.fill_rect(tx, ty, tile_w, tile_h, hot ? kColorBtnHover : kColorBtnNormal);
            canvas_.draw_rect_outline(tx, ty, tile_w, tile_h, hot ? kColorAccent : kColorBtnBorder);

            char* title_c = dmc_child_title(session_, i);
            std::string label = title_c ? title_c : ("Item " + std::to_string(i));
            if (title_c) dmc_string_free(title_c);

            auto it = child_thumb_cache_.find(i);
            if (it == child_thumb_cache_.end()) {
                int pw = 0, ph = 0;
                if (dmc_child_preview_size(session_, i, &pw, &ph) && pw > 0 && ph > 0 && pw <= 1024 && ph <= 1024) {
                    ChildThumb thumb;
                    thumb.w = pw;
                    thumb.h = ph;
                    thumb.rgba.resize(static_cast<size_t>(pw * ph * 4));
                    if (dmc_child_preview(session_, i, thumb.rgba.data(), thumb.rgba.size())) {
                        it = child_thumb_cache_.emplace(i, std::move(thumb)).first;
                    }
                }
            }

            if (it != child_thumb_cache_.end()) {
                canvas_.blit_rgba_scaled_to_bgra(tx + 6, ty + 9, 56, 56, it->second.w, it->second.h, it->second.rgba.data());
                canvas_.draw_rect_outline(tx + 6, ty + 9, 56, 56, kColorBtnBorder);
                canvas_.draw_text(tx + 68, ty + 14, "[" + std::to_string(i) + "]", kColorAccent);
                if (label.size() > 14) label = label.substr(0, 11) + "...";
                canvas_.draw_text(tx + 68, ty + 36, label, kColorText);
            } else {
                canvas_.draw_text(tx + 10, ty + 14, "[" + std::to_string(i) + "]", kColorAccent);
                if (label.size() > 20) label = label.substr(0, 17) + "...";
                canvas_.draw_text(tx + 10, ty + 38, label, kColorText);
            }
        }
    } else {
        // Image preview or 3D scene
        int img_w = 0, img_h = 0;
        if (dmc_image_preview_size(session_, &img_w, &img_h) && img_w > 0 && img_h > 0) {
            std::vector<uint8_t> preview_rgba(static_cast<size_t>(img_w * img_h * 4));
            if (dmc_image_preview(session_, preview_rgba.data(), preview_rgba.size())) {
                const float sx = static_cast<float>(viewport_w) / img_w;
                const float sy = static_cast<float>(viewport_h) / img_h;
                const float scale = std::min(sx, sy);
                const int dw = static_cast<int>(img_w * scale);
                const int dh = static_cast<int>(img_h * scale);
                const int dx = (viewport_w - dw) / 2;
                const int dy = viewport_y + (viewport_h - dh) / 2;
                // Nearest scale into viewport
                for (int y = 0; y < dh; ++y) {
                    const int sy_src = std::clamp(static_cast<int>(y / scale), 0, img_h - 1);
                    const uint8_t* srow = preview_rgba.data() + sy_src * img_w * 4;
                    for (int x = 0; x < dw; ++x) {
                        const int sx_src = std::clamp(static_cast<int>(x / scale), 0, img_w - 1);
                        const uint8_t r = srow[sx_src * 4 + 0];
                        const uint8_t g = srow[sx_src * 4 + 1];
                        const uint8_t b = srow[sx_src * 4 + 2];
                        const int px = dx + x;
                        const int py = dy + y;
                        if (px >= 0 && px < window_width_ && py >= viewport_y && py < viewport_y + viewport_h) {
                            canvas_.data()[py * window_width_ + px] =
                                (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
                        }
                    }
                }
            }
        } else {
            // Core 3D render
            view_.render_flags = render_flags_;
            scene_rgba_.resize(static_cast<size_t>(viewport_w * viewport_h * 4));
            const int rc = dmc_render(session_, viewport_w, viewport_h, &view_, scene_rgba_.data(), scene_rgba_.size());
            if (rc > 0) {
                canvas_.blit_rgba_to_bgra(0, viewport_y, viewport_w, viewport_h, scene_rgba_.data());
            }
        }
    }

    // 2. HUD Overlays
    if (fly_mode_) {
        // Fly mode active banner
        canvas_.fill_rect(viewport_w / 2 - 220, viewport_y + 10, 440, 26, 0x1A1A26);
        canvas_.draw_rect_outline(viewport_w / 2 - 220, viewport_y + 10, 440, 26, kColorAccent);
        canvas_.draw_text_centered(
            viewport_w / 2 - 220, viewport_y + 10, 440, 26,
            "FLY MODE: [W][A][S][D] Move  [Arrows] Look  [F] Exit", kColorAccent);
    }

    if (animation_playing_ || std::isfinite(view_.motion_frame)) {
        char anim_buf[140];
        std::string mot_title = (current_motion_idx_ >= 0 && current_motion_idx_ < static_cast<int>(motion_cards_.size()))
                                ? (" · " + motion_cards_[current_motion_idx_].name) : "";
        std::snprintf(anim_buf, sizeof(anim_buf), "Motion%s: %.1f / %.1f  %s  [Space]  [[ / ]] Step  [M] Next",
                      mot_title.c_str(), view_.motion_frame, motion_end_frame_,
                      animation_playing_ ? "[>] PLAY" : "[||] PAUSE");
        canvas_.fill_rect(12, viewport_y + viewport_h - 32, 460, 24, 0x181822);
        canvas_.draw_rect_outline(12, viewport_y + viewport_h - 32, 460, 24, kColorBtnBorder);
        canvas_.draw_text(18, viewport_y + viewport_h - 26, anim_buf, kColorText);
    }

    if (hitbox_attack_ != -2) {
        std::string hit_str = "Hitboxes: " + (hitbox_attack_ == -1 ? std::string("All Attacks") : ("Attack " + std::to_string(hitbox_attack_)));
        canvas_.fill_rect(viewport_w - 200, viewport_y + 10, 190, 24, 0x181822);
        canvas_.draw_rect_outline(viewport_w - 200, viewport_y + 10, 190, 24, kColorAccent);
        canvas_.draw_text(viewport_w - 192, viewport_y + 16, hit_str, kColorAccent);
    }

    // Camera Viewfinder & Guides Overlay
    if (dmc_camera_mode() && session_) {
        dmc_finder finder = dmc_viewfinder(session_, viewport_w, viewport_h, view_.zoom, false, 0);
        if (finder.shown) {
            const int fx = static_cast<int>(finder.frame[0]);
            const int fy = viewport_y + static_cast<int>(finder.frame[1]);
            const int fw = static_cast<int>(finder.frame[2]);
            const int fh = static_cast<int>(finder.frame[3]);

            canvas_.draw_rect_outline(fx, fy, fw, fh, 0x00C49A45);

            // Corner brackets
            const int bl = std::min(24, std::min(fw, fh) / 4);
            canvas_.fill_rect(fx, fy, bl, 3, 0x00FFCC00);
            canvas_.fill_rect(fx, fy, 3, bl, 0x00FFCC00);
            canvas_.fill_rect(fx + fw - bl, fy, bl, 3, 0x00FFCC00);
            canvas_.fill_rect(fx + fw - 3, fy, 3, bl, 0x00FFCC00);
            canvas_.fill_rect(fx, fy + fh - 3, bl, 3, 0x00FFCC00);
            canvas_.fill_rect(fx, fy + fh - bl, 3, bl, 0x00FFCC00);
            canvas_.fill_rect(fx + fw - bl, fy + fh - 3, bl, 3, 0x00FFCC00);
            canvas_.fill_rect(fx + fw - 3, fy + fh - bl, 3, bl, 0x00FFCC00);

            // Guide lines (rule of thirds)
            for (size_t i = 0; i < finder.line_count; ++i) {
                const float* l = finder.lines + i * 4;
                canvas_.draw_line(static_cast<int>(l[0]), viewport_y + static_cast<int>(l[1]),
                                  static_cast<int>(l[2]), viewport_y + static_cast<int>(l[3]),
                                  0x004A5060);
            }

            // Viewfinder status
            if (finder.status && finder.status[0] != '\0') {
                const int sw = static_cast<int>(std::strlen(finder.status)) * 8 + 16;
                canvas_.fill_rect(fx + 10, fy + fh - 26, sw, 20, 0x0014141C);
                canvas_.draw_rect_outline(fx + 10, fy + fh - 26, sw, 20, 0x00FFCC00);
                canvas_.draw_text(fx + 18, fy + fh - 22, finder.status, 0x00FFCC44);
            }
        }
        dmc_finder_free(&finder);

        // Camera message / toast
        if (!camera_message_.empty()) {
            const int mw = static_cast<int>(camera_message_.size()) * 8 + 16;
            canvas_.fill_rect(14, viewport_y + 40, mw, 22, 0x00181824);
            canvas_.draw_rect_outline(14, viewport_y + 40, mw, 22, 0x0044EE99);
            canvas_.draw_text(22, viewport_y + 45, camera_message_, 0x0044EE99);
        }

        // Camera Dials Bar at top of viewport
        camera_dial_buttons_.clear();
        dmc_camera_dials* dials = dmc_camera_dials_list();
        if (dials) {
            const size_t dial_cnt = dmc_camera_dials_count(dials);
            int dial_x = 14;
            const int dial_y = viewport_y + 10;
            for (size_t i = 0; i < dial_cnt; ++i) {
                const dmc_camera_dial* d = dmc_camera_dials_at(dials, i);
                if (!d) continue;
                std::string label = (d->title ? std::string(d->title) : "") + ": " + (d->value ? std::string(d->value) : "");
                const int dw = static_cast<int>(label.size()) * 8 + 16;
                Rect dr{dial_x, dial_y, dw, 24};
                camera_dial_buttons_.push_back({d->key, dr});
                canvas_.fill_rect(dial_x, dial_y, dw, 24, 0x0020202E);
                canvas_.draw_rect_outline(dial_x, dial_y, dw, 24, 0x0078AAFF);
                canvas_.draw_text(dial_x + 8, dial_y + 6, label, 0x00EBEBEE);
                dial_x += dw + 8;
            }
            dmc_camera_dials_free(dials);
        }
    }

    // Cutscene Subtitles
    if (session_) {
        dmc_subtitle sub{};
        if (dmc_cutscene_subtitle(session_, &sub)) {
            if (sub.text && sub.text[0] != '\0') {
                const int sub_y = viewport_y + static_cast<int>(sub.top * viewport_h);
                const int sub_w = static_cast<int>(std::strlen(sub.text)) * 8;
                const int sub_x = static_cast<int>(sub.centre_x * viewport_w) - sub_w / 2;
                canvas_.fill_rect(std::max(10, sub_x - 8), sub_y - 2, sub_w + 16, 18, 0x000A0A0E);
                const uint32_t f_color = (static_cast<uint32_t>(sub.front[0]) << 16) |
                                         (static_cast<uint32_t>(sub.front[1]) << 8) |
                                         sub.front[2];
                canvas_.draw_text(std::max(10, sub_x), sub_y + 2, sub.text, f_color != 0 ? f_color : 0x00E0E0E0);
            }
            dmc_subtitle_free(&sub);
        }
    }

    // 3. Header Bar
    canvas_.fill_rect(0, 0, window_width_, kHeaderHeight, kColorHeaderBg);
    canvas_.fill_rect(0, kHeaderHeight - 1, window_width_, 1, kColorBtnBorder);

    for (const auto& btn : header_buttons_) {
        if (!btn.visible) continue;
        const bool hover = (hover_btn_id_ == btn.id);
        const uint32_t bg = btn.active ? kColorBtnActive : (hover ? kColorBtnHover : kColorBtnNormal);
        canvas_.fill_rect(btn.rect.x, btn.rect.y, btn.rect.w, btn.rect.h, bg);
        canvas_.draw_rect_outline(btn.rect.x, btn.rect.y, btn.rect.w, btn.rect.h,
                                  btn.active ? kColorAccent : (hover ? kColorAccent : kColorBtnBorder));
        canvas_.draw_text_centered(btn.rect.x, btn.rect.y, btn.rect.w, btn.rect.h, btn.label,
                                   btn.active ? kColorAccent : kColorText);
    }

    // Title and Health Dot
    int title_x = 14;
    for (const auto& btn : header_buttons_) {
        if (btn.visible && btn.id == kBtnBack) {
            title_x = btn.rect.x + btn.rect.w + 14;
        }
    }

    canvas_.draw_text(title_x, 15, current_title_, kColorText);

    if (session_) {
        const int dot_x = title_x + static_cast<int>(current_title_.size()) * 8 + 16;
        const uint32_t dot_col = (file_health_ == 0) ? kColorHealthClean :
                                 ((file_health_ == 1) ? kColorHealthNotice : kColorHealthProblem);
        canvas_.draw_circle(dot_x, 21, 5, dot_col);
        const char* health_lbl = (file_health_ == 0) ? "Clean" : ((file_health_ == 1) ? "Notice" : "Problem");
        canvas_.draw_text(dot_x + 10, 15, health_lbl, dot_col);
    }

    // 4. Toolbar Bar
    const int toolbar_y = window_height_ - kToolbarHeight;
    canvas_.fill_rect(0, toolbar_y, window_width_, kToolbarHeight, kColorToolbarBg);
    canvas_.fill_rect(0, toolbar_y, window_width_, 1, kColorBtnBorder);

    update_button_states();
    for (const auto& btn : toolbar_buttons_) {
        if (!btn.visible) continue;
        const bool hover = (hover_btn_id_ == btn.id);
        const uint32_t bg = btn.active ? kColorBtnActive : (hover ? kColorBtnHover : kColorBtnNormal);
        canvas_.fill_rect(btn.rect.x, btn.rect.y, btn.rect.w, btn.rect.h, bg);
        canvas_.draw_rect_outline(btn.rect.x, btn.rect.y, btn.rect.w, btn.rect.h,
                                  btn.active ? kColorAccent : (hover ? kColorAccent : kColorBtnBorder));
        canvas_.draw_text_centered(btn.rect.x, btn.rect.y, btn.rect.w, btn.rect.h, btn.label,
                                   btn.active ? kColorAccent : kColorText);
    }

    // 5. Overlays (Info Panel & Kit Panel)
    if (show_info_panel_) {
        const int pw = std::min(680, window_width_ - 40);
        const int ph = std::min(500, window_height_ - 80);
        const int px = (window_width_ - pw) / 2;
        const int py = (window_height_ - ph) / 2;

        canvas_.fill_rect(px, py, pw, ph, 0x14141A);
        canvas_.draw_rect_outline(px, py, pw, ph, kColorAccent);

        canvas_.fill_rect(px, py, pw, 34, 0x1C1C24);
        canvas_.draw_text(px + 14, py + 10, "Resource Information & Deep Diagnostics", kColorAccent);
        canvas_.draw_text(px + pw - 30, py + 10, "[X]", kColorTextDim);

        // Topic tabs: 0: Diag, 1: UV Layout, 2: Meshes, 3: Bones
        const char* tab_names[4] = {"[0] Overview", "[1] UV Map", "[2] Meshes", "[3] Bones"};
        int tab_x = px + 14;
        const int tab_y = py + 40;
        for (int t = 0; t < 4; ++t) {
            const int tw = 120;
            const int th = 24;
            info_tab_rects_[t] = {tab_x, tab_y, tw, th};
            const bool active_tab = (info_topic_ == t);
            canvas_.fill_rect(tab_x, tab_y, tw, th, active_tab ? kColorBtnActive : kColorBtnNormal);
            canvas_.draw_rect_outline(tab_x, tab_y, tw, th, active_tab ? kColorAccent : kColorBtnBorder);
            canvas_.draw_text_centered(tab_x, tab_y, tw, th, tab_names[t], active_tab ? kColorAccent : kColorText);
            tab_x += tw + 8;
        }

        int cur_y = py + 72;
        canvas_.draw_text(px + 16, cur_y, "File: " + current_path_.filename().string(), kColorText);
        cur_y += 20;

        char* topic_text = nullptr;
        if (session_) {
            if (info_topic_ == 0) {
                topic_text = dmc_diagnostics(session_);
            } else {
                topic_text = dmc_inspect(session_, info_topic_);
            }
        }
        std::string diag_str = topic_text ? topic_text : "Information unavailable.";
        if (topic_text) dmc_string_free(topic_text);

        std::string_view lines(diag_str);
        size_t l_start = 0;
        int line_count = 0;
        while (l_start < lines.size() && line_count < 14) {
            size_t l_end = lines.find('\n', l_start);
            if (l_end == std::string_view::npos) l_end = lines.size();
            std::string line(lines.substr(l_start, l_end - l_start));
            if (line.size() > 76) line = line.substr(0, 73) + "...";
            canvas_.draw_text(px + 20, cur_y, line, kColorTextDim);
            cur_y += 16;
            l_start = l_end + 1;
            ++line_count;
        }

        if (!health_report_.empty()) {
            cur_y += 8;
            canvas_.draw_text(px + 16, cur_y, "Health Note: " + health_report_.substr(0, 60), kColorHealthNotice);
        }

        if (session_ && !child_browser_mode_) {
            cur_y += 14;
            std::string cur_fmt = (current_tex_format_idx_ >= 0 && current_tex_format_idx_ < static_cast<int>(tex_format_names_.size()))
                                  ? tex_format_names_[current_tex_format_idx_] : "Default (PS2/Retail)";
            canvas_.draw_text(px + 16, cur_y, "Texture Compression: " + cur_fmt + "  [Press 'T' to cycle]", kColorAccent);
        }

        canvas_.draw_text_centered(px, py + ph - 28, pw, 20, "[ Press 0-3 or Tab to switch topics | 'I' or [X] to close ]", kColorAccent);
    }

    if (show_kit_panel_) {
        const int pw = std::min(580, window_width_ - 40);
        const int ph = std::min(400, window_height_ - 80);
        const int px = (window_width_ - pw) / 2;
        const int py = (window_height_ - ph) / 2;

        canvas_.fill_rect(px, py, pw, ph, 0x14141A);
        canvas_.draw_rect_outline(px, py, pw, ph, kColorAccent);

        canvas_.fill_rect(px, py, pw, 34, 0x1C1C24);
        canvas_.draw_text(px + 14, py + 10, "Character Kit & Weapon Attachments", kColorAccent);
        canvas_.draw_text(px + pw - 30, py + 10, "[X]", kColorTextDim);

        int cur_y = py + 46;
        for (size_t i = 0; i < kit_items_.size() && i < 8; ++i) {
            const auto& item = kit_items_[i];
            const std::string status = item.active ? "[LOADED]" : (item.found_on_disk ? "[OFF]" : "[NOT FOUND]");
            const uint32_t scolor = item.active ? kColorHealthClean : (item.found_on_disk ? kColorTextDim : kColorHealthProblem);
            canvas_.draw_text(px + 16, cur_y, std::to_string(i + 1) + ". " + item.label + " (" + item.file + ")", kColorText);
            canvas_.draw_text(px + pw - 120, cur_y, status, scolor);
            cur_y += 24;
        }

        canvas_.fill_rect(px + 16, py + ph - 44, 180, 28, kColorBtnHover);
        canvas_.draw_rect_outline(px + 16, py + ph - 44, 180, 28, kColorAccent);
        canvas_.draw_text_centered(px + 16, py + ph - 44, 180, 28, "Add Whole Kit", kColorAccent);

        canvas_.draw_text_centered(px + 210, py + ph - 40, pw - 220, 20, "[Press number key 1-8 to toggle]", kColorTextDim);
    }

    if (show_file_browser_) {
        const int pw = std::min(740, window_width_ - 40);
        const int ph = std::min(520, window_height_ - 80);
        const int px = (window_width_ - pw) / 2;
        const int py = (window_height_ - ph) / 2;

        canvas_.fill_rect(px, py, pw, ph, 0x14141E);
        canvas_.draw_rect_outline(px, py, pw, ph, kColorAccent);

        canvas_.fill_rect(px, py, pw, 34, 0x1E1E2A);
        canvas_.draw_rect_outline(px, py, pw, 34, kColorBtnBorder);
        std::string title_dir = "Browse: " + browser_dir_.string();
        if (title_dir.size() > 62) title_dir = "..." + title_dir.substr(title_dir.size() - 59);
        canvas_.draw_text(px + 14, py + 10, title_dir, kColorAccent);
        canvas_.draw_text(px + pw - 30, py + 10, "[X]", kColorTextDim);

        const int item_h = 24;
        const int list_y = py + 40;
        const int max_visible = (ph - 78) / item_h;
        const int total_items = static_cast<int>(browser_entries_.size());
        if (max_visible > 0) {
            browser_scroll_ = std::clamp(browser_scroll_, 0, std::max(0, total_items - max_visible));
        }

        for (int i = 0; i < max_visible; ++i) {
            const int idx = browser_scroll_ + i;
            if (idx >= total_items) break;
            auto& entry = browser_entries_[idx];
            const int ey = list_y + i * item_h;
            entry.rect = {px + 8, ey, pw - 16, item_h};

            const bool is_hover = (hover_browser_idx_ == idx);
            if (is_hover) {
                canvas_.fill_rect(entry.rect.x, entry.rect.y, entry.rect.w, entry.rect.h, kColorBtnHover);
                canvas_.draw_rect_outline(entry.rect.x, entry.rect.y, entry.rect.w, entry.rect.h, kColorBtnBorder);
            }

            if (entry.is_dir) {
                canvas_.draw_text(entry.rect.x + 8, ey + 5, "[DIR]  " + entry.name, kColorAccent);
            } else {
                uint32_t fcol = kColorText;
                const std::string ext = entry.full_path.extension().string();
                if (ext == ".pac") fcol = 0x00FFCC00;
                else if (ext == ".mod") fcol = 0x0055D0FF;
                else if (ext == ".ptx" || ext == ".tm2" || ext == ".dds") fcol = 0x00FF88AA;
                else if (ext == ".mot") fcol = 0x0066FF88;
                canvas_.draw_text(entry.rect.x + 8, ey + 5, "       " + entry.name, fcol);
            }
        }

        canvas_.fill_rect(px, py + ph - 32, pw, 32, 0x181822);
        canvas_.draw_rect_outline(px, py + ph - 32, pw, 32, kColorBtnBorder);
        std::string scroll_info = "Items " + std::to_string(total_items > 0 ? (browser_scroll_ + 1) : 0) +
                                  "-" + std::to_string(std::min(total_items, browser_scroll_ + max_visible)) +
                                  " of " + std::to_string(total_items) + "  |  [Wheel/Arrows] Scroll  [ESC] Close";
        canvas_.draw_text(px + 14, py + ph - 22, scroll_info, kColorTextDim);
    }
}

void App::present_frame() {
    if (!host_.display || !host_.window) return;

    if (!host_.ximage || host_.ximage->width != window_width_ || host_.ximage->height != window_height_) {
        if (host_.ximage) {
            host_.ximage->data = nullptr;
            XDestroyImage(host_.ximage);
            host_.ximage = nullptr;
        }
        host_.ximage = XCreateImage(
            host_.display, host_.visual, host_.depth, ZPixmap, 0,
            reinterpret_cast<char*>(canvas_.data()),
            static_cast<unsigned int>(window_width_), static_cast<unsigned int>(window_height_),
            32, 0);
    }

    if (host_.ximage) {
        host_.ximage->data = reinterpret_cast<char*>(canvas_.data());
        XPutImage(
            host_.display, host_.window, host_.gc, host_.ximage,
            0, 0, 0, 0,
            static_cast<unsigned int>(window_width_), static_cast<unsigned int>(window_height_));
    }

    XFlush(host_.display);
}

// ---- Event Handling & Interaction -----------------------------------------

void App::handle_button_click(int id) {
    switch (id) {
        case kBtnBack:
            go_back();
            break;
        case kBtnBrowse:
            toggle_file_browser();
            break;
        case kBtnSave:
            export_source_file();
            break;
        case kBtnOpen:
            trigger_open_dialog();
            break;
        case kBtnKit:
            toggle_kit_panel();
            break;
        case kBtnWireframe:
            render_flags_ ^= dmcresource::render_flag(dmcresource::RenderFlag::Wireframe);
            break;
        case kBtnHierarchy:
            render_flags_ ^= dmcresource::render_flag(dmcresource::RenderFlag::Hierarchy);
            break;
        case kBtnNormals:
            render_flags_ ^= dmcresource::render_flag(dmcresource::RenderFlag::Normals);
            break;
        case kBtnBounds:
            render_flags_ ^= dmcresource::render_flag(dmcresource::RenderFlag::Bounds);
            break;
        case kBtnUv:
            toggle_uv_view();
            break;
        case kBtnInfo:
            toggle_info_panel();
            break;
        case kBtnCollision:
            cycle_hitboxes();
            break;
        case kBtnSound:
            play_next_sound();
            break;
        case kBtnEvents:
            trigger_next_class_event();
            break;
        case kBtnEffects:
            trigger_next_effect();
            break;
        case kBtnLook:
            cycle_variant();
            break;
        case kBtnMotion:
            cycle_motion();
            break;
        case kBtnTexFormat:
            cycle_texture_format();
            break;
        case kBtnCamera:
            toggle_camera_mode();
            break;
        case kBtnResetView:
            reset_view();
            break;
        default:
            break;
    }
}

void App::handle_key_press(KeySym sym) {
    if (show_file_browser_) {
        switch (sym) {
            case XK_Escape:
                show_file_browser_ = false;
                return;
            case XK_Up:
                browser_scroll_ = std::max(0, browser_scroll_ - 1);
                return;
            case XK_Down:
                browser_scroll_ += 1;
                return;
            case XK_Page_Up:
                browser_scroll_ = std::max(0, browser_scroll_ - 5);
                return;
            case XK_Page_Down:
                browser_scroll_ += 5;
                return;
            case XK_BackSpace:
                if (browser_dir_.has_parent_path() && browser_dir_.parent_path() != browser_dir_) {
                    refresh_file_browser(browser_dir_.parent_path());
                }
                return;
            default:
                break;
        }
    }

    if (show_info_panel_) {
        if (sym >= XK_0 && sym <= XK_3) {
            info_topic_ = static_cast<int>(sym - XK_0);
            return;
        }
        if (sym == XK_Tab) {
            info_topic_ = (info_topic_ + 1) % 4;
            return;
        }
    }

    if (show_kit_panel_ && sym >= XK_1 && sym <= XK_9) {
        const size_t idx = static_cast<size_t>(sym - XK_1);
        if (idx < kit_items_.size()) {
            toggle_kit_item(idx);
            return;
        }
    }

    switch (sym) {
        case XK_Escape:
            if (show_file_browser_) show_file_browser_ = false;
            else if (show_info_panel_) show_info_panel_ = false;
            else if (show_kit_panel_) show_kit_panel_ = false;
            else if (!nav_stack_.empty()) go_back();
            else running_ = false;
            break;
        case XK_f:
        case XK_F:
            toggle_fly_mode();
            break;
        case XK_r:
        case XK_R:
        case XK_0:
            reset_view();
            break;
        case XK_space:
            toggle_animation();
            break;
        case XK_bracketleft:
            step_animation(-1.0f);
            break;
        case XK_bracketright:
            step_animation(1.0f);
            break;
        case XK_h:
        case XK_H:
            cycle_hitboxes();
            break;
        case XK_i:
        case XK_I:
            toggle_info_panel();
            break;
        case XK_u:
        case XK_U:
            toggle_uv_view();
            break;
        case XK_k:
        case XK_K:
            if (dmc_camera_mode()) {
                camera_focus_lock();
            } else {
                toggle_kit_panel();
            }
            break;
        case XK_v:
        case XK_V:
            toggle_camera_mode();
            break;
        case XK_l:
        case XK_L:
            if (dmc_camera_mode()) {
                camera_level();
            }
            break;
        case XK_comma:
            if (dmc_camera_mode()) {
                camera_focus_ring(-1.0f);
            }
            break;
        case XK_period:
            if (dmc_camera_mode()) {
                camera_focus_ring(1.0f);
            }
            break;
        case XK_p:
        case XK_P:
            if (dmc_camera_mode()) {
                capture_photo_png();
            } else {
                export_high_res_png();
            }
            break;
        case XK_b:
        case XK_B:
            room_broken_ = !room_broken_;
            dmc_room_set_broken(room_broken_);
            camera_message_ = room_broken_ ? "Destruction: BROKEN" : "Destruction: INTACT";
            break;
        case XK_n:
        case XK_N:
            {
                dmc_base_spot spot = dmc_next_base_spot();
                if (spot.title) {
                    camera_message_ = "Floor Spot: " + std::string(spot.title);
                }
                dmc_base_spot_free(&spot);
            }
            break;
        case XK_t:
        case XK_T:
            if (session_ && dmc_has_dual_preview(session_)) {
                dual_info_preview_ = !dual_info_preview_;
                dmc_set_info_preview(session_, dual_info_preview_);
                camera_message_ = dual_info_preview_ ? "Preview: Info Layout" : "Preview: Diffuse Texture";
            } else if (session_) {
                cycle_texture_format();
            }
            break;
        case XK_s:
        case XK_S:
            if (!fly_mode_) {
                play_next_sound();
            } else {
                fly_key_s_ = true;
            }
            break;
        case XK_e:
        case XK_E:
            if (!fly_mode_) {
                trigger_next_class_event();
            }
            break;
        case XK_x:
        case XK_X:
            trigger_next_effect();
            break;
        case XK_c:
        case XK_C:
            cycle_variant();
            break;
        case XK_m:
        case XK_M:
            cycle_motion();
            break;
        case XK_o:
        case XK_O:
            toggle_file_browser();
            break;
        case XK_w:
        case XK_W:
            fly_key_w_ = true;
            break;
        case XK_a:
        case XK_A:
            fly_key_a_ = true;
            break;
        case XK_s:
        case XK_S:
            fly_key_s_ = true;
            break;
        case XK_d:
        case XK_D:
            fly_key_d_ = true;
            break;
        case XK_Up:
            fly_key_up_ = true;
            break;
        case XK_Down:
            fly_key_down_ = true;
            break;
        case XK_Left:
            fly_key_left_ = true;
            break;
        case XK_Right:
            fly_key_right_ = true;
            break;
        default:
            break;
    }
}

void App::handle_key_release(KeySym sym) {
    switch (sym) {
        case XK_w:
        case XK_W:
            fly_key_w_ = false;
            break;
        case XK_a:
        case XK_A:
            fly_key_a_ = false;
            break;
        case XK_s:
        case XK_S:
            fly_key_s_ = false;
            break;
        case XK_d:
        case XK_D:
            fly_key_d_ = false;
            break;
        case XK_Up:
            fly_key_up_ = false;
            break;
        case XK_Down:
            fly_key_down_ = false;
            break;
        case XK_Left:
            fly_key_left_ = false;
            break;
        case XK_Right:
            fly_key_right_ = false;
            break;
        default:
            break;
    }
}

void App::toggle_fly_mode() {
    if (!session_) return;
    if (fly_mode_) {
        dmc_fly_leave(session_, view_.yaw, view_.pitch);
        fly_mode_ = false;
        std::cout << "Left fly mode.\n";
    } else {
        const int viewport_h = std::max(64, window_height_ - kHeaderHeight - kToolbarHeight);
        if (dmc_fly_enter(session_, window_width_, viewport_h, &view_)) {
            fly_mode_ = true;
            std::cout << "Entered fly mode.\n";
        } else {
            std::cout << "Current session cannot enter fly mode.\n";
        }
    }
}

void App::toggle_animation() {
    if (!session_) return;
    animation_playing_ = !animation_playing_;
    if (animation_playing_ && !std::isfinite(view_.motion_frame)) {
        view_.motion_frame = 0.0f;
    }
}

void App::step_animation(float delta) {
    if (!session_) return;
    if (!std::isfinite(view_.motion_frame)) view_.motion_frame = 0.0f;
    view_.motion_frame += delta;
    if (view_.motion_frame < 0.0f) view_.motion_frame = 0.0f;
    if (motion_end_frame_ > motion_loop_start_ && view_.motion_frame > motion_end_frame_) {
        const float span = motion_end_frame_ - motion_loop_start_;
        if (span > 0.001f) {
            view_.motion_frame = motion_loop_start_ + std::fmod(view_.motion_frame - motion_end_frame_, span);
        } else {
            view_.motion_frame = motion_loop_start_;
        }
    }
}

void App::play_next_sound() {
    if (!session_) return;
    dmc_menu* menu = dmc_sound_menu(session_);
    if (!menu) return;
    if (menu->count == 0) {
        dmc_menu_free(menu);
        return;
    }
    if (current_sound_index_ >= static_cast<int>(menu->count)) {
        current_sound_index_ = 0;
    }
    dmc_pcm pcm = dmc_sound_pcm(session_, current_sound_index_);
    if (pcm.samples && pcm.sample_count > 0) {
        std::cout << "Playing sound [" << current_sound_index_ << "]: "
                  << (menu->items[current_sound_index_].title ? menu->items[current_sound_index_].title : "")
                  << " (" << pcm.sample_rate << " Hz)\n";
        audio_.play_pcm(pcm.samples, pcm.sample_count, pcm.sample_rate);
    }
    current_sound_index_ = (current_sound_index_ + 1) % static_cast<int>(menu->count);
    dmc_pcm_free(&pcm);
    dmc_menu_free(menu);
}

void App::trigger_next_class_event() {
    if (!session_ || class_events_.empty()) return;
    active_class_event_ = (active_class_event_ + 1) % static_cast<int>(class_events_.size());
    const bool ok = dmc_trigger_class_event(session_, static_cast<size_t>(active_class_event_));
    camera_message_ = "Class Event [" + std::to_string(active_class_event_ + 1) + "/" +
                      std::to_string(class_events_.size()) + "]: " +
                      class_events_[active_class_event_] + (ok ? " (Fired)" : " (Failed)");
    std::cout << camera_message_ << "\n";
}

void App::trigger_next_effect() {
    if (!session_ || effect_items_.empty()) return;
    active_effect_idx_ = (active_effect_idx_ + 1) % static_cast<int>(effect_items_.size());
    const auto& item = effect_items_[active_effect_idx_];
    dmc_outcome out = dmc_play_effect(session_, item.first);
    std::string msg = (out.message && *out.message) ? out.message : (out.ok ? "Playing" : "Failed");
    camera_message_ = "Effect [" + std::to_string(active_effect_idx_ + 1) + "/" +
                      std::to_string(effect_items_.size()) + "]: " + item.second + " (" + msg + ")";
    std::cout << camera_message_ << "\n";
    dmc_outcome_free(&out);
}

void App::cycle_motion() {
    if (!session_ || motion_cards_.empty()) return;
    current_motion_idx_ = (current_motion_idx_ + 1) % static_cast<int>(motion_cards_.size());
    const auto& card = motion_cards_[current_motion_idx_];
    char* rep = dmc_load_library_motion(session_, static_cast<size_t>(card.library_index));
    std::string report = rep ? rep : "";
    if (rep) dmc_string_free(rep);

    dmc_motion_range(session_, &motion_end_frame_, &motion_loop_start_);
    view_.motion_frame = motion_loop_start_;
    animation_playing_ = true;

    camera_message_ = "Motion [" + std::to_string(current_motion_idx_ + 1) + "/" +
                      std::to_string(motion_cards_.size()) + "]: " + card.name;
    std::cout << camera_message_ << " (" << report << ")\n";
}

void App::cycle_texture_format() {
    if (!session_) return;
    if (tex_format_names_.empty()) {
        dmc_strings* names = dmc_texture_format_names();
        dmc_strings* labels = dmc_texture_format_labels();
        if (names && labels) {
            const size_t cnt = dmc_strings_count(names);
            for (size_t i = 0; i < cnt; ++i) {
                const char* n = dmc_strings_at(names, i);
                const char* l = dmc_strings_at(labels, i);
                if (n && l) {
                    tex_format_names_.emplace_back(n);
                    tex_format_labels_.emplace_back(l);
                }
            }
        }
        if (names) dmc_strings_free(names);
        if (labels) dmc_strings_free(labels);
    }
    if (tex_format_names_.empty()) return;

    current_tex_format_idx_ = (current_tex_format_idx_ + 1) % static_cast<int>(tex_format_names_.size());
    const std::string& fmt = tex_format_names_[current_tex_format_idx_];

    char* detail = nullptr;
    dmc_session* reencoded = dmc_reencode_textures(session_, fmt.c_str(), false, &detail);
    if (reencoded) {
        dmc_close(session_);
        session_ = reencoded;
        current_title_ = current_path_.filename().string() + " · " + fmt;
        refresh_session_metadata();
        camera_message_ = "Texture Re-encoded: " + fmt + " (" + (detail ? std::string(detail) : "OK") + ")";
        std::cout << "[Texture] " << camera_message_ << "\n";
    } else {
        camera_message_ = fmt + ": " + (detail ? std::string(detail) : "No texture to re-encode");
        std::cout << "[Texture] " << camera_message_ << "\n";
    }
    if (detail) dmc_string_free(detail);
}

void App::cycle_hitboxes() {
    if (!session_) return;
    if (hitbox_attack_ == -2) {
        hitbox_attack_ = -1; // All attacks
    } else if (hitbox_attack_ == -1 && !attack_ids_.empty()) {
        hitbox_attack_ = attack_ids_[0];
    } else if (hitbox_attack_ >= 0) {
        auto it = std::find(attack_ids_.begin(), attack_ids_.end(), hitbox_attack_);
        if (it != attack_ids_.end() && (it + 1) != attack_ids_.end()) {
            hitbox_attack_ = *(it + 1);
        } else {
            hitbox_attack_ = -2; // Off
        }
    } else {
        hitbox_attack_ = -2;
    }

    if (hitbox_attack_ == -2) {
        render_flags_ &= ~dmcresource::render_flag(dmcresource::RenderFlag::Collision);
    } else {
        render_flags_ |= dmcresource::render_flag(dmcresource::RenderFlag::Collision);
        char* desc = dmc_select_collision_attack(session_, hitbox_attack_);
        if (desc) dmc_string_free(desc);
    }
}

void App::toggle_info_panel() {
    show_info_panel_ = !show_info_panel_;
    if (show_info_panel_) show_kit_panel_ = false;
}

void App::toggle_kit_panel() {
    show_kit_panel_ = !show_kit_panel_;
    if (show_kit_panel_) show_info_panel_ = false;
}

void App::toggle_uv_view() {
    if (!session_) return;
    dmc_session* gallery = dmc_open_uv_gallery(session_);
    if (gallery) {
        nav_stack_.push_back({session_, current_title_, current_path_.string(), view_});
        session_ = gallery;
        current_title_ = "UV Gallery";
        child_count_ = dmc_child_count(session_);
        child_browser_mode_ = true;
    } else {
        render_flags_ ^= dmcresource::render_flag(dmcresource::RenderFlag::UvLayout);
    }
}

void App::reset_view() {
    float start[3] = {0.0f, 0.0f, 1.0f};
    if (session_ && dmc_stage_start_view(session_, start)) {
        view_.yaw = start[0];
        view_.pitch = start[1];
        view_.zoom = start[2];
        view_.pan_x = 0.0f;
        view_.pan_y = 0.0f;
        view_.room_yaw = 0.0f;
    } else {
        dmc_view_reset(&view_);
    }
}

void App::init_camera() {
    dmc_camera_settings* list = dmc_camera_settings_list();
    if (!list) return;
    const size_t count = dmc_camera_settings_count(list);
    camera_choices_.clear();
    for (size_t i = 0; i < count; ++i) {
        const dmc_camera_setting* s = dmc_camera_settings_at(list, i);
        if (s) {
            camera_choices_.push_back(s->start);
        }
    }
    dmc_camera_settings_free(list);
}

void App::toggle_camera_mode() {
    if (camera_choices_.empty()) {
        init_camera();
    }
    dmc_camera_settings* list = dmc_camera_settings_list();
    if (!list) return;
    const size_t count = dmc_camera_settings_count(list);
    int finder_idx = -1;
    for (size_t i = 0; i < count; ++i) {
        const dmc_camera_setting* s = dmc_camera_settings_at(list, i);
        if (s && s->key == 0) { // Key::FinderMode
            finder_idx = static_cast<int>(i);
            break;
        }
    }
    dmc_camera_settings_free(list);

    if (finder_idx >= 0 && finder_idx < static_cast<int>(camera_choices_.size())) {
        const bool currently_on = dmc_camera_mode();
        camera_choices_[finder_idx] = currently_on ? 0 : 1;
        dmc_camera_set_choices(camera_choices_.data(), camera_choices_.size());
        camera_mode_ = dmc_camera_mode();
        camera_message_ = camera_mode_ ? "Photo Camera Viewfinder Active" : "Normal 3D View";
        std::cout << "Camera mode toggled: " << (camera_mode_ ? "ON" : "OFF") << "\n";
    }
}

void App::camera_focus_at(int x, int y) {
    if (!session_ || !dmc_camera_mode()) return;
    const int viewport_h = std::max(64, window_height_ - kHeaderHeight - kToolbarHeight);
    char* line = dmc_camera_focus_at(session_, window_width_, viewport_h, &view_, static_cast<float>(x), static_cast<float>(y));
    if (line) {
        camera_message_ = line;
        dmc_string_free(line);
    } else {
        camera_message_ = "Nothing in focus there.";
    }
}

void App::camera_focus_ring(float steps) {
    char* line = dmc_camera_focus_ring(steps);
    if (line) {
        camera_message_ = line;
        dmc_string_free(line);
    }
}

void App::camera_focus_lock() {
    char* line = dmc_camera_focus_lock();
    if (line) {
        camera_message_ = line;
        dmc_string_free(line);
    }
}

void App::camera_level() {
    dmc_camera_level();
    camera_message_ = "Camera Leveled";
}

void App::step_camera_dial(int key) {
    dmc_camera_settings* list = dmc_camera_settings_list();
    if (!list) return;
    const size_t count = dmc_camera_settings_count(list);
    int target_idx = -1;
    size_t opt_count = 0;
    for (size_t i = 0; i < count; ++i) {
        const dmc_camera_setting* s = dmc_camera_settings_at(list, i);
        if (s && s->key == key) {
            target_idx = static_cast<int>(i);
            opt_count = s->option_count;
            break;
        }
    }
    dmc_camera_settings_free(list);

    if (target_idx >= 0 && opt_count > 0 && target_idx < static_cast<int>(camera_choices_.size())) {
        const int next_opt = (camera_choices_[target_idx] + 1) % static_cast<int>(opt_count);
        camera_choices_[target_idx] = next_opt;

        dmc_camera_follow_up follow{};
        if (dmc_camera_follow_up_of(key, next_opt, &follow)) {
            for (size_t f = 0; f < follow.count; ++f) {
                const int f_key = follow.keys[f];
                dmc_camera_settings* l2 = dmc_camera_settings_list();
                if (l2) {
                    for (size_t i = 0; i < dmc_camera_settings_count(l2); ++i) {
                        const dmc_camera_setting* s2 = dmc_camera_settings_at(l2, i);
                        if (s2 && s2->key == f_key && i < camera_choices_.size()) {
                            camera_choices_[i] = follow.options[f];
                            break;
                        }
                    }
                    dmc_camera_settings_free(l2);
                }
            }
            if (follow.message && follow.message[0] != '\0') {
                camera_message_ = follow.message;
            }
            dmc_camera_follow_up_free(&follow);
        }
        dmc_camera_set_choices(camera_choices_.data(), camera_choices_.size());
    }
}

void App::capture_photo_png() {
    if (!session_) return;
    const int viewport_h = std::max(64, window_height_ - kHeaderHeight - kToolbarHeight);
    dmc_image* img = dmc_render_capture(session_, false, window_width_, viewport_h, &view_, 60.0f);
    if (!img) {
        std::cerr << "error: dmc_render_capture failed\n";
        return;
    }
    const auto now = std::chrono::system_clock::now();
    const auto epoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    const std::string filename = "dmc_capture_" + std::to_string(epoch) + ".png";
    if (linux_png::SaveRgbaToPng(filename, img->width, img->height, img->rgba)) {
        camera_message_ = "Saved capture: " + filename;
        std::cout << "Captured photo saved to: " << filename << " (" << img->width << "x" << img->height << ")\n";
    }
    dmc_image_free(img);
}

void App::export_high_res_png() {
    if (!session_) return;
    dmc_image* exp_img = dmc_render_export(session_);
    if (!exp_img || !exp_img->rgba) {
        capture_photo_png();
        return;
    }
    const auto now = std::chrono::system_clock::now();
    const auto epoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    const std::string filename = "dmc_export_" + std::to_string(epoch) + ".png";
    if (linux_png::SaveRgbaToPng(filename, exp_img->width, exp_img->height, exp_img->rgba)) {
        camera_message_ = "Exported Pro PNG: " + filename + " (" + std::to_string(exp_img->width) + "x" + std::to_string(exp_img->height) + ")";
        std::cout << "[Export] " << camera_message_ << "\n";
    }
    dmc_image_free(exp_img);
}

void App::export_source_file() {
    if (!session_) return;
    const char* src_name = nullptr;
    const uint8_t* src_bytes = nullptr;
    size_t src_size = 0;
    if (dmc_source(session_, &src_name, &src_bytes, &src_size) && src_bytes && src_size > 0) {
        std::string fname = src_name ? src_name : "exported.pac";
        std::ofstream out(fname, std::ios::binary);
        if (out) {
            out.write(reinterpret_cast<const char*>(src_bytes), src_size);
            camera_message_ = "Saved file: " + fname + " (" + std::to_string(src_size) + " bytes)";
            std::cout << "[Save] " << camera_message_ << "\n";
        }
    } else {
        camera_message_ = "Current session has no serializable source file.";
        std::cout << "[Save] " << camera_message_ << "\n";
    }
}

namespace {
fs::path get_xdg_settings_path() {
    const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
    fs::path base_dir;
    if (xdg_config && *xdg_config) {
        base_dir = fs::path(xdg_config) / "dmc-native-reader";
    } else {
        const char* home = std::getenv("HOME");
        base_dir = (home && *home) ? (fs::path(home) / ".config" / "dmc-native-reader") : fs::current_path();
    }
    return base_dir / "settings.json";
}
} // namespace

void App::load_settings() {
    try {
        const fs::path p = get_xdg_settings_path();
        if (!fs::exists(p)) return;
        std::ifstream f(p);
        if (!f) return;
        std::string line;
        std::vector<std::string> keys;
        std::vector<float> values;
        while (std::getline(f, line)) {
            size_t eq = line.find(':');
            if (eq == std::string::npos) eq = line.find('=');
            if (eq != std::string::npos) {
                std::string k = line.substr(0, eq);
                k.erase(std::remove_if(k.begin(), k.end(), [](char c) { return c == '"' || c == ' ' || c == '\t'; }), k.end());
                std::string v_str = line.substr(eq + 1);
                v_str.erase(std::remove_if(v_str.begin(), v_str.end(), [](char c) { return c == ',' || c == ' ' || c == '\t' || c == '\r' || c == '\n'; }), v_str.end());
                try {
                    float v = std::stof(v_str);
                    keys.push_back(k);
                    values.push_back(v);
                } catch (...) {}
            }
        }
        if (!keys.empty()) {
            std::vector<const char*> key_ptrs;
            for (const auto& k : keys) key_ptrs.push_back(k.c_str());
            dmc_applied_settings applied = dmc_apply_settings(key_ptrs.data(), values.data(), keys.size());
            render_flags_ = applied.render_flags;
            view_.render_flags = applied.render_flags;
            std::cout << "[Settings] Loaded " << keys.size() << " user settings from " << p.string() << "\n";
        }
    } catch (...) {}
}

void App::save_settings() {
    try {
        const fs::path p = get_xdg_settings_path();
        fs::create_directories(p.parent_path());
        std::ofstream f(p);
        if (!f) return;
        f << "{\n";
        f << "  \"render_flags\": " << render_flags_ << ",\n";
        f << "  \"smooth_textures\": " << ((render_flags_ & dmcresource::render_flag(dmcresource::RenderFlag::SmoothTextures)) ? 1 : 0) << ",\n";
        f << "  \"unlit\": " << ((render_flags_ & dmcresource::render_flag(dmcresource::RenderFlag::Unlit)) ? 1 : 0) << "\n";
        f << "}\n";
    } catch (...) {}
}

void App::run() {
    auto last_tick = std::chrono::steady_clock::now();

    while (running_) {
        while (host_.display && XPending(host_.display) > 0) {
            XEvent ev;
            XNextEvent(host_.display, &ev);

            if (ev.type == ClientMessage) {
                if (static_cast<Atom>(ev.xclient.data.l[0]) == host_.wm_delete_window) {
                    running_ = false;
                } else if (ev.xclient.message_type == host_.xdnd_enter) {
                    host_.xdnd_source_window = static_cast<Window>(ev.xclient.data.l[0]);
                } else if (ev.xclient.message_type == host_.xdnd_position) {
                    XEvent reply{};
                    reply.type = ClientMessage;
                    reply.xclient.window = host_.xdnd_source_window;
                    reply.xclient.message_type = host_.xdnd_status;
                    reply.xclient.format = 32;
                    reply.xclient.data.l[0] = static_cast<long>(host_.window);
                    reply.xclient.data.l[1] = 1; // Accept drop
                    reply.xclient.data.l[2] = 0;
                    reply.xclient.data.l[3] = 0;
                    reply.xclient.data.l[4] = static_cast<long>(host_.xdnd_action_copy);
                    XSendEvent(host_.display, host_.xdnd_source_window, False, NoEventMask, &reply);
                    XFlush(host_.display);
                } else if (ev.xclient.message_type == host_.xdnd_drop) {
                    const Time drop_time = static_cast<Time>(ev.xclient.data.l[2]);
                    XConvertSelection(host_.display, host_.xdnd_selection, host_.text_uri_list,
                                     host_.primary, host_.window, drop_time);
                }
            } else if (ev.type == SelectionNotify) {
                if (ev.xselection.property != None) {
                    Atom actual_type;
                    int actual_format;
                    unsigned long count = 0, bytes_after = 0;
                    unsigned char* data = nullptr;
                    XGetWindowProperty(host_.display, host_.window, ev.xselection.property,
                                       0, 65536, False, AnyPropertyType,
                                       &actual_type, &actual_format, &count, &bytes_after, &data);
                    if (data && count > 0) {
                        std::string uris(reinterpret_cast<char*>(data), count);
                        XFree(data);
                        size_t pos = uris.find("file://");
                        if (pos != std::string::npos) {
                            size_t end_pos = uris.find_first_of("\r\n", pos);
                            std::string uri = (end_pos == std::string::npos) ? uris.substr(pos) : uris.substr(pos, end_pos - pos);
                            std::string path_str = uri.substr(7);
                            std::string decoded_path;
                            for (size_t i = 0; i < path_str.size(); ++i) {
                                if (path_str[i] == '%' && i + 2 < path_str.size()) {
                                    int ch = 0;
                                    if (std::sscanf(path_str.substr(i + 1, 2).c_str(), "%x", &ch) == 1) {
                                        decoded_path += static_cast<char>(ch);
                                        i += 2;
                                        continue;
                                    }
                                }
                                decoded_path += path_str[i];
                            }
                            if (!decoded_path.empty()) {
                                std::cout << "[XDND] Drag-and-drop opening file: " << decoded_path << "\n";
                                open_file(decoded_path);
                            }
                        }
                    }
                    if (host_.xdnd_source_window != 0) {
                        XEvent fin{};
                        fin.type = ClientMessage;
                        fin.xclient.window = host_.xdnd_source_window;
                        fin.xclient.message_type = host_.xdnd_finished;
                        fin.xclient.format = 32;
                        fin.xclient.data.l[0] = static_cast<long>(host_.window);
                        fin.xclient.data.l[1] = 1;
                        fin.xclient.data.l[2] = static_cast<long>(host_.xdnd_action_copy);
                        XSendEvent(host_.display, host_.xdnd_source_window, False, NoEventMask, &fin);
                        XFlush(host_.display);
                        host_.xdnd_source_window = 0;
                    }
                }
            } else if (ev.type == ConfigureNotify) {
                if (ev.xconfigure.width != window_width_ || ev.xconfigure.height != window_height_) {
                    update_layout(ev.xconfigure.width, ev.xconfigure.height);
                }
            } else if (ev.type == KeyPress) {
                const KeySym sym = XLookupKeysym(&ev.xkey, 0);
                handle_key_press(sym);
            } else if (ev.type == KeyRelease) {
                const KeySym sym = XLookupKeysym(&ev.xkey, 0);
                handle_key_release(sym);
            } else if (ev.type == ButtonPress) {
                const int mx = ev.xbutton.x;
                const int my = ev.xbutton.y;

                if (show_file_browser_) {
                    const int pw = std::min(740, window_width_ - 40);
                    const int ph = std::min(520, window_height_ - 80);
                    const int px = (window_width_ - pw) / 2;
                    const int py = (window_height_ - ph) / 2;

                    if (ev.xbutton.button == Button4) {
                        browser_scroll_ = std::max(0, browser_scroll_ - 3);
                        continue;
                    } else if (ev.xbutton.button == Button5) {
                        const int item_h = 24;
                        const int max_vis = (ph - 78) / item_h;
                        const int max_scroll = std::max(0, static_cast<int>(browser_entries_.size()) - max_vis);
                        browser_scroll_ = std::min(max_scroll, browser_scroll_ + 3);
                        continue;
                    }

                    if (ev.xbutton.button == Button1) {
                        if (mx >= px + pw - 34 && mx <= px + pw - 8 && my >= py + 6 && my <= py + 30) {
                            show_file_browser_ = false;
                            continue;
                        }
                        bool item_clicked = false;
                        for (const auto& entry : browser_entries_) {
                            if (entry.rect.contains(mx, my)) {
                                if (entry.is_dir) {
                                    refresh_file_browser(entry.full_path);
                                } else {
                                    open_file(entry.full_path);
                                    show_file_browser_ = false;
                                }
                                item_clicked = true;
                                break;
                            }
                        }
                        if (item_clicked || (mx >= px && mx <= px + pw && my >= py && my <= py + ph)) {
                            continue;
                        }
                    }
                }

                if (ev.xbutton.button == Button1) {
                    // Check info panel clicks (close and topic tabs)
                    if (show_info_panel_) {
                        const int pw = std::min(680, window_width_ - 40);
                        const int ph = std::min(500, window_height_ - 80);
                        const int px = (window_width_ - pw) / 2;
                        const int py = (window_height_ - ph) / 2;
                        if (mx >= px + pw - 34 && mx <= px + pw - 8 && my >= py + 6 && my <= py + 30) {
                            show_info_panel_ = false;
                            continue;
                        }
                        bool clicked_tab = false;
                        for (int t = 0; t < 4; ++t) {
                            if (info_tab_rects_[t].contains(mx, my)) {
                                info_topic_ = t;
                                clicked_tab = true;
                                break;
                            }
                        }
                        if (clicked_tab || (mx >= px && mx <= px + pw && my >= py && my <= py + ph)) {
                            continue;
                        }
                    }
                    if (show_kit_panel_) {
                        const int pw = std::min(580, window_width_ - 40);
                        const int ph = std::min(400, window_height_ - 80);
                        const int px = (window_width_ - pw) / 2;
                        const int py = (window_height_ - ph) / 2;
                        if (mx >= px + pw - 34 && mx <= px + pw - 8 && my >= py + 6 && my <= py + 30) {
                            show_kit_panel_ = false;
                            continue;
                        }
                        if (mx >= px + 16 && mx <= px + 196 && my >= py + ph - 44 && my <= py + ph - 16) {
                            add_whole_kit();
                            continue;
                        }
                    }

                    // Check Header Buttons
                    bool clicked_btn = false;
                    for (const auto& btn : header_buttons_) {
                        if (btn.visible && btn.enabled && btn.rect.contains(mx, my)) {
                            handle_button_click(btn.id);
                            clicked_btn = true;
                            break;
                        }
                    }
                    // Check Toolbar Buttons
                    if (!clicked_btn) {
                        for (const auto& btn : toolbar_buttons_) {
                            if (btn.visible && btn.enabled && btn.rect.contains(mx, my)) {
                                handle_button_click(btn.id);
                                clicked_btn = true;
                                break;
                            }
                        }
                    }
                    // Check Camera Dials Clicks
                    if (!clicked_btn && dmc_camera_mode()) {
                        for (const auto& db : camera_dial_buttons_) {
                            if (db.rect.contains(mx, my)) {
                                step_camera_dial(db.key);
                                clicked_btn = true;
                                break;
                            }
                        }
                    }
                    // Check Child Gallery Clicks
                    if (!clicked_btn && child_browser_mode_ && child_count_ > 0) {
                        const int viewport_y = kHeaderHeight;
                        const int cols = std::max(1, window_width_ / 200);
                        const int tile_w = 184;
                        const int tile_h = 74;
                        const int pad = 12;
                        for (size_t i = 0; i < child_count_; ++i) {
                            const int col = static_cast<int>(i) % cols;
                            const int row = static_cast<int>(i) / cols;
                            const int tx = pad + col * (tile_w + pad);
                            const int ty = viewport_y + pad + row * (tile_h + pad);
                            Rect tile_rect{tx, ty, tile_w, tile_h};
                            if (tile_rect.contains(mx, my)) {
                                open_child_resource(i);
                                clicked_btn = true;
                                break;
                            }
                        }
                    }

                    if (!clicked_btn && !show_info_panel_ && !show_kit_panel_ && !show_file_browser_) {
                        if (dmc_camera_mode() && my >= kHeaderHeight && my < window_height_ - kToolbarHeight) {
                            camera_focus_at(mx, my - kHeaderHeight);
                        } else if (session_ && !child_browser_mode_ && my >= kHeaderHeight && my < window_height_ - kToolbarHeight) {
                            const int viewport_h = std::max(64, window_height_ - kHeaderHeight - kToolbarHeight);
                            dmc_pick_result pick = dmc_pick(session_, window_width_, viewport_h, &view_,
                                                            static_cast<float>(mx), static_cast<float>(my - kHeaderHeight), true);
                            if (pick.kind == DMC_PICK_MODEL && pick.joint >= 0) {
                                std::string bone = (pick.joint_name && pick.joint_name[0] != '\0') ? pick.joint_name : "Bone";
                                camera_message_ = bone + " (Joint " + std::to_string(pick.joint) + ")";
                            } else if (pick.kind == DMC_PICK_PLACED) {
                                camera_message_ = "Placed on room floor";
                            } else if (pick.marker >= 0 && pick.marker_title) {
                                camera_message_ = "Marker: " + std::string(pick.marker_title);
                            }
                            dmc_pick_result_free(&pick);
                        }
                        is_spinning_ = false;
                        drag_left_ = true;
                        last_mouse_x_ = mx;
                        last_mouse_y_ = my;
                        drag_sample_x_ = mx;
                        drag_sample_y_ = my;
                        drag_sample_time_ = std::chrono::steady_clock::now();
                        drag_velocity_x_ = 0.0f;
                        drag_velocity_y_ = 0.0f;
                    }
                } else if (ev.xbutton.button == Button2) {
                    drag_pan_ = true;
                    last_mouse_x_ = mx;
                    last_mouse_y_ = my;
                } else if (ev.xbutton.button == Button3) {
                    drag_twist_ = true;
                    last_mouse_x_ = mx;
                    last_mouse_y_ = my;
                } else if (ev.xbutton.button == Button4) {
                    // Scroll wheel up (Zoom in)
                    view_.zoom = dmc_pinch_zoom(view_.zoom, 1.1f);
                } else if (ev.xbutton.button == Button5) {
                    // Scroll wheel down (Zoom out)
                    view_.zoom = dmc_pinch_zoom(view_.zoom, 0.9f);
                }
            } else if (ev.type == ButtonRelease) {
                if (ev.xbutton.button == Button1) {
                    if (drag_left_) {
                        drag_left_ = false;
                        if (dmc_fling(drag_velocity_x_, drag_velocity_y_, 1.0f, &spin_)) {
                            is_spinning_ = true;
                        }
                    }
                }
                if (ev.xbutton.button == Button2) drag_pan_ = false;
                if (ev.xbutton.button == Button3) drag_twist_ = false;
            } else if (ev.type == MotionNotify) {
                const int mx = ev.xmotion.x;
                const int my = ev.xmotion.y;

                hover_btn_id_ = -1;
                for (const auto& btn : header_buttons_) {
                    if (btn.visible && btn.rect.contains(mx, my)) hover_btn_id_ = btn.id;
                }
                for (const auto& btn : toolbar_buttons_) {
                    if (btn.visible && btn.rect.contains(mx, my)) hover_btn_id_ = btn.id;
                }

                // File browser entry hover
                hover_browser_idx_ = -1;
                if (show_file_browser_) {
                    for (size_t i = 0; i < browser_entries_.size(); ++i) {
                        if (browser_entries_[i].rect.contains(mx, my)) {
                            hover_browser_idx_ = static_cast<int>(i);
                            break;
                        }
                    }
                }

                // Child item hover
                child_hover_ = -1;
                if (child_browser_mode_ && child_count_ > 0) {
                    const int viewport_y = kHeaderHeight;
                    const int cols = std::max(1, window_width_ / 200);
                    const int tile_w = 184;
                    const int tile_h = 74;
                    const int pad = 12;
                    for (size_t i = 0; i < child_count_; ++i) {
                        const int col = static_cast<int>(i) % cols;
                        const int row = static_cast<int>(i) / cols;
                        const int tx = pad + col * (tile_w + pad);
                        const int ty = viewport_y + pad + row * (tile_h + pad);
                        Rect r{tx, ty, tile_w, tile_h};
                        if (r.contains(mx, my)) {
                            child_hover_ = static_cast<int>(i);
                            break;
                        }
                    }
                }

                if (drag_left_) {
                    const auto now = std::chrono::steady_clock::now();
                    const float dt_sample = std::chrono::duration<float>(now - drag_sample_time_).count();
                    if (dt_sample >= 0.015f) {
                        drag_velocity_x_ = static_cast<float>(mx - drag_sample_x_) / dt_sample;
                        drag_velocity_y_ = static_cast<float>(my - drag_sample_y_) / dt_sample;
                        drag_sample_x_ = mx;
                        drag_sample_y_ = my;
                        drag_sample_time_ = now;
                    }
                    const float dx = static_cast<float>(mx - last_mouse_x_);
                    const float dy = static_cast<float>(my - last_mouse_y_);
                    dmc_view_drag(&view_, dx, dy);
                    last_mouse_x_ = mx;
                    last_mouse_y_ = my;

                    if (session_) {
                        float cam_pos[3];
                        dmc_cutscene_take_camera(session_, cam_pos);
                    }
                } else if (drag_twist_) {
                    const float dx = static_cast<float>(mx - last_mouse_x_);
                    const float radians = dx * 0.01f;
                    dmc_view_twist(&view_, radians, false);
                    last_mouse_x_ = mx;
                    last_mouse_y_ = my;
                } else if (drag_pan_) {
                    const float dx = static_cast<float>(mx - last_mouse_x_);
                    const float dy = static_cast<float>(my - last_mouse_y_);
                    if (dmc_camera_mode()) {
                        const float radians = dx * 0.01f;
                        if (dmc_camera_polarizer_turnable()) {
                            const float angle = dmc_camera_polarizer_turn(radians);
                            camera_message_ = "Polarizer " + std::to_string(static_cast<int>(angle * 180.0f / 3.14159265f)) + " deg";
                        } else {
                            const float roll = dmc_camera_roll(radians);
                            camera_message_ = "Roll " + std::to_string(static_cast<int>(roll * 180.0f / 3.14159265f)) + " deg";
                        }
                    } else {
                        const int viewport_h = std::max(64, window_height_ - kHeaderHeight - kToolbarHeight);
                        dmc_view_pan(&view_, dx, dy, window_width_, viewport_h);
                    }
                    last_mouse_x_ = mx;
                    last_mouse_y_ = my;
                }
            }
        }

        // Time step
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::clamp(std::chrono::duration<float>(now - last_tick).count(), 0.001f, 0.1f);
        last_tick = now;

        // Inertia spin step (dmc_spin_step)
        if (is_spinning_ && !drag_left_) {
            is_spinning_ = dmc_spin_step(&view_, &spin_, dt);
        }

        // Fly mode tick
        if (fly_mode_ && session_) {
            const float reach = 120.0f;
            const float move_dx = (fly_key_d_ ? reach : 0.0f) - (fly_key_a_ ? reach : 0.0f);
            const float move_dy = (fly_key_s_ ? reach : 0.0f) - (fly_key_w_ ? reach : 0.0f);
            const float look_dx = (fly_key_right_ ? reach : 0.0f) - (fly_key_left_ ? reach : 0.0f);
            const float look_dy = (fly_key_down_ ? reach : 0.0f) - (fly_key_up_ ? reach : 0.0f);
            const bool move_held = (move_dx != 0.0f || move_dy != 0.0f);
            const bool look_held = (look_dx != 0.0f || look_dy != 0.0f);

            dmc_fly_step step = dmc_fly_tick(session_, view_.yaw, view_.pitch,
                                             move_held, move_dx, move_dy,
                                             look_held, look_dx, look_dy,
                                             1.0f, dt);
            view_.yaw = step.yaw;
            view_.pitch = step.pitch;
        }

        // Animation playback step
        if (animation_playing_ && session_) {
            if (!std::isfinite(view_.motion_frame)) view_.motion_frame = 0.0f;
            view_.motion_frame += dt * 60.0f;
            if (motion_end_frame_ > motion_loop_start_ && view_.motion_frame > motion_end_frame_) {
                const float span = motion_end_frame_ - motion_loop_start_;
                if (span > 0.001f) {
                    view_.motion_frame = motion_loop_start_ + std::fmod(view_.motion_frame - motion_end_frame_, span);
                } else {
                    view_.motion_frame = motion_loop_start_;
                }
            }
        }

        render_frame();
        present_frame();

        // Target ~60 FPS
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

} // namespace

int main(int argc, char** argv) {
    App app;
    if (!app.init_x11(kDefaultWindowWidth, kDefaultWindowHeight)) {
        return 1;
    }

    if (argc >= 2) {
        app.open_file(argv[1]);
    }

    app.run();
    return 0;
}
