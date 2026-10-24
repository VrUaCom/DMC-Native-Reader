#include "dmcresource/spider/camera.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

#include <unistd.h>

#include "dmcresource/camera_control.h"
#include "dmcresource/demo_session.h"

namespace dmcresource::spider::camera {
namespace {

constexpr float kTanHalfFov = 0.54630249F;  // tan(0.5): the view's zoom 1
constexpr float kHalfShortSideMm = 12.0F;   // a full-frame sensor is 36 x 24 mm
constexpr int kMaxPictureSide = 24576;  // larger than one render: drawn in tiles
constexpr int kMaxRenderTile = 8192;
constexpr int kTileSide = 4096;
constexpr int kMaxVideoSide = 4096;

struct Aspect final {
    const char* label;
    int w;
    int h;  // 0 x 0: the view's own shape
};
// The camera's frame, the same for its pictures and its videos.
constexpr Aspect kShapes[] = {{"View", 0, 0}, {"16:9", 16, 9}, {"3:2", 3, 2},    {"4:3", 4, 3},
                              {"1:1", 1, 1},          {"21:9", 21, 9}, {"2.35:1", 235, 100}, {"9:16", 9, 16},
                              {"2:3", 2, 3}};
constexpr int kPhotoQualities[] = {100, 95, 90, 85, 75, 60};
constexpr int kPhotoLongSides[] = {0, 1280, 1920, 2560, 3840, 5120, 6144, 7680};
constexpr int kMegapixels[] = {0, 2, 5, 8, 12, 16, 20, 24, 33, 40, 48, 64, 100, 124, 150, 200};
constexpr float kInfinity = std::numeric_limits<float>::infinity();
constexpr float kFocusUnits[] = {0.0F, 50.0F, 100.0F, 200.0F, 300.0F, 500.0F, 1000.0F, 2000.0F, kInfinity};
constexpr int kBlades[] = {0, 9, 8, 7, 6, 5};
constexpr float kUnitMm = 10.0F;  // a scene unit is about a centimetre (a character is ~180 tall)
constexpr int kTimers[] = {0, 2, 5, 10};
constexpr int kBursts[] = {1, 3, 5, 10};
constexpr int kVideoShortSides[] = {720, 1080, 1440, 2160};
constexpr int kFps[] = {24, 25, 30, 50, 60};
constexpr int kMbps[] = {0, 4, 8, 16, 32, 64, 100};
constexpr int kKeyframeMs[] = {500, 1000, 2000, 5000};
constexpr int kLimits[] = {0, 15, 30, 60, 300, 600};
constexpr int kFocalMm[] = {0, 14, 18, 24, 28, 35, 50, 85, 105, 135, 200, 300, 400};
constexpr float kApertures[] = {0.5F, 0.95F, 1.2F, 1.4F, 2.0F, 2.8F, 4.0F, 5.6F, 8.0F, 11.0F, 16.0F, 22.0F};
// The ISO dial: thirds of a stop from 25 to 102400 (a camera's own range),
// then whole stops of the extended range up to 4 000 000 (0: auto).
constexpr int kIsos[] = {0,      25,     32,     40,      50,      64,      80,      100,     125,    160,
                         200,    250,    320,    400,     500,     640,     800,     1000,    1250,   1600,
                         2000,   2500,   3200,   4000,    5000,    6400,    8000,    10000,   12800,  16000,
                         20000,  25600,  32000,  40000,   51200,   64000,   80000,   102400,  204800, 409600,
                         819200, 1638400, 3276800, 4000000};
// The shutter dial: thirds of a stop from 30 s to 1/2000 s.
struct Shutter final {
    const char* label;
    float seconds;
};
constexpr Shutter kShutters[] = {
    {"30\"", 30.0F},    {"25\"", 25.0F},    {"20\"", 20.0F},    {"15\"", 15.0F},    {"13\"", 13.0F},
    {"10\"", 10.0F},    {"8\"", 8.0F},      {"6\"", 6.0F},      {"5\"", 5.0F},      {"4\"", 4.0F},
    {"3.2\"", 3.2F},    {"2.5\"", 2.5F},    {"2\"", 2.0F},      {"1.6\"", 1.6F},    {"1.3\"", 1.3F},
    {"1\"", 1.0F},      {"0.8\"", 0.8F},    {"0.6\"", 0.6F},    {"0.5\"", 0.5F},    {"0.4\"", 0.4F},
    {"0.3\"", 0.3F},    {"1/4", 0.25F},     {"1/5", 0.2F},       {"1/6", 1.0F / 6},   {"1/8", 0.125F},
    {"1/10", 0.1F},      {"1/13", 1.0F / 13}, {"1/15", 1.0F / 15}, {"1/20", 0.05F},     {"1/25", 0.04F},
    {"1/30", 1.0F / 30}, {"1/40", 0.025F},   {"1/50", 0.02F},     {"1/60", 1.0F / 60}, {"1/80", 0.0125F},
    {"1/100", 0.01F},    {"1/125", 0.008F},  {"1/160", 1.0F / 160}, {"1/200", 0.005F}, {"1/250", 0.004F},
    {"1/320", 1.0F / 320}, {"1/400", 0.0025F}, {"1/500", 0.002F}, {"1/640", 1.0F / 640}, {"1/800", 0.00125F},
    {"1/1000", 0.001F},  {"1/1250", 0.0008F}, {"1/1600", 0.000625F}, {"1/2000", 0.0005F}};
constexpr int kShutterStart = 33;  // 1/60
constexpr float kPolarizerStrength[] = {0.5F, 0.75F, 1.0F};
constexpr float kGyroSensitivity[] = {0.5F, 1.0F, 1.5F, 2.0F};
constexpr float kWalkScale[] = {0.25F, 0.5F, 1.0F, 2.0F, 5.0F, 10.0F, 20.0F};
constexpr float kGyroFollow[] = {1.0F, 0.5F, 0.25F, 0.12F};  // the share of a turn taken per reading
constexpr float kPi = 3.14159265F;

float wrap_angle(float a) noexcept {
    while (a > kPi) a -= 2.0F * kPi;
    while (a <= -kPi) a += 2.0F * kPi;
    return a;
}

std::atomic<float> g_roll_manual{0.0F};
std::atomic<float> g_roll_phone{0.0F};
std::atomic<float> g_focus_point{0.0F};  // scene units; 0: auto
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
    // Option names are short (they sit in square tiles); what they mean is
    // the setting's hint.
    const auto add = [&out](Key key, const char* id, const char* section, const char* label, const char* hint,
                            std::vector<std::string> options, int start) {
        out.push_back(Setting{key, id, section, label, hint, std::move(options), start});
    };
    // One camera: its mode, its frame (the pictures' and the videos'), the
    // viewfinder; then what a picture and a video are made into.
    const char* camera = "Camera";
    add(Key::FinderMode, "finder_mode", camera, "Camera mode",
        "The view through the lens, with the look; two fingers turn a polarizer", {"Off", "On"}, 0);
    {
        std::vector<std::string> names;
        for (const auto& a : kShapes) names.emplace_back(a.label);
        add(Key::CameraShape, "camera_shape", camera, "Frame", "The shape of pictures and videos", std::move(names), 0);
    }
    add(Key::FinderGrid, "finder_grid", camera, "Grid", "", {"Off", "Thirds", "4\xC3\x97" "4", "Golden", "Cross"}, 1);
    add(Key::FinderInfo, "finder_info", camera, "Settings line", "The camera's values over the view",
        {"Show", "Hide"}, 0);
    const char* picture = "Picture";
    add(Key::PhotoFormat, "photo_format", picture, "File",
        "PNG and WebP are lossless; RAW is a DNG (Adobe's open RAW)", {"PNG", "JPEG", "WebP", "RAW", "RAW+JPEG"}, 0);
    add(Key::PhotoQuality, "photo_quality", picture, "JPEG quality", "", {"100", "95", "90", "85", "75", "60"}, 1);
    add(Key::PhotoRawData, "photo_raw_data", picture, "RAW data",
        "Linear RGB opens in every editor; Bayer is an RGGB mosaic like a sensor's", {"Linear", "Bayer"}, 0);
    add(Key::PhotoSize, "photo_size", picture, "Size", "The long side in pixels",
        {"View", "1280", "1920", "2560", "4K", "5K", "6K", "8K"}, 0);
    add(Key::PhotoMegapixels, "photo_megapixels", picture, "Megapixels", "Instead of the size, in the frame's shape",
        {"Size", "2 MP", "5 MP", "8 MP", "12 MP", "16 MP", "20 MP", "24 MP", "33 MP", "40 MP", "48 MP", "64 MP",
         "100 MP", "124 MP", "150 MP", "200 MP"},
        0);
    add(Key::PhotoTimer, "photo_timer", picture, "Self-timer", "", {"Off", "2 s", "5 s", "10 s"}, 0);
    add(Key::PhotoBurst, "photo_burst", picture, "Shots per press", "A quarter second apart", {"1", "3", "5", "10"}, 0);
    add(Key::PhotoSubtitles, "photo_subtitles", picture, "Subtitles in pictures", "", {"Show", "Hide"}, 0);
    const char* video = "Video";
    add(Key::VideoSize, "video_size", video, "Size", "The short side", {"720p", "1080p", "1440p", "4K"}, 1);
    add(Key::VideoFps, "video_fps", video, "Frame rate", "", {"24", "25", "30", "50", "60"}, 2);
    add(Key::VideoBitrate, "video_bitrate", video, "Bit rate", "Mbit/s", {"Auto", "4", "8", "16", "32", "64", "100"},
        0);
    add(Key::VideoCodec, "video_codec", video, "Codec", "H.264 (AVC) or H.265 (HEVC)", {"H.264", "H.265"}, 0);
    add(Key::VideoKeyframe, "video_keyframe", video, "Key frame every", "", {"0.5 s", "1 s", "2 s", "5 s"}, 1);
    add(Key::VideoLimit, "video_limit", video, "Stop after", "", {"Never", "15 s", "30 s", "1 min", "5 min", "10 min"},
        0);
    add(Key::VideoMotionBlur, "video_motion_blur", video, "Motion blur", "The frames before blend in by the shutter",
        {"Off", "Shutter"}, 0);
    add(Key::VideoSubtitles, "video_subtitles", video, "Subtitles in videos", "", {"Show", "Hide"}, 0);
    const char* lens = "Lens";
    add(Key::LensFocal, "lens_focal", lens, "Focal length", "Full frame; Zoom follows the pinch",
        {"Zoom", "14 mm", "18 mm", "24 mm", "28 mm", "35 mm", "50 mm", "85 mm", "105 mm", "135 mm", "200 mm",
         "300 mm", "400 mm"},
        0);
    add(Key::LensVignette, "lens_vignette", lens, "Vignetting", "Lens: by the aperture", {"Off", "Lens", "Strong"},
        0);
    add(Key::LensDistortion, "lens_distortion", lens, "Distortion", "Lens: barrel when wide, pincushion when long",
        {"Off", "Lens"}, 0);
    add(Key::LensFringe, "lens_fringe", lens, "Colour fringing", "", {"Off", "Weak", "Strong"}, 0);
    const char* aperture = "Aperture";
    {
        std::vector<std::string> names;
        for (const float n : kApertures) {
            char text[16];
            std::snprintf(text, sizeof(text), "f/%g", static_cast<double>(n));
            names.emplace_back(text);
        }
        add(Key::LensAperture, "lens_aperture", aperture, "Aperture", "", std::move(names), 6);
    }
    add(Key::LensDepthOfField, "lens_depth_of_field", aperture, "Depth of field",
        "What is not in focus blurs by the aperture and the focal length", {"Off", "On"}, 0);
    add(Key::LensFocus, "lens_focus", aperture, "Focus", "Auto: the middle of the frame, or a double tap",
        {"Auto", "0.5 m", "1 m", "2 m", "3 m", "5 m", "10 m", "20 m", "\xE2\x88\x9E"}, 0);
    add(Key::LensBlades, "lens_blades", aperture, "Aperture blades", "The shape of the bokeh",
        {"Round", "9", "8", "7", "6", "5"}, 0);
    add(Key::LensBokeh, "lens_bokeh", aperture, "Bokeh highlights", "", {"Soft", "Bright"}, 0);
    const char* filter = "Filter";
    add(Key::FilterPolarizer, "filter_polarizer", filter, "Polarizer",
        "Circular; in camera mode two fingers turn it", {"Off", "On"}, 0);
    add(Key::FilterPolarizerStrength, "filter_polarizer_strength", filter, "Polarizer strength", "",
        {"Weak", "Medium", "Strong"}, 1);
    const char* exposure = "Exposure";
    add(Key::ExposureMode, "exposure_mode", exposure, "Mode", "Manual: the ISO, shutter and aperture count",
        {"Auto", "Manual"}, 0);
    {
        std::vector<std::string> names;
        for (const int iso : kIsos) names.push_back(iso == 0 ? std::string("Auto") : std::to_string(iso));
        add(Key::ExposureIso, "exposure_iso", exposure, "ISO", "", std::move(names), 0);
    }
    {
        std::vector<std::string> names;
        for (const auto& t : kShutters) names.emplace_back(t.label);
        add(Key::ExposureShutter, "exposure_shutter", exposure, "Shutter", "", std::move(names), kShutterStart);
    }
    add(Key::ExposureCompensation, "exposure_compensation", exposure, "Compensation", "EV",
        {"\xE2\x88\x92" "3", "\xE2\x88\x92" "2", "\xE2\x88\x92" "1.5", "\xE2\x88\x92" "1", "\xE2\x88\x92" "0.5", "0",
         "+0.5", "+1", "+1.5", "+2", "+3"},
        5);
    add(Key::ExposureHighlights, "exposure_highlights", exposure, "Highlights", "", {"Clip", "Soft"}, 0);
    add(Key::ExposureNoise, "exposure_noise", exposure, "Sensor noise", "By the ISO, or film grain",
        {"Off", "ISO", "Grain"}, 1);
    const char* colour = "Colour";
    add(Key::ColourBalance, "colour_balance", colour, "White balance", "AWB is as rendered",
        {"AWB", "Day 5500K", "Cloud 6500K", "Shade 7500K", "Lamp 3200K", "Fluo 4000K", "Flash 5800K",
         "Candle 2000K", "Sky 10000K"},
        0);
    {
        std::vector<std::string> names;
        for (const char* st : kStyles) names.emplace_back(st);
        add(Key::ColourStyle, "colour_style", colour, "Picture style", "", std::move(names), 0);
    }
    const std::vector<std::string> steps{"\xE2\x88\x92" "2", "\xE2\x88\x92" "1", "0", "+1", "+2"};
    add(Key::ColourContrast, "colour_contrast", colour, "Contrast", "", steps, 2);
    add(Key::ColourSaturation, "colour_saturation", colour, "Saturation", "", steps, 2);
    add(Key::ColourSharpness, "colour_sharpness", colour, "Sharpness", "", {"Off", "Low", "Medium", "High"}, 0);
    const char* gyro = "Gyroscope";
    add(Key::GyroMode, "gyro_mode", gyro, "Gyroscope", "In camera mode, turning the phone turns the camera",
        {"Off", "On"}, 0);
    add(Key::GyroHorizon, "gyro_horizon", gyro, "Horizon",
        "Locked: level pictures, like a gimbal; Phone: the picture tilts with the phone", {"Locked", "Phone"}, 0);
    add(Key::GyroSensitivity, "gyro_sensitivity", gyro, "Sensitivity", "",
        {"0.5\xC3\x97", "1\xC3\x97", "1.5\xC3\x97", "2\xC3\x97"}, 1);
    add(Key::GyroSmoothing, "gyro_smoothing", gyro, "Smoothing", "Steadies the hands", {"Off", "Low", "Medium", "High"},
        2);
    const char* walking = "Walking (ARCore)";
    add(Key::WalkMode, "walk_mode", walking, "Walking", "Walk and crouch to move the camera (it flies the drone)",
        {"Off", "On"}, 0);
    add(Key::WalkScale, "walk_scale", walking, "A metre walked", "How far it moves the camera",
        {"0.25 m", "0.5 m", "1 m", "2 m", "5 m", "10 m", "20 m"}, 2);
    add(Key::WalkHeight, "walk_height", walking, "Height", "Follows: crouch and stand; Fixed: walking only",
        {"Follows", "Fixed"}, 0);
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
    (void)kind;  // one camera: pictures and videos share its frame
    const Aspect a = pick(Key::CameraShape, kShapes);
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

std::atomic<float> g_focus{kInfinity};

// Runs rows [0, height) in bands on the machine's cores.
template <typename Band>
void in_bands(int height, Band&& band) {
    const int threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 16);
    if (threads == 1 || height < 64) {
        band(0, height);
        return;
    }
    std::vector<std::thread> workers;
    const int step = (height + threads - 1) / threads;
    for (int begin = 0; begin < height; begin += step) {
        workers.emplace_back([&band, begin, end = std::min(height, begin + step)] { band(begin, end); });
    }
    for (auto& worker : workers) worker.join();
}

// The focus distance: a chosen one, or the middle of the frame (the median
// of what is drawn there).
float focus_distance(const std::vector<float>& depth, int w, int h) {
    const float chosen = pick(Key::LensFocus, kFocusUnits);
    if (chosen > 0.0F) return chosen;
    if (g_focus_point.load() > 0.0F) return g_focus_point.load();
    const int half = std::max(1, std::min(w, h) / 40);
    std::vector<float> near_middle;
    for (int y = h / 2 - half; y <= h / 2 + half; ++y) {
        for (int x = w / 2 - half; x <= w / 2 + half; ++x) {
            if (x < 0 || y < 0 || x >= w || y >= h) continue;
            const float z = depth[static_cast<std::size_t>(y) * w + x];
            if (std::isfinite(z) && z > 0.0F) near_middle.push_back(z);
        }
    }
    if (near_middle.empty()) return kInfinity;
    const auto mid = near_middle.begin() + static_cast<std::ptrdiff_t>(near_middle.size() / 2U);
    std::nth_element(near_middle.begin(), mid, near_middle.end());
    return *mid;
}

// Depth of field of a thin lens of focal length mm at aperture N focused at
// `focus`: each pixel gathers the pixels inside its circle of confusion
// (shaped by the blades), a nearer pixel reaching over a farther one but not
// the other way round, so a sharp subject keeps its edge.
void depth_of_field(RgbaImage* image, const std::vector<float>& depth, float mm, float aperture) {
    const int w = image->width;
    const int h = image->height;
    if (depth.size() != static_cast<std::size_t>(w) * h || w < 8 || h < 8) return;
    const float focus = focus_distance(depth, w, h);
    g_focus.store(focus);
    const float f = std::max(1.0F, mm);
    const float s = std::isfinite(focus) ? std::max(focus * kUnitMm, f * 1.05F) : kInfinity;
    const float px_per_mm = static_cast<float>(std::min(w, h)) / 24.0F;
    const float max_radius = 0.05F * static_cast<float>(std::min(w, h));
    const float k = f * f / aperture;
    // The circle of confusion's radius (pixels) of a pixel, worked out when
    // asked: a second full-size buffer would not fit beside 100+ MP.
    const auto radius_at = [&](std::size_t i) {
        const float z = std::isfinite(depth[i]) ? std::max(depth[i] * kUnitMm, f * 1.05F) : kInfinity;
        float coc = 0.0F;
        if (std::isfinite(s)) {
            coc = std::isfinite(z) ? k / (s - f) * std::fabs(z - s) / z : k / (s - f);
        } else if (std::isfinite(z)) {
            coc = k / z;
        }
        return std::min(max_radius, 0.5F * coc * px_per_mm);
    };
    float widest = 0.0F;
    for (std::size_t i = 0; i < depth.size(); i += 7U) widest = std::max(widest, radius_at(i));
    if (widest < 0.5F) return;
    // Golden-angle points in the unit disk, pushed out to the blades' polygon.
    constexpr int kTaps = 128;
    const int blades = pick(Key::LensBlades, kBlades);
    std::array<std::array<float, 2>, kTaps> taps{};
    for (int i = 0; i < kTaps; ++i) {
        const float rho = std::sqrt((static_cast<float>(i) + 0.5F) / kTaps);
        const float theta = static_cast<float>(i) * 2.39996323F;
        float edge = 1.0F;
        if (blades >= 5) {
            const float sector = 6.28318531F / static_cast<float>(blades);
            const float a = std::fmod(theta, sector) - 0.5F * sector;
            edge = std::cos(0.5F * sector) / std::cos(a);
        }
        taps[static_cast<std::size_t>(i)] = {rho * edge * std::cos(theta), rho * edge * std::sin(theta)};
    }
    const bool bright = choice(Key::LensBokeh) == 1;
    const std::vector<std::uint8_t> src = image->pixels;
    in_bands(h, [&](int row_begin, int row_end) {
        for (int y = row_begin; y < row_end; ++y) {
            for (int x = 0; x < w; ++x) {
                const std::size_t p = static_cast<std::size_t>(y) * w + x;
                const float r = radius_at(p);
                if (r < 0.5F) continue;
                const int n = std::clamp(static_cast<int>(r * r * 1.5F) + 4, 4, kTaps);
                const float zp = depth[p];
                float sum[3] = {0.0F, 0.0F, 0.0F};
                float weights = 0.0F;
                for (int t = 0; t < n; ++t) {
                    const float dx = taps[static_cast<std::size_t>(t)][0] * r;
                    const float dy = taps[static_cast<std::size_t>(t)][1] * r;
                    const int qx = std::clamp(x + static_cast<int>(std::lround(dx)), 0, w - 1);
                    const int qy = std::clamp(y + static_cast<int>(std::lround(dy)), 0, h - 1);
                    const std::size_t q = static_cast<std::size_t>(qy) * w + qx;
                    const float rq = radius_at(q);
                    const float reach = !(depth[q] < zp) ? std::min(rq, r) : rq;
                    if (reach + 0.5F < std::sqrt(dx * dx + dy * dy)) continue;
                    const auto* c = &src[q * 4U];
                    float weight = 1.0F;
                    if (bright) {
                        const float luma = (0.2126F * c[0] + 0.7152F * c[1] + 0.0722F * c[2]) / 255.0F;
                        weight += 8.0F * std::max(0.0F, luma - 0.75F);
                    }
                    sum[0] += weight * c[0];
                    sum[1] += weight * c[1];
                    sum[2] += weight * c[2];
                    weights += weight;
                }
                if (weights <= 0.0F) continue;
                auto* out = &image->pixels[p * 4U];
                for (int c = 0; c < 3; ++c) {
                    out[c] = static_cast<std::uint8_t>(std::clamp(sum[c] / weights + 0.5F, 0.0F, 255.0F));
                }
            }
        }
    });
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
        const int megapixels = pick(Key::PhotoMegapixels, kMegapixels);
        float w = 0.0F;
        float h = 0.0F;
        if (megapixels > 0) {
            // The pixel count in the picture's shape; the renderer's 8192
            // side caps it (a 50 MP square is 64 MP at most).
            w = std::sqrt(static_cast<float>(megapixels) * 1.0e6F * aw / ah);
            h = w * ah / aw;
        } else if (long_side == 0) {
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
        out.format = std::clamp(choice(Key::PhotoFormat), 0, 4);
        const bool jpeg = out.format == static_cast<int>(PhotoFormat::Jpeg) ||
                          out.format == static_cast<int>(PhotoFormat::DngJpeg);
        out.quality = jpeg ? pick(Key::PhotoQuality, kPhotoQualities) : 100;
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
    out.shutter_seconds = pick(Key::ExposureShutter, kShutters).seconds;
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
    // Manual: ISO 100 at 1/60 and f/4 is the rendered picture; a polarizer
    // takes about a stop and a half.
    float light = out.shutter_seconds * 60.0F * (4.0F / out.aperture) * (4.0F / out.aperture);
    if (choice(Key::FilterPolarizer) == 1) light *= 0.35F;
    out.iso = chosen_iso > 0 ? chosen_iso
                             : static_cast<int>(std::clamp(std::lround(100.0 / light), 25L, 4000000L));
    out.gain = static_cast<float>(out.iso) / 100.0F * light * compensation;
    return out;
}

namespace {

bool valid_image(const RgbaImage* image) noexcept {
    return image != nullptr && image->width > 0 && image->height > 0 &&
           image->pixels.size() >= static_cast<std::size_t>(image->width) * image->height * 4U;
}

std::atomic<float> g_polarizer{0.0F};

// The circular polarizer: each pixel's light is partly polarized (glare and
// whitish highlights the most, the blue of the sky, a little everything);
// the filter passes that part by cos² of its ring against the light's
// polarization, which turns slowly across the frame as a sky's does. What
// it holds back darkens the pixel and leaves its colour deeper.
void polarizer_pass(RgbaImage* image) {
    const int w = image->width;
    const int h = image->height;
    const float strength = pick(Key::FilterPolarizerStrength, kPolarizerStrength);
    const float ring = g_polarizer.load();
    std::array<float, 256> linear{};
    for (int v = 0; v < 256; ++v) linear[static_cast<std::size_t>(v)] = srgb_to_linear(static_cast<float>(v) / 255.0F);
    std::array<std::uint8_t, 4096> encode{};
    for (int i = 0; i < 4096; ++i) {
        encode[static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>(std::lround(linear_to_srgb(static_cast<float>(i) / 4095.0F) * 255.0F));
    }
    const auto smooth = [](float a, float b, float x) {
        const float t = std::clamp((x - a) / (b - a), 0.0F, 1.0F);
        return t * t * (3.0F - 2.0F * t);
    };
    in_bands(h, [&](int row_begin, int row_end) {
        for (int y = row_begin; y < row_end; ++y) {
            for (int x = 0; x < w; ++x) {
                auto* p = &image->pixels[(static_cast<std::size_t>(y) * w + x) * 4U];
                const float r = p[0] / 255.0F;
                const float g = p[1] / 255.0F;
                const float b = p[2] / 255.0F;
                const float hi = std::max({r, g, b});
                const float lo = std::min({r, g, b});
                const float saturation = hi > 1.0e-3F ? (hi - lo) / hi : 0.0F;
                const float luma = 0.2126F * r + 0.7152F * g + 0.0722F * b;
                const float glare = smooth(0.55F, 0.95F, luma) * (1.0F - saturation);
                const float sky = std::clamp((b - std::max(r, g)) / std::max(b, 1.0e-3F), 0.0F, 1.0F) *
                                  smooth(0.25F, 0.7F, b);
                const float polarized = std::clamp(0.15F + 0.85F * glare + 0.6F * sky, 0.0F, 0.95F);
                const float light_angle = 0.5F * (static_cast<float>(x) / w - 0.5F) +
                                          0.3F * (static_cast<float>(y) / h - 0.5F);
                const float s = std::sin(ring - light_angle);
                const float held = strength * polarized * s * s;
                // Darker by what is held back, the colour deeper by it.
                float lr = linear[p[0]] * (1.0F - held);
                float lg = linear[p[1]] * (1.0F - held);
                float lb = linear[p[2]] * (1.0F - held);
                const float l = 0.2126F * lr + 0.7152F * lg + 0.0722F * lb;
                const float deeper = 1.0F + 0.8F * held;
                lr = l + (lr - l) * deeper;
                lg = l + (lg - l) * deeper;
                lb = l + (lb - l) * deeper;
                const auto out = [&](float v) { return encode[static_cast<std::size_t>(std::clamp(v, 0.0F, 1.0F) * 4095.0F + 0.5F)]; };
                p[0] = out(lr);
                p[1] = out(lg);
                p[2] = out(lb);
            }
        }
    });
}

// What the lens does to the light: the polarizer in front of it, the depth
// of field, the distortion and the colour fringing (a RAW keeps these).
void optics(RgbaImage* image, float mm, const std::vector<float>* depth) {
    if (!valid_image(image)) return;
    if (choice(Key::FilterPolarizer) == 1) polarizer_pass(image);
    if (depth != nullptr && depth_of_field_on()) depth_of_field(image, *depth, mm, exposure().aperture);
    const float k = choice(Key::LensDistortion) == 1 ? distortion_k(mm) : 0.0F;
    const float fringe = choice(Key::LensFringe) == 0 ? 0.0F : (choice(Key::LensFringe) == 1 ? 0.0012F : 0.003F);
    if (k != 0.0F || fringe != 0.0F) lens_pass(image, k, fringe);
}

float vignette_strength() noexcept {
    const int vignette_choice = choice(Key::LensVignette);
    return vignette_choice == 0   ? 0.0F
           : vignette_choice == 2 ? 0.6F
                                  : std::clamp(0.62F * std::pow(1.4F / exposure().aperture, 0.8F), 0.04F, 0.6F);
}

// Sensor noise (8-bit steps) of the ISO the exposure works at.
float noise_sigma() noexcept {
    const int noise_choice = choice(Key::ExposureNoise);
    const int iso = exposure().iso;
    float sigma = 0.0F;
    if (noise_choice != 0 && iso > 100) sigma = 1.6F * std::pow(std::log2(static_cast<float>(iso) / 100.0F), 1.3F);
    if (noise_choice == 2) sigma = std::max(sigma, 5.0F);
    return sigma;
}

// What the camera makes of the light: exposure, white balance, the curves,
// the style, vignetting, noise and sharpening (a RAW leaves these to the editor).
void look(RgbaImage* image, std::uint32_t seed) {
    if (!valid_image(image)) return;
    const int w = image->width;
    const int h = image->height;
    const float scale = static_cast<float>(std::max(w, h)) / 1920.0F;

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
    const float vignette = vignette_strength();
    const int noise_choice = choice(Key::ExposureNoise);
    const float sigma = noise_sigma();
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

}  // namespace

void develop(RgbaImage* image, std::uint32_t seed, float mm, const std::vector<float>* depth) {
    optics(image, mm, depth);
    look(image, seed);
}

bool depth_of_field_on() noexcept { return choice(Key::LensDepthOfField) == 1; }

float last_focus() noexcept { return g_focus.load(); }

void develop(RgbaImage* image, std::uint32_t seed) {
    const float fixed = fixed_lens_zoom();
    develop(image, seed, fixed > 0.0F ? focal_mm(fixed) : 50.0F);
}

namespace {

// Sizes of `total` split into the fewest pieces of at most `limit` (even
// pieces, so none is a sliver).
std::vector<std::pair<int, int>> spans(int total, int limit) {
    const int count = std::max(1, (total + limit - 1) / limit);
    std::vector<std::pair<int, int>> out;
    int begin = 0;
    for (int i = 0; i < count; ++i) {
        const int end = static_cast<int>(static_cast<long long>(total) * (i + 1) / count);
        out.emplace_back(begin, end - begin);
        begin = end;
    }
    return out;
}

// ---- DNG (Adobe's open RAW, TIFF based) --------------------------------------

struct TiffEntry final {
    std::uint16_t tag{};
    std::uint16_t type{};  // 1 BYTE, 2 ASCII, 3 SHORT, 4 LONG, 5 RATIONAL, 10 SRATIONAL
    std::uint32_t count{};
    std::vector<std::uint8_t> data;
};

void put16(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8U));
}
void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    put16(out, v & 0xFFFFU);
    put16(out, v >> 16U);
}

TiffEntry shorts(std::uint16_t tag, std::initializer_list<std::uint32_t> values) {
    TiffEntry e{tag, 3, static_cast<std::uint32_t>(values.size()), {}};
    for (const auto v : values) put16(e.data, v);
    return e;
}
TiffEntry longs(std::uint16_t tag, std::initializer_list<std::uint32_t> values) {
    TiffEntry e{tag, 4, static_cast<std::uint32_t>(values.size()), {}};
    for (const auto v : values) put32(e.data, v);
    return e;
}
TiffEntry bytes(std::uint16_t tag, std::initializer_list<std::uint8_t> values) {
    return TiffEntry{tag, 1, static_cast<std::uint32_t>(values.size()), std::vector<std::uint8_t>(values)};
}
TiffEntry ascii(std::uint16_t tag, const std::string& text) {
    TiffEntry e{tag, 2, static_cast<std::uint32_t>(text.size() + 1U), {}};
    e.data.assign(text.begin(), text.end());
    e.data.push_back(0U);
    return e;
}
TiffEntry rationals(std::uint16_t tag, bool is_signed, std::initializer_list<double> values) {
    TiffEntry e{tag, static_cast<std::uint16_t>(is_signed ? 10 : 5), static_cast<std::uint32_t>(values.size()), {}};
    for (const double v : values) {
        const std::int32_t den = 10000;
        const auto num = static_cast<std::int32_t>(std::lround(v * den));
        put32(e.data, static_cast<std::uint32_t>(num));
        put32(e.data, static_cast<std::uint32_t>(den));
    }
    return e;
}

std::size_t ifd_size(const std::vector<TiffEntry>& entries) {
    std::size_t size = 2U + entries.size() * 12U + 4U;
    for (const auto& e : entries) {
        if (e.data.size() > 4U) size += (e.data.size() + 1U) & ~std::size_t{1};
    }
    return size;
}

// One IFD at `offset` (entries sorted by tag), its values after it.
std::vector<std::uint8_t> ifd_bytes(std::vector<TiffEntry> entries, std::uint32_t offset) {
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.tag < b.tag; });
    std::vector<std::uint8_t> out;
    std::vector<std::uint8_t> extra;
    const std::uint32_t extra_at = offset + 2U + static_cast<std::uint32_t>(entries.size()) * 12U + 4U;
    put16(out, static_cast<std::uint32_t>(entries.size()));
    for (const auto& e : entries) {
        put16(out, e.tag);
        put16(out, e.type);
        put32(out, e.count);
        if (e.data.size() <= 4U) {
            for (std::size_t i = 0; i < 4U; ++i) out.push_back(i < e.data.size() ? e.data[i] : 0U);
        } else {
            put32(out, extra_at + static_cast<std::uint32_t>(extra.size()));
            extra.insert(extra.end(), e.data.begin(), e.data.end());
            if (extra.size() % 2U != 0U) extra.push_back(0U);
        }
    }
    put32(out, 0U);  // no next IFD
    out.insert(out.end(), extra.begin(), extra.end());
    return out;
}

