#include "dmcresource/spider/camera.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <utility>

#include "dmcresource/demo_session.h"

namespace dmcresource::spider::camera {
namespace {

constexpr float kTanHalfFov = 0.54630249F;  // tan(0.5): the view's zoom 1
constexpr float kHalfShortSideMm = 12.0F;   // a full-frame sensor is 36 x 24 mm
constexpr int kMaxPictureSide = 8192;
constexpr int kMaxVideoSide = 4096;

struct Aspect final {
    const char* label;
    int w;
    int h;  // 0 x 0: the view's own shape
};
constexpr Aspect kPhotoAspects[] = {{"As the view", 0, 0}, {"16:9", 16, 9}, {"3:2", 3, 2}, {"4:3", 4, 3},
                                    {"1:1", 1, 1}, {"21:9", 21, 9}, {"9:16", 9, 16}, {"2:3", 2, 3}};
constexpr Aspect kVideoAspects[] = {{"As the view", 0, 0}, {"16:9", 16, 9}, {"4:3", 4, 3}, {"1:1", 1, 1},
                                    {"21:9", 21, 9}, {"9:16", 9, 16}};
constexpr int kPhotoQualities[] = {100, 95, 90, 85, 75, 60};
constexpr int kPhotoLongSides[] = {0, 1280, 1920, 2560, 3840, 5120, 6144, 7680};
constexpr int kTimers[] = {0, 2, 5, 10};
constexpr int kBursts[] = {1, 3, 5, 10};
constexpr int kVideoShortSides[] = {720, 1080, 1440, 2160};
constexpr int kFps[] = {24, 25, 30, 50, 60};
constexpr int kMbps[] = {0, 4, 8, 16, 32, 64, 100};
constexpr int kKeyframeMs[] = {500, 1000, 2000, 5000};
constexpr int kLimits[] = {0, 15, 30, 60, 300, 600};
constexpr int kFocalMm[] = {0, 14, 18, 24, 28, 35, 50, 85, 105, 135, 200};
constexpr float kApertures[] = {1.4F, 2.0F, 2.8F, 4.0F, 5.6F, 8.0F, 11.0F, 16.0F, 22.0F};
constexpr int kIsos[] = {0, 50, 100, 200, 400, 800, 1600, 3200, 6400, 12800, 25600};
constexpr int kShutterDenominators[] = {8000, 4000, 2000, 1000, 500, 250, 125, 60, 30, 15, 8, 4, 2, 1};
constexpr float kCompensation[] = {-3.0F, -2.0F, -1.5F, -1.0F, -0.5F, 0.0F, 0.5F, 1.0F, 1.5F, 2.0F, 3.0F};
constexpr int kKelvin[] = {0, 5500, 6500, 7500, 3200, 4000, 5800, 2000, 10000};
constexpr const char* kStyles[] = {"Standard", "Vivid", "Neutral", "Monochrome", "Sepia", "Film", "Cinema"};
constexpr float kSteps[] = {-2.0F, -1.0F, 0.0F, 1.0F, 2.0F};

template <typename T, std::size_t N>
constexpr int count(const T (&)[N]) noexcept {
    return static_cast<int>(N);
}

std::vector<Setting> build_settings() {
    std::vector<Setting> out;
    const auto add = [&out](Key key, const char* id, const char* section, const char* label,
                            std::vector<std::string> options, int start) {
        out.push_back(Setting{key, id, section, label, std::move(options), start});
    };
    const auto aspects = [](const auto& list) {
        std::vector<std::string> names;
        for (const auto& a : list) names.emplace_back(a.label);
        return names;
    };
    const char* picture = "Picture";
    add(Key::PhotoFormat, "photo_format", picture, "File", {"PNG (lossless)", "JPEG", "WebP (lossless)"}, 0);
    add(Key::PhotoQuality, "photo_quality", picture, "JPEG quality", {"100", "95", "90", "85", "75", "60"}, 1);
    add(Key::PhotoSize, "photo_size", picture, "Size (long side)",
        {"As the view", "HD 1280", "Full HD 1920", "QHD 2560", "4K 3840", "5K 5120", "6K 6144", "8K 7680"}, 0);
    add(Key::PhotoAspect, "photo_aspect", picture, "Shape", aspects(kPhotoAspects), 0);
    add(Key::PhotoTimer, "photo_timer", picture, "Self-timer", {"Off", "2 s", "5 s", "10 s"}, 0);
    add(Key::PhotoBurst, "photo_burst", picture, "Shots per press", {"1", "3", "5", "10"}, 0);
    add(Key::PhotoSubtitles, "photo_subtitles", picture, "Subtitles in the picture", {"Show", "Hide"}, 0);
    const char* video = "Video";
    add(Key::VideoSize, "video_size", video, "Size",
        {"720p (HD)", "1080p (Full HD)", "1440p (QHD)", "2160p (4K)"}, 1);
    add(Key::VideoAspect, "video_aspect", video, "Shape", aspects(kVideoAspects), 1);
    add(Key::VideoFps, "video_fps", video, "Frame rate", {"24", "25", "30", "50", "60"}, 2);
    add(Key::VideoBitrate, "video_bitrate", video, "Bit rate",
        {"Auto", "4 Mbit/s", "8 Mbit/s", "16 Mbit/s", "32 Mbit/s", "64 Mbit/s", "100 Mbit/s"}, 0);
    add(Key::VideoCodec, "video_codec", video, "Codec", {"H.264 (AVC)", "H.265 (HEVC)"}, 0);
    add(Key::VideoKeyframe, "video_keyframe", video, "Key frame every", {"0.5 s", "1 s", "2 s", "5 s"}, 1);
    add(Key::VideoLimit, "video_limit", video, "Stop after",
        {"No limit", "15 s", "30 s", "1 min", "5 min", "10 min"}, 0);
    add(Key::VideoMotionBlur, "video_motion_blur", video, "Motion blur", {"Off", "By the shutter"}, 0);
    add(Key::VideoSubtitles, "video_subtitles", video, "Subtitles in the video", {"Show", "Hide"}, 0);
    const char* lens = "Lens";
    add(Key::LensFocal, "lens_focal", lens, "Focal length (full frame)",
        {"As the view (pinch)", "14 mm", "18 mm", "24 mm", "28 mm", "35 mm", "50 mm", "85 mm", "105 mm",
         "135 mm", "200 mm"},
        0);
    add(Key::LensAperture, "lens_aperture", lens, "Aperture",
        {"f/1.4", "f/2", "f/2.8", "f/4", "f/5.6", "f/8", "f/11", "f/16", "f/22"}, 3);
    add(Key::LensVignette, "lens_vignette", lens, "Vignetting", {"Off", "By the aperture", "Strong"}, 0);
    add(Key::LensDistortion, "lens_distortion", lens, "Distortion", {"Off", "By the focal length"}, 0);
    add(Key::LensFringe, "lens_fringe", lens, "Colour fringing", {"Off", "Weak", "Strong"}, 0);
    const char* exposure = "Exposure";
    add(Key::ExposureMode, "exposure_mode", exposure, "Mode",
        {"Auto", "Manual (ISO, shutter, aperture)"}, 0);
    add(Key::ExposureIso, "exposure_iso", exposure, "ISO",
        {"Auto", "50", "100", "200", "400", "800", "1600", "3200", "6400", "12800", "25600"}, 0);
    {
        std::vector<std::string> names;
        for (const int d : kShutterDenominators) names.push_back(d == 1 ? "1 s" : "1/" + std::to_string(d));
        add(Key::ExposureShutter, "exposure_shutter", exposure, "Shutter", std::move(names), 7);
    }
    add(Key::ExposureCompensation, "exposure_compensation", exposure, "Compensation",
        {"\xE2\x88\x92" "3 EV", "\xE2\x88\x92" "2", "\xE2\x88\x92" "1.5", "\xE2\x88\x92" "1",
         "\xE2\x88\x92" "0.5", "0", "+0.5", "+1", "+1.5", "+2", "+3 EV"},
        5);
    add(Key::ExposureHighlights, "exposure_highlights", exposure, "Highlights", {"Clip", "Soft roll-off"}, 0);
    add(Key::ExposureNoise, "exposure_noise", exposure, "Sensor noise", {"Off", "By the ISO", "Film grain"}, 1);
    const char* colour = "Colour";
    add(Key::ColourBalance, "colour_balance", colour, "White balance",
        {"As rendered", "Daylight 5500 K", "Cloudy 6500 K", "Shade 7500 K", "Tungsten 3200 K",
         "Fluorescent 4000 K", "Flash 5800 K", "Candle 2000 K", "Blue sky 10000 K"},
        0);
    {
        std::vector<std::string> names;
        for (const char* s : kStyles) names.emplace_back(s);
        add(Key::ColourStyle, "colour_style", colour, "Picture style", std::move(names), 0);
    }
    const std::vector<std::string> steps{"\xE2\x88\x92" "2", "\xE2\x88\x92" "1", "0", "+1", "+2"};
    add(Key::ColourContrast, "colour_contrast", colour, "Contrast", steps, 2);
    add(Key::ColourSaturation, "colour_saturation", colour, "Saturation", steps, 2);
    add(Key::ColourSharpness, "colour_sharpness", colour, "Sharpness", {"Off", "Low", "Medium", "High"}, 0);
    const char* finder = "Viewfinder";
    add(Key::FinderMode, "finder_mode", finder, "Viewfinder in the view (the lens and the look, live)",
        {"Off", "On"}, 0);
    add(Key::FinderGrid, "finder_grid", finder, "Grid",
        {"Off", "Thirds", "4 \xC3\x97 4", "Golden ratio", "Centre cross"}, 1);
    add(Key::FinderInfo, "finder_info", finder, "Settings line", {"Show", "Hide"}, 0);
    return out;
}

std::array<std::atomic<int>, static_cast<std::size_t>(Key::Count)>& stored() {
    static std::array<std::atomic<int>, static_cast<std::size_t>(Key::Count)> values{};
    static const bool started = [] {
        for (const auto& s : settings()) values[static_cast<std::size_t>(s.key)].store(s.start);
        return true;
    }();
    (void)started;
    return values;
}

template <typename T, std::size_t N>
T pick(Key key, const T (&table)[N]) noexcept {
    return table[std::clamp(choice(key), 0, static_cast<int>(N) - 1)];
}

int even(int v) noexcept { return std::max(2, v / 2 * 2); }
int multiple_of_16(int v) noexcept { return std::max(16, v / 16 * 16); }

// The shape of a capture (w:h), the view's own when 0 x 0.
std::pair<float, float> shape(Kind kind, int view_width, int view_height) noexcept {
    const Aspect a = kind == Kind::Photo ? pick(Key::PhotoAspect, kPhotoAspects) : pick(Key::VideoAspect, kVideoAspects);
    if (a.w == 0 || a.h == 0) return {static_cast<float>(std::max(1, view_width)), static_cast<float>(std::max(1, view_height))};
    return {static_cast<float>(a.w), static_cast<float>(a.h)};
}

// A fixed focal length as a zoom (0: the view's own zoom).
float fixed_lens_zoom() noexcept {
    const int mm = pick(Key::LensFocal, kFocalMm);
    return mm > 0 ? kTanHalfFov * static_cast<float>(mm) / kHalfShortSideMm : 0.0F;
}

float srgb_to_linear(float v) noexcept {
    return v <= 0.04045F ? v / 12.92F : std::pow((v + 0.055F) / 1.055F, 2.4F);
}

float linear_to_srgb(float v) noexcept {
    v = std::clamp(v, 0.0F, 1.0F);
    return v <= 0.0031308F ? v * 12.92F : 1.055F * std::pow(v, 1.0F / 2.4F) - 0.055F;
}

// Colour of a black body at kelvin, linear RGB (Tanner Helland's fit).
std::array<float, 3> kelvin_rgb(float kelvin) noexcept {
    const float t = std::clamp(kelvin, 1000.0F, 40000.0F) / 100.0F;
    float r = 255.0F;
    float g = 0.0F;
    float b = 255.0F;
    if (t > 66.0F) {
        r = 329.698727446F * std::pow(t - 60.0F, -0.1332047592F);
        g = 288.1221695283F * std::pow(t - 60.0F, -0.0755148492F);
    } else {
        g = 99.4708025861F * std::log(t) - 161.1195681661F;
    }
    if (t < 66.0F) b = t <= 19.0F ? 0.0F : 138.5177312231F * std::log(t - 10.0F) - 305.0447927307F;
    const auto lin = [](float v) { return srgb_to_linear(std::clamp(v, 1.0F, 255.0F) / 255.0F); };
    return {lin(r), lin(g), lin(b)};
}

// The gains of a white balance set to kelvin over a picture rendered as
// daylight: a warm setting cools the picture and the other way round.
std::array<float, 3> balance_gains() noexcept {
    const int kelvin = pick(Key::ColourBalance, kKelvin);
    if (kelvin == 0) return {1.0F, 1.0F, 1.0F};
    const auto ref = kelvin_rgb(5500.0F);
    const auto set = kelvin_rgb(static_cast<float>(kelvin));
    std::array<float, 3> g{ref[0] / set[0], ref[1] / set[1], ref[2] / set[2]};
    const float luma = 0.2126F * g[0] + 0.7152F * g[1] + 0.0722F * g[2];
    for (auto& v : g) v /= luma;
    return g;
}

// Lens distortion of a full-frame focal length: barrel when wide, a little
// pincushion when long.
float distortion_k(float mm) noexcept {
    constexpr std::pair<float, float> table[] = {{14.0F, 0.10F}, {18.0F, 0.07F}, {24.0F, 0.04F}, {28.0F, 0.03F},
                                                 {35.0F, 0.015F}, {50.0F, 0.0F}, {85.0F, -0.01F},
                                                 {135.0F, -0.015F}, {200.0F, -0.02F}};
    if (mm <= table[0].first) return table[0].second;
    for (std::size_t i = 1; i < std::size(table); ++i) {
        if (mm <= table[i].first) {
            const float u = (mm - table[i - 1].first) / (table[i].first - table[i - 1].first);
            return table[i - 1].second + (table[i].second - table[i - 1].second) * u;
        }
    }
    return table[std::size(table) - 1].second;
}

std::uint32_t hash(std::uint32_t x, std::uint32_t y, std::uint32_t seed) noexcept {
    std::uint32_t h = x * 0x8DA6B343U ^ y * 0xD8163841U ^ seed * 0xCB1AB31FU;
    h ^= h >> 13U;
    h *= 0x5BD1E995U;
    h ^= h >> 15U;
    return h;
}

// About normal, -1..1 (the sum of two uniforms).
float noise(std::uint32_t h) noexcept {
    return (static_cast<float>(h & 0xFFFFU) + static_cast<float>(h >> 16U)) / 65535.0F - 1.0F;
}

// The lens pass: distortion and colour fringing, both radial.
void lens_pass(RgbaImage* image, float k, float fringe) {
    const int w = image->width;
    const int h = image->height;
    const std::vector<std::uint8_t> src = image->pixels;
    const float cx = 0.5F * static_cast<float>(w);
    const float cy = 0.5F * static_cast<float>(h);
    const float half_diagonal = std::sqrt(cx * cx + cy * cy);
    const auto sample = [&](float x, float y, int channel) {
        x = std::clamp(x, 0.0F, static_cast<float>(w - 1));
        y = std::clamp(y, 0.0F, static_cast<float>(h - 1));
        const int x0 = static_cast<int>(x);
        const int y0 = static_cast<int>(y);
        const int x1 = std::min(x0 + 1, w - 1);
        const int y1 = std::min(y0 + 1, h - 1);
        const float fx = x - static_cast<float>(x0);
        const float fy = y - static_cast<float>(y0);
        const auto at = [&](int px, int py) {
            return static_cast<float>(src[(static_cast<std::size_t>(py) * w + px) * 4U + channel]);
        };
        const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * fx;
        const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * fx;
        return top + (bottom - top) * fy;
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float dx = (static_cast<float>(x) + 0.5F - cx) / half_diagonal;
            const float dy = (static_cast<float>(y) + 0.5F - cy) / half_diagonal;
            const float r2 = dx * dx + dy * dy;
            // The corners stay the corners: the source radius is scaled back.
            const float scale = (1.0F + k * r2) / (1.0F + k);
            auto* out = &image->pixels[(static_cast<std::size_t>(y) * w + x) * 4U];
            for (int c = 0; c < 3; ++c) {
                const float s = scale * (c == 0 ? 1.0F + fringe : c == 2 ? 1.0F - fringe : 1.0F);
                const float v = sample(cx + dx * s * half_diagonal - 0.5F, cy + dy * s * half_diagonal - 0.5F, c);
                out[c] = static_cast<std::uint8_t>(std::clamp(v + 0.5F, 0.0F, 255.0F));
            }
        }
    }
}

void sharpen(RgbaImage* image, float amount) {
    const int w = image->width;
    const int h = image->height;
    if (w < 3 || h < 3) return;
    const std::vector<std::uint8_t> src = image->pixels;
    for (int y = 1; y + 1 < h; ++y) {
        for (int x = 1; x + 1 < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4U;
            for (int c = 0; c < 3; ++c) {
                const float centre = src[i + c];
                const float around = (static_cast<float>(src[i - 4 + c]) + src[i + 4 + c] +
                                      src[i - static_cast<std::size_t>(w) * 4U + c] +
                                      src[i + static_cast<std::size_t>(w) * 4U + c]) * 0.25F;
                image->pixels[i + c] =
                    static_cast<std::uint8_t>(std::clamp(centre + amount * (centre - around) + 0.5F, 0.0F, 255.0F));
            }
        }
    }
}

std::mutex& blur_mutex() {
    static std::mutex m;
    return m;
}
std::vector<std::uint8_t>& blur_frame() {
    static std::vector<std::uint8_t> frame;
    return frame;
}

std::atomic<std::uint32_t> g_seed{1U};

// The zoom changes that frame a capture of `kind`: a fixed lens, or the
// view's zoom (the cutscene camera's too) carried over to its shape.
void frame_controls(Kind kind, int view_width, int view_height, ViewControls* controls) noexcept {
    const Rect frame = frame_in_view(kind, view_width, view_height);
    const float fixed = fixed_lens_zoom();
    if (fixed > 0.0F) {
        controls->lens_zoom = fixed;
        controls->zoom_scale = 1.0F;
        return;
    }
    controls->lens_zoom = 0.0F;
    const float frame_short = std::max(1.0F, std::min(frame.width, frame.height));
    controls->zoom_scale = static_cast<float>(std::max(1, std::min(view_width, view_height))) / frame_short;
}

float effective_view_zoom(const Session* session, float view_zoom) noexcept {
    if (session != nullptr) {
        if (const auto director = demo::director_view(*session)) return director->zoom;
    }
    return view_zoom;
}

std::string clock_text(int seconds) {
    char text[16];
    std::snprintf(text, sizeof(text), "%d:%02d", seconds / 60, seconds % 60);
    return text;
}

}  // namespace

