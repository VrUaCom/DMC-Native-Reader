#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "dmcresource/resource_session.h"

namespace dmcresource::pac_assembly {

struct AssemblyReport final {
    std::size_t models{};
    std::size_t textures_attached{};
    std::size_t textures_unpaired{};
    std::size_t motions{};
    std::size_t shadows{};
    std::size_t shadows_bound{};
    std::size_t nested_archives{};
    std::size_t attached_parts{};
    std::size_t effect_models_skipped{};
    std::string detail_attachments;
    std::string detail;
};

// Read-only character/scene assembly from an opened PAC Session:
//  * every MOD (including MODs inside nested PACs, depth <= 3) becomes one
//    composite part;
//  * each MOD takes the nearest PTX before it in the same container (else the
//    nearest after it). For player PACs this is slot 0 for both the body
//    (slot 1) and the coat (slot 12), exactly as IPlayer loads them;
//  * player layout (top-level slot 1 and slot 12 MODs, `archive_name` starting
//    with "pl"): the slot-12 coat skeleton hangs from body joint 3 with an
//    identity root local, as CPlVergil/CPlDante/CPlNewVergil do every frame;
//  * MOTs are retained in Session::motion_library for playback;
//  * the PAC's own children stay browsable from the assembled Session.
// Returns nullptr when the archive holds no MOD. Never writes to the archive.
[[nodiscard]] std::unique_ptr<Session> assemble_pac(const Session& pac,
                                                    AssemblyReport* report = nullptr,
                                                    std::string_view archive_name = {}) noexcept;

// Several archives in one scene: archives[0] is the character, the rest are
// added to it. An added archive named like a catalogued weapon PAC
// (plwp_sword.pac = Rebellion, ...) hangs every MOD it holds from the body at
// the weapon's state-0 joint with its recorded offset (0x1401FD8F0); other
// archives keep their source coordinates. MOTs of every archive are listed.
[[nodiscard]] std::unique_ptr<Session> assemble_archives(
    std::span<const Session* const> archives,
    std::span<const std::string_view> archive_names,
    AssemblyReport* report = nullptr) noexcept;

}  // namespace dmcresource::pac_assembly
