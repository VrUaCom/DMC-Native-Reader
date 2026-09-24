#include "dmcresource/stage_room.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>

#include "dmcresource/archive_entry.h"
#include "dmcresource/render_scene.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/spider/session_actions.h"

namespace dmcresource::stage_room {
namespace {

constexpr std::size_t kMaxDepth = 3U;
constexpr std::size_t kMaxPtxTries = 6U;

struct Item final {
    std::string name;
    std::string container;
    Format format{Format::Unknown};
    const std::vector<std::uint8_t>* bytes{};
};

// Same walk as PAC assembly: nested PACs are opened and kept alive; effect
// banks and PNSTs (effects, trails) are not room geometry.
void walk(const Session& container,
          const std::string& prefix,
          std::size_t depth,
          std::vector<std::unique_ptr<Session>>* owner,
          std::vector<Item>* out) {
    for (const auto& child : container.children) {
        if (child.source_bytes.empty()) continue;
        const auto kind = archive::classify_payload(child.source_bytes.data(), child.source_bytes.size());
        const std::string name = prefix + child.suggested_filename;
        if (kind.format == Format::Pac) {
            if (depth >= kMaxDepth) continue;
            auto nested = open_session(name, child.source_bytes.data(), child.source_bytes.size());
            if (!nested || nested->children.empty()) continue;
            owner->push_back(std::move(nested));
            walk(*owner->back(), name + "/", depth + 1U, owner, out);
            continue;
        }
        if (kind.format == Format::Scm || kind.format == Format::Mod || kind.format == Format::Ptx) {
            out->push_back({name, prefix, kind.format, &child.source_bytes});
        }
    }
}

// PTX candidates for a model: nearest before / after it in its container,
// then the rest of the archive by distance.
[[nodiscard]] std::vector<std::size_t> ptx_candidates(const std::vector<Item>& items, std::size_t index) {
    std::vector<std::pair<std::size_t, std::size_t>> ranked;  // (rank, item)
    for (std::size_t i = 0U; i < items.size(); ++i) {
        if (items[i].format != Format::Ptx) continue;
        const std::size_t distance = i < index ? index - i : i - index;
        const bool same = items[i].container == items[index].container;
        ranked.emplace_back((same ? 0U : 1000000U) + distance * 2U + (i > index ? 1U : 0U), i);
    }
    std::sort(ranked.begin(), ranked.end());
    std::vector<std::size_t> out;
    for (const auto& [rank, i] : ranked) {
        if (out.size() >= kMaxPtxTries) break;
        out.push_back(i);
    }
    return out;
}

void append(const Session& piece, Room* room) {
    const auto& src = piece.render_mesh;
    auto& dst = room->mesh;
    const auto base = static_cast<std::uint32_t>(dst.vertices.size());
    const auto count = src.vertices.size();
    const bool had = base > 0U;
    // Keep every optional channel either full or empty across pieces.
    const auto grow = [&](auto& channel, bool piece_has, auto fill, const auto& source) {
        const bool room_has = !channel.empty() || (!had && piece_has);
        if (!room_has && !piece_has) return;
        if (channel.empty()) channel.assign(base, fill);
        if (piece_has) {
            channel.insert(channel.end(), source.begin(), source.end());
        } else {
            channel.insert(channel.end(), count, fill);
        }
    };
    grow(dst.uv0, src.has_uv0(), Vec2{}, src.uv0);
    grow(dst.color0, src.has_color0(), std::array<std::uint8_t, 4>{0x80U, 0x80U, 0x80U, 0x80U}, src.color0);
    grow(dst.normal0, src.has_normal0(), Vec3{}, src.normal0);
    dst.vertices.insert(dst.vertices.end(), src.vertices.begin(), src.vertices.end());
    for (const auto i : src.indices) dst.indices.push_back(base + i);

    const auto texture_base = static_cast<std::uint32_t>(room->textures.size());
    const auto triangles = src.indices.size() / 3U;
    const bool slotted = piece.render_triangle_texture_slots.size() == triangles && !piece.attached_textures.empty();
    for (std::size_t t = 0U; t < triangles; ++t) {
        const auto slot = slotted ? piece.render_triangle_texture_slots[t] : kNoTextureSlot;
        room->triangle_texture_slots.push_back(
            slot == kNoTextureSlot || slot >= piece.attached_textures.size() ? kNoTextureSlot : texture_base + slot);
    }
    if (slotted) {
        room->textures.insert(room->textures.end(), piece.attached_textures.begin(), piece.attached_textures.end());
    }
}

// Stage layout text (top-level "# GAME" slot; record parser near
// 0x140247720): per "# SET n <kind>" block `model` (byte at +0), `pos`
// (+0x10), `rot` in degrees (+0x20, stored as radians), `scale` (+0x30);
// the CONFIG block holds `cam_init`.
struct GameSet final {
    std::string kind;
    int model{-1};
    Vec3 pos{};
    Vec3 rot{};
    Vec3 scale{1.0F, 1.0F, 1.0F};
};

struct GameLayout final {
    std::vector<GameSet> sets;
    bool has_camera{};
    Vec3 camera{};
};

[[nodiscard]] std::vector<float> numbers(std::string_view line) {
    std::vector<float> out;
    std::string token;
    const auto flush = [&] {
        if (token.empty()) return;
        try {
            std::size_t used = 0U;
            const float v = std::stof(token, &used);
            if (used == token.size()) out.push_back(v);
        } catch (...) {
        }
        token.clear();
    };
    for (const char c : line) {
        if (c == ';') break;
        if (c == ',' || c == ' ' || c == '\t' || c == '\r') {
            flush();
        } else {
            token.push_back(c);
        }
    }
    flush();
    return out;
}

[[nodiscard]] GameLayout parse_game(std::string_view text) {
    GameLayout out;
    GameSet* current = nullptr;
    std::size_t start = 0U;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        auto line = text.substr(start, end - start);
        start = end + 1U;
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1U);
        if (line.starts_with("$") || line.starts_with("# GAME_END")) break;
        if (line.starts_with("# SET")) {
            auto rest = line.substr(5U);
            const auto words = numbers(rest);  // the set number
            (void)words;
            std::string kind;
            std::size_t i = 0U;
            while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
            while (i < rest.size() && rest[i] != ' ' && rest[i] != '\t') ++i;  // number
            while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
            while (i < rest.size() && rest[i] > ' ' && rest[i] != ';') kind.push_back(rest[i++]);
            out.sets.push_back({kind});
            current = &out.sets.back();
            continue;
        }
        if (current == nullptr) continue;
        const auto key_end = line.find_first_of(" \t");
        if (key_end == std::string_view::npos) continue;
        const auto key = line.substr(0U, key_end);
        const auto values = numbers(line.substr(key_end));
        if (key == "model" && !values.empty()) {
            current->model = static_cast<int>(values[0]);
        } else if (key == "pos" && values.size() >= 3U) {
            current->pos = {values[0], values[1], values[2]};
        } else if (key == "rot" && values.size() >= 3U) {
            current->rot = {values[0], values[1], values[2]};
        } else if (key == "scale" && values.size() >= 3U) {
            current->scale = {values[0], values[1], values[2]};
        } else if (key == "cam_init" && values.size() >= 3U) {
            out.has_camera = true;
            out.camera = {values[0], values[1], values[2]};
        }
    }
    return out;
}