const std::vector<Setting>& settings() {
    static const std::vector<Setting> list = build_settings();
    return list;
}

void set_choices(std::span<const int> choices) {
    const auto& list = settings();
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& s = list[i];
        const int value = i < choices.size() ? choices[i] : s.start;
        const bool valid = value >= 0 && value < static_cast<int>(s.options.size());
        stored()[static_cast<std::size_t>(s.key)].store(valid ? value : s.start);
    }
}

int choice(Key key) noexcept {
    if (key >= Key::Count) return 0;
    return stored()[static_cast<std::size_t>(key)].load(std::memory_order_relaxed);
}

void reset_choices() { set_choices({}); }

Plan plan(Kind kind, int view_width, int view_height) noexcept {
    Plan out;
    view_width = std::max(1, view_width);
    view_height = std::max(1, view_height);
    const auto [aw, ah] = shape(kind, view_width, view_height);
    if (kind == Kind::Photo) {
        const int long_side = pick(Key::PhotoSize, kPhotoLongSides);
        float w = 0.0F;
        float h = 0.0F;
        if (long_side == 0) {
            const Rect frame = frame_in_view(kind, view_width, view_height);
            w = frame.width;
            h = frame.height;
        } else if (aw >= ah) {
            w = static_cast<float>(long_side);
            h = w * ah / aw;
        } else {
            h = static_cast<float>(long_side);
            w = h * aw / ah;
        }
        const float fit = std::min(1.0F, static_cast<float>(kMaxPictureSide) / std::max(w, h));
        out.width = even(std::max(64, static_cast<int>(std::lround(w * fit))));
        out.height = even(std::max(64, static_cast<int>(std::lround(h * fit))));
        out.format = std::clamp(choice(Key::PhotoFormat), 0, 2);
        out.quality = out.format == static_cast<int>(PhotoFormat::Jpeg) ? pick(Key::PhotoQuality, kPhotoQualities) : 100;
        out.timer_seconds = pick(Key::PhotoTimer, kTimers);
        out.burst = pick(Key::PhotoBurst, kBursts);
        out.subtitles = choice(Key::PhotoSubtitles) == 0;
        return out;
    }
    const float short_side = static_cast<float>(pick(Key::VideoSize, kVideoShortSides));
    float w = aw >= ah ? short_side * aw / ah : short_side;
    float h = aw >= ah ? short_side : short_side * ah / aw;
    const float fit = std::min(1.0F, static_cast<float>(kMaxVideoSide) / std::max(w, h));
    out.width = multiple_of_16(static_cast<int>(w * fit));
    out.height = multiple_of_16(static_cast<int>(h * fit));
    out.format = std::clamp(choice(Key::VideoCodec), 0, 1);
    out.fps = pick(Key::VideoFps, kFps);
    const int mbps = pick(Key::VideoBitrate, kMbps);
    const float bits_per_pixel = out.format == static_cast<int>(VideoCodec::Hevc) ? 0.07F : 0.12F;
    out.quality = mbps > 0 ? mbps * 1'000'000
                           : std::max(1'500'000, static_cast<int>(static_cast<float>(out.width) * out.height *
                                                                  out.fps * bits_per_pixel));
    out.keyframe_ms = pick(Key::VideoKeyframe, kKeyframeMs);
    out.limit_seconds = pick(Key::VideoLimit, kLimits);
    out.subtitles = choice(Key::VideoSubtitles) == 0;
    return out;
}

