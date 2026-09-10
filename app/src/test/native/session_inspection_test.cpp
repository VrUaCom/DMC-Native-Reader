#include "dmcresource/session_inspection.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/inspection_format.h"
#include <cassert>

using namespace dmcresource;
namespace widow = dmcresource::spider::black_widow;

int main() {
    Session session;
    session.inspection.format = "TEST";
    session.inspection.root.title = "Resource";
    InspectionNode objects;
    objects.kind = InspectionKind::Collection;
    for (int i = 0; i < 3; ++i) {
        InspectionNode object;
        object.title = "Object " + std::to_string(i);
        object.kind = InspectionKind::Object;
        for (int m = 0; m < i; ++m) {
            InspectionNode mesh;
            mesh.title = "Mesh " + std::to_string(m);
            mesh.kind = InspectionKind::Mesh;
            object.children.push_back(mesh);
        }
        objects.children.push_back(object);
    }
    session.inspection.root.children.push_back(objects);
    // No render geometry: information must retain empty objects and be usable
    // through the W long-press even when wireframe itself is unavailable.
    auto state = black_widow_state(&session);
    assert(widow::has_state(state, widow::StateFlag::CanInspectMeshes));
    assert(!widow::has_state(state, widow::StateFlag::CanWireframe));
    const auto meshes = inspect_session(&session, InspectionTopic::Meshes);
    assert(meshes.root.children.size() == 3);
    assert(meshes.root.children[0].children.empty());
    assert(meshes.root.children[2].children.size() == 2);
    assert(format_inspection_tree(meshes).find("Objects: 3") != std::string::npos);
    assert(format_inspection_tree(meshes).find("Meshes: 3") != std::string::npos);

    session.scene.nodes.resize(3);
    session.scene.nodes[0].name = "Child";
    session.scene.nodes[0].parent = 2;
    session.scene.nodes[0].parent_authority = true;
    session.scene.nodes[1].name = "Unknown";
    session.scene.nodes[2].name = "Root bone";
    session.scene.nodes[2].parent_authority = true;
    // Non-topological node storage, no world transforms, no invented roots.
    auto text = format_inspection_tree(inspect_session(&session, InspectionTopic::Hierarchy));
    assert(text.find("Parent: Root bone [2]") != std::string::npos);
    assert(text.find("Parent: Unconfirmed") != std::string::npos);
    state = black_widow_state(&session);
    assert(widow::has_state(state, widow::StateFlag::CanInspectHierarchy));
    assert(!widow::has_state(state, widow::StateFlag::CanShowHierarchy));
    session.scene.nodes[0].parent = 99;
    assert(format_inspection_tree(inspect_session(&session, InspectionTopic::Hierarchy)).find(
        "Invalid reference") != std::string::npos);
    session.scene.nodes[0].parent = 0;
    assert(!inspect_session(&session, InspectionTopic::Hierarchy).empty()); // bounded listing

    session.render_mesh.vertices.resize(3);
    session.render_mesh.uv0 = {{0,0},{1,0},{0,1}};
    session.render_mesh.indices = {0,1,2,0,1,2,0,1,2};
    session.render_triangle_texture_slots = {9,2,9};
    const auto summaries = summarize_uv_maps(session.render_mesh, session.render_triangle_texture_slots);
    assert(summaries.size() == 2 && summaries[0].texture_slot == 2);
    assert(summaries[0].triangle_count == 1 && summaries[1].triangle_count == 2);
    const auto gallery = build_uv_gallery(session.render_mesh, session.render_triangle_texture_slots);
    for (std::size_t i = 0; i < summaries.size(); ++i) {
        assert(gallery.maps[i].texture_slot == summaries[i].texture_slot);
        assert(gallery.maps[i].indices.size() / 3 == summaries[i].triangle_count);
    }
    text = format_inspection_tree(inspect_session(&session, InspectionTopic::Uv));
    assert(text.find("Maps: 2") != std::string::npos && text.find("UV / Slot 9") != std::string::npos);
    session.render_triangle_texture_slots.clear();
    assert(widow::has_state(black_widow_state(&session), widow::StateFlag::CanInspectUv));
    assert(format_inspection_tree(inspect_session(&session, InspectionTopic::Uv)).find(
        "incomplete") != std::string::npos);
    session.uv_gallery = std::make_shared<UvGallery>(gallery);
    session.uv_map_index = 1;
    assert(format_inspection_tree(inspect_session(&session, InspectionTopic::Uv)).find(
        "Slot 9 (selected)") != std::string::npos);
    assert(inspect_session(nullptr, InspectionTopic::Uv).empty());
    assert(inspect_session(&session, static_cast<InspectionTopic>(999)).empty());
}
