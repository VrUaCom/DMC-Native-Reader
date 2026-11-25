#include "dmcresource/motion/model_cloth.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

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
using V3 = std::array<float, 3>;

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

[[nodiscard]] V3 sub(const V3& a, const V3& b) noexcept { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
[[nodiscard]] V3 add(const V3& a, const V3& b) noexcept { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
[[nodiscard]] V3 scale(const V3& a, float s) noexcept { return {a[0] * s, a[1] * s, a[2] * s}; }
[[nodiscard]] float length(const V3& a) noexcept { return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
[[nodiscard]] V3 cross(const V3& a, const V3& b) noexcept {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
[[nodiscard]] V3 normalized(const V3& a) noexcept {
    const float l = length(a);
    return l > 1.0e-6F ? scale(a, 1.0F / l) : V3{1.0F, 0.0F, 0.0F};
}

// The palette's skinning over the model's meshes (rest vertices in
// materialized order); vertices without influence follow the root.
[[nodiscard]] bool skin_vertices(const Session& model, const Mesh& rest, std::span<const Mat> palette, std::size_t root,
                                 std::vector<Vec3>* out) {
    const auto count = palette.size();
    out->resize(rest.vertices.size());
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
            if (vertex >= out->size()) return false;
            const auto& source = rest.vertices[vertex];
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
                                   : transform_row(source, palette[root < count ? root : 0U]);
            if (!std::isfinite(skinned.x) || !std::isfinite(skinned.y) || !std::isfinite(skinned.z)) return false;
            (*out)[vertex] = skinned;
        }
    }
    return vertex == out->size();
}

[[nodiscard]] bool fill_skin(const Session& model, ModelSkin* out) {
    const auto& rig = model.scene.rig;
    if (rig == nullptr || rig->node_count() != model.scene.nodes.size()) return false;
    const auto count = rig->node_count();
    const auto binding = animation::project_animation_binding(rig->domain);
    const auto inverse_rest = world::build_model_space_inverse_rest_matrices(rig->domain);
    if (!binding || binding->by_node_index.size() != count || !inverse_rest || inverse_rest->size() != count) {
        return false;
    }
    if (!materialize_render_scene(model.scene, &out->rest) ||
        out->rest.vertices.size() != model.render_mesh.vertices.size()) {
        return false;
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
    out->order.clear();
    for (const auto node : binding->node_at_order_position) {
        if (node >= count) return false;
        out->order.push_back(node);
    }
    return out->order.size() == count;
}

[[nodiscard]] float to_float(std::string_view s) {
    try {
        return std::stof(std::string{s});
    } catch (...) {
        return 0.0F;
    }
}

[[nodiscard]] std::vector<std::string_view> tokens(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t i = 0U;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        std::size_t j = i;
        while (j < line.size() && !std::isspace(static_cast<unsigned char>(line[j]))) ++j;
        if (j > i) out.push_back(line.substr(i, j - i));
        i = j;
    }
    return out;
}

}  // namespace

std::shared_ptr<ModelSkin> make_model_skin(const Session& model) {
    try {
        auto out = std::make_shared<ModelSkin>();
        return fill_skin(model, out.get()) ? out : nullptr;
    } catch (...) {
        return nullptr;
    }
}

bool skin_model(const Session& model, const ModelSkin& skin, std::span<const Matrix4> worlds,
                std::vector<Vec3>* vertices) {
    if (vertices == nullptr || worlds.size() != skin.inverse_rest.size() || skin.order.empty()) return false;
    try {
        std::vector<Mat> palette(worlds.size());
        for (std::size_t node = 0U; node < worlds.size(); ++node) {
            palette[node] = multiply(skin.inverse_rest[node].values, worlds[node].values);
        }
        return skin_vertices(model, skin.rest, palette, skin.order.front(), vertices);
    } catch (...) {
        return false;
    }
}

std::shared_ptr<ModelCloth> make_model_cloth(const Session& model, std::string_view clt_text) {
    try {
        auto state = make_cloth_state(model.scene.nodes.size(), clt_text);
        if (state == nullptr) return nullptr;
        auto out = std::make_shared<ModelCloth>();
        if (!fill_skin(model, &out->skin)) return nullptr;
        out->state = std::move(*state);
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
        const auto& skin = cloth.skin;
        const auto count = model.scene.nodes.size();
        if (skin.order.size() != count || state.sim.size() != count || state.axis_by_node.size() != count) {
            return false;
        }
        // The motion's worlds placed in the world, and the animated locals.
        std::vector<Mat> animated(count), local(count), current(count);
        for (std::size_t node = 0U; node < count; ++node) {
            animated[node] = multiply(model.scene.nodes[node].world.values, placement.values);
        }
        for (const auto node : skin.order) {
            const auto parent = skin.parent[node];
            local[node] = parent >= 0 && static_cast<std::size_t>(parent) < count
                ? multiply(animated[node], rigid_inverse(animated[static_cast<std::size_t>(parent)]))
                : animated[node];
        }
        const bool step = steps > 0U || !state.initialized;
        const std::uint32_t passes = step ? std::max<std::uint32_t>(steps, 1U) : 1U;
        for (std::uint32_t pass = 0U; pass < passes; ++pass) {
            for (const auto node : skin.order) {
                const auto parent = skin.parent[node];
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
                                                skin.rest_length[node], 1.0F);
                if (!finite(current[node])) {
                    // Diverged: restart this node from its target.
                    state.sim[node] = target;
                    state.velocity[node] = {};
                    current[node] = target;
                }
            }
            state.initialized = true;
        }
        std::vector<Matrix4> worlds(count);
        for (std::size_t node = 0U; node < count; ++node) worlds[node].values = current[node];
        return skin_model(model, skin, worlds, world_vertices);
    } catch (...) {
        return false;
    }
}

