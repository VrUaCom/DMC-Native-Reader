#include "dmcresource/particle_sprt.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "dmcresource/motion/part_attachment.h"

namespace dmcresource::particle {
namespace {

constexpr float kPi = 3.14159250F;       // 0x140371918 (the EXE's float pi)
constexpr float kTwoPi = 6.28318501F;    // 0x140371920
constexpr float kTick = 1.0F;            // dt: one 60 Hz update

[[nodiscard]] std::int16_t s16(std::span<const std::uint8_t> b, std::size_t at) noexcept {
    if (at + 2U > b.size()) return 0;
    return static_cast<std::int16_t>(b[at] | (b[at + 1U] << 8U));
}
[[nodiscard]] std::uint16_t u16(std::span<const std::uint8_t> b, std::size_t at) noexcept {
    return static_cast<std::uint16_t>(s16(b, at));
}
[[nodiscard]] std::int32_t i32(std::span<const std::uint8_t> b, std::size_t at) noexcept {
    if (at + 4U > b.size()) return 0;
    std::uint32_t v = 0U;
    for (std::size_t i = 0U; i < 4U; ++i) v |= static_cast<std::uint32_t>(b[at + i]) << (8U * i);
    return static_cast<std::int32_t>(v);
}
[[nodiscard]] float f32(std::span<const std::uint8_t> b, std::size_t at) noexcept {
    const auto bits = static_cast<std::uint32_t>(i32(b, at));
    float v = 0.0F;
    std::memcpy(&v, &bits, sizeof v);
    return v;
}

[[nodiscard]] Track parse_track(std::span<const std::uint8_t> b, std::size_t at) {
    Track t;
    for (std::size_t k = 0U; k < 4U; ++k) {
        const std::size_t o = at + k * 20U;
        t.keys[k].duration = s16(b, o);
        for (std::size_t c = 0U; c < 4U; ++c) {
            t.keys[k].color[c] = o + 4U + c < b.size() ? b[o + 4U + c] : 0U;
        }
    }
    // +0x50 of the track block: enable, loop (0x1402D30E0 r9+0x50/+0x51).
    t.enabled = at + 0x50U < b.size() && b[at + 0x50U] != 0U;
    t.loop = at + 0x51U < b.size() && b[at + 0x51U] == 1U;
    return t;
}

[[nodiscard]] TransformMotion parse_motion(std::span<const std::uint8_t> b, std::size_t at) {
    TransformMotion m;
    for (std::size_t i = 0U; i < 3U; ++i) {
        m.translation[i] = s16(b, at + 2U * i);
        m.rotation[i] = s16(b, at + 6U + 2U * i);
        m.scale[i] = s16(b, at + 12U + 2U * i);
    }
    return m;
}

[[nodiscard]] std::array<float, 3> vec3_at(std::span<const std::uint8_t> b, std::size_t at) {
    return {f32(b, at), f32(b, at + 4U), f32(b, at + 8U)};
}

// ---- 4x4 helpers (row vector convention, as 0x140030A70) ------------------

using Mat = std::array<float, 16>;

[[nodiscard]] Mat identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }
[[nodiscard]] Mat mul(const Mat& a, const Mat& b) {  // v * a * b
    Mat r{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float s = 0.0F;
            for (int k = 0; k < 4; ++k) s += a[static_cast<std::size_t>(i * 4 + k)] * b[static_cast<std::size_t>(k * 4 + j)];
            r[static_cast<std::size_t>(i * 4 + j)] = s;
        }
    }
    return r;
}
[[nodiscard]] Mat rotation_from_euler(const std::array<float, 3>& rot) {
    const Matrix4 m = motion::attach_local_matrix_zyx({0.0F, 0.0F, 0.0F}, rot);
    return m.values;
}
// Transpose of the 3x3 (0x140030DC0 on an orthonormal matrix, translation 0).
[[nodiscard]] Mat transpose3(const Mat& m) {
    Mat r = identity();
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) r[static_cast<std::size_t>(i * 4 + j)] = m[static_cast<std::size_t>(j * 4 + i)];
    }
    return r;
}
// Unit-length rows of the world rotation (the Reader's effect worlds may carry
// the V entry scale; the EXE inverts them as if orthonormal).
[[nodiscard]] Mat normalized_rotation(const Matrix4& world) {
    Mat r = identity();
    for (int i = 0; i < 3; ++i) {
        const float* row = &world.values[static_cast<std::size_t>(i * 4)];
        const float len = std::sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
        const float k = len > 1.0e-6F ? 1.0F / len : 1.0F;
        for (int j = 0; j < 3; ++j) r[static_cast<std::size_t>(i * 4 + j)] = row[j] * k;
    }
    return r;
}
[[nodiscard]] Vec3 apply3(const Vec3& v, const Mat& m) {
    return {v.x * m[0] + v.y * m[4] + v.z * m[8],
            v.x * m[1] + v.y * m[5] + v.z * m[9],
            v.x * m[2] + v.y * m[6] + v.z * m[10]};
}

