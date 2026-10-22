#include "dmcresource/demo_session.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "dmc_rengine/formats/mod/world_transform.hpp"
#include "dmc_rengine/formats/scm.hpp"
#include "dmc_rengine/formats/scm_transform.hpp"
#include "dmcresource/effect_host.h"
#include "dmcresource/matrix_ops.h"
#include "dmcresource/motion/motion_clip.h"
#include "dmcresource/motion/model_cloth.h"
#include "dmcresource/motion/motion_player.h"
#include "dmcresource/spider/session_actions.h"
#include "dmcresource/stage_room.h"

namespace dmcresource::demo {

namespace world = dmc::rengine::formats::mod::world_transform;

// A stage piece's own motion: its scene nodes driven by a MOT (the SCM node
// records have the MOD joint record layout: translation, rotation xyz), every
// object moving rigidly with its node. The MOT gives each node's channels;
// the node's matrix is built the SCM way (its rotation order is not the MOD
// joints'), so the rest pose is the piece's own.
struct StageMotion final {
    std::shared_ptr<motion::SkeletonRig> rig;
    std::vector<std::int32_t> parents;
    std::vector<float> magnitudes;  // SCM translation magnitude per node
    // The objects' own vertices (SCM meshes are local to their node), in the
    // piece's vertex order.
    std::vector<Vec3> local_vertices;
    std::optional<motion::MotionClip> clip;
    float last_local{-1.0F};
};

namespace {

constexpr float kHalfFovRadians = 0.5F;  // the viewer's framing at zoom 1 (view_renderer.cpp)
constexpr float kDegrees = 3.14159265358979323846F / 180.0F;

// A PNST of the given slots, every slot 16-byte aligned.
[[nodiscard]] std::vector<std::uint8_t> make_pnst(const std::vector<std::span<const std::uint8_t>>& slots) {
    const auto align = [](std::size_t v) { return (v + 15U) & ~std::size_t{15U}; };
    std::size_t at = align(8U + slots.size() * 4U);
    std::vector<std::uint32_t> offsets;
    for (const auto& slot : slots) {
        offsets.push_back(static_cast<std::uint32_t>(at));
        at = align(at + slot.size());
    }
    std::vector<std::uint8_t> out(at, 0U);
    std::memcpy(out.data(), "PNST", 4U);
    const auto count = static_cast<std::uint32_t>(slots.size());
    std::memcpy(out.data() + 4U, &count, 4U);
    for (std::size_t i = 0; i < slots.size(); ++i) {
        std::memcpy(out.data() + 8U + i * 4U, &offsets[i], 4U);
        if (!slots[i].empty()) std::memcpy(out.data() + offsets[i], slots[i].data(), slots[i].size());
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

[[nodiscard]] bool near_identity(const Matrix4& m) noexcept {
    const Matrix4 identity{};
    for (std::size_t i = 0; i < 16U; ++i) {
        if (!(std::fabs(m.values[i] - identity.values[i]) < 1.0e-3F)) return false;
    }
    return true;
}

[[nodiscard]] Vec3 transform(const Vec3& p, const Matrix4& m) noexcept {
    const auto& v = m.values;
    return {p.x * v[0] + p.y * v[4] + p.z * v[8] + v[12], p.x * v[1] + p.y * v[5] + p.z * v[9] + v[13],
            p.x * v[2] + p.y * v[6] + p.z * v[10] + v[14]};
}

// A scene node's local matrix from channels, the SCM way (scale applied
// first: rows of the basis scaled).
[[nodiscard]] world::Matrix4f scm_local(const motion::JointChannels& c, float magnitude) noexcept {
    namespace scm = dmc::rengine::formats::scm;
    scm::SceneTransform t;
    t.translation = {c.translation[0], c.translation[1], c.translation[2]};
    t.translation_magnitude = magnitude;
    t.rotation_xyz_radians = {c.rotation[0], c.rotation[1], c.rotation[2]};
    const auto built = scm::build_local_transform(t);
    world::Matrix4f out;
    out.values = built.values;
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 3; ++col) out.values[row * 4U + col] *= c.scale[row];
    }
    return out;
}

[[nodiscard]] Vec3 transform(const Vec3& p, const world::Matrix4f& m) noexcept {
    const auto& v = m.values;
    return {p.x * v[0] + p.y * v[4] + p.z * v[8] + v[12], p.x * v[1] + p.y * v[5] + p.z * v[9] + v[13],
            p.x * v[2] + p.y * v[6] + p.z * v[10] + v[14]};
}

// World matrices from locals: world = local x parent world.
[[nodiscard]] std::vector<world::Matrix4f> compose_worlds(std::span<const world::Matrix4f> locals,
                                                          std::span<const std::int32_t> parents) {
    const std::size_t n = locals.size();
    std::vector<world::Matrix4f> out(n, world::identity_matrix());
    std::vector<std::uint8_t> state(n, 0U);  // 0 open, 1 in progress, 2 done
    const auto solve = [&](auto& self, std::size_t i) -> void {
        if (state[i] == 2U) return;
        if (state[i] == 1U) {  // a cycle: treat as a root
            out[i] = locals[i];
            state[i] = 2U;
            return;
        }
        state[i] = 1U;
        const auto parent = i < parents.size() ? parents[i] : -1;
        if (parent >= 0 && static_cast<std::size_t>(parent) < n && static_cast<std::size_t>(parent) != i) {
            self(self, static_cast<std::size_t>(parent));
            out[i] = world::multiply_dmc3_matrices(locals[i], out[static_cast<std::size_t>(parent)]);
        } else {
            out[i] = locals[i];
        }
        state[i] = 2U;
    };
    for (std::size_t i = 0; i < n; ++i) solve(solve, i);
    return out;
}

// The objects of a track's model: its meshes in order, each with its object
// and node; one range for the whole model when the projection is not the
// plain concatenation of its meshes.
[[nodiscard]] std::vector<ObjectRange> object_ranges(const Session& model) {
    std::vector<ObjectRange> out;
    std::size_t vertices = 0, triangles = 0;
    for (const auto& primitive : model.scene.meshes) {
        ObjectRange r;
        r.object = primitive.object_index;
        r.node = primitive.node_index;
        r.first_vertex = vertices;
        r.vertex_count = primitive.mesh.vertices.size();
        r.first_triangle = triangles;
        r.triangle_count = primitive.mesh.indices.size() / 3U;
        vertices += r.vertex_count;
        triangles += r.triangle_count;
        out.push_back(r);
    }
    if (vertices != model.render_mesh.vertices.size() || triangles * 3U != model.render_mesh.indices.size()) {
        out.assign(1U, ObjectRange{0U, -1, 0U, model.render_mesh.vertices.size(), 0U,
                                   model.render_mesh.indices.size() / 3U});
    }
    return out;
}

// The scene-node rig of an SCM piece, checked against the piece's own rest
// worlds; nullptr when they differ (the piece then stays still).
[[nodiscard]] std::shared_ptr<StageMotion> make_stage_motion(const Session& piece, std::span<const std::uint8_t> bytes) {
    namespace scm = dmc::rengine::formats::scm;
    const auto parsed = scm::Parser::parse(std::as_bytes(bytes));
    if (!parsed.ok()) return nullptr;
    const auto& transforms = parsed.document.scene_nodes.transform_by_node_index;
    const std::size_t count = transforms.size();
    if (count == 0U || count != piece.scene.nodes.size()) return nullptr;
    auto out = std::make_shared<StageMotion>();
    out->rig = std::make_shared<motion::SkeletonRig>();
    auto& records = out->rig->domain.local_transform_records_by_node_index;
    records.resize(count);
    std::vector<world::Matrix4f> rest_locals(count);
    for (std::size_t n = 0; n < count; ++n) {
        const auto& t = transforms[n];
        auto& r = records[n];
        r.translation = {t.translation.x, t.translation.y, t.translation.z};
        r.translation_magnitude = t.translation_magnitude;
        r.rotation_xyz_radians = {t.rotation_xyz_radians.x, t.rotation_xyz_radians.y, t.rotation_xyz_radians.z};
        motion::JointChannels channels;
        channels.translation = {r.translation.x, r.translation.y, r.translation.z};
        channels.rotation = {r.rotation_xyz_radians.x, r.rotation_xyz_radians.y, r.rotation_xyz_radians.z};
        rest_locals[n] = scm_local(channels, t.translation_magnitude);
        out->parents.push_back(piece.scene.nodes[n].parent);
        out->magnitudes.push_back(t.translation_magnitude);
    }
    const auto rest = compose_worlds(rest_locals, out->parents);
    for (std::size_t n = 0; n < count; ++n) {
        const auto& a = rest[n].values;
        const auto& b = piece.scene.nodes[n].world.values;
        for (std::size_t k = 0; k < 16; ++k) {
            if (std::fabs(a[k] - b[k]) > 1.0e-2F * std::max(1.0F, std::fabs(b[k]))) return nullptr;
        }
    }
    for (const auto& primitive : piece.scene.meshes) {
        out->local_vertices.insert(out->local_vertices.end(), primitive.mesh.vertices.begin(),
                                   primitive.mesh.vertices.end());
    }
    if (out->local_vertices.size() != piece.render_mesh.vertices.size()) return nullptr;
    return out;
}

// Which objects of a track are shown at the player's frame: every Hide clip
// set up to the frame applies in turn (H hides the model, D shows it, A plays
// a .hid: its objects take its states; DispOn / DispOff are .hid files that
// show / hide objects 0..99). Before any, everything is shown.
void hide_state(const Player& p, const PlayerTrack& t, std::vector<std::uint8_t>* shown) {
    std::fill(shown->begin(), shown->end(), std::uint8_t{1U});
    const auto* list = p.doc.list("Hide", t.track);
    if (list == nullptr) return;
    std::vector<const Clip*> applied;
    for (const auto& clip : list->clips) {
        if (clip.set_frame <= p.frame) applied.push_back(&clip);
    }
    std::stable_sort(applied.begin(), applied.end(),
                     [](const Clip* a, const Clip* b) { return a->set_frame < b->set_frame; });
    for (const auto* clip : applied) {
        const auto kind = clip->value("HideKind");
        if (kind == "H" || kind == "D") {
            std::fill(shown->begin(), shown->end(), static_cast<std::uint8_t>(kind == "D" ? 1U : 0U));
            continue;
        }
        const auto* hid = p.doc.hide(clip->value("Hide"));
        if (hid == nullptr) continue;
        for (const auto& o : hid->objects) {
            if (o.object < shown->size()) (*shown)[o.object] = o.shown ? 1U : 0U;
        }
    }
}

// Keeps the shown objects' triangles in the composite and in the room.
// Returns whether the room's triangles changed.
bool apply_visibility(Session& scene, Player& p, bool force) {
    bool models_changed = force, room_changed = force;
    std::vector<std::uint8_t> next;
    for (auto& t : p.tracks) {
        next.assign(t.shown.size(), 1U);
        hide_state(p, t, &next);
        if (next != t.shown) {
            t.shown = next;
            (t.stage ? room_changed : models_changed) = true;
        }
    }
    const auto rebuild = [&](bool stage, const std::vector<std::uint32_t>& all, const std::vector<std::uint32_t>& all_slots,
                             const std::vector<std::uint8_t>* all_translucent, std::vector<std::uint32_t>& indices,
                             std::vector<std::uint32_t>& slots, std::vector<std::uint8_t>* translucent) {
        if (all.empty()) return;
        indices.clear();
        slots.clear();
        if (translucent != nullptr) translucent->clear();
        const std::size_t triangles = all.size() / 3U;
        const bool with_slots = all_slots.size() == triangles;
        const bool with_translucent = all_translucent != nullptr && all_translucent->size() == triangles;
        for (const auto& t : p.tracks) {
            if (t.stage != stage) continue;
            for (const auto& r : t.objects) {
                if (r.object < t.shown.size() && t.shown[r.object] == 0U) continue;
                const std::size_t first = t.triangle_begin + r.first_triangle;
                if (first + r.triangle_count > triangles) continue;
                indices.insert(indices.end(), all.begin() + static_cast<std::ptrdiff_t>(first * 3U),
                               all.begin() + static_cast<std::ptrdiff_t>((first + r.triangle_count) * 3U));
                if (with_slots) {
                    slots.insert(slots.end(), all_slots.begin() + static_cast<std::ptrdiff_t>(first),
                                 all_slots.begin() + static_cast<std::ptrdiff_t>(first + r.triangle_count));
                }
                if (with_translucent) {
                    translucent->insert(translucent->end(), all_translucent->begin() + static_cast<std::ptrdiff_t>(first),
                                        all_translucent->begin() + static_cast<std::ptrdiff_t>(first + r.triangle_count));
                }
            }
        }
        // An empty mesh still draws (one degenerate triangle).
        if (indices.empty()) {
            indices = {0U, 0U, 0U};
            if (with_slots) slots = {kNoTextureSlot};
            if (with_translucent) translucent->assign(1U, 0U);
        }
    };
    if (models_changed) {
        rebuild(false, p.all_indices, p.all_triangle_slots, nullptr, scene.render_mesh.indices,
                scene.render_triangle_texture_slots, nullptr);
    }
    if (room_changed && p.room) {
        rebuild(true, p.room_indices, p.room_triangle_slots, &p.room_translucent, p.room->mesh.indices,
                p.room->triangle_texture_slots, &p.room->translucent_triangles);
    }
    return room_changed;
}

// A cutscene MOT keeps the frame range it was cut from, like a camera:
// f32 start at +0x08 and end at +0x0C (repeated at +0x10 / +0x14), and its
// keys sit on those frames: pl100_00_0005 covers 88..468 for its 380-frame
// clip. A range that starts below zero (pl100_00_c010, -70..565) has its
// keys count from its start, so its clip plays from 0. 0 without a range.
[[nodiscard]] float mot_start(const Player& p, std::string_view name) noexcept {
    const auto* member = p.doc.member(name);
    if (member == nullptr || member->size < 0x18U) return 0.0F;
    float start = 0.0F, end = 0.0F;
    std::memcpy(&start, p.bytes->data() + member->offset + 0x08U, 4U);
    std::memcpy(&end, p.bytes->data() + member->offset + 0x0CU, 4U);
    return std::isfinite(start) && std::isfinite(end) && start > 0.0F && end >= start ? start : 0.0F;
}

// The Motion clip of a track at the player's frame and the time of its MOT:
// the MOT's start frame plus the clip's own time (CutFrame trims the clip's
// end, it does not move its start: Life already spans the MOT's range).
[[nodiscard]] const Clip* motion_clip(const Player& p, int track, float* time) {
    const auto* list = p.doc.list("Motion", track);
    if (list == nullptr || list->clips.empty()) return nullptr;
    float local = 0.0F;
    const auto* clip = clip_at(*list, p.frame, &local);
    if (clip != nullptr) *time = mot_start(p, clip->value("Motion")) + std::clamp(local, 0.0F, clip->life);
    return clip;
}

// A stage piece at the player's frame; returns whether its vertices moved.
bool pose_stage_piece(Player& p, PlayerTrack& t) {
    if (!t.stage_motion || !p.room) return false;
    auto& sm = *t.stage_motion;
    float time = 0.0F;
    const auto* clip = motion_clip(p, t.track, &time);
    if (clip == nullptr) return false;
    const auto name = clip->value("Motion");
    if (name != t.motion) {
        if (name == t.failed_motion) return false;
        const auto* member = p.doc.member(name);
        std::optional<motion::MotionClip> bound;
        if (member != nullptr) {
            auto made = motion::MotionClip::bind(
                std::as_bytes(std::span<const std::uint8_t>{p.bytes->data() + member->offset, member->size}), *sm.rig,
                nullptr, true);
            if (made) bound = std::move(*made);
        }
        if (!bound) {
            t.failed_motion = name;
            t.failure = "the MOT does not drive the piece's scene nodes";
            return false;
        }
        sm.clip = std::move(bound);
        t.motion = name;
        sm.last_local = -1.0F;
    }
    if (!sm.clip || time == sm.last_local) return false;
    std::vector<motion::JointChannels> channels(sm.rig->node_count());
    if (!sm.clip->evaluate_channels(time, channels)) return false;
    sm.last_local = time;
    std::vector<world::Matrix4f> locals(channels.size());
    for (std::size_t n = 0; n < channels.size(); ++n) {
        locals[n] = scm_local(channels[n], n < sm.magnitudes.size() ? sm.magnitudes[n] : 0.0F);
    }
    const auto worlds = compose_worlds(locals, sm.parents);
    auto& out = p.room->mesh.vertices;
    if (t.vertex_begin + t.vertex_count > out.size() || sm.local_vertices.size() != t.vertex_count) return false;
    for (const auto& r : t.objects) {
        const bool moved = r.node >= 0 && static_cast<std::size_t>(r.node) < worlds.size();
        for (std::size_t v = r.first_vertex; v < r.first_vertex + r.vertex_count && v < t.vertex_count; ++v) {
            out[t.vertex_begin + v] =
                moved ? transform(sm.local_vertices[v], worlds[static_cast<std::size_t>(r.node)]) : sm.local_vertices[v];
        }
    }
    return true;
}

// Poses every track at the player's frame: models into the scene (free
// tracks first, then the ones hanging from a joint of an already posed
// track), stage pieces into the room. Returns whether the room moved.
// The cloth of a track at the player's frame: the .clt of the last Motion
// clip set up to it that names one ("Cloth pl100_01.clt"), whether it runs
// (the last "ClothMode": 1 simulates, as the hair, amulet and em105 parts
// ask; 0 leaves the bones to the MOT, as m01_c00's coat, carried over the
// shoulder, is baked; 0 until a clip says), and the last reset ("ResetClt" /
// "ResetClt2", settled "ResetCltLevel" frames, 20 without).
struct ClothCue final {
    std::string file;
    bool on{false};
    float reset{-1.0F};
    std::uint32_t level{20U};
};

[[nodiscard]] ClothCue cloth_cue(const Player& p, int track) {
    ClothCue out;
    const auto* list = p.doc.list("Motion", track);
    if (list == nullptr) return out;
    float set = -1.0F, mode_set = -1.0F;
    for (const auto& clip : list->clips) {
        if (clip.set_frame > p.frame) continue;
        bool reset = false;
        int level = -1;
        for (const auto& [key, values] : clip.fields) {
            if (key == "Cloth" && !values.empty() && clip.set_frame >= set) {
                out.file = values.front();
                set = clip.set_frame;
            }
            if (key == "ClothMode" && !values.empty() && clip.set_frame >= mode_set) {
                out.on = std::atoi(values.front().c_str()) != 0;
                mode_set = clip.set_frame;
            }
            if (key == "ResetClt" || key == "ResetClt2") reset = true;
            if (key == "ResetCltLevel" && !values.empty()) level = std::atoi(values.front().c_str());
        }
        if (reset && clip.set_frame >= out.reset) {
            out.reset = clip.set_frame;
            out.level = level > 0 ? static_cast<std::uint32_t>(std::min(level, 600)) : 20U;
        }
    }
    return out;
}

// Steps the track's cloth to the player's frame and writes its world
// vertices: one solver step a game frame while the timeline plays forward; a
// reset clip crossed, a jump (a scrub, a step back) or a new .clt restarts it
// at the pose and settles it. False when the track has no cloth.
[[nodiscard]] bool pose_cloth(const Player& p, PlayerTrack& t, const Session& model, const Matrix4& placement,
                              std::vector<Vec3>* out) {
    const auto cue = cloth_cue(p, t.track);
    if (cue.file.empty() || !cue.on) {
        t.cloth.reset();
        t.cloth_file.clear();
        return false;
    }
    if (cue.file != t.cloth_file) {
        t.cloth_file = cue.file;
        t.cloth_frame = -1.0F;
        t.cloth.reset();
        if (const auto* member = p.doc.member(cue.file)) {
            const std::string_view text{reinterpret_cast<const char*>(p.bytes->data() + member->offset), member->size};
            t.cloth = motion::make_model_cloth(model, text);
        }
    }
    if (!t.cloth) return false;
    std::uint32_t steps = 0U;
    const float delta = p.frame - t.cloth_frame;
    const bool crossed = cue.reset >= 0.0F && cue.reset > t.cloth_frame;
    if (t.cloth_frame < 0.0F || delta < 0.0F || delta > 30.0F || crossed) {
        t.cloth->state.initialized = false;
        steps = cue.level;
        t.cloth_frame = p.frame;
    } else {
        steps = static_cast<std::uint32_t>(delta);
        t.cloth_frame += static_cast<float>(steps);
    }
    return motion::pose_model_cloth(model, placement, *t.cloth, steps, out);
}

bool pose_tracks(Session& scene, Player& p) {
    bool room_moved = false;
    auto& out = scene.render_mesh.vertices;
    std::vector<char> done(p.tracks.size(), 0);
    const auto find = [&](int track) -> int {
        for (std::size_t i = 0; i < p.tracks.size(); ++i) {
            if (!p.tracks[i].stage && p.tracks[i].track == track) return static_cast<int>(i);
        }
        return -1;
    };
    for (int pass = 0; pass < 4; ++pass) {
        for (std::size_t i = 0; i < p.tracks.size(); ++i) {
            if (done[i] != 0) continue;
            auto& t = p.tracks[i];
            if (t.stage) {
                done[i] = 1;
                room_moved = pose_stage_piece(p, t) || room_moved;
                continue;
            }
            const int host = t.host_track >= 0 ? find(t.host_track) : -1;
            if (host >= 0 && done[static_cast<std::size_t>(host)] == 0 && pass < 3) continue;
            done[i] = 1;
            if (!t.source) continue;
            Session& model = *t.source;
            float time = 0.0F;
            if (const auto* clip = motion_clip(p, t.track, &time)) {
                const auto name = clip->value("Motion");
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
                if (name == t.motion) (void)motion::apply_motion_frame(&model, time);
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
            Matrix4 placement{};
            if (joint != nullptr) {
                const Matrix4 base = t.limb && model.scene.nodes.size() > 1U ? affine_inverse(model.scene.nodes[1].world)
                                                                             : t.host_rest_inverse;
                if (!matrix_ops::multiply(base, *joint, &placement)) placement = {};
            }
            t.placement = placement;
            if (std::vector<Vec3> clothed; pose_cloth(p, t, model, placement, &clothed) && clothed.size() == t.vertex_count) {
                std::copy(clothed.begin(), clothed.end(), out.begin() + static_cast<std::ptrdiff_t>(t.vertex_begin));
                continue;
            }
            for (std::size_t v = 0; v < t.vertex_count; ++v) {
                const auto& src = model.render_mesh.vertices[v];
                out[t.vertex_begin + v] = joint != nullptr ? transform(src, placement) : src;
            }
        }
    }
    return room_moved;
}

// One frame of the cutscene: camera, poses, visibility.
void advance(Session& scene, Player& p, bool force) {
    update_shot(p);
    const bool moved = pose_tracks(scene, p);
    const bool hidden = apply_visibility(scene, p, force);
    if ((moved || hidden) && p.room) ++p.room->revision;
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

        // Stage pieces to the room; models to the composite. Every track
        // keeps its model on its own and the ranges of its objects.
        std::vector<std::unique_ptr<Session>> models, pieces;
        std::vector<PlayerTrack> tracks, stage_tracks;
        for (const auto& track : doc->tracks()) {
            if (track.model == nullptr || track.model->clips.empty()) continue;
            const auto& clip = track.model->clips.front();
            const auto model = clip.value("Model");
            const auto texture = clip.value("Texture");
            const auto model_bytes = member_bytes(model);
            if (model_bytes.empty()) continue;
            auto opened = open_session(model, model_bytes.data(), model_bytes.size());
            if (!opened || !opened->renderable || opened->render_mesh.indices.size() < 3U) continue;
            PlayerTrack t;
            t.track = track.index;
            t.model = model;
            t.texture = texture;
            t.vertex_count = opened->render_mesh.vertices.size();
            t.triangle_count = opened->render_mesh.indices.size() / 3U;
            t.objects = object_ranges(*opened);
            std::uint32_t objects = 0U;
            for (const auto& r : t.objects) objects = std::max(objects, r.object + 1U);
            t.shown.assign(std::max<std::uint32_t>(objects, 1U), 1U);
            if (opened->probe.format == Format::Scm) {
                t.stage = true;
                if (const auto tex = member_bytes(texture); !tex.empty()) {
                    (void)spider::actions::attach_ptx(opened.get(), texture, tex.data(), tex.size());
                }
                t.stage_motion = make_stage_motion(*opened, model_bytes);
                stage_tracks.push_back(std::move(t));
                pieces.push_back(std::move(opened));
                continue;
            }
            if (opened->probe.format != Format::Mod) continue;
            for (const auto& [key, values] : clip.fields) {
                if (key == "CnsPos" && values.size() >= 2U) {
                    t.host_track = std::atoi(values[0].c_str());
                    t.host_joint = std::atoi(values[1].c_str());
                }
            }
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

        // The stage: the pieces in their order, so piece k's vertices and
        // triangles follow the previous ones'.
        std::shared_ptr<stage_room::Room> room;
        if (!pieces.empty()) {
            std::vector<const Session*> parts;
            std::vector<std::string> names;
            for (std::size_t i = 0; i < pieces.size(); ++i) {
                parts.push_back(pieces[i].get());
                names.push_back(stage_tracks[i].model);
            }
            room = stage_room::assemble_room(name, parts, names);
        }
        if (room) {
            std::size_t vertices = 0, triangles = 0;
            for (auto& t : stage_tracks) {
                t.vertex_begin = vertices;
                vertices += t.vertex_count;
                t.triangle_begin = triangles;
                triangles += t.triangle_count;
            }
            if (vertices == room->mesh.vertices.size() && triangles * 3U == room->mesh.indices.size()) {
                for (std::size_t i = 0; i < stage_tracks.size(); ++i) stage_tracks[i].source = std::move(pieces[i]);
                state->room_indices = room->mesh.indices;
                state->room_triangle_slots = room->triangle_texture_slots;
                state->room_translucent = room->translucent_triangles;
            } else {
                stage_tracks.clear();  // not the plain concatenation: keep the stage still
            }
        } else {
            stage_tracks.clear();
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

        // Vertex ranges of the model tracks in the composite (sources in order).
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
        // A CnsPos model's root sits on its joint: the inverse of the root's
        // rest world (m01_c00: pl100_01's root rests at the head joint, at
        // 0 166 -1; pl100_03's at 0, in the forearm's frame).
        for (auto& t : tracks) {
            if (t.host_track < 0 || t.host_joint < 0 || !t.source || t.source->scene.nodes.empty()) continue;
            t.host_rest_inverse = affine_inverse(t.source->scene.nodes.front().world);
        }
        // Limb parts ride the body joint their node 1 is: the joint of a model
        // of the same character (pl100_03 and pl100_10 are both "pl100")
        // whose child sits where the part's node 2 does (pl100_03's wrist
        // 26.86 along x: pl100_10 joint 12 -> 13; pl100_04's: joint 8 -> 9).
        for (auto& t : tracks) {
            if (t.host_track >= 0 || !t.source) continue;
            const auto& nodes = t.source->scene.nodes;
            if (nodes.size() < 3U || nodes[1].parent != 0 || nodes[2].parent != 1 || !near_identity(nodes[0].world) ||
                !near_identity(nodes[1].world)) {
                continue;
            }
            const auto& w = nodes[2].local.values;
            if (std::hypot(w[12], w[13], w[14]) < 1.0F) continue;
            const auto stem = t.model.substr(0, t.model.find('_'));
            for (const auto& body : tracks) {
                if (&body == &t || !body.source || body.host_track >= 0 || body.model.substr(0, body.model.find('_')) != stem) {
                    continue;
                }
                const auto& joints = body.source->scene.nodes;
                for (std::size_t c = 0; c < joints.size() && !t.limb; ++c) {
                    const auto& l = joints[c].local.values;
                    if (joints[c].parent < 0 || std::hypot(l[12] - w[12], l[13] - w[13], l[14] - w[14]) > 0.05F) continue;
                    t.host_track = body.track;
                    t.host_joint = joints[c].parent;
                    t.limb = true;
                }
                if (t.limb) break;
            }
        }
        if (!tracks.empty()) {
            state->all_indices = scene->render_mesh.indices;
            state->all_triangle_slots = scene->render_triangle_texture_slots;
        }
        const std::size_t stage_count = stage_tracks.size();
        for (auto& t : stage_tracks) tracks.push_back(std::move(t));
        state->tracks = std::move(tracks);
        state->doc = std::move(*doc);
        state->room = room;
        // The effect bank, held by a host session like a stage's.
        // A stage's FXBANK is a PNST of its manifest and its records; the
        // cutscene keeps the two as a name list / container pair: put them
        // together the same way.
        const auto& d = state->doc;
        if (d.effect_bank_size > 0U && d.effect_bank_offset + d.effect_bank_size <= size &&
            d.effect_manifest_offset + d.effect_manifest_size <= size) {
            auto bank = std::make_shared<const std::vector<std::uint8_t>>(make_pnst(
                {data.subspan(d.effect_manifest_offset, d.effect_manifest_size),
                 data.subspan(d.effect_bank_offset, d.effect_bank_size)}));
            if (auto loaded = load_effect_bank(std::move(bank), 0U)) {
                state->effect_host = std::make_shared<Session>();
                state->effect_host->archive_name = std::string{name};
                state->effect_host->effect_banks.push_back(std::move(*loaded));
            }
        }

        // The archive's identity, Info and files stay. The scene is drawn in
        // 3D, never as a picture: the timeline picture becomes the first of
        // its files.
        scene->probe = archive.probe;
        scene->capabilities |= archive.capabilities & ~capability(ResourceCapability::ImagePreview);
        scene->inspection = std::move(archive.inspection);
        scene->children.clear();
        if (archive.image_preview.available()) {
            ChildResource timeline;
            timeline.id = "timeline";
            timeline.title = "Timeline " + state->doc.name;
            timeline.suggested_filename = state->doc.name + "_timeline.png";
            timeline.probe = archive.probe;
            timeline.capabilities = capability(ResourceCapability::ImagePreview);
            timeline.image_preview = std::move(archive.image_preview);
            scene->children.push_back(std::move(timeline));
        }
        for (auto& child : archive.children) scene->children.push_back(std::move(child));
        scene->archive_name = std::string{name};
        scene->source_name = std::string{name};
        scene->stage = room;
        scene->detail = archive.detail + " | scene: " + std::to_string(state->tracks.size() - stage_count) +
                        " model track(s), " + std::to_string(stage_count) + " stage piece(s)";
        // The timeline is the scene's one motion (motion_player.h plays it).
        scene->motion_library.clear();
        Session::MotionPayload timeline;
        timeline.name = "Cutscene " + state->doc.name;
        scene->motion_library.push_back(std::move(timeline));
        scene->demo = std::move(state);
        advance(*scene, *scene->demo, true);
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
        advance(*session, *p, false);
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
    const auto& pose = p->shot->pose;
    p->free_focus = std::hypot(pose.target.x - pose.eye.x, pose.target.y - pose.eye.y, pose.target.z - pose.eye.z);
    return view_of(pose);
}

float free_camera_focus(const Session& session) noexcept {
    const auto* p = player(&session);
    return p == nullptr || p->director || !std::isfinite(p->free_focus) ? 0.0F : p->free_focus;
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

// ---- effects ---------------------------------------------------------------

namespace {

[[nodiscard]] std::array<float, 3> floats3(const Clip& clip, std::string_view key, float fallback) {
    std::array<float, 3> out{fallback, fallback, fallback};
    for (const auto& [name, values] : clip.fields) {
        if (name != key) continue;
        for (std::size_t i = 0; i < 3 && i < values.size(); ++i) {
            char* end = nullptr;
            const float v = std::strtof(values[i].c_str(), &end);
            if (end != values[i].c_str() && std::isfinite(v)) out[i] = v;
        }
        break;
    }
    return out;
}

[[nodiscard]] float float_value(const Clip& clip, std::string_view key, float fallback) {
    const auto text = clip.value(key);
    char* end = nullptr;
    const float v = std::strtof(text.c_str(), &end);
    return end != text.c_str() && std::isfinite(v) ? v : fallback;
}

[[nodiscard]] Matrix4 multiply(const Matrix4& a, const Matrix4& b) noexcept {
    Matrix4 out;
    for (std::size_t r = 0; r < 4; ++r) {
        for (std::size_t c = 0; c < 4; ++c) {
            float sum = 0.0F;
            for (std::size_t k = 0; k < 4; ++k) sum += a.values[r * 4U + k] * b.values[k * 4U + c];
            out.values[r * 4U + c] = sum;
        }
    }
    return out;
}

// Pos / Rot (radians, x then y then z) / Sca as a row-vector matrix.
[[nodiscard]] Matrix4 placement_matrix(const std::array<float, 3>& pos, const std::array<float, 3>& rot,
                                       const std::array<float, 3>& scale) noexcept {
    const auto turn = [&](Vec3 v) {
        const auto axis = [](float& a, float& b, float r) {
            const float c = std::cos(r), s = std::sin(r);
            const float na = a * c - b * s, nb = a * s + b * c;
            a = na;
            b = nb;
        };
        axis(v.y, v.z, rot[0]);
        axis(v.z, v.x, rot[1]);
        axis(v.x, v.y, rot[2]);
        return v;
    };
    const Vec3 rows[3] = {turn({1.0F, 0.0F, 0.0F}), turn({0.0F, 1.0F, 0.0F}), turn({0.0F, 0.0F, 1.0F})};
    Matrix4 m;
    for (std::size_t r = 0; r < 3; ++r) {
        m.values[r * 4U + 0U] = rows[r].x * scale[r];
        m.values[r * 4U + 1U] = rows[r].y * scale[r];
        m.values[r * 4U + 2U] = rows[r].z * scale[r];
        m.values[r * 4U + 3U] = 0.0F;
    }
    m.values[12] = pos[0];
    m.values[13] = pos[1];
    m.values[14] = pos[2];
    m.values[15] = 1.0F;
    return m;
}

// The world of joint `joint` of track `track` now (a model's posed joint
// where its placement puts it, a stage piece's scene node); none when there
// is no such joint.
[[nodiscard]] std::optional<Matrix4> joint_world(const Player& p, int track, int joint) noexcept {
    if (track < 0 || joint < 0) return std::nullopt;
    for (const auto& t : p.tracks) {
        if (t.track != track || !t.source) continue;
        const auto& nodes = t.source->scene.nodes;
        if (static_cast<std::size_t>(joint) >= nodes.size()) return std::nullopt;
        const auto& node = nodes[static_cast<std::size_t>(joint)].world;
        if (t.stage) return node;
        Matrix4 out{};
        return matrix_ops::multiply(node, t.placement, &out) ? std::optional<Matrix4>{out} : std::optional<Matrix4>{node};
    }
    return std::nullopt;
}

constexpr std::size_t kMaxEffects = 96;

}  // namespace

std::shared_ptr<const Session> effect_host(const Session& session) noexcept {
    const auto* p = player(&session);
    return p != nullptr ? p->effect_host : nullptr;
}

std::vector<EffectShot> active_effects(const Session& session) {
    std::vector<EffectShot> out;
    const auto* p = player(&session);
    if (p == nullptr || !p->effect_host) return out;
    for (const auto& list : p->doc.lists) {
        if (list.kind != "Effect" && list.kind != "EffectI") continue;
        for (const auto& clip : list.clips) {
            const float elapsed = p->frame - clip.set_frame;
            if (elapsed < 0.0F) continue;
            const float local = clip.clip_scale > 0.0F ? elapsed / clip.clip_scale : elapsed;
            // CutFrame stops the effect (0: it runs its own length).
            if (clip.cut_frame > 0.0F && local >= clip.cut_frame) continue;
            const float age = local - float_value(clip, "WaitTimer", 0.0F);
            if (age < 0.0F) continue;
            const auto id_text = clip.value("Id");
            const auto number = clip.value("No");
            if (id_text.size() != 1U || number.empty()) continue;
            EffectShot shot;
            shot.kind = id_text[0];
            shot.id = static_cast<std::uint16_t>(std::clamp(std::atoi(number.c_str()), 0, 0xFFFF));
            shot.age = age;
            shot.cut = clip.cut_frame > 0.0F;
            shot.world = placement_matrix(floats3(clip, "Pos", 0.0F), floats3(clip, "Rot", 0.0F),
                                          floats3(clip, "Sca", 1.0F));
            // SpFlag: set on joint SpBoneNo of the model of SpTrackNo, which
            // counts the script's Set lines (WorkRate 0, Camera 1, Model 00
            // 2, Motion 00 3, Hide 00 4, Model 01 5, ...).
            if (float_value(clip, "SpFlag", 0.0F) != 0.0F) {
                const int line = static_cast<int>(float_value(clip, "SpTrackNo", -1.0F));
                int track = line;
                if (line >= 0 && static_cast<std::size_t>(line) < p->doc.lists.size() &&
                    p->doc.lists[static_cast<std::size_t>(line)].kind == "Model") {
                    track = p->doc.lists[static_cast<std::size_t>(line)].track;
                }
                const auto joint = joint_world(*p, track, static_cast<int>(float_value(clip, "SpBoneNo", -1.0F)));
                if (joint) shot.world = multiply(shot.world, *joint);
            }
            out.push_back(shot);
            if (out.size() >= kMaxEffects) return out;
        }
    }
    return out;
}

// ---- sound ---------------------------------------------------------------

bool attach_audio(Session* session, std::string_view name, std::span<const std::uint8_t> bytes, std::string* message) {
    auto* p = player(session);
    std::string line;
    bool ok = false;
    try {
        auto pcm = p != nullptr ? sound::decode_ogg(bytes) : std::nullopt;
        if (p == nullptr) {
            line = "Not a cutscene";
        } else if (!pcm || pcm->frames() == 0U) {
            line = std::string{name} + " is not an Ogg Vorbis sound";
        } else {
            std::vector<const Clip*> clips;
            if (const auto* list = p->doc.list("Sound")) {
                for (const auto& clip : list->clips) clips.push_back(&clip);
            }
            std::stable_sort(clips.begin(), clips.end(),
                             [](const Clip* a, const Clip* b) { return a->set_frame < b->set_frame; });
            if (clips.empty() || p->audio.size() >= clips.size()) p->audio.clear();
            const Clip* clip = clips.empty() ? nullptr : clips[p->audio.size()];
            Player::Audio audio;
            audio.name = std::string{name};
            audio.start_frame = clip != nullptr ? clip->set_frame : 0.0F;
            audio.pcm = std::move(*pcm);
            char text[256];
            std::snprintf(text, sizeof text, "Sound: %s from frame %.0f%s%s, %u Hz, %s, %.1f s", audio.name.c_str(),
                          static_cast<double>(audio.start_frame), clip != nullptr ? " (stream " : "",
                          clip != nullptr ? (clip->value("No") + ")").c_str() : "", audio.pcm.sample_rate,
                          audio.pcm.channels == 1U ? "mono" : "stereo",
                          static_cast<double>(audio.pcm.frames()) / static_cast<double>(audio.pcm.sample_rate));
            line = text;
            if (!p->audio.empty() && p->audio.front().pcm.sample_rate != audio.pcm.sample_rate) {
                line += " - its rate differs from the first sound's, it stays silent";
            }
            p->audio.push_back(std::move(audio));
            ok = true;
        }
    } catch (...) {
        line = "Sound could not be read";
        ok = false;
    }
    if (message != nullptr) *message = line;
    return ok;
}

std::optional<AudioFormat> audio_format(const Session* session) noexcept {
    const auto* p = player(session);
    if (p == nullptr || p->audio.empty()) return std::nullopt;
    AudioFormat format;
    format.sample_rate = p->audio.front().pcm.sample_rate;
    format.channels = 1U;
    for (const auto& a : p->audio) {
        if (a.pcm.sample_rate == format.sample_rate && a.pcm.channels >= 2U) format.channels = 2U;
    }
    return format;
}

std::int64_t audio_position(const Session* session, float frame) noexcept {
    const auto format = audio_format(session);
    if (!format || !std::isfinite(frame)) return 0;
    return std::llround(static_cast<double>(frame) / 60.0 * format->sample_rate);
}

std::vector<std::int16_t> audio_read(const Session* session, std::int64_t position, std::size_t frames) {
    const auto* p = player(session);
    const auto format = audio_format(session);
    if (p == nullptr || !format || frames == 0U) return {};
    const auto rate = static_cast<double>(format->sample_rate);
    const auto end = std::llround(static_cast<double>(std::max(p->doc.end_frame, 0.0F)) / 60.0 * rate);
    if (position >= end) return {};
    frames = static_cast<std::size_t>(std::min<std::int64_t>(static_cast<std::int64_t>(frames), end - position));
    const std::size_t channels = format->channels;
    std::vector<std::int32_t> mix(frames * channels, 0);
    for (const auto& a : p->audio) {
        if (a.pcm.sample_rate != format->sample_rate || a.pcm.channels == 0U) continue;
        const auto start = std::llround(static_cast<double>(a.start_frame) / 60.0 * rate);
        const auto length = static_cast<std::int64_t>(a.pcm.frames());
        const std::size_t source_channels = a.pcm.channels;
        for (std::size_t i = 0; i < frames; ++i) {
            const std::int64_t src = position + static_cast<std::int64_t>(i) - start;
            if (src < 0 || src >= length) continue;
            const auto* in = a.pcm.samples.data() + static_cast<std::size_t>(src) * source_channels;
            if (channels == 1U) {
                std::int32_t sum = 0;
                for (std::size_t c = 0; c < source_channels; ++c) sum += in[c];
                mix[i] += sum / static_cast<std::int32_t>(source_channels);
            } else {
                mix[i * 2U] += in[0];
                mix[i * 2U + 1U] += in[source_channels >= 2U ? 1U : 0U];
            }
        }
    }
    std::vector<std::int16_t> out(mix.size());
    for (std::size_t i = 0; i < mix.size(); ++i) out[i] = static_cast<std::int16_t>(std::clamp(mix[i], -32768, 32767));
    return out;
}

}  // namespace dmcresource::demo