bool write_all(int fd, const std::uint8_t* data, std::size_t size) {
    while (size > 0U) {
        const auto n = ::write(fd, data, size);
        if (n <= 0) return false;
        data += n;
        size -= static_cast<std::size_t>(n);
    }
    return true;
}

// A small developed copy for the gallery: box-sampled to at most 1024 long.
RgbaImage preview_of(const RgbaImage& image) {
    const int side = std::max(image.width, image.height);
    const int step = std::max(1, (side + 1023) / 1024);
    RgbaImage out;
    out.width = std::max(1, image.width / step);
    out.height = std::max(1, image.height / step);
    out.pixels.assign(static_cast<std::size_t>(out.width) * out.height * 4U, 255U);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            std::uint32_t sum[3] = {0U, 0U, 0U};
            for (int sy = 0; sy < step; ++sy) {
                for (int sx = 0; sx < step; ++sx) {
                    const auto* p = &image.pixels[(static_cast<std::size_t>(y * step + sy) * image.width +
                                                   static_cast<std::size_t>(x * step + sx)) * 4U];
                    for (int c = 0; c < 3; ++c) sum[c] += p[c];
                }
            }
            auto* o = &out.pixels[(static_cast<std::size_t>(y) * out.width + x) * 4U];
            for (int c = 0; c < 3; ++c) o[c] = static_cast<std::uint8_t>(sum[c] / static_cast<std::uint32_t>(step * step));
        }
    }
    return out;
}

}  // namespace

