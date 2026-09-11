#include "dmcresource/resource_session.h"
#include "dmcresource/inspection_format.h"
#include "dmcresource/scene_projection.h"
#include "dmcresource/texture_companion.h"

#include <algorithm>
#include <cstdint>
#include <limits>
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

void retain_lazy_child_sources(Session* session,
                               const std::uint8_t* bytes,
                               std::size_t size) noexcept {
    if (session == nullptr || bytes == nullptr || size == 0U) return;
    bool allocation_failed = false;
    for (auto& child : session->children) {
        if (child.image_preview.available() ||
            !has_capability(child.capabilities, ResourceCapability::ImagePreview)) {
            continue;
        }
        const auto offset64 = child.source_span.offset;
        const auto size64 = child.source_span.size;
        if (offset64 > static_cast<std::uint64_t>(size) ||
            size64 > static_cast<std::uint64_t>(size) - offset64) {
            continue;
        }
        const auto offset = static_cast<std::size_t>(offset64);
        const auto child_size = static_cast<std::size_t>(size64);
        try {
            child.source_bytes.assign(bytes + offset, bytes + offset + child_size);
        } catch (...) {
            child.source_bytes.clear();
            allocation_failed = true;
        }
    }
    if (allocation_failed) {
        if (!session->detail.empty()) session->detail += "\n";
        session->detail +=
            "Lazy child source retention unavailable for one or more image children";
    }
}

[[nodiscard]] bool update_max_texture_slot(std::uint32_t slot,
                                           bool* seen,
                                           std::uint32_t* max_slot) noexcept {
    if (seen == nullptr || max_slot == nullptr) return false;
    if (slot == kNoTextureSlot) return true;
    *seen = true;
    *max_slot = std::max(*max_slot, slot);
    return true;
}

[[nodiscard]] bool compute_texture_slot_span(
        const RenderScene& scene,
        const std::vector<std::uint32_t>& slots,
        std::uint32_t* out_span) noexcept {
    if (out_span == nullptr) return false;
    bool seen = false;
    std::uint32_t max_slot = 0U;
    for (const auto slot : slots) {
        if (!update_max_texture_slot(slot, &seen, &max_slot)) return false;
    }
    for (const auto& binding : scene.textures) {
        if (!update_max_texture_slot(binding.texture_slot, &seen, &max_slot)) return false;
    }
    if (!seen) {
        *out_span = 0U;
        return true;
    }
    if (max_slot == std::numeric_limits<std::uint32_t>::max()) return false;
    const std::uint64_t span = static_cast<std::uint64_t>(max_slot) + 1ULL;
    if (span >= static_cast<std::uint64_t>(kNoTextureSlot)) return false;
    *out_span = static_cast<std::uint32_t>(span);
    return true;
}

[[nodiscard]] bool add_u32_offset(std::uint32_t value, std::size_t offset,
                                  std::uint32_t* out) noexcept {
    if (out == nullptr) return false;
    if (offset > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }
    const auto offset32 = static_cast<std::uint32_t>(offset);
    if (value > std::numeric_limits<std::uint32_t>::max() - offset32) return false;
    *out = value + offset32;
    return true;
}

[[nodiscard]] bool add_i32_offset(std::int32_t value, std::size_t offset,
                                  std::int32_t* out) noexcept {
    if (out == nullptr) return false;
    if (value < 0) {
        *out = value;
        return true;
    }
    if (offset > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        return false;
    }
    const auto offset32 = static_cast<std::int32_t>(offset);
    if (value > std::numeric_limits<std::int32_t>::max() - offset32) return false;
    *out = value + offset32;
    return true;
}

