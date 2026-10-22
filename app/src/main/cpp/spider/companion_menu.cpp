#include "dmcresource/spider/companion_menu.h"

#include <string>

#include "dmcresource/camera_control.h"
#include "dmcresource/demo_session.h"
#include "dmcresource/spider/black_widow.h"
#include "dmcresource/stage_markers.h"
#include "dmcresource/stage_room.h"

namespace dmcresource::spider::actions {
namespace {

namespace widow = dmcresource::spider::black_widow;

void add(std::vector<MenuItem>& items, Companion id, std::string label, std::string icon = {}) {
    items.push_back({static_cast<int>(id), std::move(label), std::move(icon)});
}

}  // namespace

CompanionMenu companion_menu(const Session* session, const ShellFacts& facts) {
    CompanionMenu menu;
    const auto state = black_widow_state(session);
    const auto has = [state](widow::StateFlag flag) { return widow::has_state(state, flag); };
    const bool open = session != nullptr;
    const bool root = open && facts.root_scene;

    add(menu.shortcuts, Companion::Settings, "Settings", "\xE2\x9A\x99");
    add(menu.shortcuts, Companion::Gestures, "Gestures", "\xE2\x9C\x8B");
    if (facts.room_loaded) {
        add(menu.shortcuts, Companion::RoomToggle, facts.room_shown ? "Hide the room" : "Show the room",
            "\xE2\x8C\x82");
        if (!facts.room_shown) menu.dimmed.push_back(static_cast<int>(Companion::RoomToggle));
        const auto room = stage_room::current();
        if (room != nullptr && room->spots.size() > 1U) {
            add(menu.shortcuts, Companion::NextRoomSpot, "Next floor spot", "\xE2\x87\x84");
        }
    }

    add(menu.rows, Companion::OpenFile, "Open / replace resource");
    if (has(widow::StateFlag::CanReencodeTextures)) add(menu.rows, Companion::TextureFormat, "Texture format (BC1\xE2\x80\xA6" "BC7)\xE2\x80\xA6");
    if (has(widow::StateFlag::CanSaveSource)) add(menu.rows, Companion::SaveFile, "Save file\xE2\x80\xA6");
    if (root && facts.assembled_pac_source) {
        add(menu.rows, Companion::AddPac, "Add weapon / .PAC\xE2\x80\xA6");
        add(menu.rows, Companion::BrowseAssembledPac, "Browse .PAC files\xE2\x80\xA6");
    }
    if (root && facts.stage_source) {
        add(menu.rows, Companion::BrowseStagePac, "Browse .PAC files\xE2\x80\xA6");
        add(menu.rows, Companion::StageEffects, "Stage effects (st*_effect.pac)\xE2\x80\xA6");
    }
    // An assembled PAC already owns its part list; re-composing it from
    // picked files would drop the archive's own MODs.
    if (root && has(widow::StateFlag::CanAddModelPart) && !facts.assembled_pac_source) {
        add(menu.rows, Companion::AddModParts, "Add .MOD part(s)");
    }
    if (root && session_composite_part_count(session) > 1U) {
        add(menu.rows, Companion::CompositeParts, "Composite parts / state\xE2\x80\xA6");
        add(menu.rows, Companion::PlaceModPart, "Place MOD part on host joint\xE2\x80\xA6");
        add(menu.rows, Companion::ResetModPlacement, "Reset MOD part placement\xE2\x80\xA6");
    }
    if (root && has(widow::StateFlag::TextureCompanionAttachable)) add(menu.rows, Companion::AttachPtx, "Attach .PTX texture");
    if (root && has(widow::StateFlag::CanStageCompanion)) {
        add(menu.rows, Companion::StageMotion, "Add animation / motion\xE2\x80\xA6");
        add(menu.rows, Companion::StageTexture, "Add texture asset (.TM2 / .DDS / \xE2\x80\xA6)");
        add(menu.rows, Companion::StagePhysics, "Add physics resource\xE2\x80\xA6");
        add(menu.rows, Companion::StageCloth, "Add cloth resource\xE2\x80\xA6");
        add(menu.rows, Companion::StageOther, "Add other companion\xE2\x80\xA6");
    }
    // The stage configuration over a stage: spawn points, zones, items.
    if (has(widow::StateFlag::CanLoadStageConfig)) {
        const bool loaded = has(widow::StateFlag::StageConfigLoaded);
        add(menu.rows, Companion::StageConfigLoad,
            loaded ? "Load another stage config (stNNNcfg.pac)\xE2\x80\xA6" : "Stage config (stNNNcfg.pac)\xE2\x80\xA6");
        if (loaded) {
            add(menu.rows, Companion::StageConfigToggle,
                has(widow::StateFlag::StageMarkersShown) ? "Hide stage config" : "Show stage config");
            add(menu.rows, Companion::StageConfigRemove, "Remove stage config");
        }
    }
    // A cutscene's sound lives in .ogg files beside the archive.
    if (demo::is_demo(session)) add(menu.rows, Companion::CutsceneSound, "\xF0\x9F\x94\x8A Cutscene sound (.ogg)\xE2\x80\xA6");
    // A cutscene whose camera the user took: back to following its cuts.
    if (demo::is_demo(session) && !demo::director(session)) {
        add(menu.rows, Companion::CutsceneCamera, "\xF0\x9F\x8E\xAC Cutscene camera (follow the cuts)");
    }
    add(menu.rows, Companion::Diagnostics, "Diagnostics / build info\xE2\x80\xA6");
    return menu;
}

ActionOutcome run_companion_action(Session* session, int id) noexcept {
    if (static_cast<Companion>(id) == Companion::CutsceneSound) {
        ActionOutcome out;
        out.ok = demo::is_demo(session);
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
        out.ok = bytes != nullptr && demo::attach_audio(session, name, std::span<const std::uint8_t>{bytes, size},
                                                         &out.message);
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
    default: {
        ActionOutcome none;
        none.ok = true;
        return none;
    }
    }
}

}  // namespace dmcresource::spider::actions
