#include "dmcresource/motion/object_visibility.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>

#include "dmcresource/resource_session.h"

namespace dmcresource::motion {
namespace {

std::string stem_lower(std::string_view name) {
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string_view::npos) name.remove_prefix(slash + 1U);
    const auto dot = name.find('.');
    if (dot != std::string_view::npos) name = name.substr(0, dot);
    std::string out;
    for (const char c : name) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

void show(ObjectVisibility& v, std::uint32_t object, bool drawn) {
    if (object < v.hidden.size()) v.hidden[object] = !drawn;
}

// The draw bits the class sets for the current state (see the header).
void apply_rule(ObjectVisibility& v) {
    if (v.rule == VisibilityRule::dante) {
        show(v, 0U, v.hand_variant == 0);
        show(v, 1U, v.hand_variant == 1);
        show(v, 3U, !v.beowulf);
        show(v, 4U, v.beowulf);
        show(v, 5U, v.beowulf);
        show(v, 6U, !v.beowulf);
    } else if (v.rule == VisibilityRule::new_vergil) {
        const bool fist = v.beowulf || v.hand_variant == 1;
        show(v, 2U, !fist);
        show(v, 4U, fist);
        show(v, 7U, v.beowulf);
        show(v, 8U, !v.beowulf);
        show(v, 9U, v.beowulf);
        show(v, 10U, !v.beowulf);
    }
}

void rebuild(Session& session, const ObjectVisibility& v) {
    const auto hidden_vertex = [&v](std::uint32_t vertex) {
        for (std::size_t object = 0U; object < v.hidden.size(); ++object) {
            if (!v.hidden[object]) continue;
            const auto [lo, hi] = v.object_ranges[object];
            if (vertex >= lo && vertex < hi) return true;
        }
        return false;
    };
    const bool slotted = v.all_slots.size() * 3U == v.all_indices.size();
    std::vector<std::uint32_t> indices;
    std::vector<std::uint32_t> slots;
    indices.reserve(v.all_indices.size());
    if (slotted) slots.reserve(v.all_slots.size());
    for (std::size_t t = 0U; t + 2U < v.all_indices.size(); t += 3U) {
        if (hidden_vertex(v.all_indices[t]) || hidden_vertex(v.all_indices[t + 1U]) ||
            hidden_vertex(v.all_indices[t + 2U])) {
            continue;
        }
        indices.insert(indices.end(), v.all_indices.begin() + static_cast<std::ptrdiff_t>(t),
                       v.all_indices.begin() + static_cast<std::ptrdiff_t>(t + 3U));
        if (slotted) slots.push_back(v.all_slots[t / 3U]);
    }
    session.render_mesh.indices = std::move(indices);
    if (slotted) session.render_triangle_texture_slots = std::move(slots);
}

void update(Session* session, const std::function<void(ObjectVisibility&)>& change) {
    if (session == nullptr || !session->object_visibility) return;
    auto& v = *session->object_visibility;
    const auto before = v.hidden;
    change(v);
    apply_rule(v);
    if (v.hidden != before) rebuild(*session, v);
}

}  // namespace

VisibilityRule visibility_rule_for(std::string_view archive_name) noexcept {
    try {
        const auto stem = stem_lower(archive_name);
        for (const char* dante : {"pl000", "pl011", "pl013", "pl015", "pl016", "pl018"}) {
            if (stem == dante) return VisibilityRule::dante;
        }
        for (const char* vergil : {"pl021", "pl023", "pl026"}) {
            if (stem == vergil) return VisibilityRule::new_vergil;
        }
    } catch (...) {
    }
    return VisibilityRule::none;
}

bool install_object_visibility(Session* session, std::size_t body_part, VisibilityRule rule) {
    if (session == nullptr || rule == VisibilityRule::none || body_part >= session->composite_parts.size()) {
        return false;
    }
    std::size_t begin = 0U;
    for (std::size_t part = 0U; part < body_part; ++part) {
        for (const auto& primitive : session->composite_parts[part].scene.meshes) begin += primitive.mesh.vertices.size();
    }
    auto v = std::make_shared<ObjectVisibility>();
    v->rule = rule;
    std::size_t cursor = begin;
    for (const auto& primitive : session->composite_parts[body_part].scene.meshes) {
        const auto count = primitive.mesh.vertices.size();
        const auto object = primitive.object_index;
        if (object >= v->object_ranges.size()) v->object_ranges.resize(object + 1U, {0U, 0U});
        auto& range = v->object_ranges[object];
        if (range.first == range.second) {
            range = {static_cast<std::uint32_t>(cursor), static_cast<std::uint32_t>(cursor + count)};
        } else {
            range.second = static_cast<std::uint32_t>(cursor + count);  // a further mesh of the object
        }
        cursor += count;
    }
    // The rule's objects must exist with geometry (a costume without them keeps all).
    const std::uint32_t needed = rule == VisibilityRule::dante ? 7U : 11U;
    if (v->object_ranges.size() < needed) return false;
    v->hidden.assign(v->object_ranges.size(), false);
    v->all_indices = session->render_mesh.indices;
    v->all_slots = session->render_triangle_texture_slots;
    apply_rule(*v);
    session->object_visibility = v;
    rebuild(*session, *v);
    return true;
}

int hand_variant_at(std::span<const ScriptSignalKey> keys, float frame) noexcept {
    int variant = 0;
    for (const auto& key : keys) {
        if (!(frame > key.after_frame)) continue;
        const auto b3 = key.channels[2];
        if ((b3 & 0x40U) == 0U) continue;
        const auto value = b3 & 0x3FU;
        if (value <= 1U) variant = static_cast<int>(value);
    }
    return variant;
}

void set_hand_variant(Session* session, int variant) {
    update(session, [variant](ObjectVisibility& v) { v.hand_variant = variant; });
}

void set_beowulf(Session* session, bool equipped) {
    update(session, [equipped](ObjectVisibility& v) { v.beowulf = equipped; });
}

std::vector<std::uint32_t> hidden_objects(const Session* session) {
    std::vector<std::uint32_t> out;
    if (session == nullptr || !session->object_visibility) return out;
    const auto& hidden = session->object_visibility->hidden;
    for (std::uint32_t i = 0U; i < hidden.size(); ++i) {
        if (hidden[i]) out.push_back(i);
    }
    return out;
}

}  // namespace dmcresource::motion
