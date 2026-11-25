#include "dmcresource/texture_override.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

#include "dmcresource/dds_bcn.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/slot_inflate.h"

namespace dmcresource {

namespace {

constexpr std::uint32_t kMaxSide = 8192U;

std::uint32_t be32(std::span<const std::uint8_t> b, std::size_t o) noexcept {
    return (static_cast<std::uint32_t>(b[o]) << 24U) | (static_cast<std::uint32_t>(b[o + 1U]) << 16U) |
           (static_cast<std::uint32_t>(b[o + 2U]) << 8U) | static_cast<std::uint32_t>(b[o + 3U]);
}

std::uint8_t paeth(int a, int b, int c) noexcept {
    const int p = a + b - c;
    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return static_cast<std::uint8_t>(a);
    return static_cast<std::uint8_t>(pb <= pc ? b : c);
}

bool fail(std::string* error, const char* text) {
    if (error != nullptr) *error = text;
    return false;
}

}  // namespace

std::optional<ImagePreview> decode_png(std::span<const std::uint8_t> bytes, std::string* error) {
    static constexpr std::array<std::uint8_t, 8> kSignature{137, 80, 78, 71, 13, 10, 26, 10};
    if (bytes.size() < 8U || !std::equal(kSignature.begin(), kSignature.end(), bytes.begin())) {
        fail(error, "not a PNG");
        return std::nullopt;
    }
    std::uint32_t width = 0U, height = 0U;
    int depth = 0, colour = -1;
    bool header = false;
    std::vector<std::uint8_t> idat;
    std::vector<std::array<std::uint8_t, 4>> palette;
    std::array<int, 3> transparent_rgb{-1, -1, -1};
    std::size_t o = 8U;
    bool ended = false;
    while (o + 12U <= bytes.size()) {
        const std::uint32_t length = be32(bytes, o);
        if (length > bytes.size() - o - 12U) {
            fail(error, "a PNG chunk runs past the end of the file");
            return std::nullopt;
        }
        const auto type = bytes.subspan(o + 4U, 4U);
        const auto data = bytes.subspan(o + 8U, length);
        const auto is = [&type](const char* name) { return std::memcmp(type.data(), name, 4U) == 0; };
        if (is("IHDR")) {
            if (length != 13U) { fail(error, "bad PNG header"); return std::nullopt; }
            width = be32(data, 0U);
            height = be32(data, 4U);
            depth = data[8];
            colour = data[9];
            if (data[10] != 0U || data[11] != 0U) { fail(error, "unknown PNG compression or filter"); return std::nullopt; }
            if (data[12] != 0U) { fail(error, "interlaced PNGs are not read: save it without interlacing"); return std::nullopt; }
            header = true;
        } else if (is("PLTE")) {
            palette.clear();
            for (std::size_t i = 0; i + 2U < length && palette.size() < 256U; i += 3U) {
                palette.push_back({data[i], data[i + 1U], data[i + 2U], 255U});
            }
        } else if (is("tRNS")) {
            if (colour == 3) {
                for (std::size_t i = 0; i < length && i < palette.size(); ++i) palette[i][3] = data[i];
            } else if (colour == 0 && length >= 2U) {
                transparent_rgb = {static_cast<int>((data[0] << 8) | data[1]), -1, -1};
            } else if (colour == 2 && length >= 6U) {
                transparent_rgb = {static_cast<int>((data[0] << 8) | data[1]), static_cast<int>((data[2] << 8) | data[3]),
                                   static_cast<int>((data[4] << 8) | data[5])};
            }
        } else if (is("IDAT")) {
            if (idat.size() + length > 512U * 1024U * 1024U) { fail(error, "PNG data too large"); return std::nullopt; }
            idat.insert(idat.end(), data.begin(), data.end());
        } else if (is("IEND")) {
            ended = true;
            break;
        }
        o += 12U + length;
    }
    if (!header || !ended || idat.empty()) { fail(error, "an incomplete PNG"); return std::nullopt; }
    if (width == 0U || height == 0U || width > kMaxSide || height > kMaxSide) {
        fail(error, "PNG size out of range (1..8192 per side)");
        return std::nullopt;
    }
    int channels = 0;
    switch (colour) {
        case 0: channels = 1; break;
        case 2: channels = 3; break;
        case 3: channels = 1; break;
        case 4: channels = 2; break;
        case 6: channels = 4; break;
        default: fail(error, "unknown PNG colour type"); return std::nullopt;
    }
    const bool depth_ok = depth == 8 || depth == 16 ||
                          ((colour == 0 || colour == 3) && (depth == 1 || depth == 2 || depth == 4));
    if (!depth_ok || (colour == 3 && depth == 16)) { fail(error, "unsupported PNG bit depth"); return std::nullopt; }
    if (colour == 3 && palette.empty()) { fail(error, "a palette PNG without a palette"); return std::nullopt; }
    const std::size_t bits = static_cast<std::size_t>(channels) * static_cast<std::size_t>(depth);
    const std::size_t row = (static_cast<std::size_t>(width) * bits + 7U) / 8U;
    const std::size_t bpp = std::max<std::size_t>(1U, bits / 8U);
    const std::size_t expected = (row + 1U) * height;
    auto raw = slot_inflate::inflate_zlib(idat, expected);
    if (!raw || raw->size() < expected) { fail(error, "the PNG's image data does not inflate"); return std::nullopt; }

    // Unfilter in place, row by row.
    std::vector<std::uint8_t> prior(row, 0U), current(row, 0U);
    ImagePreview out;
    out.width = width;
    out.height = height;
    out.rgba8.resize(static_cast<std::size_t>(width) * height * 4U);
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* src = raw->data() + y * (row + 1U);
        const std::uint8_t filter = src[0];
        for (std::size_t x = 0; x < row; ++x) {
            const int a = x >= bpp ? current[x - bpp] : 0;
            const int b = prior[x];
            const int c = x >= bpp ? prior[x - bpp] : 0;
            int v = src[1U + x];
            switch (filter) {
                case 0: break;
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) / 2; break;
                case 4: v += paeth(a, b, c); break;
                default: fail(error, "bad PNG row filter"); return std::nullopt;
            }
            current[x] = static_cast<std::uint8_t>(v);
        }
        // To RGBA8.
        std::uint8_t* dst = out.rgba8.data() + static_cast<std::size_t>(y) * width * 4U;
        const auto sample = [&](std::size_t index) -> int {  // the index-th sample of the row, full precision
            if (depth == 8) return current[index];
            if (depth == 16) return (current[index * 2U] << 8) | current[index * 2U + 1U];
            const std::size_t bit = index * static_cast<std::size_t>(depth);
            const int shift = 8 - depth - static_cast<int>(bit % 8U);
            return (current[bit / 8U] >> shift) & ((1 << depth) - 1);
        };
        const auto to8 = [&](int v) -> std::uint8_t {
            if (depth == 16) return static_cast<std::uint8_t>(v >> 8);
            if (depth == 8) return static_cast<std::uint8_t>(v);
            return static_cast<std::uint8_t>(v * 255 / ((1 << depth) - 1));
        };
        for (std::uint32_t x = 0; x < width; ++x) {
            std::uint8_t* p = dst + x * 4U;
            const std::size_t s = static_cast<std::size_t>(x) * static_cast<std::size_t>(channels);
            switch (colour) {
                case 0: {
                    const int g = sample(s);
                    p[0] = p[1] = p[2] = to8(g);
                    p[3] = g == transparent_rgb[0] ? 0U : 255U;
                    break;
                }
                case 2: {
                    const int r = sample(s), g = sample(s + 1U), bl = sample(s + 2U);
                    p[0] = to8(r); p[1] = to8(g); p[2] = to8(bl);
                    p[3] = (r == transparent_rgb[0] && g == transparent_rgb[1] && bl == transparent_rgb[2]) ? 0U : 255U;
                    break;
                }
                case 3: {
                    const auto index = static_cast<std::size_t>(sample(s));
                    const auto& c = index < palette.size() ? palette[index] : palette.back();
                    std::memcpy(p, c.data(), 4U);
                    break;
                }
                case 4:
                    p[0] = p[1] = p[2] = to8(sample(s));
                    p[3] = to8(sample(s + 1U));
                    break;
                default:
                    p[0] = to8(sample(s)); p[1] = to8(sample(s + 1U)); p[2] = to8(sample(s + 2U));
                    p[3] = to8(sample(s + 3U));
                    break;
            }
        }
        std::swap(prior, current);
    }
    return out;
}

