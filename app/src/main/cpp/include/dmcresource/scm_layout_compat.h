#pragma once

#include <cstddef>

#include "dmc_rengine/formats/scm.hpp"

// SCM files whose streams are placed in another order than the game's own
// writer uses. The canonical parser (vendored DMC Rengine, never edited)
// reads every stream through its record offset and then rejects the file
// for placement alone: its mesh tables and streams are laid out mesh by
// mesh (positions, normals, UVs, colours of one mesh, then the next) where
// the game's files keep each stream kind together. The game itself follows
// the offsets, so such a file is a readable model. The viewer shows it, and
// the session carries a non-canonical note; any other error still rejects.
namespace dmcresource::scm_layout_compat {

inline constexpr const char* kModuleName = "native.scm-community-layout";

// True when every error of `parsed` is a placement one (mesh table or mesh
// streams), every mesh's streams were read in full, and every stream lies
// inside the file without overlapping another.
[[nodiscard]] bool readable_despite_placement(const dmc::rengine::formats::scm::ParseResult& parsed,
                                              std::size_t file_size) noexcept;

}  // namespace dmcresource::scm_layout_compat
