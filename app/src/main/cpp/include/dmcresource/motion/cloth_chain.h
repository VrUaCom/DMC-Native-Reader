#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

// Read-only port of the DMC3 chain/cloth solver. Reverse authority:
// dmc-rengine-cpp docs/research/dmc3-cloth-chain-solver-2026-09-23.md.
//
// A .clt text (";pl000_02.clt", ";em028_01.clt", ...) lists one or more cloth
// blocks; parser 0x1402CA345 / 0x1402CA42A fills a CCnsChain (defaults
// 0x1402CA000) and gives every listed bone an axis (X Y Z NX NY NZ -> 0-5,
// joint +0x240). Each simulated joint is a node constraint whose apply
// 0x1402C9450 runs once per game frame (dt = [c+0xE0], 1 at 60 fps):
//   S.t += dt*v; point S's axis at the parent (0x14032EEE0 / 0x14032F4C0 /
//   0x14032FD90); W = blend(rest target, S) by Stiffness (0x14032DA20);
//   v += dt*(wind + Gravity); keep the rest bone length (LimitLength) and pull
//   v back by SpringForce; clamp |v| <= MaxSpeed; v *= 0.99; floor clamp.
namespace dmcresource::motion {

struct ClothBone final {
    std::uint32_t node{};
    std::uint8_t axis{};  // 0 X, 1 Y, 2 Z, 3 NX, 4 NY, 5 NZ
};

struct ClothParams final {
    std::array<float, 3> gravity{0.0F, -0.2F, 0.0F};  // +0xB0
    float spring_force{0.05F};                          // +0x78
    float max_speed{50.0F};                             // +0x74
    float stiffness{0.3F};                              // +0x6C
    std::array<float, 3> wind{0.0F, 0.0F, 0.0F};        // +0xA0
    bool wind_local{false};                             // +0x80
    std::int32_t wind_parent{0};                        // +0x7C
    std::int32_t wind_type{0};                          // +0x8C
    float floor_level{-1000000.0F};                     // +0x84
    bool limit_length{true};                            // +0xE4
    float damping{0.99F};                               // +0x88
    std::vector<ClothBone> bones;
};

// Every block of a .clt text; empty when the text is not a cloth file.
[[nodiscard]] std::vector<ClothParams> parse_clt(std::string_view text);

// Per-part solver state (simulated world and velocity of each listed node).
struct ClothState final {
    ClothParams params;
    std::vector<std::array<float, 16>> sim;
    std::vector<std::array<float, 3>> velocity;
    std::vector<std::int8_t> axis_by_node;  // -1: not simulated
    bool initialized{false};
};

// One solver step for `node`: `target` = rest local x parent world, `parent`
// = parent world, `wind_parent_world` = world of the wind parent joint.
// Returns the new node world (row-vector, translation in [12..14]).
[[nodiscard]] std::array<float, 16> step_cloth_node(ClothState& state,
                                                    std::uint32_t node,
                                                    const std::array<float, 16>& target,
                                                    const std::array<float, 16>& parent,
                                                    const std::array<float, 16>& wind_parent_world,
                                                    float rest_length,
                                                    float dt);

}  // namespace dmcresource::motion
