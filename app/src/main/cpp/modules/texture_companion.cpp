#include "dmcresource/texture_companion.h"

#include <span>
#include <sstream>
#include <utility>

#include "dmcresource/model_texture_binding.h"
#include "dmcresource/texture_set.h"

namespace dmcresource::texture_companion {
namespace {

[[nodiscard]] std::span<const std::byte> as_bytes(
    const std::uint8_t* bytes,
    std::size_t size) noexcept {
    if (bytes == nullptr) return {};
    return std::as_bytes(std::span<const std::uint8_t>{bytes, size});
}

[[nodiscard]] bool collect_required(const ModelTextureView& model,
                                    model_texture_binding::RequiredSlots* out) noexcept {
    if (model.mesh != nullptr) {
        return model_texture_binding::collect_required_slots(
            *model.mesh, model.triangle_texture_slots, out);
    }
    if (model.scene != nullptr) {
        return model_texture_binding::collect_required_slots(
            *model.scene, model.triangle_texture_slots, out);
    }
    return false;
}

}  // namespace

bool can_attach(const ModelTextureView& model) noexcept {
    model_texture_binding::RequiredSlots required;
    return collect_required(model, &required);
}

AttachmentResult attach_ptx(
    std::string_view filename,
    const std::uint8_t* bytes,
    std::size_t size,
    const ModelTextureView& model) noexcept {
    AttachmentResult out;

    if (model.mesh == nullptr && model.scene == nullptr) {
        out.detail =
            "PTX companion rejected: current resource has no model texture projection";
        return out;
    }

    model_texture_binding::RequiredSlots required;
    if (!collect_required(model, &required)) {
        out.detail =
            "PTX companion rejected: current resource has no complete UV + texture-slot render mapping";
        return out;
    }
    if (bytes == nullptr) {
        out.detail = "PTX companion rejected: null source";
        return out;
    }

    const auto source = as_bytes(bytes, size);
    const auto set = texture_set::parse_ptx(source);
    if (!set.ok() || set.kind != texture_set::Kind::ptx_bundle) {
        out.detail = set.detail.empty()
            ? "PTX companion rejected: selected file did not pass TextureSet validation"
            : set.detail;
        return out;
    }

    out.required_slot_count = required.slots.size();
    out.source_texture_count = set.slots.size();

    try {
        std::vector<ImagePreview> textures(
            static_cast<std::size_t>(required.max_slot) + 1U);
        for (const auto slot_index : required.slots) {
            const auto* slot = texture_set::find_slot(set, slot_index);
            if (slot == nullptr) {
                std::ostringstream detail;
                detail << "PTX companion rejected: model requests texture slot "
                       << slot_index << " but PTX does not expose that slot";
                out.detail = detail.str();
                return out;
            }

            std::string decode_detail;
            ImagePreview decoded;
            if (!texture_set::decode_base_mip(
                    source, *slot, &decoded, &decode_detail)) {
                std::ostringstream detail;
                detail << "PTX companion rejected: texture slot " << slot_index
                       << " could not decode base mip";
                if (!decode_detail.empty()) detail << " | " << decode_detail;
                out.detail = detail.str();
                return out;
            }
            textures[static_cast<std::size_t>(slot_index)] = std::move(decoded);
        }

        std::ostringstream detail;
        detail << "PTX companion attached: " << filename
               << " | requiredSlots=" << required.slots.size()
               << " | bundleTextures=" << set.slots.size()
               << " | route=TextureSet/Crusader/PTX->DDS->UV";
        if (set.ptx_aux_compat_used) {
            detail << " | auxCompat=retail-DXT1";
        }

        out.textures = std::move(textures);
        out.detail = detail.str();
        out.attached = true;
        return out;
    } catch (...) {
        out.detail = "PTX companion rejected: texture attachment allocation failed";
        return out;
    }
}

}  // namespace dmcresource::texture_companion
