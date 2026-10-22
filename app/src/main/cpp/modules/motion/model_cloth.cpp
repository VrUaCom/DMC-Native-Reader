#include "dmcresource/motion/model_cloth.h"

#include <algorithm>
#include <cmath>

#include "dmc_rengine/analysis/mod/animation_binding.hpp"
#include "dmc_rengine/formats/mod/world_transform.hpp"
#include "dmcresource/motion/skeleton_rig.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/scene_projection.h"

namespace dmcresource::motion {
namespace {

namespace world = dmc::rengine::formats::mod::world_transform;
namespace animation = dmc::rengine::analysis::mod;

using Mat = std::array<float, 16>;

[[nodiscard]] Mat multiply(const Mat& left, const Mat& right) noexcept {
    world::Matrix4f a{}, b{};
    a.values = left;
    b.values = right;
    return world::multiply_dmc3_matrices(a, b).values;
}

[[nodiscard]] Mat rigid_inverse(const Mat& m) noexcept {
    world::Matrix4f a{};
    a.values = m;
    return world::rigid_inverse_dmc3_matrix(a).values;
}

[[nodiscard]] Vec3 transform_row(const Vec3& p, const Mat& m) noexcept {
    return {p.x * m[0] + p.y * m[4] + p.z * m[8] + m[12], p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13],
            p.x * m[2] + p.y * m[6] + p.z * m[10] + m[14]};
}

[[nodiscard]] bool finite(const Mat& m) noexcept {
    return std::all_of(m.begin(), m.end(), [](float v) { return std::isfinite(v); });
}

}  // namespace

std::shared_ptr<ModelCloth> make_model_cloth(const Session& model, std::string_view clt_text) {
    try {
        const auto& rig = model.scene.rig;
        if (rig == nullptr || rig->node_count() != model.scene.nodes.size()) return nullptr;
        const auto count = rig->node_count();
        auto state = make_cloth_state(count, clt_text);
        if (state == nullptr) return nullptr;
        const auto binding = animation::project_animation_binding(rig->domain);
        const auto inverse_rest = world::build_model_space_inverse_rest_matrices(rig->domain);
        if (!binding || binding->by_node_index.size() != count || !inverse_rest || inverse_rest->size() != count) {
            return nullptr;
        }
        auto out = std::make_shared<ModelCloth>();
        out->state = std::move(*state);
        if (!materialize_render_scene(model.scene, &out->rest) ||
            out->rest.vertices.size() != model.render_mesh.vertices.size()) {
            return nullptr;
        }
        out->inverse_rest.resize(count);
        out->rest_length.resize(count);
        out->parent.resize(count);
        for (std::size_t node = 0U; node < count; ++node) {
            out->inverse_rest[node].values = (*inverse_rest)[node].values;
            const auto local = world::build_local_matrix(rig->domain.local_transform_records_by_node_index[node]);
            const auto& t = local.values;
            out->rest_length[node] = std::sqrt(t[12] * t[12] + t[13] * t[13] + t[14] * t[14]);
            out->parent[node] = binding->by_node_index[node].parent_node_index;
        }
        for (const auto node : binding->node_at_order_position) {
            if (node >= count) return nullptr;
            out->order.push_back(node);
        }
        if (out->order.size() != count) return nullptr;
        return out;
    } catch (...) {
        return nullptr;
    }
}

bool pose_model_cloth(const Session& model, const Matrix4& placement, ModelCloth& cloth, std::uint32_t steps,
                      std::vector<Vec3>* world_vertices) {
    if (world_vertices == nullptr) return false;
    try {
        auto& state = cloth.state;
        const auto count = model.scene.nodes.size();
        if (cloth.order.size() != count || state.sim.size() != count || state.axis_by_node.size() != count) {
            return false;
        }
        // The motion's worlds placed in the world, and the animated locals.
        std::vector<Mat> animated(count), local(count), current(count);
        for (std::size_t node = 0U; node < count; ++node) {
            animated[node] = multiply(model.scene.nodes[node].world.values, placement.values);
        }
        for (const auto node : cloth.order) {
            const auto parent = cloth.parent[node];
            local[node] = parent >= 0 && static_cast<std::size_t>(parent) < count
                ? multiply(animated[node], rigid_inverse(animated[static_cast<std::size_t>(parent)]))
                : animated[node];
        }
        const bool step = steps > 0U || !state.initialized;
        const std::uint32_t passes = step ? std::max<std::uint32_t>(steps, 1U) : 1U;
        for (std::uint32_t pass = 0U; pass < passes; ++pass) {
            for (const auto node : cloth.order) {
                const auto parent = cloth.parent[node];
                if (parent < 0 || static_cast<std::size_t>(parent) >= count) {
                    current[node] = animated[node];
                    continue;
                }
                const auto& parent_world = current[static_cast<std::size_t>(parent)];
                const auto target = multiply(local[node], parent_world);
                if (state.axis_by_node[node] < 0) {
                    current[node] = target;
                    continue;
                }
                if (!state.initialized) {
                    state.sim[node] = target;
                    state.velocity[node] = {};
                }
                if (!step) {
                    current[node] = state.sim[node];
                    continue;
                }
                const auto wind_parent =
                    static_cast<std::size_t>(std::max(state.params_for(node).wind_parent, 0));
                const auto& wind_world = wind_parent < count ? current[wind_parent] : placement.values;
                current[node] = step_cloth_node(state, node, target, parent_world, wind_world,
                                                cloth.rest_length[node], 1.0F);
                if (!finite(current[node])) {
                    // Diverged: restart this node from its target.
                    state.sim[node] = target;
                    state.velocity[node] = {};
                    current[node] = target;
                }
            }
            state.initialized = true;
        }

        // Skin: palette = inverse rest x current world.
        std::vector<Mat> palette(count);
        for (std::size_t node = 0U; node < count; ++node) {
            palette[node] = multiply(cloth.inverse_rest[node].values, current[node]);
        }
        const std::size_t root = cloth.order.front();
        auto& out = *world_vertices;
        out.resize(cloth.rest.vertices.size());
        std::size_t vertex = 0U;
        for (std::size_t primitive = 0U; primitive < model.scene.meshes.size(); ++primitive) {
            const auto& mesh = model.scene.meshes[primitive].mesh;
            const SkinBinding* binding = nullptr;
            for (const auto& candidate : model.scene.skins) {
                if (candidate.mesh_primitive == primitive && candidate.vertices.size() == mesh.vertices.size()) {
                    binding = &candidate;
                    break;
                }
            }
            for (std::size_t i = 0U; i < mesh.vertices.size(); ++i, ++vertex) {
                if (vertex >= out.size()) return false;
                const auto& source = cloth.rest.vertices[vertex];
                Vec3 skinned{};
                float total = 0.0F;
                if (binding != nullptr) {
                    for (const auto& joint : binding->vertices[i].influences) {
                        if (joint.node_index >= count || !(joint.weight > 0.0F)) continue;
                        const auto moved = transform_row(source, palette[joint.node_index]);
                        skinned.x += moved.x * joint.weight;
                        skinned.y += moved.y * joint.weight;
                        skinned.z += moved.z * joint.weight;
                        total += joint.weight;
                    }
                }
                skinned = total > 0.0F ? Vec3{skinned.x / total, skinned.y / total, skinned.z / total}
                                       : transform_row(source, palette[root]);
                if (!std::isfinite(skinned.x) || !std::isfinite(skinned.y) || !std::isfinite(skinned.z)) return false;
                out[vertex] = skinned;
            }
        }
        return vertex == out.size();
    } catch (...) {
        return false;
    }
}

}  // namespace dmcresource::motion