[[nodiscard]] bool append_composite_scene_part(RenderScene* merged,
                                                const CompositePart& part) {
    if (merged == nullptr) return false;
    const std::size_t node_base = merged->nodes.size();
    const std::size_t mesh_base = merged->meshes.size();

    try {
        for (const auto& source_node : part.scene.nodes) {
            auto node = source_node;
            if (!add_i32_offset(source_node.parent, node_base, &node.parent)) return false;
            if (!part.name.empty()) node.name = part.name + " / " + source_node.name;
            merged->nodes.push_back(std::move(node));
        }

        for (const auto& source_mesh : part.scene.meshes) {
            auto mesh = source_mesh;
            if (!add_i32_offset(source_mesh.node_index, node_base, &mesh.node_index)) {
                return false;
            }
            if (!part.name.empty()) mesh.name = part.name + " / " + source_mesh.name;
            merged->meshes.push_back(std::move(mesh));
        }

        for (const auto& source_skin : part.scene.skins) {
            auto skin = source_skin;
            if (!add_u32_offset(source_skin.mesh_primitive, mesh_base,
                                &skin.mesh_primitive)) {
                return false;
            }
            for (auto& vertex : skin.vertices) {
                for (auto& influence : vertex.influences) {
                    std::uint32_t adjusted = 0U;
                    if (!add_u32_offset(influence.node_index, node_base, &adjusted)) {
                        return false;
                    }
                    influence.node_index = adjusted;
                }
            }
            merged->skins.push_back(std::move(skin));
        }

        for (const auto& source_texture : part.scene.textures) {
            auto texture = source_texture;
            if (!add_u32_offset(source_texture.mesh_primitive, mesh_base,
                                &texture.mesh_primitive)) {
                return false;
            }
            if (source_texture.texture_slot != kNoTextureSlot) {
                const std::uint64_t remapped =
                    static_cast<std::uint64_t>(source_texture.texture_slot) +
                    static_cast<std::uint64_t>(part.texture_slot_base);
                if (remapped >= static_cast<std::uint64_t>(kNoTextureSlot)) return false;
                texture.texture_slot = static_cast<std::uint32_t>(remapped);
            }
            merged->textures.push_back(std::move(texture));
        }
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool has_required_texture_slots(const CompositePart& part) noexcept {
    for (const auto slot : part.render_triangle_texture_slots) {
        if (slot != kNoTextureSlot) return true;
    }
    return false;
}

[[nodiscard]] bool has_attachable_composite_part(const Session* session) noexcept {
    if (session == nullptr) return false;
    for (const auto& part : session->composite_parts) {
        if (texture_companion::can_attach({
                .mesh = &part.render_mesh,
                .triangle_texture_slots = part.render_triangle_texture_slots,
            })) {
            return true;
        }
    }
    return false;
}

void refresh_composite_texture_completion(Session* session) noexcept {
    if (session == nullptr || session->composite_parts.empty()) return;
    bool complete = true;
    bool any_required = false;
    for (const auto& part : session->composite_parts) {
        if (!has_required_texture_slots(part)) continue;
        any_required = true;
        if (!part.texture_companion_attached) complete = false;
    }
    session->texture_companion_attached = any_required && complete;
}

[[nodiscard]] bool session_png_export_available(const Session* session) noexcept {
    if (session == nullptr) return false;
    if (session->image_preview.available()) return true;
    if (session->uv_gallery) {
        if (session->uv_map_index &&
            *session->uv_map_index < session->uv_gallery->maps.size()) {
            return true;
        }
        return !session->uv_gallery->maps.empty();
    }
    if (session->children.empty()) return false;
    for (const auto& child : session->children) {
        if (!has_capability(child.capabilities, ResourceCapability::ImagePreview)) {
            return false;
        }
        if (!child.image_preview.available() && child.source_bytes.empty()) {
            return false;
        }
    }
    return true;
}

InspectionNode make_composite_inspection_part(const Session& source,
                                              const CompositePart& part,
                                              std::size_t index) {
    InspectionNode node;
    node.id = "mod-part-" + std::to_string(index);
    node.title = part.name;
    node.kind = InspectionKind::Collection;
    node.properties.push_back({"source_format", "MOD", EvidenceLevel::DataConfirmed});
    node.properties.push_back({"vertices", std::to_string(part.render_mesh.vertices.size()),
                               EvidenceLevel::DataConfirmed});
    node.properties.push_back({"triangles",
                               std::to_string(part.render_mesh.indices.size() / 3U),
                               EvidenceLevel::DataConfirmed});
    node.properties.push_back({"texture_slot_base", std::to_string(part.texture_slot_base),
                               EvidenceLevel::StructuralConfirmed});
    node.properties.push_back({"texture_slot_span", std::to_string(part.texture_slot_span),
                               EvidenceLevel::StructuralConfirmed});
    if (!source.inspection.empty()) node.children.push_back(source.inspection.root);
    return node;
}

}  // namespace

std::unique_ptr<Session> session_from_child(const ChildResource& child) {
    if (!child.image_preview.available() &&
        has_capability(child.capabilities, ResourceCapability::ImagePreview) &&
        !child.source_bytes.empty()) {
        const std::string filename = child.suggested_filename.empty()
            ? child.title
            : child.suggested_filename;
        auto materialized = open_session(
            filename, child.source_bytes.data(), child.source_bytes.size());
        if (materialized && materialized->image_preview.available()) {
            if (!materialized->detail.empty()) materialized->detail += "\n";
            materialized->detail += "Lazy child materialized from retained container payload";
            return materialized;
        }
    }
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
        .part_texture_attachment_available = has_attachable_composite_part(session),
        .png_export_available = session_png_export_available(session),
    });
}

