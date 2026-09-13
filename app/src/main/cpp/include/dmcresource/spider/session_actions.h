#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "dmcresource/resource_session.h"

namespace dmcresource::spider::actions {

// Product action boundary. JNI/platform shells call these operations instead of
// directly orchestrating session composition or texture attachment. Crusader
// owns execution; resource_session remains the data/model implementation layer.
[[nodiscard]] std::unique_ptr<Session> compose_mod_sessions(
    const std::vector<const Session*>& parts,
    const std::vector<std::string>& names) noexcept;

[[nodiscard]] bool attach_ptx(
    Session* session,
    std::string_view name,
    const std::uint8_t* bytes,
    std::size_t size) noexcept;

[[nodiscard]] bool attach_ptx_to_part(
    Session* session,
    int part_index,
    std::string_view name,
    const std::uint8_t* bytes,
    std::size_t size) noexcept;

}  // namespace dmcresource::spider::actions