// Scale, rotate (degrees; X then Y then Z, all zero in the samples) and
// move a placed object's mesh.
void place(Mesh& mesh, const GameSet& set) {
    constexpr float kRad = 3.14159265358979F / 180.0F;
    const float cx = std::cos(set.rot.x * kRad), sx = std::sin(set.rot.x * kRad);
    const float cy = std::cos(set.rot.y * kRad), sy = std::sin(set.rot.y * kRad);
    const float cz = std::cos(set.rot.z * kRad), sz = std::sin(set.rot.z * kRad);
    const auto turn = [&](Vec3 v) {
        v = {v.x, cx * v.y - sx * v.z, sx * v.y + cx * v.z};
        v = {cy * v.x + sy * v.z, v.y, -sy * v.x + cy * v.z};
        return Vec3{cz * v.x - sz * v.y, sz * v.x + cz * v.y, v.z};
    };
    for (auto& v : mesh.vertices) {
        const auto r = turn({v.x * set.scale.x, v.y * set.scale.y, v.z * set.scale.z});
        v = {r.x + set.pos.x, r.y + set.pos.y, r.z + set.pos.z};
    }
    for (auto& n : mesh.normal0) n = turn(n);
}

[[nodiscard]] Vec3 sub(const Vec3& a, const Vec3& b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
[[nodiscard]] Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] float length(const Vec3& a) noexcept { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

[[nodiscard]] bool stage_child(const ChildResource& child, std::size_t depth) noexcept {
    if (child.probe.format == Format::Scm) return true;
    if (depth >= kMaxDepth) return false;
    return std::any_of(child.children.begin(), child.children.end(),
                       [depth](const ChildResource& c) { return stage_child(c, depth + 1U); });
}

std::mutex g_mutex;
std::shared_ptr<const Room> g_room;
std::size_t g_spot = 0U;

}  // namespace