Rect frame_in_view(Kind kind, int view_width, int view_height) noexcept {
    const float vw = static_cast<float>(std::max(1, view_width));
    const float vh = static_cast<float>(std::max(1, view_height));
    const auto [aw, ah] = shape(kind, view_width, view_height);
    float w = vw;
    float h = vw * ah / aw;
    if (h > vh) {
        h = vh;
        w = vh * aw / ah;
    }
    return {(vw - w) * 0.5F, (vh - h) * 0.5F, w, h};
}

float capture_zoom(Kind kind, float view_zoom, int view_width, int view_height) noexcept {
    ViewControls controls;
    frame_controls(kind, view_width, view_height, &controls);
    return controls.lens_zoom > 0.0F ? controls.lens_zoom : view_zoom * controls.zoom_scale;
}

float finder_zoom(Kind kind, float view_zoom, int view_width, int view_height) noexcept {
    const float fixed = fixed_lens_zoom();
    if (fixed <= 0.0F) return view_zoom;
    const Rect frame = frame_in_view(kind, view_width, view_height);
    return fixed * std::min(frame.width, frame.height) /
           static_cast<float>(std::max(1, std::min(view_width, view_height)));
}

float focal_mm(float zoom) noexcept { return kHalfShortSideMm * zoom / kTanHalfFov; }

