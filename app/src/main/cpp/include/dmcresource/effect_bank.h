#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Read-only port of the effect bank loader 0x1402C04C0(bank, mode).
// Reverse authority: dmc-rengine-cpp docs/research/dmc3-effect-bank-loader-2026-09-24.md.
//
// A bank is a PNST: slot 0 a text manifest, slot 1 a PNST of records. The
// loader tokenizes the manifest (0x140322AB0, the .tsc tokenizer) and for each
// `<kind> <id>` pair hands the next record to the kind's registrar (jump table
// on kind - 'A'):
//   A 0x140322990   C 0x1402D3BE0   E 0x1402E87A0   G 0x1402ECBD0
//   M 0x1402E35D0 (record + the next one: model and its 16-byte companion)
//   P 0x140314B80   T 0x140322F20   V 0x140325030
// Any other letter consumes one record without registering it; a `#` token
// (e.g. "# End") ends the manifest. Enemies load their bank with mode 2
// (em028 slot 9, em000 family slot 41), stages with mode 0.
namespace dmcresource::effect_bank {

struct Record final {
    char kind{};
    std::uint32_t id{};
    std::uint32_t slot{};                    // record slot in the inner PNST
    std::span<const std::uint8_t> bytes;     // empty when the slot is absent
    std::span<const std::uint8_t> companion; // M only (the next slot)
};

struct Bank final {
    std::vector<Record> records;
    std::size_t record_slots{};   // inner PNST slot count
    std::size_t manifest_bytes{};
    bool terminated{};            // a '#' token ended the manifest
};

// Registrar address of a kind (0 when the loader ignores it).
[[nodiscard]] std::uint64_t registrar(char kind) noexcept;
// Short description of a kind; tentative except T (texture) and M (model).
[[nodiscard]] std::string_view kind_name(char kind) noexcept;

// Structural identity: PNST, slot 0 a manifest whose first token is one
// letter A..Z followed by a decimal id, slot 1 a PNST.
[[nodiscard]] bool looks_like_bank(std::span<const std::uint8_t> bytes) noexcept;

[[nodiscard]] std::optional<Bank> parse_bank(std::span<const std::uint8_t> bytes);

// Texture records: 112-byte descriptor, then a DDS file (DXT, with mips).
inline constexpr std::size_t kTextureDescriptorSize = 112U;
[[nodiscard]] std::span<const std::uint8_t> texture_dds(const Record& record) noexcept;

// Sprite animation (kind A, 336 bytes; layout from the data, registrar
// 0x140322990): [1, texture id, frame time, last frame index, loop, 0], then
// 10-byte frames (u16 x, y, w, h, 0) in texture pixels.
struct SpriteFrame final {
    std::uint16_t x{}, y{}, w{}, h{};
};
struct SpriteAnimation final {
    std::uint8_t texture{};
    std::uint8_t frame_time{};
    bool loop{};
    std::vector<SpriteFrame> frames;
};
[[nodiscard]] std::optional<SpriteAnimation> sprite_animation(const Record& record);

}  // namespace dmcresource::effect_bank
