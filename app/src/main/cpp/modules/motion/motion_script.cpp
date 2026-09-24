#include "dmcresource/motion/motion_script.h"

#include <cctype>
#include <span>

namespace dmcresource::motion {
namespace {

// Instruction lengths of the fixed-size opcodes (interpreter 0x140058FE0,
// jump table 0x140059240 / 0x140059204).
[[nodiscard]] std::size_t opcode_length(std::uint8_t op) noexcept {
    switch (op) {
    case 1U: return 8U;               // play MOT (0x140059950)
    case 3U: return 6U;               // channel 0 bytes -> +0x91..+0x95
    case 4U: return 6U;               // channel 1 bytes -> +0x96..+0x9A
    case 5U: return 2U;               // clear channels
    case 6U: return 2U;               // 0x140059650
    case 7U: return 2U;
    case 8U: return 2U;
    case 32U: return 4U;              // 0x140059820
    case 33U: return 2U;
    case 34U: return 6U;              // 0x1400596A0
    case 35U: return 2U;
    case 36U: return 4U;              // 0x1400598C0
    default: break;
    }
    if (op >= 16U && op <= 31U) return static_cast<std::size_t>(op - 15U) * 2U + 4U;
    return 0U;
}

}  // namespace

std::optional<MotionScriptFile> MotionScriptFile::parse(std::span<const std::uint8_t> bytes) {
    const auto u16 = [&bytes](std::size_t o) -> std::optional<std::size_t> {
        if (o + 2U > bytes.size()) return std::nullopt;
        return static_cast<std::size_t>(bytes[o]) | (static_cast<std::size_t>(bytes[o + 1U]) << 8U);
    };
    // 0x1400594B0 (mode 0): table = file + u16[0]; banks = table + u16[table].
    const auto table = u16(0U);
    if (!table || *table < 6U) return std::nullopt;
    const auto first = u16(*table);
    if (!first) return std::nullopt;
    const std::size_t base = *table + *first;
    MotionScriptFile out;
    for (std::size_t o = base; out.banks_.size() < 64U; o += 2U) {
        const auto entry = u16(o);
        if (!entry) return std::nullopt;
        if (*entry == 0xFFFFU) break;
        if (base + *entry >= bytes.size()) return std::nullopt;
        out.banks_.push_back(base + *entry);
    }
    if (out.banks_.empty()) return std::nullopt;
    out.bytes_.assign(bytes.begin(), bytes.end());
    return out;
}

std::vector<WeaponStateKey> MotionScriptFile::weapon_states(std::size_t bank,
                                                           std::size_t index) const {
    std::vector<WeaponStateKey> keys;
    if (bank >= banks_.size()) return keys;
    const auto& s = bytes_;
    const auto u16 = [&s](std::size_t o) -> std::size_t {
        return o + 2U <= s.size()
            ? static_cast<std::size_t>(s[o]) | (static_cast<std::size_t>(s[o + 1U]) << 8U)
            : 0xFFFFFFFFU;
    };
    const std::size_t sub = banks_[bank];
    const std::size_t entry = u16(sub + index * 2U);
    if (entry == 0xFFFFFFFFU) return keys;
    std::size_t p = sub + entry;
    float after = -1.0F;
    bool started = false;
    for (int step = 0; step < 4096 && p < s.size(); ++step) {
        const std::uint8_t op = s[p];
        if (op == 0U) {
            // Wait: the next block runs once the frame is past `frame`.
            const auto frame = u16(p + 2U);
            if (frame == 0xFFFFFFFFU || frame == 0x7FFFU) break;
            after = static_cast<float>(frame);
            p += 6U;
            continue;
        }
        if (op == 1U) {
            // Play MOT: a second one hands over to another motion.
            if (started) break;
            started = true;
        }
        if (op == 2U) break;  // backward jump (loop)
        if (op == 3U && p + 6U <= s.size()) {
            const std::uint8_t state = static_cast<std::uint8_t>(s[p + 2U] & 0x3FU);
            if (state != 0U) keys.push_back({after, state});
        }
        const auto length = opcode_length(op);
        if (length == 0U) break;
        p += length;
    }
    return keys;
}

std::uint8_t weapon_state_at(const std::vector<WeaponStateKey>& keys, float frame) noexcept {
    std::uint8_t state = 0U;
    for (const auto& key : keys) {
        if (frame > key.after_frame) state = key.state;
    }
    return state;
}

std::optional<std::size_t> player_motion_bank(std::string_view name) noexcept {
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string_view::npos) name.remove_prefix(slash + 1U);
    constexpr std::string_view prefix = "pl000_00_";
    if (name.size() <= prefix.size() + 4U) return std::nullopt;
    for (std::size_t i = 0U; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(name[i])) != prefix[i]) return std::nullopt;
    }
    std::size_t value = 0U;
    std::size_t i = prefix.size();
    for (; i < name.size() && std::isdigit(static_cast<unsigned char>(name[i])); ++i) {
        value = value * 10U + static_cast<std::size_t>(name[i] - '0');
    }
    if (i == prefix.size() || name.substr(i).size() != 4U) return std::nullopt;
    const auto ext = name.substr(i);
    if (std::tolower(static_cast<unsigned char>(ext[1])) != 'p' ||
        std::tolower(static_cast<unsigned char>(ext[2])) != 'a' ||
        std::tolower(static_cast<unsigned char>(ext[3])) != 'c' || ext[0] != '.') {
        return std::nullopt;
    }
    return value;
}

}  // namespace dmcresource::motion