// Uniform [0,1) from a 32-bit xorshift (the EXE's own generator, 0x140059390,
// is a four-word LCG mix; only the distribution matters here).
struct Rng final {
    std::uint32_t s;
    std::uint32_t next() {
        s ^= s << 13U;
        s ^= s >> 17U;
        s ^= s << 5U;
        return s;
    }
};

}  // namespace

std::optional<Def> parse_sprt00(std::span<const std::uint8_t> record) {
    if (record.size() < 0x150U || i32(record, 0) != 2) return std::nullopt;
    const std::int32_t def_off = i32(record, 0x10);
    if (def_off < 0 || static_cast<std::size_t>(def_off) + 0x10U + 0x130U > record.size()) {
        return std::nullopt;
    }
    const std::size_t d = static_cast<std::size_t>(def_off) + 0x10U;
    if (record[d + 1U] != 3U) return std::nullopt;
    Def def;
    for (std::size_t i = 2U; i < 0x10U && record[d + i] != 0U; ++i) def.name.push_back(static_cast<char>(record[d + i]));
    def.translation = vec3_at(record, d + 0x20U);
    def.rotation = vec3_at(record, d + 0x30U);
    def.scale = vec3_at(record, d + 0x40U);
    def.motion = parse_motion(record, d + 0x52U);
    def.life = i32(record, d + 0x68U);
    def.track = parse_track(record, d + 0x6CU);
    def.friction = vec3_at(record, d + 0xF4U);
    def.friction_decay = vec3_at(record, d + 0x11CU);
    def.gravity = f32(record, d + 0x10CU);
    for (std::size_t i = 0U; i < 3U; ++i) {
        def.spread[i] = static_cast<float>(s16(record, d + 0x100U + 2U * i)) * 0.0625F;
        def.hollow[i] = static_cast<float>(s16(record, d + 0x116U + 2U * i)) * 0.0625F * 0.5F;
        def.push[i] = static_cast<float>(s16(record, d + 0xE2U + 2U * i)) * 0.0625F;
        def.bias[i] = static_cast<float>(s16(record, d + 0xEEU + 2U * i)) * 0.0625F;
    }
    def.half_width = static_cast<float>(u16(record, d + 0x108U)) * 0.0625F;
    def.half_height = static_cast<float>(u16(record, d + 0x10AU)) * 0.0625F;
    def.animation = u16(record, d + 0x106U);
    def.world_gravity = record[d + 0x110U] == 1U;
    def.random_frame = record[d + 0x128U] == 1U;
    const std::size_t layers = u16(record, d + 0xC0U);
    for (std::size_t i = 0U; i < layers && i < 8U; ++i) {
        const std::int32_t off = i32(record, 0x18U + 4U * i);
        if (off < 0) return std::nullopt;
        const std::size_t l = static_cast<std::size_t>(off) + 0x10U;
        if (l + 0xA0U > record.size()) return std::nullopt;
        LayerDef layer;
        layer.translation = vec3_at(record, l);
        layer.rotation = vec3_at(record, l + 0x10U);
        layer.scale = vec3_at(record, l + 0x20U);
        layer.motion = parse_motion(record, l + 0x30U);
        layer.track = parse_track(record, l + 0x48U);
        def.layers.push_back(layer);
    }
    return def;
}

