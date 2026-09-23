#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "dmcresource/mesh.h"

// Read-only port of the DMC3 texture UV scroll (.tsc text, class CDrawUV).
// Reverse authority: dmc-rengine-cpp
// docs/research/dmc3-tsc-uv-scroll-2026-09-23.md.
//
// CDrawUV (vtable 0x1404C9838) loads the text into a model: 0x14030A9B0 walks
// ".TSC" / "# RELATIVE" / "<Start ... End>" / "<Finish>" (the ABSOLUTE part is
// never read) and 0x14030ABE0 fills one 0x80-byte record per ScrlNo. Every
// frame 0x14030C1C0 advances each record by type (jump table 0x14030C2A0) and
// 0x14030BDD0 writes the offset (1/4096 texture, & 0xFFF) into every mesh of
// every MOD object whose source flags +0x10 carry ScrlNo + 1 in bits 24-27
// (and whose texture index equals TexNo when TexNo >= 0).
namespace dmcresource {
struct Session;
}

namespace dmcresource::motion {

struct ScrollRecord final {
    std::uint8_t number{};           // ScrlNo  -> record index
    std::uint8_t type{};             // ScrlType (0..5, 10)
    std::int16_t texture{-1};        // TexNo   (+0x08), -1 = every mesh
    std::int16_t joint{-1};          // JntNo   (+0x07), -1 = model default
    std::array<std::int16_t, 2> direction{};  // DirUV: u left 1 / right -1, v up 1 / down -1
    std::array<float, 2> rate{};              // RateUV     (+0x20 / +0x28)
    std::array<float, 2> time{1.0F, 1.0F};    // TimeUV     (+0x30 / +0x38)
    std::array<float, 2> interval{};          // InterUV    (+0x50 / +0x54)
    std::array<float, 2> turn_time{};         // TurnTimeUV (+0x34 / +0x3C)
    std::array<float, 2> minimum{};           // MinimumUV  (+0x24 / +0x2C), flag 4
    std::array<float, 4> random{};            // RndUV      (+0x68..+0x74), flag 2
    bool has_minimum{};
    bool has_random{};
};

// Byte identity: the first token is ".TSC" (0x14030A9B0 rejects anything else).
[[nodiscard]] bool looks_like_tsc(std::string_view text);

// Records of the "# RELATIVE" section; empty when the text is not a TSC.
[[nodiscard]] std::vector<ScrollRecord> parse_tsc(std::string_view text);

// UV offset (texture units, [0, 1)) after `frames` game frames (dt 1) from
// the start. Types 0-3 are ported; the rest return nullopt. Random jitter
// (RndUV) is not reproduced.
[[nodiscard]] std::optional<std::array<float, 2>> scroll_offset(const ScrollRecord& record,
                                                                float frames) noexcept;

// Scroll number of a MOD object from its source flags (+0x10), or -1.
[[nodiscard]] constexpr int object_scroll_number(std::uint32_t source_flags) noexcept {
    return static_cast<int>((source_flags >> 24U) & 0xFU) - 1;
}

// One scrolled vertex range of the session render mesh.
struct UvScrollBinding final {
    std::size_t vertex_begin{};
    std::vector<Vec2> rest_uv;
    ScrollRecord record;
};

// Write every binding's scrolled UVs into the session render mesh for a clock
// of `frames` game frames. Returns the number of bindings that moved.
std::size_t apply_uv_scrolls(Session* session, float frames) noexcept;

}  // namespace dmcresource::motion