std::optional<ImagePreview> decode_texture_file(std::span<const std::uint8_t> bytes, std::string* error) {
    if (bytes.size() >= 4U && std::memcmp(bytes.data(), "DDS ", 4U) == 0) {
        const std::span<const std::byte> raw{reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()};
        const auto parsed = dds_bcn::parse(raw);
        if (!parsed.ok()) {
            fail(error, "a DDS the Reader cannot read");
            return std::nullopt;
        }
        auto decoded = dds_bcn::decode_level_rgba8(raw, parsed.document, 0U,
                                                   static_cast<std::uint64_t>(kMaxSide) * kMaxSide);
        if (!decoded.ok) {
            fail(error, "the DDS's first mip does not decode");
            return std::nullopt;
        }
        ImagePreview out;
        out.width = static_cast<std::uint32_t>(decoded.image.width);
        out.height = static_cast<std::uint32_t>(decoded.image.height);
        out.rgba8 = std::move(decoded.image.rgba8);
        if (!out.available()) { fail(error, "an empty DDS"); return std::nullopt; }
        return out;
    }
    return decode_png(bytes, error);
}

std::vector<TextureSlotInfo> texture_slots(const Session* session) {
    std::vector<TextureSlotInfo> out;
    if (session == nullptr) return out;
    for (std::size_t i = 0; i < session->attached_textures.size(); ++i) {
        TextureSlotInfo info;
        info.width = session->attached_textures[i].width;
        info.height = session->attached_textures[i].height;
        if (const auto it = session->texture_overrides.find(i); it != session->texture_overrides.end()) {
            info.overridden = true;
            info.override_name = it->second.name;
        }
        out.push_back(std::move(info));
    }
    return out;
}

