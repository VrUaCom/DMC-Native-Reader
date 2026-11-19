#include "dmcresource/slot_inflate.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "dmcresource/ptx_rgba.h"
#include "dmcresource/resource_limits.h"

namespace dmcresource::slot_inflate {
namespace {

// ---- RFC 1951 -------------------------------------------------------------

class Bits final {
public:
    explicit Bits(std::span<const std::uint8_t> bytes) noexcept : bytes_{bytes} {}

    // `count` bits (0..16), least significant first; false past the end.
    bool take(int count, int* out) noexcept {
        while (held_ < count) {
            if (next_ >= bytes_.size()) return false;
            buffer_ |= static_cast<std::uint32_t>(bytes_[next_++]) << held_;
            held_ += 8;
        }
        *out = static_cast<int>(buffer_ & ((1U << count) - 1U));
        buffer_ >>= count;
        held_ -= count;
        return true;
    }

    void align() noexcept {
        buffer_ = 0U;
        held_ = 0;
    }

    [[nodiscard]] std::size_t position() const noexcept { return next_; }
    void skip(std::size_t count) noexcept { next_ += count; }
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return bytes_; }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t next_{};
    std::uint32_t buffer_{};
    int held_{};
};

constexpr int kMaxBits = 15;

// A canonical Huffman code: how many codes of each length, the symbols in
// code order.
struct Code final {
    std::array<short, kMaxBits + 1> count{};
    std::array<short, 320> symbol{};
};

// Builds `code` from `lengths`; false when the lengths over-subscribe the code
// or (with `complete`) leave it incomplete beyond a single code.
bool build(Code* code, const short* lengths, int n) noexcept {
    code->count.fill(0);
    for (int s = 0; s < n; ++s) ++code->count[static_cast<std::size_t>(lengths[s])];
    if (code->count[0] == n) return true;  // no codes: only valid if never used
    int left = 1;
    for (int len = 1; len <= kMaxBits; ++len) {
        left <<= 1;
        left -= code->count[static_cast<std::size_t>(len)];
        if (left < 0) return false;
    }
    std::array<short, kMaxBits + 1> offs{};
    for (int len = 1; len < kMaxBits; ++len) {
        offs[static_cast<std::size_t>(len + 1)] =
            static_cast<short>(offs[static_cast<std::size_t>(len)] + code->count[static_cast<std::size_t>(len)]);
    }
    for (int s = 0; s < n; ++s) {
        if (lengths[s] != 0) code->symbol[static_cast<std::size_t>(offs[static_cast<std::size_t>(lengths[s])]++)] =
            static_cast<short>(s);
    }
    return true;
}

// The next symbol of `code`; -1 at the end of the input or on an unused code.
int decode(Bits& in, const Code& code) noexcept {
    int value = 0;
    int first = 0;
    int index = 0;
    for (int len = 1; len <= kMaxBits; ++len) {
        int bit = 0;
        if (!in.take(1, &bit)) return -1;
        value |= bit;
        const int count = code.count[static_cast<std::size_t>(len)];
        if (value - count < first) return code.symbol[static_cast<std::size_t>(index + (value - first))];
        index += count;
        first += count;
        first <<= 1;
        value <<= 1;
    }
    return -1;
}

constexpr std::array<short, 29> kLengthBase{3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
                                            31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<short, 29> kLengthExtra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                             2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<int, 30> kDistanceBase{1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                            193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                            6145, 8193, 12289, 16385, 24577};
constexpr std::array<short, 30> kDistanceExtra{0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                               6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool codes(Bits& in, std::vector<std::uint8_t>& out, std::size_t limit, const Code& lengths,
           const Code& distances) {
    for (;;) {
        int symbol = decode(in, lengths);
        if (symbol < 0) return false;
        if (symbol < 256) {
            if (out.size() >= limit) return false;
            out.push_back(static_cast<std::uint8_t>(symbol));
            continue;
        }
        if (symbol == 256) return true;
        symbol -= 257;
        if (symbol >= 29) return false;
        int extra = 0;
        if (!in.take(kLengthExtra[static_cast<std::size_t>(symbol)], &extra)) return false;
        const auto length = static_cast<std::size_t>(kLengthBase[static_cast<std::size_t>(symbol)] + extra);
        symbol = decode(in, distances);
        if (symbol < 0 || symbol >= 30) return false;
        if (!in.take(kDistanceExtra[static_cast<std::size_t>(symbol)], &extra)) return false;
        const auto distance = static_cast<std::size_t>(kDistanceBase[static_cast<std::size_t>(symbol)] + extra);
        if (distance > out.size() || length > limit - std::min(limit, out.size())) return false;
        const std::size_t from = out.size() - distance;
        for (std::size_t i = 0; i < length; ++i) out.push_back(out[from + i]);
    }
}

bool fixed_block(Bits& in, std::vector<std::uint8_t>& out, std::size_t limit) {
    static const auto tables = [] {
        std::pair<Code, Code> t;
        std::array<short, 288> lengths{};
        int s = 0;
        for (; s < 144; ++s) lengths[static_cast<std::size_t>(s)] = 8;
        for (; s < 256; ++s) lengths[static_cast<std::size_t>(s)] = 9;
        for (; s < 280; ++s) lengths[static_cast<std::size_t>(s)] = 7;
        for (; s < 288; ++s) lengths[static_cast<std::size_t>(s)] = 8;
        (void)build(&t.first, lengths.data(), 288);
        lengths.fill(5);
        (void)build(&t.second, lengths.data(), 30);
        return t;
    }();
    return codes(in, out, limit, tables.first, tables.second);
}

bool dynamic_block(Bits& in, std::vector<std::uint8_t>& out, std::size_t limit) {
    int nlen = 0;
    int ndist = 0;
    int ncode = 0;
    if (!in.take(5, &nlen) || !in.take(5, &ndist) || !in.take(4, &ncode)) return false;
    nlen += 257;
    ndist += 1;
    ncode += 4;
    if (nlen > 286 || ndist > 30) return false;
    static constexpr std::array<int, 19> order{16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    std::array<short, 320> lengths{};
    for (int i = 0; i < ncode; ++i) {
        int value = 0;
        if (!in.take(3, &value)) return false;
        lengths[static_cast<std::size_t>(order[static_cast<std::size_t>(i)])] = static_cast<short>(value);
    }
    Code length_code;
    if (!build(&length_code, lengths.data(), 19)) return false;
    int index = 0;
    while (index < nlen + ndist) {
        const int symbol = decode(in, length_code);
        if (symbol < 0) return false;
        if (symbol < 16) {
            lengths[static_cast<std::size_t>(index++)] = static_cast<short>(symbol);
            continue;
        }
        short value = 0;
        int repeat = 0;
        if (symbol == 16) {
            if (index == 0) return false;
            value = lengths[static_cast<std::size_t>(index - 1)];
            if (!in.take(2, &repeat)) return false;
            repeat += 3;
        } else if (symbol == 17) {
            if (!in.take(3, &repeat)) return false;
            repeat += 3;
        } else {
            if (!in.take(7, &repeat)) return false;
            repeat += 11;
        }
        if (index + repeat > nlen + ndist) return false;
        while (repeat-- > 0) lengths[static_cast<std::size_t>(index++)] = value;
    }
    if (lengths[256] == 0) return false;  // no end-of-block code
    Code literal;
    Code distance;
    if (!build(&literal, lengths.data(), nlen)) return false;
    if (!build(&distance, lengths.data() + nlen, ndist)) return false;
    return codes(in, out, limit, literal, distance);
}

bool stored_block(Bits& in, std::vector<std::uint8_t>& out, std::size_t limit) {
    in.align();
    const auto bytes = in.bytes();
    const std::size_t at = in.position();
    if (bytes.size() < at + 4U) return false;
    const unsigned length = bytes[at] | (bytes[at + 1U] << 8U);
    const unsigned inverse = bytes[at + 2U] | (bytes[at + 3U] << 8U);
    if ((length ^ 0xFFFFU) != inverse) return false;
    if (bytes.size() < at + 4U + length || length > limit - std::min(limit, out.size())) return false;
    out.insert(out.end(), bytes.begin() + static_cast<std::ptrdiff_t>(at + 4U),
               bytes.begin() + static_cast<std::ptrdiff_t>(at + 4U + length));
    in.skip(4U + length);
    return true;
}

// Raw DEFLATE from `in`; the reader then stands at the byte after the stream.
bool inflate_raw(Bits& in, std::vector<std::uint8_t>& out, std::size_t limit) {
    int last = 0;
    do {
        int type = 0;
        if (!in.take(1, &last) || !in.take(2, &type)) return false;
        bool ok = false;
        switch (type) {
            case 0: ok = stored_block(in, out, limit); break;
            case 1: ok = fixed_block(in, out, limit); break;
            case 2: ok = dynamic_block(in, out, limit); break;
            default: return false;
        }
        if (!ok) return false;
    } while (last == 0);
    in.align();
    return true;
}

std::uint32_t adler32(std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t a = 1U;
    std::uint32_t b = 0U;
    std::size_t i = 0;
    while (i < bytes.size()) {
        const std::size_t end = std::min(bytes.size(), i + 5552U);
        for (; i < end; ++i) {
            a += bytes[i];
            b += a;
        }
        a %= 65521U;
        b %= 65521U;
    }
    return (b << 16U) | a;
}

std::uint32_t u32(std::span<const std::uint8_t> bytes, std::size_t at) noexcept {
    std::uint32_t value = 0;
    std::memcpy(&value, bytes.data() + at, 4U);
    return value;  // the archives are little-endian, as every supported host
}

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) noexcept {
    std::memcpy(bytes.data() + at, &value, 4U);
}

// Slots start on 4-byte boundaries and keep their exact size: a slot's size
// is the distance to the next one, and readers check it (a collision shape
// table is a whole number of 80-byte records, em000 slot 40 has 23). Padding
// slots to 0x800 sectors as the retail files do would change those sizes.
constexpr std::size_t kSlotAlignment = 4U;
constexpr std::size_t kHeaderAlignment = 16U;

std::size_t align_up(std::size_t value, std::size_t to) noexcept { return (value + to - 1U) / to * to; }

}  // namespace

bool looks_like_zlib(std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() < 2U) return false;
    const unsigned cmf = bytes[0];
    const unsigned flg = bytes[1];
    return (cmf & 0x0FU) == 8U && (cmf >> 4U) <= 7U && (flg & 0x20U) == 0U && ((cmf << 8U) | flg) % 31U == 0U;
}

std::optional<std::vector<std::uint8_t>> inflate_zlib(std::span<const std::uint8_t> stream, std::size_t limit) {
    try {
        if (!looks_like_zlib(stream)) return std::nullopt;
        Bits in{stream};
        in.skip(2U);
        std::vector<std::uint8_t> out;
        out.reserve(std::min(limit, stream.size() * 4U));
        if (!inflate_raw(in, out, limit)) return std::nullopt;
        const std::size_t at = in.position();
        if (stream.size() < at + 4U) return std::nullopt;
        const std::uint32_t stored = (static_cast<std::uint32_t>(stream[at]) << 24U) |
                                     (static_cast<std::uint32_t>(stream[at + 1U]) << 16U) |
                                     (static_cast<std::uint32_t>(stream[at + 2U]) << 8U) | stream[at + 3U];
        if (stored != adler32(out)) return std::nullopt;
        return out;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::vector<std::uint8_t>> expand(std::span<const std::uint8_t> archive, Expanded* what) {
    try {
        if (archive.size() < 8U) return std::nullopt;
        const bool pac = std::memcmp(archive.data(), "PAC\0", 4U) == 0;
        const bool pnst = std::memcmp(archive.data(), "PNST", 4U) == 0;
        if (!pac && !pnst) return std::nullopt;
        const std::uint32_t count = u32(archive, 4U);
        const std::size_t table_end = 8U + static_cast<std::size_t>(count) * 4U;
        if (count == 0U || count > (1U << 20U) || table_end > archive.size()) return std::nullopt;

        // Each populated slot runs to the next higher offset, as the
        // container parser reads it.
        std::vector<std::uint32_t> offsets(count);
        for (std::uint32_t i = 0; i < count; ++i) offsets[i] = u32(archive, 8U + i * 4U);
        std::vector<std::uint32_t> sorted;
        for (const auto offset : offsets) {
            if (offset == 0U) continue;
            if (offset < table_end || offset >= archive.size()) return std::nullopt;
            sorted.push_back(offset);
        }
        std::sort(sorted.begin(), sorted.end());
        sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
        const auto slot_of = [&](std::uint32_t offset) {
            const auto next = std::upper_bound(sorted.begin(), sorted.end(), offset);
            const std::size_t end = next == sorted.end() ? archive.size() : *next;
            return archive.subspan(offset, end - offset);
        };

        // Inflate each distinct slot once (slots sharing an offset share it).
        const std::size_t limit = resource_limits::kMaxResourceBytes;
        std::vector<std::vector<std::uint8_t>> payloads(sorted.size());
        std::size_t inflated = 0;
        std::size_t textures = 0;
        std::size_t total = align_up(table_end, kHeaderAlignment);
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            const auto slot = slot_of(sorted[i]);
            if (looks_like_zlib(slot)) {
                if (auto out = inflate_zlib(slot, limit)) {
                    payloads[i] = std::move(*out);
                    ++inflated;
                } else {
                    payloads[i].assign(slot.begin(), slot.end());  // a file that only starts like zlib
                }
            } else {
                payloads[i].assign(slot.begin(), slot.end());
            }
            // A texture bundle with uncompressed textures: encoded as BC3 (ptx_rgba.h).
            if (auto compressed = ptx_rgba::compress(payloads[i])) {
                payloads[i] = std::move(*compressed);
                ++textures;
            }
            total = align_up(total, kSlotAlignment) + payloads[i].size();
            if (total > limit) return std::nullopt;
        }
        if (inflated == 0U && textures == 0U) return std::nullopt;
        if (what != nullptr) *what = Expanded{inflated, textures};

        std::vector<std::uint8_t> out(align_up(table_end, kHeaderAlignment), 0U);
        std::memcpy(out.data(), archive.data(), 8U);
        std::vector<std::uint32_t> placed(sorted.size());
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            out.resize(align_up(out.size(), kSlotAlignment), 0U);
            placed[i] = static_cast<std::uint32_t>(out.size());
            out.insert(out.end(), payloads[i].begin(), payloads[i].end());
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t offset = 0U;
            if (offsets[i] != 0U) {
                const auto at = std::lower_bound(sorted.begin(), sorted.end(), offsets[i]) - sorted.begin();
                offset = placed[static_cast<std::size_t>(at)];
            }
            put_u32(out, 8U + i * 4U, offset);
        }
        return out;
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace dmcresource::slot_inflate
