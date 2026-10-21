#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dmcresource/image_preview.h"
#include "dmcresource/stage_config.h"

// What a stage configuration member places in the world, as markers in stage
// space: spawn points (POS), item placements (ITM), stage elements (STE),
// event zones (EVE), camera zones and paths (CAM). One list feeds the file's
// own top-view map and the overlays drawn over a room, and every marker
// carries the words shown when it is held: what it is, its numbers and where
// in the file it lives.
namespace dmcresource::stage_markers {

enum class Shape : std::uint8_t {
    Point,  // points[0]; heading_degrees when the record has one
    Area,   // points[0..3] the floor corners; height how far it reaches up
    Path,   // points in order
};

struct Rgb final {
    std::uint8_t r{}, g{}, b{};
};

struct Marker final {
    stage_config::Kind source{stage_config::Kind::Pos};
    std::size_t record{};  // the record's index in its file
    Shape shape{Shape::Point};
    std::vector<stage_config::Vec3> points;
    float height{};
    float heading_degrees{std::numeric_limits<float>::quiet_NaN()};
    std::uint64_t offset{};  // the record's byte offset in its file
    std::string label;  // short: "POS 3"
    std::string title;  // "Spawn point 3"
    // What the held marker's card and its Info show, in order.
    std::vector<std::pair<std::string, std::string>> fields;
    Rgb color;

    // The fields as "name: value" lines.
    [[nodiscard]] std::string description() const;
};

// The colour each kind is drawn in, map and room alike.
[[nodiscard]] Rgb kind_color(stage_config::Kind kind) noexcept;
// What a kind is, in words ("spawn points").
[[nodiscard]] const char* kind_meaning(stage_config::Kind kind) noexcept;

// The markers of one member read from its bytes; empty for EST and SEF,
// which place nothing, or when the bytes are not that member.
[[nodiscard]] std::vector<Marker> read_markers(stage_config::Kind kind,
                                               std::span<const std::uint8_t> bytes);

// A top view (X across, Z down) of the markers: grid with its spacing,
// numbered points with their heading, zone outlines, paths, and a legend.
[[nodiscard]] ImagePreview render_plan(std::span<const Marker> markers, std::string_view title,
                                       int width = 1080, int height = 1080);

}  // namespace dmcresource::stage_markers
