#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "dmcresource/environment_collision.h"
#include "dmcresource/resource_session.h"

// Our own account of a session's collision, for the long press on its tool:
// not the file's structure but what the viewer shows and how much of it,
// grouped, each group in the colour it is drawn in. The core writes every
// line; the shell lays the groups out (a colour swatch and the heading in
// that colour, the lines under it).
namespace dmcresource::spider::actions {

struct ReportGroup final {
    std::string heading;
    bool coloured{};
    std::array<std::uint8_t, 3> colour{};
    std::vector<std::string> lines;
};

struct CollisionReport final {
    std::string title;
    std::string summary;
    std::vector<ReportGroup> groups;
};

// The kinds (one per distinct record flags value, in drawing-colour order)
// of the HITS the session shows: its own .hits file, its stage scene's, or
// the room's around it (the first source; none without one).
[[nodiscard]] std::vector<environment_collision::Kind> shown_hits_kinds(const Session* session);

// Stage collision (HITS): the sources, then one group per record kind in its
// colour: records, floors / walls / ceilings, the flag bits.
[[nodiscard]] CollisionReport stage_collision_report(const Session* session);
// Attack collision (hitboxes): the shapes by type, then the attacks used.
[[nodiscard]] CollisionReport attack_collision_report(const Session* session);

}  // namespace dmcresource::spider::actions