RgbaImage render_large(const Session* session, int width, int height, float yaw, float pitch, float zoom,
                       std::uint32_t render_flags, const ViewControls& controls, std::vector<float>* depth,
                       int max_side) {
    max_side = std::clamp(max_side, 256, kMaxRenderTile);
    if (width <= max_side && height <= max_side) {
        return render_session(session, width, height, yaw, pitch, zoom, render_flags, controls, depth);
    }
    // Tiles of the same camera: each has the whole picture's focal length
    // and its centre where the picture's centre falls in it.
    const int tile_limit = std::min(max_side, kTileSide);
    RgbaImage out;
    out.width = width;
    out.height = height;
    out.pixels.resize(static_cast<std::size_t>(width) * height * 4U);
    if (depth != nullptr) depth->assign(static_cast<std::size_t>(width) * height, kInfinity);
    std::vector<float> tile_depth;
    for (const auto& [ty, th] : spans(height, tile_limit)) {
        for (const auto& [tx, tw] : spans(width, tile_limit)) {
            ViewControls tiled = controls;
            tiled.tile.on = true;
            // A picture with a focal length of its own (a rolled one) keeps it.
            tiled.tile.focal_side = controls.tile.on ? controls.tile.focal_side
                                                     : static_cast<float>(std::min(width, height));
            tiled.tile.principal_x = 0.5F * static_cast<float>(width) - static_cast<float>(tx) - 0.5F * static_cast<float>(tw);
            tiled.tile.principal_y = 0.5F * static_cast<float>(height) - static_cast<float>(ty) - 0.5F * static_cast<float>(th);
            tiled.tile.full_height = height;
            const auto tile = render_session(session, tw, th, yaw, pitch, zoom, render_flags, tiled,
                                             depth != nullptr ? &tile_depth : nullptr);
            if (tile.width != tw || tile.height != th) return {};
            for (int y = 0; y < th; ++y) {
                std::memcpy(&out.pixels[(static_cast<std::size_t>(ty + y) * width + tx) * 4U],
                            &tile.pixels[static_cast<std::size_t>(y) * tw * 4U], static_cast<std::size_t>(tw) * 4U);
                if (depth != nullptr && tile_depth.size() == static_cast<std::size_t>(tw) * th) {
                    std::memcpy(&(*depth)[static_cast<std::size_t>(ty + y) * width + tx],
                                &tile_depth[static_cast<std::size_t>(y) * tw], static_cast<std::size_t>(tw) * sizeof(float));
                }
            }
        }
    }
    return out;
}

