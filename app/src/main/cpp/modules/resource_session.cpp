#include "dmcresource/resource_session.h"
#include "dmcresource/motion/motion_player.h"
#include "dmcresource/inspection_format.h"
#include "dmcresource/resource_limits.h"
#include "dmcresource/scene_projection.h"
#include "dmcresource/texture_companion.h"
#include "dmcresource/collision_debug.h"
#include "dmcresource/format_views.h"
#include "dmcresource/neutral_texture.h"
#include "dmcresource/raster_card.h"

#include <span>
#include <cmath>
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
                               std::size_t size) {
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

[[nodiscard]] bool append_composite_projection(
        Session* merged,
        const Session& source,
        const CompositePart& part,
        bool* uv_complete,
        bool* slots_complete) {
    if (merged == nullptr || uv_complete == nullptr || slots_complete == nullptr) {
        return false;
    }

    const std::size_t node_base = merged->scene.nodes.size();
    const std::size_t vertex_base = merged->render_mesh.vertices.size();
    const auto& source_mesh = source.render_mesh;
    if (source_mesh.indices.size() % 3U != 0U) return false;

    try {
        for (const auto& source_node : part.scene.nodes) {
            auto node = source_node;
            if (!add_i32_offset(source_node.parent, node_base, &node.parent)) return false;
            if (!part.name.empty()) node.name = part.name + " / " + source_node.name;
            merged->scene.nodes.push_back(std::move(node));
        }

        merged->render_mesh.vertices.insert(
            merged->render_mesh.vertices.end(),
            source_mesh.vertices.begin(), source_mesh.vertices.end());
        // COLOR0 / blend channels ride along (neutral when a part has none).
        append_vertex_channels(source_mesh, true, true, &merged->render_mesh, true);

        if (*uv_complete) {
            if (!source_mesh.has_uv0()) {
                merged->render_mesh.uv0.clear();
                *uv_complete = false;
            } else {
                merged->render_mesh.uv0.insert(
                    merged->render_mesh.uv0.end(),
                    source_mesh.uv0.begin(), source_mesh.uv0.end());
            }
        }

        for (const auto index : source_mesh.indices) {
            if (index >= source_mesh.vertices.size()) return false;
            std::uint32_t adjusted = 0U;
            if (!add_u32_offset(index, vertex_base, &adjusted)) return false;
            merged->render_mesh.indices.push_back(adjusted);
        }

        if (*slots_complete) {
            const std::size_t triangles = source_mesh.indices.size() / 3U;
            if (source.render_triangle_texture_slots.size() != triangles) {
                merged->render_triangle_texture_slots.clear();
                *slots_complete = false;
            } else {
                for (const auto slot : source.render_triangle_texture_slots) {
                    if (slot == kNoTextureSlot) {
                        merged->render_triangle_texture_slots.push_back(kNoTextureSlot);
                        continue;
                    }
                    const std::uint64_t remapped =
                        static_cast<std::uint64_t>(slot) +
                        static_cast<std::uint64_t>(part.texture_slot_base);
                    if (remapped >= static_cast<std::uint64_t>(kNoTextureSlot)) return false;
                    merged->render_triangle_texture_slots.push_back(
                        static_cast<std::uint32_t>(remapped));
                }
            }
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
                .scene = &part.scene,
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
    node.properties.push_back({"vertices", std::to_string(source.render_mesh.vertices.size()),
                               EvidenceLevel::DataConfirmed});
    node.properties.push_back({"triangles",
                               std::to_string(source.render_mesh.indices.size() / 3U),
                               EvidenceLevel::DataConfirmed});
    node.properties.push_back({"texture_slot_base", std::to_string(part.texture_slot_base),
                               EvidenceLevel::StructuralConfirmed});
    node.properties.push_back({"texture_slot_span", std::to_string(part.texture_slot_span),
                               EvidenceLevel::StructuralConfirmed});
    if (!source.inspection.empty()) node.children.push_back(source.inspection.root);
    return node;
}

[[nodiscard]] bool prepare_composite_capacity(
        Session* composite,
        const std::vector<const Session*>& sources,
        bool* uv_complete,
        bool* slots_complete) {
    if (composite == nullptr || uv_complete == nullptr || slots_complete == nullptr) {
        return false;
    }

    std::size_t total_vertices = 0U;
    std::size_t total_indices = 0U;
    std::size_t total_nodes = 0U;
    std::size_t total_triangles = 0U;
    *uv_complete = true;
    *slots_complete = true;

    for (const Session* source : sources) {
        if (source == nullptr || source->render_mesh.indices.size() % 3U != 0U) return false;
        if (source->render_mesh.vertices.size() > resource_limits::kMaxVertices - total_vertices ||
            source->render_mesh.indices.size() > resource_limits::kMaxIndices - total_indices) {
            return false;
        }
        total_vertices += source->render_mesh.vertices.size();
        total_indices += source->render_mesh.indices.size();
        const std::size_t triangles = source->render_mesh.indices.size() / 3U;
        if (triangles > resource_limits::kMaxIndices / 3U - total_triangles) return false;
        total_triangles += triangles;
        if (source->scene.nodes.size() >
            std::numeric_limits<std::size_t>::max() - total_nodes) {
            return false;
        }
        total_nodes += source->scene.nodes.size();
        *uv_complete = *uv_complete && source->render_mesh.has_uv0();
        *slots_complete = *slots_complete &&
            source->render_triangle_texture_slots.size() == triangles;
    }

    try {
        composite->composite_parts.reserve(sources.size());
        composite->scene.nodes.reserve(total_nodes);
        composite->render_mesh.vertices.reserve(total_vertices);
        composite->render_mesh.indices.reserve(total_indices);
        if (*uv_complete) composite->render_mesh.uv0.reserve(total_vertices);
        if (*slots_complete) composite->render_triangle_texture_slots.reserve(total_triangles);
        return true;
    } catch (...) {
        return false;
    }
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
    // Container payloads (PAC slots) retain their bytes. Any recognized payload
    // is opened through the full registry so each file gets its own viewer.
    if (!child.source_bytes.empty() && child.probe.recognized) {
        const std::string filename = child.suggested_filename.empty()
            ? child.title
            : child.suggested_filename;
        auto materialized = open_session(
            filename, child.source_bytes.data(), child.source_bytes.size());
        if (materialized) {
            if (!materialized->detail.empty()) materialized->detail += "\n";
            materialized->detail += "Opened from container slot " + child.id;
            return materialized;
        }
    }
    // Unknown bytes open as the raw binary view, unless the container already
    // drew a view for them (e.g. an effect sprite over its bank texture).
    if (!child.source_bytes.empty() && !child.probe.recognized && !child.image_preview.available()) {
        const std::string filename = child.suggested_filename.empty()
            ? child.title
            : child.suggested_filename;
        auto raw = open_session(filename, child.source_bytes.data(), child.source_bytes.size());
        if (raw) {
            raw->detail += "\nOpened from container slot " + child.id;
            return raw;
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

namespace {

// Every opened file gets a picture: a session with no mesh, no pixels, no
// child list and no UV map draws its inspection card (title, properties,
// tree) over a hex dump of its bytes.
void attach_standalone_view(Session* session, std::span<const std::uint8_t> bytes) {
    if (session == nullptr || session->renderable || session->image_preview.available() ||
        !session->children.empty() || session->uv_gallery) {
        return;
    }
    try {
        session->image_preview =
            raster::render_info_card(session->inspection, session->detail, bytes);
        session->capabilities = session->capabilities | ResourceCapability::ImagePreview;
    } catch (...) {
        session->image_preview = {};
    }
}

// Files outside the registry (or rejected by their module) still open as raw
// binary: byte profile, strings, offset-table hint and hex dump. Read-only;
// nothing here claims a format.
std::unique_ptr<Session> open_binary_session(std::string_view name,
                                             std::span<const std::uint8_t> bytes,
                                             const PipelineResult& rejected) {
    if (bytes.empty()) return nullptr;
    try {
        auto session = std::make_unique<Session>();
        session->probe = rejected.probe;
        session->capabilities = capability(ResourceCapability::Inspection) |
                                ResourceCapability::ImagePreview;
        session->inspection.format = "BIN";
        session->inspection.root.id = "binary";
        session->inspection.root.title = std::string{name.substr(name.find_last_of("/\\") + 1U)};
        session->inspection.root.kind = InspectionKind::Document;
        session->inspection.root.source_span = SourceSpan{0U, bytes.size()};
        const bool recognized = rejected.probe.recognized;
        session->inspection.root.properties.push_back(
            {"Status",
             recognized ? std::string{rejected.probe.family} + " identity, module rejected the bytes"
                        : std::string{"no known format identity"},
             EvidenceLevel::Recognized});
        if (!rejected.detail.empty()) {
            session->inspection.root.properties.push_back(
                {"Reason", rejected.detail, EvidenceLevel::Recognized});
        }
        const auto profile = views::profile_binary(bytes);
        views::append_binary_inspection(session->inspection, profile);
        session->detail = "Raw binary view | bytes=" + std::to_string(bytes.size());
        session->trace = "modules:\n  [OK] native.binary-profile";
        session->image_preview =
            views::render_binary_view(session->inspection, session->detail, profile, bytes);
        return session;
    } catch (...) {
        return nullptr;
    }
}

}  // namespace

std::unique_ptr<Session> open_session(std::string_view name,
    const std::uint8_t* bytes, std::size_t size) {
    auto pipeline = dmcresource::run_decode_pipeline(name, bytes, size);
    if (!pipeline.accepted) {
        if (bytes == nullptr) return nullptr;
        return open_binary_session(name, std::span<const std::uint8_t>{bytes, size}, pipeline);
    }

    bool community_ptx = false;
    for (const auto& module : pipeline.modules) {
        if (module.name != nullptr &&
            std::string_view{module.name} == "native.ptx-community-descriptors") {
            community_ptx = true;
        }
    }
    auto trace = pipeline_trace(pipeline);
    auto session = make_session(std::move(pipeline), std::move(trace));
    if (session && community_ptx) {
        session->non_canonical_notes.push_back(
            std::string{name} +
            ": texture descriptors were written by a community tool; the canonical "
            "validator rejects them, the viewer reads header, sector spans and DDS only");
    }
    retain_lazy_child_sources(session.get(), bytes, size);
    attach_standalone_view(session.get(), std::span<const std::uint8_t>{bytes, size});
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

    for (const Session* source : sources) {
        if (source == nullptr || source->probe.format != Format::Mod ||
            !source->probe.content_confirmed || !source->renderable ||
            !source->scene.has_geometry()) {
            return nullptr;
        }
    }

    bool uv_complete = true;
    bool slots_complete = true;
    if (!prepare_composite_capacity(
            composite.get(), sources, &uv_complete, &slots_complete)) {
        return nullptr;
    }

    std::uint64_t next_slot_base = 0U;
    try {
        for (std::size_t index = 0U; index < sources.size(); ++index) {
            const Session* source = sources[index];
            CompositePart part;
            part.name = names[index].empty()
                ? "MOD part " + std::to_string(index + 1U)
                : names[index];
            part.scene = source->scene;
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
            if (!append_composite_projection(
                    composite.get(), *source, part, &uv_complete, &slots_complete)) {
                return nullptr;
            }
            composite->composite_parts.push_back(std::move(part));
        }
    } catch (...) {
        return nullptr;
    }

    composite->renderable = !composite->render_mesh.vertices.empty() &&
        composite->render_mesh.indices.size() >= 3U;
    if (!composite->renderable) return nullptr;

    if (!materialize_hierarchy_overlay(composite->scene, &composite->hierarchy_overlay)) {
        composite->hierarchy_overlay = {};
    }
    composite->detail = "Composite MOD scene: " +
        std::to_string(composite->composite_parts.size()) +
        " canonical parts; source-local scenes retained once; one flattened render projection; "
        "placement uses source coordinates only; inferred bone/weapon attachments are disabled";
    composite->trace =
        "route=MOD[]->CompositePart[source-scene][]->RenderMesh; animation/physics=deferred";
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
    view.framing_vertices = dmcresource::motion::motion_rest_vertices(session);
    view.fallback_texture = &dmcresource::neutral_texture();

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
    // SHW footprint on a floor under the feet (lowest rest vertex).
    std::vector<dmcresource::Vec3> floor_shadow;
    if (!view.uv_layout && dmcresource::has_render_flag(flags, dmcresource::RenderFlag::Shadows)) {
        const auto& rest = view.framing_vertices.empty()
            ? std::span<const dmcresource::Vec3>{session->render_mesh.vertices}
            : view.framing_vertices;
        float floor_y = std::numeric_limits<float>::infinity();
        for (const auto& v : rest) floor_y = std::min(floor_y, v.y);
        if (std::isfinite(floor_y)) {
            // SHW hulls when the archive has them, else the mesh itself.
            floor_shadow = session->shadow_bindings.empty()
                ? dmcresource::shadow::mesh_floor_shadow(
                      session->render_mesh, dmcresource::shadow::kViewerLightDirection, floor_y)
                : dmcresource::shadow::floor_shadow_triangles(
                      *session, dmcresource::shadow::kViewerLightDirection, floor_y);
            view.floor = true;
            view.floor_y = floor_y;
            view.floor_shadow = floor_shadow;
        }
    }
    // Attack collision shapes on the current pose (debug meshes at000-at003).
    std::vector<dmcresource::Vec3> collision_lines;
    if (!view.uv_layout && session->collision != nullptr &&
        dmcresource::has_render_flag(flags, dmcresource::RenderFlag::Collision)) {
        collision_lines = dmcresource::collision::posed_collision_lines(*session);
        view.overlay_lines = collision_lines;
    }
    return dmcresource::render_view(
        session->render_mesh, width, height, view,
        hierarchy, texture_slots, textures);
}
}  // namespace dmcresource
