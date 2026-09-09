#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>

#include "dmcresource/resource_capabilities.h"
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
        capability(ResourceCapability::Geometry) |
        ResourceCapability::UvCoordinates |
        ResourceCapability::TextureBinding;

    std::vector<std::uint32_t> slots{2U};
    widow::ModelSessionView model{
        .capabilities = capabilities,
        .renderable = true,
        .render_mesh = &mesh,
        .triangle_texture_slots = slots,
        .texture_companion_attached = false,
    };

    auto state = widow::evaluate_model_session(model);
    assert(widow::has_state(
        state, widow::StateFlag::TextureCompanionAttachable));
    assert(!widow::has_state(
        state, widow::StateFlag::TextureCompanionAttached));

    model.texture_companion_attached = true;
    state = widow::evaluate_model_session(model);
    assert(widow::has_state(
        state, widow::StateFlag::TextureCompanionAttachable));
    assert(widow::has_state(
        state, widow::StateFlag::TextureCompanionAttached));

    // A diagnostic string can never make the state attached; only typed native
    // session state participates in Black Widow evaluation.
    model.renderable = false;
    state = widow::evaluate_model_session(model);
    assert(state == 0U);

    model.renderable = true;
    slots[0] = std::numeric_limits<std::uint32_t>::max();
    state = widow::evaluate_model_session(model);
    assert(state == 0U);

    slots[0] = 2U;
    model.capabilities &= ~capability(ResourceCapability::TextureBinding);
    state = widow::evaluate_model_session(model);
    assert(state == 0U);

    return 0;
}
