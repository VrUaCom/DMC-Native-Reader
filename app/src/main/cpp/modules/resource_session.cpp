#include "dmcresource/resource_session.h"
#include "dmcresource/motion/motion_player.h"
#include "dmcresource/motion/skeleton_rig.h"
#include "dmc_rengine/formats/mod/world_transform.hpp"
#include "dmcresource/inspection_format.h"
#include "dmcresource/resource_limits.h"
#include "dmcresource/scene_projection.h"
#include "dmcresource/stage_room.h"
#include "dmcresource/texture_companion.h"
#include "dmcresource/collision_debug.h"
#include "dmcresource/format_views.h"
#include "dmcresource/neutral_texture.h"
#include "dmcresource/raster_card.h"
#include "dmcresource/matrix_ops.h"

#include <span>
#include <array>
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
    // Bank-aware FXBANK children may carry both a visual surface and an
    // EXE-backed diagnostic surface. Opening them must preserve those parent-
    // resolved views instead of discarding bank context by reopening only the
    // isolated raw payload.
    if (child.info_preview.available()) {
        auto session = make_session(child, child.trace);
        session->info_preview = child.info_preview;
        session->info_preview_active = false;
        return session;
    }
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
        .image_preview_available =
            session_active_preview(session) != nullptr,
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

bool session_has_dual_preview(const Session* session) noexcept {
    return session != nullptr &&
           session->image_preview.available() &&
           session->info_preview.available();
}

bool session_info_preview_active(const Session* session) noexcept {
    return session_has_dual_preview(session) && session->info_preview_active;
}

bool session_set_info_preview(Session* session, bool info) noexcept {
    if (!session_has_dual_preview(session)) return false;
    session->info_preview_active = info;
    return true;
}