bool override_texture(Session* session, std::size_t slot, ImagePreview image, std::string name) {
    if (session == nullptr || slot >= session->attached_textures.size() || !image.available()) return false;
    auto& entry = session->texture_overrides[slot];
    if (entry.name.empty()) entry.original = session->attached_textures[slot];
    entry.name = name.empty() ? std::string{"(picture)"} : std::move(name);
    session->attached_textures[slot] = std::move(image);
    return true;
}

bool restore_texture(Session* session, std::size_t slot) {
    if (session == nullptr) return false;
    bool changed = false;
    for (auto it = session->texture_overrides.begin(); it != session->texture_overrides.end();) {
        if (slot != static_cast<std::size_t>(-1) && it->first != slot) {
            ++it;
            continue;
        }
        if (it->first < session->attached_textures.size()) {
            session->attached_textures[it->first] = std::move(it->second.original);
            changed = true;
        }
        it = session->texture_overrides.erase(it);
    }
    return changed;
}

ImagePreview texture_image(const Session* session, std::size_t slot, std::uint32_t max_side, bool original) {
    if (session == nullptr || slot >= session->attached_textures.size()) return {};
    const ImagePreview* source = &session->attached_textures[slot];
    if (original) {
        if (const auto it = session->texture_overrides.find(slot); it != session->texture_overrides.end()) {
            source = &it->second.original;
        }
    }
    if (!source->available()) return {};
    if (max_side == 0U || (source->width <= max_side && source->height <= max_side)) return *source;
    // Box filter by the smallest whole factor that fits.
    const std::uint32_t factor =
        (std::max(source->width, source->height) + max_side - 1U) / max_side;
    ImagePreview out;
    out.width = std::max(1U, source->width / factor);
    out.height = std::max(1U, source->height / factor);
    out.rgba8.assign(static_cast<std::size_t>(out.width) * out.height * 4U, 0U);
    for (std::uint32_t y = 0; y < out.height; ++y) {
        for (std::uint32_t x = 0; x < out.width; ++x) {
            std::array<std::uint32_t, 4> sum{};
            for (std::uint32_t dy = 0; dy < factor; ++dy) {
                for (std::uint32_t dx = 0; dx < factor; ++dx) {
                    const std::size_t i = (static_cast<std::size_t>(y * factor + dy) * source->width + x * factor + dx) * 4U;
                    for (std::size_t k = 0; k < 4U; ++k) sum[k] += source->rgba8[i + k];
                }
            }
            for (std::size_t k = 0; k < 4U; ++k) {
                out.rgba8[(static_cast<std::size_t>(y) * out.width + x) * 4U + k] =
                    static_cast<std::uint8_t>(sum[k] / (factor * factor));
            }
        }
    }
    return out;
}

}  // namespace dmcresource
