#include "dmcresource/motion/uv_scroll.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

#include "dmcresource/resource_session.h"

namespace dmcresource::motion {
namespace {

// Tokenizer 0x140322AB0: separators are NUL, tab, LF, CR, space and ','; ';'
// skips to the end of the line; '$' (or NUL) ends the text.
class Tokens final {
public:
    explicit Tokens(std::string_view text) : text_(text) {}

    [[nodiscard]] std::optional<std::string_view> next() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == '$' || c == '\0') {
                pos_ = text_.size();
                return std::nullopt;
            }
            if (c == ';') {
                while (pos_ < text_.size() && text_[pos_] != '\n') ++pos_;
                continue;
            }
            if (separator(c)) {
                ++pos_;
                continue;
            }
            break;
        }
        if (pos_ >= text_.size()) return std::nullopt;
        const auto begin = pos_;
        while (pos_ < text_.size() && !separator(text_[pos_]) && text_[pos_] != '$' &&
               text_[pos_] != ';' && text_[pos_] != '\0') {
            ++pos_;
        }
        return text_.substr(begin, pos_ - begin);
    }

    [[nodiscard]] int next_int() {
        const auto token = next();
        if (!token) return 0;
        try {
            return std::stoi(std::string{*token});
        } catch (...) {
            return 0;
        }
    }

    [[nodiscard]] float next_float() {
        const auto token = next();
        if (!token) return 0.0F;
        try {
            return std::stof(std::string{*token});
        } catch (...) {
            return 0.0F;
        }
    }

private:
    [[nodiscard]] static bool separator(char c) noexcept {
        return c == '\t' || c == '\n' || c == '\r' || c == ' ' || c == ',';
    }

    std::string_view text_;
    std::size_t pos_{};
};

// 0x14030ABE0: "ScrlNo n ScrlType t" then keys until "End>".
std::optional<ScrollRecord> parse_block(Tokens& tokens) {
    if (tokens.next() != std::string_view{"ScrlNo"}) return std::nullopt;
    ScrollRecord record;
    record.number = static_cast<std::uint8_t>(tokens.next_int());
    if (tokens.next() != std::string_view{"ScrlType"}) return std::nullopt;
    record.type = static_cast<std::uint8_t>(tokens.next_int());
    while (const auto token = tokens.next()) {
        const auto key = *token;
        if (key == "End>") break;
        if (key == "TexNo") {
            record.texture = static_cast<std::int16_t>(tokens.next_int());
        } else if (key == "JntNo") {
            record.joint = static_cast<std::int16_t>(tokens.next_int());
        } else if (key == "DirUV") {
            const auto u = tokens.next().value_or("");
            record.direction[0] = u == "left" ? 1 : u == "right" ? -1 : 0;
            const auto v = tokens.next().value_or("");
            record.direction[1] = v == "up" ? 1 : v == "down" ? -1 : 0;
        } else if (key == "RateUV") {
            record.rate[0] = tokens.next_float();
            record.rate[1] = tokens.next_float();
        } else if (key == "TimeUV") {
            record.time[0] = tokens.next_float();
            record.time[1] = tokens.next_float();
        } else if (key == "InterUV") {
            record.interval[0] = tokens.next_float();
            record.interval[1] = tokens.next_float();
        } else if (key == "TurnTimeUV") {
            record.turn_time[0] = tokens.next_float();
            record.turn_time[1] = tokens.next_float();
        } else if (key == "MinimumUV") {
            record.minimum[0] = tokens.next_float();
            record.minimum[1] = tokens.next_float();
            record.has_minimum = true;
        } else if (key == "RndUV") {
            for (auto& value : record.random) value = tokens.next_float();
            record.has_random = true;
        }
    }
    return record;
}

[[nodiscard]] float wrap(float value) noexcept { return value - std::floor(value); }

