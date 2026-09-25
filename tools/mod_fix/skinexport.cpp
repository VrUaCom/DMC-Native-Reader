#include "dmcresource/resource_session.h"
#include "dmcresource/scene_projection.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
using namespace dmcresource;
// Rest-space vertices of one MOD child with their dominant joint: "x y z joint".
int main(int, char** argv) {
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<std::uint8_t> b((std::istreambuf_iterator<char>(f)), {});
    auto s = open_session("x.pac", b.data(), b.size());
    for (auto& c : s->children) {
        if (c.suggested_filename != argv[2]) continue;
        auto m = open_session("m.mod", c.source_bytes.data(), c.source_bytes.size());
        std::FILE* o = std::fopen(argv[3], "w");
        std::size_t unbound = 0;
        for (std::size_t p = 0; p < m->scene.meshes.size(); ++p) {
            const auto& mesh = m->scene.meshes[p].mesh;
            const SkinBinding* skin = nullptr;
            for (auto& k : m->scene.skins) if (k.mesh_primitive == p && k.vertices.size() == mesh.vertices.size()) skin = &k;
            // Rest positions: the flattened render mesh keeps primitive order.
            for (std::size_t v = 0; v < mesh.vertices.size(); ++v) {
                int joint = m->scene.meshes[p].node_index;
                float best = -1.0f;
                if (skin) for (auto& in : skin->vertices[v].influences) if (in.weight > best) { best = in.weight; joint = (int)in.node_index; }
                if (joint < 0) { ++unbound; continue; }
                std::fprintf(o, "%zu %zu %d\n", p, v, joint);
            }
        }
        std::fclose(o);
        // flattened rest positions in the same order
        std::FILE* q = std::fopen((std::string(argv[3]) + ".pos").c_str(), "w");
        for (auto& v : m->render_mesh.vertices) std::fprintf(q, "%.5f %.5f %.5f\n", v.x, v.y, v.z);
        std::fclose(q);
        std::printf("%s: nodes %zu primitives %zu render verts %zu unbound %zu\n", argv[2], m->scene.nodes.size(), m->scene.meshes.size(), m->render_mesh.vertices.size(), unbound);
    }
}
