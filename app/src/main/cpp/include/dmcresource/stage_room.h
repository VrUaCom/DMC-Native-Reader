#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dmcresource/image_preview.h"
#include "dmcresource/mesh.h"
#include "dmcresource/scene_projection.h"
#include "dmcresource/environment_collision.h"

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
    // One record per merged model: its triangle range in `mesh` and bounds.
    struct Piece final {
        std::string name;
        std::size_t first_triangle{};
        std::size_t triangle_count{};
        Vec3 bounds_min{};
        Vec3 bounds_max{};
        bool layout_object{};  // placed by the "# GAME" layout
    };
    std::vector<Piece> piece_info;
    // Joint hierarchy of every merged model, in room coordinates (layout
    // objects moved with their placement): the bones view of a stage scene.
    HierarchyOverlay hierarchy;
    // Texture scrolls of the "# GAME" layout (`uv part, texture, U, V`): room
    // texture index and rate per game frame (texture units).
    struct UvScroll final {
        std::uint32_t texture{};
        float u_per_frame{};
        float v_per_frame{};
    };
    std::vector<UvScroll> uv_scrolls;
    // Effects the layout keeps on its objects (`eff V 98` + `epos`, e.g. the
    // burning drums): kind, id and the room position of the effect root.
    struct LayoutEffect final {
        char kind{};
        std::uint16_t id{};
        Vec3 position{};
        // A broken object's `beff`: played once from the moment the room was
        // switched to its broken state, not looped.
        bool once{};
    };
    std::vector<LayoutEffect> effects;
    // "# SET n BREAK" objects of the layout, and the same room with all of
    // them broken (bmodel shown, beff played; null when there are none).
    std::size_t breakable_objects{};
    std::shared_ptr<const Room> broken;
    // Environment collision sources are preserved independently from the
    // visible room mesh. HITS source ordering and physical slots remain part
    // of provenance; only the first source is selected for the optional
    // inspection overlay.
    std::vector<environment_collision::Source> collision_sources;
    std::vector<Vec3> collision_lines;
    // Kinds (distinct flag values) of the overlay source and the kind of each
    // line pair of collision_lines.
    std::vector<environment_collision::Kind> collision_kinds;
    std::vector<std::uint8_t> collision_line_kinds;
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

// Break toggle of the viewer: draw every room / stage scene in its broken
// state (Room::broken) when it has one. `broken_frames` counts game frames
// (60 per second) since the toggle was switched on, the clock of `once`
// effects; negative while off.
void set_broken(bool broken) noexcept;
[[nodiscard]] bool broken() noexcept;
[[nodiscard]] float broken_frames() noexcept;
// The state of `room` to draw now.
[[nodiscard]] const Room& shown(const Room& room) noexcept;
[[nodiscard]] std::shared_ptr<const Room> shown(std::shared_ptr<const Room> room) noexcept;

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

// Room -> model space of the session drawn in it (the room pass of
// view_renderer): turned by `yaw` about `pivot` (the floor spot), then moved
// by `offset`, so the spot lands under the model's rest centre and feet.
struct Placement final {
    Vec3 pivot{};
    float yaw{};
    Vec3 offset{};
};
// The placement for a model whose rest vertices are `rest`, standing on the
// current spot, with the room turned by `yaw` radians.
[[nodiscard]] Placement placement_for(std::span<const Vec3> rest, float yaw) noexcept;
[[nodiscard]] Vec3 room_to_model(const Placement& placement, const Vec3& point) noexcept;
[[nodiscard]] Vec3 model_to_room(const Placement& placement, const Vec3& point) noexcept;
[[nodiscard]] Vec3 room_direction_to_model(const Placement& placement, const Vec3& direction) noexcept;

// Live stage collision. While a room with HITS is drawn around a session, its
// first HITS source (the detailed source 0) is the collision world of that
// session's runtime: characters are held out of walls and follow floors,
// shells hit walls and floors. The owner is the Session address.
struct ActiveCollision final {
    std::shared_ptr<const Room> room;
    Placement placement;
    // Changes whenever the room or its placement changes.
    std::uint64_t revision{};

    [[nodiscard]] const environment_collision::Source* source() const noexcept {
        return room && !room->collision_sources.empty() ? &room->collision_sources.front() : nullptr;
    }
};
void set_active_collision(const void* owner, std::shared_ptr<const Room> room,
                          const Placement& placement) noexcept;
void clear_active_collision(const void* owner) noexcept;
[[nodiscard]] std::optional<ActiveCollision> active_collision(const void* owner) noexcept;

// 0x14005E880 on the active HITS, in the owner's model space: the nearest hit
// of the segment, its point and normal in model space.
[[nodiscard]] std::optional<environment_collision::SegmentHit> segment_hit_model(
    const ActiveCollision& collision, const Vec3& from, const Vec3& to,
    std::uint16_t skip_mask = 0U) noexcept;

// A stage archive (st*.pac) opened as its assembled scene: the merged room
// (every SCM/MOD, the PNST objects at their layout, the stage textures) as a
// renderable session, its HITS kept in Session::stage for the collision view.
// nullptr when the archive holds no SCM.
[[nodiscard]] std::unique_ptr<Session> open_stage(std::string_view name, const std::uint8_t* bytes,
                                                  std::size_t size) noexcept;

// The stage's effect bank (st*_effect.pac, an FXBANK) the layout effects play
// from. It is held as a host Session that only carries the bank.
[[nodiscard]] std::shared_ptr<Session> make_effect_host(std::string_view name, const std::uint8_t* bytes,
                                                        std::size_t size) noexcept;
void set_effect_host(std::shared_ptr<const Session> host) noexcept;
[[nodiscard]] std::shared_ptr<const Session> effect_host() noexcept;

// A stage itself (SCM, or a session holding SCM children) never gets a room.
[[nodiscard]] bool is_stage_session(const Session& session) noexcept;

}  // namespace dmcresource::stage_room
