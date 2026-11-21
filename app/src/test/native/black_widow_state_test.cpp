#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>

#include "dmcresource/resource_capabilities.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/spider/black_widow.h"

int main() {
    using namespace dmcresource;
    namespace widow = dmcresource::spider::black_widow;

    Mesh mesh;
    mesh.vertices = {
        {-1.0F, -1.0F, 0.0F},
        { 1.0F, -1.0F, 0.0F},
        { 0.0F,  1.0F, 0.0F},
    };
    mesh.indices = {0U, 1U, 2U};
    mesh.uv0 = {{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}};

    const auto capabilities =
        capability(ResourceCapability::Inspection) |
        ResourceCapability::Geometry |
        ResourceCapability::Wireframe |
        ResourceCapability::NodeHierarchy |
        ResourceCapability::TextureBinding |
        ResourceCapability::UvCoordinates |
        ResourceCapability::SkeletalSkinning |
        ResourceCapability::SkinWeights;

    std::vector<std::uint32_t> slots{2U};
    widow::ModelSessionView model{
        .capabilities = capabilities,
        .renderable = true,
        .render_mesh = &mesh,
        .triangle_texture_slots = slots,
        .hierarchy_available = true,
        .image_preview_available = false,
        .child_resource_count = 0U,
        .texture_companion_attached = false,
    };

    auto state = widow::evaluate_model_session(model);
    assert(widow::has_state(state, widow::StateFlag::CanRender));
    assert(widow::has_state(state, widow::StateFlag::CanWireframe));
    assert(widow::has_state(state, widow::StateFlag::CanInspect));
    assert(widow::has_state(state, widow::StateFlag::CanShowHierarchy));
    assert(widow::has_state(state, widow::StateFlag::CanShowUv));
    assert(widow::has_state(
        state, widow::StateFlag::TextureCompanionAttachable));
    assert(!widow::has_state(
        state, widow::StateFlag::TextureCompanionAttached));
    assert(!widow::has_state(state, widow::StateFlag::ChildBrowserMode));
    assert(!widow::has_state(state, widow::StateFlag::CanExportPng));
    assert(widow::has_state(state, widow::StateFlag::CanAddModelPart));
    assert(widow::has_state(state, widow::StateFlag::CanStageCompanion));

    // PNG export is a typed native application-state decision, independent of
    // generic model rendering. Android only changes the shared button when this
    // flag is present.
    model.png_export_available = true;
    state = widow::evaluate_model_session(model);
    assert(widow::has_state(state, widow::StateFlag::CanExportPng));
    model.png_export_available = false;

    model.texture_companion_attached = true;
    state = widow::evaluate_model_session(model);
    assert(widow::has_state(
        state, widow::StateFlag::TextureCompanionAttachable));
    assert(widow::has_state(
        state, widow::StateFlag::TextureCompanionAttached));

    // Runtime hierarchy authority stays a native decision; a capability alone
    // must not expose a hierarchy control.
    model.hierarchy_available = false;
    state = widow::evaluate_model_session(model);
    assert(!widow::has_state(state, widow::StateFlag::CanShowHierarchy));
    model.hierarchy_available = true;

    // A diagnostic string can never make the state attached; only typed native
    // session state participates in Black Widow evaluation. When rendering is
    // unavailable, inspection may remain available but model actions disappear.
    model.renderable = false;
    state = widow::evaluate_model_session(model);
    assert(widow::has_state(state, widow::StateFlag::CanInspect));
    assert(!widow::has_state(state, widow::StateFlag::CanRender));
    assert(!widow::has_state(state, widow::StateFlag::CanShowUv));
    assert(!widow::has_state(
        state, widow::StateFlag::TextureCompanionAttachable));
    assert(!widow::has_state(
        state, widow::StateFlag::TextureCompanionAttached));
    assert(!widow::has_state(state, widow::StateFlag::CanAddModelPart));
    assert(!widow::has_state(state, widow::StateFlag::CanStageCompanion));

    model.renderable = true;
    slots[0] = std::numeric_limits<std::uint32_t>::max();
    state = widow::evaluate_model_session(model);
    assert(widow::has_state(state, widow::StateFlag::CanRender));
    assert(!widow::has_state(
        state, widow::StateFlag::TextureCompanionAttachable));

    slots[0] = 2U;
    // A partially mapped model must not enable the whole-bundle action.
    mesh.indices.insert(mesh.indices.end(), {0U, 1U, 2U});
    slots.push_back(std::numeric_limits<std::uint32_t>::max());
    model.triangle_texture_slots = slots;
    state = widow::evaluate_model_session(model);
    assert(!widow::has_state(state, widow::StateFlag::TextureCompanionAttachable));
    assert(!widow::has_state(state, widow::StateFlag::CanShowUv));
    slots.pop_back();
    mesh.indices.resize(3U);
    model.triangle_texture_slots = slots;

    mesh.uv0[0].u = std::numeric_limits<float>::quiet_NaN();
    state = widow::evaluate_model_session(model);
    assert(!widow::has_state(state, widow::StateFlag::TextureCompanionAttachable));
    assert(!widow::has_state(state, widow::StateFlag::CanShowUv));
    mesh.uv0[0].u = 0.0F;
    mesh.indices[0] = 99U;
    state = widow::evaluate_model_session(model);
    assert(!widow::has_state(state, widow::StateFlag::TextureCompanionAttachable));
    assert(!widow::has_state(state, widow::StateFlag::CanShowUv));
    mesh.indices[0] = 0U;

    model.capabilities &= ~capability(ResourceCapability::TextureBinding);
    state = widow::evaluate_model_session(model);
    assert(!widow::has_state(state, widow::StateFlag::HasTextureBindings));
    assert(!widow::has_state(
        state, widow::StateFlag::TextureCompanionAttachable));

    // Removing MOD's skeletal-model capability removes model-part/companion
    // actions even though generic geometry can still render. Android must consume
    // these Spider flags instead of inferring a MOD context from file names.
    model.capabilities &= ~capability(ResourceCapability::SkeletalSkinning);
    state = widow::evaluate_model_session(model);
    assert(widow::has_state(state, widow::StateFlag::CanRender));
    assert(!widow::has_state(state, widow::StateFlag::CanAddModelPart));
    assert(!widow::has_state(state, widow::StateFlag::CanStageCompanion));

    // Structural PTX-like sessions are native child-browser state, not a Java
    // combination of capabilities and child count.
    widow::ModelSessionView container{
        .capabilities = capability(ResourceCapability::Inspection) |
            ResourceCapability::ChildResources,
        .renderable = false,
        .render_mesh = nullptr,
        .triangle_texture_slots = {},
        .hierarchy_available = false,
        .image_preview_available = false,
        .child_resource_count = 4U,
        .texture_companion_attached = false,
        .png_export_available = true,
    };
    state = widow::evaluate_model_session(container);
    assert(widow::has_state(state, widow::StateFlag::CanInspect));
    assert(widow::has_state(state, widow::StateFlag::HasChildResources));
    assert(widow::has_state(state, widow::StateFlag::ChildBrowserMode));
    assert(widow::has_state(state, widow::StateFlag::CanExportPng));
    assert(!widow::has_state(state, widow::StateFlag::CanAddModelPart));
    assert(!widow::has_state(state, widow::StateFlag::CanStageCompanion));

    // A direct DDS/texture preview is not a child browser and can still expose
    // the same PNG export action.
    widow::ModelSessionView image{
        .capabilities = capability(ResourceCapability::Inspection) |
            ResourceCapability::ImagePreview,
        .renderable = false,
        .render_mesh = nullptr,
        .triangle_texture_slots = {},
        .hierarchy_available = false,
        .image_preview_available = true,
        .child_resource_count = 0U,
        .texture_companion_attached = false,
        .png_export_available = true,
    };
    state = widow::evaluate_model_session(image);
    assert(widow::has_state(state, widow::StateFlag::CanPreviewImage));
    assert(!widow::has_state(state, widow::StateFlag::ChildBrowserMode));
    assert(widow::has_state(state, widow::StateFlag::CanExportPng));
    assert(!widow::has_state(state, widow::StateFlag::CanAddModelPart));
    assert(!widow::has_state(state, widow::StateFlag::CanStageCompanion));

    // Android/desktop diagnostics consume one native-produced projection rather
    // than reconstructing capability policy in a platform shell.
    Session diagnostic;
    diagnostic.probe.family = "MOD";
    diagnostic.probe.domain = "model";
    diagnostic.probe.support = "read";
    diagnostic.probe.evidence = "test-evidence";
    diagnostic.probe.content_confirmed = true;
    diagnostic.capabilities = capabilities;
    diagnostic.renderable = true;
    diagnostic.render_mesh = mesh;
    diagnostic.render_triangle_texture_slots = {2U};
    diagnostic.detail = "line one\nline two";

    const auto diagnostic_text = describe_session_diagnostics(&diagnostic);
    assert(diagnostic_text.find("family=MOD") != std::string::npos);
    assert(diagnostic_text.find("domain=model") != std::string::npos);
    assert(diagnostic_text.find("support=read") != std::string::npos);
    assert(diagnostic_text.find("evidence=test-evidence") != std::string::npos);
    assert(diagnostic_text.find("content_confirmed=true") != std::string::npos);
    assert(diagnostic_text.find("detail=line one line two") != std::string::npos);
    assert(diagnostic_text.find("CanRender") != std::string::npos);
    assert(diagnostic_text.find("CanAddModelPart") != std::string::npos);
    assert(describe_session_diagnostics(nullptr).empty());

    return 0;
}
