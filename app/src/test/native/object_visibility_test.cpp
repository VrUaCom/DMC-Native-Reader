// Which body objects a player class draws (motion/object_visibility.h): the
// rules of CPlDante / CPlNewVergil on a synthetic body of one triangle per
// object, and the hand switch read from the script's opcode-3 b3.
#include "dmcresource/motion/object_visibility.h"
#include "dmcresource/motion/part_attachment.h"
#include "dmcresource/resource_session.h"

#include <cassert>
#include <set>

using namespace dmcresource;
namespace m = dmcresource::motion;

namespace {

// A session whose part 1 is a body of `objects` objects, one triangle each;
// part 0 is another model of one triangle in front of it.
Session body(std::uint32_t objects) {
    Session s;
    s.composite_parts.resize(2U);
    MeshPrimitive other;
    other.mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    s.composite_parts[0].scene.meshes.push_back(other);
    s.render_mesh.vertices = other.mesh.vertices;
    s.render_mesh.indices = {0, 1, 2};
    s.render_triangle_texture_slots = {9};
    for (std::uint32_t o = 0; o < objects; ++o) {
        MeshPrimitive p;
        p.object_index = o;
        p.mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        s.composite_parts[1].scene.meshes.push_back(p);
        const auto base = static_cast<std::uint32_t>(s.render_mesh.vertices.size());
        s.render_mesh.vertices.insert(s.render_mesh.vertices.end(), p.mesh.vertices.begin(), p.mesh.vertices.end());
        s.render_mesh.indices.insert(s.render_mesh.indices.end(), {base, base + 1, base + 2});
        s.render_triangle_texture_slots.push_back(o);
    }
    return s;
}

// The objects whose triangle is still drawn (by the texture slot each carries).
std::set<std::uint32_t> drawn(const Session& s) {
    std::set<std::uint32_t> out;
    assert(s.render_triangle_texture_slots.size() * 3U == s.render_mesh.indices.size());
    for (const auto slot : s.render_triangle_texture_slots) {
        if (slot != 9U) out.insert(slot);
    }
    return out;
}

}  // namespace

int main() {
    assert(m::visibility_rule_for("C:/data/PL000.pac") == m::VisibilityRule::dante);
    assert(m::visibility_rule_for("pl018.pac") == m::VisibilityRule::dante);
    assert(m::visibility_rule_for("pl021.pac") == m::VisibilityRule::new_vergil);
    assert(m::visibility_rule_for("pl001.pac") == m::VisibilityRule::none);
    assert(m::visibility_rule_for("em034.pac") == m::VisibilityRule::none);

    // Dante: hand 0, no Beowulf -> objects 1, 4, 5 hidden; the other model untouched.
    auto dante = body(20U);
    assert(m::install_object_visibility(&dante, 1U, m::VisibilityRule::dante));
    auto shown = drawn(dante);
    assert(!shown.count(1) && !shown.count(4) && !shown.count(5));
    assert(shown.count(0) && shown.count(3) && shown.count(6) && shown.count(19));
    assert(dante.render_triangle_texture_slots.front() == 9U);
    assert((m::hidden_objects(&dante) == std::vector<std::uint32_t>{1, 4, 5}));
    // The script's b3 0x41 switches to hand 1; Beowulf swaps forearms and shins.
    m::set_hand_variant(&dante, 1);
    shown = drawn(dante);
    assert(shown.count(1) && !shown.count(0));
    m::set_beowulf(&dante, true);
    shown = drawn(dante);
    assert(shown.count(4) && shown.count(5) && !shown.count(3) && !shown.count(6));
    m::set_beowulf(&dante, false);
    m::set_hand_variant(&dante, 0);
    assert((m::hidden_objects(&dante) == std::vector<std::uint32_t>{1, 4, 5}));

    // NewVergil: hands 2 / 4, Beowulf parts 7 / 9 against 8 / 10.
    auto vergil = body(14U);
    assert(m::install_object_visibility(&vergil, 1U, m::VisibilityRule::new_vergil));
    assert((m::hidden_objects(&vergil) == std::vector<std::uint32_t>{4, 7, 9}));
    m::set_beowulf(&vergil, true);
    assert((m::hidden_objects(&vergil) == std::vector<std::uint32_t>{2, 8, 10}));

    // A body with too few objects keeps everything.
    auto small = body(4U);
    assert(!m::install_object_visibility(&small, 1U, m::VisibilityRule::dante) && !small.object_visibility);

    // The hand at a frame: b3 with bit 0x40 and value 0 / 1, the last one passed.
    std::vector<m::ScriptSignalKey> keys;
    assert(m::hand_variant_at(keys, 10.0F) == 0);
    keys.push_back({-1.0F, {0, 0, 0x41, 0, 0}});
    assert(m::hand_variant_at(keys, 0.0F) == 1);
    keys.push_back({20.0F, {0, 0, 0x40, 0, 0}});
    assert(m::hand_variant_at(keys, 20.0F) == 1 && m::hand_variant_at(keys, 21.0F) == 0);
    keys.push_back({30.0F, {0, 0, 0x45, 0, 0}});   // other values leave the hand as it is
    keys.push_back({40.0F, {0, 0, 0x21, 0, 0}});   // bit 0x20 is a rumble, not a hand
    assert(m::hand_variant_at(keys, 50.0F) == 0);

    // Beowulf's model: plwp_fight.pac, ten nodes on body joints (0x1404E0BD0).
    assert(m::beowulf_archive("obj/PLWP_FIGHT.pac") && !m::beowulf_archive("plwp_fight2.pac"));
    assert(m::kBeowulfNodeJoints.front() == (std::pair<std::uint32_t, std::uint32_t>{1U, 8U}));
    assert(m::kBeowulfNodeJoints.back() == (std::pair<std::uint32_t, std::uint32_t>{10U, 22U}));

    // Jester: his look (body, hat on joint 5, cane on joint 9) apart from the ball and shells.
    const auto jester = m::archive_variants("EM037.pac");
    assert(jester.size() == 3U && jester[0].include_top_level_mod_count == 3U);
    assert(jester[0].include_top_level_mod_slots[2] == 15U && jester[0].part_attachment_count == 2U);
    assert(jester[0].part_attachments[0].host_joint == 5U && jester[0].part_attachments[1].host_joint == 9U);
    assert(jester[0].part_attachments[1].zyx_offset && jester[1].include_top_level_mod_slots[0] == 17U);
    return 0;
}
