#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "dmcresource/resource_session.h"

namespace dmcresource::spider::actions {

// Product action boundary. JNI/platform shells call these operations instead of
// directly orchestrating session composition or texture attachment. Crusader
// owns execution; resource_session remains the low-level data/session layer.
//
// v33 Android compatibility: two-argument composition treats part 0 as the
// primary host. The Add-MOD flow guarantees that the already-open base session
// occupies part 0. Call the explicit overload with -1 when the caller has no
// authoritative primary host and wants source-coordinate composition only.
[[nodiscard]] std::unique_ptr<Session> compose_mod_sessions(
    const std::vector<const Session*>& parts,
    const std::vector<std::string>& names) noexcept;

[[nodiscard]] std::unique_ptr<Session> compose_mod_sessions(
    const std::vector<const Session*>& parts,
    const std::vector<std::string>& names,
    int primary_host_index) noexcept;

[[nodiscard]] bool attach_ptx(
    Session* session,
    std::string_view name,
    const std::uint8_t* bytes,
    std::size_t size) noexcept;

[[nodiscard]] bool attach_ptx_to_part(
    Session* session,
    int part_index,
    std::string_view name,
    const std::uint8_t* bytes,
    std::size_t size) noexcept;

// Texture format change. Re-encodes every texture of `target` (DDS, PTX,
// single gfxTexture, or the texture slots of a PAC) to `format_name`
// ("bc1".."bc7", "dxt1", "dxt5", "bc4s", "bc5s", "bc6h", "bc6h_sf16"); the
// game's mips are kept. A session opened from a PAC slot carries its
// container (Session::container_source): the whole PAC is rebuilt with only
// that slot replaced, so the result is the file the game loads. Runs as a
// Crusader plan (select source -> re-encode -> open result -> verify).
// Returns the opened result, marked authored (savable through its
// source_bytes), or null with `detail` naming the failed step and reason.
[[nodiscard]] std::unique_ptr<Session> reencode_textures(
    const Session* target,
    std::string_view format_name,
    bool force_dx10,
    std::string* detail) noexcept;

// Format names the action accepts, in menu order, with a short label each
// ("bc7", "BC7 (DX10): best quality, RGBA").
struct TextureFormatChoice final {
    std::string name;
    std::string label;
};
[[nodiscard]] std::vector<TextureFormatChoice> texture_format_choices();

// How a file the user picked is opened, decided by the core. A PAC holding
// MODs opens as its assembled character or scene, a stage archive as its
// assembled room, anything else as itself. The shell only reads the bytes,
// shows `title` and keeps the source for later reopens.
enum class OpenedAs : std::uint8_t {
    Rejected = 0,  // no module accepted the file; `title` says why
    Plain = 1,
    AssembledPac = 2,
    Stage = 3,
};

struct OpenedResource final {
    std::unique_ptr<Session> session;
    OpenedAs as = OpenedAs::Rejected;
    std::string title;
    // A stage's effect bank (stNNN*effect*) opened on its own is also the
    // stage effects of the base room.
    bool room_effects = false;
};

[[nodiscard]] OpenedResource open_resource(std::string_view name, const std::uint8_t* bytes,
                                           std::size_t size) noexcept;

}  // namespace dmcresource::spider::actions
