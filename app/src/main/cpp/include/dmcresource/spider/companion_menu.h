#pragma once

#include <vector>

#include "dmcresource/resource_session.h"
#include "dmcresource/spider/stage_actions.h"

// The ⋮ menu, decided by the core: which shortcuts and rows the opened
// session offers and what they are called. The shell passes the few facts
// only it holds (navigation, retained source documents, the stored room
// preference), shows the items, and sends the picked id back: to the core
// (run_companion_action) or to the platform step the id names.
namespace dmcresource::spider::actions {

// Ids are stable: the shell switches on them.
enum class Companion : int {
    // shortcuts
    Settings = 100,
    Gestures = 101,
    RoomToggle = 102,
    NextRoomSpot = 103,
    // rows
    OpenFile = 1,
    TextureFormat = 2,
    SaveFile = 3,
    AddPac = 4,
    BrowseAssembledPac = 5,
    BrowseStagePac = 6,
    StageEffects = 7,
    AddModParts = 8,
    CompositeParts = 9,
    PlaceModPart = 10,
    ResetModPlacement = 11,
    AttachPtx = 12,
    StageMotion = 13,
    StageTexture = 14,
    StagePhysics = 15,
    StageCloth = 16,
    StageOther = 17,
    StageConfigLoad = 18,
    StageConfigToggle = 19,
    StageConfigRemove = 20,
    Diagnostics = 21,
    CutsceneCamera = 22,
    CutsceneSound = 23,
    CutsceneMovie = 24,
    CutsceneSubtitles = 25,
};

// What only the shell knows.
struct ShellFacts final {
    bool root_scene{};             // nothing navigated into
    bool assembled_pac_source{};   // the assembled PAC's document is retained
    bool stage_source{};           // the stage archive's document is retained
    bool room_loaded{};
    bool room_shown{};
};

struct CompanionMenu final {
    std::vector<MenuItem> shortcuts;
    std::vector<MenuItem> rows;
    // A shortcut drawn dimmed (the room while hidden): ids listed here.
    std::vector<int> dimmed;
};

[[nodiscard]] CompanionMenu companion_menu(const Session* session, const ShellFacts& facts);

// The items the core carries out itself (the stage configuration ones);
// every other id answers with ShellStep::None and an empty message, and the
// shell runs its platform step for it.
[[nodiscard]] ActionOutcome run_companion_action(int id) noexcept;
// The same with the opened session, for the items that act on it (the
// cutscene camera).
[[nodiscard]] ActionOutcome run_companion_action(Session* session, int id) noexcept;

// The picked file of a cutscene: a sound (Ogg Vorbis) plays along the
// timeline (demo_session.h attach_audio), a movie (.wmv) becomes its movie
// part (attach_movie), a message table (`*_msg_<lang>.txt`) its subtitles
// (attach_subtitles); the line says from which frame or how many.
[[nodiscard]] ActionOutcome load_cutscene_sound(Session* session, std::string_view name, const std::uint8_t* bytes,
                                                std::size_t size) noexcept;

}  // namespace dmcresource::spider::actions
