#include "dmcresource/demo_session.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "dmcresource/motion/motion_player.h"
#include "dmcresource/spider/session_actions.h"
#include "dmcresource/stage_room.h"

namespace dmcresource::demo {
namespace {

constexpr float kHalfFovRadians = 0.5F;  // the viewer's framing at zoom 1 (view_renderer.cpp)
constexpr float kDegrees = 3.14159265358979323846F / 180.0F;

[[nodiscard]] bool ends_with(std::string_view s, std::string_view tail) noexcept {
    if (s.size() < tail.size()) return false;
    for (std::size_t i = 0; i < tail.size(); ++i) {
        char c = s[s.size() - tail.size() + i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != tail[i]) return false;
    }
    return true;
}

// A PAC of the given files in order, every slot 16-byte aligned: the stage
// pieces with their PTX before each, the way build_room pairs them.
[[nodiscard]] std::vector<std::uint8_t> make_pac(const std::vector<std::span<const std::uint8_t>>& files) {
    const auto align = [](std::size_t v) { return (v + 15U) & ~std::size_t{15U}; };
    std::size_t at = align(8U + files.size() * 4U);
    std::vector<std::uint32_t> offsets;
    for (const auto& f : files) {
        offsets.push_back(static_cast<std::uint32_t>(at));
        at = align(at + f.size());
    }
    std::vector<std::uint8_t> out(at, 0U);
    std::memcpy(out.data(), "PAC\0", 4U);
    const auto count = static_cast<std::uint32_t>(files.size());
    std::memcpy(out.data() + 4U, &count, 4U);
    for (std::size_t i = 0; i < files.size(); ++i) {
        std::memcpy(out.data() + 8U + i * 4U, &offsets[i], 4U);
        if (!files[i].empty()) std::memcpy(out.data() + offsets[i], files[i].data(), files[i].size());
    }
    return out;
}

[[nodiscard]] Player* player(const Session* session) noexcept {
    return session != nullptr ? session->demo.get() : nullptr;
}

void update_shot(Player& p) {
    p.shot = camera_at(p.doc, p.frame);
}

// The clip of a list that plays at `frame`: the covering clip that started
// last; between clips the last one that ended (held at its end); before the
// first, the first (at its start). The clip's own time comes back in `local`.
[[nodiscard]] const Clip* clip_at(const ClipList& list, float frame, float* local) noexcept {
    const Clip* chosen = nullptr;
    for (const auto& clip : list.clips) {
        if (clip.covers(frame) && (chosen == nullptr || clip.set_frame >= chosen->set_frame)) chosen = &clip;
    }
    if (chosen != nullptr) {
        *local = chosen->local_frame(frame);
        return chosen;
    }
    for (const auto& clip : list.clips) {
        if (clip.end_frame() <= frame && (chosen == nullptr || clip.end_frame() > chosen->end_frame())) chosen = &clip;
    }
    if (chosen != nullptr) {
        *local = chosen->life;
        return chosen;
    }
    for (const auto& clip : list.clips) {
        if (chosen == nullptr || clip.set_frame < chosen->set_frame) chosen = &clip;
    }
    *local = 0.0F;
    return chosen;
}

// Inverse of an affine row-vector matrix (rows 0-2 the basis, row 3 the
// translation); identity when it is singular.
[[nodiscard]] Matrix4 affine_inverse(const Matrix4& m) noexcept {
    const auto& a = m.values;
    const float c00 = a[5] * a[10] - a[6] * a[9], c01 = a[2] * a[9] - a[1] * a[10], c02 = a[1] * a[6] - a[2] * a[5];
    const float c10 = a[6] * a[8] - a[4] * a[10], c11 = a[0] * a[10] - a[2] * a[8], c12 = a[2] * a[4] - a[0] * a[6];
    const float c20 = a[4] * a[9] - a[5] * a[8], c21 = a[1] * a[8] - a[0] * a[9], c22 = a[0] * a[5] - a[1] * a[4];
    const float det = a[0] * c00 + a[1] * c10 + a[2] * c20;
    Matrix4 out;
    if (!std::isfinite(det) || std::fabs(det) < 1.0e-12F) return out;
    const float k = 1.0F / det;
    auto& o = out.values;
    o = {c00 * k, c01 * k, c02 * k, 0.0F, c10 * k, c11 * k, c12 * k, 0.0F, c20 * k, c21 * k, c22 * k, 0.0F,
         0.0F, 0.0F, 0.0F, 1.0F};
    o[12] = -(a[12] * o[0] + a[13] * o[4] + a[14] * o[8]);
    o[13] = -(a[12] * o[1] + a[13] * o[5] + a[14] * o[9]);
    o[14] = -(a[12] * o[2] + a[13] * o[6] + a[14] * o[10]);
    return out;
}

[[nodiscard]] Vec3 transform(const Vec3& p, const Matrix4& m) noexcept {
    const auto& v = m.values;
    return {p.x * v[0] + p.y * v[4] + p.z * v[8] + v[12], p.x * v[1] + p.y * v[5] + p.z * v[9] + v[13],
            p.x * v[2] + p.y * v[6] + p.z * v[10] + v[14]};
}

// Whether a track is drawn at `frame`: the Hide clip set last before it
// decides (H hides the model, D shows it, A plays a hide animation:
// DispOff.hid hides every object, DispOn.hid and the per-object animations
// leave it shown). Without Hide clips a model is shown.
[[nodiscard]] bool visible_at(const ClipList* list, float frame) {
    if (list == nullptr) return true;
    const Clip* last = nullptr;
    for (const auto& clip : list->clips) {
        if (clip.set_frame <= frame && (last == nullptr || clip.set_frame >= last->set_frame)) last = &clip;
    }
    if (last == nullptr) return true;
    const auto kind = last->value("HideKind");
    if (kind == "H") return false;
    if (kind == "D") return true;
    auto file = last->value("Hide");
    for (auto& c : file) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return file != "dispoff.hid";
}

// Draws the visible tracks' triangles only.
void apply_visibility(Session& scene, Player& p) {
    bool changed = false;
    for (auto& t : p.tracks) {
        const bool now = visible_at(p.doc.list("Hide", t.track), p.frame);
        changed = changed || now != t.visible;
        t.visible = now;
    }
    if (!changed || p.all_indices.empty()) return;
    auto& indices = scene.render_mesh.indices;
    auto& slots = scene.render_triangle_texture_slots;
    indices.clear();
    slots.clear();
    const bool with_slots = p.all_triangle_slots.size() * 3U == p.all_indices.size();
    for (const auto& t : p.tracks) {
        if (!t.visible) continue;
        const auto first = t.triangle_begin * 3U, last = (t.triangle_begin + t.triangle_count) * 3U;
        if (last > p.all_indices.size()) continue;
        indices.insert(indices.end(), p.all_indices.begin() + static_cast<std::ptrdiff_t>(first),
                       p.all_indices.begin() + static_cast<std::ptrdiff_t>(last));
        if (with_slots) {
            slots.insert(slots.end(), p.all_triangle_slots.begin() + static_cast<std::ptrdiff_t>(t.triangle_begin),
                         p.all_triangle_slots.begin() + static_cast<std::ptrdiff_t>(t.triangle_begin + t.triangle_count));
        }
    }
    // An empty frame still draws (one degenerate triangle).
    if (indices.empty()) {
        indices = {0U, 0U, 0U};
        if (with_slots) slots = {kNoTextureSlot};
    }
}

// Poses every model track at the player's frame and writes its vertices
// into the scene: free tracks first, then the ones hanging from a joint of
// an already posed track.
void pose_tracks(Session& scene, Player& p) {
    auto& out = scene.render_mesh.vertices;
    std::vector<char> done(p.tracks.size(), 0);
    const auto find = [&](int track) -> int {
        for (std::size_t i = 0; i < p.tracks.size(); ++i) {
            if (p.tracks[i].track == track) return static_cast<int>(i);
        }
        return -1;
    };
    for (int pass = 0; pass < 4; ++pass) {
        for (std::size_t i = 0; i < p.tracks.size(); ++i) {
            if (done[i] != 0) continue;
            auto& t = p.tracks[i];
            const int host = t.host_track >= 0 ? find(t.host_track) : -1;
            if (host >= 0 && done[static_cast<std::size_t>(host)] == 0 && pass < 3) continue;
            done[i] = 1;
            if (!t.source) continue;
            Session& model = *t.source;
            if (const auto* list = p.doc.list("Motion", t.track); list != nullptr && !list->clips.empty()) {
                float local = 0.0F;
                const auto* clip = clip_at(*list, p.frame, &local);
                const auto name = clip != nullptr ? clip->value("Motion") : std::string{};
                if (!name.empty() && name != t.motion && name != t.failed_motion) {
                    const auto* member = p.doc.member(name);
                    const auto report = member != nullptr
                        ? motion::load_cutscene_motion(&model, name, p.bytes->data() + member->offset, member->size)
                        : motion::MotionLoadReport{};
                    if (report.ok) {
                        t.motion = name;
                    } else {
                        t.failed_motion = name;
                        t.failure = report.detail;
                        t.motion.clear();
                    }
                }
                if (clip != nullptr && name == t.motion) {
                    (void)motion::apply_motion_frame(&model, clip->cut_frame + std::clamp(local, 0.0F, clip->life));
                }
            }
            if (t.vertex_begin + t.vertex_count > out.size() || model.render_mesh.vertices.size() != t.vertex_count) {
                continue;
            }
            const Matrix4* joint = nullptr;
            if (host >= 0 && p.tracks[static_cast<std::size_t>(host)].source && t.host_joint >= 0) {
                const auto& nodes = p.tracks[static_cast<std::size_t>(host)].source->scene.nodes;
                if (static_cast<std::size_t>(t.host_joint) < nodes.size()) {
                    joint = &nodes[static_cast<std::size_t>(t.host_joint)].world;
                }
            }
            for (std::size_t v = 0; v < t.vertex_count; ++v) {
                const auto& src = model.render_mesh.vertices[v];
                out[t.vertex_begin + v] = joint != nullptr ? transform(transform(src, t.host_rest_inverse), *joint) : src;
            }
        }
    }
}

}  // namespace

bool is_demo(const Session* session) noexcept {
    return player(session) != nullptr;
}

DirectorView view_of(const CameraPose& pose) noexcept {
    DirectorView view;
    view.eye = pose.eye;
    const float dx = pose.target.x - pose.eye.x, dy = pose.target.y - pose.eye.y, dz = pose.target.z - pose.eye.z;
    const float flat = std::hypot(dx, dz);
    // camera_basis: forward = (cos p sin y, sin p, cos p cos y).
    view.yaw = flat > 1.0e-6F ? std::atan2(dx, dz) : 0.0F;
    view.pitch = std::atan2(dy, std::max(flat, 1.0e-6F));
    view.fov_degrees = pose.fov_degrees;
    const float half = std::clamp(pose.fov_degrees, 2.0F, 160.0F) * 0.5F * kDegrees;
    view.zoom = std::tan(kHalfFovRadians) / std::tan(half);
    return view;
}

std::optional<DirectorView> director_view(const Session& session) noexcept {
    const auto* p = player(&session);
    if (p == nullptr || !p->director || !p->shot) return std::nullopt;
    return view_of(p->shot->pose);
}

std::unique_ptr<Session> open_demo(Session&& archive, std::string_view name, const std::uint8_t* bytes,
                                   std::size_t size) noexcept {
    try {
        if (bytes == nullptr || size == 0U) return nullptr;
        const std::span<const std::uint8_t> data{bytes, size};
        auto doc = parse_demo(data);
        if (!doc) return nullptr;
        auto state = std::make_shared<Player>();
        state->bytes = std::make_shared<const std::vector<std::uint8_t>>(bytes, bytes + size);
        const auto& source = *state->bytes;
        const auto member_bytes = [&](std::string_view file) -> std::span<const std::uint8_t> {
            const auto* m = doc->member(file);
            if (m == nullptr) return {};
            return std::span<const std::uint8_t>{source}.subspan(m->offset, m->size);
        };

        // Stage pieces to the room; models to the composite.
        std::vector<std::span<const std::uint8_t>> stage_files;
        std::vector<std::unique_ptr<Session>> models;
        std::vector<PlayerTrack> tracks;
        for (const auto& track : doc->tracks()) {
            if (track.model == nullptr || track.model->clips.empty()) continue;
            const auto& clip = track.model->clips.front();
            const auto model = clip.value("Model");
            const auto texture = clip.value("Texture");
            const auto model_bytes = member_bytes(model);
            if (model_bytes.empty()) continue;
            if (ends_with(model, ".scm")) {
                if (const auto tex = member_bytes(texture); !tex.empty()) stage_files.push_back(tex);
                stage_files.push_back(model_bytes);
                state->stage_pieces.push_back(model);
                continue;
            }
            auto opened = open_session(model, model_bytes.data(), model_bytes.size());
            if (!opened || opened->probe.format != Format::Mod || !opened->renderable) continue;
            PlayerTrack t;
            t.track = track.index;
            t.model = model;
            t.texture = texture;
            for (const auto& [key, values] : clip.fields) {
                if (key == "CnsPos" && values.size() >= 2U) {
                    t.host_track = std::atoi(values[0].c_str());
                    t.host_joint = std::atoi(values[1].c_str());
                }
            }
            t.vertex_count = opened->render_mesh.vertices.size();
            t.triangle_count = opened->render_mesh.indices.size() / 3U;
            tracks.push_back(std::move(t));
            models.push_back(std::move(opened));
        }

        std::unique_ptr<Session> scene;
        if (models.size() >= 2U) {
            std::vector<const Session*> parts;
            std::vector<std::string> names;
            for (std::size_t i = 0; i < models.size(); ++i) {
                parts.push_back(models[i].get());
                names.push_back("track " + std::to_string(tracks[i].track) + " " + tracks[i].model);
            }
            scene = spider::actions::compose_mod_sessions(parts, names, -1);
            if (scene) {
                for (std::size_t i = 0; i < tracks.size(); ++i) {
                    const auto tex = member_bytes(tracks[i].texture);
                    if (!tex.empty()) {
                        (void)spider::actions::attach_ptx_to_part(scene.get(), static_cast<int>(i), tracks[i].texture,
                                                                   tex.data(), tex.size());
                    }
                }
            }
        } else if (models.size() == 1U) {
            // One model: a copy of it is the scene.
            const auto& only = tracks.front();
            const auto model_bytes = member_bytes(only.model);
            scene = open_session(only.model, model_bytes.data(), model_bytes.size());
            if (scene) {
                const auto tex = member_bytes(only.texture);
                if (!tex.empty()) (void)spider::actions::attach_ptx(scene.get(), only.texture, tex.data(), tex.size());
            }
        }

        std::shared_ptr<const stage_room::Room> room;
        if (!stage_files.empty()) {
            const auto pac = make_pac(stage_files);
            room = stage_room::build_room(name, pac.data(), pac.size());
        }
        if (!scene && !room) return nullptr;
        if (!scene) {
            // Only the stage: like a stage scene, a character-sized anchor
            // the camera starts at, drawn as one degenerate triangle.
            scene = std::make_unique<Session>();
            Vec3 spot = room->spots.empty() ? (room->mesh.vertices.empty() ? Vec3{} : room->mesh.vertices.front())
                                            : room->spots.front();
            for (const float y : {0.0F, 200.0F}) {
                for (const float x : {-120.0F, 120.0F}) {
                    for (const float z : {-120.0F, 120.0F}) scene->render_mesh.vertices.push_back({spot.x + x, spot.y + y, spot.z + z});
                }
            }
            scene->render_mesh.indices = {0U, 0U, 0U};
            scene->renderable = true;
            tracks.clear();
            models.clear();
        }

        // Vertex ranges of the tracks in the composite (sources in order).
        std::size_t cursor = 0, triangles = 0;
        for (auto& t : tracks) {
            t.vertex_begin = cursor;
            cursor += t.vertex_count;
            t.triangle_begin = triangles;
            triangles += t.triangle_count;
        }
        if (cursor != scene->render_mesh.vertices.size() || triangles * 3U != scene->render_mesh.indices.size()) {
            // The projection is not the plain concatenation: keep the scene
            // still rather than move the wrong vertices.
            tracks.clear();
            models.clear();
        }
        for (std::size_t i = 0; i < tracks.size(); ++i) tracks[i].source = std::move(models[i]);
        // Rest worlds of the joints the CnsPos models hang from.
        for (auto& t : tracks) {
            if (t.host_track < 0 || t.host_joint < 0) continue;
            for (const auto& host : tracks) {
                if (host.track != t.host_track || !host.source) continue;
                const auto& nodes = host.source->scene.nodes;
                if (static_cast<std::size_t>(t.host_joint) < nodes.size()) {
                    t.host_rest_inverse = affine_inverse(nodes[static_cast<std::size_t>(t.host_joint)].world);
                }
            }
        }
        state->tracks = std::move(tracks);
        state->doc = std::move(*doc);
        if (!state->tracks.empty()) {
            state->all_indices = scene->render_mesh.indices;
            state->all_triangle_slots = scene->render_triangle_texture_slots;
        }

        // The archive's identity, Info, timeline picture and files stay.
        scene->probe = archive.probe;
        scene->capabilities |= archive.capabilities;
        scene->inspection = std::move(archive.inspection);
        scene->image_preview = std::move(archive.image_preview);
        scene->children = std::move(archive.children);
        scene->archive_name = std::string{name};
        scene->source_name = std::string{name};
        scene->stage = std::move(room);
        scene->detail = archive.detail + " | scene: " + std::to_string(state->tracks.size()) + " model track(s), " +
                        std::to_string(state->stage_pieces.size()) + " stage piece(s)";
        // The timeline is the scene's one motion (motion_player.h plays it).
        scene->motion_library.clear();
        Session::MotionPayload timeline;
        timeline.name = "Cutscene " + state->doc.name;
        scene->motion_library.push_back(std::move(timeline));
        scene->demo = std::move(state);
        update_shot(*scene->demo);
        pose_tracks(*scene, *scene->demo);
        apply_visibility(*scene, *scene->demo);
        return scene;
    } catch (...) {
        return nullptr;
    }
}

bool bind(Session* session) noexcept {
    auto* p = player(session);
    if (p == nullptr) return false;
    p->bound = true;
    p->director = true;
    return seek(session, 0.0F);
}

void unbind(Session* session) noexcept {
    if (auto* p = player(session)) p->bound = false;
}

bool bound(const Session* session) noexcept {
    const auto* p = player(session);
    return p != nullptr && p->bound;
}

float end_frame(const Session* session) noexcept {
    const auto* p = player(session);
    return p != nullptr ? p->doc.end_frame : 0.0F;
}

bool seek(Session* session, float frame) noexcept {
    auto* p = player(session);
    if (p == nullptr || !std::isfinite(frame)) return false;
    try {
        p->frame = std::clamp(frame, 0.0F, std::max(0.0F, p->doc.end_frame));
        update_shot(*p);
        pose_tracks(*session, *p);
        apply_visibility(*session, *p);
        return true;
    } catch (...) {
        return false;
    }
}

bool set_director(Session* session, bool on) noexcept {
    auto* p = player(session);
    if (p == nullptr) return false;
    p->director = on;
    return true;
}

bool director(const Session* session) noexcept {
    const auto* p = player(session);
    return p != nullptr && p->director;
}

std::optional<DirectorView> take_camera(Session* session) noexcept {
    auto* p = player(session);
    if (p == nullptr || !p->director || !p->shot) return std::nullopt;
    p->director = false;
    return view_of(p->shot->pose);
}

std::string status_line(const Session* session) {
    const auto* p = player(session);
    if (p == nullptr) return {};
    char text[160];
    if (p->shot) {
        std::snprintf(text, sizeof text, "%s \xC2\xB7 frame %.0f / %.0f \xC2\xB7 fov %.1f%s", p->shot->camera.c_str(),
                      static_cast<double>(p->frame), static_cast<double>(p->doc.end_frame),
                      static_cast<double>(p->shot->pose.fov_degrees), p->director ? "" : " \xC2\xB7 free camera");
    } else {
        std::snprintf(text, sizeof text, "frame %.0f / %.0f", static_cast<double>(p->frame),
                      static_cast<double>(p->doc.end_frame));
    }
    return text;
}

}  // namespace dmcresource::demo
