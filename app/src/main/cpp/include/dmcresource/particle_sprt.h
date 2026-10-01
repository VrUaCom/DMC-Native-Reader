#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "dmcresource/mesh.h"
#include "dmcresource/render_scene.h"

// P records of class 3 ("Sprt00"): the CPtclSprt00 emitter (vtable
// 0x1404E2C68, 0xD00 bytes) and its CFPtclSprt00 layers (vtable 0x1404E2CE8,
// 0x520 bytes). Reverse authority: dmc3.exe factory 0x140236AA0, init
// 0x140236D10 / 0x140236BF0, update 0x1402374F0 / 0x140237400, key track
// 0x1402D30E0, transform integrator 0x140312260, draw composer 0x140312F10;
// every constant below was checked against an emulated run of those
// functions (see docs/research/dmc3-particle-sprt00-exe-v75.md).
//
// The record is a burst of N (12) camera-facing quads: positions come from a
// hollow-box random draw (0x140312450), velocities decay under per-axis
// friction (0x140314080), colours follow a 3-segment key track. Every layer
// re-draws the same quads with its own local transform and colour track.
namespace dmcresource::particle {

// One key of a colour track: s16 segment length (ticks) and the RGBA of the
// first vertex group (the track copies it to all four corners).
struct TrackKey final {
    std::int16_t duration{};
    std::array<std::uint8_t, 4> color{};
};
struct Track final {
    std::array<TrackKey, 4> keys{};
    bool enabled{};
    bool loop{};
};

// Local transform integrator inputs (0x140312260): s16 per tick.
struct TransformMotion final {
    std::array<std::int16_t, 3> translation{};  // x 1/16 per tick
    std::array<std::int16_t, 3> rotation{};     // x 2*pi/65536 per tick
    std::array<std::int16_t, 3> scale{};        // x 1/4096 per tick
};

struct LayerDef final {
    std::array<float, 3> translation{};
    std::array<float, 3> rotation{};  // radians, Rz*Ry*Rx
    std::array<float, 3> scale{1.0F, 1.0F, 1.0F};
    TransformMotion motion{};
    Track track{};
};

struct Def final {
    std::string name;
    std::uint16_t animation{0xFFFFU};  // A record id (def +0x106)
    bool random_frame{};               // def +0x128 == 1: one random A frame, no playback
    bool world_gravity{};              // def +0x110 == 1
    std::int32_t life{};               // ticks (def +0x68)
    std::array<float, 3> translation{};
    std::array<float, 3> rotation{};
    std::array<float, 3> scale{1.0F, 1.0F, 1.0F};
    TransformMotion motion{};
    Track track{};
    std::array<float, 3> friction{};        // per tick, def +0xF4
    std::array<float, 3> friction_decay{};  // per tick, def +0x11C
    float gravity{};                        // def +0x10C
    std::array<float, 3> spread{};          // s16 +0x100 / 16
    std::array<float, 3> hollow{};          // s16 +0x116 / 32 (inner half extent)
    std::array<float, 3> push{};            // s16 +0xE2 / 16
    std::array<float, 3> bias{};            // s16 +0xEE / 16
    float half_width{};                     // u16 +0x108 / 16
    float half_height{};                    // u16 +0x10A / 16
    std::uint32_t count{12U};               // object +0xF4 set by 0x140312B20(2, 12)
    std::vector<LayerDef> layers;
};

// Parses a class-3 record (u32 version 2, definition at +0x20).
[[nodiscard]] std::optional<Def> parse_sprt00(std::span<const std::uint8_t> record);

// Sprite animation timing the particle needs from its A record.
struct Animation final {
    std::uint8_t frame_time{};
    std::uint8_t last_frame{};
    bool loop{};
    std::uint8_t loop_frame{};
    std::uint32_t frame_count{1U};
};

struct Camera final {
    Vec3 right{1.0F, 0.0F, 0.0F};
    Vec3 up{0.0F, 1.0F, 0.0F};
    Vec3 forward{0.0F, 0.0F, 1.0F};
};

// Quad corners in the order bottom-left, bottom-right, top-right, top-left of
// the *image* (atlas) rectangle, so uv = (u0,v1),(u1,v1),(u1,v0),(u0,v0).
struct Quad final {
    std::array<Vec3, 4> corners{};
    std::array<std::uint8_t, 4> rgba{};
    std::uint32_t layer{};  // 0 = the emitter itself
};

class Simulation final {
public:
    Simulation(const Def& def, const Animation& animation, std::uint32_t seed);

    // One 60 Hz tick (dt 1). Returns false once the effect expired.
    bool update(const Matrix4& world);
    [[nodiscard]] bool expired() const noexcept { return expired_; }
    [[nodiscard]] std::uint32_t frame() const noexcept { return frame_; }

    // Camera-facing quads of every layer for the current state.
    void quads(const Matrix4& world, const Camera& camera, std::vector<Quad>* out) const;

    // Test hooks: the emitter state the EXE keeps at object +0xB80 / +0x120.
    struct State final {
        std::vector<std::array<float, 3>> velocity;
        std::vector<std::array<float, 3>> position;
        std::array<float, 3> translation{}, rotation{}, scale{};
        std::array<std::uint8_t, 4> color{};
        float life{};
    };
    [[nodiscard]] const State& state() const noexcept { return emitter_; }
    void set_particles(std::vector<std::array<float, 3>> position,
                       std::vector<std::array<float, 3>> velocity);
    [[nodiscard]] const std::vector<State>& layer_states() const noexcept { return layers_; }

private:
    struct TrackState final {
        std::uint8_t index{};
        bool done{};
        float remaining{};
    };
    void step_track(const Track& track, TrackState* state, std::array<std::uint8_t, 4>* color) const;

    Def def_;
    Animation animation_;
    State emitter_;
    TrackState emitter_track_;
    std::vector<State> layers_;
    std::vector<TrackState> layer_tracks_;
    std::array<float, 3> friction_{};
    std::uint32_t frame_{};
    float anim_timer_{};
    bool expired_{};
    std::uint32_t rng_{};
};

}  // namespace dmcresource::particle
