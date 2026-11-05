#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace dmcresource {
struct Session;
}

// The start screen's Info, written by the core: the version and build the
// shell reports, every family the module registry reads (grouped, so a new
// module shows up without touching the shell), and how to begin.
namespace dmcresource::spider::actions {

[[nodiscard]] std::string welcome_text(std::string_view app_version, std::string_view native_build);

// A file staged next to the open session (a MOT, a physics or cloth part):
// its role as the shell's picker named it and its file name.
struct StagedFile {
    std::string role;
    std::string name;
};

// What only the shell knows when Info is rebuilt: the title it shows, how
// deep the child navigation is and where Back returns, and the staged files.
struct InfoContext {
    std::string_view title;
    std::size_t navigation_depth = 0;
    std::string_view back_title;
    std::vector<StagedFile> staged;
};

// The open session's Info, written by the core: STRUCTURE (the typed
// inspection), NOT CANONICAL notes, SESSION / EVIDENCE, NAVIGATION and
// STAGED COMPANIONS. A null session gives the empty sections.
[[nodiscard]] std::string session_info_text(const Session* session, const InfoContext& context);

}  // namespace dmcresource::spider::actions
