#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "dmcresource/mesh.h"

namespace dmcresource {

inline constexpr std::uint32_t kNoTextureSlot =
    std::numeric_limits<std::uint32_t>::max();

struct Matrix4 final {
    std::array<float, 16> values{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
};

enum class RenderNodeKind : std::uint8_t {
    Scene,
    Bone,
    TransformSelector,
};

struct RenderNode final {
    std::string name;
    RenderNodeKind kind{RenderNodeKind::Scene};
    std::int32_t parent{-1};

    // True only when local/world matrices are backed by the canonical format
    // authority for this concrete document. Identity matrices alone must never
    // be interpreted as decoded spatial transforms.
    bool spatial_authority{false};
    // Parent relation authority is independent of decoded spatial transforms.
    bool parent_authority{false};

    Matrix4 local;
    Matrix4 world;
};

struct MeshPrimitive final {
    std::string name;
    Mesh mesh;
    std::uint32_t object_index{};
    std::uint32_t mesh_index{};

    // Optional scene-node binding. SCM has an EXE/corpus-confirmed mapping from
    // scene node -> geometry object; MOD does not yet publish an equivalent
    // typed binding in the canonical reader and therefore leaves this at -1.
    std::int32_t node_index{-1};
};

struct JointWeight final {
    std::uint32_t node_index{};
    float weight{};
};

struct VertexSkinBinding final {
    std::vector<JointWeight> influences;
};

struct SkinBinding final {
    std::uint32_t mesh_primitive{};
    std::vector<VertexSkinBinding> vertices;
};

struct TextureBinding final {
    std::uint32_t mesh_primitive{};
    std::uint32_t texture_slot{};
    std::string external_source;
};

struct RenderScene final {
    std::vector<MeshPrimitive> meshes;
    std::vector<RenderNode> nodes;
    std::vector<SkinBinding> skins;
    std::vector<TextureBinding> textures;

    [[nodiscard]] bool has_geometry() const noexcept {
        for (const auto& primitive : meshes) {
            if (!primitive.mesh.vertices.empty() && !primitive.mesh.indices.empty()) {
                return true;
            }
        }
        return false;
    }
};

}  // namespace dmcresource
