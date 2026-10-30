#include "dmcresource/spider/companion_menu.h"

#include <cstdio>
#include <initializer_list>
#include <string>

#include "dmcresource/camera_control.h"
#include "dmcresource/demo_session.h"
#include "dmcresource/spider/black_widow.h"
#include "dmcresource/game_lights.h"
#include "dmcresource/stage_config.h"
#include "dmcresource/stage_markers.h"
#include "dmcresource/stage_room.h"
#include "dmcresource/video.h"

namespace dmcresource::spider::actions {
namespace {

namespace widow = dmcresource::spider::black_widow;

void add(std::vector<MenuItem>& items, Companion id, std::string label, std::string icon = {}) {
    items.push_back({static_cast<int>(id), std::move(label), std::move(icon)});
}

// The four archives of a full stage (stNNN.pac, stNNN_effect.pac,
// stNNNcfg.pac and its sounds): each row names the file loaded, or the one
// to add. The stage number comes from whichever of them is loaded.
void stage_ops(const Session* session, const ShellFacts& facts, CompanionMenu& menu, StageSlot slot) {
    const auto room = stage_room::current();
    const auto host = stage_room::effect_host(slot);
    const auto config = stage_markers::current(slot);
    std::string room_name;
    if (session != nullptr && session->stage != nullptr) {
        // The opened stage is the room, whatever its name says.
        room_name = !session->stage->name.empty() ? session->stage->name
                    : !session->source_name.empty() ? session->source_name
                                                    : std::string{"the opened stage"};
    }
    if (room_name.empty() && facts.room_loaded && room != nullptr) room_name = room->name;
    const std::string effects_name = host != nullptr ? host->archive_name : std::string{};
    int number = -1;
    for (const std::string* name : std::initializer_list<const std::string*>{&room_name, &effects_name}) {
        if (number < 0) number = stage_markers::stage_number(*name);
    }
    // A configuration of another stage does not count for this one (it is
    // neither drawn nor used); it shows in orange.
    const bool config_fits = config != nullptr && (number < 0 || stage_markers::stage_number(config->name) < 0 ||
                                                   stage_markers::stage_number(config->name) == number);
    const std::string config_name = config_fits ? config->name : std::string{};
    if (number < 0 && config_fits) number = stage_markers::stage_number(config_name);
    char stem[16] = "stNNN";
    if (number >= 0 && number < 1000) std::snprintf(stem, sizeof stem, "st%03d", number);
    // What the loaded configuration holds: lights, spawn points, placed effects.
    std::string config_summary;
    if (config_fits) {
        std::size_t spawns = 0U, lights = 0U, placed = 0U;
        for (const auto& marker : config->markers) spawns += marker.source == stage_config::Kind::Pos ? 1U : 0U;
        if (config->lights) {
            for (const auto& record : config->lights->records) lights += record.active ? 1U : 0U;
        }
        for (const auto& member : config->members) {
            if (member.kind != stage_config::Kind::Ste || member.bytes == nullptr) continue;
            placed += stage_config::read_ste(*member.bytes).document.records.size();
        }
        config_summary = "\n" + std::to_string(lights) + " lights \xC2\xB7 " + std::to_string(spawns) +
                         " spawn points \xC2\xB7 " + std::to_string(placed) + " placed effects";
    }
    const auto row = [&](Companion id, const char* icon, const char* what, const std::string& loaded,
                         const std::string& expected, const char* missing, const std::string& extra = {}) {
        const bool ready = !loaded.empty();
        add(menu.stage_ops, id,
            std::string{what} + "\n" + (ready ? loaded + " \xC2\xB7 loaded" + extra : expected + " \xC2\xB7 " + missing),
            icon);
        if (ready) menu.stage_ops_ready.push_back(static_cast<int>(id));
    };
    // An archive of another stage than the room: named, in orange.
    const auto other_stage = [&](Companion id, const char* icon, const char* what, const std::string& name) {
        add(menu.stage_ops, id,
            std::string{what} + "\n" + name + " \xC2\xB7 another stage's (the room is " + stem + ")", icon);
        menu.stage_ops_warn.push_back(static_cast<int>(id));
    };
    row(Companion::StageOpsRoom, "\xF0\x9F\x8F\x9B", "Room", room_name, std::string{stem} + ".pac",
        "missing, tap to add");
    const int effects_number = stage_markers::stage_number(effects_name);
    if (!effects_name.empty() && number >= 0 && effects_number >= 0 && effects_number != number) {
        other_stage(Companion::StageEffects, "\xE2\x9C\xA8", "Effects", effects_name);
    } else {
        row(Companion::StageEffects, "\xE2\x9C\xA8", "Effects", effects_name, std::string{stem} + "_effect.pac",
            "missing, tap to add (static_effect.pac too)");
    }
    if (config != nullptr && !config_fits) {
        other_stage(Companion::StageConfigLoad, "\xF0\x9F\x97\xBA", "Config", config->name);
    } else {
        row(Companion::StageConfigLoad, "\xF0\x9F\x97\xBA", "Config", config_name, std::string{stem} + "cfg.pac",
            "missing, tap to add", config_summary);
    }
    row(Companion::StageOpsSound, "\xF0\x9F\x94\x8A", "Sounds", std::string{}, "the stage's sound bank",
        "not read by the Reader yet");
}

}  // namespace

CompanionMenu companion_menu(const Session* session, const ShellFacts& facts) {
    CompanionMenu menu;
    // The menu acts on the stage companions of the opened file (stage_slot.h).
    set_active_slot(slot_for(session));
    const auto state = black_widow_state(session);
    const auto has = [state](widow::StateFlag flag) { return widow::has_state(state, flag); };
    const bool open = session != nullptr;
    const bool root = open && facts.root_scene;
    // What is on the screen decides where an item goes: a cutscene plays
    // its own scene (a model's tools fold away), a model has the stage tools
    // at hand only while a room is drawn around it.
    const bool cutscene = demo::is_demo(session);
    const bool stage_scene = open && session->stage != nullptr && !cutscene;
    const bool room_drawn = facts.room_loaded && facts.room_shown && !cutscene;
    auto& model_rows = cutscene ? menu.more : menu.rows;
    auto& stage_rows = stage_scene || room_drawn ? menu.rows : menu.more;

    // Squares: the quick switches and a cutscene's media (the label is the tooltip).
    add(menu.shortcuts, Companion::Settings, "Settings", "\xE2\x9A\x99");
    add(menu.shortcuts, Companion::Gestures, "Gestures", "\xE2\x9C\x8B");
    if (facts.room_loaded && !cutscene) {
        add(menu.shortcuts, Companion::RoomToggle, facts.room_shown ? "Hide the room" : "Show the room",
            "\xE2\x8C\x82");
        if (!facts.room_shown) menu.dimmed.push_back(static_cast<int>(Companion::RoomToggle));
        // Through the base configuration's spawn points when it has any,
        // otherwise through the room's floor spots.
        const bool spawns = !stage_markers::spawn_points(StageSlot::base).empty();
        if (facts.room_shown && (spawns || base_spot_count() > 1)) {
            add(menu.shortcuts, Companion::NextRoomSpot, spawns ? "Next spawn point (config)" : "Next floor spot",
                "\xE2\x87\x84");
        }
    }
    if (cutscene) {
        // A cutscene's sound, movie part and subtitles live beside the archive.
        add(menu.shortcuts, Companion::CutsceneSound, "Cutscene sound (.ogg)", "\xF0\x9F\x94\x8A");
        if (video::available()) add(menu.shortcuts, Companion::CutsceneMovie, "Cutscene movie (.wmv)", "\xF0\x9F\x8E\x9E");
        add(menu.shortcuts, Companion::CutsceneSubtitles, "Subtitles (*_msg_<lang>.txt)", "\xF0\x9F\x92\xAC");
        // A cutscene whose camera the user took: back to following its cuts.
        if (!demo::director(session)) {
            add(menu.shortcuts, Companion::CutsceneCamera, "Cutscene camera (follow the cuts)", "\xF0\x9F\x8E\xAC");
        }
    }
    if (has(widow::StateFlag::CanSaveSource)) add(menu.shortcuts, Companion::SaveFile, "Save file", "\xF0\x9F\x92\xBE");
    // Capture what the view shows: a picture, a video (the square stops it).
    if (has(widow::StateFlag::CanRender)) add(menu.shortcuts, Companion::Photo, "Take a picture", "\xF0\x9F\x93\xB7");
    if (has(widow::StateFlag::CanRender) || facts.recording) {
        add(menu.shortcuts, Companion::Video, facts.recording ? "Stop recording" : "Record a video",
            facts.recording ? "\xE2\x8F\xB9" : "\xF0\x9F\x8E\xA5");
    }
    // The camera's settings: lens, exposure, colour, sizes, viewfinder.
    add(menu.shortcuts, Companion::CameraSettings, "Camera settings", "\xF0\x9F\x8E\x9B");

    // Every file: open another, and the build / log window.
    add(menu.rows, Companion::OpenFile, "Open / replace resource");
    add(menu.rows, Companion::Diagnostics, "Diagnostics / build info\xE2\x80\xA6");
    // One browser for the archive the scene came from.
    if (root && (facts.assembled_pac_source || facts.stage_source)) {
        add(menu.rows, facts.assembled_pac_source ? Companion::BrowseAssembledPac : Companion::BrowseStagePac,
            "Browse .PAC files\xE2\x80\xA6");
    } else if (root && demo::is_demo(session) && session != nullptr && !session->children.empty()) {
        // A cutscene plays as its scene; its archive's files are one step away.
        add(menu.rows, Companion::BrowseCutscenePac, "Browse .PAC files\xE2\x80\xA6");
    }
    // Any effect of the banks loaded with the scene, played on its own.
    if (open && !effect_choices(session).empty()) {
        add(menu.rows, Companion::Effects, "Effects (play any)\xE2\x80\xA6");
    }
    if (has(widow::StateFlag::CanReencodeTextures)) {
        add(model_rows, Companion::TextureFormat, "Texture format (BC1\xE2\x80\xA6" "BC7)\xE2\x80\xA6");
    }
    if (root && facts.assembled_pac_source) add(model_rows, Companion::AddPac, "Add weapon / .PAC\xE2\x80\xA6");
    // An assembled PAC already owns its part list; re-composing it from
    // picked files would drop the archive's own MODs.
    if (root && has(widow::StateFlag::CanAddModelPart) && !facts.assembled_pac_source) {
        add(model_rows, Companion::AddModParts, "Add .MOD part(s)");
    }
    if (root && session_composite_part_count(session) > 1U) {
        add(model_rows, Companion::CompositeParts, "Composite parts / state\xE2\x80\xA6");
        add(model_rows, Companion::PlaceModPart, "Place MOD part on host joint\xE2\x80\xA6");
        add(model_rows, Companion::ResetModPlacement, "Reset MOD part placement\xE2\x80\xA6");
    }
    if (root && has(widow::StateFlag::TextureCompanionAttachable)) {
        add(model_rows, Companion::AttachPtx, "Attach .PTX texture");
    }
    if (root && has(widow::StateFlag::CanStageCompanion)) {
        add(model_rows, Companion::StageMotion, "Add animation / motion\xE2\x80\xA6");
        add(model_rows, Companion::StageTexture, "Add texture asset (.TM2 / .DDS / \xE2\x80\xA6)");
        add(model_rows, Companion::StagePhysics, "Add physics resource\xE2\x80\xA6");
        add(model_rows, Companion::StageCloth, "Add cloth resource\xE2\x80\xA6");
        add(model_rows, Companion::StageOther, "Add other companion\xE2\x80\xA6");
    }
    // Stage Ops: what a full stage needs, loaded or missing; in ⋮ for a
    // stage opened as its scene only (the base room around a model is
    // built in Settings, base_room_menu).
    if (stage_scene) stage_ops(session, facts, menu, StageSlot::scene);
    // The markers show over a stage scene only (removing is the ✕ of its
    // Stage Ops row).
    if (stage_scene && has(widow::StateFlag::CanLoadStageConfig) && has(widow::StateFlag::StageConfigLoaded)) {
        add(stage_rows, Companion::StageConfigToggle,
            has(widow::StateFlag::StageMarkersShown) ? "Hide stage config markers" : "Show stage config markers");
    }
    return menu;
}

CompanionMenu base_room_menu(const ShellFacts& facts) {
    CompanionMenu menu;
    stage_ops(nullptr, facts, menu, StageSlot::base);
    return menu;
}

ActionOutcome run_companion_action(Session* session, int id, std::unique_ptr<Session>* opened) noexcept {
    if (static_cast<Companion>(id) != Companion::BrowseCutscenePac) return run_companion_action(session, id);
    ActionOutcome out;
    try {
        auto files = open_files_view(session);
        if (files == nullptr || opened == nullptr) {
            out.message = "This cutscene has no files to browse";
            return out;
        }
        *opened = std::move(files);
        out.ok = true;
        out.step = ShellStep::OpenSession;
        out.title = (session->source_name.empty() ? std::string{"Cutscene"} : session->source_name) +
                    " \xC2\xB7 files";
    } catch (...) {
        out = {};
    }
    return out;
}

ActionOutcome run_companion_action(Session* session, int id) noexcept {
    const auto picked = static_cast<Companion>(id);
    if (picked == Companion::CutsceneSound || picked == Companion::CutsceneMovie || picked == Companion::CutsceneSubtitles) {
        ActionOutcome out;
        out.ok = demo::is_demo(session);
        // The same picker: the picked file's bytes decide (load_cutscene_sound).
        if (out.ok) out.step = ShellStep::PickCutsceneSound;
        return out;
    }
    if (static_cast<Companion>(id) != Companion::CutsceneCamera) return run_companion_action(id);
    ActionOutcome out;
    out.ok = demo::set_director(session, true);
    if (out.ok) {
        // The free camera's eye goes; the cuts drive the view again.
        camera::reset();
        out.step = ShellStep::LeaveFly;
        out.message = "Cutscene camera: " + demo::status_line(session);
    }
    return out;
}

ActionOutcome load_cutscene_sound(Session* session, std::string_view name, const std::uint8_t* bytes,
                                  std::size_t size) noexcept {
    ActionOutcome out;
    try {
        const std::span<const std::uint8_t> file{bytes, bytes != nullptr ? size : 0U};
        out.ok = bytes != nullptr &&
                 (video::looks_like_asf(file)          ? demo::attach_movie(session, name, file, &out.message)
                  : messages::looks_like_table(file) ? demo::attach_subtitles(session, name, file, &out.message)
                                                     : demo::attach_audio(session, name, file, &out.message));
        if (bytes == nullptr) out.message = std::string{name} + " could not be read";
    } catch (...) {
        out.ok = false;
        out.message = "Sound could not be read";
    }
    return out;
}

ActionOutcome run_companion_action(int id) noexcept {
    switch (static_cast<Companion>(id)) {
    case Companion::StageConfigLoad: return run_stage_config_menu(1);
    case Companion::StageConfigToggle: return run_stage_config_menu(2);
    case Companion::StageConfigRemove: return run_stage_config_menu(3);
    case Companion::StageOpsSound: {
        ActionOutcome out;
        out.ok = true;
        out.message = "Stage sounds: the Reader does not read a stage's sound bank yet";
        return out;
    }
    default: {
        ActionOutcome none;
        none.ok = true;
        return none;
    }
    }
}

}  // namespace dmcresource::spider::actions
