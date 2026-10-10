#include "dmcresource/spider/session_actions.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <sstream>
#include <utility>

#include "dmcresource/dds_bcn_encode.h"
#include "dmcresource/spider/cpp23_language.h"
#include "dmcresource/texture_reencode.h"

namespace dmcresource::spider::actions {
namespace {

namespace crusader = dmcresource::spider::crusader;
namespace spider_cpp = dmcresource::spider::cpp23;
namespace bcn = dmcresource::dds_bcn;

constexpr crusader::OperationId kSelectSource = 1U;
constexpr crusader::OperationId kReencode = 2U;
constexpr crusader::OperationId kOpenResult = 3U;
constexpr crusader::OperationId kVerifyResult = 4U;

struct ReencodeState final {
    const Session* target{};
    bcn::Format format{};
    bool force_dx10{};
    // select
    std::shared_ptr<const std::vector<std::uint8_t>> source;
    std::string name;
    int pac_slot{-1};
    // re-encode
    texture_reencode::TextureReencodeResult encoded;
    // open / verify
    std::unique_ptr<Session> result;
    std::string detail;
};

bool select_source(ReencodeState& s, std::uint32_t) noexcept {
    if (s.target == nullptr || s.target->source_bytes == nullptr) {
        s.detail = "this resource holds no texture that can be re-encoded";
        return false;
    }
    try {
        // A PAC child re-encodes inside its PAC (Session::container_source).
        if (s.target->container_source != nullptr && s.target->container_slot >= 0) {
            s.source = s.target->container_source;
            s.name = s.target->container_name;
            s.pac_slot = s.target->container_slot;
        } else {
            s.source = s.target->source_bytes;
            s.name = s.target->source_name;
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool reencode(ReencodeState& s, std::uint32_t) noexcept {
    if (s.source == nullptr) return false;
    try {
        s.encoded = texture_reencode::reencode_textures(
            std::span<const std::byte>{reinterpret_cast<const std::byte*>(s.source->data()), s.source->size()},
            {.format = s.format, .force_dx10 = s.force_dx10, .pac_slot = s.pac_slot});
        if (!s.encoded.ok) s.detail = s.encoded.detail;
        return s.encoded.ok;
    } catch (...) {
        return false;
    }
}

bool open_result(ReencodeState& s, std::uint32_t) noexcept {
    if (!s.encoded.ok) return false;
    try {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(s.encoded.bytes.data());
        s.result = open_session(s.name, bytes, s.encoded.bytes.size());
        if (!s.result) {
            s.detail = "the re-encoded file does not open";
            return false;
        }
        s.result->source_bytes = std::make_shared<const std::vector<std::uint8_t>>(
            bytes, bytes + s.encoded.bytes.size());
        s.result->source_name = s.name;
        s.result->authored = true;
        return true;
    } catch (...) {
        s.result.reset();
        return false;
    }
}

// The result must still hold every texture, now in the requested format.
bool verify_result(ReencodeState& s, std::uint32_t) noexcept {
    if (!s.result) return false;
    try {
        // Read-only pass: the file still holds every texture and each one the
        // action touched is now in the requested format.
        const auto textures = texture_reencode::list_textures(
            std::span<const std::byte>{s.encoded.bytes.data(), s.encoded.bytes.size()});
        std::size_t matching = 0U;
        for (const auto& d : textures) matching += d.format == s.format ? 1U : 0U;
        if (matching < s.encoded.textures.size()) {
            s.detail = "the re-encoded file does not read back with the requested format";
            return false;
        }
        std::ostringstream out;
        out << s.name << ": " << s.encoded.detail;
        for (const auto& t : s.encoded.textures) {
            out << "\n";
            if (t.pac_slot >= 0) out << "slot " << t.pac_slot << " ";
            out << "tex " << t.index << "  " << t.width << "x" << t.height << " mips=" << t.mips << "  "
                << bcn::format_name(t.from) << (t.from_dx10 ? " DX10" : "") << " -> "
                << bcn::format_name(t.to) << (t.to_dx10 ? " DX10" : "") << "  " << t.old_bytes << " -> "
                << t.new_bytes << " B  PSNR " << static_cast<int>(t.psnr_db * 10.0) / 10.0 << " dB";
        }
        s.detail = out.str();
        if (!s.result->detail.empty()) s.result->detail += "\n";
        s.result->detail += "Re-encoded by Spider: " + s.detail;
        if (!s.result->trace.empty()) s.result->trace += "\n";
        s.result->trace += "[OK] spider.cpp23/crusader.action.texture-reencode";
        return true;
    } catch (...) {
        return false;
    }
}

// select source -> re-encode -> open result -> verify. Selecting reads only
// bytes the session already holds, so every step is CPU work.
const crusader::Builder& reencode_plan() {
    static const crusader::Builder plan = [] {
        crusader::Builder b;
        const auto selected = b.add(kSelectSource, 0U, crusader::Domain::cpu, {}, "select source");
        const auto encoded = b.add(kReencode, 0U, crusader::Domain::cpu, {selected}, "re-encode");
        const auto opened = b.add(kOpenResult, 0U, crusader::Domain::cpu, {encoded}, "open result");
        b.add(kVerifyResult, 0U, crusader::Domain::cpu, {opened}, "verify");
        return b;
    }();
    return plan;
}

constexpr std::array k_bindings{
    spider_cpp::bind<ReencodeState, &select_source>(kSelectSource),
    spider_cpp::bind<ReencodeState, &reencode>(kReencode),
    spider_cpp::bind<ReencodeState, &open_result>(kOpenResult),
    spider_cpp::bind<ReencodeState, &verify_result>(kVerifyResult),
};

}  // namespace

std::unique_ptr<Session> reencode_textures(const Session* target, std::string_view format_name,
                                           bool force_dx10, std::string* detail) noexcept {
    try {
        const auto format = bcn::parse_format_name(format_name);
        if (!format) {
            if (detail != nullptr) *detail = "unknown texture format";
            return nullptr;
        }
        ReencodeState state;
        state.target = target;
        state.format = *format;
        state.force_dx10 = force_dx10;
        const auto& plan = reencode_plan();
        const auto report = spider_cpp::execute(plan.build(), k_bindings, state);
        if (detail != nullptr) {
            *detail = state.detail;
            // Name the step the plan stopped at.
            const auto failed = plan.failed_label(report);
            if (!report.ok() && !failed.empty()) {
                *detail = std::string(failed) + " failed" + (state.detail.empty() ? "" : ": " + state.detail);
            }
        }
        if (!report.ok()) return nullptr;
        return std::move(state.result);
    } catch (...) {
        return nullptr;
    }
}

std::vector<TextureFormatChoice> texture_format_choices() {
    return {
        {"bc7", "BC7 (DX10): best quality, RGBA, 1 byte/pixel"},
        {"bc3", "BC3 / DXT5: retail DMC3, RGBA, 1 byte/pixel"},
        {"bc1", "BC1 / DXT1: half size, 1-bit alpha"},
        {"bc2", "BC2 / DXT3: sharp 4-bit alpha"},
        {"bc4", "BC4: one channel (grey)"},
        {"bc4s", "BC4 SNORM: one signed channel"},
        {"bc5", "BC5: two channels (R, G)"},
        {"bc5s", "BC5 SNORM: two signed channels"},
        {"bc6h", "BC6H UF16 (DX10): HDR"},
        {"bc6h_sf16", "BC6H SF16 (DX10): signed HDR"},
    };
}

}  // namespace dmcresource::spider::actions
