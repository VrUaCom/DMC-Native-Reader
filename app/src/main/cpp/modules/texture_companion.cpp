#include "dmcresource/texture_companion.h"

#include <sstream>
#include <utility>

#include "dmcresource/decode_pipeline.h"
#include "dmcresource/dmc_resource.h"
#include "dmcresource/model_texture_binding.h"

namespace dmcresource::texture_companion {

bool can_attach(const ModelTextureView& model) noexcept {
    if (model.mesh == nullptr) return false;
    return model_texture_binding::can_attach_texture_companion(
        *model.mesh, model.triangle_texture_slots);
}

AttachmentResult attach_ptx(
    std::string_view filename,
    const std::uint8_t* bytes,
    std::size_t size,
    const ModelTextureView& model) noexcept {
    AttachmentResult out;

    if (model.mesh == nullptr) {
        out.detail =
            "PTX companion rejected: current resource has no model texture projection";
        return out;
    }

    model_texture_binding::RequiredSlots required;
    if (!model_texture_binding::collect_required_slots(
            *model.mesh, model.triangle_texture_slots, &required)) {
        out.detail =
            "PTX companion rejected: current resource has no complete UV + texture-slot render mapping";
        return out;
    }
    if (bytes == nullptr) {
        out.detail = "PTX companion rejected: null source";
        return out;
    }

    auto pipeline = run_decode_pipeline(filename, bytes, size);
    if (!pipeline.accepted || pipeline.probe.format != Format::Ptx) {
        out.detail =
            "PTX companion rejected: selected file did not pass the Native Reader PTX pipeline";
        return out;
    }

    out.required_slot_count = required.slots.size();
    out.source_texture_count = pipeline.children.size();

    try {
        std::vector<ImagePreview> textures(
            static_cast<std::size_t>(required.max_slot) + 1U);
        for (const auto slot : required.slots) {
            const auto index = static_cast<std::size_t>(slot);
            if (index >= pipeline.children.size()) {
                std::ostringstream detail;
                detail << "PTX companion rejected: model requests texture slot "
                       << slot << " but PTX exposes only "
                       << pipeline.children.size() << " texture entries";
                out.detail = detail.str();
                return out;
            }

            auto& child = pipeline.children[index];
            if (!child.image_preview.available()) {
                std::ostringstream detail;
                detail << "PTX companion rejected: texture slot " << slot
                       << " has no decoded base-mip image";
                if (!child.detail.empty()) detail << " | " << child.detail;
                out.detail = detail.str();
                return out;
            }
            textures[index] = std::move(child.image_preview);
        }

        std::ostringstream detail;
        detail << "PTX companion attached: " << filename
               << " | requiredSlots=" << required.slots.size()
               << " | bundleTextures=" << pipeline.children.size()
               << " | route=Crusader/PTX->DDS->UV";

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
