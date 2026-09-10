#include "dmcresource/resource_session.h"
#include "dmcresource/inspection_format.h"
#include "dmcresource/scene_projection.h"
#include "dmcresource/texture_companion.h"
#include <algorithm>
#include <sstream>
#include <utility>

namespace dmcresource {
namespace {
void prepare_session_caches(Session* session) {
    if (session == nullptr) return;

    if (!dmcresource::materialize_hierarchy_overlay(
            session->scene, &session->hierarchy_overlay)) {
        session->hierarchy_overlay = {};
        if (!session->detail.empty()) session->detail += "\n";
        session->detail += "Hierarchy overlay rejected malformed node/matrix data";
    }

    if (session->renderable) {
        if (!session->scene.has_geometry() ||
            !dmcresource::materialize_render_scene(
                session->scene, &session->render_mesh)) {
            session->renderable = false;
            if (!session->detail.empty()) session->detail += "\n";
            session->detail +=
                "RenderScene projection rejected malformed or missing geometry";
            return;
        }

        if (!dmcresource::materialize_triangle_texture_slots(
                session->scene, &session->render_triangle_texture_slots)) {
            session->render_triangle_texture_slots.clear();
            if (!session->detail.empty()) session->detail += "\n";
            session->detail +=
                "Texture-slot projection unavailable: conflicting or malformed material bindings";
        }
    }
}


template<class Source>
std::unique_ptr<Session> make_session(Source&& source, std::string trace) {
    auto session = std::make_unique<Session>();
    session->probe = source.probe;
    session->capabilities = source.capabilities;
    session->inspection = std::forward<Source>(source).inspection;
    session->scene = std::forward<Source>(source).scene;
    session->image_preview = std::forward<Source>(source).image_preview;
    session->children = std::forward<Source>(source).children;
    session->detail = std::forward<Source>(source).detail;
    session->trace = std::move(trace);
    session->renderable = source.renderable;
    prepare_session_caches(session.get());
    return session;
}

}  // namespace

std::unique_ptr<Session> session_from_child(const ChildResource& child) {
    return make_session(child, child.trace);
}

[[nodiscard]] dmcresource::spider::black_widow::StateBits black_widow_state(
        const Session* session) noexcept {
    if (session == nullptr) return 0U;
    return dmcresource::spider::black_widow::evaluate_model_session({
        .capabilities = session->capabilities,
        .renderable = session->renderable,
        .render_mesh = &session->render_mesh,
        .triangle_texture_slots = session->render_triangle_texture_slots,
        .hierarchy_available = session->hierarchy_overlay.available(),
        .image_preview_available = session->image_preview.available(),
        .child_resource_count = session_child_count(session),
        .texture_companion_attached = session->texture_companion_attached,
        .uv_map_view = session->uv_gallery && session->uv_map_index &&
            *session->uv_map_index < session->uv_gallery->maps.size(),
        .uv_data_available = session->render_mesh.has_uv0() ||
            (session->uv_gallery && !session->uv_gallery->maps.empty()),
        .object_count = count_inspection_nodes(session->inspection.root, InspectionKind::Object),
        .hierarchy_node_count = session->scene.nodes.size(),
    });
}

std::unique_ptr<Session> open_session(std::string_view name,
    const std::uint8_t* bytes, std::size_t size) {
    auto pipeline = dmcresource::run_decode_pipeline(name, bytes, size);
    if (!pipeline.accepted) return nullptr;

    auto trace = pipeline_trace(pipeline);
    return make_session(std::move(pipeline), std::move(trace));
}

std::string describe_session(const Session* session) {
    if (session == nullptr) return "no session";
    std::ostringstream out;
    out << session->probe.family
        << " | domain=" << session->probe.domain
        << " | support=" << session->probe.support
        << " | evidence=" << session->probe.evidence
        << " | identity="
        << (session->probe.content_confirmed ? "content-confirmed" : "extension/name-only");
    if (session->renderable) {
        out << " | vertices=" << session->render_mesh.vertices.size()
            << " | triangles=" << (session->render_mesh.indices.size() / 3u)
            << " | uv0=" << session->render_mesh.uv0.size();
    } else if (session->image_preview.available()) {
        out << " | imagePreview=" << session->image_preview.width
            << "x" << session->image_preview.height;
    } else {
        out << " | preview=inspection";
    }
    if (!session->children.empty()) {
        out << " | children=" << session->children.size();
    }
    out << " | spatialHierarchy="
        << (session->hierarchy_overlay.available() ? "yes" : "no");
    if (!session->attached_textures.empty()) {
        std::size_t attached = 0U;
        for (const auto& texture : session->attached_textures) {
            if (texture.available()) ++attached;
        }
        out << " | companionTextures=" << attached;
    }
    if (!session->detail.empty()) out << "\n" << session->detail;
    if (!session->texture_attachment_detail.empty()) {
        out << "\n" << session->texture_attachment_detail;
    }
    if (!session->trace.empty()) out << "\n" << session->trace;
    return out.str();
}

bool attach_session_ptx(Session* session, std::string_view name,
    const std::uint8_t* bytes, std::size_t size) {
    if (session == nullptr) return false;
    auto attachment = dmcresource::texture_companion::attach_ptx(
        name,
        bytes, size,
        {
            .mesh = &session->render_mesh,
            .triangle_texture_slots = session->render_triangle_texture_slots,
        });

    session->texture_attachment_detail = std::move(attachment.detail);
    if (!attachment.attached) return false;

    session->attached_textures = std::move(attachment.textures);
    session->texture_companion_attached = true;
    return true;
}

RgbaImage render_session(const Session* session, int requested_width,
    int requested_height, float yaw, float pitch, float zoom, std::uint32_t render_flags) {
    if (session == nullptr) return {};
    if (session->uv_gallery && session->uv_map_index &&
        *session->uv_map_index < session->uv_gallery->maps.size()) {
        return render_uv_map(session->uv_gallery->coordinates,
            session->uv_gallery->maps[*session->uv_map_index].indices,
            std::clamp(requested_width, 64, 1024),
            std::clamp(requested_height, 64, 1024), zoom);
    }
    if (!session->renderable) return {};
    const auto flags = static_cast<dmcresource::RenderFlags>(
        static_cast<std::uint32_t>(render_flags));

    dmcresource::ViewState view;
    view.yaw_radians = static_cast<float>(yaw);
    view.pitch_radians = std::clamp(static_cast<float>(pitch), -1.55f, 1.55f);
    view.zoom = std::clamp(static_cast<float>(zoom), 0.15f, 8.0f);
    view.wireframe = dmcresource::has_render_flag(
        flags, dmcresource::RenderFlag::Wireframe);
    view.uv_layout = dmcresource::has_render_flag(
        flags, dmcresource::RenderFlag::UvLayout);

    const int width = std::clamp(static_cast<int>(requested_width), 64, 1024);
    const int height = std::clamp(static_cast<int>(requested_height), 64, 1024);
    const auto* hierarchy =
        !view.uv_layout &&
        dmcresource::has_render_flag(flags, dmcresource::RenderFlag::Hierarchy) &&
        session->hierarchy_overlay.available()
            ? &session->hierarchy_overlay
            : nullptr;
    const auto* texture_slots = session->render_triangle_texture_slots.empty()
        ? nullptr
        : &session->render_triangle_texture_slots;
    const auto* textures = session->attached_textures.empty()
        ? nullptr
        : &session->attached_textures;
    return dmcresource::render_view(
        session->render_mesh, width, height, view,
        hierarchy, texture_slots, textures);
}
}  // namespace dmcresource
