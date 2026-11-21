// Archives of zlib-compressed files (HD collection GData.afs) open as the
// archive they make. The streams below were made with zlib from this test's
// own text, no game data.
#include "dmcresource/slot_inflate.h"
#include "dmcresource/resource_session.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace z = dmcresource::slot_inflate;

namespace {

// zlib level 9 (dynamic Huffman) of "MOD " + "slot 0 of the test archive. " ... "slot 39 ...".
const std::vector<std::uint8_t> kDynamic{0x78, 0xda, 0x7d, 0xd3, 0x31, 0x0a, 0xc2, 0x50, 0x14, 0x44, 0xd1, 0xad, 0xbc, 0x15, 0x88, 0x33, 0x63, 0x34, 0xf6, 0xb6, 0xe2, 0x1a, 0x42, 0xf8, 0x12, 0x21, 0x10, 0x30, 0x1f, 0xd7, 0x6f, 0x61, 0xed, 0xad, 0x6f, 0xf5, 0x0e, 0xf3, 0xee, 0x8f, 0x5b, 0xed, 0xeb, 0xd6, 0xeb, 0x58, 0xdb, 0xb3, 0xfa, 0xd2, 0xaa, 0xb7, 0xbd, 0xd7, 0xf4, 0x9e, 0x97, 0xd7, 0xa7, 0x1d, 0x7e, 0x4d, 0xd0, 0x0c, 0x2d, 0xd0, 0x4e, 0xd0, 0x06, 0x68, 0x67, 0x68, 0x17, 0x68, 0x23, 0xb4, 0x2b, 0xdd, 0x8e, 0x30, 0x24, 0x23, 0xa2, 0x11, 0xd9, 0x88, 0x70, 0x44, 0x3a, 0x22, 0x1e, 0x91, 0x8f, 0x08, 0x48, 0x24, 0x64, 0x12, 0x32, 0x6e, 0x87, 0x84, 0x4c, 0x42, 0x26, 0x21, 0x93, 0x90, 0x49, 0xc8, 0x24, 0x64, 0x12, 0x32, 0x09, 0x85, 0x84, 0x42, 0x42, 0xc1, 0xf7, 0x22, 0xa1, 0x90, 0x50, 0x48, 0x28, 0x24, 0x14, 0x12, 0x0a, 0x09, 0xe5, 0x8f, 0xd0, 0x17, 0x66, 0xa1, 0x87, 0x60};
// zlib level 0 (a stored block) of "PAC\0tiny".
const std::vector<std::uint8_t> kStored{0x78, 0x01, 0x01, 0x08, 0x00, 0xf7, 0xff, 0x50, 0x41, 0x43, 0x00, 0x74, 0x69, 0x6e, 0x79, 0x0a, 0x41, 0x02, 0x99};
// zlib with fixed Huffman codes of "abcabcabcabc".
const std::vector<std::uint8_t> kFixed{0x78, 0x01, 0x4b, 0x4c, 0x4a, 0x4e, 0x84, 0x21, 0x00, 0x1d, 0xe0, 0x04, 0x99};

std::string expected_text() {
    std::string text = "MOD ";
    for (int i = 0; i < 40; ++i) text += "slot " + std::to_string(i) + " of the test archive. ";
    return text;
}

void put(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    std::memcpy(bytes.data() + at, &value, 4U);
}
std::uint32_t get(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    std::uint32_t value = 0;
    std::memcpy(&value, bytes.data() + at, 4U);
    return value;
}

}  // namespace

int main() {
    // The three block kinds, Adler-32 checked.
    const auto text = expected_text();
    auto dynamic = z::inflate_zlib(kDynamic, 1U << 20U);
    assert(dynamic && std::string(dynamic->begin(), dynamic->end()) == text);
    auto stored = z::inflate_zlib(kStored, 64U);
    assert(stored && stored->size() == 8U && std::memcmp(stored->data(), "PAC\0tiny", 8U) == 0);
    auto fixed = z::inflate_zlib(kFixed, 64U);
    assert(fixed && std::string(fixed->begin(), fixed->end()) == "abcabcabcabc");
    // Over the limit, a broken checksum, a cut stream, not zlib at all.
    assert(!z::inflate_zlib(kDynamic, text.size() - 1U));
    auto broken = kDynamic;
    broken.back() ^= 1U;
    assert(!z::inflate_zlib(broken, 1U << 20U));
    assert(!z::inflate_zlib(std::span<const std::uint8_t>{kDynamic.data(), kDynamic.size() / 2U}, 1U << 20U));
    const std::uint8_t mod[] = {'M', 'O', 'D', ' '};
    assert(!z::looks_like_zlib(mod) && z::looks_like_zlib(kDynamic) && !z::inflate_zlib(mod, 64U));

    // PAC: slot 0 compressed, slot 1 empty, slot 2 stored as it is, slot 3 compressed.
    const std::uint8_t plain[] = {'S', 'C', 'M', ' ', 1, 2, 3, 4};
    std::vector<std::uint8_t> pac(0x20U, 0U);
    std::memcpy(pac.data(), "PAC\0", 4U);
    put(pac, 4U, 4U);
    put(pac, 8U, static_cast<std::uint32_t>(pac.size()));
    pac.insert(pac.end(), kDynamic.begin(), kDynamic.end());
    put(pac, 16U, static_cast<std::uint32_t>(pac.size()));
    pac.insert(pac.end(), std::begin(plain), std::end(plain));
    put(pac, 20U, static_cast<std::uint32_t>(pac.size()));
    pac.insert(pac.end(), kStored.begin(), kStored.end());
    const auto expanded = z::expand(pac);
    assert(expanded && std::memcmp(expanded->data(), "PAC\0", 4U) == 0 && get(*expanded, 4U) == 4U);
    const auto s0 = get(*expanded, 8U);
    const auto s2 = get(*expanded, 16U);
    const auto s3 = get(*expanded, 20U);
    assert(get(*expanded, 12U) == 0U);
    assert(s0 % 4U == 0U && s2 % 4U == 0U && s3 % 4U == 0U && s0 < s2 && s2 < s3);
    // Each slot keeps its exact size (the distance to the next slot or the end).
    assert(s2 - s0 == (text.size() + 3U) / 4U * 4U && s3 - s2 == sizeof plain && expanded->size() - s3 == 8U);
    assert(std::memcmp(expanded->data() + s0, text.data(), text.size()) == 0);
    assert(std::memcmp(expanded->data() + s2, plain, sizeof plain) == 0);
    assert(std::memcmp(expanded->data() + s3, "PAC\0tiny", 8U) == 0);

    // An archive with nothing compressed, or not an archive: left alone.
    std::vector<std::uint8_t> raw(0x10U, 0U);
    std::memcpy(raw.data(), "PAC\0", 4U);
    put(raw, 4U, 1U);
    put(raw, 8U, 0x10U);
    raw.insert(raw.end(), std::begin(plain), std::end(plain));
    assert(!z::expand(raw) && !z::expand(kDynamic) && !z::expand({}));
    // An offset past the end is no archive to rebuild.
    auto bad = pac;
    put(bad, 8U, 0xFFFFFFU);
    assert(!z::expand(bad));

    // The session opens the inflated archive: its slots are the files they hold.
    auto session = dmcresource::open_session("test.pac", pac.data(), pac.size());
    assert(session && session->detail.find("zlib-compressed") != std::string::npos);
    return 0;
}
