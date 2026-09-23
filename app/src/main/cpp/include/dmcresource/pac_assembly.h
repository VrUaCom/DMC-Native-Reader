#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "dmcresource/resource_session.h"

namespace dmcresource::pac_assembly {

struct AssemblyReport final {
    std::size_t models{};
    std::size_t textures_attached{};
    std::size_t textures_unpaired{};
    std::size_t motions{};
    std::size_t shadows{};
    std::size_t nested_archives{};
    std::string detail;
};

// Read-only character/scene assembly from an opened PAC Session:
//  * every MOD (including MODs inside nested PACs, depth <= 3) becomes one
//    composite part in its source/model-space coordinates;
//  * a PTX is paired with the MOD that precedes it in slot order (slot-adjacency
//    policy, reported as such; not an EXE-proven Model Set pairing);
//  * MOTs are retained in Session::motion_library for playback;
//  * the PAC's own children stay browsable from the assembled Session.
// Returns nullptr when the archive holds no MOD. Never writes to the archive.
[[nodiscard]] std::unique_ptr<Session> assemble_pac(const Session& pac,
                                                    AssemblyReport* report = nullptr) noexcept;

}  // namespace dmcresource::pac_assembly