Exposure exposure() noexcept {
    Exposure out;
    out.shutter_seconds = 1.0F / static_cast<float>(pick(Key::ExposureShutter, kShutterDenominators));
    out.aperture = pick(Key::LensAperture, kApertures);
    const float compensation = std::exp2(pick(Key::ExposureCompensation, kCompensation));
    const int chosen_iso = pick(Key::ExposureIso, kIsos);
    if (choice(Key::ExposureMode) == 0) {
        // Auto: the rendered picture is the right exposure; only the
        // compensation moves it, and the ISO is the one chosen (100 when auto).
        out.iso = chosen_iso > 0 ? chosen_iso : 100;
        out.gain = compensation;
        return out;
    }
    // Manual: ISO 100 at 1/60 and f/4 is the rendered picture.
    const float light = out.shutter_seconds * 60.0F * (4.0F / out.aperture) * (4.0F / out.aperture);
    out.iso = chosen_iso > 0 ? chosen_iso
                             : std::clamp(static_cast<int>(std::lround(100.0F / light)), 50, 25600);
    out.gain = static_cast<float>(out.iso) / 100.0F * light * compensation;
    return out;
}

void develop(RgbaImage* image, std::uint32_t seed, float mm) {
    if (image == nullptr || image->width <= 0 || image->height <= 0 ||
        image->pixels.size() < static_cast<std::size_t>(image->width) * image->height * 4U) {
        return;
    }
    const int w = image->width;
    const int h = image->height;
    const float scale = static_cast<float>(std::max(w, h)) / 1920.0F;
    const float k = choice(Key::LensDistortion) == 1 ? distortion_k(mm) : 0.0F;
    const float fringe = choice(Key::LensFringe) == 0 ? 0.0F : (choice(Key::LensFringe) == 1 ? 0.0012F : 0.003F);
    if (k != 0.0F || fringe != 0.0F) lens_pass(image, k, fringe);

    // Exposure, white balance, highlights and contrast are curves per
    // channel; the rest mixes the channels.
    const Exposure e = exposure();
    const auto wb = balance_gains();
    const int style = std::clamp(choice(Key::ColourStyle), 0, count(kStyles) - 1);
    float contrast = pick(Key::ColourContrast, kSteps) * 0.15F;
    float saturation = pick(Key::ColourSaturation, kSteps) * 0.25F;
    std::array<float, 3> tint{1.0F, 1.0F, 1.0F};
    float lift = 0.0F;
    switch (style) {
        case 1: contrast += 0.15F; saturation += 0.35F; break;           // Vivid
        case 2: contrast -= 0.15F; saturation -= 0.2F; break;            // Neutral
        case 3: saturation = -1.0F; break;                               // Monochrome
        case 4: saturation = -1.0F; tint = {1.08F, 0.96F, 0.78F}; break; // Sepia
        case 5: contrast += 0.05F; saturation -= 0.12F; tint = {1.04F, 1.0F, 0.93F}; lift = 0.05F; break;  // Film
        case 6: contrast += 0.1F; break;                                 // Cinema (tinted by luma below)
        default: break;
    }
    const bool soft = choice(Key::ExposureHighlights) == 1;
    std::array<std::array<std::uint8_t, 256>, 3> curve{};
    for (int c = 0; c < 3; ++c) {
        for (int v = 0; v < 256; ++v) {
            float lin = srgb_to_linear(static_cast<float>(v) / 255.0F) * e.gain * wb[c];
            if (soft && lin > 0.6F) lin = 0.6F + 0.4F * (1.0F - std::exp(-(lin - 0.6F) / 0.4F));
            float x = linear_to_srgb(lin);
            const float s = x * x * (3.0F - 2.0F * x);
            x = std::clamp(x + 2.0F * contrast * (s - x), 0.0F, 1.0F);
            x = lift + x * (1.0F - lift * 1.6F);
            curve[c][v] = static_cast<std::uint8_t>(std::clamp(x * 255.0F + 0.5F, 0.0F, 255.0F));
        }
    }
    const int vignette_choice = choice(Key::LensVignette);
    const float vignette = vignette_choice == 0 ? 0.0F
                           : vignette_choice == 2
                               ? 0.6F
                               : std::clamp(0.62F * std::pow(1.4F / e.aperture, 0.8F), 0.04F, 0.6F);
    const int noise_choice = choice(Key::ExposureNoise);
    float sigma = 0.0F;
    if (noise_choice != 0 && e.iso > 100) sigma = 1.6F * std::pow(std::log2(static_cast<float>(e.iso) / 100.0F), 1.3F);
    if (noise_choice == 2) sigma = std::max(sigma, 5.0F);
    const int grain = noise_choice == 2 ? std::max(1, static_cast<int>(std::lround(2.0F * scale))) : 1;
    const float cx = 0.5F * static_cast<float>(w);
    const float cy = 0.5F * static_cast<float>(h);
    const float inv_r2 = 1.0F / (cx * cx + cy * cy);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            auto* p = &image->pixels[(static_cast<std::size_t>(y) * w + x) * 4U];
            float r = curve[0][p[0]];
            float g = curve[1][p[1]];
            float b = curve[2][p[2]];
            const float luma = 0.2126F * r + 0.7152F * g + 0.0722F * b;
            if (saturation != 0.0F) {
                const float f = 1.0F + saturation;
                r = luma + (r - luma) * f;
                g = luma + (g - luma) * f;
                b = luma + (b - luma) * f;
            }
            if (style == 6) {
                // Teal shadows, warm highlights.
                const float t = luma / 255.0F;
                r *= 0.92F + 0.16F * t;
                b *= 1.08F - 0.18F * t;
            } else {
                r *= tint[0];
                g *= tint[1];
                b *= tint[2];
            }
            if (vignette > 0.0F) {
                const float dx = static_cast<float>(x) - cx;
                const float dy = static_cast<float>(y) - cy;
                const float rr = (dx * dx + dy * dy) * inv_r2;
                const float f = 1.0F - vignette * rr * std::sqrt(rr);
                r *= f;
                g *= f;
                b *= f;
            }
            if (sigma > 0.0F) {
                // Stronger in the shadows; film grain is in clumps and has no colour.
                const float shade = 1.3F - 0.7F * std::clamp(luma / 255.0F, 0.0F, 1.0F);
                const auto gx = static_cast<std::uint32_t>(x / grain);
                const auto gy = static_cast<std::uint32_t>(y / grain);
                const float n = noise(hash(gx, gy, seed)) * sigma * shade;
                r += n;
                g += n;
                b += n;
                if (noise_choice == 1) {
                    r += noise(hash(gx, gy, seed ^ 0x9E3779B9U)) * sigma * 0.6F * shade;
                    b += noise(hash(gx, gy, seed ^ 0x7F4A7C15U)) * sigma * 0.6F * shade;
                }
            }
            p[0] = static_cast<std::uint8_t>(std::clamp(r + 0.5F, 0.0F, 255.0F));
            p[1] = static_cast<std::uint8_t>(std::clamp(g + 0.5F, 0.0F, 255.0F));
            p[2] = static_cast<std::uint8_t>(std::clamp(b + 0.5F, 0.0F, 255.0F));
        }
    }
    const int sharpness = choice(Key::ColourSharpness);
    if (sharpness > 0) sharpen(image, 0.35F * static_cast<float>(sharpness));
}