// Steps taken after `frames` frames: the InterUV counter starts at the
// interval, drops by dt each frame and steps (then reloads) at <= 0.
[[nodiscard]] float steps_after(float frames, float interval) noexcept {
    const float period = interval > 1.0F ? std::ceil(interval) : 1.0F;
    return std::floor(std::max(frames, 0.0F) / period);
}

}  // namespace

bool looks_like_tsc(std::string_view text) {
    Tokens tokens{text.substr(0U, std::min<std::size_t>(text.size(), 256U))};
    return tokens.next() == std::string_view{".TSC"};
}

std::vector<ScrollRecord> parse_tsc(std::string_view text) {
    std::vector<ScrollRecord> out;
    Tokens tokens{text};
    // 0x14030A9B0: ".TSC", then "#" -> RELATIVE (100) / ABSOLUTE (200, unread);
    // in RELATIVE "<Start" parses a block and "<Finish>" returns to state 0.
    if (tokens.next() != std::string_view{".TSC"}) return out;
    int state = 0;
    while (const auto token = tokens.next()) {
        if (state == 0) {
            if (*token == "#") state = 1;
        } else if (state == 1) {
            if (*token == "RELATIVE") state = 100;
            else if (*token == "ABSOLUTE") state = 200;
        } else if (state == 100) {
            if (*token == "<Finish>") {
                state = 0;
            } else if (*token == "<Start") {
                auto record = parse_block(tokens);
                if (!record) break;
                out.push_back(*record);
            }
        }
    }
    return out;
}

std::optional<std::array<float, 2>> scroll_offset(const ScrollRecord& record,
                                                  float frames) noexcept {
    if (!std::isfinite(frames)) return std::nullopt;
    const bool eased = record.type == 2U || record.type == 3U;
    const bool timed = record.type == 1U || record.type == 3U;
    if (record.type > 3U) return std::nullopt;
    std::array<float, 2> out{};
    for (std::size_t axis = 0U; axis < 2U; ++axis) {
        const float dir = static_cast<float>(record.direction[axis]);
        // Types 1-3 (0x14030C710 / 0x14030C850 and their v twins) stop at
        // TimeUV == 0 or DirUV stay; type 0 (0x14030C2D0) never checks.
        if (record.type != 0U && (record.time[axis] == 0.0F || dir == 0.0F)) continue;
        // Rate per step: 1/TimeUV for types 1 and 3 (dt 1), RateUV otherwise.
        const float rate = timed ? 1.0F / record.time[axis] : record.rate[axis];
        const float steps = steps_after(frames, record.interval[axis]);
        const float phase = wrap(dir * rate * steps);
        float value = phase;
        if (eased) {
            // 0x14030CA93: (cos((1 - p) * pi) + 1) * 0.5, then MinimumUV drift.
            value = (std::cos((1.0F - phase) * std::numbers::pi_v<float>) + 1.0F) * 0.5F;
            if (record.has_minimum) value += wrap(dir * record.minimum[axis] * steps);
        }
        // Stored as u16 (x 4096) and masked with 0xFFF by 0x140309570.
        const auto fixed = static_cast<std::int32_t>(value * 4096.0F) & 0xFFF;
        out[axis] = static_cast<float>(fixed) / 4096.0F;
    }
    return out;
}

std::size_t apply_uv_scrolls(Session* session, float frames) noexcept {
    if (session == nullptr) return 0U;
    auto& uv = session->render_mesh.uv0;
    std::size_t moved = 0U;
    for (const auto& binding : session->uv_scrolls) {
        const auto offset = scroll_offset(binding.record, frames);
        if (!offset || binding.vertex_begin + binding.rest_uv.size() > uv.size()) continue;
        for (std::size_t i = 0U; i < binding.rest_uv.size(); ++i) {
            // texcoord + offset (DMC3 shaders sample at uv + texOffset).
            uv[binding.vertex_begin + i] = {binding.rest_uv[i].u + (*offset)[0],
                                            binding.rest_uv[i].v + (*offset)[1]};
        }
        ++moved;
    }
    return moved;
}

}  // namespace dmcresource::motion
