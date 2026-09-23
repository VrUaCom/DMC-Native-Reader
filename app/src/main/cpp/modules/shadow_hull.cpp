#include "dmcresource/shadow_hull.h"

#include <algorithm>
#include <cmath>
#include <span>

#include "dmc_rengine/formats/mod/world_transform.hpp"
#include "dmc_rengine/formats/shw.hpp"
#include "dmcresource/motion/skeleton_rig.h"
#include "dmcresource/resource_session.h"

namespace dmcresource::shadow {
namespace {

namespace world = dmc::rengine::formats::mod::world_transform;

[[nodiscard]] Vec3 transform_row(const Vec3& p, const std::array<float, 16>& m) noexcept {
    return {p.x * m[0] + p.y * m[4] + p.z * m[8] + m[12],
            p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13],
            p.x * m[2] + p.y * m[6] + p.z * m[10] + m[14]};
}

[[nodiscard]] bool finite(const Vec3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

}  // namespace

std::size_t HullSet::vertex_count() const noexcept {
    std::size_t total = 0U;
    for (const auto& hull : hulls) total += hull.vertices.size();
    return total;
}

std::size_t HullSet::triangle_count() const noexcept {
    std::size_t total = 0U;
    for (const auto& hull : hulls) total += hull.indices.size() / 3U;
    return total;
}

std::size_t HullSet::closed_hulls() const noexcept {
    std::size_t closed = 0U;
    for (const auto& hull : hulls) {
        if (hull.vertices.size() >= 4U &&
            hull.indices.size() / 3U == 2U * hull.vertices.size() - 4U) {
            ++closed;
        }
    }
    return closed;
}

std::uint32_t HullSet::max_selector() const noexcept {
    std::uint32_t highest = 0U;
    for (const auto& hull : hulls) {
        for (const auto selector : hull.selectors) highest = std::max<std::uint32_t>(highest, selector);
    }
    return highest;
}

std::optional<HullSet> parse_hulls(const std::uint8_t* bytes, std::size_t size) noexcept {
    try {
        if (bytes == nullptr || size < 0x20U) return std::nullopt;
        const auto parsed = dmc::rengine::formats::shw::Parser::parse(
            std::span<const std::byte>{reinterpret_cast<const std::byte*>(bytes), size});
        if (!parsed.ok()) return std::nullopt;
        HullSet out;
        out.version = parsed.document.header.version;
        out.node_count = bytes[0x11];
        for (const auto& source : parsed.document.hulls) {
            Hull hull;
            hull.vertices.reserve(source.vertices.size());
            for (const auto& v : source.vertices) hull.vertices.push_back({v.x, v.y, v.z});
            hull.selectors = source.transform_selectors;
            for (const auto& triangle : source.triangles) {
                for (const auto index : triangle.vertices) {
                    if (index >= hull.vertices.size()) return std::nullopt;
                    hull.indices.push_back(index);
                }
            }
            for (const auto& adjacency : source.adjacency) {
                hull.adjacency.push_back(adjacency.neighbors);
            }
            if (hull.selectors.size() != hull.vertices.size()) return std::nullopt;
            out.hulls.push_back(std::move(hull));
        }
        return out;
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<Vec3> posed_hull_triangles(const Session& session) {
    std::vector<Vec3> out;
    for (const auto& binding : session.shadow_bindings) {
        if (binding.rig == nullptr || binding.rig->node_count() != binding.node_count ||
            binding.node_begin + binding.node_count > session.scene.nodes.size()) {
            continue;
        }
        const auto inverse_rest =
            world::build_model_space_inverse_rest_matrices(binding.rig->domain);
        if (!inverse_rest || inverse_rest->size() != binding.node_count) continue;
        std::vector<world::Matrix4f> palette(binding.node_count);
        for (std::size_t node = 0U; node < binding.node_count; ++node) {
            world::Matrix4f current{};
            current.values = session.scene.nodes[binding.node_begin + node].world.values;
            palette[node] = world::multiply_dmc3_matrices((*inverse_rest)[node], current);
        }
        for (const auto& hull : binding.hulls.hulls) {
            std::vector<Vec3> posed(hull.vertices.size());
            bool ok = true;
            for (std::size_t v = 0U; v < hull.vertices.size() && ok; ++v) {
                const auto selector = hull.selectors[v];
                ok = selector < palette.size();
                if (ok) posed[v] = transform_row(hull.vertices[v], palette[selector].values);
                ok = ok && finite(posed[v]);
            }
            if (!ok) continue;
            for (const auto index : hull.indices) out.push_back(posed[index]);
        }
    }
    return out;
}

std::vector<Vec3> floor_shadow_triangles(const Session& session, Vec3 light, float floor_y) {
    auto triangles = posed_hull_triangles(session);
    if (!(light.y < -1.0e-4F)) return {};
    for (auto& p : triangles) {
        const float t = (floor_y - p.y) / light.y;
        p = {p.x + light.x * t, floor_y, p.z + light.z * t};
    }
    return triangles;
}

}  // namespace dmcresource::shadow
