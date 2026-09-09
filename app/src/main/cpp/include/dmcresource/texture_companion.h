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

// Platform-neutral attachment gate shared by Black Widow/JNI tests. The
// renderer and Java shell never need PTX-specific slot rules.
[[nodiscard]] bool can_attach(const ModelTextureView& model) noexcept;

// Attach a complete PTX companion to a neutral model texture-slot projection.
// PTX/DDS parsing remains delegated to the registered Native Reader texture
// pipeline (Crusader + canonical framing/codecs). This module owns only
// companion matching, required-slot validation and attachment result state.
[[nodiscard]] AttachmentResult attach_ptx(
    std::string_view filename,
    const std::uint8_t* bytes,
    std::size_t size,
    const ModelTextureView& model) noexcept;

}  // namespace dmcresource::texture_companion
