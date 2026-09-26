#include "dmcresource/collision_debug.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "dmcresource/resource_session.h"

namespace dmcresource::collision {
namespace {

constexpr float kPi = 3.14159265358979F;

void add_ring(WireMesh& mesh, const Vec3& centre, const Vec3& u, const Vec3& v, float radius,
              int segments) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    for (int i = 0; i < segments; ++i) {
        const float t = 2.0F * kPi * static_cast<float>(i) / static_cast<float>(segments);
        const float c = std::cos(t) * radius, s = std::sin(t) * radius;
        mesh.vertices.push_back({centre.x + u.x * c + v.x * s, centre.y + u.y * c + v.y * s,
                                 centre.z + u.z * c + v.z * s});
        mesh.lines.push_back(base + static_cast<std::uint32_t>(i));
        mesh.lines.push_back(base + static_cast<std::uint32_t>((i + 1) % segments));
    }
}

void add_line(WireMesh& mesh, const Vec3& a, const Vec3& b) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(a);
    mesh.vertices.push_back(b);
    mesh.lines.push_back(base);
    mesh.lines.push_back(base + 1U);
}

[[nodiscard]] Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] Vec3 normalized(const Vec3& a) noexcept {
    const float l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    return l > 1.0e-6F ? Vec3{a.x / l, a.y / l, a.z / l} : Vec3{0.0F, 1.0F, 0.0F};
}

WireMesh make_sphere() {
    WireMesh m;
    // Latitude rings and three great circles (the at000 UV sphere outline).
    for (int k = -3; k <= 3; ++k) {
        const float phi = static_cast<float>(k) * kPi / 8.0F;
        add_ring(m, {0.0F, std::sin(phi), 0.0F}, {1, 0, 0}, {0, 0, 1}, std::cos(phi), 16);
    }
    add_ring(m, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, 1.0F, 16);
    add_ring(m, {0, 0, 0}, {0, 0, 1}, {0, 1, 0}, 1.0F, 16);
    return m;
}

WireMesh make_box() {
    WireMesh m;
    // Corner order of 0x1405CEC60 is not needed for edges: +-1 on each axis.
    for (int k = 0; k < 8; ++k) {
        m.vertices.push_back({k & 1 ? 1.0F : -1.0F, k & 2 ? 1.0F : -1.0F, k & 4 ? 1.0F : -1.0F});
    }
    for (std::uint32_t k = 0U; k < 8U; ++k) {
        for (const std::uint32_t bit : {1U, 2U, 4U}) {
            if ((k & bit) == 0U) {
                m.lines.push_back(k);
                m.lines.push_back(k | bit);
            }
        }
    }
    return m;
}

WireMesh make_cylinder() {
    WireMesh m;
    add_ring(m, {0, 1, 0}, {1, 0, 0}, {0, 0, 1}, 1.0F, 8);
    add_ring(m, {0, -1, 0}, {1, 0, 0}, {0, 0, 1}, 1.0F, 8);
    for (int i = 0; i < 8; ++i) {
        const float t = 2.0F * kPi * static_cast<float>(i) / 8.0F;
        add_line(m, {std::cos(t), 1.0F, std::sin(t)}, {std::cos(t), -1.0F, std::sin(t)});
    }
    return m;
}

[[nodiscard]] Vec3 transform_row(const Vec3& p, const std::array<float, 16>& m) noexcept {
    return {p.x * m[0] + p.y * m[4] + p.z * m[8] + m[12],
            p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13],
            p.x * m[2] + p.y * m[6] + p.z * m[10] + m[14]};
}

[[nodiscard]] Vec3 rotate_euler(Vec3 p, const std::array<float, 3>& deg) noexcept {
    constexpr float k = kPi / 180.0F;
    const float cx = std::cos(deg[0] * k), sx = std::sin(deg[0] * k);
    const float cy = std::cos(deg[1] * k), sy = std::sin(deg[1] * k);
    const float cz = std::cos(deg[2] * k), sz = std::sin(deg[2] * k);
    p = {p.x, p.y * cx - p.z * sx, p.y * sx + p.z * cx};
    p = {p.x * cy + p.z * sy, p.y, -p.x * sy + p.z * cy};
    p = {p.x * cz - p.y * sz, p.x * sz + p.y * cz, p.z};
    return p;
}

void append_lines(std::vector<Vec3>& out, const WireMesh& mesh, auto&& place) {
    for (std::size_t i = 0U; i + 1U < mesh.lines.size(); i += 2U) {
        out.push_back(place(mesh.vertices[mesh.lines[i]]));
        out.push_back(place(mesh.vertices[mesh.lines[i + 1U]]));
    }
}

[[nodiscard]] bool attack_valid(const CollisionBinding& b, const AttackEntry& e) noexcept {
    return e.mask != 0U && e.bone < b.node_count && e.shape < b.shapes.size();
}

}  // namespace

const WireMesh& debug_sphere() {
    static const WireMesh mesh = make_sphere();
    return mesh;
}

const WireMesh& debug_box() {
    static const WireMesh mesh = make_box();
    return mesh;
}

const WireMesh& debug_capsule() {
    static const WireMesh mesh = capsule_wire({0.0F, -0.5F, 0.0F}, {0.0F, 0.5F, 0.0F}, 1.0F);
    return mesh;
}

const WireMesh& debug_cylinder() {
    static const WireMesh mesh = make_cylinder();
    return mesh;
}

