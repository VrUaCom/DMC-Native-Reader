#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace dmcresource {

class BinaryReader {
public:
    BinaryReader(const std::uint8_t* bytes, std::size_t size) noexcept
        : bytes_(bytes), size_(size) {}

    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    [[nodiscard]] bool range(std::size_t offset, std::size_t count) const noexcept {
        return bytes_ != nullptr && offset <= size_ && count <= size_ - offset;
    }

    template <typename T>
    [[nodiscard]] bool read_le(std::size_t offset, T* out) const noexcept {
        static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>);
        if (out == nullptr || !range(offset, sizeof(T))) return false;
        std::memcpy(out, bytes_ + offset, sizeof(T));
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        return false;
#else
        return true;
#endif
    }

    [[nodiscard]] const std::uint8_t* ptr(std::size_t offset,
                                          std::size_t count) const noexcept {
        return range(offset, count) ? bytes_ + offset : nullptr;
    }

    [[nodiscard]] bool table(std::size_t offset,
                             std::size_t count,
                             std::size_t stride) const noexcept {
        if (stride == 0) return count == 0 && offset <= size_;
        if (count > std::numeric_limits<std::size_t>::max() / stride) return false;
        return range(offset, count * stride);
    }

private:
    const std::uint8_t* bytes_{};
    std::size_t size_{};
};

}  // namespace dmcresource
