#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

// EFM (effect model) uses the MOD document layout: its post-load 0x1402F7A90
// relocates the same header +0x20, 0x40-byte objects and 0x50-byte meshes
// (+0x10..+0x38) as MOD's 0x1402FE3B0, and mesh +0x38 is the COLOR0 stream
// (RGBA8 per vertex, 0x80 = 1.0) that MOD leaves unused. CEm005Shl01 loads the
// em000.pac EFM through the ordinary model loader (vtbl +0x50, 0x1400AD77A).
// The canonical MOD parser keys on "MOD ", so EFM bytes are read from a copy
// under that magic; every offset stays the same.
namespace dmcresource {

[[nodiscard]] inline bool is_efm_bytes(const std::uint8_t* bytes, std::size_t size) noexcept {
    return bytes != nullptr && size >= 4U && std::memcmp(bytes, "EFM ", 4U) == 0;
}

class ModBytes final {
public:
    ModBytes(const std::uint8_t* bytes, std::size_t size)
        : efm_(is_efm_bytes(bytes, size)) {
        if (efm_) {
            copy_.resize(size);
            std::memcpy(copy_.data(), bytes, size);
            std::memcpy(copy_.data(), "MOD ", 4U);
            span_ = copy_;
        } else {
            span_ = std::span<const std::byte>{reinterpret_cast<const std::byte*>(bytes), size};
        }
    }

    [[nodiscard]] std::span<const std::byte> span() const noexcept { return span_; }
    [[nodiscard]] bool efm() const noexcept { return efm_; }

private:
    bool efm_{};
    std::vector<std::byte> copy_;
    std::span<const std::byte> span_;
};

}  // namespace dmcresource