// ---- ropes -----------------------------------------------------------------------

bool looks_like_rope(std::string_view text) noexcept {
    const auto head = text.substr(0U, std::min<std::size_t>(text.size(), 256U));
    return head.find("ClothSim1D") != std::string_view::npos;
}

RopeParams parse_rope(std::string_view text) {
    RopeParams out;
    std::size_t at = 0U;
    while (at < text.size()) {
        auto end = text.find_first_of("\r\n", at);
        if (end == std::string_view::npos) end = text.size();
        const auto t = tokens(text.substr(at, end - at));
        at = end + 1U;
        if (t.empty()) continue;
        if (t[0] == "Gravity" && t.size() >= 4U) {
            out.gravity = {to_float(t[1]), to_float(t[2]), to_float(t[3])};
        } else if (t[0] == "SpringForce" && t.size() >= 2U) {
            out.spring = to_float(t[1]);
        } else if (t[0] == "Damping" && t.size() >= 2U) {
            out.damping = to_float(t[1]);
        } else if (t[0] == "MaxSpeed" && t.size() >= 2U) {
            out.max_speed = to_float(t[1]);
        } else if (t[0] == "FloorLevel" && t.size() >= 2U) {
            out.has_floor = true;
            out.floor = to_float(t[1]);
        } else if (t[0] == "Cut" && t.size() >= 3U) {
            out.cut = static_cast<int>(to_float(t[1]));
        }
    }
    return out;
}

std::shared_ptr<Rope> make_rope(const Session& model, std::string_view clt_text) {
    try {
        if (!looks_like_rope(clt_text)) return nullptr;
        auto rope = std::make_shared<Rope>();
        if (!fill_skin(model, &rope->skin)) return nullptr;
        rope->params = parse_rope(clt_text);
        const auto count = rope->skin.parent.size();
        // Node 1 and its only children after it.
        for (std::uint32_t node = 1U; node < count;) {
            rope->chain.push_back(node);
            std::uint32_t next = 0U;
            for (std::uint32_t c = 0U; c < count; ++c) {
                if (rope->skin.parent[c] == static_cast<std::int32_t>(node)) {
                    next = c;
                    break;
                }
            }
            if (next == 0U) break;
            node = next;
        }
        if (rope->chain.size() < 2U) return nullptr;
        for (std::size_t k = 0; k + 1U < rope->chain.size(); ++k) {
            rope->rest.push_back(rope->skin.rest_length[rope->chain[k + 1U]]);
        }
        const float x = model.scene.nodes[rope->chain[1]].local.values[12];
        rope->axis_sign = x < 0.0F ? -1.0F : 1.0F;
        return rope;
    } catch (...) {
        return nullptr;
    }
}

