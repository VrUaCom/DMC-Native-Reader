#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "dmcresource/decode_pipeline.h"
#include "dmcresource/dmc_resource.h"
#include "dmcresource/inspection_format.h"
#include "dmcresource/native_module.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/spider/black_widow.h"
#include "dmcresource/spider/stage_actions.h"
#include "dmcresource/spider/companion_menu.h"
#include "dmcresource/stage_room.h"
#include "dmcresource/view_renderer.h"
#include "dmcresource/stage_config.h"
#include "dmcresource/stage_markers.h"

using namespace dmcresource;

namespace {

struct Writer {
    std::vector<std::uint8_t> bytes;
    void tag(std::string_view text) { for (char c : text) bytes.push_back(static_cast<std::uint8_t>(c)); bytes.push_back(0); }
    void u16(std::uint16_t v) { put(&v, 2); }
    void u32(std::uint32_t v) { put(&v, 4); }
    void f32(float v) { put(&v, 4); }
    void zeros(std::size_t n) { bytes.insert(bytes.end(), n, 0U); }
    void pad_to(std::size_t n) { if (bytes.size() < n) zeros(n - bytes.size()); }
    void put(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        bytes.insert(bytes.end(), b, b + n);
    }
    void header(std::string_view t, std::uint16_t version, std::uint16_t count) {
        tag(t); u16(version); u16(count); zeros(8);
    }
    void corner(float x, float y, float z) { f32(x); f32(y); f32(z); f32(1.0F); }
};

PipelineResult run(std::string_view name, const std::vector<std::uint8_t>& bytes) {
    return run_decode_pipeline(name, bytes.data(), bytes.size());
}

void expect(std::string_view name, const std::vector<std::uint8_t>& bytes, Format format, std::string_view family,
            std::string_view in_info) {
    const auto probe = dmcresource::probe("renamed.bin", bytes.data(), bytes.size());
    assert(probe.format == format && probe.content_confirmed);
    assert(std::string_view{format_name(format)} == family);
    const auto* module = NativeModuleRegistry::find(family);
    assert(module != nullptr && module->format == format && !module->renderable);
    assert(has_capability(module->capabilities, ResourceCapability::ImagePreview));
    const auto result = run(name, bytes);
    assert(result.accepted);
    assert(result.image_preview.available());
    const auto info = format_inspection_tree(result.inspection);
    assert(info.find(in_info) != std::string::npos);
}

}  // namespace

namespace {
dmcresource::spider::actions::ShellFacts no_room_facts() {
    return {.root_scene = true, .assembled_pac_source = false, .stage_source = false, .room_loaded = false,
            .room_shown = false};
}
}  // namespace

