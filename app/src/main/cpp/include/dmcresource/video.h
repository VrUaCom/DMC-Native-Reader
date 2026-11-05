#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "dmcresource/mesh.h"
#include "dmcresource/sound.h"

// Movies. The HD release stores the game's SFD movies as .wmv: the EXE
// copies the requested path and swaps its extension for ".wmv" before the
// media loader (vendored Rengine research, wave 2, section 10). A .wmv is an
// ASF file (header object GUID 75B22630-668E-11CF-A6D9-00AA0062CE6C) with
// Windows Media video and audio streams, decoded here by a minimal FFmpeg
// (third_party/ffmpeg) when the build has it.
namespace dmcresource::video {

[[nodiscard]] bool looks_like_asf(std::span<const std::uint8_t> bytes) noexcept;

// Whether this build decodes movies (built with FFmpeg).
[[nodiscard]] bool available() noexcept;

struct MovieInfo final {
    int width{};
    int height{};
    double duration{};    // seconds
    double frame_rate{};  // frames a second (0 when the file does not say)
    std::string video_codec;
    std::string audio_codec;  // empty without a sound stream
    std::uint32_t audio_rate{};
    std::uint32_t audio_channels{};
};

class Movie {
public:
    virtual ~Movie() = default;
    [[nodiscard]] virtual const MovieInfo& info() const noexcept = 0;
    // The picture shown at `seconds`: the last frame that starts at or before
    // it (the first frame before the first). Decodes forward from the last
    // call, seeking back or far ahead from the nearest key frame.
    [[nodiscard]] virtual bool frame_at(double seconds, RgbaImage* out) = 0;
    // The whole sound stream, decoded (16-bit, up to two channels); none
    // without one.
    [[nodiscard]] virtual std::optional<sound::Pcm> sound() = 0;
};

// The movie in `bytes`; nullptr (and why, in `error`) when it cannot be
// decoded or this build has no decoder.
[[nodiscard]] std::unique_ptr<Movie> open_movie(std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                                                std::string* error = nullptr);

// The picture fitted into width x height, centred on black (letterboxed),
// bilinear.
[[nodiscard]] RgbaImage fit(const RgbaImage& picture, int width, int height);

}  // namespace dmcresource::video