std::optional<Vec3> floor_spots_near(const Mesh& mesh, const Vec3& focus) {
    // Floor triangle under (or nearest to) the focus in xz, below its height.
    std::optional<Vec3> best;
    float best_score = std::numeric_limits<float>::max();
    const bool normals = mesh.has_normal0();
    for (std::size_t t = 0U; t + 2U < mesh.indices.size(); t += 3U) {
        const auto ia = mesh.indices[t], ib = mesh.indices[t + 1U], ic = mesh.indices[t + 2U];
        if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() || ic >= mesh.vertices.size()) continue;
        const auto& a = mesh.vertices[ia];
        const auto& b = mesh.vertices[ib];
        const auto& c = mesh.vertices[ic];
        const Vec3 n = cross(sub(b, a), sub(c, a));
        const float twice = length(n);
        if (!(twice > 1.0e-4F)) continue;
        float ny = std::fabs(n.y) / twice;
        if (normals) {
            const float s = mesh.normal0[ia].y + mesh.normal0[ib].y + mesh.normal0[ic].y;
            const Vec3 sn{mesh.normal0[ia].x + mesh.normal0[ib].x + mesh.normal0[ic].x, s,
                          mesh.normal0[ia].z + mesh.normal0[ib].z + mesh.normal0[ic].z};
            const float l = length(sn);
            if (l > 0.0F) ny = s / l;
        }
        if (ny < 0.7F) continue;
        const Vec3 centre{(a.x + b.x + c.x) / 3.0F, (a.y + b.y + c.y) / 3.0F, (a.z + b.z + c.z) / 3.0F};
        if (centre.y > focus.y) continue;
        // Closest in xz, then the highest floor below the focus.
        const float dx = centre.x - focus.x, dz = centre.z - focus.z;
        const float score = std::sqrt(dx * dx + dz * dz) + (focus.y - centre.y) * 0.25F;
        if (score < best_score) {
            best_score = score;
            best = centre;
        }
    }
    return best;
}

