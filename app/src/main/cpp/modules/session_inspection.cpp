#include "dmcresource/session_inspection.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/inspection_format.h"

namespace dmcresource {
namespace {
void property(InspectionNode& node, std::string key, std::string value) {
    node.properties.push_back({std::move(key), std::move(value)});
}
void count(InspectionNode& node, const char* key, std::size_t value) {
    property(node, key, std::to_string(value));
}

// Project the existing typed inspection tree, retaining empty objects and mesh
// groups omitted from rendering. No second MOD/SCM object reconstruction.
void append_objects(const InspectionNode& source, InspectionNode& target) {
    if (source.kind == InspectionKind::Object || source.kind == InspectionKind::Mesh) {
        InspectionNode node;
        node.id = source.id;
        node.title = source.title;
        node.kind = source.kind;
        if (source.kind == InspectionKind::Object)
            count(node, "Meshes", count_inspection_nodes(source, InspectionKind::Mesh));
        for (const auto& child : source.children) append_objects(child, node);
        target.children.push_back(std::move(node));
    } else {
        for (const auto& child : source.children) append_objects(child, target);
    }
}

void append_uv(const Session& session, InspectionNode& root) {
    root.title = "UV maps";
    std::vector<UvMapSummary> maps;
    if (session.uv_gallery) {
        for (const auto& map : session.uv_gallery->maps)
            maps.push_back({map.texture_slot, map.indices.size() / 3U});
    } else {
        maps = summarize_uv_maps(session.render_mesh, session.render_triangle_texture_slots);
    }
    count(root, "Maps", maps.size());
    if (maps.empty()) {
        property(root, "Status", "UV maps unavailable: incomplete UV or texture-slot bindings");
        return;
    }
    for (std::size_t i = 0; i < maps.size(); ++i) {
        const auto& map = maps[i];
        InspectionNode node;
        node.kind = InspectionKind::Texture;
        node.title = "UV / Slot " + std::to_string(map.texture_slot);
        if (session.uv_map_index && *session.uv_map_index == i) node.title += " (selected)";
        count(node, "Triangles", map.triangle_count);
        root.children.push_back(std::move(node));
    }
}

void append_hierarchy(const Session& session, InspectionNode& root) {
    root.title = "Bones / scene hierarchy";
    const auto& nodes = session.scene.nodes;
    count(root, "Nodes", nodes.size());
    if (nodes.empty()) {
        property(root, "Status", "No hierarchy nodes available");
        return;
    }
    // List explicit parent relations without recursive tree expansion: arbitrary
    // node order and even malformed cycles cannot hang this information view.
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto& source = nodes[i];
        InspectionNode node;
        node.kind = source.kind == RenderNodeKind::Bone ? InspectionKind::Bone : InspectionKind::Node;
        node.title = source.name.empty() ? "Node " + std::to_string(i) : source.name;
        count(node, "Index", i);
        if (!source.parent_authority) property(node, "Parent", "Unconfirmed");
        else if (source.parent == -1) property(node, "Parent", "Root");
        else if (source.parent < 0 || static_cast<std::size_t>(source.parent) >= nodes.size())
            property(node, "Parent", "Invalid reference");
        else {
            const auto& parent = nodes[source.parent];
            property(node, "Parent", (parent.name.empty() ? "Node" : parent.name) +
                " [" + std::to_string(source.parent) + "]");
        }
        root.children.push_back(std::move(node));
    }
}
}

InspectionDocument inspect_session(const Session* session, InspectionTopic topic) {
    InspectionDocument doc;
    if (!session) return doc;
    doc.format = session->inspection.format;
    doc.root.kind = InspectionKind::Document;
    switch (topic) {
    case InspectionTopic::Uv:
        append_uv(*session, doc.root);
        break;
    case InspectionTopic::Meshes:
        doc.root.title = "Objects / meshes";
        count(doc.root, "Objects", count_inspection_nodes(session->inspection.root, InspectionKind::Object));
        count(doc.root, "Meshes", count_inspection_nodes(session->inspection.root, InspectionKind::Mesh));
        append_objects(session->inspection.root, doc.root);
        break;
    case InspectionTopic::Hierarchy:
        append_hierarchy(*session, doc.root);
        break;
    default:
        return {};
    }
    return doc;
}
}  // namespace dmcresource
