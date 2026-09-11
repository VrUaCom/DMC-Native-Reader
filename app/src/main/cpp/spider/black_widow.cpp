#include "dmcresource/spider/black_widow.h"

#include "dmcresource/model_texture_binding.h"

namespace dmcresource::spider::black_widow {
namespace {

inline void set_if(StateBits* state, StateFlag flag, bool condition) noexcept {
    if (state != nullptr && condition) *state |= state_flag(flag);
}

}  // namespace

StateBits evaluate_model_session(const ModelSessionView& session) noexcept {
    StateBits state = 0U;

    const bool has_geometry_capability =
        has_capability(session.capabilities, ResourceCapability::Geometry);
    const bool can_render = session.renderable && session.render_mesh != nullptr &&
        has_geometry_capability;
    const bool can_wireframe = can_render &&
        has_capability(session.capabilities, ResourceCapability::Wireframe);
    const bool can_inspect =
        has_capability(session.capabilities, ResourceCapability::Inspection);
    const bool can_show_hierarchy = session.hierarchy_available &&
        has_capability(session.capabilities, ResourceCapability::NodeHierarchy);
    const bool has_skinning =
        has_capability(session.capabilities, ResourceCapability::SkeletalSkinning);
    const bool has_skin_weights =
        has_capability(session.capabilities, ResourceCapability::SkinWeights);
    const bool has_texture_bindings =
        has_capability(session.capabilities, ResourceCapability::TextureBinding);
    const bool can_preview_image = session.image_preview_available &&
        has_capability(session.capabilities, ResourceCapability::ImagePreview);
    const bool has_child_resources = session.child_resource_count != 0U &&
        has_capability(session.capabilities, ResourceCapability::ChildResources);
    const bool is_text =
        has_capability(session.capabilities, ResourceCapability::Text);
    const bool is_container =
        has_capability(session.capabilities, ResourceCapability::Container);
    const bool has_collision =
        has_capability(session.capabilities, ResourceCapability::Collision);
    const bool has_adjacency =
        has_capability(session.capabilities, ResourceCapability::Adjacency);
    const bool has_transform_selectors =
        has_capability(session.capabilities, ResourceCapability::TransformSelectors);

    const bool complete_binding = can_render && has_texture_bindings &&
        model_texture_binding::can_attach_texture_companion(
            *session.render_mesh, session.triangle_texture_slots);
    const bool can_show_uv = complete_binding &&
        has_capability(session.capabilities, ResourceCapability::UvCoordinates);
    const bool child_browser_mode =
        has_child_resources && !can_render && !can_preview_image && !session.uv_map_view;
    const bool texture_companion_attachable =
        can_show_uv || session.part_texture_attachment_available;

    set_if(&state, StateFlag::CanRender, can_render || session.uv_map_view);
    set_if(&state, StateFlag::CanWireframe, can_wireframe);
    set_if(&state, StateFlag::CanInspect, can_inspect);
    set_if(&state, StateFlag::CanShowHierarchy, can_show_hierarchy);
    set_if(&state, StateFlag::HasSkinning, has_skinning);
    set_if(&state, StateFlag::HasSkinWeights, has_skin_weights);
    set_if(&state, StateFlag::HasTextureBindings, has_texture_bindings);
    set_if(&state, StateFlag::CanPreviewImage, can_preview_image);
    set_if(&state, StateFlag::HasChildResources, has_child_resources);
    set_if(&state, StateFlag::IsText, is_text);
    set_if(&state, StateFlag::IsContainer, is_container);
    set_if(&state, StateFlag::HasCollision, has_collision);
    set_if(&state, StateFlag::HasAdjacency, has_adjacency);
    set_if(&state, StateFlag::HasTransformSelectors, has_transform_selectors);
    set_if(&state, StateFlag::CanShowUv, can_show_uv);
    set_if(&state, StateFlag::ChildBrowserMode, child_browser_mode);
    set_if(&state, StateFlag::TextureCompanionAttachable,
           texture_companion_attachable);
    set_if(&state, StateFlag::TextureCompanionAttached,
           texture_companion_attachable && session.texture_companion_attached);

    set_if(&state, StateFlag::UvMapView, session.uv_map_view);
    set_if(&state, StateFlag::CanInspectUv, session.uv_data_available);
    set_if(&state, StateFlag::CanInspectMeshes, session.object_count != 0U);
    set_if(&state, StateFlag::CanInspectHierarchy, session.hierarchy_node_count != 0U);
    set_if(&state, StateFlag::CanExportPng, session.png_export_available);
    return state;
}

}  // namespace dmcresource::spider::black_widow
