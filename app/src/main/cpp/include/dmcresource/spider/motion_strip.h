#pragma once

#include <string>
#include <vector>

namespace dmcresource {
struct Session;
}

// The animation strip under the view, written by the core: one card per MOT
// (the session's motion library first, then the MOTs staged beside it), the
// Script Play buttons each card really has, where a pack boundary falls, and
// the card's text. The shell draws the cards and sends the picks back.
namespace dmcresource::spider::actions {

struct ScriptButton final {
    int script_index = -1;
    int slot = -1;            // the script's PAC slot, -1 unknown
    std::string label;        // "▶\nS3"
    std::string description;  // "Play pl000_00.mot through MotionScript slot 3"
};

struct MotionCard final {
    std::string name;
    int library_index = -1;   // >= 0: Session::motion_library
    int staged_index = -1;    // >= 0: the shell's staged MOT of that order
    int pack_slot = -1;       // top-level nested PAC slot; -1 unknown, -2 staged
    bool separator_before = false;
    std::string separator_description;  // "Animation pack slot 4"
    std::string kind;         // "MOT": the extension, upper case, at most 4
    std::string stem;         // "PL000_0": the name without it, upper case, at most 7
    std::string description;  // "Play raw MOT pl000_00.mot"
    std::vector<ScriptButton> scripts;
};

// `with_library`: the session's own motions are shown (its root scene, not a
// child opened from it). `staged`: names of the MOTs staged beside it.
[[nodiscard]] std::vector<MotionCard> motion_strip(const Session* session, bool with_library,
                                                   const std::vector<std::string>& staged);

}  // namespace dmcresource::spider::actions