std::vector<Vec3> floor_spots(const Mesh& mesh, std::size_t limit) {
    struct Floor final {
        Vec3 centre;
        float area{};
        Vec3 a, b, c;
    };
    std::vector<Floor> floors;
    const bool normals = mesh.has_normal0();
    for (std::size_t t = 0U; t + 2U < mesh.indices.size(); t += 3U) {
        const auto ia = mesh.indices[t], ib = mesh.indices[t + 1U], ic = mesh.indices[t + 2U];
        if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() || ic >= mesh.vertices.size()) continue;
        const auto& a = mesh.vertices[ia];
        const auto& b = mesh.vertices[ib];
        const auto& c = mesh.vertices[ic];
        const Vec3 n = cross(sub(b, a), sub(c, a));
        const float twice = length(n);
        if (!(twice > 1.0e-4F) || !std::isfinite(twice)) continue;
        bool up = false;
        if (normals) {
            const Vec3 s{mesh.normal0[ia].x + mesh.normal0[ib].x + mesh.normal0[ic].x,
                         mesh.normal0[ia].y + mesh.normal0[ib].y + mesh.normal0[ic].y,
                         mesh.normal0[ia].z + mesh.normal0[ib].z + mesh.normal0[ic].z};
            const float l = length(s);
            up = l > 0.0F ? s.y / l > 0.7F : std::fabs(n.y) / twice > 0.9F;
        } else {
            up = std::fabs(n.y) / twice > 0.9F;
        }
        if (!up) continue;
        floors.push_back({{(a.x + b.x + c.x) / 3.0F, (a.y + b.y + c.y) / 3.0F, (a.z + b.z + c.z) / 3.0F},
                          twice * 0.5F, a, b, c});
    }
    std::vector<Vec3> out;
    if (floors.empty()) {
        if (mesh.vertices.empty()) return out;
        Vec3 lo{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()};
        Vec3 hi{-lo.x, -lo.y, -lo.z};
        for (const auto& v : mesh.vertices) {
            lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
            hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
        }
        out.push_back({(lo.x + hi.x) * 0.5F, lo.y, (lo.z + hi.z) * 0.5F});
        return out;
    }
    // Area-weighted centre of all floors and their horizontal extent.
    double wx = 0.0, wz = 0.0, total = 0.0;
    float minx = floors[0].centre.x, maxx = minx, minz = floors[0].centre.z, maxz = minz;
    for (const auto& f : floors) {
        wx += static_cast<double>(f.centre.x) * f.area;
        wz += static_cast<double>(f.centre.z) * f.area;
        total += f.area;
        minx = std::min(minx, f.centre.x);
        maxx = std::max(maxx, f.centre.x);
        minz = std::min(minz, f.centre.z);
        maxz = std::max(maxz, f.centre.z);
    }
    const float cx = static_cast<float>(wx / total);
    const float cz = static_cast<float>(wz / total);
    const float extent = std::max({maxx - minx, maxz - minz, 1.0F});
    // Floors are often tessellated evenly: among the larger half pick the one
    // nearest the centre, then the largest floors spread over the room.
    // The centre itself when a floor lies under it (largest such floor;
    // height from that triangle's plane).
    float cover = 0.0F;
    for (const auto& f : floors) {
        const float d = (f.b.z - f.c.z) * (f.a.x - f.c.x) + (f.c.x - f.b.x) * (f.a.z - f.c.z);
        if (std::fabs(d) < 1.0e-6F) continue;
        const float l0 = ((f.b.z - f.c.z) * (cx - f.c.x) + (f.c.x - f.b.x) * (cz - f.c.z)) / d;
        const float l1 = ((f.c.z - f.a.z) * (cx - f.c.x) + (f.a.x - f.c.x) * (cz - f.c.z)) / d;
        const float l2 = 1.0F - l0 - l1;
        if (l0 < 0.0F || l1 < 0.0F || l2 < 0.0F || f.area <= cover) continue;
        cover = f.area;
        if (out.empty()) out.push_back({});
        out[0] = {cx, l0 * f.a.y + l1 * f.b.y + l2 * f.c.y, cz};
    }
    std::vector<float> areas;
    areas.reserve(floors.size());
    for (const auto& f : floors) areas.push_back(f.area);
    std::nth_element(areas.begin(), areas.begin() + static_cast<std::ptrdiff_t>(areas.size() / 2U), areas.end());
    const float median = areas[areas.size() / 2U];
    std::size_t best = 0U;
    float best_distance = std::numeric_limits<float>::max();
    for (std::size_t i = 0U; i < floors.size(); ++i) {
        if (floors[i].area < median) continue;
        const float dx = floors[i].centre.x - cx, dz = floors[i].centre.z - cz;
        const float d = dx * dx + dz * dz;
        if (d < best_distance) {
            best_distance = d;
            best = i;
        }
    }
    if (out.empty()) out.push_back(floors[best].centre);
    std::vector<std::size_t> order(floors.size());
    for (std::size_t i = 0U; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return floors[a].area > floors[b].area; });
    const float spacing = extent * 0.15F;
    for (const auto i : order) {
        if (out.size() >= limit) break;
        const auto& p = floors[i].centre;
        const bool apart = std::all_of(out.begin(), out.end(), [&](const Vec3& q) {
            const float dx = p.x - q.x, dz = p.z - q.z;
            return dx * dx + dz * dz > spacing * spacing;
        });
        if (apart) out.push_back(p);
    }
    return out;
}

