#include "dmcresource/spider/motion_strip.h"

#include <cctype>
#include <limits>

#include "dmcresource/motion/motion_player.h"
#include "dmcresource/resource_session.h"

namespace dmcresource::spider::actions {
namespace {

[[nodiscard]] std::string upper(std::string_view text, std::size_t limit) {
    std::string out;
    for (const char c : text.substr(0, limit)) out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

MotionCard card_for(const std::string& name, int pack_slot) {
    MotionCard card;
    card.name = name;
    card.pack_slot = pack_slot;
    const auto dot = name.rfind('.');
    const bool has_extension = dot != std::string::npos && dot + 1U < name.size();
    card.kind = has_extension ? upper(std::string_view{name}.substr(dot + 1U), 4U) : std::string{"MOT"};
    card.stem = upper(dot == std::string::npos ? std::string_view{name} : std::string_view{name}.substr(0, dot), 7U);
    card.description = "Play raw MOT " + name;
    return card;
}

}  // namespace

std::vector<MotionCard> motion_strip(const Session* session, bool with_library,
                                     const std::vector<std::string>& staged) {
    std::vector<MotionCard> cards;
    if (session != nullptr && with_library) {
        const std::size_t scripts = motion::motion_script_count(session);
        for (std::size_t index = 0; index < session->motion_library.size(); ++index) {
            const auto& motion = session->motion_library[index];
            auto card = card_for(motion.name, motion.pack_slot);
            card.library_index = static_cast<int>(index);
            for (std::size_t script = 0; script < scripts; ++script) {
                // A controller without a real link to this MOT is absent
                // data, not a disabled action: it gets no button.
                if (!motion::motion_script_can_play_motion(session, script, index)) continue;
                const auto raw_slot = motion::motion_script_slot(session, script);
                const int slot = raw_slot > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
                                     ? -1
                                     : static_cast<int>(raw_slot);
                ScriptButton button;
                button.script_index = static_cast<int>(script);
                button.slot = slot;
                button.label = "▶\nS" + (slot >= 0 ? std::to_string(slot) : std::string{"?"});
                button.description = "Play " + motion.name + " through MotionScript " +
                                     (slot >= 0 ? "slot " + std::to_string(slot) : std::to_string(script));
                card.scripts.push_back(std::move(button));
            }
            cards.push_back(std::move(card));
        }
    }
    for (std::size_t index = 0; index < staged.size(); ++index) {
        auto card = card_for(staged[index], -2);
        card.staged_index = static_cast<int>(index);
        cards.push_back(std::move(card));
    }
    for (std::size_t index = 1; index < cards.size(); ++index) {
        if (cards[index].pack_slot == cards[index - 1U].pack_slot) continue;
        cards[index].separator_before = true;
        cards[index].separator_description = cards[index].pack_slot >= 0
                                                 ? "Animation pack slot " + std::to_string(cards[index].pack_slot)
                                                 : std::string{"Animation source boundary"};
    }
    return cards;
}

}  // namespace dmcresource::spider::actions
