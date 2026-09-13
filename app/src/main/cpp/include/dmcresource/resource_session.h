#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "dmcresource/uv_gallery.h"
#include "dmcresource/decode_pipeline.h"
#include "dmcresource/spider/black_widow.h"
#include "dmcresource/view_renderer.h"

namespace dmcresource {

// One canonical source MOD inside a composite scene. The source RenderScene and
// local texture-slot projection are retained intact so future animation / physics
// work can address parts explicitly without reconstructing ownership from the
// flattened render projection. A second per-part flattened Mesh is intentionally
// not retained: PTX validation operates directly on the authoritative scene.
struct CompositePart final {
    std::string name;
    RenderScene scene;
    std::vector<std::uint32_t> render_triangle_texture_slots;
    std::uint32_t texture_slot_base{};
    std::uint32_t texture_slot_span{};
    bool texture_companion_attached{};
    std::string texture_attachment_detail;
};

// Portable product session; platform shells own only handles and byte transport.
struct Session {
    dmcresource::ProbeResult probe;
    dmcresource::ResourceCapabilities capabilities{};
    dmcresource::InspectionDocument inspection;
    dmcresource::RenderScene scene;
    dmcresource::ImagePreview image_preview;
    std::vector<dmcresource::ChildResource> children;

    dmcresource::Mesh render_mesh;
    dmcresource::HierarchyOverlay hierarchy_overlay;
    std::vector<std::uint32_t> render_triangle_texture_slots;

    // Render-bank indexed decoded textures. For a single model this normally
    // matches canonical source slots. Composite Spider actions may remap triangle
    // slots onto one shared or mixed bank while each CompositePart retains the
    // authoritative source-local slot identity separately.
    std::vector<dmcresource::ImagePreview> attached_textures;
    std::string texture_attachment_detail;
    bool texture_companion_attached{};

    // Non-empty only for an explicitly composed multi-MOD scene. Parts retain
    // their source-local scene/node/texture namespaces. The top-level Session
    // owns one derived flattened render cache; it is not a second format authority.
    std::vector<CompositePart> composite_parts;

    std::string detail;
    std::string trace;
    bool renderable{};

    std::shared_ptr<const UvGallery> uv_gallery;
    std::optional<std::size_t> uv_map_index;
};

[[nodiscard]] std::unique_ptr<Session> open_session(std::string_view name,
    const std::uint8_t* bytes, std::size_t size);
[[nodiscard]] std::unique_ptr<Session> session_from_child(const ChildResource& child);
[[nodiscard]] std::unique_ptr<Session> open_uv_gallery(const Session* model);

// Low-level composition primitive used by the Spider session-action layer.
// Platform/JNI callers must use spider::actions::compose_mod_sessions instead.
[[nodiscard]] std::unique_ptr<Session> compose_mod_sessions(
    const std::vector<const Session*>& parts,
    const std::vector<std::string>& names);

[[nodiscard]] std::size_t session_composite_part_count(const Session* session) noexcept;
[[nodiscard]] std::string session_composite_part_name(const Session* session, int index);
[[nodiscard]] std::size_t session_child_count(const Session* session) noexcept;
[[nodiscard]] std::string session_child_title(const Session* session, int index);
[[nodiscard]] std::pair<std::uint32_t, std::uint32_t> session_child_preview_size(
    const Session* session, int index) noexcept;
// PTX previews are borrowed. Generated previews use caller-owned scratch only.
[[nodiscard]] const ImagePreview* session_child_preview(
    const Session* session, int index, ImagePreview* scratch);
[[nodiscard]] std::unique_ptr<Session> open_session_child(const Session* session, int index);
[[nodiscard]] std::string describe_session(const Session* session);
[[nodiscard]] spider::black_widow::StateBits black_widow_state(const Session* session) noexcept;

// Texture attachment is intentionally absent from this generic session API.
// Product attachment actions are owned by dmcresource::spider::actions so JNI,
// desktop shells and future platforms cannot bypass the Spider execution layer.
[[nodiscard]] RgbaImage render_session(const Session* session, int requested_width,
    int requested_height, float yaw, float pitch, float zoom, std::uint32_t render_flags);

}  // namespace dmcresource