Simulation::Simulation(const Def& def, const Animation& animation, std::uint32_t seed)
    : def_(def), animation_(animation), rng_(seed == 0U ? 0x9E3779B9U : seed) {
    Rng rng{rng_};
    const std::size_t n = def_.count;
    emitter_.velocity.assign(n, {});
    emitter_.position.assign(n, {});
    emitter_.translation = def_.translation;
    emitter_.rotation = def_.rotation;
    emitter_.scale = def_.scale;
    emitter_.life = static_cast<float>(def_.life);
    emitter_.color = def_.track.keys[0].color;
    emitter_track_.remaining = static_cast<float>(def_.track.keys[0].duration);
    friction_ = def_.friction;
    for (const auto& layer : def_.layers) {
        State s;
        s.translation = layer.translation;
        s.rotation = layer.rotation;
        s.scale = layer.scale;
        s.color = layer.track.keys[0].color;
        layers_.push_back(s);
        TrackState t;
        t.remaining = static_cast<float>(layer.track.keys[0].duration);
        layer_tracks_.push_back(t);
    }

    // 0x140236D10: positions = uniform box, pushed out of the hollow centre by
    // 0x140312450; velocity = position * (push / spread - 1) + bias.
    const float inv = 1.0F / 4294967296.0F;
    std::array<float, 3> ratio{};
    for (std::size_t a = 0U; a < 3U; ++a) {
        const float clamped = std::max(def_.spread[a], 1.0F);
        ratio[a] = def_.push[a] / clamped;
    }
    for (std::size_t i = 0U; i < n; ++i) {
        std::array<float, 3> r{};
        for (auto& v : r) v = static_cast<float>(rng.next()) * inv - 0.5F;
        std::array<float, 3> p{r[0] * def_.spread[0], r[1] * def_.spread[1], r[2] * def_.spread[2]};
        // 0x140312450: only when the point lies inside the hollow box.
        bool inside = true;
        for (std::size_t a = 0U; a < 3U; ++a) {
            if (!(def_.hollow[a] >= p[a] && p[a] >= -def_.hollow[a])) inside = false;
        }
        if (inside) {
            int index = static_cast<int>(rng.next() % 6U);
            int guard = 0;
            while (guard++ < 12 && !(def_.hollow[static_cast<std::size_t>(index % 3)] <
                                     def_.spread[static_cast<std::size_t>(index % 3)] * 0.5F)) {
                index = index + 1 > 5 ? 0 : index + 1;
            }
            if (guard <= 12) {
                const auto a = static_cast<std::size_t>(index % 3);
                const float shell = def_.spread[a] * 0.5F * (r[a] + 0.5F) + (0.5F - r[a]) * def_.hollow[a];
                p[a] = index < 3 ? shell : -shell;
            }
        }
        emitter_.position[i] = p;
        for (std::size_t a = 0U; a < 3U; ++a) {
            emitter_.velocity[i][a] = p[a] * ratio[a] + def_.bias[a] - p[a];
        }
    }
    // def +0x128 == 1: one random A frame (0x140313EA0 mode 1).
    if (def_.random_frame && animation_.frame_count > 0U) {
        frame_ = rng.next() % animation_.frame_count;
    }
    rng_ = rng.s;
}

void Simulation::set_particles(std::vector<std::array<float, 3>> position,
                               std::vector<std::array<float, 3>> velocity) {
    emitter_.position = std::move(position);
    emitter_.velocity = std::move(velocity);
}

void Simulation::step_track(const Track& track, TrackState* state,
                            std::array<std::uint8_t, 4>* color) const {
    if (!track.enabled) return;
    if (!track.loop && state->done) return;
    const auto& a = track.keys[state->index];
    const auto& b = track.keys[(state->index + 1U) & 3U];
    const float duration = static_cast<float>(a.duration);
    float p = duration != 0.0F ? 1.0F - state->remaining / duration : 1.0F;
    for (std::size_t c = 0U; c < 4U; ++c) {
        const float v = static_cast<float>(a.color[c]) +
                        (static_cast<float>(b.color[c]) - static_cast<float>(a.color[c])) * p;
        (*color)[c] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(v), 0, 255));
    }
    state->remaining -= kTick;
    int guard = 0;
    while (!(0.0F < state->remaining) && guard++ < 8) {
        if (state->index + 1U >= 3U) {
            state->index = 0U;
            state->done = true;
        } else {
            ++state->index;
        }
        state->remaining += static_cast<float>(track.keys[state->index].duration);
    }
}

