#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dmcresource/image_preview.h"
#include "dmcresource/mesh.h"

namespace dmcresource::texture_companion {

struct ModelTextureView final {
    const Mesh* mesh{};
    std::span<const std::uint32_t> triangle_texture_slots{};
};

struct AttachmentResult final {
    bool attached{};
    std::vector<ImagePreview> textures;
    std::string detail;
    std::size_t required_slot_count{};
    std::size_t source_texture_count{};
};

// Platform-neutral attachment gate shared by Black Widow and platform bridges.
// The renderer and Java shell never need PTX/DDS-specific slot rules.
[[nodiscard]] bool can_attach(const ModelTextureView& model) noexcept;

// Attach a complete PTX companion to a neutral model texture-slot projection.
// Physical PTX/DDS parsing and on-demand base-mip decode are delegated to the
// reusable TextureSet module. This layer owns only model-required slot matching
// and the resulting attachment state; it does not depend on gallery previews or
// Android presentation policy.
[[nodiscard]] AttachmentResult attach_ptx(
    std::string_view filename,
    const std::uint8_t* bytes,
    std::size_t size,
    const ModelTextureView& model) noexcept;

}  // namespace dmcresource::texture_companion
