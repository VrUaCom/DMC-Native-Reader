#include <cassert>
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
        const auto card = stage_marker_card(1);
        assert(card.rfind("Spawn point 1\n", 0) == 0 && card.find("Heading: 90.00 deg") != std::string::npos);
        assert(card.find("File: st100cfg.pac / slot 0 (POS)") != std::string::npos);
        auto file = open_stage_marker_file(1);
        assert(file && file->image_preview.available());
        assert(stage_config_summary().find("2 spawn points") != std::string::npos);
        assert(stage_marker_card(99).empty() && !open_stage_marker_file(-1));
        stage_markers::set_current(nullptr);
        assert(stage_config_summary().empty());

        // ---- the same flow through the Spider actions and Black Widow ----
        namespace act = dmcresource::spider::actions;
        namespace widow = dmcresource::spider::black_widow;
        auto state = black_widow_state(&stage);
        assert(widow::has_state(state, widow::StateFlag::CanLoadStageConfig));
        assert(!widow::has_state(state, widow::StateFlag::StageConfigLoaded));
        // No configuration: the tool asks the shell for the file picker.
        assert(act::stage_config_tool().step == act::ShellStep::PickStageConfig);
        assert(act::stage_config_menu().items.size() == 1U);
        const auto bad = act::load_stage_config("noise.bin", std::vector<std::uint8_t>(64, 7U).data(), 64U);
        assert(!bad.ok && bad.message.find("No stage configuration in noise.bin") == 0U);
        const auto loaded = act::load_stage_config("st100cfg.pac", cfg.data(), cfg.size());
        assert(loaded.ok && loaded.step == act::ShellStep::None && loaded.message.find("2 spawn points") != std::string::npos);
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
        assert(spider_card.title == "Spawn point 1" && spider_card.actions.size() == 2U &&
               spider_card.rows.back().first == "File");
        std::unique_ptr<Session> opened;
        const auto open = act::run_marker_card_action(1, spider_card.actions[0].id, &opened);
        assert(open.step == act::ShellStep::OpenSession && opened && open.title == "Spawn point 1 \xC2\xB7 file");
        assert(stage_markers::selected() == -1);
        // The opened POS exports its map at the map's size.
        assert(act::export_image_size(opened.get()).first == 1080);
        assert(act::render_export_image(opened.get()).width == 1080);
        // Sky: nothing held, no scrub without a motion.
        const auto sky = act::hold_view(&stage, 320, 240, 0.4F, -0.6F, 1.0F, 0U, controls, 2.0F, 2.0F, true, true);
        assert(sky.step == act::ShellStep::None && sky.message.empty());
        assert(act::run_stage_config_menu(menu.items[2].id).message == "Stage config removed");
        assert(!widow::has_state(black_widow_state(&stage), widow::StateFlag::StageConfigLoaded));
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