std::shared_ptr<const Room> build_room(std::string_view name, const std::uint8_t* bytes, std::size_t size) noexcept {
    try {
        if (bytes == nullptr || size == 0U) return nullptr;
        auto root = open_session(name, bytes, size);
        if (!root) return nullptr;
        auto room = std::make_shared<Room>();
        room->name = std::string{name};
        std::vector<std::unique_ptr<Session>> owner;
        std::vector<Item> items;
        walk(*root, "", 0U, &owner, &items);
        std::size_t scm = 0U, mod = 0U;
        if (items.empty() && root->renderable && !root->render_mesh.vertices.empty()) {
            // A lone .scm / .mod: the file itself is the room.
            ++room->pieces;
            append(*root, room.get());
        }
        for (std::size_t i = 0U; i < items.size(); ++i) {
            if (items[i].format != Format::Scm && items[i].format != Format::Mod) continue;
            auto piece = open_session(items[i].name, items[i].bytes->data(), items[i].bytes->size());
            if (!piece || !piece->renderable || piece->render_mesh.vertices.empty() ||
                piece->render_mesh.indices.size() < 3U) {
                continue;
            }
            for (const auto p : ptx_candidates(items, i)) {
                if (spider::actions::attach_ptx(piece.get(), items[p].name, items[p].bytes->data(),
                                                items[p].bytes->size())) {
                    ++room->textured_pieces;
                    break;
                }
            }
            (items[i].format == Format::Scm ? scm : mod) += 1U;
            ++room->pieces;
            append(*piece, room.get());
        }
        // Stage objects: the "# GAME" layout places model k (k-th model entry
        // of the top-level PNST, EFM / SCM / MOD in order, MOT PACs skipped)
        // at its pos / rot / scale. Broken variants (bmodel) are not shown.
        std::size_t objects = 0U;
        GameLayout layout;
        const Session* object_bank = nullptr;
        std::unique_ptr<Session> bank_owner;
        for (const auto& child : root->children) {
            const auto& b = child.source_bytes;
            if (b.size() > 6U && std::string_view{reinterpret_cast<const char*>(b.data()), 6U} == "# GAME") {
                layout = parse_game({reinterpret_cast<const char*>(b.data()), b.size()});
            } else if (!b.empty() && object_bank == nullptr &&
                       archive::classify_payload(b.data(), b.size()).format == Format::Pnst) {
                bank_owner = open_session(child.suggested_filename, b.data(), b.size());
                if (bank_owner && !bank_owner->children.empty()) object_bank = bank_owner.get();
            }
        }
        if (object_bank != nullptr && !layout.sets.empty()) {
            std::vector<const ChildResource*> models;
            for (const auto& child : object_bank->children) {
                if (child.source_bytes.empty()) continue;
                const auto format = archive::classify_payload(child.source_bytes.data(), child.source_bytes.size()).format;
                if (format == Format::Mod || format == Format::Scm) models.push_back(&child);
            }
            for (const auto& set : layout.sets) {
                if (set.model < 0 || static_cast<std::size_t>(set.model) >= models.size()) continue;
                const auto& child = *models[static_cast<std::size_t>(set.model)];
                auto piece = open_session(child.suggested_filename, child.source_bytes.data(), child.source_bytes.size());
                if (!piece || !piece->renderable || piece->render_mesh.indices.size() < 3U) continue;
                // Objects use the stage texture bank (the PNST holds none).
                for (std::size_t i = 0U; i < items.size(); ++i) {
                    if (items[i].format != Format::Ptx || !items[i].container.empty()) continue;
                    if (spider::actions::attach_ptx(piece.get(), items[i].name, items[i].bytes->data(),
                                                    items[i].bytes->size())) {
                        ++room->textured_pieces;
                        break;
                    }
                }
                place(piece->render_mesh, set);
                ++objects;
                ++room->pieces;
                append(*piece, room.get());
            }
        }
        if (room->mesh.indices.size() < 3U) return nullptr;
        // Soft-alpha textures: more than 2 % of texels between 8 and 239.
        std::vector<std::uint8_t> soft(room->textures.size(), 0U);
        for (std::size_t i = 0U; i < room->textures.size(); ++i) {
            const auto& t = room->textures[i];
            if (!t.available()) continue;
            std::size_t partial = 0U;
            for (std::size_t o = 3U; o < t.rgba8.size(); o += 4U) {
                if (t.rgba8[o] >= 8U && t.rgba8[o] < 240U) ++partial;
            }
            soft[i] = partial * 50U > t.rgba8.size() / 4U ? 1U : 0U;
        }
        room->translucent_triangles.reserve(room->triangle_texture_slots.size());
        for (const auto slot : room->triangle_texture_slots) {
            room->translucent_triangles.push_back(slot < soft.size() ? soft[slot] : 0U);
        }
        room->spots = floor_spots(room->mesh);
        // Stand first where the game frames the stage: the floor spot nearest
        // to the midpoint of cam_init and the placed objects.
        if (layout.has_camera && room->spots.size() > 1U) {
            Vec3 focus = layout.camera;
            std::size_t placed = 0U;
            Vec3 sum{};
            for (const auto& set : layout.sets) {
                if (set.model < 0 || (set.pos.x == 0.0F && set.pos.y == 0.0F && set.pos.z == 0.0F)) continue;
                sum = {sum.x + set.pos.x, sum.y + set.pos.y, sum.z + set.pos.z};
                ++placed;
            }
            if (placed > 0U) {
                const float k = 1.0F / static_cast<float>(placed);
                focus = {(focus.x + sum.x * k) * 0.5F, focus.y, (focus.z + sum.z * k) * 0.5F};
            }
            const auto near_focus = floor_spots_near(room->mesh, focus);
            if (near_focus) room->spots.insert(room->spots.begin(), *near_focus);
        }
        std::ostringstream detail;
        detail << room->name << ": " << room->pieces << " pieces (" << scm << " SCM, " << mod << " MOD, "
               << objects << " placed objects), "
               << room->textured_pieces << " textured, " << room->mesh.vertices.size() << " vertices, "
               << room->mesh.indices.size() / 3U << " triangles, " << room->spots.size() << " floor spots";
        room->detail = detail.str();
        return room;
    } catch (...) {
        return nullptr;
    }
}

void set_current(std::shared_ptr<const Room> room) noexcept {
    const std::lock_guard lock{g_mutex};
    g_room = std::move(room);
    g_spot = 0U;
}

std::shared_ptr<const Room> current() noexcept {
    const std::lock_guard lock{g_mutex};
    return g_room;
}

void set_spot(std::size_t index) noexcept {
    const std::lock_guard lock{g_mutex};
    g_spot = g_room && !g_room->spots.empty() ? index % g_room->spots.size() : 0U;
}

std::size_t spot() noexcept {
    const std::lock_guard lock{g_mutex};
    return g_spot;
}

bool is_stage_session(const Session& session) noexcept {
    if (session.probe.format == Format::Scm) return true;
    return std::any_of(session.children.begin(), session.children.end(),
                       [](const ChildResource& c) { return stage_child(c, 1U); });
}

}  // namespace dmcresource::stage_room
