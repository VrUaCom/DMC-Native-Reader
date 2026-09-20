#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dmcresource/image_preview.h"
#include "dmcresource/mesh.h"
#include "dmcresource/render_scene.h"

namespace dmcresource::texture_companion {

struct ModelTextureView final {
    const Mesh* mesh{};
    const RenderScene* scene{};
    std::span<const std::uint32_t> triangle_texture_slots{};
};

struct AttachmentResult final {
    bool attached{};

    // Local PTX slot-indexed decoded bank. Each required PTX slot is decoded at
    // most once per AttachmentResult; empty entries are unused local slots.
    // Composite shared attachment can therefore bind several MOD parts to one
    // bank instead of materializing duplicate RGBA images per global slot.
    std::vector<ImagePreview> textures;

    std::string detail;
    std::size_t required_slot_count{};
    std::size_t source_texture_count{};
};

// Platform-neutral attachment gate shared by Black Widow and platform bridges.
// The renderer and Java shell never need PTX/DDS-specific slot rules. A caller
// may provide either a flattened Mesh or an authoritative source-local scene.
[[nodiscard]] bool can_attach(const ModelTextureView& model) noexcept;

// Attach a complete PTX companion to one neutral model texture-slot projection.
// Physical PTX/DDS parsing and on-demand base-mip decode are delegated to the
// reusable TextureSet module. The returned texture bank is indexed by local PTX
// slot and decodes only the slots required by the model. Diagnostic/result
// construction may allocate; the Spider OperationFn/public action boundary is
// responsible for converting allocation failure into fail-closed behavior.
[[nodiscard]] AttachmentResult attach_ptx(
    std::string_view filename,
    const std::uint8_t* bytes,
    std::size_t size,
    const ModelTextureView& model);

// Decode one PTX bank for several source-local model projections. Required local
// slots are unioned before decoding, so a shared companion used by several MOD
// parts parses once and decodes each required texture once. No model geometry is
// copied and no per-part RGBA duplicate is produced by this layer.
[[nodiscard]] AttachmentResult attach_shared_ptx(
    std::string_view filename,
    const std::uint8_t* bytes,
    std::size_t size,
    std::span<const ModelTextureView> models);

}  // namespace dmcresource::texture_companion