std::unique_ptr<Session> open_session(std::string_view name,
    const std::uint8_t* bytes, std::size_t size) {
    auto pipeline = dmcresource::run_decode_pipeline(name, bytes, size);
    if (!pipeline.accepted) return nullptr;

    auto trace = pipeline_trace(pipeline);
    auto session = make_session(std::move(pipeline), std::move(trace));
    retain_lazy_child_sources(session.get(), bytes, size);
    return session;
}

std::unique_ptr<Session> compose_mod_sessions(
        const std::vector<const Session*>& sources,
        const std::vector<std::string>& names) {
    if (sources.size() < 2U || sources.size() != names.size()) return nullptr;

    auto composite = std::make_unique<Session>();
    composite->probe = sources.front() ? sources.front()->probe : ProbeResult{};
    composite->inspection.format = "MOD composite";
    composite->inspection.root.id = "mod-composite";
    composite->inspection.root.title = "Composite MOD Scene";
    composite->inspection.root.kind = InspectionKind::Document;
    composite->inspection.root.properties.push_back({
        "part_count", std::to_string(sources.size()), EvidenceLevel::DataConfirmed});
    composite->inspection.root.properties.push_back({
        "placement", "source coordinates; no inferred bone or weapon attachment",
        EvidenceLevel::StructuralConfirmed});

    std::uint64_t next_slot_base = 0U;
    try {
        composite->composite_parts.reserve(sources.size());
        for (std::size_t index = 0U; index < sources.size(); ++index) {
            const Session* source = sources[index];
            if (source == nullptr || source->probe.format != Format::Mod ||
                !source->probe.content_confirmed || !source->renderable ||
                !source->scene.has_geometry()) {
                return nullptr;
            }

            CompositePart part;
            part.name = names[index].empty()
                ? "MOD part " + std::to_string(index + 1U)
                : names[index];
            part.scene = source->scene;
            part.render_mesh = source->render_mesh;
            part.render_triangle_texture_slots = source->render_triangle_texture_slots;
            if (!compute_texture_slot_span(
                    part.scene, part.render_triangle_texture_slots,
                    &part.texture_slot_span)) {
                return nullptr;
            }
            if (next_slot_base >= static_cast<std::uint64_t>(kNoTextureSlot)) return nullptr;
            if (next_slot_base + static_cast<std::uint64_t>(part.texture_slot_span) >=
                static_cast<std::uint64_t>(kNoTextureSlot)) {
                return nullptr;
            }
            part.texture_slot_base = static_cast<std::uint32_t>(next_slot_base);
            next_slot_base += static_cast<std::uint64_t>(part.texture_slot_span);

            composite->capabilities |= source->capabilities;
            composite->inspection.root.children.push_back(
                make_composite_inspection_part(*source, part, index));
            composite->composite_parts.push_back(std::move(part));
        }

        for (const auto& part : composite->composite_parts) {
            if (!append_composite_scene_part(&composite->scene, part)) return nullptr;
        }
    } catch (...) {
        return nullptr;
    }

    composite->renderable = true;
    composite->detail = "Composite MOD scene: " +
        std::to_string(composite->composite_parts.size()) +
        " canonical parts; each source scene/node namespace is retained; "
        "placement uses source coordinates only; inferred bone/weapon attachments are disabled";
    composite->trace = "route=MOD[]->CompositePart[]->RenderScene; animation/physics=deferred";
    prepare_session_caches(composite.get());
    if (!composite->renderable) return nullptr;
    refresh_composite_texture_completion(composite.get());
    return composite;
}