bool pose_rope(const Session& model, Rope& rope, const Vec3* first, const Vec3* last, std::uint32_t steps,
               std::vector<Vec3>* world_vertices) {
    if (world_vertices == nullptr) return false;
    try {
        const std::size_t n = rope.chain.size();
        const auto& prm = rope.params;
        const auto pin = [](const Vec3* v) { return V3{v->x, v->y, v->z}; };
        const bool held_anchor = last == nullptr && rope.hold_last && rope.has_anchor;
        if (!rope.initialized) {
            rope.point.assign(n, {});
            if (first != nullptr && (last != nullptr || held_anchor)) {
                const auto a = pin(first), b = last != nullptr ? pin(last) : rope.anchor;
                for (std::size_t k = 0; k < n; ++k) {
                    const float t = n > 1U ? static_cast<float>(k) / static_cast<float>(n - 1U) : 0.0F;
                    rope.point[k] = add(a, scale(sub(b, a), t));
                }
            } else {
                // From the pinned end (or the model's place), at rest.
                const auto& base = model.scene.nodes[rope.chain.front()].world.values;
                const V3 origin = first != nullptr ? pin(first) : last != nullptr ? pin(last)
                                                                  : V3{base[12], base[13], base[14]};
                for (std::size_t k = 0; k < n; ++k) {
                    const auto& w = model.scene.nodes[rope.chain[k]].world.values;
                    rope.point[k] = add(origin, V3{w[12] - base[12], w[13] - base[13], w[14] - base[14]});
                }
            }
            rope.previous = rope.point;
            steps = std::max<std::uint32_t>(steps, 1U);
            rope.initialized = true;
        }
        if (last == nullptr && rope.hold_last && !rope.has_anchor) {
            rope.anchor = rope.point.back();
            rope.has_anchor = true;
        }
        const V3 held = rope.anchor;
        const bool pin_last = last != nullptr || rope.hold_last;
        const auto last_point = [&]() { return last != nullptr ? pin(last) : held; };
        const float k_spring = std::clamp(prm.spring, 0.05F, 1.0F);
        for (std::uint32_t s = 0; s < steps; ++s) {
            for (std::size_t k = 0; k < n; ++k) {
                if (k == 0U && first != nullptr) {
                    rope.point[k] = rope.previous[k] = pin(first);
                    continue;
                }
                if (k + 1U == n && pin_last) {
                    rope.point[k] = rope.previous[k] = last_point();
                    continue;
                }
                auto v = scale(sub(rope.point[k], rope.previous[k]), prm.damping);
                const float speed = length(v);
                if (prm.max_speed > 0.0F && speed > prm.max_speed) v = scale(v, prm.max_speed / speed);
                rope.previous[k] = rope.point[k];
                rope.point[k] = add(add(rope.point[k], v), prm.gravity);
                if (prm.has_floor) rope.point[k][1] = std::max(rope.point[k][1], prm.floor);
            }
            // Keep the rest distances: SpringForce of the stretch back a pass.
            for (int pass = 0; pass < 16; ++pass) {
                for (std::size_t k = 0; k + 1U < n; ++k) {
                    if (static_cast<int>(k) == prm.cut) continue;
                    const bool pinned0 = k == 0U && first != nullptr;
                    const bool pinned1 = k + 2U == n && pin_last;
                    const float w0 = pinned0 ? 0.0F : 1.0F, w1 = pinned1 ? 0.0F : 1.0F;
                    if (w0 + w1 == 0.0F) continue;
                    const auto d = sub(rope.point[k + 1U], rope.point[k]);
                    const float l = length(d);
                    if (l < 1.0e-6F) continue;
                    const auto fix = scale(d, k_spring * (l - rope.rest[k]) / l / (w0 + w1));
                    rope.point[k] = add(rope.point[k], scale(fix, w0));
                    rope.point[k + 1U] = sub(rope.point[k + 1U], scale(fix, w1));
                }
            }
        }
        // Each chain node points along the rope (its rest steps run along x).
        const auto count = rope.skin.parent.size();
        std::vector<Matrix4> worlds(count);
        for (std::size_t k = 0; k < n; ++k) {
            V3 dir{};
            const bool forward = k + 1U < n && static_cast<int>(k) != prm.cut;
            if (forward) dir = sub(rope.point[k + 1U], rope.point[k]);
            else if (k > 0U) dir = sub(rope.point[k], rope.point[k - 1U]);
            const auto x = scale(normalized(dir), rope.axis_sign);
            V3 up{0.0F, 1.0F, 0.0F};
            if (std::fabs(x[1]) > 0.95F) up = {0.0F, 0.0F, 1.0F};
            const auto z = normalized(cross(x, up));
            const auto y = cross(z, x);
            auto& m = worlds[rope.chain[k]].values;
            m = {x[0], x[1], x[2], 0.0F, y[0], y[1], y[2], 0.0F, z[0], z[1], z[2], 0.0F,
                 rope.point[k][0], rope.point[k][1], rope.point[k][2], 1.0F};
        }
        // Nodes off the chain (the root) stand with its first point.
        for (std::size_t node = 0U; node < count; ++node) {
            if (std::find(rope.chain.begin(), rope.chain.end(), static_cast<std::uint32_t>(node)) == rope.chain.end()) {
                worlds[node] = worlds[rope.chain.front()];
            }
        }
        return skin_model(model, rope.skin, worlds, world_vertices);
    } catch (...) {
        return false;
    }
}

}  // namespace dmcresource::motion
