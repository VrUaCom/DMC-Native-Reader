#include "dmcresource/texture_companion.h"

#include <algorithm>
#include <limits>
#include <sstream>
#include <utility>

#include "dmcresource/decode_pipeline.h"
#include "dmcresource/dmc_resource.h"

namespace dmcresource::texture_companion {
namespace {

constexpr std::uint32_t kNoTextureSlot =
    std::numeric_limits<std::uint32_t>::max();
constexpr std::uint32_t kMaxCompanionTextureSlot = 4095U;

[[nodiscard]] bool collect_required_slots(
    const ModelTextureView& model,
    std::vector<std::uint32_t>* required,
    std::uint32_t* max_slot) {
    if (required == nullptr || max_slot == nullptr || model.mesh == nullptr) {
        return false;
    }
    required->clear();
    *max_slot = 0U;

    const auto& mesh = *model.mesh;
    if (!mesh.has_uv0() || mesh.indices.size() < 3U ||
        mesh.indices.size() % 3U != 0U ||
        model.triangle_texture_slots.size() != mesh.indices.size() / 3U) {
        return false;
    }

    try {
        for (const auto slot : model.triangle_texture_slots) {
            if (slot == kNoTextureSlot) continue;
            if (slot > kMaxCompanionTextureSlot) return false;
            if (std::find(required->begin(), required->end(), slot) == required->end()) {
                required->push_back(slot);
                *max_slot = std::max(*max_slot, slot);
            }
        }
    } catch (...) {
        return false;
    }
    return !required->empty();
}

}  // namespace

bool can_attach(const ModelTextureView& model) noexcept {
    std::vector<std::uint32_t> required;
    std::uint32_t max_slot = 0U;
    return collect_required_slots(model, &required, &max_slot);
}

AttachmentResult attach_ptx(
    std::string_view filename,
    const std::uint8_t* bytes,
    std::size_t size,
    const ModelTextureView& model) noexcept {
    AttachmentResult out;

    std::vector<std::uint32_t> required_slots;
    std::uint32_t max_slot = 0U;
    if (!collect_required_slots(model, &required_slots, &max_slot)) {
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

    out.required_slot_count = required_slots.size();
    out.source_texture_count = pipeline.children.size();

    try {
        std::vector<ImagePreview> textures(static_cast<std::size_t>(max_slot) + 1U);
        for (const auto slot : required_slots) {
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
               << " | requiredSlots=" << required_slots.size()
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
