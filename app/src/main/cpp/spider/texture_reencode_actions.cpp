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
    const Session* container{};
    int child_index{-1};
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

// A PAC child is identified by its container slot ("slot-N").
[[nodiscard]] int container_slot(const Session& container, int child_index) noexcept {
    if (child_index < 0 || static_cast<std::size_t>(child_index) >= container.children.size()) return -1;
    const auto& id = container.children[static_cast<std::size_t>(child_index)].id;
    constexpr std::string_view prefix = "slot-";
    if (id.rfind(prefix, 0) != 0) return -1;
    int slot = -1;
    const auto* begin = id.data() + prefix.size();
    const auto [ptr, ec] = std::from_chars(begin, id.data() + id.size(), slot);
    return ec == std::errc{} && ptr == id.data() + id.size() ? slot : -1;
}

bool select_source(void* raw, std::uint32_t) noexcept {
    auto* s = static_cast<ReencodeState*>(raw);
    if (s == nullptr || s->target == nullptr || s->target->source_bytes == nullptr) {
        if (s != nullptr) s->detail = "this resource holds no texture that can be re-encoded";
        return false;
    }
    try {
        if (s->container != nullptr && s->container->source_bytes != nullptr) {
            const auto slot = container_slot(*s->container, s->child_index);
            if (slot >= 0) {
                s->source = s->container->source_bytes;
                s->name = s->container->source_name;
                s->pac_slot = slot;
                return true;
            }
        }
        s->source = s->target->source_bytes;
        s->name = s->target->source_name;
        return true;
    } catch (...) {
        return false;
    }
}

bool reencode(void* raw, std::uint32_t) noexcept {
    auto* s = static_cast<ReencodeState*>(raw);
    if (s == nullptr || s->source == nullptr) return false;
    try {
        s->encoded = texture_reencode::reencode_textures(
            std::span<const std::byte>{reinterpret_cast<const std::byte*>(s->source->data()), s->source->size()},
            {.format = s->format, .force_dx10 = s->force_dx10, .pac_slot = s->pac_slot});
        if (!s->encoded.ok) s->detail = s->encoded.detail;
        return s->encoded.ok;
    } catch (...) {
        return false;
    }
}

bool open_result(void* raw, std::uint32_t) noexcept {
    auto* s = static_cast<ReencodeState*>(raw);
    if (s == nullptr || !s->encoded.ok) return false;
    try {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(s->encoded.bytes.data());
        s->result = open_session(s->name, bytes, s->encoded.bytes.size());
        if (!s->result) {
            s->detail = "the re-encoded file does not open";
            return false;
        }
        s->result->source_bytes = std::make_shared<const std::vector<std::uint8_t>>(
            bytes, bytes + s->encoded.bytes.size());
        s->result->source_name = s->name;
        s->result->authored = true;
        return true;
    } catch (...) {
        s->result.reset();
        return false;
    }
}

// The result must still hold every texture, now in the requested format.
bool verify_result(void* raw, std::uint32_t) noexcept {
    auto* s = static_cast<ReencodeState*>(raw);
    if (s == nullptr || !s->result) return false;
    try {
        // Read-only pass: the file still holds every texture and each one the
        // action touched is now in the requested format.
        const auto textures = texture_reencode::list_textures(
            std::span<const std::byte>{s->encoded.bytes.data(), s->encoded.bytes.size()});
        std::size_t matching = 0U;
        for (const auto& d : textures) matching += d.format == s->format ? 1U : 0U;
        if (matching < s->encoded.textures.size()) {
            s->detail = "the re-encoded file does not read back with the requested format";
            return false;
        }
        std::ostringstream out;
        out << s->name << ": " << s->encoded.detail;
        for (const auto& t : s->encoded.textures) {
            out << "\n";
            if (t.pac_slot >= 0) out << "slot " << t.pac_slot << " ";
            out << "tex " << t.index << "  " << t.width << "x" << t.height << " mips=" << t.mips << "  "
                << bcn::format_name(t.from) << (t.from_dx10 ? " DX10" : "") << " -> "
                << bcn::format_name(t.to) << (t.to_dx10 ? " DX10" : "") << "  " << t.old_bytes << " -> "
                << t.new_bytes << " B  PSNR " << static_cast<int>(t.psnr_db * 10.0) / 10.0 << " dB";
        }
        s->detail = out.str();
        if (!s->result->detail.empty()) s->result->detail += "\n";
        s->result->detail += "Re-encoded by Spider: " + s->detail;
        if (!s->result->trace.empty()) s->result->trace += "\n";
        s->result->trace += "[OK] spider.cpp23/crusader.action.texture-reencode";
        return true;
    } catch (...) {
        return false;
    }
}

const crusader::Plan& reencode_plan() {
    static const crusader::Plan plan = [] {
        crusader::Plan out;
        out.dependencies = {0U, 1U, 2U};
        out.instructions = {
            {.operation = kSelectSource, .operand = 0U, .dependency_begin = 0U, .dependency_count = 0U,
             .domain = crusader::Domain::io},
            {.operation = kReencode, .operand = 0U, .dependency_begin = 0U, .dependency_count = 1U,
             .domain = crusader::Domain::cpu},
            {.operation = kOpenResult, .operand = 0U, .dependency_begin = 1U, .dependency_count = 1U,
             .domain = crusader::Domain::cpu},
            {.operation = kVerifyResult, .operand = 0U, .dependency_begin = 2U, .dependency_count = 1U,
             .domain = crusader::Domain::cpu},
        };
        return out;
    }();
    return plan;
}

}  // namespace

std::unique_ptr<Session> reencode_textures(const Session* target, const Session* container, int child_index,
                                           std::string_view format_name, bool force_dx10,
                                           std::string* detail) noexcept {
    try {
        const auto format = bcn::parse_format_name(format_name);
        if (!format) {
            if (detail != nullptr) *detail = "unknown texture format";
            return nullptr;
        }
        ReencodeState state;
        state.target = target;
        state.container = container;
        state.child_index = child_index;
        state.format = *format;
        state.force_dx10 = force_dx10;
        static const std::array bindings{
            crusader::OperationBinding{.operation = kSelectSource, .execute = &select_source},
            crusader::OperationBinding{.operation = kReencode, .execute = &reencode},
            crusader::OperationBinding{.operation = kOpenResult, .execute = &open_result},
            crusader::OperationBinding{.operation = kVerifyResult, .execute = &verify_result},
        };
        const auto report = spider_cpp::execute(reencode_plan(), bindings, state);
        if (detail != nullptr) *detail = state.detail;
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
