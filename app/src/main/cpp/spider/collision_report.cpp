#include "dmcresource/spider/collision_report.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <map>

#include "dmcresource/collision_debug.h"
#include "dmcresource/stage_room.h"
#include "dmcresource/view_renderer.h"

namespace dmcresource::spider::actions {
namespace {

[[nodiscard]] std::string format(const char* pattern, auto... values) {
    char text[256];
    std::snprintf(text, sizeof text, pattern, values...);
    return text;
}

[[nodiscard]] std::string plural(std::size_t count, const char* one, const char* many) {
    return std::to_string(count) + " " + (count == 1U ? one : many);
}

// The stage or room whose HITS the session shows, and what it is called.
struct Shown final {
    std::shared_ptr<const stage_room::Room> room;  // keeps a room's sources alive
    std::vector<const environment_collision::Source*> sources;  // the first is drawn
    std::string where;
};

[[nodiscard]] Shown shown(const Session* session) {
    Shown out;
    if (session == nullptr) return out;
    if (session->hits != nullptr) {
        out.sources.push_back(session->hits.get());
        out.where = "this .hits file";
        return out;
    }
    out.room = session->stage != nullptr ? session->stage : stage_room::current();
    if (out.room == nullptr) return out;
    out.where = session->stage != nullptr ? (out.room->name.empty() ? std::string{"this stage"} : out.room->name)
                                          : "the room " + out.room->name;
    for (const auto& source : out.room->collision_sources) out.sources.push_back(&source);
    return out;
}

}  // namespace

std::vector<environment_collision::Kind> shown_hits_kinds(const Session* session) {
    const auto s = shown(session);
    return s.sources.empty() ? std::vector<environment_collision::Kind>{} : environment_collision::kinds(*s.sources.front());
}

CollisionReport stage_collision_report(const Session* session) {
    CollisionReport report;
    report.title = "Stage collision (HITS)";
    const auto s = shown(session);
    if (s.sources.empty()) {
        report.summary = "No stage collision here: open a stage, a .hits file, or show a room around the model.";
        return report;
    }
    const auto& drawn = *s.sources.front();
    const auto kinds = environment_collision::kinds(drawn);
    std::size_t floors = 0U, walls = 0U, ceilings = 0U;
    for (const auto& k : kinds) {
        floors += k.floors;
        walls += k.walls;
        ceilings += k.ceilings;
    }
    report.summary = "In " + s.where + ": " + plural(s.sources.size(), "HITS source", "HITS sources") + "; the first is drawn, " +
                     plural(drawn.triangles.size(), "record", "records") + " (" + plural(floors, "floor", "floors") +
                     ", " + plural(walls, "wall", "walls") + ", " + plural(ceilings, "ceiling", "ceilings") + ") in " +
                     plural(kinds.size(), "kind", "kinds") +
                     ". Each kind (one value of the record flags) has its own colour. Characters stand on floors "
                     "(normal pointing up, y >= 0.7), stop at walls, and shots hit both.";

    // The sources.
    ReportGroup sources;
    sources.heading = "Sources";
    for (std::size_t i = 0; i < s.sources.size(); ++i) {
        const auto& src = *s.sources[i];
        std::string line = format("%zu. %s", i + 1U, src.resource_name.c_str());
        if (src.resource_slot != std::numeric_limits<std::uint32_t>::max()) line += format(" (slot %u)", src.resource_slot);
        line += ": " + plural(src.triangles.size(), "record", "records") +
                format(", grid %u x %u x %u, from %.0f %.0f %.0f to %.0f %.0f %.0f", src.grid_count_x, src.grid_count_y,
                       src.grid_count_z, src.bounds_min.x, src.bounds_min.y, src.bounds_min.z, src.bounds_max.x,
                       src.bounds_max.y, src.bounds_max.z);
        line += i == 0U ? " \xE2\x80\x94 drawn" : " \xE2\x80\x94 not drawn (the view shows the first)";
        sources.lines.push_back(std::move(line));
    }
    report.groups.push_back(std::move(sources));

    // One group per kind, in its drawing colour.
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        const auto& k = kinds[i];
        ReportGroup group;
        group.coloured = true;
        group.colour = collision_kind_color(i);
        group.heading = format("Kind %zu \xC2\xB7 flags 0x%08X \xC2\xB7 ", i + 1U, k.flags) +
                        plural(k.count, "record", "records");
        group.lines.push_back(plural(k.floors, "floor", "floors") + ", " + plural(k.walls, "wall", "walls") + ", " +
                              plural(k.ceilings, "ceiling", "ceilings"));
        const unsigned type_bits = k.flags & 0xFFU;
        const unsigned categories = k.flags >> 16U;
        group.lines.push_back(format("Type bits (low byte): 0x%02X", type_bits));
        if (categories == 0U) {
            group.lines.push_back("Categories (flags >> 16): none \xE2\x80\x94 every collision test sees these records");
        } else {
            std::string bits;
            for (unsigned bit = 1U; bit <= 0x8000U; bit <<= 1U) {
                if ((categories & bit) != 0U) bits += format(bits.empty() ? "0x%X" : " + 0x%X", bit);
            }
            group.lines.push_back(format("Categories (flags >> 16): 0x%04X = ", categories) + bits);
            // The skip masks the EXE is known to pass (0x14005E880's callers).
            std::string skipped;
            if ((categories & 0x10U) != 0U) skipped += "Lady's line of sight (mask 0x10)";
            for (const unsigned mask : {0x02U, 0x20U, 0x40U}) {
                if ((categories & mask) == 0U) continue;
                if (!skipped.empty()) skipped += ", ";
                skipped += format("objects of type mask 0x%02X", mask);
            }
            group.lines.push_back(skipped.empty() ? std::string{"No known test skips them"}
                                                  : "Skipped by: " + skipped + "; every other test sees them");
        }
        report.groups.push_back(std::move(group));
    }
    return report;
}