bool Simulation::update(const Matrix4& world) {
    if (expired_) return false;
    // 0x1402374F0: life counts down unless it is below -10000 (endless).
    if (!(-10000.0F > emitter_.life)) {
        emitter_.life -= kTick;
        if (0.0F > emitter_.life) {
            expired_ = true;
            return false;
        }
    }
    step_track(def_.track, &emitter_track_, &emitter_.color);
    const auto integrate = [](const TransformMotion& m, State* s) {
        for (std::size_t a = 0U; a < 3U; ++a) {
            s->translation[a] += static_cast<float>(m.translation[a]) * 0.0625F * kTick;
            float r = s->rotation[a] + static_cast<float>(m.rotation[a]) * (1.0F / 65536.0F) * kTwoPi * kTick;
            if (!(r < kPi)) r -= kTwoPi;
            if (-kPi > r) r += kTwoPi;
            s->rotation[a] = r;
            s->scale[a] += static_cast<float>(m.scale[a]) * (1.0F / 4096.0F) * kTick;
        }
    };
    integrate(def_.motion, &emitter_);
    for (std::size_t l = 0U; l < layers_.size(); ++l) {
        step_track(def_.layers[l].track, &layer_tracks_[l], &layers_[l].color);
        integrate(def_.layers[l].motion, &layers_[l]);
    }

    // Particle loop (0x140237780-0x140237DAA). The friction vector uses the
    // value from before this tick's decay.
    std::array<float, 3> fr{friction_[0] * kTick, friction_[1] * kTick, friction_[2] * kTick};
    for (std::size_t a = 0U; a < 3U; ++a) friction_[a] -= def_.friction_decay[a] * kTick;
    std::array<float, 3> g{0.0F, def_.gravity * kTick, 0.0F};
    if (def_.world_gravity) {
        // (0, g dt, 0) carried through the inverse world rotation.
        const Vec3 v = apply3({g[0], g[1], g[2]}, transpose3(normalized_rotation(world)));
        g = {v.x, v.y, v.z};
    }
    for (std::size_t i = 0U; i < emitter_.position.size(); ++i) {
        auto& v = emitter_.velocity[i];
        for (std::size_t a = 0U; a < 3U; ++a) {
            // 0x140314080: decelerate towards zero, never past it.
            float c = v[a];
            if (c != 0.0F) {
                const float n = c > 0.0F ? c - fr[a] : c + fr[a];
                v[a] = n;
                if (c * n < 0.0F) v[a] = 0.0F;
            }
        }
        for (std::size_t a = 0U; a < 3U; ++a) {
            v[a] += g[a];
            emitter_.position[i][a] += v[a] * kTick;
        }
    }
    // def +0x128 == 0: the A animation plays (0x1403228F0).
    if (!def_.random_frame && animation_.frame_time != 0U) {
        anim_timer_ -= kTick;
        int guard = 0;
        while (!(0.0F < anim_timer_) && guard++ < 8) {
            if (frame_ < animation_.last_frame) {
                ++frame_;
            } else if (animation_.loop) {
                frame_ = animation_.loop_frame;
            } else {
                anim_timer_ = 0.0F;
                break;
            }
            anim_timer_ = static_cast<float>(animation_.frame_time) - anim_timer_;
        }
    }
    return true;
}

void Simulation::quads(const Matrix4& world, const Camera& camera, std::vector<Quad>* out) const {
    if (out == nullptr || expired_) return;
    const Mat w = world.values;
    const Mat w_inv_rot = transpose3(normalized_rotation(world));
    const Mat r_inv = transpose3(rotation_from_euler(emitter_.rotation));
    // Quads face the camera: x to the right, y towards the image bottom (the
    // EXE's non-flag path bakes the camera inverse, the inverse world
    // rotation and the inverse emitter rotation into the corners).
    const Vec3 down{-camera.up.x, -camera.up.y, -camera.up.z};
    const std::array<std::array<float, 2>, 4> corner_sign{{{-1.0F, 1.0F}, {1.0F, 1.0F}, {1.0F, -1.0F}, {-1.0F, -1.0F}}};
    const auto emit = [&](const State& object, std::uint32_t layer) {
        // 0x140312F10: local = scale rows * Rz*Ry*Rx, translation row (w = 2).
        Mat local = rotation_from_euler(object.rotation);
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 4; ++col) {
                local[static_cast<std::size_t>(row * 4 + col)] *= object.scale[static_cast<std::size_t>(row)];
            }
        }
        local[12] = object.translation[0];
        local[13] = object.translation[1];
        local[14] = object.translation[2];
        local[15] = 2.0F;  // 0x140031200 adds the vector's w = 1 to the row's w
        const Mat to_world = mul(local, w);
        for (const auto& pos : emitter_.position) {
            Quad quad;
            quad.layer = layer;
            quad.rgba = object.color;
            for (std::size_t k = 0U; k < 4U; ++k) {
                const float cx = corner_sign[k][0] * def_.half_width;
                const float cy = corner_sign[k][1] * def_.half_height;
                Vec3 v{camera.right.x * cx + down.x * cy, camera.right.y * cx + down.y * cy,
                       camera.right.z * cx + down.z * cy};
                v = apply3(apply3(v, w_inv_rot), r_inv);
                v = {v.x + pos[0], v.y + pos[1], v.z + pos[2]};
                // (x, y, z, 1) * to_world, then the homogeneous divide.
                const float x = v.x * to_world[0] + v.y * to_world[4] + v.z * to_world[8] + to_world[12];
                const float y = v.x * to_world[1] + v.y * to_world[5] + v.z * to_world[9] + to_world[13];
                const float z = v.x * to_world[2] + v.y * to_world[6] + v.z * to_world[10] + to_world[14];
                const float h = v.x * to_world[3] + v.y * to_world[7] + v.z * to_world[11] + to_world[15];
                const float inv = std::fabs(h) > 1.0e-6F ? 1.0F / h : 1.0F;
                quad.corners[k] = {x * inv, y * inv, z * inv};
            }
            out->push_back(quad);
        }
    };
    emit(emitter_, 0U);
    for (std::size_t l = 0U; l < layers_.size(); ++l) emit(layers_[l], static_cast<std::uint32_t>(l + 1U));
}

}  // namespace dmcresource::particle
