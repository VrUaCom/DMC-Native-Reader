#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "dmcresource/mesh.h"
#include "dmcresource/resource_session.h"

// The reader's photo and video camera: what a picture or a video of the view
// is and how it looks. The core owns the settings (every choice, its
// starting value), the lens (focal length, aperture, distortion, fringing,
// vignetting), the exposure (ISO, shutter, compensation, highlights, sensor
// noise), the colour (white balance, picture style, contrast, saturation,
// sharpness), the sizes and shapes of pictures and videos, and the
// viewfinder (frame, grid, the settings line). The shell keeps the chosen
// indexes, lays the settings out, saves the files and runs the encoder.
namespace dmcresource::spider::camera {

enum class Key : std::uint8_t {
    PhotoFormat,
    PhotoQuality,
    PhotoSize,
    PhotoMegapixels,
    PhotoAspect,
    PhotoTimer,
    PhotoBurst,
    PhotoSubtitles,
    VideoSize,
    VideoAspect,
    VideoFps,
    VideoBitrate,
    VideoCodec,
    VideoKeyframe,
    VideoLimit,
    VideoMotionBlur,
    VideoSubtitles,
    LensFocal,
    LensVignette,
    LensDistortion,
    LensFringe,
    LensAperture,
    LensDepthOfField,
    LensFocus,
    LensBlades,
    LensBokeh,
    ExposureMode,
    ExposureIso,
    ExposureShutter,
    ExposureCompensation,
    ExposureHighlights,
    ExposureNoise,
    ColourBalance,
    ColourStyle,
    ColourContrast,
    ColourSaturation,
    ColourSharpness,
    FinderMode,
    FinderGrid,
    FinderInfo,
    Count,
};

struct Setting final {
    Key key{};
    std::string id;       // stable name the shell stores the choice under
    std::string section;  // heading it is listed under
    std::string label;
    std::vector<std::string> options;
    int start{};          // the starting value
};

// Every setting in the order it is listed.
[[nodiscard]] const std::vector<Setting>& settings();

// The chosen option of each setting, in settings() order; a missing or
// out-of-range entry keeps its starting value. Shared by every view.
void set_choices(std::span<const int> choices);
[[nodiscard]] int choice(Key key) noexcept;
void reset_choices();

// What a picture (kind Photo) or a video (Video) is, for a view rendered at
// view_width x view_height.
enum class Kind : std::uint8_t { Photo = 0, Video = 1 };
enum class PhotoFormat : std::uint8_t { Png = 0, Jpeg = 1, Webp = 2 };
enum class VideoCodec : std::uint8_t { Avc = 0, Hevc = 1 };

struct Plan final {
    int width{};
    int height{};
    int format{};          // PhotoFormat or VideoCodec
    int quality{};         // picture: 0..100; video: bits per second
    int fps{};
    int keyframe_ms{};
    int limit_seconds{};   // 0: no limit
    int timer_seconds{};
    int burst{};
    bool subtitles{};
};
[[nodiscard]] Plan plan(Kind kind, int view_width, int view_height) noexcept;

// The part of a view_width x view_height view the picture or the video
// covers (the largest rect of its shape), in view pixels.
struct Rect final {
    float x{};
    float y{};
    float width{};
    float height{};
};
[[nodiscard]] Rect frame_in_view(Kind kind, int view_width, int view_height) noexcept;

// The zoom that frames the capture: a fixed focal length, or the view's own
// zoom carried over to the capture's shape (the capture shows exactly the
// frame of the viewfinder).
[[nodiscard]] float capture_zoom(Kind kind, float view_zoom, int view_width, int view_height) noexcept;
// The view's zoom while the viewfinder shows a fixed focal length.
[[nodiscard]] float finder_zoom(Kind kind, float view_zoom, int view_width, int view_height) noexcept;
// Full-frame equivalent focal length (mm) of a zoom for a frame of that shape.
[[nodiscard]] float focal_mm(float capture_zoom) noexcept;

// Exposure the settings give: the gain over the rendered picture and the ISO
// the sensor works at (an automatic ISO is worked out in manual mode).
struct Exposure final {
    float gain{1.0F};
    int iso{100};
    float shutter_seconds{1.0F / 60.0F};
    float aperture{4.0F};
};
[[nodiscard]] Exposure exposure() noexcept;

// Develops a rendered picture with the lens, exposure and colour settings.
// seed varies the sensor noise from frame to frame; mm is the focal length
// the distortion and the depth of field follow (the fixed lens, else 50 mm,
// without it); depth (each pixel's camera distance, scene units of 1 cm)
// gives the depth of field when it is on.
void develop(RgbaImage* image, std::uint32_t seed, float mm, const std::vector<float>* depth = nullptr);
void develop(RgbaImage* image, std::uint32_t seed);
// Whether the depth of field is on (a capture then renders its depth too).
[[nodiscard]] bool depth_of_field_on() noexcept;
// The distance (scene units) the last developed frame was focused at;
// infinity when nothing was in the middle.
[[nodiscard]] float last_focus() noexcept;

// A picture or a video frame of the session: rendered at plan() size with
// capture_zoom(), then developed. A video frame blends the frames before it
// by the shutter when the motion blur is on (reset by begin_video()).
[[nodiscard]] RgbaImage render_capture(const Session* session, Kind kind, int view_width, int view_height,
                                       float yaw, float pitch, float zoom, std::uint32_t render_flags,
                                       const ViewControls& controls);
void begin_video();

// A frame of the view: as before, or through the viewfinder when it is on
// (the fixed lens, the look developed). No UV gallery goes through it.
[[nodiscard]] RgbaImage render_view_frame(const Session* session, int width, int height, float yaw,
                                          float pitch, float zoom, std::uint32_t render_flags,
                                          const ViewControls& controls);
[[nodiscard]] bool finder_on() noexcept;

// What the viewfinder draws over a width x height view: the frame of the
// picture (or of the video while it records), the grid lines in it and the
// settings line. Lines are x0, y0, x1, y1 in view pixels.
struct Finder final {
    bool shown{};
    Rect frame{};
    std::vector<std::array<float, 4>> lines;
    std::string status;
};
[[nodiscard]] Finder viewfinder(const Session* session, int width, int height, float view_zoom, bool recording,
                                int recorded_seconds);

}  // namespace dmcresource::spider::camera