const ImagePreview* session_active_preview(const Session* session) noexcept {
    if (session == nullptr) return nullptr;
    if (session->info_preview_active && session->info_preview.available()) {
        return &session->info_preview;
    }
    if (session->image_preview.available()) return &session->image_preview;
    if (session->info_preview.available()) return &session->info_preview;
    return nullptr;
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

namespace {

// Everything a view of a session needs; spans in `view` point into the
// vectors here, so a PreparedView is filled in place and never moved.
struct PreparedView final {
    ViewState view;
    int width{};
    int height{};
    const HierarchyOverlay* hierarchy{};
    const std::vector<std::uint32_t>* texture_slots{};
    const std::vector<ImagePreview>* textures{};
    // Presentation-only mesh/banks used for dynamic CShell visuals. The
    // canonical Session render_mesh/composite_parts are never mutated by
    // Shl spawn/despawn.
    Mesh presentation_mesh;
    std::vector<std::uint32_t> presentation_texture_slots;
    std::vector<ImagePreview> presentation_textures;
    std::vector<ViewState::EffectSprite> effect_sprites;
    bool dynamic_presentation{};
    std::vector<Vec3> floor_shadow;
    std::vector<Vec3> collision_lines;
    std::vector<Vec3> room_collision_lines;
    HierarchyOverlay stage_hierarchy;
    std::shared_ptr<const stage_room::Room> room;
};

[[nodiscard]] const Session::EffectBank* find_effect_bank(
    const Session& session, std::uint32_t slot) noexcept {
    const auto it = std::find_if(
        session.effect_banks.begin(), session.effect_banks.end(),
        [slot](const Session::EffectBank& bank) {
            return bank.resource_slot == slot;
        });
    return it == session.effect_banks.end() ? nullptr : &*it;
}

[[nodiscard]] const effect_bank::Record* find_effect_record(
    const Session& session, char kind, std::uint16_t id,
    std::uint32_t slot) noexcept {
    const auto* bank = find_effect_bank(session, slot);
    if (bank == nullptr) return nullptr;
    const auto it = std::find_if(
        bank->bank.records.begin(), bank->bank.records.end(),
        [kind, id](const effect_bank::Record& record) {
            return record.kind == kind && record.id == id;
        });
    return it == bank->bank.records.end() ? nullptr : &*it;
}

[[nodiscard]] const ImagePreview* find_effect_texture(
    const Session& session, std::uint32_t slot, std::uint16_t id) noexcept {
    const auto* bank = find_effect_bank(session, slot);
    if (bank == nullptr) return nullptr;
    const auto it = std::find_if(
        bank->textures.begin(), bank->textures.end(),
        [id](const Session::EffectTexture& texture) { return texture.id == id; });
    return it == bank->textures.end() ? nullptr : &it->image;
}

[[nodiscard]] char effect_kind_for_dispatch(
    std::uint8_t dispatch_kind) noexcept;

[[nodiscard]] bool effect_child_shape_is_valid(
    const motion::EffectChildRef& child) noexcept;

[[nodiscard]] Matrix4 effect_child_matrix(
    const motion::EffectChildRef& child) noexcept {
    constexpr float kDegreesToRadians = 0.017453292519943295769F;
    const std::array<float, 3> translation = child.translation;
    const std::array<float, 3> rotation = {
        child.rotation_degrees[0] * kDegreesToRadians,
        child.rotation_degrees[1] * kDegreesToRadians,
        child.rotation_degrees[2] * kDegreesToRadians};
    Matrix4 out = motion::attach_local_matrix_zyx(translation, rotation);
    // The V registrar stores a scale triplet. DMC3's row-vector transform
    // domain applies those factors to the three local basis rows.
    for (std::size_t component = 0U; component < 3U; ++component) {
        const float scale = child.scale[component];
        if (!std::isfinite(scale)) return Matrix4{};
        for (std::size_t column = 0U; column < 3U; ++column) {
            out.values[component * 4U + column] *= scale;
        }
    }
    return out;
}

[[nodiscard]] bool compose_effect_world(
    const Matrix4& parent, const motion::EffectChildRef& child,
    Matrix4* out) noexcept {
    if (out == nullptr || !effect_child_shape_is_valid(child)) return false;
    const auto local = effect_child_matrix(child);
    // Same local*parent order as the canonical MOD world transform builder.
    return matrix_ops::multiply(local, parent, out);
}

[[nodiscard]] const effect_bank::Record* find_animation_record(
    const Session& session, std::uint32_t slot, std::uint16_t id) noexcept {
    return find_effect_record(session, 'A', id, slot);
}

[[nodiscard]] char effect_kind_for_dispatch(
    std::uint8_t dispatch_kind) noexcept {
    switch (dispatch_kind) {
    case 0U: return 'P';
    case 1U: return 'E';
    case 2U: return 'G';
    case 3U: return 'V';
    default: return '\0';
    }
}

[[nodiscard]] std::uint8_t dispatch_kind_for_effect_kind(
    char effect_kind) noexcept {
    switch (effect_kind) {
    case 'P': return 0U;
    case 'E': return 1U;
    case 'G': return 2U;
    case 'V': return 3U;
    default: return 0xFFU;
    }
}

[[nodiscard]] bool effect_child_shape_is_valid(
    const motion::EffectChildRef& child) noexcept {
    if (effect_kind_for_dispatch(child.dispatch_kind) != child.effect_kind) {
        return false;
    }
    for (const float value : child.translation) {
        if (!std::isfinite(value)) return false;
    }
    for (const float value : child.rotation_degrees) {
        if (!std::isfinite(value)) return false;
    }
    for (const float value : child.scale) {
        if (!std::isfinite(value)) return false;
    }
    return true;
}

// Camera forward of the view being prepared (same mirrored basis as the
// renderer). Mode-2 axial billboards turn their width toward it.
thread_local Vec3 g_effect_view_forward{0.0F, 0.0F, 1.0F};

[[nodiscard]] Vec3 effect_row_transform(const Vec3& p, const Matrix4& m) noexcept {
    return {
        p.x * m.values[0] + p.y * m.values[4] + p.z * m.values[8] + m.values[12],
        p.x * m.values[1] + p.y * m.values[5] + p.z * m.values[9] + m.values[13],
        p.x * m.values[2] + p.y * m.values[6] + p.z * m.values[10] + m.values[14],
    };
}

[[nodiscard]] bool append_effect_sprite(
    const Session& session, const motion::EffectChildRef& child,
    const Matrix4& world,
    std::vector<ViewState::EffectSprite>* out) {
    if (out == nullptr || child.effect_kind != 'E') return true;
    const auto* record = find_effect_record(
        session, 'E', child.effect_id, child.resource_slot);
    if (record == nullptr) return false;
    const auto descriptor = effect_bank::effect_descriptor(*record);
    if (!descriptor.has_value()) return false;
    // CEffect draw 0x1402E5C70 dispatches modes 0/1/2/5; modes 3 and 4 draw
    // nothing. Modes 0 and 5 keep their undecoded resource contracts.
    if (descriptor->mode != 1U && descriptor->mode != 2U) return true;

    effect_bank::SpriteFrame frame = descriptor->rectangle;
    std::uint16_t texture_id = descriptor->texture;
    if (descriptor->animation_gate == 1U && descriptor->animation != 0xFFFFU) {
        const auto* animation_record = find_animation_record(
            session, child.resource_slot, descriptor->animation);
        if (animation_record != nullptr) {
            const auto animation = effect_bank::sprite_animation(*animation_record);
            if (animation.has_value() && !animation->frames.empty()) {
                texture_id = animation->texture;
                // A's local animation clock is advanced by the E runtime,
                // not by MotionScript/actor age. Until that EXE clock is
                // bridged, retain the exact A/T dependency and show its
                // canonical first atlas frame instead of guessing a frame
                // rate or treating script frames as effect ticks.
                frame = animation->frames.front();
            }
        }
    }
    if (frame.w == 0U || frame.h == 0U) return true;
    const auto* texture = find_effect_texture(
        session, child.resource_slot, texture_id);
    if (texture == nullptr || !texture->available()) return false;
    const float inv_w = 1.0F / static_cast<float>(texture->width);
    const float inv_h = 1.0F / static_cast<float>(texture->height);
    ViewState::EffectSprite sprite{
        world,
        texture,
        static_cast<float>(frame.w),
        static_cast<float>(frame.h),
        static_cast<float>(frame.x) * inv_w,
        static_cast<float>(frame.y) * inv_h,
        static_cast<float>(frame.x + frame.w) * inv_w,
        static_cast<float>(frame.y + frame.h) * inv_h};
    if (!descriptor->geometry_known) {
        out->push_back(sprite);
        return true;
    }
    const auto& a = descriptor->size;
    const auto& b = descriptor->pivot;
    const auto& scale = descriptor->scale;
    if (descriptor->mode == 1U) {
        // 0x1402E5D00: camera-facing, extents x in [-Bx, Ax-Bx], y in
        // [-By, Ay-By], scaled by |row i| of the effect world times the
        // record scale (0x1402E5FC2 loop).
        const auto row_length = [&world](std::size_t row) {
            const float* r = &world.values[row * 4U];
            return std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        };
        const float sx = row_length(0U) * std::fabs(scale[0]);
        const float sy = row_length(1U) * std::fabs(scale[1]);
        if (!(sx > 0.0F) || !(sy > 0.0F) || !(a[0] > 0.0F) || !(a[1] > 0.0F)) {
            return true;
        }
        sprite.extents = true;
        sprite.left = -b[0] * sx;
        sprite.right = (a[0] - b[0]) * sx;
        sprite.bottom = -b[1] * sy;
        sprite.top = (a[1] - b[1]) * sy;
        if (scale[0] < 0.0F) std::swap(sprite.u0, sprite.u1);
        if (scale[1] < 0.0F) std::swap(sprite.v0, sprite.v1);
        out->push_back(sprite);
        return true;
    }
    // 0x1402E69E0: quad -B, -B+U, -B+U+V, -B+V with U = (Ax,0,Az),
    // V = (0,Ay,0), transformed by S (record scale), R(D) (0x1403304A0,
    // Rz*Ry*Rx) and the effect world.
    constexpr float kDegreesToRadians = 0.017453292519943295769F;
    const Matrix4 rotation = motion::attach_local_matrix_zyx(
        {0.0F, 0.0F, 0.0F},
        {descriptor->rotation_degrees[0] * kDegreesToRadians,
         descriptor->rotation_degrees[1] * kDegreesToRadians,
         descriptor->rotation_degrees[2] * kDegreesToRadians});
    Matrix4 local_to_world;
    if (!matrix_ops::multiply(rotation, world, &local_to_world)) return true;
    const auto place = [&](float x, float y, float z) {
        return effect_row_transform(
            {x * scale[0], y * scale[1], z * scale[2]}, local_to_world);
    };
    Vec3 c0 = place(-b[0], -b[1], -b[2]);
    Vec3 c1 = place(a[0] - b[0], -b[1], a[2] - b[2]);
    Vec3 c2 = place(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
    Vec3 c3 = place(-b[0], a[1] - b[1], -b[2]);
    if (descriptor->orientation != 0U) {
        // Variants 1..3 rebuild the width axis from the camera (cross
        // products at 0x1402E6CB8): keep the U edge, turn V to face the view.
        const Vec3 axis{c1.x - c0.x, c1.y - c0.y, c1.z - c0.z};
        const Vec3 width_edge{c3.x - c0.x, c3.y - c0.y, c3.z - c0.z};
        const float width = std::sqrt(width_edge.x * width_edge.x +
                                      width_edge.y * width_edge.y +
                                      width_edge.z * width_edge.z);
        const auto& f = g_effect_view_forward;
        Vec3 side{axis.y * f.z - axis.z * f.y, axis.z * f.x - axis.x * f.z,
                  axis.x * f.y - axis.y * f.x};
        const float side_length = std::sqrt(side.x * side.x + side.y * side.y + side.z * side.z);
        if (side_length > 1.0e-6F && width > 0.0F) {
            const float k = width / side_length;
            side = {side.x * k, side.y * k, side.z * k};
            const Vec3 mid{0.5F * (c0.x + c3.x), 0.5F * (c0.y + c3.y), 0.5F * (c0.z + c3.z)};
            c0 = {mid.x - 0.5F * side.x, mid.y - 0.5F * side.y, mid.z - 0.5F * side.z};
            c3 = {mid.x + 0.5F * side.x, mid.y + 0.5F * side.y, mid.z + 0.5F * side.z};
            c1 = {c0.x + axis.x, c0.y + axis.y, c0.z + axis.z};
            c2 = {c3.x + axis.x, c3.y + axis.y, c3.z + axis.z};
        }
    }
    sprite.oriented = true;
    sprite.corners = {c0, c1, c2, c3};
    out->push_back(sprite);
    return true;
}

// V-local clock (0x140324A80): every update first adds dt (0x1403261B0,
// 1.0 per 60 Hz tick at unit speed) to V+0xF0, then spawns each entry whose
// signed i16 +0x04 threshold is below the accumulator. The spawn update is
// state 0 (0x140324C50) and only zeroes the accumulator, so an entry with
// threshold a appears at V age floor(a) + 1 (age 1 for a <= 0). Script frames
// are the same 60 Hz ticks.
[[nodiscard]] float effect_entry_spawn_age(std::int16_t activation) noexcept {
    return static_cast<float>(activation < 0 ? 0 : activation) + 1.0F;
}

bool collect_effect_children(
    const Session& session, const motion::RuntimeEffectInstance& instance,
    const motion::EffectChildRef& child, const Matrix4& parent_world,
    float age, std::vector<ViewState::EffectSprite>* out, std::size_t depth) {
    if (out == nullptr || depth >= 16U ||
        !effect_child_shape_is_valid(child) ||
        find_effect_record(session, child.effect_kind, child.effect_id,
                           child.resource_slot) == nullptr) {
        return false;
    }
    // `age` is this entry's own age: not spawned yet below zero.
    if (!(age >= 0.0F)) return true;
    Matrix4 world;
    if (!compose_effect_world(parent_world, child, &world)) return false;
    if (child.effect_kind == 'E') {
        const auto* record = find_effect_record(
            session, 'E', child.effect_id, child.resource_slot);
        const auto descriptor = record == nullptr
            ? std::optional<effect_bank::EffectDescriptor>{}
            : effect_bank::effect_descriptor(*record);
        // CEffect retires once its +0x80 countdown is negative: drawn for
        // ages 0..lifetime. A +0x84 record is held until its V is retired.
        if (descriptor.has_value() && descriptor->lifetime_known &&
            !descriptor->held_by_parent &&
            age > static_cast<float>(descriptor->lifetime_ticks)) {
            return true;
        }
        return append_effect_sprite(session, child, world, out);
    }
    if (child.effect_kind != 'V') {
        // P/G are retained as exact dependencies. Their render subtype is not
        // decoded, so they are deliberately not replaced by a guessed sprite.
        return true;
    }
    // Profile bindings may already carry the reverse-confirmed graph (Lady
    // currently does). Generic profiles are also allowed to provide only the
    // V root: in that case decode the exact child dispatch table from the
    // retained FXBANK record. No semantic name or guessed child is introduced.
    std::vector<motion::EffectChildRef> decoded_children;
    std::span<const motion::EffectChildRef> children = child.children;
    if (children.empty()) {
        const auto* record = find_effect_record(
            session, 'V', child.effect_id, child.resource_slot);
        const auto composite = record == nullptr
            ? std::optional<effect_bank::CompositeRecord>{}
            : effect_bank::composite_record(*record);
        if (composite.has_value()) {
            decoded_children.reserve(composite->entries.size());
            for (const auto& entry : composite->entries) {
                const char kind = effect_kind_for_dispatch(entry.dispatch_kind);
                if (kind == '\0') return false;
                motion::EffectChildRef decoded;
                decoded.effect_kind = kind;
                decoded.effect_id = entry.id;
                decoded.resource_slot = child.resource_slot;
                decoded.dispatch_kind = entry.dispatch_kind;
                decoded.translation = entry.translation;
                decoded.rotation_degrees = entry.rotation_degrees;
                decoded.scale = entry.scale;
                decoded.evidence = child.evidence;
                decoded.activation_offset = entry.activation_offset;
                decoded_children.push_back(decoded);
            }
            children = decoded_children;
        }
    }
    for (const auto& nested : children) {
        if (!collect_effect_children(
                session, instance, nested, world,
                age - effect_entry_spawn_age(nested.activation_offset),
                out, depth + 1U)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool build_effect_presentation(
    const Session& session, PreparedView* out) {
    if (out == nullptr || session.effect_runtime == nullptr ||
        session.effect_banks.empty()) return false;
    try {
        {
            const float cy = std::cos(out->view.yaw_radians);
            const float sy = std::sin(out->view.yaw_radians);
            const float cp = std::cos(out->view.pitch_radians);
            const float sp = std::sin(out->view.pitch_radians);
            // forward = right x up of the renderer's mirrored camera basis.
            g_effect_view_forward = {sy * cp, -cy * sp, -cy * cp};
        }
        std::vector<ViewState::EffectSprite> staged;
        for (const auto& instance : session.effect_runtime->presentation_instances()) {
            const auto& source = instance.source;
            if (!source.world_authoritative) continue;
            if (!source.children.empty()) {
                // The binding's child span is the root V's entry table.
                for (const auto& child : source.children) {
                    if (!collect_effect_children(
                            session, instance, child, source.world,
                            instance.age -
                                effect_entry_spawn_age(child.activation_offset),
                            &staged, 0U)) {
                        return false;
                    }
                }
                continue;
            }
            // A generic profile may register an E/V root without a prebuilt
            // child span. Resolve that root through the same raw bank path.
            motion::EffectChildRef root;
            root.effect_kind = source.effect_kind;
            root.effect_id = static_cast<std::uint16_t>(source.effect_id);
            root.resource_slot = source.resource_slot;
            root.dispatch_kind =
                dispatch_kind_for_effect_kind(source.effect_kind);
            root.evidence = source.evidence;
            root.scale = {1.0F, 1.0F, 1.0F};
            if (!collect_effect_children(
                    session, instance, root, source.world, instance.age,
                    &staged, 0U)) {
                return false;
            }
        }
        if (staged.empty()) return false;
        out->effect_sprites.insert(
            out->effect_sprites.end(), staged.begin(), staged.end());
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] Vec3 transform_dynamic_vertex(
    const Vec3& p, const Matrix4& m) noexcept {
    return {
        p.x * m.values[0] + p.y * m.values[4] + p.z * m.values[8] + m.values[12],
        p.x * m.values[1] + p.y * m.values[5] + p.z * m.values[9] + m.values[13],
        p.x * m.values[2] + p.y * m.values[6] + p.z * m.values[10] + m.values[14],
    };
}

[[nodiscard]] Vec3 transform_dynamic_normal(
    const Vec3& n, const Matrix4& m) noexcept {
    return {
        n.x * m.values[0] + n.y * m.values[4] + n.z * m.values[8],
        n.x * m.values[1] + n.y * m.values[5] + n.z * m.values[9],
        n.x * m.values[2] + n.y * m.values[6] + n.z * m.values[10],
    };
}

namespace mod_world = dmc::rengine::formats::mod::world_transform;

struct DynamicVertexInfluences final {
    std::array<std::uint32_t, 3> node{};
    std::array<float, 3> weight{};
    std::uint8_t count{};
};

[[nodiscard]] std::size_t dynamic_scene_vertex_count(
    const RenderScene& scene) noexcept {
    std::size_t total = 0U;
    for (const auto& primitive : scene.meshes) {
        total += primitive.mesh.vertices.size();
    }
    return total;
}

[[nodiscard]] bool flatten_dynamic_influences(
    const RenderScene& scene,
    std::size_t node_count,
    std::vector<DynamicVertexInfluences>* out) {
    if (out == nullptr) return false;
    out->clear();
    out->reserve(dynamic_scene_vertex_count(scene));
    for (std::size_t primitive = 0U; primitive < scene.meshes.size(); ++primitive) {
        const auto& source = scene.meshes[primitive].mesh;
        const SkinBinding* skin = nullptr;
        for (const auto& candidate : scene.skins) {
            if (candidate.mesh_primitive == primitive &&
                candidate.vertices.size() == source.vertices.size()) {
                skin = &candidate;
                break;
            }
        }
        for (std::size_t vertex = 0U; vertex < source.vertices.size(); ++vertex) {
            DynamicVertexInfluences influences;
            if (skin != nullptr) {
                for (const auto& joint : skin->vertices[vertex].influences) {
                    if (influences.count >= influences.node.size()) break;
                    if (joint.node_index >= node_count ||
                        !std::isfinite(joint.weight) || joint.weight <= 0.0F) {
                        continue;
                    }
                    influences.node[influences.count] = joint.node_index;
                    influences.weight[influences.count] = joint.weight;
                    ++influences.count;
                }
            }
            out->push_back(influences);
        }
    }
    return true;
}

[[nodiscard]] std::optional<Matrix4> lady_dynamic_component_node_world(
    const Session& session,
    std::uint8_t component,
    std::size_t local_node) noexcept {
    const motion::LadyComponentBinding* binding = nullptr;
    for (const auto& candidate : session.lady_component_bindings) {
        if (candidate.component == component) {
            binding = &candidate;
            break;
        }
    }
    if (binding == nullptr ||
        binding->part >= session.composite_parts.size() ||
        local_node >= session.composite_parts[binding->part].scene.nodes.size()) {
        return std::nullopt;
    }

    std::size_t node_begin = 0U;
    for (std::size_t part = 0U; part < binding->part; ++part) {
        const auto count = session.composite_parts[part].scene.nodes.size();
        if (count > std::numeric_limits<std::size_t>::max() - node_begin) {
            return std::nullopt;
        }
        node_begin += count;
    }
    if (node_begin + local_node >= session.scene.nodes.size()) {
        return std::nullopt;
    }
    return session.scene.nodes[node_begin + local_node].world;
}

[[nodiscard]] Vec3 dynamic_cross(const Vec3& a, const Vec3& b) noexcept {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

[[nodiscard]] float dynamic_length_sq(const Vec3& v) noexcept {
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

[[nodiscard]] float dynamic_length(const Vec3& v) noexcept {
    return std::sqrt(std::max(dynamic_length_sq(v), 0.0F));
}

[[nodiscard]] Vec3 dynamic_normalize(
    Vec3 v, const Vec3& fallback) noexcept {
    float length_sq = dynamic_length_sq(v);
    if (!(length_sq > 1.0e-15F) || !std::isfinite(length_sq)) {
        v = fallback;
        length_sq = dynamic_length_sq(v);
    }
    if (!(length_sq > 1.0e-15F) || !std::isfinite(length_sq)) {
        return {1.0F, 0.0F, 0.0F};
    }
    const float inv = 1.0F / std::sqrt(length_sq);
    return {v.x * inv, v.y * inv, v.z * inv};
}

[[nodiscard]] std::array<mod_world::Matrix4f, 5> lady_slot30_worlds(
    const Vec3& anchor,
    const Vec3& endpoint) noexcept {
    const Vec3 delta{
        endpoint.x - anchor.x,
        endpoint.y - anchor.y,
        endpoint.z - anchor.z,
    };
    std::array<Vec3, 5> points{};
    for (std::size_t i = 0U; i < points.size(); ++i) {
        const float t = static_cast<float>(i) * 0.25F;
        points[i] = {
            anchor.x + delta.x * t,
            anchor.y + delta.y * t,
            anchor.z + delta.z * t,
        };
    }

    const Vec3 segment{
        points[1].x - points[0].x,
        points[1].y - points[0].y,
        points[1].z - points[0].z,
    };
    const float segment_length = dynamic_length(segment);
    const Vec3 direction = dynamic_normalize(segment, {1.0F, 0.0F, 0.0F});

    // 0x14032EEE0 with reference=(0,1,0,1): row0 follows the segment,
    // row2 = direction x reference, row1 = row2 x direction. The helper
    // perturbs the reference by +0.1 X/Y/Z if it is parallel.
    Vec3 reference{0.0F, 1.0F, 0.0F};
    Vec3 row2 = dynamic_cross(direction, reference);
    if (!(dynamic_length_sq(row2) > 1.0e-15F)) {
        reference.x += 0.1F;
        row2 = dynamic_cross(direction, reference);
    }
    if (!(dynamic_length_sq(row2) > 1.0e-15F)) {
        reference.y += 0.1F;
        row2 = dynamic_cross(direction, reference);
    }
    if (!(dynamic_length_sq(row2) > 1.0e-15F)) {
        reference.z += 0.1F;
        row2 = dynamic_cross(direction, reference);
    }
    row2 = dynamic_normalize(row2, {0.0F, 0.0F, 1.0F});
    const Vec3 row1 =
        dynamic_normalize(dynamic_cross(row2, direction), {0.0F, 1.0F, 0.0F});
    const float longitudinal_scale = segment_length / 70.0F;

    std::array<mod_world::Matrix4f, 5> current{};
    for (std::size_t i = 0U; i < current.size(); ++i) {
        current[i].values = {
            direction.x * longitudinal_scale,
            direction.y * longitudinal_scale,
            direction.z * longitudinal_scale,
            0.0F,
            row1.x, row1.y, row1.z, 0.0F,
            row2.x, row2.y, row2.z, 0.0F,
            points[i].x, points[i].y, points[i].z, 1.0F,
        };
    }
    return current;
}

[[nodiscard]] std::optional<std::vector<Vec3>> skin_lady_slot30(
    const Session& session,
    const Session::LadyDynamicVisual& visual) {
    if (visual.model_slot != 30U ||
        visual.source_scene.rig == nullptr ||
        visual.source_scene.rig->node_count() != 5U ||
        visual.source_mesh.vertices.empty()) {
        return std::nullopt;
    }

    const auto anchor_world =
        lady_dynamic_component_node_world(session, 0U, 2U);
    if (!anchor_world.has_value()) return std::nullopt;

    const Vec3 anchor{
        anchor_world->values[12],
        anchor_world->values[13],
        anchor_world->values[14],
    };
    const Vec3 endpoint{
        visual.world.values[12],
        visual.world.values[13],
        visual.world.values[14],
    };
    const auto current = lady_slot30_worlds(anchor, endpoint);
    const auto palette = mod_world::build_skin_palette(
        visual.source_scene.rig->domain,
        std::span<const mod_world::Matrix4f>{current.data(), current.size()});
    if (!palette.has_value() || palette->size() != current.size()) {
        return std::nullopt;
    }

    std::vector<DynamicVertexInfluences> influences;
    if (!flatten_dynamic_influences(
            visual.source_scene, current.size(), &influences) ||
        influences.size() != visual.source_mesh.vertices.size()) {
        return std::nullopt;
    }

    std::vector<Vec3> vertices;
    vertices.reserve(visual.source_mesh.vertices.size());
    for (std::size_t index = 0U;
         index < visual.source_mesh.vertices.size();
         ++index) {
        const auto& rest = visual.source_mesh.vertices[index];
        const auto& skin = influences[index];
        Vec3 moved = rest;
        float total = 0.0F;
        for (std::uint8_t k = 0U; k < skin.count; ++k) {
            total += skin.weight[k];
        }
        if (skin.count > 0U && total > 0.0F) {
            moved = {};
            for (std::uint8_t k = 0U; k < skin.count; ++k) {
                const auto& matrix = (*palette)[skin.node[k]].values;
                const Vec3 point{
                    rest.x * matrix[0] + rest.y * matrix[4] +
                        rest.z * matrix[8] + matrix[12],
                    rest.x * matrix[1] + rest.y * matrix[5] +
                        rest.z * matrix[9] + matrix[13],
                    rest.x * matrix[2] + rest.y * matrix[6] +
                        rest.z * matrix[10] + matrix[14],
                };
                const float weight = skin.weight[k] / total;
                moved.x += point.x * weight;
                moved.y += point.y * weight;
                moved.z += point.z * weight;
            }
        }
        if (!std::isfinite(moved.x) || !std::isfinite(moved.y) ||
            !std::isfinite(moved.z)) {
            return std::nullopt;
        }
        vertices.push_back(moved);
    }
    return vertices;
}

[[nodiscard]] bool build_lady_dynamic_presentation(
    const Session& session, PreparedView* out) {
    if (out == nullptr) return false;
    const bool any = std::any_of(
        session.lady_dynamic_visuals.begin(),
        session.lady_dynamic_visuals.end(),
        [](const Session::LadyDynamicVisual& v) { return v.active; });
    if (!any) return false;

    auto& mesh = out->presentation_mesh;
    mesh = session.render_mesh;
    out->presentation_textures = session.attached_textures;

    const std::size_t base_triangles = mesh.indices.size() / 3U;
    if (session.render_triangle_texture_slots.size() == base_triangles) {
        out->presentation_texture_slots =
            session.render_triangle_texture_slots;
    } else {
        out->presentation_texture_slots.assign(
            base_triangles, kNoTextureSlot);
    }

    const auto ensure_uv = [&mesh]() {
        if (mesh.uv0.empty()) mesh.uv0.assign(mesh.vertices.size(), {});
    };
    const auto ensure_color = [&mesh]() {
        if (mesh.color0.empty()) {
            mesh.color0.assign(mesh.vertices.size(), {128U, 128U, 128U, 128U});
        }
    };
    const auto ensure_blend = [&mesh]() {
        if (mesh.blend0.empty()) mesh.blend0.assign(mesh.vertices.size(), 0U);
    };
    const auto ensure_normal = [&mesh]() {
        if (mesh.normal0.empty()) mesh.normal0.assign(mesh.vertices.size(), {});
    };

    for (const auto& visual : session.lady_dynamic_visuals) {
        if (!visual.active || visual.source_mesh.vertices.empty() ||
            visual.source_mesh.indices.size() < 3U) {
            continue;
        }

        const std::size_t vertex_base = mesh.vertices.size();
        const std::size_t before = mesh.vertices.size();

        if (!mesh.uv0.empty() || visual.source_mesh.has_uv0()) ensure_uv();
        if (!mesh.color0.empty() || visual.source_mesh.has_color0()) ensure_color();
        if (!mesh.blend0.empty() || visual.source_mesh.has_blend0()) ensure_blend();
        if (!mesh.normal0.empty() || visual.source_mesh.has_normal0()) ensure_normal();

        mesh.vertices.reserve(mesh.vertices.size() + visual.source_mesh.vertices.size());
        const auto tether_vertices =
            visual.model_slot == 30U
                ? skin_lady_slot30(session, visual)
                : std::nullopt;
        if (tether_vertices.has_value()) {
            mesh.vertices.insert(
                mesh.vertices.end(),
                tether_vertices->begin(),
                tether_vertices->end());
        } else {
            for (const auto& v : visual.source_mesh.vertices) {
                mesh.vertices.push_back(
                    transform_dynamic_vertex(v, visual.world));
            }
        }

        if (!mesh.uv0.empty()) {
            if (visual.source_mesh.has_uv0()) {
                mesh.uv0.insert(mesh.uv0.end(),
                                visual.source_mesh.uv0.begin(),
                                visual.source_mesh.uv0.end());
            } else {
                mesh.uv0.resize(mesh.vertices.size());
            }
        }
        if (!mesh.color0.empty()) {
            if (visual.source_mesh.has_color0()) {
                mesh.color0.insert(mesh.color0.end(),
                                   visual.source_mesh.color0.begin(),
                                   visual.source_mesh.color0.end());
            } else {
                mesh.color0.resize(
                    mesh.vertices.size(), {128U, 128U, 128U, 128U});
            }
        }
        if (!mesh.blend0.empty()) {
            if (visual.source_mesh.has_blend0()) {
                mesh.blend0.insert(mesh.blend0.end(),
                                   visual.source_mesh.blend0.begin(),
                                   visual.source_mesh.blend0.end());
            } else {
                mesh.blend0.resize(mesh.vertices.size(), 0U);
            }
        }
        if (!mesh.normal0.empty()) {
            if (visual.source_mesh.has_normal0()) {
                if (tether_vertices.has_value()) {
                    // The current renderer, like MOT preview, does not rebuild
                    // skinned normals. Preserve the source normals rather than
                    // incorrectly applying the Shl03 actor transform to a
                    // cable whose vertices use five independent bone worlds.
                    mesh.normal0.insert(
                        mesh.normal0.end(),
                        visual.source_mesh.normal0.begin(),
                        visual.source_mesh.normal0.end());
                } else {
                    for (const auto& n : visual.source_mesh.normal0) {
                        mesh.normal0.push_back(
                            transform_dynamic_normal(n, visual.world));
                    }
                }
            } else {
                mesh.normal0.resize(mesh.vertices.size());
            }
        }

        mesh.indices.reserve(mesh.indices.size() + visual.source_mesh.indices.size());
        for (const auto index : visual.source_mesh.indices) {
            if (index >= visual.source_mesh.vertices.size() ||
                vertex_base > std::numeric_limits<std::uint32_t>::max() - index) {
                return false;
            }
            mesh.indices.push_back(
                static_cast<std::uint32_t>(vertex_base + index));
        }

        const std::uint32_t texture_base =
            static_cast<std::uint32_t>(out->presentation_textures.size());
        out->presentation_textures.insert(
            out->presentation_textures.end(),
            visual.textures.begin(), visual.textures.end());

        const std::size_t dynamic_triangles =
            visual.source_mesh.indices.size() / 3U;
        if (visual.texture_slots.size() == dynamic_triangles) {
            for (const auto slot : visual.texture_slots) {
                if (slot == kNoTextureSlot) {
                    out->presentation_texture_slots.push_back(kNoTextureSlot);
                } else if (slot < visual.textures.size() &&
                           texture_base <=
                               std::numeric_limits<std::uint32_t>::max() - slot) {
                    out->presentation_texture_slots.push_back(
                        texture_base + slot);
                } else {
                    out->presentation_texture_slots.push_back(kNoTextureSlot);
                }
            }
        } else {
            out->presentation_texture_slots.insert(
                out->presentation_texture_slots.end(),
                dynamic_triangles, kNoTextureSlot);
        }

        if (mesh.vertices.size() <= before) return false;
    }

    out->dynamic_presentation =
        mesh.vertices.size() > session.render_mesh.vertices.size();
    return out->dynamic_presentation;
}

constexpr float kDollyMin = -2.0F;

void prepare_view(const Session& session, int requested_width, int requested_height, float yaw,
                  float pitch, float zoom, std::uint32_t render_flags, const ViewControls& controls,
                  PreparedView* out) {
    const auto flags = static_cast<RenderFlags>(render_flags);
    auto& view = out->view;
    view.yaw_radians = yaw;
    view.pitch_radians = std::clamp(pitch, -1.55f, 1.55f);
    view.zoom = std::clamp(zoom, 0.15f, 8.0f);
    view.wireframe = has_render_flag(flags, RenderFlag::Wireframe);
    view.uv_layout = has_render_flag(flags, RenderFlag::UvLayout);
    view.framing_vertices = motion::motion_rest_vertices(&session);
    view.fallback_texture = &neutral_texture();
    view.smooth_textures = has_render_flag(flags, RenderFlag::SmoothTextures);
    view.unlit = has_render_flag(flags, RenderFlag::Unlit);
    view.fast_preview = has_render_flag(flags, RenderFlag::Preview);
    view.background = static_cast<std::uint8_t>((flags >> kRenderBackgroundShift) & 3U);
    view.pan_x = std::isfinite(controls.pan_x) ? std::clamp(controls.pan_x, -20.0F, 20.0F) : 0.0F;
    view.pan_y = std::isfinite(controls.pan_y) ? std::clamp(controls.pan_y, -20.0F, 20.0F) : 0.0F;
    view.dolly = std::isfinite(controls.dolly)
        ? std::clamp(controls.dolly, kDollyMin, session_dolly_limit(&session)) : 0.0F;

    out->width = std::clamp(requested_width, 64, 1024);
    out->height = std::clamp(requested_height, 64, 1024);
    out->hierarchy = !view.uv_layout && has_render_flag(flags, RenderFlag::Hierarchy) &&
            session.hierarchy_overlay.available()
        ? &session.hierarchy_overlay
        : nullptr;
    out->texture_slots = session.render_triangle_texture_slots.empty() ? nullptr : &session.render_triangle_texture_slots;
    out->textures = session.attached_textures.empty() ? nullptr : &session.attached_textures;
    if (build_lady_dynamic_presentation(session, out)) {
        out->texture_slots = out->presentation_texture_slots.empty()
            ? nullptr : &out->presentation_texture_slots;
        out->textures = out->presentation_textures.empty()
            ? nullptr : &out->presentation_textures;
    }
    (void)build_effect_presentation(session, out);
    view.effect_sprites = out->effect_sprites;

    const auto& rest = view.framing_vertices.empty() ? std::span<const Vec3>{session.render_mesh.vertices}
                                                     : view.framing_vertices;
    // Camera follow: frame the model where its motion has taken it (x/z of
    // the vertex centre against the rest pose; height stays put).
    if (controls.follow && !view.framing_vertices.empty() && !session.render_mesh.vertices.empty()) {
        double rx = 0.0, rz = 0.0, cx = 0.0, cz = 0.0;
        for (const auto& v : view.framing_vertices) {
            rx += v.x;
            rz += v.z;
        }
        for (const auto& v : session.render_mesh.vertices) {
            cx += v.x;
            cz += v.z;
        }
        const auto rn = static_cast<double>(view.framing_vertices.size());
        const auto cn = static_cast<double>(session.render_mesh.vertices.size());
        view.frame_shift = {static_cast<float>(cx / cn - rx / rn), 0.0F, static_cast<float>(cz / cn - rz / rn)};
    }

    // SHW footprint on a floor under the feet (lowest rest vertex).
    if (!view.uv_layout && session.stage == nullptr && has_render_flag(flags, RenderFlag::Shadows)) {
        float floor_y = std::numeric_limits<float>::infinity();
        for (const auto& v : rest) floor_y = std::min(floor_y, v.y);
        if (std::isfinite(floor_y)) {
            // SHW hulls when the archive has them, else the mesh itself.
            out->floor_shadow = session.shadow_bindings.empty()
                ? shadow::mesh_floor_shadow(session.render_mesh, shadow::kViewerLightDirection, floor_y)
                : shadow::floor_shadow_triangles(session, shadow::kViewerLightDirection, floor_y);
            view.floor = true;
            view.floor_y = floor_y;
            view.floor_shadow = out->floor_shadow;
        }
    }
    // Room (stage_room.h): the chosen stage around the model, its floor spot
    // (or the point placed by a double tap) under the model's feet, turned
    // about that spot by the twist gesture; a stage itself has no room.
    if (!view.uv_layout && has_render_flag(flags, RenderFlag::Room) &&
        !stage_room::is_stage_session(session)) {
        out->room = stage_room::current();
    }
    if (out->room && !rest.empty()) {
        const auto placement = stage_room::placement_for(rest, controls.room_yaw);
        view.room_mesh = &out->room->mesh;
        view.room_texture_slots = &out->room->triangle_texture_slots;
        view.room_textures = &out->room->textures;
        view.room_translucent_triangles = &out->room->translucent_triangles;
        view.room_pivot = placement.pivot;
        view.room_yaw = placement.yaw;
        view.room_offset = placement.offset;
        // The drawn room is the collision world of this session's runtime.
        if (!view.uv_layout && !out->room->collision_sources.empty()) {
            stage_room::set_active_collision(&session, out->room, placement);
        } else {
            stage_room::clear_active_collision(&session);
        }
    } else if (!view.uv_layout) {
        stage_room::clear_active_collision(&session);
    }
    // A stage opened as its scene: its merged mesh is drawn by the room pass
    // (near-plane clipped), turned about the floor spot by the twist gesture.
    if (!view.uv_layout && session.stage != nullptr) {
        const auto& stage = *session.stage;
        view.room_mesh = &stage.mesh;
        view.room_texture_slots = &stage.triangle_texture_slots;
        view.room_textures = &stage.textures;
        view.room_translucent_triangles = &stage.translucent_triangles;
        view.room_pivot = stage.spots.empty() ? Vec3{} : stage.spots.front();
        view.room_yaw = std::isfinite(controls.room_yaw) ? controls.room_yaw : 0.0F;
        view.room_offset = {};
        view.room_wire_main = true;
        // The joint hierarchy follows the twist like the meshes.
        if (out->hierarchy != nullptr && &session.hierarchy_overlay == out->hierarchy) {
            out->stage_hierarchy = session.hierarchy_overlay;
            const stage_room::Placement turn{view.room_pivot, view.room_yaw, {}};
            for (auto& point : out->stage_hierarchy.points) point = stage_room::room_to_model(turn, point);
            out->hierarchy = &out->stage_hierarchy;
        }
    }
    // Attack collision shapes on the current pose (debug meshes at000-at003).
    if (!view.uv_layout && session.collision != nullptr && has_render_flag(flags, RenderFlag::Collision)) {
        out->collision_lines = collision::posed_collision_lines(session);
        view.overlay_lines = out->collision_lines;
    }
    if (!view.uv_layout && out->room != nullptr &&
        has_render_flag(flags, RenderFlag::RoomCollision) &&
        !out->room->collision_lines.empty()) {
        out->room_collision_lines = out->room->collision_lines;
        view.room_collision_lines = out->room_collision_lines;
    }
    // A HITS file opened on its own: the record edges outline its surfaces.
    if (!view.uv_layout && session.probe.format == Format::Hits && session.renderable &&
        out->room_collision_lines.empty()) {
        const auto& mesh = session.render_mesh;
        out->room_collision_lines.reserve(mesh.indices.size() * 2U);
        for (std::size_t t = 0U; t + 2U < mesh.indices.size(); t += 3U) {
            for (std::size_t k = 0U; k < 3U; ++k) {
                const auto a = mesh.indices[t + k], b = mesh.indices[t + (k + 1U) % 3U];
                if (a >= mesh.vertices.size() || b >= mesh.vertices.size()) continue;
                out->room_collision_lines.push_back(mesh.vertices[a]);
                out->room_collision_lines.push_back(mesh.vertices[b]);
            }
        }
        view.room_collision_lines = out->room_collision_lines;
    }
    // A stage opened as its scene shows its own HITS in place.
    if (!view.uv_layout && session.stage != nullptr &&
        has_render_flag(flags, RenderFlag::RoomCollision) &&
        !session.stage->collision_lines.empty()) {
        out->room_collision_lines = session.stage->collision_lines;
        view.room_collision_lines = out->room_collision_lines;
    }
}

}  // namespace

float session_camera_distance(const Session* session) noexcept {
    if (session == nullptr) return 0.0F;
    const auto rest = motion::motion_rest_vertices(session);
    return framing_camera_distance(rest.empty() ? std::span<const Vec3>{session->render_mesh.vertices} : rest);
}

float session_dolly_limit(const Session* session) noexcept {
    return session != nullptr && session->stage != nullptr ? 4.0F : 0.9F;
}

RgbaImage render_session(const Session* session, int requested_width,
    int requested_height, float yaw, float pitch, float zoom, std::uint32_t render_flags) {
    return render_session(session, requested_width, requested_height, yaw, pitch, zoom, render_flags,
                          ViewControls{});
}

RgbaImage render_session(const Session* session, int requested_width, int requested_height, float yaw,
                         float pitch, float zoom, std::uint32_t render_flags, const ViewControls& controls) {
    if (session == nullptr) return {};
    if (session->uv_gallery && session->uv_map_index &&
        *session->uv_map_index < session->uv_gallery->maps.size()) {
        return render_uv_map(session->uv_gallery->coordinates,
            session->uv_gallery->maps[*session->uv_map_index].indices,
            std::clamp(requested_width, 64, 1024),
            std::clamp(requested_height, 64, 1024), zoom);
    }
    if (!session->renderable) return {};
    PreparedView prepared;
    prepare_view(*session, requested_width, requested_height, yaw, pitch, zoom, render_flags, controls, &prepared);
    const Mesh& presented =
        prepared.dynamic_presentation
            ? prepared.presentation_mesh
            : session->render_mesh;
    return render_view(presented, prepared.width, prepared.height, prepared.view,
                       prepared.hierarchy, prepared.texture_slots, prepared.textures);
}

SessionPick pick_session(const Session* session, int requested_width, int requested_height, float yaw,
                         float pitch, float zoom, std::uint32_t render_flags, const ViewControls& controls,
                         float x, float y) {
    SessionPick out;
    if (session == nullptr || !session->renderable || session->uv_gallery) return out;
    PreparedView prepared;
    prepare_view(*session, requested_width, requested_height, yaw, pitch, zoom, render_flags, controls, &prepared);
    if (prepared.view.uv_layout) return out;
    const auto* bones = session->hierarchy_overlay.available() ? &session->hierarchy_overlay : nullptr;
    const auto pick = pick_view(session->render_mesh, prepared.width, prepared.height, prepared.view, x, y, bones);
    out.model = pick.model;
    out.room = pick.room;
    out.room_floor = pick.room_floor;
    out.room_point = pick.room_point;
    out.joint = pick.joint;
    if (pick.joint >= 0 && static_cast<std::size_t>(pick.joint) < session->scene.nodes.size()) {
        // Composite scenes list their parts' nodes one part after another.
        std::string part;
        std::size_t local = static_cast<std::size_t>(pick.joint);
        std::size_t begin = 0U;
        for (const auto& p : session->composite_parts) {
            const auto count = p.scene.nodes.size();
            if (local >= begin && local < begin + count) {
                part = p.name;
                local -= begin;
                break;
            }
            begin += count;
        }
        const auto& name = session->scene.nodes[static_cast<std::size_t>(pick.joint)].name;
        out.joint_name = "joint " + std::to_string(local) + (name.empty() ? "" : " · " + name) +
                         (part.empty() ? "" : " (" + part + ")");
    }
    return out;
}
}  // namespace dmcresource
