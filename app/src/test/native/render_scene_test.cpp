#include "dmcresource/view_renderer.h"

#include <cassert>
#include <cmath>
#include <cstdint>

namespace {

[[nodiscard]] bool near(float actual, float expected,
                        float epsilon = 0.0001F) noexcept {
    return std::fabs(actual - expected) <= epsilon;
}

dmcresource::Mesh triangle(float z = 0.0F) {
    dmcresource::Mesh mesh;
    mesh.vertices = {
        {0.0F, 0.0F, z},
        {1.0F, 0.0F, z},
        {0.0F, 1.0F, z},
    };
    mesh.indices = {0U, 1U, 2U};
    return mesh;
}

}  // namespace

int main() {
    using namespace dmcresource;

    RenderScene scene;
    RenderNode node;
    node.name = "translated";
    node.world.values[12] = 10.0F;
    node.world.values[13] = 20.0F;
    node.world.values[14] = 30.0F;
    scene.nodes.push_back(node);

    MeshPrimitive bound;
    bound.name = "bound";
    bound.mesh = triangle();
    bound.node_index = 0;
    scene.meshes.push_back(bound);

    MeshPrimitive unbound;
    unbound.name = "unbound";
    unbound.mesh = triangle(2.0F);
    unbound.node_index = -1;
    scene.meshes.push_back(unbound);

    Mesh materialized;
    assert(materialize_render_scene(scene, &materialized));
    assert(materialized.vertices.size() == 6U);
    assert(materialized.indices.size() == 6U);

    assert(near(materialized.vertices[0].x, 10.0F));
    assert(near(materialized.vertices[0].y, 20.0F));
    assert(near(materialized.vertices[0].z, 30.0F));
    assert(near(materialized.vertices[1].x, 11.0F));
    assert(near(materialized.vertices[1].y, 20.0F));

    // Unbound compatibility primitives stay in local/model space.
    assert(near(materialized.vertices[3].x, 0.0F));
    assert(near(materialized.vertices[3].y, 0.0F));
    assert(near(materialized.vertices[3].z, 2.0F));
    assert(materialized.indices[0] == 0U);
    assert(materialized.indices[1] == 1U);
    assert(materialized.indices[2] == 2U);
    assert(materialized.indices[3] == 3U);
    assert(materialized.indices[4] == 4U);
    assert(materialized.indices[5] == 5U);

    // DMC3 matrices use row-vector convention. A +90 degree Z rotation maps
    // local (1,0,0) to (0,1,0), then row-3 translation is added.
    RenderScene rotated;
    RenderNode rotated_node;
    rotated_node.world.values = {
        0.0F, 1.0F, 0.0F, 0.0F,
       -1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        5.0F, 6.0F, 7.0F, 1.0F,
    };
    rotated.nodes.push_back(rotated_node);
    MeshPrimitive rotated_primitive;
    rotated_primitive.mesh.vertices = {{1.0F, 0.0F, 0.0F}};
    rotated_primitive.mesh.indices = {0U, 0U, 0U};
    rotated_primitive.node_index = 0;
    rotated.meshes.push_back(rotated_primitive);

    Mesh rotated_mesh;
    assert(materialize_render_scene(rotated, &rotated_mesh));
    assert(rotated_mesh.vertices.size() == 1U);
    assert(near(rotated_mesh.vertices[0].x, 5.0F));
    assert(near(rotated_mesh.vertices[0].y, 7.0F));
    assert(near(rotated_mesh.vertices[0].z, 7.0F));

    RenderScene invalid = scene;
    invalid.meshes[0].node_index = 99;
    Mesh rejected;
    assert(!materialize_render_scene(invalid, &rejected));

    // Rasterization consumes the cached materialized mesh. Scene projection is
    // intentionally not hidden in render_view(), so touch frames do not repeat
    // all world transforms and allocations.
    const auto image = render_view(materialized, 64, 64, ViewState{});
    assert(image.width == 64);
    assert(image.height == 64);
    assert(image.pixels.size() == 64U * 64U * 4U);

    return 0;
}