WireMesh capsule_wire(const Vec3& a, const Vec3& b, float radius) {
    WireMesh m;
    const Vec3 axis = normalized({b.x - a.x, b.y - a.y, b.z - a.z});
    const Vec3 helper = std::fabs(axis.y) < 0.9F ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    const Vec3 u = normalized(cross(axis, helper));
    const Vec3 v = cross(axis, u);
    add_ring(m, a, u, v, radius, 16);
    add_ring(m, b, u, v, radius, 16);
    for (int i = 0; i < 4; ++i) {
        const float t = static_cast<float>(i) * kPi / 2.0F;
        const Vec3 d{u.x * std::cos(t) + v.x * std::sin(t), u.y * std::cos(t) + v.y * std::sin(t),
                     u.z * std::cos(t) + v.z * std::sin(t)};
        add_line(m, {a.x + d.x * radius, a.y + d.y * radius, a.z + d.z * radius},
                 {b.x + d.x * radius, b.y + d.y * radius, b.z + d.z * radius});
    }
    // Hemisphere caps: half circles in the two planes through the axis.
    for (const Vec3& side : {u, v}) {
        for (int end = 0; end < 2; ++end) {
            const Vec3 c = end == 0 ? a : b;
            const float dir = end == 0 ? -1.0F : 1.0F;
            const auto base = static_cast<std::uint32_t>(m.vertices.size());
            for (int i = 0; i <= 8; ++i) {
                const float t = kPi * static_cast<float>(i) / 8.0F;
                const float s = std::cos(t) * radius, h = std::sin(t) * radius * dir;
                m.vertices.push_back({c.x + side.x * s + axis.x * h, c.y + side.y * s + axis.y * h,
                                      c.z + side.z * s + axis.z * h});
                if (i != 0) {
                    m.lines.push_back(base + static_cast<std::uint32_t>(i - 1));
                    m.lines.push_back(base + static_cast<std::uint32_t>(i));
                }
            }
        }
    }
    return m;
}

std::vector<Vec3> shape_lines(const Shape& shape) {
    std::vector<Vec3> out;
    const Vec3 c{shape.a[0], shape.a[1], shape.a[2]};
    switch (static_cast<ShapeType>(shape.type)) {
    case ShapeType::Sphere:
        append_lines(out, debug_sphere(), [&](const Vec3& p) {
            return Vec3{c.x + p.x * shape.radius, c.y + p.y * shape.radius, c.z + p.z * shape.radius};
        });
        break;
    case ShapeType::Box:
        append_lines(out, debug_box(), [&](const Vec3& p) {
            const Vec3 q = rotate_euler({p.x * shape.size[0], p.y * shape.size[1], p.z * shape.size[2]}, shape.b);
            return Vec3{c.x + q.x, c.y + q.y, c.z + q.z};
        });
        break;
    case ShapeType::Capsule:
        append_lines(out, capsule_wire(c, {shape.b[0], shape.b[1], shape.b[2]}, shape.radius),
                     [](const Vec3& p) { return p; });
        break;
    default:
        break;
    }
    return out;
}

std::vector<Vec3> posed_collision_lines(const Session& session) {
    std::vector<Vec3> out;
    const auto* binding = session.collision.get();
    if (binding == nullptr || binding->node_begin + binding->node_count > session.scene.nodes.size()) {
        return out;
    }
    for (std::size_t id = 0U; id < binding->attacks.size(); ++id) {
        if (binding->attack >= 0 && static_cast<std::size_t>(binding->attack) != id) continue;
        const auto& e = binding->attacks[id];
        if (!attack_valid(*binding, e)) continue;
        const auto& world = session.scene.nodes[binding->node_begin + e.bone].world.values;
        for (const auto& p : shape_lines(binding->shapes[e.shape])) {
            const Vec3 w = transform_row(p, world);
            if (!std::isfinite(w.x) || !std::isfinite(w.y) || !std::isfinite(w.z)) continue;
            out.push_back(w);
        }
        if (out.size() % 2U != 0U) out.pop_back();
    }
    return out;
}

std::vector<int> collision_attack_ids(const Session& session) {
    std::vector<int> ids;
    const auto* binding = session.collision.get();
    if (binding == nullptr) return ids;
    for (std::size_t id = 0U; id < binding->attacks.size(); ++id) {
        if (attack_valid(*binding, binding->attacks[id])) ids.push_back(static_cast<int>(id));
    }
    return ids;
}

bool select_collision_attack(Session* session, int attack) noexcept {
    if (session == nullptr || session->collision == nullptr) return false;
    session->collision->attack = attack < 0 ? -1 : attack;
    return true;
}

std::string describe_collision_selection(const Session& session) {
    const auto* binding = session.collision.get();
    if (binding == nullptr) return {};
    if (binding->attack < 0) {
        return "all " + std::to_string(collision_attack_ids(session).size()) + " attacks";
    }
    const auto id = static_cast<std::size_t>(binding->attack);
    if (id >= binding->attacks.size()) return "attack " + std::to_string(id);
    const auto& e = binding->attacks[id];
    std::string shape = "?";
    if (e.shape < binding->shapes.size()) {
        switch (binding->shapes[e.shape].type) {
        case 2U: shape = "sphere"; break;
        case 3U: shape = "box"; break;
        case 4U: shape = "capsule"; break;
        default: shape = "type " + std::to_string(binding->shapes[e.shape].type); break;
        }
    }
    return "attack " + std::to_string(id) + ": bone " + std::to_string(e.bone) + " " + shape + " #" +
           std::to_string(e.shape) + " mask " + std::to_string(e.mask);
}

}  // namespace dmcresource::collision