namespace {

// A picture through a camera rolled about its axis: a larger frame of the
// same focal length (it holds the turned one), sampled back turned. A
// pinhole camera turned about its axis is exactly its picture turned.
RgbaImage render_rolled(const Session* session, int width, int height, float yaw, float pitch, float zoom,
                        std::uint32_t render_flags, const ViewControls& controls, std::vector<float>* depth) {
    const float roll = camera_roll();
    if (std::fabs(roll) < 1.0e-4F) {
        return render_large(session, width, height, yaw, pitch, zoom, render_flags, controls, depth);
    }
    const float c = std::cos(roll);
    const float s = std::sin(roll);
    const int big_w = (static_cast<int>(std::ceil(width * std::fabs(c) + height * std::fabs(s))) + 3) & ~1;
    const int big_h = (static_cast<int>(std::ceil(width * std::fabs(s) + height * std::fabs(c))) + 3) & ~1;
    ViewControls wide = controls;
    wide.tile.on = true;
    wide.tile.focal_side = static_cast<float>(std::min(width, height));
    wide.tile.principal_x = 0.0F;
    wide.tile.principal_y = 0.0F;
    wide.tile.full_height = height;
    std::vector<float> big_depth;
    const auto big = render_large(session, big_w, big_h, yaw, pitch, zoom, render_flags, wide,
                                  depth != nullptr ? &big_depth : nullptr);
    if (!valid_image(&big)) return {};
    RgbaImage out;
    out.width = width;
    out.height = height;
    out.pixels.assign(static_cast<std::size_t>(width) * height * 4U, 255U);
    const bool with_depth = depth != nullptr && big_depth.size() == static_cast<std::size_t>(big_w) * big_h;
    if (depth != nullptr) depth->assign(with_depth ? static_cast<std::size_t>(width) * height : 0U, kInfinity);
    in_bands(height, [&](int row_begin, int row_end) {
        for (int y = row_begin; y < row_end; ++y) {
            for (int x = 0; x < width; ++x) {
                const float dx = static_cast<float>(x) + 0.5F - 0.5F * static_cast<float>(width);
                const float dy = static_cast<float>(y) + 0.5F - 0.5F * static_cast<float>(height);
                const float sx = std::clamp(0.5F * big_w + dx * c - dy * s - 0.5F, 0.0F, static_cast<float>(big_w - 1));
                const float sy = std::clamp(0.5F * big_h + dx * s + dy * c - 0.5F, 0.0F, static_cast<float>(big_h - 1));
                const int x0 = static_cast<int>(sx);
                const int y0 = static_cast<int>(sy);
                const int x1 = std::min(x0 + 1, big_w - 1);
                const int y1 = std::min(y0 + 1, big_h - 1);
                const float fx = sx - static_cast<float>(x0);
                const float fy = sy - static_cast<float>(y0);
                auto* o = &out.pixels[(static_cast<std::size_t>(y) * width + x) * 4U];
                for (int k = 0; k < 3; ++k) {
                    const auto at = [&](int px, int py) {
                        return static_cast<float>(big.pixels[(static_cast<std::size_t>(py) * big_w + px) * 4U + k]);
                    };
                    const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * fx;
                    const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * fx;
                    o[k] = static_cast<std::uint8_t>(std::clamp(top + (bottom - top) * fy + 0.5F, 0.0F, 255.0F));
                }
                if (with_depth) {
                    const int nx = std::lround(sx) < big_w ? static_cast<int>(std::lround(sx)) : big_w - 1;
                    const int ny = std::lround(sy) < big_h ? static_cast<int>(std::lround(sy)) : big_h - 1;
                    (*depth)[static_cast<std::size_t>(y) * width + x] = big_depth[static_cast<std::size_t>(ny) * big_w + nx];
                }
            }
        }
    });
    return out;
}

struct GyroState final {
    bool started{};
    float heading{};
    float elevation{};
    float roll{};
};
std::mutex& gyro_mutex() {
    static std::mutex m;
    return m;
}
GyroState& gyro_state() {
    static GyroState state;
    return state;
}

}  // namespace

