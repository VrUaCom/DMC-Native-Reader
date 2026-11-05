#include "dmcresource/spider/session_actions.h"

#include <cctype>
#include <span>
#include <string>

#include "dmcresource/file_health.h"
#include "dmcresource/pac_assembly.h"
#include "dmcresource/stage_room.h"

namespace dmcresource::spider::actions {
namespace {

// "st" + digits, then "effect" somewhere after them (any case).
[[nodiscard]] bool names_stage_effect_bank(std::string_view name) {
    std::string lower;
    lower.reserve(name.size());
    for (const char c : name) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.size() < 3U || lower.compare(0, 2, "st") != 0 ||
        std::isdigit(static_cast<unsigned char>(lower[2])) == 0) {
        return false;
    }
    std::size_t digits_end = 2U;
    while (digits_end < lower.size() && std::isdigit(static_cast<unsigned char>(lower[digits_end])) != 0) ++digits_end;
    return lower.find("effect", digits_end) != std::string::npos;
}

}  // namespace

OpenedResource open_resource(std::string_view name, const std::uint8_t* bytes, std::size_t size) noexcept {
    OpenedResource out;
    try {
        out.title = std::string{name};
        auto opened = open_session(name, bytes, size);
        if (!opened) {
            out.title += "\nRejected: supported route failed structural validation or format is outside "
                         "MOD / SCM / DDS / PTX / PAC / MOT.";
            return out;
        }
        out.room_effects = names_stage_effect_bank(name);

        // A PAC holding MODs opens assembled; the raw archive stays browsable
        // from the assembled session's children.
        if (auto assembled = pac_assembly::assemble_pac(*opened, nullptr, name)) {
            out.session = std::move(assembled);
            out.as = OpenedAs::AssembledPac;
            out.title += " · assembled";
            return out;
        }

        // A stage archive opens as its assembled room, not as the file gallery.
        if (stage_room::is_stage_session(*opened) && session_child_count(opened.get()) > 0U) {
            if (auto stage = stage_room::open_stage(name, bytes, size)) {
                stage->health = file_health::check(name, std::span<const std::uint8_t>{bytes, size}, true,
                                                   stage->non_canonical_notes);
                out.session = std::move(stage);
                out.as = OpenedAs::Stage;
                out.title += " · stage";
                return out;
            }
        }

        out.session = std::move(opened);
        out.as = OpenedAs::Plain;
        return out;
    } catch (...) {
        out.session.reset();
        out.as = OpenedAs::Rejected;
        out.title = std::string{name} + "\nOpen failed in the core.";
        out.room_effects = false;
        return out;
    }
}

}  // namespace dmcresource::spider::actions