std::size_t session_composite_part_count(const Session* session) noexcept {
    return session == nullptr ? 0U : session->composite_parts.size();
}

std::string session_composite_part_name(const Session* session, int index) {
    if (session == nullptr || index < 0 ||
        static_cast<std::size_t>(index) >= session->composite_parts.size()) {
        return {};
    }
    return session->composite_parts[static_cast<std::size_t>(index)].name;
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
    if (!session->composite_parts.empty()) {
        out << " | compositeParts=" << session->composite_parts.size();
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
    if (!session->composite_parts.empty()) {
        out << "\nComposite parts:";
        for (const auto& part : session->composite_parts) {
            out << "\n- " << part.name
                << " | slotBase=" << part.texture_slot_base
                << " | slotSpan=" << part.texture_slot_span
                << " | PTX=" << (part.texture_companion_attached ? "attached" : "not-attached");
        }
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
    if (!session->composite_parts.empty()) {
        session->texture_attachment_detail =
            "PTX companion requires an explicit composite MOD part; automatic part matching is disabled";
        return false;
    }

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

bool attach_session_part_ptx(Session* session, int part_index,
    std::string_view name, const std::uint8_t* bytes, std::size_t size) {
    if (session == nullptr || part_index < 0 ||
        static_cast<std::size_t>(part_index) >= session->composite_parts.size()) {
        return false;
    }

    auto& part = session->composite_parts[static_cast<std::size_t>(part_index)];
    auto attachment = dmcresource::texture_companion::attach_ptx(
        name, bytes, size,
        {
            .mesh = &part.render_mesh,
            .triangle_texture_slots = part.render_triangle_texture_slots,
        });

    part.texture_attachment_detail = attachment.detail;
    session->texture_attachment_detail = part.name + ": " + attachment.detail;
    if (!attachment.attached) return false;

    const std::uint64_t required_size =
        static_cast<std::uint64_t>(part.texture_slot_base) + attachment.textures.size();
    if (required_size >= static_cast<std::uint64_t>(kNoTextureSlot) ||
        required_size > static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max())) {
        session->texture_attachment_detail =
            part.name + ": PTX companion rejected: composite texture namespace overflow";
        return false;
    }

    try {
        if (session->attached_textures.size() < static_cast<std::size_t>(required_size)) {
            session->attached_textures.resize(static_cast<std::size_t>(required_size));
        }
        for (std::size_t local_slot = 0U;
             local_slot < attachment.textures.size(); ++local_slot) {
            const std::size_t global_slot =
                static_cast<std::size_t>(part.texture_slot_base) + local_slot;
            session->attached_textures[global_slot] =
                std::move(attachment.textures[local_slot]);
        }
    } catch (...) {
        session->texture_attachment_detail =
            part.name + ": PTX companion rejected: texture allocation failed";
        return false;
    }

    part.texture_companion_attached = true;
    refresh_composite_texture_completion(session);

    std::size_t attached_parts = 0U;
    std::size_t required_parts = 0U;
    for (const auto& candidate : session->composite_parts) {
        if (!has_required_texture_slots(candidate)) continue;
        ++required_parts;
        if (candidate.texture_companion_attached) ++attached_parts;
    }
    session->texture_attachment_detail = part.name + ": " + part.texture_attachment_detail +
        " | compositeParts=" + std::to_string(attached_parts) + "/" +
        std::to_string(required_parts);
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