bool gyro_active() noexcept { return finder_on() && choice(Key::GyroMode) == 1; }

bool walk_active() noexcept { return finder_on() && choice(Key::WalkMode) == 1; }

bool walk(float yaw_radians, float forward_m, float right_m, float up_m) noexcept {
    if (!walk_active()) return false;
    // A scene unit is a centimetre; the scale makes a room's walk a street's.
    const float units = 1000.0F / kUnitMm * pick(Key::WalkScale, kWalkScale);
    const float rise = choice(Key::WalkHeight) == 0 ? up_m * units : 0.0F;
    return dmcresource::camera::walk_eye(yaw_radians, forward_m * units, right_m * units, rise);
}

std::array<float, 2> gyro_turn(float heading, float elevation, float roll) noexcept {
    if (!gyro_active() || !std::isfinite(heading) || !std::isfinite(elevation) || !std::isfinite(roll)) {
        return {0.0F, 0.0F};
    }
    const std::scoped_lock lock{gyro_mutex()};
    auto& g = gyro_state();
    if (!g.started) {
        g = {true, heading, elevation, roll};
        g_roll_phone.store(roll);
        return {0.0F, 0.0F};
    }
    // Each reading takes a share of the way to the phone (the smoothing);
    // the camera turns by what the smoothed phone turned, times the
    // sensitivity. Looking straight up or down the heading means nothing.
    const float follow = pick(Key::GyroSmoothing, kGyroFollow);
    const float sensitivity = pick(Key::GyroSensitivity, kGyroSensitivity);
    const float heading_step = follow * wrap_angle(heading - g.heading);
    const float elevation_step = follow * (elevation - g.elevation);
    g.heading = wrap_angle(g.heading + heading_step);
    g.elevation += elevation_step;
    g.roll = wrap_angle(g.roll + follow * wrap_angle(roll - g.roll));
    g_roll_phone.store(g.roll);
    const bool steep = std::fabs(g.elevation) > 1.3F;
    // As the drone's look stick: turning right lowers the yaw, looking up
    // raises the pitch.
    return {steep ? 0.0F : -heading_step * sensitivity, elevation_step * sensitivity};
}

