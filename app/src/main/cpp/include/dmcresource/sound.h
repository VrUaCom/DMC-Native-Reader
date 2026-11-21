#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "dmcresource/image_preview.h"

// Sound resources of the stage corpus (stage corpus reverse notes, Ada-Astra
// pass): VAGp streams (250 .ukn payloads; the EXE's VAGp/FM0D sound path) and
// dbsT sound tables (33 .tsb files, 32-byte records).
namespace dmcresource::sound {

// ---- VAG -------------------------------------------------------------------
// Sony VAG: a 0x30-byte big-endian header ("VAGp", version, reserved, data
// size, sample rate, 12 reserved bytes, a 16-byte name) and PS-ADPCM frames
// of 16 bytes (shift / filter, flags, 28 four-bit samples). A file may hold
// several such chunks one after another.
struct VagChunk final {
    std::uint64_t offset{};
    std::uint32_t version{};
    std::uint32_t data_size{};
    std::uint32_t sample_rate{};
    std::string name;
    std::uint64_t data_offset{};
    std::uint64_t frames{};  // 16-byte frames present in the file
};

[[nodiscard]] bool looks_like_vag(std::span<const std::uint8_t> bytes) noexcept;
// Every chunk from the first header on; empty when the bytes are not VAG.
[[nodiscard]] std::vector<VagChunk> read_vag(std::span<const std::uint8_t> bytes);
// The chunk's samples, 16-bit mono, decoded from its PS-ADPCM frames up to
// its data size or an end flag.
[[nodiscard]] std::vector<std::int16_t> decode_vag(std::span<const std::uint8_t> bytes, const VagChunk& chunk);
// A picture of the samples: the waveform (min and max per column), a time
// axis in seconds, and the title.
[[nodiscard]] ImagePreview render_waveform(std::span<const std::int16_t> samples, std::uint32_t sample_rate,
                                           const std::string& title, int width = 1080, int height = 540);

// ---- dbsT ------------------------------------------------------------------
// "dbsT", a 16-byte header, then 32-byte records to the end; a header word
// counts the records.
struct SoundTable final {
    std::uint32_t records{};
    std::uint32_t header[3]{};      // u32 at +4, +8, +12
    int count_field{-1};            // which header word equals the record count, -1 none
    std::uint64_t trailing{};       // bytes after the last whole record
};
[[nodiscard]] bool read_sound_table(std::span<const std::uint8_t> bytes, SoundTable* out) noexcept;

// Ogg Vorbis: the HD release stores the game's ADX streams (music, voices,
// cutscene sound) as .ogg; the EXE renames each requested .adx to .ogg
// (0x140031D80). Decoded with stb_vorbis (third_party).
struct Pcm final {
    std::uint32_t sample_rate{};
    std::uint32_t channels{};
    std::vector<std::int16_t> samples;  // interleaved, channels per frame

    [[nodiscard]] std::size_t frames() const noexcept {
        return channels == 0U ? 0U : samples.size() / channels;
    }
};
// An Ogg page ("OggS", version 0) whose first packet is a Vorbis
// identification header.
[[nodiscard]] bool looks_like_ogg(std::span<const std::uint8_t> bytes) noexcept;
[[nodiscard]] std::optional<Pcm> decode_ogg(std::span<const std::uint8_t> bytes);
// The channels averaged into one.
[[nodiscard]] std::vector<std::int16_t> mono(const Pcm& pcm);
// The sound at another sample rate (linear interpolation), so sounds of
// different rates mix.
[[nodiscard]] Pcm resample(const Pcm& pcm, std::uint32_t sample_rate);

}  // namespace dmcresource::sound
