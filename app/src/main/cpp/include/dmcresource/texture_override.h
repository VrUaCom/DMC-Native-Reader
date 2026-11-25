#pragma once

// A picture of your own over one of the model's textures, to see a modded
// costume on the character without repacking anything: the Reader only
// swaps the decoded texture it draws with; no file is written. PNG or DDS in,
// any size (the UVs are 0..1); the original comes back on restore.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "dmcresource/image_preview.h"

namespace dmcresource {

struct Session;

// PNG (8- or 16-bit grey, grey + alpha, RGB, RGBA, palette with tRNS; 1..8
// bit grey and palette; not interlaced) to RGBA8. Untrusted input: bounded
// (at most 8192 x 8192), every chunk checked. nullopt with `error` set when
// it is not one the decoder reads.
[[nodiscard]] std::optional<ImagePreview> decode_png(std::span<const std::uint8_t> bytes, std::string* error);

// A PNG or a DDS (by its signature) to RGBA8, the DDS's first mip.
[[nodiscard]] std::optional<ImagePreview> decode_texture_file(std::span<const std::uint8_t> bytes,
                                                              std::string* error);

struct TextureSlotInfo final {
    std::uint32_t width{};
    std::uint32_t height{};
    bool overridden{};
    std::string override_name;  // the file shown instead, when overridden
};

// The textures the model is drawn with (the render bank), in slot order.
[[nodiscard]] std::vector<TextureSlotInfo> texture_slots(const Session* session);

// Slot `slot` drawn as `image` (the original kept). False when there is no
// such slot or the image is empty.
bool override_texture(Session* session, std::size_t slot, ImagePreview image, std::string name);

// The original back for `slot`, or for every slot with `slot` npos. True
// when something changed.
bool restore_texture(Session* session, std::size_t slot);

// Slot `slot` as it is drawn now (`max_side` > 0: box-scaled to fit), for a
// thumbnail or an export; the original instead with `original` true.
[[nodiscard]] ImagePreview texture_image(const Session* session, std::size_t slot, std::uint32_t max_side,
                                         bool original);

}  // namespace dmcresource
