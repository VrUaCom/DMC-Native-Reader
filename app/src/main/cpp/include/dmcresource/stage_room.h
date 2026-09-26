#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "dmcresource/image_preview.h"
#include "dmcresource/mesh.h"

// Viewer "room": a stage archive (st*.pac, a lone .scm, or any PAC with
// models) the user picked as the backdrop for every model instead of the
// plain floor. Read-only and presentation-only: the room never becomes part
// of the opened Session, it is drawn around it (view_renderer RenderFlag::Room).
namespace dmcresource {
struct Session;
}

namespace dmcresource::stage_room {

struct Room final {
    // Every SCM and MOD of the archive (nested PACs included, depth <= 3) in
    // its source coordinates, merged into one mesh. Textures come from the
    // nearest PTX that attaches (same container first, then the archive).
    Mesh mesh;
    std::vector<std::uint32_t> triangle_texture_slots;
    std::vector<ImagePreview> textures;
    // 1 for triangles whose texture has soft alpha (light shafts, decals):
    // the room pass blends them after the opaque surfaces.
    std::vector<std::uint8_t> translucent_triangles;
    // Where a model can stand: centres of upward-facing floor triangles, the
    // most central large floor first, then other large floors far apart.
    std::vector<Vec3> spots;
    std::size_t pieces{};
    std::size_t textured_pieces{};
    std::string name;
    std::string detail;
};

// Builds a room from the archive bytes; nullptr when nothing in it draws.
[[nodiscard]] std::shared_ptr<const Room> build_room(std::string_view name,
                                                     const std::uint8_t* bytes,
                                                     std::size_t size) noexcept;

// Floor spots of a merged mesh (exposed for tests).
[[nodiscard]] std::vector<Vec3> floor_spots(const Mesh& mesh, std::size_t limit = 8U);
// Floor point nearest to `focus` in xz, not above it.
[[nodiscard]] std::optional<Vec3> floor_spots_near(const Mesh& mesh, const Vec3& focus);

// The room of the viewer (shared by every render; thread-safe).
void set_current(std::shared_ptr<const Room> room) noexcept;
[[nodiscard]] std::shared_ptr<const Room> current() noexcept;
// Spot the model stands on (wraps around the room's spots).
void set_spot(std::size_t index) noexcept;
[[nodiscard]] std::size_t spot() noexcept;
// A point the user placed the model on (double tap on the room floor); it
// wins over the listed spots until the room or the spot changes.
void place_at(const Vec3& point) noexcept;
// Where the model stands now: the placed point, else the current spot.
[[nodiscard]] Vec3 spot_position() noexcept;

// A stage itself (SCM, or a session holding SCM children) never gets a room.
[[nodiscard]] bool is_stage_session(const Session& session) noexcept;

}  // namespace dmcresource::stage_room
