#include "dmcresource/texture_set.h"

#include <algorithm>
#include <span>
#include <utility>

#include "dmc_rengine/profiles/dmc3/texture_slot_framing.hpp"
#include "dmcresource/ptx_framing_compat.h"

namespace dmcresource::texture_set {
namespace {

namespace dds_bc = dmc::rengine::codecs::dds_bc;
namespace dmc3 = dmc::rengine::profiles::dmc3;

[[nodiscard]] dds_bc::ParseResult parse_exact_dds(
    std::span<const std::byte> bytes) noexcept {
    auto parsed = dds_bc::parse(bytes);
    if (parsed.ok() && parsed.document.total_size != bytes.size()) {
        return dds_bc::ParseResult{
            .status = dds_bc::Status::payload_out_of_bounds,
            .document = {},
            .detail = "DDS has trailing bytes outside its bounded image extent",
        };
    }
    return parsed;
}

[[nodiscard]] std::span<const std::byte> bounded_dds_span(
    std::span<const std::byte> source,
    std::uint64_t offset64,
    std::uint64_t size64) noexcept {
    if (offset64 > source.size()) return {};
    const auto offset = static_cast<std::size_t>(offset64);
    if (size64 > source.size() - offset) return {};
    return source.subspan(offset, static_cast<std::size_t>(size64));
}

[[nodiscard]] bool append_framed_slot(
    ParseResult* out,
    std::span<const std::byte> source,
    const dmc3::TextureSlotEntry& entry) {
    if (out == nullptr) return false;
    const auto bytes = bounded_dds_span(source, entry.dds_offset, entry.dds_size);
    const auto parsed = parse_exact_dds(bytes);
    if (!parsed.ok()) return false;

    out->slots.push_back(Slot{
        .index = entry.texture_index,
        .descriptor_offset = entry.descriptor_offset,
        .dds_offset = entry.dds_offset,
        .dds_size = entry.dds_size,
        .sector_span = entry.sector_span,
        .secondary_width = entry.secondary_width,
        .secondary_height = entry.secondary_height,
        .dds = parsed.document,
    });
    return true;
}

}  // namespace

ParseResult parse_dds(std::span<const std::byte> source) noexcept {
    ParseResult out;
    if (source.empty()) {
        out.detail = "DDS rejected: empty source";
        return out;
    }

    const auto direct = parse_exact_dds(source);
    if (direct.ok()) {
        try {
            out.kind = Kind::standalone_dds;
            out.slots.push_back(Slot{
                .index = 0U,
                .descriptor_offset = 0U,
                .dds_offset = 0U,
                .dds_size = direct.document.total_size,
                .sector_span = 0U,
                .secondary_width = 0U,
                .secondary_height = 0U,
                .dds = direct.document,
            });
        } catch (...) {
            out = {};
            out.detail = "DDS rejected: texture-set allocation failed";
        }
        return out;
    }

    const auto framing = dmc3::TextureSlotFramingParser::parse(source);
    if (!framing.ok() ||
        framing.document.kind != dmc3::TextureSlotFramingKind::wrapped_dds ||
        framing.document.textures.size() != 1U) {
        out.detail =
            "DDS rejected: neither a bounded standalone DXT DDS nor canonical descriptor-wrapped DDS";
        return out;
    }

    try {
        out.kind = Kind::wrapped_dds;
        if (!append_framed_slot(&out, source, framing.document.textures.front())) {
            out = {};
            out.detail =
                "Wrapped DDS rejected: canonical framing child failed bounded DDS validation";
        }
    } catch (...) {
        out = {};
        out.detail = "DDS rejected: texture-set allocation failed";
    }
    return out;
}

ParseResult parse_ptx(std::span<const std::byte> source) noexcept {
    ParseResult out;
    if (source.empty()) {
        out.detail = "PTX rejected: empty source";
        return out;
    }

    bool compat_used = false;
    const auto framing = ptx_compat::parse_texture_bundle(source, &compat_used);
    if (!framing.ok() ||
        framing.document.kind != dmc3::TextureSlotFramingKind::texture_bundle) {
        out.detail = "PTX rejected by canonical texture-slot framing";
        if (!framing.detail.empty()) {
            out.detail += ": ";
            out.detail += framing.detail;
        }
        return out;
    }

    try {
        out.kind = Kind::ptx_bundle;
        out.ptx_aux_compat_used = compat_used;
        out.slots.reserve(framing.document.textures.size());
        for (const auto& entry : framing.document.textures) {
            if (!append_framed_slot(&out, source, entry)) {
                out = {};
                out.detail =
                    "PTX rejected: framed child failed portable DDS validation";
                return out;
            }
        }
    } catch (...) {
        out = {};
        out.detail = "PTX rejected: texture-set allocation failed";
    }
    return out;
}

const Slot* find_slot(const ParseResult& set, std::uint32_t index) noexcept {
    const auto it = std::find_if(
        set.slots.begin(), set.slots.end(),
        [index](const Slot& slot) { return slot.index == index; });
    return it == set.slots.end() ? nullptr : &*it;
}

std::span<const std::byte> dds_bytes(
    std::span<const std::byte> source,
    const Slot& slot) noexcept {
    return bounded_dds_span(source, slot.dds_offset, slot.dds_size);
}

bool decode_base_mip(
    std::span<const std::byte> source,
    const Slot& slot,
    ImagePreview* out,
    std::string* detail) noexcept {
    if (out == nullptr) return false;
    *out = {};

    const auto bytes = dds_bytes(source, slot);
    if (bytes.empty()) {
        if (detail != nullptr) *detail = "DDS slot lies outside source bounds";
        return false;
    }

    const auto decoded = dds_bc::decode_base_mip_rgba8(bytes, slot.dds);
    if (!decoded.ok) {
        if (detail != nullptr) *detail = decoded.detail;
        return false;
    }

    try {
        out->width = decoded.image.width;
        out->height = decoded.image.height;
        out->rgba8 = std::move(decoded.image.rgba8);
    } catch (...) {
        *out = {};
        if (detail != nullptr) *detail = "RGBA texture allocation failed";
        return false;
    }
    return out->available();
}

}  // namespace dmcresource::texture_set