int main() {
    // POS: two spawn points declared, room for three; the spare is zero.
    Writer pos;
    pos.header("POS", 1, 2);
    for (int i = 0; i < 2; ++i) {
        pos.u32(0x10U + i); pos.f32(100.0F * i); pos.f32(0.0F); pos.f32(-50.0F * i); pos.f32(90.0F * i); pos.zeros(28);
    }
    pos.zeros(48);
    expect("st100_pos.pos", pos.bytes, Format::StagePos, "POS", "Spawn point 1");
    {
        const auto markers = stage_markers::read_markers(stage_config::Kind::Pos, pos.bytes);
        assert(markers.size() == 2U && markers[1].shape == stage_markers::Shape::Point);
        assert(markers[1].points[0].x == 100.0F && markers[1].heading_degrees == 90.0F);
        assert(markers[1].description().find("0x40") != std::string::npos);  // record offset 16 + 48
    }

    // EVE: one zone, 128 bytes.
    Writer eve;
    eve.header("EVE", 1, 1);
    eve.u32(0); eve.u32(5); eve.u32(7); eve.zeros(20);
    eve.corner(0, 0, 0); eve.corner(10, 0, 0); eve.corner(10, 0, 10); eve.corner(0, 0, 10);
    eve.f32(25.0F); eve.zeros(28);
    assert(eve.bytes.size() == 0x10U + 0x80U);
    expect("st100_eve.eve", eve.bytes, Format::StageEve, "EVE", "Kind: 5");
    {
        const auto markers = stage_markers::read_markers(stage_config::Kind::Eve, eve.bytes);
        assert(markers.size() == 1U && markers[0].shape == stage_markers::Shape::Area && markers[0].height == 25.0F);
    }

    // CAM: one 0x400 record of two zones, then a path found by shape.
    Writer cam;
    cam.header("CAM", 1, 2);
    const auto record_at = cam.bytes.size();
    cam.u32(0x400U); cam.zeros(0x10); cam.u32(2); cam.pad_to(record_at + 0x40U);
    for (int z = 0; z < 2; ++z) {
        const float o = 20.0F * z;
        cam.corner(o, 5, 0); cam.corner(o + 10, 5, 0); cam.corner(o + 10, 5, 10); cam.corner(o, 5, 10);
        cam.f32(30.0F); cam.zeros(12);
    }
    for (int p = 0; p < 3; ++p) { cam.f32(100.0F + p); cam.f32(7.5F); cam.f32(200.0F + p); }
    cam.pad_to((cam.bytes.size() + 15U) & ~std::size_t{15U});
    {
        const auto read = stage_config::read_cam(cam.bytes);
        assert(read.ok());
        assert(read.document.records.size() == 1U && read.document.records[0].zone_count == 2U);
        assert(read.document.areas.size() == 2U && read.document.areas[1].record == 0);
        assert(read.document.paths.size() == 1U && read.document.paths[0].points.size() == 3U);
    }
    expect("st100_cam.cam", cam.bytes, Format::StageCam, "CAM", "camera record 0");
    // Not a walk of camera records (the second record is the path): no shots.
    assert(stage_config::read_cam_shots(cam.bytes).empty());

    std::vector<std::uint8_t> shot_bytes;  // kept for the base room's game camera below
    // The game's cameras: a fixed camera following the player over one zone,
    // a rail camera over a far zone and a wide one round both.
    {
        Writer shots;
        shots.header("CAM", 1, 2);
        const auto zone = [&](float x0, float z0, float x1, float z1) {
            shots.corner(x0, -10, z0); shots.corner(x0, -10, z1); shots.corner(x1, -10, z1); shots.corner(x1, -10, z0);
            shots.f32(500.0F); shots.zeros(12);
        };
        auto at = shots.bytes.size();
        // The zone count is the byte at +0x14; st002 keeps 01 78 after it.
        shots.u32(0x000U); shots.zeros(0x10); shots.u32(0x00780101U);
        shots.pad_to(at + 0x40U); shots.u32(0x200U);
        shots.pad_to(at + 0x50U); shots.f32(0.0F); shots.f32(105.0F); shots.f32(0.0F);
        shots.pad_to(at + 0x90U); shots.f32(500.0F); shots.f32(300.0F); shots.f32(500.0F);
        shots.pad_to(at + 0xA0U); shots.f32(1.05F);
        shots.pad_to(at + 0xB0U);
        zone(0, 0, 1000, 1000);
        at = shots.bytes.size();
        shots.u32(0x100U); shots.zeros(0x10); shots.u32(2);
        shots.pad_to(at + 0x80U);
        for (const float x : {2000.0F, 3000.0F}) { shots.f32(x); shots.f32(100.0F); shots.f32(0.0F); }
        shots.pad_to(at + 0x200U);
        for (const float x : {2000.0F, 3000.0F}) { shots.f32(x); shots.f32(150.0F); shots.f32(500.0F); }
        shots.pad_to(at + 0x380U); shots.f32(1.0F); shots.f32(1.2F);
        shots.pad_to(at + 0x480U); shots.u32(2);
        shots.pad_to(at + 0x490U);
        zone(1500, -500, 3500, 1500);
        zone(-1000, -1000, 5000, 2000);
        shot_bytes = shots.bytes;
        const auto read = stage_config::read_cam_shots(shots.bytes);
        assert(read.size() == 2U && read[0].type == 0U && read[0].follows_player && read[1].eye.size() == 2U);
        // The fixed camera: its eye, the player's chest.
        const auto fixed = stage_config::cam_view_at(read, {200.0F, 0.0F, 300.0F});
        assert(fixed && fixed->shot == 0 && fixed->eye.x == 500.0F && fixed->target.y == 105.0F &&
               fixed->target.x == 200.0F && std::fabs(fixed->fov_radians - 1.05F) < 1.0e-6F);
        // On the rail: halfway along the target rail, halfway along the eye rail.
        const auto rail = stage_config::cam_view_at(read, {2500.0F, 0.0F, 800.0F});
        assert(rail && rail->shot == 1 && std::fabs(rail->eye.x - 2500.0F) < 1.0e-2F && rail->eye.z == 0.0F &&
               std::fabs(rail->target.z - 500.0F) < 1.0e-3F && std::fabs(rail->fov_radians - 1.1F) < 1.0e-5F);
        // Only the wide zone: the rail; outside every zone: none; above a zone's height: none.
        assert(stage_config::cam_view_at(read, {4500.0F, 0.0F, 0.0F})->shot == 1);
        assert(!stage_config::cam_view_at(read, {9000.0F, 0.0F, 0.0F}));
        assert(!stage_config::cam_view_at(read, {200.0F, 600.0F, 300.0F}));
        // A torn file reads nothing.
        auto torn = shots.bytes;
        torn.resize(torn.size() - 0x50U);
        assert(stage_config::read_cam_shots(torn).empty());
    }

    // ITM: two items, padded to 16.
    Writer itm;
    itm.header("ITM", 1, 2);
    for (int i = 0; i < 2; ++i) { itm.u32(40U + i); itm.f32(1.0F); itm.f32(2.0F); itm.f32(3.0F + i); itm.f32(0.5F); }
    itm.pad_to((itm.bytes.size() + 15U) & ~std::size_t{15U});
    expect("st100_itm.itm", itm.bytes, Format::StageItm, "ITM", "Item id: 41");

    // STE: one element.
    Writer ste;
    ste.header("STE", 1, 1);
    ste.u16(2); ste.u16(9);
    for (float v : {1.0F, 2.0F, 3.0F, 0.0F, 45.0F, 0.0F, 1.0F, 1.0F, 1.0F}) ste.f32(v);
    expect("st100_ste.ste", ste.bytes, Format::StageSte, "STE", "Number: 9");

    // EST: one row, two programs (the last two modes share one), table after them.
    Writer est;
    est.tag("EST"); est.u16(1); est.u16(1); est.u32(0x30U); est.zeros(4);
    const auto program_a = est.bytes.size();
    est.u32(0x0201U); est.u32(5); est.u32(6); est.u32(0);  // code 1, two arguments
    const auto program_b = est.bytes.size();
    est.u32(0x0003U); est.u32(0);                          // code 3, none
    est.pad_to(0x30U);
    for (std::uint32_t at : {static_cast<std::uint32_t>(program_a), static_cast<std::uint32_t>(program_a), 0U,
                             static_cast<std::uint32_t>(program_b), static_cast<std::uint32_t>(program_b)}) {
        est.u32(at);
    }
    {
        const auto read = stage_config::read_est(est.bytes);
        assert(read.ok() && read.document.programs.size() == 2U);
        assert(read.document.rows[0][2] == -1 && read.document.rows[0][4] == 1);
        assert(read.document.programs[0].commands[0].arguments.size() == 2U);
    }
    expect("st100_est.est", est.bytes, Format::StageEst, "EST", "Dante Must Die");

    // SEF: two sections of words; value is the word count.
    Writer sef;
    sef.tag("SEF"); sef.u16(2); sef.u16(0);
    sef.u32(2); sef.u32(0x18U); sef.u32(1); sef.u32(0x20U);
    sef.u32(11); sef.u32(12); sef.u32(13);
    {
        const auto read = stage_config::read_sef(sef.bytes);
        assert(read.ok() && read.document.sections.size() == 2U && read.document.sections[0].size == 8U);
    }
    expect("st100_sef.sef", sef.bytes, Format::StageSef, "SEF", "a count of words");

    // ---- the stage configuration in the viewer ----
    // A cfg PAC: POS and EVE at the top, CAM inside a nested PAC.
    const auto pack = [](const std::vector<std::vector<std::uint8_t>>& slots) {
        Writer w;
        w.tag("PAC");
        w.u32(static_cast<std::uint32_t>(slots.size()));
        std::size_t at = (8U + slots.size() * 4U + 15U) & ~std::size_t{15U};
        std::vector<std::uint32_t> offsets;
        for (const auto& slot : slots) {
            offsets.push_back(static_cast<std::uint32_t>(at));
            at = (at + slot.size() + 15U) & ~std::size_t{15U};
        }
        for (const auto o : offsets) w.u32(o);
        for (std::size_t i = 0; i < slots.size(); ++i) {
            w.pad_to(offsets[i]);
            w.bytes.insert(w.bytes.end(), slots[i].begin(), slots[i].end());
        }
        w.pad_to((w.bytes.size() + 15U) & ~std::size_t{15U});
        return w.bytes;
    };
    const auto cfg = pack({pos.bytes, eve.bytes, pack({cam.bytes})});
    {
        // The base room's game camera: a model on spawn point 0 of a
        // configuration whose fixed camera (eye 500 300 500) follows the
        // player looks from that eye at the model's chest.
        const auto base = stage_markers::load_stage_config("st100cfg.pac", pack({pos.bytes, shot_bytes}));
        assert(base != nullptr);
        auto room = std::make_shared<stage_room::Room>();
        room->name = "st100.pac";
        room->mesh.vertices = {{-5000, 0, -5000}, {5000, 0, -5000}, {5000, 0, 5000}};
        room->mesh.indices = {0, 1, 2};
        room->spots = {{0, 0, 0}};
        stage_room::set_current(room);
        stage_markers::set_current(StageSlot::base, base);
        Session model;
        model.renderable = true;
        model.render_mesh.vertices = {{-50, 0, -50}, {50, 200, 50}};
        model.render_mesh.indices = {0, 0, 0};
        namespace act = dmcresource::spider::actions;
        assert(!act::stage_start_view(&model));  // no spawn point chosen
        const auto spawns = stage_markers::spawn_points(StageSlot::base);
        assert(!spawns.empty() && act::choose_base_spawn(0) && act::base_spawn() == 0);
        const auto at = stage_room::spot_position();
        assert(at.x == spawns[0].position.x && at.z == spawns[0].position.z);
        const auto view = act::stage_start_view(&model);
        assert(view);
        const float dx = at.x - 500.0F, dy = at.y + 105.0F - 300.0F, dz = at.z - 500.0F;
        assert(std::fabs((*view)[0] - std::atan2(dx, dz)) < 1.0e-4F);
        assert(std::fabs((*view)[1] - std::atan2(dy, std::hypot(dx, dz))) < 1.0e-4F && (*view)[2] > 0.0F);
        assert(!act::choose_base_spawn(99) && act::choose_base_spawn(-1) && act::base_spawn() == -1);
        assert(!act::stage_start_view(&model));
        // "Next spot" steps through the configuration's spawn points, round.
        assert(act::base_spot_count() == static_cast<int>(spawns.size()));
        for (std::size_t step = 0; step <= spawns.size(); ++step) {
            const auto next = act::next_base_spot();
            assert(next.spawn && next.index == static_cast<int>(step % spawns.size()) &&
                   next.title == spawns[step % spawns.size()].title && act::base_spawn() == next.index);
        }
        act::choose_base_spawn(-1);
        stage_markers::set_current(StageSlot::base, nullptr);
        // Without a configuration: the room's floor spots.
        const auto floor = act::next_base_spot();
        assert(!floor.spawn && floor.count == 1 && act::base_spawn() == -1);
        stage_room::set_current(nullptr);
        assert(act::next_base_spot().index == -1);
    }
    const auto config = stage_markers::load_stage_config("st100cfg.pac", cfg);
    assert(config != nullptr && config->members.size() == 3U);
    assert(config->members[2].name.find("slot 2 / slot 0") != std::string::npos);
    // 2 spawn points + 1 event zone + 2 camera zones + 1 camera path.
    assert(config->markers.size() == 6U);
    assert(config->markers[2].member == 1U && config->markers[2].source == stage_config::Kind::Eve);
    assert(!stage_markers::load_stage_config("noise.bin", std::vector<std::uint8_t>(64, 7U)));

    const auto overlay = stage_markers::build_overlay(*config, 1);
    assert(overlay.labels.size() == 6U && overlay.labels[0].text == "POS 0");
    // The chosen marker is white and wider; the others keep their kind colour.
    bool white = false, green = false;
    for (const auto& line : overlay.lines) {
        white = white || (line.color == std::array<std::uint8_t, 3>{255U, 255U, 255U} && line.width == 3U);
        green = green || line.color == std::array<std::uint8_t, 3>{90U, 220U, 120U};
    }
    assert(white && green);

    // A stage scene: a floor under the markers, drawn by the room pass.
    {
        auto room = std::make_shared<stage_room::Room>();
        room->mesh.vertices = {{-50, 0, -100}, {150, 0, -100}, {150, 0, 100}, {-50, 0, 100}};
        room->mesh.indices = {0, 1, 2, 0, 2, 3};
        room->triangle_texture_slots = {kNoTextureSlot, kNoTextureSlot};
        room->spots = {{50, 0, 0}};
        Session stage;
        stage.renderable = true;
        stage.stage = room;
        // The framing box of a stage session, here wide enough to frame every marker.
        stage.render_mesh.vertices = {{-50, 0, -100}, {150, 0, -100}, {150, 0, 250}, {-50, 60, 250}};
        stage.render_mesh.indices = {0, 0, 0};
        stage_markers::set_current(nullptr);
        const auto plain = render_session(&stage, 320, 240, 0.4F, -0.6F, 1.0F, 0U);
        stage_markers::set_current(config);
        stage_markers::set_visible(true);
        const auto marked = render_session(&stage, 320, 240, 0.4F, -0.6F, 1.0F, 0U);
        assert(plain.pixels.size() == marked.pixels.size() && plain.pixels != marked.pixels);
        stage_markers::set_visible(false);
        assert(render_session(&stage, 320, 240, 0.4F, -0.6F, 1.0F, 0U).pixels == plain.pixels);
        stage_markers::set_visible(true);

        // Holding spawn point 1's post picks it; empty sky picks nothing.
        ViewControls controls;
        const auto miss = pick_session(&stage, 320, 240, 0.4F, -0.6F, 1.0F, 0U, controls, 2.0F, 2.0F);
        assert(miss.marker == -1);
        int found = -1;
        for (int y = 0; y < 240 && found < 0; y += 2) {
            for (int x = 0; x < 320 && found < 0; x += 2) {
                const auto hit = pick_session(&stage, 320, 240, 0.4F, -0.6F, 1.0F, 0U, controls,
                                              static_cast<float>(x), static_cast<float>(y));
                if (hit.marker == 1) found = hit.marker;
            }
        }
        assert(found == 1);
        // The card the shell shows (spider marker_card): title, fields, the file.
        const auto card = dmcresource::spider::actions::marker_card(1);
        bool heading = false, file_row = false;
        for (const auto& [key, value] : card.rows) {
            heading = heading || (key == "Heading" && value.find("90.00 deg") != std::string::npos);
            file_row = file_row || (key == "File" && value == "st100cfg.pac / slot 0 (POS)");
        }
        assert(card.title == "Spawn point 1" && heading && file_row);
        auto file = open_stage_marker_file(1);
        assert(file && file->image_preview.available());
        assert(stage_config_summary().find("2 spawn points") != std::string::npos);
        assert(dmcresource::spider::actions::marker_card(99).title.empty() && !open_stage_marker_file(-1));
        stage_markers::set_current(nullptr);
        assert(stage_config_summary().empty());

        // ---- the same flow through the Spider actions and Black Widow ----
        namespace act = dmcresource::spider::actions;
        namespace widow = dmcresource::spider::black_widow;
        auto state = black_widow_state(&stage);
        // View tools of a stage scene: fly, no shadows / hitboxes; no HITS, no broken state here.
        assert(widow::has_state(state, widow::StateFlag::CanFly));
        assert(!widow::has_state(state, widow::StateFlag::CanShowShadows) &&
               !widow::has_state(state, widow::StateFlag::CanShowHitboxes) &&
               !widow::has_state(state, widow::StateFlag::CanShowStageCollision) &&
               !widow::has_state(state, widow::StateFlag::CanBreakRoom));
        assert(widow::has_state(state, widow::StateFlag::CanLoadStageConfig));
        assert(!widow::has_state(state, widow::StateFlag::StageConfigLoaded));
        // No configuration: the tool asks the shell for the file picker.
        assert(act::stage_config_tool().step == act::ShellStep::PickStageConfig);
        assert(act::stage_config_menu().items.size() == 1U);
        // A diagnostics log written over the file is named as such.
        const std::string log = "[2026-10-21 23:02:17] build=0 route=external name=em000.pac\n";
        const auto logged = act::load_stage_config(
            "st001cfg.pac", reinterpret_cast<const std::uint8_t*>(log.data()), log.size());
        assert(!logged.ok && logged.message.find("diagnostics log") != std::string::npos);
        const auto bad = act::load_stage_config("noise.bin", std::vector<std::uint8_t>(64, 7U).data(), 64U);
        assert(!bad.ok && bad.message.find("No stage configuration in noise.bin") == 0U);
        const auto loaded = act::load_stage_config("st100cfg.pac", cfg.data(), cfg.size());
        // The view starts again at the configuration's spawn point.
        assert(loaded.ok && loaded.step == act::ShellStep::ResetView && loaded.message.find("2 spawn points") != std::string::npos);
        {
            // Into the base room: no markers to hold, the spawn points are in Settings.
            const auto previous = active_slot();
            set_active_slot(StageSlot::base);
            const auto base = act::load_stage_config("st100cfg.pac", cfg.data(), cfg.size());
            assert(base.ok && base.step == act::ShellStep::None && base.message.rfind("Base room config:", 0) == 0U &&
                   base.message.find("Hold a marker") == std::string::npos);
            stage_markers::set_current(StageSlot::base, nullptr);
            set_active_slot(previous);
            assert(act::load_stage_config("st100cfg.pac", cfg.data(), cfg.size()).step == act::ShellStep::ResetView);
        }
        {
            // st100's view starts behind spawn point 0 (0 0 0, facing +z),
            // looking at its chest; another stage's ignores the config and
            // starts on its own floor (here its first floor spot).
            const auto centre = [](const Session& session) {
                const auto& box = session.render_mesh.vertices;
                return Vec3{(box.front().x + box.back().x) * 0.5F, (box.front().y + box.back().y) * 0.5F,
                            (box.front().z + box.back().z) * 0.5F};
            };
            Session ours;
            ours.stage = stage.stage;
            ours.archive_name = "st100.pac";
            const auto view = act::stage_start_view(&ours);
            assert(view && std::fabs((*view)[0]) < 1.0e-3F && (*view)[1] < 0.0F);
            const auto c = centre(ours);
            assert(std::fabs(c.x) < 1.0e-3F && c.y > 50.0F && std::fabs(c.z) < 1.0e-3F);
            Session other;
            other.stage = stage.stage;
            other.archive_name = "st200.pac";
            assert(act::stage_start_view(&other) && std::fabs(centre(other).x - 50.0F) < 1.0e-3F);
            assert(!act::stage_start_view(nullptr));
        }
        state = black_widow_state(&stage);
        assert(widow::has_state(state, widow::StateFlag::StageConfigLoaded) &&
               widow::has_state(state, widow::StateFlag::StageMarkersShown));
        // Tool: hide, then show; the menu offers load / hide / remove.
        assert(act::stage_config_tool().message == "Stage config hidden");
        assert(!widow::has_state(black_widow_state(&stage), widow::StateFlag::StageMarkersShown));
        assert(act::stage_config_tool().message.rfind("Stage config shown", 0) == 0U);
        const auto menu = act::stage_config_menu();
        assert(menu.items.size() == 3U && menu.items[1].label == "Hide");
        assert(act::run_stage_config_menu(menu.items[0].id).step == act::ShellStep::PickStageConfig);
        // Holding spawn point 1 asks for its card; the card's rows and buttons.
        int held = -1;
        for (int y = 0; y < 240 && held < 0; y += 2) {
            for (int x = 0; x < 320 && held < 0; x += 2) {
                const auto hold = act::hold_view(&stage, 320, 240, 0.4F, -0.6F, 1.0F, 0U, controls,
                                                 static_cast<float>(x), static_cast<float>(y), true, true);
                if (hold.step == act::ShellStep::ShowMarkerCard && hold.marker == 1) held = hold.marker;
            }
        }
        assert(held == 1 && stage_markers::selected() == 1);
        const auto spider_card = act::marker_card(1);
        // A spawn point's card: the game's camera, its file, close.
        assert(spider_card.title == "Spawn point 1" && spider_card.actions.size() == 3U &&
               spider_card.actions[0].label == "Game camera" && spider_card.rows.back().first == "File");
        {
            // The game camera of spawn point 1: the view starts there (this
            // configuration's CAM is no camera walk: from behind, 100 0 -50).
            const auto camera = act::run_marker_card_action(1, spider_card.actions[0].id, nullptr);
            assert(camera.ok && camera.step == act::ShellStep::ResetView && stage_markers::selected() == -1);
            Session ours;
            ours.stage = stage.stage;
            ours.archive_name = "st100.pac";
            const auto view = act::stage_start_view(&ours);
            assert(view && (*view)[2] == 1.0F);
            const auto& box = ours.render_mesh.vertices;
            assert(std::fabs((box.front().x + box.back().x) * 0.5F - 100.0F) < 1.0e-3F &&
                   std::fabs((box.front().z + box.back().z) * 0.5F + 50.0F) < 1.0e-3F);
            stage_markers::set_selected(1);
        }
        std::unique_ptr<Session> opened;
        const auto open = act::run_marker_card_action(1, spider_card.actions[1].id, &opened);
        assert(open.step == act::ShellStep::OpenSession && opened && open.title == "Spawn point 1 \xC2\xB7 file");
        assert(stage_markers::selected() == -1);
        // The opened POS exports its map at the map's size.
        assert(act::export_image_size(opened.get()).first == 1080);
        assert(act::render_export_image(opened.get()).width == 1080);
        // Sky: nothing held, no scrub without a motion.
        const auto sky = act::hold_view(&stage, 320, 240, 0.4F, -0.6F, 1.0F, 0U, controls, 2.0F, 2.0F, true, true);
        assert(sky.step == act::ShellStep::None && sky.message.empty());
        // The ⋮ menu carries the stage config rows (no toolbar button), decided by the core.
        {
            const act::ShellFacts facts{.root_scene = true, .assembled_pac_source = false, .stage_source = true,
                                        .room_loaded = false, .room_shown = false};
            const auto companions = act::companion_menu(&stage, facts);
            const auto has_row = [&](act::Companion id) {
                for (const auto& row : companions.rows) if (row.id == static_cast<int>(id)) return true;
                return false;
            };
            const auto has_op = [&](act::Companion id) {
                for (const auto& row : companions.stage_ops) if (row.id == static_cast<int>(id)) return true;
                return false;
            };
            const auto ready = [&](act::Companion id) {
                for (const int r : companions.stage_ops_ready) if (r == static_cast<int>(id)) return true;
                return false;
            };
            assert(has_row(act::Companion::StageConfigToggle) && !has_row(act::Companion::StageConfigRemove) &&
                   has_row(act::Companion::BrowseStagePac));
            // Stage Ops: room, effect bank, configuration and sounds, each with
            // its emoji; the opened stage is the room, its configuration loaded.
            assert(companions.stage_ops.size() == 4U && has_op(act::Companion::StageOpsRoom) &&
                   has_op(act::Companion::StageEffects) && has_op(act::Companion::StageConfigLoad) &&
                   has_op(act::Companion::StageOpsSound));
            for (const auto& op : companions.stage_ops) assert(!op.icon.empty());
            assert(ready(act::Companion::StageOpsRoom) && ready(act::Companion::StageConfigLoad) &&
                   !ready(act::Companion::StageOpsSound));
            // The loaded configuration's row tells what it holds.
            for (const auto& op : companions.stage_ops) {
                if (op.id == static_cast<int>(act::Companion::StageConfigLoad)) {
                    assert(op.label.find("2 spawn points") != std::string::npos);
                }
            }
            assert(companions.stage_ops_warn.empty());
            {
                // A stage of another number: st100's configuration is another stage's (orange).
                auto named = std::make_shared<stage_room::Room>(*stage.stage);
                named->name = "st200.pac";
                Session other_stage;
                other_stage.renderable = true;
                other_stage.stage = named;
                const auto other = act::companion_menu(&other_stage, facts);
                bool warned = false;
                for (const int id : other.stage_ops_warn) warned = warned || id == static_cast<int>(act::Companion::StageConfigLoad);
                assert(warned);
            }
            assert(!has_row(act::Companion::StageConfigLoad) && !has_row(act::Companion::StageEffects));
            assert(!has_row(act::Companion::AddPac));
            // Squares: settings, gestures, the camera's settings. Rows: Open
            // and Diagnostics first (every file has them), then the stage's;
            // one browser row.
            assert(companions.shortcuts.size() == 3U && !companions.shortcuts[0].icon.empty());
            assert(companions.shortcuts[2].id == static_cast<int>(act::Companion::CameraSettings));
            assert(companions.rows[0].id == static_cast<int>(act::Companion::OpenFile) &&
                   companions.rows[1].id == static_cast<int>(act::Companion::Diagnostics));
            assert(companions.more.empty());
            std::size_t browsers = 0U;
            for (const auto& row : companions.rows) {
                browsers += row.label.find("Browse") != std::string::npos ? 1U : 0U;
            }
            assert(browsers == 1U);
            assert(act::run_companion_action(static_cast<int>(act::Companion::StageConfigToggle)).message ==
                   "Stage config hidden");
            assert(act::run_companion_action(static_cast<int>(act::Companion::StageConfigLoad)).step ==
                   act::ShellStep::PickStageConfig);
            assert(act::run_companion_action(static_cast<int>(act::Companion::OpenFile)).step == act::ShellStep::None);
            act::run_companion_action(static_cast<int>(act::Companion::StageConfigToggle));
            // A model with a room loaded but hidden: the stage tools fold
            // away under "Other actions"; with no stage around at all there are
            // none. Open and Diagnostics stay.
            Session lone;
            const act::ShellFacts hidden_room{.root_scene = true, .assembled_pac_source = false,
                                              .stage_source = false, .room_loaded = true, .room_shown = false};
            const auto folded = act::companion_menu(&lone, hidden_room);
            for (const auto& row : folded.rows) {
                assert(row.id != static_cast<int>(act::Companion::StageEffects));
            }
            // No Stage Ops in ⋮ around a model: its base room is in Settings.
            assert(folded.rows.size() == 2U && folded.stage_ops.empty());
            // Settings' base room: the same four archives, of the room loaded
            // there (none here), whatever file is open.
            const auto base = act::base_room_menu(no_room_facts());
            assert(base.stage_ops.size() == 4U && base.rows.empty() && base.shortcuts.empty());
            for (const int r : base.stage_ops_ready) assert(r != static_cast<int>(act::Companion::StageOpsRoom));
            assert(!act::run_companion_action(static_cast<int>(act::Companion::StageOpsSound)).message.empty());
            const act::ShellFacts no_room{.root_scene = true, .assembled_pac_source = false,
                                          .stage_source = false, .room_loaded = false, .room_shown = false};
            const auto bare = act::companion_menu(&lone, no_room);
            assert(bare.rows.size() == 2U && bare.more.empty());
            // While a video records, its square stops it whatever is open;
            // nothing to draw: no picture square.
            act::ShellFacts recording = no_room;
            recording.recording = true;
            bool stop = false;
            for (const auto& square : act::companion_menu(&lone, recording).shortcuts) {
                assert(square.id != static_cast<int>(act::Companion::Photo));
                stop = stop || (square.id == static_cast<int>(act::Companion::Video) &&
                                square.label == "Stop recording");
            }
            assert(stop);
        }
        // Removing acts on the set of the session the menu is for: the
        // stage's own (stage_slot.h), as the shell builds the menu first.
        set_active_slot(slot_for(&stage));
        assert(act::run_stage_config_menu(menu.items[2].id).message == "Stage config removed");
        assert(!widow::has_state(black_widow_state(&stage), widow::StateFlag::StageConfigLoaded));
        // The base room's set and the opened stage's never mix.
        stage_markers::set_current(StageSlot::base, config);
        assert(stage_markers::current(StageSlot::scene) == nullptr);
        assert(!widow::has_state(black_widow_state(&stage), widow::StateFlag::StageConfigLoaded));
        Session model;  // a model stands in the base room: its set
        set_active_slot(slot_for(&model));
        assert(stage_markers::current() == config);
        // The base room offers its spawn points (POS) to put a model on.
        const auto spawns = stage_markers::spawn_points(StageSlot::base);
        assert(spawns.size() == 2U && spawns[0].title.rfind("Spawn point", 0) == 0U);
        assert(stage_markers::spawn_points(StageSlot::scene).empty());
        stage_markers::set_current(StageSlot::base, nullptr);
        assert(stage_markers::spawn_points(StageSlot::base).empty());
    }

    // A configuration's STE records are effects of the stage's bank, played
    // in that stage only.
    {
        Writer fx;
        fx.header("STE", 1, 2);
        fx.u16(3); fx.u16(98);
        for (float v : {1050.0F, 570.0F, 2130.0F, 0.0F, 90.0F, 0.0F, 0.5F, 0.5F, 0.5F}) fx.f32(v);
        fx.u16(9); fx.u16(5);  // no such dispatch kind: skipped
        for (float v : {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F}) fx.f32(v);
        const auto loaded = dmcresource::spider::actions::load_stage_config("st006cfg.pac", fx.bytes.data(), fx.bytes.size());
        assert(loaded.ok);
        const auto placed = stage_markers::placed_effects("st006.pac");
        assert(placed.size() == 1U && placed[0].kind == 'V' && placed[0].id == 98U &&
               placed[0].position.y == 570.0F && placed[0].rotation_degrees.y == 90.0F && placed[0].scale.x == 0.5F);
        assert(stage_markers::placed_effects("st100.pac").empty());
        assert(stage_markers::stage_number("ST006CFG.PAC") == 6 && stage_markers::stage_number("m02_s00.pac") == -1);
        // The archives found next to a picked stage.
        const auto archives = stage_markers::stage_archives("ST001.pac");
        assert(archives && archives->effects == "st001_effect.pac" && archives->config == "st001cfg.pac");
        assert(!stage_markers::stage_archives("pl000.pac"));
    }
    // The SEF section of value 2 is the stage's fog (st006cfg: 1000 .. 2360,
    // grey 80 80 80 at 0x34 of 0x80).
    {
        Writer sef;
        sef.tag("SEF"); sef.u16(1); sef.u16(1);
        sef.u32(2U); sef.u32(0x10U);
        sef.f32(1000.0F); sef.f32(2360.0F); sef.u32(0x34808080U); sef.u32(3U);
        assert(dmcresource::spider::actions::load_stage_config("st006cfg.pac", sef.bytes.data(), sef.bytes.size()).ok);
        const auto fog = stage_markers::stage_fog("st006.pac");
        assert(fog && fog->enabled && fog->start == 1000.0F && fog->end == 2360.0F && fog->near_alpha == 0.0F &&
               std::fabs(fog->far_alpha - 52.0F / 128.0F) < 1.0e-6F && fog->far_colour[0] == 0x80U);
        assert(!stage_markers::stage_fog("st100.pac"));
    }

    // Truncated or foreign bytes are not taken.
    std::vector<std::uint8_t> short_pos(pos.bytes.begin(), pos.bytes.begin() + 40);
    assert(!stage_config::read_pos(short_pos).ok());
    assert(!run("bad.pos", short_pos).accepted);
    std::vector<std::uint8_t> bad_eve = eve.bytes;
    bad_eve.pop_back();
    assert(!stage_config::read_eve(bad_eve).ok());
    // A CAM record whose zones are not corner quads is left to the shape scan.
    Writer odd;
    odd.header("CAM", 1, 1);
    odd.u32(0x400U); odd.zeros(0x10); odd.u32(1); odd.pad_to(0x50U); odd.zeros(0x50);
    assert(stage_config::read_cam(odd.bytes).document.records.empty());
    // Tags alone: three letters and a zero byte; "POSX" is not POS.
    std::vector<std::uint8_t> not_pos = pos.bytes;
    not_pos[3] = 'X';
    assert(dmcresource::probe("renamed.bin", not_pos.data(), not_pos.size()).format != Format::StagePos);
    return 0;
}