void gyro_reset() noexcept {
    const std::scoped_lock lock{gyro_mutex()};
    gyro_state().started = false;
    g_roll_phone.store(0.0F);
}

float camera_roll() noexcept {
    const bool follows = gyro_active() && choice(Key::GyroHorizon) == 1;
    return wrap_angle(g_roll_manual.load() + (follows ? g_roll_phone.load() : 0.0F));
}

float turn_roll(float radians) noexcept {
    if (std::isfinite(radians)) g_roll_manual.store(wrap_angle(g_roll_manual.load() + radians));
    return camera_roll();
}

void level_camera() noexcept { g_roll_manual.store(0.0F); }

float focus_at(const Session* session, int width, int height, float yaw, float pitch, float zoom,
               std::uint32_t render_flags, const ViewControls& controls, float x, float y) {
    if (session == nullptr || session->uv_gallery || width <= 0 || height <= 0) return 0.0F;
    // A small render of the same view is enough to know what is there.
    const float scale = std::min(1.0F, 480.0F / static_cast<float>(std::max(width, height)));
    const int w = std::max(16, static_cast<int>(width * scale));
    const int h = std::max(16, static_cast<int>(height * scale));
    ViewControls framed = controls;
    if (fixed_lens_zoom() > 0.0F) framed.lens_zoom = finder_zoom(Kind::Photo, zoom, width, height);
    std::vector<float> depth;
    const auto small = render_rolled(session, w, h, yaw, pitch, zoom,
                                     render_flags | render_flag(RenderFlag::Preview), framed, &depth);
    if (depth.size() != static_cast<std::size_t>(w) * h) return 0.0F;
    const int cx = std::clamp(static_cast<int>(x * w / width), 0, w - 1);
    const int cy = std::clamp(static_cast<int>(y * h / height), 0, h - 1);
    std::vector<float> near;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            const int px = std::clamp(cx + dx, 0, w - 1);
            const int py = std::clamp(cy + dy, 0, h - 1);
            const float z = depth[static_cast<std::size_t>(py) * w + px];
            if (std::isfinite(z) && z > 0.0F) near.push_back(z);
        }
    }
    // Nothing there (the sky): focus at infinity.
    float distance = 1.0e9F;
    if (!near.empty()) {
        std::nth_element(near.begin(), near.begin() + static_cast<std::ptrdiff_t>(near.size() / 2U), near.end());
        distance = near[near.size() / 2U];
    }
    g_focus_point.store(distance);
    (void)small;
    return distance;
}

bool toggle_focus_lock() noexcept {
    if (g_focus_point.load() > 0.0F) {
        g_focus_point.store(0.0F);
        return false;
    }
    const float found = g_focus.load();
    g_focus_point.store(std::isfinite(found) && found > 0.0F ? found : 1.0e9F);
    return true;
}

float turn_focus_ring(float steps) noexcept {
    float base = g_focus_point.load();
    if (!(base > 0.0F) || base >= 1.0e9F) {
        const float found = g_focus.load();
        base = std::isfinite(found) && found > 0.0F ? found : 500.0F;
    }
    const float distance = std::clamp(base * std::exp(std::isfinite(steps) ? steps : 0.0F), 20.0F, 1.0e6F);
    g_focus_point.store(distance);
    return distance;
}

float focus_point() noexcept { return g_focus_point.load(); }

void clear_focus_point() noexcept { g_focus_point.store(0.0F); }

float units_to_metres(float units) noexcept { return units * kUnitMm / 1000.0F; }

std::string focus_message(float units) {
    std::string text;
    if (!(units > 0.0F)) {
        text = "Auto focus";
    } else if (units >= 1.0e9F) {
        text = "Focus: infinity";
    } else {
        char line[32];
        std::snprintf(line, sizeof(line), "Focus %.1f m", static_cast<double>(units_to_metres(units)));
        text = line;
    }
    if (!depth_of_field_on()) text += " (the Focus dial turns the depth of field on to see it)";
    return text;
}

