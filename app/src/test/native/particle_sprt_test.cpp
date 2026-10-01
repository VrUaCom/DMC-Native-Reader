// CPtclSprt00 port against an emulated run of dmc3.exe (0x1402374F0 update,
// 0x140312F10 draw composer) on a synthetic record: transform integrator,
// colour tracks, friction, gravity and the camera-facing quad pipeline.
#include "dmcresource/particle_sprt.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "particle_sprt_truth.inc"

namespace {

std::vector<std::uint8_t> from_hex(const char* hex) {
    std::vector<std::uint8_t> out;
    for (const char* p = hex; p[0] != '\0' && p[1] != '\0'; p += 2) {
        out.push_back(static_cast<std::uint8_t>(std::stoi(std::string(p, 2U), nullptr, 16)));
    }
    return out;
}

bool near(float a, float b, float tol) {
    return std::fabs(a - b) <= tol * (1.0F + std::fabs(b));
}

template <class Truth>
void check(const char* label) {
    using namespace dmcresource;
    const auto record = from_hex(Truth::kRecordHex);
    const auto def = particle::parse_sprt00(record);
    assert(def.has_value());
    assert(def->name == "sy0");
    assert(def->layers.size() == 2U);
    assert(def->count == 12U);
    particle::Animation animation;
    particle::Simulation sim(*def, animation, 1U);
    std::vector<std::array<float, 3>> pos, vel;
    for (int i = 0; i < 12; ++i) {
        pos.push_back({Truth::kInitPos[i][0], Truth::kInitPos[i][1], Truth::kInitPos[i][2]});
        vel.push_back({Truth::kInitVel[i][0], Truth::kInitVel[i][1], Truth::kInitVel[i][2]});
    }
    sim.set_particles(pos, vel);
    Matrix4 world;
    for (int i = 0; i < 16; ++i) world.values[static_cast<std::size_t>(i)] = Truth::kWorld[i];
    for (int f = 0; f < Truth::kFrames; ++f) {
        assert(sim.update(world));
        const auto& t = Truth::kFrame[f];
        const auto& s = sim.state();
        for (int a = 0; a < 3; ++a) {
            if (!near(s.translation[a], t.tr[a], 2.0e-5F) || !near(s.rotation[a], t.rot[a], 2.0e-5F) ||
                !near(s.scale[a], t.sc[a], 2.0e-5F)) {
                std::printf("%s frame %d transform axis %d: %g %g %g vs %g %g %g\n", label, f, a, s.translation[a],
                            s.rotation[a], s.scale[a], t.tr[a], t.rot[a], t.sc[a]);
                assert(false);
            }
        }
        for (int c = 0; c < 4; ++c) assert(s.color[static_cast<std::size_t>(c)] == t.col[c]);
        assert(near(s.life, t.life, 1.0e-6F));
        for (int i = 0; i < 12; ++i) {
            for (int a = 0; a < 3; ++a) {
                if (!near(s.position[static_cast<std::size_t>(i)][static_cast<std::size_t>(a)], t.pos[i][a], 1.0e-4F) ||
                    !near(s.velocity[static_cast<std::size_t>(i)][static_cast<std::size_t>(a)], t.vel[i][a], 1.0e-4F)) {
                    std::printf("%s frame %d particle %d axis %d: pos %g vel %g vs %g %g\n", label, f, i, a,
                                s.position[static_cast<std::size_t>(i)][static_cast<std::size_t>(a)],
                                s.velocity[static_cast<std::size_t>(i)][static_cast<std::size_t>(a)], t.pos[i][a], t.vel[i][a]);
                    assert(false);
                }
            }
        }
        for (int l = 0; l < 2; ++l) {
            const auto& ls = sim.layer_states()[static_cast<std::size_t>(l)];
            const auto& lt = t.layer[l];
            for (int a = 0; a < 3; ++a) {
                if (!near(ls.translation[a], lt.tr[a], 2.0e-5F) || !near(ls.rotation[a], lt.rot[a], 2.0e-5F) ||
                    !near(ls.scale[a], lt.sc[a], 2.0e-5F)) {
                    std::printf("%s frame %d layer %d axis %d\n", label, f, l, a);
                    assert(false);
                }
            }
            for (int c = 0; c < 4; ++c) {
                if (ls.color[static_cast<std::size_t>(c)] != lt.col[c]) {
                    std::printf("%s frame %d layer %d colour %d: %d vs %d\n", label, f, l, c,
                                ls.color[static_cast<std::size_t>(c)], lt.col[c]);
                    assert(false);
                }
            }
        }
    }
    // Final pose: the emulated vertex packets run through the draw matrices.
    particle::Camera camera;
    camera.right = {1.0F, 0.0F, 0.0F};
    camera.up = {0.0F, -1.0F, 0.0F};
    camera.forward = {0.0F, 0.0F, 1.0F};
    std::vector<particle::Quad> quads;
    sim.quads(world, camera, &quads);
    assert(quads.size() == 36U);
    // EXE vertex order BR, BL, TL, TR -> Reader order BL, BR, TR, TL.
    constexpr int kExeIndex[4] = {2, 3, 0, 1};
    for (std::size_t q = 0U; q < quads.size(); ++q) {
        const std::size_t object = q / 12U;
        const std::size_t particle_index = q % 12U;
        for (int k = 0; k < 4; ++k) {
            const float* v = Truth::kVerts[particle_index][kExeIndex[k]];
            const float* m = Truth::kFinal[object];
            const float x = v[0] * m[0] + v[1] * m[4] + v[2] * m[8] + m[12];
            const float y = v[0] * m[1] + v[1] * m[5] + v[2] * m[9] + m[13];
            const float z = v[0] * m[2] + v[1] * m[6] + v[2] * m[10] + m[14];
            const float w = v[0] * m[3] + v[1] * m[7] + v[2] * m[11] + m[15];
            const auto& c = quads[q].corners[static_cast<std::size_t>(k)];
            if (!near(c.x, x / w, 2.0e-4F) || !near(c.y, y / w, 2.0e-4F) || !near(c.z, z / w, 2.0e-4F)) {
                std::printf("%s quad %zu corner %d: %g %g %g vs %g %g %g\n", label, q, k, c.x, c.y, c.z, x / w, y / w, z / w);
                assert(false);
            }
        }
    }
}

}  // namespace

int main() {
    check<truth_wg1>("world gravity");
    check<truth_wg0>("local gravity");

    // Parser guards.
    assert(!dmcresource::particle::parse_sprt00({}).has_value());
    std::puts("particle_sprt_test OK");
    return 0;
}