void develop(RgbaImage* image, std::uint32_t seed) {
    const float fixed = fixed_lens_zoom();
    develop(image, seed, fixed > 0.0F ? focal_mm(fixed) : 50.0F);
}

RgbaImage render_capture(const Session* session, Kind kind, int view_width, int view_height, float yaw,
                         float pitch, float zoom, std::uint32_t render_flags, const ViewControls& controls) {
    if (session == nullptr) return {};
    const Plan p = plan(kind, view_width, view_height);
    if (session->uv_gallery) {
        return render_session(session, p.width, p.height, yaw, pitch, zoom, render_flags, controls);
    }
    ViewControls framed = controls;
    frame_controls(kind, view_width, view_height, &framed);
    auto image = render_session(session, p.width, p.height, yaw, pitch, zoom, render_flags, framed);
    const float lens = framed.lens_zoom > 0.0F ? framed.lens_zoom
                                               : effective_view_zoom(session, zoom) * framed.zoom_scale;
    develop(&image, g_seed.fetch_add(1U), focal_mm(lens));
    if (kind == Kind::Video && choice(Key::VideoMotionBlur) == 1 && !image.pixels.empty()) {
        const Exposure e = exposure();
        const float a = std::clamp(1.0F - 1.0F / (2.0F * e.shutter_seconds * static_cast<float>(p.fps)), 0.0F, 0.85F);
        const std::scoped_lock lock{blur_mutex()};
        auto& previous = blur_frame();
        if (a > 0.0F && previous.size() == image.pixels.size()) {
            for (std::size_t i = 0; i < previous.size(); ++i) {
                const float v = a * previous[i] + (1.0F - a) * image.pixels[i];
                image.pixels[i] = static_cast<std::uint8_t>(v + 0.5F);
            }
        }
        previous = image.pixels;
    }
    return image;
}