CollisionReport attack_collision_report(const Session* session) {
    CollisionReport report;
    report.title = "Attack collision (hitboxes)";
    const auto* binding = session != nullptr ? session->collision.get() : nullptr;
    if (binding == nullptr) {
        report.summary = "No attack collision: it comes with a character's PAC (its index and shape tables).";
        return report;
    }
    const auto used = collision::collision_attack_ids(*session);
    std::map<std::uint8_t, std::size_t> by_type;
    for (const auto& shape : binding->shapes) ++by_type[shape.type];
    const auto type_name = [](std::uint8_t type) -> std::string {
        switch (type) {
        case 2U: return "sphere";
        case 3U: return "box";
        case 4U: return "capsule";
        default: return "type " + std::to_string(type);
        }
    };
    report.summary = (binding->name.empty() ? std::string{"This model"} : binding->name) + ": " +
                     plural(used.size(), "attack", "attacks") + " used of " + std::to_string(binding->attacks.size()) +
                     " entries, " + plural(binding->shapes.size(), "shape", "shapes") +
                     ". Each attack places one shape on one bone; all are drawn in one colour. Tap the tool: all "
                     "attacks, then each in turn, then off. Now shown: " +
                     collision::describe_collision_selection(*session) + ".";

    ReportGroup shapes;
    shapes.heading = "Shapes";
    shapes.coloured = true;
    shapes.colour = kAttackCollisionColor;
    for (const auto& [type, count] : by_type) {
        float smallest = std::numeric_limits<float>::max(), largest = 0.0F;
        for (const auto& shape : binding->shapes) {
            if (shape.type != type) continue;
            const float size = type == 3U ? std::max({shape.size[0], shape.size[1], shape.size[2]}) : shape.radius;
            smallest = std::min(smallest, size);
            largest = std::max(largest, size);
        }
        shapes.lines.push_back(plural(count, type_name(type).c_str(), (type_name(type) + "s").c_str()) +
                               (largest > 0.0F ? format(", %s %.1f .. %.1f", type == 3U ? "half size" : "radius",
                                                        smallest, largest)
                                               : std::string{}));
    }
    report.groups.push_back(std::move(shapes));

    // The attacks, by the target layers they hit (mask bits 1 / 2 / 4).
    std::map<std::uint8_t, std::vector<int>> by_mask;
    for (const int id : used) {
        if (static_cast<std::size_t>(id) < binding->attacks.size()) by_mask[binding->attacks[id].mask].push_back(id);
    }
    for (const auto& [mask, ids] : by_mask) {
        ReportGroup group;
        group.coloured = true;
        group.colour = kAttackCollisionColor;
        std::string layers;
        for (const unsigned bit : {1U, 2U, 4U}) {
            if ((mask & bit) == 0U) continue;
            if (!layers.empty()) layers += " + ";
            layers += std::to_string(bit);
        }
        group.heading = "Target layers " + (layers.empty() ? std::string{"none"} : layers) + " \xC2\xB7 " +
                        plural(ids.size(), "attack", "attacks");
        for (const int id : ids) {
            const auto& e = binding->attacks[static_cast<std::size_t>(id)];
            std::string line = format("attack %d: bone %u, ", id, e.bone);
            if (e.shape < binding->shapes.size()) {
                const auto& shape = binding->shapes[e.shape];
                line += type_name(shape.type) + format(" #%u", e.shape);
                line += shape.type == 3U ? format(", half size %.1f %.1f %.1f", shape.size[0], shape.size[1], shape.size[2])
                                         : format(", radius %.1f", shape.radius);
            }
            group.lines.push_back(std::move(line));
        }
        report.groups.push_back(std::move(group));
    }
    return report;
}

}  // namespace dmcresource::spider::actions