bool write_dng(int fd, const RgbaImage& optical, const RgbaImage& preview, bool bayer, std::uint32_t seed,
               float focal_length_mm) {
    if (fd < 0 || !valid_image(&optical) || !valid_image(&preview)) return false;
    const auto w = static_cast<std::uint32_t>(optical.width);
    const auto h = static_cast<std::uint32_t>(optical.height);
    const std::uint32_t spp = bayer ? 1U : 3U;
    const std::uint64_t raw_bytes = static_cast<std::uint64_t>(w) * h * spp * 2U;
    if (raw_bytes > 0xF0000000ULL) return false;  // TIFF offsets are 32-bit
    const auto pw = static_cast<std::uint32_t>(preview.width);
    const auto ph = static_cast<std::uint32_t>(preview.height);
    const std::uint32_t preview_bytes = pw * ph * 3U;
    const Exposure e = exposure();
    // The white balance travels as the neutral the editor divides by.
    const auto wb = balance_gains();
    std::array<double, 3> neutral{1.0 / wb[0], 1.0 / wb[1], 1.0 / wb[2]};
    const double top = std::max({neutral[0], neutral[1], neutral[2]});
    for (auto& n : neutral) n /= top;

    std::vector<TiffEntry> main{
        longs(254, {1U}), longs(256, {pw}), longs(257, {ph}), shorts(258, {8U, 8U, 8U}), shorts(259, {1U}),
        shorts(262, {2U}), ascii(271, "DMC Native Reader"), ascii(272, "Virtual camera"), longs(273, {0U}),
        shorts(274, {1U}), shorts(277, {3U}), longs(278, {ph}), longs(279, {preview_bytes}), shorts(284, {1U}),
        ascii(305, "DMC Native Reader"), longs(330, {0U}),
        rationals(33434, false, {static_cast<double>(e.shutter_seconds)}),
        rationals(33437, false, {static_cast<double>(e.aperture)}),
        shorts(34855, {static_cast<std::uint32_t>(std::clamp(e.iso, 1, 65535))}),
        shorts(34864, {3U}),  // SensitivityType: ISO speed, the whole of it below
        longs(34867, {static_cast<std::uint32_t>(std::max(e.iso, 1))}),
        rationals(37386, false, {static_cast<double>(focal_length_mm)}),
        bytes(50706, {1U, 4U, 0U, 0U}), bytes(50707, {1U, 1U, 0U, 0U}),
        ascii(50708, "DMC Native Reader virtual camera"),
        // XYZ (D65) to the camera's colours, which are linear sRGB.
        rationals(50721, true, {3.2404542, -1.5371385, -0.4985314, -0.9692660, 1.8760108, 0.0415560, 0.0556434,
                                -0.2040259, 1.0572252}),
        rationals(50728, false, {neutral[0], neutral[1], neutral[2]}),
        rationals(50730, true, {0.0}),
        shorts(50778, {21U}),
    };
    std::vector<TiffEntry> raw{
        longs(254, {0U}), longs(256, {w}), longs(257, {h}),
        bayer ? shorts(258, {16U}) : shorts(258, {16U, 16U, 16U}), shorts(259, {1U}),
        shorts(262, {bayer ? 32803U : 34892U}), longs(273, {0U}), shorts(277, {spp}), longs(278, {h}),
        longs(279, {static_cast<std::uint32_t>(raw_bytes)}), shorts(284, {1U}),
        longs(50714, {0U}), longs(50717, {65535U}),
    };
    if (bayer) {
        raw.push_back(shorts(33421, {2U, 2U}));
        raw.push_back(bytes(33422, {0U, 1U, 1U, 2U}));  // RGGB
        raw.push_back(bytes(50710, {0U, 1U, 2U}));
        raw.push_back(shorts(50711, {1U}));
    }
    const std::uint32_t main_at = 8U;
    const auto preview_at = static_cast<std::uint32_t>(main_at + ifd_size(main));
    const std::uint32_t raw_ifd_at = (preview_at + preview_bytes + 1U) & ~1U;
    const auto raw_at = static_cast<std::uint32_t>(raw_ifd_at + ifd_size(raw));
    for (auto& entry : main) {
        if (entry.tag == 273) entry = longs(273, {preview_at});
        if (entry.tag == 330) entry = longs(330, {raw_ifd_at});
    }
    for (auto& entry : raw) {
        if (entry.tag == 273) entry = longs(273, {raw_at});
    }

    std::vector<std::uint8_t> head{'I', 'I', 42U, 0U};
    put32(head, main_at);
    const auto main_bytes = ifd_bytes(main, main_at);
    head.insert(head.end(), main_bytes.begin(), main_bytes.end());
    if (!write_all(fd, head.data(), head.size())) return false;
    std::vector<std::uint8_t> row(static_cast<std::size_t>(std::max(pw * 3U, w * spp * 2U)) + 1U);
    for (std::uint32_t y = 0; y < ph; ++y) {
        for (std::uint32_t x = 0; x < pw; ++x) {
            const auto* p = &preview.pixels[(static_cast<std::size_t>(y) * pw + x) * 4U];
            row[x * 3U] = p[0];
            row[x * 3U + 1U] = p[1];
            row[x * 3U + 2U] = p[2];
        }
        if (!write_all(fd, row.data(), pw * 3U)) return false;
    }
    if ((preview_at + preview_bytes) % 2U != 0U) {
        const std::uint8_t pad = 0U;
        if (!write_all(fd, &pad, 1U)) return false;
    }
    const auto raw_ifd = ifd_bytes(raw, raw_ifd_at);
    if (!write_all(fd, raw_ifd.data(), raw_ifd.size())) return false;

    // The sensor's values: linear light times the exposure and the lens's
    // vignetting, its noise, clipped at full scale. Colour, curves, style and
    // sharpening are the editor's.
    std::array<float, 256> linear{};
    for (int v = 0; v < 256; ++v) linear[static_cast<std::size_t>(v)] = srgb_to_linear(static_cast<float>(v) / 255.0F);
    const float vignette = vignette_strength();
    const float sigma = noise_sigma() / 255.0F * 0.5F;
    const float cx = 0.5F * static_cast<float>(w);
    const float cy = 0.5F * static_cast<float>(h);
    const float inv_r2 = 1.0F / (cx * cx + cy * cy);
    for (std::uint32_t y = 0; y < h; ++y) {
        auto* out = row.data();
        for (std::uint32_t x = 0; x < w; ++x) {
            const auto* p = &optical.pixels[(static_cast<std::size_t>(y) * w + x) * 4U];
            float light = e.gain;
            if (vignette > 0.0F) {
                const float dx = static_cast<float>(x) - cx;
                const float dy = static_cast<float>(y) - cy;
                const float rr = (dx * dx + dy * dy) * inv_r2;
                light *= 1.0F - vignette * rr * std::sqrt(rr);
            }
            const auto sample = [&](int c) {
                float v = linear[p[c]] * light;
                if (sigma > 0.0F) v += noise(hash(x, y, seed ^ static_cast<std::uint32_t>(c * 0x9E3779B9U))) * sigma;
                const auto q = static_cast<std::uint32_t>(std::clamp(v, 0.0F, 1.0F) * 65535.0F + 0.5F);
                *out++ = static_cast<std::uint8_t>(q);
                *out++ = static_cast<std::uint8_t>(q >> 8U);
            };
            if (bayer) {
                sample((y % 2U == 0U) ? (x % 2U == 0U ? 0 : 1) : (x % 2U == 0U ? 1 : 2));
            } else {
                sample(0);
                sample(1);
                sample(2);
            }
        }
        if (!write_all(fd, row.data(), static_cast<std::size_t>(w) * spp * 2U)) return false;
    }
    return true;
}

