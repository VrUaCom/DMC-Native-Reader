#pragma once

#include <array>
#include <cstdint>
#include <limits>
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
    // The one camera both pictures and videos are taken with.
    FinderMode,
    CameraShape,
    FinderGrid,
    FinderInfo,
    PhotoFormat,
    PhotoQuality,
    PhotoRawData,
    PhotoSize,
    PhotoMegapixels,
    PhotoTimer,
    PhotoBurst,
    PhotoSubtitles,
    VideoSize,
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
    FilterPolarizer,
    FilterPolarizerStrength,
    ExposureMode,
    ExposureIso,
    ExposureShutter,
    ExposureCompensation,
    ExposureHighlights,
    ExposureNoise,
    ExposureSupport,
    ColourBalance,
    ColourStyle,
    ColourContrast,
    ColourSaturation,
    ColourSharpness,
    GyroMode,
    GyroHorizon,
    GyroSensitivity,
    GyroSmoothing,
    WalkMode,
    WalkScale,
    WalkHeight,
    Count,
};

struct Setting final {
    Key key{};
    std::string id;       // stable name the shell stores the choice under
    std::string section;  // heading it is listed under
    std::string label;
    std::string hint;     // what the options mean (they are short)
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
enum class PhotoFormat : std::uint8_t { Png = 0, Jpeg = 1, Webp = 2, Dng = 3, DngJpeg = 4 };
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

// What else changes when a setting is turned by hand to `option`, and the
// line that says so. In Auto the camera keeps the brightness itself, so the
// ISO, the shutter and the aperture would change little (the ISO's noise,
// the aperture's depth of field): turning one of them goes to Manual, where
// they set the brightness as on a camera's M dial. Manual starts from the
// same picture (ISO 100 at 1/60 and f/4 is the rendered one), so an Auto ISO
// becomes 100 unless the ISO itself is turned.
struct FollowUp final {
    struct Change final {
        Key key{};
        int option{};
    };
    std::vector<Change> changes;
    std::string message;
};
[[nodiscard]] FollowUp follow_up(Key key, int option);

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

// What moves while a picture's shutter is open, besides the camera's shake
// (the Holding setting, and the phone's own turns with the gyroscope): the
// scene's motion, as the frame it shows when the shutter closes and how
// many frames a second it plays (NaN or 0: still).
struct SceneMotion final {
    float frame{std::numeric_limits<float>::quiet_NaN()};
    float frames_per_second{};
};
// A picture exposed over the shutter: while the scene or the camera moves,
// it is the mean (in linear light) of renders along the shutter's time, so
// a slow shutter blurs what moves and a hand-held camera shakes. The
// session is posed along the way and left at motion.frame.
[[nodiscard]] RgbaImage render_capture(Session* session, Kind kind, int view_width, int view_height, float yaw,
                                       float pitch, float zoom, std::uint32_t render_flags,
                                       const ViewControls& controls, const SceneMotion& motion);
// The shutter time a picture is exposed for (Auto: the camera's own, 1/60
// or 1/focal length, whichever is shorter) and whether it is slower than a
// hand can hold at that focal length (the viewfinder's SHAKE).
[[nodiscard]] float picture_shutter(float mm) noexcept;
[[nodiscard]] bool shake_warning(float mm) noexcept;
void begin_video();

// A picture of width x height in tiles of at most max_side when it is larger
// than one render (the whole picture's camera in every tile), with its depth.
[[nodiscard]] RgbaImage render_large(const Session* session, int width, int height, float yaw, float pitch,
                                     float zoom, std::uint32_t render_flags, const ViewControls& controls,
                                     std::vector<float>* depth, int max_side = 8192);

// A RAW picture into the file `fd` as DNG (Adobe's open RAW): the lens's
// work (depth of field, distortion, fringing, vignetting), the exposure and
// the sensor noise are in it. The white balance is the neutral, and a
// developed preview is in the first IFD. The colour, curves, style and
// sharpening are left to the editor. Linear RGB, or a Bayer RGGB mosaic
// (PhotoRawData). `developed`, when given, gets the developed picture (RAW + JPEG).
[[nodiscard]] bool capture_raw(const Session* session, int view_width, int view_height, float yaw, float pitch,
                               float zoom, std::uint32_t render_flags, const ViewControls& controls, int fd,
                               RgbaImage* developed);
[[nodiscard]] bool capture_raw(Session* session, int view_width, int view_height, float yaw, float pitch,
                               float zoom, std::uint32_t render_flags, const ViewControls& controls, int fd,
                               RgbaImage* developed, const SceneMotion& motion);
[[nodiscard]] bool write_dng(int fd, const RgbaImage& optical, const RgbaImage& preview, bool bayer,
                             std::uint32_t seed, float focal_length_mm);

// A frame of the view: as before, or through the viewfinder when it is on
// (the fixed lens, the look developed). No UV gallery goes through it.
[[nodiscard]] RgbaImage render_view_frame(const Session* session, int width, int height, float yaw,
                                          float pitch, float zoom, std::uint32_t render_flags,
                                          const ViewControls& controls);
[[nodiscard]] bool finder_on() noexcept;

// Camera mode keeps a cutscene's cuts: while the cutscene's camera drives the
// view, the mode films through it (no free camera of its own, the phone's
// turns wait); a finger still takes the camera, and the cutscene camera
// companion gives it back.
[[nodiscard]] bool follows_cuts(const Session* session) noexcept;

// ---- Gyroscope, roll and focus by touch (camera mode) ----------------------

// The phone's turns become the camera's while camera mode and the
// gyroscope are on. The shell reads the phone's heading (radians, turning
// right grows it), elevation (up grows it) and roll (clockwise grows it) and
// gets back how far to turn the camera's yaw and pitch: changes, not
// angles, so a finger, the drone's sticks and the phone turn it together.
// Smoothing and sensitivity are the settings'. The phone's roll tilts the
// picture unless the horizon is locked.
[[nodiscard]] bool gyro_active() noexcept;
// The screen keeps its orientation while the phone itself aims the camera
// (camera mode with the gyroscope or walking on): turning the phone must
// not turn the app.
[[nodiscard]] bool holds_screen_orientation() noexcept;

// Walking (ARCore, 6DoF): in camera mode with it on, the phone's own steps
// move the camera. The shell reads how far the phone moved (metres:
// forward and right along the ground as it faces, up) and the core moves
// the drone's eye by that, times the walk scale (1 m of the room is so
// many metres of the scene); the height follows unless it is fixed.
[[nodiscard]] bool walk_active() noexcept;
bool walk(float yaw_radians, float forward_m, float right_m, float up_m) noexcept;
[[nodiscard]] std::array<float, 2> gyro_turn(float heading, float elevation, float roll) noexcept;
void gyro_reset() noexcept;

// The camera's roll about its axis (radians, clockwise grows it): the
// three-finger twist's plus the phone's when the horizon follows it.
[[nodiscard]] float camera_roll() noexcept;
float turn_roll(float radians) noexcept;
// Level: no roll from the fingers.
void level_camera() noexcept;

// Focus by touch: the distance (scene units) under view pixel (x, y) of
// that view through the viewfinder (the depth of a small render), kept as
// the focus point; a lock keeps what the auto focus found; the focus ring
// moves the point nearer or farther (log steps). A focus chosen in the
// settings wins over the point; clear_focus_point() goes back to auto.
[[nodiscard]] float focus_at(const Session* session, int width, int height, float yaw, float pitch, float zoom,
                             std::uint32_t render_flags, const ViewControls& controls, float x, float y);
[[nodiscard]] bool toggle_focus_lock() noexcept;
float turn_focus_ring(float steps) noexcept;
[[nodiscard]] float focus_point() noexcept;
void clear_focus_point() noexcept;
[[nodiscard]] float units_to_metres(float units) noexcept;
// What a focus gesture says: "Focus 3.2 m", "Focus: infinity", "Auto focus",
// and how to see it when the depth of field is off.
[[nodiscard]] std::string focus_message(float units);

// The dials of the camera mode's bar, in order: the setting each one turns
// (and the ones that go with it, shown together when it is opened), its
// short name and its value as a camera shows it ("ISO 400", "1/60").
struct Dial final {
    Key key{};
    std::string title;
    std::string value;
    std::vector<Key> with;
};
[[nodiscard]] std::vector<Dial> dials();

// The circular polarizer in front of the lens: in camera mode two fingers
// turn it (radians, like the ring of a real one). It darkens the polarized
// light (reflections, glare, the blue of the sky) by sin² of the ring
// against the light's polarization, which turns slowly across the frame.
[[nodiscard]] bool polarizer_turnable() noexcept;
float turn_polarizer(float radians) noexcept;
[[nodiscard]] float polarizer_angle() noexcept;
void set_polarizer_angle(float radians) noexcept;

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
