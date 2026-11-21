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

// ---- Extensible Host & EGL/GLES Context Structure -------------------------
struct HostContext {
    Display* display = nullptr;
    Window window = 0;
    Visual* visual = nullptr;
    int depth = 0;
    GC gc = nullptr;
    Atom wm_delete_window = 0;

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
    void play_next_sound();
    void refresh_session_metadata();

    void init_camera();
    void toggle_camera_mode();
    void camera_focus_at(int x, int y);
    void camera_focus_ring(float steps);
    void camera_focus_lock();
    void camera_level();
    void step_camera_dial(int key);
    void capture_photo_png();

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
    bool show_kit_panel_ = false;

    // Character Kit
    std::vector<KitItem> kit_items_{};
    std::string base_character_name_{};
    fs::path base_character_path_{};
    std::string dt_body_file_{};

    // Interaction & Drag
    bool drag_left_ = false;
    bool drag_pan_ = false;
    int last_mouse_x_ = 0;
    int last_mouse_y_ = 0;
    int hover_btn_id_ = -1;

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

    host_.gc = XCreateGC(host_.display, host_.window, 0, nullptr);

    // Disable auto-repeat for smooth game/fly mode key holding
    XAutoRepeatOff(host_.display);

    canvas_.resize(window_width_, window_height_);
    update_layout(window_width_, window_height_);

    XMapWindow(host_.display, host_.window);
    XFlush(host_.display);

    return true;
}

void App::teardown_x11() {
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
        {kBtnOpen, "OPEN", "Open DMC game file", {}, true, true, false},
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
        } else if (btn.id == kBtnOpen) {
            btn.rect = {width - 85, 6, 75, 30};
        }
    }

    // Toolbar buttons centered along bottom
    const int btn_count = static_cast<int>(toolbar_buttons_.size());
    const int btn_w = 78;
    const int btn_h = 32;
    const int spacing = 6;
    const int total_w = btn_count * btn_w + (btn_count - 1) * spacing;
    int tx = std::max(10, (width - total_w) / 2);
    const int ty = height - kToolbarHeight + (kToolbarHeight - btn_h) / 2;

    for (auto& btn : toolbar_buttons_) {
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

    std::cout << "Opened " << path.string() << " (" << current_title_ << ")\n";
    if (host_.display && host_.window) {
        XStoreName(host_.display, host_.window, ("DMC Native Reader - " + current_title_).c_str());
    }
}

