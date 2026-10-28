#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dmcresource/image_preview.h"
#include "dmcresource/view_renderer.h"
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
    std::size_t member{};  // the file's index in StageConfig::members
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

// ---- A stage's configuration in the viewer --------------------------------

// The members found in a stage configuration archive (or one member opened
// on its own) and every marker they place.
struct StageConfig final {
    struct Member final {
        std::string name;  // "st100cfg.pac / slot 3 (POS)"
        stage_config::Kind kind{stage_config::Kind::Pos};
        std::shared_ptr<const std::vector<std::uint8_t>> bytes;
    };
    std::string name;
    std::vector<Member> members;
    std::vector<Marker> markers;
    float extent{1.0F};  // the markers' horizontal span, for drawing sizes
    // Its light table (LIG2, a member of the archive), null when none.
    std::shared_ptr<const game_lights::LightTable> lights;
};

// Reads a stNNNcfg.pac (nested PACs to depth 3) or a single member; nullptr
// when the bytes hold no member.
[[nodiscard]] std::shared_ptr<const StageConfig> load_stage_config(std::string_view name,
                                                                   std::span<const std::uint8_t> bytes);

// The configuration drawn over the viewer's room or stage scene (shared by
// every render; thread-safe), whether it shows, and the held marker.
void set_current(std::shared_ptr<const StageConfig> config) noexcept;
[[nodiscard]] std::shared_ptr<const StageConfig> current() noexcept;
void set_visible(bool visible) noexcept;
[[nodiscard]] bool visible() noexcept;
void set_selected(int marker) noexcept;  // -1 none
[[nodiscard]] int selected() noexcept;

// The stage number of a file name ("st006cfg.pac", "st006.pac": 6), -1 none.
[[nodiscard]] int stage_number(std::string_view name) noexcept;

// The effects the current configuration places in its stage: its STE
// records, each (u16 dispatch kind 0 P, 1 E, 2 G, 3 V; u16 effect id) of the
// stage's effect bank (stNNN_effect.pac) at a position, turned (degrees)
// and scaled. st006cfg's ten records are all V 98 at scale 0.5, the
// bank's one V (fire E 7, glow E 154 above it): the wall torches at y
// 510..580 and the braziers at y 150. Empty when no configuration is loaded
// or it is another stage's (both names carry a stage number that differs).
struct PlacedEffect final {
    char kind{};  // 'P', 'E', 'G', 'V'
    std::uint16_t id{};
    stage_config::Vec3 position;
    stage_config::Vec3 rotation_degrees;
    stage_config::Vec3 scale{1.0F, 1.0F, 1.0F};
};
[[nodiscard]] std::vector<PlacedEffect> placed_effects(std::string_view stage_name);

// The stage's fog from its configuration's SEF: the section of value 2 is
// laid out as a cutscene's GFG fog (st006cfg: f32 1000, f32 2360, then u32
// 0x34808080, bytes R G B A): from the first distance clear to the second
// at the colour and alpha (0x80 = 1.0); st001 writes the distances the
// other way round (2100, 1680), so the nearer one starts it. An inference
// from those files and the cutscene clips; none without a configuration of
// this stage.
[[nodiscard]] std::optional<ViewState::Fog> stage_fog(std::string_view stage_name);

// The stage's light table from its configuration (LIG2, game_lights.h);
// null without a configuration of this stage or when it has none.
[[nodiscard]] std::shared_ptr<const game_lights::LightTable> stage_lights(std::string_view stage_name);

// The 3D drawing of the markers in room coordinates: spawn and placement
// points as posts with a foot cross and a heading arrow, zones as boxes
// from their floor to their height, paths as lines; the selected marker
// is drawn white and wider. One label per marker.
struct Overlay final {
    std::vector<OverlayLine> lines;
    std::vector<OverlayLabel> labels;
};
[[nodiscard]] Overlay build_overlay(const StageConfig& config, int selected_marker);

// Points a finger can hold to pick a marker: a post's foot and top, a
// zone's centre and corners, a path's points.
[[nodiscard]] std::vector<Vec3> pick_anchors(const StageConfig& config, const Marker& marker);

// The marker whose anchor is drawn nearest to image point (x, y), within
// max_px; -1 when none. `anchors` are the projected anchors (nullopt behind
// the camera) and `anchor_marker` the marker each belongs to.
[[nodiscard]] int nearest_marker(std::span<const std::optional<HierarchyScreenPoint>> anchors,
                                 std::span<const std::size_t> anchor_marker, float x, float y, float max_px);

}  // namespace dmcresource::stage_markers
