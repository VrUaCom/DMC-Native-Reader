#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace dmcresource::formats::dds {

enum class Compression : std::uint8_t {
    Dxt1,
    Dxt5,
};

struct Document final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t mip_count{};
    std::uint32_t payload_size{};
    std::uint32_t total_size{};
    Compression compression{Compression::Dxt1};
};

struct ParseResult final {
    bool ok{};
    Document document;
    std::string diagnostic;
};

[[nodiscard]] ParseResult parse(std::span<const std::uint8_t> bytes) noexcept;
[[nodiscard]] const char* compression_name(Compression compression) noexcept;

}  // namespace dmcresource::formats::dds
