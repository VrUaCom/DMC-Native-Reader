#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dmcresource/resource_session.h"

// Spider session actions for the stage configuration over a stage, the view's
// long press, sounds and PNG export. The core decides every step and writes
// every line the user reads; a platform shell forwards the event, carries out
// the one platform step it is told to (open the file picker, play PCM, show
// a card) and shows the typed result. It never reads meaning out of a string.
namespace dmcresource::spider::actions {

// The one thing left for the shell after an action.
enum class ShellStep : std::uint8_t {
    None = 0,             // show `message` if any; nothing else
    PickStageConfig = 1,  // open the platform file picker for a stage config
    ShowMarkerCard = 2,   // show the card of `marker` (marker_card)
    StartScrub = 3,       // start scrubbing the motion under the finger
    OpenSession = 4,      // navigate to the session handed back
};

struct ActionOutcome final {
    bool ok{};
    ShellStep step{ShellStep::None};
    std::string message;  // written by the core; empty when there is nothing to say
    int marker{-1};
    std::string title;    // OpenSession: the title of the opened session
};

// A menu or a card's buttons: stable ids the shell sends back, labels it shows.
struct MenuItem final {
    int id{};
    std::string label;
};
struct Menu final {
    std::string title;
    std::string message;
    std::vector<MenuItem> items;
};

// ---- stage configuration -------------------------------------------------

// Reads a stNNNcfg.pac or one member and makes it the configuration drawn
// over the stage. Crusader plan: read members -> place markers -> publish.
[[nodiscard]] ActionOutcome load_stage_config(std::string_view name, const std::uint8_t* bytes,
                                              std::size_t size) noexcept;
// The stage-config tool tapped: with no configuration, pick one; with one,
// show or hide its markers.
[[nodiscard]] ActionOutcome stage_config_tool() noexcept;
// The tool's long press: what the configuration holds and what can be done.
[[nodiscard]] Menu stage_config_menu();
[[nodiscard]] ActionOutcome run_stage_config_menu(int id) noexcept;

// ---- the view's long press -----------------------------------------------

// A held stage-config marker comes first (it is selected and its card is
// shown), then the joint under the finger, then motion scrubbing. `joints`
// and `scrub` are the user's gesture settings.
[[nodiscard]] ActionOutcome hold_view(const Session* session, int width, int height, float yaw, float pitch,
                                      float zoom, std::uint32_t render_flags, const ViewControls& controls,
                                      float x, float y, bool joints, bool scrub) noexcept;

// The card of a marker of the current configuration: title, rows and buttons.
struct MarkerCard final {
    std::string title;
    std::vector<std::pair<std::string, std::string>> rows;
    std::vector<MenuItem> actions;
};
[[nodiscard]] MarkerCard marker_card(int marker);
// A card button. "Open file" hands the marker's file back through `opened`
// with ShellStep::OpenSession; "Close" (and the card going away) clears the
// selection.
[[nodiscard]] ActionOutcome run_marker_card_action(int marker, int id, std::unique_ptr<Session>* opened) noexcept;

// ---- sounds ----------------------------------------------------------------

// The sounds of a session to choose from (empty items when it has none).
[[nodiscard]] Menu sound_menu(const Session* session);
// One sound as 16-bit mono PCM for the platform player, and the line to show.
struct SoundPcm final {
    std::uint32_t sample_rate{};
    std::vector<std::int16_t> samples;
    std::string message;
};
[[nodiscard]] SoundPcm sound_pcm(const Session* session, int index) noexcept;

// ---- PNG export --------------------------------------------------------------

// The picture a session exports and its size: an opened UV map at its
// texture's resolution and proportions, anything else its active preview.
[[nodiscard]] std::pair<int, int> export_image_size(const Session* session) noexcept;
[[nodiscard]] RgbaImage render_export_image(const Session* session) noexcept;

}  // namespace dmcresource::spider::actions
