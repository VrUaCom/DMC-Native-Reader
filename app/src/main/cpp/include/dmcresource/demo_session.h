#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dmcresource/demo.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/sound.h"

// A cutscene opened as its scene: the stage pieces (SCM) drawn by the room
// pass in their own coordinates, moving and hiding with their clips, the track models (MOD / EFM with their PTX)
// as one composite, and the cutscene camera over the timeline. Playback goes
// through the motion API (motion_player.h): a bound cutscene is the session's
// motion, its frames the timeline's, so the viewer's play / pause / scrub
// drive it like any motion.
namespace dmcresource::demo {

// One object of a track's model (a MOD / SCM object, its meshes in order):
// its vertices and triangles in the track's own mesh, and the scene node it
// hangs from (SCM; -1 none).
struct ObjectRange final {
    std::uint32_t object{};
    std::int32_t node{-1};
    std::size_t first_vertex{};
    std::size_t vertex_count{};
    std::size_t first_triangle{};
    std::size_t triangle_count{};
};

// A stage piece's own motion (its scene nodes driven by a MOT); defined in
// demo_session.cpp.
struct StageMotion;

struct PlayerTrack final {
    int track{};
    std::string model;
    std::string texture;
    // A stage piece lives in the room mesh, a model in the composite.
    bool stage{};
    // The model on its own; the cutscene poses it and copies its vertices
    // into its mesh at [vertex_begin, vertex_begin + vertex_count).
    std::unique_ptr<Session> source;
    std::size_t vertex_begin{};
    std::size_t vertex_count{};
    std::size_t triangle_begin{};
    std::size_t triangle_count{};
    std::vector<ObjectRange> objects;
    // Which objects are drawn now (by object index; the Hide clips decide).
    std::vector<std::uint8_t> shown;
    // CnsPos: the model hangs from joint `host_joint` of track `host_track`
    // (its own motion is relative to that joint); -1 when free.
    int host_track{-1};
    int host_joint{-1};
    // The model is authored at the host's rest pose: it follows the joint's
    // move from its rest world (vertex x inverse rest world x current world).
    Matrix4 host_rest_inverse{};
    std::shared_ptr<StageMotion> stage_motion;
    // The MOT bound now, and one that failed to bind (not retried).
    std::string motion;
    std::string failed_motion;
    std::string failure;
};

struct Player final {
    Document doc;
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
    std::vector<PlayerTrack> tracks;
    // The stage the room pass draws (the scene's Session::stage), owned here
    // so its pieces can move and hide.
    std::shared_ptr<stage_room::Room> room;
    // Every triangle of the composite and of the room with its texture slot
    // (and room translucency); the meshes draw the shown objects' share.
    std::vector<std::uint32_t> all_indices;
    std::vector<std::uint32_t> all_triangle_slots;
    std::vector<std::uint32_t> room_indices;
    std::vector<std::uint32_t> room_triangle_slots;
    std::vector<std::uint8_t> room_translucent;
    // The cutscene's effect bank, held by a host session the effect pass
    // reads its records and textures from (null when there is none).
    std::shared_ptr<Session> effect_host;
    // Sound the user gave the cutscene (its streams are .ogg files outside
    // the archive): each starts at a timeline frame.
    struct Audio final {
        std::string name;
        float start_frame{};
        sound::Pcm pcm;
    };
    std::vector<Audio> audio;
    float frame{};
    bool bound{};
    // The view follows the cutscene camera until the user takes it.
    bool director{true};
    std::optional<CameraShot> shot;
    // How far ahead the shot looked when the user took the camera: the free
    // camera's pan and dolly move about content at that distance.
    float free_focus{};
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
// The taken camera's focus distance (eye to the shot's target); 0 while the
// cutscene camera drives the view.
[[nodiscard]] float free_camera_focus(const Session& session) noexcept;

// ---- effects ---------------------------------------------------------------

// An effect of an Effect / EffectI clip playing at the current frame: its
// kind and id in the cutscene's effect bank, its world (Pos, Rot, Sca, on a
// track's joint when SpFlag says so) and its age in its own frames (the
// clip's time scale and WaitTimer applied). `cut`: the clip stops it at
// CutFrame, so it plays even past the effect's own length.
struct EffectShot final {
    char kind{};
    std::uint16_t id{};
    Matrix4 world{};
    float age{};
    bool cut{};
};
[[nodiscard]] std::vector<EffectShot> active_effects(const Session& session);
[[nodiscard]] std::shared_ptr<const Session> effect_host(const Session& session) noexcept;

// ---- sound ---------------------------------------------------------------

// Gives the cutscene a sound file (Ogg Vorbis). The n-th file plays from the
// n-th Sound clip's frame (its ADX stream), or from frame 0 when the
// cutscene has no Sound clip; once every clip has one the files start over.
// The line to show comes back in `message`.
[[nodiscard]] bool attach_audio(Session* session, std::string_view name, std::span<const std::uint8_t> bytes,
                                std::string* message);
struct AudioFormat final {
    std::uint32_t sample_rate{};
    std::uint32_t channels{};
};
// The mix the shell streams (the first file's rate, stereo or mono).
[[nodiscard]] std::optional<AudioFormat> audio_format(const Session* session) noexcept;
// The mix's frame (sample frame) at a timeline frame.
[[nodiscard]] std::int64_t audio_position(const Session* session, float frame) noexcept;
// `frames` sample frames of the mix from `position`, interleaved, silence
// where no file plays; empty past the end of every file and the cutscene.
[[nodiscard]] std::vector<std::int16_t> audio_read(const Session* session, std::int64_t position, std::size_t frames);

// "Shot cm005.cam · frame 512 / 12455 · fov 41.5" for the HUD and Info.
[[nodiscard]] std::string status_line(const Session* session);

}  // namespace dmcresource::demo
