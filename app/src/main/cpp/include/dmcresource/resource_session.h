#pragma once

#include <memory>
#include <string_view>
#include "dmcresource/decode_pipeline.h"
#include "dmcresource/spider/black_widow.h"
#include "dmcresource/view_renderer.h"

namespace dmcresource {

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

    // Vector index == canonical texture slot. Empty entries represent slots not
    // required by the current model. PTX parsing/decoding stays in native
    // reusable modules rather than Java or the renderer.
    std::vector<dmcresource::ImagePreview> attached_textures;
    std::string texture_attachment_detail;
    bool texture_companion_attached{};

    std::string detail;
    std::string trace;
    bool renderable{};
};


[[nodiscard]] std::unique_ptr<Session> open_session(std::string_view name,
    const std::uint8_t* bytes, std::size_t size);
[[nodiscard]] std::unique_ptr<Session> session_from_child(const ChildResource& child);
[[nodiscard]] std::string describe_session(const Session* session);
[[nodiscard]] spider::black_widow::StateBits black_widow_state(const Session* session) noexcept;
[[nodiscard]] bool attach_session_ptx(Session* session, std::string_view name,
    const std::uint8_t* bytes, std::size_t size);
[[nodiscard]] RgbaImage render_session(const Session* session, int requested_width,
    int requested_height, float yaw, float pitch, float zoom, std::uint32_t render_flags);

}  // namespace dmcresource