void App::load_character_kit(const std::string& archive_name, const fs::path& archive_path) {
    kit_items_.clear();
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

    dmc_session* assembled = dmc_assemble(open_sources.data(), name_ptrs.data(), open_sources.size(), 0);
    for (auto* s : open_sources) dmc_close(s);

    if (assembled) {
        if (session_) dmc_close(session_);
        session_ = assembled;
        current_title_ = base_character_name_ + " (Assembled)";
    }
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
        std::cout << "Open file cancelled or no GUI dialog available.\n";
    }
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
        char anim_buf[96];
        std::snprintf(anim_buf, sizeof(anim_buf), "Frame: %.1f / %.1f  %s  [Space] Play/Pause  [ [ / ] ] Step",
                      view_.motion_frame, motion_end_frame_, animation_playing_ ? "[>] PLAYING" : "[||] PAUSED");
        canvas_.fill_rect(12, viewport_y + viewport_h - 32, 420, 24, 0x181822);
        canvas_.draw_rect_outline(12, viewport_y + viewport_h - 32, 420, 24, kColorBtnBorder);
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
        const int pw = std::min(640, window_width_ - 40);
        const int ph = std::min(460, window_height_ - 80);
        const int px = (window_width_ - pw) / 2;
        const int py = (window_height_ - ph) / 2;

        canvas_.fill_rect(px, py, pw, ph, 0x14141A);
        canvas_.draw_rect_outline(px, py, pw, ph, kColorAccent);

        canvas_.fill_rect(px, py, pw, 34, 0x1C1C24);
        canvas_.draw_text(px + 14, py + 10, "Resource Information & Health Report", kColorAccent);
        canvas_.draw_text(px + pw - 30, py + 10, "[X]", kColorTextDim);

        int cur_y = py + 46;
        canvas_.draw_text(px + 16, cur_y, "File: " + current_path_.filename().string(), kColorText);
        cur_y += 20;

        char* diag_c = session_ ? dmc_diagnostics(session_) : nullptr;
        std::string diag_str = diag_c ? diag_c : "Diagnostics unavailable.";
        if (diag_c) dmc_string_free(diag_c);

        canvas_.draw_text(px + 16, cur_y, "Diagnostics:", kColorAccent);
        cur_y += 18;

        std::string_view lines(diag_str);
        size_t l_start = 0;
        int line_count = 0;
        while (l_start < lines.size() && line_count < 15) {
            size_t l_end = lines.find('\n', l_start);
            if (l_end == std::string_view::npos) l_end = lines.size();
            std::string line(lines.substr(l_start, l_end - l_start));
            if (line.size() > 70) line = line.substr(0, 67) + "...";
            canvas_.draw_text(px + 20, cur_y, line, kColorTextDim);
            cur_y += 16;
            l_start = l_end + 1;
            ++line_count;
        }

        if (!health_report_.empty()) {
            cur_y += 10;
            canvas_.draw_text(px + 16, cur_y, "Health Note: " + health_report_.substr(0, 60), kColorHealthNotice);
        }

        canvas_.draw_text_centered(px, py + ph - 28, pw, 20, "[ Press 'I' or 'Escape' or click [X] to close ]", kColorAccent);
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
    if (show_kit_panel_ && sym >= XK_1 && sym <= XK_9) {
        const size_t idx = static_cast<size_t>(sym - XK_1);
        if (idx < kit_items_.size()) {
            toggle_kit_item(idx);
            return;
        }
    }

    switch (sym) {
        case XK_Escape:
            if (show_info_panel_) show_info_panel_ = false;
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
            capture_photo_png();
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
        case XK_o:
        case XK_O:
            trigger_open_dialog();
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
        view_.motion_frame = motion_loop_start_ + std::fmod(view_.motion_frame - motion_end_frame_, span);
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
    dmc_view_reset(&view_);
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

void App::run() {
    auto last_tick = std::chrono::steady_clock::now();

    while (running_) {
        while (host_.display && XPending(host_.display) > 0) {
            XEvent ev;
            XNextEvent(host_.display, &ev);

            if (ev.type == ClientMessage) {
                if (static_cast<Atom>(ev.xclient.data.l[0]) == host_.wm_delete_window) {
                    running_ = false;
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

                if (ev.xbutton.button == Button1) {
                    // Check info panel close button
                    if (show_info_panel_) {
                        const int pw = std::min(640, window_width_ - 40);
                        const int ph = std::min(460, window_height_ - 80);
                        const int px = (window_width_ - pw) / 2;
                        const int py = (window_height_ - ph) / 2;
                        if (mx >= px + pw - 34 && mx <= px + pw - 8 && my >= py + 6 && my <= py + 30) {
                            show_info_panel_ = false;
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

                    if (!clicked_btn && !show_info_panel_ && !show_kit_panel_) {
                        if (dmc_camera_mode() && my >= kHeaderHeight && my < window_height_ - kToolbarHeight) {
                            camera_focus_at(mx, my - kHeaderHeight);
                        }
                        drag_left_ = true;
                        last_mouse_x_ = mx;
                        last_mouse_y_ = my;
                    }
                } else if (ev.xbutton.button == Button2 || ev.xbutton.button == Button3) {
                    drag_pan_ = true;
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
                if (ev.xbutton.button == Button1) drag_left_ = false;
                if (ev.xbutton.button == Button2 || ev.xbutton.button == Button3) drag_pan_ = false;
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
                    const float dx = static_cast<float>(mx - last_mouse_x_);
                    const float dy = static_cast<float>(my - last_mouse_y_);
                    dmc_view_drag(&view_, dx, dy);
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
                view_.motion_frame = motion_loop_start_ + std::fmod(view_.motion_frame - motion_end_frame_, span);
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
