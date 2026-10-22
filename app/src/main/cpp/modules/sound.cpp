#include "dmcresource/sound.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "dmcresource/raster_card.h"

extern "C" {
// third_party/stb_vorbis/stb_vorbis.c (built as its own C library).
int stb_vorbis_decode_memory(const unsigned char* mem, int len, int* channels, int* sample_rate, short** output);
}

namespace dmcresource::sound {
namespace {

constexpr std::size_t kVagHeader = 0x30U;
constexpr std::size_t kVagFrame = 16U;

[[nodiscard]] std::uint32_t be32(std::span<const std::uint8_t> b, std::size_t at) noexcept {
    return (std::uint32_t{b[at]} << 24U) | (std::uint32_t{b[at + 1U]} << 16U) |
           (std::uint32_t{b[at + 2U]} << 8U) | std::uint32_t{b[at + 3U]};
}

[[nodiscard]] std::uint32_t le32(std::span<const std::uint8_t> b, std::size_t at) noexcept {
    std::uint32_t value = 0U;
    std::memcpy(&value, b.data() + at, 4U);
    return value;
}

[[nodiscard]] bool vag_at(std::span<const std::uint8_t> bytes, std::size_t at) noexcept {
    return at + kVagHeader <= bytes.size() && std::memcmp(bytes.data() + at, "VAGp", 4U) == 0;
}

}  // namespace

bool looks_like_vag(std::span<const std::uint8_t> bytes) noexcept { return vag_at(bytes, 0U); }

std::vector<VagChunk> read_vag(std::span<const std::uint8_t> bytes) {
    std::vector<VagChunk> chunks;
    std::size_t at = 0U;
    while (vag_at(bytes, at) && chunks.size() < 4096U) {
        VagChunk chunk;
        chunk.offset = at;
        chunk.version = be32(bytes, at + 4U);
        chunk.data_size = be32(bytes, at + 12U);
        chunk.sample_rate = be32(bytes, at + 16U);
        const auto* name = reinterpret_cast<const char*>(bytes.data() + at + 32U);
        chunk.name.assign(name, strnlen(name, 16U));
        for (auto& c : chunk.name) {
            if (static_cast<unsigned char>(c) < 0x20U || static_cast<unsigned char>(c) > 0x7EU) c = '?';
        }
        chunk.data_offset = at + kVagHeader;
        const auto available = bytes.size() - static_cast<std::size_t>(chunk.data_offset);
        const auto used = std::min<std::size_t>(chunk.data_size, available);
        chunk.frames = used / kVagFrame;
        chunks.push_back(chunk);
        // The next chunk, if any, starts after this one's data on a 16-byte step.
        std::size_t next = static_cast<std::size_t>(chunk.data_offset) + used;
        next = (next + 15U) & ~std::size_t{15U};
        if (next <= at) break;
        at = next;
    }
    return chunks;
}

std::vector<std::int16_t> decode_vag(std::span<const std::uint8_t> bytes, const VagChunk& chunk) {
    // PS-ADPCM prediction filters (coefficients / 64).
    static constexpr std::array<std::array<int, 2>, 5> kFilters{{{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}}};
    std::vector<std::int16_t> samples;
    samples.reserve(static_cast<std::size_t>(chunk.frames) * 28U);
    int history1 = 0, history2 = 0;
    for (std::uint64_t frame = 0; frame < chunk.frames; ++frame) {
        const auto at = static_cast<std::size_t>(chunk.data_offset + frame * kVagFrame);
        const int shift = bytes[at] & 0x0F;
        const int filter = std::min<int>((bytes[at] >> 4U) & 0x0F, 4);
        const std::uint8_t flags = bytes[at + 1U];
        if (flags == 7U) break;  // end marker frame
        for (std::size_t i = 0; i < 28U; ++i) {
            const std::uint8_t packed = bytes[at + 2U + i / 2U];
            int nibble = (i % 2U == 0U) ? (packed & 0x0F) : (packed >> 4U);
            if (nibble >= 8) nibble -= 16;
            int sample = (nibble << 12) >> std::min(shift, 12);
            sample += (history1 * kFilters[static_cast<std::size_t>(filter)][0] +
                       history2 * kFilters[static_cast<std::size_t>(filter)][1] + 32) >> 6;
            sample = std::clamp(sample, -32768, 32767);
            history2 = history1;
            history1 = sample;
            samples.push_back(static_cast<std::int16_t>(sample));
        }
        // Flag 1 without loop (bit 1) ends a one-shot sound.
        if ((flags & 1U) != 0U && (flags & 2U) == 0U) break;
    }
    return samples;
}

ImagePreview render_waveform(std::span<const std::int16_t> samples, std::uint32_t sample_rate,
                             const std::string& title, int width, int height) {
    raster::Canvas canvas(width, height);
    canvas.fill(0, 0, width, height, raster::kBackground);
    canvas.text(16, 12, title, raster::kLabel, 2);
    const int left = 16, right = width - 16, top = 50, bottom = height - 40;
    const int mid = (top + bottom) / 2;
    canvas.line(left, mid, right, mid, raster::kGrid);
    if (!samples.empty()) {
        const double per_column = static_cast<double>(samples.size()) / static_cast<double>(right - left);
        for (int x = left; x < right; ++x) {
            const auto first = static_cast<std::size_t>((x - left) * per_column);
            const auto last = std::min(samples.size(), static_cast<std::size_t>((x - left + 1) * per_column) + 1U);
            int low = 0, high = 0;
            for (auto i = first; i < last; ++i) {
                low = std::min<int>(low, samples[i]);
                high = std::max<int>(high, samples[i]);
            }
            const int y0 = mid - high * (mid - top) / 32768;
            const int y1 = mid - low * (mid - top) / 32768;
            canvas.line(x, y0, x, y1, raster::Rgb{90, 200, 240});
        }
    }
    const double seconds = sample_rate == 0U ? 0.0 : static_cast<double>(samples.size()) / sample_rate;
    char axis[96];
    std::snprintf(axis, sizeof axis, "%.2f S  %u HZ  %zu SAMPLES", seconds, sample_rate, samples.size());
    canvas.text(16, bottom + 12, axis, raster::kDim, 2);
    return canvas.take();
}

bool read_sound_table(std::span<const std::uint8_t> bytes, SoundTable* out) noexcept {
    if (out == nullptr || bytes.size() < 16U || std::memcmp(bytes.data(), "dbsT", 4U) != 0) return false;
    SoundTable table;
    table.records = static_cast<std::uint32_t>((bytes.size() - 16U) / 32U);
    table.trailing = (bytes.size() - 16U) % 32U;
    for (int i = 0; i < 3; ++i) {
        table.header[i] = le32(bytes, 4U + static_cast<std::size_t>(i) * 4U);
        if (table.count_field < 0 && table.header[i] == table.records) table.count_field = i;
    }
    *out = table;
    return true;
}

bool looks_like_ogg(std::span<const std::uint8_t> bytes) noexcept {
    // Page header (27 bytes + segment table), then the identification
    // packet: 0x01 "vorbis".
    if (bytes.size() < 35U || std::memcmp(bytes.data(), "OggS", 4U) != 0 || bytes[4] != 0U) return false;
    const std::size_t segments = bytes[26];
    const std::size_t packet = 27U + segments;
    return bytes.size() >= packet + 7U && bytes[packet] == 0x01U &&
           std::memcmp(bytes.data() + packet + 1U, "vorbis", 6U) == 0;
}

std::optional<Pcm> decode_ogg(std::span<const std::uint8_t> bytes) {
    if (!looks_like_ogg(bytes) || bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    int channels = 0, rate = 0;
    short* output = nullptr;
    const int frames = stb_vorbis_decode_memory(bytes.data(), static_cast<int>(bytes.size()), &channels, &rate, &output);
    if (frames <= 0 || channels <= 0 || rate <= 0 || output == nullptr) {
        std::free(output);
        return std::nullopt;
    }
    Pcm pcm;
    pcm.sample_rate = static_cast<std::uint32_t>(rate);
    pcm.channels = static_cast<std::uint32_t>(channels);
    try {
        pcm.samples.assign(output, output + static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels));
    } catch (...) {
        std::free(output);
        throw;
    }
    std::free(output);
    return pcm;
}

std::vector<std::int16_t> mono(const Pcm& pcm) {
    if (pcm.channels <= 1U) return pcm.samples;
    std::vector<std::int16_t> out(pcm.frames());
    for (std::size_t f = 0; f < out.size(); ++f) {
        int sum = 0;
        for (std::uint32_t c = 0; c < pcm.channels; ++c) sum += pcm.samples[f * pcm.channels + c];
        out[f] = static_cast<std::int16_t>(sum / static_cast<int>(pcm.channels));
    }
    return out;
}

}  // namespace dmcresource::sound
