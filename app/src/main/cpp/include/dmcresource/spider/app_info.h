#pragma once

#include <string>
#include <string_view>

// The start screen's Info, written by the core: the version and build the
// shell reports, every family the module registry reads (grouped, so a new
// module shows up without touching the shell), and how to begin.
namespace dmcresource::spider::actions {

[[nodiscard]] std::string welcome_text(std::string_view app_version, std::string_view native_build);

}  // namespace dmcresource::spider::actions