void begin_video() {
    const std::scoped_lock lock{blur_mutex()};
    blur_frame().clear();
}

bool finder_on() noexcept { return choice(Key::FinderMode) == 1; }

RgbaImage render_view_frame(const Session* session, int width, int height, float yaw, float pitch, float zoom,
                            std::uint32_t render_flags, const ViewControls& controls) {
    if (session == nullptr || !finder_on() || session->uv_gallery) {
        return render_session(session, width, height, yaw, pitch, zoom, render_flags, controls);
    }
    ViewControls framed = controls;
    const float fixed = fixed_lens_zoom();
    if (fixed > 0.0F) framed.lens_zoom = finder_zoom(Kind::Photo, zoom, width, height);
    auto image = render_session(session, width, height, yaw, pitch, zoom, render_flags, framed);
    develop(&image, g_seed.fetch_add(1U),
            focal_mm(capture_zoom(Kind::Photo, effective_view_zoom(session, zoom), width, height)));
    return image;
}

Finder viewfinder(const Session* session, int width, int height, float view_zoom, bool recording,
                  int recorded_seconds) {
    Finder out;
    out.shown = recording || finder_on();
    if (!out.shown) return out;
    const Kind kind = recording ? Kind::Video : Kind::Photo;
    out.frame = frame_in_view(kind, width, height);
    const Rect f = out.frame;
    const auto vertical = [&](float u) { out.lines.push_back({f.x + f.width * u, f.y, f.x + f.width * u, f.y + f.height}); };
    const auto horizontal = [&](float v) { out.lines.push_back({f.x, f.y + f.height * v, f.x + f.width, f.y + f.height * v}); };
    switch (choice(Key::FinderGrid)) {
        case 1: for (const float u : {1.0F / 3.0F, 2.0F / 3.0F}) { vertical(u); horizontal(u); } break;
        case 2: for (const float u : {0.25F, 0.5F, 0.75F}) { vertical(u); horizontal(u); } break;
        case 3: for (const float u : {0.382F, 0.618F}) { vertical(u); horizontal(u); } break;
        case 4: {
            const float cx = f.x + f.width * 0.5F;
            const float cy = f.y + f.height * 0.5F;
            const float arm = std::min(f.width, f.height) * 0.04F;
            out.lines.push_back({cx - arm, cy, cx + arm, cy});
            out.lines.push_back({cx, cy - arm, cx, cy + arm});
            break;
        }
        default: break;
    }
    const Plan p = plan(kind, width, height);
    std::string status;
    if (recording) {
        status = "\xE2\x97\x8F REC " + clock_text(recorded_seconds) + "  " + std::to_string(p.width) + "\xC3\x97" +
                 std::to_string(p.height) + " " + std::to_string(p.fps) + "p " +
                 (p.format == static_cast<int>(VideoCodec::Hevc) ? "H.265" : "H.264");
    }
    if (choice(Key::FinderInfo) == 0) {
        const Exposure e = exposure();
        const int den = pick(Key::ExposureShutter, kShutterDenominators);
        const float zoom = capture_zoom(kind, effective_view_zoom(session, view_zoom), width, height);
        const int kelvin = pick(Key::ColourBalance, kKelvin);
        char aperture[16];
        std::snprintf(aperture, sizeof(aperture), "f/%g", static_cast<double>(e.aperture));
        std::string line = std::string(choice(Key::ExposureMode) == 0 ? "P" : "M") + "  ISO " +
                           std::to_string(e.iso) + "  " + (den == 1 ? std::string("1 s") : "1/" + std::to_string(den)) +
                           "  " + aperture + "  " + std::to_string(static_cast<int>(std::lround(focal_mm(zoom)))) +
                           " mm  " + (kelvin == 0 ? std::string("AWB") : std::to_string(kelvin) + " K") + "  " +
                           kStyles[std::clamp(choice(Key::ColourStyle), 0, count(kStyles) - 1)];
        const float ev = pick(Key::ExposureCompensation, kCompensation);
        if (ev != 0.0F) {
            char text[32];
            std::snprintf(text, sizeof(text), "  %+g EV", static_cast<double>(ev));
            line += text;
        }
        if (!recording) {
            constexpr const char* formats[] = {"PNG", "JPEG", "WebP"};
            line += "  " + std::string(formats[std::clamp(p.format, 0, 2)]) + " " + std::to_string(p.width) +
                    "\xC3\x97" + std::to_string(p.height);
        }
        status = status.empty() ? line : status + "\n" + line;
    }
    out.status = std::move(status);
    return out;
}

}  // namespace dmcresource::spider::camera