bool capture_raw(const Session* session, int view_width, int view_height, float yaw, float pitch, float zoom,
                 std::uint32_t render_flags, const ViewControls& controls, int fd, RgbaImage* developed) {
    if (session == nullptr || session->uv_gallery) return false;
    const Plan p = plan(Kind::Photo, view_width, view_height);
    ViewControls framed = controls;
    frame_controls(Kind::Photo, view_width, view_height, &framed);
    std::vector<float> depth;
    auto image = render_rolled(session, p.width, p.height, yaw, pitch, zoom, render_flags, framed,
                               depth_of_field_on() ? &depth : nullptr);
    if (!valid_image(&image)) return false;
    const float lens = framed.lens_zoom > 0.0F ? framed.lens_zoom
                                               : effective_view_zoom(session, zoom) * framed.zoom_scale;
    const float mm = focal_mm(lens);
    optics(&image, mm, &depth);
    depth = {};
    const std::uint32_t seed = g_seed.fetch_add(1U);
    auto preview = preview_of(image);
    look(&preview, seed);
    if (!write_dng(fd, image, preview, choice(Key::PhotoRawData) == 1, seed, mm)) return false;
    if (developed != nullptr) {
        look(&image, seed);
        *developed = std::move(image);
    }
    return true;
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
    std::vector<float> depth;
    auto image = render_rolled(session, p.width, p.height, yaw, pitch, zoom, render_flags, framed,
                               depth_of_field_on() ? &depth : nullptr);
    const float lens = framed.lens_zoom > 0.0F ? framed.lens_zoom
                                               : effective_view_zoom(session, zoom) * framed.zoom_scale;
    develop(&image, g_seed.fetch_add(1U), focal_mm(lens), &depth);
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

std::vector<Dial> dials() {
    std::vector<Dial> out;
    const auto option = [](Key key) {
        return settings()[static_cast<std::size_t>(key)].options[static_cast<std::size_t>(
            std::clamp(choice(key), 0, static_cast<int>(settings()[static_cast<std::size_t>(key)].options.size()) - 1))];
    };
    const int mm = pick(Key::LensFocal, kFocalMm);
    out.push_back({Key::LensFocal, "Lens", mm > 0 ? std::to_string(mm) + " mm" : std::string("Zoom"), {}});
    out.push_back({Key::LensAperture, "Aperture", option(Key::LensAperture), {}});
    out.push_back({Key::ExposureMode, "Mode", choice(Key::ExposureMode) == 0 ? "P" : "M",
                   {Key::ExposureHighlights, Key::ExposureNoise}});
    const int iso = pick(Key::ExposureIso, kIsos);
    out.push_back({Key::ExposureIso, "ISO", iso > 0 ? std::to_string(iso) : std::string("Auto"), {}});
    out.push_back({Key::ExposureShutter, "Shutter", pick(Key::ExposureShutter, kShutters).label, {}});
    const float ev = pick(Key::ExposureCompensation, kCompensation);
    char text[24];
    std::snprintf(text, sizeof(text), ev == 0.0F ? "\xC2\xB1" "0" : "%+g", static_cast<double>(ev));
    out.push_back({Key::ExposureCompensation, "EV", text, {}});
    const int kelvin = pick(Key::ColourBalance, kKelvin);
    out.push_back({Key::ColourBalance, "WB", kelvin == 0 ? std::string("AWB") : std::to_string(kelvin) + " K", {}});
    out.push_back({Key::CameraShape, "Frame",
                   choice(Key::CameraShape) == 0 ? std::string("View") : option(Key::CameraShape), {}});
    std::string focus = "Off";
    if (depth_of_field_on()) {
        focus = choice(Key::LensFocus) != 0 ? option(Key::LensFocus) : std::string("AF");
        const float point = g_focus_point.load();
        if (choice(Key::LensFocus) == 0 && point > 0.0F) {
            if (point >= 1.0e9F) {
                focus = "MF \xE2\x88\x9E";
            } else {
                std::snprintf(text, sizeof(text), "MF %.1f m", static_cast<double>(units_to_metres(point)));
                focus = text;
            }
        }
    }
    out.push_back({Key::LensDepthOfField, "Focus", focus, {Key::LensFocus, Key::LensBlades, Key::LensBokeh}});
    std::string polarizer = "Off";
    if (choice(Key::FilterPolarizer) == 1) {
        float degrees = std::fmod(g_polarizer.load() * 57.2957795F, 180.0F);
        if (degrees < 0.0F) degrees += 180.0F;
        std::snprintf(text, sizeof(text), "%.0f\xC2\xB0", static_cast<double>(degrees));
        polarizer = text;
    }
    out.push_back({Key::FilterPolarizer, "CPL", polarizer, {Key::FilterPolarizerStrength}});
    out.push_back({Key::ColourStyle, "Style", option(Key::ColourStyle),
                   {Key::ColourContrast, Key::ColourSaturation, Key::ColourSharpness}});
    constexpr const char* formats[] = {"PNG", "JPEG", "WebP", "RAW", "RAW+JPG"};
    out.push_back({Key::PhotoFormat, "File", formats[std::clamp(choice(Key::PhotoFormat), 0, 4)],
                   {Key::PhotoMegapixels, Key::PhotoSize, Key::VideoSize, Key::VideoFps}});
    return out;
}

bool polarizer_turnable() noexcept { return finder_on() && choice(Key::FilterPolarizer) == 1; }

float turn_polarizer(float radians) noexcept {
    float angle = g_polarizer.load() + (std::isfinite(radians) ? radians : 0.0F);
    angle = std::fmod(angle, 3.14159265F);  // half a turn is the same filter
    if (angle < 0.0F) angle += 3.14159265F;
    g_polarizer.store(angle);
    return angle;
}

float polarizer_angle() noexcept { return g_polarizer.load(); }

void set_polarizer_angle(float radians) noexcept {
    g_polarizer.store(0.0F);
    (void)turn_polarizer(radians);
}

RgbaImage render_view_frame(const Session* session, int width, int height, float yaw, float pitch, float zoom,
                            std::uint32_t render_flags, const ViewControls& controls) {
    if (session == nullptr || !finder_on() || session->uv_gallery) {
        return render_session(session, width, height, yaw, pitch, zoom, render_flags, controls);
    }
    ViewControls framed = controls;
    const float fixed = fixed_lens_zoom();
    if (fixed > 0.0F) framed.lens_zoom = finder_zoom(Kind::Photo, zoom, width, height);
    // A frame while the view moves skips the depth of field (it needs a
    // second, software pass for the depth).
    std::vector<float> depth;
    const bool moving = has_render_flag(render_flags, RenderFlag::Preview);
    auto image = render_rolled(session, width, height, yaw, pitch, zoom, render_flags, framed,
                               depth_of_field_on() && !moving ? &depth : nullptr);
    develop(&image, g_seed.fetch_add(1U),
            focal_mm(capture_zoom(Kind::Photo, effective_view_zoom(session, zoom), width, height)), &depth);
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
        const char* shutter = pick(Key::ExposureShutter, kShutters).label;
        const float zoom = capture_zoom(kind, effective_view_zoom(session, view_zoom), width, height);
        const int kelvin = pick(Key::ColourBalance, kKelvin);
        char aperture[16];
        std::snprintf(aperture, sizeof(aperture), "f/%g", static_cast<double>(e.aperture));
        std::string line = std::string(choice(Key::ExposureMode) == 0 ? "P" : "M") + "  ISO " +
                           std::to_string(e.iso) + "  " + shutter +
                           "  " + aperture + "  " + std::to_string(static_cast<int>(std::lround(focal_mm(zoom)))) +
                           " mm  " + (kelvin == 0 ? std::string("AWB") : std::to_string(kelvin) + " K") + "  " +
                           kStyles[std::clamp(choice(Key::ColourStyle), 0, count(kStyles) - 1)];
        if (gyro_active()) line += "  GYRO";
        if (walk_active()) line += "  WALK";
        const float roll_degrees = camera_roll() * 57.2957795F;
        if (std::fabs(roll_degrees) >= 0.5F) {
            char text[24];
            std::snprintf(text, sizeof(text), "  ROLL %+.0f\xC2\xB0", static_cast<double>(roll_degrees));
            line += text;
        }
        if (choice(Key::FilterPolarizer) == 1) {
            char text[24];
            float degrees = std::fmod(polarizer_angle() * 57.2957795F, 180.0F);
            if (degrees < 0.0F) degrees += 180.0F;
            std::snprintf(text, sizeof(text), "  CPL %.0f\xC2\xB0", static_cast<double>(degrees));
            line += text;
        }
        if (depth_of_field_on()) {
            const bool chosen = pick(Key::LensFocus, kFocusUnits) > 0.0F;
            const bool point = !chosen && g_focus_point.load() > 0.0F;
            const float focus = chosen ? pick(Key::LensFocus, kFocusUnits)
                                : point ? (g_focus_point.load() >= 1.0e9F ? kInfinity : g_focus_point.load())
                                        : last_focus();
            char text[32];
            if (std::isfinite(focus)) {
                std::snprintf(text, sizeof(text), "  %s %.1f m", chosen || point ? "MF" : "AF",
                              static_cast<double>(focus * kUnitMm / 1000.0F));
            } else {
                std::snprintf(text, sizeof(text), "  %s \xE2\x88\x9E", chosen || point ? "MF" : "AF");
            }
            line += text;
        }
        const float ev = pick(Key::ExposureCompensation, kCompensation);
        if (ev != 0.0F) {
            char text[32];
            std::snprintf(text, sizeof(text), "  %+g EV", static_cast<double>(ev));
            line += text;
        }
        if (!recording) {
            constexpr const char* formats[] = {"PNG", "JPEG", "WebP", "RAW", "RAW+JPEG"};
            char megapixels[24];
            std::snprintf(megapixels, sizeof(megapixels), " %.1f MP",
                          static_cast<double>(p.width) * static_cast<double>(p.height) / 1.0e6);
            line += "  " + std::string(formats[std::clamp(p.format, 0, 4)]) + " " + std::to_string(p.width) +
                    "\xC3\x97" + std::to_string(p.height) + megapixels;
        }
        status = status.empty() ? line : status + "\n" + line;
    }
    out.status = std::move(status);
    return out;
}

}  // namespace dmcresource::spider::camera
