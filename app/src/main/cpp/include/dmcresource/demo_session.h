#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "dmcresource/demo.h"
#include "dmcresource/resource_session.h"

// A cutscene opened as its scene: the stage pieces (SCM) drawn by the room
// pass in their own coordinates, the track models (MOD / EFM with their PTX)
// as one composite, and the cutscene camera over the timeline. Playback goes
// through the motion API (motion_player.h): a bound cutscene is the session's
// motion, its frames the timeline's, so the viewer's play / pause / scrub
// drive it like any motion.
namespace dmcresource::demo {

struct PlayerTrack final {
    int track{};
    std::string model;
    std::string texture;
    // The model on its own; the cutscene poses it and copies its vertices
    // into the composite at [vertex_begin, vertex_begin + vertex_count).
    std::unique_ptr<Session> source;
    std::size_t vertex_begin{};
    std::size_t vertex_count{};
    // Its triangles in the composite, and whether they are drawn now.
    std::size_t triangle_begin{};
    std::size_t triangle_count{};
    bool visible{true};
    // CnsPos: the model hangs from joint `host_joint` of track `host_track`
    // (its own motion is relative to that joint); -1 when free.
    int host_track{-1};
    int host_joint{-1};
    // The model is authored at the host's rest pose: it follows the joint's
    // move from its rest world (vertex x inverse rest world x current world).
    Matrix4 host_rest_inverse{};
    // The MOT bound now, and one that failed to bind (not retried).
    std::string motion;
    std::string failed_motion;
    std::string failure;
};

struct Player final {
    Document doc;
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
    std::vector<PlayerTrack> tracks;
    std::vector<std::string> stage_pieces;
    // Every track's triangles and their texture slots; the scene draws the
    // visible tracks' share of them.
    std::vector<std::uint32_t> all_indices;
    std::vector<std::uint32_t> all_triangle_slots;
    float frame{};
    bool bound{};
    // The view follows the cutscene camera until the user takes it.
    bool director{true};
    std::optional<CameraShot> shot;
};

// Builds the scene of a cutscene archive from its opened PAC session (whose
// Info and files it keeps); nullptr when nothing in it draws.
[[nodiscard]] std::unique_ptr<Session> open_demo(Session&& archive, std::string_view name,
                                                 const std::uint8_t* bytes, std::size_t size) noexcept;

[[nodiscard]] bool is_demo(const Session* session) noexcept;

// The viewer's camera for a cutscene camera pose: standing at the eye,
// looking at the target, the field of view as the viewer's zoom (its focal
// length spans the shorter image side).
struct DirectorView final {
    float yaw{};
    float pitch{};
    float zoom{1.0F};
    Vec3 eye{};
    float fov_degrees{45.0F};
};
[[nodiscard]] DirectorView view_of(const CameraPose& pose) noexcept;
// The cutscene camera of the current frame while it drives the view.
[[nodiscard]] std::optional<DirectorView> director_view(const Session& session) noexcept;

// Playback (through the motion API).
[[nodiscard]] bool bind(Session* session) noexcept;
void unbind(Session* session) noexcept;
[[nodiscard]] bool bound(const Session* session) noexcept;
[[nodiscard]] float end_frame(const Session* session) noexcept;
[[nodiscard]] bool seek(Session* session, float frame) noexcept;

// The cutscene camera drives the view (on) or the user does (off).
[[nodiscard]] bool set_director(Session* session, bool on) noexcept;
[[nodiscard]] bool director(const Session* session) noexcept;
// The user takes the camera: the cutscene camera stops driving the view and
// the view it stood at is handed back to continue from.
[[nodiscard]] std::optional<DirectorView> take_camera(Session* session) noexcept;

// "Shot cm005.cam · frame 512 / 12455 · fov 41.5" for the HUD and Info.
[[nodiscard]] std::string status_line(const Session* session);

}  // namespace dmcresource::demo
